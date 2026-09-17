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

#
# The awk of Darwin and the BSDs does not know --posix and merely warns
# about it, but gawk needs the option to behave predictably.
#
if command -v gawk >/dev/null 2>&1 ; then
    AWK="gawk --posix"
else
    AWK="awk"
fi

${NM} --format=sysv "$@" |
    ${AWK} -vTABLE_NAME=generated_table \
           -vSTRIP_UNDERSCORE=${strip_underscore} \
           -f "${MYDIR}"/te_generate_symtbl
