#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Shell wrapper to generate symbol table.
#
# Copyright (C) 2018-2022 OKTET Labs Ltd. All rights reserved.

MYDIR="$(dirname "$0")"

NM="$1" ; shift

#
# Mach-O prefixes every C symbol with an underscore, so the name has to
# be stripped off to get the one the sources use.
#
strip_underscore=0
if test "$1" = "--strip-underscore" ; then
    strip_underscore=1
    shift
fi

${NM} --format=sysv "$@" |
    awk --posix -vTABLE_NAME=generated_table \
                -vSTRIP_UNDERSCORE=${strip_underscore} \
                -f "${MYDIR}"/te_generate_symtbl
