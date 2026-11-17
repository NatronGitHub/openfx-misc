#!/bin/bash
# Compare two sets of OpenFX bundles, e.g. the make bundles and the CMake install
# tree of the same configuration:
#   .github/tools/bundlediff.sh <DIR_A> <DIR_B>
# Each DIR is searched for *.ofx.bundle directories (a bundle found twice in one DIR
# is an error). For each bundle, compare:
# - the bundle names;
# - the file and directory lists;
# - the contents of every file but the binaries (Info.plist, Resources, ...);
# - for each binary (Contents/<arch>/*.ofx): the exported symbols, the rpath, and
#   the plug-ins it registers (identifier and version, with ofxload.cpp), which must
#   be at least one, without an ofxload error (except DebugProxy, which registers the
#   plug-ins of $OFX_DEBUGPROXY_BINARY);
# - the libraries each binary links: a difference is an error, except for OpenGL and
#   pthread (the Makefiles link OpenGL to every bundle, CMake only where it is used;
#   SupportExt's Threads::Threads links libpthread with glibc < 2.34), which is printed.
# The binaries in Contents/Libraries (osxDeploy.sh) are compared by name only.
# Contents/_CodeSignature on one side only is an expected difference, reported but not
# an error: openfx's CMake signs the installed macOS bundles (with or without
# OFX_DEPLOY_BUNDLES), make does not.
# Run from the top of the source tree (ofxload is built from .github/tools/ofxload.cpp).
# Exit status: 0 if the bundles match, 1 otherwise.
set -eo pipefail

if [ $# -ne 2 ]; then
    echo "Usage: $0 <DIR_A> <DIR_B>"
    exit 2
fi
a=$1
b=$2

case "$(uname -s)" in
    Darwin) arch=MacOS; libs= ;;
    Linux) arch=Linux-$(uname -m); [ "$arch" = Linux-x86_64 ] && arch=Linux-x86-64; libs=-ldl ;;
    *_NT-*) arch=Win64; libs= ;;
    *) echo "unsupported OS $(uname -s)"; exit 2 ;;
esac

work=$(mktemp -d "${TMPDIR:-/tmp}/bundlediff.XXXXXX")
trap 'rm -rf "$work"' EXIT
tools=$(dirname "$0")
"${CXX:-c++}" -std=c++11 -Wall -Iopenfx/include "$tools/ofxload.cpp" -o "$work/ofxload" $libs

# bundles DIR: the bundle paths in DIR, one per line, sorted by name
bundles() {
    find "$1" -name '*.ofx.bundle' -type d -prune -print | awk -F/ '{print $NF "\t" $0}' | sort
}
bundles "$a" > "$work/a.list"
bundles "$b" > "$work/b.list"
status=0
for l in a b; do
    dups=$(cut -f1 "$work/$l.list" | uniq -d)
    if [ -n "$dups" ]; then
        echo "ERROR: bundles found more than once in ${!l}: $dups"
        exit 2
    fi
done
cut -f1 "$work/a.list" > "$work/a.names"
cut -f1 "$work/b.list" > "$work/b.names"
if ! diff "$work/a.names" "$work/b.names" > "$work/names.diff"; then
    echo "DIFF: bundle names (< $a, > $b):"
    sed 's/^/  /' "$work/names.diff"
    status=1
fi

# exported symbols of a binary
symbols() {
    case "$arch" in
        MacOS) nm -gU "$1" | awk '{print $NF}' ;;
        Win*) objdump -p "$1" | sed -n '/^\[Ordinal\/Name Pointer\] Table/,/^$/p' | awk 'NF>1 {print $NF}' ;;
        *) nm -D --defined-only "$1" | awk '{print $NF}' ;;
    esac | sort -u
}
# rpath of a binary
rpaths() {
    case "$arch" in
        MacOS) otool -l "$1" | awk '/cmd LC_RPATH/ {getline; getline; print $2}' ;;
        Win*) ;;
        *) objdump -p "$1" | awk '$1 == "RPATH" || $1 == "RUNPATH" {print $2}' ;;
    esac
}
# libraries linked by a binary (names only)
linked() {
    case "$arch" in
        MacOS) otool -L "$1" | tail -n +2 | awk '{print $1}' | sed 's,.*/,,' ;;
        Win*) objdump -p "$1" | awk '/DLL Name:/ {print $3}' ;;
        *) objdump -p "$1" | awk '$1 == "NEEDED" {print $2}' ;;
    esac | sort -u
}
# plug-ins registered by a binary
plugins() {
    { "$work/ofxload" "$1" || true; } | sed -n -E 's/^  ([^ ]+ [0-9]+\.[0-9]+|ERROR: .*)$/\1/p' | sort
}

ncompared=0
while IFS= read -r name; do
    pa=$(awk -F'\t' -v n="$name" '$1 == n {print $2}' "$work/a.list")
    pb=$(awk -F'\t' -v n="$name" '$1 == n {print $2}' "$work/b.list")
    ncompared=$((ncompared + 1))
    bstatus=0
    (cd "$pa" && find . -mindepth 1 | sort) > "$work/fa"
    (cd "$pb" && find . -mindepth 1 | sort) > "$work/fb"
    # a bundle signature on one side only: expected (compare the other files)
    sig='^\./Contents/_CodeSignature(/|$)'
    if [ "$(grep -cE "$sig" "$work/fa" || true)" = 0 ] || [ "$(grep -cE "$sig" "$work/fb" || true)" = 0 ]; then
        for s in a b; do
            if grep -qE "$sig" "$work/f$s"; then
                echo "  expected: $name: Contents/_CodeSignature only in ${!s} (bundle signature)"
                grep -vE "$sig" "$work/f$s" > "$work/f$s.tmp" || true
                mv "$work/f$s.tmp" "$work/f$s"
            fi
        done
    fi
    if ! diff "$work/fa" "$work/fb" > "$work/files.diff"; then
        echo "DIFF: $name: file lists (< $pa, > $pb):"
        sed 's/^/  /' "$work/files.diff"
        bstatus=1
    fi
    nfiles=0
    while IFS= read -r f; do
        [ -f "$pa/$f" ] && [ -f "$pb/$f" ] || continue
        nfiles=$((nfiles + 1))
        case "$f" in
            ./Contents/Libraries/*) ;; # deployed libraries: compared by name
            ./Contents/"$arch"/*.ofx)
                for what in symbols rpaths plugins; do
                    "$what" "$pa/$f" > "$work/xa"
                    "$what" "$pb/$f" > "$work/xb"
                    if ! diff "$work/xa" "$work/xb" > "$work/x.diff"; then
                        echo "DIFF: $name: $f: $what (< $pa, > $pb):"
                        sed 's/^/  /' "$work/x.diff"
                        bstatus=1
                    fi
                done
                echo "  $name: $f: $(grep -vc ERROR "$work/xa" || true) plug-ins, $(symbols "$pa/$f" | tr '\n' ' ')"
                if [ "$name" != DebugProxy.ofx.bundle ]; then
                    for pp in "$pa" "$pb"; do
                        plugins "$pp/$f" > "$work/xp"
                        if grep -q ERROR "$work/xp" || ! grep -qv ERROR "$work/xp"; then
                            echo "DIFF: $name: $f: no plug-in, or an ofxload error, in $pp:"
                            grep ERROR "$work/xp" | sed 's/^/  /' || true
                            bstatus=1
                        fi
                    done
                fi
                linked "$pa/$f" > "$work/xa"
                linked "$pb/$f" > "$work/xb"
                if ! diff "$work/xa" "$work/xb" > "$work/x.diff"; then
                    allowed='^[<>] (OpenGL|libGL\.so.*|libOpenGL\.so.*|libGLX\.so.*|opengl32\.dll|libpthread\.so.*)$'
                    if grep -E '^[<>] ' "$work/x.diff" | grep -viqE "$allowed"; then
                        echo "DIFF: $name: $f: linked libraries (< $pa, > $pb):"
                        bstatus=1
                    else
                        echo "  note: $name: $f: linked libraries differ (OpenGL, pthread) (< $pa, > $pb):"
                    fi
                    sed 's/^/    /' "$work/x.diff"
                fi
                ;;
            *)
                if ! cmp -s "$pa/$f" "$pb/$f"; then
                    echo "DIFF: $name: $f: contents differ"
                    bstatus=1
                fi
                ;;
        esac
    done < <(sort -u "$work/fa" "$work/fb")
    if [ $bstatus = 0 ]; then
        echo "SAME: $name ($nfiles files)"
    else
        status=1
    fi
done < <(comm -12 "$work/a.names" "$work/b.names")

if [ $ncompared = 0 ]; then
    echo "no bundle to compare"
    exit 1
fi
[ $status = 0 ] && echo "The bundles match." || echo "The bundles differ."
exit $status
