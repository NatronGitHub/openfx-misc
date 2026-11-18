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
# Environment: HOSTDEMO (the hostDemo binary, default PREFIX/bin/hostDemo),
# CXX (to build ofxload.cpp, default c++), CMAKE_ARGS (--supportext),
# SMOKETEST_TIMEOUT (seconds per describe or render, default 120),
# SMOKETEST_DEBUG=1 (a debug build: also use the "debug:" entries of the lists below),
# SMOKETEST_BUNDLES (the expected bundles, see BUNDLES below; e.g. "Misc:0" for a build
# with -DBUILD_CIMG_PLUGINS=OFF -DBUILD_SHADERTOY=OFF -DBUILD_TESTING=OFF).
# Linux and macOS (needs python3 and perl).
#
# Each render is one hostDemo run (--no-std-paths --no-cache: only the bundle of
# the plug-in is in OFX_PLUGIN_PATH), at t=0 and 1 (both views: hostDemo is
# built with the OFX_EXTENSIONS_* of the plug-ins), on the PAL SD project
# (720x576), with the outputs in float PFM files. The inputs are: ramp for the
# first input clip, checker:16 for the second one, a solid colour for the
# others; the optional clips of generators (plug-ins with a generator context)
# are disconnected; masks are disconnected in the filter context and get a
# checker:32 in the general context; INPUTS below overrides these. A plug-in
# fails if:
# - hostDemo --describe fails (load, describe or describeInContext), or the
#   plug-in supports neither the filter nor the general context;
# - a render fails: hostDemo fails (an action failed: createInstance,
#   getClipPreferences, getRegionOfDefinition, render...; an image fetch failed;
#   a float output pixel was not written, i.e. is still NaN; or the plug-in
#   crashed), or takes more than SMOKETEST_TIMEOUT seconds; the --report JSON
#   shows a missing render or output, or an error or fatal message from the
#   plug-in; or an output file is missing or empty.
# - a bundle of BUNDLES is missing, or has fewer plug-ins than its minimum;
# - an entry of SKIP, IDENTITY or XFAIL is stale (see below).
# Two cases are not failures: an identity effect (isIdentity) of the IDENTITY list
# that does not write its output when hostDemo renders it anyway (the spec says
# the host should not call render then; hostDemo stops at that render), and an
# empty region of definition (nothing to render, e.g. FrameRange outside its
# range). The plug-ins that cannot run in hostDemo (OpenGL only, ...) are in the
# SKIP list below, with the reason. Known bugs are expected failures (XFAIL, in
# the XFAIL list, each with its GitHub issue), with the message that their
# failure must show: an XFAIL render that passes (XPASS) fails the test, so that
# the list is updated.
# Stale entries fail the test, so that the lists stay exact: a SKIP entry that
# matches no plug-in (except with --only), and, in full mode only (not --quick,
# not --only; --quick does not render every context), an IDENTITY or XFAIL entry
# that matches no identity render or no expected failure. With SMOKETEST_BUNDLES
# (a partial build), stale entries are only a warning.
set -eo pipefail

# ---------------------------------------------------------------------------
# The plug-ins that are not tested: "identifier regex|reason" (the whole identifier).
# The expected bundles: "name:minimum number of plug-ins (identifier and major version)"
# (the counts of a release build; debug builds have more). A missing bundle, or one with
# fewer plug-ins, fails the test (raise or lower the minimum when plug-ins are added or
# removed). Other bundles (DebugProxy) are not required. SMOKETEST_BUNDLES replaces it.
BUNDLES=(Misc:168 CImg:40 Shadertoy:1 Test:6)
# shellcheck disable=SC2206
[ -z "$SMOKETEST_BUNDLES" ] || BUNDLES=($SMOKETEST_BUNDLES)
# The plug-ins that are not tested: "identifier regex|reason" (the whole identifier; a
# "debug:" reason is only used in debug builds).
SKIP=(
    "net.sf.openfx.Shadertoy|OpenGL only: hostDemo has no OpenGL render suite (describe fails)"
    "net.sf.openfx.TestOpenGL|OpenGL only (Test bundle): hostDemo has no OpenGL render suite (describe fails)"
    "net.sf.openfx.ColorLookupPlugin|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.HueCorrect|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.HueKeyer|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.TimeBufferRead|debug:reads the buffer of a TimeBufferWrite instance of the same name, in the same host process (getClipPreferences fails: No TimeBuffer exists with name \"\")"
)
# The plug-ins that are identity effects with their default parameters, and do not write
# their output when hostDemo renders them anyway: "identifier regex[:major][@context]|why"
# (a "debug:" reason is only used in debug builds). Another identity render without an
# output fails, so that a plug-in that becomes an identity by default is noticed.
IDENTITY=(
    "net.sf.openfx.FrameHold|always an identity: the Source at another time"
    "net.sf.openfx.timeOffset|always an identity: the Source at another time"
    "net.sf.openfx.TrackerPM@general|trackers are identity effects (SupportExt's GenericTrackerPlugin)"
    "net.sf.openfx.ShufflePlugin:1@filter|the default channels are those of A, which is the Source in the filter context"
    # debug builds: render posts "OFX Host should not render" and fails (kOfxStatFailed) when
    # it is called for an identity (release builds copy the input)
    "net.sf.openfx.NoOpPlugin@filter|debug:an identity unless Force Copy is set"
    "net.sf.openfx.NoTimeBlurPlugin@filter|debug:always an identity: the Source at another time"
    "net.sf.openfx.Retime|debug:an identity at the default speed (1: whole source frames)"
    "net.sf.openfx.TestGroups|debug:an identity unless Force Copy is set (Test bundle)"
)
# (the entries of SKIP, IDENTITY, INPUTS and XFAIL are split at "|": no "|" in the regexes)
# Inputs that replace the default ones: "identifier regex[@context]|Clip=spec ...".
# DATA is the directory of the files made below (square32.pfm: a 32x32 white Pf,
# box5.pfm: a 5x5 white PF).
INPUTS=(
    # the kernel (Filter) is a whole image by default: a 720x576 convolution takes minutes
    "net.sf.cimg.CImgConvolve|Filter=file:DATA/box5.pfm"
    # inpaint a 32x32 square, not half of the image (minutes)
    "eu.cimg.Inpaint@general|Mask=file:DATA/square32.pfm@344,272"
)
# Known bugs, each tracked by a GitHub issue: "identifier regex[:major][@context][/depth]|
# expected message (Python re, searched in the failure summary and the hostDemo log)|
# description" (context: filter or general, depth: byte, short or float; a "debug:"
# description is only expected in debug builds). The entries without a depth also apply
# to describe failures: the entries without a context to hostDemo --describe failures,
# those with a context to the describeInContext failures of that context.
XFAIL=(
    # issue #166
    "net.sf.openfx.Premult:2|describe in context action failed|v2 describes the multi-plane parameters when the host has the plane suite V2 and dynamic choices, but not kFnOfxImageEffectPropMultiPlanar (Premult.cpp does not check isMultiPlanar), and SupportExt's MultiPlane (describeInContextAddPlaneChannelChoice) throws"
    "net.sf.openfx.Unpremult:2|describe in context action failed|v2 describes the multi-plane parameters when the host has the plane suite V2 and dynamic choices, but not kFnOfxImageEffectPropMultiPlanar (Premult.cpp does not check isMultiPlanar), and SupportExt's MultiPlane (describeInContextAddPlaneChannelChoice) throws"
    # issue #163
    "net.sf.openfx.switchPlugin|create instance action failed \(status 9\)|on hosts other than Natron, describeInContext defines 2 source clips, but the instance fetches kClipSourceCount (16): fetchClip(\"2\") throws kOfxStatErrBadHandle"
    # issue #164
    "net.sf.openfx.TrackerPM@filter|create instance action failed \(status 9\)|the instance fetches the Mask clip, which describeInContext only defines in the general, paint and tracker contexts"
    # issue #165 (PIK supports short and float only)
    "net.sf.openfx.PIK@filter|get region of definition action failed|the filter context has no Source clip (the spec mandates one), so HostSupport's default region of definition fails"
    # issue #167: debug builds, failed assertions (an unconnected clip has the components
    # kOfxImageComponentNone)
    "net.sf.openfx.DenoiseSharpen@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.Log2Lin@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.PLogLin@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.FrameBlend@filter|_fgMClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the FgM clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.ShufflePlugin:1@filter|_srcClipA && _srcClipB && _dstClip|debug:v1 (ShuffleClassic) is an identity in the filter context by default, and its render, which hostDemo calls anyway, asserts that the A and B clips exist (the filter context has Source only)"
    "net.sf.openfx.TimeBufferWrite|Pure virtual function called|debug:render fails (kOfxStatFailed) without a TimeBufferRead, then a pure virtual function is called (abort)"
)
# ---------------------------------------------------------------------------

case "$(uname -s)" in
    # the bundle binaries are in Contents/<arch>: MacOS, Linux-x86-64, Linux-aarch64...
    Darwin) arch=MacOS ;;
    Linux) arch='Linux-*' ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

SMOKETEST_TIMEOUT=${SMOKETEST_TIMEOUT:-120}

# watchdog LOG BUNDLE HOSTDEMO-ARGS...: run hostDemo with only BUNDLE in OFX_PLUGIN_PATH
# and its output in LOG, killed by SIGALRM after SMOKETEST_TIMEOUT seconds (perl's alarm
# survives exec), and set $status to its exit status. The caller sends its stderr to LOG,
# for the shell's message about a crash.
watchdog() {
    local log=$1 bundle=$2
    shift 2
    status=0
    ( OFX_PLUGIN_PATH=$SMOKETEST_WORK/path/$bundle exec perl -e 'alarm shift; exec @ARGV or die "$!\n"' \
          "$SMOKETEST_TIMEOUT" "$HOSTDEMO" --no-std-paths --no-cache --quiet "$@" ) > "$log" 2>&1 || status=$?
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
    watchdog describe.log "$bundle" -p "$id" -m "$major" --describe describe.json
    echo "$status" > status
    exit 0
fi
if [ "$1" = --render-job ]; then
    IFS='|' read -r name bundle id major ctx depth inputs <<< "$2"
    d=$SMOKETEST_WORK/jobs/$name
    mkdir -p "$d"
    cd "$d"
    args=()
    IFS=';' read -r -a specs <<< "$inputs"
    for i in "${specs[@]}"; do [ -z "$i" ] || args+=(-i "$i"); done
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
top=$(cd "$(dirname "$0")/../.." && pwd)
supportext='' build='' out='' only='' quick=0 prefix='' jobs=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)
while [ $# -gt 0 ]; do
    case "$1" in
        --supportext) supportext=$2; shift ;;
        --build) build=$2; shift ;;
        --out) out=$2; shift ;;
        --only) only=$2; shift ;;
        --quick) quick=1 ;;
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
echo "hostDemo: $HOSTDEMO"
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
printf '%s\n' "${INPUTS[@]}" > "$work/inputs.txt"
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

# Describe each plug-in (in parallel). A worker that fails (xargs exit status 123)
# leaves no status file: its plug-ins fail below.
export HOSTDEMO SMOKETEST_WORK=$work SMOKETEST_TIMEOUT
[ -z "$out" ] || export SMOKETEST_KEEP=1
failed_workers=
tr '\n' '\0' < "$work/plugins.txt" | xargs -r -0 -n 1 -P "$jobs" "$0" --describe-job ||
    failed_workers="describe (xargs exit status $?)"

# The render jobs, one per line: "name|bundle|id|major|context|depth|Clip=spec;..."
python3 -I - "$work" "$quick" "${SMOKETEST_DEBUG:-0}" > "$work/jobs.txt" 2> "$work/describe-results.txt" << 'EOF' ||
import json, os, re, sys
work, quick, debug = sys.argv[1], sys.argv[2] == "1", sys.argv[3] == "1"
def entries(name):
    return [l.rstrip("\n") for l in open(os.path.join(work, name)) if l.strip()]
skip = []
for s in entries("skip.txt"):
    rx, reason = s.split("|", 1)
    if reason.startswith("debug:"):
        if not debug:
            continue
        reason = reason[6:]
    skip.append((rx, reason))
overrides = []
for s in entries("inputs.txt"):
    m = re.fullmatch(r"([^@|]*)(?:@(filter|general))?\|(.*)", s)
    if not m:
        sys.exit("bad INPUTS entry: " + s)
    overrides.append((m.group(1), m.group(2), m.group(3).replace("DATA", os.path.join(work, "data")).split()))
depths = {"OfxBitDepthByte": "byte", "OfxBitDepthShort": "short", "OfxBitDepthFloat": "float"}
def text(path):
    try:
        return open(path, errors="replace").read().strip().replace("\n", " / ")
    except OSError:
        return "(no log)"
# describe-results.txt: "SKIP|tag|identifier||reason" or "FAIL|tag|identifier|context|message"
# (context: empty for hostDemo --describe failures)
for line in open(os.path.join(work, "plugins.txt")):
    bundle, pid, major = line.split()
    reason = next((r for rx, r in skip if re.fullmatch(rx, pid)), None)
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
    n = 0
    for ctx in ("filter", "general"):
        c = contexts.get("OfxImageEffectContext" + ctx.capitalize())
        if c is None or (quick and ctx == "filter" and "OfxImageEffectContextGeneral" in contexts):
            continue
        if not c.get("ok"):
            print("FAIL|%s|%s|%s|describeInContext(%s) failed: %s"
                  % (tag, pid, ctx, ctx, text(os.path.join(d, "describe.log"))), file=sys.stderr)
            n += 1  # not also "no render"
            continue
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
        for depth in sorted(set(depths[p] for p in c["flags"]["supportedPixelDepths"] if p in depths),
                            key=["byte", "short", "float"].index):
            print("%s-v%s-%s-%s|%s|%s|%s|%s|%s|%s" % (pid, major, ctx, depth, bundle, pid, major, ctx,
                                                     depth, ins))
            n += 1
    if n == 0:
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
[ -z "$SMOKETEST_BUNDLES" ] || mode=$mode-partial
status=0
python3 -I - "$work" "${SMOKETEST_DEBUG:-0}" "$mode" << 'EOF' || status=$?
import json, os, re, sys
work, debug, mode = sys.argv[1], sys.argv[2] == "1", sys.argv[3]
def entries(name):
    return [l.rstrip("\n") for l in open(os.path.join(work, name)) if l.strip()]
def debug_only(desc):
    # (keep, desc): the "debug:" entries are only used in debug builds
    if desc.startswith("debug:"):
        return debug, desc[6:]
    return True, desc
xfail = []
for x in entries("xfail.txt"):
    m = re.fullmatch(r"([^:@/|]*)(?::([0-9]+))?(?:@(filter|general))?(?:/(byte|short|float))?\|([^|]*)\|(.*)", x)
    if not m:
        sys.exit("bad XFAIL entry: " + x)
    rx, major, ctx, depth, msg, desc = m.groups()
    keep, desc = debug_only(desc)
    if keep:
        xfail.append((rx, major, ctx, depth, msg, desc, x))
identity = []
for x in entries("identity.txt"):
    m = re.fullmatch(r"([^:@|]*)(?::([0-9]+))?(?:@(filter|general))?\|(.*)", x)
    if not m:
        sys.exit("bad IDENTITY entry: " + x)
    if debug_only(m.group(4))[0]:
        identity.append(m.groups() + (x,))
skip = []
for x in entries("skip.txt"):
    rx, reason = x.split("|", 1)
    keep, reason = debug_only(reason)
    if keep:
        skip.append((rx, x))
used = set()  # the entries of XFAIL, IDENTITY and SKIP that were used
order = ["ok", "SKIP", "XFAIL", "XPASS", "FAIL"]
plugins = {}  # plug-in (identifier and major version) -> worst result
nrender = dict((k, 0) for k in order)
notes = {"identity": set(), "empty RoD": set()}
# hostDemo is built with the OFX_EXTENSIONS_* of the plug-ins (-DOFX_BUILD_HOSTDEMO=ON):
# it always renders both views of a stereo frame (the report has no view count)
VIEWS = 2


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
            return "XPASS", "known bug fixed? Update the XFAIL list of smoketest.sh: " + x[5]
        if re.search(x[4], summary + "\n" + log):
            used.add(x[6])
            return "XFAIL", x[5]
        return "FAIL", "%s (known bug, but not the expected message \"%s\": %s)" % (summary, x[4], x[5])
    return ("FAIL", summary) if errors else ("ok", "")


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
    else:
        major = tag.rsplit(" v", 1)[1]
        # a describe failure: the XFAIL entries without a depth, and without a context
        # (hostDemo --describe failed) or with this context (describeInContext failed)
        res, msg = result([msg], "", known(pid, major, ctx or None, None))
    print("%-5s %-50s %s" % (res, tag + (" " + ctx if ctx else ""), msg))
    note(tag, res)
times = []
for line in open(os.path.join(work, "jobs.txt")):
    name, bundle, pid, major, ctx, depth, inputs = line.rstrip("\n").split("|")
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
        for a in r.get("actions") or []:
            if not a.get("ok"):
                errors.append("action %s failed (%s)" % (a.get("action"), a.get("status")))
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
    if res in ("FAIL", "XPASS"):
        for l in log.strip().splitlines()[-15:]:
            print("  | " + l)
        print("  | (cd %s && OFX_PLUGIN_PATH=<%s bundle dir> hostDemo --no-std-paths --no-cache -p %s -m %s -c %s -d %s%s)"
              % (name, bundle, pid, major, ctx, depth, "".join(" -i " + i for i in inputs.split(";") if i)))
times.sort(reverse=True)
print()
for k, v in notes.items():
    if v:
        print("not failures (%s): %s" % (k, ", ".join(sorted(v))))
print("slowest renders: " + ", ".join("%s %ds" % (n, s) for s, n in times[:5]))
# Stale entries: SKIP unless --only; XFAIL and IDENTITY in full mode only (--quick does
# not render every context). They fail, except with SMOKETEST_BUNDLES (a partial build).
stale = []
if not mode.startswith("only"):
    stale += ["SKIP     " + x for rx, x in skip if x not in used]
if mode.startswith("full"):
    stale += ["XFAIL    " + x[6] for x in xfail if x[6] not in used]
    stale += ["IDENTITY " + x[4] for x in identity if x[4] not in used]
for x in stale:
    print("%s stale entry (matches nothing in this run: remove it from smoketest.sh): %s"
          % ("WARNING:" if mode.endswith("partial") else "FAIL ", x))
count = dict((k, sum(1 for v in plugins.values() if v == k)) for k in order)
print("smoketest: %d plug-ins: %d ok, %d skipped, %d with known bugs (XFAIL), %d failed, %d XPASS"
      % (len(plugins), count["ok"], count["SKIP"], count["XFAIL"], count["FAIL"], count["XPASS"]))
print("smoketest: %d renders: %d ok, %d XFAIL, %d XPASS, %d FAIL"
      % (nrender["ok"] + nrender["XFAIL"] + nrender["XPASS"] + nrender["FAIL"], nrender["ok"],
         nrender["XFAIL"], nrender["XPASS"], nrender["FAIL"]))
bad = sorted(t for t, v in plugins.items() if v in ("FAIL", "XPASS"))
if bad:
    print("failed: " + ", ".join(bad))
if stale and not mode.endswith("partial"):
    print("failed: %d stale entries" % len(stale))
sys.exit(1 if bad or (stale and not mode.endswith("partial")) else 0)
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
