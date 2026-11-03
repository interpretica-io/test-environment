/* SPDX-License-Identifier: Apache-2.0 */
/** @file
 * @brief IPC library
 *
 * Inter-process communication library internal definitions.
 *
 *
 * Copyright (C) 2004-2022 OKTET Labs Ltd. All rights reserved.
 */

#ifndef __TE_IPC_INTERNAL_H__
#define __TE_IPC_INTERNAL_H__

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef UNIX_PATH_MAX
/** There is no common place with UNIX_PATH_MAX define */
#define UNIX_PATH_MAX   108
#endif

#include "te_stdint.h"
#include "te_defs.h"
#include "te_errno.h"
#include "te_queue.h"


#ifndef SA
/**
 * Cast pointer to 'struct sockaddr *'.
 *
 * @param _p    - a pointer
 *
 * @return pointer to 'struct sockaddr *'
 */
#define SA(_p)      ((struct sockaddr *)(_p))
#endif


#define SUN_NAME(_p)    ipc_sun_name((const struct sockaddr_un *)(_p))


#if 0
#define KTRC(x...)  do { fprintf(stderr, x); fflush(stderr); } while (0)
#else
#define KTRC(x...)
#endif

#ifdef __cplusplus
extern "C" {
#endif


/** Should AF_UNIX be used (AF_INET is used otherwise) */
#define TE_IPC_AF_UNIX
#ifndef TE_IPC_AF_UNIX
#define TE_IPC_AF_INET
#endif

#ifdef TE_IPC_AF_UNIX
#ifdef __linux__
/**
 * The names of the sockets are in the abstract namespace of Linux. The
 * other systems lack it, and the sockets are files in IPC_SUN_DIR_FMT.
 */
#define IPC_SUN_ABSTRACT
#endif

/** The directory of the sockets of a user, of the uid */
#define IPC_SUN_DIR_FMT "/tmp/te_ipc_%u"

/**
 * Make the address of a named socket.
 *
 * @param sa        The address.
 * @param name      The name.
 *
 * @return Status code.
 * @retval TE_ENAMETOOLONG  The name does not fit into the address.
 */
extern te_errno ipc_sun_set(struct sockaddr_un *sa, const char *name);

/**
 * Get the name of a named socket from its address.
 *
 * @param sa        The address.
 *
 * @return The name, empty for an unnamed socket.
 */
extern const char *ipc_sun_name(const struct sockaddr_un *sa);

/**
 * Bind a socket to a name.
 *
 * @param s         The socket.
 * @param name      The name.
 * @param replace   Whether to take the name of a file a closed socket
 *                  has left (a server name), or to fail as the abstract
 *                  namespace does.
 *
 * @return Status code.
 */
extern te_errno ipc_sun_bind(int s, const char *name, bool replace);

/**
 * Remove the file of a socket bound by ipc_sun_bind(). The abstract
 * names go away with the socket by themselves.
 *
 * @param s         The socket, before it is closed.
 */
extern void ipc_sun_unlink(int s);
#endif /* TE_IPC_AF_UNIX */

#define IPC_RETRY   5   /**< Number of retries for connecting to server */
#define IPC_SLEEP   1   /**< Interval (in seconds) between retries */


/**
 * The maximal size of the datagram
 * (has effect only if TE_IPC_AF_UNIX is defined).
 */
#define IPC_SEGMENT_SIZE    2048

/** Structure of the datagram header */
struct ipc_dgram_header {
    size_t length;  /**< Length of the whole message */
    size_t left;    /**< Number of bytes left in the message, including
                         payload of this datagram */
};

/** Structure to store datagram in pool */
struct ipc_datagram {
    /** Tail queue links of list with datagrams */
    TAILQ_ENTRY(ipc_datagram)   links;

    socklen_t            sa_len; /**< Length of the source address */
    struct sockaddr_un   sa;     /**< Source address of the datagram */

    void                *buffer; /**< Buffer with data */
    size_t               octets; /**< Octets in the buffer */

};

/** Definition of type for head of the tail queue with datagrams */
TAILQ_HEAD(ipc_datagrams, ipc_datagram);


/**
 * Store datagram in the pool.
 *
 * @param p_pool    - pointer to the datagram pool
 * @param data      - pointer to data to be owned by routine on success
 * @param len       - length of the datagram
 * @param addr      - source address of the datagram
 * @param addr_len  - length of the address
 *
 * @return Status code.
 * @retval 0        - success
 * @retval TE_ENOMEM   - memory allocation failure
 */
extern int ipc_remember_datagram(struct ipc_datagrams *p_pool,
                                 void *data, size_t len,
                                 struct sockaddr_un *addr,
                                 size_t addr_len);


/** @name
 * Sizes of the internal server/client buffers. It is used to avoid
 * duplicate writing to the TCP channel to increase performance. In the
 * case message is longer than buffer the message will be send in two
 * steps (header + message). There is no reason to make this value
 * greater than MSS.
 */
#define IPC_TCP_SERVER_BUFFER_SIZE 1000
#define IPC_TCP_CLIENT_BUFFER_SIZE 500
/*@}*/


#ifndef TE_IPC_AF_UNIX

/** RPC program name of Test Environment */
#define IPC_TE_NAME   "TE"

/** Possible commands for IPC PMAP server */
enum ipc_pm_command_type_e {
    IPC_PM_REG_SERVER,    /**< IPC Server registers it's own port number */
    IPC_PM_UNREG_SERVER,  /**< IPC Server unregisters itself */
    IPC_PM_GET_SERVER,    /**< IPC Client asks for the server number */
    IPC_PM_BYE,           /**< IPC Client or Server tells 'Bye' and closes
                               connection */
    IPC_PM_KILL           /**< ipc_kill function sends this message */
};

/* Structure of IPC PMAP commands */
struct ipc_pmap_command {
    enum ipc_pm_command_type_e command_type;
    char server_name[UNIX_PATH_MAX];
    uint16_t server_port; /* In network byte order */
};

/* Structure of IPC PMAP answers */
struct ipc_pmap_answer {
    uint16_t data;  /**< 0 - error, non-zero - success / port number */
};


/**
 * Connect to the IPC PMAP server, send specified command, get answer,
 * send bye-bye, get answer and disconnect.
 *
 * @param cmd_type        Type of the command.
 * @param server_name     String to be passed as server_name field
 *                        of the command body, can be NULL.
 * @param port            Number to be passes as server_port field
 *                        of the command body.
 *
 * @return Value from the server answer or zero on error.
 */
extern uint16_t ipc_pmap_process_command(
                    enum ipc_pm_command_type_e cmd_type,
                    const char *server_name, uint16_t port);

#endif /* !TE_IPC_AF_UNIX */

#endif /* !__TE_IPC_INTERNAL_H__ */
