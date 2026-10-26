/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief RPC server
 *
 * RPC routines which do not depend on the system the agent runs on:
 * close hooks, function lookup and access to the agent variables.
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#define TE_LGR_USER     "RPC"

#include "rpc_server.h"

#ifdef HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#ifdef HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include "te_alloc.h"
#include "te_defs.h"
#include "te_queue.h"
#include "te_str.h"

#include "agentlib.h"

/** Entry in a queue of FD close hooks */
typedef struct close_fd_hook_entry {
    TAILQ_ENTRY(close_fd_hook_entry)   links;  /**< Queue links */
    tarpc_close_fd_hook               *hook;   /**< Hook function pointer */
    void                              *cookie; /**< Pointer which should be
                                                    passed to each hook
                                                    invocation */
} close_fd_hook_entry;

/** Hooks called just before closing FD */
static TAILQ_HEAD(close_fd_hook_entries,
                  close_fd_hook_entry) close_fd_hooks =
                              TAILQ_HEAD_INITIALIZER(close_fd_hooks);

/** Lock protecting close_fd_hooks */
static pthread_mutex_t      close_fd_hooks_lock = PTHREAD_MUTEX_INITIALIZER;

extern sigset_t         rpcs_received_signals;
extern tarpc_siginfo_t  last_siginfo;

static bool dynamic_library_set = false;
static char      dynamic_library_name[RCF_MAX_PATH];
static void     *dynamic_library_handle = NULL;

/* See description in rpc_server.h */
void
tarpc_close_fd_hooks_call(int fd)
{
    int                  res = 0;
    close_fd_hook_entry *entry;

    /*
     * It seems safe to check this without mutex protection,
     * and it will allow to avoid mutex locking/unlocking in case
     * there is no hooks.
     */
    if (TAILQ_EMPTY(&close_fd_hooks))
        return;

    res = tarpc_mutex_lock(&close_fd_hooks_lock);
    if (res != 0)
        return;

    TAILQ_FOREACH(entry, &close_fd_hooks, links)
    {
        entry->hook(fd, entry->cookie);
    }

    tarpc_mutex_unlock(&close_fd_hooks_lock);
}

/* See description in rpc_server.h */
int
tarpc_close_fd_hook_register(tarpc_close_fd_hook *hook, void *cookie)
{
    close_fd_hook_entry *entry;
    int                  rc;

    if (hook == NULL)
    {
        te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EINVAL),
                         "%s(): hook cannot be NULL",
                         __FUNCTION__);
        return -1;
    }

    entry = TE_ALLOC(sizeof(*entry));
    entry->hook = hook;
    entry->cookie = cookie;

    rc = tarpc_mutex_lock(&close_fd_hooks_lock);
    if (rc != 0)
    {
        free(entry);
        return -1;
    }
    TAILQ_INSERT_TAIL(&close_fd_hooks, entry, links);
    tarpc_mutex_unlock(&close_fd_hooks_lock);

    return 0;
}

/* See description in rpc_server.h */
int
tarpc_close_fd_hook_unregister(tarpc_close_fd_hook *hook, void *cookie)
{
    close_fd_hook_entry *entry;
    bool found = false;
    int                  rc;

    rc = tarpc_mutex_lock(&close_fd_hooks_lock);
    if (rc != 0)
        return -1;

    /*
     * List is walked in reverse order so that the last added hook
     * is unregistered firstly (in case of hooks duplication).
     */
    TAILQ_FOREACH_REVERSE(entry, &close_fd_hooks, close_fd_hook_entries,
                          links)
    {
        if (entry->hook == hook && entry->cookie == cookie)
        {
            found = true;
            TAILQ_REMOVE(&close_fd_hooks, entry, links);
            free(entry);
            break;
        }
    }

    tarpc_mutex_unlock(&close_fd_hooks_lock);

    if (!found)
    {
        te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_ENOENT),
                         "%s(): failed to find hook %p",
                         __FUNCTION__, hook);
        return -1;
    }

    return 0;
}

/* See description in rpc_server.h */
int
tarpc_call_close_with_hooks(api_func close_func, int fd)
{
    tarpc_close_fd_hooks_call(fd);
    return close_func(fd);
}

/**
 * Set name of the dynamic library to be used to resolve function
 * called via RPC.
 *
 * @param libname       Full name of the dynamic library or NULL
 *
 * @return Status code.
 *
 * @note The dynamic library is opened with RTLD_NODELETE flag.
 * This flag is necessary for all libraries using atfork since there is no
 * way to undo the atfork call.  This flag is also necessary if the library
 * does not have correct _fini.  See man dlopen of other details.
 */
te_errno
tarpc_setlibname(const char *libname)
{
    extern int (*tce_notify_function)(void);
    extern int (*tce_get_peer_function)(void);
    extern const char *(*tce_get_conn_function)(void);

    void (*tce_initializer)(const char *, int) = NULL;

    if (libname == NULL)
        libname = "";

    if (dynamic_library_set)
    {
        char *old = getenv("TARPC_DL_NAME");

        if (old == NULL)
        {
            ERROR("Inconsistent state of dynamic library flag and "
                  "Environment");
            return TE_RC(TE_TA_UNIX, TE_EFAULT);
        }
        if (strcmp(libname, old) == 0)
        {
            /* It is OK, if we try to set the same library once more */
            return 0;
        }
        ERROR("Dynamic library has already been set to %s", old);
        return TE_RC(TE_TA_UNIX, TE_EEXIST);
    }
    dynamic_library_handle = dlopen(*libname == '\0' ? NULL : libname,
                                    RTLD_LAZY
#ifdef HAVE_RTLD_NODELETE
                                    | RTLD_NODELETE
#endif
                                    );
    if (dynamic_library_handle == NULL)
    {
        if (*libname == 0)
        {
            dynamic_library_set = true;
            return 0;
        }
        ERROR("Cannot load shared library '%s': %s", libname, dlerror());
        return TE_RC(TE_TA_UNIX, TE_ENOENT);
    }
    if (setenv("TARPC_DL_NAME", libname, 1) != 0)
    {
        ERROR("No enough space in environment to save dynamic library "
              "'%s' name", libname);
        (void)dlclose(dynamic_library_handle);
        dynamic_library_handle = NULL;
        return TE_RC(TE_TA_UNIX, TE_ENOSPC);
    }
    dynamic_library_set = true;
    TE_STRLCPY(dynamic_library_name, libname,
               sizeof(dynamic_library_name));
    RING("Dynamic library is set to '%s'", libname);

    if (tce_get_peer_function != NULL)
    {
        tce_initializer = dlsym(dynamic_library_handle,
                                "__bb_init_connection");
        if (tce_initializer != NULL)
        {
            const char *ptc = tce_get_conn_function();

            if (ptc == NULL)
                WARN("tce_init_connect() has not been called");
            else
            {
                if (tce_notify_function != NULL)
                    tce_notify_function();
                tce_initializer(ptc, tce_get_peer_function());
                RING("TCE initialized for dynamic library '%s'",
                     getenv("TARPC_DL_NAME"));
            }
        }
    }

    return 0;
}

/* See the description in tarpc_server.h */
bool
tarpc_dynamic_library_loaded(void)
{
    return dynamic_library_set && dynamic_library_handle != NULL;
}

/* See the description in rpc_server.h */
const char *
tarpc_dynamic_library_name(void)
{
    return (dynamic_library_set && dynamic_library_name[0] != '\0') ?
               dynamic_library_name : NULL;
}

/**
 * Find the function by its name.
 *
 * @param lib_flags     how to resolve function name
 * @param name          function name
 * @param func          location for function address
 *
 * @return status code
 */
int
tarpc_find_func(tarpc_lib_flags lib_flags, const char *name, api_func *func)
{
    te_errno    rc;
    void       *handle;
    char       *tarpc_dl_name;

    *func = NULL;

    tarpc_dl_name = getenv("TARPC_DL_NAME");
    if (!dynamic_library_set && tarpc_dl_name != NULL &&
        (rc = tarpc_setlibname(tarpc_dl_name)) != 0)
    {
        /* Error is always logged from tarpc_setlibname() */
        return rc;
    }
#if defined (__QNX__)
    /*
     * QNX may set errno to ESRCH even after successful
     * call to 'getenv'.
     */
    errno = 0;
#endif

#define TARPC_MAX_FUNC_NAME 64

    if (lib_flags & TARPC_LIB_USE_SYSCALL)
    {
        char func_syscall_wrap_name[TARPC_MAX_FUNC_NAME] = {0};

        if ((lib_flags & TARPC_LIB_USE_LIBC) || !tarpc_dynamic_library_loaded())
            TE_SPRINTF(func_syscall_wrap_name, "%s_te_wrap_syscall", name);
        else
            TE_SPRINTF(func_syscall_wrap_name, "%s_te_wrap_syscall_dl", name);

        if ((*func = rcf_ch_symbol_addr(func_syscall_wrap_name, 1)) != NULL)
            return 0;

        /* Wrapper was not found, continues to standard name resolving */
    }

    if ((lib_flags & TARPC_LIB_USE_LIBC) || !tarpc_dynamic_library_loaded())
    {
        static void *libc_handle = NULL;
        static bool dlopen_null = false;

        if (dlopen_null)
            goto try_ta_symtbl;

        if (libc_handle == NULL)
        {
            if ((libc_handle = dlopen(NULL, RTLD_LAZY)) == NULL)
            {
                dlopen_null = true;
                goto try_ta_symtbl;
            }
        }
        handle = libc_handle;
        VERB("Call from libc");
    }
    else
    {
        /*
         * We get this branch of the code only if user set some
         * library to be used with tarpc_setlibname() function earlier,
         * and so we should use it to find symbol.
         */
        assert(dynamic_library_set == true);
        assert(dynamic_library_handle != NULL);

        handle = dynamic_library_handle;
        VERB("Call from registered library");
    }

    *func = dlsym(handle, name);

try_ta_symtbl:
    if (*func == NULL)
    {
        if ((*func = rcf_ch_symbol_addr(name, 1)) == NULL)
        {
            ERROR("Cannot resolve symbol %s", name);
            return TE_RC(TE_TA_UNIX, TE_ENOENT);
        }
    }
    return 0;
}

/*-------------- setlibname() -----------------------------*/

bool_t
_setlibname_1_svc(tarpc_setlibname_in *in, tarpc_setlibname_out *out,
                 struct svc_req *rqstp)
{
    UNUSED(rqstp);
    memset(out, 0, sizeof(*out));
    VERB("PID=%d TID=%llu: Entry %s",
         (int)getpid(), (unsigned long long int)pthread_self(),
         "setlibname");

    out->common._errno = tarpc_setlibname((in->libname.libname_len == 0) ?
                                          NULL : in->libname.libname_val);
    out->retval = (out->common._errno == 0) ? 0 : -1;
    out->common.duration = 0;

    return true;
}

/*-------------- rpc_find_func() ----------------------*/

bool_t
_rpc_find_func_1_svc(tarpc_rpc_find_func_in  *in,
                     tarpc_rpc_find_func_out *out,
                     struct svc_req          *rqstp)
{
    api_func func;

    UNUSED(rqstp);

    memset(out, 0, sizeof(*out));

    out->find_result = tarpc_find_func(in->common.lib_flags,
                                       in->func_name, &func);
    return true;
}

/*-------------- sizeof() -------------------------------*/
#define MAX_TYPE_NAME_SIZE 30
typedef struct {
    char           type_name[MAX_TYPE_NAME_SIZE];
    tarpc_ssize_t  type_size;
} type_info_t;

static type_info_t type_info[] =
{
    {"bool", sizeof(bool)},
    {"te_bool", sizeof(bool)},
    {"char", sizeof(char)},
    {"short", sizeof(short)},
    {"int", sizeof(int)},
    {"long", sizeof(long)},
    {"long long", sizeof(long long)},
    {"te_errno", sizeof(te_errno)},
    {"size_t", sizeof(size_t)},
    {"socklen_t", sizeof(socklen_t)},
    {"struct timeval", sizeof(struct timeval)},
   {"struct linger", sizeof(struct linger)},
    {"struct in_addr", sizeof(struct in_addr)},
    {"struct ip_mreq", sizeof(struct ip_mreq)},
#if HAVE_STRUCT_TCP_INFO
    {"struct tcp_info", sizeof(struct tcp_info)},
#endif
    {"struct ip_mreq_source", sizeof(struct ip_mreq_source)},
#if HAVE_STRUCT_IP_MREQN
    {"struct ip_mreqn", sizeof(struct ip_mreqn)},
#endif
    {"struct sockaddr", sizeof(struct sockaddr)},
    {"struct sockaddr_in", sizeof(struct sockaddr_in)},
    {"struct sockaddr_in6", sizeof(struct sockaddr_in6)},
    {"struct sockaddr_storage", sizeof(struct sockaddr_storage)},
    {"struct cmsghdr", sizeof(struct cmsghdr)},
    {"struct msghdr", sizeof(struct msghdr)},
};

/*-------------- get_sizeof() ---------------------------------*/
bool_t
_get_sizeof_1_svc(tarpc_get_sizeof_in *in, tarpc_get_sizeof_out *out,
                  struct svc_req *rqstp)
{
    uint32_t i;

    UNUSED(rqstp);

    out->size = -1;

    if (in->typename == NULL)
    {
        ERROR("Name of type not specified");
        return false;
    }

    if (in->typename[0] == '*')
    {
        out->size = sizeof(void *);
        return true;
    }

    for (i = 0; i < sizeof(type_info) / sizeof(type_info_t); i++)
    {
        if (strcmp(in->typename, type_info[i].type_name) == 0)
        {
            out->size = type_info[i].type_size;
            return true;
        }
    }

    ERROR("Unknown type (%s)", in->typename);
#if 0
    out->common._errno = TE_RC(TE_TA_UNIX, TE_EINVAL);
#endif
    return true;
}

/*-------------- get_addrof() ---------------------------------*/
bool_t
_get_addrof_1_svc(tarpc_get_addrof_in *in, tarpc_get_addrof_out *out,
                  struct svc_req *rqstp)
{
    void *addr = rcf_ch_symbol_addr(in->name, 0);

    UNUSED(rqstp);

    out->addr = addr == NULL ? 0 : rcf_pch_mem_alloc(addr);

    return true;
}

/*-------------- get_var() ---------------------------------*/
bool_t
_get_var_1_svc(tarpc_get_var_in *in, tarpc_get_var_out *out,
                   struct svc_req *rqstp)
{
    void *addr = rcf_ch_symbol_addr(in->name, 0);

    UNUSED(rqstp);

    if (addr == NULL)
    {
        ERROR("Variable %s is not found", in->name);
        out->found = false;
        return true;
    }

    out->found = true;

    switch (in->size)
    {
        case 1: out->val = *(uint8_t *)addr; break;
        case 2: out->val = *(uint16_t *)addr; break;
        case 4: out->val = *(uint32_t *)addr; break;
        case 8: out->val = *(uint64_t *)addr; break;
        default: return false;
    }

    return true;
}

/*-------------- set_var() ---------------------------------*/
bool_t
_set_var_1_svc(tarpc_set_var_in *in, tarpc_set_var_out *out,
               struct svc_req *rqstp)
{
    void *addr = rcf_ch_symbol_addr(in->name, 0);

    UNUSED(rqstp);
    UNUSED(out);

    if (addr == NULL)
    {
        ERROR("Variable %s is not found", in->name);
        out->found = false;
        return true;
    }

    out->found = true;

    switch (in->size)
    {
        case 1: *(uint8_t *)addr  = in->val; break;
        case 2: *(uint16_t *)addr = in->val; break;
        case 4: *(uint32_t *)addr = in->val; break;
        case 8: *(uint64_t *)addr = in->val; break;
        default: return false;
    }

    return true;
}
