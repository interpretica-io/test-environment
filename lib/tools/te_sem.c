/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief Semaphores private to a process
 *
 * Implementation of the semaphores that work on the systems without
 * POSIX unnamed semaphores.
 */

#define TE_LGR_USER     "TE Semaphore"

#include "te_config.h"

#if HAVE_ERRNO_H
#include <errno.h>
#endif
#if HAVE_FCNTL_H
#include <fcntl.h>
#endif
#if HAVE_STDINT_H
#include <stdint.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "logger_api.h"
#include "te_sem.h"
#include "te_str.h"

/*
 * Darwin limits the name of a semaphore to PSEMNAMLEN, which is 31
 * characters, so the name is kept as short as it can be while staying
 * unique: no two live semaphores of one process share an address.
 */
#define TE_SEM_NAME_FMT     "/te-%jx-%jx"

/* See description in te_sem.h */
te_errno
te_sem_init(te_sem *sem, unsigned int value)
{
    char name[32];

    sem->ptr = NULL;

    if (sem_init(&sem->embedded, 0, value) == 0)
    {
        sem->ptr = &sem->embedded;
        return 0;
    }

    if (errno != ENOSYS)
    {
        te_errno rc = te_rc_os2te(errno);

        ERROR("%s(): sem_init() failed: %r", __func__, rc);
        return rc;
    }

    /* The platform has no unnamed semaphores, fall back to a named one. */
    TE_SPRINTF(name, TE_SEM_NAME_FMT, (uintmax_t)getpid(),
               (uintmax_t)(uintptr_t)sem);

    sem->ptr = sem_open(name, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, value);
    if (sem->ptr == SEM_FAILED)
    {
        te_errno rc = te_rc_os2te(errno);

        sem->ptr = NULL;
        ERROR("%s(): sem_open(%s) failed: %r", __func__, name, rc);
        return rc;
    }

    /*
     * The semaphore remains usable through the descriptor, so remove
     * the name at once to leave nothing behind if the process dies.
     */
    if (sem_unlink(name) != 0)
        WARN("%s(): sem_unlink(%s) failed: %r", __func__, name,
             te_rc_os2te(errno));

    return 0;
}

/* See description in te_sem.h */
te_errno
te_sem_destroy(te_sem *sem)
{
    int rc;

    if (sem->ptr == NULL)
        return 0;

    rc = (sem->ptr == &sem->embedded) ? sem_destroy(sem->ptr) :
                                        sem_close(sem->ptr);
    sem->ptr = NULL;

    if (rc != 0)
    {
        te_errno te_rc = te_rc_os2te(errno);

        ERROR("%s(): failed to destroy the semaphore: %r", __func__, te_rc);
        return te_rc;
    }

    return 0;
}
