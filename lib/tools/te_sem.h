/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief Semaphores private to a process
 *
 * @defgroup te_tools_te_sem Semaphores
 * @ingroup te_tools
 * @{
 *
 * Darwin does not implement the POSIX unnamed semaphores: @b sem_init()
 * always fails with @c ENOSYS there. te_sem_init() falls back to a named
 * semaphore that is unlinked as soon as it is created, so that nothing
 * is left in the system-wide namespace.
 *
 * @section te_tools_te_sem_example Example of usage
 *
 * @code
 * te_sem sem;
 *
 * CHECK_RC(te_sem_init(&sem, 1));
 * sem_wait(TE_SEM_PTR(&sem));
 * ...
 * sem_post(TE_SEM_PTR(&sem));
 * CHECK_RC(te_sem_destroy(&sem));
 * @endcode
 */
#ifndef __TE_TOOLS_SEM_H__
#define __TE_TOOLS_SEM_H__

#include "te_config.h"

#if HAVE_SEMAPHORE_H
#include <semaphore.h>
#endif

#include "te_errno.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Semaphore that is only used inside the process that created it */
typedef struct te_sem {
    sem_t  *ptr;        /**< Semaphore to operate on, @c NULL until
                             te_sem_init() succeeds */
    sem_t   embedded;   /**< Storage for an unnamed semaphore */
} te_sem;

/**
 * Get the POSIX semaphore behind @p sem_.
 *
 * All the plain POSIX operations, such as @b sem_wait(), @b sem_post()
 * and @b sem_trywait(), are applied to the result.
 */
#define TE_SEM_PTR(sem_)    ((sem_)->ptr)

/**
 * Create a semaphore.
 *
 * @param sem       Semaphore handle.
 * @param value     Initial value of the semaphore.
 *
 * @return Status code.
 */
extern te_errno te_sem_init(te_sem *sem, unsigned int value);

/**
 * Destroy a semaphore and release its resources.
 *
 * It is safe to call the function for a semaphore that has never been
 * created or has already been destroyed.
 *
 * @param sem       Semaphore handle.
 *
 * @return Status code.
 */
extern te_errno te_sem_destroy(te_sem *sem);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_TOOLS_SEM_H__ */
/**@} <!-- END te_tools_te_sem --> */
