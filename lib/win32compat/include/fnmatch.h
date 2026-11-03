/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief fnmatch.h for a native Windows host
 *
 * Shell wildcard matching, see fnmatch(3).
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_FNMATCH_H__
#define __TE_WIN32_FNMATCH_H__

#define FNM_NOMATCH     1

#define FNM_NOESCAPE    0x01
#define FNM_PATHNAME    0x02
#define FNM_PERIOD      0x04

extern int fnmatch(const char *pattern, const char *string, int flags);

#endif /* !__TE_WIN32_FNMATCH_H__ */
