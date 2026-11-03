/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief netinet/tcp.h for a native Windows host
 *
 * TCP options of Winsock.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_NETINET_TCP_H__
#define __TE_WIN32_NETINET_TCP_H__

#include <netinet/in.h>
#include <mstcpip.h>

#ifndef SOL_TCP
#define SOL_TCP     IPPROTO_TCP
#endif

#endif /* !__TE_WIN32_NETINET_TCP_H__ */
