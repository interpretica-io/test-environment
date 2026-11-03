/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief netinet/in.h for a native Windows host
 *
 * Internet address family of Winsock.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_NETINET_IN_H__
#define __TE_WIN32_NETINET_IN_H__

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

#endif /* !__TE_WIN32_NETINET_IN_H__ */
