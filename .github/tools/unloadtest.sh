#!/bin/bash
# Plug-in unload test used by the CI: build openfx's plugin-unload-test
# (openfx/Support/tests/PluginUnloadTest.cpp) and run it on every *.ofx.bundle binary found under
# the given directories (default: the source tree), each one twice: dlclose() must really unload
# it, and it must load again. Hosts that unload a plug-in and load it again (e.g. to rescan the
# plug-ins) otherwise keep its old static state. On macOS, a binary with a thread-local variable
# is never unloaded (MH_HAS_TLV_DESCRIPTORS); on Linux, an STB_GNU_UNIQUE symbol (a template static
# or an inline variable with default visibility, with GCC) pins it.
# Usage (from the top of the source tree): .github/tools/unloadtest.sh [DIR...]
# Linux and macOS (no dlopen on Windows). UNLOADTEST=<path>: a prebuilt plugin-unload-test,
# instead of building it with $CXX (c++ by default).
set -eo pipefail
tools=$(dirname "$0")
. "$tools/lib.sh"

arch=$(os_arch) || { echo "unsupported OS $(uname -s)"; exit 1; }
libs=''
case "$arch" in
    Linux-*) libs=-ldl ;;
    MacOS) ;;
    *) echo "unloadtest.sh: Linux and macOS only"; exit 1 ;;
esac

if [ -n "$UNLOADTEST" ]; then
    unloadtest=$UNLOADTEST
else
    unloadtest=$(mktemp "${TMPDIR:-/tmp}/plugin-unload-test.XXXXXX")
    trap 'rm -f "$unloadtest"' EXIT
    "${CXX:-c++}" -std=c++11 -Wall -Iopenfx/include openfx/Support/tests/PluginUnloadTest.cpp -o "$unloadtest" $libs
fi

binaries=()
while IFS= read -r f; do binaries+=("$f" "$f"); done < <(ofx_binaries "$arch" "$@")
if [ ${#binaries[@]} -eq 0 ]; then
    echo "no *.ofx.bundle/Contents/$arch/*.ofx found"
    exit 1
fi
"$unloadtest" "${binaries[@]}"
