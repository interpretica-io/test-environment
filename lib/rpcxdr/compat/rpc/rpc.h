/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Sun RPC declarations for hosts without an RPC library
 *
 * RCF RPC carries its calls itself and uses only XDR. The client and
 * service types appear in the prototypes rpcgen writes, so they are
 * declared, with only the fields RCF RPC uses.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_RPC_COMPAT_RPC_H__
#define __TE_RPC_COMPAT_RPC_H__

#include <rpc/types.h>
#include <rpc/xdr.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Status of a client call */
enum clnt_stat {
    RPC_SUCCESS = 0,
    RPC_CANTENCODEARGS = 1,
    RPC_CANTDECODERES = 2,
    RPC_CANTSEND = 3,
    RPC_CANTRECV = 4,
    RPC_TIMEDOUT = 5,
    RPC_FAILED = 16,
};

typedef struct CLIENT CLIENT;

/*
 * RCF RPC passes its own data to the call implementations in these,
 * so they have the fields it uses.
 */
typedef struct SVCXPRT {
    void   *xp_p1;      /**< Private data */
    void   *xp_p2;      /**< Private data */
} SVCXPRT;

struct svc_req {
    u_int       rq_prog;    /**< Program number */
    u_int       rq_vers;    /**< Version number */
    u_int       rq_proc;    /**< Procedure number */
    SVCXPRT    *rq_xprt;    /**< Transport */
};

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_RPC_COMPAT_RPC_H__ */
