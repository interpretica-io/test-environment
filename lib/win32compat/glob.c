/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: glob()
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

#include <fnmatch.h>
#include <glob.h>

/** Compare paths for qsort() */
static int
path_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/** Append a path to the result */
static int
glob_add(glob_t *pglob, const char *dir, size_t dir_len, const char *name)
{
    char **pathv;
    char *path;

    pathv = realloc(pglob->gl_pathv,
                    (pglob->gl_pathc + 2) * sizeof(*pathv));
    if (pathv == NULL)
        return GLOB_NOSPACE;
    pglob->gl_pathv = pathv;

    path = malloc(dir_len + strlen(name) + 1);
    if (path == NULL)
        return GLOB_NOSPACE;
    memcpy(path, dir, dir_len);
    strcpy(path + dir_len, name);

    pathv[pglob->gl_pathc++] = path;
    pathv[pglob->gl_pathc] = NULL;
    return 0;
}

/* See the description in glob.h */
int
glob(const char *pattern, int flags,
     int (*errfunc)(const char *epath, int eerrno), glob_t *pglob)
{
    const char *base;
    const char *p;
    size_t dir_len;
    char *search;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int rc = 0;

    (void)errfunc;

    pglob->gl_pathc = 0;
    pglob->gl_pathv = NULL;
    pglob->gl_offs = 0;

    base = pattern;
    for (p = pattern; *p != '\0'; p++)
    {
        if (*p == '/' || *p == '\\')
            base = p + 1;
    }
    dir_len = base - pattern;

    if (strpbrk(base, "*?[") == NULL)
    {
        /* No wildcards: the path matches if it exists */
        if (_access(pattern, 0) == 0)
            return glob_add(pglob, pattern, strlen(pattern), "");
        return (flags & GLOB_NOCHECK) ?
                   glob_add(pglob, pattern, strlen(pattern), "") :
                   GLOB_NOMATCH;
    }

    search = malloc(dir_len + 2);
    if (search == NULL)
        return GLOB_NOSPACE;
    memcpy(search, pattern, dir_len);
    strcpy(search + dir_len, "*");

    h = FindFirstFileA(search, &fd);
    free(search);
    if (h == INVALID_HANDLE_VALUE)
    {
        if (GetLastError() == ERROR_FILE_NOT_FOUND ||
            GetLastError() == ERROR_PATH_NOT_FOUND)
            return GLOB_NOMATCH;
        return GLOB_ABORTED;
    }

    do {
        if (strcmp(fd.cFileName, ".") == 0 ||
            strcmp(fd.cFileName, "..") == 0)
            continue;

        if (fnmatch(base, fd.cFileName,
                    FNM_PERIOD |
                    ((flags & GLOB_NOESCAPE) ? FNM_NOESCAPE : 0)) != 0)
            continue;

        rc = glob_add(pglob, pattern, dir_len, fd.cFileName);
    } while (rc == 0 && FindNextFileA(h, &fd));

    FindClose(h);

    if (rc != 0)
    {
        globfree(pglob);
        return rc;
    }

    if (pglob->gl_pathc == 0)
    {
        return (flags & GLOB_NOCHECK) ?
                   glob_add(pglob, pattern, strlen(pattern), "") :
                   GLOB_NOMATCH;
    }

    if (!(flags & GLOB_NOSORT))
        qsort(pglob->gl_pathv, pglob->gl_pathc, sizeof(char *), path_cmp);

    return 0;
}

/* See the description in glob.h */
void
globfree(glob_t *pglob)
{
    size_t i;

    for (i = 0; i < pglob->gl_pathc; i++)
        free(pglob->gl_pathv[i]);
    free(pglob->gl_pathv);
    pglob->gl_pathv = NULL;
    pglob->gl_pathc = 0;
}
