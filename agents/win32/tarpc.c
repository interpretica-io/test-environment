/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Windows Test Agent
 *
 * RPC calls of the Windows Test Agent. The calls of Winsock report
 * errors by WSAGetLastError() instead of errno, so the wrappers call
 * them directly and set errno before the RPC framework records it.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER     "RPC"

#include "te_config.h"

#include "rpc_server.h"

#include <pthread.h>

#include "te_alloc.h"
#include "te_defs.h"
#include "rcf_ch_api.h"

/**
 * Set errno from the error of the last Winsock call if it failed.
 *
 * @param rc        Value returned by the call
 *
 * @return @p rc
 */
static int
wsa_rc(int rc)
{
    if (rc == SOCKET_ERROR)
        errno = te_win32_wsa2errno(WSAGetLastError());
    return rc;
}

/** Winsock takes the size of an address as @c int */
#define WSA_LEN(_len) ((int *)(_len))

/*-------------- create_process() ---------------------------------*/

/*
 * Windows has no fork(): a new RPC server is a new instance of the agent,
 * which starts with nothing of its father but the environment.
 */
bool_t
_create_process_1_svc(tarpc_create_process_in *in,
                      tarpc_create_process_out *out,
                      struct svc_req *rqstp)
{
    void       *argv[1] = { in->name.name_val };
    pid_t       pid;
    te_errno    rc;

    UNUSED(rqstp);
    memset(out, 0, sizeof(*out));

    if ((in->flags & RCF_RPC_SERVER_GET_EXEC) == 0)
    {
        ERROR("Windows has no fork(), only forkexec RPC servers can be "
              "created");
        out->pid = -1;
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP);
        return true;
    }

    rc = rcf_ch_start_process(&pid, 0, "rcf_pch_rpc_server_argv",
                              true, 1, argv);
    if (rc != 0)
    {
        out->pid = -1;
        out->common._errno = rc;
        return true;
    }

    out->pid = pid;
    return true;
}

/*-------------- thread_create() -----------------------------*/
bool_t
_thread_create_1_svc(tarpc_thread_create_in *in,
                     tarpc_thread_create_out *out,
                     struct svc_req *rqstp)
{
    pthread_t tid;

    UNUSED(rqstp);

    TE_COMPILE_TIME_ASSERT(sizeof(pthread_t) <= sizeof(tarpc_pthread_t));

    memset(out, 0, sizeof(*out));

    out->retval = pthread_create(&tid, NULL, (void *)rcf_pch_rpc_server,
                                 strdup(in->name.name_val));

    if (out->retval == 0)
        out->tid = TARPC_PTHREAD_T_H2RPC(tid);

    return true;
}

/*-------------- thread_cancel() -----------------------------*/

/*
 * winpthreads cancels a thread only at pthread_testcancel(), so a thread
 * RPC server blocked in a call does not stop. RCF asks the thread to
 * finish first, and cancels it only when that fails.
 */
bool_t
_thread_cancel_1_svc(tarpc_thread_cancel_in *in,
                     tarpc_thread_cancel_out *out,
                     struct svc_req *rqstp)
{
    UNUSED(rqstp);
    memset(out, 0, sizeof(*out));

    out->retval = pthread_cancel(TARPC_PTHREAD_T(in->tid));

    return true;
}

/*-------------- thread_join() -----------------------------*/
bool_t
_thread_join_1_svc(tarpc_thread_join_in *in,
                   tarpc_thread_join_out *out,
                   struct svc_req *rqstp)
{
    UNUSED(rqstp);
    memset(out, 0, sizeof(*out));

    out->retval = pthread_join(TARPC_PTHREAD_T(in->tid), NULL);

    return true;
}

/*-------------- getpid() --------------------------------*/
TARPC_FUNC_STATIC(getpid, {}, { MAKE_CALL(out->retval = func_void()); })

/*-------------- pthread_self() --------------------------*/
TARPC_FUNC_STATIC(pthread_self, {},
{
    MAKE_CALL(out->retval = TARPC_PTHREAD_T_H2RPC(func()));
}
)

/*-------------- getenv() --------------------------------*/
TARPC_FUNC_STATIC(getenv, {},
{
    char *val;

    MAKE_CALL(val = func_ptr_ret_ptr(in->name));
    out->val = strdup(val == NULL ? "" : val);
    out->val_null = (val == NULL);
}
)

/*-------------- setenv() --------------------------------*/
TARPC_FUNC_STATIC(setenv, {},
{
    MAKE_CALL(out->retval = func(in->name, in->val, in->overwrite));
}
)

/*-------------- unsetenv() --------------------------------*/
TARPC_FUNC_STATIC(unsetenv, {},
{
    MAKE_CALL(out->retval = func(in->name));
}
)

/*-------------- socket() ------------------------------*/
TARPC_FUNC_STANDALONE(socket, {},
{
    SOCKET s;

    MAKE_CALL(s = socket(domain_rpc2h(in->domain),
                         socktype_rpc2h(in->type),
                         proto_rpc2h(in->proto));
              if (s == INVALID_SOCKET)
                  errno = te_win32_wsa2errno(WSAGetLastError()));
    out->fd = (s == INVALID_SOCKET) ? -1 : (int)s;
}
)

/*-------------- close() ------------------------------*/
TARPC_FUNC_STANDALONE(close, {},
{
    tarpc_close_fd_hooks_call(in->fd);

    MAKE_CALL(out->retval = close(in->fd));
}
)

/*-------------- closesocket() ------------------------------*/
TARPC_FUNC_STANDALONE(closesocket, {},
{
    tarpc_close_fd_hooks_call(in->s);

    MAKE_CALL(out->retval = wsa_rc(closesocket((SOCKET)in->s)));
}
)

/*-------------- bind() ------------------------------*/
TARPC_FUNC_STANDALONE(bind, {},
{
    PREPARE_ADDR(my_addr, in->addr, 0);
    MAKE_CALL(out->retval = wsa_rc(bind((SOCKET)in->fd, my_addr,
                                        in->fwd_len ? (int)in->len :
                                                      my_addrlen)));
}
)

/*-------------- connect() ------------------------------*/
TARPC_FUNC_STANDALONE(connect, {},
{
    PREPARE_ADDR(serv_addr, in->addr, 0);
    MAKE_CALL(out->retval = wsa_rc(connect((SOCKET)in->fd, serv_addr,
                                           serv_addrlen)));
}
)

/*-------------- listen() ------------------------------*/
TARPC_FUNC_STANDALONE(listen, {},
{
    MAKE_CALL(out->retval = wsa_rc(listen((SOCKET)in->fd, in->backlog)));
}
)

/*-------------- accept() ------------------------------*/
TARPC_FUNC_STANDALONE(accept,
{
    COPY_ARG(len);
    COPY_ARG_ADDR(addr);
},
{
    SOCKET s;

    PREPARE_ADDR(addr, out->addr,
                 out->len.len_len == 0 ? 0 : *out->len.len_val);

    MAKE_CALL(s = accept((SOCKET)in->fd, addr,
                         out->len.len_len == 0 ? NULL :
                             WSA_LEN(out->len.len_val));
              if (s == INVALID_SOCKET)
                  errno = te_win32_wsa2errno(WSAGetLastError()));
    out->retval = (s == INVALID_SOCKET) ? -1 : (int)s;

    sockaddr_output_h2rpc(addr, addrlen,
                          out->len.len_len == 0 ? 0 :
                              *(out->len.len_val),
                          &(out->addr));
}
)

/*-------------- send() ------------------------------*/
TARPC_FUNC_STANDALONE(send, {},
{
    INIT_CHECKED_ARG(in->buf.buf_val, in->buf.buf_len, 0);

    MAKE_CALL(out->retval = wsa_rc(send((SOCKET)in->fd,
                                        (const char *)in->buf.buf_val,
                                        in->len,
                                        send_recv_flags_rpc2h(in->flags))));
}
)

/*-------------- sendto() ------------------------------*/
TARPC_FUNC_STANDALONE(sendto, {},
{
    PREPARE_ADDR(to, in->to, 0);

    INIT_CHECKED_ARG(in->buf.buf_val, in->buf.buf_len, 0);

    MAKE_CALL(out->retval = wsa_rc(sendto((SOCKET)in->fd,
                                          (const char *)in->buf.buf_val,
                                          in->len,
                                          send_recv_flags_rpc2h(in->flags),
                                          to, tolen)));
}
)

/*-------------- recv() ------------------------------*/
TARPC_FUNC_STANDALONE(recv,
{
    COPY_ARG(buf);
},
{
    INIT_CHECKED_ARG(out->buf.buf_val, out->buf.buf_len, in->len);

    MAKE_CALL(out->retval = wsa_rc(recv((SOCKET)in->fd,
                                        (char *)out->buf.buf_val, in->len,
                                        send_recv_flags_rpc2h(in->flags))));
}
)

/*-------------- recvfrom() ------------------------------*/
TARPC_FUNC_STANDALONE(recvfrom,
{
    COPY_ARG(buf);
    COPY_ARG(fromlen);
    COPY_ARG_ADDR(from);
},
{
    PREPARE_ADDR(from, out->from, out->fromlen.fromlen_len == 0 ? 0 :
                                        *out->fromlen.fromlen_val);

    INIT_CHECKED_ARG(out->buf.buf_val, out->buf.buf_len, in->len);

    MAKE_CALL(out->retval =
                  wsa_rc(recvfrom((SOCKET)in->fd, (char *)out->buf.buf_val,
                                  in->len, send_recv_flags_rpc2h(in->flags),
                                  from,
                                  out->fromlen.fromlen_len == 0 ? NULL :
                                      WSA_LEN(out->fromlen.fromlen_val))));

    sockaddr_output_h2rpc(from, fromlen,
                          out->fromlen.fromlen_len == 0 ? 0 :
                              *(out->fromlen.fromlen_val),
                          &(out->from));
}
)

/*-------------- shutdown() ------------------------------*/
TARPC_FUNC_STANDALONE(shutdown, {},
{
    MAKE_CALL(out->retval = wsa_rc(shutdown((SOCKET)in->fd,
                                            shut_how_rpc2h(in->how))));
}
)

/*-------------- getsockname() ------------------------------*/
TARPC_FUNC_STANDALONE(getsockname,
{
    COPY_ARG(len);
    COPY_ARG_ADDR(addr);
},
{
    PREPARE_ADDR(name, out->addr,
                 out->len.len_len == 0 ? 0 : *out->len.len_val);

    MAKE_CALL(out->retval =
                  wsa_rc(getsockname((SOCKET)in->fd, name,
                                     out->len.len_len == 0 ? NULL :
                                         WSA_LEN(out->len.len_val))));

    sockaddr_output_h2rpc(name, namelen,
                          out->len.len_len == 0 ? 0 : *(out->len.len_val),
                          &(out->addr));
}
)

/*-------------- getpeername() ------------------------------*/
TARPC_FUNC_STANDALONE(getpeername,
{
    COPY_ARG(len);
    COPY_ARG_ADDR(addr);
},
{
    PREPARE_ADDR(name, out->addr,
                 out->len.len_len == 0 ? 0 : *out->len.len_val);

    MAKE_CALL(out->retval =
                  wsa_rc(getpeername((SOCKET)in->fd, name,
                                     out->len.len_len == 0 ? NULL :
                                         WSA_LEN(out->len.len_val))));

    sockaddr_output_h2rpc(name, namelen,
                          out->len.len_len == 0 ? 0 : *(out->len.len_val),
                          &(out->addr));
}
)

/*-------------- setsockopt() ------------------------------*/

/** Option value in the form Winsock takes it */
typedef union sockopt_param {
    int             integer;
    struct linger   linger;
    DWORD           timeout;
} sockopt_param;

/**
 * Convert an option value of RPC to the one of Winsock.
 *
 * @param level     Option level of RPC
 * @param optname   Option name of RPC
 * @param val       Option value of RPC
 * @param param     Where to put the Winsock value
 * @param len       Where to put the length of the value
 *
 * @return Status code.
 */
static te_errno
sockopt_val_rpc2h(tarpc_int level, tarpc_int optname,
                  const option_value *val, sockopt_param *param, int *len)
{
    switch (val->opttype)
    {
        case OPT_INT:
            param->integer = val->option_value_u.opt_int;
            *len = sizeof(param->integer);
            return 0;

        case OPT_LINGER:
            param->linger.l_onoff = val->option_value_u.opt_linger.l_onoff;
            param->linger.l_linger = val->option_value_u.opt_linger.l_linger;
            *len = sizeof(param->linger);
            return 0;

        case OPT_TIMEVAL:
            /* SO_RCVTIMEO and SO_SNDTIMEO take milliseconds in DWORD */
            param->timeout =
                val->option_value_u.opt_timeval.tv_sec * 1000 +
                val->option_value_u.opt_timeval.tv_usec / 1000;
            *len = sizeof(param->timeout);
            return 0;

        default:
            ERROR("Option %s of level %s of type %d is not supported",
                  sockopt_rpc2str(optname), socklevel_rpc2str(level),
                  val->opttype);
            return TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP);
    }
}

TARPC_FUNC_STANDALONE(setsockopt, {},
{
    sockopt_param   param;
    const void     *val = in->raw_optval.raw_optval_val;
    int             len = in->raw_optlen;
    te_errno        rc;

    if (in->optval.optval_val != NULL)
    {
        rc = sockopt_val_rpc2h(in->level, in->optname,
                               in->optval.optval_val, &param, &len);
        if (rc != 0)
        {
            te_rpc_error_set(rc, "Unsupported option value");
            out->retval = -1;
            return;
        }
        val = &param;
    }

    MAKE_CALL(out->retval = wsa_rc(setsockopt((SOCKET)in->s,
                                              socklevel_rpc2h(in->level),
                                              sockopt_rpc2h(in->optname),
                                              (const char *)val, len)));
}
)

/*-------------- getsockopt() ------------------------------*/
TARPC_FUNC_STANDALONE(getsockopt,
{
    COPY_ARG(optval);
    COPY_ARG(raw_optval);
    COPY_ARG(raw_optlen);
},
{
    sockopt_param   param;
    option_value   *val = out->optval.optval_val;
    int             len = sizeof(param);

    if (val == NULL)
    {
        MAKE_CALL(out->retval =
                      wsa_rc(getsockopt((SOCKET)in->s,
                                        socklevel_rpc2h(in->level),
                                        sockopt_rpc2h(in->optname),
                                        (char *)out->raw_optval.raw_optval_val,
                                        out->raw_optlen.raw_optlen_val == NULL ?
                                            NULL :
                                            WSA_LEN(out->raw_optlen.
                                                        raw_optlen_val))));
        return;
    }

    memset(&param, 0, sizeof(param));
    MAKE_CALL(out->retval = wsa_rc(getsockopt((SOCKET)in->s,
                                              socklevel_rpc2h(in->level),
                                              sockopt_rpc2h(in->optname),
                                              (char *)&param, &len)));
    if (out->retval != 0)
        return;

    switch (val->opttype)
    {
        case OPT_INT:
            /* Winsock writes BOOL options as a single byte sometimes */
            if (len == sizeof(char))
                param.integer = *(unsigned char *)&param;
            val->option_value_u.opt_int = param.integer;
            break;

        case OPT_LINGER:
            val->option_value_u.opt_linger.l_onoff = param.linger.l_onoff;
            val->option_value_u.opt_linger.l_linger = param.linger.l_linger;
            break;

        case OPT_TIMEVAL:
            val->option_value_u.opt_timeval.tv_sec = param.timeout / 1000;
            val->option_value_u.opt_timeval.tv_usec =
                (param.timeout % 1000) * 1000;
            break;

        default:
            ERROR("Option %s of level %s of type %d is not supported",
                  sockopt_rpc2str(in->optname), socklevel_rpc2str(in->level),
                  val->opttype);
            te_rpc_error_set(TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP),
                             "Unsupported option value");
            out->retval = -1;
            break;
    }
}
)
