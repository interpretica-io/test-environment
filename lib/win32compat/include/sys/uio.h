/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief sys/uio.h for a native Windows host
 *
 * Scatter/gather I/O vector.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_SYS_UIO_H__
#define __TE_WIN32_SYS_UIO_H__

#include <stddef.h>
#include <sys/types.h>

struct iovec {
    void   *iov_base;
    size_t  iov_len;
};

extern ssize_t readv(int fd, const struct iovec *iov, int iovcnt);
extern ssize_t writev(int fd, const struct iovec *iov, int iovcnt);

#endif /* !__TE_WIN32_SYS_UIO_H__ */
