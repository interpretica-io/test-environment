/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: error codes
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

/* See the description in te_win32_posix.h */
int
te_win32_wsa2errno(int wsa_error)
{
    switch (wsa_error)
    {
        case 0:                     return 0;
        case WSAEINTR:              return EINTR;
        case WSAEBADF:              return EBADF;
        case WSAEACCES:             return EACCES;
        case WSAEFAULT:             return EFAULT;
        case WSAEINVAL:             return EINVAL;
        case WSAEMFILE:             return EMFILE;
        case WSAEWOULDBLOCK:        return EAGAIN;
        case WSAEINPROGRESS:        return EINPROGRESS;
        case WSAEALREADY:           return EALREADY;
        case WSAENOTSOCK:           return ENOTSOCK;
        case WSAEDESTADDRREQ:       return EDESTADDRREQ;
        case WSAEMSGSIZE:           return EMSGSIZE;
        case WSAEPROTOTYPE:         return EPROTOTYPE;
        case WSAENOPROTOOPT:        return ENOPROTOOPT;
        case WSAEPROTONOSUPPORT:    return EPROTONOSUPPORT;
        case WSAEOPNOTSUPP:         return EOPNOTSUPP;
        case WSAEAFNOSUPPORT:       return EAFNOSUPPORT;
        case WSAEADDRINUSE:         return EADDRINUSE;
        case WSAEADDRNOTAVAIL:      return EADDRNOTAVAIL;
        case WSAENETDOWN:           return ENETDOWN;
        case WSAENETUNREACH:        return ENETUNREACH;
        case WSAENETRESET:          return ENETRESET;
        case WSAECONNABORTED:       return ECONNABORTED;
        case WSAECONNRESET:         return ECONNRESET;
        case WSAENOBUFS:            return ENOBUFS;
        case WSAEISCONN:            return EISCONN;
        case WSAENOTCONN:           return ENOTCONN;
        case WSAETIMEDOUT:          return ETIMEDOUT;
        case WSAECONNREFUSED:       return ECONNREFUSED;
        case WSAELOOP:              return ELOOP;
        case WSAENAMETOOLONG:       return ENAMETOOLONG;
        case WSAEHOSTUNREACH:       return EHOSTUNREACH;
        case WSAENOTEMPTY:          return ENOTEMPTY;
        case WSA_NOT_ENOUGH_MEMORY: return ENOMEM;
        case WSANOTINITIALISED:     return EINVAL;
        default:                    return EIO;
    }
}

/* See the description in te_win32_posix.h */
int
te_win32_err2errno(DWORD error)
{
    switch (error)
    {
        case ERROR_SUCCESS:             return 0;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_INVALID_DRIVE:
        case ERROR_MOD_NOT_FOUND:
        case ERROR_PROC_NOT_FOUND:      return ENOENT;
        case ERROR_TOO_MANY_OPEN_FILES: return EMFILE;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:      return EACCES;
        case ERROR_INVALID_HANDLE:      return EBADF;
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:         return ENOMEM;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:      return EEXIST;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_NAME:        return EINVAL;
        case ERROR_BROKEN_PIPE:
        case ERROR_NO_DATA:             return EPIPE;
        case ERROR_DISK_FULL:           return ENOSPC;
        case ERROR_DIR_NOT_EMPTY:       return ENOTEMPTY;
        case ERROR_BUFFER_OVERFLOW:
        case ERROR_INSUFFICIENT_BUFFER: return ENOBUFS;
        case ERROR_NOT_SUPPORTED:
        case ERROR_CALL_NOT_IMPLEMENTED: return ENOTSUP;
        case ERROR_TIMEOUT:
        case WAIT_TIMEOUT:              return ETIMEDOUT;
        case ERROR_BAD_EXE_FORMAT:      return ENOEXEC;
        case ERROR_NOT_FOUND:           return ENOENT;
        default:                        return EIO;
    }
}
