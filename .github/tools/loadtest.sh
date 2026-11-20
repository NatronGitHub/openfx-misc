#!/bin/bash
# Plugin load test used by the CI: build ofxload.cpp and run it on every
# *.ofx.bundle found under the given directories (default: the source tree).
# Usage (from the top of the source tree): .github/tools/loadtest.sh [DIR...]
# Windows: the binaries of every Contents/Win* folder (Win64, Win-arm64ec, Win-arm64...), which
# must all load in ofxload's process: LOADTEST_ARCHDIR=<folder> (e.g. Win-arm64) tests one folder
# only (any OS). OFXLOAD=<path>: a prebuilt ofxload (e.g. built with MSVC for ARM64 or Arm64EC),
# instead of building it with $CXX (c++ by default), with GCC-style options.
set -eo pipefail

windows=
case "$(uname -s)" in
    Darwin) arch=MacOS; libs= ;;
    Linux) arch=Linux-x86-64; libs=-ldl ;;
    # MSYS2: MINGW64_NT-*, UCRT64_NT-*, CLANG64_NT-*, MSYS_NT-*... (also Git for Windows' bash)
    *_NT-*) arch='Win*'; libs=; windows=1 ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac
arch=${LOADTEST_ARCHDIR:-$arch}

tools=$(dirname "$0")
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
[ ${#installed[@]} -eq 0 ] || "$ofxload" "${installed[@]}" || status=1
[ ${#tests[@]} -eq 0 ] || "$ofxload" "${tests[@]}" || status=1
exit $status
