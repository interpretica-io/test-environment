/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: fnmatch()
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#include "te_win32_posix.h"

#include <fnmatch.h>

/** Whether a character is a path separator for FNM_PATHNAME */
static bool
is_sep(char c, int flags)
{
    return (flags & FNM_PATHNAME) && (c == '/' || c == '\\');
}

/**
 * Match a bracket expression.
 *
 * @param p         pattern just past '['
 * @param c         character to match
 * @param end       location for the pattern past ']'
 *
 * @return Whether the character matches; @p end is @c NULL if the
 *         expression is not terminated.
 */
static bool
match_bracket(const char *p, char c, int flags, const char **end)
{
    bool negate = false;
    bool matched = false;
    bool first = true;

    if (*p == '!' || *p == '^')
    {
        negate = true;
        p++;
    }

    for (; *p != '\0'; first = false)
    {
        char lo;
        char hi;

        if (*p == ']' && !first)
        {
            *end = p + 1;
            return matched != negate;
        }

        lo = *p++;
        if (lo == '\\' && !(flags & FNM_NOESCAPE) && *p != '\0')
            lo = *p++;

        hi = lo;
        if (*p == '-' && p[1] != ']' && p[1] != '\0')
        {
            hi = p[1];
            p += 2;
            if (hi == '\\' && !(flags & FNM_NOESCAPE) && *p != '\0')
                hi = *p++;
        }

        if (c >= lo && c <= hi)
            matched = true;
    }

    *end = NULL;
    return false;
}

/** Match a pattern against a string, see fnmatch(3) */
static bool
match(const char *p, const char *s, const char *start, int flags)
{
    while (*p != '\0')
    {
        switch (*p)
        {
            case '?':
                if (*s == '\0' || is_sep(*s, flags))
                    return false;
                if (*s == '.' && (flags & FNM_PERIOD) &&
                    (s == start || is_sep(s[-1], flags)))
                    return false;
                p++;
                s++;
                break;

            case '*':
                while (*p == '*')
                    p++;
                if (*s == '.' && (flags & FNM_PERIOD) &&
                    (s == start || is_sep(s[-1], flags)))
                    return false;
                for (;; s++)
                {
                    if (match(p, s, start, flags))
                        return true;
                    if (*s == '\0' || is_sep(*s, flags))
                        return false;
                }

            case '[':
            {
                const char *end;

                if (*s == '\0' || is_sep(*s, flags))
                    return false;
                if (!match_bracket(p + 1, *s, flags, &end))
                {
                    /* An unterminated '[' is an ordinary character */
                    if (end == NULL && *s == '[')
                    {
                        p++;
                        s++;
                        break;
                    }
                    return false;
                }
                p = end;
                s++;
                break;
            }

            case '\\':
                if (!(flags & FNM_NOESCAPE) && p[1] != '\0')
                    p++;
                /* fall through */

            default:
                if (*p != *s)
                    return false;
                p++;
                s++;
                break;
        }
    }

    return *s == '\0';
}

/* See the description in fnmatch.h */
int
fnmatch(const char *pattern, const char *string, int flags)
{
    return match(pattern, string, string, flags) ? 0 : FNM_NOMATCH;
}
