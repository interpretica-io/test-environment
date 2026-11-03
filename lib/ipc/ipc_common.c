/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief IPC library
 *
 * Common routines for IPC client and server.
 *
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#include "te_config.h"

#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#if HAVE_ASSERT_H
#include <assert.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "te_alloc.h"
#include "te_errno.h"

#include "ipc_internal.h"



/* See description in ipc_internal.h */
int
ipc_remember_datagram(struct ipc_datagrams *p_pool, void *data, size_t len,
                      struct sockaddr_un *addr, size_t addr_len)
{
    struct ipc_datagram *p;

    assert(p_pool != NULL);
    assert(data != NULL);
    assert(len > 0);
    assert(len <= IPC_SEGMENT_SIZE);
    assert(addr != NULL);

    p = TE_ALLOC( sizeof(*p));

    p->buffer = data;
    p->octets = len;
    p->sa     = *addr;
    p->sa_len = addr_len;

    TAILQ_INSERT_TAIL(p_pool, p, links);

    return 0;
}


#ifdef TE_IPC_AF_UNIX

/* See description in ipc_internal.h */
te_errno
ipc_sun_set(struct sockaddr_un *sa, const char *name)
{
    int n;

    memset(sa, 0, sizeof(*sa));
    sa->sun_family = AF_UNIX;
#ifdef IPC_SUN_ABSTRACT
    /* Sockets in the abstract namespace are started from '\0' */
    n = snprintf(sa->sun_path + 1, sizeof(sa->sun_path) - 1, "%s", name);
    if (n < 0 || (size_t)n >= sizeof(sa->sun_path) - 1)
        return TE_RC(TE_IPC, TE_ENAMETOOLONG);
#else
    n = snprintf(sa->sun_path, sizeof(sa->sun_path), IPC_SUN_DIR_FMT "/%s",
                 (unsigned int)getuid(), name);
    if (n < 0 || (size_t)n >= sizeof(sa->sun_path))
        return TE_RC(TE_IPC, TE_ENAMETOOLONG);
#endif
    return 0;
}

/* See description in ipc_internal.h */
const char *
ipc_sun_name(const struct sockaddr_un *sa)
{
#ifdef IPC_SUN_ABSTRACT
    return sa->sun_path + 1;
#else
    const char *slash = strrchr(sa->sun_path, '/');

    return slash != NULL ? slash + 1 : sa->sun_path;
#endif
}

/* See description in ipc_internal.h */
te_errno
ipc_sun_bind(int s, const char *name, bool replace)
{
    struct sockaddr_un sa;
    te_errno           rc;

    rc = ipc_sun_set(&sa, name);
    if (rc != 0)
        return rc;

#ifndef IPC_SUN_ABSTRACT
    {
        char dir[sizeof(sa.sun_path)];

        snprintf(dir, sizeof(dir), IPC_SUN_DIR_FMT, (unsigned int)getuid());
        if (mkdir(dir, S_IRWXU) != 0 && errno != EEXIST)
            return TE_OS_RC(TE_IPC, errno);
        /*
         * A socket file stays after its process has died. The abstract
         * name of a live server is busy all the same, so the name is
         * taken over as Linux would give it to a new server.
         */
        if (replace)
            (void)unlink(sa.sun_path);
    }
#else
    UNUSED(replace);
#endif

    if (bind(s, SA(&sa), sizeof(sa)) != 0)
        return TE_OS_RC(TE_IPC, errno);

    return 0;
}

/* See description in ipc_internal.h */
void
ipc_sun_unlink(int s)
{
#ifdef IPC_SUN_ABSTRACT
    UNUSED(s);
#else
    struct sockaddr_un sa;
    socklen_t          len = sizeof(sa);

    if (getsockname(s, SA(&sa), &len) == 0 && sa.sun_family == AF_UNIX &&
        sa.sun_path[0] != '\0')
        (void)unlink(sa.sun_path);
#endif
}

#endif /* TE_IPC_AF_UNIX */
