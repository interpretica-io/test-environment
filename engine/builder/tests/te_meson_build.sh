#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Interpretica, Unipessoal Lda. All rights reserved.
#
# Tests for the external repository support of
# engine/builder/te_meson_build.
#
# The test sources the script rather than run it, and calls its
# function process_builder_conf(), which turns the builder
# configuration into what a build reads. The test creates the
# repository in the work directory, so it needs no network.
#
# The test compiles nothing. Its checks stop at the inputs of the
# build: the variables a configuration turns into and where the
# sources are checked out. It does not check that a library from
# such a repository links.
#
# Usage: ./te_meson_build.sh

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILDER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TE_BASE_DIR="$(cd "${BUILDER_DIR}/../.." && pwd)"
MESON_BUILD="${BUILDER_DIR}/te_meson_build"
FETCH="${BUILDER_DIR}/te_fetch_ext_repos"

# shellcheck source=test_lib.sh
. "${SCRIPT_DIR}/test_lib.sh"

for f in "${MESON_BUILD}" "${FETCH}" ; do
    [[ -x "${f}" ]] || {
        echo "ERROR: ${f} is not executable" >&2
        exit 1
    }
done

# The platform the configurations below declare first. It becomes
# TE_HOST, and that is what TE_EXT_REPO binds libraries to when no
# platform is named.
TEST_PLATFORM=default

# Create a directory $1 with one source file $2 and the meson.build
# that names it: the least a library can hold. The test does not
# compile it.
mk_dir() {
    local dir="$1"; shift
    local src="$1"; shift

    mkdir -p "${dir}"
    echo "sources += files('${src}')" >"${dir}/meson.build"
    echo "/* Nothing builds this, see the test header. */" >"${dir}/${src}"
}

# Create a bare repository $1 with one library, tagged v1.
mk_ext_repo() {
    local bare="$1"; shift
    local work="${bare}.work"

    ${GIT} init -q --bare "${bare}"
    ${GIT} init -q "${work}"
    mk_dir "${work}/tapi_ext_selftest" tapi_ext_selftest.c
    ${GIT} -C "${work}" add .
    ${GIT} -C "${work}" commit -q -m "minimal external repository"
    ${GIT} -C "${work}" tag v1
    ${GIT} -C "${work}" push -q "${bare}" HEAD:refs/heads/main --tags
}

# Run "$@" with the functions of te_meson_build.
#
# The script builds only when executed, so the test sources it in a
# subshell with the environment of a build: without 'set -u', which
# the script does not use and its configuration macros would trip
# over, and with the positional parameters cleared, since the script
# takes its configuration from $1. The test then drops the 'set -e'
# and the error trap the script arms, so that a check can inspect a
# failure instead of dying of it.
with_builder() {
    local cmd=("$@")

    (
        set --
        set +u
        TE_BASE="${TE_BASE_DIR}"
        TE_BUILD="${WORK}/build"
        TE_INSTALL="${WORK}/inst"
        TE_INSTALL_NUT="${WORK}/inst/nut"
        TE_INSTALL_SUITE="${WORK}/inst/suites"
        # shellcheck source=../te_meson_build
        . "${MESON_BUILD}"
        set +e
        trap - ERR
        # The script looks for its helpers next to the script that
        # was run, here the test, so point it back at the Builder.
        SCRIPT_DIR="${BUILDER_DIR}"
        "${cmd[@]}"
    )
}

# Write a builder configuration to ${WORK}/builder.conf: the platform
# declaration a real configuration opens with, then the lines
# given on stdin.
mk_conf() {
    {
        echo "TE_PLATFORM([], [], [], [], [], [])"
        cat
    } >"${WORK}/builder.conf"
}

# Process ${WORK}/builder.conf into ${WORK}/processed the way a build
# does.
process_conf() {
    if with_builder process_builder_conf "${WORK}/builder.conf" \
            >"${WORK}/processed" 2>"${WORK}/err" ; then
        return 0
    fi
    fail "processing the configuration failed: $(cat "${WORK}/err")"
    return 1
}

# Print variable $1 of the processed configuration, read the way
# read_processed_builder_conf() reads it: inside a loop, since the
# macros report an error by breaking out of one, and without 'set -u',
# since they test variables that may be unset.
conf_get() {
    local var="$1"

    (
        set +u
        TE_BS_CONF_ERR=
        while true ; do
            # shellcheck source=/dev/null
            . "${WORK}/processed"
            break
        done
        printf '%s\n' "${!var-}"
    )
}

# Check that the processed configuration is refused for a reason
# mentioning $1.
expect_refused() {
    local want="$1"
    local err

    err="$(conf_get TE_BS_CONF_ERR)"
    case "${err}" in
        "") fail "the configuration was accepted" ;;
        *"${want}"*) ok "refused: ${err}" ;;
        *) fail "refused for another reason: ${err}" ;;
    esac
}

# Check that file $2, described as $1, was checked out.
expect_file() {
    local what="$1"; shift
    local file="$1"; shift

    if [[ -f "${file}" ]] ; then
        ok "${what} is checked out"
    else
        fail "${what}: no ${file}"
    fi
}

# Fetch the repositories of the processed configuration into
# ${WORK}/build.
run_fetch() {
    if ( cd "${WORK}" && TE_BUILD="${WORK}/build" \
            "${FETCH}" "${WORK}/processed" ) \
            >"${WORK}/out" 2>&1 ; then
        return 0
    fi
    fail "fetching failed: $(cat "${WORK}/out")"
    return 1
}

##########################################################################

BARE="${WORK}/ext.git"
mk_ext_repo "${BARE}"
LIB_SRC="${WORK}/build/ext-repos/extselftest/tapi_ext_selftest"

step "TE_EXT_REPO declares the repository and its library"
mk_conf <<EOF
TE_EXT_REPO([extselftest], [], [${BARE}], [v1], [tapi_ext_selftest])
EOF
if process_conf ; then
    expect_eq "the configuration error" "$(conf_get TE_BS_CONF_ERR)" ""
    expect_contains "the repository list" "$(conf_get TE_BS_EXT_REPOS)" \
                    extselftest
    expect_eq "the recorded URL" \
              "$(conf_get TE_BS_EXT_REPO_extselftest_URL)" "${BARE}"
    expect_eq "the recorded ref" \
              "$(conf_get TE_BS_EXT_REPO_extselftest_REF)" v1
    expect_contains "the platform library list" \
                    "$(conf_get "${TEST_PLATFORM}_LIBS")" tapi_ext_selftest
    expect_eq "the library sources" \
        "$(conf_get "TE_BS_LIB_${TEST_PLATFORM}_tapi_ext_selftest_SOURCES")" \
        "${LIB_SRC}"
fi

step "A repository name that is not an identifier is refused"
mk_conf <<EOF
TE_EXT_REPO([1extselftest], [], [${BARE}], [v1], [tapi_ext_selftest])
EOF
if process_conf ; then
    expect_refused "does not start with a letter"
fi

step "A repository without a ref is refused"
mk_conf <<EOF
TE_EXT_REPO([extselftest], [], [${BARE}], [], [tapi_ext_selftest])
EOF
if process_conf ; then
    expect_refused "URL and ref are mandatory"
fi

step "The sources land where the configuration says they will"
mk_conf <<EOF
TE_EXT_REPO([extselftest], [], [${BARE}], [v1], [tapi_ext_selftest])
EOF
if process_conf && run_fetch ; then
    expect_file "the library sources" "${LIB_SRC}/tapi_ext_selftest.c"
    expect_file "the library build file" "${LIB_SRC}/meson.build"
fi

finish
