/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief RCF Portable Command Handler
 *
 * Registry of configuration tree extensions provided by external
 * libraries linked into a Test Agent.
 *
 * Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER     "Conf Ext"

#include "te_config.h"

#include <stdlib.h>

#include "te_errno.h"
#include "te_queue.h"
#include "logger_api.h"

#include "rcf_pch_conf_ext.h"

/** Registered extension. */
typedef struct conf_ext {
    SLIST_ENTRY(conf_ext) links;    /**< List links */
    const char *name;               /**< Extension name */
    rcf_pch_conf_ext_init_fn fn;    /**< Initializer */
} conf_ext;

/*
 * Registrations come from constructor functions before main(),
 * i.e. strictly single-threaded, so no locking is needed.
 */
static SLIST_HEAD(, conf_ext) conf_exts = SLIST_HEAD_INITIALIZER(conf_exts);

/* See description in rcf_pch_conf_ext.h */
te_errno
rcf_pch_conf_ext_register(const char *name, rcf_pch_conf_ext_init_fn fn)
{
    conf_ext *ext;
    conf_ext *last = NULL;

    if (name == NULL || fn == NULL)
        return TE_RC(TE_RCF_PCH, TE_EINVAL);

    ext = calloc(1, sizeof(*ext));
    if (ext == NULL)
        return TE_RC(TE_RCF_PCH, TE_ENOMEM);

    ext->name = name;
    ext->fn = fn;

    /* Keep registration order: initialize in the order of linking */
    SLIST_FOREACH(last, &conf_exts, links)
    {
        if (SLIST_NEXT(last, links) == NULL)
            break;
    }
    if (last == NULL)
        SLIST_INSERT_HEAD(&conf_exts, ext, links);
    else
        SLIST_INSERT_AFTER(last, ext, links);

    return 0;
}

/* See description in rcf_pch_conf_ext.h */
te_errno
rcf_pch_conf_ext_init_all(void)
{
    const conf_ext *ext;
    te_errno rc;

    SLIST_FOREACH(ext, &conf_exts, links)
    {
        RING("Initializing configuration extension '%s'", ext->name);
        rc = ext->fn();
        if (rc != 0)
        {
            ERROR("Configuration extension '%s' failed to initialize: %r",
                  ext->name, rc);
            return rc;
        }
    }

    return 0;
}
