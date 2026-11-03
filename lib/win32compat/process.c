/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: processes
 *
 * A process ID may be reused as soon as the last handle of the process
 * is closed, so TE keeps the handle of every process it starts in a
 * registry until waitpid() reaps the process.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

#include <pthread.h>

/** Registered process */
typedef struct proc_entry {
    pid_t   pid;        /**< Process ID */
    HANDLE  handle;     /**< Process handle */
    HANDLE  job;        /**< Job of the process and its children or NULL */
    int     signo;      /**< Signal kill() terminated it with, or 0 */
} proc_entry;

/*
 * WaitForMultipleObjects() takes up to MAXIMUM_WAIT_OBJECTS handles, so
 * waitpid(-1) looks at that many processes at a time.
 */
#define PROC_MAX 1024

static proc_entry procs[PROC_MAX];
static unsigned int procs_num;
static pthread_mutex_t procs_lock = PTHREAD_MUTEX_INITIALIZER;

/** Exit code kill() makes a process terminate with */
#define KILL_EXIT_CODE_BASE 0x7e000000

/* See the description in te_win32_posix.h */
int
te_win32_process_register(pid_t pid, HANDLE handle, HANDLE job)
{
    int rc = 0;

    pthread_mutex_lock(&procs_lock);
    if (procs_num == PROC_MAX)
    {
        errno = EAGAIN;
        rc = -1;
    }
    else
    {
        procs[procs_num].pid = pid;
        procs[procs_num].handle = handle;
        procs[procs_num].job = job;
        procs[procs_num].signo = 0;
        procs_num++;
    }
    pthread_mutex_unlock(&procs_lock);

    return rc;
}

/** Find a registered process, call with procs_lock held */
static proc_entry *
proc_find(pid_t pid)
{
    unsigned int i;

    for (i = 0; i < procs_num; i++)
    {
        if (procs[i].pid == pid)
            return &procs[i];
    }
    return NULL;
}

/**
 * Get the status of an exited process and drop it from the registry.
 * Call with procs_lock held.
 */
static void
proc_reap(proc_entry *p, int *status)
{
    DWORD code = 0;

    GetExitCodeProcess(p->handle, &code);
    CloseHandle(p->handle);
    if (p->job != NULL)
        CloseHandle(p->job);

    if (status != NULL)
    {
        if (p->signo != 0)
            *status = p->signo & 0x7f;
        else
            *status = (int)((code & 0xff) << 8);
    }

    *p = procs[--procs_num];
}

/* See the description in te_win32_posix.h */
pid_t
waitpid(pid_t pid, int *status, int options)
{
    DWORD timeout = (options & WNOHANG) ? 0 : INFINITE;

    for (;;)
    {
        HANDLE handles[MAXIMUM_WAIT_OBJECTS];
        pid_t pids[MAXIMUM_WAIT_OBJECTS];
        unsigned int n = 0;
        unsigned int i;
        DWORD rc;

        pthread_mutex_lock(&procs_lock);
        for (i = 0; i < procs_num && n < MAXIMUM_WAIT_OBJECTS; i++)
        {
            if (pid == -1 || procs[i].pid == pid)
            {
                handles[n] = procs[i].handle;
                pids[n] = procs[i].pid;
                n++;
            }
        }
        pthread_mutex_unlock(&procs_lock);

        if (n == 0)
        {
            errno = ECHILD;
            return -1;
        }

        /*
         * With more processes than one wait takes, poll them in turns
         * instead of blocking on the first batch.
         */
        rc = WaitForMultipleObjects(n, handles, FALSE,
                                    (pid == -1 && procs_num > n &&
                                     timeout == INFINITE) ? 100 : timeout);
        if (rc == WAIT_TIMEOUT)
        {
            if (timeout == 0)
                return 0;
            continue;
        }
        if (rc >= WAIT_OBJECT_0 + n)
        {
            errno = te_win32_err2errno(GetLastError());
            return -1;
        }

        pthread_mutex_lock(&procs_lock);
        {
            proc_entry *p = proc_find(pids[rc - WAIT_OBJECT_0]);
            pid_t found = -1;

            /* Another thread may have reaped it meanwhile */
            if (p != NULL)
            {
                found = p->pid;
                proc_reap(p, status);
            }
            pthread_mutex_unlock(&procs_lock);

            if (found != -1)
                return found;
        }
    }
}

/* See the description in te_win32_posix.h */
int
kill(pid_t pid, int sig)
{
    proc_entry *p;
    HANDLE handle;
    DWORD code;
    bool group = false;
    int rc = 0;

    /* The job of a process TE has started is its process group */
    if (pid < -1)
    {
        pid = -pid;
        group = true;
    }

    if (pid <= 0 || sig < 0)
    {
        errno = EINVAL;
        return -1;
    }

    pthread_mutex_lock(&procs_lock);
    p = proc_find(pid);
    if (p != NULL)
        handle = p->handle;
    else
        handle = OpenProcess(PROCESS_TERMINATE |
                             PROCESS_QUERY_LIMITED_INFORMATION,
                             FALSE, (DWORD)pid);

    if (handle == NULL || !GetExitCodeProcess(handle, &code))
    {
        errno = ESRCH;
        rc = -1;
    }
    else if (code != STILL_ACTIVE)
    {
        /*
         * A registered process which has exited is a zombie: it still
         * exists for kill() until waitpid() reaps it.
         */
        if (p == NULL)
        {
            errno = ESRCH;
            rc = -1;
        }
    }
    else if (sig != 0)
    {
        /* Signal 0 checks for existence only, as POSIX defines */
        bool ok;

        if (group && p != NULL && p->job != NULL)
            ok = TerminateJobObject(p->job, KILL_EXIT_CODE_BASE | sig);
        else
            ok = TerminateProcess(handle, KILL_EXIT_CODE_BASE | sig);

        if (!ok)
        {
            errno = te_win32_err2errno(GetLastError());
            rc = -1;
        }
        else if (p != NULL)
        {
            p->signo = sig;
        }
    }

    pthread_mutex_unlock(&procs_lock);

    if (p == NULL && handle != NULL)
        CloseHandle(handle);
    return rc;
}
