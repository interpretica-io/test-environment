/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Sun RPC basic types for hosts without an RPC library
 *
 * The C library of a native Windows host provides no XDR, so TE
 * supplies the subset that rpcgen output and RCF RPC use. The layout
 * follows the one of glibc and libtirpc.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_RPC_COMPAT_TYPES_H__
#define __TE_RPC_COMPAT_TYPES_H__

#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * C11 accepts a repeated typedef of the same type, so these do not
 * clash with the ones of the Winsock headers.
 */
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
typedef unsigned long u_long;

typedef int bool_t;
typedef int enum_t;
typedef char *caddr_t;
typedef int64_t quad_t;
typedef uint64_t u_quad_t;
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define mem_alloc(_size)        calloc(1, (_size))
#define mem_free(_ptr, _size)   free(_ptr)

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_RPC_COMPAT_TYPES_H__ */
