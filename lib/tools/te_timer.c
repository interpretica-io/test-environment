/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Test API to operate the Timer service
 *
 * Functions to check if time expired
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#include "te_timer.h"
#ifdef HAVE_SIGNAL_H
#include <signal.h>
#endif
#include "logger_api.h"
#include "te_time.h"

#define TE_TIMER_CLOCKID    CLOCK_MONOTONIC

/**
 * Check an expression passed as the argument against zero.
 * If the expression is something not zero the macro reports an
 * error based on set errno, and returns errno converted to TE error code
 *
 * @param _expr     Expression to be checked
 */
#define TE_TIMER_CHECK_ERRNO(_expr) \
    do {                                                    \
        int _res = (_expr);                                 \
                                                            \
        if (_res != 0)                                      \
        {                                                   \
            te_errno _rc = te_rc_os2te(errno);              \
                                                            \
            ERROR("%s(): %s failed: %r",                    \
                  __func__, #_expr, _rc);                   \
            return _rc;                                     \
        }                                                   \
    } while (0)

#ifndef HAVE_TIMER_CREATE
/**
 * Set the moment when the timer is considered expired.
 *
 * It is used where POSIX.1 per-process timers are not available, i.e.
 * on Darwin. The timer is never used to deliver a notification, so
 * a monotonic deadline is enough.
 *
 * @param timer         Timer handle.
 * @param timeout_s     Timeout for triggering timer.
 *
 * @return @c 0 on success, @c -1 with @b errno set otherwise.
 */
static int
timer_set_deadline(te_timer_t *timer, unsigned int timeout_s)
{
    if (clock_gettime(TE_TIMER_CLOCKID, &timer->deadline) != 0)
        return -1;

    timer->deadline.tv_sec += timeout_s;

    return 0;
}
#endif

/* See description in te_timer.h */
te_errno
te_timer_start(te_timer_t *timer, unsigned int timeout_s)
{
#ifdef HAVE_TIMER_CREATE
    struct sigevent sev = { .sigev_notify = SIGEV_NONE, };
    struct itimerspec trigger;
#endif

    if (timer->is_valid)
    {
        ERROR("Timer is already in progress or initialized incorrectly");
        return TE_EINPROGRESS;
    }

#ifdef HAVE_TIMER_CREATE
    /* Create the timer */
    TE_TIMER_CHECK_ERRNO(timer_create(TE_TIMER_CLOCKID, &sev, &timer->id));
    timer->is_valid = true;

    /* Start the timer */
    trigger.it_value.tv_sec = timeout_s;
    trigger.it_value.tv_nsec = 0;
    trigger.it_interval.tv_sec = 0;
    trigger.it_interval.tv_nsec = 0;

    TE_TIMER_CHECK_ERRNO(timer_settime(timer->id, 0, &trigger, NULL));
#else
    TE_TIMER_CHECK_ERRNO(timer_set_deadline(timer, timeout_s));
    timer->is_valid = true;
#endif

    return 0;
}

/* See description in te_timer.h */
te_errno
te_timer_restart(te_timer_t *timer, unsigned int timeout_s)
{
#ifdef HAVE_TIMER_CREATE
    struct itimerspec trigger;
#endif

    if (!timer->is_valid)
    {
        ERROR("Timer is not running or initialized incorrectly");
        return TE_EINVAL;
    }

#ifdef HAVE_TIMER_CREATE
    /* Rearm the timer */
    trigger.it_value.tv_sec = timeout_s;
    trigger.it_value.tv_nsec = 0;
    trigger.it_interval.tv_sec = 0;
    trigger.it_interval.tv_nsec = 0;

    TE_TIMER_CHECK_ERRNO(timer_settime(timer->id, 0, &trigger, NULL));
#else
    TE_TIMER_CHECK_ERRNO(timer_set_deadline(timer, timeout_s));
#endif

    return 0;
}

/* See description in te_timer.h */
te_errno
te_timer_stop(te_timer_t *timer)
{
    if (timer->is_valid)
    {
#ifdef HAVE_TIMER_CREATE
        TE_TIMER_CHECK_ERRNO(timer_delete(timer->id));
#endif
        timer->is_valid = false;
    }

    return 0;
}

/* See description in te_timer.h */
te_errno
te_timer_expired(te_timer_t *timer)
{
#ifdef HAVE_TIMER_CREATE
    struct itimerspec remaining;
#else
    struct timespec now;
#endif

    if (!timer->is_valid)
    {
        ERROR("Timer is not running or initialized incorrectly");
        return TE_EINVAL;
    }

#ifdef HAVE_TIMER_CREATE
    TE_TIMER_CHECK_ERRNO(timer_gettime(timer->id, &remaining));
    if (remaining.it_value.tv_sec == 0 && remaining.it_value.tv_nsec == 0)
        return TE_ETIMEDOUT;
#else
    TE_TIMER_CHECK_ERRNO(clock_gettime(TE_TIMER_CLOCKID, &now));
    if (now.tv_sec > timer->deadline.tv_sec ||
        (now.tv_sec == timer->deadline.tv_sec &&
         now.tv_nsec >= timer->deadline.tv_nsec))
    {
        return TE_ETIMEDOUT;
    }
#endif

    return 0;
}
