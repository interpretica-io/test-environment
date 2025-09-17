# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2025 Interpretica, Unipessoal Lda. All rights reserved.

ta_wifi_helpers_include = include_directories('')

lib_ta_wifi_helpers = static_library('ta_wifi_helpers', 'ta_wifi_helpers.c',
                                     install: install_lib,
                                     include_directories: [includes, ta_wifi_helpers_include])
dep_lib_ta_wifi_helpers = declare_dependency(link_with: lib_ta_wifi_helpers,
                                             include_directories: [includes, ta_wifi_helpers_include])
dep_lib_static_ta_wifi_helpers = dep_lib_ta_wifi_helpers

headers += files(
    'ta_wifi_def.h'
)

sources += files(
    'ta_wifi_helpers.c'
)
