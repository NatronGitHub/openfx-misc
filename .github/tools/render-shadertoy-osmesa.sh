#!/bin/bash
# Render test of an OSMesa Shadertoy bundle with hostDemo (release.yml). smoketest.sh skips
# Shadertoy, since the OpenGL-only one doesn't describe in hostDemo (no OpenGL render suite), but
# the OSMesa one renders on the CPU: describe it, render frame 0 in the general context, and check
# that the output is not empty. On macOS, each architecture of the bundle that the machine can run
# (x86_64 through Rosetta, if installed) is tested, with the same architecture of hostDemo.
#
# Usage: .github/tools/render-shadertoy-osmesa.sh PLUGINS_DIR HOSTDEMO
#   PLUGINS_DIR: the directory of Shadertoy.ofx.bundle (e.g. <prefix>/OFX/Plugins)
set -eo pipefail

if [ $# -ne 2 ] || [ ! -d "$1/Shadertoy.ofx.bundle" ] || [ ! -x "$2" ]; then
    sed -n '/^# Usage/,/^#   PLUGINS_DIR/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
plugins=$(cd "$1" && pwd)
hostdemo=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
work=$(mktemp -d "${TMPDIR:-/tmp}/render-shadertoy.XXXXXX")
trap 'rm -rf "$work"' EXIT

run() { # run [ARCH]: describe and render with hostDemo (macOS: as ARCH)
    local pre=() d=$work/${1:-native}
    [ -z "$1" ] || pre=(arch "-$1")
    mkdir "$d"
    echo "== Shadertoy ${1:-}"
    (cd "$d" && OFX_PLUGIN_PATH=$plugins "${pre[@]}" "$hostdemo" --no-std-paths --no-cache --quiet \
        -p net.sf.openfx.Shadertoy --describe describe.json)
    (cd "$d" && OFX_PLUGIN_PATH=$plugins "${pre[@]}" "$hostdemo" --no-std-paths --no-cache --quiet \
        -p net.sf.openfx.Shadertoy -c general --format pfm -o out --frames 0)
    shopt -s nullglob
    local outs=("$d"/out.0*.pfm)
    shopt -u nullglob
    if [ ${#outs[@]} = 0 ]; then
        echo "ERROR: no output"
        exit 1
    fi
    local f
    for f in "${outs[@]}"; do
        # a PFM: 3 header lines, then the floats; an all-zero image means nothing was rendered
        if [ "$(tail -c +64 "$f" | LC_ALL=C tr -d '\000' | wc -c)" -eq 0 ]; then
            echo "ERROR: $f is all zeros"
            exit 1
        fi
        echo "ok: $(basename "$f") ($(wc -c < "$f" | tr -d " ") bytes)"
    done
}

if [ "$(uname)" = Darwin ]; then
    bin=$plugins/Shadertoy.ofx.bundle/Contents/MacOS/Shadertoy.ofx
    for a in $(lipo -archs "$bin"); do
        if arch "-$a" /usr/bin/true 2> /dev/null; then
            run "$a"
        else
            echo "== Shadertoy $a: not tested (this machine can't run $a)"
        fi
    done
else
    run
fi
