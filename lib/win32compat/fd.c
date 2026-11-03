/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: descriptors
 *
 * A Winsock socket is a kernel handle, a C library file descriptor
 * is an index in the table of the C library. A descriptor which is not
 * an open file descriptor is taken for a socket.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

#include <sys/uio.h>

/**
 * Whether a descriptor is a C library file descriptor. _get_osfhandle()
 * sets errno to EBADF on a socket, the check keeps it.
 */
static bool
fd_is_file(int fd)
{
    int     saved_errno = errno;
    bool    is_file;

    is_file = (_get_osfhandle(fd) != (intptr_t)INVALID_HANDLE_VALUE);
    errno = saved_errno;

    return is_file;
}

/** Whether a descriptor is a socket */
static bool
fd_is_socket(int fd)
{
    int type;
    int len = sizeof(type);

    return getsockopt((SOCKET)fd, SOL_SOCKET, SO_TYPE, (char *)&type,
                      &len) == 0;
}

/** Kernel handle behind a descriptor */
static HANDLE
fd_handle(int fd)
{
    if (fd_is_socket(fd))
        return (HANDLE)(uintptr_t)fd;
    return (HANDLE)_get_osfhandle(fd);
}

/**
 * Apply a file lock. LockFileEx() locks are mandatory and are not
 * merged or split, so only a lock over the same range is released.
 */
static int
fd_lock(HANDLE handle, int cmd, struct flock *fl)
{
    OVERLAPPED ov;
    LARGE_INTEGER start;
    LARGE_INTEGER len;
    DWORD flags = 0;

    if (fl->l_whence != SEEK_SET)
    {
        errno = EINVAL;
        return -1;
    }

    start.QuadPart = fl->l_start;
    len.QuadPart = fl->l_len == 0 ? INT64_MAX : fl->l_len;

    memset(&ov, 0, sizeof(ov));
    ov.Offset = start.LowPart;
    ov.OffsetHigh = start.HighPart;

    switch (fl->l_type)
    {
        case F_UNLCK:
            if (!UnlockFileEx(handle, 0, len.LowPart, len.HighPart, &ov))
            {
                errno = te_win32_err2errno(GetLastError());
                return -1;
            }
            return 0;

        case F_WRLCK:
            flags |= LOCKFILE_EXCLUSIVE_LOCK;
            /* fall through */

        case F_RDLCK:
            if (cmd == F_SETLK)
                flags |= LOCKFILE_FAIL_IMMEDIATELY;
            if (!LockFileEx(handle, flags, 0, len.LowPart, len.HighPart,
                            &ov))
            {
                DWORD err = GetLastError();

                errno = (err == ERROR_LOCK_VIOLATION) ?
                            EAGAIN : te_win32_err2errno(err);
                return -1;
            }
            return 0;

        default:
            errno = EINVAL;
            return -1;
    }
}

/* See the description in te_win32_posix.h */
int
fcntl(int fd, int cmd, ...)
{
    HANDLE handle = fd_handle(fd);
    struct flock *fl = NULL;
    DWORD flags;
    va_list ap;
    int arg = 0;

    va_start(ap, cmd);
    if (cmd == F_SETFD || cmd == F_SETFL)
        arg = va_arg(ap, int);
    else if (cmd == F_GETLK || cmd == F_SETLK || cmd == F_SETLKW)
        fl = va_arg(ap, struct flock *);
    va_end(ap);

    if (handle == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return -1;
    }

    switch (cmd)
    {
        case F_GETFD:
            if (!GetHandleInformation(handle, &flags))
            {
                errno = te_win32_err2errno(GetLastError());
                return -1;
            }
            return (flags & HANDLE_FLAG_INHERIT) ? 0 : FD_CLOEXEC;

        case F_SETFD:
            if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT,
                                      (arg & FD_CLOEXEC) ?
                                          0 : HANDLE_FLAG_INHERIT))
            {
                errno = te_win32_err2errno(GetLastError());
                return -1;
            }
            return 0;

        case F_GETFL:
            /* Winsock cannot tell whether a socket is non-blocking */
            return 0;

        case F_SETFL:
            if (fd_is_socket(fd))
            {
                u_long nb = (arg & O_NONBLOCK) ? 1 : 0;

                if (ioctlsocket((SOCKET)fd, FIONBIO, &nb) != 0)
                {
                    errno = te_win32_wsa2errno(WSAGetLastError());
                    return -1;
                }
                return 0;
            }
            if (arg & O_NONBLOCK)
            {
                errno = ENOTSUP;
                return -1;
            }
            return 0;

        case F_SETLK:
        case F_SETLKW:
            return fd_lock(handle, cmd, fl);

        case F_GETLK:
            /* Windows cannot tell who holds a lock */
            fl->l_type = F_UNLCK;
            return 0;

        default:
            errno = EINVAL;
            return -1;
    }
}

/* See the description in te_win32_posix.h */
int
pipe(int fds[2])
{
    if (_pipe(fds, 65536, _O_BINARY | _O_NOINHERIT) != 0)
        return -1;
    return 0;
}

/* See the description in te_win32_posix.h */
int
te_win32_close(int fd)
{
    if (!fd_is_file(fd) && fd_is_socket(fd))
    {
        if (closesocket((SOCKET)fd) != 0)
        {
            errno = te_win32_wsa2errno(WSAGetLastError());
            return -1;
        }
        return 0;
    }
    return _close(fd);
}

/* See the description in te_win32_posix.h */
ssize_t
te_win32_read(int fd, void *buf, size_t count)
{
    if (!fd_is_file(fd) && fd_is_socket(fd))
    {
        int rc = recv((SOCKET)fd, buf, count > INT_MAX ? INT_MAX : count, 0);

        if (rc == SOCKET_ERROR)
        {
            errno = te_win32_wsa2errno(WSAGetLastError());
            return -1;
        }
        return rc;
    }
    return _read(fd, buf, count > UINT_MAX ? UINT_MAX : count);
}

/* See the description in te_win32_posix.h */
ssize_t
te_win32_write(int fd, const void *buf, size_t count)
{
    if (!fd_is_file(fd) && fd_is_socket(fd))
    {
        int rc = send((SOCKET)fd, buf, count > INT_MAX ? INT_MAX : count, 0);

        if (rc == SOCKET_ERROR)
        {
            errno = te_win32_wsa2errno(WSAGetLastError());
            return -1;
        }
        return rc;
    }
    return _write(fd, buf, count > UINT_MAX ? UINT_MAX : count);
}

/* See the description in sys/uio.h */
ssize_t
readv(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    int i;

    for (i = 0; i < iovcnt; i++)
    {
        ssize_t rc = te_win32_read(fd, iov[i].iov_base, iov[i].iov_len);

        if (rc < 0)
            return total > 0 ? total : rc;
        total += rc;
        if ((size_t)rc < iov[i].iov_len)
            break;
    }
    return total;
}

/* See the description in sys/uio.h */
ssize_t
writev(int fd, const struct iovec *iov, int iovcnt)
{
    ssize_t total = 0;
    int i;

    for (i = 0; i < iovcnt; i++)
    {
        ssize_t rc = te_win32_write(fd, iov[i].iov_base, iov[i].iov_len);

        if (rc < 0)
            return total > 0 ? total : rc;
        total += rc;
        if ((size_t)rc < iov[i].iov_len)
            break;
    }
    return total;
}

/* See the description in te_win32_posix.h */
ssize_t
pread(int fd, void *buf, size_t count, off_t offset)
{
    __int64 saved = _lseeki64(fd, 0, SEEK_CUR);
    ssize_t rc;

    if (saved < 0 || _lseeki64(fd, offset, SEEK_SET) < 0)
        return -1;
    rc = _read(fd, buf, count > UINT_MAX ? UINT_MAX : count);
    _lseeki64(fd, saved, SEEK_SET);
    return rc;
}

/* See the description in te_win32_posix.h */
ssize_t
pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    __int64 saved = _lseeki64(fd, 0, SEEK_CUR);
    ssize_t rc;

    if (saved < 0 || _lseeki64(fd, offset, SEEK_SET) < 0)
        return -1;
    rc = _write(fd, buf, count > UINT_MAX ? UINT_MAX : count);
    _lseeki64(fd, saved, SEEK_SET);
    return rc;
}
