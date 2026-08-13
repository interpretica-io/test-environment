/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief RCF Portable Command Handler
 *
 * Registry of configuration tree extensions provided by external
 * libraries linked into a Test Agent.
 *
 * An external library registers its configuration subtree
 * initializer from a constructor function, so that no Test Agent
 * source code modification is required:
 *
 * @code
 * static te_errno
 * my_conf_init(void)
 * {
 *     return rcf_pch_add_node("/agent", &node_my_subtree);
 * }
 *
 * TE_RCF_PCH_CONF_EXT(my_conf_init);
 * @endcode
 *
 * The library must be linked wholly into the agent (set
 * 'link_whole = true' in its meson.build), otherwise the linker
 * may drop the object file with the constructor.
 *
 * Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_RCF_PCH_CONF_EXT_H__
#define __TE_RCF_PCH_CONF_EXT_H__

#include "te_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type of a configuration tree extension initializer. */
typedef te_errno (*rcf_pch_conf_ext_init_fn)(void);

/**
 * Register a configuration tree extension initializer.
 *
 * Normally called from a constructor function before the agent
 * enters main(), see TE_RCF_PCH_CONF_EXT(). The initializer itself
 * is called by the agent from its configuration initialization
 * (after built-in subtrees are set up).
 *
 * @param name  Human-readable extension name (used in logs).
 * @param fn    Initializer to call.
 *
 * @return Status code.
 */
extern te_errno rcf_pch_conf_ext_register(const char *name,
                                          rcf_pch_conf_ext_init_fn fn);

/**
 * Call all registered extension initializers.
 *
 * Called by the Test Agent at the end of its configuration
 * initialization. Not intended for use by extensions.
 *
 * @return Status code (first failed initializer's one).
 */
extern te_errno rcf_pch_conf_ext_init_all(void);

/**
 * Register function @p fn_ as a configuration tree extension
 * initializer at program startup.
 */
#define TE_RCF_PCH_CONF_EXT(fn_)                                    \
    static __attribute__((constructor)) void                        \
    te_rcf_pch_conf_ext_ctor_##fn_(void)                            \
    {                                                               \
        (void)rcf_pch_conf_ext_register(#fn_, fn_);                 \
    }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__TE_RCF_PCH_CONF_EXT_H__ */
