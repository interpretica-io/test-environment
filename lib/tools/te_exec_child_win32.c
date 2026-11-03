/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Routines to execute a program in a child process on Windows
 *
 * Windows has no fork(), so the child is created by CreateProcess()
 * with the standard handles in place. The child inherits only these
 * handles. It is put into a job of its own, which plays the role of
 * the process group of the POSIX implementation: kill() terminates the
 * whole tree the child has started.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#define TE_LGR_USER     "TE Exec Child"

#include "te_config.h"

#include "logger_api.h"

#include "te_exec_child.h"
#include "te_string.h"
#include "te_errno.h"

#define VALID_FD_PTR(ptr) ((ptr) != NULL && (ptr) != TE_EXEC_CHILD_DEV_NULL_FD)

/** Append a character @p n times */
static void
append_repeat(te_string *str, char c, unsigned int n)
{
    while (n-- > 0)
        te_string_append(str, "%c", c);
}

/**
 * Append an argument to a command line, quoted the way the Microsoft
 * C runtime splits it back into argv.
 */
static void
append_quoted_arg(te_string *cmdline, const char *arg)
{
    const char *p;

    if (cmdline->len > 0)
        te_string_append(cmdline, " ");

    if (*arg != '\0' && strpbrk(arg, " \t\n\v\"") == NULL)
    {
        te_string_append(cmdline, "%s", arg);
        return;
    }

    te_string_append(cmdline, "\"");
    for (p = arg; ; p++)
    {
        unsigned int backslashes = 0;

        while (*p == '\\')
        {
            backslashes++;
            p++;
        }

        if (*p == '\0')
        {
            /* Backslashes before the closing quote must be doubled */
            append_repeat(cmdline, '\\', backslashes * 2);
            break;
        }

        if (*p == '"')
        {
            append_repeat(cmdline, '\\', backslashes * 2 + 1);
            te_string_append(cmdline, "\"");
        }
        else
        {
            append_repeat(cmdline, '\\', backslashes);
            te_string_append(cmdline, "%c", *p);
        }
    }
    te_string_append(cmdline, "\"");
}

/**
 * Build an environment block: NAME=value strings, each terminated by
 * a NUL, and one more NUL at the end.
 */
static char *
make_env_block(char *const envp[])
{
    size_t len = 1;
    size_t i;
    char *block;
    char *p;

    for (i = 0; envp[i] != NULL; i++)
        len += strlen(envp[i]) + 1;

    block = malloc(len + 1);
    if (block == NULL)
        return NULL;

    p = block;
    for (i = 0; envp[i] != NULL; i++)
    {
        size_t n = strlen(envp[i]) + 1;

        memcpy(p, envp[i], n);
        p += n;
    }
    *p++ = '\0';
    /* An empty block still needs two NULs */
    *p = '\0';

    return block;
}

/**
 * Prepare a standard handle of the child.
 *
 * @param fd_arg        Descriptor argument of te_exec_child().
 * @param input         Whether it is stdin of the child.
 * @param std_id        Standard handle of the parent to inherit.
 * @param child         Location for the handle the child gets.
 * @param parent        Location for the handle the parent keeps, or
 *                      @c INVALID_HANDLE_VALUE.
 * @param close_child   Location for whether the child handle has to be
 *                      closed after the child is created.
 *
 * @return @c 0 or @c -1 with @c errno set.
 */
static int
prepare_std_handle(const int *fd_arg, bool input, DWORD std_id,
                   HANDLE *child, HANDLE *parent, bool *close_child)
{
    SECURITY_ATTRIBUTES sa = {
        .nLength = sizeof(sa),
        .bInheritHandle = TRUE,
    };

    *parent = INVALID_HANDLE_VALUE;
    *close_child = false;

    if (fd_arg == NULL)
    {
        *child = GetStdHandle(std_id);
        if (*child == NULL || *child == INVALID_HANDLE_VALUE)
        {
            /* No console, e.g. in a service: nothing to inherit */
            *child = INVALID_HANDLE_VALUE;
            return 0;
        }

        /* It is inherited only if it is inheritable */
        if (!DuplicateHandle(GetCurrentProcess(), *child,
                             GetCurrentProcess(), child, 0, TRUE,
                             DUPLICATE_SAME_ACCESS))
        {
            errno = te_win32_err2errno(GetLastError());
            return -1;
        }
        *close_child = true;
        return 0;
    }

    if (fd_arg == TE_EXEC_CHILD_DEV_NULL_FD)
    {
        *child = CreateFileA("NUL", input ? GENERIC_READ : GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
        if (*child == INVALID_HANDLE_VALUE)
        {
            errno = te_win32_err2errno(GetLastError());
            return -1;
        }
        *close_child = true;
        return 0;
    }

    {
        HANDLE rd;
        HANDLE wr;

        if (!CreatePipe(&rd, &wr, NULL, 0))
        {
            errno = te_win32_err2errno(GetLastError());
            return -1;
        }

        *child = input ? rd : wr;
        *parent = input ? wr : rd;

        if (!SetHandleInformation(*child, HANDLE_FLAG_INHERIT,
                                  HANDLE_FLAG_INHERIT))
        {
            errno = te_win32_err2errno(GetLastError());
            CloseHandle(rd);
            CloseHandle(wr);
            *parent = INVALID_HANDLE_VALUE;
            return -1;
        }
        *close_child = true;
    }

    return 0;
}

/** Map a niceness value to a Windows priority class */
static DWORD
nice2priority_class(int nice_value)
{
    if (nice_value >= 15)
        return IDLE_PRIORITY_CLASS;
    if (nice_value >= 5)
        return BELOW_NORMAL_PRIORITY_CLASS;
    if (nice_value > -5)
        return NORMAL_PRIORITY_CLASS;
    if (nice_value > -15)
        return ABOVE_NORMAL_PRIORITY_CLASS;
    return HIGH_PRIORITY_CLASS;
}

/**
 * Get the process parameters which CreateProcess() takes.
 *
 * @param exec_param    Parameters of te_exec_child().
 * @param flags         Process creation flags to update.
 * @param workdir       Location for the working directory.
 * @param affinity      Location for the affinity mask, @c 0 if not set.
 *
 * @return Status code.
 */
static te_errno
get_exec_params(const te_exec_param *exec_param, DWORD *flags,
                const char **workdir, DWORD_PTR *affinity)
{
    const te_exec_param *iter;

    *workdir = NULL;
    *affinity = 0;

    if (exec_param == NULL)
        return 0;

    for (iter = exec_param; iter->type != TE_EXEC_END; iter++)
    {
        switch (iter->type)
        {
            case TE_EXEC_AFFINITY:
            {
                const te_exec_affinity_param *data = iter->data;
                size_t i;

                for (i = 0; i < data->cpu_ids_len; i++)
                {
                    if (data->cpu_ids[i] < 0 ||
                        (size_t)data->cpu_ids[i] >= sizeof(DWORD_PTR) * 8)
                    {
                        ERROR("CPU %d is out of the affinity mask range",
                              data->cpu_ids[i]);
                        return TE_EINVAL;
                    }
                    *affinity |= (DWORD_PTR)1 << data->cpu_ids[i];
                }
                break;
            }

            case TE_EXEC_PRIORITY:
            {
                const te_exec_priority_param *data = iter->data;

                *flags |= nice2priority_class(data->priority);
                break;
            }

            case TE_EXEC_WORKDIR:
            {
                const te_exec_workdir_param *data = iter->data;

                *workdir = data->workdir;
                break;
            }

            default:
                ERROR("Unsupported process parameter. type = %d", iter->type);
                return TE_EINVAL;
        }
    }

    return 0;
}

/** Close the handle if it is valid */
static void
close_valid_handle(HANDLE h)
{
    if (h != NULL && h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
}

pid_t
te_exec_child(const char *file, char *const argv[],
              char *const envp[], uid_t uid, int *in_fd,
              int *out_fd, int *err_fd,
              const te_exec_param *exec_param)
{
    int *const fd_args[3] = { in_fd, out_fd, err_fd };
    static const DWORD std_ids[3] = {
        STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
    };
    HANDLE child_h[3] = {
        INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE
    };
    HANDLE parent_h[3] = {
        INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE
    };
    bool close_child[3] = { false, false, false };
    HANDLE inherit[3];
    unsigned int n_inherit = 0;
    te_string cmdline = TE_STRING_INIT;
    LPPROC_THREAD_ATTRIBUTE_LIST attrs = NULL;
    STARTUPINFOEXA si;
    PROCESS_INFORMATION pi;
    SIZE_T attrs_size = 0;
    DWORD flags = CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT |
                  CREATE_NEW_PROCESS_GROUP;
    const char *workdir;
    DWORD_PTR affinity;
    HANDLE job = NULL;
    char *env_block = NULL;
    pid_t pid = -1;
    int saved_errno = 0;
    unsigned int i;
    size_t j;

    if (file == NULL || argv == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (uid != (uid_t)(-1))
    {
        ERROR("Running \"%s\" as user %d is not supported on Windows",
              file, (int)uid);
        errno = ENOTSUP;
        return -1;
    }

    if (get_exec_params(exec_param, &flags, &workdir, &affinity) != 0)
    {
        errno = EINVAL;
        return -1;
    }

    /*
     * The program is the first word of the command line rather than
     * the application name, so that it is searched in PATH like
     * execvp() does. argv[0] is not passed: Windows has no place
     * for it other than the program name.
     */
    append_quoted_arg(&cmdline, file);
    for (j = 1; argv[0] != NULL && argv[j] != NULL; j++)
        append_quoted_arg(&cmdline, argv[j]);

    if (envp != NULL)
    {
        env_block = make_env_block(envp);
        if (env_block == NULL)
        {
            saved_errno = ENOMEM;
            goto out;
        }
    }

    for (i = 0; i < 3; i++)
    {
        if (prepare_std_handle(fd_args[i], i == 0, std_ids[i], &child_h[i],
                               &parent_h[i], &close_child[i]) != 0)
        {
            saved_errno = errno;
            goto out;
        }
        if (child_h[i] != INVALID_HANDLE_VALUE)
            inherit[n_inherit++] = child_h[i];
    }

    /* Let the child inherit the standard handles and nothing else */
    InitializeProcThreadAttributeList(NULL, 1, 0, &attrs_size);
    attrs = malloc(attrs_size);
    if (attrs == NULL ||
        !InitializeProcThreadAttributeList(attrs, 1, 0, &attrs_size))
    {
        saved_errno = ENOMEM;
        free(attrs);
        attrs = NULL;
        goto out;
    }
    if (n_inherit > 0 &&
        !UpdateProcThreadAttribute(attrs, 0,
                                   PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherit, n_inherit * sizeof(inherit[0]),
                                   NULL, NULL))
    {
        saved_errno = te_win32_err2errno(GetLastError());
        goto out;
    }

    memset(&si, 0, sizeof(si));
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = child_h[0];
    si.StartupInfo.hStdOutput = child_h[1];
    si.StartupInfo.hStdError = child_h[2];
    si.lpAttributeList = attrs;

    if (!CreateProcessA(NULL, cmdline.ptr, NULL, NULL, n_inherit > 0,
                        flags, env_block, workdir, &si.StartupInfo, &pi))
    {
        saved_errno = te_win32_err2errno(GetLastError());
        ERROR("Failed to run \"%s\": %s", cmdline.ptr,
              strerror(saved_errno));
        goto out;
    }

    if (affinity != 0 && !SetProcessAffinityMask(pi.hProcess, affinity))
    {
        WARN("Failed to set affinity of pid %lu: error %lu",
             pi.dwProcessId, GetLastError());
    }

    job = CreateJobObjectA(NULL, NULL);
    if (job == NULL || !AssignProcessToJobObject(job, pi.hProcess))
    {
        WARN("Failed to put pid %lu into a job, its children will "
             "outlive it: error %lu", pi.dwProcessId, GetLastError());
        close_valid_handle(job);
        job = NULL;
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    if (te_win32_process_register((pid_t)pi.dwProcessId, pi.hProcess,
                                  job) != 0)
    {
        saved_errno = errno;
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        close_valid_handle(job);
        goto out;
    }

    for (i = 0; i < 3; i++)
    {
        int fd;

        if (!VALID_FD_PTR(fd_args[i]))
            continue;

        fd = _open_osfhandle((intptr_t)parent_h[i],
                             i == 0 ? _O_WRONLY : _O_RDONLY);
        if (fd < 0)
        {
            ERROR("Failed to make a descriptor of the pipe to pid %lu",
                  pi.dwProcessId);
            continue;
        }
        parent_h[i] = INVALID_HANDLE_VALUE;
        *fd_args[i] = fd;
    }

    pid = (pid_t)pi.dwProcessId;

out:
    for (i = 0; i < 3; i++)
    {
        if (close_child[i])
            close_valid_handle(child_h[i]);
        close_valid_handle(parent_h[i]);
    }
    if (attrs != NULL)
    {
        DeleteProcThreadAttributeList(attrs);
        free(attrs);
    }
    free(env_block);
    te_string_free(&cmdline);

    if (pid < 0)
        errno = saved_errno;
    return pid;
}
