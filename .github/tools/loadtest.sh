#!/bin/bash
# Plugin load test used by the CI: build ofxload.cpp and run it on every
# *.ofx.bundle found under the given directories (default: the source tree).
# Usage (from the top of the source tree): .github/tools/loadtest.sh [DIR...]
set -eo pipefail

case "$(uname -s)" in
    Darwin) arch=MacOS; libs= ;;
    Linux) arch=Linux-x86-64; libs=-ldl ;;
    *_NT-*) arch=Win64; libs= ;; # MSYS2: MINGW64_NT-*, UCRT64_NT-*, CLANG64_NT-*, MSYS_NT-*...
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

tools=$(dirname "$0")
"${CXX:-c++}" -std=c++11 -Wall -Iopenfx/include "$tools/ofxload.cpp" -o ofxload $libs

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
# Windows: the MinGW runtime must be linked statically (LDFLAGS_ADD=-static),
# since hosts do not ship these DLLs. ofxload would not notice, because MSYS2
# puts them on PATH.
if [ "$arch" = Win64 ]; then
    command -v objdump > /dev/null || { echo "objdump not found"; exit 1; }
    for f in "${binaries[@]}"; do
        if objdump -p "$f" | grep -i -E 'DLL Name: *(libgcc_s|libstdc\+\+|libwinpthread|libgomp)'; then
            echo "ERROR: $f depends on the MinGW runtime DLLs above"
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
[ ${#installed[@]} -eq 0 ] || ./ofxload "${installed[@]}" || status=1
[ ${#tests[@]} -eq 0 ] || ./ofxload "${tests[@]}" || status=1
exit $status
