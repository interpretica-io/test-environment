/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief poll.h for a native Windows host
 *
 * poll() is WSAPoll(), so it works on sockets only.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_POLL_H__
#define __TE_WIN32_POLL_H__

#include <winsock2.h>
#include <errno.h>

typedef unsigned long nfds_t;

extern int te_win32_wsa2errno(int wsa_error);

static inline int
poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    int rc = WSAPoll(fds, nfds, timeout);

    if (rc == SOCKET_ERROR)
        errno = te_win32_wsa2errno(WSAGetLastError());
    return rc;
}

#endif /* !__TE_WIN32_POLL_H__ */
