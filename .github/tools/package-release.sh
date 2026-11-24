#!/bin/bash
# Package the openfx-misc binary release downloads of one platform (release.yml):
#   openfx-misc-<VERSION>-<PLATFORM>                   the full set: Misc and CImg of --full, and
#                                                      Shadertoy (OSMesa if available, else OpenGL
#                                                      only), the bundles that Natron ships
#   openfx-misc-Shadertoy-OpenGL-<VERSION>-<PLATFORM>  Shadertoy alone, OpenGL only
#   openfx-misc-Shadertoy-OSMesa-<VERSION>-<PLATFORM>  Shadertoy alone, OpenGL + OSMesa (CPU
#                                                      rendering): the Shadertoy of the full set
#   openfx-misc-DenoiseSharpen-<VERSION>-<PLATFORM>    DenoiseSharpen alone
#   openfx-misc-NoOp-<VERSION>-<PLATFORM>              NoOp alone (its Host Info button shows what
#                                                      the host supports)
# Each is a .tar.gz (.zip on Windows) of a directory of the same name, with the bundles, a
# README.txt (what it contains, where to install it, and which downloads exclude each other),
# LICENSE.txt (openfx-misc's GPLv2) and LICENSES/: the licenses of the third-party code in the
# bundles (openfx's Support library in every bundle; freeglut's text renderer and font data and
# pixman's blend modes, from SupportExt, in Misc.ofx; CImg (the full CeCILL-C text) and pocketfft in
# CImg.ofx; and a copy of each bundle's Contents/Resources/licenses/, e.g. Mesa and LLVM, linked
# statically into the OSMesa Shadertoy, or the libraries bundled on Linux). A bundle with bundled libraries (Libraries/,
# Contents/Libraries/, Contents/Frameworks/, or a DLL) but no Contents/Resources/licenses/ is an
# error, and so is an OSMesa Shadertoy without it.
#
# Usage (from the top of the source tree, whose LICENSE, openfx/LICENSE.md, SupportExt and CImg
# sources give the license texts; the script itself, its CeCILL-C text and its parameters may come
# from another checkout, e.g. release.yml's .release-tools):
#   .github/tools/package-release.sh [--version VERSION] --platform PLATFORM --out DIR \
#       --full DIR --shadertoy-gl DIR [--shadertoy-osmesa DIR] --denoisesharpen DIR --noop DIR \
#       [--licenses DIR] \
#       [--fft pocketfft|pow2|fftw3]
# --fft: the FFT of the CImg plug-ins (CMake's FFT; default pocketfft, whose
#   CImg/pocketfft_hdronly.h, downloaded at configure, is then required for its license).
# --licenses DIR: license files of code linked into every bundle of this platform, copied into
#   every archive's LICENSES/ (Windows: the MinGW-w64 runtime and winpthreads that -static links;
#   required for windows-*, except windows-*-msvc: MSVC builds, whose static runtime (/MT) needs
#   no notice).
# VERSION: by default, $RELEASE_TAG without its Natron- or openfx-misc- prefix (Natron-2.6.0:
#   2.6.0), else <date>-<commit> (from $GITHUB_SHA, or git).
# Each input DIR holds the *.ofx.bundle directories (e.g. an install prefix's OFX/Plugins):
#   --full: Misc and CImg; --shadertoy-gl and --shadertoy-osmesa: Shadertoy;
#   --denoisesharpen: DenoiseSharpen; --noop: NoOp. The full set gets the Shadertoy of --shadertoy-osmesa;
#   without it (no OSMesa build for this platform), the OpenGL-only one, and the OSMesa download
#   is not made.
# PLATFORM: linux-x86_64, windows-x86_64, macos-universal... (linux-*, windows-*, macos-*);
#   windows-*-msvc (e.g. windows-x86_64-msvc, windows-arm64x-msvc): built with MSVC (nightly.yml).
set -eo pipefail

usage() {
    sed -n '/^# Usage/,/^# PLATFORM/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
}

version='' platform='' out='' full='' stgl='' stmesa='' ds='' noop='' extra='' fft=pocketfft msvc=
while [ $# -gt 0 ]; do
    case "$1" in
        --version) version=$2; shift 2 ;;
        --platform) platform=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        --full) full=$2; shift 2 ;;
        --shadertoy-gl) stgl=$2; shift 2 ;;
        --shadertoy-osmesa) stmesa=$2; shift 2 ;;
        --denoisesharpen) ds=$2; shift 2 ;;
        --noop) noop=$2; shift 2 ;;
        --licenses) extra=$2; shift 2 ;;
        --fft) fft=$2; shift 2 ;;
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
[ -n "$version" ] && [ -n "$platform" ] && [ -n "$out" ] && [ -n "$full" ] && [ -n "$stgl" ] && [ -n "$ds" ] && [ -n "$noop" ] || usage

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
        # zip (MSYS2), else 7-Zip (Git for Windows' bash on the GitHub runners has no zip)
        command -v zip > /dev/null || command -v 7z > /dev/null || { echo "zip or 7z not found"; exit 1; }
        case "$platform" in
            *-msvc) msvc=1 ;;
            # -static: every bundle contains the MinGW-w64 runtime and winpthreads
            *) [ -n "$(ls -A "$extra" 2> /dev/null)" ] || { echo "ERROR: $platform needs --licenses DIR (MinGW-w64 runtime, winpthreads)"; exit 1; } ;;
        esac
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

The bundles are universal (arm64 and x86_64), for macOS 14 or later. They are
not notarized: if the host refuses to load them, remove the quarantine
attribute after copying, e.g.:
  sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/*.ofx.bundle
EOF
            ;;
        Linux) cat << 'EOF'

The bundles are built on Rocky Linux 9 for x86_64. They need these system
libraries, which are never included: glibc 2.34 or later, libstdc++ and
libgcc_s (GCC 11 or later), the OpenGL library (libGL.so.1), and GCC's OpenMP
runtime (libgomp.so.1, e.g. the libgomp package), which Misc.ofx (DenoiseSharpen),
CImg.ofx and the standalone net.sf.openfx.DenoiseSharpen.ofx need. Any other library is in the
bundle (<bundle>.ofx.bundle/Libraries).
EOF
            ;;
        Windows)
            if [ -z "$msvc" ]; then cat << 'EOF'

The bundles are 64-bit, for Windows 10 or later, with the MinGW runtime linked
statically (no extra DLL is needed): the licenses of the MinGW-w64 runtime and
winpthreads are in LICENSES/.
EOF
            else
                cat << 'EOF'

The bundles are built with Microsoft Visual C++, with its runtime linked
statically (no extra DLL is needed). The host finds its binary in the folder
of its architecture, in each bundle's Contents:
EOF
                # the folders of the full set's Misc.ofx.bundle
                for d in "$full"/Misc.ofx.bundle/Contents/Win*; do
                    case "$(basename "$d")" in
                        Win64) echo "- Win64: x64 (Intel and AMD) hosts, Windows 10 or later" ;;
                        Win-arm64ec) echo "- Win-arm64ec: an Arm64X binary, for the Arm64EC hosts of Windows 11 on Arm"
                                     echo "  (and the x64 hosts emulated there that look in this folder)" ;;
                        Win-arm64) echo "- Win-arm64: native ARM64 hosts, Windows 11 on Arm (a folder reserved by"
                                   echo "  the OpenFX packaging specification, not yet normative)" ;;
                        *) echo "- $(basename "$d")" ;;
                    esac
                done
            fi
            ;;
    esac
    cat << EOF

Install only one of each plug-in
--------------------------------
Install EITHER the full set (openfx-misc-$version-$platform) OR standalone
bundles (openfx-misc-Shadertoy-*, openfx-misc-DenoiseSharpen-*,
openfx-misc-NoOp-*), NOT both: the standalone bundles contain the same
plug-ins, with the same identifiers and versions, as the full set
(Shadertoy.ofx.bundle is in the full set, DenoiseSharpen and NoOp are in
Misc.ofx.bundle), and a host must not find the same plug-in twice.
EOF
    if [ -n "$stmesa" ]; then cat << 'EOF'

The two Shadertoy downloads (OpenGL only, and OpenGL + OSMesa) are mutually
exclusive too: they are both Shadertoy.ofx.bundle. The OSMesa one is the one in
the full set.
EOF
    fi
    cat << 'EOF'

Natron already includes all these plug-ins: Natron users don't need them.

Licenses
--------
openfx-misc is free software, under the GNU General Public License, version 2
or later: see LICENSE.txt. The bundles also contain third-party code, whose
licenses are in LICENSES/: openfx's Support library (BSD 3-clause) and
TinyThread++ (zlib) in every bundle, freeglut's text renderer and pixman's blend
modes (MIT) in Misc.ofx, CImg (CeCILL-C) and pocketfft (BSD 3-clause) in
CImg.ofx, and, in LICENSES/<bundle>/, those of the libraries linked statically
into a bundle or bundled with it (e.g. Mesa and LLVM in the OSMesa Shadertoy),
which are also in <bundle>.ofx.bundle/Contents/Resources/licenses/.
Misc.ofx also contains code first published under other licences, compatible
with the GPL and distributed under the GPL as part of Misc.ofx (see the
sources): Misc/randomGenerator.cpp (the MT19937 generator of Makoto Matsumoto
and Takuji Nishimura, GNU Library General Public License, version 2 or later)
and the Yadif mode of Deinterlace/Deinterlace.cpp (from FFmpeg's
libavfilter/vf_yadif.c, Michael Niedermayer, GNU Lesser General Public License,
version 2.1 or later). Distortion's PFBarrel (.pfb) file reader contains code
"Copyright (C) 2011 The Pixel Farm Ltd", distributed under the GPL as part of
Misc.ofx.
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
    licenses "$work/$name" "$@"
    readme "$name" "$contents" > "$work/$name/README.txt"
    [ "$os" != Windows ] || sed -i 's/$/\r/' "$work/$name/README.txt"
    rm -f "$out/$name.$ext"
    if [ "$ext" = zip ] && command -v zip > /dev/null; then
        (cd "$work" && zip -qr -X "$out/$name.$ext" "$name")
    elif [ "$ext" = zip ]; then
        (cd "$work" && 7z a -tzip -bd -bso0 "$out/$name.$ext" "$name")
    else
        # COPYFILE_DISABLE: no ._* AppleDouble files (macOS tar)
        (cd "$work" && COPYFILE_DISABLE=1 tar -czf "$out/$name.$ext" "$name")
    fi
    echo "$out/$name.$ext: $(cd "$work/$name" && echo *.ofx.bundle) + README.txt LICENSE.txt LICENSES/: $(cd "$work/$name/LICENSES" && echo *)"
}

# header_block FILE: the first comment block of FILE (its license), up to the first "*/" line
header_block() {
    awk '{ print } /^[[:space:]]*\*\/[[:space:]]*$/ { exit }' "$1"
}

# notice_block FILE TEXT: the first /* ... */ comment block of FILE that contains TEXT (a
# license notice in the middle of a file); fails if there is none
notice_block() {
    awk -v t="$2" '
        !inb && /\/\*/ { inb = 1; b = "" }
        inb { b = b $0 "\n" }
        inb && /\*\// { inb = 0; if (index(b, t)) { printf "%s", b; found = 1; exit } }
        END { exit !found }' "$1" || { echo "ERROR: no comment block with \"$2\" in $1" >&2; return 1; }
}

# The CeCILL-C licence of CImg, in English: CImg/Licence_CeCILL-C_V1-en.txt, a copy of
# https://github.com/GreycLab/CImg/blob/1a9fef439d17b73d9af8d8e59b65df9c989fde7a/Licence_CeCILL-C_V1-en.txt
# (CIMGVERSION of CImg/CImgVersion.mk, CImg 4.0.8, the same as in 4.0.7; git blob
# 2e9ffbaca595a36d06e5f3adbf2e2bb776529494),
# taken from the checkout of this script and checked against this SHA256.
cecill_c=$(dirname "$0")/../../CImg/Licence_CeCILL-C_V1-en.txt
cecill_c_sha256=e134e750de47bfa3119e74a10920774d00a87e361ec6da25463ef1fca97b9261

# bundled_libs BUNDLE: the libraries that the bundle carries, besides its plug-in binary
bundled_libs() {
    find "$1/Libraries" "$1/Contents/Libraries" "$1/Contents/Frameworks" -mindepth 1 -maxdepth 1 2> /dev/null || true
    find "$1/Contents" -iname '*.dll' 2> /dev/null || true
}

# licenses DIR BUNDLE...: LICENSE.txt and LICENSES/ of the download DIR, with these bundles
licenses() {
    local dir=$1 b bn
    shift
    cp "$src/LICENSE" "$dir/LICENSE.txt"
    mkdir "$dir/LICENSES"
    # (openfx's LICENSE.md, then the Support library's LICENSE; openfx before the
    # OFX 1.5.1 merge had the original top-level LICENSE instead)
    { cat "$src/openfx/LICENSE.md"; echo
      if [ -f "$src/openfx/LICENSE" ]; then cat "$src/openfx/LICENSE"; else cat "$src/openfx/Support/LICENSE"; fi; } \
        > "$dir/LICENSES/openfx-Support-BSD-3-Clause.txt"
    # (SupportExt's THREADS component, in every bundle)
    notice_block "$src/SupportExt/tinythread.cpp" "Marcus Geelnard" > "$dir/LICENSES/TinyThread++-zlib.txt"
    [ -z "$extra" ] || cp -R "$extra"/. "$dir/LICENSES/"
    for b in "$@"; do
        bn=$(basename "$b" .ofx.bundle)
        if [ "$bn" = Misc ]; then
            # SupportExt's TEXT component (ofxsOGLTextRenderer.cpp, ofxsOGLFontData.cpp: freeglut)
            # and ofxsMerging.h (pixman's blend modes: Merge, Roto, FrameBlend), only in Misc.ofx
            {
                notice_block "$src/SupportExt/ofxsOGLTextRenderer.cpp" "Permission is hereby granted"
                echo
                notice_block "$src/SupportExt/ofxsOGLFontData.cpp" "Permission is hereby granted"
            } > "$dir/LICENSES/freeglut-MIT.txt"
            notice_block "$src/SupportExt/ofxsMerging.h" "Soren Sandmann Pedersen" > "$dir/LICENSES/pixman-MIT.txt"
        fi
        if [ "$bn" = CImg ]; then
            [ -f "$src/CImg/CImg.h" ] || { echo "ERROR: $src/CImg/CImg.h not found (its license)"; exit 1; }
            header_block "$src/CImg/CImg.h" > "$dir/LICENSES/CImg-notice.txt"
            cp "$cecill_c" "$dir/LICENSES/CImg-Licence_CeCILL-C_V1-en.txt"
            if [ "$fft" = pocketfft ]; then
                [ -f "$src/CImg/pocketfft_hdronly.h" ] || { echo "ERROR: $src/CImg/pocketfft_hdronly.h not found (FFT=pocketfft: its license)"; exit 1; }
                notice_block "$src/CImg/pocketfft_hdronly.h" "Redistribution and use" > "$dir/LICENSES/pocketfft-BSD-3-Clause.txt"
            fi
        fi
        if [ -n "$(ls -A "$b/Contents/Resources/licenses" 2> /dev/null)" ]; then
            cp -R "$b/Contents/Resources/licenses" "$dir/LICENSES/$bn"
        elif [ -n "$(bundled_libs "$b")" ]; then
            echo "ERROR: $b has bundled libraries but no Contents/Resources/licenses:"
            bundled_libs "$b"
            exit 1
        fi
    done
}

src=$PWD
case "$fft" in pocketfft|pow2|fftw3) ;; *) echo "ERROR: --fft $fft (pocketfft, pow2 or fftw3)"; exit 2 ;; esac
for f in LICENSE openfx/LICENSE.md SupportExt/ofxsMerging.h SupportExt/tinythread.cpp; do
    [ -f "$src/$f" ] || { echo "ERROR: $src/$f not found: run from the top of the source tree"; exit 1; }
done
if command -v sha256sum > /dev/null; then
    actual=$(sha256sum "$cecill_c" | cut -d' ' -f1)
else
    actual=$(shasum -a 256 "$cecill_c" | cut -d' ' -f1)
fi
if [ "$actual" != "$cecill_c_sha256" ]; then
    echo "ERROR: SHA256 of $cecill_c is $actual, expected $cecill_c_sha256"
    exit 1
fi
if [ -n "$stmesa" ] && [ -z "$(ls -A "$stmesa/Shadertoy.ofx.bundle/Contents/Resources/licenses" 2> /dev/null)" ]; then
    echo "ERROR: $stmesa/Shadertoy.ofx.bundle has no Contents/Resources/licenses (OSMesa and LLVM: CMake's OSMESA_LICENSES_DIR)"
    exit 1
fi
shadertoy_src=${stmesa:-$stgl}

package "openfx-misc-$version-$platform" \
"- Misc.ofx.bundle: the Misc plug-ins (DenoiseSharpen and NoOp included)
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
"- net.sf.openfx.DenoiseSharpen.ofx.bundle: DenoiseSharpen alone (the same plug-in as in Misc.ofx.bundle).
  Older releases named it DenoiseSharpen.ofx.bundle: remove that old bundle." \
    "$ds/net.sf.openfx.DenoiseSharpen.ofx.bundle"
package "openfx-misc-NoOp-$version-$platform" \
"- net.sf.openfx.NoOpPlugin.ofx.bundle: NoOp alone (the same plug-in as in Misc.ofx.bundle).
  Its \"Host Info...\" button shows what the OpenFX host supports (also saved to
  OFXHostInfo-<host>.txt in the temporary directory), to paste into a bug report." \
    "$noop/net.sf.openfx.NoOpPlugin.ofx.bundle"
