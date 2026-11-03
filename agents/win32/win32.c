/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Windows Test Agent
 *
 * Windows Test Agent implementation.
 *
 * Windows has no fork(), so a routine which RCF asks to run in a
 * process of its own is run by a new instance of the agent executable:
 * "ta.exe exec <routine> <arguments>". The other parts follow the Unix
 * Test Agent.
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER      "Main"

#include "te_config.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include "te_alloc.h"
#include "te_defs.h"
#include "te_errno.h"
#include "te_sem.h"
#include "te_sleep.h"
#include "te_str.h"
#include "te_shell_cmd.h"
#include "te_string.h"
#include "te_kernel_log.h"
#include "te_exec_child.h"
#include "comm_agent.h"
#include "rcf_ch_api.h"
#include "rcf_pch.h"
#include "logger_api.h"
#include "logger_file.h"
#include "logger_ta.h"
#include "logfork.h"
#include "ta_common.h"
#include "agentlib.h"

#include "win32_internal.h"

/** Send answer to the TEN */
#define SEND_ANSWER(_fmt...) \
    do {                                                                \
        int _rc;                                                        \
                                                                        \
        if ((size_t)snprintf(cbuf + answer_plen, buflen - answer_plen,  \
                             _fmt) >= (buflen - answer_plen))           \
        {                                                               \
            VERB("answer is truncated\n");                              \
        }                                                               \
        RCF_CH_LOCK;                                                    \
        _rc = rcf_comm_agent_reply(handle, cbuf, strlen(cbuf) + 1);     \
        RCF_CH_UNLOCK;                                                  \
        return _rc;                                                     \
    } while (false)

/** Test Agent executable name */
char ta_execname_storage[RCF_MAX_PATH];
const char *ta_execname = ta_execname_storage;

/** Test Agent name */
const char *ta_name = "(win32)";
/** Test Agent data and binaries location */
char ta_dir[RCF_MAX_PATH];
/** Directory for temporary files */
char ta_tmp_dir[RCF_MAX_PATH];
/** Whether the agent has created ta_tmp_dir and has to remove it */
static bool ta_tmp_dir_own = false;

/** Directory for the resource locks shared by the agents of the host */
static char ta_lockdir[RCF_MAX_PATH] = ".";
const char *te_lockdir = ta_lockdir;

#if defined(ENABLE_GENERATED_SYMTBL)
extern rcf_symbol_entry generated_table[];
#endif

static rcf_symbol_entry essential_symbols[] = {
   {.name = "waitpid",      .addr = (void *)waitpid,      .is_func = true},
   {.name = "te_shell_cmd", .addr = (void *)te_shell_cmd, .is_func = true},
   {.name = NULL, .addr = NULL}
};

/*
 * Test coverage estimation hooks the RCF PCH calls if they are set.
 * Coverage is not collected on Windows.
 */
int (*tce_stop_function)(void);
int (*tce_notify_function)(void);
int (*tce_get_peer_function)(void);
const char *(*tce_get_conn_function)(void);

/** Tasks to be killed during TA shutdown */
static unsigned int     tasks_len = 0;
static unsigned int     tasks_index = 0;
static pid_t           *tasks = NULL;

static pthread_mutex_t ta_lock = PTHREAD_MUTEX_INITIALIZER;

/** Remember a task to kill it on shutdown */
static void
store_pid(pid_t pid)
{
    unsigned int i;

    for (i = 0; i < tasks_index; i++)
    {
        if (tasks[i] == 0)
        {
            tasks[i] = pid;
            return;
        }
    }

    if (tasks_index == tasks_len)
    {
        pid_t *new_tasks = realloc(tasks,
                                   (tasks_len + 32) * sizeof(*tasks));

        if (new_tasks == NULL)
        {
            ERROR("Out of memory, PID %d will not be killed on shutdown",
                  (int)pid);
            return;
        }
        tasks = new_tasks;
        tasks_len += 32;
    }
    tasks[tasks_index++] = pid;
}

/** Kill all the tasks started by rcf_ch_start_process() */
static void
kill_tasks(void)
{
    unsigned int i;

    for (i = 0; i < tasks_index; i++)
    {
        if (tasks[i] != 0)
            ta_kill_and_wait(tasks[i], SIGKILL, 1);
    }
    free(tasks);
    tasks = NULL;
    tasks_len = tasks_index = 0;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_init(void)
{
    return 0;
}

/* See description in rcf_ch_api.h */
void
rcf_ch_lock(void)
{
    pthread_mutex_lock(&ta_lock);
}

/* See description in rcf_ch_api.h */
void
rcf_ch_unlock(void)
{
    if (pthread_mutex_trylock(&ta_lock) == 0)
    {
        WARN("rcf_ch_unlock() without rcf_ch_lock()!\n"
             "It may happen in the case of asynchronous cancellation.");
    }
    pthread_mutex_unlock(&ta_lock);
}

/* See description in rcf_ch_api.h */
int
rcf_ch_reboot(struct rcf_comm_connection *handle,
              char *cbuf, size_t buflen, size_t answer_plen,
              const uint8_t *ba, size_t cmdlen, const char *params)
{
    size_t len = answer_plen;

    UNUSED(ba);
    UNUSED(cmdlen);
    UNUSED(params);

    len += snprintf(cbuf + answer_plen, buflen - answer_plen, "0") + 1;
    RCF_CH_LOCK;
    rcf_comm_agent_reply(handle, cbuf, len);
    RCF_CH_UNLOCK;

    ta_system("shutdown /r /t 0");
    return 0;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_configure(struct rcf_comm_connection *handle,
                 char *cbuf, size_t buflen, size_t answer_plen,
                 const uint8_t *ba, size_t cmdlen,
                 rcf_ch_cfg_op_t op, const char *oid, const char *val)
{
    UNUSED(handle);
    UNUSED(cbuf);
    UNUSED(buflen);
    UNUSED(answer_plen);
    UNUSED(ba);
    UNUSED(cmdlen);

    /* Standard handler is OK */
    VERB("Configure: op %d OID <%s> val <%s>\n", op,
         oid == NULL ? "" : oid, val == NULL ? "" : val);
    return -1;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_vread(struct rcf_comm_connection *handle,
             char *cbuf, size_t buflen, size_t answer_plen,
             rcf_var_type_t type, const char *var)
{
    if (type == RCF_STRING && strcmp(var, "rcf_consistency_checks") == 0)
    {
        /* te_rcf_consistency_checks generates the value */
        extern const char * const rcf_consistency_checks;

        SEND_ANSWER("0 %s", rcf_consistency_checks);
        return 0;
    }

    /* Standard handler is OK */
    return -1;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_vwrite(struct rcf_comm_connection *handle,
              char *cbuf, size_t buflen, size_t answer_plen,
              rcf_var_type_t type, const char *var, ...)
{
    UNUSED(handle);
    UNUSED(cbuf);
    UNUSED(buflen);
    UNUSED(answer_plen);
    UNUSED(type);
    UNUSED(var);

    /* Standard handler is OK */
    return -1;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_file(struct rcf_comm_connection *handle,
            char *cbuf, size_t buflen, size_t answer_plen,
            const uint8_t *ba, size_t cmdlen,
            rcf_op_t op, const char *filename)
{
    UNUSED(handle);
    UNUSED(cbuf);
    UNUSED(buflen);
    UNUSED(answer_plen);
    UNUSED(ba);
    UNUSED(cmdlen);
    UNUSED(op);
    UNUSED(filename);

    /* Standard handler is OK */
    return -1;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_call(struct rcf_comm_connection *handle,
            char *cbuf, size_t buflen, size_t answer_plen,
            const char *rtn, bool is_argv, int argc, void **params)
{
    UNUSED(handle);
    UNUSED(cbuf);
    UNUSED(buflen);
    UNUSED(answer_plen);
    UNUSED(rtn);
    UNUSED(is_argv);
    UNUSED(argc);
    UNUSED(params);

    /* Standard handler is OK */
    return -1;
}

/**
 * Start a program with arguments.
 *
 * @param pid       location for the process ID
 * @param priority  niceness of the process
 * @param file      program to run
 * @param argv0     first arguments, @c NULL-terminated
 * @param argc      number of the arguments in @p params
 * @param params    the rest of the arguments
 *
 * @return Status code.
 */
static te_errno
start_program(pid_t *pid, int priority, const char *file,
              const char * const *argv0, int argc, void **params)
{
    te_exec_priority_param prio = { .priority = priority };
    te_exec_param exec_param[] = {
        { .type = TE_EXEC_PRIORITY, .data = &prio },
        { .type = TE_EXEC_END, .data = NULL },
    };
    const char **argv;
    int n0 = 0;
    int i;

    while (argv0[n0] != NULL)
        n0++;

    argv = TE_ALLOC((n0 + argc + 1) * sizeof(*argv));
    for (i = 0; i < n0; i++)
        argv[i] = argv0[i];
    for (i = 0; i < argc; i++)
        argv[n0 + i] = params[i];
    argv[n0 + argc] = NULL;

    *pid = te_exec_child(file, (char * const *)argv, NULL, (uid_t)-1,
                         NULL, NULL, NULL,
                         priority != 0 ? exec_param : NULL);
    free(argv);

    if (*pid < 0)
    {
        te_errno rc = TE_OS_RC(TE_TA_WIN32, errno);

        ERROR("Failed to start '%s': %r", file, rc);
        return rc;
    }

    store_pid(*pid);
    return 0;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_start_process(pid_t *pid, int priority, const char *rtn,
                     bool do_exec, int argc, void **params)
{
    const char *provider = rcf_pch_rpc_get_provider();

    if (*provider != '\0' && strcmp(rtn, "rcf_pch_rpc_server_argv") == 0)
    {
        const char *argv0[] = { provider, NULL };

        return start_program(pid, priority, provider, argv0, argc, params);
    }

    if (rcf_ch_symbol_addr(rtn, true) != NULL)
    {
        const char *argv0[] = { ta_execname, "exec", rtn, NULL };

        /*
         * The routine runs in a new instance of the agent, so only
         * the string arguments of the argv form can be passed to it.
         */
        if (!do_exec)
        {
            ERROR("Routine '%s' has to be run with argv arguments on "
                  "Windows", rtn);
            return TE_RC(TE_TA_WIN32, TE_EOPNOTSUPP);
        }
        return start_program(pid, priority, ta_execname, argv0,
                             argc, params);
    }

    /* Try a program from PATH */
    if (do_exec || argc == 0)
    {
        char path[MAX_PATH];
        const char *argv0[] = { rtn, NULL };

        if (SearchPathA(NULL, rtn, ".exe", sizeof(path), path, NULL) == 0)
            return TE_RC(TE_TA_WIN32, TE_ENOENT);

        return start_program(pid, priority, path, argv0, argc, params);
    }

    return TE_RC(TE_TA_WIN32, TE_ENOENT);
}

struct rcf_thread_parameter {
    bool        active;
    pthread_t   id;
    void       *addr;
    bool        is_argv;
    int         argc;
    void      **params;
    te_errno    rc;
    bool        sem_created;
    te_sem      params_processed;
};

#define TA_MAX_THREADS 16
static struct rcf_thread_parameter thread_pool[TA_MAX_THREADS];
static pthread_mutex_t thread_pool_mutex = PTHREAD_MUTEX_INITIALIZER;

static void *
rcf_ch_thread_wrapper(void *arg)
{
    struct rcf_thread_parameter *parm = arg;
    sem_t *sem = TE_SEM_PTR(&parm->params_processed);

    if (parm->is_argv)
    {
        parm->rc = ((rcf_argv_thr_rtn)(parm->addr))(sem, parm->argc,
                                                    (char **)parm->params);
    }
    else
    {
        parm->rc = ((rcf_thr_rtn)(parm->addr))(sem,
                                               parm->params[0],
                                               parm->params[1],
                                               parm->params[2],
                                               parm->params[3],
                                               parm->params[4],
                                               parm->params[5],
                                               parm->params[6],
                                               parm->params[7],
                                               parm->params[8],
                                               parm->params[9]);
    }
    VERB("thread is terminating");
    pthread_mutex_lock(&thread_pool_mutex);
    parm->active = false;
    pthread_mutex_unlock(&thread_pool_mutex);
    return NULL;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_start_thread(int *tid, int priority, const char *rtn, bool is_argv,
                    int argc, void **params)
{
    void *addr = rcf_ch_symbol_addr(rtn, true);
    struct rcf_thread_parameter *iter;

    UNUSED(priority);

    if (addr == NULL)
        return TE_RC(TE_TA_WIN32, TE_ENOENT);

    VERB("start thread with entry point '%s'", rtn);

    pthread_mutex_lock(&thread_pool_mutex);
    for (iter = thread_pool; iter < thread_pool + TA_MAX_THREADS; iter++)
    {
        int rc;

        if (iter->active)
            continue;

        iter->addr = addr;
        iter->argc = argc;
        iter->is_argv = is_argv;
        iter->params = params;
        iter->rc = 0;
        if (!iter->sem_created)
        {
            if (te_sem_init(&iter->params_processed, 0) != 0)
            {
                pthread_mutex_unlock(&thread_pool_mutex);
                return TE_RC(TE_TA_WIN32, TE_EFAIL);
            }
            iter->sem_created = true;
        }
        if ((rc = pthread_create(&iter->id, NULL,
                                 rcf_ch_thread_wrapper, iter)) != 0)
        {
            pthread_mutex_unlock(&thread_pool_mutex);
            return TE_OS_RC(TE_TA_WIN32, rc);
        }
        iter->active = true;
        sem_wait(TE_SEM_PTR(&iter->params_processed));
        pthread_mutex_unlock(&thread_pool_mutex);
        *tid = (int)(iter - thread_pool);
        return 0;
    }
    pthread_mutex_unlock(&thread_pool_mutex);

    return TE_RC(TE_TA_WIN32, TE_ETOOMANY);
}

/** Kill all the threads started by rcf_ch_start_thread() */
static void
kill_threads(void)
{
    struct rcf_thread_parameter *iter;

    for (iter = thread_pool; iter < thread_pool + TA_MAX_THREADS; iter++)
    {
        if (iter->active)
        {
            pthread_cancel(iter->id);
            pthread_join(iter->id, NULL);
        }
    }
}

/* See description in rcf_ch_api.h */
int
rcf_ch_kill_process(unsigned int pid)
{
    unsigned int i;
    pid_t p = pid;

    /* A task the agent has started is killed with its children */
    for (i = 0; i < tasks_index; i++)
    {
        if (tasks[i] == (pid_t)pid)
        {
            tasks[i] = 0;
            p = -(pid_t)pid;
            break;
        }
    }

    if (kill(p, SIGKILL) != 0)
    {
        te_errno rc = TE_OS_RC(TE_TA_WIN32, errno);

        ERROR("Failed to kill process with PID=%u: %r", pid, rc);
        return rc;
    }
    RING("Killed PID=%u", pid);

    return 0;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_free_proc_data(unsigned int pid)
{
    unsigned int i;

    for (i = 0; i < tasks_index; i++)
    {
        if (tasks[i] == (pid_t)pid)
        {
            tasks[i] = 0;
            break;
        }
    }
    return 0;
}

/* See description in rcf_ch_api.h */
int
rcf_ch_kill_thread(unsigned int tid)
{
    if (tid >= TA_MAX_THREADS || !thread_pool[tid].active)
        return TE_RC(TE_TA_WIN32, TE_ESRCH);

    if (pthread_cancel(thread_pool[tid].id) != 0 ||
        pthread_join(thread_pool[tid].id, NULL) != 0)
        return TE_OS_RC(TE_TA_WIN32, errno);
    return 0;
}

/**
 * Routine to be executed remotely to run any program from shell.
 *
 * @param argc      number of arguments in array
 * @param argv      array with pointer to string arguments
 *
 * @return Status code.
 */
int
shell(int argc, char * const argv[])
{
    te_string cmd = TE_STRING_INIT;
    int status;
    int i;

    for (i = 0; i < argc; i++)
        te_string_append(&cmd, "%s%s", i == 0 ? "" : " ", argv[i]);

    status = ta_system(cmd.ptr != NULL ? cmd.ptr : "");
    te_string_free(&cmd);

    if (status == -1)
        return TE_OS_RC(TE_TA_WIN32, errno);
    if (!WIFEXITED(status))
        ERROR("Abnormal termination of command executed in shell");

    return TE_RC(TE_TA_WIN32, WEXITSTATUS(status));
}

/**
 * Create a file with specified size filled by the specified pattern.
 *
 * @return 0 (success) or system error
 */
int
create_data_file(char *pathname, char c, int len)
{
    char  buf[1024];
    FILE *f;

    if ((f = fopen(pathname, "wb")) == NULL)
    {
        ERROR("fopen(\"%s\") failed: errno %d", pathname, errno);
        return TE_OS_RC(TE_TA_WIN32, errno);
    }

    memset(buf, c, sizeof(buf));

    while (len > 0)
    {
        size_t copy_len = ((size_t)len > sizeof(buf)) ? sizeof(buf) : len;

        if (fwrite(buf, 1, copy_len, f) != copy_len)
        {
            int err = errno;

            ERROR("fwrite() failed: errno %d", err);
            fclose(f);
            return TE_OS_RC(TE_TA_WIN32, err);
        }
        len -= copy_len;
    }

    if (fclose(f) != 0)
    {
        ERROR("fclose() failed: errno %d", errno);
        return TE_OS_RC(TE_TA_WIN32, errno);
    }

    return 0;
}

/* Declarations of routines to be linked with the agent */
int
ta_rtn_unlink(char *arg)
{
    int rc = unlink(arg);

    VERB("%s(): arg=%s rc=%d errno=%d", __FUNCTION__,
         (arg == NULL) ? "(null)" : arg, rc, errno);
    return (rc == 0) ? 0 : TE_OS_RC(TE_TA_WIN32, errno);
}

/**
 * Signal handler to be registered for SIGINT signal.
 *
 * @note It is declared as non-static to be visible in TA symbol table.
 */
/* static, see above */ void
ta_sigint_handler(int sig)
{
    UNUSED(sig);

    /*
     * We can't log here using TE logging facilities, but we need
     * to make a mark, that TA was killed.
     */
    fprintf(stderr, "Test Agent killed by SIGINT\n");
    exit(EXIT_FAILURE);
}

int
rcf_ch_shutdown(struct rcf_comm_connection *handle,
                char *cbuf, size_t buflen, size_t answer_plen)
{
    int rc;

    UNUSED(handle);
    UNUSED(cbuf);
    UNUSED(buflen);
    UNUSED(answer_plen);

    (void)signal(SIGINT, SIG_DFL);

    /* Reaping the children logs, so the log goes down after them */
    kill_tasks();
    kill_threads();

    rc = ta_log_shutdown();
    if (rc != 0)
        fprintf(stderr, "ta_log_shutdown() failed: error=0x%X\n", rc);

    return -1; /* Call default callback as well */
}

/**
 * Get identifier of the current thread.
 *
 * @return Thread identifier
 */
uint32_t
thread_self(void)
{
    return GetCurrentThreadId();
}

/**
 * Create a mutex.
 *
 * @return Mutex handle
 */
void *
thread_mutex_create(void)
{
    pthread_mutex_t *mutex = TE_ALLOC(sizeof(*mutex));

    pthread_mutex_init(mutex, NULL);
    return mutex;
}

/**
 * Destroy a mutex.
 *
 * @param mutex     mutex handle
 */
void
thread_mutex_destroy(void *mutex)
{
    if (mutex != NULL)
        pthread_mutex_destroy(mutex);
    free(mutex);
}

/**
 * Lock the mutex.
 *
 * @param mutex     mutex handle
 */
void
thread_mutex_lock(void *mutex)
{
    if (mutex == NULL)
        ERROR("%s: try to lock NULL mutex", __FUNCTION__);
    else
        pthread_mutex_lock(mutex);
}

/**
 * Unlock the mutex.
 *
 * @param mutex     mutex handle
 */
void
thread_mutex_unlock(void *mutex)
{
    if (mutex == NULL)
        ERROR("%s: try to unlock NULL mutex", __FUNCTION__);
    else
        pthread_mutex_unlock(mutex);
}

/* See description in ta_common.h */
int
rcf_rpc_server_init(void)
{
    return 0;
}

/* See description in ta_common.h */
int
rcf_rpc_server_finalize(void)
{
    return 0;
}

/**
 * Remove a directory with its contents.
 *
 * @param path      directory path
 *
 * @return @c 0 or @c -1.
 */
static int
remove_tree(const char *path)
{
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    HANDLE h;
    int rc = 0;

    if (snprintf(pattern, sizeof(pattern), "%s\\*", path) >=
        (int)sizeof(pattern))
        return -1;

    h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do {
            char child[MAX_PATH];

            if (strcmp(fd.cFileName, ".") == 0 ||
                strcmp(fd.cFileName, "..") == 0)
                continue;

            /* A truncated path names another file, leave it alone */
            if (snprintf(child, sizeof(child), "%s\\%s", path,
                         fd.cFileName) >= (int)sizeof(child))
            {
                rc = -1;
                continue;
            }

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (remove_tree(child) != 0)
                    rc = -1;
            }
            else
            {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(child))
                    rc = -1;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }

    if (!RemoveDirectoryA(path))
        rc = -1;
    return rc;
}

static void
log_init(void)
{
    te_log_init("(win32)", te_log_message_file);
}

/**
 * Entry point of the Windows Test Agent.
 *
 * Usage:
 *     ta.exe <ta_name> <communication library configuration string>
 *     ta.exe exec <routine> [<arguments>]
 */
int
main(int argc, char **argv)
{
    pthread_t logfork_tid;
    char buf[16];
    char *tmp;
    int retval = 0;
    int rc;

    log_init();

    te_kernel_log_set_system_func(&ta_system);

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc < 3)
    {
        fprintf(stderr, "Invalid number of arguments\n");
        return -1;
    }

    /* argv[0] may lack the directory and the extension */
    if (GetModuleFileNameA(NULL, ta_execname_storage,
                           sizeof(ta_execname_storage)) == 0)
        te_strlcpy(ta_execname_storage, argv[0],
                   sizeof(ta_execname_storage));

    te_strlcpy(ta_dir, ta_execname, sizeof(ta_dir));
    tmp = strrchr(ta_dir, '\\');
    if (tmp == NULL)
        tmp = strrchr(ta_dir, '/');
    if (tmp == NULL)
        te_strlcpy(ta_dir, ".", sizeof(ta_dir));
    else
        *tmp = '\0';

    /*
     * Create 'tmp' directory inside TA directory and change current
     * working directory to it in order to create all temporary files
     * there by default. It is removed on TA shutdown.
     */
    TE_SPRINTF(ta_tmp_dir, "%s\\tmp", ta_dir);
    if (mkdir(ta_tmp_dir, 0777) == 0 || errno == EEXIST)
    {
        ta_tmp_dir_own = true;
    }
    else
    {
        const char *sys_tmp = getenv("TEMP");

        fprintf(stderr, "Failed to create tmp directory inside TA "
                "directory, fallback to %%TEMP%%\n");
        te_strlcpy(ta_tmp_dir, sys_tmp != NULL ? sys_tmp : ".",
                   sizeof(ta_tmp_dir));
    }

    if (chdir(ta_tmp_dir) != 0)
    {
        fprintf(stderr, "Failed to change current directory to '%s'\n",
                ta_tmp_dir);
        /* Continue */
    }

    /* %TEMP% of the user, which the agents of the user share */
    if (GetTempPathA(sizeof(ta_lockdir), ta_lockdir) == 0)
        te_strlcpy(ta_lockdir, ta_tmp_dir, sizeof(ta_lockdir));

    rcf_ch_register_symbol_table(essential_symbols);
#if defined(ENABLE_GENERATED_SYMTBL)
    rcf_ch_register_symbol_table(generated_table);
#endif

    if (getenv("TE_LEAVE_SIGINT_HANDLER") == NULL)
        (void)signal(SIGINT, ta_sigint_handler);

    rc = ta_process_mgmt_init();
    if (rc != 0)
    {
        LOG_PRINT("Cannot initialize process management: %d", rc);
        return rc;
    }

    if (strcmp(argv[1], "exec") == 0)
    {
        void (*func)(int, char **);

        /* A routine run in a process of its own */
        te_log_init(NULL, logfork_log_message);

        func = rcf_ch_symbol_addr(argv[2], true);
        if (func == NULL)
        {
            LOG_PRINT("Cannot resolve address of the function %s",
                      argv[2]);
            return 1;
        }
        logfork_register_user(argv[2]);
        func(argc - 3, argv + 3);
        return 0;
    }

    ta_name = argv[1];

    if ((rc = ta_log_init(ta_name)) != 0)
    {
        fprintf(stderr, "ta_log_init() failed: error=%d\n", rc);
        return rc;
    }

    RING("Starting");

    sprintf(buf, "PID %lu", (unsigned long)getpid());

    rc = pthread_create(&logfork_tid, NULL, (void *)logfork_entry, NULL);
    if (rc != 0)
    {
        fprintf(stderr, "pthread_create(logfork_entry) failed: rc=%d\n",
                rc);
        /* Continue */
    }

    rc = rcf_pch_run(argv[2], buf);
    if (rc != 0)
    {
        fprintf(stderr, "rcf_pch_run() failed: error=0x%X\n", rc);
        retval = rc;
    }

    /*
     * winpthreads cancels a thread in pthread_testcancel() only, while
     * the logfork thread waits in recv(), so pthread_join() would never
     * return. The thread ends with the process.
     */
    (void)logfork_tid;

    if (ta_tmp_dir_own)
    {
        unsigned int attempt;

        /*
         * Windows does not remove a directory which is the current one
         * of a process, and the processes killed on shutdown may be still
         * exiting.
         */
        for (attempt = 0; attempt < 50; attempt++)
        {
            if (chdir(ta_dir) == 0 && remove_tree(ta_tmp_dir) == 0)
                break;
            te_msleep(100);
        }
        if (attempt == 50)
            fprintf(stderr, "Failed to remove tmp directory\n");
    }

    return retval;
}
