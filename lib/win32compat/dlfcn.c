/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: dynamic loading
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

#include <dlfcn.h>
#include <psapi.h>

/** Message of the last failure, dlerror() returns it once */
static __thread char dl_error[256];
static __thread bool dl_error_set;

static void
dl_set_error(const char *what, const char *name)
{
    DWORD err = GetLastError();
    int len;

    len = snprintf(dl_error, sizeof(dl_error), "%s(%s): ", what,
                   name == NULL ? "" : name);
    if (len < 0 || (size_t)len >= sizeof(dl_error) ||
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, 0,
                       dl_error + len, sizeof(dl_error) - len, NULL) == 0)
    {
        snprintf(dl_error + len, sizeof(dl_error) - len, "error %lu", err);
    }
    dl_error_set = true;
}

/* See the description in dlfcn.h */
void *
dlopen(const char *filename, int flags)
{
    HMODULE mod;

    (void)flags;

    if (filename == NULL)
        return GetModuleHandleA(NULL);

    mod = LoadLibraryExA(filename, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (mod == NULL)
        dl_set_error("dlopen", filename);
    return mod;
}

/**
 * Look a symbol up in every module of the process, which is what
 * RTLD_DEFAULT means on POSIX.
 */
static void *
dlsym_default(const char *symbol)
{
    HMODULE mods[1024];
    DWORD needed;
    DWORD i;
    void *addr;

    addr = (void *)GetProcAddress(GetModuleHandleA(NULL), symbol);
    if (addr != NULL)
        return addr;

    if (!EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods),
                            &needed))
        return NULL;

    if (needed > sizeof(mods))
        needed = sizeof(mods);

    for (i = 0; i < needed / sizeof(mods[0]); i++)
    {
        addr = (void *)GetProcAddress(mods[i], symbol);
        if (addr != NULL)
            return addr;
    }
    return NULL;
}

/* See the description in dlfcn.h */
void *
dlsym(void *handle, const char *symbol)
{
    void *addr;

    if (handle == RTLD_DEFAULT)
        addr = dlsym_default(symbol);
    else
        addr = (void *)GetProcAddress((HMODULE)handle, symbol);

    if (addr == NULL)
        dl_set_error("dlsym", symbol);
    return addr;
}

/* See the description in dlfcn.h */
int
dlclose(void *handle)
{
    if (handle == RTLD_DEFAULT || handle == GetModuleHandleA(NULL))
        return 0;

    if (!FreeLibrary((HMODULE)handle))
    {
        dl_set_error("dlclose", NULL);
        return -1;
    }
    return 0;
}

/* See the description in dlfcn.h */
char *
dlerror(void)
{
    if (!dl_error_set)
        return NULL;

    dl_error_set = false;
    return dl_error;
}
