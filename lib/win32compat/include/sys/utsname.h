/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief sys/utsname.h for a native Windows host
 *
 * uname() reports the Windows version and the computer name.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_SYS_UTSNAME_H__
#define __TE_WIN32_SYS_UTSNAME_H__

#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
};

extern int uname(struct utsname *buf);

#endif /* !__TE_WIN32_SYS_UTSNAME_H__ */
