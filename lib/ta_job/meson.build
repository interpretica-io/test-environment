# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2021-2022 OKTET Labs Ltd. All rights reserved.

headers += files(
    'ta_job.h',
)

sources += files(
    'ta_job.c',
)

te_libs += [
    'agentlib',
    'rpc_types',
    'tools',
]

#
# pipe2() is a GNU extension: Darwin and the BSDs have none.
#
f = 'pipe2'
if cc.has_function(f, args: te_cflags)
    c_args += [ '-DHAVE_' + f.to_upper() ]
endif

dep_pcre = dependency('libpcre2-8', required: false)
required_deps += 'libpcre2-8'
if not dep_pcre.found()
    missed_deps += 'libpcre2-8'
endif

deps += [
    dep_pcre,
]
