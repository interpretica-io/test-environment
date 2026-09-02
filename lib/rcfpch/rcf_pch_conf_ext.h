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
 * @note The order in which the initializers are called is
 *       unspecified: it follows the order in which the constructors
 *       run, which depends on the linker and on the way the
 *       libraries are linked. Extensions must not depend on each
 *       other or on being initialized first or last. All built-in
 *       subtrees are set up before any extension is initialized.
 *
 * Copyright (C) 2026 OKTET Ltd.
 */

#ifndef __TE_RCF_PCH_CONF_EXT_H__
#define __TE_RCF_PCH_CONF_EXT_H__

#include "te_errno.h"
#include "te_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Type of a configuration tree extension initializer. */
typedef te_errno (*rcf_pch_conf_ext_init_fn)(void);

/**
 * Registration entry of a configuration tree extension.
 *
 * It is defined by TE_RCF_PCH_CONF_EXT() as a static object with
 * the lifetime of the program, so that registration never needs to
 * allocate and thus cannot fail. It is not meant to be filled in or
 * inspected directly.
 */
typedef struct rcf_pch_conf_ext {
    TAILQ_ENTRY(rcf_pch_conf_ext) links; /**< List links */
    const char *name;                    /**< Extension name */
    rcf_pch_conf_ext_init_fn init;       /**< Initializer */
} rcf_pch_conf_ext;

/**
 * Register a configuration tree extension.
 *
 * Normally called from a constructor function before the agent
 * enters main(), see TE_RCF_PCH_CONF_EXT(). The initializer itself
 * is called by the agent from its configuration initialization
 * (after built-in subtrees are set up).
 *
 * @param ext   Registration entry with a static lifetime; it is
 *              linked into the registry as is, so it must not be
 *              a local or a freed object.
 */
extern void rcf_pch_conf_ext_register(rcf_pch_conf_ext *ext);

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
    static rcf_pch_conf_ext te_rcf_pch_conf_ext_##fn_ = {           \
        .name = #fn_,                                               \
        .init = fn_,                                                \
    };                                                              \
                                                                    \
    static __attribute__((constructor)) void                        \
    te_rcf_pch_conf_ext_ctor_##fn_(void)                            \
    {                                                               \
        rcf_pch_conf_ext_register(&te_rcf_pch_conf_ext_##fn_);      \
    }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* !__TE_RCF_PCH_CONF_EXT_H__ */
