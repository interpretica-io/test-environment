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
#include <fcntl.h>
#include <io.h>
#include <poll.h>
#include <sys/uio.h>

#include "te_alloc.h"
#include "te_defs.h"
#include "te_shell_cmd.h"
#include "rcf_ch_api.h"
#include "agentlib.h"

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

/*-------------- read() ------------------------------*/
TARPC_FUNC_STANDALONE(read,
{
    COPY_ARG(buf);
},
{
    INIT_CHECKED_ARG(out->buf.buf_val, out->buf.buf_len, in->len);

    MAKE_CALL(out->retval = read(in->fd, out->buf.buf_val, in->len));
}
)

/*-------------- write() ------------------------------*/
TARPC_FUNC_STANDALONE(write, {},
{
    INIT_CHECKED_ARG(in->buf.buf_val, in->buf.buf_len, 0);

    MAKE_CALL(out->retval = write(in->fd, in->buf.buf_val, in->len));
}
)

/**
 * Convert an I/O vector of RPC to the one of the host.
 *
 * @param rpc       Vector of RPC
 * @param h         Where to put the vector of the host
 * @param len       Number of elements
 * @param out       Whether the call writes to the buffers
 * @param arglist   List of the checked arguments
 */
static void
iovec_rpc2h(struct tarpc_iovec *rpc, struct iovec *h, unsigned int len,
            bool out, checked_arg_list *arglist)
{
    unsigned int i;

    for (i = 0; i < len; i++)
    {
        h[i].iov_base = rpc[i].iov_base.iov_base_val;
        h[i].iov_len = rpc[i].iov_len;
        INIT_CHECKED_ARG(rpc[i].iov_base.iov_base_val,
                         rpc[i].iov_base.iov_base_len,
                         out ? rpc[i].iov_len : 0);
    }
}

/*-------------- readv() ------------------------------*/
TARPC_FUNC_STANDALONE(readv,
{
    if (out->vector.vector_len > RCF_RPC_MAX_IOVEC)
    {
        ERROR("Too long iovec is provided");
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        return true;
    }
    COPY_ARG(vector);
},
{
    struct iovec iov[RCF_RPC_MAX_IOVEC];

    iovec_rpc2h(out->vector.vector_val, iov, out->vector.vector_len,
                true, arglist);

    MAKE_CALL(out->retval = readv(in->fd,
                                  out->vector.vector_val == NULL ?
                                      NULL : iov,
                                  in->count));
}
)

/*-------------- writev() ------------------------------*/
TARPC_FUNC_STANDALONE(writev,
{
    if (in->vector.vector_len > RCF_RPC_MAX_IOVEC)
    {
        ERROR("Too long iovec is provided");
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EINVAL);
        return true;
    }
},
{
    struct iovec iov[RCF_RPC_MAX_IOVEC];

    iovec_rpc2h(in->vector.vector_val, iov, in->vector.vector_len,
                false, arglist);

    MAKE_CALL(out->retval = writev(in->fd,
                                   in->vector.vector_val == NULL ?
                                       NULL : iov,
                                   in->count));
}
)

/*-------------- open() --------------------------------*/

/**
 * Convert the mode of a new file of RPC to the one of the C runtime,
 * which tells only whether the owner can write the file.
 *
 * @param mode      Mode of RPC
 *
 * @return Mode of the C runtime.
 */
static int
file_mode_rpc2h(unsigned int mode)
{
    return (mode & RPC_S_IWUSR) ? (_S_IREAD | _S_IWRITE) : _S_IREAD;
}

/* The C runtime translates line ends in files not opened as binary ones */
TARPC_FUNC_STANDALONE(open, {},
{
    TARPC_ENSURE_NOT_NULL(path);
    MAKE_CALL(out->fd = open(in->path.path_val,
                             fcntl_flags_rpc2h(in->flags) | O_BINARY,
                             file_mode_rpc2h(in->mode)));
}
)

/*-------------- lseek() ------------------------------*/

/*
 * MinGW-w64 makes lseek a macro of lseek64 with 64-bit file offsets,
 * which would rename the RPC. The call takes 64-bit offsets anyway.
 */
#undef lseek
TARPC_FUNC_STANDALONE(lseek, {},
{
    MAKE_CALL(out->retval = _lseeki64(in->fd, in->pos,
                                      lseek_mode_rpc2h(in->mode)));
}
)

/*-------------- fsync() ------------------------------*/
TARPC_FUNC_STANDALONE(fsync, {},
{
    MAKE_CALL(out->retval = _commit(in->fd));
}
)

/*-------------- unlink() ------------------------------*/
TARPC_FUNC_STANDALONE(unlink, {},
{
    TARPC_ENSURE_NOT_NULL(path);
    MAKE_CALL(out->retval = unlink(in->path.path_val));
}
)

/*-------------- mkdir() ------------------------------*/

/* A directory of Windows has no mode */
TARPC_FUNC_STANDALONE(mkdir, {},
{
    TARPC_ENSURE_NOT_NULL(path);
    MAKE_CALL(out->retval = mkdir(in->path.path_val, 0));
}
)

/*-------------- rmdir() ------------------------------*/
TARPC_FUNC_STANDALONE(rmdir, {},
{
    TARPC_ENSURE_NOT_NULL(path);
    MAKE_CALL(out->retval = rmdir(in->path.path_val));
}
)

/*-------------- access() ------------------------------*/

/* The C runtime knows no X_OK and fails on it, every file passes it */
TARPC_FUNC_STANDALONE(access, {},
{
    MAKE_CALL(out->retval = access(in->path.path_val,
                                   access_mode_flags_rpc2h(in->mode) &
                                       (R_OK | W_OK)));
}
)

/*-------------- pipe() ------------------------------*/
TARPC_FUNC_STANDALONE(pipe,
{
    COPY_ARG(filedes);
},
{
    if (out->filedes.filedes_len < 2)
    {
        te_rpc_error_set(TE_RC(TE_TA_WIN32, TE_EINVAL),
                         "No room for two descriptors");
        out->retval = -1;
        return;
    }
    MAKE_CALL(out->retval = pipe(out->filedes.filedes_val));
}
)

/*-------------- fcntl() ------------------------------*/

/* fcntl() of win32compat supports FD_CLOEXEC, O_NONBLOCK and locks */
TARPC_FUNC_STANDALONE(fcntl,
{
    COPY_ARG(arg);
},
{
    long arg = 0;

    if (in->cmd == RPC_F_GETFD || in->cmd == RPC_F_GETFL)
    {
        MAKE_CALL(out->retval = fcntl(in->fd, fcntl_rpc2h(in->cmd)));
    }
    else if (in->cmd == RPC_F_SETFD || in->cmd == RPC_F_SETFL)
    {
        if (out->arg.arg_len > 0)
            arg = out->arg.arg_val[0].fcntl_request_u.req_int;
        /* FD_CLOEXEC is 1 everywhere, as RPC passes it */
        if (in->cmd == RPC_F_SETFL)
            arg = fcntl_flags_rpc2h(arg);
        MAKE_CALL(out->retval = fcntl(in->fd, fcntl_rpc2h(in->cmd), arg));
    }
    else
    {
        te_rpc_error_set(TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP),
                         "fcntl() command %s is not supported",
                         fcntl_rpc2str(in->cmd));
        out->retval = -1;
        return;
    }

    if (in->cmd == RPC_F_GETFL)
        out->retval = fcntl_flags_h2rpc(out->retval);
}
)

/*-------------- ioctl() ------------------------------*/

/* Winsock has the integer requests of sockets only */
TARPC_FUNC_STANDALONE(ioctl,
{
    COPY_ARG(req);
},
{
    u_long arg;
    long   code;

    switch (in->code)
    {
        case RPC_FIONBIO:
            code = FIONBIO;
            break;

        case RPC_FIONREAD:
            code = FIONREAD;
            break;

        case RPC_SIOCATMARK:
            code = SIOCATMARK;
            break;

        default:
            te_rpc_error_set(TE_RC(TE_TA_WIN32, TE_ENOIOCTLCMD),
                             "ioctl() request %s is not supported",
                             ioctl_rpc2str(in->code));
            out->retval = -1;
            return;
    }

    if (out->req.req_val == NULL ||
        out->req.req_val[0].type != IOCTL_INT)
    {
        te_rpc_error_set(TE_RC(TE_TA_WIN32, TE_EINVAL),
                         "ioctl() request %s takes an integer",
                         ioctl_rpc2str(in->code));
        out->retval = -1;
        return;
    }

    arg = out->req.req_val[0].ioctl_request_u.req_int;
    MAKE_CALL(out->retval = wsa_rc(ioctlsocket((SOCKET)in->s, code, &arg)));
    out->req.req_val[0].ioctl_request_u.req_int = arg;
}
)

/*-------------- poll() ------------------------------*/

/*
 * WSAPoll() fails on an empty set instead of sleeping, so the call
 * sleeps by itself then.
 */
TARPC_FUNC_STANDALONE(poll,
{
    if (in->ufds.ufds_len > RPC_POLL_NFDS_MAX ||
        in->nfds > RPC_POLL_NFDS_MAX)
    {
        ERROR("Too big nfds is passed to the poll()");
        out->common._errno = TE_RC(TE_TA_WIN32, TE_ENOMEM);
        return true;
    }
    COPY_ARG(ufds);
},
{
    struct pollfd   ufds[RPC_POLL_NFDS_MAX];
    unsigned int    i;

    for (i = 0; i < out->ufds.ufds_len; i++)
    {
        ufds[i].fd = (SOCKET)out->ufds.ufds_val[i].fd;
        ufds[i].events = poll_event_rpc2h(out->ufds.ufds_val[i].events);
        ufds[i].revents = 0;
    }

    if (in->nfds == 0)
    {
        MAKE_CALL(Sleep(in->timeout < 0 ? INFINITE : (DWORD)in->timeout);
                  out->retval = 0);
        return;
    }

    MAKE_CALL(out->retval = poll(ufds, in->nfds, in->timeout));

    for (i = 0; i < out->ufds.ufds_len; i++)
        out->ufds.ufds_val[i].revents = poll_event_h2rpc(ufds[i].revents);
}
)

/*-------------- fd_set constructor ----------------------------*/
TARPC_FUNC_STANDALONE(fd_set_new, {},
{
    UNUSED(in);
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        fd_set *ptr = TE_ALLOC(sizeof(fd_set));

        FD_ZERO(ptr);
        out->retval = RCF_PCH_MEM_INDEX_ALLOC(ptr, ns);
    });
}
)

/*-------------- fd_set destructor ----------------------------*/
TARPC_FUNC_STANDALONE(fd_set_delete, {},
{
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        free(IN_FDSET_NS(ns));
        out->common._errno = RCF_PCH_MEM_INDEX_FREE(in->set, ns);
    });
}
)

/*-------------- FD_ZERO --------------------------------*/
TARPC_FUNC_STANDALONE(do_fd_zero, {},
{
    UNUSED(out);
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        FD_ZERO(IN_FDSET_NS(ns));
    });
}
)

/*-------------- FD_SET --------------------------------*/
TARPC_FUNC_STANDALONE(do_fd_set, {},
{
    UNUSED(out);
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        FD_SET((SOCKET)in->fd, IN_FDSET_NS(ns));
    });
}
)

/*-------------- FD_CLR --------------------------------*/
TARPC_FUNC_STANDALONE(do_fd_clr, {},
{
    UNUSED(out);
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        FD_CLR((SOCKET)in->fd, IN_FDSET_NS(ns));
    });
}
)

/*-------------- FD_ISSET --------------------------------*/
TARPC_FUNC_STANDALONE(do_fd_isset, {},
{
    RPC_PCH_MEM_WITH_NAMESPACE(ns, RPC_TYPE_NS_FD_SET, {
        out->retval = FD_ISSET((SOCKET)in->fd, IN_FDSET_NS(ns)) ? 1 : 0;
    });
}
)

/*-------------- select() --------------------------------*/

/*
 * Winsock ignores the first argument and fails when every set is empty,
 * so the call sleeps by itself then.
 */
TARPC_FUNC_STANDALONE(select,
{
    COPY_ARG(timeout);
},
{
    fd_set         *rfds;
    fd_set         *wfds;
    fd_set         *efds;
    struct timeval  tv;
    static rpc_ptr_id_namespace ns = RPC_PTR_ID_NS_INVALID;

    out->retval = -1;
    RCF_PCH_MEM_NS_CREATE_IF_NEEDED_RETURN(&ns, RPC_TYPE_NS_FD_SET, );

    if (out->timeout.timeout_len > 0)
        TARPC_CHECK_RC(timeval_rpc2h(out->timeout.timeout_val, &tv));

    if (out->common._errno != 0)
        return;

    RCF_PCH_MEM_INDEX_TO_PTR_RPC(rfds, in->readfds, ns, );
    RCF_PCH_MEM_INDEX_TO_PTR_RPC(wfds, in->writefds, ns, );
    RCF_PCH_MEM_INDEX_TO_PTR_RPC(efds, in->exceptfds, ns, );

    if ((rfds == NULL || rfds->fd_count == 0) &&
        (wfds == NULL || wfds->fd_count == 0) &&
        (efds == NULL || efds->fd_count == 0))
    {
        MAKE_CALL(Sleep(out->timeout.timeout_len == 0 ? INFINITE :
                            (DWORD)TE_SEC2MS(tv.tv_sec) +
                                TE_US2MS(tv.tv_usec));
                  out->retval = 0);
        if (out->timeout.timeout_len > 0)
            memset(out->timeout.timeout_val, 0,
                   sizeof(*out->timeout.timeout_val));
        return;
    }

    MAKE_CALL(out->retval =
                  wsa_rc(select(0, rfds, wfds, efds,
                                out->timeout.timeout_len == 0 ?
                                    NULL : &tv)));

    if (out->timeout.timeout_len > 0)
        TARPC_CHECK_RC(timeval_h2rpc(&tv, out->timeout.timeout_val));
    if (TE_RC_GET_ERROR(out->common._errno) == TE_EH2RPC)
        out->retval = -1;
}
)

/*-------------- gethostname() --------------------------------*/
TARPC_FUNC_STANDALONE(gethostname,
{
    COPY_ARG(name);
},
{
    MAKE_CALL(out->retval = wsa_rc(gethostname(out->name.name_val,
                                               in->len)));
}
)

/*-------------- kill() --------------------------------*/
TARPC_FUNC_STANDALONE(kill, {},
{
    MAKE_CALL(out->retval = kill(in->pid, signum_rpc2h(in->signum)));
}
)

/*-------------- waitpid() --------------------------------*/

/* The agent reaps its children itself, RPC_WSYSTEM makes no difference */
TARPC_FUNC_STANDALONE(waitpid, {},
{
    int             st = 0;
    rpc_wait_status r_st;

    MAKE_CALL(out->pid = ta_waitpid(in->pid, &st,
                                    waitpid_opts_rpc2h(in->options)));
    r_st = wait_status_h2rpc(st);
    out->status_flag = r_st.flag;
    out->status_value = r_st.value;
}
)

/*-------------- ta_kill_death() --------------------------------*/
TARPC_FUNC_STANDALONE(ta_kill_death, {},
{
    MAKE_CALL(out->retval = ta_kill_death(in->pid));
}
)

/*-------------- ta_kill_and_wait() -----------------------------*/
TARPC_FUNC_STANDALONE(ta_kill_and_wait, {},
{
    MAKE_CALL(out->retval = ta_kill_and_wait(in->pid,
                                             signum_rpc2h(in->sig),
                                             in->timeout));
}
)

/*-------------- te_shell_cmd() --------------------------------*/
TARPC_FUNC_STANDALONE(te_shell_cmd, {},
{
    MAKE_CALL(out->pid = te_shell_cmd(in->cmd.cmd_val, in->uid,
                                      in->in_fd ? &out->in_fd : NULL,
                                      in->out_fd ? &out->out_fd : NULL,
                                      in->err_fd ? &out->err_fd : NULL));
}
)

/*-------------- system() ----------------------------------*/
TARPC_FUNC_STANDALONE(system, {},
{
    int             st;
    rpc_wait_status r_st;

    MAKE_CALL(st = ta_system(in->cmd.cmd_val));
    r_st = wait_status_h2rpc(st);
    out->status_flag = r_st.flag;
    out->status_value = r_st.value;
}
)

/*--------------------------- malloc ---------------------------------*/
TARPC_FUNC_STANDALONE(malloc, {},
{
    void *buf = malloc(in->size);

    if (buf == NULL)
        out->common._errno = TE_RC(TE_TA_WIN32, TE_ENOMEM);
    else
        out->retval = rcf_pch_mem_alloc(buf);
}
)

/*--------------------------- free ---------------------------------*/
TARPC_FUNC_STANDALONE(free, {},
{
    UNUSED(out);
    free(rcf_pch_mem_base_ptr(in->buf));
    rcf_pch_mem_free(in->buf);
}
)

/*-------------- memcmp() ------------------------------*/
TARPC_FUNC_STANDALONE(memcmp, {},
{
    out->retval = memcmp((uint8_t *)rcf_pch_mem_get(in->s1_base) +
                             in->s1_off,
                         (uint8_t *)rcf_pch_mem_get(in->s2_base) +
                             in->s2_off,
                         in->n);
}
)

/*-------------------------- set_buf() ----------------------------*/
TARPC_FUNC_STANDALONE(set_buf, {},
{
    uint8_t *dst = rcf_pch_mem_get(in->dst_buf);
    size_t   len = in->src_buf.src_buf_len;

    UNUSED(out);
    if (dst != NULL && len != 0)
        memcpy(dst + in->dst_off, in->src_buf.src_buf_val, len);
    else if (len != 0)
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EFAULT);
}
)

/*-------------------------- get_buf() ----------------------------*/
TARPC_FUNC_STANDALONE(get_buf, {},
{
    uint8_t *src = rcf_pch_mem_get(in->src_buf);

    out->dst_buf.dst_buf_val = NULL;
    out->dst_buf.dst_buf_len = 0;
    if (src != NULL && in->len != 0)
    {
        out->dst_buf.dst_buf_val = TE_ALLOC(in->len);
        memcpy(out->dst_buf.dst_buf_val, src + in->src_off, in->len);
        out->dst_buf.dst_buf_len = in->len;
    }
    else if (in->len != 0)
    {
        out->common._errno = TE_RC(TE_TA_WIN32, TE_EFAULT);
    }
}
)

/*---------------------- set_buf_pattern() ----------------------*/
TARPC_FUNC_STANDALONE(set_buf_pattern, {},
{
    uint8_t *dst = rcf_pch_mem_get(in->dst_buf);
    size_t   i;

    UNUSED(out);
    if (dst == NULL)
    {
        if (in->len != 0)
            out->common._errno = TE_RC(TE_TA_WIN32, TE_EFAULT);
        return;
    }

    dst += in->dst_off;
    if (in->pattern < TAPI_RPC_BUF_RAND)
    {
        memset(dst, in->pattern, in->len);
    }
    else
    {
        for (i = 0; i < in->len; i++)
            dst[i] = rand() % TAPI_RPC_BUF_RAND;
    }
}
)
