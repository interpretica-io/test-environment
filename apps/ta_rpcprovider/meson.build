# SPDX-License-Identifier: Apache-2.0
# This meson script is intended to be used with the TE_TA_APP macro
# after all the TE libraries and the main TE agent have been built.
# More information can be found in the TE_TA_APP macro description.
#
# Copyright (C) 2020-2022 OKTET Labs Ltd. All rights reserved.

project('ta_rpcprovider', 'c', meson_version: '>= 0.49.0')

cc = meson.get_compiler('c')

#
# Get project options
#
c_args = get_option('te_cppflags')
ld_args = get_option('te_ldflags')
te_libdir = get_option('te_libdir')
te_libs = get_option('te_libs')

# Cross-build to a MinGW Windows agent: the platform compat layer
# (POSIX-on-Windows shims) must be force-included here too, since this
# app is configured by its own meson that does not inherit the platform
# cflags. _FILE_OFFSET_BITS=64 (meson default) is undone so TARPC_FUNC
# name-pasting is not broken by the LFS lseek->lseek64 renames.
if host_machine.system() == 'windows'
    c_args += [ '-include', '/home/mmenshikov/mingw-rpc/types_compat.h',
                '-I/home/mmenshikov/mingw-rpc/include',
                '-D_GNU_SOURCE', '-DRPC_MAXDATASIZE=9000',
                '-U_FILE_OFFSET_BITS', '-Wno-error' ]
endif

#
# Prepare linker arguments
#

# Use static implementation of TE libraries
if host_machine.system() == 'darwin'
    # The ld of macOS understands neither -Bstatic/-Bdynamic nor
    # --as-needed. Link the TE libraries by the full path of their
    # static archive to force static linkage; its ld resolves the
    # circular references between them without a group. System
    # libraries keep linking dynamically as usual.
    foreach l : te_libs
        ld_args += [ join_paths(te_libdir, 'lib' + l + '.a') ]
    endforeach
else
    ld_args += [ '-L' + te_libdir, '-Wl,-Bstatic' ]
    foreach l : te_libs
        ld_args += [ '-l' + l ]
    endforeach

    # Use shared external (system) libraries and only if are needed
    ld_args += [ '-Wl,-Bdynamic', '-Wl,--as-needed' ]
endif

# MinGW Windows agent: the compat runtime (librt.a: POSIX shims + the
# strong RPC _svc stubs for syscalls absent on Windows) and winsock.
if host_machine.system() == 'windows'
    ld_args += [ '-L/home/mmenshikov/mingw-rpc/lib',
                 '-Wl,--no-as-needed',
                 '-lrt', '-lrpc', '-lws2_32',
                 '/usr/x86_64-w64-mingw32/lib/libpcre2-8.a' ]
endif

# External dependencies
deps = []
extra_deps = get_option('extra_deps')
foreach d : extra_deps
    deps += [ dependency(d, required: false) ]
endforeach

deps += [ dependency('threads', required: false) ]
deps += [ dependency('libbsd', required: false) ]

dep_rpc = dependency('libtirpc', required: false)
if dep_rpc.found()
    # Modern Linux distros, such as Fedora 28, do not have RPC
    # support in glibc: libtirpc can be used instead.
    deps += dep_rpc
else
    # Solaris2 (SunOS 5.11) -lnsl
    deps += cc.find_library('nsl', required: false)
endif

# The list of possible dependencies from external (system) libraries
ext_libs = [ 'dl', 'm', 'rt' ]
foreach l : ext_libs
    # Note: the 'static' argument is allowed starting from meson 0.51.0,
    # suppose, if found, a shared version of library is present in a system.
    deps += cc.find_library(l, required: false)
endforeach

#
# Generate symtables
#
symtbl_libs = []
foreach l : te_libs
    symtbl_libs += [ te_libdir + '/lib' + l + '.a' ]
endforeach

te_generate_symtbl = find_program('te_generate_symtbl.sh')

# The symbol table must be read with the target's nm when cross-building
# (host nm mangles PE/COFF symbol names differently).
if host_machine.system() == 'windows'
    nm = find_program('x86_64-w64-mingw32-nm')
else
    nm = find_program('nm')
endif

# Mach-O prefixes every C symbol with an underscore, which the symbol
# table generator has to strip off to get the name the sources use.
# The engine's own meson.build does the same for its symbol tables.
if host_machine.system() == 'darwin'
    te_symtbl_args = [ '--strip-underscore' ]
else
    te_symtbl_args = []
endif

symtbl = custom_target('gen-symtbl', output: 'symtbl.c', capture: true,
                       input: [ symtbl_libs ],
                       command: [ te_generate_symtbl, nm, te_symtbl_args,
                                  '@INPUT@' ]
                      )
#
# Build rpc provider
#
executable(get_option('app-name'),
           sources: [ 'main.c', symtbl ],
           c_args: c_args,
           link_args: ld_args,
           dependencies: deps
          )
