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
#                     submodule (-DOFXMISC_SUPPORTEXT_DIR=DIR -DOFX_BUILD_HOSTDEMO=ON,
#                     plus $CMAKE_ARGS), and install it into PREFIX
#   --build DIR       the build directory of --supportext (default: a temporary one)
#   --out DIR         keep the outputs (gzipped PFM files), reports and logs in DIR
#   --only REGEX      only the plug-ins whose identifier matches REGEX (the whole
#                     identifier, Python re syntax)
#   --quick           only the most general context of each plug-in (general if
#                     it supports it, else filter): about half the renders (per-PR CI)
#   -j N              run N renders at a time (default: the number of CPUs)
# Environment: HOSTDEMO (the hostDemo binary, default PREFIX/bin/hostDemo),
# CXX (to build ofxload.cpp, default c++), CMAKE_ARGS (--supportext),
# SMOKETEST_TIMEOUT (seconds per render, default 120),
# SMOKETEST_DEBUG=1 (a debug build: also expect the "debug:" known bugs).
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
# Two cases are not failures: an identity effect (isIdentity) that does not
# write its output when hostDemo renders it anyway (the spec says the host
# should not call render then; hostDemo stops at that render), and an empty
# region of definition (nothing to render, e.g. FrameRange outside its range).
# The plug-ins that cannot run in hostDemo (OpenGL only, ...) are in the SKIP
# list below, with the reason. Known bugs are expected failures (XFAIL, in the
# XFAIL list), with the message that their failure must show: an XFAIL render
# that passes (XPASS) fails the test, so that the list is updated.
set -eo pipefail

# ---------------------------------------------------------------------------
# The plug-ins that are not tested: "identifier regex|reason" (the whole identifier).
SKIP=(
    "net.sf.openfx.Shadertoy|OpenGL only: hostDemo has no OpenGL render suite (describe fails)"
    "net.sf.openfx.TestOpenGL|OpenGL only (Test bundle): hostDemo has no OpenGL render suite (describe fails)"
    "net.sf.openfx.ColorLookupPlugin|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.HueCorrect|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.HueKeyer|needs parametric parameters, which hostDemo does not advertise (kOfxParamHostPropSupportsParametric: describeInContext throws kOfxStatErrMissingHostFeature)"
    "net.sf.openfx.TimeBufferRead|debug builds only: reads the buffer of a TimeBufferWrite instance of the same name, in the same host process (getClipPreferences fails: No TimeBuffer exists with name \"\")"
)
# (the entries of SKIP, INPUTS and XFAIL are split at "|": no "|" in the regexes)
# Inputs that replace the default ones: "identifier regex[@context]|Clip=spec ...".
# DATA is the directory of the files made below (square32.pfm: a 32x32 white Pf,
# box5.pfm: a 5x5 white PF).
INPUTS=(
    # the kernel (Filter) is a whole image by default: a 720x576 convolution takes minutes
    "net.sf.cimg.CImgConvolve|Filter=file:DATA/box5.pfm"
    # inpaint a 32x32 square, not half of the image (minutes)
    "eu.cimg.Inpaint@general|Mask=file:DATA/square32.pfm@344,272"
)
# Known bugs: "identifier regex[:major][@context][/depth]|expected message (Python re,
# searched in the failure summary and the hostDemo log)|description" (context: filter or
# general, depth: byte, short or float; a "debug:" description is only expected in debug
# builds). The entries without a context and depth also apply to describe failures.
XFAIL=(
    "net.sf.openfx.Premult:2|describe in context action failed|v2 describes the multi-plane parameters when the host has the plane suite V2 and dynamic choices, but SupportExt's MultiPlane (describeInContextAddPlaneChannelChoice) also requires kFnOfxImageEffectPropMultiPlanar, and throws"
    "net.sf.openfx.Unpremult:2|describe in context action failed|v2 describes the multi-plane parameters when the host has the plane suite V2 and dynamic choices, but SupportExt's MultiPlane (describeInContextAddPlaneChannelChoice) also requires kFnOfxImageEffectPropMultiPlanar, and throws"
    "net.sf.openfx.switchPlugin|create instance action failed \(status 9\)|on hosts other than Natron, describeInContext defines 2 source clips, but the instance fetches kClipSourceCount (16): fetchClip(\"2\") throws kOfxStatErrBadHandle"
    "net.sf.openfx.TrackerPM@filter|create instance action failed \(status 9\)|the instance fetches the Mask clip, which describeInContext only defines in the general, paint and tracker contexts"
    "net.sf.openfx.PIK@filter/short|get region of definition action failed|the filter context has no Source clip (the spec mandates one), so HostSupport's default region of definition fails"
    "net.sf.openfx.PIK@filter/float|get region of definition action failed|the filter context has no Source clip (the spec mandates one), so HostSupport's default region of definition fails"
    "net.sf.openfx.ImageStatistics|kOfxStatErrMissingHostFeature|render calls ValueParam::getKeyIndex, which hostDemo does not implement (kOfxStatErrMissingHostFeature), and does not handle the exception"
    # debug builds: failed assertions (an unconnected clip has the components kOfxImageComponentNone)
    "net.sf.openfx.DenoiseSharpen@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.Log2Lin@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.PLogLin@filter|_maskClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the Mask clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.FrameBlend@filter|_fgMClip->getPixelComponents\(\) == ePixelComponentAlpha|debug:the constructor asserts that the FgM clip is Alpha, also when it is not connected (filter context)"
    "net.sf.openfx.ShufflePlugin:1@filter|_srcClipA && _srcClipB && _dstClip|debug:v1 (ShuffleClassic) is an identity in the filter context by default, and its render, which hostDemo calls anyway, asserts that the A and B clips exist (the filter context has Source only)"
    "net.sf.openfx.TimeBufferWrite|Pure virtual function called|debug:render fails (kOfxStatFailed) without a TimeBufferRead, then a pure virtual function is called (abort)"
    "net.sf.openfx.TestPosition|killed by signal 11|Test bundle: SupportExt's Transform3x3Plugin only fetches the mix parameter if the Transform3x3 invert parameter exists, and getRegionsOfInterest dereferences it (NULL) when the mask is connected"
)
# ---------------------------------------------------------------------------

case "$(uname -s)" in
    Darwin) arch=MacOS ;;
    Linux) arch=Linux-x86-64 ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

# The worker: render one job (a line of jobs.txt, see below), in its own directory.
if [ "$1" = --render-job ]; then
    IFS='|' read -r name bundle id major ctx depth inputs <<< "$2"
    d=$SMOKETEST_WORK/jobs/$name
    mkdir -p "$d"
    cd "$d"
    args=()
    IFS=';' read -r -a specs <<< "$inputs"
    for i in "${specs[@]}"; do [ -z "$i" ] || args+=(-i "$i"); done
    start=$(date +%s)
    # A render that takes more than SMOKETEST_TIMEOUT seconds is killed by SIGALRM
    # (perl's alarm survives exec). The shell's message about a crash goes to the log.
    exec 2>> run.log
    status=0
    ( OFX_PLUGIN_PATH=$SMOKETEST_WORK/path/$bundle exec perl -e 'alarm shift; exec @ARGV or die "$!\n"' \
          "${SMOKETEST_TIMEOUT:-120}" "$HOSTDEMO" --no-std-paths --no-cache --quiet \
          -p "$id" -m "$major" -c "$ctx" -d "$depth" --format pfm -o out --report report.json \
          "${args[@]}" ) > run.log 2>&1 || status=$?
    [ "$status" != 142 ] || echo "timeout: killed after ${SMOKETEST_TIMEOUT:-120}s" >> run.log
    [ "$status" -lt 129 ] || echo "killed by signal $((status - 128))" >> run.log
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
    cmake -S "$top" -B "$build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release -DOFX_BUILD_HOSTDEMO=ON \
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
    "$work/ofxload" "$b/Contents/$arch/$name.ofx" |
        sed -n "s/^  \([^ ]*\) \([0-9]*\)\.[0-9]*$/$name \1 \2/p" >> "$work/plugins.txt"
done
if [ -n "$only" ]; then
    python3 -I -c 'import re, sys
for l in open(sys.argv[1]):
    if re.fullmatch(sys.argv[2], l.split()[1]):
        print(l, end="")' "$work/plugins.txt" "$only" > "$work/sel.txt"
    mv "$work/sel.txt" "$work/plugins.txt"
fi
[ -s "$work/plugins.txt" ] || { echo "no plug-in found in $plugins${only:+ matching $only}"; exit 1; }
echo "$(wc -l < "$work/plugins.txt" | tr -d ' ') plug-ins (identifier and major version) in $(ls "$work/path" | wc -l | tr -d ' ') bundles"

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

# Describe each plug-in (in parallel)
export HOSTDEMO SMOKETEST_WORK=$work
[ -z "$out" ] || export SMOKETEST_KEEP=1
# shellcheck disable=SC2016
tr '\n' '\0' < "$work/plugins.txt" | xargs -0 -n 1 -P "$jobs" bash -c '
    set -- $1
    d=$SMOKETEST_WORK/describe/$2-v$3
    mkdir -p "$d" && cd "$d"
    ( OFX_PLUGIN_PATH=$SMOKETEST_WORK/path/$1 exec "$HOSTDEMO" --no-std-paths --no-cache --quiet \
          -p "$2" -m "$3" --describe describe.json ) > describe.log 2>&1
    echo $? > status' _

# The render jobs, one per line: "name|bundle|id|major|context|depth|Clip=spec;..."
python3 -I - "$work" "$quick" "${#SKIP[@]}" "${SKIP[@]}" "${INPUTS[@]}" > "$work/jobs.txt" 2> "$work/describe-results.txt" << 'EOF' ||
import json, os, re, sys
work, quick, nskip = sys.argv[1], sys.argv[2] == "1", int(sys.argv[3])
skip = [s.split("|", 1) for s in sys.argv[4:4 + nskip]]
overrides = []
for s in sys.argv[4 + nskip:]:
    m = re.fullmatch(r"([^@|]*)(?:@(filter|general))?\|(.*)", s)
    if not m:
        sys.exit("bad INPUTS entry: " + s)
    overrides.append((m.group(1), m.group(2), m.group(3).replace("DATA", os.path.join(work, "data")).split()))
depths = {"OfxBitDepthByte": "byte", "OfxBitDepthShort": "short", "OfxBitDepthFloat": "float"}
for line in open(os.path.join(work, "plugins.txt")):
    bundle, pid, major = line.split()
    reason = next((r for rx, r in skip if re.fullmatch(rx, pid)), None)
    tag = "%s v%s" % (pid, major)
    if reason:
        print("SKIP|%s|%s|%s" % (tag, pid, reason), file=sys.stderr)
        continue
    d = os.path.join(work, "describe", "%s-v%s" % (pid, major))
    status = open(os.path.join(d, "status")).read().strip()
    try:
        desc = json.load(open(os.path.join(d, "describe.json")))
    except (OSError, ValueError):
        desc = None
    if status != "0" or not desc or not desc.get("ok"):
        log = open(os.path.join(d, "describe.log"), errors="replace").read().strip().replace("\n", " / ")
        print("FAIL|%s|%s|describe failed (exit status %s): %s" % (tag, pid, status, log), file=sys.stderr)
        continue
    contexts = desc["contexts"]
    generator = "OfxImageEffectContextGenerator" in contexts
    n = 0
    for ctx in ("filter", "general"):
        c = contexts.get("OfxImageEffectContext" + ctx.capitalize())
        if c is None or (quick and ctx == "filter" and "OfxImageEffectContextGeneral" in contexts):
            continue
        if not c.get("ok"):
            print("FAIL|%s|%s|describeInContext(%s) failed" % (tag, pid, ctx), file=sys.stderr)
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
        print("FAIL|%s|%s|no render: no filter or general context, or no supported depth (contexts: %s)"
              % (tag, pid, " ".join(contexts)), file=sys.stderr)
EOF
    { cat "$work/describe-results.txt"; echo "smoketest: listing the renders failed"; exit 1; }
nrenders=$(wc -l < "$work/jobs.txt" | tr -d ' ')
echo "$nrenders renders, $jobs at a time..."

# Render (in parallel)
tr '\n' '\0' < "$work/jobs.txt" | xargs -0 -n 1 -P "$jobs" "$0" --render-job

# Check the renders and print the results
status=0
python3 -I - "$work" "${SMOKETEST_DEBUG:-0}" "${XFAIL[@]}" << 'EOF' || status=$?
import json, os, re, sys
work, debug = sys.argv[1], sys.argv[2] == "1"
xfail = []
for x in sys.argv[3:]:
    m = re.fullmatch(r"([^:@/|]*)(?::([0-9]+))?(?:@(filter|general))?(?:/(byte|short|float))?\|([^|]*)\|(.*)", x)
    if not m:
        sys.exit("bad XFAIL entry: " + x)
    rx, major, ctx, depth, msg, desc = m.groups()
    if desc.startswith("debug:"):
        if not debug:
            continue
        desc = desc[6:]
    xfail.append((rx, major, ctx, depth, msg, desc))
order = ["ok", "SKIP", "XFAIL", "XPASS", "FAIL"]
plugins = {}  # plug-in (identifier and major version) -> worst result
nrender = dict((k, 0) for k in order)
notes = {"identity": set(), "empty RoD": set()}


def note(tag, res):
    if tag not in plugins or order.index(res) > order.index(plugins[tag]):
        plugins[tag] = res


def known(pid, major, ctx, depth):
    return next((x for x in xfail if re.fullmatch(x[0], pid) and x[1] in (None, major)
                 and x[2] in (None, ctx) and x[3] in (None, depth)), None)


def result(tag, name, errors, log, x):
    summary = "; ".join(errors[:4]) + (" (+%d)" % (len(errors) - 4) if len(errors) > 4 else "")
    if x:
        if not errors:
            return "XPASS", "known bug fixed? Update the XFAIL list of smoketest.sh: " + x[5]
        if re.search(x[4], summary + "\n" + log):
            return "XFAIL", x[5]
        return "FAIL", "%s (known bug, but not the expected message \"%s\": %s)" % (summary, x[4], x[5])
    return ("FAIL", summary) if errors else ("ok", "")


for line in open(os.path.join(work, "describe-results.txt")):
    kind, tag, pid, msg = line.rstrip("\n").split("|", 3)
    if kind == "SKIP":
        res = "SKIP"
    else:
        major = tag.rsplit(" v", 1)[1]
        x = known(pid, major, None, None)
        res, msg = result(tag, tag, [msg], "", x if x and x[2] is None and x[3] is None else None)
    print("%-5s %-50s %s" % (res, tag, msg))
    note(tag, res)
times = []
for line in open(os.path.join(work, "jobs.txt")):
    name, bundle, pid, major, ctx, depth, inputs = line.rstrip("\n").split("|")
    tag = "%s v%s" % (pid, major)
    d = os.path.join(work, "jobs", name)
    status, secs = open(os.path.join(d, "status")).read().split()
    times.append((int(secs), name))
    log = open(os.path.join(d, "run.log"), errors="replace").read()
    sizes = dict((l.split()[0], int(l.split()[1])) for l in open(os.path.join(d, "files.txt")))
    errors = []
    try:
        r = json.load(open(os.path.join(d, "report.json")))
    except (OSError, ValueError) as e:
        r = None
        errors.append("hostDemo exit status %s, no report (%s)" % (status, e.__class__.__name__))
    if r is not None:
        for a in r.get("actions") or []:
            if not a.get("ok"):
                errors.append("action %s failed (%s)" % (a.get("action"), a.get("status")))
        renders = r.get("renders") or []
        identity = any((k.get("isIdentity") or {}).get("identity") for k in renders)
        for m in r.get("messages") or []:
            # the debug builds of identity effects post "OFX Host should not render"
            # when hostDemo renders them anyway
            if identity and "should not render" in (m.get("text") or ""):
                continue
            if m.get("type") in ("OfxMessageError", "OfxMessageFatal"):
                errors.append("%s %s: %s" % (m.get("kind"), m.get("type"), m.get("text")))
        frames = r.get("frames") or [0, 1]
        views = max([k.get("view") or 0 for k in renders] + [0]) + 1
        stopped = False  # hostDemo stopped at an identity render
        for k in renders:
            o = k.get("output")
            at = "t=%s%s" % (k.get("time"), "" if views == 1 else " view %s" % k.get("view"))
            rod = (k.get("regionOfDefinition") or {})
            ident = (k.get("isIdentity") or {}).get("identity")
            if not o:
                b = rod.get("rod")
                if rod.get("ok") and b and (b[0] >= b[2] or b[1] >= b[3]) and not k.get("render"):
                    notes["empty RoD"].add(tag)
                    continue
                if ident and k.get("render") is not None:
                    # rendered anyway: the plug-in may not write the output (or fail)
                    notes["identity"].add(tag)
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
        if len(renders) != len(frames) * views and not stopped:
            errors.append("%d renders, expected %d" % (len(renders), len(frames) * views))
        if status != "0" and not stopped and not errors:
            errors.append("hostDemo exit status %s" % status)
    if status not in ("0", "1") and r is not None:
        errors.insert(0, "hostDemo exit status %s" % status)
    if status == "0" and r is not None and not r.get("ok"):
        errors.append("report: not ok")
    res, msg = result(tag, name, errors, log, known(pid, major, ctx, depth))
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
count = dict((k, sum(1 for v in plugins.values() if v == k)) for k in order)
print("smoketest: %d plug-ins: %d ok, %d skipped, %d with known bugs (XFAIL), %d failed, %d XPASS"
      % (len(plugins), count["ok"], count["SKIP"], count["XFAIL"], count["FAIL"], count["XPASS"]))
print("smoketest: %d renders: %d ok, %d XFAIL, %d XPASS, %d FAIL"
      % (len(times), nrender["ok"], nrender["XFAIL"], nrender["XPASS"], nrender["FAIL"]))
bad = sorted(t for t, v in plugins.items() if v in ("FAIL", "XPASS"))
if bad:
    print("failed: " + ", ".join(bad))
    sys.exit(1)
EOF
if [ -n "$out" ]; then
    mkdir -p "$out"
    rm -rf "$out/jobs" "$out/describe"
    mv "$work/jobs" "$work/describe" "$out/"
    cp "$work/plugins.txt" "$work/jobs.txt" "$out/"
fi
echo "smoketest: $(( $(date +%s) - start ))s"
exit $status
