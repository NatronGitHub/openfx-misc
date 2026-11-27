#!/bin/sh
# Installs openfx_misc_tests.py in Resolve's Workspace > Scripts > Utility
# menu (user folder, see Resolve's Developer/Scripting/README.md, "Script
# Menu and Folders"), and prints how to start Resolve with an install.
#
# Usage: tests/resolve/install-script.sh [PREFIX]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
dest="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Utility"
mkdir -p "$dest"
cp "$here/openfx_misc_tests.py" "$dest/openfx_misc_tests.py"
echo "Installed $dest/openfx_misc_tests.py"
prefix=${1:-PREFIX}
cat <<EOT

Start Resolve from Terminal (quit any running Resolve first):

  OFX_PLUGIN_PATH="$prefix/OFX/Plugins" "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/MacOS/Resolve"

Then run Workspace > Scripts > Utility > openfx_misc_tests, wait for
"results in ..." in Workspace > Console, and quit Resolve normally.
Check the results with:

  pytest tests/resolve
EOT
