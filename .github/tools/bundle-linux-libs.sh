#!/bin/bash
# Copy third-party shared libraries into a Linux plug-in bundle (release.yml): Shadertoy with the
# OSMesa built by build-osmesa.sh needs the libz and libzstd of LLVM's llvm-config --system-libs.
#
# Usage: .github/tools/bundle-linux-libs.sh <bundle>.ofx.bundle SONAME...
# Each SONAME (e.g. libzstd.so.1) that a binary of <bundle>.ofx.bundle/Contents/Linux-*/ needs
# (DT_NEEDED) is copied, as the file that ldd resolves, into <bundle>.ofx.bundle/Libraries: the
# $ORIGIN/../../Libraries rpath of openfx's add_ofx_plugin. A SONAME that no binary needs is
# skipped. Only list third-party libraries: openfx's linuxCheckBundle.sh (run afterwards) reports
# a bundled system library, and any needed library that is still missing from the bundle.
set -eo pipefail

if [ $# -lt 2 ] || [ ! -d "$1" ]; then
    sed -n '/^# Usage/,/^# a bundled/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
bundle=${1%/}
shift
shopt -s nullglob
bins=("$bundle"/Contents/Linux-*/*.ofx)
if [ ${#bins[@]} = 0 ]; then
    echo "ERROR: no $bundle/Contents/Linux-*/*.ofx"
    exit 1
fi
for f in "${bins[@]}"; do
    needed=$(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    for lib in "$@"; do
        grep -qxF "$lib" <<< "$needed" || continue
        [ ! -e "$bundle/Libraries/$lib" ] || continue
        path=$(ldd "$f" | awk -v n="$lib" '$1 == n && $3 ~ /^\// { print $3 }')
        if [ -z "$path" ]; then
            echo "ERROR: $f needs $lib, which ldd doesn't find"
            exit 1
        fi
        mkdir -p "$bundle/Libraries"
        cp -L "$path" "$bundle/Libraries/$lib"
        echo "bundled $path as $bundle/Libraries/$lib"
    done
done
