#!/bin/sh
# Installs openfx_misc_tests.lua in Resolve's Workspace > Scripts > Utility
# menu (user folder, see Resolve's Developer/Scripting/README.md, "Script
# Menu and Folders"; the free Resolve 21.2 lists only Lua scripts there),
# creates the results directory, and prints how to start Resolve.
#
# Usage: tests/resolve/install-script.sh PREFIX
#   PREFIX: an openfx-misc install prefix (with OFX/Plugins).
#   $OPENFX_MISC_RESOLVE_RESULTS: the results directory (default
#     ~/openfx-misc-resolve-results); Resolve must then be started with it.
set -eu
if [ $# -ne 1 ] || [ ! -d "$1/OFX/Plugins" ]; then
    echo "usage: $0 PREFIX (PREFIX/OFX/Plugins must exist)" >&2
    exit 2
fi
prefix=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
results=${OPENFX_MISC_RESOLVE_RESULTS:-$HOME/openfx-misc-resolve-results}
scripts="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Utility"

mkdir -p "$scripts" "$results"
cp "$here/openfx_misc_tests.lua" "$scripts/openfx_misc_tests.lua"
echo "Installed $scripts/openfx_misc_tests.lua"
# An earlier version of the tests installed a Python script, which the free
# Resolve does not list.
if [ -f "$scripts/openfx_misc_tests.py" ]; then
    rm "$scripts/openfx_misc_tests.py"
    echo "Removed $scripts/openfx_misc_tests.py"
fi
env=
if [ -n "${OPENFX_MISC_RESOLVE_RESULTS:-}" ]; then
    env="OPENFX_MISC_RESOLVE_RESULTS=\"$results\" "
fi

cat <<EOT

1. Save your work and quit Resolve, then start it from Terminal:

  ${env}OFX_PLUGIN_PATH="$prefix/OFX/Plugins" "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/MacOS/Resolve" > "$results/resolve-stdout.log" 2>&1

2. Run Workspace > Scripts > Utility > openfx_misc_tests (nothing shows in
   the Console: the script prints to resolve-stdout.log).
3. Quit Resolve normally (DaVinci Resolve > Quit, not kill).
4. In $(cd "$here/../.." && pwd), run: pytest tests/resolve -rs
EOT
