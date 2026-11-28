#!/bin/bash
# Headless smoke test of ALL the plug-ins, used by the CI (and by SupportExt's CI,
# to test a SupportExt pull request with openfx-misc): every plug-in (every
# identifier and major version) of the bundles is described and rendered with
# openfx's hostDemo, with its default parameters, in each of the contexts that
# hostDemo supports (filter and general), at each pixel depth it supports.
# rendertest.sh checks the output values of a few plug-ins; this test only
# checks that each plug-in loads, describes, instantiates and renders.
#
# Usage (from anywhere): .github/tools/smoketest.sh [options] PREFIX
#   PREFIX: a CMake install prefix (cmake --install build --prefix PREFIX): the
#           bundles are in PREFIX/OFX/Plugins (or in PREFIX itself), and hostDemo
#           in PREFIX/bin if openfx-misc was configured with -DOFX_BUILD_HOSTDEMO=ON.
# Options:
#   --supportext DIR  first build openfx-misc (the source tree of this script) in
#                     Release with the SupportExt sources of DIR instead of the
#                     submodule (-DOFXMISC_SUPPORTEXT_DIR=DIR -DOFX_BUILD_HOSTDEMO=ON
#                     -DBUILD_TESTING=ON, plus $CMAKE_ARGS), and install it into PREFIX
#   --build DIR       the build directory of --supportext (default: a temporary one)
#   --out DIR         keep the outputs (gzipped PFM files), reports and logs in DIR
#   --only REGEX      only the plug-ins whose identifier matches REGEX (the whole
#                     identifier, Python re syntax)
#   --quick           only the most general context of each plug-in (general if
#                     it supports it, else filter): about half the renders (per-PR CI)
#   -j N              run N renders at a time (default: the number of CPUs)
#   --ofx14           run hostDemo as an OFX 1.4 host (hostDemo --ofx14: only the OFX 1.4
#                     host properties and suites, no OfxSetHost, no StrChoice or Bytes
#                     params, one view): every plug-in must still describe and render
#                     there, without the OFX 1.5 features and the host extensions. One view
#                     is rendered per frame (the outputs have no l/r suffix), the report
#                     must say host.profile "ofx14", and the "ofx14:" entries of the lists
#                     below are used (they are ignored otherwise; e.g. a plug-in that needs
#                     an optional OFX 1.4 feature that hostDemo lacks in this mode)
#   --ofx11           run hostDemo as an OFX 1.1 host modelled on VEGAS Pro (hostDemo --ofx11:
#                     only the OFX 1.1 host properties and suites, no interact suite, no
#                     kOfxParamPropDefaultCoordinateSystem, byte and float depths, one clip PAR,
#                     no tiles, no multi-resolution, persistent messages refused, one view;
#                     issue #66). As --ofx14: one view per frame, host.profile "ofx11", and the
#                     "ofx11:" entries of the lists below. The short depth is not rendered, and
#                     the OPENGL plug-ins are skipped (--ofx11 refuses --opengl-describe).
#                     --ofx14 and --ofx11 can't be combined
# Environment: HOSTDEMO (the hostDemo binary, default PREFIX/bin/hostDemo),
# CXX (to build ofxload.cpp, default c++), CMAKE_ARGS (--supportext),
# SMOKETEST_TIMEOUT (seconds per describe or render, default 120),
# SMOKETEST_DEBUG=1 (a debug build: also use the "debug:" entries of the lists below; an
# entry can be "debug:ofx14:..." or "debug:ofx11:..."),
# SMOKETEST_BUNDLES (the expected bundles, see BUNDLES below; e.g. "Misc:0" for a build
# with -DBUILD_CIMG_PLUGINS=OFF -DBUILD_SHADERTOY=OFF -DBUILD_TESTING=OFF),
# SMOKETEST_NO_TEST=1 (a build without the Test bundle, -DBUILD_TESTING=OFF: BUNDLES without Test,
# and stale entries are only a warning, as with SMOKETEST_BUNDLES),
# SMOKETEST_GPU=require (fail if hostDemo can't render with OpenGL, see OPENGL below, except with
# --ofx11; by default the OpenGL renders are only skipped then),
# SMOKETEST_XPASS_OK=1 (accept the known bugs that were fixed: an XFAIL entry that passes
# (XPASS) is reported but does not fail, and stale entries are only a warning; an XFAIL
# render whose failure does not show the expected message still fails; for SupportExt's CI, so that a SupportExt pull request that fixes a bug of the XFAIL list
# passes, while the CI of openfx-misc, which still has the old SupportExt submodule, keeps
# the entry; the default is strict).
# Linux and macOS (needs python3 and perl).
#
# Each render is one hostDemo run (--no-std-paths --no-cache: only the bundle of
# the plug-in is in OFX_PLUGIN_PATH), at t=0 and 1 (both views: hostDemo is
# built with the OFX_EXTENSIONS_* of the plug-ins; one view with --ofx14 or --ofx11), on the PAL SD project
# (720x576), with the outputs in float PFM files. The inputs are: ramp for the
# first input clip, checker:16 for the second one, a solid colour for the
# others; the optional clips of generators (plug-ins with a generator context)
# are disconnected; masks are disconnected in the filter context and get a
# checker:32 in the general context; INPUTS below overrides these. PARAMS below adds
# renders with other parameters (e.g. for the plug-ins that are identities by
# default). A plug-in fails if:
# - hostDemo --describe fails (load, describe or describeInContext), or the
#   plug-in supports neither the filter nor the general context;
# - a describe-time string that hostDemo reports (the plug-in's label or grouping, a
#   clip's label or hint, a parameter's label, hint or choice options) has a percent
#   sign: a host may pass it to a printf-style function (issue #66). hostDemo does not
#   report the plug-in description or the option hints: .github/tools/check-percent-strings.py
#   checks those in the string literals of the sources. These failures have the context
#   "strings", so that no XFAIL entry matches them;
# - in the filter context, the Output clip does not support every component
#   that the Source clip supports (the spec says that Output always has the
#   components of Source there), or, in a filter-context render, the Output components
#   after getClipPreferences (hostDemo --report: clipPreferences.clips) are not the
#   Source components (a plug-in that remaps Source or Output there, issue #217);
# - a render fails: hostDemo fails (an action failed: createInstance,
#   getClipPreferences, getRegionOfDefinition, render...; an image fetch failed;
#   a float output pixel was not written, i.e. is still NaN; or the plug-in
#   crashed), or takes more than SMOKETEST_TIMEOUT seconds; the --report JSON
#   shows a missing render or output, or an error or fatal message from the
#   plug-in; or an output file is missing or empty.
# - a bundle of BUNDLES is missing, or has fewer plug-ins than its minimum;
# - a PARAMS render is an identity (see PARAMS below);
# - an entry of SKIP, IDENTITY, PARAMS or XFAIL is stale (see below).
# Two cases are not failures: an identity effect (isIdentity) of the IDENTITY list
# that does not write its output when hostDemo renders it anyway (the spec says
# the host should not call render then; hostDemo stops at that render), and an
# empty region of definition (nothing to render, e.g. FrameRange outside its
# range). The plug-ins that cannot run in hostDemo are in the SKIP list below, with
# the reason. The OpenGL-only plug-ins (OPENGL below) are described with hostDemo
# --opengl-describe (a host that says it supports OpenGL render), and not rendered
# when they need OpenGL (kOfxImageEffectPropOpenGLRenderSupported "needed": hostDemo
# cannot render with OpenGL); a build where they also render on the CPU (OSMesa)
# renders them as the other plug-ins. If hostDemo renders with OpenGL (hostDemo --gpu=opengl, built
# with OFX_HOSTSUPPORT_OPENGL, with an OpenGL context; not with --ofx11), the OPENGL plug-ins are
# also rendered with --gpu=opengl, and these renders (named *-gpu) fail if hostDemo did not render
# them with OpenGL or retried them on the CPU. Known bugs are expected failures (XFAIL, in
# the XFAIL list, each with its GitHub issue), with the message that their
# failure must show: an XFAIL render that passes (XPASS) fails the test, so that
# the list is updated (except with SMOKETEST_XPASS_OK=1).
# Stale entries fail the test, so that the lists stay exact: a SKIP entry that
# matches no plug-in and a PARAMS entry that adds no render (except with --only),
# and, in full mode only (not --quick, not --only; --quick does not render every
# context), an IDENTITY or XFAIL entry that matches no identity render or no
# expected failure. With SMOKETEST_BUNDLES or SMOKETEST_NO_TEST=1 (a partial build) or
# SMOKETEST_XPASS_OK=1, stale entries are only a warning.
set -eo pipefail
. "$(dirname "$0")/lib.sh"
# the Python helpers of the list checks (smoketest_lists.py), for the scripts below
SMOKETEST_TOOLS=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export SMOKETEST_TOOLS

# ---------------------------------------------------------------------------
# The expected bundles: "name:minimum number of plug-ins (identifier and major version)"
# (the counts of a release build; debug builds have more). A missing bundle, or one with
# fewer plug-ins, fails the test (raise or lower the minimum when plug-ins are added or
# removed). Other bundles (DebugProxy) are not required. SMOKETEST_BUNDLES replaces it.
BUNDLES=(Misc:193 CImg:41 Shadertoy:1 Test:6)
# shellcheck disable=SC2206
[ -z "$SMOKETEST_BUNDLES" ] || BUNDLES=($SMOKETEST_BUNDLES)
if [ "${SMOKETEST_NO_TEST:-0}" = 1 ]; then
    with_test=("${BUNDLES[@]}") BUNDLES=()
    for b in "${with_test[@]}"; do [ "${b%%:*}" = Test ] || BUNDLES+=("$b"); done
fi
# The plug-ins that are not tested: "identifier regex|reason" (the whole identifier; a
# "debug:" reason is only used in debug builds, an "ofx14:" one only with --ofx14, an "ofx11:"
# one only with --ofx11; these prefixes, which can be combined, only work at the start of the
# reason or description field of the SKIP, IDENTITY and XFAIL lists).
# (a regex can't have "|": one entry per plug-in, each checked for staleness)
parametric="needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
SKIP=(
    "net.sf.openfx.ColorLookupPlugin|$parametric"
    "net.sf.openfx.HueCorrect|$parametric"
    "net.sf.openfx.HueKeyer|$parametric"
    "net.sf.openfx.TimeBufferRead|debug:reads the buffer of a TimeBufferWrite instance of the same name, in the same host process (getClipPreferences fails: No TimeBuffer exists with name \"\")"
    "net.sf.openfx.TimeBufferWrite|debug:writes the buffer of a TimeBufferRead instance of the same name, in the same host process (render fails: No TimeBuffer exists with name \"\")"
)
# The OpenGL-only plug-ins: "identifier regex|why" (the whole identifier). They are described
# with hostDemo --opengl-describe (only they: it changes the host description), and their CPU
# renders are skipped when a context says they need OpenGL ("needed"). If hostDemo renders with
# OpenGL (--gpu=opengl), each of their renders is also done with OpenGL. An entry that matches
# no plug-in is stale, as in SKIP.
OPENGL=(
    "net.sf.openfx.Shadertoy|OpenGL only unless built with OSMesa (CPU rendering)"
    "net.sf.openfx.TestOpenGL|OpenGL only unless built with OSMesa (Test bundle)"
)
# The plug-ins that are identity effects with their default parameters, and do not write
# their output when hostDemo renders them anyway: "identifier regex[:major][@context]|why"
# (a "debug:" reason is only used in debug builds). Another identity render without an
# output fails, so that a plug-in that becomes an identity by default is noticed.
IDENTITY=(
    "net.sf.openfx.TrackerPM|trackers are identity effects (SupportExt's GenericTrackerPlugin)"
    # debug builds: render posts "OFX Host should not render" and fails (kOfxStatFailed) when
    # it is called for an identity (release builds copy the input)
    "net.sf.openfx.NoOpPlugin@filter|debug:an identity unless Force Copy is set"
    "net.sf.openfx.NoTimeBlurPlugin@filter|debug:always an identity: the Source at another time"
    "net.sf.openfx.Retime|debug:an identity at the default speed (1: whole source frames)"
    "net.sf.openfx.TestGroups|debug:an identity unless Force Copy is set (Test bundle)"
)
# (the entries of SKIP, OPENGL, IDENTITY, INPUTS, PARAMS and XFAIL are split at "|": no "|" in the regexes)
# Inputs that replace the default ones: "identifier regex[@context]|Clip=spec ...".
# DATA is the directory of the files made below (square32.pfm: a 32x32 white Pf,
# box5.pfm: a 5x5 white PF).
INPUTS=(
    # the kernel (Filter) is a whole image by default: a 720x576 convolution takes minutes
    "net.sf.cimg.CImgConvolve|Filter=file:DATA/box5.pfm"
    # inpaint a 32x32 square, not half of the image (minutes)
    "eu.cimg.Inpaint@general|Mask=file:DATA/square32.pfm@344,272"
)
# Parameter overrides: "identifier regex[:major][@context][/depth]|argument ...", where the
# arguments are hostDemo's (name=value[,value...], -s name@t=value for a key; split into words;
# no "|" or ";"), and "differs:N" (see below). Each entry adds one render of each matching
# plug-in with these parameters, besides the renders with the default parameters, also with
# --quick: in the given context (default: general if the plug-in has it, else filter), at the
# given depth (default: float, or the highest supported one). It must not be an identity: it
# fails if isIdentity is true, or if its output at t=0 (bounds and CRC32) is the one of the
# default render (same context and depth; without a default render to compare to, e.g. an
# @filter entry with --quick or a failed default render, a note says so), so that it cannot
# silently become an identity (a renamed parameter fails: hostDemo rejects unknown parameters).
# With differs:N, its output at each time must also differ from the one of the render of
# PARAMS entry N (1 is the first entry) of the same plug-in, context and depth.
PARAMS=(
    # Transform, Position and CornerPin are identities with their default parameters: render
    # SupportExt's Transform3x3 (filtering, motion blur)
    "net.sf.openfx.Transform(Masked)?Plugin|rotate=30 scale=0.5,0.5"
    # motion blur: the last key is after t=1, so that both frames move; entry 3 must differ
    # from entry 2 (the same keys without motion blur), so that it really blurs
    "net.sf.openfx.Transform(Masked)?Plugin|motionBlur=0 -s translate@0=10,5 -s translate@2=50,25"
    "net.sf.openfx.Transform(Masked)?Plugin|motionBlur=1 -s translate@0=10,5 -s translate@2=50,25 differs:2"
    "net.sf.openfx.Position|translate=10,5"
    "net.sf.openfx.CornerPin(Masked)?Plugin|to1=40,30 to2=680,10 to3=700,560 to4=20,540"
    # RGB output, on a host that supports it (hostDemo --components: the default is RGBA and Alpha)
    "net.sf.openfx.ConstantPlugin|--components rgba,rgb,alpha outputComponents=RGB"
    "net.sf.openfx.CheckerBoardPlugin|--components rgba,rgb,alpha outputComponents=RGB"
    "net.sf.openfx.ShufflePlugin:4|--components rgba,rgb,alpha outputComponents=RGB"
)
# Known bugs, each tracked by a GitHub issue: "identifier regex[:major][@context][/depth]|
# expected message (Python re, searched in the failure summary and the hostDemo log)|
# description" (context: filter or general, depth: byte, short or float; a "debug:"
# description is only expected in debug builds). The entries without a depth also apply
# to describe failures: the entries without a context to hostDemo --describe failures,
# those with a context to the describeInContext failures of that context.
XFAIL=(
    # (none: add the known bugs here, each with its GitHub issue)
)
# ---------------------------------------------------------------------------

case "$(uname -s)" in
    # the bundle binaries are in Contents/<arch>: MacOS, Linux-x86-64, Linux-aarch64...
    Darwin) arch=MacOS ;;
    Linux) arch='Linux-*' ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

SMOKETEST_TIMEOUT=${SMOKETEST_TIMEOUT:-120}
case "${SMOKETEST_GPU:-}" in
    ''|require) ;;
    *) echo "smoketest: SMOKETEST_GPU=$SMOKETEST_GPU: the only value is require"; exit 1 ;;
esac

# watchdog LOG BUNDLE HOSTDEMO-ARGS...: run hostDemo with only BUNDLE in OFX_PLUGIN_PATH
# and its output in LOG, killed after SMOKETEST_TIMEOUT seconds (run_with_timeout), and set
# $status to its exit status. The caller sends its stderr to LOG, for the shell's message about
# a crash.
watchdog() {
    local log=$1 bundle=$2 host=()
    shift 2
    # --ofx14 or --ofx11: an OFX 1.4 or OFX 1.1 host
    [ "${SMOKETEST_PROFILE:-default}" = default ] || host=("--$SMOKETEST_PROFILE")
    status=0
    ( export OFX_PLUGIN_PATH=$SMOKETEST_WORK/path/$bundle
      run_with_timeout "$SMOKETEST_TIMEOUT" "$HOSTDEMO" --no-std-paths --no-cache --quiet "${host[@]}" "$@" ) \
        > "$log" 2>&1 || status=$?
    if [ "$status" = 142 ]; then
        echo "timeout: killed after ${SMOKETEST_TIMEOUT}s" >> "$log"
    elif [ "$status" -gt 128 ]; then
        echo "killed by signal $((status - 128))" >> "$log"
    fi
}

# The workers (run by xargs), each in its own directory: describe one plug-in (a line of
# plugins.txt: "bundle identifier major"), or render one job (a line of jobs.txt, see
# below). They write a status file, which the main script checks.
if [ "$1" = --describe-job ]; then
    read -r bundle id major <<< "$2"
    d=$SMOKETEST_WORK/describe/$id-v$major
    mkdir -p "$d"
    cd "$d"
    exec 2>> describe.log
    gl=()
    # the OpenGL-only plug-ins (OPENGL): describe them as a host with OpenGL render
    # (if grep fails, e.g. no file: no flag)
    if grep -qxF "$id" "$SMOKETEST_WORK/opengl-ids.txt"; then gl=(--opengl-describe); fi
    watchdog describe.log "$bundle" -p "$id" -m "$major" "${gl[@]}" --describe describe.json
    echo "$status" > status
    exit 0
fi
if [ "$1" = --render-job ]; then
    IFS='|' read -r name bundle id major ctx depth inputs _ params _ <<< "$2"
    d=$SMOKETEST_WORK/jobs/$name
    mkdir -p "$d"
    cd "$d"
    args=()
    IFS=';' read -r -a specs <<< "$inputs"
    for i in "${specs[@]}"; do [ -z "$i" ] || args+=(-i "$i"); done
    # the parameter overrides of PARAMS (hostDemo arguments)
    IFS=';' read -r -a words <<< "$params"
    for i in "${words[@]}"; do [ -z "$i" ] || args+=("$i"); done
    start=$(date +%s)
    exec 2>> run.log
    watchdog run.log "$bundle" -p "$id" -m "$major" -c "$ctx" -d "$depth" --format pfm -o out \
             --report report.json "${args[@]}"
    echo "$status $(( $(date +%s) - start ))" > status
    # The output files and their sizes. The files are removed (about 25 MB per render),
    # or compressed for --out.
    for f in out.*; do [ ! -f "$f" ] || echo "$f $(wc -c < "$f")"; done > files.txt
    if [ -n "$SMOKETEST_KEEP" ]; then
        for f in out.*; do [ ! -f "$f" ] || gzip -1 -n -f "$f"; done
    else
        rm -f out.*
    fi
    exit 0
fi

usage() { sed -n '/^# Usage/,/^# Linux and macOS/p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-1}"; }
top=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
supportext='' build='' out='' only='' quick=0 profile=default prefix='' jobs=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)
while [ $# -gt 0 ]; do
    case "$1" in
        --supportext) supportext=$2; shift ;;
        --build) build=$2; shift ;;
        --out) out=$2; shift ;;
        --only) only=$2; shift ;;
        --quick) quick=1 ;;
        --ofx14|--ofx11)
            [ "$profile" = default ] || [ "$profile" = "${1#--}" ] || { echo "--ofx14 and --ofx11 can't be combined"; usage; }
            profile=${1#--} ;;
        -j) jobs=$2; shift ;;
        -h|--help) usage 0 ;;
        -*) echo "unknown option $1"; usage ;;
        *) [ -z "$prefix" ] || usage; prefix=$1 ;;
    esac
    shift
done
[ -n "$prefix" ] || usage
start=$(date +%s)
work=$(mktemp -d "${TMPDIR:-/tmp}/smoketest.XXXXXX")
trap 'rm -rf "$work"' EXIT

if [ -n "$supportext" ]; then
    supportext=$(cd "$supportext" && pwd)
    [ -n "$build" ] || build=$work/build
    gen=()
    command -v ninja > /dev/null && gen=(-G Ninja)
    echo "Building openfx-misc with the SupportExt of $supportext in $build..."
    # $CMAKE_ARGS is split into words on purpose
    # shellcheck disable=SC2086
    cmake -S "$top" -B "$build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DOFX_BUILD_HOSTDEMO=ON -DBUILD_TESTING=ON \
          -DOFXMISC_SUPPORTEXT_DIR="$supportext" $CMAKE_ARGS > "$work/configure.log" 2>&1 \
        || { cat "$work/configure.log"; echo "configure failed"; exit 1; }
    cmake --build "$build" --parallel "$jobs" > "$work/build.log" 2>&1 \
        || { grep -E -B 2 -A 10 'error|Error' "$work/build.log" | head -n 200; echo "build failed"; exit 1; }
    mkdir -p "$prefix"
    cmake --install "$build" --prefix "$prefix" > "$work/install.log" 2>&1 \
        || { cat "$work/install.log"; echo "install failed"; exit 1; }
    echo "built and installed in $(( $(date +%s) - start ))s"
fi

prefix=$(cd "$prefix" && pwd)
plugins=$prefix/OFX/Plugins
[ -d "$plugins" ] || plugins=$prefix
HOSTDEMO=${HOSTDEMO:-$prefix/bin/hostDemo}
case "$HOSTDEMO" in /*) ;; *) HOSTDEMO=$PWD/$HOSTDEMO ;; esac
[ -x "$HOSTDEMO" ] || { echo "hostDemo not found: $HOSTDEMO (configure with -DOFX_BUILD_HOSTDEMO=ON, or set HOSTDEMO)"; exit 1; }
case "$profile" in
    ofx14) echo "hostDemo: $HOSTDEMO --ofx14 (an OFX 1.4 host)" ;;
    ofx11) echo "hostDemo: $HOSTDEMO --ofx11 (an OFX 1.1 host)" ;;
    *) echo "hostDemo: $HOSTDEMO" ;;
esac
echo "bundles: $plugins"

# The plug-ins: ofxload prints the identifier and version of each plug-in of a binary.
# DebugProxy is not tested: it wraps the plug-in named by $OFX_DEBUGPROXY_BINARY.
libs=
[ "$arch" = MacOS ] || libs=-ldl
"${CXX:-c++}" -std=c++11 -I"$top/openfx/include" "$top/.github/tools/ofxload.cpp" -o "$work/ofxload" $libs
mkdir -p "$work/path" "$work/jobs" "$work/data"
: > "$work/plugins.txt"
for b in "$plugins"/*.ofx.bundle; do
    [ -d "$b" ] || continue
    name=$(basename "$b" .ofx.bundle)
    if [ "$name" = DebugProxy ]; then
        echo "SKIP  DebugProxy.ofx.bundle: it wraps the plug-in named by \$OFX_DEBUGPROXY_BINARY"
        continue
    fi
    # OFX_PLUGIN_PATH of the plug-ins of this bundle: only this bundle, so that each
    # hostDemo run loads one binary
    mkdir -p "$work/path/$name"
    ln -s "$b" "$work/path/$name/"
    binary=
    # shellcheck disable=SC2086
    for f in "$b"/Contents/$arch/"$name.ofx"; do
        if [ -f "$f" ]; then binary=$f; break; fi
    done
    if [ -z "$binary" ]; then
        echo "smoketest: $b has no binary Contents/$arch/$name.ofx"
        exit 1
    fi
    status=0
    listing=$("$work/ofxload" "$binary" 2>&1) || status=$?
    if [ "$status" != 0 ]; then
        printf '%s\n' "$listing"
        echo "smoketest: ofxload failed on $binary (exit status $status, see above): the bundle does not load, or its plug-ins are invalid"
        exit 1
    fi
    printf '%s\n' "$listing" | sed -n "s/^  \([^ ]*\) \([0-9]*\)\.[0-9]*$/$name \1 \2/p" >> "$work/plugins.txt"
done
# The expected bundles and their minimum number of plug-ins
python3 -I - "$work/plugins.txt" "${BUNDLES[@]}" << 'EOF' || { echo "smoketest: missing bundles or plug-ins in $plugins (BUNDLES in $0, or SMOKETEST_BUNDLES)"; exit 1; }
import collections, sys
n = collections.Counter(l.split()[0] for l in open(sys.argv[1]))
print("bundles: " + ", ".join("%s %d" % i for i in sorted(n.items())))
bad = False
for b in sys.argv[2:]:
    name, _, least = b.partition(":")
    if name not in n:
        print("FAIL  %s.ofx.bundle: missing" % name)
        bad = True
    elif n[name] < int(least or 1):
        print("FAIL  %s.ofx.bundle: %d plug-ins, expected at least %s" % (name, n[name], least))
        bad = True
sys.exit(1 if bad else 0)
EOF
if [ -n "$only" ]; then
    python3 -I -c 'import re, sys
for l in open(sys.argv[1]):
    if re.fullmatch(sys.argv[2], l.split()[1]):
        print(l, end="")' "$work/plugins.txt" "$only" > "$work/sel.txt"
    mv "$work/sel.txt" "$work/plugins.txt"
fi
[ -s "$work/plugins.txt" ] || { echo "no plug-in found in $plugins${only:+ matching $only}"; exit 1; }
echo "$(wc -l < "$work/plugins.txt" | tr -d ' ') plug-ins (identifier and major version) in $(ls "$work/path" | wc -l | tr -d ' ') bundles"

# The lists, for the Python scripts below (one entry per line)
printf '%s\n' "${SKIP[@]}" > "$work/skip.txt"
printf '%s\n' "${OPENGL[@]}" > "$work/opengl.txt"
printf '%s\n' "${INPUTS[@]}" > "$work/inputs.txt"
printf '%s\n' "${PARAMS[@]}" > "$work/params.txt"
printf '%s\n' "${XFAIL[@]}" > "$work/xfail.txt"
printf '%s\n' "${IDENTITY[@]}" > "$work/identity.txt"

# The input files of INPUTS (little-endian PFM, bottom row first)
python3 -I - "$work/data" << 'EOF'
import os, struct, sys
def pfm(name, kind, w, h, value):
    n = w * h * (3 if kind == "PF" else 1)
    with open(os.path.join(sys.argv[1], name), "wb") as f:
        f.write(("%s\n%d %d\n-1.0\n" % (kind, w, h)).encode() + struct.pack("<%df" % n, *([value] * n)))
pfm("square32.pfm", "Pf", 32, 32, 1.)
pfm("box5.pfm", "PF", 5, 5, 1.)
EOF

# Does hostDemo have --opengl-describe (openfx#312), and an OpenGL render suite
# (OFX_SUPPORTS_OPENGLRENDER)? An older hostDemo rejects the option, one without the suite
# says so: both name the option in an "Error:" line, a capable one only says that the dummy
# plug-in is not found. Without it, the plug-ins of OPENGL are skipped.
gl_ok=1
if [ "$profile" = ofx11 ]; then
    gl_ok=0
    echo "note: hostDemo --ofx11 refuses --opengl-describe: the OPENGL plug-ins are skipped"
elif ( cd "$work" && OFX_PLUGIN_PATH=$work/path/none "$HOSTDEMO" --no-std-paths --no-cache --quiet \
         --opengl-describe -p smoketest.probe --describe probe.json 2>&1 ) | grep -q '^Error: .*opengl-describe'; then
    gl_ok=0
    echo "note: $HOSTDEMO has no --opengl-describe (or no OpenGL render suite): the OPENGL plug-ins are skipped"
fi
# Does hostDemo render with OpenGL (--gpu=opengl: openfx#367, built with OFX_HOSTSUPPORT_OPENGL)
# here, with an OpenGL context? An older hostDemo rejects the option, one built without it says so,
# one without a context warns: all name the option. A capable one only says that the dummy plug-in
# is not found. Without it, the OPENGL plug-ins are not rendered with OpenGL.
gpu_ok=0
if [ "$gl_ok" = 1 ]; then
    # (under the render timeout: a stuck OpenGL initialization fails like a stuck render)
    probe_status=0
    probe=$(cd "$work" && export OFX_PLUGIN_PATH=$work/path/none && run_with_timeout "$SMOKETEST_TIMEOUT" "$HOSTDEMO" \
                --no-std-paths --no-cache --quiet --gpu=opengl -p smoketest.probe 2>&1) || probe_status=$?
    if [ "$probe_status" = 142 ]; then
        echo "note: the --gpu=opengl probe of $HOSTDEMO timed out: the OPENGL plug-ins are not rendered with OpenGL"
        echo "  | timeout: killed after ${SMOKETEST_TIMEOUT}s"
    elif printf '%s\n' "$probe" | grep -q -e '--gpu' || ! printf '%s\n' "$probe" | grep -q 'not found'; then
        echo "note: $HOSTDEMO does not render with OpenGL here (--gpu=opengl): the OPENGL plug-ins are not rendered with OpenGL"
        printf '%s\n' "$probe" | grep -e '--gpu' | sed 's/^/  | /' || true
    else
        gpu_ok=1
        echo "hostDemo renders with OpenGL (--gpu=opengl): the OPENGL plug-ins are also rendered with OpenGL"
    fi
fi
# (--ofx11 refuses --gpu=opengl: nothing to require there)
if [ "$gpu_ok" = 0 ] && [ "${SMOKETEST_GPU:-}" = require ] && [ "$profile" != ofx11 ]; then
    echo "smoketest: SMOKETEST_GPU=require, but hostDemo does not render with OpenGL here (profile $profile)"
    exit 1
fi
# The plug-ins of OPENGL: opengl-map.txt ("identifier|reason", for the render list) and
# opengl-ids.txt (the identifiers to describe with --opengl-describe, for the describe workers)
python3 -I - "$work" "$gl_ok" << 'EOF'
import os
import re
import sys
work, gl_ok = sys.argv[1], sys.argv[2] == "1"
sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.environ["SMOKETEST_TOOLS"])
import smoketest_lists
rules = [line.split("|", 1) for line in smoketest_lists.entries(work, "opengl.txt")]
with open(os.path.join(work, "plugins.txt")) as f:
    pids = sorted(set(line.split()[1] for line in f))
found = [(pid, next(r for rx, r in rules if re.fullmatch(rx, pid)))
         for pid in pids if any(re.fullmatch(rx, pid) for rx, _ in rules)]
with open(os.path.join(work, "opengl-map.txt"), "w") as f:
    f.writelines("%s|%s\n" % p for p in found)
with open(os.path.join(work, "opengl-ids.txt"), "w") as f:
    f.writelines(pid + "\n" for pid, _ in found if gl_ok)
EOF

# Describe each plug-in (in parallel). A worker that fails (xargs exit status 123)
# leaves no status file: its plug-ins fail below.
export HOSTDEMO SMOKETEST_WORK=$work SMOKETEST_TIMEOUT SMOKETEST_PROFILE=$profile
[ -z "$out" ] || export SMOKETEST_KEEP=1
failed_workers=
tr '\n' '\0' < "$work/plugins.txt" | xargs -r -0 -n 1 -P "$jobs" "$0" --describe-job ||
    failed_workers="describe (xargs exit status $?)"

# The render jobs, one per line: "name|bundle|id|major|context|depth|Clip=spec;...|
# PARAMS entry number|hostDemo argument;...|differs:N entry number" (the last three are
# empty for a default render)
python3 -I - "$work" "$quick" "${SMOKETEST_DEBUG:-0}" "$profile" "$gl_ok" "$gpu_ok" > "$work/jobs.txt" 2> "$work/describe-results.txt" << 'EOF' ||
import json, os, re, sys
work, quick, debug, profile = sys.argv[1], sys.argv[2] == "1", sys.argv[3] == "1", sys.argv[4]
gl_ok, gpu_ok = sys.argv[5] == "1", sys.argv[6] == "1"
sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.environ["SMOKETEST_TOOLS"])
import smoketest_lists
def entries(name):
    return smoketest_lists.entries(work, name)
def only_in(desc):
    return smoketest_lists.only_in(desc, debug, profile)
skip = []
for s in entries("skip.txt"):
    rx, reason = s.split("|", 1)
    keep, reason = only_in(reason)
    if keep:
        skip.append((rx, reason))
# OPENGL: identifier -> reason (opengl-map.txt)
opengl = dict(s.split("|", 1) for s in entries("opengl-map.txt"))
overrides = []
for s in entries("inputs.txt"):
    m = re.fullmatch(r"([^@|]*)(?:@(filter|general))?\|(.*)", s)
    if not m:
        sys.exit("bad INPUTS entry: " + s)
    overrides.append((m.group(1), m.group(2), m.group(3).replace("DATA", os.path.join(work, "data")).split()))
params = []
for s in entries("params.txt"):
    m = re.fullmatch(smoketest_lists.SELECTOR + r"\|([^|;]*)", s)
    words = m.group(5).split() if m else []
    refs = [w[8:] for w in words if w.startswith("differs:")]
    words = [w for w in words if not w.startswith("differs:")]
    if not words or len(refs) > 1 or (refs and not (refs[0].isdigit()
                                                    and 1 <= int(refs[0]) <= len(entries("params.txt")))):
        sys.exit("bad PARAMS entry: " + s)
    params.append(m.groups()[:4] + (words, refs[0] if refs else ""))
depths = {"OfxBitDepthByte": "byte", "OfxBitDepthShort": "short", "OfxBitDepthFloat": "float"}
def text(path):
    try:
        return open(path, errors="replace").read().strip().replace("\n", " / ")
    except OSError:
        return "(no log)"
# describe-results.txt: "SKIP|tag|identifier||reason", "FAIL|tag|identifier|context|message"
# (context: empty for hostDemo --describe failures) or "DESC|tag|identifier||reason" (OPENGL:
# described, not rendered)
for line in open(os.path.join(work, "plugins.txt")):
    bundle, pid, major = line.split()
    reason = next((r for rx, r in skip if re.fullmatch(rx, pid)), None)
    if reason is None and pid in opengl and not gl_ok:
        reason = ("OpenGL only, and hostDemo --ofx11 refuses --opengl-describe: " if profile == "ofx11" else
                  "OpenGL only, and this hostDemo has no --opengl-describe: ") + opengl[pid]
    tag = "%s v%s" % (pid, major)
    if reason:
        print("SKIP|%s|%s||%s" % (tag, pid, reason), file=sys.stderr)
        continue
    d = os.path.join(work, "describe", "%s-v%s" % (pid, major))
    try:
        status = open(os.path.join(d, "status")).read().strip()
    except OSError:
        status = "unknown: the describe worker failed"
    try:
        desc = json.load(open(os.path.join(d, "describe.json")))
    except (OSError, ValueError):
        desc = None
    if status != "0" or not desc or not desc.get("ok"):
        print("FAIL|%s|%s||describe failed (exit status %s): %s"
              % (tag, pid, status, text(os.path.join(d, "describe.log"))), file=sys.stderr)
        continue
    contexts = desc["contexts"]
    generator = "OfxImageEffectContextGenerator" in contexts
    # no percent sign in the describe-time strings (issue #66: a host may use them as printf formats)
    percent = []
    for key in ("label", "grouping"):
        if "%" in json.dumps((desc.get("plugin") or {}).get(key)):
            percent.append("plug-in " + key)
    for c in contexts.values():
        for kind, key, fields in (("clip", "clips", ("label", "hint")),
                                  ("param", "params", ("label", "hint", "choiceOptions"))):
            for item in c.get(key) or []:
                for field in fields:
                    if "%" in json.dumps(item.get(field)):
                        percent.append("%s %s %s" % (kind, item.get("name"), field))
    if percent:
        print("FAIL|%s|%s|strings|percent sign in describe-time strings (issue #66): %s"
              % (tag, pid, ", ".join(sorted(set(percent)))), file=sys.stderr)
    # OPENGL: described with --opengl-describe; not rendered where it needs OpenGL
    gl_reason = opengl.get(pid)
    gl_only = False
    n = 0
    info = {}  # context -> (inputs, depths), for PARAMS
    for ctx in ("filter", "general"):
        c = contexts.get("OfxImageEffectContext" + ctx.capitalize())
        if c is None:
            continue
        skipped = quick and ctx == "filter" and "OfxImageEffectContextGeneral" in contexts
        if skipped and not c.get("ok"):
            continue
        if not c.get("ok"):
            print("FAIL|%s|%s|%s|describeInContext(%s) failed: %s"
                  % (tag, pid, ctx, ctx, text(os.path.join(d, "describe.log"))), file=sys.stderr)
            n += 1  # not also "no render"
            continue
        if ctx == "filter":
            # "a plugin cannot change the component type of the Output clip, it will always be the
            # same as the Source clip" (ofxImageEffectContexts.rst, The Filter Context): Output must
            # support every component that Source supports (issue #190; None, no image, is ignored)
            comps = dict((clip["name"], set(clip.get("supportedComponents") or []) - {"OfxImageComponentNone"})
                         for clip in c["clips"])
            if "Source" in comps and "Output" in comps and not comps["Source"] <= comps["Output"]:
                print("FAIL|%s|%s|%s|filter context: Source supports %s, Output only %s (Output must have the components of Source)"
                      % (tag, pid, ctx, " ".join(sorted(comps["Source"])), " ".join(sorted(comps["Output"]))),
                      file=sys.stderr)
        inputs, k = {}, 0
        for clip in c["clips"]:
            name = clip["name"]
            if name == "Output":
                continue
            if clip["isMask"]:
                spec = "checker:32" if ctx == "general" else "none"
            elif clip["optional"] and generator:
                spec = "none"
            else:
                spec = ("ramp", "checker:16")[k] if k < 2 else "solid:0.25,0.5,0.75,1"
                k += 1
            if spec == "none" and not clip["optional"]:
                spec = "ramp"
            inputs[name] = spec
        for rx, octx, specs in overrides:
            if re.fullmatch(rx, pid) and octx in (None, ctx):
                for s in specs:
                    name, spec = s.split("=", 1)
                    if name in inputs:
                        inputs[name] = spec
        ins = ";".join("%s=%s" % i for i in inputs.items())
        ds = sorted(set(depths[p] for p in c["flags"]["supportedPixelDepths"] if p in depths),
                    key=["byte", "short", "float"].index)
        if profile == "ofx11":
            # the --ofx11 host only has byte and float (hostDemo refuses -d short)
            ds = [d for d in ds if d != "short"]
        info[ctx] = (ins, ds)
        if skipped:
            continue
        gl_needed = gl_reason is not None and c["flags"].get("openGLRenderSupported") == "needed"
        if gl_needed and not gpu_ok:
            # this hostDemo cannot render with OpenGL (--opengl-describe is refused without --describe)
            gl_only = True
            continue
        for depth in ds:
            if not gl_needed:
                print("%s-v%s-%s-%s|%s|%s|%s|%s|%s|%s|||" % (pid, major, ctx, depth, bundle, pid, major, ctx,
                                                           depth, ins))
                n += 1
            if gl_reason is not None and gpu_ok:
                # the same render with OpenGL (hostDemo --gpu=opengl)
                print("%s-v%s-%s-%s-gpu|%s|%s|%s|%s|%s|%s||--gpu=opengl|" % (pid, major, ctx, depth, bundle, pid,
                                                                         major, ctx, depth, ins))
                n += 1
    # PARAMS: one more render per entry, in the given context (default: general if the
    # plug-in has it, else filter) and at the given depth (default: float, or the highest
    # supported one)
    for i, (rx, pmajor, pctx, pdepth, words, ref) in enumerate(params):
        if not re.fullmatch(rx, pid) or pmajor not in (None, major):
            continue
        ctx = pctx or ("general" if "general" in info else "filter")
        if ctx not in info or not info[ctx][1] or (pdepth and pdepth not in info[ctx][1]):
            continue
        ins, ds = info[ctx]
        depth = pdepth or ds[-1]
        print("%s-v%s-%s-%s-params%d|%s|%s|%s|%s|%s|%s|%d|%s|%s" % (pid, major, ctx, depth, i + 1, bundle, pid,
                                                                 major, ctx, depth, ins, i + 1, ";".join(words), ref))
        n += 1
    if gl_only:
        print("DESC|%s|%s||described only (needs OpenGL render, which hostDemo lacks): %s"
              % (tag, pid, gl_reason), file=sys.stderr)
    if n == 0 and not gl_only:
        print("FAIL|%s|%s||no render: no filter or general context, or no supported depth (contexts: %s)"
              % (tag, pid, " ".join(contexts)), file=sys.stderr)
EOF
    { cat "$work/describe-results.txt"; echo "smoketest: listing the renders failed"; exit 1; }
nrenders=$(wc -l < "$work/jobs.txt" | tr -d ' ')
echo "$nrenders renders, $jobs at a time..."

# Render (in parallel). As above, a failed worker leaves no status file.
tr '\n' '\0' < "$work/jobs.txt" | xargs -r -0 -n 1 -P "$jobs" "$0" --render-job ||
    failed_workers="$failed_workers${failed_workers:+, }render (xargs exit status $?)"

# Check the renders and print the results
mode=full
[ "$quick" = 0 ] || mode=quick
[ -z "$only" ] || mode=only
[ -z "$SMOKETEST_BUNDLES" ] && [ "${SMOKETEST_NO_TEST:-0}" != 1 ] || mode=$mode-partial
status=0
python3 -I - "$work" "${SMOKETEST_DEBUG:-0}" "$mode" "${SMOKETEST_XPASS_OK:-0}" "$profile" << 'EOF' || status=$?
import json, os, re, sys
work, debug, mode, xpass_ok = sys.argv[1], sys.argv[2] == "1", sys.argv[3], sys.argv[4] == "1"
profile = sys.argv[5]
# stale entries are only a warning in a partial build (SMOKETEST_BUNDLES, SMOKETEST_NO_TEST) or
# with SMOKETEST_XPASS_OK
lenient = mode.endswith("partial") or xpass_ok
sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, os.environ["SMOKETEST_TOOLS"])
import smoketest_lists
def entries(name):
    return smoketest_lists.entries(work, name)
def only_in(desc):
    return smoketest_lists.only_in(desc, debug, profile)
xfail = []
for x in entries("xfail.txt"):
    m = re.fullmatch(smoketest_lists.SELECTOR + r"\|([^|]*)\|(.*)", x)
    if not m:
        sys.exit("bad XFAIL entry: " + x)
    rx, major, ctx, depth, msg, desc = m.groups()
    keep, desc = only_in(desc)
    if keep:
        xfail.append((rx, major, ctx, depth, msg, desc, x))
identity = []
for x in entries("identity.txt"):
    m = re.fullmatch(r"([^:@|]*)(?::([0-9]+))?(?:@(filter|general))?\|(.*)", x)
    if not m:
        sys.exit("bad IDENTITY entry: " + x)
    if only_in(m.group(4))[0]:
        identity.append(m.groups() + (x,))
skip = []
for x in entries("skip.txt"):
    rx, reason = x.split("|", 1)
    keep, reason = only_in(reason)
    if keep:
        skip.append((rx, x))
# OPENGL: an entry that matches no plug-in is stale, as in SKIP
opengl_entries = entries("opengl.txt")
skip += [(x.split("|", 1)[0], x) for x in opengl_entries]
used = set()  # the entries of XFAIL, IDENTITY and SKIP that were used (PARAMS: params_used)
params_used = set()  # the numbers of the PARAMS entries that were rendered
order = ["ok", "SKIP", "XFAIL", "XPASS", "FAIL"]
plugins = {}  # plug-in (identifier and major version) -> worst result
nrender = dict((k, 0) for k in order)
NOREF = "PARAMS renders without a default render to compare to"
DESCRIBED = "described only, OpenGL render needed (OPENGL)"
notes = {"identity": set(), "empty RoD": set(), NOREF: set(), DESCRIBED: set()}
# hostDemo is built with the OFX_EXTENSIONS_* of the plug-ins (-DOFX_BUILD_HOSTDEMO=ON):
# it always renders both views of a stereo frame (the report has no view count), except
# with --ofx14 and --ofx11 (one view, outputs without the l/r suffix)
VIEWS = 1 if profile in ("ofx14", "ofx11") else 2
PROFILE = profile


def note(tag, res):
    if tag not in plugins or order.index(res) > order.index(plugins[tag]):
        plugins[tag] = res


def known(pid, major, ctx, depth):
    return next((x for x in xfail if re.fullmatch(x[0], pid) and x[1] in (None, major)
                 and x[2] in (None, ctx) and x[3] in (None, depth)), None)


def expected_identity(pid, major, ctx):
    return next((x for x in identity if re.fullmatch(x[0], pid) and x[1] in (None, major)
                 and x[2] in (None, ctx)), None)


def result(errors, log, x):
    summary = "; ".join(errors[:4]) + (" (+%d)" % (len(errors) - 4) if len(errors) > 4 else "")
    if x:
        if not errors:
            if xpass_ok:
                used.add(x[6])
                return "XPASS", "known bug fixed (accepted: SMOKETEST_XPASS_OK): " + x[5]
            return "XPASS", "known bug fixed? Update the XFAIL list of smoketest.sh: " + x[5]
        if re.search(x[4], summary + "\n" + log):
            used.add(x[6])
            return "XFAIL", x[5]
        return "FAIL", "%s (known bug, but not the expected message \"%s\": %s)" % (summary, x[4], x[5])
    return ("FAIL", summary) if errors else ("ok", "")


def read_json(path):
    try:
        return json.load(open(path))
    except (OSError, ValueError):
        return None


def outputs(r):
    # time -> (bounds, CRC32) of the output of view 0, in a report (with the bounds, an
    # integer translation of the same pixels is not the same output)
    res = {}
    for k in (r or {}).get("renders") or []:
        o = k.get("output") or {}
        if not k.get("view") and o.get("crc32"):
            res[k.get("time")] = (json.dumps(o.get("bounds")), o["crc32"])
    return res


def read(path, default=None):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return default


for line in open(os.path.join(work, "plugins.txt")):
    pid = line.split()[1]
    for rx, x in skip:
        if re.fullmatch(rx, pid):
            used.add(x)
for line in open(os.path.join(work, "describe-results.txt")):
    kind, tag, pid, ctx, msg = line.rstrip("\n").split("|", 4)
    if kind == "SKIP":
        res = "SKIP"
    elif kind == "DESC":
        # an OPENGL plug-in that needs OpenGL: described, not rendered
        res = "ok"
        notes[DESCRIBED].add(tag)
    else:
        major = tag.rsplit(" v", 1)[1]
        # a describe failure: the XFAIL entries without a depth, and without a context
        # (hostDemo --describe failed) or with this context (describeInContext failed)
        res, msg = result([msg], "", known(pid, major, ctx or None, None))
    print("%-5s %-50s %s" % (res, tag + (" " + ctx if ctx else ""), msg))
    note(tag, res)
times = []
for line in open(os.path.join(work, "jobs.txt")):
    name, bundle, pid, major, ctx, depth, inputs, pnum, pwords, pref = line.rstrip("\n").split("|")
    if pnum:
        params_used.add(int(pnum))
    tag = "%s v%s" % (pid, major)
    d = os.path.join(work, "jobs", name)
    log = read(os.path.join(d, "run.log"), "")
    errors = []
    st = (read(os.path.join(d, "status")) or "").split()
    if len(st) != 2:
        # the worker failed (set -e) before it wrote its status
        status, r = "unknown", None
        errors.append("the render worker failed (no status)")
    else:
        status, secs = st
        times.append((int(secs), name))
        sizes = dict((l.split()[0], int(l.split()[1]))
                     for l in (read(os.path.join(d, "files.txt")) or "").splitlines() if l.strip())
        try:
            r = json.load(open(os.path.join(d, "report.json")))
        except (OSError, ValueError) as e:
            r = None
            errors.append("hostDemo exit status %s, no report (%s)" % (status, e.__class__.__name__))
    ident_x = expected_identity(pid, major, ctx)
    if r is not None:
        if (r.get("host") or {}).get("profile", "default") != PROFILE:
            errors.append("report: host profile %s, expected %s" % ((r.get("host") or {}).get("profile"), PROFILE))
        for a in r.get("actions") or []:
            if not a.get("ok"):
                errors.append("action %s failed (%s)" % (a.get("action"), a.get("status")))
        if ctx == "filter":
            # "a plugin cannot change the component type of the Output clip, it will always be the
            # same as the Source clip" (ofxImageEffectContexts.rst, The Filter Context), and "mandated
            # input clips cannot have their component types remapped, nor can the output"
            # (ofxClipPreferences.rst): after getClipPreferences, Output has the components of Source
            cpc = (r.get("clipPreferences") or {}).get("clips") or {}
            src, dst = cpc.get("Source") or {}, cpc.get("Output") or {}
            if src.get("components") and dst.get("components") and src["components"] != dst["components"]:
                errors.append("filter context: after getClipPreferences, Output is %s and Source %s (Output must"
                              " have the components of Source)" % (dst["components"], src["components"]))
        renders = r.get("renders") or []
        is_identity = any((k.get("isIdentity") or {}).get("identity") for k in renders)
        for m in r.get("messages") or []:
            # the debug builds of identity effects post "OFX Host should not render"
            # when hostDemo renders them anyway
            if is_identity and "should not render" in (m.get("text") or ""):
                continue
            if m.get("type") in ("OfxMessageError", "OfxMessageFatal"):
                errors.append("%s %s: %s" % (m.get("kind"), m.get("type"), m.get("text")))
        frames = r.get("frames") or [0, 1]
        stopped = False  # hostDemo stopped at an identity render
        for k in renders:
            o = k.get("output")
            at = "t=%s view %s" % (k.get("time"), k.get("view"))
            rod = (k.get("regionOfDefinition") or {})
            ident = (k.get("isIdentity") or {}).get("identity")
            if not o:
                b = rod.get("rod")
                if rod.get("ok") and b and (b[0] >= b[2] or b[1] >= b[3]) and not k.get("render"):
                    notes["empty RoD"].add(tag)
                    continue
                if ident and k.get("render") is not None:
                    # rendered anyway: the plug-in may not write the output (or fail)
                    if ident_x:
                        used.add(ident_x[4])
                        notes["identity"].add(tag)
                        stopped = True
                        continue
                    errors.append("%s: an identity (isIdentity: %s) without an output, not in the IDENTITY list"
                                  " of smoketest.sh: render %s" % (at, json.dumps(k.get("isIdentity")),
                                                                   json.dumps(k.get("render"))))
                    stopped = True
                    continue
                errors.append("%s: render failed: %s" % (at, json.dumps(k.get("render"))))
                continue
            if not (k.get("render") or {}).get("ok"):
                errors.append("%s: render failed: %s" % (at, json.dumps(k.get("render"))))
            if "--gpu=opengl" in pwords.split(";") and (not (k.get("render") or {}).get("openGL")
                                                       or (k.get("render") or {}).get("cpuRetry")):
                # a *-gpu render (OPENGL): rendered with OpenGL, without a CPU retry
                errors.append("%s: not rendered with OpenGL (hostDemo --gpu=opengl): render %s"
                              % (at, json.dumps(k.get("render"))))
            if o.get("nan"):
                errors.append("%s: %d unwritten (NaN) pixels" % (at, o["nan"]))
            if not o.get("files"):
                errors.append("%s: no output file" % at)
            for f in o.get("files") or []:
                if not sizes.get(f):
                    errors.append("%s: missing output file %s" % (at, f))
        want = [(t, v) for t in frames for v in range(VIEWS)]
        got = [(k.get("time"), k.get("view") or 0) for k in renders]
        if got != want and not stopped:
            errors.append("renders (time, view) %s, expected %s" % (got, want))
        if pnum:
            # a PARAMS render must not be an identity: not isIdentity, and not the output of
            # the default render at t=0 (view 0)
            if is_identity:
                errors.append("an identity with the parameters of PARAMS (%s): change them"
                              % " ".join(pwords.split(";")))
            base = name.rsplit("-params", 1)[0]
            out = outputs(r)
            ref = outputs(read_json(os.path.join(work, "jobs", base, "report.json")))
            if 0 not in ref:
                notes[NOREF].add(name)
            elif out.get(0) == ref[0]:
                errors.append("t=0: the output is the one of the default parameters (bounds and CRC32 %s) with"
                              " the parameters of PARAMS (%s): change them"
                              % (" ".join(ref[0]), " ".join(pwords.split(";"))))
            if pref:
                other = outputs(read_json(os.path.join(work, "jobs", "%s-params%s" % (base, pref), "report.json")))
                same = sorted(t for t in out if other.get(t) == out[t])
                if not other:
                    errors.append("differs:%s: no output of PARAMS entry %s to compare to" % (pref, pref))
                elif same:
                    errors.append("t=%s: the output is the one of PARAMS entry %s (differs:%s)"
                                  % (",".join("%g" % t for t in same), pref, pref))
        if status != "0" and not stopped and not errors:
            errors.append("hostDemo exit status %s" % status)
    if status not in ("0", "1") and r is not None:
        errors.insert(0, "hostDemo exit status %s" % status)
    if status == "0" and r is not None and not r.get("ok"):
        errors.append("report: not ok")
    res, msg = result(errors, log, known(pid, major, ctx, depth))
    if res == "XFAIL" and ident_x:
        # e.g. debug builds: the identity render asserts (XFAIL), so it has no report
        used.add(ident_x[4])
    nrender[res] += 1
    note(tag, res)
    if res != "ok":
        print("%-5s %-50s %s" % (res, name, msg))
    if res == "FAIL" or (res == "XPASS" and not xpass_ok):
        for l in log.strip().splitlines()[-15:]:
            print("  | " + l)
        print("  | (cd %s && OFX_PLUGIN_PATH=<%s bundle dir> hostDemo --no-std-paths --no-cache%s -p %s -m %s -c %s -d %s%s%s)"
              % (name, bundle, "" if profile == "default" else " --" + profile, pid, major, ctx, depth, "".join(" -i " + i for i in inputs.split(";") if i),
                 "".join(" " + w for w in pwords.split(";") if w)))
times.sort(reverse=True)
print()
for k, v in notes.items():
    if v:
        print("not failures (%s): %s" % (k, ", ".join(sorted(v))))
print("slowest renders: " + ", ".join("%s %ds" % (n, s) for s, n in times[:5]))
# Stale entries: SKIP and PARAMS unless --only; XFAIL and IDENTITY in full mode only
# (--quick does not render every context). They fail, except in a partial build
# (SMOKETEST_BUNDLES, SMOKETEST_NO_TEST) or with SMOKETEST_XPASS_OK (then they are warnings).
stale = []
if not mode.startswith("only"):
    stale += [("OPENGL   " if x in opengl_entries else "SKIP     ") + x for rx, x in skip if x not in used]
    stale += ["PARAMS   " + x for i, x in enumerate(entries("params.txt")) if i + 1 not in params_used]
if mode.startswith("full"):
    stale += ["XFAIL    " + x[6] for x in xfail if x[6] not in used]
    stale += ["IDENTITY " + x[4] for x in identity if x[4] not in used]
for x in stale:
    print("%s stale entry (matches nothing in this run: remove it from smoketest.sh): %s"
          % ("WARNING:" if lenient else "FAIL ", x))
count = dict((k, sum(1 for v in plugins.values() if v == k)) for k in order)
print("smoketest: %d plug-ins: %d ok, %d skipped, %d with known bugs (XFAIL), %d failed, %d XPASS"
      % (len(plugins), count["ok"], count["SKIP"], count["XFAIL"], count["FAIL"], count["XPASS"]))
print("smoketest: %d renders: %d ok, %d XFAIL, %d XPASS, %d FAIL"
      % (nrender["ok"] + nrender["XFAIL"] + nrender["XPASS"] + nrender["FAIL"], nrender["ok"],
         nrender["XFAIL"], nrender["XPASS"], nrender["FAIL"]))
bad = sorted(t for t, v in plugins.items() if v == "FAIL" or (v == "XPASS" and not xpass_ok))
if bad:
    print("failed: " + ", ".join(bad))
if stale and not lenient:
    print("failed: %d stale entries" % len(stale))
sys.exit(1 if bad or (stale and not lenient) else 0)
EOF
if [ -n "$failed_workers" ]; then
    echo "smoketest: workers failed: $failed_workers (their plug-ins or renders fail above: no status)"
    status=1
fi
if [ -n "$out" ]; then
    mkdir -p "$out"
    rm -rf "$out/jobs" "$out/describe"
    mv "$work/jobs" "$work/describe" "$out/"
    cp "$work/plugins.txt" "$work/jobs.txt" "$out/"
fi
echo "smoketest: $(( $(date +%s) - start ))s"
exit $status
