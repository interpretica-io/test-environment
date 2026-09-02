/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief RCF Portable Command Handler
 *
 * Registry of configuration tree extensions provided by external
 * libraries linked into a Test Agent.
 *
 * Copyright (C) 2026 OKTET Ltd.
 */

#define TE_LGR_USER     "RCF PCH CONF EXT"

#include "te_config.h"

#include <assert.h>

#include "te_errno.h"
#include "te_queue.h"
#include "logger_api.h"

#include "rcf_pch_conf_ext.h"

/*
 * Registrations come from constructor functions before main(),
 * i.e. strictly single-threaded, so no locking is needed.
 */
static TAILQ_HEAD(, rcf_pch_conf_ext) conf_exts =
    TAILQ_HEAD_INITIALIZER(conf_exts);

/* See description in rcf_pch_conf_ext.h */
void
rcf_pch_conf_ext_register(rcf_pch_conf_ext *ext)
{
    assert(ext != NULL);
    assert(ext->name != NULL);
    assert(ext->init != NULL);

    TAILQ_INSERT_TAIL(&conf_exts, ext, links);
}

/* See description in rcf_pch_conf_ext.h */
te_errno
rcf_pch_conf_ext_init_all(void)
{
    rcf_pch_conf_ext *ext;

    TAILQ_FOREACH(ext, &conf_exts, links)
    {
        te_errno rc;

        RING("Initializing configuration extension '%s'", ext->name);
        rc = ext->init();
        if (rc != 0)
        {
            ERROR("Configuration extension '%s' failed to initialize: %r",
                  ext->name, rc);
            return rc;
        }
    }

    return 0;
}
