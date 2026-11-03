/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief POSIX interfaces for a native Windows host
 *
 * te_config.h includes this file on a MinGW-w64 host, so every TE source
 * sees the POSIX declarations a native Windows C library lacks. The
 * POSIX-named headers next to it (sys/socket.h, sys/wait.h, ...) map the
 * socket API to Winsock. The functions are implemented in
 * lib/win32compat.
 *
 * Process IDs are the Windows ones. A process TE starts is registered
 * with its handle, so waitpid() and kill() keep working after the
 * process exits and until it is reaped, as they do on POSIX.
 *
 * Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
 */

#ifndef __TE_WIN32_POSIX_H__
#define __TE_WIN32_POSIX_H__

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mswsock.h>
#include <iphlpapi.h>

/*
 * basetyps.h, which these headers include, makes 'interface' a keyword,
 * while TE has structure fields of that name. The headers are included
 * here, so that it does not come back with them later.
 */
#undef interface

#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <direct.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Signals. The C library knows SIGINT, SIGILL, SIGFPE, SIGSEGV, SIGTERM,
 * SIGBREAK and SIGABRT only. The rest get the Linux numbers which do not
 * collide with those. Only kill() delivers them: any of them terminates
 * the target process.
 */
#ifndef SIGHUP
#define SIGHUP      1
#endif
#ifndef SIGQUIT
#define SIGQUIT     3
#endif
#ifndef SIGTRAP
#define SIGTRAP     5
#endif
#ifndef SIGBUS
#define SIGBUS      7
#endif
#ifndef SIGKILL
#define SIGKILL     9
#endif
#ifndef SIGUSR1
#define SIGUSR1     10
#endif
#ifndef SIGUSR2
#define SIGUSR2     12
#endif
#ifndef SIGPIPE
#define SIGPIPE     13
#endif
#ifndef SIGALRM
#define SIGALRM     14
#endif
#ifndef SIGCHLD
#define SIGCHLD     17
#endif
#ifndef SIGCONT
#define SIGCONT     18
#endif
#ifndef SIGSTOP
#define SIGSTOP     19
#endif
#ifndef SIGTSTP
#define SIGTSTP     20
#endif

/** Set of signals */
#ifndef _POSIX
typedef _sigset_t sigset_t;
#endif

/** Whether a signal number fits into sigset_t */
#define TE_WIN32_SIG_VALID(_sig) \
    ((_sig) > 0 && (size_t)(_sig) <= sizeof(sigset_t) * 8)

static inline int
sigemptyset(sigset_t *set)
{
    *set = 0;
    return 0;
}

static inline int
sigfillset(sigset_t *set)
{
    *set = ~(sigset_t)0;
    return 0;
}

static inline int
sigaddset(sigset_t *set, int sig)
{
    if (!TE_WIN32_SIG_VALID(sig))
    {
        errno = EINVAL;
        return -1;
    }
    *set |= (sigset_t)1 << (sig - 1);
    return 0;
}

static inline int
sigdelset(sigset_t *set, int sig)
{
    if (!TE_WIN32_SIG_VALID(sig))
    {
        errno = EINVAL;
        return -1;
    }
    *set &= ~((sigset_t)1 << (sig - 1));
    return 0;
}

static inline int
sigismember(const sigset_t *set, int sig)
{
    if (!TE_WIN32_SIG_VALID(sig))
    {
        errno = EINVAL;
        return -1;
    }
    return (*set & ((sigset_t)1 << (sig - 1))) != 0;
}

/** Value passed with a signal */
union sigval {
    int     sival_int;
    void   *sival_ptr;
};

/** Information about a signal, nothing on Windows fills it in */
typedef struct {
    int             si_signo;
    int             si_errno;
    int             si_code;
    pid_t           si_pid;
    int             si_uid;
    int             si_status;
    void           *si_addr;
    union sigval    si_value;
} siginfo_t;

/**
 * Send a signal to a process, see the description above. A negative
 * @p pid names the job of a process TE has started.
 */
extern int kill(pid_t pid, int sig);

/*
 * Process status as waitpid() reports it, in the Linux encoding: the
 * exit code in bits 8-15, or the number of the signal kill() sent.
 */
#ifndef WNOHANG
#define WNOHANG     1
#endif
#ifndef WUNTRACED
#define WUNTRACED   2
#endif

#define WEXITSTATUS(_s)     (((_s) & 0xff00) >> 8)
#define WTERMSIG(_s)        ((_s) & 0x7f)
#define WSTOPSIG(_s)        WEXITSTATUS(_s)
#define WIFEXITED(_s)       (WTERMSIG(_s) == 0)
#define WIFSIGNALED(_s)     (WTERMSIG(_s) != 0 && WTERMSIG(_s) != 0x7f)
#define WIFSTOPPED(_s)      0
#define WIFCONTINUED(_s)    0
#define WCOREDUMP(_s)       0

/**
 * Wait for a registered process.
 *
 * @param pid       process ID or -1 for any registered process
 * @param status    location for the status or @c NULL
 * @param options   @c 0 or @c WNOHANG
 *
 * @return Process ID, @c 0 if @c WNOHANG is given and no process has
 *         exited, or @c -1 with @c errno set (@c ECHILD if the process
 *         is not registered).
 */
extern pid_t waitpid(pid_t pid, int *status, int options);

/**
 * Register a process TE has started, so that waitpid() can reap it.
 *
 * @param pid       process ID
 * @param handle    process handle
 * @param job       job of the process and its children, or @c NULL;
 *                  kill() of the negated @p pid terminates the job
 *
 * The registry owns the handles from now on.
 *
 * @return @c 0 or @c -1 with @c errno set.
 */
extern int te_win32_process_register(pid_t pid, HANDLE handle, HANDLE job);

/*
 * File descriptor flags and locks. Only FD_CLOEXEC (handle inheritance),
 * O_NONBLOCK on sockets and write locks of a whole file are supported.
 */
#ifndef F_GETFD
#define F_GETFD     1
#endif
#ifndef F_SETFD
#define F_SETFD     2
#endif
#ifndef F_GETFL
#define F_GETFL     3
#endif
#ifndef F_SETFL
#define F_SETFL     4
#endif
#ifndef FD_CLOEXEC
#define FD_CLOEXEC  1
#endif
#ifndef F_GETLK
#define F_GETLK     5
#endif
#ifndef F_SETLK
#define F_SETLK     6
#endif
#ifndef F_SETLKW
#define F_SETLKW    7
#endif
#ifndef F_RDLCK
#define F_RDLCK     0
#endif
#ifndef F_WRLCK
#define F_WRLCK     1
#endif
#ifndef F_UNLCK
#define F_UNLCK     2
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK  0x4000
#endif
#ifndef O_NOCTTY
#define O_NOCTTY    0
#endif

/** File lock, see fcntl(2) */
struct flock {
    short   l_type;     /**< F_RDLCK, F_WRLCK or F_UNLCK */
    short   l_whence;   /**< SEEK_SET, SEEK_CUR or SEEK_END */
    off_t   l_start;    /**< Start of the locked range */
    off_t   l_len;      /**< Length of the range, 0 up to the end */
    pid_t   l_pid;      /**< Holder of the lock, F_GETLK only */
};

/** Manipulate a file descriptor or a socket, see the note above */
extern int fcntl(int fd, int cmd, ...);

/** Create a pipe, both ends are binary and not inherited */
extern int pipe(int fds[2]);

/*
 * Sockets are not file descriptors on Windows. These close, read and
 * write either kind of descriptor.
 */
extern int te_win32_close(int fd);
extern ssize_t te_win32_read(int fd, void *buf, size_t count);
extern ssize_t te_win32_write(int fd, const void *buf, size_t count);

/*
 * TE closes, reads and writes sockets with close(), read() and write().
 * The macros take arguments, so that structure fields of these names stay.
 */
#define close(_fd)                  te_win32_close(_fd)
#define read(_fd, _buf, _count)     te_win32_read(_fd, _buf, _count)
#define write(_fd, _buf, _count)    te_win32_write(_fd, _buf, _count)

/** errno value corresponding to a Winsock error code */
extern int te_win32_wsa2errno(int wsa_error);

/** errno value corresponding to a Windows error code */
extern int te_win32_err2errno(DWORD error);

/* Files */

/** POSIX mkdir(), the mode is ignored */
static inline int
te_win32_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    return _mkdir(path);
}
#define mkdir(_path, _mode) te_win32_mkdir(_path, _mode)

/**
 * readlink() never finds a symbolic link, since the C library does not
 * handle them: it fails with EINVAL, as for a file which is not a link.
 */
extern ssize_t readlink(const char *path, char *buf, size_t bufsize);

/** Create a unique file from a template ending in XXXXXX and a suffix */
extern int mkstemps(char *template, int suffixlen);

/* Users. Windows has security identifiers, not numeric IDs */
typedef int uid_t;
typedef int gid_t;

/** @c 0 if the process runs elevated, as root does, a non-zero ID else */
extern uid_t getuid(void);
#define geteuid getuid

/* Time */

/** Set the system time, the time zone is not supported */
extern int settimeofday(const struct timeval *tv, const void *tz);

/* Environment */
extern int setenv(const char *name, const char *value, int overwrite);
extern int unsetenv(const char *name);

/* Strings */

/** GNU strerror_r(): the message may be in @p buf or a static string */
extern char *strerror_r(int errnum, char *buf, size_t buflen);

static inline char *
index(const char *s, int c)
{
    return strchr(s, c);
}

static inline char *
rindex(const char *s, int c)
{
    return strrchr(s, c);
}

/** Read a line, see getline(3) */
extern ssize_t getline(char **lineptr, size_t *n, FILE *stream);

/* Random numbers, see random(3) */
extern long random(void);
extern void srandom(unsigned int seed);
extern char *initstate(unsigned int seed, char *state, size_t n);

/* System configuration */
#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE            1
#endif
#ifndef _SC_PAGE_SIZE
#define _SC_PAGE_SIZE           _SC_PAGESIZE
#endif
#ifndef _SC_NPROCESSORS_CONF
#define _SC_NPROCESSORS_CONF    2
#endif
#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN    3
#endif
#ifndef _SC_CLK_TCK
#define _SC_CLK_TCK             4
#endif

extern long sysconf(int name);
extern int getpagesize(void);

/* Positioned I/O, not atomic against other users of the descriptor */
extern ssize_t pread(int fd, void *buf, size_t count, off_t offset);
extern ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TE_WIN32_POSIX_H__ */
