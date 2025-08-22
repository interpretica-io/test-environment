/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved. */
/** @file
 * @brief WiFi agent library
 *
 * Basic WiFi tree implementation
 */

#ifndef __TA_WIFI_H__
#define __TA_WIFI_H__

#include "te_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize WiFi configuration.
 *
 * @return Status code.
 */
extern te_errno ta_unix_conf_wifi_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TA_WIFI_H__ */
