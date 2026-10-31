..
  SPDX-License-Identifier: Apache-2.0
  Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
  te-parent: te_agents

.. index:: pair: group; Windows Test Agent
.. _doxid-group__te__agents__win32:

Windows Test Agent
==================

.. toctree::
	:hidden:



.. _doxid-group__te__agents__win32_1te_agent_win32_introduction:

Introduction
~~~~~~~~~~~~

The Windows Test Agent is a native Windows program built by MinGW-w64.
It needs Windows 10 or later: it uses AF_UNIX sockets, the IP Helper API
of Windows Vista and later, and WSAPoll().

Sources of the Windows Test Agent can be found under agents/win32
directory of TE tree.

The Windows Test Agent supports the following features:

* RCF commands: configuration, variables, files, routine calls;

* process and thread creation;

* the configuration tree described below;

* RPC servers and the RPC calls listed below.

TAD protocols and sniffers are not supported yet.



.. _doxid-group__te__agents__win32_1te_agent_win32_build:

Building
~~~~~~~~

The agent is built for a Windows platform by the Builder, as an agent
of type ``win32``. The platform names the MinGW-w64 toolchain in its
host parameter, and the Builder makes a meson cross file of it:

.. ref-code-block:: none

	TE_PLATFORM([win64], [--host=x86_64-w64-mingw32], [], [], [],
	            [logger_core tools conf_oid asn ndn rpcxdr rpc_types \
	             comm_net_agent loggerta rpctransport agentlib \
	             rpcserver rcfpch tad])

	TE_LIB_PARMS([tad], [win64], [], [--without-tad])

	TE_TA_TYPE([win64], [win64], [win32], [], [], [], [], [])

The toolchain is the ``mingw-w64`` package on Debian and Ubuntu, and
the ``mingw-w64`` formula of Homebrew on macOS. MSYS2 provides the same
toolchain on Windows itself, in its UCRT64 environment.

GCC of MinGW-w64 builds for x86 only. An agent for Windows on ARM64 is
built by `llvm-mingw <https://github.com/mstorsjo/llvm-mingw>`_, which
names its tools after the triplet as well. Put its ``bin`` directory into
``PATH`` and take the triplet for ARM64:

.. ref-code-block:: none

	TE_PLATFORM([winarm], [--host=aarch64-w64-mingw32], [], [], [],
	            [logger_core tools conf_oid asn ndn rpcxdr rpc_types \
	             comm_net_agent loggerta rpctransport agentlib \
	             rpcserver rcfpch tad])

	TE_LIB_PARMS([tad], [winarm], [], [--without-tad])

	TE_TA_TYPE([winarm], [winarm], [win32], [], [], [], [], [])

The Builder reads the symbol table of the agent with ``nm`` of the
platform, ``<triplet>-nm`` unless ``NM=`` of the platform parameters
names another one.

TE also needs ``rpcgen`` of glibc on the build host: the ``rpcsvc-proto``
package of Linux distributions and of Homebrew.

The C runtime of Windows has no POSIX layer and no Sun RPC. lib/win32compat
provides the POSIX interfaces TE uses on top of Winsock and the Windows API,
and lib/rpcxdr provides XDR. The libraries of TE are built static there,
and the agent is a single ``ta.exe``.



.. _doxid-group__te__agents__win32_1te_agent_win32_run:

Running
~~~~~~~

RCF starts the agent with rcfunix on a host with the OpenSSH server of
Windows. The ``windows`` element of the agent configuration selects the
commands of cmd.exe, see
:ref:`RCF UNIX Communication Library <doxid-group__te__engine__rcf_1te_engine_rcf_comm_lib_unix>`:

.. ref-code-block:: xml

	<ta name="Agt_W" type="win64" rcflib="rcfunix">
	    <conf name="host">winhost</conf>
	    <conf name="port">23571</conf>
	    <conf name="windows"/>
	</ta>

The agent takes the same arguments as the Unix one:

.. ref-code-block:: none

	ta.exe <TA name> <RCF connection string>

It creates a ``tmp`` directory next to ``ta.exe``, works in it and
removes it on shutdown. Resource locks are kept in ``%TEMP%``, which the
agents of one user share.

Windows has no fork(). A routine RCF asks to run in a process of its own
runs in a new instance of the agent, ``ta.exe exec <routine> <arguments>``,
so only the argv form of the routine arguments is supported there.

A shell command runs in ``cmd.exe``. Set ``TE_WIN32_SHELL`` in the agent
environment to the path of a POSIX shell, such as ``C:\msys64\usr\bin\sh.exe``,
to run the commands as ``<shell> -c <command>`` instead.

A child process gets a job object of its own. Killing its negated process
ID terminates the process and everything it has started, as killing a
process group does on POSIX systems.



.. _doxid-group__te__agents__win32_1te_agent_win32_rpc:

RPC
~~~

An RPC server is a new instance of the agent, ``ta.exe exec
rcf_pch_rpc_server_argv <name>``. It connects to the agent over an AF_UNIX
socket in the directory of ``ta.exe``.

Windows has no fork(), so an RPC server can be created as a root one,
as a thread of another one, or by ``rcf_rpc_server_fork_exec()``. The
child of the latter is a new instance of the agent too: it gets the
environment of its father and nothing else. ``rcf_rpc_server_fork()``
fails with ``TE_EOPNOTSUPP``.

winpthreads cancels a thread only at ``pthread_testcancel()``. A thread
RPC server stops when RCF asks it to finish, but not when it is blocked
in a call and RCF has to cancel it.

The agent implements these RPC calls, the others fail with
``TE_ERPCNOTSUPP``:

* ``getpid``, ``pthread_self``, ``getenv``, ``setenv``, ``unsetenv``,
  ``gethostname``, ``uname``, ``gettimeofday``, ``clock_gettime``;

* ``socket``, ``close``, ``closesocket``, ``bind``, ``connect``,
  ``listen``, ``accept``, ``send``, ``sendto``, ``recv``, ``recvfrom``,
  ``shutdown``, ``getsockname``, ``getpeername``, ``socketpair``,
  ``dup``, ``dup2``, ``getaddrinfo``, ``freeaddrinfo``,
  ``if_nametoindex``, ``if_indextoname``;

* ``setsockopt`` and ``getsockopt`` with integer, linger and timeval
  values;

* ``read``, ``write``, ``readv``, ``writev``, ``pipe``;

* ``poll``, ``select`` and the calls of ``fd_set``;

* ``ioctl`` with ``FIONBIO``, ``FIONREAD`` and ``SIOCATMARK``, ``fcntl``
  with ``F_GETFD``, ``F_SETFD``, ``F_GETFL`` and ``F_SETFL``;

* ``open``, ``lseek``, ``fsync``, ``access``, ``unlink``, ``mkdir``,
  ``rmdir``, ``rename``, ``ftruncate``, ``pread``, ``pwrite``,
  ``te_fstat``, ``te_fstat64``;

* ``simple_sender``, ``simple_receiver``, ``overfill_buffers``;

* ``te_shell_cmd``, ``system``, ``kill``, ``waitpid``, ``ta_kill_death``,
  ``ta_kill_and_wait``;

* ``malloc``, ``free``, ``memcmp``, ``set_buf``, ``get_buf``,
  ``set_buf_pattern``;

* the calls of Winsock: ``wsa_startup``, ``wsa_cleanup``, ``wsa_socket``,
  ``duplicate_socket``, ``wsa_address_to_string``,
  ``wsa_string_to_address``, ``get_current_process_id``,
  ``get_sys_info``;

* event objects: ``create_event``, ``close_event``, ``set_event``,
  ``reset_event``, ``event_select``, ``enum_network_events``,
  ``wait_for_multiple_events``;

* overlapped I/O: ``create_overlapped``, ``delete_overlapped``,
  ``wsa_send``, ``wsa_send_to``, ``wsa_recv``, ``wsa_recv_from``,
  ``wsa_get_overlapped_result``, ``has_overlapped_io_completed``,
  ``cancel_io``, ``completion_callback``, and the extension functions
  ``connect_ex``, ``accept_ex``, ``get_accept_addr``, ``disconnect_ex``;

* the calls of lib/rpcserver: ``setlibname``, ``rpc_find_func``,
  ``get_sizeof``, ``get_addrof``, ``get_var``, ``set_var``,
  ``namespace_id2str``, ``release_rpc_ptr``.

A socket is the Winsock ``SOCKET`` taken as an integer, a file is
a descriptor of the C runtime. ``close``, ``read``, ``write``, ``readv``
and ``writev`` take either kind. ``poll`` and ``select`` take sockets
only, as Winsock does. The errors of Winsock are reported as the POSIX
error codes.

Files are opened as binary ones. Windows tells only whether the owner
may write a file, so ``open`` takes nothing else of the mode, and
``access`` takes ``X_OK`` as granted. ``F_GETFL`` cannot tell whether
a socket is non-blocking.

Winsock has no ``socketpair()``: the agent connects two sockets of the
loopback interface, of AF_INET for ``PF_UNIX`` too. ``dup`` of a socket
makes another socket of the same connection by ``WSADuplicateSocket()``,
and ``dup2`` takes files only. ``rename`` replaces an existing file, as
POSIX does. ``gettimeofday`` reports no time zone.

An overlapped operation in progress fails with ``RPC_E_IO_PENDING``, and
``wsa_get_overlapped_result`` of an incomplete one fails with
``RPC_E_IO_INCOMPLETE``. The buffers of the operation stay with the
overlapped object until ``wsa_get_overlapped_result`` returns them. The
completion routines are ``default_completion_callback``, whose calls
``completion_callback`` reports, and ``empty_completion_callback``.



.. _doxid-group__te__agents__win32_1te_agent_win32_conf:

Configuration tree
~~~~~~~~~~~~~~~~~~

An interface is named by its NDIS name, such as ``ethernet_32769`` or
``loopback_0``, which ``if_nametoindex()`` understands as well. The alias
a user sees, such as "Ethernet 2", may contain spaces, so it is a node of
the interface instead:

* ``/agent/interface``: ``index``, ``alias``, ``link_addr``,
  ``bcast_link_addr``, ``mtu``, ``status``, ``oper_status``,
  ``iface_ip4_fw``, ``iface_ip6_fw``;

* ``/agent/interface/net_addr``: IPv4 and IPv6 addresses with the prefix
  length as the value, and ``broadcast``;

* ``/agent/interface/neigh_static`` and ``/agent/interface/neigh_dynamic``:
  neighbours of IPv4 and IPv6, ``state`` of the dynamic ones;

* ``/agent/route``: IPv4 and IPv6 routes with ``dev``;

* ``/agent/env``, ``/agent/uname``, ``/agent/platform``, ``/agent/dir``,
  ``/agent/tmp_dir``.

Windows keeps the routes Linux puts into the local table, to the own
addresses and to the broadcast ones, in the same table, and adds
a multicast route on every interface. The agent leaves these out, so that
the names of the routes stay unique.



.. _doxid-group__te__agents__win32_1te_agent_win32_source:

Source organization
~~~~~~~~~~~~~~~~~~~

* agents/win32/win32.c - entry point of the Test Agent and the functions of
  the RCF PCH interface;

* agents/win32/conf.c - the configuration tree;

* agents/win32/tarpc.c - the RPC calls of POSIX;

* agents/win32/tarpc_wsa.c - the RPC calls of Winsock which POSIX lacks.
