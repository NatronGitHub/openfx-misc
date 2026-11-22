#!/bin/bash
# Windows MSVC build of the downloadable bundles (nightly.yml, ci.yml): Misc, CImg, Shadertoy
# (OpenGL only: the OSMesa Shadertoy is built with MSYS2 UCRT64) and the standalone
# net.sf.openfx.DenoiseSharpen.ofx.bundle and net.sf.openfx.NoOpPlugin.ofx.bundle, with CMake and
# Ninja, then a check of each binary.
#   .github/tools/msvc-build.sh [--no-standalone] x64|arm64x [OUTDIR]
# Run from the top of the source tree, in Git for Windows' bash (its sh patches CImg.h), in an
# MSVC environment (cl, link and dumpbin in the PATH, e.g. ilammy/msvc-dev-cmd): x64 for x64,
# the ARM64 tools (amd64_arm64 cross, or arm64 native) for arm64x.
# - x64: OUTDIR/install/OFX/Plugins, bundles with Contents/Win64;
# - arm64x: an ARM64 build, then an Arm64EC build linked with its objects into Arm64X binaries
#   (cmake/OFXMiscArm64X.cmake): bundles with Contents/Win-arm64ec (the Arm64X binary, for Arm64EC
#   hosts and for the x64 hosts emulated on Windows on Arm that look there) and Contents/Win-arm64
#   (the ARM64 binary, for native ARM64 hosts: a reserved folder of the OFX packaging spec).
# The standalone DenoiseSharpen goes into OUTDIR/install-denoisesharpen/OFX/Plugins, and NoOp into
# OUTDIR/install-noop/OFX/Plugins (a host must not load them with Misc.ofx.bundle, which has the
# same plug-ins); --no-standalone skips them (ci.yml).
# OUTDIR defaults to msvc-<arch>. Its build directories are removed first: every run is a clean
# build (an Arm64EC build must not reuse a CMake cache, or the linker inputs, of an older one).
# Static CRT (CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded, /MT): no vcruntime/msvcp DLL to ship; the
# check below fails on an import of an MSVC runtime DLL.
# No OpenMP: if it is ever enabled for MSVC, vcomp140.dll must be shipped app-local in the bundle
# and pinned, as openfx-gmic does (NatronGitHub/openfx-gmic#108: it pins vcomp140 through the
# address of omp_get_max_threads, so that it is not unloaded with the plug-in by FreeLibrary,
# which crashes when the OpenMP worker threads are still alive).
set -eo pipefail

standalone=1
if [ "$1" = --no-standalone ]; then
    standalone=
    shift
fi
arch=$1
out=${2:-msvc-$arch}
case "$arch" in
    x64|arm64x) ;;
    *) echo "Usage: $0 [--no-standalone] x64|arm64x [OUTDIR]"; exit 2 ;;
esac
command -v cl > /dev/null && command -v dumpbin > /dev/null || { echo "cl or dumpbin not found: run in an MSVC environment"; exit 1; }
mkdir -p "$out"
out=$(cd "$out" && pwd)
w() { cygpath -m "$1"; }

# Debug symbols: /Z7 (Embedded: in the objects, so that parallel compiles don't share a PDB) and
# /DEBUG, with /OPT:REF /OPT:ICF to keep the Release code: each binary gets a .pdb, collected into
# OUTDIR/symbols/<bundle folder>/ (for crash dumps from hosts; not shipped in the bundles).
options=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DOFX_CXX_STANDARD=17
    -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
    -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
    "-DCMAKE_MODULE_LINKER_FLAGS_RELEASE=/DEBUG /OPT:REF /OPT:ICF"
    -DOFX_USE_OPENMP=OFF -DBUILD_TESTING=OFF)
[ -z "$standalone" ] || options+=("-DOFXMISC_STANDALONE_BUNDLES=DenoiseSharpen;NoOp")

# build BUILDDIR PREFIX SYMBOLDIR [CMAKE_OPTION...]: configure, build and install the bundles into
# PREFIX, and copy the binaries' .pdb into OUTDIR/symbols/SYMBOLDIR
build() {
    local b=$1 p=$2 sym=$out/symbols/$3 c start=$SECONDS
    shift 3
    echo "::group::$b"
    cmake -S . -B "$b" "${options[@]}" "$@"
    cmake --build "$b"
    for c in Misc CImg Shadertoy; do cmake --install "$b" --prefix "$(w "$p")" --component $c; done
    if [ -n "$standalone" ]; then
        cmake --install "$b" --prefix "$(w "$p-denoisesharpen")" --component DenoiseSharpen
        cmake --install "$b" --prefix "$(w "$p-noop")" --component NoOp
    fi
    mkdir -p "$sym"
    find "$b" -name '*.pdb' -not -path '*/CMakeFiles/*' -exec cp {} "$sym/" \;
    echo "::endgroup::"
    echo "$b: built in $(( (SECONDS - start) / 60 )) min $(( (SECONDS - start) % 60 )) s"
}

rm -rf "$out/build" "$out/build-arm64" "$out/build-arm64x" "$out/install" "$out/install-denoisesharpen" "$out/install-noop" "$out/symbols"
mkdir -p "$out/install-denoisesharpen" "$out/install-noop" # (empty with --no-standalone)
if [ "$arch" = x64 ]; then
    build "$out/build" "$out/install" Win64
else
    rm -rf "$out/install-arm64" "$out/install-arm64-denoisesharpen" "$out/install-arm64-noop" "$out/arm64x"
    build "$out/build-arm64" "$out/install-arm64" Win-arm64 \
        -DOFXMISC_ARM64X=ARM64 -DOFXMISC_ARM64X_DIR="$(w "$out/arm64x")"
    # CFLAGS/CXXFLAGS: added to CMake's default flags (a -DCMAKE_CXX_FLAGS would replace them)
    CFLAGS=-arm64EC CXXFLAGS=-arm64EC build "$out/build-arm64x" "$out/install" Win-arm64ec \
        -DOFXMISC_ARM64X=ARM64EC -DOFXMISC_ARM64X_DIR="$(w "$out/arm64x")"
    # one bundle with both: Win-arm64ec (Arm64X) and Win-arm64 (ARM64)
    for i in install install-denoisesharpen install-noop; do
        [ -d "$out/install-arm64${i#install}" ] || continue # (--no-standalone)
        for b in "$out/install-arm64${i#install}/OFX/Plugins"/*.ofx.bundle; do
            cp -R "$b/Contents/Win-arm64" "$out/$i/OFX/Plugins/$(basename "$b")/Contents/"
        done
    done
fi

# Check each binary: its folder, its machine type, and that it imports no MSVC runtime DLL.
status=0 n=0
while IFS= read -r f; do
    n=$((n + 1))
    dir=$(basename "$(dirname "$f")")
    headers=$(dumpbin -nologo -headers "$(cygpath -w "$f")" | tr -d '\r')
    machine=$(printf '%s\n' "$headers" | grep -m1 ' machine (' | sed 's/^ *//; s/ *$//')
    deps=$(dumpbin -nologo -dependents "$(cygpath -w "$f")" | tr -d '\r' |
           awk '/following (delay load )?dependencies/ {d = 1; next} /Summary/ {d = 0} d && tolower($1) ~ /\.dll$/ {print $1}' |
           sort -u | tr '\n' ' ')
    echo "${f#"$out"/}: $machine; imports $deps"
    case "$dir:$machine" in
        "Win64:8664 machine (x64)") ;;
        "Win-arm64:AA64 machine (ARM64)") ;;
        "Win-arm64ec:AA64 machine (ARM64) (ARM64X)") ;;
        *) echo "ERROR: $f: unexpected machine for $dir: $machine"; status=1 ;;
    esac
    if printf '%s\n' $deps | grep -i -E '^(vcruntime|msvcp|vcomp|concrt|ucrtbase|api-ms-win-crt-)'; then
        echo "ERROR: $f imports the MSVC runtime DLLs above (it must be built with /MT)"
        status=1
    fi
done < <(find "$out/install" "$out/install-denoisesharpen" "$out/install-noop" -path '*.ofx.bundle/Contents/Win*/*.ofx' | sort)
# x64: 5 (Misc, CImg, Shadertoy, DenoiseSharpen, NoOp; 3 with --no-standalone); arm64x: twice as
# many (Win-arm64ec and Win-arm64 of each)
expected=5
[ -n "$standalone" ] || expected=3
[ "$arch" = x64 ] || expected=$((expected * 2))
if [ "$n" != "$expected" ]; then
    echo "ERROR: $n binaries, expected $expected"
    status=1
fi
# one .pdb per binary, for the crash dumps
npdb=$(find "$out/symbols" -name '*.pdb' | wc -l)
echo "symbols: $npdb .pdb files: $(cd "$out/symbols" && find . -name '*.pdb' | sort | tr '\n' ' ')"
if [ "$npdb" != "$n" ]; then
    echo "ERROR: $npdb .pdb files for $n binaries"
    status=1
fi
find "$out/install" "$out/install-denoisesharpen" "$out/install-noop" -maxdepth 5 -path '*.ofx.bundle/Contents/*' -type d -name 'Win*' | sed "s|^$out/||" | sort
exit $status
