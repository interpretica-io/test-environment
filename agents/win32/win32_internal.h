/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Windows Test Agent
 *
 * Definitions shared by the sources of the Windows Test Agent.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_AGENTS_WIN32_INTERNAL_H__
#define __TE_AGENTS_WIN32_INTERNAL_H__

#include "te_config.h"

#include <winsock2.h>
#include <iphlpapi.h>

#include "te_errno.h"
#include "rcf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Test Agent name */
extern const char *ta_name;

/** Directory of the agent executable */
extern char ta_dir[RCF_MAX_PATH];

/** Directory for temporary files */
extern char ta_tmp_dir[RCF_MAX_PATH];

/**
 * Get the index of an interface.
 *
 * @param ifname    NDIS interface name, e.g. "ethernet_32769"
 * @param ifindex   location for the index
 *
 * @return Status code.
 */
extern te_errno win32_if_index(const char *ifname, NET_IFINDEX *ifindex);

/**
 * Get the NDIS name of an interface.
 *
 * @param luid      interface LUID
 * @param ifname    location for the name
 * @param len       size of the location
 *
 * @return Status code.
 */
extern te_errno win32_if_name(const NET_LUID *luid, char *ifname,
                              size_t len);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_AGENTS_WIN32_INTERNAL_H__ */
