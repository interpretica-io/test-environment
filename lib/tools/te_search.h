/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief The binary search trees of <search.h>
 *
 * @defgroup te_tools_te_search Binary search trees
 * @ingroup te_tools
 * @{
 *
 * <search.h> with tdestroy() of glibc on the systems which lack it,
 * such as macOS.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_SEARCH_H__
#define __TE_SEARCH_H__

#include "te_config.h"

#include <stddef.h>
#include <search.h>

#ifndef HAVE_TDESTROY

/* Every key is equal to the root one, so tdelete() takes the root */
static inline int
te_search_root_compare(const void *a, const void *b)
{
    (void)a;
    (void)b;
    return 0;
}

/**
 * Free a tree made by tsearch().
 *
 * @param root          The root of the tree.
 * @param free_node     The function to free a key, called once for
 *                      every key.
 */
static inline void
tdestroy(void *root, void (*free_node)(void *nodep))
{
    while (root != NULL)
    {
        /* A node starts with its key, as tsearch() returns it */
        void *key = *(void **)root;

        tdelete(key, &root, te_search_root_compare);
        free_node(key);
    }
}

#endif /* !HAVE_TDESTROY */

#endif /* !__TE_SEARCH_H__ */
/**@} <!-- END te_tools_te_search --> */
