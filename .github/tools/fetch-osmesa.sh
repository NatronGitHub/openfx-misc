#!/bin/bash
# Download and unpack a prebuilt OSMesa + LLVM of an osmesa-install binary release, for the
# OSMesa variant of Shadertoy (release.yml, macOS and Windows; Linux builds it, build-osmesa.sh).
#
# Usage (from the top of the source tree):
#   .github/tools/fetch-osmesa.sh --check TAG PLATFORM   exit 0 if the asset is listed in
#                                                       .github/parameters/osmesa-install.sha256
#   .github/tools/fetch-osmesa.sh TAG PLATFORM DEST      download, verify, unpack into DEST
# The asset is
#   https://github.com/devernay/osmesa-install/releases/download/TAG/osmesa-install-TAG-PLATFORM.tar.xz
# (PLATFORM: macos-universal, macos-arm64, macos-x86_64, windows-ucrt64-x86_64). Its SHA256 must
# be listed in .github/parameters/osmesa-install.sha256: the checksum is pinned in this repository,
# not taken from the release. The archive is expected to contain, at its top or in one top-level
# directory, the two prefixes of osmesa-install.sh:
#   osmesa/  OSMESA_PREFIX: include/GL/osmesa.h, lib/libMangledOSMesa32.a, lib/pkgconfig/osmesa.pc
#   llvm/    LLVM_PREFIX: bin/llvm-config (relocatable), lib/libLLVM*.a
#   licenses/  the license texts of Mesa and LLVM (CMake's OSMESA_LICENSES_DIR)
# After unpacking, DEST/osmesa, DEST/llvm and DEST/licenses are these directories (OSMESA_PATH,
# LLVM_PATH and OSMESA_LICENSES_DIR), and
# osmesa.pc is made relocatable: its prefix is DEST/osmesa, and the LLVM flags of its Libs.private
# (paths of the build machine) are removed, since CMake adds LLVM's flags from llvm-config.
set -eo pipefail

sums=$(dirname "$0")/../parameters/osmesa-install.sha256
check=0
if [ "$1" = --check ]; then
    check=1
    shift
fi
if { [ $check = 1 ] && [ $# -ne 2 ]; } || { [ $check = 0 ] && [ $# -ne 3 ]; }; then
    sed -n '/^# Usage/,/^# The asset/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
tag=$1
platform=$2
asset=osmesa-install-$tag-$platform.tar.xz
# (OSMESA_INSTALL_DOWNLOAD_URL: another base URL, for local tests, e.g. file:///tmp/assets)
url=${OSMESA_INSTALL_DOWNLOAD_URL:-https://github.com/devernay/osmesa-install/releases/download}/$tag/$asset
expected=$(awk -v a="$asset" '$1 !~ /^#/ && ($2 == a || $2 == "*" a) { print $1 }' "$sums")
if [ -z "$expected" ]; then
    [ $check = 1 ] || echo "ERROR: $asset is not listed in $sums"
    exit 1
fi
[ $check = 0 ] || exit 0

dest=$3
mkdir -p "$dest"
dest=$(cd "$dest" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/fetch-osmesa.XXXXXX")
trap 'rm -rf "$work"' EXIT
echo "Downloading $url"
curl -fsSL --retry 3 -o "$work/$asset" "$url"
if command -v sha256sum > /dev/null; then
    actual=$(sha256sum "$work/$asset" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 "$work/$asset" | cut -d' ' -f1)
fi
if [ "$actual" != "$expected" ]; then
    echo "ERROR: SHA256 of $asset is $actual, expected $expected ($sums)"
    exit 1
fi
mkdir "$work/x"
tar -xJf "$work/$asset" -C "$work/x"
top=$work/x
if [ ! -d "$top/osmesa" ]; then
    top=$(find "$work/x" -mindepth 1 -maxdepth 1 -type d | head -1)
fi
for f in osmesa/include/GL/osmesa.h osmesa/lib/libMangledOSMesa32.a osmesa/lib/pkgconfig/osmesa.pc; do
    [ -f "$top/$f" ] || { echo "ERROR: $asset has no $f"; exit 1; }
done
[ -f "$top/llvm/bin/llvm-config" ] || [ -f "$top/llvm/bin/llvm-config.exe" ] || { echo "ERROR: $asset has no llvm/bin/llvm-config"; exit 1; }
[ -n "$(ls -A "$top/licenses" 2> /dev/null)" ] || { echo "ERROR: $asset has no licenses/"; exit 1; }
rm -rf "$dest/osmesa" "$dest/llvm" "$dest/licenses"
mv "$top/osmesa" "$top/llvm" "$top/licenses" "$dest/"

prefix=$dest/osmesa
if command -v cygpath > /dev/null; then
    prefix=$(cygpath -m "$prefix") # Windows: a native path, for the native pkgconf and compiler
fi
pc=$dest/osmesa/lib/pkgconfig/osmesa.pc
awk -v p="$prefix" '
    /^prefix=/ { print "prefix=" p; next }
    /^Libs\.private:/ {
        out = "Libs.private:"
        for (i = 2; i <= NF; i++) {
            if ($i ~ /^-L/ || $i ~ /\// || $i ~ /^-lLLVM/) continue
            out = out " " $i
        }
        print out; next
    }
    { print }' "$pc" > "$pc.new"
mv "$pc.new" "$pc"
echo "OSMesa in $dest/osmesa, LLVM $("$dest/llvm/bin/llvm-config" --version) in $dest/llvm"
cat "$pc"
