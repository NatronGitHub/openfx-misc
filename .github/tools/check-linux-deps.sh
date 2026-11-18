#!/bin/bash
# Check that the Linux plug-in bundles only need the system libraries of the oldest supported
# Linux (Natron 2.6's baseline, Rocky Linux 9), with symbol versions no newer than its runtime, and
# optionally bundle the other (third-party) shared libraries they need (release.yml).
#
# Usage: .github/tools/check-linux-deps.sh [--bundle] DIR...
# Every <bundle>.ofx.bundle/Contents/<arch>/*.ofx under DIR, and the libraries in its
# <bundle>.ofx.bundle/Libraries, are checked:
# - their direct dependencies (DT_NEEDED) must be system libraries that are never bundled
#   (glibc's libc, libm, libpthread, libdl, librt, the dynamic loader; libstdc++, libgcc_s,
#   libgomp; GLVND's libGL.so.1), or libraries in <bundle>.ofx.bundle/Libraries: the
#   $ORIGIN/../../Libraries rpath of openfx's add_ofx_plugin, relative to Contents/<arch>.
#   With --bundle, a missing third-party library is first copied there (the file that ldd
#   resolves), and checked in turn;
# - ldd must resolve every library ("not found" fails), and the third-party ones from inside
#   the bundle (not from the system, which would hide a wrong bundle layout);
# - every symbol version they need (readelf -V) must be no newer than Rocky Linux 9's runtime:
#   GLIBC_2.34 (glibc 2.34), and the system GCC 11's GLIBCXX_3.4.29 and CXXABI_1.3.13
#   (libstdc++), GCC_7.0.0 (libgcc_s, x86_64; aarch64 has GCC_11.0), GOMP_5.0.1 and OMP_5.0.1
#   (libgomp). The environment
#   variables MAX_GLIBC, MAX_GLIBCXX, MAX_CXXABI, MAX_GCC, MAX_GOMP, MAX_OMP override them.
# Exit status: 0 if every binary passes, 1 otherwise.
set -eo pipefail

bundle=0
if [ "$1" = --bundle ]; then
    bundle=1
    shift
fi
if [ $# -eq 0 ]; then
    sed -n '/^# Usage/,/^# Exit/p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
for t in readelf ldd; do
    command -v $t > /dev/null || { echo "$t not found"; exit 2; }
done

declare -A max=(
    [GLIBC]=${MAX_GLIBC:-2.34} [GLIBCXX]=${MAX_GLIBCXX:-3.4.29} [CXXABI]=${MAX_CXXABI:-1.3.13}
    [GCC]=${MAX_GCC:-7.0.0} [GOMP]=${MAX_GOMP:-5.0.1} [OMP]=${MAX_OMP:-5.0.1}
)
# the system libraries that are never bundled
system='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux.*\.so\.[0-9]+|libstdc\+\+\.so\.6|libgcc_s\.so\.1|libgomp\.so\.1|libGL\.so\.1)$'

# newer A B: true if version A > version B
newer() {
    [ "$1" != "$2" ] && [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" = "$1" ]
}

status=0
# check FILE LIBDIR: check one binary or bundled library (LIBDIR: its bundle's Libraries)
check() {
    local f=$1 libdir=$2 n v p ver path
    local -a needed
    mapfile -t needed < <(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    for n in "${needed[@]}"; do
        [[ $n =~ $system ]] && continue
        path=$(ldd "$f" | awk -v n="$n" '$1 == n && $3 ~ /^\// { print $3 }')
        if [ -e "$libdir/$n" ]; then
            # bundled: ldd must find this copy (the binaries' rpath), not the system's
            if [ "$(realpath "$path" 2> /dev/null)" != "$(realpath "$libdir/$n")" ]; then
                echo "ERROR: $f: $n resolves to ${path:-nothing}, not to the bundled $libdir/$n"
                status=1
            fi
            continue
        fi
        if [ $bundle = 1 ] && [ -n "$path" ]; then
            mkdir -p "$libdir"
            cp -L "$path" "$libdir/$n"
            echo "  bundled $path as $libdir/$n"
            check "$libdir/$n" "$libdir"
        else
            echo "ERROR: $f needs $n, which is not a system library and is not in the bundle (${path:-not found})"
            status=1
        fi
    done
    if ldd "$f" | grep -F 'not found'; then
        echo "ERROR: $f: libraries not found"
        status=1
    fi
    # the versions of .gnu.version_r ("Name: GLIBC_2.17")
    while read -r v; do
        p=${v%%_[0-9]*}
        ver=${v#"${p}"_}
        [ -n "${max[$p]:-}" ] || continue
        if newer "$ver" "${max[$p]}"; then
            echo "ERROR: $f needs $v (newer than ${p}_${max[$p]}): $(readelf -s --wide "$f" | grep -F "@$v" | awk '{print $8}' | head -5 | tr '\n' ' ')"
            status=1
        fi
    done < <(readelf -V --wide "$f" | sed -n 's/.*Name: \([A-Z_]*_[0-9][0-9.]*\).*/\1/p' | sort -u)
}

n=0
while IFS= read -r f; do
    libdir=${f%/*/*/*}/Libraries # <bundle>.ofx.bundle/Libraries
    check "$f" "$libdir"
    for l in "$libdir"/*; do
        [ -f "$l" ] && check "$l" "$libdir"
    done
    printf '%s: %s\n' "$f" "$(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p' | tr '\n' ' ')"
    n=$((n + 1))
done < <(find "$@" -path '*.ofx.bundle/Contents/*/*.ofx' -type f | sort)
if [ $n = 0 ]; then
    echo "no *.ofx.bundle/Contents/*/*.ofx found in $*"
    exit 1
fi
[ $status = 0 ] && echo "$n binaries: only Rocky Linux 9 system libraries and symbol versions (max: $(for k in "${!max[@]}"; do printf '%s_%s ' "$k" "${max[$k]}"; done))"
exit $status
