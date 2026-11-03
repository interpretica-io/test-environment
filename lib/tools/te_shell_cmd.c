/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief Routines to call shell command
 *
 * Auxiluary routines to call shell command
 *
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#define TE_LGR_USER     "TE Shell Cmd"

#include "te_config.h"

#include "logger_api.h"

#include "te_shell_cmd.h"

#ifdef _WIN32
/*
 * Windows has cmd.exe as its shell. A POSIX shell, e.g. the one of
 * MSYS2, may be chosen by TE_WIN32_SHELL: it is run as "<shell> -c".
 */
pid_t
te_shell_cmd(const char *cmd, uid_t uid,
             int *in_fd, int *out_fd, int *err_fd)
{
    const char *shell = getenv("TE_WIN32_SHELL");

    if (shell != NULL && *shell != '\0')
    {
        return te_exec_child(shell,
                (char *const[]){(char *)shell, "-c", (char *)cmd, NULL},
                NULL, uid, in_fd, out_fd, err_fd, NULL);
    }

    shell = getenv("ComSpec");
    if (shell == NULL || *shell == '\0')
        shell = "cmd.exe";

    return te_exec_child(shell,
            (char *const[]){(char *)shell, "/d", "/s", "/c", (char *)cmd,
                            NULL},
            NULL, uid, in_fd, out_fd, err_fd, NULL);
}
#else
pid_t
te_shell_cmd(const char *cmd, uid_t uid,
             int *in_fd, int *out_fd, int *err_fd)
{
    return te_exec_child("/bin/sh",
            (char *const[]){"sh", "-c", (char *)cmd, (char *)NULL},
            NULL, uid, in_fd, out_fd, err_fd, NULL);
}
#endif
