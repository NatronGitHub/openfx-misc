#!/bin/bash
# Build the mangled OSMesa used by Shadertoy's CPU rendering (-DUSE_OSMESA=ON) with
# https://github.com/devernay/osmesa-install (the script of Natron's SDKs), for release.yml:
# Mesa 25.0.7 (the last Mesa with OSMesa, llvmpipe driver), static and mangled
# (libMangledOSMesa32.a), linked with the static libraries of an existing LLVM (>= 15).
# The Mesa archive is downloaded here (HTTPS only) and checked against its pinned SHA256 before
# osmesa-install.sh runs (which then skips its own download); GLU is not built (BUILD_GLU=0).
# Usage: .github/tools/build-osmesa.sh OSMESA_PREFIX LLVM_PREFIX
#        .github/tools/build-osmesa.sh --mesa-sha256 (prints the SHA256 of the Mesa archive, for
#        release.yml's cache key)
# Environment: OSMESA_INSTALL_COMMIT (the osmesa-install commit, default below),
# OSMESA_VERSION (default 25.0.7) and OSMESA_SHA256 (the SHA256 of mesa-$OSMESA_VERSION.tar.xz;
# needed with another OSMESA_VERSION), PYTHON (python3 with mako, yaml and packaging; default
# python3), MKJOBS (default: the number of CPUs).
# Needs bash, curl, git, patch, xz, sha256sum (or shasum), a C/C++ compiler, cmake, meson, ninja,
# pkg-config, bison, flex, zlib. The LLVM in LLVM_PREFIX is not rebuilt (LLVM_BUILD=0): building
# it takes hours.
set -eo pipefail

# mesa-25.0.7.tar.xz, as published in https://docs.mesa3d.org/relnotes/25.0.7.html
mesa_version_default=25.0.7
mesa_sha256_default=592272df3cf01e85e7db300c449df5061092574d099da275d19e97ef0510f8a6
mesa_version=${OSMESA_VERSION:-$mesa_version_default}
if [ -n "$OSMESA_SHA256" ]; then
    mesa_sha256=$OSMESA_SHA256
elif [ "$mesa_version" = "$mesa_version_default" ]; then
    mesa_sha256=$mesa_sha256_default
else
    echo "OSMESA_VERSION=$mesa_version needs OSMESA_SHA256 (the SHA256 of mesa-$mesa_version.tar.xz)"
    exit 2
fi
if [ "$1" = --mesa-sha256 ] && [ $# -eq 1 ]; then
    echo "$mesa_sha256"
    exit 0
fi
if [ $# -ne 2 ]; then
    echo "Usage: $0 OSMESA_PREFIX LLVM_PREFIX"
    echo "       $0 --mesa-sha256"
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
# The Mesa archive, verified (osmesa-install.sh downloads it without a checksum, with FTP fallbacks,
# unless it is already in its build directory). Mesa 23 and later are at the root of
# archive.mesa3d.org, older ones in older-versions/X.x/.
mesa_archive=mesa-$mesa_version.tar.xz
if [ "${mesa_version%%.*}" -ge 23 ]; then
    mesa_url=https://archive.mesa3d.org/$mesa_archive
else
    mesa_url=https://archive.mesa3d.org/older-versions/${mesa_version%%.*}.x/$mesa_archive
fi
curl -fsSL --proto '=https' --tlsv1.2 -o "$mesa_archive" "$mesa_url"
if command -v sha256sum > /dev/null; then
    echo "$mesa_sha256  $mesa_archive" | sha256sum -c -
else
    echo "$mesa_sha256  $mesa_archive" | shasum -a 256 -c -
fi
# OSMESA_DRIVER=3: llvmpipe (and softpipe); BUILD_OSDEMO=0: no GLUT needed; BUILD_GLU=0: Shadertoy
# doesn't use GLU; SILENT_LOG=0: the log goes to the CI output
env OSMESA_PREFIX="$osmesa_prefix" LLVM_PREFIX="$llvm_prefix" LLVM_BUILD=0 \
    OSMESA_VERSION="$mesa_version" OSMESA_DRIVER=3 BUILD_OSDEMO=0 BUILD_GLU=0 SILENT_LOG=0 \
    MKJOBS="${MKJOBS:-$(getconf _NPROCESSORS_ONLN)}" CFLAGS="-fPIC" \
    bash ../osmesa-install.sh
test -s "$osmesa_prefix/lib/libMangledOSMesa32.a"
test -s "$osmesa_prefix/include/GL/osmesa.h"
echo "OSMesa installed in $osmesa_prefix"
