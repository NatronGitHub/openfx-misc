#!/bin/bash
# Headless render smoke test used by the CI: build hostDemo (the OFX test host
# of the openfx submodule) and render a representative set of plugins (all the
# generators and some common filters) at the byte, short and float depths.
# Linux and macOS only (the checks need python3).
#
# Usage (from the top of the source tree): .github/tools/rendertest.sh [DIR...]
# The plugins are the *.ofx.bundle found under the DIRs, which should be the
# build directories of one configuration, e.g. Misc/Linux-64-release
# CImg/Linux-64-release. Without DIRs, the *-$CONFIG directories of the source
# tree are used (CONFIG=release by default). A bundle found twice is an error.
# hostDemo is $HOSTDEMO if set, else DIR/bin/hostDemo (installed by openfx-misc's
# CMake with -DOFX_BUILD_HOSTDEMO=ON), else it is built with openfx's CMake out of
# the openfx tree, in a temporary directory (or in $HOSTDEMO_BUILD_DIR, kept
# between runs if set), so that the openfx submodule stays clean.
#
# hostDemo renders the frames t=0 and t=1 (or --frames), both views of a stereo
# frame (hostDemo is built with the OFX_EXTENSIONS_* of the plugins), of a PAL SD project
# (720x576), at render scale 1 (or --scale), in the whole RoD (or --window), on
# synthetic inputs (-i Clip=solid:r,g,b,a|ramp|checker:N|none), with the
# depth and components that the clip preferences of the plugin map each
# clip to. A render fails if:
# - hostDemo fails (exit status not 0): an action failed, an image fetch
#   failed, a float output pixel was not written (hostDemo fills the float
#   output images with NaN), or the plugin crashed;
# - the --report JSON file shows a failed render, an output depth other than
#   the requested one, or an error or fatal message from the plugin;
# - the output at the first time does not have the expected mean value (mean=r,g,b,a,
#   normalized to [0,1]), does not have the expected components (comps=RGBA,
#   RGB or Alpha), is constant though it should not be (nonconst), or
#   is not the same region of the full render (tile: for a --window render),
#   renders the same image at different times (framesdiffer: --frames), or
#   is the same image as an input (changed:Clip: the CRC32 of the output
#   must differ from the one of the input Clip, copied at the same depth by
#   Invert with no channel to process; NoOp would be simpler, but its debug
#   builds fail when rendered, since it is always an identity).
# - the parameters after the run (the results of an analysis: -s button presses
#   a push button) do not have the expected value at t=0 (value:NAME=v,...),
#   number of keys (keys:NAME=N) or first and last key times
#   (keyrange:NAME=first,last).
# The output images are not compared to reference images.
# Known plugin bugs are expected failures (XFAIL), with the message that their
# failure must show; a crash of an XFAIL render (e.g. a segmentation fault)
# is expected, and its message is in the log of that render only.
set -eo pipefail

case "$(uname -s)" in
    Darwin|Linux) ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

top=$PWD
start=$(date +%s)
work=$(mktemp -d "${TMPDIR:-/tmp}/rendertest.XXXXXX")
trap 'rm -rf "$work"' EXIT

# The plugin path: the directories that hold our bundles, and nothing else
# (hostDemo --no-std-paths). Skip the openfx submodule (not our plugins),
# DebugProxy (debug builds only, it wraps the plugin named by
# $OFX_DEBUGPROXY_BINARY), Test (for developers) and Shadertoy (it only renders
# with OpenGL, which hostDemo does not provide). The prunes match names, so
# that they also work with explicit DIRs.
if [ $# -eq 0 ]; then
    filter="*-${CONFIG:-release}"
    set -- .
else
    filter="*"
fi
bundles=$(cd "$top" && find "$@" \( -name openfx -o -name DebugProxy \
              -o -name Test.ofx.bundle -o -name Shadertoy.ofx.bundle \) -prune -o \
              -name '*.ofx.bundle' -path "$filter/*.ofx.bundle" -print -prune | sort)
dups=$(echo "$bundles" | sed 's,.*/,,' | sort | uniq -d)
if [ -n "$dups" ]; then
    echo "bundles found more than once (give the build directories of one configuration):"
    echo "$bundles" | grep -F "$dups"
    exit 1
fi
dirs=()
while IFS= read -r d; do
    [ -n "$d" ] || continue
    case "$d" in /*) ;; *) d=$top/${d#./} ;; esac
    dirs+=("$d")
done < <(echo "$bundles" | sed 's,/[^/]*$,,' | sort -u)
if [ ${#dirs[@]} -eq 0 ]; then
    echo "no *.ofx.bundle found in $* ($filter)"
    exit 1
fi
OFX_PLUGIN_PATH=$(IFS=:; echo "${dirs[*]}")
export OFX_PLUGIN_PATH
echo "OFX_PLUGIN_PATH=$OFX_PLUGIN_PATH"
# debug builds (*-debug directories, or RENDERTEST_DEBUG=1, e.g. for a CMake install
# tree) have the plugins' assertions
case ":$OFX_PLUGIN_PATH:" in *-debug:*) debug=1 ;; *) debug=0 ;; esac
[ -z "$RENDERTEST_DEBUG" ] || debug=$RENDERTEST_DEBUG

# hostDemo: $HOSTDEMO, else DIR/bin/hostDemo (a CMake install tree of openfx-misc configured
# with -DOFX_BUILD_HOSTDEMO=ON), else built here with openfx's CMake (BUILD_EXAMPLE_HOSTS;
# HostSupport needs the system expat).
if [ -z "$HOSTDEMO" ]; then
    for d in "$@"; do
        if [ -x "$d/bin/hostDemo" ]; then
            HOSTDEMO=$d/bin/hostDemo
            break
        fi
    done
fi
if [ -n "$HOSTDEMO" ]; then
    # absolute, since the renders run in $work
    HOSTDEMO=$(cd "$(dirname "$HOSTDEMO")" && pwd)/$(basename "$HOSTDEMO")
else
    # Build hostDemo with openfx's CMake, out of the openfx tree (so that the submodule stays
    # clean), with the OFX_EXTENSIONS_* that openfx-misc uses (openfx's defaults), as
    # -DOFX_BUILD_HOSTDEMO=ON does: it renders the two views of a stereo frame.
    b=${HOSTDEMO_BUILD_DIR:-$work/hostdemo}
    mkdir -p "$b"
    b=$(cd "$b" && pwd)
    jobs=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)
    log=$b/build.log
    echo "Building hostDemo in $b..."
    {
        cmake -S "$top/openfx" -B "$b" -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLE_HOSTS=ON &&
            cmake --build "$b" --target hostDemo --parallel "$jobs"
    } > "$log" 2>&1 || { tail -n 50 "$log"; echo "hostDemo build failed"; exit 1; }
    HOSTDEMO=$b/HostSupport/examples/hostDemo
    echo "hostDemo built in $(( $(date +%s) - start ))s"
fi
[ -x "$HOSTDEMO" ] || { echo "hostDemo not found: $HOSTDEMO"; exit 1; }

cd "$work"

# The input files (little-endian PFM, bottom row first): autocrop.pfm is a 64x48
# PF (RGB, so opaque), black except a white 32x16 rectangle at (10,8).
python3 -I - << 'EOF2'
import struct
w, h = 64, 48
pix = []
for y in range(h):
    for x in range(w):
        pix += [1. if 10 <= x < 42 and 8 <= y < 24 else 0.] * 3
with open("autocrop.pfm", "wb") as f:
    f.write(b"PF\n%d %d\n-1.0\n" % (w, h) + struct.pack("<%df" % len(pix), *pix))
EOF2

# check REPORT DEPTH EXPECT [CLIP=CRC32...]: check the --report JSON file of a
# hostDemo run (format: openfx/HostSupport/examples/python/README.md). EXPECT
# is one or more expectations separated by "+"; changed:Clip needs the CRC32
# of the input Clip. Prints a short description of the output at t=0, then the
# reasons of the failure, if any (exit status 1).
check() {
    python3 -I - "$@" << 'EOF'
import array, json, sys
path, depth, expects = sys.argv[1:4]
refs = dict(a.split("=", 1) for a in sys.argv[4:])
with open(path) as f:
    r = json.load(f)
errors = []
if not r.get("ok") or r.get("exitStatus") != 0:
    errors.append("hostDemo failed (exit status %s)" % r.get("exitStatus"))
for m in r.get("messages") or []:
    if m.get("type") in ("OfxMessageError", "OfxMessageFatal"):
        errors.append("%s %s: %s" % (m.get("kind"), m.get("type"), m.get("text")))
renders = r.get("renders") or []
nframes = int(refs.get("frames", 2))
# hostDemo is built with the OFX_EXTENSIONS_* of the plugins (by -DOFX_BUILD_HOSTDEMO=ON,
# or above with openfx's defaults): it always renders views 0 and 1 of each frame (the
# report has no view count). The expectations are checked on view 0.
nviews = 2
views = [k.get("view") or 0 for k in renders]
if len(renders) != nframes * nviews or views != list(range(nviews)) * nframes:
    errors.append("%d renders (views %s), expected %d frames of %d views" % (len(renders), views, nframes, nviews))
out0 = None
for k in renders:
    o = k.get("output")
    if not (k.get("render") or {}).get("ok") or not o:
        errors.append("t=%s: render failed: %s" % (k.get("time"), k.get("render")))
        continue
    if o["depth"] != depth:
        errors.append("t=%s: output depth %s, expected %s" % (k.get("time"), o["depth"], depth))
    if o["nan"]:
        errors.append("t=%s: %d unwritten (NaN) pixels" % (k.get("time"), o["nan"]))
    if k is renders[0]:
        out0 = o
renders = [k for k in renders if not k.get("view")]
desc = ""
if out0:
    scale = {"byte": 255., "short": 65535., "float": 1.}[out0["depth"]]
    mean = [v / scale for v in out0["mean"]]
    desc = "%s %s mean=%s" % (out0["depth"], out0["comps"], ",".join("%.3g" % v for v in mean))
    for expect in expects.split("+"):
        if expect == "nonconst":
            if out0["min"] == out0["max"]:
                errors.append("t=0: constant output, expected an image")
        elif expect.startswith("mean="):
            want = [float(v) for v in expect[5:].split(",")]
            # 0.01: byte and short inputs and outputs are quantized
            if len(want) != len(mean) or any(abs(a - b) > 0.01 for a, b in zip(mean, want)):
                errors.append("t=0: mean %s, expected %s" % (",".join("%.4g" % v for v in mean), expect[5:]))
        elif expect.startswith("comps="):
            if out0["comps"] != expect[6:]:
                errors.append("t=0: components %s, expected %s" % (out0["comps"], expect[6:]))
        elif expect == "framesdiffer":
            crcs = [(k.get("output") or {}).get("crc32") for k in renders]
            if len(set(crcs)) != len(crcs):
                errors.append("the same image at different times: %s" % crcs)
        elif expect == "tile":
            # the tile (render window) must be the same region of the full
            # render (float RGB PFM files, bottom row first)
            with open(refs["full"]) as f:
                full = json.load(f)["renders"][0]["output"]
            def pfm(path):
                with open(path, "rb") as f:
                    h = f.read().split(b"\n", 3)
                w, hh = (int(v) for v in h[1].split())
                a = array.array("f")
                a.frombytes(h[3][:w * hh * 3 * 4])
                if sys.byteorder != "little":
                    a.byteswap()
                return w, a
            tw, tile = pfm(out0["files"][0])
            fw, fpix = pfm(full["files"][0])
            tb, fb = out0["bounds"], full["bounds"]
            diff = 0
            for y in range(tb[1], tb[3]):
                for x in range(tb[0], tb[2]):
                    i = ((y - tb[1]) * tw + x - tb[0]) * 3
                    j = ((y - fb[1]) * fw + x - fb[0]) * 3
                    if tile[i:i + 3] != fpix[j:j + 3]:
                        diff += 1
            if diff:
                errors.append("%d pixels of the tile %s differ from the full render" % (diff, tb))
            desc += " tile=%s" % ",".join(str(v) for v in tb)
        elif expect.startswith(("value:", "keys:", "keyrange:")):
            # the parameters after the run (analysis results): value:NAME=v,... (the
            # value at t=0; with keys, also the one of the t=0 render), keys:NAME=N (the
            # number of keys), keyrange:NAME=first,last (the first and last key times)
            kind, rest = expect.split(":", 1)
            pname, want = rest.split("=", 1)
            p = (r.get("params") or {}).get(pname)
            if p is None:
                errors.append("no parameter " + pname)
                continue
            keys = p.get("keys") or []
            if kind == "value":
                want = [float(v) for v in want.split(",")]
                def close(got):
                    # got: a number, a list, or None (no value, or NaN)
                    got = got if isinstance(got, list) else [got]
                    return (len(got) == len(want) and
                            all(a is not None and abs(a - b) <= 1e-4 * max(1., abs(b)) for a, b in zip(got, want)))
                if not close(p.get("value")):
                    errors.append("%s = %s, expected %s" % (pname, p.get("value"), rest.split("=", 1)[1]))
                k0 = [k for k in renders if k.get("time") == 0]
                if keys and k0 and not close((k0[0].get("params") or {}).get(pname)):
                    errors.append("t=0 render: %s = %s, expected %s" % (pname, (k0[0].get("params") or {}).get(pname),
                                                                       rest.split("=", 1)[1]))
                desc += " %s=%s" % (pname, ",".join("%.6g" % v for v in want))
            elif kind == "keys":
                if len(keys) != int(want):
                    errors.append("%s: %d keys, expected %s" % (pname, len(keys), want))
                desc += " %s:%dkeys" % (pname, len(keys))
            else:
                first, last = (float(v) for v in want.split(","))
                if not keys or keys[0][0] != first or keys[-1][0] != last:
                    errors.append("%s: keys at %s, expected %s..%s" % (pname, [k[0] for k in keys], first, last))
                desc += " %s:keys%s" % (pname, "%g..%g" % (keys[0][0], keys[-1][0]) if keys else "=none")
        elif expect.startswith("changed:"):
            clip = expect[8:]
            if not refs.get(clip):
                errors.append("no CRC32 for the input " + clip)
            elif out0["crc32"] == refs[clip]:
                errors.append("t=0: the output is the %s input (CRC32 %s)" % (clip, refs[clip]))
        else:
            errors.append("unknown expectation " + expect)
elif not errors:
    errors.append("no output at t=0")
print(desc)
for e in errors:
    print("  " + e)
sys.exit(1 if errors else 0)
EOF
}

# crc32 REPORT: the CRC32 of the output at t=0 in the --report JSON file
crc32() {
    python3 -I -c 'import json, sys
r = json.load(open(sys.argv[1]))
print([k for k in r["renders"] if k["time"] == 0 and not k.get("view")][0]["output"]["crc32"])' "$1"
}

# hostdemo LOG ARGS...: run hostDemo with its output in LOG. The subshell
# keeps the shell's message about a crash (e.g. "Segmentation fault") in LOG.
hostdemo() {
    local log=$1
    shift
    ( "$HOSTDEMO" --no-std-paths --no-cache --quiet "$@"; exit $? ) > "$log" 2>&1
}

# The renders, one per line: "label|plugin id|context|depths|expected output|
# known bug|hostDemo arguments" (no quotes in the arguments: they are split
# into words). depths: * for byte, short and float. The plugins all support
# the three depths, except the CImg plugins (float only: the other depths are
# mapped to float, which the float render already tests).
# Generators are rendered in the general context with their optional Source
# clip disconnected (hostDemo has no generator context). Optional Mask clips
# are disconnected, except where a mask is tested.
# Parameters are set in canonical coordinates (the normalized coordinate
# system only applies to their defaults).
# Filters that could pass by copying an input are checked with changed:Clip
# (or with mean=).
# A known bug is "description~message": the render is expected to fail with
# the extended regular expression "message" in its log or in the check
# result. It is then reported as XFAIL and does not fail the test. If it
# fails otherwise, it is a FAIL; if it passes (XPASS), the test fails too, so
# that the entry is updated when the bug is fixed. A "debug:" known bug (a
# failed assertion) is only expected in debug builds.
tests=(
    # generators. The byte and short outputs of Constant are sRGB and Rec.709:
    # 1 and 0 are the same in all three.
    "Constant|net.sf.openfx.ConstantPlugin|general|*|mean=1,0,1,0.5||-i Source=none color=1,0,1,0.5"
    "Constant float|net.sf.openfx.ConstantPlugin|general|float|mean=0.25,0.5,0.75,1||-i Source=none color=0.25,0.5,0.75,1"
    "CheckerBoard|net.sf.openfx.CheckerBoardPlugin|general|*|nonconst||-i Source=none"
    "ColorBars|net.sf.openfx.ColorBars|general|*|nonconst||-i Source=none"
    "ColorWheel|net.sf.openfx.ColorWheel|general|*|nonconst||-i Source=none"
    "Rand|net.sf.openfx.Noise|general|*|nonconst||-i Source=none"
    "Rectangle|net.sf.openfx.Rectangle|general|*|nonconst||-i Source=none -i Mask=none bottomLeft=100,100 size=300,200"
    "Radial|net.sf.openfx.Radial|general|*|nonconst||-i Source=none -i Mask=none"
    "Ramp|net.sf.openfx.Ramp|general|*|nonconst||-i Source=none -i Mask=none"
    "DiscBlades|net.sf.openfx.DiscBlades|general|*|nonconst||-i Source=none"
    # filters. Invert also inverts alpha by default.
    "Invert solid|net.sf.openfx.Invert|filter|*|mean=0.75,0.5,0,0||-i Source=solid:0.25,0.5,1,1 -i Mask=none"
    "Invert checker|net.sf.openfx.Invert|filter|*|nonconst+changed:Source||-i Source=checker:16 -i Mask=none"
    "Grade solid|net.sf.openfx.GradePlugin|filter|*|mean=0.5,1,0.25,1||-i Source=solid:0.25,0.5,0.125,1 -i Mask=none multiply=2,2,2,2"
    "Grade ramp masked|net.sf.openfx.GradePlugin|filter|*|nonconst+changed:Source||-i Source=ramp -i Mask=checker:32 gamma=2,2,2,2"
    "Merge over solid|net.sf.openfx.MergePlugin|filter|*|mean=0.5,0,0.5,1||-i A=solid:0.5,0,0,0.5 -i B=solid:0,0,1,1 -i Mask=none"
    "Merge over checker|net.sf.openfx.MergePlugin|filter|*|nonconst+changed:A+changed:B||-i A=checker:16 -i B=ramp -i Mask=none"
    "CImgBlur|net.sf.cimg.CImgBlur|filter|float|nonconst+changed:Source||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0"
    "CImgBlur expandRoD|net.sf.cimg.CImgBlur|filter|float|nonconst+changed:Source||-i Source=checker:8 -i Mask=none size=5,5"
    "Transform|net.sf.openfx.TransformPlugin|filter|*|nonconst+changed:Source||-i Source=checker:32 rotate=30 scale=0.8,0.8"
    "Crop|net.sf.openfx.CropPlugin|filter|*|nonconst+changed:Source||-i Source=ramp bottomLeft=100,100 size=400,300"
    # saturation 0: the Rec.709 luminance
    "ColorCorrect|net.sf.openfx.ColorCorrectPlugin|filter|*|mean=0.465,0.465,0.465,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none MasterSaturation=0,0,0,0"
    "Clamp|net.sf.openfx.Clamp|filter|*|mean=1,0,0.5,1||-i Source=solid:1.5,-0.5,0.5,1 -i Mask=none"
    # degenerate inputs that used to give NaN (or inf) pixels, which check() rejects. Grade:
    # gamma 0 maps values above 1 to FLT_MAX (was +inf), premultiplied by alpha 0 (was NaN).
    "Grade gamma 0 alpha 0|net.sf.openfx.GradePlugin|filter|*|mean=0,0,0,0||-i Source=solid:2,0.5,1,0 -i Mask=none gamma=0,0,0,0 premult=1"
    # ColorCorrect: gamma 0 in the shadows (was pow(2, 1/0) = inf), whose weight is 0 at luminance 2 (was 0 * inf = NaN)
    "ColorCorrect gamma 0|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=2,2,2,1||-i Source=solid:2,2,2,1 -i Mask=none ShadowsGamma=0,0,0,0"
    # PLogLin: the undefined conversions pass the channels through (were 1/0 and log of inf)
    "PLogLin nGamma 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none nGamma=0,0,0"
    "PLogLin linRef 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none operation=1 linRef=0,0,0"
    "PLogLin density 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none operation=1 density=0,0,0"
    # Log2Lin: black and white so close that the offset rounds to 1 (was an infinite gain)
    "Log2Lin black ~ white|net.sf.openfx.Log2Lin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none black=0,0,0 white=1e-15,1e-15,1e-15"
    # PIK: a soft screen replace color with zero Rec.709 luminance (was 0/0 = NaN where alpha
    # is raised to 1 by Clip White). Exactly zero only without FMA contraction (e.g. x86-64).
    "PIK replace color lum 0|net.sf.openfx.PIK|general|float|comps=RGBA||-i Fg=solid:0,0,0.25,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0,0,1 screenClipMax=0.5 screenReplaceColor=0.072175,0,-0.2126729"
    # render scale 0.5 (the means are those of the full-scale renders), tiles
    # (--window: the same pixels as the full render), and a fractional time
    "Radial scale 0.5|net.sf.openfx.Radial|general|*|mean=0.231,0.231,0.231,0.231||-i Source=none -i Mask=none --scale 0.5"
    "Transform scale 0.5|net.sf.openfx.TransformPlugin|filter|float|mean=0.31,0.31,0.31,0.31||-i Source=checker:32 rotate=30 scale=0.8,0.8 --scale 0.5"
    "CImgBlur scale 0.5|net.sf.cimg.CImgBlur|filter|float|mean=0.497,0.497,0.497,0.497||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 --scale 0.5"
    # filter=2: Box (FIR). The default IIR filters have an infinite support, so
    # their tiles differ slightly (up to 0.003) from the full render.
    "CImgBlur tile|net.sf.cimg.CImgBlur|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 filter=2 --frames 0 --window 100,100,300,250"
    "Merge tile|net.sf.openfx.MergePlugin|filter|float|nonconst+tile||-i A=checker:16 -i B=ramp -i Mask=none --frames 0 --window 50,60,250,200"
    "Rand t=0,0.5|net.sf.openfx.Noise|general|float|nonconst+framesdiffer||-i Source=none --frames 0,0.5"
    # Shuffle (version 4 unless -m) swaps R and B (outputR=8: B.b, outputB=6:
    # B.r; 0-3: A.r-A.a, 6-9: B.r-B.a). After the first test, the A and B
    # inputs differ, so that reading A for B (or B for A) fails.
    "Shuffle|net.sf.openfx.ShufflePlugin|general|*|mean=0.75,0.5,0.25,1||-i A=solid:0.25,0.5,0.75,1 -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle B|net.sf.openfx.ShufflePlugin|general|float|mean=0.75,0.5,0.25,1||-i A=ramp -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    # the default: the B input (identity)
    "Shuffle default|net.sf.openfx.ShufflePlugin|general|*|mean=0.5,0.6,0.7,0.8||-i A=solid:0.1,0.2,0.3,0.4 -i B=solid:0.5,0.6,0.7,0.8"
    "Shuffle A and B|net.sf.openfx.ShufflePlugin|general|*|mean=0.3,0.6,0.1,0.8||-i A=solid:0.1,0.2,0.3,0.4 -i B=solid:0.5,0.6,0.7,0.8 outputR=2 outputG=7 outputB=0 outputA=9"
    # LayerRename: hostDemo has no multiplane suite, so it is an identity
    # (the planes themselves can only be tested in Natron)
    "LayerRename|net.sf.openfx.LayerRename|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 find=scene. renameColor=1"
    # B (the default input) disconnected: A gives the depth of the images
    "Shuffle B none|net.sf.openfx.ShufflePlugin|general|*|mean=0.3,0.2,0.1,0.4||-i A=solid:0.1,0.2,0.3,0.4 -i B=none outputR=2 outputG=1 outputB=0 outputA=3"
    # Shuffle 3 swaps A and B on hosts without the multiplane suites: kept, so
    # that version 3 projects render the same
    "Shuffle 3 A and B|net.sf.openfx.ShufflePlugin|general|*|mean=0.7,0.2,0.5,0.4||-m 3 -i A=solid:0.1,0.2,0.3,0.4 -i B=solid:0.5,0.6,0.7,0.8 outputR=2 outputG=7 outputB=0 outputA=9"
    "Shuffle 3 B none|net.sf.openfx.ShufflePlugin|general|*|mean=0,0,0,0||-m 3 -i A=solid:0.1,0.2,0.3,0.4 -i B=none outputR=2 outputG=1 outputB=0 outputA=3"
    "Shuffle 2 A and B|net.sf.openfx.ShufflePlugin|general|*|mean=0.3,0.6,0.1,0.8||-m 2 -i A=solid:0.1,0.2,0.3,0.4 -i B=solid:0.5,0.6,0.7,0.8 outputR=2 outputG=7 outputB=0 outputA=9"
    "Shuffle 3 default|net.sf.openfx.ShufflePlugin|general|float|mean=0.1,0.2,0.3,0.4||-m 3 -i A=solid:0.1,0.2,0.3,0.4 -i B=solid:0.5,0.6,0.7,0.8"
    # the filter context: the A and B channels both read Source (so these tests
    # cannot detect an A/B swap). The Output components are those of Source
    # (RGBA in hostDemo), whatever outputComponents (1: RGB).
    "Shuffle filter|net.sf.openfx.ShufflePlugin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1"
    "Shuffle filter swap|net.sf.openfx.ShufflePlugin|filter|*|mean=0.75,0.5,0.25,1||-i Source=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle 3 filter|net.sf.openfx.ShufflePlugin|filter|float|mean=0.75,0.5,0.25,1||-m 3 -i Source=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle 2 filter|net.sf.openfx.ShufflePlugin|filter|float|mean=0.75,0.5,0.25,1||-m 2 -i Source=solid:0.25,0.5,0.75,1 outputR=2 outputB=0"
    "Shuffle filter comps|net.sf.openfx.ShufflePlugin|filter|*|comps=RGBA+mean=0.75,0.5,0.25,1||-i Source=solid:0.25,0.5,0.75,1 outputComponents=1 outputR=8 outputB=6"
    # ImageStatistics: the analysis buttons (pressed with -s, instanceChanged with
    # kOfxChangeUserEdited) set keys at the analyzed frames; the output is the input.
    # hostDemo's input frame range is 0..25. HSVL: hue in degrees, L=(min+max)/2. The ramp is (x+0.5)/720.
    "ImageStatistics frame|net.sf.openfx.ImageStatistics|filter|float|mean=0.25,0.5,0.75,1+value:statMean=0.25,0.5,0.75,1+keys:statMean=1+keys:statHSVLMean=0+value:autoCropMode=0||-i Source=solid:0.25,0.5,0.75,1 -s analyzeFrame"
    "ImageStatistics frame t=1|net.sf.openfx.ImageStatistics|filter|float|value:statMax=0.999306,0.999306,0.999306,0.999306+keys:statMax=1+keyrange:statMax=1,1||-i Source=ramp -s analyzeFrame@1"
    "ImageStatistics HSVL|net.sf.openfx.ImageStatistics|filter|float|value:statHSVLMean=210,0.666667,0.75,0.5+keys:statHSVLMean=1+keys:statMean=0||-i Source=solid:0.25,0.5,0.75,1 -s analyzeFrameHSVL"
    "ImageStatistics sequence|net.sf.openfx.ImageStatistics|filter|float|keys:statMean=26+keyrange:statMean=0,25||--frames 0,1,2 -i Source=ramp -s analyzeSequence"
    # Custom with Frame Range 1-1 (its default) starts from the input range
    "ImageStatistics custom|net.sf.openfx.ImageStatistics|filter|float|value:frameRange=0,25||-i Source=ramp sequenceRange=Custom"
    "ImageStatistics custom 5-7|net.sf.openfx.ImageStatistics|filter|float|keys:statMean=3+keyrange:statMean=5,7||-i Source=ramp frameRange=5,7 sequenceRange=Custom -s analyzeSequence"
    # the box of the non-black pixels, in canonical coordinates: the file pixels
    # (110,58)-(142,74) at the project PAR 768/720 (x * 16/15). RGBA: the
    # pixels are opaque, so the box is the whole input (100,50)-(164,98).
    "ImageStatistics auto crop|net.sf.openfx.ImageStatistics|filter|float|value:autoCropBottomLeft=117.333333,58+value:autoCropSize=34.133333,16+keys:autoCropSize=1||-i Source=file:autocrop.pfm@100,50 -s analyzeFrameAutoCrop"
    "ImageStatistics auto crop RGBA|net.sf.openfx.ImageStatistics|filter|float|value:autoCropBottomLeft=106.666667,50+value:autoCropSize=68.266667,48||-i Source=file:autocrop.pfm@100,50 autoCropMode=RGBA -s analyzeFrameAutoCrop"
    "ImageStatistics auto crop seq|net.sf.openfx.ImageStatistics|filter|float|value:autoCropSize=34.133333,16+keys:autoCropBottomLeft=26+keys:autoCropSize=26+keyrange:autoCropSize=0,25||-i Source=file:autocrop.pfm@100,50 -s analyzeSequenceAutoCrop"
)

n=0 failed=() xfailed=()
for t in "${tests[@]}"; do
    IFS='|' read -r label id ctx depths expect bug args <<< "$t"
    [ "$depths" != '*' ] || depths="byte short float"
    case "$bug" in debug:*) [ $debug = 1 ] && bug=${bug#debug:} || bug= ;; esac
    bugmsg=
    if [ -n "$bug" ]; then
        bugmsg=${bug##*~}
        bug=${bug%~*}
    fi
    for depth in $depths; do
        n=$((n + 1))
        name="$label ($depth)"
        # the hostDemo options: the number of frames, the render scale (for the
        # copies of the inputs), and the arguments without the render window
        # (for the full render that a tile is compared to)
        # shellcheck disable=SC2086
        set -- $args
        frames=2 scale=() nowindow=()
        while [ $# -gt 0 ]; do
            case "$1" in
                --frames) f=${2//[^,]/}; frames=$(( ${#f} + 1 )) ;;
                --scale) scale=(--scale "$2") ;;
            esac
            if [ "$1" = --window ]; then shift; else nowindow+=("$1"); fi
            shift
        done
        refs=("frames=$frames") refok=1
        for e in ${expect//+/ }; do
            case "$e" in
                changed:*)
                    # the CRC32 of the input Clip, i.e. of a copy of it
                    clip=${e#changed:} spec=
                    # shellcheck disable=SC2086
                    set -- $args
                    while [ $# -gt 0 ]; do
                        [ "$1" = -i ] && case "$2" in "$clip="*) spec=${2#*=} ;; esac
                        shift
                    done
                    rm -f out.* ref.json
                    if [ -n "$spec" ] && hostdemo ref.log -p net.sf.openfx.Invert -c filter -d "$depth" \
                           --format pfm -o out --report ref.json -i "Source=$spec" -i Mask=none "${scale[@]}" \
                           NatronOfxParamProcessR=0 NatronOfxParamProcessG=0 NatronOfxParamProcessB=0 NatronOfxParamProcessA=0; then
                        refs+=("$clip=$(crc32 ref.json)")
                    else
                        refok=0
                        printf 'FAIL  %-26s could not copy the %s input (%s) with Invert\n' "$name" "$clip" "$spec"
                        sed 's/^/  | /' ref.log 2> /dev/null || true
                    fi
                    ;;
                tile)
                    # the full render, without --window
                    rm -f full.* full.json
                    if hostdemo ref.log -p "$id" -c "$ctx" -d "$depth" --format pfm -o full \
                           --report full.json "${nowindow[@]}"; then
                        refs+=("full=full.json")
                    else
                        refok=0
                        printf 'FAIL  %-26s the full render (without --window) failed\n' "$name"
                        sed 's/^/  | /' ref.log
                    fi
                    ;;
            esac
        done
        if [ $refok = 0 ]; then
            failed+=("$name")
            continue
        fi
        rm -f out.* report.json
        # $args is split into words on purpose
        # shellcheck disable=SC2086
        if hostdemo run.log -p "$id" -c "$ctx" -d "$depth" --format pfm -o out --report report.json $args; then
            result=$(check report.json "$depth" "$expect" "${refs[@]}") && ok=1 || ok=0
        else
            status=$?
            ok=0
            result="hostDemo exit status $status"
            [ ! -f report.json ] || result="$result: $(check report.json "$depth" "$expect" "${refs[@]}" || true)"
        fi
        if [ -n "$bug" ]; then
            if [ $ok = 1 ]; then
                printf 'XPASS %-26s %s\n' "$name" "$result"
                echo "  known bug \"$bug\" fixed? Update the test list of $0."
                failed+=("$name (XPASS)")
                continue
            elif { echo "$result"; cat run.log; } | grep -E -e "$bugmsg" > /dev/null; then
                printf 'XFAIL %-26s %s\n' "$name" "$bug"
                xfailed+=("$name")
                continue
            fi
            result="$result (known bug \"$bug\", but not the expected message \"$bugmsg\")"
        fi
        if [ $ok = 1 ]; then
            printf 'ok    %-26s %s\n' "$name" "$result"
        else
            printf 'FAIL  %-26s %s\n' "$name" "$result"
            sed 's/^/  | /' run.log
            echo "  | hostDemo -p $id -c $ctx -d $depth $args"
            failed+=("$name")
        fi
    done
done
rm -f out.* full.* report.json ref.json autocrop.pfm

echo
echo "rendertest: $((n - ${#failed[@]} - ${#xfailed[@]})) passed, ${#xfailed[@]} known bugs (XFAIL), ${#failed[@]} failed, $n renders in $(( $(date +%s) - start ))s"
if [ ${#failed[@]} -ne 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
