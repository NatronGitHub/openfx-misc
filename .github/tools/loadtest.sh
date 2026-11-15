#!/bin/bash
# Plugin load test used by the CI: build ofxload.cpp and run it on every
# *.ofx.bundle found under the given directories (default: the source tree).
# Usage (from the top of the source tree): .github/tools/loadtest.sh [DIR...]
set -eo pipefail

case "$(uname -s)" in
    Darwin) arch=MacOS; libs= ;;
    Linux) arch=Linux-x86-64; libs=-ldl ;;
    MINGW*|MSYS*) arch=Win64; libs= ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

tools=$(dirname "$0")
"${CXX:-c++}" -std=c++11 -Wall -Iopenfx/include "$tools/ofxload.cpp" -o ofxload $libs

# Skip the openfx submodule (not our plugins) and DebugProxy (debug builds
# only), which wraps the plugin named by $OFX_DEBUGPROXY_BINARY.
binaries=()
while IFS= read -r f; do binaries+=("$f"); done < <(
    find "${@:-.}" \( -path ./openfx -o -path ./DebugProxy \) -prune -o \
         -path "*.ofx.bundle/Contents/$arch/*.ofx" -print | sort)
if [ ${#binaries[@]} -eq 0 ]; then
    echo "no *.ofx.bundle/Contents/$arch/*.ofx found"
    exit 1
fi
./ofxload "${binaries[@]}"
