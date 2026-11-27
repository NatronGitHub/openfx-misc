#!/bin/sh
# Installs openfx_misc_tests.py in Resolve's Workspace > Scripts > Utility
# menu (user folder, see Resolve's Developer/Scripting/README.md, "Script
# Menu and Folders"), records this checkout and pytest for it in
# install.json, and prints how to start Resolve with an install.
#
# Usage: tests/resolve/install-script.sh PREFIX
#   PREFIX: an openfx-misc install prefix (with OFX/Plugins).
#   $PYTEST: the pytest the script runs (default /opt/local/bin/pytest-3.11).
#   $OPENFX_MISC_RESOLVE_RESULTS: the configuration and results directory
#     (default ~/openfx-misc-resolve-results).
set -eu
if [ $# -ne 1 ] || [ ! -d "$1/OFX/Plugins" ]; then
    echo "usage: $0 PREFIX (PREFIX/OFX/Plugins must exist)" >&2
    exit 2
fi
prefix=$(cd "$1" && pwd)
here=$(cd "$(dirname "$0")" && pwd)
checkout=$(cd "$here/../.." && pwd)
pytest=${PYTEST:-/opt/local/bin/pytest-3.11}
config=${OPENFX_MISC_RESOLVE_RESULTS:-$HOME/openfx-misc-resolve-results}
scripts="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Utility"

mkdir -p "$scripts" "$config"
cp "$here/openfx_misc_tests.py" "$scripts/openfx_misc_tests.py"
echo "Installed $scripts/openfx_misc_tests.py"
# The paths are JSON-escaped by Python, which also checks they are valid.
"${PYTHON:-/opt/local/bin/python3.11}" - "$checkout" "$pytest" "$prefix" \
    "$config/install.json" <<'PY'
import json, sys
checkout, pytest, prefix, path = sys.argv[1:]
with open(path, "w") as f:
    json.dump({"checkout": checkout, "pytest": pytest, "prefix": prefix}, f,
              indent=1)
PY
echo "Wrote $config/install.json"
if [ ! -x "$pytest" ]; then
    echo "warning: $pytest is not executable: the script will not run the checks" >&2
fi

cat <<EOT

Save your work and quit Resolve, then start it from Terminal:

  OFX_PLUGIN_PATH="$prefix/OFX/Plugins" "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/MacOS/Resolve" > "$config/resolve-stdout.log" 2>&1

Then run Workspace > Scripts > Utility > openfx_misc_tests. It shows PASS or
FAIL in Workspace > Console when done. Quit Resolve normally (not with kill).
EOT
