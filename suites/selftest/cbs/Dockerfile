# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 OKTET Labs Ltd. All rights reserved.
#
# Build environment for the Test Environment continuous build.
#
# The package names come from the dependency lists in
# doc/sphinx/pages/group_te_user.rst. Two of them are not in those
# lists, and both are needed for any build at all:
#
#  - rsync, which engine/builder/te_cross_build_meson drives even when
#    the platform is built locally;
#
#  - rpcsvc-proto, which is where rpcgen lives since Sun RPC left
#    glibc.
#
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Toolchain.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential pkg-config meson ninja-build \
        bison flex gawk file wget ssh xsltproc rsync rpcsvc-proto \
        ca-certificates git python3 && \
    rm -rf /var/lib/apt/lists/*

# Test Engine.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libxml2-dev libpopt-dev libjansson-dev libyaml-dev \
        libcurl4-openssl-dev libglib2.0-dev libssl-dev \
        libpcre2-dev libbsd-dev libreadline-dev libncurses-dev && \
    rm -rf /var/lib/apt/lists/*

# Test Agent.
#
# The development files of expect are in tcl-expect-dev, not in the
# expect-dev which meson.build names: on the current Debian derivatives
# expect-dev is a virtual package provided by expect, and that one
# carries neither expect.h nor the unversioned libexpect.so. The header
# lands in tcl8.6/, which is the layout lib/tad already knows about, and
# links against libtcl8.6 as well.
#
RUN apt-get update && apt-get install -y --no-install-recommends \
        libpcap-dev libtirpc-dev libnl-3-dev \
        libpam0g-dev libelf-dev libsnmp-dev \
        tcl-expect-dev tcl8.6-dev && \
    rm -rf /var/lib/apt/lists/*
