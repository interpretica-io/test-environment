/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief glob.h for a native Windows host
 *
 * Wildcards are supported in the last path component only.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_GLOB_H__
#define __TE_WIN32_GLOB_H__

#include <stddef.h>

#define GLOB_ERR        0x01
#define GLOB_MARK       0x02
#define GLOB_NOSORT     0x04
#define GLOB_NOCHECK    0x08
#define GLOB_NOESCAPE   0x10

#define GLOB_NOSPACE    1
#define GLOB_ABORTED    2
#define GLOB_NOMATCH    3

typedef struct {
    size_t  gl_pathc;   /**< Number of matched paths */
    char  **gl_pathv;   /**< Matched paths, NULL-terminated */
    size_t  gl_offs;    /**< Not supported, must be 0 */
} glob_t;

extern int glob(const char *pattern, int flags,
                int (*errfunc)(const char *epath, int eerrno),
                glob_t *pglob);
extern void globfree(glob_t *pglob);

#endif /* !__TE_WIN32_GLOB_H__ */
