#!/bin/bash
# Plugin load test used by the CI: build ofxload.cpp and run it on every
# *.ofx.bundle found under the given directories (default: the source tree).
# Usage (from the top of the source tree): .github/tools/loadtest.sh [DIR...]
# Windows: the binaries of every Contents/Win* folder (Win64, Win-arm64ec, Win-arm64...), which
# must all load in ofxload's process: LOADTEST_ARCHDIR=<folder> (e.g. Win-arm64) tests one folder
# only (any OS). OFXLOAD=<path>: a prebuilt ofxload (e.g. built with MSVC for ARM64 or Arm64EC),
# instead of building it with $CXX (c++ by default), with GCC-style options.
set -eo pipefail
tools=$(dirname "$0")
. "$tools/lib.sh"

arch=$(os_arch) || { echo "unsupported OS $(uname -s)"; exit 1; }
windows='' libs=''
case "$arch" in
    Linux-*) libs=-ldl ;;
    Win\*) windows=1 ;;
esac
arch=${LOADTEST_ARCHDIR:-$arch}

if [ -n "$OFXLOAD" ]; then
    ofxload=$OFXLOAD
else
    "${CXX:-c++}" -std=c++11 -Wall -Iopenfx/include "$tools/ofxload.cpp" -o ofxload $libs
    ofxload=./ofxload
fi

# Skip the openfx submodule (not our plugins) and DebugProxy (debug builds
# only), which wraps the plugin named by $OFX_DEBUGPROXY_BINARY. DebugProxy is
# also matched by bundle name, for install trees (cmake --install).
binaries=()
while IFS= read -r f; do binaries+=("$f"); done < <(
    find "${@:-.}" \( -path ./openfx -o -path ./DebugProxy -o -name DebugProxy.ofx.bundle \) -prune -o \
         -path "*.ofx.bundle/Contents/$arch/*.ofx" -print | sort)
if [ ${#binaries[@]} -eq 0 ]; then
    echo "no *.ofx.bundle/Contents/$arch/*.ofx found"
    exit 1
fi

status=0
# Windows: the MinGW runtime (and FFTW) must be linked statically (LDFLAGS_ADD=-static),
# and so must the MSVC runtime (/MT), since hosts do not ship these DLLs. ofxload would
# not notice, because MSYS2 puts them on PATH (and Windows may have the MSVC ones).
# The imports come from objdump (MSYS2) or, if it cannot read the binary (e.g. ARM64 with an
# x86-64 binutils) or is missing, from MSVC's dumpbin.
# imports FILE: the DLLs that FILE imports, one per line
imports() {
    local o
    if command -v objdump > /dev/null && o=$(objdump -p "$1" 2> /dev/null); then
        printf '%s\n' "$o" | awk '/DLL Name:/ {print $3}'
    elif command -v dumpbin > /dev/null; then
        dumpbin -nologo -dependents "$(cygpath -w "$1")" | tr -d '\r' |
            awk '/following (delay load )?dependencies/ {d = 1; next} /Summary/ {d = 0} d && tolower($1) ~ /\.dll$/ {print $1}'
    else
        echo "ERROR: cannot read the imports of $1 (objdump or dumpbin)" >&2
        return 1
    fi
}
if [ -n "$windows" ]; then
    for f in "${binaries[@]}"; do
        deps=$(imports "$f") || { status=1; continue; }
        if printf '%s\n' "$deps" | grep -i -E '^(libgcc_s|libstdc\+\+|libwinpthread|libgomp|libfftw3)'; then
            echo "ERROR: $f depends on the MinGW runtime DLLs above"
            status=1
        fi
        if printf '%s\n' "$deps" | grep -i -E '^(vcruntime|msvcp|vcomp|concrt)'; then
            echo "ERROR: $f depends on the MSVC runtime DLLs above (build with /MT)"
            status=1
        fi
    done
fi

# The bundles that contain PIK or CImgBlur (the combined Misc or CImg, the separate one of CMake,
# or of make nomulti) must have the PIKColor or EdgeBlur PyPlug and its icons in
# Contents/Resources/PyPlugs, where Natron loads the PyPlugs of a bundle (and finds each icon
# next to its .py), and not in Contents/Resources itself
for f in "${binaries[@]}"; do
    bundle=${f%/Contents/*}
    case "${bundle##*/}" in
        Misc.ofx.bundle|PIK.ofx.bundle|net.sf.openfx.PIK.ofx.bundle)
            pyplugs="PIKColor.py PIKColor.gizmo fr.inria.PIKColor.png fr.inria.PIKColor.svg" ;;
        CImg.ofx.bundle|CImgBlur.ofx.bundle|net.sf.cimg.CImgBlur.ofx.bundle)
            pyplugs="EdgeBlur.py EdgeBlur.gizmo fr.inria.EdgeBlur.png fr.inria.EdgeBlur.svg" ;;
        *) pyplugs= ;;
    esac
    for r in $pyplugs; do
        if [ ! -f "$bundle/Contents/Resources/PyPlugs/$r" ]; then
            echo "ERROR: $bundle has no Contents/Resources/PyPlugs/$r"
            status=1
        fi
        # (moved, not copied: no dead copy in Contents/Resources)
        if [ -e "$bundle/Contents/Resources/$r" ]; then
            echo "ERROR: $bundle has Contents/Resources/$r (it belongs in Contents/Resources/PyPlugs)"
            status=1
        fi
    done
done

# Duplicate identifier+version are checked across all the binaries given to
# one ofxload run. The Test bundle is checked on its own: it is for developers,
# and in debug builds Misc also registers its TestRender plugins.
installed=() tests=()
for f in "${binaries[@]}"; do
    case "$f" in
        */Test.ofx.bundle/*) tests+=("$f") ;;
        *) installed+=("$f") ;;
    esac
done
# run_ofxload BINARY...: ofxload must exit with 0 and end with its summary line, with at
# least one binary and no error (a process that dies or exits early, even with status 0,
# prints no summary)
run_ofxload() {
    local log
    log=$(mktemp "${TMPDIR:-/tmp}/loadtest.XXXXXX")
    "$ofxload" "$@" | tee "$log" || { rm -f "$log"; return 1; }
    if ! tail -n 1 "$log" | tr -d '\r' | grep -q -E '^[1-9][0-9]* binaries, [0-9]+ plugins, 0 errors$'; then
        echo "ERROR: $ofxload printed no summary line with at least one binary and 0 errors"
        rm -f "$log"
        return 1
    fi
    rm -f "$log"
}
[ ${#installed[@]} -eq 0 ] || run_ofxload "${installed[@]}" || status=1
[ ${#tests[@]} -eq 0 ] || run_ofxload "${tests[@]}" || status=1
exit $status
