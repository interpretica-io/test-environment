/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Test API for RPC
 *
 * TAPI for remote calls of name resolution routines
 *
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#include "te_config.h"

#include <stddef.h>
#include <stdio.h>
#include <ctype.h>
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#include <stdarg.h>

#include "tapi_rpc_internal.h"
#include "tapi_rpc_netdb.h"

#include "te_alloc.h"

/**
 * Convert hostent received via RPC to the host struct hostent.
 * Memory allocated by RPC library is partially used and rpc_he is
 * updated properly to avoid releasing of re-used memory.
 *
 * @param rpc_he        pointer to the field of RPC out structure
 *
 * @return pointer to struct hostent or NULL is memory allocation failed
 */
static struct hostent *
hostent_rpc2h(tarpc_hostent *rpc_he)
{
    struct hostent *he;

    if (rpc_he == NULL)
        return NULL;

    he = TE_ALLOC(sizeof(*he));

    if (rpc_he->h_aliases.h_aliases_val != NULL)
        he->h_aliases = TE_ALLOC(rpc_he->h_aliases.h_aliases_len *
                                 sizeof(char *));

    if (rpc_he->h_addr_list.h_addr_list_val != NULL)
        he->h_addr_list = TE_ALLOC(rpc_he->h_addr_list.h_addr_list_len *
                                  sizeof(char *));

    he->h_name = rpc_he->h_name.h_name_val;
    rpc_he->h_name.h_name_val = NULL;
    rpc_he->h_name.h_name_len = 0;

    if (he->h_aliases != NULL)
    {
        unsigned int i;

        for (i = 0; i < rpc_he->h_aliases.h_aliases_len; i++)
        {
            he->h_aliases[i] =
                rpc_he->h_aliases.h_aliases_val[i].name.name_val;
            rpc_he->h_aliases.h_aliases_val[i].name.name_val = NULL;
            rpc_he->h_aliases.h_aliases_val[i].name.name_len = 0;
        }
    }

    if (he->h_addr_list != NULL)
    {
        unsigned int i;

        for (i = 0; i < rpc_he->h_addr_list.h_addr_list_len; i++)
        {
            he->h_addr_list[i] =
                rpc_he->h_addr_list.h_addr_list_val[i].val.val_val;
            rpc_he->h_addr_list.h_addr_list_val[i].val.val_val = NULL;
            rpc_he->h_addr_list.h_addr_list_val[i].val.val_len = 0;
        }
    }

    he->h_length = rpc_he->h_length;
    he->h_addrtype = domain_rpc2h(rpc_he->h_addrtype);

    return he;
}

struct hostent *
rpc_gethostbyname(rcf_rpc_server *rpcs, const char *name)
{
    tarpc_gethostbyname_in  in;
    tarpc_gethostbyname_out out;

    struct hostent *res = NULL;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    if (rpcs == NULL)
    {
        ERROR("%s(): Invalid RPC server handle", __FUNCTION__);
        RETVAL_PTR(gethostbyname, NULL);
    }

    in.name.name_val = (char *)name;
    in.name.name_len = (name == NULL) ? 0 : (strlen(name) + 1);

    rcf_rpc_call(rpcs, "gethostbyname", &in, &out);

    if (RPC_IS_CALL_OK(rpcs) && out.res.res_val != NULL)
    {
        if ((res = hostent_rpc2h(out.res.res_val)) == NULL)
            rpcs->_errno = TE_RC(TE_RCF, TE_ENOMEM);
    }

    TAPI_RPC_LOG(rpcs, gethostbyname, "%s", "%p",
                 name == NULL ? "" : name, res);
    RETVAL_PTR(gethostbyname, res);
}

struct hostent *
rpc_gethostbyaddr(rcf_rpc_server *rpcs,
                  const char *addr, int len, rpc_socket_addr_family type)
{
    tarpc_gethostbyaddr_in  in;
    tarpc_gethostbyaddr_out out;

    struct hostent *res = NULL;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    if (rpcs == NULL)
    {
        ERROR("%s(): Invalid RPC server handle", __FUNCTION__);
        RETVAL_PTR(gethostbyaddr, NULL);
    }

    in.type = type;

    if (addr != NULL)
    {
        in.addr.val.val_val = (char *)addr;
        in.addr.val.val_len = len;
    }

    rcf_rpc_call(rpcs, "gethostbyaddr",  &in, &out);

    if (RPC_IS_CALL_OK(rpcs) && out.res.res_val != NULL)
    {
        if ((res = hostent_rpc2h(out.res.res_val)) == NULL)
            rpcs->_errno = TE_RC(TE_RCF, TE_ENOMEM);
    }

    TAPI_RPC_LOG(rpcs, gethostbyaddr, "%p, %d, %d", "%p",
                 addr, len, type, res);
    RETVAL_PTR(gethostbyaddr, res);
}

/**
 * Addresses of getaddrinfo() with the pointer to them on the RPC server.
 * The list a test gets is @p ai.
 */
typedef struct addrinfo_list {
    tarpc_ptr       mem_ptr;    /**< Addresses on the RPC server */
    struct addrinfo ai[];       /**< Addresses on the engine */
} addrinfo_list;

/** Get the list of addresses which getaddrinfo() returned */
static addrinfo_list *
addrinfo_list_of(struct addrinfo *res)
{
    return (addrinfo_list *)((uint8_t *)res -
                             offsetof(addrinfo_list, ai));
}

/**
 * Convert RPC addrinfo to the host native one.
 *
 * @param rpc_ai        RPC addrinfo structure
 * @param ai            pre-allocated host addrinfo structure
 *
 * @return 0 in the case of success or -1 in the case of memory allocation
 * failure
 */
static int
ai_rpc2h(struct tarpc_ai *ai_rpc, struct addrinfo *ai)
{
    struct sockaddr *addr;

    ai->ai_flags = ai_flags_rpc2h(ai_rpc->flags);
    ai->ai_family = domain_rpc2h(ai_rpc->family);
    ai->ai_socktype = socktype_rpc2h(ai_rpc->socktype);
    ai->ai_protocol = proto_rpc2h(ai_rpc->protocol);
    ai->ai_addrlen = ai_rpc->addrlen + SA_COMMON_LEN;

    addr = TE_ALLOC(sizeof(struct sockaddr_storage));
    if (sockaddr_rpc2h(&ai_rpc->addr, addr, sizeof(struct sockaddr_storage),
                       &ai->ai_addr, NULL) != 0 || ai->ai_addr == NULL)
    {
        free(addr);
        ai->ai_addr = NULL;
    }

    if (ai_rpc->canonname.canonname_val != NULL)
    {
        ai->ai_canonname = ai_rpc->canonname.canonname_val;
        ai_rpc->canonname.canonname_val = NULL;
        ai_rpc->canonname.canonname_len = 0;
    }

    return 0;
}


int
rpc_getaddrinfo(rcf_rpc_server *rpcs,
                const char *node, const char *service,
                const struct addrinfo *hints,
                struct addrinfo **res)
{
    tarpc_getaddrinfo_in  in;
    tarpc_getaddrinfo_out out;

    addrinfo_list   *list = NULL;
    struct tarpc_ai  rpc_hints;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));
    memset(&rpc_hints, 0, sizeof(rpc_hints));

    if (rpcs == NULL)
    {
        ERROR("%s(): Invalid RPC server handle", __FUNCTION__);
        RETVAL_INT(getaddrinfo, -1);
    }

    /*
     * xdr_char() of libtirpc writes each character back when it encodes,
     * so the strings are copied: a caller may pass constant ones.
     */
    if (node != NULL)
    {
        in.node.node_val = TE_STRDUP(node);
        in.node.node_len = strlen(node) + 1;
    }

    if (service != NULL)
    {
        in.service.service_val = TE_STRDUP(service);
        in.service.service_len = strlen(service) + 1;
    }

    if (hints != NULL)
    {
        rpc_hints.flags = ai_flags_h2rpc(hints->ai_flags);
        rpc_hints.family = addr_family_h2rpc(hints->ai_family);
        rpc_hints.socktype = socktype_h2rpc(hints->ai_socktype);
        rpc_hints.protocol = proto_h2rpc(hints->ai_protocol);
        rpc_hints.addrlen = hints->ai_addrlen - SA_COMMON_LEN;

        sockaddr_input_h2rpc(hints->ai_addr, &rpc_hints.addr);

        if (hints->ai_canonname != NULL)
        {
            rpc_hints.canonname.canonname_val =
                TE_STRDUP(hints->ai_canonname);
            rpc_hints.canonname.canonname_len =
                strlen(hints->ai_canonname) + 1;
        }

        in.hints.hints_val = &rpc_hints;
        in.hints.hints_len = 1;
    }

    rcf_rpc_call(rpcs, "getaddrinfo", &in, &out);

    free(in.node.node_val);
    free(in.service.service_val);
    free(rpc_hints.canonname.canonname_val);

    if (RPC_IS_CALL_OK(rpcs) && out.res.res_val != NULL)
    {
        int i;

        list = TE_ALLOC(sizeof(*list) +
                        out.res.res_len * sizeof(list->ai[0]));
        list->mem_ptr = out.mem_ptr;

        for (i = 0; i < (int)out.res.res_len; i++)
        {
            if (ai_rpc2h(&out.res.res_val[i], &list->ai[i]) < 0)
            {
                for (i--; i >= 0; i--)
                {
                    free(list->ai[i].ai_addr);
                    free(list->ai[i].ai_canonname);
                }
                free(list);
                rpcs->_errno = TE_RC(TE_RCF, TE_ENOMEM);
                RETVAL_INT(getaddrinfo, -1);
            }
            list->ai[i].ai_next = (i == (int)out.res.res_len - 1) ?
                                      NULL : &list->ai[i + 1];
        }
        *res = list->ai;
    }

    CHECK_RETVAL_VAR_IS_ZERO_OR_MINUS_ONE(getaddrinfo, out.retval);
    TAPI_RPC_LOG(rpcs, getaddrinfo, "%s, %s, %p, %p", "%d",
                 node, service, hints, res, out.retval);
    RETVAL_INT(getaddrinfo, out.retval);
}

void
rpc_freeaddrinfo(rcf_rpc_server *rpcs,
                 struct addrinfo *res)
{
    tarpc_freeaddrinfo_in  in;
    tarpc_freeaddrinfo_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    if (rpcs == NULL)
    {
        ERROR("%s(): Invalid RPC server handle", __FUNCTION__);
        RETVAL_VOID(freeaddrinfo);
    }

    rpcs->op = RCF_RPC_CALL_WAIT;

    if (res != NULL)
        in.mem_ptr = addrinfo_list_of(res)->mem_ptr;

    rcf_rpc_call(rpcs, "freeaddrinfo",  &in, &out);

    TAPI_RPC_LOG(rpcs, freeaddrinfo, "%p", "", res);

    if (res != NULL)
    {
        struct addrinfo *ai;

        for (ai = res; ai != NULL; ai = ai->ai_next)
        {
            free(ai->ai_addr);
            free(ai->ai_canonname);
        }
        free(addrinfo_list_of(res));
    }

    RETVAL_VOID(freeaddrinfo);
}
