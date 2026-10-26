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

* the configuration tree described below.

RPC calls, TAD protocols and sniffers are not supported yet.



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

* agents/win32/conf.c - the configuration tree.
