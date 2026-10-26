/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief netinet/udp.h for a native Windows host
 *
 * UDP header with the BSD field names.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_NETINET_UDP_H__
#define __TE_WIN32_NETINET_UDP_H__

#include <stdint.h>
#include <netinet/in.h>

struct udphdr {
    uint16_t uh_sport;  /**< Source port */
    uint16_t uh_dport;  /**< Destination port */
    uint16_t uh_ulen;   /**< UDP length */
    uint16_t uh_sum;    /**< UDP checksum */
};

#ifndef SOL_UDP
#define SOL_UDP     IPPROTO_UDP
#endif

#endif /* !__TE_WIN32_NETINET_UDP_H__ */
