/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Windows Test Agent
 *
 * RPC calls of Winsock which POSIX lacks: event objects, overlapped I/O
 * and the extension functions of Microsoft.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER     "RPC WSA"

#include "te_config.h"

#include "rpc_server.h"

#include <pthread.h>

#include "te_alloc.h"
#include "te_defs.h"
#include "te_rpc_wsa.h"
#include "win32_internal.h"

/** The most event objects WSAWaitForMultipleEvents() takes */
#define MULTIPLE_EVENTS_MAX     WSA_MAXIMUM_WAIT_EVENTS

/** Event object of an RPC call */
#define IN_HEVENT       ((WSAEVENT)rcf_pch_mem_get(in->hevent))

/** Overlapped object of an RPC call */
#define IN_OVERLAPPED   ((rpc_overlapped *)rcf_pch_mem_get(in->overlapped))

/** Completion routine of an RPC call */
#define IN_CALLBACK \
    ((LPWSAOVERLAPPED_COMPLETION_ROUTINE)completion_callback_addr( \
                                             in->callback))

/**
 * Overlapped object with the buffers of the operation in progress.
 * Windows writes into the buffers until the operation completes, so
 * they live with the object, not with the RPC call.
 */
typedef struct rpc_overlapped {
    WSAOVERLAPPED   overlapped; /**< The object itself, must be first */
    WSABUF         *buffers;    /**< Buffers of the operation */
    unsigned int   *sizes;      /**< Allocated sizes of the buffers */
    unsigned int    bufnum;     /**< Number of the buffers */
    uint32_t        cookie1;    /**< Value of the test */
    uint32_t        cookie2;    /**< Value of the test */
} rpc_overlapped;

/**
 * Set the error of an RPC call from the last Winsock call.
 *
 * @param _failed   whether the call failed
 *
 * The caller has @c wsa_err for the error of Winsock.
 */
#define WSA_SET_ERRNO(_failed) \
    do {                                                            \
        if (_failed)                                                \
        {                                                           \
            wsa_err = WSAGetLastError();                            \
            errno = te_win32_wsa2errno(wsa_err);                    \
        }                                                           \
    } while (0)

/**
 * Report the errors of overlapped I/O which have no errno: an operation
 * in progress and an incomplete one.
 */
#define WSA_REPORT_PENDING \
    do {                                                            \
        if (wsa_err == WSA_IO_PENDING ||                            \
            wsa_err == WSA_IO_INCOMPLETE)                           \
        {                                                           \
            out->common._errno = (wsa_err == WSA_IO_PENDING) ?      \
                                 RPC_E_IO_PENDING :                 \
                                 RPC_E_IO_INCOMPLETE;               \
            out->common.errno_changed = true;                       \
        }                                                           \
    } while (0)

/** Free the buffers of an overlapped object */
static void
overlapped_free_buffers(rpc_overlapped *ovl)
{
    unsigned int i;

    if (ovl == NULL || ovl->buffers == NULL)
        return;

    for (i = 0; i < ovl->bufnum; i++)
        free(ovl->buffers[i].buf);
    free(ovl->buffers);
    free(ovl->sizes);
    ovl->buffers = NULL;
    ovl->sizes = NULL;
    ovl->bufnum = 0;
}

/**
 * Copy the data of RPC vectors to the buffers of an overlapped object.
 *
 * @param ovl       overlapped object
 * @param iov       vectors
 * @param iovcnt    number of the vectors
 */
static void
iovec2overlapped(rpc_overlapped *ovl, const struct tarpc_iovec *iov,
                 unsigned int iovcnt)
{
    unsigned int i;

    overlapped_free_buffers(ovl);
    if (iovcnt == 0)
        return;

    ovl->buffers = TE_ALLOC(iovcnt * sizeof(*ovl->buffers));
    ovl->sizes = TE_ALLOC(iovcnt * sizeof(*ovl->sizes));
    for (i = 0; i < iovcnt; i++)
    {
        ovl->sizes[i] = MAX(iov[i].iov_len, iov[i].iov_base.iov_base_len);
        ovl->buffers[i].len = iov[i].iov_len;
        ovl->buffers[i].buf = TE_ALLOC(ovl->sizes[i] + 1);
        if (iov[i].iov_base.iov_base_val != NULL)
        {
            memcpy(ovl->buffers[i].buf, iov[i].iov_base.iov_base_val,
                   MIN(iov[i].iov_len, iov[i].iov_base.iov_base_len));
        }
    }
    ovl->bufnum = iovcnt;
}

/**
 * Move the buffers of an overlapped object to RPC vectors.
 *
 * @param ovl       overlapped object
 * @param len       location for the number of the vectors
 * @param val       location for the vectors
 */
static void
overlapped2iovec(rpc_overlapped *ovl, u_int *len, struct tarpc_iovec **val)
{
    unsigned int i;

    *len = 0;
    *val = NULL;
    if (ovl->bufnum == 0)
        return;

    *val = TE_ALLOC(ovl->bufnum * sizeof(**val));
    for (i = 0; i < ovl->bufnum; i++)
    {
        (*val)[i].iov_base.iov_base_val = (uint8_t *)ovl->buffers[i].buf;
        (*val)[i].iov_base.iov_base_len = ovl->sizes[i];
        (*val)[i].iov_len = ovl->buffers[i].len;
    }
    *len = ovl->bufnum;
    free(ovl->buffers);
    free(ovl->sizes);
    ovl->buffers = NULL;
    ovl->sizes = NULL;
    ovl->bufnum = 0;
}

/*-------------- Completion routines ------------------------------*/

static pthread_mutex_t completion_lock = PTHREAD_MUTEX_INITIALIZER;
static int completion_called;
static int completion_error;
static int completion_bytes;
static tarpc_overlapped completion_overlapped;

/** Completion routine which records its call for completion_callback() */
static void CALLBACK
default_completion_callback(DWORD error, DWORD bytes,
                            LPWSAOVERLAPPED overlapped, DWORD flags)
{
    UNUSED(flags);

    pthread_mutex_lock(&completion_lock);
    completion_called++;
    completion_error = errno_h2rpc(te_win32_wsa2errno(error));
    completion_bytes = bytes;
    completion_overlapped = rcf_pch_mem_get_id(overlapped);
    pthread_mutex_unlock(&completion_lock);
}

/** Completion routine which does nothing */
static void CALLBACK
empty_completion_callback(DWORD error, DWORD bytes,
                          LPWSAOVERLAPPED overlapped, DWORD flags)
{
    UNUSED(error);
    UNUSED(bytes);
    UNUSED(overlapped);
    UNUSED(flags);
}

/**
 * Find a completion routine by its name.
 *
 * @param name      name, empty for no routine
 *
 * @return The routine or @c NULL.
 */
static void *
completion_callback_addr(const char *name)
{
    if (name == NULL || *name == '\0')
        return NULL;
    if (strcmp(name, "default_completion_callback") == 0)
        return default_completion_callback;
    if (strcmp(name, "empty_completion_callback") == 0)
        return empty_completion_callback;

    ERROR("Unknown completion routine '%s'", name);
    return NULL;
}

TARPC_FUNC_STANDALONE(completion_callback, {},
{
    UNUSED(in);

    pthread_mutex_lock(&completion_lock);
    out->called = completion_called;
    out->error = completion_error;
    out->bytes = completion_bytes;
    out->overlapped = completion_overlapped;
    completion_called = 0;
    completion_bytes = 0;
    pthread_mutex_unlock(&completion_lock);
}
)

/*-------------- WSAStartup() and WSACleanup() --------------------*/

TARPC_FUNC_STANDALONE(wsa_startup, {},
{
    WSADATA data;

    UNUSED(in);
    MAKE_CALL(out->retval = WSAStartup(MAKEWORD(2, 2), &data));
}
)

TARPC_FUNC_STANDALONE(wsa_cleanup, {},
{
    UNUSED(in);
    MAKE_CALL(out->retval = WSACleanup());
}
)

/*-------------- WSASocket() --------------------------------------*/

TARPC_FUNC_STANDALONE(wsa_socket, {},
{
    SOCKET  s;
    int     wsa_err = 0;

    if (in->info.info_len != 0 &&
        in->info.info_len < sizeof(WSAPROTOCOL_INFOA))
    {
        ERROR("Protocol information is too short");
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        out->fd = -1;
        return;
    }

    /* The protocol information of a duplicated socket has all */
    if (in->info.info_len != 0)
    {
        MAKE_CALL(s = WSASocketA(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                                 FROM_PROTOCOL_INFO,
                                 (LPWSAPROTOCOL_INFOA)in->info.info_val,
                                 0, open_sock_flags_rpc2h(in->flags));
                  WSA_SET_ERRNO(s == INVALID_SOCKET));
        UNUSED(wsa_err);
        out->fd = (s == INVALID_SOCKET) ? -1 : (int)s;
        return;
    }

    MAKE_CALL(s = WSASocketA(domain_rpc2h(in->domain),
                             socktype_rpc2h(in->type),
                             proto_rpc2h(in->proto), NULL,
                             0, open_sock_flags_rpc2h(in->flags));
              WSA_SET_ERRNO(s == INVALID_SOCKET));
    UNUSED(wsa_err);
    out->fd = (s == INVALID_SOCKET) ? -1 : (int)s;
}
)

/*-------------- WSADuplicateSocket() -----------------------------*/

TARPC_FUNC_STANDALONE(duplicate_socket, {},
{
    WSAPROTOCOL_INFOA   info;
    int                 wsa_err = 0;

    MAKE_CALL(out->retval = WSADuplicateSocketA((SOCKET)in->s, in->pid,
                                                &info);
              WSA_SET_ERRNO(out->retval != 0));
    UNUSED(wsa_err);
    if (out->retval == 0)
    {
        out->info.info_val = TE_ALLOC(sizeof(info));
        memcpy(out->info.info_val, &info, sizeof(info));
        out->info.info_len = sizeof(info);
    }
}
)

/*-------------- GetCurrentProcessId() ----------------------------*/

TARPC_FUNC_STANDALONE(get_current_process_id, {},
{
    UNUSED(in);
    out->retval = GetCurrentProcessId();
}
)

/*-------------- System information -------------------------------*/

TARPC_FUNC_STANDALONE(get_sys_info, {},
{
    MEMORYSTATUSEX  ms;
    SYSTEM_INFO     si;

    UNUSED(in);

    memset(&ms, 0, sizeof(ms));
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        out->ram_size = ms.ullTotalPhys;

    GetNativeSystemInfo(&si);
    out->page_size = si.dwPageSize;
    out->number_of_processors = si.dwNumberOfProcessors;
}
)

/*-------------- Event objects ------------------------------------*/

TARPC_FUNC_STANDALONE(create_event, {},
{
    WSAEVENT ev;

    UNUSED(in);
    MAKE_CALL(ev = WSACreateEvent();
              if (ev == WSA_INVALID_EVENT)
                  errno = te_win32_wsa2errno(WSAGetLastError()));
    out->retval = (ev == WSA_INVALID_EVENT) ? RPC_NULL :
                                              rcf_pch_mem_alloc(ev);
}
)

TARPC_FUNC_STANDALONE(close_event, {},
{
    MAKE_CALL(out->retval = WSACloseEvent(IN_HEVENT));
    rcf_pch_mem_free(in->hevent);
}
)

TARPC_FUNC_STANDALONE(reset_event, {},
{
    MAKE_CALL(out->retval = WSAResetEvent(IN_HEVENT));
}
)

TARPC_FUNC_STANDALONE(set_event, {},
{
    MAKE_CALL(out->retval = WSASetEvent(IN_HEVENT));
}
)

/*-------------- WSAEventSelect() ---------------------------------*/

TARPC_FUNC_STANDALONE(event_select, {},
{
    int  wsa_err = 0;

    MAKE_CALL(out->retval = WSAEventSelect((SOCKET)in->fd, IN_HEVENT,
                                           network_event_rpc2h(in->event));
              WSA_SET_ERRNO(out->retval != 0));
    UNUSED(wsa_err);
}
)

/*-------------- WSAEnumNetworkEvents() ---------------------------*/

TARPC_FUNC_STANDALONE(enum_network_events,
{
    COPY_ARG(events);
},
{
    WSANETWORKEVENTS    ev;
    unsigned int        i;
    int                 wsa_err = 0;

    memset(&ev, 0, sizeof(ev));
    MAKE_CALL(out->retval = WSAEnumNetworkEvents((SOCKET)in->fd,
                                                 IN_HEVENT, &ev);
              WSA_SET_ERRNO(out->retval != 0));
    UNUSED(wsa_err);

    if (out->retval == 0 && out->events.events_len > 0)
    {
        out->events.events_val[0].network_events =
            network_event_h2rpc(ev.lNetworkEvents);
        for (i = 0; i < TE_ARRAY_LEN(ev.iErrorCode) &&
                    i < TE_ARRAY_LEN(out->events.events_val[0].error_code);
             i++)
        {
            out->events.events_val[0].error_code[i] =
                ev.iErrorCode[i] == 0 ? 0 :
                errno_h2rpc(te_win32_wsa2errno(ev.iErrorCode[i]));
        }
    }
}
)

/*-------------- WSAWaitForMultipleEvents() -----------------------*/

TARPC_FUNC_STANDALONE(wait_for_multiple_events, {},
{
    WSAEVENT    events[MULTIPLE_EVENTS_MAX];
    DWORD       rc;
    unsigned int i;

    if (in->events.events_len > MULTIPLE_EVENTS_MAX)
    {
        ERROR("Too many event objects to wait for");
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        out->retval = TARPC_WSA_WAIT_FAILED;
        return;
    }

    for (i = 0; i < in->events.events_len; i++)
        events[i] = rcf_pch_mem_get(in->events.events_val[i]);

    MAKE_CALL(rc = WSAWaitForMultipleEvents(in->events.events_len, events,
                                            in->wait_all, in->timeout,
                                            in->alertable);
              if (rc == WSA_WAIT_FAILED)
                  errno = te_win32_wsa2errno(WSAGetLastError()));

    if (rc == WSA_WAIT_FAILED)
        out->retval = TARPC_WSA_WAIT_FAILED;
    else if (rc == WSA_WAIT_IO_COMPLETION)
        out->retval = TARPC_WAIT_IO_COMPLETION;
    else if (rc == WSA_WAIT_TIMEOUT)
        out->retval = TARPC_WSA_WAIT_TIMEOUT;
    else
        out->retval = TARPC_WSA_WAIT_EVENT_0 + (rc - WSA_WAIT_EVENT_0);
}
)

/*-------------- Overlapped objects -------------------------------*/

TARPC_FUNC_STANDALONE(create_overlapped, {},
{
    rpc_overlapped *ovl = TE_ALLOC(sizeof(*ovl));

    ovl->overlapped.hEvent = IN_HEVENT;
    ovl->overlapped.Offset = in->offset;
    ovl->overlapped.OffsetHigh = in->offset_high;
    ovl->cookie1 = in->cookie1;
    ovl->cookie2 = in->cookie2;
    out->retval = rcf_pch_mem_alloc(ovl);
}
)

TARPC_FUNC_STANDALONE(delete_overlapped, {},
{
    rpc_overlapped *ovl = IN_OVERLAPPED;

    UNUSED(out);
    overlapped_free_buffers(ovl);
    free(ovl);
    rcf_pch_mem_free(in->overlapped);
}
)

TARPC_FUNC_STANDALONE(has_overlapped_io_completed, {},
{
    rpc_overlapped *ovl = IN_OVERLAPPED;

    if (ovl == NULL)
    {
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        return;
    }
    out->retval = HasOverlappedIoCompleted(&ovl->overlapped);
}
)

TARPC_FUNC_STANDALONE(cancel_io, {},
{
    MAKE_CALL(out->retval = CancelIo((HANDLE)(intptr_t)in->fd);
              if (!out->retval)
                  errno = te_win32_err2errno(GetLastError()));
}
)

TARPC_FUNC_STANDALONE(wsa_get_overlapped_result, {},
{
    rpc_overlapped *ovl = IN_OVERLAPPED;
    DWORD           bytes = 0;
    DWORD           flags = 0;
    int             wsa_err = 0;

    if (ovl == NULL)
    {
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        return;
    }

    MAKE_CALL(out->retval = WSAGetOverlappedResult((SOCKET)in->s,
                                                   &ovl->overlapped,
                                                   &bytes, in->wait,
                                                   &flags);
              WSA_SET_ERRNO(!out->retval));
    WSA_REPORT_PENDING;

    if (in->bytes.bytes_len > 0)
    {
        out->bytes.bytes_val = TE_ALLOC(sizeof(*out->bytes.bytes_val));
        out->bytes.bytes_val[0] = bytes;
        out->bytes.bytes_len = 1;
    }
    if (in->flags.flags_len > 0)
    {
        out->flags.flags_val = TE_ALLOC(sizeof(*out->flags.flags_val));
        out->flags.flags_val[0] = send_recv_flags_h2rpc(flags);
        out->flags.flags_len = 1;
    }
    if (out->retval && in->get_data)
        overlapped2iovec(ovl, &out->vector.vector_len,
                         &out->vector.vector_val);
}
)

/*-------------- WSASend() and WSASendTo() ------------------------*/

/**
 * Prepare an overlapped object for a call.
 *
 * @param _ovl      location for the object
 * @param _tmp      object of the call without overlapped I/O
 */
#define OVERLAPPED_PREPARE(_ovl, _tmp) \
    do {                                                            \
        _ovl = IN_OVERLAPPED;                                       \
        if (_ovl == NULL)                                           \
        {                                                           \
            memset(&_tmp, 0, sizeof(_tmp));                         \
            _ovl = &_tmp;                                           \
        }                                                           \
        iovec2overlapped(_ovl, in->vector.vector_val,               \
                         in->vector.vector_len);                    \
    } while (0)

/** Number of the buffers Winsock takes */
#define OVERLAPPED_BUFNUM(_ovl) MIN((_ovl)->bufnum, in->count)

/** Overlapped object to pass to Winsock */
#define OVERLAPPED_ARG(_ovl) \
    (in->overlapped == RPC_NULL ? NULL : &(_ovl)->overlapped)

/** Number of transferred bytes to report */
#define BYTES_OUT(_name, _bytes) \
    do {                                                            \
        if (in->_name._name##_len > 0)                              \
        {                                                           \
            out->_name._name##_val =                                \
                TE_ALLOC(sizeof(*out->_name._name##_val));          \
            out->_name._name##_val[0] = (_bytes);                   \
            out->_name._name##_len = 1;                             \
        }                                                           \
    } while (0)

TARPC_FUNC_STANDALONE(wsa_send, {},
{
    rpc_overlapped *ovl;
    rpc_overlapped  tmp;
    DWORD           bytes = 0;
    int             wsa_err = 0;

    OVERLAPPED_PREPARE(ovl, tmp);

    MAKE_CALL(out->retval = WSASend((SOCKET)in->s, ovl->buffers,
                                    OVERLAPPED_BUFNUM(ovl),
                                    in->bytes_sent.bytes_sent_len == 0 ?
                                        NULL : &bytes,
                                    send_recv_flags_rpc2h(in->flags),
                                    OVERLAPPED_ARG(ovl), IN_CALLBACK);
              WSA_SET_ERRNO(out->retval != 0));
    WSA_REPORT_PENDING;
    BYTES_OUT(bytes_sent, bytes);

    if (wsa_err != WSA_IO_PENDING)
        overlapped_free_buffers(ovl);
}
)

TARPC_FUNC_STANDALONE(wsa_send_to, {},
{
    rpc_overlapped *ovl;
    rpc_overlapped  tmp;
    DWORD           bytes = 0;
    int             wsa_err = 0;

    PREPARE_ADDR(to, in->to, 0);
    OVERLAPPED_PREPARE(ovl, tmp);

    MAKE_CALL(out->retval = WSASendTo((SOCKET)in->s, ovl->buffers,
                                      OVERLAPPED_BUFNUM(ovl),
                                      in->bytes_sent.bytes_sent_len == 0 ?
                                          NULL : &bytes,
                                      send_recv_flags_rpc2h(in->flags),
                                      to, tolen,
                                      OVERLAPPED_ARG(ovl), IN_CALLBACK);
              WSA_SET_ERRNO(out->retval != 0));
    WSA_REPORT_PENDING;
    BYTES_OUT(bytes_sent, bytes);

    if (wsa_err != WSA_IO_PENDING)
        overlapped_free_buffers(ovl);
}
)

/*-------------- WSARecv() and WSARecvFrom() ----------------------*/

/**
 * Finish a receiving call: return the data it received at once and
 * keep the buffers of an operation in progress.
 */
#define RECV_FINISH(_ovl) \
    do {                                                            \
        if (out->retval == 0 || wsa_err == WSAEMSGSIZE)             \
        {                                                           \
            overlapped2iovec(_ovl, &out->vector.vector_len,         \
                             &out->vector.vector_val);              \
        }                                                           \
        else if (wsa_err != WSA_IO_PENDING)                         \
        {                                                           \
            overlapped_free_buffers(_ovl);                          \
        }                                                           \
    } while (0)

TARPC_FUNC_STANDALONE(wsa_recv, {},
{
    rpc_overlapped *ovl;
    rpc_overlapped  tmp;
    DWORD           bytes = 0;
    DWORD           flags = 0;
    int             wsa_err = 0;

    OVERLAPPED_PREPARE(ovl, tmp);
    if (in->flags.flags_len > 0)
        flags = send_recv_flags_rpc2h(in->flags.flags_val[0]);

    errno = 0;
    MAKE_CALL(out->retval = WSARecv((SOCKET)in->s, ovl->buffers,
                                    OVERLAPPED_BUFNUM(ovl),
                                    in->bytes_received.bytes_received_len
                                        == 0 ? NULL : &bytes,
                                    in->flags.flags_len == 0 ? NULL :
                                        &flags,
                                    OVERLAPPED_ARG(ovl), IN_CALLBACK);
              WSA_SET_ERRNO(out->retval != 0));
    WSA_REPORT_PENDING;
    BYTES_OUT(bytes_received, bytes);
    BYTES_OUT(flags, send_recv_flags_h2rpc(flags));

    RECV_FINISH(ovl);
}
)

TARPC_FUNC_STANDALONE(wsa_recv_from,
{
    COPY_ARG(fromlen);
    COPY_ARG_ADDR(from);
},
{
    rpc_overlapped *ovl;
    rpc_overlapped  tmp;
    DWORD           bytes = 0;
    DWORD           flags = 0;
    int             wsa_err = 0;

    PREPARE_ADDR(from, out->from, out->fromlen.fromlen_len == 0 ? 0 :
                                      *out->fromlen.fromlen_val);
    OVERLAPPED_PREPARE(ovl, tmp);
    if (in->flags.flags_len > 0)
        flags = send_recv_flags_rpc2h(in->flags.flags_val[0]);

    errno = 0;
    MAKE_CALL(out->retval =
                  WSARecvFrom((SOCKET)in->s, ovl->buffers,
                              OVERLAPPED_BUFNUM(ovl),
                              in->bytes_received.bytes_received_len == 0 ?
                                  NULL : &bytes,
                              in->flags.flags_len == 0 ? NULL : &flags,
                              from,
                              out->fromlen.fromlen_len == 0 ? NULL :
                                  (LPINT)out->fromlen.fromlen_val,
                              OVERLAPPED_ARG(ovl), IN_CALLBACK);
              WSA_SET_ERRNO(out->retval != 0));
    WSA_REPORT_PENDING;
    BYTES_OUT(bytes_received, bytes);
    BYTES_OUT(flags, send_recv_flags_h2rpc(flags));

    if (out->retval == 0)
    {
        sockaddr_output_h2rpc(from, fromlen,
                              out->fromlen.fromlen_len == 0 ? 0 :
                                  *(out->fromlen.fromlen_val),
                              &(out->from));
    }
    RECV_FINISH(ovl);
}
)

/*-------------- Extension functions ------------------------------*/

/**
 * Get an extension function of Winsock.
 *
 * @param s         socket
 * @param guid      identifier of the function
 * @param func      location for the function
 * @param size      size of the location
 *
 * @return Status code.
 */
static te_errno
wsa_extension_get(SOCKET s, GUID guid, void *func, DWORD size)
{
    DWORD bytes;

    if (WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid,
                 sizeof(guid), func, size, &bytes, NULL, NULL) != 0)
    {
        errno = te_win32_wsa2errno(WSAGetLastError());
        return TE_OS_RC(TE_TA_WIN32, errno);
    }
    return 0;
}

TARPC_FUNC_STANDALONE(connect_ex, {},
{
    LPFN_CONNECTEX  func_connect_ex;
    rpc_overlapped *ovl = IN_OVERLAPPED;
    DWORD           sent = 0;
    int             wsa_err = 0;

    PREPARE_ADDR(serv_addr, in->addr, 0);

    if (wsa_extension_get((SOCKET)in->fd, (GUID)WSAID_CONNECTEX,
                          &func_connect_ex, sizeof(func_connect_ex)) != 0)
    {
        out->common._errno = RPC_ERRNO;
        out->retval = false;
        return;
    }

    MAKE_CALL(out->retval = func_connect_ex((SOCKET)in->fd, serv_addr,
                                            serv_addrlen,
                                            rcf_pch_mem_get(in->send_buf),
                                            in->buflen,
                                            in->len_sent.len_sent_len == 0 ?
                                                NULL : &sent,
                                            ovl == NULL ? NULL :
                                                &ovl->overlapped);
              WSA_SET_ERRNO(!out->retval));
    WSA_REPORT_PENDING;
    BYTES_OUT(len_sent, sent);
}
)

TARPC_FUNC_STANDALONE(disconnect_ex, {},
{
    LPFN_DISCONNECTEX   func_disconnect_ex;
    rpc_overlapped     *ovl = IN_OVERLAPPED;
    int                 wsa_err = 0;

    if (wsa_extension_get((SOCKET)in->fd, (GUID)WSAID_DISCONNECTEX,
                          &func_disconnect_ex,
                          sizeof(func_disconnect_ex)) != 0)
    {
        out->common._errno = RPC_ERRNO;
        out->retval = false;
        return;
    }

    MAKE_CALL(out->retval =
                  func_disconnect_ex((SOCKET)in->fd,
                                     ovl == NULL ? NULL : &ovl->overlapped,
                                     transmit_file_flags_rpc2h(in->flags),
                                     0);
              WSA_SET_ERRNO(!out->retval));
    WSA_REPORT_PENDING;
}
)

TARPC_FUNC_STANDALONE(accept_ex, {},
{
    LPFN_ACCEPTEX   func_accept_ex;
    rpc_overlapped *ovl = IN_OVERLAPPED;
    DWORD           count = 0;
    int             wsa_err = 0;

    if (wsa_extension_get((SOCKET)in->fd, (GUID)WSAID_ACCEPTEX,
                          &func_accept_ex, sizeof(func_accept_ex)) != 0)
    {
        out->common._errno = RPC_ERRNO;
        out->retval = false;
        return;
    }

    MAKE_CALL(out->retval = func_accept_ex((SOCKET)in->fd,
                                           (SOCKET)in->fd_a,
                                           rcf_pch_mem_get(in->out_buf),
                                           in->buflen, in->laddr_len,
                                           in->raddr_len,
                                           in->count.count_len == 0 ?
                                               NULL : &count,
                                           ovl == NULL ? NULL :
                                               &ovl->overlapped);
              WSA_SET_ERRNO(!out->retval));
    WSA_REPORT_PENDING;
    BYTES_OUT(count, count);
}
)

TARPC_FUNC_STANDALONE(get_accept_addr, {},
{
    LPFN_GETACCEPTEXSOCKADDRS   func_get_addrs;
    struct sockaddr            *la = NULL;
    struct sockaddr            *ra = NULL;
    INT                         la_len = 0;
    INT                         ra_len = 0;

    if (wsa_extension_get((SOCKET)in->fd,
                          (GUID)WSAID_GETACCEPTEXSOCKADDRS,
                          &func_get_addrs, sizeof(func_get_addrs)) != 0)
    {
        out->common._errno = RPC_ERRNO;
        return;
    }

    func_get_addrs(rcf_pch_mem_get(in->buf), in->buflen, in->laddr_len,
                   in->raddr_len, &la, &la_len, &ra, &ra_len);

    if (!in->l_sa_null)
        sockaddr_output_h2rpc(la, la_len, la_len, &out->laddr);
    if (!in->r_sa_null)
        sockaddr_output_h2rpc(ra, ra_len, ra_len, &out->raddr);
    BYTES_OUT(l_sa_len, la_len);
    BYTES_OUT(r_sa_len, ra_len);
}
)

/*-------------- WSAAddressToString() -----------------------------*/

TARPC_FUNC_STANDALONE(wsa_address_to_string, {},
{
    DWORD   len = 0;
    int     wsa_err = 0;

    PREPARE_ADDR(addr, in->addr, 0);

    /* TAPI passes the size of the buffer as the length of addrstr */
    len = in->addrstr.addrstr_len;
    out->addrstr.addrstr_val = TE_ALLOC(len + 1);

    MAKE_CALL(out->retval =
                  WSAAddressToStringA(addr, addrlen,
                                      in->info.info_len == 0 ? NULL :
                                          (LPWSAPROTOCOL_INFOA)
                                              in->info.info_val,
                                      (char *)out->addrstr.addrstr_val,
                                      &len);
              WSA_SET_ERRNO(out->retval != 0));
    UNUSED(wsa_err);

    out->addrstr.addrstr_len = in->addrstr.addrstr_len;
    BYTES_OUT(addrstr_len, len);
}
)

/*-------------- WSAStringToAddress() -----------------------------*/

TARPC_FUNC_STANDALONE(wsa_string_to_address, {},
{
    struct sockaddr_storage addr;
    INT                     len = sizeof(addr);
    int                     wsa_err = 0;

    if (in->addrlen.addrlen_len > 0)
        len = MIN((INT)in->addrlen.addrlen_val[0], len);

    memset(&addr, 0, sizeof(addr));
    MAKE_CALL(out->retval =
                  WSAStringToAddressA(in->addrstr.addrstr_val,
                                      domain_rpc2h(in->address_family),
                                      in->info.info_len == 0 ? NULL :
                                          (LPWSAPROTOCOL_INFOA)
                                              in->info.info_val,
                                      SA(&addr), &len);
              WSA_SET_ERRNO(out->retval != 0));
    UNUSED(wsa_err);

    if (out->retval == 0)
        sockaddr_output_h2rpc(SA(&addr), len, len, &out->addr);
    BYTES_OUT(addrlen, len);
}
)
