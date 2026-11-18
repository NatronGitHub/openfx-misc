#!/bin/bash
# Package the openfx-misc binary release downloads of one platform (release.yml):
#   openfx-misc-<VERSION>-<PLATFORM>                   the full set: Misc and CImg of --full, and
#                                                      Shadertoy (OSMesa if available, else OpenGL
#                                                      only), the bundles that Natron ships
#   openfx-misc-Shadertoy-OpenGL-<VERSION>-<PLATFORM>  Shadertoy alone, OpenGL only
#   openfx-misc-Shadertoy-OSMesa-<VERSION>-<PLATFORM>  Shadertoy alone, OpenGL + OSMesa (CPU
#                                                      rendering): the Shadertoy of the full set
#   openfx-misc-DenoiseSharpen-<VERSION>-<PLATFORM>    DenoiseSharpen alone
# Each is a .tar.gz (.zip on Windows) of a directory of the same name, with the bundles and a
# README.txt (what it contains, where to install it, and which downloads exclude each other).
#
# Usage (from the top of the source tree):
#   .github/tools/package-release.sh [--version VERSION] --platform PLATFORM --out DIR \
#       --full DIR --shadertoy-gl DIR [--shadertoy-osmesa DIR] --denoisesharpen DIR
# VERSION: by default, $RELEASE_TAG without its Natron- or openfx-misc- prefix (Natron-2.6.0:
#   2.6.0), else <date>-<commit> (from $GITHUB_SHA, or git).
# Each input DIR holds the *.ofx.bundle directories (e.g. an install prefix's OFX/Plugins):
#   --full: Misc and CImg; --shadertoy-gl and --shadertoy-osmesa: Shadertoy;
#   --denoisesharpen: DenoiseSharpen. The full set gets the Shadertoy of --shadertoy-osmesa;
#   without it (no OSMesa build for this platform), the OpenGL-only one, and the OSMesa download
#   is not made. --osmesa-min-macos V: the minimum macOS of the OSMesa Shadertoy, if it is
#   higher than the other bundles' (11.0), for the README.
# PLATFORM: linux-x86_64, windows-x86_64, macos-universal... (linux-*, windows-*, macos-*).
set -eo pipefail

usage() {
    sed -n '/^# Usage/,/^# PLATFORM/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

version='' platform='' out='' full='' stgl='' stmesa='' ds='' mesamacos=''
while [ $# -gt 0 ]; do
    case "$1" in
        --version) version=$2; shift 2 ;;
        --platform) platform=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --full) full=$2; shift 2 ;;
        --shadertoy-gl) stgl=$2; shift 2 ;;
        --shadertoy-osmesa) stmesa=$2; shift 2 ;;
        --denoisesharpen) ds=$2; shift 2 ;;
        --osmesa-min-macos) mesamacos=$2; shift 2 ;;
        *) usage ;;
    esac
done
if [ -z "$version" ]; then
    if [ -n "$RELEASE_TAG" ]; then
        version=${RELEASE_TAG#Natron-}
        version=${version#openfx-misc-}
    else
        commit=${GITHUB_SHA:-$(git rev-parse HEAD 2> /dev/null || echo unknown)}
        version=$(date -u +%Y%m%d)-${commit:0:7}
    fi
fi
[ -n "$version" ] && [ -n "$platform" ] && [ -n "$out" ] && [ -n "$full" ] && [ -n "$stgl" ] && [ -n "$ds" ] || usage

case "$platform" in
    linux-*)
        os=Linux; ext=tar.gz
        sysdir=/usr/OFX/Plugins; pathsep=':'
        ;;
    macos-*)
        os=macOS; ext=tar.gz
        sysdir=/Library/OFX/Plugins; pathsep=';'
        ;;
    windows-*)
        os=Windows; ext=zip
        sysdir='C:\Program Files\Common Files\OFX\Plugins'; pathsep=';'
        command -v zip > /dev/null || { echo "zip not found"; exit 1; }
        ;;
    *) echo "unknown platform $platform (linux-*, macos-*, windows-*)"; exit 2 ;;
esac

if [ -n "$stmesa" ]; then
    shadertoy_full="Shadertoy.ofx.bundle: Shadertoy, OpenGL + OSMesa (CPU rendering when the host has no OpenGL render)"
else
    shadertoy_full="Shadertoy.ofx.bundle: Shadertoy, OpenGL only (no OSMesa build for $os in this release)"
fi

mkdir -p "$out"
out=$(cd "$out" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/package-release.XXXXXX")
trap 'rm -rf "$work"' EXIT

# readme NAME CONTENTS: the README.txt of the download NAME, whose bundles are described by CONTENTS
readme() {
    cat << EOF
$1
$(printf '%s' "$1" | tr '[:print:]' '=')

openfx-misc $version for $os: OpenFX plug-ins, mainly written for Natron
(https://natrongithub.github.io), also usable in other OpenFX hosts.
Sources, documentation and license (GPLv2 or later):
https://github.com/NatronGitHub/openfx-misc

This download contains:
$2

Installation
------------
Copy the *.ofx.bundle directories (not this directory) into the OpenFX plug-in
directory of $os:
  $sysdir
or into a directory listed in the OFX_PLUGIN_PATH environment variable
(directories separated by '$pathsep'), and restart the host.
EOF
    case "$os" in
        macOS)
            cat << 'EOF'
The bundles are universal (arm64 and x86_64), for macOS 11 or later. They are
not notarized: if the host refuses to load them, remove the quarantine
attribute after copying, e.g.:
  sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/*.ofx.bundle
EOF
            if [ -n "$stmesa" ] && [ -n "$mesamacos" ]; then
                echo "The Shadertoy with OSMesa (also in the full set) needs macOS $mesamacos or later: on"
                echo "older macOS versions, use the OpenGL-only Shadertoy download instead."
            fi
            ;;
        Linux) cat << 'EOF'
The bundles are built on Rocky Linux 9 for x86_64. They need these system
libraries, which are never included: glibc 2.34 or later, libstdc++ and
libgcc_s (GCC 11 or later), the OpenGL library (libGL.so.1), and GCC's OpenMP
runtime (libgomp.so.1, for DenoiseSharpen and the CImg plug-ins; e.g. the
libgomp package). Any other library is in the bundle (<bundle>.ofx.bundle/Libraries).
EOF
            ;;
        Windows) cat << 'EOF'
The bundles are 64-bit, for Windows 10 or later, with the MinGW runtime linked
statically (no extra DLL is needed).
EOF
            ;;
    esac
    cat << EOF

Install only one of each plug-in
--------------------------------
Install EITHER the full set (openfx-misc-$version-$platform) OR standalone
bundles (openfx-misc-Shadertoy-*, openfx-misc-DenoiseSharpen-*), NOT both: the
standalone bundles contain the same plug-ins, with the same identifiers and
versions, as the full set (Shadertoy.ofx.bundle is in the full set,
DenoiseSharpen is in Misc.ofx.bundle), and a host must not find the same
plug-in twice.
EOF
    if [ -n "$stmesa" ]; then cat << 'EOF'
The two Shadertoy downloads (OpenGL only, and OpenGL + OSMesa) are mutually
exclusive too: they are both Shadertoy.ofx.bundle. The OSMesa one is the one in
the full set.
EOF
    fi
    cat << 'EOF'
Natron already includes all these plug-ins: Natron users don't need them.
EOF
}

# package NAME CONTENTS BUNDLE...: the download NAME, with the BUNDLE directories (*.ofx.bundle)
package() {
    local name=$1 contents=$2 b
    shift 2
    mkdir "$work/$name"
    for b in "$@"; do
        if [ ! -d "$b" ]; then
            echo "ERROR: $b not found (for $name)"
            exit 1
        fi
        cp -R "$b" "$work/$name/"
    done
    readme "$name" "$contents" > "$work/$name/README.txt"
    [ "$os" != Windows ] || sed -i 's/$/\r/' "$work/$name/README.txt"
    rm -f "$out/$name.$ext"
    if [ "$ext" = zip ]; then
        (cd "$work" && zip -qr -X "$out/$name.$ext" "$name")
    else
        # COPYFILE_DISABLE: no ._* AppleDouble files (macOS tar)
        (cd "$work" && COPYFILE_DISABLE=1 tar -czf "$out/$name.$ext" "$name")
    fi
    echo "$out/$name.$ext: $(cd "$work/$name" && echo *.ofx.bundle) + README.txt"
}

shadertoy_src=${stmesa:-$stgl}

package "openfx-misc-$version-$platform" \
"- Misc.ofx.bundle: the Misc plug-ins (DenoiseSharpen included)
- CImg.ofx.bundle: the CImg plug-ins
- $shadertoy_full" \
    "$full/Misc.ofx.bundle" "$full/CImg.ofx.bundle" "$shadertoy_src/Shadertoy.ofx.bundle"
package "openfx-misc-Shadertoy-OpenGL-$version-$platform" \
"- Shadertoy.ofx.bundle: Shadertoy, OpenGL only (the host must support OpenGL rendering)" \
    "$stgl/Shadertoy.ofx.bundle"
if [ -n "$stmesa" ]; then
    package "openfx-misc-Shadertoy-OSMesa-$version-$platform" \
"- Shadertoy.ofx.bundle: Shadertoy, OpenGL + OSMesa (CPU rendering when the host has
  no OpenGL render), the same bundle as in the full set" \
        "$stmesa/Shadertoy.ofx.bundle"
fi
package "openfx-misc-DenoiseSharpen-$version-$platform" \
"- DenoiseSharpen.ofx.bundle: DenoiseSharpen alone (the same plug-in as in Misc.ofx.bundle)" \
    "$ds/DenoiseSharpen.ofx.bundle"
