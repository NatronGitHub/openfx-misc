#!/bin/bash
# Headless render smoke test used by the CI: build hostDemo (the OFX test host
# of the openfx submodule) and render a representative set of plugins (all the
# generators and some common filters) at the byte, short and float depths.
# Linux, macOS and Windows (MSYS2, e.g. UCRT64, with its python and a hostDemo from
# -DOFX_BUILD_HOSTDEMO=ON, which needs its expat). The checks need python3.
#
# Usage (from the top of the source tree): .github/tools/rendertest.sh [DIR...]
# RENDERTEST_ONLY=ERE: only the renders whose label (the first field of the test
# list below) matches the extended regular expression ERE, e.g. ^CImgConvolve.
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
#   normalized to [0,1], within 0.01 or within the tolerance given by tol=),
#   the effect is not an identity at all times though it should be (identity:
#   the isIdentity action), is an identity at some time though its render changes
#   the input (notidentity: a host would pass the input through), does not have
#   the expected components (comps=RGBA, RGB or Alpha) or output premultiplication
#   (premult=Opaque, PreMultiplied or UnPreMultiplied: the clip preferences), is
#   constant though it should not be (nonconst), or
#   is not the same region of the full render (tile: for a --window render),
#   renders the same image at different times (framesdiffer: --frames), has a time
#   domain (the getTimeDomain action, general context) other than the expected one
#   (timedomain=min,max, or timedomain=default if the plugin lets the host use its default),
#   does not have the expected maximum value (max=r,g,b,a, as mean=), e.g. the peak
#   of a blurred impulse, which gives the blur size, or
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
    Darwin|Linux) windows=0 ;;
    *_NT-*) windows=1 ;; # MSYS2: MINGW64_NT-*, UCRT64_NT-*, CLANG64_NT-*...
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
# Windows: a native list (C:\...;D:\...), for the native hostDemo
[ $windows = 0 ] || OFX_PLUGIN_PATH=$(cygpath -w -p "$OFX_PLUGIN_PATH")
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
        elif [ -x "$d/bin/hostDemo.exe" ]; then
            HOSTDEMO=$d/bin/hostDemo.exe
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
# convsrc.pfm: a 720x576 PF (not a power of two) with pseudo-random values in [0,1), different in
# each channel; convk.pfm: a 7x5 PF kernel, positive, not symmetric, different in each channel
# (for the "conv" check: an exact linear convolution)
def pfm(name, w, h, val):
    pix = [val(x, y, c) for y in range(h) for x in range(w) for c in range(3)]
    with open(name, "wb") as f:
        f.write(b"PF\n%d %d\n-1.0\n" % (w, h) + struct.pack("<%df" % len(pix), *pix))
pfm("convsrc.pfm", 720, 576, lambda x, y, c: ((x * 7919 + y * 104729 + c * 4099) % 1009) / 1009.)
pfm("convk.pfm", 7, 5, lambda x, y, c: 1. + ((x * 3 + y * 5 + c * 7) % 11))
# vramp.pfm: a 720x576 PF vertical ramp, (y + 0.5) / 576 at row y (for the Y axis of GodRays scale 0)
pfm("vramp.pfm", 720, 576, lambda x, y, c: (y + 0.5) / 576.)
# impulse.pfm: a 64x48 PF, black except a white pixel at (32,24) (for the "max" check of a blur);
# delta.pfm: a 1x1 PF kernel (for the "conv" check of an output that must be its input)
pfm("impulse.pfm", 64, 48, lambda x, y, c: 1. if (x, y) == (32, 24) else 0.)
pfm("delta.pfm", 1, 1, lambda x, y, c: 1.)
# diffa.pfm, diffb.pfm: 64x48 PF solid colours (0.25,0.5,0.75) and (0.5,0.5,0.25), for Difference
# with opaque inputs (#opaque)
pfm("diffa.pfm", 64, 48, lambda x, y, c: (0.25, 0.5, 0.75)[c])
pfm("diffb.pfm", 64, 48, lambda x, y, c: (0.5, 0.5, 0.25)[c])
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
    # hostDemo's mean (also min and max) of a channel is null if it has no finite value
    # (an empty image, or all NaN), and JSON writes an infinite mean as null too
    mean = [None if v is None else v / scale for v in out0["mean"]]
    fmt = lambda f, v: "null" if v is None else f % v
    desc = "%s %s mean=%s" % (out0["depth"], out0["comps"], ",".join(fmt("%.3g", v) for v in mean))
    # the tolerance of mean=. 0.01 by default: byte and short inputs and outputs
    # are quantized
    tol = 0.01
    for expect in expects.split("+"):
        if expect.startswith("tol="):
            tol = float(expect[4:])
    for expect in expects.split("+"):
        if expect == "nonconst":
            if out0["min"] == out0["max"]:
                errors.append("t=0: constant output, expected an image")
        elif expect.startswith("tol="):
            pass
        elif expect.startswith("max="):
            want = [float(v) for v in expect[4:].split(",")]
            got = [None if v is None else v / scale for v in out0["max"]]
            if None in got or len(want) != len(got) or any(abs(a - b) > tol for a, b in zip(got, want)):
                errors.append("t=0: max %s, expected %s (tolerance %g)" % (",".join("%.7g" % v if v is not None else "null" for v in got), expect[4:], tol))
            desc += " max=%s" % ",".join("null" if v is None else "%.3g" % v for v in got)
        elif expect.startswith("mean="):
            want = [float(v) for v in expect[5:].split(",")]
            if None in mean:
                errors.append("t=0: no mean for channel(s) %s (null: no finite value, or an infinite mean), expected %s"
                              % (",".join(str(c) for c, v in enumerate(mean) if v is None), expect[5:]))
            elif len(want) != len(mean) or any(abs(a - b) > tol for a, b in zip(mean, want)):
                errors.append("t=0: mean %s, expected %s (tolerance %g)" % (",".join("%.7g" % v for v in mean), expect[5:], tol))
        elif expect == "identity":
            ids = [(k.get("isIdentity") or {}).get("identity") for k in renders]
            if not all(ids):
                errors.append("not an identity at all times: %s" % ids)
        elif expect == "notidentity":
            ids = [(k.get("isIdentity") or {}).get("identity") for k in renders]
            if any(ids):
                errors.append("an identity at some time, though the render changes the input: %s" % ids)
        elif expect.startswith("comps="):
            if out0["comps"] != expect[6:]:
                errors.append("t=0: components %s, expected %s" % (out0["comps"], expect[6:]))
        elif expect.startswith("premult="):
            # the kOfxImageEffectPropPreMultiplication values (ofxImageEffect.h)
            want = {"Opaque": "OfxImageOpaque", "PreMultiplied": "OfxImageAlphaPremultiplied",
                    "UnPreMultiplied": "OfxImageAlphaUnPremultiplied"}[expect[8:]]
            got = (r.get("clipPreferences") or {}).get("outputPremultiplication")
            if got != want:
                errors.append("output premultiplication %s, expected %s" % (got, want))
        elif expect.startswith("timedomain="):
            td = r.get("timeDomain") or {}
            rng = td.get("range")
            if expect[11:] == "default":
                if rng is not None:
                    errors.append("time domain %s, expected the host default" % rng)
            else:
                want = [float(v) for v in expect[11:].split(",")]
                if rng is None or len(rng) != 2 or any(abs(a - b) > 1e-9 for a, b in zip(rng, want)):
                    errors.append("time domain %s (%s), expected %s" % (rng, td.get("status"), expect[11:]))
            desc += " timeDomain=%s" % (",".join("%g" % v for v in rng) if rng else "default")
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
        elif expect.startswith("conv="):
            # conv=SRC,KERNEL (float RGB PFM files, bottom row first): the output (RGB) must be the
            # linear convolution of SRC (at 0,0, zero outside) by KERNEL (its RGB channels, each
            # normalized by its sum), with the kernel origin at pixel (kw//2, kh//2) of KERNEL
            # (filterCenter=2, the center of its RoD), on the rendered part of the RoD. Checked on
            # every 7th pixel and on the borders of the output, within 1e-4.
            def readpfm(path):
                with open(path, "rb") as f:
                    h = f.read().split(b"\n", 3)
                w, hh = (int(v) for v in h[1].split())
                a = array.array("f")
                a.frombytes(h[3][:w * hh * 3 * 4])
                if sys.byteorder != "little":
                    a.byteswap()
                return w, hh, a
            sw, sh, src = readpfm(expect[5:].split(",")[0])
            kw, kh, ker = readpfm(expect[5:].split(",")[1])
            ow, oh, out = readpfm(out0["files"][0])
            ob = out0["bounds"]
            cx, cy = kw // 2, kh // 2
            # the output RoD is the source RoD expanded by the kernel, but hostDemo may render
            # only a part of it (the project): the pixels outside SRC are checked if rendered
            want = [-cx, -cy, sw + kw - 1 - cx, sh + kh - 1 - cy]
            if not (want[0] <= ob[0] < ob[2] <= want[2] and want[1] <= ob[1] < ob[3] <= want[3]):
                errors.append("output bounds %s, not within %s" % (ob, want))
            else:
                ksum = [sum(ker[c::3]) for c in range(3)]
                xs = sorted(set(range(ob[0], ob[2], 7)) | {ob[0], ob[0] + 1, ob[2] - 2, ob[2] - 1})
                ys = sorted(set(range(ob[1], ob[3], 7)) | {ob[1], ob[1] + 1, ob[3] - 2, ob[3] - 1})
                pts = {(x, y) for x in xs for y in ys}
                pts |= {(x, y) for x in range(ob[0], ob[2]) for y in (ob[1], ob[3] - 1)}
                pts |= {(x, y) for y in range(ob[1], ob[3]) for x in (ob[0], ob[2] - 1)}
                maxerr, worst = 0., None
                for x, y in pts:
                    for c in range(3):
                        v = 0.
                        for j in range(kh):
                            sy = y + cy - j
                            if 0 <= sy < sh:
                                for i in range(kw):
                                    sx = x + cx - i
                                    if 0 <= sx < sw:
                                        v += ker[(j * kw + i) * 3 + c] * src[(sy * sw + sx) * 3 + c]
                        v /= ksum[c]
                        err = abs(out[((y - ob[1]) * ow + x - ob[0]) * 3 + c] - v)
                        if err > maxerr:
                            maxerr, worst = err, (x, y, c, v)
                if maxerr > 1e-4:
                    errors.append("not the linear convolution: error %.3g at (x,y,c)=%s (expected %.6g)" %
                                  (maxerr, worst[:3], worst[3]))
                desc += " conv: max error %.2g on %d pixels" % (maxerr, len(pts))
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
print([k for k in r["renders"] if k["time"] == 0 and not k.get("view")][0]["output"]["crc32"])' "$1" |
        tr -d '\r' # Windows: the CRLF of python's print
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
    # Clamp (issue #294, off by default) clamps the inverted channels to [0,1]: superwhite and
    # superblack give 0 and 1 (also on byte and short inputs, where the solid is clamped)
    "Invert out of range|net.sf.openfx.Invert|filter|float|mean=-0.5,1.5,0.75,0||-i Source=solid:1.5,-0.5,0.25,1 -i Mask=none"
    "Invert clamp|net.sf.openfx.Invert|filter|*|mean=0,1,0.75,0||-i Source=solid:1.5,-0.5,0.25,1 -i Mask=none clamp=1"
    # (that case also passes without Clamp on byte and short, where the solid is clamped.) With
    # Premultiplied, a colour above alpha unpremultiplies above 1, also on byte and short: R 0.75
    # at alpha 0.5 is 1.5, inverted -0.5, clamped 0, premultiplied 0, mixed 0.5 with the source:
    # 0.375 (0.25 without Clamp). G (1, inverted 0) and B (0.5, inverted 0.5): 0.25
    "Invert clamp premult|net.sf.openfx.Invert|filter|*|mean=0.375,0.25,0.25,0.5||-i Source=solid:0.75,0.5,0.25,0.5 -i Mask=none premult=1 NatronOfxParamProcessA=0 mix=0.5 clamp=1"
    "Grade solid|net.sf.openfx.GradePlugin|filter|*|mean=0.5,1,0.25,1||-i Source=solid:0.25,0.5,0.125,1 -i Mask=none multiply=2,2,2,2"
    "Grade ramp masked|net.sf.openfx.GradePlugin|filter|*|nonconst+changed:Source||-i Source=ramp -i Mask=checker:32 gamma=2,2,2,2"
    "Merge over solid|net.sf.openfx.MergePlugin|filter|*|mean=0.5,0,0.5,1||-i A=solid:0.5,0,0,0.5 -i Source=solid:0,0,1,1 -i Mask=none"
    "Merge over checker|net.sf.openfx.MergePlugin|filter|*|nonconst+changed:A+changed:Source||-i A=checker:16 -i Source=ramp -i Mask=none"
    # Keyer, luminance key with the defaults (A = 0.5, B = C = 1): the foreground key 0.625 gives
    # the background key 0.25, alpha 0.75 (Intermediate output: the color is the source; no byte depth).
    # Invert (issue #280) inverts the key, before the Inside Mask (alpha >= 0.5 here) is applied,
    # and is ignored in None mode (no keying: alpha 0)
    "Keyer|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.75||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none"
    "Keyer invert|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.25||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none invert=1"
    "Keyer invert inside mask|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.5||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none invert=1"
    "Keyer invert None mode|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none mode=3 invert=1"
    # Screen mode, despill 1, Unpremultiplied output: Invert inverts alpha but not the despill
    # amount, which uses the key without Invert (the colour is the same)
    "Keyer screen despill|net.sf.openfx.KeyerPlugin|general|short float|mean=0.1,0.141,0.1,0.341||-i Source=solid:0.1,0.8,0.1,1 -i Bg=none -i InM=none -i OutM=none mode=2 keyColor=0,1,0 despill=1 show=2"
    "Keyer screen despill invert|net.sf.openfx.KeyerPlugin|general|short float|mean=0.1,0.141,0.1,0.659||-i Source=solid:0.1,0.8,0.1,1 -i Bg=none -i InM=none -i OutM=none mode=2 keyColor=0,1,0 despill=1 show=2 invert=1"
    # Premult and Unpremult (R, G and B by A by default); Invert uses 1 - A (issue #295)
    "Premult|net.sf.openfx.Premult|filter|*|mean=0.0625,0.125,0.1875,0.25||-i Source=solid:0.25,0.5,0.75,0.25"
    "Premult invert|net.sf.openfx.Premult|filter|*|mean=0.1875,0.375,0.5625,0.25||-i Source=solid:0.25,0.5,0.75,0.25 invert=1"
    "Unpremult|net.sf.openfx.Unpremult|filter|*|mean=0.25,0.5,0.75,0.25||-i Source=solid:0.0625,0.125,0.1875,0.25"
    "Unpremult invert|net.sf.openfx.Unpremult|filter|*|mean=0.25,0.5,0.75,0.75||-i Source=solid:0.0625,0.125,0.1875,0.75 invert=1"
    # Invert swaps the constant channels (premultChannel 4: 0, 5: 1): Unpremult by 1 - 0 is an identity
    # (Premult is not: the hostDemo input is not PreMultiplied, and Premult changes that state)
    "Premult invert 0|net.sf.openfx.Premult|filter|*|mean=0.25,0.5,0.75,0.25||-i Source=solid:0.25,0.5,0.75,0.25 premultChannel=4 invert=1"
    "Premult invert 1|net.sf.openfx.Premult|filter|*|mean=0,0,0,0.25||-i Source=solid:0.25,0.5,0.75,0.25 premultChannel=5 invert=1"
    "Unpremult invert 0|net.sf.openfx.Unpremult|filter|*|identity+mean=0.25,0.5,0.75,0.25||-i Source=solid:0.25,0.5,0.75,0.25 premultChannel=4 invert=1"
    "CImgBlur|net.sf.cimg.CImgBlur|filter|float|nonconst+changed:Source||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0"
    "CImgBlur expandRoD|net.sf.cimg.CImgBlur|filter|float|nonconst+changed:Source||-i Source=checker:8 -i Mask=none size=5,5"
    "Transform|net.sf.openfx.TransformPlugin|filter|*|nonconst+changed:Source||-i Source=checker:32 rotate=30 scale=0.8,0.8"
    "Crop|net.sf.openfx.CropPlugin|filter|*|nonconst+changed:Source||-i Source=ramp bottomLeft=100,100 size=400,300"
    # render windows out of the Source RoD, where the host has no Source image to give
    # (fetchImage returns NULL, issue #192): black, or the edge pixels with Nearest borders.
    # These windows are out of the project, which the full render (tile) is clipped to.
    "AdjustRoD out of Source|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=0,0,0,0||-i Source=checker:16 addPixels=100,100 boundary=0 --frames 0 --window 100,590,300,670"
    "AdjustRoD Nearest out|net.sf.openfx.AdjustRoDPlugin|filter|*|nonconst+mean=0.52,0.52,0.52,0.52||-i Source=checker:16 addPixels=100,100 boundary=1 --frames 0 --window 100,590,300,670"
    "AdjustRoD Nearest corner|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=1,1,1,1||-i Source=checker:16 addPixels=100,100 boundary=1 --frames 0 --window 730,590,810,670"
    "Crop out of Source|net.sf.openfx.CropPlugin|filter|*|mean=0,0,0,0||-i Source=ramp bottomLeft=-100,-100 size=920,776 --frames 0 --window -90,-90,-10,-10"
    # saturation 0: the Rec.709 luminance
    "ColorCorrect|net.sf.openfx.ColorCorrectPlugin|filter|*|mean=0.465,0.465,0.465,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none MasterSaturation=0,0,0,0"
    "Clamp|net.sf.openfx.Clamp|filter|*|mean=1,0,0.5,1||-i Source=solid:1.5,-0.5,0.5,1 -i Mask=none"
    # WhiteBalance. The default (source 6504 K, i.e. D65, and the working space white
    # target) is an identity in the D65 working spaces (Rec. 709, Rec. 2020)
    "WhiteBalance default|net.sf.openfx.WhiteBalance|filter|*|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "WhiteBalance default Rec.2020|net.sf.openfx.WhiteBalance|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none workingSpace=1"
    # the same temperatures (and tints) for the source and target whites: an identity
    "WhiteBalance same whites|net.sf.openfx.WhiteBalance|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none sourceTemperature=3200 sourceTint=0.01 targetWhite=0 targetTemperature=3200 targetTint=0.01"
    "WhiteBalance ramp masked|net.sf.openfx.WhiteBalance|filter|*|nonconst+changed:Source||-i Source=ramp -i Mask=checker:32 sourceTemperature=3200"
    # D65 (6504 K) to D50 (5003 K), Bradford, in XYZ (workingSpace=5): the columns of the
    # Bradford D65->D50 matrix. They are within 6.1e-4 of Lindbloom's matrix
    # (http://www.brucelindbloom.com/Eqn_ChromAdapt.html), whose whites differ slightly:
    # D65 is x=0.3127, y=0.3290 here, and D50 is on the CIE daylight locus. The expected
    # values were computed independently (Python).
    "WhiteBalance Bradford X|net.sf.openfx.WhiteBalance|filter|float|tol=2e-5+mean=1.0476843,0.0294033,-0.0092620,1||-i Source=solid:1,0,0,1 -i Mask=none workingSpace=5 targetWhite=0 targetTemperature=5003"
    "WhiteBalance Bradford Y|net.sf.openfx.WhiteBalance|filter|float|tol=2e-5+mean=0.0228019,0.9906383,0.0150977,1||-i Source=solid:0,1,0,1 -i Mask=none workingSpace=5 targetWhite=0 targetTemperature=5003"
    "WhiteBalance Bradford Z|net.sf.openfx.WhiteBalance|filter|float|tol=2e-5+mean=-0.0502026,-0.0170650,0.7515227,1||-i Source=solid:0,0,1,1 -i Mask=none workingSpace=5 targetWhite=0 targetTemperature=5003"
    # round trip in Rec. 709: the white of 3200 K with a +0.01 tint (Planckian locus, RGB with
    # Y=1 computed independently) to the working space white (the default target), and the
    # working space white (1,1,1) from D65 (6504 K) to the 3200 K white
    "WhiteBalance 3200K to working|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=1,1,1,1||-i Source=solid:1.6013659,0.903962,0.1800995,1 -i Mask=none sourceTemperature=3200 sourceTint=0.01"
    "WhiteBalance D65 to 3200K|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=1.6013659,0.903962,0.1800995,1||-i Source=solid:1,1,1,1 -i Mask=none targetWhite=0 targetTemperature=3200 targetTint=0.01"
    # a neutral color (the 3200 K white in Rec. 2020) becomes gray (the working space white,
    # the default target), keeping the luminance
    "WhiteBalance neutral|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=0.5,0.5,0.5,1||-i Source=solid:0.6764015,0.458127,0.1972952,1 -i Mask=none workingSpace=1 sourceWhite=1 neutral=1.3528031,0.916254,0.3945904"
    # the temperatures are clamped to the domain of the locus (Daylight: 4000-25000 K, the
    # CIE x_D(T) cubic falls below about 2244 K; Planckian: 1000-15000 K, Krystek 1985), and
    # whites that clamp to the same values are an identity. The 4000 K daylight white (Rec. 709,
    # Y=1, computed independently) becomes (1,1,1) with a 1000 K source on the Daylight locus.
    "WhiteBalance Daylight 1000K|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=1,1,1,1||-i Source=solid:1.3876891,0.935643,0.4956286,1 -i Mask=none locus=2 sourceTemperature=1000"
    "WhiteBalance Daylight clamp|net.sf.openfx.WhiteBalance|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none locus=2 sourceTemperature=1000 targetWhite=0 targetTemperature=3000"
    "WhiteBalance Planckian clamp|net.sf.openfx.WhiteBalance|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none locus=1 sourceTemperature=25000 targetWhite=0 targetTemperature=15000"
    # (hostDemo only accepts finite param values, so a NaN Neutral Color can't be tested here)
    # degenerate neutral colors give the identity, not NaN: a negative luminance
    "WhiteBalance neutral Y<0|net.sf.openfx.WhiteBalance|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none sourceWhite=1 neutral=-1,-1,0.5"
    # a bluish pick near DBL_MAX, whose Rec. 2020 Z (1.87e308) would overflow to inf without the
    # division by the largest component: the pixel of the same chromaticity becomes gray with
    # its luminance Y=0.2296509 (computed independently)
    "WhiteBalance neutral huge|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=0.2296509,0.2296509,0.2296509,1||-i Source=solid:0.2,0.2,0.7,1 -i Mask=none workingSpace=1 sourceWhite=1 neutral=0.5e308,0.5e308,1.75e308"
    # the other working spaces, adaptations and loci (expected values computed independently)
    "WhiteBalance AP1 CAT02|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=0.1301937,0.5979903,1.1143985,1||-i Source=solid:0.3,0.5,0.7,1 -i Mask=none workingSpace=3 adaptation=1 sourceTemperature=3200 sourceTint=-0.02 targetWhite=0 targetTemperature=7500 targetTint=0.005"
    "WhiteBalance P3 von Kries Daylight|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=0.2494918,0.4910365,0.9323406,1||-i Source=solid:0.3,0.5,0.7,1 -i Mask=none workingSpace=4 adaptation=2 locus=2 sourceTemperature=5000"
    "WhiteBalance AP0 XYZ scaling Planckian|net.sf.openfx.WhiteBalance|filter|float|tol=1e-5+mean=0.2238629,0.9481926,4.8613681,1||-i Source=solid:0.3,0.5,0.7,1 -i Mask=none workingSpace=2 adaptation=3 locus=1 sourceTemperature=2000"
    # ColorTransform on the degenerate inputs that gave NaN or inf before openfx-supportext#55
    # (hue in [0,1]): HSL with l=0 and s!=0 (0/0 in hsl_to_rgb); RGB->HSI with a subnormal
    # sum (3/sum overflows, inf*0); RGB->HSL near white (2-(max+min) rounds to 0: s=inf)
    "HSLToRGB l=0|net.sf.openfx.HSLToRGB|filter|*|mean=0,0,0,1||-i Source=solid:0.3,0.5,0,1"
    "RGBToHSI subnormal|net.sf.openfx.RGBToHSI|filter|float|mean=0,1,0,1||-i Source=solid:1e-40,0,0,1"
    "RGBToHSL near white|net.sf.openfx.RGBToHSL|filter|float|mean=0.1667,1,1,1||-i Source=solid:1,1,0.99999994,1"
    # degenerate inputs that used to give NaN (or inf) pixels, which check() rejects. Grade:
    # gamma 0 maps values above 1 to FLT_MAX (was +inf), premultiplied by alpha 0 (was NaN).
    "Grade gamma 0 alpha 0|net.sf.openfx.GradePlugin|filter|*|mean=0,0,0,0||-i Source=solid:2,0.5,1,0 -i Mask=none gamma=0,0,0,0 premult=1"
    # Grade: a small gamma (10^100 overflows the float range) is capped to FLT_MAX, as gamma 0
    "Grade gamma 0.01 alpha 0|net.sf.openfx.GradePlugin|filter|*|mean=0,0,0,0||-i Source=solid:10,10,10,0 -i Mask=none gamma=0.01,0.01,0.01,0.01 premult=1"
    # Grade (version 3 unless -m): above 1 the gamma curve is continued by its tangent at 1, as in Nuke
    # (1 + (x - 1) / gamma: 2 -> 1.5, 4 -> 2.5 with gamma 2), and Reverse is the inverse of that line.
    # Version 2 keeps the power curve above 1 (sqrt(2), sqrt(4)), so that old projects render the same.
    "Grade gamma above 1|net.sf.openfx.GradePlugin|filter|float|mean=1.5,0.5,2.5,1||-i Source=solid:2,0.25,4,1 -i Mask=none gamma=2,2,2,2"
    "Grade gamma above 1 reverse|net.sf.openfx.GradePlugin|filter|float|mean=2,0.25,4,1||-i Source=solid:1.5,0.5,2.5,1 -i Mask=none gamma=2,2,2,2 reverse=1"
    "Grade 2 gamma above 1|net.sf.openfx.GradePlugin|filter|float|mean=1.4142,0.5,2,1||-m 2 -i Source=solid:2,0.25,4,1 -i Mask=none gamma=2,2,2,2"
    # ColorCorrect: gamma 0 in the shadows (was pow(2, 1/0) = inf), whose weight is 0 at luminance 2 (was 0 * inf = NaN)
    "ColorCorrect gamma 0|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=2,2,2,1||-i Source=solid:2,2,2,1 -i Mask=none ShadowsGamma=0,0,0,0"
    "ColorCorrect gamma 0.001|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=10,10,10,1||-i Source=solid:10,10,10,1 -i Mask=none ShadowsGamma=0.001,0.001,0.001,0.001"
    # PLogLin: the undefined conversions pass the channels through (were 1/0 and log of inf)
    "PLogLin nGamma 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none nGamma=0,0,0"
    "PLogLin linRef 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none operation=1 linRef=0,0,0"
    "PLogLin density 0|net.sf.openfx.PLogLin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none operation=1 density=0,0,0"
    # PLogLin: a tiny nGamma overflows pow (capped to FLT_MAX), premultiplied by alpha 0
    "PLogLin nGamma tiny alpha 0|net.sf.openfx.PLogLin|filter|*|mean=0,0,0,0||-i Source=solid:0.25,0.5,0.75,0 -i Mask=none nGamma=1e-6,1e-6,1e-6 premult=1"
    # Log2Lin: black and white so close that the offset rounds to 1 (was an infinite gain)
    "Log2Lin black ~ white|net.sf.openfx.Log2Lin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none black=0,0,0 white=1e-15,1e-15,1e-15"
    # Log2Lin: the offset 10^((black-white)*0.002/gamma) overflows to +inf (was NaN): pass through
    "Log2Lin offset overflow|net.sf.openfx.Log2Lin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none black=1023,1023,1023 white=0,0,0 gamma=0.001,0.001,0.001"
    # Log2Lin: the minimum gamma overflows the float range (capped to FLT_MAX), premultiplied by alpha 0
    "Log2Lin gamma min alpha 0|net.sf.openfx.Log2Lin|filter|*|mean=0,0,0,0||-i Source=solid:0.25,0.5,0.75,0 -i Mask=none gamma=0.001,0.001,0.001 premult=1"
    # PIK: a soft screen replace color with zero Rec.709 luminance (was 0/0 = NaN where alpha
    # is raised to 1 by Clip White, or a huge color with FMA contraction, e.g. on arm64): white
    "PIK replace color lum 0|net.sf.openfx.PIK|general|float|comps=RGBA+mean=0.0045,0.0045,0.0045,1||-i Fg=solid:0,0,0.25,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0,0,1 screenClipMax=0.5 screenReplaceColor=0.072175,0,-0.2126729"
    # Difference: RGB from B, alpha = gain * sum of the squared RGB differences - offset, in an RGBA
    # unpremultiplied output (issue #275: RGB inputs gave an RGB output with the matte in blue;
    # hostDemo's inputs are RGBA, so the RGB case is not tested: only the RGBA output, the alpha
    # matte and the output premultiplication are checked here). Only the opaque-inputs case can
    # detect a wrong premultiplication flag: with unpremultiplied inputs, the host default is
    # already unpremultiplied
    "Difference|net.sf.openfx.DifferencePlugin|general|*|comps=RGBA+premult=UnPreMultiplied+mean=0.5,0.5,0.25,0.3125||-i A=solid:0.25,0.5,0.75,1 -i B=solid:0.5,0.5,0.25,0.3"
    # opaque inputs: the output is still unpremultiplied, since alpha is the matte
    "Difference opaque inputs|net.sf.openfx.DifferencePlugin|general|*|comps=RGBA+premult=UnPreMultiplied+mean=0.5,0.5,0.25,0.3125||-i A=file:diffa.pfm#opaque -i B=file:diffb.pfm#opaque"
    "Difference gain offset|net.sf.openfx.DifferencePlugin|general|*|comps=RGBA+mean=0.5,0.5,0.25,0.525||-i A=solid:0.25,0.5,0.75,1 -i B=solid:0.5,0.5,0.25,0.3 gain=2 offset=0.1"
    # PIK (version 2 unless -m): the Colorspace (1: Rec. 2020, 2: ACES AP0, 3: ACES AP1) also
    # gives the luminance of the Soft Color screen and inside replace colors, and the conversion of
    # Bg for Use Bg Luminance/Chroma; version 1 always used Rec. 709 there, and is kept so that old
    # projects render the same. A partial key (Clip Black/White), a colored alpha bias and replace
    # colors, and an inside mask.
    "PIK 1 AP0 soft colors|net.sf.openfx.PIK|general|float|mean=0.3371,0.2247,1.0113,0.5||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=solid:0.15,0.65,0.2,1 -i Bg=none screenType=0 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenClipMin=0.1 screenClipMax=0.6 colorspace=2 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK AP0 soft colors|net.sf.openfx.PIK|general|float|mean=0.4982,0.3321,1.4946,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=solid:0.15,0.65,0.2,1 -i Bg=none screenType=0 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenClipMin=0.1 screenClipMax=0.6 colorspace=2 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    # the same with screenType=2 (Pick) and Rec. 2020
    "PIK 1 Rec.2020 soft colors|net.sf.openfx.PIK|general|float|mean=0.2435,0.2943,0.1309,0.52||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK Rec.2020 soft colors|net.sf.openfx.PIK|general|float|mean=0.2319,0.29,0.128,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    # Rec. 709, or neutral (gray) replace colors: version 2 renders as version 1
    "PIK Rec.709 soft colors|net.sf.openfx.PIK|general|float|mean=0.2518,0.2974,0.133,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=0 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK AP0 gray soft colors|net.sf.openfx.PIK|general|float|mean=0.258,0.2649,0.2717,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 alphaBias=0.5,0.5,0.5 screenReplaceColor=0.7,0.7,0.7 insideReplaceColor=0.4,0.4,0.4"
    # ACES AP0: bluish replace colors have a tiny or negative AP0 luminance (0,0.1,1: 6.7e-4;
    # 0,0.099,1 and 0,0,1: negative), which gave huge or negated colors (means of 14.5 and -219):
    # they are normalized with their Rec. 709 luminance, as in version 1
    "PIK AP0 bluish soft colors|net.sf.openfx.PIK|general|float|mean=0.0275,0.194,1.6536,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 screenReplaceColor=0,0.1,1 insideReplaceColor=0,0.099,1"
    "PIK AP0 blue soft colors|net.sf.openfx.PIK|general|float|mean=0.0275,0.0343,3.2353,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 screenReplaceColor=0,0,1 insideReplaceColor=0,0,1"
    # Use Bg Luminance and Chroma: version 2 converts Bg with the Colorspace too (version 1:
    # always Rec. 709), so that Rec. 2020 differs and Rec. 709 is the same
    "PIK 1 Rec.2020 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.4303,0.392,0.2097,0.52||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK Rec.2020 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.4695,0.3811,0.1991,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK Rec.709 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.5012,0.3865,0.2059,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=0 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    # show=2 (Clean Plate) with a Pick screen: the screen color divided by the alpha bias, which
    # is always normalized with Rec. 709, so that all versions and colorspaces give the same output
    "PIK 1 AP1 clean plate|net.sf.openfx.PIK|general|float|mean=0.3182,0.7158,0.3182,1||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.9,0.15 colorspace=3 alphaBias=0.2,0.8,0.3 show=2"
    "PIK AP1 clean plate|net.sf.openfx.PIK|general|float|mean=0.3182,0.7158,0.3182,1||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.9,0.15 colorspace=3 alphaBias=0.2,0.8,0.3 show=2"
    # render scale 0.5 (the means are those of the full-scale renders), tiles
    # (--window: the same pixels as the full render), and a fractional time
    "Radial scale 0.5|net.sf.openfx.Radial|general|*|mean=0.231,0.231,0.231,0.231||-i Source=none -i Mask=none --scale 0.5"
    "Transform scale 0.5|net.sf.openfx.TransformPlugin|filter|float|mean=0.31,0.31,0.31,0.31||-i Source=checker:32 rotate=30 scale=0.8,0.8 --scale 0.5"
    "CImgBlur scale 0.5|net.sf.cimg.CImgBlur|filter|float|mean=0.497,0.497,0.497,0.497||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 --scale 0.5"
    # filter=2: Box (FIR). The default IIR filters have an infinite support, so
    # their tiles differ slightly (up to 0.003) from the full render.
    "CImgBlur tile|net.sf.cimg.CImgBlur|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 filter=2 --frames 0 --window 100,100,300,250"
    "Merge tile|net.sf.openfx.MergePlugin|filter|float|nonconst+tile||-i A=checker:16 -i Source=ramp -i Mask=none --frames 0 --window 50,60,250,200"
    # ReConverge (checked on view 0, the left view): Shift Both, disparity 11 and offset 3: each
    # half is truncated separately (trunc(5.5) + trunc(1.5) = 6, where
    # trunc((11 + 3) / 2) would give 7), so the left view is shifted right by 6 pixels: the first
    # 6 of the 10 columns are black (mean 0.3 with 7). The disparity is a float image. Then tiles,
    # with a disparity (the source RoI is the horizontal band) and without one (the RoI is the
    # shifted render window).
    "ReConverge both per half|net.sf.openfx.reConvergePlugin|general|float|mean=0.4,0.4,0.4,0.4||-i Source=solid:1,1,1,1 -i Disparity=solid:11,0,0,1 convergeMode=2 offset=3 --frames 0 --window 0,0,10,10"
    "ReConverge tile|net.sf.openfx.reConvergePlugin|general|float|nonconst+tile||-i Source=ramp -i Disparity=solid:-2.75,0,0,1 convergeMode=2 offset=-3 --frames 0 --window 100,60,300,200"
    "ReConverge tile no disparity|net.sf.openfx.reConvergePlugin|general|*|nonconst+tile||-i Source=ramp -i Disparity=none convergeMode=1 offset=37.5 --frames 0 --window 100,60,300,200"
    "Rand t=0,0.5|net.sf.openfx.Noise|general|float|nonconst+framesdiffer||-i Source=none --frames 0,0.5"
    # Shuffle (version 4 unless -m) swaps R and B (outputR=8: B.b, outputB=6:
    # B.r; 0-3: A.r-A.a, 6-9: B.r-B.a). After the first test, the A and B
    # inputs differ, so that reading A for B (or B for A) fails.
    "Shuffle|net.sf.openfx.ShufflePlugin|general|*|mean=0.75,0.5,0.25,1||-i A=solid:0.25,0.5,0.75,1 -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle B|net.sf.openfx.ShufflePlugin|general|float|mean=0.75,0.5,0.25,1||-i A=ramp -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    # ErodeBlur remaps and clamps the values (here only the alpha, the default) even when the blur
    # does nothing (size 0.25: (0.5 - 0.125) / 0.875), and the Blur Alpha Threshold zeroes the alpha
    # even when a box-type filter does not blur (issue #324); with the default (Quasi-Gaussian)
    # filter and a tiny size, the render skips the threshold: an identity.
    "ErodeBlur small|eu.cimg.ErodeBlur|filter|float|notidentity+mean=0.5,0.5,0.5,0.4286||-i Source=solid:0.5,0.5,0.5,0.5 -i Mask=none size=0.25"
    "CImgBlur alpha threshold|net.sf.cimg.CImgBlur|filter|float|notidentity+mean=0.25,0.5,0.75,0||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none alphaThreshold=0.6 filter=2"
    "CImgBlur size 0|net.sf.cimg.CImgBlur|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none alphaThreshold=0.6"
    # FrameBlend, one frame (relative range -1..-1, at t=1: frame 0 of the default pattern, which
    # changes with time, so that the render reads the right frame; view 0 is red): not a copy of that frame with Mix, a mask, an unprocessed channel (from
    # the current frame; with Mix 1 and no mask, it was 0), Product with Decay (the frame times
    # 1 - decay), or Average on float images (-0 and infinite values); Min, and Average on byte
    # images, copy it (issue #325). Version 2, Frame Interval 2: the one-frame range blends that
    # frame (frame 1 at t=2), as version 2.0 passed it through (it rendered black).
    "FrameBlend one frame mix|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.5,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 mix=0.5 --frames 1"
    "FrameBlend one frame mask|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.5,0,0,1||-i Source=default -i FgM=none -i Mask=solid:0.5,0.5,0.5,0.5 frameRange=-1,-1 --frames 1"
    "FrameBlend one frame no A|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i FgM=none -i Mask=none frameRange=-1,-1 NatronOfxParamProcessA=0"
    "FrameBlend one frame product|net.sf.openfx.FrameBlend|filter|float|notidentity+mean=0.125,0.25,0.375,0.5||-i Source=solid:0.25,0.5,0.75,1 -i FgM=none -i Mask=none frameRange=-1,-1 operation=Product decay=0.5"
    "FrameBlend one frame min|net.sf.openfx.FrameBlend|filter|*|identity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 operation=Min --frames 1"
    "FrameBlend one frame average byte|net.sf.openfx.FrameBlend|filter|byte|identity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 --frames 1"
    "FrameBlend one frame average float|net.sf.openfx.FrameBlend|filter|float|notidentity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 --frames 1"
    "FrameBlend 2 one frame interval 2|net.sf.openfx.FrameBlend|filter|*|mean=0.998,0,0,1||-m 2 -i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 frameInterval=2 --frames 2"
    # LensDistortion 3DE Anamorphic 4 with zero coefficients: Squeeze 1 (the default) is an
    # identity, another squeeze is not (issue #326; it tested Squeeze 0)
    "LensDistortion 3DE Anamorphic 4 default|net.sf.openfx.LensDistortion|general|*|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 model=6"
    "LensDistortion 3DE Anamorphic 4 squeeze 2|net.sf.openfx.LensDistortion|general|float|notidentity||-i Source=checker:16 model=6 tde4_Squeeze_X=2"
    # Rectangle and Radial with Process A on an opaque Source make the Output UnPremultiplied, also
    # with Mix 0 (issue #328)
    "Rectangle opaque mix 0|net.sf.openfx.Rectangle|general|*|notidentity||-i Source=file:convsrc.pfm#opaque -i Mask=none mix=0"
    "Radial opaque mix 0|net.sf.openfx.Radial|general|*|notidentity||-i Source=file:convsrc.pfm#opaque -i Mask=none mix=0"
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
    # isIdentity is false where the render does not copy the input (a host would pass the input
    # through instead of rendering). KeyMix: out of A, but in the mask, the output is
    # B * (1 - mask * mix), here 0, not B (issue #321).
    "KeyMix out of A|net.sf.openfx.KeyMix|filter|*|notidentity+mean=0,0,0,0||-i A=file:autocrop.pfm -i Source=solid:0.25,0.5,0.75,1 -i Mask=solid:1,1,1,1 --frames 0 --window 100,100,200,200"
    # no mask (Mask disconnected): the mask is 0, the output is B
    "KeyMix no mask|net.sf.openfx.KeyMix|filter|*|identity+mean=0.25,0.5,0.75,1||-i A=solid:1,1,1,1 -i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    # A disconnected, inverted mask 0.5: B * 0.5; inverted mask, out of its RoD: the mask is 1, A
    "KeyMix no A inverted|net.sf.openfx.KeyMix|filter|*|notidentity+mean=0.125,0.25,0.375,0.5||-i A=none -i Source=solid:0.25,0.5,0.75,1 -i Mask=solid:0.5,0.5,0.5,0.5 maskInvert=1"
    "KeyMix inverted out of mask|net.sf.openfx.KeyMix|filter|*|notidentity+mean=1,1,1,1||-i A=solid:1,1,1,1 -i Source=solid:0.25,0.5,0.75,1 -i Mask=file:autocrop.pfm maskInvert=1 --frames 0 --window 100,100,200,200"
    "KeyMix out of mask|net.sf.openfx.KeyMix|filter|*|identity+mean=0.25,0.5,0.75,1||-i A=solid:1,1,1,1 -i Source=solid:0.25,0.5,0.75,1 -i Mask=file:autocrop.pfm --frames 0 --window 100,100,200,200"
    # HSVTool (issue #322): the default Output Alpha (Hue, full range) sets alpha to 1, even with Mix
    # 0; Clamp Black (on by default) clamps float images; a gain other than 1 adjusts (saturation
    # 2/3 -> 0.5 here); outside the mask, a non-Source Output Alpha is 0. With Output Alpha = Source,
    # the alpha is the source alpha (it was 0), also with Premultiply.
    "HSVTool default alpha|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none"
    "HSVTool mix 0 alpha|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none mix=0"
    "HSVTool clamp black|net.sf.openfx.HSVToolPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none outputAlpha=0"
    "HSVTool gain|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.375,0.5625,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none outputAlpha=0 clampBlack=0 saturationAdjustmentGain=0"
    "HSVTool out of mask|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.25,0.5,0.75,0||-i Source=solid:0.25,0.5,0.75,1 -i Mask=file:autocrop.pfm hueRotation=120 --frames 0 --window 100,100,200,200"
    # no adjustment, Output Alpha = Source: an identity (Clamp Black does not change byte images)
    "HSVTool no clamp|net.sf.openfx.HSVToolPlugin|filter|*|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none outputAlpha=0 clampBlack=0"
    "HSVTool clamp byte|net.sf.openfx.HSVToolPlugin|filter|byte|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none outputAlpha=0"
    # Output Alpha = Source: an identity with Mix 0 (even with Clamp Black on float), or outside the mask
    "HSVTool source mix 0|net.sf.openfx.HSVToolPlugin|filter|*|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none outputAlpha=0 hueRotation=120 mix=0"
    "HSVTool source out of mask|net.sf.openfx.HSVToolPlugin|filter|*|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=file:autocrop.pfm outputAlpha=0 hueRotation=120 --frames 0 --window 100,100,200,200"
    # byte, Premultiply, Clamp White: a colour above its alpha is clamped (R 0.5 / 0.25 = 2 -> 1)
    "HSVTool clamp white premult|net.sf.openfx.HSVToolPlugin|filter|byte|notidentity+mean=0.25,0.25,0.25,0.25||-i Source=solid:0.5,0.25,0.25,0.25 -i Mask=none outputAlpha=0 clampBlack=0 clampWhite=1 premult=1"
    "HSVTool source alpha|net.sf.openfx.HSVToolPlugin|filter|*|mean=0.75,0.25,0.5,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none outputAlpha=0 clampBlack=0 hueRotation=120"
    "HSVTool premult|net.sf.openfx.HSVToolPlugin|filter|*|mean=0.375,0.125,0.25,0.5||-i Source=solid:0.125,0.25,0.375,0.5 -i Mask=none outputAlpha=0 clampBlack=0 premult=1 hueRotation=120"
    # DenoiseSharpen, RGB color model, R unchecked: B is denoised (issue #323)
    "DenoiseSharpen B, not R|net.sf.openfx.DenoiseSharpen|filter|float|notidentity+changed:Source||--frames 0 -i Source=file:convsrc.pfm -i Mask=none analysisLock=1 colorModel=3 NatronOfxParamProcessR=0 crbbNoiseLevelHigh=0.02"
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
    # Degenerate inputs that used to give NaN or infinite values (NaN audit): the outputs must be
    # finite (a NaN output pixel fails hostDemo). A 2-pixel analysis (size 1x1 at PAR 768/720) has
    # no skewness and kurtosis: 0 (it was infinity).
    "ImageStatistics 2 pixels|net.sf.openfx.ImageStatistics|filter|float|value:statSkewness=0,0,0,0+value:statKurtosis=0,0,0,0||-i Source=ramp restrictToRectangle=1 bottomLeft=100,100 size=1,1 -s analyzeFrame"
    # LensDistortion STMap output (u, v, 1, 1): PFBarrel squeeze 0, 3DE Classic filmback width 0 and
    # 3DE Standard cylindric bending b=-2 (sqrt(1+b)) gave a NaN RoD (nothing rendered). Models: 1
    # PFBarrel, 2 3DE Classic, 5 3DE4 Radial Standard Degree 4.
    "LensDistortion PFBarrel squeeze 0|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=1 pfSqueeze=0 outputMode=STMap"
    "LensDistortion 3DE filmback 0|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=2 tde4_filmback_width_cm=0 outputMode=STMap"
    "LensDistortion 3DE b=-2|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=5 tde4_B_Cylindric_Bending=-2 outputMode=STMap"
    # FrameBlend Min/Max where no frame is used (FgM > 0 everywhere): 0 (it was +-infinity)
    "FrameBlend Min no frame|net.sf.openfx.FrameBlend|filter|*|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i FgM=solid:1,1,1,1 -i Mask=none operation=Min"
    "FrameBlend Max no frame|net.sf.openfx.FrameBlend|filter|*|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i FgM=solid:1,1,1,1 -i Mask=none operation=Max"
    "FrameBlend Min|net.sf.openfx.FrameBlend|filter|*|mean=0.5,0.25,0.75,1||-i Source=solid:0.5,0.25,0.75,1 -i FgM=none -i Mask=none operation=Min frameRange=-2,0"
    # Retime (#309), general context, float, on hostDemo's default input (frames 0 to 25, red = t in
    # view 0, with the time drawn in black: tol=0.05). hostDemo has no parametric parameters, so the
    # time domain is set by Duration (the speed is used on hosts with a Warp curve, e.g. Natron).
    "Retime speed 1|net.sf.openfx.Retime|general|float|identity+timedomain=0,25+tol=0.05+mean=10,0,0,1||--frames 10"
    "Retime speed 2|net.sf.openfx.Retime|general|float|identity+timedomain=0,25+tol=0.05+mean=20,0,0,1||speed=2 --frames 10"
    "Retime duration 2|net.sf.openfx.Retime|general|float|identity+timedomain=0,50+tol=0.05+mean=5,0,0,1||speed=0.5 duration=2 --frames 10"
    # Box filter (filter=3): at speed 1, the input frame; at speed 10, t=0 covers the source frames
    # -5 to 5 (weights 0.05, 0.1 x 9, 0.05), and -5 to -1 are transparent black (Before = Black): the
    # mean is 0.1 x (1+2+3+4) + 0.05 x 5 = 1.25, alpha 0.55 (Linear gives frame 0: 0, alpha 1);
    # at speed 0.5, t=21 is the source time 10.5: frames 10 and 11, half each.
    "Retime Box speed 1|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=10,0,0,1||filter=3 --frames 10"
    "Retime Box speed 10|net.sf.openfx.Retime|general|float|mean=1.25,0,0,0.55||filter=3 speed=10 before=2 --frames 0"
    "Retime Box speed 0.5|net.sf.openfx.Retime|general|float|tol=0.05+mean=10.5,0,0,1||filter=3 speed=0.5 --frames 21"
    # GodRays with gamma 0 computed pow(x, 1/0) of x = 1 up to rounding: the colors were 1 (or
    # infinite); they are now interpolated geometrically from fromColor to toColor
    "GodRays gamma 0|net.sf.openfx.GodRays|filter|float|mean=0.209,0.264,0.310,0.351||-i Source=checker:16 scale=0.7,0.7 fromColor=1,1,1,1 toColor=0.2,0.3,0.4,0.5 gamma=0,0,0,0"
    # GodRays with scale 0 clamped it to 1e-4 and interpolated it geometrically (scales down to 1.3e-4,
    # each supersampled over up to 81x81 pixels per output pixel: minutes per frame, Natron#1005, and a
    # mean of 0.926 here, mostly the edge of the ramp). It is now interpolated linearly from 1 to 0, like
    # a negative scale: the mean of ramp(center + (p - center) * 32 / k) over k = 1..32 is 0.641
    "GodRays scale 0|net.sf.openfx.GodRays|filter|float|mean=0.641,0.641,0.641,0.641||-i Source=ramp center=200,150 scale=0,0 --window 280,200,312,232"
    # The same on the Y axis, with a vertical ramp (any X scale gives the same result): scale 0 on Y
    # only, and with uniform scaling (Y takes the X scale). The mean of
    # vramp(center + (p - center) * 32 / k) over k = 1..32 is 0.578 (the old code gave 0.907)
    "GodRays scale 0 Y|net.sf.openfx.GodRays|filter|float|mean=0.578,0.578,0.578,1||-i Source=file:vramp.pfm center=200,150 scale=5,0 --window 280,200,312,232"
    "GodRays scale 0 uniform|net.sf.openfx.GodRays|filter|float|mean=0.578,0.578,0.578,1||-i Source=file:vramp.pfm center=200,150 uniform=1 scale=0,5 --window 280,200,312,232"
    # Rand with density 0 computed 0/0 for its noise level
    "Rand density 0|net.sf.openfx.Noise|general|*|mean=0,0,0,0||-i Source=none density=0"
    "ImageStatistics auto crop seq|net.sf.openfx.ImageStatistics|filter|float|value:autoCropSize=34.133333,16+keys:autoCropBottomLeft=26+keys:autoCropSize=26+keyrange:autoCropSize=0,25||-i Source=file:autocrop.pfm@100,50 -s analyzeSequenceAutoCrop"
    # CImg fixes that openfx-gmic's patches of CImg 4.0.7 backported, upstream since CImg 4.0.8.
    # A math parser macro whose body ends with an argument (GreycLab/CImg#496: read past the end
    # of the body).
    "CImgExpression macro|net.sf.cimg.CImgExpression|filter|float|mean=0.5,0.5,0.5,0.5||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=f(a)=0.25+a;f(0.25)"
    # J() with linear interpolation and periodic boundaries, and cubic and Neumann, 500 pixels
    # below the image: the Source values; cubic and Dirichlet, half a pixel right: zero, except in
    # the top 76 rows (GreycLab/CImg#498; without the fix, a segmentation fault)
    "CImgExpression J periodic|net.sf.cimg.CImgExpression|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=J(0,-500,0,1,2)"
    "CImgExpression J Neumann|net.sf.cimg.CImgExpression|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=J(0,-500,0,2,1)"
    "CImgExpression J Dirichlet|net.sf.cimg.CImgExpression|filter|float|mean=0.033,0.066,0.099,0.132||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=J(0.5,-500,0,2,0)"
    # symmetric_eigen() on the rank-deficient structure tensors of flat images and ramps
    # (GreycLab/gmic#221: NaN eigenvectors when the compiler contracts to FMAs): no NaN
    # output pixels (the float outputs are checked for NaN)
    "CImgSmooth flat|net.sf.cimg.CImgSmooth|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgSmooth ramp|net.sf.cimg.CImgSmooth|filter|float|mean=0.5,0.5,0.5,0.5+nonconst||-i Source=ramp -i Mask=none"
    "CImgSharpenShock flat|net.sf.cimg.CImgSharpenShock|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgSharpenShock ramp|net.sf.cimg.CImgSharpenShock|filter|float|nonconst||-i Source=ramp -i Mask=none"
    # FFT convolution of a 720x576 image (not a power of two: with FFTW or pocketfft, the FFT is
    # not padded to 1024x1024) by a 7x5 kernel: the exact linear convolution, without a CImg
    # "non 2^N dimension" error, whatever the FFT library (CImgFFTConvolve.h)
    "CImgConvolve 720x576|net.sf.cimg.CImgConvolve|filter|float|conv=convsrc.pfm,convk.pfm||--frames 0 -i Source=file:convsrc.pfm -i Filter=file:convk.pfm -i Mask=none filterCenter=2"
    # ZDefocus (NatronGitHub/openfx-misc#266). A sharp sky (disparity 0) with Disparity with
    # Offset and a Focus Point (the image center) on the sky: the output is the input (without
    # Blur Inside).
    "CImgZDefocus sky focus|net.sf.cimg.CImgZDefocus|general|float|conv=convsrc.pfm,delta.pfm||--frames 0 -i Source=file:convsrc.pfm -i Depth=solid:0,0,0,1 -i Filter=none depthType=2 useFocusPoint=1 blurInside=0"
    # A depth map with a focus of 0: the radius is Size*|1 - 0/z| = Size everywhere (Size 2, a disc
    # whose center weight is 0.126)
    "CImgZDefocus depth focus 0|net.sf.cimg.CImgZDefocus|general|float|max=0.126,0.126,0.126,1+tol=0.003||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:3,3,3,1 -i Filter=none depthType=0 useFocusPoint=0 focus=0 size=2 maxSize=20 slices=10 aperture=1 blurInside=0"
    # Disparity with Offset: blur radius = Size*|d - d_f| = 10*0.2 = 2 pixels, a disc whose center
    # weight is 0.126 (Disparity would give a radius of 10*(0.3/0.1 - 1) = 20 pixels: 0.0008)
    "CImgZDefocus disparity offset|net.sf.cimg.CImgZDefocus|general|float|max=0.126,0.126,0.126,1+tol=0.003||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:0.3,0.3,0.3,1 -i Filter=none depthType=2 useFocusPoint=0 focus=0.1 size=10 maxSize=20 slices=10 aperture=1"
    # Blur Inside (on by default): an in-focus impulse gets the 3x3 kernel [0.125, 0.75, 0.125]^2,
    # whose center weight is 0.5625
    "CImgZDefocus blur inside|net.sf.cimg.CImgZDefocus|general|float|max=0.5625,0.5625,0.5625,1+tol=0.0005||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:2,2,2,1 -i Filter=none depthType=0 useFocusPoint=0 focus=2"
    # Blur Inside only blurs the pixels with no blur: a pixel just out of focus (radius 0.05, so
    # 2.5% in the 2-pixel slice) is not blurred by it: its peak is 0.978, not 0.5625
    "CImgZDefocus blur inside, slightly out of focus|net.sf.cimg.CImgZDefocus|general|float|max=0.978,0.978,0.978,1+tol=0.003||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:2.01,2.01,2.01,1 -i Filter=none depthType=0 useFocusPoint=0 focus=2 size=10 maxSize=20 slices=10 aperture=1"
    # A disparity map with a focus of 0: the ratio disparity/focus is infinite, so every pixel with
    # a disparity above 0 is blurred by Maximum Size (2 pixels: center weight 0.126)
    "CImgZDefocus disparity focus 0|net.sf.cimg.CImgZDefocus|general|float|max=0.126,0.126,0.126,1+tol=0.003||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:0.5,0.5,0.5,1 -i Filter=none depthType=1 useFocusPoint=0 focus=0 size=1 maxSize=2 slices=10 aperture=1 blurInside=0"
    # The Depth of Focus is a dead zone, subtracted from the normalized defocus:
    # radius = Size*(|1 - d/d_f| - dof/2) = 10*(1.667 - 0.305) = 13.6 pixels, center weight
    # 0.0019 (in disparity units, 6.5 pixels: 0.0087)
    "CImgZDefocus depth of focus|net.sf.cimg.CImgZDefocus|general|float|max=0.00186,0.00186,0.00186,1+tol=0.0003||--frames 0 -i Source=file:impulse.pfm -i Depth=solid:0.8,0.8,0.8,1 -i Filter=none depthType=1 useFocusPoint=0 focus=0.3 depthOfFocus=0.61 size=10 maxSize=20 slices=10 aperture=1"
    # TimeDissolve, In 1 and Out 10 (the defaults): the In and Out frames are part of the dissolve
    # (issue #320). The curve abscissa is 0 at the frame before In and 1 at the frame after Out:
    # frame 0 is A, frame 11 is B, and with the smoothstep curve (hostDemo has no parametric
    # parameters) frame 1 has 3x^2-2x^3 = 0.0233 of B (x = 1/11), frame 10 has 0.9767 of B. Byte
    # images truncate (0.0233 gives 5/255): tolerance 0.005 in byte, 0.0005 in short and float.
    "TimeDissolve before In|net.sf.openfx.TimeDissolvePlugin|filter|*|identity+mean=0,0,0,1||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 0"
    "TimeDissolve at In|net.sf.openfx.TimeDissolvePlugin|filter|byte|mean=0.0233,0.0233,0.0233,1+tol=0.005||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 1"
    "TimeDissolve at In|net.sf.openfx.TimeDissolvePlugin|filter|short float|mean=0.02329,0.02329,0.02329,1+tol=0.0005||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 1"
    "TimeDissolve at Out|net.sf.openfx.TimeDissolvePlugin|filter|byte|mean=0.9767,0.9767,0.9767,1+tol=0.005||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 10"
    "TimeDissolve at Out|net.sf.openfx.TimeDissolvePlugin|filter|short float|mean=0.97671,0.97671,0.97671,1+tol=0.0005||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 10"
    "TimeDissolve after Out|net.sf.openfx.TimeDissolvePlugin|filter|*|identity+mean=1,1,1,1||-i A=solid:0,0,0,1 -i Source=solid:1,1,1,1 --frames 11"
    # the A input at the same time (hostDemo's default pattern: red = t at t > 1, except the text),
    # at frame 5 weighted by 1 - 0.432 (x = 5/11)
    "TimeDissolve default input|net.sf.openfx.TimeDissolvePlugin|filter|float|mean=2.834,0,0,1+framesdiffer||-i A=default -i Source=solid:0,0,0,1 --frames 5,1,10"
    # AppendClip (issue #319), on hostDemo's default pattern (clip 1, frames 0 to 25: red = t except
    # the text, 0.002 at t=0, 1 - 0.002 at t=1, float only above 1) then a solid (clip 2): the first
    # frame of the first clip is at First Frame (frame 1 shows its frame 0), clip 2 follows its last
    # frame (frame 26 shows frame 25 of clip 1, frame 27 frame 0 of clip 2).
    "AppendClip first frame|net.sf.openfx.AppendClip|general|float|identity+mean=0.002,0,0,1+framesdiffer||-i 1=default -i 2=solid:0.5,0.5,0.5,1 --frames 1,2,26"
    "AppendClip first frame 5|net.sf.openfx.AppendClip|general|float|identity+mean=0.002,0,0,1+framesdiffer||-i 1=default -i 2=solid:0.5,0.5,0.5,1 firstFrame=5 --frames 5,6,30"
    "AppendClip end of clip 1|net.sf.openfx.AppendClip|general|float|identity+mean=24.949,0,0,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 --frames 26"
    "AppendClip start of clip 2|net.sf.openfx.AppendClip|general|*|identity+mean=0.5,0.5,0.5,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 --frames 27"
    # linear fades over N frames: 1/(N+1)... N/(N+1); Fade In 3 at frames 1 and 3, then not faded
    "AppendClip fade in 3 first|net.sf.openfx.AppendClip|general|*|mean=0.25,0.25,0.25,0.25||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 1"
    "AppendClip fade in 3 last|net.sf.openfx.AppendClip|general|*|mean=0.75,0.75,0.75,0.75||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 3"
    "AppendClip fade in 3 after|net.sf.openfx.AppendClip|general|*|identity+mean=1,1,1,1||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 4"
    # Fade Out 2: frame 50 is not faded, frame 51 is at 2/3, the last frame (52) at 1/3
    "AppendClip fade out 2 before|net.sf.openfx.AppendClip|general|*|identity+mean=0.5,0.5,0.5,1||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 50"
    "AppendClip fade out 2 first|net.sf.openfx.AppendClip|general|*|mean=0.3333,0.3333,0.3333,0.6667||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 51"
    "AppendClip fade out 2 last|net.sf.openfx.AppendClip|general|*|mean=0.1667,0.1667,0.1667,0.3333||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 52"
    # Cross Dissolve 4: the last 4 frames of clip 1 (frames 23 to 26) overlap the first 4 of clip 2;
    # clip 1 (its frame 22, then 25) is weighted by 0.8 at frame 23 and 0.2 at frame 26
    "AppendClip dissolve 4 first|net.sf.openfx.AppendClip|general|float|mean=17.665,0.1,0.1,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 23"
    "AppendClip dissolve 4 last|net.sf.openfx.AppendClip|general|float|mean=5.390,0.4,0.4,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 26"
    "AppendClip dissolve 4 after|net.sf.openfx.AppendClip|general|*|identity+mean=0.5,0.5,0.5,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 27"
)

n=0 failed=() xfailed=()
for t in "${tests[@]}"; do
    IFS='|' read -r label id ctx depths expect bug args <<< "$t"
    if [ -n "$RENDERTEST_ONLY" ] && ! echo "$label" | grep -E -e "$RENDERTEST_ONLY" > /dev/null; then
        continue
    fi
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
rm -f out.* full.* report.json ref.json autocrop.pfm convsrc.pfm convk.pfm diffa.pfm diffb.pfm

if [ $n = 0 ]; then
    echo "no render matches RENDERTEST_ONLY=$RENDERTEST_ONLY"
    exit 1
fi
echo
echo "rendertest: $((n - ${#failed[@]} - ${#xfailed[@]})) passed, ${#xfailed[@]} known bugs (XFAIL), ${#failed[@]} failed, $n renders in $(( $(date +%s) - start ))s"
if [ ${#failed[@]} -ne 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
