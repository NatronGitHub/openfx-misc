#!/bin/bash
# The license files of the MinGW-w64 runtime that -static links into every MSYS2 (UCRT64) bundle:
# winpthreads (MIT/BSD-style) and the MinGW-w64 CRT (ZPL 2.1, parts public domain), whose licenses
# ask for the notice in binaries. Their license files (pacman -Ql of the packages that own the
# static libraries) are copied into OUTDIR/winpthreads and OUTDIR/mingw-w64-crt, for the
# --licenses option of package-release.sh (release.yml, and ci.yml's testing bundles). Missing
# ones fail. (libstdc++ and libgcc, also static, fall under the GCC Runtime Library Exception: no
# notice needed. Nothing else is linked statically: the bundles only need OpenGL and system DLLs,
# and osmesa-install's OSMesa and LLVM, whose licenses are in Shadertoy, are built without zlib,
# zstd and expat.)
#
# Usage (in the MSYS2 shell, with MINGW_PREFIX set): .github/tools/mingw-runtime-licenses.sh OUTDIR
set -eo pipefail

[ $# -eq 1 ] && [ -n "$MINGW_PREFIX" ] || { echo "usage: $0 OUTDIR (in an MSYS2 shell: MINGW_PREFIX)"; exit 2; }
out=$1

collect() { # NAME PACKAGE_OR_FILE...: the license files of these packages (or of the owners of these files)
    local name=$1 pkgs='' a files
    shift
    for a in "$@"; do
        case "$a" in
            /*) pkgs="$pkgs $(pacman -Qqo "$a")" ;;
            *) if pacman -Qq "$a" > /dev/null 2>&1; then pkgs="$pkgs $a"; fi ;;
        esac
    done
    # shellcheck disable=SC2086 # package names, without spaces
    pkgs=$(printf '%s\n' $pkgs | sort -u)
    # shellcheck disable=SC2086,SC2116
    echo "$name: packages $(echo $pkgs)"
    for a in $pkgs; do pacman -Ql "$a" | grep -i -e licen -e copying || true; done
    files=$(for a in $pkgs; do pacman -Ql "$a"; done | awk '$2 ~ /\/share\/licenses\/.*[^\/]$/ { print $2 }')
    if [ -z "$files" ]; then
        # shellcheck disable=SC2086,SC2116
        echo "ERROR: no license file (share/licenses) in the packages of $name: $(echo $pkgs)"
        exit 1
    fi
    mkdir -p "$out/$name"
    for a in $files; do cp "$a" "$out/$name/"; done
    ls -l "$out/$name"
}
collect winpthreads mingw-w64-ucrt-x86_64-libwinpthread "$MINGW_PREFIX/lib/libwinpthread.a"
collect mingw-w64-crt "$MINGW_PREFIX/lib/libmingw32.a" "$MINGW_PREFIX/lib/crt2.o"
