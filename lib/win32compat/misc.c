/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host: miscellaneous
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

/* rand_s() */
#define _CRT_RAND_S

#include "te_win32_posix.h"

#include <crtdbg.h>
#include <sys/stat.h>
#include <sys/utsname.h>

/*
 * POSIX code passes an invalid descriptor and expects EBADF. The C
 * library calls the invalid parameter handler instead, which terminates
 * the process by default, so it is replaced by one that returns. Winsock
 * is initialized here too, as nothing works without it.
 */
static void
invalid_parameter_ignore(const wchar_t *expression, const wchar_t *function,
                         const wchar_t *file, unsigned int line,
                         uintptr_t reserved)
{
    (void)expression;
    (void)function;
    (void)file;
    (void)line;
    (void)reserved;
}

static void __attribute__((constructor))
te_win32_init(void)
{
    WSADATA wsa;

    _set_invalid_parameter_handler(invalid_parameter_ignore);
    _CrtSetReportMode(_CRT_ASSERT, 0);

    /*
     * POSIX files have no text mode: open() and fopen() without 'b'
     * must not convert line ends, or the size fstat() reports would not
     * match the number of bytes read().
     */
    _fmode = _O_BINARY;

    WSAStartup(MAKEWORD(2, 2), &wsa);
}

/* See the description in te_win32_posix.h */
int
setenv(const char *name, const char *value, int overwrite)
{
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (!overwrite && getenv(name) != NULL)
        return 0;

    if (_putenv_s(name, value) != 0)
    {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* See the description in te_win32_posix.h */
int
unsetenv(const char *name)
{
    if (name == NULL || *name == '\0' || strchr(name, '=') != NULL)
    {
        errno = EINVAL;
        return -1;
    }

    /* An empty value removes the variable */
    if (_putenv_s(name, "") != 0)
    {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* See the description in te_win32_posix.h */
char *
strerror_r(int errnum, char *buf, size_t buflen)
{
    if (buflen == 0)
        return buf;

    if (strerror_s(buf, buflen, errnum) != 0)
        snprintf(buf, buflen, "Unknown error %d", errnum);
    return buf;
}

/* See the description in te_win32_posix.h */
ssize_t
getline(char **lineptr, size_t *n, FILE *stream)
{
    size_t len = 0;
    int c;

    if (lineptr == NULL || n == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    if (*lineptr == NULL || *n == 0)
    {
        *n = 128;
        *lineptr = malloc(*n);
        if (*lineptr == NULL)
            return -1;
    }

    while ((c = fgetc(stream)) != EOF)
    {
        if (len + 2 > *n)
        {
            size_t new_n = *n * 2;
            char *p = realloc(*lineptr, new_n);

            if (p == NULL)
                return -1;
            *lineptr = p;
            *n = new_n;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n')
            break;
    }

    if (len == 0)
        return -1;

    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}

/** State of random(), a 64-bit linear congruential generator */
static uint64_t random_state = 1;

/* See the description in te_win32_posix.h */
long
random(void)
{
    random_state = random_state * 6364136223846793005ULL +
                   1442695040888963407ULL;
    return (long)((random_state >> 33) & 0x7fffffff);
}

/* See the description in te_win32_posix.h */
void
srandom(unsigned int seed)
{
    random_state = seed;
}

/* See the description in te_win32_posix.h */
char *
initstate(unsigned int seed, char *state, size_t n)
{
    (void)state;
    (void)n;

    srandom(seed);
    return NULL;
}

/* See the description in te_win32_posix.h */
ssize_t
readlink(const char *path, char *buf, size_t bufsize)
{
    (void)buf;
    (void)bufsize;

    errno = (_access(path, 0) == 0) ? EINVAL : ENOENT;
    return -1;
}

/* See the description in te_win32_posix.h */
int
mkstemps(char *template, int suffixlen)
{
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    size_t len = strlen(template);
    char *x;
    unsigned int attempt;

    if (suffixlen < 0 || len < (size_t)suffixlen + 6 ||
        strncmp(template + len - suffixlen - 6, "XXXXXX", 6) != 0)
    {
        errno = EINVAL;
        return -1;
    }
    x = template + len - suffixlen - 6;

    for (attempt = 0; attempt < 100; attempt++)
    {
        unsigned int i;
        int fd;

        for (i = 0; i < 6; i++)
        {
            unsigned int r;

            rand_s(&r);
            x[i] = chars[r % (sizeof(chars) - 1)];
        }

        fd = _open(template, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY,
                   _S_IREAD | _S_IWRITE);
        if (fd >= 0 || errno != EEXIST)
            return fd;
    }

    errno = EEXIST;
    return -1;
}

/* See the description in te_win32_posix.h */
uid_t
getuid(void)
{
    TOKEN_ELEVATION elevation;
    HANDLE token;
    DWORD size;
    bool elevated = false;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        if (GetTokenInformation(token, TokenElevation, &elevation,
                                sizeof(elevation), &size))
            elevated = elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }

    /* 1000 is the first ID of an ordinary user on many POSIX systems */
    return elevated ? 0 : 1000;
}

/* See the description in te_win32_posix.h */
int
settimeofday(const struct timeval *tv, const void *tz)
{
    /* Seconds between 1601-01-01 (FILETIME) and 1970-01-01 (Unix) */
    const uint64_t epoch_diff = 11644473600ULL;
    ULARGE_INTEGER t;
    FILETIME ft;
    SYSTEMTIME st;

    if (tv == NULL || tz != NULL)
    {
        errno = EINVAL;
        return -1;
    }

    t.QuadPart = ((uint64_t)tv->tv_sec + epoch_diff) * 10000000ULL +
                 (uint64_t)tv->tv_usec * 10;
    ft.dwLowDateTime = t.LowPart;
    ft.dwHighDateTime = t.HighPart;

    if (!FileTimeToSystemTime(&ft, &st) || !SetSystemTime(&st))
    {
        errno = te_win32_err2errno(GetLastError());
        return -1;
    }
    return 0;
}

/* See the description in te_win32_posix.h */
long
sysconf(int name)
{
    SYSTEM_INFO si;

    switch (name)
    {
        case _SC_PAGESIZE:
            GetSystemInfo(&si);
            return si.dwPageSize;

        case _SC_NPROCESSORS_CONF:
        case _SC_NPROCESSORS_ONLN:
            GetSystemInfo(&si);
            return si.dwNumberOfProcessors;

        case _SC_CLK_TCK:
            return CLOCKS_PER_SEC;

        default:
            errno = EINVAL;
            return -1;
    }
}

/* See the description in te_win32_posix.h */
int
getpagesize(void)
{
    return (int)sysconf(_SC_PAGESIZE);
}

/* See the description in sys/utsname.h */
int
uname(struct utsname *buf)
{
    typedef LONG (WINAPI *rtl_get_version_f)(OSVERSIONINFOW *);
    OSVERSIONINFOW ver = { .dwOSVersionInfoSize = sizeof(ver) };
    rtl_get_version_f rtl_get_version;
    DWORD size = sizeof(buf->nodename);
    SYSTEM_INFO si;

    memset(buf, 0, sizeof(*buf));
    snprintf(buf->sysname, sizeof(buf->sysname), "Windows");

    if (!GetComputerNameA(buf->nodename, &size))
        snprintf(buf->nodename, sizeof(buf->nodename), "localhost");

    /* GetVersionEx() lies to applications without a manifest */
    rtl_get_version = (rtl_get_version_f)(void *)GetProcAddress(
                          GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
    if (rtl_get_version != NULL && rtl_get_version(&ver) == 0)
    {
        snprintf(buf->release, sizeof(buf->release), "%lu.%lu",
                 ver.dwMajorVersion, ver.dwMinorVersion);
        snprintf(buf->version, sizeof(buf->version), "%lu",
                 ver.dwBuildNumber);
    }

    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture)
    {
        case PROCESSOR_ARCHITECTURE_AMD64:
            snprintf(buf->machine, sizeof(buf->machine), "x86_64");
            break;

        case PROCESSOR_ARCHITECTURE_ARM64:
            snprintf(buf->machine, sizeof(buf->machine), "aarch64");
            break;

        case PROCESSOR_ARCHITECTURE_INTEL:
            snprintf(buf->machine, sizeof(buf->machine), "i686");
            break;

        default:
            snprintf(buf->machine, sizeof(buf->machine), "unknown");
            break;
    }

    return 0;
}
