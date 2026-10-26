/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief dlfcn.h for a native Windows host
 *
 * Dynamic loading over LoadLibrary() and GetProcAddress().
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_DLFCN_H__
#define __TE_WIN32_DLFCN_H__

#define RTLD_LAZY       0x1
#define RTLD_NOW        0x2
#define RTLD_GLOBAL     0x100
#define RTLD_LOCAL      0

/** The handle of the executable itself */
#define RTLD_DEFAULT    ((void *)0)

extern void *dlopen(const char *filename, int flags);
extern void *dlsym(void *handle, const char *symbol);
extern int dlclose(void *handle);
extern char *dlerror(void);

#endif /* !__TE_WIN32_DLFCN_H__ */
