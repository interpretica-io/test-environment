/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief net/if.h for a native Windows host
 *
 * Interface names and flags of the IP Helper API. A Windows interface
 * name is the alias the user sees, such as "Ethernet 2".
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_NET_IF_H__
#define __TE_WIN32_NET_IF_H__

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>

#ifndef IFNAMSIZ
#define IFNAMSIZ    IF_NAMESIZE
#endif

#endif /* !__TE_WIN32_NET_IF_H__ */
