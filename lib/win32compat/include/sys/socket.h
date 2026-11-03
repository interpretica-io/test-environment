/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief sys/socket.h for a native Windows host
 *
 * The socket API is the one of Winsock.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_SYS_SOCKET_H__
#define __TE_WIN32_SYS_SOCKET_H__

#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <afunix.h>
#include <sys/uio.h>

#ifndef AF_LOCAL
#define AF_LOCAL    AF_UNIX
#endif
#ifndef PF_LOCAL
#define PF_LOCAL    AF_LOCAL
#endif
#ifndef PF_UNIX
#define PF_UNIX     AF_UNIX
#endif

#ifndef SHUT_RD
#define SHUT_RD     SD_RECEIVE
#endif
#ifndef SHUT_WR
#define SHUT_WR     SD_SEND
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR   SD_BOTH
#endif

/*
 * POSIX message header. Winsock has WSAMSG with different fields, the
 * calls which take it convert. The control messages are WSACMSGHDR,
 * whose layout is the POSIX one, and the macros walk them over
 * struct msghdr instead of WSAMSG.
 */
struct msghdr {
    void           *msg_name;       /**< Address */
    socklen_t       msg_namelen;    /**< Size of the address */
    struct iovec   *msg_iov;        /**< Data buffers */
    size_t          msg_iovlen;     /**< Number of the buffers */
    void           *msg_control;    /**< Control messages */
    size_t          msg_controllen; /**< Size of the control messages */
    int             msg_flags;      /**< Flags of the received message */
};

/*
 * mingw-w64 before version 11 names the control message structure
 * _WSACMSGHDR only and has the WSA_ names of the macros only.
 */
#ifndef CMSG_LEN
#define cmsghdr     _WSACMSGHDR
#define CMSG_SPACE  WSA_CMSG_SPACE
#define CMSG_LEN    WSA_CMSG_LEN
#endif

#undef CMSG_FIRSTHDR
#define CMSG_FIRSTHDR(_msg) \
    ((_msg)->msg_controllen >= sizeof(struct cmsghdr) ? \
         (struct cmsghdr *)(_msg)->msg_control : (struct cmsghdr *)NULL)

#undef CMSG_NXTHDR
#define CMSG_NXTHDR(_msg, _cmsg) \
    ((_cmsg) == NULL ? CMSG_FIRSTHDR(_msg) :                            \
     ((u_char *)(_cmsg) + WSA_CMSGHDR_ALIGN((_cmsg)->cmsg_len) +        \
          sizeof(struct cmsghdr) >                                      \
      (u_char *)(_msg)->msg_control + (_msg)->msg_controllen) ?         \
         (struct cmsghdr *)NULL :                                       \
         (struct cmsghdr *)((u_char *)(_cmsg) +                         \
                            WSA_CMSGHDR_ALIGN((_cmsg)->cmsg_len)))

/* wincrypt.h has CMSG_DATA for cryptographic messages */
#undef CMSG_DATA
#define CMSG_DATA   WSA_CMSG_DATA

#endif /* !__TE_WIN32_SYS_SOCKET_H__ */
