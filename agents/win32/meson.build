# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.

if not host_windows
    error('Windows Test Agent requires a Windows (MinGW-w64) host')
endif

name = get_option('agent-win32-name')
ta_platform = get_option('agent-win32-platform')
ta_libs = get_option('agent-win32-libs')
tadir = join_paths(get_option('agentsdir'), name)

includes = []

c_args = [
    '-fno-strict-aliasing',
    '-DTE_AGT_PLATFORM="' + ta_platform + '"',
]

# The agent is a single file: a host may have an older libwinpthread-1.dll
# of another program in PATH, which lacks the functions the agent imports.
link_args = [ '-static' ]

deps = [ dep_threads, dep_winsock ]
foreach l : [ 'ole32', 'oleaut32', 'psapi' ]
    deps += [ cc.find_library(l) ]
endforeach

sources = files(
    'conf.c',
    'win32.c',
)

ta_symtbl_libs = [
    'tools',
    'rcfpch',
    'rpcserver',
    'agentlib',
    'tad',
]

ta_non_symtbl_libs = [
    'rpcxdrta',
    'rpc_types',
    'rpctransport',
    'conf_oid',
    'comm_net_agent',
    'loggerta',
    'logger_file',
    'logger_core',
]

foreach extra_lib : ta_libs
    if not ta_symtbl_libs.contains(extra_lib)
        ta_symtbl_libs += [ extra_lib ]
    endif
endforeach

foreach l : ta_non_symtbl_libs + ta_symtbl_libs
    deps += [ get_variable('dep_lib_static_' + l) ]
endforeach

if not get_option('sniffers')
    deps += [ dep_lib_rcf_sniffers_dummy ]
endif

# Nothing refers to the constructor which initializes Winsock. The
# symbol table does not list these functions: their prototypes clash
# with the declarations it makes.
exe_deps = deps + [ declare_dependency(link_whole: te_lib_win32compat) ]

generated_sources = []

if get_option('agent-win32-symtbl')
    nm = find_program('nm')
    ta_library = static_library('ta', sources,
                                include_directories: includes,
                                c_args: c_args,
                                dependencies: deps)
    symtbl_src = [ ta_library ]
    foreach l : ta_symtbl_libs
        symtbl_src += [ get_variable('te_lib_' + l) ]
    endforeach
    c_args += [ '-DENABLE_GENERATED_SYMTBL' ]
    symtbl = custom_target('gen-symtbl', output: 'symtbl.c', capture: true,
                           input: [ symtbl_src ],
                           command: [ te_generate_symtbl, nm, '@INPUT@' ])
    generated_sources += [ symtbl ]
endif

rcf_consistency_checks = custom_target('gen-rcf-consistency-check',
    output: 'rcf_consistency_checks.c',
    capture: true, build_always_stale: true, build_by_default: true,
    command: [ te_rcf_consistency_checks, '--generate-c' ])
generated_sources += [ rcf_consistency_checks ]

executable('ta', sources, generated_sources,
           install: true, install_dir: tadir,
           include_directories: includes,
           c_args: c_args,
           link_args: link_args,
           dependencies: exe_deps)
