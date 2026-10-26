/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief netinet/ip6.h for a native Windows host
 *
 * IPv6 header in the layout of glibc and the BSDs.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_NETINET_IP6_H__
#define __TE_WIN32_NETINET_IP6_H__

#include <stdint.h>
#include <netinet/in.h>

struct ip6_hdr {
    union {
        struct ip6_hdrctl {
            uint32_t ip6_un1_flow;  /**< 4 bits version, 8 bits TC,
                                         20 bits flow label */
            uint16_t ip6_un1_plen;  /**< Payload length */
            uint8_t  ip6_un1_nxt;   /**< Next header */
            uint8_t  ip6_un1_hlim;  /**< Hop limit */
        } ip6_un1;
        uint8_t ip6_un2_vfc;        /**< 4 bits version, top 4 bits TC */
    } ip6_ctlun;
    struct in6_addr ip6_src;
    struct in6_addr ip6_dst;
};

#define ip6_vfc     ip6_ctlun.ip6_un2_vfc
#define ip6_flow    ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen    ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt     ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim    ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops    ip6_ctlun.ip6_un1.ip6_un1_hlim

#endif /* !__TE_WIN32_NETINET_IP6_H__ */
