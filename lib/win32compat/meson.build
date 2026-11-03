# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.

if not host_windows
    error('win32compat is for a Windows (MinGW-w64) host only')
endif

includes += include_directories('include')

sources += files(
    'dlfcn.c',
    'errno.c',
    'fd.c',
    'fnmatch.c',
    'glob.c',
    'misc.c',
    'process.c',
)

deps += [ dep_threads, dep_winsock, cc.find_library('psapi') ]
