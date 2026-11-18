#!/bin/bash
# Build the mangled OSMesa used by Shadertoy's CPU rendering (-DUSE_OSMESA=ON) with
# https://github.com/devernay/osmesa-install (the script of Natron's SDKs), for release.yml:
# Mesa 25.0.7 (the last Mesa with OSMesa, llvmpipe driver), static and mangled
# (libMangledOSMesa32.a), linked with the static libraries of an existing LLVM (>= 15).
# Usage: .github/tools/build-osmesa.sh OSMESA_PREFIX LLVM_PREFIX
# Environment: OSMESA_INSTALL_COMMIT (the osmesa-install commit, default below),
# OSMESA_VERSION (default 25.0.7), PYTHON (python3 with mako, yaml and packaging; default python3),
# MKJOBS (default: the number of CPUs).
# Needs bash, curl, git, patch, xz, a C/C++ compiler, cmake, meson, ninja, pkg-config, bison, flex,
# zlib. The LLVM in LLVM_PREFIX is not rebuilt (LLVM_BUILD=0): building it takes hours.
set -eo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 OSMESA_PREFIX LLVM_PREFIX"
    exit 2
fi
osmesa_prefix=$1
llvm_prefix=$2
commit=${OSMESA_INSTALL_COMMIT:-7b47351ba1b9c11fa91f566b492b91e281b8a656}

work=$(mktemp -d "${TMPDIR:-/tmp}/build-osmesa.XXXXXX")
trap 'rm -rf "$work"' EXIT
git -C "$work" init -q osmesa-install
git -C "$work/osmesa-install" fetch -q --depth 1 https://github.com/devernay/osmesa-install.git "$commit"
git -C "$work/osmesa-install" checkout -q FETCH_HEAD
mkdir -p "$work/osmesa-install/build" "$osmesa_prefix"
cd "$work/osmesa-install/build"
# OSMESA_DRIVER=3: llvmpipe (and softpipe); BUILD_OSDEMO=0: no GLUT needed; SILENT_LOG=0: the
# log goes to the CI output
env OSMESA_PREFIX="$osmesa_prefix" LLVM_PREFIX="$llvm_prefix" LLVM_BUILD=0 \
    OSMESA_VERSION="${OSMESA_VERSION:-25.0.7}" OSMESA_DRIVER=3 BUILD_OSDEMO=0 SILENT_LOG=0 \
    MKJOBS="${MKJOBS:-$(getconf _NPROCESSORS_ONLN)}" CFLAGS="-fPIC" \
    bash ../osmesa-install.sh
test -s "$osmesa_prefix/lib/libMangledOSMesa32.a"
test -s "$osmesa_prefix/include/GL/osmesa.h"
echo "OSMesa installed in $osmesa_prefix"
