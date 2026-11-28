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
# RENDERTEST_TIMEOUT=N: kill a hostDemo run after N seconds (default 300; not on Windows).
# RENDERTEST_GPU=require: fail if hostDemo can't render with OpenGL (by default, the renders with
# --gpu=opengl in their arguments are skipped then, see below).
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
# synthetic inputs (-i Clip=solid:r,g,b,a|ramp|checker:N|none, RGBA unless #rgb or #alpha
# follows), with the depth and components that the clip preferences of the plugin map each
# clip to. With --multiplane (in the arguments of a render), hostDemo is a multiplane host, and
# -i Clip@Plane=spec gives an input an extra plane (openfx#331). A render fails if:
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
#   has values outside [lo, hi] at the first time (range=lo,hi, normalized like mean=),
#   does not have the expected output bounds at the first time (bounds=x1,y1,x2,y2: the rendered
#   pixels, the render window clipped to the RoD),
#   takes more than N seconds (maxsec=N: the whole hostDemo run),
#   does not have the expected maximum value (max=r,g,b,a, as mean=), e.g. the peak
#   of a blurred impulse, which gives the blur size, has values outside [lo, hi]
#   (range=lo,hi), or
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
. "$(dirname "$0")/lib.sh"

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
# with OpenGL): these two are only added for the OpenGL renders (gl_path below). The prunes
# match names, so that they also work with explicit DIRs.
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
# The plugin path of the OpenGL renders (--gpu=opengl): also the directories of Test and Shadertoy
# (in a CMake install tree, they are already there)
gl_dirs=("${dirs[@]}")
while IFS= read -r d; do
    [ -n "$d" ] || continue
    case "$d" in /*) ;; *) d=$top/${d#./} ;; esac
    case " ${gl_dirs[*]} " in *" $d "*) ;; *) gl_dirs+=("$d") ;; esac
done < <(cd "$top" && find "$@" \( -name openfx -o -name DebugProxy \) -prune -o \
             \( -name Test.ofx.bundle -o -name Shadertoy.ofx.bundle \) -path "$filter/*.ofx.bundle" -print -prune |
             sed 's,/[^/]*$,,' | sort -u)
gl_path=$(IFS=:; echo "${gl_dirs[*]}")
# Windows: a native list (C:\...;D:\...), for the native hostDemo
[ $windows = 0 ] || OFX_PLUGIN_PATH=$(cygpath -w -p "$OFX_PLUGIN_PATH")
[ $windows = 0 ] || gl_path=$(cygpath -w -p "$gl_path")
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
def pfm(name, w, h, val):
    # a W x H PF whose channel C at pixel (X, Y) is VAL(X, Y, C)
    pix = [val(x, y, c) for y in range(h) for x in range(w) for c in range(3)]
    with open(name, "wb") as f:
        f.write(b"PF\n%d %d\n-1.0\n" % (w, h) + struct.pack("<%df" % len(pix), *pix))
pfm("autocrop.pfm", 64, 48, lambda x, y, c: 1. if 10 <= x < 42 and 8 <= y < 24 else 0.)
# convsrc.pfm: a 720x576 PF (not a power of two) with pseudo-random values in [0,1), different in
# each channel; convk.pfm: a 7x5 PF kernel, positive, not symmetric, different in each channel
# (for the "conv" check: an exact linear convolution)
pfm("convsrc.pfm", 720, 576, lambda x, y, c: ((x * 7919 + y * 104729 + c * 4099) % 1009) / 1009.)
pfm("convk.pfm", 7, 5, lambda x, y, c: 1. + ((x * 3 + y * 5 + c * 7) % 11))
# vramp.pfm: a 720x576 PF vertical ramp, (y + 0.5) / 576 at row y (for the Y axis of GodRays scale 0)
pfm("vramp.pfm", 720, 576, lambda x, y, c: (y + 0.5) / 576.)
# impulse.pfm: a 64x48 PF, black except a white pixel at (32,24) (for the "max" check of a blur);
# delta.pfm: a 1x1 PF kernel (for the "conv" check of an output that must be its input)
pfm("impulse.pfm", 64, 48, lambda x, y, c: 1. if (x, y) == (32, 24) else 0.)
# impulse3.pfm: impulse.pfm times 3 (a difference above 1 for Sharpen and Soften, #313)
pfm("impulse3.pfm", 64, 48, lambda x, y, c: 3. if (x, y) == (32, 24) else 0.)
pfm("delta.pfm", 1, 1, lambda x, y, c: 1.)
# diffa.pfm, diffb.pfm: 64x48 PF solid colours (0.25,0.5,0.75) and (0.5,0.5,0.25), for Difference
# with opaque inputs (#opaque)
pfm("diffa.pfm", 64, 48, lambda x, y, c: (0.25, 0.5, 0.75)[c])
pfm("diffb.pfm", 64, 48, lambda x, y, c: (0.5, 0.5, 0.25)[c])
# kimpulse.pfm: a 64x64 PF, black except a white pixel at (32,32) (the filter kernels, #270)
pfm("kimpulse.pfm", 64, 64, lambda x, y, c: 1. if (x, y) == (32, 32) else 0.)
# mirror65.pfm: a 65x8 PF (an odd width), white at x 0 to 9, black elsewhere (Mirror at render scale 0.5)
pfm("mirror65.pfm", 65, 8, lambda x, y, c: 1. if x < 10 else 0.)
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
def readpfm(path):
    # (width, height, pixels) of a float RGB PFM file (bottom row first)
    with open(path, "rb") as f:
        h = f.read().split(b"\n", 3)
    w, hh = (int(v) for v in h[1].split())
    a = array.array("f")
    a.frombytes(h[3][:w * hh * 3 * 4])
    if sys.byteorder != "little":
        a.byteswap()
    return w, hh, a
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
# report has no view count; one view with --ofx14 and --ofx11). The expectations are checked
# on view 0.
nviews = int(refs.get("views", 2))
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
    if refs.get("gpu") == "1" and (not k["render"].get("openGL") or k["render"].get("cpuRetry")):
        # --gpu=opengl: rendered with OpenGL, without a CPU retry
        errors.append("t=%s: not rendered with OpenGL: %s" % (k.get("time"), k.get("render")))
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
        if expect.startswith("range="):
            lo, hi = (float(v) for v in expect[6:].split(","))
            mins = [None if v is None else v / scale for v in out0["min"]]
            maxs = [None if v is None else v / scale for v in out0["max"]]
            if None in mins or None in maxs or min(mins) < lo - 1e-6 or max(maxs) > hi + 1e-6:
                errors.append("t=0: values in [%s, %s], expected within [%g, %g]" % (
                    ",".join(fmt("%.4g", v) for v in mins), ",".join(fmt("%.4g", v) for v in maxs), lo, hi))
            desc += " range=%s..%s" % (fmt("%.3g", min(mins) if None not in mins else None),
                                       fmt("%.3g", max(maxs) if None not in maxs else None))
        elif expect.startswith("maxsec="):
            if "elapsed" not in refs:
                errors.append("no elapsed time")
            elif int(refs["elapsed"]) > float(expect[7:]):
                errors.append("took %ss, more than %s s" % (refs["elapsed"], expect[7:]))
            desc += " %ss" % refs.get("elapsed")
        elif expect == "nonconst":
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
        elif expect.startswith("bounds="):
            # the output bounds (pixels) at the first time: the render window clipped to the RoD
            want = [int(v) for v in expect[7:].split(",")]
            if list(out0["bounds"]) != want:
                errors.append("output bounds %s, expected %s" % (out0["bounds"], want))
            desc += " bounds=%s" % ",".join(str(v) for v in out0["bounds"])
        elif expect.startswith("range="):
            # every channel of the output at the first time is within [lo, hi]
            lo, hi = (float(v) for v in expect[6:].split(","))
            if min(out0["min"]) < lo or max(out0["max"]) > hi:
                errors.append("t=0: values from %s to %s, expected all in [%g, %g]" % (out0["min"], out0["max"], lo, hi))
            desc += " range=%.4g..%.4g" % (min(out0["min"]), max(out0["max"]))
        elif expect == "framesdiffer":
            crcs = [(k.get("output") or {}).get("crc32") for k in renders]
            if len(set(crcs)) != len(crcs):
                errors.append("the same image at different times: %s" % crcs)
        elif expect == "tile":
            # the tile (render window) must be the same region of the full
            # render (float RGB PFM files, bottom row first)
            with open(refs["full"]) as f:
                full = json.load(f)["renders"][0]["output"]
            tw, _, tile = readpfm(out0["files"][0])
            fw, _, fpix = readpfm(full["files"][0])
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
# On Linux and macOS, a render that takes more than RENDERTEST_TIMEOUT seconds
# (default 300) is killed (run_with_timeout), so that a hang fails instead of
# blocking the test.
RENDERTEST_TIMEOUT=${RENDERTEST_TIMEOUT:-300}
case "${RENDERTEST_GPU:-}" in
    ''|require) ;;
    *) echo "rendertest: RENDERTEST_GPU=$RENDERTEST_GPU: the only value is require"; exit 1 ;;
esac
hostdemo() {
    local log=$1 status=0
    shift
    if [ $windows = 1 ]; then
        ( "$HOSTDEMO" --no-std-paths --no-cache --quiet "$@"; exit $? ) > "$log" 2>&1
        return
    fi
    ( run_with_timeout "$RENDERTEST_TIMEOUT" "$HOSTDEMO" --no-std-paths --no-cache --quiet "$@" ) \
        > "$log" 2>&1 || status=$?
    [ "$status" != 142 ] || echo "timeout: killed after ${RENDERTEST_TIMEOUT}s" >> "$log"
    return "$status"
}

# Does hostDemo render with OpenGL here (--gpu=opengl: openfx#367, built with
# OFX_HOSTSUPPORT_OPENGL, with an OpenGL context)? An older hostDemo rejects the option, one built
# without it says so, one without a context warns: all name the option. A capable one only says
# that the dummy plugin is not found. Without it, the renders with --gpu=opengl are skipped.
gpu_ok=0
( OFX_PLUGIN_PATH=$work/none; export OFX_PLUGIN_PATH; hostdemo gpuprobe.log --gpu=opengl -p rendertest.probe ) || true
if ! grep -q -e '--gpu' gpuprobe.log && grep -q 'not found' gpuprobe.log; then
    gpu_ok=1
    echo "hostDemo renders with OpenGL (--gpu=opengl)"
    "$HOSTDEMO" --gpu-renderers 2> /dev/null | sed 's/^/  /' || true
else
    echo "note: hostDemo does not render with OpenGL here (--gpu=opengl): its renders are skipped"
    grep -e '--gpu' gpuprobe.log | sed 's/^/  | /' || true
    if [ "${RENDERTEST_GPU:-}" = require ]; then
        echo "rendertest: RENDERTEST_GPU=require, but hostDemo does not render with OpenGL here"
        exit 1
    fi
fi

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
# failed assertion) is only expected in debug builds. An "error:" entry is not
# a bug but an expected error (e.g. parameters that the plug-in refuses): it is
# checked in the same way, and its render passes ("ok", an expected error).
# ContactSheet (filter context): hostDemo connects every clip that has no -i, so the inputs
# after the last one of a test are disconnected with -i N=none (Center Rows depends on the last
# connected input). The clips of inputs 10 to 15 are named 10 to 15; in ContactSheet 1, they are
# named 01, 11, 21, 31, 41 and 51 (it wrote the digits in reverse order, #401).
cs_none10="10 11 12 13 14 15"
cs_none1=$(for i in $(seq 1 9) $cs_none10; do printf -- ' -i %s=none' "$i"; done)
cs1_none1=$(for i in $(seq 1 9) 01 11 21 31 41 51; do printf -- ' -i %s=none' "$i"; done)
cs_none2=$(for i in $(seq 2 9) $cs_none10; do printf -- ' -i %s=none' "$i"; done)
cs_none3=$(for i in $(seq 3 9) $cs_none10; do printf -- ' -i %s=none' "$i"; done)
# input 10 only (with Source), in ContactSheet 2 and 1
cs_only10=$(for i in $(seq 1 9) 11 12 13 14 15; do printf -- ' -i %s=none' "$i"; done)
cs1_only10=$(for i in $(seq 1 9) 11 21 31 41 51; do printf -- ' -i %s=none' "$i"; done)
tests=(
    # generators. The byte and short outputs of Constant are sRGB and Rec.709:
    # 1 and 0 are the same in all three.
    "Constant|net.sf.openfx.ConstantPlugin|general|*|mean=1,0,1,0.5||-i Source=none color=1,0,1,0.5"
    "Constant float|net.sf.openfx.ConstantPlugin|general|float|mean=0.25,0.5,0.75,1||-i Source=none color=0.25,0.5,0.75,1"
    # Output Components Alpha: the colour's alpha only
    "Constant Alpha|net.sf.openfx.ConstantPlugin|general|*|comps=Alpha+mean=0.5||-i Source=none color=1,0,1,0.5 outputComponents=Alpha"
    # RGB output, on a host that supports it (hostDemo --components: the default is RGBA and Alpha)
    "Constant RGB|net.sf.openfx.ConstantPlugin|general|*|comps=RGB+mean=1,0,1||--components rgba,rgb,alpha -i Source=none color=1,0,1,0.5 outputComponents=RGB"
    "CheckerBoard RGB|net.sf.openfx.CheckerBoardPlugin|general|*|comps=RGB+nonconst||--components rgba,rgb,alpha -i Source=none outputComponents=RGB"
    "CheckerBoard|net.sf.openfx.CheckerBoardPlugin|general|*|nonconst||-i Source=none"
    "ColorBars|net.sf.openfx.ColorBars|general|*|nonconst||-i Source=none"
    # Constant 1.1, Infinite (#284): an infinite RoD, so a window out of the format (as in
    # "AdjustRoD out of Source") is filled too; without Infinite it is out of the RoD
    "Constant infinite|net.sf.openfx.ConstantPlugin|general|*|mean=1,0,1,0.5||-i Source=none color=1,0,1,0.5 infinite=1 --frames 0 --window -90,-90,-10,-10"
    # CheckerBoard 2, Grid Origin Bottom Left (#285): the bottom-left 32x32 pixels are in box (0,0),
    # Color 0 (with Center, boxes of 60x64 pixels from (360,288), they are in box (-6,-5): Color 3)
    "CheckerBoard grid origin bottom left|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=none gridOrigin=1 lineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --frames 0 --window 0,0,32,32"
    # at render scale 0.5 (boxes of 30x32 pixels): the bottom-left 16x16 pixels are Color 0
    "CheckerBoard grid origin bottom left scale 0.5|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=none gridOrigin=1 lineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --scale 0.5 --frames 0 --window 0,0,16,16"
    # Extent Default with Source connected: the RoD is Source's (canonical 100,100,400,300), so box
    # (0,0) starts at pixel (93.75,100); anchored to the project corner, this window would span
    # four boxes, from (1,1) to (2,2)
    # Center with Source connected, as in 1.0: the grid is still centred on the project (360,288):
    # this window is box (-4,-2), Color 0 (centred on the Source RoD, it would be Color 3)
    "CheckerBoard grid origin center, Source RoD|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=rod:100,100,400,300 lineWidth=0 centerlineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --frames 0 --window 124,164,156,196"
    "CheckerBoard grid origin bottom left, Source RoD|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=rod:100,100,400,300 gridOrigin=1 lineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --frames 0 --window 96,102,128,134"
    # ColorBars 2, PAL (#286): the seven bars at intensity 0.5 fill the image, 103 or 102 pixels wide
    # (hostDemo has no RGB output, so it is RGBA, with alpha 0): each of R, G, B is 0.5 in four bars
    # (412 or 411 pixels of 720)
    "ColorBars PAL|net.sf.openfx.ColorBars|general|float|comps=RGBA+mean=0.286,0.286,0.285,0||-i Source=none PAL=1"
    # the obsolete version 1 (Natron 2.5.0)
    "CheckerBoard 1|net.sf.openfx.CheckerBoardPlugin|general|*|nonconst||-m 1 -i Source=none"
    "ColorBars 1|net.sf.openfx.ColorBars|general|*|nonconst+comps=RGBA||-m 1 -i Source=none"
    # windows that tell the versions apart (#285, #286). CheckerBoard 2 rounds the box size to whole
    # pixels at full size: 50/(768/720) = 46.875 pixels (version 1) or 47 (version 2), so pixel x=31
    # is 7.02 boxes left of the centre (360) in version 1 (box -8: Color 0) and 7 in version 2 (box
    # -7: Color 1)
    "CheckerBoard 1 box 50, not rounded|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-m 1 -i Source=none boxSize=50,50 lineWidth=0 centerlineWidth=0 color0=1,0,0,1 color1=0,1,0,1 --frames 0 --window 31,300,32,301"
    "CheckerBoard box 50, rounded|net.sf.openfx.CheckerBoardPlugin|general|*|mean=0,1,0,1||-i Source=none boxSize=50,50 lineWidth=0 centerlineWidth=0 color0=1,0,0,1 color1=0,1,0,1 --frames 0 --window 31,300,32,301"
    # at render scale 0.5, the rounded boxes are 23.5 pixels wide (47 at full size, so the grid does
    # not drift): pixel x=39 is 6 boxes left of the centre (180), box -6, Color 0 (23.4375 without
    # rounding would give box -7)
    "CheckerBoard box 50, rounded, scale 0.5|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=none boxSize=50,50 lineWidth=0 centerlineWidth=0 color0=1,0,0,1 color1=0,1,0,1 --scale 0.5 --frames 0 --window 39,150,40,151"
    # Bottom Left with box 10: boxes of 9 (rounded) x 10 pixels from the corner, each window is one box
    "CheckerBoard bottom left box 10, box (0,0)|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,0,0,1||-i Source=none gridOrigin=1 boxSize=10,10 lineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --frames 0 --window 0,0,9,10"
    "CheckerBoard bottom left box 10, box (1,0)|net.sf.openfx.CheckerBoardPlugin|general|*|mean=0,1,0,1||-i Source=none gridOrigin=1 boxSize=10,10 lineWidth=0 color0=1,0,0,1 color1=0,1,0,1 color2=0,0,1,1 color3=1,1,0,1 --frames 0 --window 9,0,18,10"
    # the center line: 3 pixels wide by default in version 2 (x 359..361), 1 in version 1 (x 360);
    # next to it, the default boxes (gray 0.5 in version 2 at x=362, 0.1 in version 1 at x=359)
    "CheckerBoard centre line|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,1,0,1||-i Source=none --frames 0 --window 359,10,362,11"
    "CheckerBoard next to the centre line|net.sf.openfx.CheckerBoardPlugin|general|float|mean=0.5,0.5,0.5,1||-i Source=none --frames 0 --window 362,10,363,11"
    "CheckerBoard 1 centre line|net.sf.openfx.CheckerBoardPlugin|general|*|mean=1,1,0,1||-m 1 -i Source=none --frames 0 --window 360,10,361,11"
    "CheckerBoard 1 next to the centre line|net.sf.openfx.CheckerBoardPlugin|general|float|mean=0.1,0.1,0.1,1||-m 1 -i Source=none --frames 0 --window 359,10,360,11"
    # ColorBars 1: the 40 IRE panel (top left), opaque, in video range (0.0625 + 0.85546875 * 0.4)
    "ColorBars 1 40 IRE|net.sf.openfx.ColorBars|general|float|comps=RGBA+mean=0.405,0.405,0.405,1||-m 1 -i Source=none --frames 0 --window 10,400,30,420"
    # ColorBars 2 defaults: the -I patch and the white patch at the bottom (linear, alpha 0)
    "ColorBars -I|net.sf.openfx.ColorBars|general|float|tol=1e-4+mean=0,0.0048,0.048,0||-i Source=none --frames 0 --window 0,0,20,20"
    "ColorBars white|net.sf.openfx.ColorBars|general|float|tol=1e-4+mean=0.9911,0.9911,0.9911,0||-i Source=none --frames 0 --window 140,0,160,20"
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
    # Keyer (version 2), luminance key with the defaults (A, B, C, D = 0, 1, 1, 1): alpha is the
    # foreground key 0.625 (Intermediate output: the color is the source; no byte depth). Invert
    # (issue #280) inverts the key, before the Inside Mask (alpha >= 0.5 here) is applied, and is
    # ignored in None mode (no keying: alpha 0)
    "Keyer|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.625||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none"
    "Keyer invert|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.375||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none invert=1"
    "Keyer invert inside mask|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.5||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none invert=1"
    "Keyer invert None mode|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0||-i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none mode=3 invert=1"
    # Saturation (max - min) / max = 0.75, Min 0.2 and Max 0.8 of (0.2, 0.4, 0.8), version 2 only
    "Keyer saturation|net.sf.openfx.KeyerPlugin|general|short float|mean=0.2,0.4,0.8,0.75||-i Source=solid:0.2,0.4,0.8,1 -i Bg=none -i InM=none -i OutM=none mode=4"
    "Keyer min|net.sf.openfx.KeyerPlugin|general|short float|mean=0.2,0.4,0.8,0.2||-i Source=solid:0.2,0.4,0.8,1 -i Bg=none -i InM=none -i OutM=none mode=5"
    # Color mode, key color pure red (foreground key l = 1), set without resetting A, B, C, D: the key
    # color is transparent (alpha 0) and the alpha ramps down from 1 at 0 to 0 at l (0.5 at 0.5 gray)
    "Keyer key color red|net.sf.openfx.KeyerPlugin|general|short float|mean=1,0,0,0||-i Source=solid:1,0,0,1 -i Bg=none -i InM=none -i OutM=none mode=1 keyColor=1,0,0"
    "Keyer key color red gray|net.sf.openfx.KeyerPlugin|general|short float|mean=0.5,0.5,0.5,0.5||-i Source=solid:0.5,0.5,0.5,1 -i Bg=none -i InM=none -i OutM=none mode=1 keyColor=1,0,0"
    "Keyer max|net.sf.openfx.KeyerPlugin|general|short float|mean=0.2,0.4,0.8,0.8||-i Source=solid:0.2,0.4,0.8,1 -i Bg=none -i InM=none -i OutM=none mode=6"
    # version 1 (deprecated) keeps its polarity and defaults (A = 0.5, B = C = 1): the background
    # key 0.25, alpha 0.75; inverted, 0.25
    "Keyer 1|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.75||-m 1 -i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none"
    "Keyer 1 invert|net.sf.openfx.KeyerPlugin|general|short float|mean=0.625,0.625,0.625,0.25||-m 1 -i Source=solid:0.625,0.625,0.625,1 -i Bg=none -i InM=none -i OutM=none invert=1"
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
    # Clamp Alpha (issue #295) clamps alpha to [0, 1] before multiplying: 2 gives 1 and -0.5 gives 0;
    # with Invert, 1 - 2 = -1 is clamped to 0. It is on by default in Premult 3 (the highest
    # version, which hostDemo picks); Premult 2 (-m 2, obsolete) and Clamp Alpha off multiply
    # by the raw alpha (alpha 2 doubles the colour).
    "Premult alpha above 1|net.sf.openfx.Premult|filter|float|mean=0.25,0.5,0.75,2||-i Source=solid:0.25,0.5,0.75,2"
    "Premult 2 alpha above 1|net.sf.openfx.Premult|filter|float|mean=0.5,1,1.5,2||-m 2 -i Source=solid:0.25,0.5,0.75,2"
    "Premult no clamp alpha above 1|net.sf.openfx.Premult|filter|float|mean=0.5,1,1.5,2||-i Source=solid:0.25,0.5,0.75,2 clampAlpha=0"
    "Premult clamp alpha above 1|net.sf.openfx.Premult|filter|float|mean=0.25,0.5,0.75,2||-i Source=solid:0.25,0.5,0.75,2 clampAlpha=1"
    "Premult clamp alpha below 0|net.sf.openfx.Premult|filter|float|mean=0,0,0,-0.5||-i Source=solid:0.25,0.5,0.75,-0.5 clampAlpha=1"
    "Premult clamp alpha invert|net.sf.openfx.Premult|filter|float|mean=0,0,0,2||-i Source=solid:0.25,0.5,0.75,2 invert=1 clampAlpha=1"
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
    # AdjustRoD with negative amounts shrinks the RoD (#299): -100 canonical is 93.75 pixels in x, truncated to
    # 93 in version 2 (version 1: the RoD starts at 93.75, pixel 93 enclosing), and the window 0-100 of the ramp ((x + 0.5) / 720) renders the pixels 93
    # to 99, whose mean is 96.5 / 720 = 0.134 (pixels 0 to 99 without shrinking: 0.069)
    "AdjustRoD shrink|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=0.134,0.134,0.134,0.134||-i Source=ramp addPixels=-100,-100 boundary=0 --frames 0 --window 0,288,100,289"
    # mixed signs, shrunk in x and enlarged by 100 in y: in the added rows (out of the Source), Nearest repeats
    # the edge row of the shrunk image (the same mean), Black is transparent black
    "AdjustRoD shrink Nearest added rows|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=0.134,0.134,0.134,0.134||-i Source=ramp addPixels=-100,100 boundary=1 --frames 0 --window 0,-50,100,-49"
    "AdjustRoD shrink Black added rows|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=0,0,0,0||-i Source=ramp addPixels=-100,100 boundary=0 --frames 0 --window 0,-50,100,-49"
    # version 2 truncates -16 / PAR to 14 whole pixels (the PAR as a float), version 1 shrinks by 16 canonical, 15
    # pixels enclosing: the window 0-20 of the ramp renders the pixels 14 to 19 (mean 17 / 720) or 15 to 19 (17.5 / 720)
    "AdjustRoD shrink whole pixels|net.sf.openfx.AdjustRoDPlugin|filter|float|tol=0.0002+mean=0.023611,0.023611,0.023611,0.023611||-i Source=ramp addPixels=-16,-16 --frames 0 --window 0,288,20,289"
    "AdjustRoD v1 shrink|net.sf.openfx.AdjustRoDPlugin|filter|float|tol=0.0002+mean=0.024306,0.024306,0.024306,0.024306||-m 1 -i Source=ramp addPixels=-16,-16 --frames 0 --window 0,288,20,289"
    # the default amount: 25 in version 2 (23 pixels in x: the window -30 to -20 renders -23 to -21, the edge
    # pixels with Nearest), 0 (an identity) in version 1
    "AdjustRoD default|net.sf.openfx.AdjustRoDPlugin|filter|*|mean=1,1,1,1||-i Source=solid:1,1,1,1 --frames 0 --window -30,288,-20,289"
    "AdjustRoD v1 default|net.sf.openfx.AdjustRoDPlugin|filter|*|identity||-m 1 -i Source=solid:1,1,1,1 --frames 0"
    # Crop softness (#298), 4 at the row 102 of a crop from y = 100 (pixel centre at d = 2.5): version 2,
    # smoothstep((d + 0.5) / (4 + 1)) = smoothstep(0.6) = 0.648; version 1, 1 - 2 (1 - d/4)^2 = 0.71875
    "Crop softness|net.sf.openfx.CropPlugin|filter|*|mean=0.648,0.648,0.648,0.648||-i Source=solid:1,1,1,1 bottomLeft=100,100 size=400,300 softness=4 --frames 0 --window 300,102,301,103"
    "Crop v1 softness|net.sf.openfx.CropPlugin|filter|*|mean=0.719,0.719,0.719,0.719||-m 1 -i Source=solid:1,1,1,1 bottomLeft=100,100 size=400,300 softness=4 --frames 0 --window 300,102,301,103"
    # a fractional edge, y = 100.99, softness 1: the row 100 is in the RoD (enclosing), but its centre is outside
    # the crop rectangle (d = -0.49). Version 2: smoothstep(0.01 / 2), about 0; version 1 without Black Outside:
    # black (it gave 2 (d/1)^2 = 0.48 before 1.1)
    "Crop softness fractional edge|net.sf.openfx.CropPlugin|filter|*|mean=0,0,0,0||-i Source=solid:1,1,1,1 bottomLeft=100,100.99 size=400,300 softness=1 --frames 0 --window 300,100,301,101"
    "Crop v1 softness fractional edge|net.sf.openfx.CropPlugin|filter|*|mean=0,0,0,0||-m 1 -i Source=solid:1,1,1,1 bottomLeft=100,100.99 size=400,300 softness=1 --frames 0 --window 300,100,301,101"
    # CopyRectangle softness (#282), white A over transparent B, softness 4, at the row 101 of a rectangle from
    # y = 100 (d = 1.5): version 3, smoothstep((1.5 + 0.5) / 5) = smoothstep(0.4) = 0.352; version 2, 1 pixel
    # from the edge pixel: 1/4
    "CopyRectangle softness|net.sf.openfx.CopyRectanglePlugin|general|*|mean=0.352,0.352,0.352,0.352||-i A=solid:1,1,1,1 -i B=solid:0,0,0,0 -i Mask=none bottomLeft=100,100 size=400,300 softness=4 --frames 0 --window 300,101,301,102"
    "CopyRectangle v2 softness|net.sf.openfx.CopyRectanglePlugin|general|*|mean=0.25,0.25,0.25,0.25||-m 2 -i A=solid:1,1,1,1 -i B=solid:0,0,0,0 -i Mask=none bottomLeft=100,100 size=400,300 softness=4 --frames 0 --window 300,101,301,102"
    # the default rectangle: the middle half of the project in version 3 (pixels 180 to 540 in x), the whole
    # project in version 2
    "CopyRectangle default outside|net.sf.openfx.CopyRectanglePlugin|general|*|mean=0,0,0,0||-i A=solid:1,1,1,1 -i B=solid:0,0,0,0 -i Mask=none --frames 0 --window 100,288,101,289"
    "CopyRectangle default inside|net.sf.openfx.CopyRectanglePlugin|general|*|mean=1,1,1,1||-i A=solid:1,1,1,1 -i B=solid:0,0,0,0 -i Mask=none --frames 0 --window 360,288,361,289"
    "CopyRectangle v2 default|net.sf.openfx.CopyRectanglePlugin|general|*|mean=1,1,1,1||-m 2 -i A=solid:1,1,1,1 -i B=solid:0,0,0,0 -i Mask=none --frames 0 --window 100,288,101,289"
    # saturation 0: the Rec.709 luminance
    "ColorCorrect|net.sf.openfx.ColorCorrectPlugin|filter|*|mean=0.465,0.465,0.465,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none MasterSaturation=0,0,0,0"
    # Clamp Black (issue #272) is off by default from ColorCorrect 3, ColorMatrix 3, Saturation 3 and
    # HSVTool 2: the default node is an identity that keeps negative values; the obsolete versions
    # (-m) clamp them by default
    "ColorCorrect default negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|identity+mean=-0.25,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    "ColorCorrect 2 default clamp black|net.sf.openfx.ColorCorrectPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-m 2 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    "ColorMatrix default negatives|net.sf.openfx.ColorMatrixPlugin|filter|float|identity+mean=-0.25,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    "ColorMatrix 2 default clamp black|net.sf.openfx.ColorMatrixPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-m 2 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    "Saturation default negatives|net.sf.openfx.SaturationPlugin|filter|float|identity+mean=-0.25,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    "Saturation 2 default clamp black|net.sf.openfx.SaturationPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-m 2 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none"
    # ColorCorrect 3 Contrast (issue #289): a negative value is multiplied by 0.18^(1 - contrast),
    # -0.25 / 0.18 for contrast 2; the positive ones are (v / 0.18)^2 * 0.18. ColorCorrect 2 keeps -0.25
    "ColorCorrect contrast negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=-1.388889,1.388889,3.125,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none MasterContrast=2,2,2,2"
    # Gamma alone keeps the negative values (versions 3 and 2; 0.5 squares the positive ones); with Contrast 2, Gamma 0.5 squares the
    # factor: -0.25 / 0.18^2; Gamma 0 keeps them (the tests/nuke reference, the exponent 1 / gamma is 0);
    # a small gamma would give -inf: it gives -FLT_MAX
    "ColorCorrect gamma negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=-0.25,0.25,0.5625,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none MasterGamma=0.5,0.5,0.5,0.5"
    "ColorCorrect 2 gamma negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=-0.25,0.25,0.5625,1||-m 2 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none MasterGamma=0.5,0.5,0.5,0.5 clampBlack=0"
    "ColorCorrect contrast gamma negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-3+mean=-7.716049,1.929012,9.765625,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none MasterContrast=2,2,2,2 MasterGamma=0.5,0.5,0.5,0.5"
    "ColorCorrect contrast 2 gamma 0 negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=-2,-2,-2,1||-i Source=solid:-2,-2,-2,1 -i Mask=none MasterContrast=2,2,2,2 MasterGamma=0,0,0,0"
    "ColorCorrect contrast 2 gamma 0.01 negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e33+mean=-3.402823e38,-3.402823e38,-3.402823e38,1||-i Source=solid:-2,-2,-2,1 -i Mask=none MasterContrast=2,2,2,2 MasterGamma=0.01,0.01,0.01,0.01 --frames 0 --window 0,0,1,1"
    # ColorCorrect 3 Gamma on positive values (issue #289): Gamma 0 gives 1 (0 stays 0); a gamma below 0.001
    # is 0.001: 0.999^1000, 1, 1.001^1000; beyond the float range, FLT_MAX (not inf). ColorCorrect 2: Gamma 0
    # gives 0 below 1 and FLT_MAX above 1
    "ColorCorrect gamma 0 positives|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=0,1,1,1||-i Source=solid:0,0.5,2,1 -i Mask=none MasterGamma=0,0,0,0"
    "ColorCorrect 2 gamma 0 positives|net.sf.openfx.ColorCorrectPlugin|filter|float|mean=0,0,1,1||-m 2 -i Source=solid:0,0.5,1,1 -i Mask=none MasterGamma=0,0,0,0 clampBlack=0"
    "ColorCorrect 2 gamma 0 above 1|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e33+mean=3.402823e38,3.402823e38,3.402823e38,1||-m 2 -i Source=solid:2,2,2,1 -i Mask=none MasterGamma=0,0,0,0 clampBlack=0 --frames 0 --window 0,0,1,1"
    "ColorCorrect gamma 1e-6|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=2e-3+mean=0.367695,1,2.716924,1||-i Source=solid:0.999,1,1.001,1 -i Mask=none MasterGamma=1e-6,1e-6,1e-6,1e-6"
    "ColorCorrect gamma 0.001 above 1|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e33+mean=3.402823e38,3.402823e38,3.402823e38,1||-i Source=solid:2,2,2,1 -i Mask=none MasterGamma=0.001,0.001,0.001,0.001 --frames 0 --window 0,0,1,1"
    "ColorCorrect 2 contrast negatives|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=-0.25,1.388889,3.125,1||-m 2 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none MasterContrast=2,2,2,2 clampBlack=0"
    # ColorMatrix Invert (issue #291, off by default) applies the inverse matrix: with R' = R + G and
    # B' = 2 B, the inverse gives R = R' - G' = 0.75 - 0.5 and B = B' / 2 = 0.25 / 2 (without
    # Invert: 1.25 and 0.5). The inverse of the identity is an identity.
    "ColorMatrix invert|net.sf.openfx.ColorMatrixPlugin|filter|*|notidentity+mean=0.25,0.5,0.125,1||-i Source=solid:0.75,0.5,0.25,1 -i Mask=none outputRed=1,1,0,0 outputBlue=0,0,2,0 invert=1"
    "ColorMatrix invert identity|net.sf.openfx.ColorMatrixPlugin|filter|*|identity+mean=0.75,0.5,0.25,1||-i Source=solid:0.75,0.5,0.25,1 -i Mask=none clampBlack=0 invert=1"
    # B unchecked: the render keeps B, so the conversion is R' = R + B, B' = B, and its inverse
    # R = R' - B' = 0.75 - 0.25 (the inverse of the matrix with its B row 2 B would give R' - B' / 2)
    "ColorMatrix invert unprocessed|net.sf.openfx.ColorMatrixPlugin|filter|*|notidentity+mean=0.5,0.5,0.25,1||-i Source=solid:0.75,0.5,0.25,1 -i Mask=none outputRed=1,0,1,0 outputBlue=0,0,2,0 NatronOfxParamProcessB=0 invert=1"
    # a matrix so close to singular (determinant about 1e-17 in double) that its inverse is
    # meaningless: the render fails with an error (an expected error, not a bug)
    "ColorMatrix invert near-singular|net.sf.openfx.ColorMatrixPlugin|filter|float|mean=0,0,0,1|error:near-singular matrix~not invertible|-i Source=solid:0.75,0.5,0.25,1 -i Mask=none outputRed=0.1,0.2,0.3,0 outputGreen=0.4,0.5,0.6,0 outputBlue=0.7,0.8,0.9,0 invert=1"
    # Saturation -1 (issue #292): a check of the formula, 2 * luma - colour, luma (Rec. 709) 0.4649.
    # It does not test the new minimum: hostDemo does not clamp parameters to their range
    "Saturation negative formula|net.sf.openfx.SaturationPlugin|filter|*|notidentity+mean=0.6798,0.4298,0.1798,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none saturation=-1"
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
    # RGBToHSL outside [0,1] (#399, openfx-supportext#215): the hue comes from the unclamped channels,
    # the saturation from the colour clamped to [0,1]. (2, 0.5, 0): H = (0.5 / 2) / 6 (it was 0.917),
    # S 1, L 1; the grey (2, 2, 2): H 0, S 0 (it was 1), L 2
    "RGBToHSL out of range|net.sf.openfx.RGBToHSL|filter|float|tol=1e-5+mean=0.0416667,1,1,1||-i Source=solid:2,0.5,0,1"
    "RGBToHSL grey above 1|net.sf.openfx.RGBToHSL|filter|float|tol=1e-5+mean=0,0,2,1||-i Source=solid:2,2,2,1"
    # the conventions in the ColorTransform descriptions (#296): HSV of the values as given (no
    # transfer function), hue in [0,1): (0.25,0.5,0.125) -> H = (2 - 1/3)/6, S = 0.375/0.5, V = 0.5;
    # Y'CbCr in the video range: white -> Y' = 235/255 (with the rounded coefficients: 0.921745),
    # Cb = Cr = 128/255 (Cb 0.001 lower); 601 encodes with sRGB: grey 0.5 -> R'G'B' 0.7353570
    "RGBToHSV as given|net.sf.openfx.RGBToHSV|filter|float|tol=1e-5+mean=0.2777778,0.75,0.5,1||-i Source=solid:0.25,0.5,0.125,1"
    "RGBToYCbCr709 white|net.sf.openfx.RGBToYCbCr709|filter|float|tol=1e-5+mean=0.921745,0.5009608,0.5019608,1||-i Source=solid:1,1,1,1"
    "RGBToYCbCr601 grey|net.sf.openfx.RGBToYCbCr601|filter|float|tol=1e-5+mean=0.6944167,0.5019608,0.5019608,1||-i Source=solid:0.5,0.5,0.5,1"
    # the default Transfer Function (None, Rec.709) keeps the output: Y'PbPr 709 from the primaries
    # (Kr 0.2126390058, Kb 0.07219231534), Rec.709 OETF; the inverses give the colours back
    "RGBToYPbPr709 blue|net.sf.openfx.RGBToYPbPr709|filter|float|tol=1e-5+mean=0.0721923,0.5,-0.0458445,1||-i Source=solid:0,0,1,1"
    "YPbPrToRGB709 blue|net.sf.openfx.YPbPrToRGB709|filter|float|tol=1e-5+mean=0,0,1,1||-i Source=solid:0.0721923,0.5,-0.0458445,1"
    "HSVToRGB as given|net.sf.openfx.HSVToRGB|filter|float|tol=1e-5+mean=0.25,0.5,0.125,1||-i Source=solid:0.2777778,0.75,0.5,1"
    # Transfer Function sRGB (= 1): the conventions of the tests/nuke reference's Colorspace (#296). Linear
    # RGB is sRGB-encoded first: (0.25,0.5,0.125) -> R'G'B' (0.5370987,0.7353570,0.3896757)
    "RGBToHSV sRGB|net.sf.openfx.RGBToHSV|filter|float|tol=1e-5+mean=0.2619509,0.4715861,0.7353570,1||-i Source=solid:0.25,0.5,0.125,1 transferFunction=1"
    "RGBToHSL sRGB|net.sf.openfx.RGBToHSL|filter|float|tol=1e-5+mean=0.2619509,0.3958406,0.5619649,1||-i Source=solid:0.25,0.5,0.125,1 transferFunction=1"
    # max + min < 0: R'G'B' (-0.5,-0.25,0.25) is converted negated, (0.5,0.25,-0.25): H 1/9, S 0.75/0.5,
    # V -0.5; HSVToRGB inverts it
    "RGBToHSV sRGB negative|net.sf.openfx.RGBToHSV|filter|float|tol=1e-5+mean=0.1111111,1.5,-0.5,1||-i Source=solid:-0.03869969,-0.01934985,0.05087609,1 transferFunction=1"
    "HSVToRGB sRGB negative|net.sf.openfx.HSVToRGB|filter|float|tol=1e-5+mean=-0.03869969,-0.01934985,0.05087609,1||-i Source=solid:0.1111111,1.5,-0.5,1 transferFunction=1"
    # the saturation is not clamped: R'G'B' (1.5,1.2,0.8): L 1.15, S 0.7 / (2 - 2.3)
    "RGBToHSL sRGB above 1|net.sf.openfx.RGBToHSL|filter|float|tol=1e-5+mean=0.0952381,-2.3333333,1.15,1||-i Source=solid:2.537155,1.516837,0.6038273,1 transferFunction=1"
    "HSLToRGB sRGB above 1|net.sf.openfx.HSLToRGB|filter|float|tol=1e-5+mean=2.537155,1.516837,0.6038273,1||-i Source=solid:0.0952381,-2.3333333,1.15,1 transferFunction=1"
    # a negated colour whose L is below -0.5: R'G'B' (-2,-1,0.5) is converted as (2,1,-0.5), H 0.1, L -0.75,
    # and S is chosen by the signed L: 2.5 / (2 + -0.5), not 2.5 / (2 - 1.5); HSLToRGB inverts it
    "RGBToHSL sRGB negated L < -0.5|net.sf.openfx.RGBToHSL|filter|float|tol=1e-5+mean=0.1,1.6666667,-0.75,1||-i Source=solid:-0.1547988,-0.07739938,0.2140411,1 transferFunction=1"
    "HSLToRGB sRGB negated L < -0.5|net.sf.openfx.HSLToRGB|filter|float|tol=1e-5+mean=-0.1547988,-0.07739938,0.2140411,1||-i Source=solid:0.1,1.6666667,-0.75,1 transferFunction=1"
    # an infinite R' (12.92 * -3e38 overflows): S is 1 and H 0, not inf / inf (NaN); V and L are infinite,
    # so there is no mean, but a NaN pixel fails the render
    "RGBToHSV sRGB infinite|net.sf.openfx.RGBToHSV|filter|float|comps=RGBA||-i Source=solid:-3e38,0.5,0,1 transferFunction=1"
    "RGBToHSL sRGB infinite|net.sf.openfx.RGBToHSL|filter|float|comps=RGBA||-i Source=solid:-3e38,0.5,0,1 transferFunction=1"
    # Y'CbCr and Y'PbPr 709 with Transfer Function sRGB: the exact BT.709 matrix, Cb and Cr centred on 0.5;
    # white -> Y' 235/255; grey 0.5 -> 16/255 + 219/255 * 0.7353570; the sRGB encoding continues below 0
    "RGBToYCbCr709 sRGB white|net.sf.openfx.RGBToYCbCr709|filter|float|tol=1e-5+mean=0.9215686,0.5,0.5,1||-i Source=solid:1,1,1,1 transferFunction=1"
    "RGBToYCbCr709 sRGB grey|net.sf.openfx.RGBToYCbCr709|filter|float|tol=1e-5+mean=0.6942870,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 transferFunction=1"
    "RGBToYCbCr709 sRGB negative|net.sf.openfx.RGBToYCbCr709|filter|float|tol=1e-5+mean=-0.0048523,0.8853748,-0.8974637,1||-i Source=solid:-0.2,0.3,0.5,1 transferFunction=1"
    "YCbCrToRGB709 sRGB grey|net.sf.openfx.YCbCrToRGB709|filter|float|tol=1e-5+mean=0.5,0.5,0.5,1||-i Source=solid:0.6942870,0.5,0.5,1 transferFunction=1"
    "YCbCrToRGB709 sRGB negative|net.sf.openfx.YCbCrToRGB709|filter|float|tol=1e-5+mean=-0.2,0.3,0.5,1||-i Source=solid:-0.0048523,0.8853748,-0.8974637,1 transferFunction=1"
    "RGBToYPbPr709 sRGB blue|net.sf.openfx.RGBToYPbPr709|filter|float|tol=1e-5+mean=0.0722,0.5,-0.0458471,1||-i Source=solid:0,0,1,1 transferFunction=1"
    "YPbPrToRGB709 sRGB blue|net.sf.openfx.YPbPrToRGB709|filter|float|tol=1e-5+mean=0,0,1,1||-i Source=solid:0.0722,0.5,-0.0458471,1 transferFunction=1"
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
    # Roto: by default the shape (the Roto input) is drawn over the Source alpha only (Process A, the
    # R, G and B of the Source are kept); Premultiply multiplies the Source RGB by the shape alpha; Replace
    # draws on black in the drawn channels
    "Roto default alpha|net.sf.openfx.RotoPlugin|general|*|mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0 -i Roto=solid:1,1,1,0.5"
    "Roto premultiply|net.sf.openfx.RotoPlugin|general|*|mean=0.25,0.25,0.25,1||-i Source=solid:0.5,0.5,0.5,1 -i Roto=solid:1,1,1,0.5 premultiply=1"
    "Roto replace|net.sf.openfx.RotoPlugin|general|*|mean=0.5,0.5,0.5,0.5||-i Source=solid:0.5,0.5,0.5,1 -i Roto=solid:0.2,0.2,0.2,0.5 replace=1"
    # Premultiply with Replace and A: the output alpha is the shape alpha, the output is premultiplied
    "Roto premultiply replace|net.sf.openfx.RotoPlugin|general|*|premult=PreMultiplied+mean=0.25,0.25,0.25,0.5||-i Source=solid:0.5,0.5,0.5,1 -i Roto=solid:1,1,1,0.5 premultiply=1 replace=1"
    # outside the Roto RoD (no Roto image: transparent black), Replace clears the alpha and
    # Premultiply the RGB: not a pass-through
    "Roto replace outside RoD|net.sf.openfx.RotoPlugin|general|*|notidentity+mean=0.5,0.5,0.5,0||-i Source=solid:0.5,0.5,0.5,1 -i Roto=rod:100,100,200,200 replace=1 --frames 0 --window 0,0,4,4"
    "Roto premultiply outside RoD|net.sf.openfx.RotoPlugin|general|*|notidentity+mean=0,0,0,1||-i Source=solid:0.5,0.5,0.5,1 -i Roto=rod:100,100,200,200 premultiply=1 --frames 0 --window 0,0,4,4"
    # Clip To (default No Clip, as Roto 1.0) on a Source in the left half of the project (pixels 0 to 360,
    # transparent black) and a shape on the whole project (alpha 0.5), rendered across the Source edge:
    # the output bounds are the whole window with No Clip, Format and Union BBox+Format (3), and stop at the
    # Source edge with BBox and Intersect BBox+Format (4; the option names with a space are given by index)
    "Roto default no clip|net.sf.openfx.RotoPlugin|general|*|bounds=350,0,370,4+mean=0,0,0,0.5||-i Source=rod:0,0,384,576 -i Roto=solid:1,1,1,0.5 --frames 0 --window 350,0,370,4"
    "Roto clip to format|net.sf.openfx.RotoPlugin|general|*|bounds=350,0,370,4+mean=0,0,0,0.5||-i Source=rod:0,0,384,576 -i Roto=solid:1,1,1,0.5 clipType=Format --frames 0 --window 350,0,370,4"
    "Roto clip to union|net.sf.openfx.RotoPlugin|general|*|bounds=350,0,370,4+mean=0,0,0,0.5||-i Source=rod:0,0,384,576 -i Roto=solid:1,1,1,0.5 clipType=3 --frames 0 --window 350,0,370,4"
    "Roto clip to bbox|net.sf.openfx.RotoPlugin|general|*|bounds=350,0,360,4+mean=0,0,0,0.5||-i Source=rod:0,0,384,576 -i Roto=solid:1,1,1,0.5 clipType=BBox --frames 0 --window 350,0,370,4"
    "Roto clip to intersect|net.sf.openfx.RotoPlugin|general|*|bounds=350,0,360,4+mean=0,0,0,0.5||-i Source=rod:0,0,384,576 -i Roto=solid:1,1,1,0.5 clipType=4 --frames 0 --window 350,0,370,4"
    # ColorCorrect Mix Luminance 1 (issue #289): gain 2 doubles the color, and the same value is added to the
    # sRGB-encoded R, G and B so that the Rec. 709 luminance of the encoded values is the input one (0.70369);
    # the decoded values are (0.197154,0.516249,0.869185) (the tests/nuke reference)
    "ColorCorrect mix luminance|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=0.197154,0.516249,0.869185,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none MasterGain=2,2,2,2 mixLuminance=1"
    # Mix Luminance 0.5: half of that shift of the encoded values
    "ColorCorrect mix luminance 0.5|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=0.328364,0.734999,1.159637,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none MasterGain=2,2,2,2 mixLuminance=0.5"
    # ColorCorrect Test: the value of the tone range of the input luminance with the largest weight in red and
    # blue (shadows 0, midtones 0.25, highlights 1), the sum of the values times the weights in green:
    # midtones (luminance 0.465), shadows 0.741 and midtones 0.259 (0.03), midtones 0.352 and highlights 0.648 (0.8)
    "ColorCorrect test midtones|net.sf.openfx.ColorCorrectPlugin|filter|*|mean=0.25,0.25,0.25,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none test=1"
    "ColorCorrect test shadows/midtones|net.sf.openfx.ColorCorrectPlugin|filter|float|tol=1e-4+mean=0,0.064815,0,1||-i Source=solid:0.03,0.03,0.03,1 -i Mask=none test=1"
    "ColorCorrect test midtones/highlights|net.sf.openfx.ColorCorrectPlugin|filter|*|mean=1,0.736,1,1||-i Source=solid:0.8,0.8,0.8,1 -i Mask=none test=1"
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
    # PIK (version 2): the existing cases below turn RGBA Legal off (on by default since issue #281).
    # RGBA Legal: a color darker than the screen (0.1, 0.2, 0.8; Pick) has the key 0, but the alpha is
    # the luminance of the screen-subtracted color, 0.1526 (0 without it, and in version 1)
    "PIK RGBA legal|net.sf.openfx.PIK|general|float|mean=0.0753,0.1782,0.1268,0.1526||-i Fg=solid:0.1771,0.3818,0.9411,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8"
    "PIK RGBA legal off|net.sf.openfx.PIK|general|float|mean=0.0753,0.1782,0.1268,0||-i Fg=solid:0.1771,0.3818,0.9411,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 rgbal=0"
    "PIK 1 RGBA legal|net.sf.openfx.PIK|general|float|mean=0.0753,0.1782,0.1268,0||-m 1 -i Fg=solid:0.1771,0.3818,0.9411,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 rgbal=1"
    # Luminance Match: a color brighter than the screen is scaled to its luminance for the alpha
    # (0.5831 without it), but the screen subtraction uses the key; with Screen Range 0.5, a dark
    # color is divided by 0.5 (alpha 0, 0.0769 without it)
    "PIK luminance match|net.sf.openfx.PIK|general|float|mean=0.2683,0.2646,0.2665,0.7415||-i Fg=solid:0.31,0.348,0.6,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 lmEnable=1"
    "PIK screen range|net.sf.openfx.PIK|general|float|mean=0,0,0,0||-i Fg=solid:0,0,0.6,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 lmEnable=1 level=0.5"
    "PIK 1 luminance match|net.sf.openfx.PIK|general|float|mean=0.2683,0.2646,0.2665,0.5831||-m 1 -i Fg=solid:0.31,0.348,0.6,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 lmEnable=1"
    # Autolevels: on a blue screen, red is lowered to green for the alpha (1 without it); Magenta
    # keeps red up to blue; on a green screen, Yellow keeps red, halving the part above green
    "PIK autolevels|net.sf.openfx.PIK|general|float|mean=0.8592,0,0,0.6923||-i Fg=solid:0.89,0,0.2,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 autolevels=1"
    "PIK autolevels magenta|net.sf.openfx.PIK|general|float|mean=0.8438,0,0.2308,0.5385||-i Fg=solid:0.89,0,0.6,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 autolevels=1 magenta=1"
    "PIK autolevels green yellow|net.sf.openfx.PIK|general|float|mean=1.1777,0.5108,0.1438,0.8885||-i Fg=solid:1.2,0.6,0.155,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.2,0.8,0.1 autolevels=1 yellow=1"
    "PIK 1 autolevels|net.sf.openfx.PIK|general|float|mean=0.89,0,0.2,1||-m 1 -i Fg=solid:0.89,0,0.2,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.2,0.8 autolevels=1"
    # PIK: a soft screen replace color with zero Rec.709 luminance (was 0/0 = NaN where alpha
    # is raised to 1 by Clip White, or a huge color with FMA contraction, e.g. on arm64): white
    "PIK replace color lum 0|net.sf.openfx.PIK|general|float|comps=RGBA+mean=0.0045,0.0045,0.0045,1||-i Fg=solid:0,0,0.25,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0,0,1 screenClipMax=0.5 screenReplaceColor=0.072175,0,-0.2126729 rgbal=0"
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
    "PIK AP0 soft colors|net.sf.openfx.PIK|general|float|mean=0.4982,0.3321,1.4946,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=solid:0.15,0.65,0.2,1 -i Bg=none screenType=0 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenClipMin=0.1 screenClipMax=0.6 colorspace=2 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9 rgbal=0"
    # the same with screenType=2 (Pick) and Rec. 2020
    "PIK 1 Rec.2020 soft colors|net.sf.openfx.PIK|general|float|mean=0.2435,0.2943,0.1309,0.52||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK Rec.2020 soft colors|net.sf.openfx.PIK|general|float|mean=0.2319,0.29,0.128,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9 rgbal=0"
    # Rec. 709, or neutral (gray) replace colors: version 2 renders as version 1
    "PIK Rec.709 soft colors|net.sf.openfx.PIK|general|float|mean=0.2518,0.2974,0.133,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=0 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9 rgbal=0"
    "PIK AP0 gray soft colors|net.sf.openfx.PIK|general|float|mean=0.258,0.2649,0.2717,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 alphaBias=0.5,0.5,0.5 screenReplaceColor=0.7,0.7,0.7 insideReplaceColor=0.4,0.4,0.4 rgbal=0"
    # ACES AP0: bluish replace colors have a tiny or negative AP0 luminance (0,0.1,1: 6.7e-4;
    # 0,0.099,1 and 0,0,1: negative), which gave huge or negated colors (means of 14.5 and -219):
    # they are normalized with their Rec. 709 luminance, as in version 1
    "PIK AP0 bluish soft colors|net.sf.openfx.PIK|general|float|mean=0.0275,0.194,1.6536,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 screenReplaceColor=0,0.1,1 insideReplaceColor=0,0.099,1 rgbal=0"
    "PIK AP0 blue soft colors|net.sf.openfx.PIK|general|float|mean=0.0275,0.0343,3.2353,0.5||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=2 screenReplaceColor=0,0,1 insideReplaceColor=0,0,1 rgbal=0"
    # Use Bg Luminance and Chroma: version 2 converts Bg with the Colorspace too (version 1:
    # always Rec. 709), so that Rec. 2020 differs and Rec. 709 is the same
    "PIK 1 Rec.2020 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.4303,0.392,0.2097,0.52||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9"
    "PIK Rec.2020 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.4695,0.3811,0.1991,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=1 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9 rgbal=0"
    "PIK Rec.709 Bg lum/chroma|net.sf.openfx.PIK|general|float|mean=0.5012,0.3865,0.2059,0.52||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=solid:0.6,0.3,0.2,1 -i InM=solid:0.5,0.5,0.5,0.5 -i OutM=none screenType=2 color=0.1,0.9,0.15 screenClipMin=0.1 screenClipMax=0.6 colorspace=0 ubl=1 ubc=1 alphaBias=0.2,0.8,0.3 screenReplaceColor=0.8,0.3,0.2 insideReplaceColor=0.3,0.2,0.9 rgbal=0"
    # show=2 (Clean Plate) with a Pick screen: the screen color divided by the alpha bias, which
    # is always normalized with Rec. 709, so that all versions and colorspaces give the same output
    # Clean Plate with C disconnected and a screen type other than Pick: transparent black (#328)
    "PIK clean plate C disconnected|net.sf.openfx.PIK|general|float|notidentity+mean=0,0,0,0||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=1 show=2"
    "PIK 1 clean plate C disconnected|net.sf.openfx.PIK|general|float|notidentity+mean=0,0,0,0||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=1 show=2"
    "PIK 1 AP1 clean plate|net.sf.openfx.PIK|general|float|mean=0.3182,0.7158,0.3182,1||-m 1 -i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.9,0.15 colorspace=3 alphaBias=0.2,0.8,0.3 show=2"
    "PIK AP1 clean plate|net.sf.openfx.PIK|general|float|mean=0.3182,0.7158,0.3182,1||-i Fg=solid:0.12,0.8,0.18,1 -i PFg=none -i C=none -i Bg=none -i InM=none -i OutM=none screenType=2 color=0.1,0.9,0.15 colorspace=3 alphaBias=0.2,0.8,0.3 show=2 rgbal=0"
    # render scale 0.5 (the means are those of the full-scale renders), tiles
    # (--window: the same pixels as the full render), and a fractional time
    "Radial 2 scale 0.5|net.sf.openfx.Radial|general|*|mean=0.231,0.231,0.231,0.231||-m 2 -i Source=none -i Mask=none --scale 0.5"
    "Radial scale 0.5|net.sf.openfx.Radial|general|*|nonconst||-i Source=none -i Mask=none --scale 0.5"
    # Radial (softness 0) and Rectangle (a 200x200 shape at 100,100) over a solid Source: Invert draws Color 0 (black and
    # transparent) inside and Color 1 (white) outside; Premultiply sets the Source RGB to black outside of the
    # shape (with nothing drawn), or inside with Invert; Replace draws on black in the drawn channels (here inside
    # and outside). With Replace and an opaque Source, the output is premultiplied (UnPremultiplied without Replace).
    "Radial invert inside|net.sf.openfx.Radial|general|*|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 invert=1 --frames 0 --window 199,199,201,201"
    "Radial invert outside|net.sf.openfx.Radial|general|*|mean=1,1,1,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 invert=1 --frames 0 --window 400,400,410,410"
    "Radial premultiply outside|net.sf.openfx.Radial|general|*|notidentity+mean=0,0,0,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 color1=0,0,0,0 premultiply=1 --frames 0 --window 400,400,410,410"
    "Radial premultiply invert inside|net.sf.openfx.Radial|general|*|notidentity+mean=0,0,0,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 color1=0,0,0,0 premultiply=1 invert=1 --frames 0 --window 199,199,201,201"
    "Radial premultiply invert outside|net.sf.openfx.Radial|general|*|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 color1=0,0,0,0 premultiply=1 invert=1 --frames 0 --window 400,400,410,410"
    "Radial replace inside|net.sf.openfx.Radial|general|*|mean=0.2,0.2,0.2,0.5||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 color1=0.2,0.2,0.2,0.5 replace=1 --frames 0 --window 199,199,201,201"
    "Radial replace outside|net.sf.openfx.Radial|general|*|notidentity+mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=0 color1=0.2,0.2,0.2,0.5 replace=1 --frames 0 --window 400,400,410,410"
    "Radial premultiply replace opaque|net.sf.openfx.Radial|general|*|premult=PreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 premultiply=1 replace=1"
    "Radial premultiply opaque|net.sf.openfx.Radial|general|*|premult=UnPreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 premultiply=1"
    "Rectangle invert inside|net.sf.openfx.Rectangle|general|*|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 invert=1 --frames 0 --window 199,199,201,201"
    "Rectangle invert outside|net.sf.openfx.Rectangle|general|*|mean=1,1,1,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 invert=1 --frames 0 --window 400,400,410,410"
    "Rectangle premultiply outside|net.sf.openfx.Rectangle|general|*|notidentity+mean=0,0,0,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 color1=0,0,0,0 premultiply=1 --frames 0 --window 400,400,410,410"
    "Rectangle premultiply invert inside|net.sf.openfx.Rectangle|general|*|notidentity+mean=0,0,0,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 color1=0,0,0,0 premultiply=1 invert=1 --frames 0 --window 199,199,201,201"
    "Rectangle premultiply invert outside|net.sf.openfx.Rectangle|general|*|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 color1=0,0,0,0 premultiply=1 invert=1 --frames 0 --window 400,400,410,410"
    "Rectangle replace inside|net.sf.openfx.Rectangle|general|*|mean=0.2,0.2,0.2,0.5||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 color1=0.2,0.2,0.2,0.5 replace=1 --frames 0 --window 199,199,201,201"
    "Rectangle replace outside|net.sf.openfx.Rectangle|general|*|notidentity+mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 color1=0.2,0.2,0.2,0.5 replace=1 --frames 0 --window 400,400,410,410"
    "Rectangle premultiply replace opaque|net.sf.openfx.Rectangle|general|*|premult=PreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 premultiply=1 replace=1"
    "Rectangle premultiply opaque|net.sf.openfx.Rectangle|general|*|premult=UnPreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 premultiply=1"
    # Premultiply in the softness: the Source RGB times the shape weight (0.399 here, the output with Color 1 white)
    "Rectangle 2 softness weight|net.sf.openfx.Rectangle|general|float|mean=0.399,0.399,0.399,0.399||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 --frames 0 --window 110,199,111,201"
    "Rectangle softness weight|net.sf.openfx.Rectangle|general|float|mean=0.4224,0.4224,0.4224,0.4224||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 --frames 0 --window 110,199,111,201"
    # Falloff (#288), Rectangle 2: t = 16.75/37.5 = 0.4467 across the Softness (40 canonical = 37.5 pixels; x 110.5 -
    # 93.75 pixels): Linear t, Smooth0 t^2(2-t), Smooth1 t(1+t(1-t)), Smooth t^2(3-2t) (Quadratic, its default, 2t^2 =
    # 0.399 above). Rectangle 3 spans one more pixel: t = (16.75 + 0.5)/(37.5 + 1) = 0.4481, and Smooth is its default.
    "Rectangle 2 falloff linear|net.sf.openfx.Rectangle|general|float|mean=0.4467,0.4467,0.4467,0.4467+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=1 --frames 0 --window 110,199,111,201"
    "Rectangle falloff linear|net.sf.openfx.Rectangle|general|float|mean=0.4481,0.4481,0.4481,0.4481+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=1 --frames 0 --window 110,199,111,201"
    "Rectangle 2 falloff smooth0|net.sf.openfx.Rectangle|general|float|mean=0.3099,0.3099,0.3099,0.3099+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=2 --frames 0 --window 110,199,111,201"
    "Rectangle falloff smooth0|net.sf.openfx.Rectangle|general|float|mean=0.3116,0.3116,0.3116,0.3116+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=2 --frames 0 --window 110,199,111,201"
    "Rectangle 2 falloff smooth1|net.sf.openfx.Rectangle|general|float|mean=0.5571,0.5571,0.5571,0.5571+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=3 --frames 0 --window 110,199,111,201"
    "Rectangle falloff smooth1|net.sf.openfx.Rectangle|general|float|mean=0.5589,0.5589,0.5589,0.5589+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=3 --frames 0 --window 110,199,111,201"
    "Rectangle 2 falloff smooth|net.sf.openfx.Rectangle|general|float|mean=0.4203,0.4203,0.4203,0.4203+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=4 --frames 0 --window 110,199,111,201"
    "Rectangle falloff smooth|net.sf.openfx.Rectangle|general|float|mean=0.4224,0.4224,0.4224,0.4224+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 falloff=4 --frames 0 --window 110,199,111,201"
    # Radial, center (187, 199.5) and radius (93.75, 100) in pixels: Radial 2 takes pixel (187, 279) at the normalized
    # distance 0.79 (its corner nearest to the center, 278.5 - 199.5 = 79), t = (1 - 0.79) / Softness 1 = 0.21;
    # Radial 3 at its center, 0.795, t = 0.205: Quadratic 2t^2, Linear t, Smooth0 t^2(2-t), Smooth1 t(1+t(1-t)),
    # Smooth t^2(3-2t) (the default of Radial 3)
    "Radial 2 falloff quadratic|net.sf.openfx.Radial|general|float|mean=0.0882,0.0882,0.0882,0.0882+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 --frames 0 --window 187,279,188,280"
    "Radial falloff default|net.sf.openfx.Radial|general|float|mean=0.1088,0.1088,0.1088,0.1088+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 --frames 0 --window 187,279,188,280"
    "Radial falloff quadratic|net.sf.openfx.Radial|general|float|mean=0.0841,0.0841,0.0841,0.0841+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=0 --frames 0 --window 187,279,188,280"
    "Radial 2 falloff linear|net.sf.openfx.Radial|general|float|mean=0.21,0.21,0.21,0.21+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=1 --frames 0 --window 187,279,188,280"
    "Radial falloff linear|net.sf.openfx.Radial|general|float|mean=0.205,0.205,0.205,0.205+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=1 --frames 0 --window 187,279,188,280"
    "Radial 2 falloff smooth0|net.sf.openfx.Radial|general|float|mean=0.0789,0.0789,0.0789,0.0789+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=2 --frames 0 --window 187,279,188,280"
    "Radial falloff smooth0|net.sf.openfx.Radial|general|float|mean=0.0754,0.0754,0.0754,0.0754+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=2 --frames 0 --window 187,279,188,280"
    "Radial 2 falloff smooth1|net.sf.openfx.Radial|general|float|mean=0.2448,0.2448,0.2448,0.2448+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=3 --frames 0 --window 187,279,188,280"
    "Radial falloff smooth1|net.sf.openfx.Radial|general|float|mean=0.2384,0.2384,0.2384,0.2384+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=3 --frames 0 --window 187,279,188,280"
    # Invert swaps the colors: 1 - t with Linear (0.79 in Radial 2, 0.795 in Radial 3)
    "Radial 2 falloff linear invert|net.sf.openfx.Radial|general|float|mean=0.79,0.79,0.79,0.79+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=1 invert=1 --frames 0 --window 187,279,188,280"
    "Radial falloff linear invert|net.sf.openfx.Radial|general|float|mean=0.795,0.795,0.795,0.795+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=1 invert=1 --frames 0 --window 187,279,188,280"
    "Radial 2 falloff smooth|net.sf.openfx.Radial|general|float|mean=0.1138,0.1138,0.1138,0.1138+tol=0.0005||-m 2 -i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=4 --frames 0 --window 187,279,188,280"
    "Radial falloff smooth|net.sf.openfx.Radial|general|float|mean=0.1088,0.1088,0.1088,0.1088+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 falloff=4 --frames 0 --window 187,279,188,280"
    "Rectangle 2 premultiply softness|net.sf.openfx.Rectangle|general|*|mean=0.1995,0.1995,0.1995,1||-m 2 -i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=40 color1=0,0,0,0 premultiply=1 --frames 0 --window 110,199,111,201"
    # Rectangle 3 with Corner Radius 40 (37.5 x 40 pixels): the straight edges keep the Softness of the rectangle
    # (pixel 110 at mid-height: 0.4224, as without a radius); in the corner, pixel (115, 120) is at d = 0.6435 of the
    # corner ellipse (u = (37.5 - 21.75)/37.5, v = (40 - 20.5)/40), 13.89 pixels inside it along its radius, over a
    # Softness of 38.88 pixels there (37.5 and 40 weighted by |u| r.x and |v| r.y): Smooth((13.89 + 0.5)/(38.88 + 1))
    # = 0.2965, less than the weight of the straight edges (0.3192)
    "Rectangle corner radius edge|net.sf.openfx.Rectangle|general|float|mean=0.4224,0.4224,0.4224,0.4224+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 cornerRadius=40,40 --frames 0 --window 110,199,111,201"
    "Rectangle corner radius corner|net.sf.openfx.Rectangle|general|float|mean=0.2965,0.2965,0.2965,0.2965+tol=0.0005||-i Source=none -i Mask=none bottomLeft=100,100 size=200,200 softness=40 cornerRadius=40,40 --frames 0 --window 115,120,116,121"
    "Rectangle premultiply softness|net.sf.openfx.Rectangle|general|*|mean=0.2112,0.2112,0.2112,1||-i Source=solid:0.5,0.5,0.5,1 -i Mask=none bottomLeft=100,100 size=200,200 softness=40 color1=0,0,0,0 premultiply=1 --frames 0 --window 110,199,111,201"
    # Extent = Format with no Source: the RoD is the format, also when only Color 0 is not black (here with Invert)
    "Radial 2 format invert|net.sf.openfx.Radial|general|*|nonconst+mean=0.769,0.769,0.769,0.769||-m 2 -i Source=none -i Mask=none extent=0 invert=1"
    "Radial format invert|net.sf.openfx.Radial|general|*|nonconst||-i Source=none -i Mask=none extent=0 invert=1"
    # Rectangle: the shape is the format, with a softness so that Invert draws Color 1 near its edges (it was an
    # empty RoD, a constant output)
    "Rectangle format invert|net.sf.openfx.Rectangle|general|*|nonconst||-i Source=none -i Mask=none extent=0 softness=50 invert=1"
    # a coloured transparent Color 0 (red) is drawn outside: the output is not premultiplied
    "Rectangle premultiply replace red color0|net.sf.openfx.Rectangle|general|*|premult=UnPreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 color0=1,0,0,0 premultiply=1 replace=1"
    "Radial premultiply replace red color0|net.sf.openfx.Radial|general|*|premult=UnPreMultiplied+notidentity||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 color0=1,0,0,0 premultiply=1 replace=1"
    # with Invert, Color 0 (red) is drawn inside, where the alpha is 0: not premultiplied; a red Color 1 is drawn
    # outside, where the alpha is 1: premultiplied
    "Radial premultiply replace invert red color0|net.sf.openfx.Radial|general|*|premult=UnPreMultiplied||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 color0=1,0,0,0 premultiply=1 replace=1 invert=1"
    "Radial premultiply replace invert red color1|net.sf.openfx.Radial|general|*|premult=PreMultiplied||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 color1=1,0,0,1 premultiply=1 replace=1 invert=1"
    # R is not drawn: the red of Color 0 does not count
    "Radial premultiply replace red color0 R off|net.sf.openfx.Radial|general|*|premult=PreMultiplied||-i Source=file:autocrop.pfm#opaque -i Mask=none bottomLeft=10,8 size=32,16 color0=1,0,0,0 premultiply=1 replace=1 NatronOfxParamProcessR=0"
    "Transform scale 0.5|net.sf.openfx.TransformPlugin|filter|float|mean=0.31,0.31,0.31,0.31||-i Source=checker:32 rotate=30 scale=0.8,0.8 --scale 0.5"
    # Hosts without normalised defaults (OFX 1.1 hosts such as VEGAS, and DaVinci Resolve; hostDemo
    # --no-normalised-defaults): the default center (0.5, 0.5) is moved to the middle of the 768x576
    # (canonical) project, not left at the bottom left corner, and Center Changed stays off (issue #66,
    # SupportExt ofxsTransformDenormaliseDefaults). hostDemo sends no instanceChanged from
    # createInstance, so the Center Changed restore is not exercised here. A host with normalised
    # defaults gives the same center. A center set by the user stays (hostDemo sets it after
    # createInstance, as a user edit, so Center Changed is on).
    "Transform center, no normalised defaults|net.sf.openfx.TransformPlugin|filter|float|value:center=384,288+value:transformCenterChanged=0||-i Source=checker:32 --frames 0 --no-normalised-defaults"
    "Transform center, normalised defaults|net.sf.openfx.TransformPlugin|filter|float|value:center=384,288+value:transformCenterChanged=0||-i Source=checker:32 --frames 0"
    "Transform center set, no normalised defaults|net.sf.openfx.TransformPlugin|filter|float|value:center=100,50+value:transformCenterChanged=1||-i Source=checker:32 center=100,50 --frames 0 --no-normalised-defaults"
    "TransformMasked center, no normalised defaults|net.sf.openfx.TransformMaskedPlugin|general|float|value:center=384,288+value:transformCenterChanged=0||-i Source=checker:32 -i Mask=none --frames 0 --no-normalised-defaults"
    "DirBlur center, no normalised defaults|net.sf.openfx.DirBlur|general|float|value:center=384,288+value:transformCenterChanged=0||-i Source=checker:32 -i Mask=none --frames 0 --no-normalised-defaults"
    "GodRays center, no normalised defaults|net.sf.openfx.GodRays|filter|float|value:center=384,288+value:transformCenterChanged=0||-i Source=checker:32 --frames 0 --no-normalised-defaults"
    "GodRays center set, no normalised defaults|net.sf.openfx.GodRays|filter|float|value:center=100,50+value:transformCenterChanged=1||-i Source=checker:32 center=100,50 --frames 0 --no-normalised-defaults"
    # CornerPin "Set to input format" (#270): the "from" points are the input format (hostDemo's
    # PAL SD, 768x576 canonical; the project on OFX 1.4 hosts, the same here), not its RoD (rod:);
    # "Set to input rod" still copies the RoD
    "CornerPin set to input format|net.sf.openfx.CornerPinPlugin|filter|float|value:from1=0,0+value:from2=768,0+value:from3=768,576+value:from4=0,576||-i Source=rod:10,20,300,200 -s setToInputFormat --frames 0"
    # a 64x48 PFM at (100,50): its format (in canonical coordinates, x times the PAR 768/720) is not the project's
    "CornerPin set to input format, file|net.sf.openfx.CornerPinPlugin|filter|float|value:from1=106.666667,50+value:from2=174.933333,50+value:from3=174.933333,98+value:from4=106.666667,98||-i Source=file:autocrop.pfm@100,50 -s setToInputFormat --frames 0"
    "CornerPin set to input rod|net.sf.openfx.CornerPinPlugin|filter|float|value:from1=10,20+value:from2=300,20+value:from3=300,200+value:from4=10,200||-i Source=rod:10,20,300,200 -s setToInputRod --frames 0"
    # Lanczos4, Lanczos6, Sinc4 (#270): the impulse translated by 0.25 in y (the x weights are 1 and 0),
    # so each output pixel is one weight of the kernel at d = 0.75, divided by the sum of the
    # truncated kernel: Lanczos4 -0.08388 0.86861 0.233 -0.01773 (rows 31-34),
    # Lanczos6 0.03011 -0.13327 0.89277 0.27101 -0.068 0.00738 (rows 30-35), Sinc4 -0.20192 1.00962
    # 0.33654 -0.14423 (rows 31-34); with Clamp, each 1D pass is clamped to its two nearest pixels
    "Transform Lanczos4 impulse|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=0.86861,0.86861,0.86861,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Lanczos4 --frames 0 --window 32,32,33,33"
    "Transform Lanczos4 impulse row 31|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=-0.08388,-0.08388,-0.08388,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Lanczos4 --frames 0 --window 32,31,33,32"
    "Transform Lanczos6 impulse|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=0.89277,0.89277,0.89277,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Lanczos6 --frames 0 --window 32,32,33,33"
    "Transform Lanczos6 impulse row 30|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=0.03011,0.03011,0.03011,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Lanczos6 --frames 0 --window 32,30,33,31"
    "Transform Sinc4 impulse|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=1.00962,1.00962,1.00962,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Sinc4 --frames 0 --window 32,32,33,33"
    "Transform Sinc4 impulse row 34|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=-0.14423,-0.14423,-0.14423,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Sinc4 --frames 0 --window 32,34,33,35"
    "Transform Sinc4 clamp impulse|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=1,1,1,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Sinc4 clamp=1 --frames 0 --window 32,32,33,33"
    "Transform Sinc4 clamp impulse row 31|net.sf.openfx.TransformPlugin|filter|float|tol=1e-4+mean=0,0,0,1||-i Source=file:kimpulse.pfm translate=0,0.25 filter=Sinc4 clamp=1 --frames 0 --window 32,31,33,32"
    # a constant image stays constant (the weights sum to 1), also through CornerPin, Card3D and Reformat
    "Transform Lanczos6 rotate solid|net.sf.openfx.TransformPlugin|filter|*|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 rotate=30 filter=Lanczos6 --frames 0 --window 200,200,400,300"
    "CornerPin Sinc4 solid|net.sf.openfx.CornerPinPlugin|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 filter=Sinc4 to2=700,30 --frames 0 --window 200,200,400,300"
    "Card3D Lanczos4 solid|net.sf.openfx.Card3D|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 filter=Lanczos4 cardRotate=0,0,10 --frames 0 --window 300,250,400,300"
    "Reformat Lanczos6 scale 0.5 solid|net.sf.openfx.Reformat|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 reformatType=Scale reformatScale=0.5,0.5 filter=Lanczos6 --frames 0 --window 100,100,200,200"
    # Prefilter Downscaling (#270): a 1-pixel checker scaled by 0.4 aliases with the default
    # supersampling (0.474 to 0.526); with the kernel widened by 2.5, it is uniform grey (0.4996 to 0.5004)
    "Transform prefilter scale 0.4 checker|net.sf.openfx.TransformPlugin|filter|float|range=0.499,0.501||-i Source=checker:1 scale=0.4,0.4 prefilter=1 --frames 0 --window 300,250,340,290"
    "Transform no prefilter scale 0.4 checker|net.sf.openfx.TransformPlugin|filter|float|tol=0.005+max=0.5263,0.5263,0.5263,0.5263||-i Source=checker:1 scale=0.4,0.4 --frames 0 --window 300,250,340,290"
    "Reformat prefilter scale 0.4 checker|net.sf.openfx.Reformat|filter|float|range=0.499,0.501||-i Source=checker:1 reformatType=Scale reformatScale=0.4,0.4 prefilter=1 --frames 0 --window 100,100,140,140"
    "Transform prefilter tile|net.sf.openfx.TransformPlugin|filter|float|nonconst+tile||-i Source=checker:4 scale=0.3,0.4 rotate=20 filter=Lanczos4 prefilter=1 --frames 0 --window 250,200,450,330"
    # tiled renders are the same as the full render with the prefilter (the RoI covers the widened
    # kernel): anisotropic scale and rotation with the PAR of the PAL input, a perspective CornerPin,
    # DirBlur (always the blurred path), motion blur, Lanczos6 near the image edge, render scale 0.5
    # (the RoI divides by it), Reformat and Card3D (projective: the RoI takes the 16x cap)
    "Transform prefilter scale 0.5 tile|net.sf.openfx.TransformPlugin|filter|float|nonconst+tile||-i Source=checker:3 scale=0.3,0.4 rotate=30 filter=Lanczos4 prefilter=1 --scale 0.5 --frames 0 --window 140,110,210,165"
    "Reformat prefilter tile|net.sf.openfx.Reformat|filter|float|nonconst+tile||-i Source=checker:3 reformatType=Scale reformatScale=0.3,0.45 filter=Lanczos6 prefilter=1 --frames 0 --window 40,40,160,140"
    "Card3D prefilter tile|net.sf.openfx.Card3D|filter|float|nonconst+tile||-i Source=checker:3 cardUniformScale=0.4 cardRotate=0,25,10 prefilter=1 --frames 0 --window 300,220,420,330"
    "Transform prefilter rotate scale tile|net.sf.openfx.TransformPlugin|filter|float|nonconst+tile||-i Source=checker:3 scale=0.25,0.6 rotate=45 filter=Cubic prefilter=1 --frames 0 --window 280,220,420,330"
    "CornerPin prefilter perspective tile|net.sf.openfx.CornerPinPlugin|filter|float|nonconst+tile||-i Source=checker:3 to1=300,200 to2=420,180 to3=460,330 to4=330,300 filter=Lanczos4 prefilter=1 --frames 0 --window 320,210,440,300"
    "DirBlur prefilter tile|net.sf.openfx.DirBlur|filter|float|nonconst+tile||-i Source=checker:3 scale=0.3,0.3 rotate=20 prefilter=1 --frames 0 --window 280,220,420,330"
    "Transform prefilter motion blur tile|net.sf.openfx.TransformPlugin|filter|float|nonconst+tile||-i Source=checker:3 -s scale@0=0.3,0.3 -s scale@1=0.2,0.2 -s rotate@0=0 -s rotate@1=10 motionBlur=1 prefilter=1 --frames 0 --window 280,220,420,330"
    "Transform Lanczos6 edge tile|net.sf.openfx.TransformPlugin|filter|float|nonconst+tile||-i Source=checker:5 translate=3,2.5 rotate=10 filter=Lanczos6 --frames 0 --window 0,0,120,90"
    "CImgBlur scale 0.5|net.sf.cimg.CImgBlur|filter|float|mean=0.497,0.497,0.497,0.497||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 --scale 0.5"
    # filter=2: Box (FIR). The default IIR filters have an infinite support, so
    # their tiles differ slightly (up to 0.003) from the full render.
    "CImgBlur tile|net.sf.cimg.CImgBlur|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 filter=2 --frames 0 --window 100,100,300,250"
    # Mirror (#301): Source is a 768x100 strip (canonical; its pixels are 0) at y 100 to 200 of the 720x576 PAL
    # format: flipped around the format, it goes to y 576-200 to 576-100; around its RoD (the default), it stays.
    "Mirror flip around format|net.sf.openfx.Mirror|filter|float|bounds=0,376,720,476||-i Source=rod:0,100,768,200 flip=1 mirrorAround=0 --frames 0"
    "Mirror flip around RoD|net.sf.openfx.Mirror|filter|float|bounds=0,100,720,200||-i Source=rod:0,100,768,200 flip=1 --frames 0"
    # autocrop.pfm (64x48, its format in hostDemo) flopped: its white rectangle (x 10 to 42, y 8 to 24) goes to
    # x 22 to 54 (64 - 42 to 64 - 10)
    "Mirror flop around format|net.sf.openfx.Mirror|filter|*|mean=1,1,1,1||-i Source=file:autocrop.pfm flop=1 mirrorAround=0 --frames 0 --window 50,10,52,12"
    "Mirror flop around format outside|net.sf.openfx.Mirror|filter|*|mean=0,0,0,1||-i Source=file:autocrop.pfm flop=1 mirrorAround=0 --frames 0 --window 12,10,14,12"
    # a 96x576 strip (canonical) at x 96 to 192 of the 768x576 PAL format, flopped: x 768-192 to 768-96, i.e. pixels
    # 540 to 630 (x 720/768); around its RoD: pixels 90 to 180
    "Mirror flop strip around format|net.sf.openfx.Mirror|filter|float|bounds=540,0,630,576||-i Source=rod:96,0,192,576 flop=1 mirrorAround=0 --frames 0"
    "Mirror flop strip around RoD|net.sf.openfx.Mirror|filter|float|bounds=90,0,180,576||-i Source=rod:96,0,192,576 flop=1 --frames 0"
    # render scale 0.5, mirror65.pfm (65x8 pixels at that scale, white at x 0 to 9): flopped, pixel x goes to 64 - x,
    # so the white columns are 55 to 64. This checks the axis at a render scale with an odd image width; the format
    # (130 pixels at scale 1) still falls on whole pixels there, so the processor's black fill beyond the source is
    # not reached
    "Mirror flop around format, scale 0.5|net.sf.openfx.Mirror|filter|*|mean=1,1,1,1||-i Source=file:mirror65.pfm flop=1 mirrorAround=0 --scale 0.5 --frames 0 --window 60,2,62,4"
    "Mirror flop around format, scale 0.5, outside|net.sf.openfx.Mirror|filter|*|mean=0,0,0,1||-i Source=file:mirror65.pfm flop=1 mirrorAround=0 --scale 0.5 --frames 0 --window 52,2,54,4"
    # Merge 3 (#277) matches the tests/nuke reference (MergeV3 in Merge.cpp); Merge 2 (and 1) keep their results.
    # Alpha masking, multiply, A (0.4, a 0.5) and B (0.3, b 0.6): A(1-b) + B(1-a) + AB = 0.16 + 0.15 + 0.12 = 0.43,
    # alpha a+b-ab = 0.8 (Merge 2: AB = 0.12); B transparent: A, unchanged (Merge 2: 0)
    # Merge 1 and 2 on Alpha-only images: B's R checkbox controls the alpha; with it unchecked, A over B
    # is A's alpha, 0.5 (#328). Merge 3 uses B's A checkbox.
    "Merge 2 Alpha-only B R off|net.sf.openfx.MergePlugin|filter|float|notidentity+comps=Alpha+mean=0.5||-m 2 -i A=solid:0.4,0.4,0.4,0.5#alpha -i Source=solid:0.3,0.3,0.3,0.6#alpha BChannelsR=0 -i Mask=none --frames 0"
    "Merge 1 Alpha-only B R off|net.sf.openfx.MergePlugin|filter|float|notidentity+comps=Alpha+mean=0.5||-m 1 -i A=solid:0.4,0.4,0.4,0.5#alpha -i Source=solid:0.3,0.3,0.3,0.6#alpha BChannelsR=0 -i Mask=none --frames 0"
    # A disconnected, Alpha-only: Merge 2 uses B's R checkbox for the alpha (off: 0); Merge 3 its A checkbox (B)
    "Merge 2 Alpha-only A disconnected B R off|net.sf.openfx.MergePlugin|filter|float|notidentity+comps=Alpha+mean=0||-m 2 -i A=none -i Source=solid:0.3,0.3,0.3,0.6#alpha BChannelsR=0 -i Mask=none --frames 0"
    "Merge Alpha-only A disconnected B R off|net.sf.openfx.MergePlugin|filter|float|identity+comps=Alpha+mean=0.6||-i A=none -i Source=solid:0.3,0.3,0.3,0.6#alpha BChannelsR=0 -i Mask=none --frames 0"
    "Merge Alpha-only B R off|net.sf.openfx.MergePlugin|filter|float|notidentity+comps=Alpha+mean=0.8||-i A=solid:0.4,0.4,0.4,0.5#alpha -i Source=solid:0.3,0.3,0.3,0.6#alpha BChannelsR=0 -i Mask=none --frames 0"
    "Merge alpha masking multiply|net.sf.openfx.MergePlugin|filter|float|mean=0.43,0.43,0.43,0.8||-i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0.3,0.3,0.3,0.6 operation=multiply screenAlpha=1 -i Mask=none --frames 0"
    "Merge 2 alpha masking multiply|net.sf.openfx.MergePlugin|filter|float|mean=0.12,0.12,0.12,0.8||-m 2 -i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0.3,0.3,0.3,0.6 operation=multiply screenAlpha=1 -i Mask=none --frames 0"
    "Merge 1 alpha masking multiply|net.sf.openfx.MergePlugin|filter|float|mean=0.12,0.12,0.12,0.8||-m 1 -i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0.3,0.3,0.3,0.6 operation=multiply screenAlpha=1 -i Mask=none --frames 0"
    "Merge alpha masking multiply, B transparent|net.sf.openfx.MergePlugin|filter|float|mean=0.4,0.4,0.4,0.5||-i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0,0,0,0 operation=multiply screenAlpha=1 -i Mask=none --frames 0"
    # overlay, the same inputs: A(1-b) + B(1-a) + T, with T = ab - 2(a-A)(b-B) = 0.3 - 2 x 0.1 x 0.3 = 0.24 (2B = b):
    # 0.16 + 0.15 + 0.24 = 0.55 (Merge 2: overlay(0.4, 0.3) = 2AB = 0.24), alpha 0.8
    "Merge alpha masking overlay|net.sf.openfx.MergePlugin|filter|float|mean=0.55,0.55,0.55,0.8||-i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0.3,0.3,0.3,0.6 operation=overlay screenAlpha=1 -i Mask=none --frames 0"
    "Merge 2 alpha masking overlay|net.sf.openfx.MergePlugin|filter|float|mean=0.24,0.24,0.24,0.8||-m 2 -i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0.3,0.3,0.3,0.6 operation=overlay screenAlpha=1 -i Mask=none --frames 0"
    "Merge 2 alpha masking multiply, B transparent|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,0.5||-m 2 -i A=solid:0.4,0.4,0.4,0.5 -i Source=solid:0,0,0,0 operation=multiply screenAlpha=1 -i Mask=none --frames 0"
    # over, a = 1.44 clamped to 1 in "A+B(1-a)": A (0.5), and alpha 1.44 (Merge 2: 0.5 + 0.25(1 - 1.44) = 0.39, alpha 1)
    "Merge over alpha above 1|net.sf.openfx.MergePlugin|filter|float|mean=0.5,0.5,0.5,1.44||-i A=solid:0.5,0.5,0.5,1.44 -i Source=solid:0.25,0.25,0.25,1 -i Mask=none --frames 0"
    "Merge 2 over alpha above 1|net.sf.openfx.MergePlugin|filter|float|mean=0.39,0.39,0.39,1||-m 2 -i A=solid:0.5,0.5,0.5,1.44 -i Source=solid:0.25,0.25,0.25,1 -i Mask=none --frames 0"
    "Merge 1 over alpha above 1|net.sf.openfx.MergePlugin|filter|float|mean=0.39,0.39,0.39,1||-m 1 -i A=solid:0.5,0.5,0.5,1.44 -i Source=solid:0.25,0.25,0.25,1 -i Mask=none --frames 0"
    # divide, "A/B if B>0, otherwise A": B = 0 gives A (Merge 2: 0); alpha 1/1
    "Merge divide by 0|net.sf.openfx.MergePlugin|filter|float|mean=0.5,0.5,0.5,1||-i A=solid:0.5,0.5,0.5,1 -i Source=solid:0,0,0,1 operation=divide -i Mask=none --frames 0"
    "Merge 2 divide by 0|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,1||-m 2 -i A=solid:0.5,0.5,0.5,1 -i Source=solid:0,0,0,1 operation=divide -i Mask=none --frames 0"
    "Merge 1 divide by 0|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,1||-m 1 -i A=solid:0.5,0.5,0.5,1 -i Source=solid:0,0,0,1 operation=divide -i Mask=none --frames 0"
    # soft-light, 2AB+BB(1-AB) if AB<1, otherwise 2AB: A = B = 0.5 gives 0.5 + 0.25 x 0.75 = 0.6875, alpha (AB = 1) 2
    # (Merge 2, W3C: 0.5, alpha 1)
    "Merge soft-light|net.sf.openfx.MergePlugin|filter|float|mean=0.6875,0.6875,0.6875,2||-i A=solid:0.5,0.5,0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=soft-light -i Mask=none --frames 0"
    "Merge 2 soft-light|net.sf.openfx.MergePlugin|filter|float|mean=0.5,0.5,0.5,1||-m 2 -i A=solid:0.5,0.5,0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=soft-light -i Mask=none --frames 0"
    "Merge 1 soft-light|net.sf.openfx.MergePlugin|filter|float|mean=0.5,0.5,0.5,1||-m 1 -i A=solid:0.5,0.5,0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=soft-light -i Mask=none --frames 0"
    # outside [0, 1]: screen A+B-AB if 0<A<1 and 0<B<1, otherwise max(A,B), A 2, B 0.5: 2 (Merge 2: 1.5);
    # hard-light 1-2(1-A)(1-B) (2A >= 1) with A,B <= 1, A 0.75, B 1.5: 1 (Merge 2: screen(2A-1, B) = 1.25), alpha 1;
    # overlay, the same with A and B swapped, A = B = 1.5: 1 (Merge 2: max(2B-1, A) = 2);
    # geometric 2AB/(A+B) if A,B>0, otherwise 0, A 1, B -0.5: 0 (Merge 2: -2); color-dodge 1 if A>=1, A 1.5,
    # B 0.5: 1 (Merge 2: A); color-burn 0 if A<=0, A -0.5, B 0.5: 0 (Merge 2: A); exclusion A+B-2AB if A,B>=0,
    # otherwise A+B, A -0.5, B 0.5: 0 (Merge 2: A+B-2AB = 0.5), alpha 1+1-2 = 0
    "Merge screen above 1|net.sf.openfx.MergePlugin|filter|float|mean=2,2,2,1||-i A=solid:2,2,2,1 -i Source=solid:0.5,0.5,0.5,1 operation=screen -i Mask=none --frames 0"
    "Merge 2 screen above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.5,1.5,1.5,1||-m 2 -i A=solid:2,2,2,1 -i Source=solid:0.5,0.5,0.5,1 operation=screen -i Mask=none --frames 0"
    "Merge hard-light above 1|net.sf.openfx.MergePlugin|filter|float|mean=1,1,1,1||-i A=solid:0.75,0.75,0.75,1 -i Source=solid:1.5,1.5,1.5,1 operation=hard-light -i Mask=none --frames 0"
    "Merge overlay above 1|net.sf.openfx.MergePlugin|filter|float|mean=1,1,1,1||-i A=solid:1.5,1.5,1.5,1 -i Source=solid:1.5,1.5,1.5,1 operation=overlay -i Mask=none --frames 0"
    "Merge geometric negative|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,1||-i A=solid:1,1,1,1 -i Source=solid:-0.5,-0.5,-0.5,1 operation=geometric -i Mask=none --frames 0"
    "Merge color-dodge above 1|net.sf.openfx.MergePlugin|filter|float|mean=1,1,1,1||-i A=solid:1.5,1.5,1.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-dodge -i Mask=none --frames 0"
    "Merge color-burn negative|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,1||-i A=solid:-0.5,-0.5,-0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-burn -i Mask=none --frames 0"
    "Merge exclusion negative|net.sf.openfx.MergePlugin|filter|float|mean=0,0,0,0||-i A=solid:-0.5,-0.5,-0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=exclusion -i Mask=none --frames 0"
    "Merge 2 exclusion negative|net.sf.openfx.MergePlugin|filter|float|mean=0.5,0.5,0.5,0||-m 2 -i A=solid:-0.5,-0.5,-0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=exclusion -i Mask=none --frames 0"
    "Merge 2 hard-light above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.25,1.25,1.25,1||-m 2 -i A=solid:0.75,0.75,0.75,1 -i Source=solid:1.5,1.5,1.5,1 operation=hard-light -i Mask=none --frames 0"
    "Merge 2 overlay above 1|net.sf.openfx.MergePlugin|filter|float|mean=2,2,2,1||-m 2 -i A=solid:1.5,1.5,1.5,1 -i Source=solid:1.5,1.5,1.5,1 operation=overlay -i Mask=none --frames 0"
    "Merge 2 geometric negative|net.sf.openfx.MergePlugin|filter|float|mean=-2,-2,-2,1||-m 2 -i A=solid:1,1,1,1 -i Source=solid:-0.5,-0.5,-0.5,1 operation=geometric -i Mask=none --frames 0"
    "Merge 2 color-dodge above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.5,1.5,1.5,1||-m 2 -i A=solid:1.5,1.5,1.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-dodge -i Mask=none --frames 0"
    "Merge 2 color-burn negative|net.sf.openfx.MergePlugin|filter|float|mean=-0.5,-0.5,-0.5,1||-m 2 -i A=solid:-0.5,-0.5,-0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-burn -i Mask=none --frames 0"
    "Merge 1 screen above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.5,1.5,1.5,1||-m 1 -i A=solid:2,2,2,1 -i Source=solid:0.5,0.5,0.5,1 operation=screen -i Mask=none --frames 0"
    "Merge 1 hard-light above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.25,1.25,1.25,1||-m 1 -i A=solid:0.75,0.75,0.75,1 -i Source=solid:1.5,1.5,1.5,1 operation=hard-light -i Mask=none --frames 0"
    "Merge 1 overlay above 1|net.sf.openfx.MergePlugin|filter|float|mean=2,2,2,1||-m 1 -i A=solid:1.5,1.5,1.5,1 -i Source=solid:1.5,1.5,1.5,1 operation=overlay -i Mask=none --frames 0"
    "Merge 1 geometric negative|net.sf.openfx.MergePlugin|filter|float|mean=-2,-2,-2,1||-m 1 -i A=solid:1,1,1,1 -i Source=solid:-0.5,-0.5,-0.5,1 operation=geometric -i Mask=none --frames 0"
    "Merge 1 color-dodge above 1|net.sf.openfx.MergePlugin|filter|float|mean=1.5,1.5,1.5,1||-m 1 -i A=solid:1.5,1.5,1.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-dodge -i Mask=none --frames 0"
    "Merge 1 color-burn negative|net.sf.openfx.MergePlugin|filter|float|mean=-0.5,-0.5,-0.5,1||-m 1 -i A=solid:-0.5,-0.5,-0.5,1 -i Source=solid:0.5,0.5,0.5,1 operation=color-burn -i Mask=none --frames 0"
    "Merge tile|net.sf.openfx.MergePlugin|filter|float|nonconst+tile||-i A=checker:16 -i Source=ramp -i Mask=none --frames 0 --window 50,60,250,200"
    # Merge "Range From" (#277): in the general context, Merge 3 passes B's frame range by default, or
    # A's (hostDemo's inputs all have the frames 0 to 25, so this only checks which input is used, by
    # disconnecting the other); the host default if that input is disconnected, in the filter context,
    # and in Merge 2 (no parameter: Natron 2.5 projects keep their frame range)
    "Merge range from B|net.sf.openfx.MergePlugin|general|float|timedomain=0,25||-i A=none -i B=ramp -i Mask=none --frames 0"
    "Merge range from B, no B|net.sf.openfx.MergePlugin|general|float|timedomain=default||-i A=checker:16 -i B=none -i Mask=none --frames 0"
    "Merge range from A|net.sf.openfx.MergePlugin|general|float|timedomain=0,25||-i A=checker:16 -i B=none -i Mask=none rangeFrom=A --frames 0"
    "Merge range from A, no A|net.sf.openfx.MergePlugin|general|float|timedomain=default||-i A=none -i B=ramp -i Mask=none rangeFrom=A --frames 0"
    "Merge range, filter context (host default: getTimeDomain is general context only)|net.sf.openfx.MergePlugin|filter|float|timedomain=default||-i A=checker:16 -i Source=ramp -i Mask=none --frames 0"
    "Merge 2 range, host default|net.sf.openfx.MergePlugin|general|float|timedomain=default||-m 2 -i A=checker:16 -i B=ramp -i Mask=none --frames 0"
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
    "Shuffle RGB|net.sf.openfx.ShufflePlugin|general|*|comps=RGB+mean=0.75,0.5,0.25||--components rgba,rgb,alpha -i A=solid:0.25,0.5,0.75,1 -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6 outputComponents=RGB"
    # ErodeBlur remaps and clamps the values (here only the alpha, the default) even when the blur
    # does nothing (size 0.25: (0.5 - 0.125) / 0.875), and the Blur Alpha Threshold zeroes the alpha
    # even when a box-type filter does not blur (issue #324); with the default (Quasi-Gaussian)
    # filter and a tiny size, the render skips the threshold: an identity.
    "ErodeBlur small|eu.cimg.ErodeBlur|filter|float|notidentity+mean=0.5,0.5,0.5,0.4286||-i Source=solid:0.5,0.5,0.5,0.5 -i Mask=none size=0.25"
    "CImgBlur alpha threshold|net.sf.cimg.CImgBlur|filter|float|notidentity+mean=0.25,0.5,0.75,0||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none alphaThreshold=0.6 filter=2"
    "CImgBlur size 0|net.sf.cimg.CImgBlur|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none alphaThreshold=0.6"
    # Blur, Laplacian, Sharpen and Soften 5 (#312, #313): the default FIR Gaussian, w(i) = 2^(-(2i/size)^2) for
    # |i| <= 1.5*size, normalized; on the impulse, blurred in y only (x size 0): the centre is w(0), 1/1.125 for
    # size 1, 0.093968 for size 10. Below size 2/3 the FIR Gaussian does nothing (an identity); the IIR Gaussian
    # of version 4 blurs from size 0.24. Sharpen and Soften 5 add Amount times the difference d = 1 - w(0),
    # with its magnitude reduced by the Minimum and clamped to the Maximum; version 4 adds Amount times d.
    "CImgBlur FIR impulse|net.sf.cimg.CImgBlur|filter|float|tol=1e-4+mean=0.093968,0.093968,0.093968,1||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,10 --frames 0 --window 32,24,33,25"
    "CImgBlur FIR small|net.sf.cimg.CImgBlur|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none size=0.5,0.5"
    "CImgBlur 4 small|net.sf.cimg.CImgBlur|filter|float|notidentity+mean=0.25,0.5,0.75,0.5||-m 4 -i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none size=0.5,0.5"
    "CImgBlur FIR tile|net.sf.cimg.CImgBlur|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0 --frames 0 --window 100,100,300,250"
    "CImgBlur FIR tile derivatives|net.sf.cimg.CImgBlur|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=4,4 orderX=1 orderY=2 expandRoD=0 --frames 0 --window 100,100,300,250"
    "CImgLaplacian FIR impulse|net.sf.cimg.CImgLaplacian|filter|float|tol=1e-4+mean=0.11111,0.11111,0.11111,0||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,1 --frames 0 --window 32,24,33,25"
    # below size 2/3, the FIR Gaussian does not blur: Laplacian 5 is 0 everywhere, while Laplacian 4 (IIR) is not
    "CImgLaplacian FIR small|net.sf.cimg.CImgLaplacian|filter|float|tol=1e-6+max=0,0,0,0||-i Source=file:impulse.pfm -i Mask=none size=0.5,0.5"
    "CImgLaplacian 4 small|net.sf.cimg.CImgLaplacian|filter|float|nonconst||-m 4 -i Source=file:impulse.pfm -i Mask=none size=0.5,0.5"
    "CImgSharpen FIR impulse|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=1.11111,1.11111,1.11111,1||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,1 --frames 0 --window 32,24,33,25"
    "CImgSharpen maximum|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=1.05,1.05,1.05,1||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,1 maximum=0.05 --frames 0 --window 32,24,33,25"
    "CImgSharpen minimum|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=1.01111,1.01111,1.01111,1||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,1 minimum=0.1 --frames 0 --window 32,24,33,25"
    "CImgSharpen 4 box|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=1.66667,1.66667,1.66667,1||-m 4 -i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    # a difference of 3 - 1 = 2 (box of 3 on a peak of 3): clamped to the Maximum 1 in version 5, not in version 4
    "CImgSharpen box above 1|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=4,4,4,1||-i Source=file:impulse3.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    "CImgSharpen 4 box above 1|net.sf.cimg.CImgSharpen|filter|float|tol=1e-4+mean=5,5,5,1||-m 4 -i Source=file:impulse3.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    # the default FIR Gaussian does not blur below size 2/3 (an identity); the IIR Gaussian of version 4 does
    "CImgSharpen FIR small|net.sf.cimg.CImgSharpen|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none size=0.5,0.5"
    "CImgSharpen 4 small|net.sf.cimg.CImgSharpen|filter|float|notidentity||-m 4 -i Source=file:impulse.pfm -i Mask=none size=0.5,0.5"
    "CImgSharpen maximum 0|net.sf.cimg.CImgSharpen|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none maximum=0"
    "CImgSoften FIR impulse|net.sf.cimg.CImgSoften|filter|float|tol=1e-4+mean=0.94444,0.94444,0.94444,1||-i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,1 --frames 0 --window 32,24,33,25"
    "CImgSoften 4 box|net.sf.cimg.CImgSoften|filter|float|tol=1e-4+mean=0.66667,0.66667,0.66667,1||-m 4 -i Source=file:impulse.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    "CImgSoften box above 1|net.sf.cimg.CImgSoften|filter|float|tol=1e-4+mean=2.5,2.5,2.5,1||-i Source=file:impulse3.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    "CImgSoften 4 box above 1|net.sf.cimg.CImgSoften|filter|float|tol=1e-4+mean=2,2,2,1||-m 4 -i Source=file:impulse3.pfm -i Mask=none uniform=0 size=0,3 filter=2 --frames 0 --window 32,24,33,25"
    "CImgSoften FIR small|net.sf.cimg.CImgSoften|filter|float|identity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none size=0.5,0.5"
    "CImgSoften 4 small|net.sf.cimg.CImgSoften|filter|float|notidentity||-m 4 -i Source=file:impulse.pfm -i Mask=none size=0.5,0.5"
    # ErodeCImg and DilateCImg 3: the default size is 0 (an identity); version 2 erodes or dilates by 1.
    # DilateCImg size (1, 2) spreads the impulse at (32, 24) to (31, 22).
    "CImgDilate defaults|net.sf.cimg.CImgDilate|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgDilate 2 defaults|net.sf.cimg.CImgDilate|filter|float|notidentity+changed:Source||-m 2 -i Source=checker:8 -i Mask=none"
    "CImgDilate size 1,2|net.sf.cimg.CImgDilate|filter|float|mean=1,1,1,1||-i Source=file:impulse.pfm -i Mask=none size=1,2 --frames 0 --window 31,22,32,23"
    "CImgErode defaults|net.sf.cimg.CImgErode|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgErode 2 defaults|net.sf.cimg.CImgErode|filter|float|notidentity+changed:Source||-m 2 -i Source=checker:8 -i Mask=none"
    # ErodeCImg and DilateCImg 3: fractional sizes in canonical units, each radius rounded half away from 0 (x
    # divided by the PAR, 768/720): DilateCImg 0.5 is a radius of 0 in x (0.47) and 1 in y, so the impulse at
    # (32, 24) spreads to (32, 23) but not to (31, 24); a size of 0.4 is an identity (version 2: 0 and 1 are).
    "CImgDilate size 0.5 y|net.sf.cimg.CImgDilate|filter|float|mean=1,1,1,1||-i Source=file:impulse.pfm -i Mask=none size=0.5,0.5 --frames 0 --window 32,23,33,24"
    "CImgDilate size 0.5 x|net.sf.cimg.CImgDilate|filter|float|mean=0,0,0,1||-i Source=file:impulse.pfm -i Mask=none size=0.5,0.5 --frames 0 --window 31,24,32,25"
    "CImgDilate size -1.5 erodes|net.sf.cimg.CImgDilate|filter|float|mean=0,0,0,1||-i Source=file:impulse.pfm -i Mask=none size=-1.5,-1.5 --frames 0 --window 32,24,33,25"
    "CImgErode size 0.4|net.sf.cimg.CImgErode|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none size=0.4,0.4"
    "CImgErode tile|net.sf.cimg.CImgErode|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=2.5,-1.5 --frames 0 --window 100,100,300,250"
    # MedianCImg 3: the size is a radius per axis, default 3 (a 7x7 box), rounded half up (0.4 is an identity);
    # version 2: one integer radius, default 1.
    "CImgMedian defaults|net.sf.cimg.CImgMedian|filter|float|notidentity+changed:Source||-i Source=file:impulse.pfm -i Mask=none"
    "CImgMedian impulse|net.sf.cimg.CImgMedian|filter|float|mean=0,0,0,1||-i Source=file:impulse.pfm -i Mask=none size=0,1 --frames 0 --window 32,24,33,25"
    "CImgMedian size 0.4|net.sf.cimg.CImgMedian|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none size=0.4,0.4"
    "CImgMedian tile|net.sf.cimg.CImgMedian|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=3,1 threshold=0.5 --frames 0 --window 100,100,300,250"
    # at render scale 0.5, the radii are the scaled sizes rounded half up (size 3,1: 2,1; ErodeCImg 2.5,-1.5:
    # 1,1 after the PAR); tiles must render as the full image
    "CImgMedian scale 0.5 tile|net.sf.cimg.CImgMedian|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=3,1 --scale 0.5 --frames 0 --window 50,50,150,125"
    "CImgErode scale 0.5 tile|net.sf.cimg.CImgErode|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=2.5,-1.5 --scale 0.5 --frames 0 --window 50,50,150,125"
    # FilterErodeCImg: the alpha channel only by default; size 0 (box) is an identity; DilateCImg-like spread of
    # the impulse at (32, 24) with size -1 (box): the x weight of the next pixel is 2/PAR/2 + 0.5 - 1 = 0.9375
    "CImgFilterErode defaults|net.sf.cimg.CImgFilterErode|filter|float|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgFilterErode size 0|net.sf.cimg.CImgFilterErode|filter|float|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none size=0,0"
    "CImgFilterErode dilate impulse x|net.sf.cimg.CImgFilterErode|filter|float|tol=1e-4+mean=0.9375,0.9375,0.9375,1||-i Source=file:impulse.pfm -i Mask=none size=-1,-1 NatronOfxParamProcessR=1 NatronOfxParamProcessG=1 NatronOfxParamProcessB=1 --frames 0 --window 31,24,32,25"
    "CImgFilterErode tile|net.sf.cimg.CImgFilterErode|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=2.5,-1.5 filter=3 NatronOfxParamProcessR=1 --frames 0 --window 100,100,300,250"
    "CImgFilterErode scale 0.5 tile|net.sf.cimg.CImgFilterErode|filter|float|nonconst+tile||-i Source=checker:8 -i Mask=none size=3,1 filter=2 NatronOfxParamProcessR=1 --scale 0.5 --frames 0 --window 50,50,150,125"
    # FilterErodeCImg on values near the largest float (+-3.4e38; hostDemo rejects infinite inputs): the
    # weighted terms 1 - w*(1-v) and w*v stay finite and the centre weight 1 gives v itself, so the output is
    # the input (a NaN output pixel would fail hostDemo)
    "CImgFilterErode erode huge|net.sf.cimg.CImgFilterErode|filter|float|tol=1e33+mean=-3.4e38,0.5,0.5,1||-i Source=solid:-3.4e38,0.5,0.5,1 -i Mask=none size=0.5,0.5 NatronOfxParamProcessR=1"
    "CImgFilterErode dilate huge|net.sf.cimg.CImgFilterErode|filter|float|tol=1e33+mean=3.4e38,0.5,0.5,1||-i Source=solid:3.4e38,0.5,0.5,1 -i Mask=none size=-0.5,-0.5 NatronOfxParamProcessR=1"
    # ErodeSmoothCImg (obsolete, hidden from the menus) still loads and renders
    "CImgErodeSmooth obsolete|net.sf.cimg.CImgErodeSmooth|filter|float|notidentity+changed:Source||-i Source=checker:8 -i Mask=none size=5,5"
    # (no NaN or infinite input: hostDemo rejects non-finite solids and PFM files; the MedianCImg history
    # comment documents NaNs)
    "CImgMedian 2 defaults|net.sf.cimg.CImgMedian|filter|float|notidentity+changed:Source||-m 2 -i Source=file:impulse.pfm -i Mask=none"
    "CImgMedian 2 size 1 impulse|net.sf.cimg.CImgMedian|filter|float|mean=0,0,0,1||-m 2 -i Source=file:impulse.pfm -i Mask=none --frames 0 --window 32,24,33,25"
    # EdgeExtend merges the image over its blurred version even when the size is too small to blur:
    # A over A is not A where alpha < 1 (5 slices: alpha 1 - 0.5^32, RGB 0.5 * alpha), not an identity (issue #337)
    "EdgeExtend small|eu.cimg.EdgeExtend|filter|float|notidentity+mean=0.5,1,1.5,1||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none edgeExtendSize=0.1"
    # FrameBlend, one frame (relative range -1..-1, at t=1: frame 0 of the default pattern, which
    # changes with time, so that the render reads the right frame; view 0 is red): not a copy of that frame with Mix, a mask, an unprocessed channel (from
    # the current frame; with Mix 1 and no mask, it was 0), Product with Decay (the frame times
    # 1 - decay), or Average on float images (-0 and infinite values); Min, and Average on byte
    # images, copy it (issue #325). Version 2, Frame Interval 2: the one-frame range blends that
    # frame (frame 1 at t=2), as version 2.0 passed it through (it rendered black).
    "FrameBlend one frame mix|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.5,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 mix=0.5 --frames 1"
    "FrameBlend one frame mask|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.5,0,0,1||-i Source=default -i FgM=none -i Mask=solid:0.5,0.5,0.5,0.5 frameRange=-1,-1 --frames 1"
    "FrameBlend one frame no A|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.25,0.5,0.75,0.5||-i Source=solid:0.25,0.5,0.75,0.5 -i FgM=none -i Mask=none frameRange=-1,-1 NatronOfxParamProcessA=0"
    "FrameBlend one frame product|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.125,0.25,0.375,0.5||-i Source=solid:0.25,0.5,0.75,1 -i FgM=none -i Mask=none frameRange=-1,-1 operation=Product decay=0.5"
    # Product of one frame with Decay 0: a copy on byte and short images (maxValue * A / maxValue,
    # issue #335), not on float images (infinite values are made finite)
    "FrameBlend one frame product no decay|net.sf.openfx.FrameBlend|filter|byte short|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i FgM=none -i Mask=none frameRange=-1,-1 operation=Product"
    "FrameBlend one frame product no decay float|net.sf.openfx.FrameBlend|filter|float|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i FgM=none -i Mask=none frameRange=-1,-1 operation=Product"
    "FrameBlend one frame min|net.sf.openfx.FrameBlend|filter|*|identity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 operation=Min --frames 1"
    "FrameBlend one frame average byte|net.sf.openfx.FrameBlend|filter|byte|identity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 --frames 1"
    "FrameBlend one frame average float|net.sf.openfx.FrameBlend|filter|float|notidentity+mean=0.002,0,0,1||-i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 --frames 1"
    # Product of 2 frames of 0.5: 0.25 at all depths (it was almost 0 on byte and short images, issue #335)
    "FrameBlend Product|net.sf.openfx.FrameBlend|filter|*|notidentity+mean=0.25,0.25,0.25,0.25||-i Source=solid:0.5,0.5,0.5,0.5 -i FgM=none -i Mask=none operation=Product frameRange=-1,0"
    "FrameBlend 2 one frame interval 2|net.sf.openfx.FrameBlend|filter|*|mean=0.998,0,0,1||-m 2 -i Source=default -i FgM=none -i Mask=none frameRange=-1,-1 frameInterval=2 --frames 2"
    # LensDistortion 3DE Anamorphic 4 with zero coefficients: Squeeze 1 (the default) is an
    # identity, another squeeze is not (issue #326; it tested Squeeze 0)
    "LensDistortion 3DE Anamorphic 4 default|net.sf.openfx.LensDistortion|general|*|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 model=6"
    "LensDistortion 3DE Anamorphic 4 squeeze 2|net.sf.openfx.LensDistortion|general|float|notidentity||-i Source=checker:16 model=6 tde4_Squeeze_X=2 direction=0"
    # Squeeze-X 0 maps the image to its center column: the RoI (Distort) and the RoD (Undistort)
    # are zero-wide before their margin, which aborted debug builds (issue #336). Distort: each
    # output pixel samples the center column of the source, so the output is the solid colour
    "LensDistortion 3DE Anamorphic 4 squeeze 0|net.sf.openfx.LensDistortion|general|*|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 model=6 tde4_Squeeze_X=0 direction=0"
    "LensDistortion 3DE Anamorphic 4 squeeze 0 undistort|net.sf.openfx.LensDistortion|general|float|notidentity||-i Source=checker:16 model=6 tde4_Squeeze_X=0 direction=1"
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
    # a multiplane host (hostDemo --multiplane, openfx#331; the menus list the channels by label).
    # hostDemo is a multiplane host without Natron's extension (kFnOfxImageEffectPropMultiPlanar
    # only): the channel menus list all the planes of A, then of B, so index 6 is A.DisparityRight.X;
    # Shuffle 4 finds its defaults by label (#405), the other tests set them by label.
    # The defaults are B's color channels:
    "Shuffle multiplane default|net.sf.openfx.ShufflePlugin|general|float|mean=0.1,0.2,0.3,0.4||--multiplane -i A=solid:0.5,0.6,0.7,0.8 -i B=solid:0.1,0.2,0.3,0.4"
    # an Alpha output with the defaults: B's alpha, not an identity of the RGBA input B (it was:
    # R, G and B, which an Alpha output does not get, were checked too)
    "Shuffle multiplane Alpha output|net.sf.openfx.ShufflePlugin|general|float|notidentity+comps=Alpha+mean=0.4||--multiplane -i A=solid:0.5,0.6,0.7,0.8 -i B=solid:0.1,0.2,0.3,0.4 outputComponents=Alpha"
    # Shuffle 3 keeps the fixed indices: R 6 A.DisparityRight.X, G 7 A.DisparityRight.Y (A has no disparity: 0),
    # B 8 A.Backward.U, A 9 A.Backward.V (A's backward motion vectors)
    "Shuffle 3 multiplane default|net.sf.openfx.ShufflePlugin|general|float|mean=0,0,0.25,0.75||-m 3 --multiplane -i A=solid:0.5,0.6,0.7,0.8 -i A@BackMotionVector=solid:0.25,0.75,0,0 -i B=solid:0.1,0.2,0.3,0.4"
    # R, G, B from B's color plane and A from another plane (B's backward motion vectors, whose U
    # is the first value of the solid) is not an identity of B: the alpha is that channel (#268)
    "Shuffle multiplane A from another plane|net.sf.openfx.ShufflePlugin|general|float|notidentity+mean=0.1,0.2,0.3,0.25||--multiplane -i A=solid:0.5,0.6,0.7,0.8 -i B=solid:0.1,0.2,0.3,0.4 -i B@BackMotionVector=solid:0.25,0.75,0,0 -s outputR=B.Color.R -s outputG=B.Color.G -s outputB=B.Color.B -s outputA=B.Backward.U"
    # R, G, B from an RGB input (B, #rgb: openfx#330) and A from an Alpha-only input (A, #alpha:
    # hostDemo makes all four channels the solid's alpha, 0.4, so the distinct R, G, B values must
    # not show): the output alpha is that of A. Regression guards for the #375 scenario only: the
    # #375 bug (the alpha was 1) shows on Natron (NatronRenderer results in #375), not in hostDemo,
    # which has no dynamic choices: its output plane is SupportExt's hard-coded RGBA plane, which
    # the old test already handled. The second case (no multiplane suites, outputA=3 is A.a) never
    # used that code path.
    "Shuffle multiplane alpha-only A|net.sf.openfx.ShufflePlugin|general|float|notidentity+comps=RGBA+mean=0.1,0.2,0.3,0.4||--multiplane -i A=solid:0.9,0.8,0.7,0.4#alpha -i B=solid:0.1,0.2,0.3,1#rgb -s outputR=B.Color.R -s outputG=B.Color.G -s outputB=B.Color.B -s outputA=A.Color.A"
    "Shuffle alpha-only A|net.sf.openfx.ShufflePlugin|general|*|notidentity+comps=RGBA+mean=0.1,0.2,0.3,0.4||-i A=solid:0.9,0.8,0.7,0.4#alpha -i B=solid:0.1,0.2,0.3,1#rgb outputA=3"
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
    # without multi-resolution (--ofx11), ofxsMaskRoDExcludesRenderWindow() skips its test (SupportExt
    # #232): the same output, but not an identity
    "KeyMix out of mask, OFX 1.1 host|net.sf.openfx.KeyMix|filter|float|notidentity+mean=0.25,0.5,0.75,1||-i A=solid:1,1,1,1 -i Source=solid:0.25,0.5,0.75,1 -i Mask=file:autocrop.pfm --frames 0 --window 100,100,200,200 --ofx11"
    # KeyMix Bounding Box (issue #278), with autocrop.pfm (64x48, 1/6 white: 512 pixels, opaque) as inputs
    # and a mask: only the chosen bounding box is rendered. Union: B at (0,0) and A at (100,50) give
    # the box (0,0)-(164,98), 0.5 * 512 + 0.5 * 512 white in 164 * 98 pixels: 0.0319 (B or A alone
    # would give 0.0833). B (RGB): 0.5 * A + 0.5 * B = 0.5 + 0.5/6 in the B box. A, with alpha from
    # B: A in the A box. Where the output is B (no A channel), the RoD is B's whatever the choice:
    # B in its box, 1/6 (the A RoD, the project frame, would give 0.0012)
    "KeyMix bbox union|net.sf.openfx.KeyMix|filter|*|tol=0.002+mean=0.0319,0.0319,0.0319,0.1911||-i A=file:autocrop.pfm@100,50 -i Source=file:autocrop.pfm -i Mask=solid:0.5,0.5,0.5,0.5"
    "KeyMix bbox B|net.sf.openfx.KeyMix|filter|*|mean=0.5833,0.5833,0.5833,1||-i A=solid:1,1,1,1 -i Source=file:autocrop.pfm -i Mask=solid:0.5,0.5,0.5,0.5 bbox=1"
    "KeyMix bbox A|net.sf.openfx.KeyMix|filter|*|mean=0.1667,0.1667,0.1667,1||-i A=file:autocrop.pfm -i Source=solid:0,0,0,1 -i Mask=solid:1,1,1,1 bbox=2 NatronOfxParamProcessA=0"
    "KeyMix bbox A no channel|net.sf.openfx.KeyMix|filter|*|identity+mean=0.1667,0.1667,0.1667,1||-i A=solid:1,1,1,1 -i Source=file:autocrop.pfm -i Mask=solid:1,1,1,1 bbox=2 NatronOfxParamProcessR=0 NatronOfxParamProcessG=0 NatronOfxParamProcessB=0 NatronOfxParamProcessA=0"
    # HSVTool (issue #322): the default Output Alpha (Hue, full range) sets alpha to 1, even with Mix
    # 0; Clamp Black (on by default in HSVTool 1, issue #272) clamps float images; a gain other than 1 adjusts (saturation
    # 2/3 -> 0.5 here); outside the mask, a non-Source Output Alpha is 0. With Output Alpha = Source,
    # the alpha is the source alpha (it was 0), also with Premultiply.
    "HSVTool default alpha|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none"
    "HSVTool mix 0 alpha|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,0.5 -i Mask=none mix=0"
    "HSVTool clamp black|net.sf.openfx.HSVToolPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none outputAlpha=0 clampBlack=1"
    "HSVTool 1 default clamp black|net.sf.openfx.HSVToolPlugin|filter|float|notidentity+mean=0,0.5,0.75,1||-m 1 -i Source=solid:-0.25,0.5,0.75,1 -i Mask=none outputAlpha=0"
    "HSVTool default negatives|net.sf.openfx.HSVToolPlugin|filter|float|identity+mean=-0.25,0.5,0.75,1||-i Source=solid:-0.25,0.5,0.75,1 -i Mask=none outputAlpha=0"
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
    # Set Saturation and Set Brightness (issue #293, off by default): (0.25, 0.5, 0.75) is HSV (210, 2/3,
    # 0.75); saturation set to 0.2 gives min = 0.75 * (1 - 0.2) = 0.6, and G halfway at hue 210 (an
    # offset of 0.2 would give a saturation of 0.867); brightness set to 0.5 gives max 0.5, min
    # 0.5 / 3 (an offset would give 1.25)
    "HSVTool set saturation|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.6,0.675,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none outputAlpha=0 clampBlack=0 setSaturation=1 saturationAdjustment=0.2"
    "HSVTool set brightness|net.sf.openfx.HSVToolPlugin|filter|*|notidentity+mean=0.16667,0.33333,0.5,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none outputAlpha=0 clampBlack=0 setBrightness=1 brightnessAdjustment=0.5"
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
    # Any render scale (issue #66: OFX 1.1 hosts such as VEGAS cannot be refused one; getRoD and
    # isIdentity used to fail): below full resolution the effect is an identity and analyzes
    # nothing. hostDemo presses the button at render scale 1, so the key is set.
    "ImageStatistics scale 0.5|net.sf.openfx.ImageStatistics|filter|*|identity+mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 --scale 0.5"
    "ImageStatistics frame scale 0.5|net.sf.openfx.ImageStatistics|filter|float|identity+mean=0.25,0.5,0.75,1+value:statMean=0.25,0.5,0.75,1+keys:statMean=1||-i Source=solid:0.25,0.5,0.75,1 -s analyzeFrame --scale 0.5"
    # LensDistortion 5 (#302): Direction defaults to Undistort (version 4, obsolete: Distort). The
    # STMap output of the Nuke model with K1 0.05 and the center at 0.5,0, in the window x 100-200, y
    # 250-330: mean u 0.1692 (Undistort) or 0.2401 (Distort).
    "LensDistortion default undistort|net.sf.openfx.LensDistortion|general|float|mean=0.1692,0.5037,1,1||-i Source=checker:16 k1=0.05 center=0.5,0 outputMode=STMap --frames 0 --window 100,250,200,330"
    "LensDistortion 4 default distort|net.sf.openfx.LensDistortion|general|float|mean=0.2401,0.5033,1,1||-m 4 -i Source=checker:16 k1=0.05 center=0.5,0 outputMode=STMap --frames 0 --window 100,250,200,330"
    # LensDistortion 5 Twist 30 and Scale 1.5,1 (no radial distortion, center 0.5,0, Distort): the STMap is
    # the scale along the twisted axes about the center, mean u 0.0116 and v 0.3521 in the same window
    "LensDistortion twist scale|net.sf.openfx.LensDistortion|general|float|mean=0.0116,0.3521,1,1||-i Source=checker:16 center=0.5,0 anamorphicTwist=30 anamorphicScale=1.5,1 direction=0 outputMode=STMap --frames 0 --window 100,250,200,330"
    # Undistort (the default) with Twist and Scale: K1 0.05, center 0.5,0, in the same window, mean u, v
    # -0.0598, 1.1396 (Twist 45, Scale 2,0.5) and 0.2538, 0.5700 (Twist 20, Scale 1.2,0.9); the linear part
    # is inverted in closed form (Newton alone did not converge with Twist 45 and Scale 2,0.5)
    "LensDistortion undistort twist 45 scale 2,0.5|net.sf.openfx.LensDistortion|general|float|mean=-0.0598,1.1396,1,1||-i Source=checker:16 k1=0.05 center=0.5,0 anamorphicTwist=45 anamorphicScale=2,0.5 outputMode=STMap --frames 0 --window 100,250,200,330"
    "LensDistortion undistort twist 20 scale 1.2,0.9|net.sf.openfx.LensDistortion|general|float|mean=0.2538,0.57,1,1||-i Source=checker:16 k1=0.05 center=0.5,0 anamorphicTwist=20 anamorphicScale=1.2,0.9 outputMode=STMap --frames 0 --window 100,250,200,330"
    # LensDistortion 5 (#302): where the model minifies (Scale 3, or 1/3 with Undistort: three source pixels
    # per output pixel), the filter's kernel is stretched over the footprint of the output pixel, so a
    # one-pixel checker averages to 0.5. The filter's ordinary interpolation (version 4, here with a K1 of
    # -0.3) gives values from 0 to 1. No case has an upstream (concatenated) transform: hostDemo does not
    # concatenate; a minifying one also stretches the kernel (the footprint is in the original source).
    "LensDistortion minify distort|net.sf.openfx.LensDistortion|general|float|range=0.45,0.55+mean=0.5,0.5,0.5,0.5||-i Source=checker:1 anamorphicScale=3,3 direction=0 --frames 0 --window 300,250,400,330"
    "LensDistortion minify undistort|net.sf.openfx.LensDistortion|general|float|range=0.45,0.55+mean=0.5,0.5,0.5,0.5||-i Source=checker:1 anamorphicScale=0.333333,0.333333 --frames 0 --window 300,250,400,330"
    "LensDistortion 4 minify distort|net.sf.openfx.LensDistortion|general|float|max=1,1,1,1||-m 4 -i Source=checker:1 k1=-0.3 direction=0 --frames 0 --window 300,250,400,330"
    # the same at render scale 0.5 (checker:2 is one pixel at that scale), and with Box (the mean over the
    # 3x3-pixel footprint: 4/9 or 5/9). Impulse stays a point sample.
    "LensDistortion minify scale 0.5|net.sf.openfx.LensDistortion|general|float|range=0.45,0.55+mean=0.5,0.5,0.5,0.5||-i Source=checker:2 anamorphicScale=3,3 direction=0 --scale 0.5 --frames 0 --window 150,125,200,165"
    "LensDistortion minify box|net.sf.openfx.LensDistortion|general|float|range=0.44,0.56+mean=0.5,0.5,0.5,0.5||-i Source=checker:1 anamorphicScale=3,3 direction=0 filter=Box --frames 0 --window 300,250,400,330"
    # tiled renders are the same as the full render where the kernel is stretched (the RoI covers it), with
    # Cubic and Keys (radius 2), Distort and Undistort
    "LensDistortion minify tile|net.sf.openfx.LensDistortion|general|float|nonconst+tile||-i Source=checker:3 k1=0.1 anamorphicScale=3,2.5 direction=0 --frames 0 --window 300,250,400,330"
    "LensDistortion minify keys tile|net.sf.openfx.LensDistortion|general|float|nonconst+tile||-i Source=checker:3 k1=-0.2 anamorphicTwist=30 anamorphicScale=2.5,3 direction=0 filter=Keys --frames 0 --window 250,200,420,330"
    "LensDistortion minify undistort tile|net.sf.openfx.LensDistortion|general|float|nonconst+tile||-i Source=checker:3 k1=0.05 anamorphicScale=0.35,0.4 filter=Keys --frames 0 --window 280,220,420,330"
    # LensDistortion STMap output (u, v, 1, 1): PFBarrel squeeze 0, 3DE Classic filmback width 0 and
    # 3DE Standard cylindric bending b=-2 (sqrt(1+b)) gave a NaN RoD (nothing rendered). Models: 1
    # PFBarrel, 2 3DE Classic, 5 3DE4 Radial Standard Degree 4.
    "LensDistortion PFBarrel squeeze 0|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=1 pfSqueeze=0 outputMode=STMap direction=0"
    "LensDistortion 3DE filmback 0|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=2 tde4_filmback_width_cm=0 outputMode=STMap direction=0"
    "LensDistortion 3DE b=-2|net.sf.openfx.LensDistortion|general|float|mean=0.5,0.5,1,1||-i Source=checker:16 model=5 tde4_B_Cylindric_Bending=-2 outputMode=STMap direction=0"
    # IDistort and STMap (#303), float (the UV input has the output depth, and only float UV values
    # are supported). Version 3 does not multiply the output by the UV alpha; version 2 (-m 2,
    # obsolete) does.
    "IDistort UV alpha|net.sf.openfx.IDistort|general|float|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,0,0.5 -i Mask=none"
    "IDistort v2 UV alpha|net.sf.openfx.IDistort|general|float|mean=0.25,0.25,0.25,0.5||-m 2 -i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,0,0.5 -i Mask=none"
    "STMap UV alpha|net.sf.openfx.STMap|general|float|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0.5,0.5,0,0.5 -i Mask=none"
    "STMap v2 UV alpha|net.sf.openfx.STMap|general|float|mean=0.25,0.25,0.25,0.5||-m 2 -i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0.5,0.5,0,0.5 -i Mask=none"
    # Blur Channel B = 4 (square) pixels, across the edge of a 64-pixel checker at x=64: the pixels x=64
    # and 65 are white without blur (Blur Scale 0). The blur along x is 4 / PAR = 3.75 pixels (hostDemo's
    # PAR is 768/720), so with the Box filter (filter=1) the sampled area is 4.75 pixels wide: 2.875/4.75
    # and 3.875/4.75 white (mean 0.7105; version 2, in x pixels: 5 pixels wide, 3/5 and 4/5, mean 0.7).
    # The default Cubic filter is a separable kernel 4.75 pixels wide along x, its scale rounded to 13/64
    # (4.923 pixels): the white part of the normalized kernel weights gives a mean of 0.6922. tile: the render
    # window matches the full render (the UV RoI includes the pixels around the window).
    "IDistort blur box|net.sf.openfx.IDistort|general|float|tile+mean=0.7105,0.7105,0.7105,0.7105||-i Source=checker:64 -i UV=solid:0,0,4,1 -i Mask=none channelBlur=2 filter=1 --frames 0 --window 64,10,66,20"
    "IDistort v2 blur box|net.sf.openfx.IDistort|general|float|mean=0.7,0.7,0.7,0.7||-m 2 -i Source=checker:64 -i UV=solid:0,0,4,1 -i Mask=none channelBlur=2 filter=1 --frames 0 --window 64,10,66,20"
    "IDistort blur cubic|net.sf.openfx.IDistort|general|float|tile+tol=2e-3+mean=0.6922,0.6922,0.6922,0.6922||-i Source=checker:64 -i UV=solid:0,0,4,1 -i Mask=none channelBlur=2 --frames 0 --window 64,10,66,20"
    "IDistort blur scale 0|net.sf.openfx.IDistort|general|float|mean=1,1,1,1||-i Source=checker:64 -i UV=solid:0,0,4,1 -i Mask=none channelBlur=2 blurScale=0 filter=1 --frames 0 --window 64,10,66,20"
    # the same at render scale 0.5: the blur is in full-scale pixels, so the Box area is 1 + 1.875 pixels
    # wide at x=32 and 33 (the edge is at x=32): 1.9375/2.875 and 1 white (mean 0.837; 0.7 if the blur
    # ignored the scale)
    "IDistort blur scale 0.5|net.sf.openfx.IDistort|general|float|mean=0.837,0.837,0.837,0.837||-i Source=checker:64 -i UV=solid:0,0,4,1 -i Mask=none channelBlur=2 filter=1 --scale 0.5 --frames 0 --window 32,5,34,10"
    # a huge blur (1e30, unpremultiplied by alpha 1e-30: inf without the clamp to the source size) is
    # finite: the average of the solid source, with the Box and the Cubic filters
    "IDistort blur huge|net.sf.openfx.IDistort|general|float|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,1,1e-30 -i Mask=none channelBlur=2 unpremultUV=1 blurScale=1e30 filter=1 --frames 0 --window 300,300,302,302"
    "IDistort blur huge cubic|net.sf.openfx.IDistort|general|float|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,1,1e-30 -i Mask=none channelBlur=2 unpremultUV=1 blurScale=1e30 --frames 0 --window 300,300,302,302"
    # the full frame with that blur: the taps per pixel are bounded (at most 64 x 64, or O(1) for Box)
    "IDistort blur huge full frame|net.sf.openfx.IDistort|general|float|maxsec=120+mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,1,1e-30 -i Mask=none channelBlur=2 unpremultUV=1 blurScale=1e30 --frames 0"
    "IDistort blur huge box full frame|net.sf.openfx.IDistort|general|float|maxsec=120+mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,1,1e-30 -i Mask=none channelBlur=2 unpremultUV=1 blurScale=1e30 filter=1 --frames 0"
    # Box with a blur, Black Outside off (the default): the sampled area far outside the image (U = -1000)
    # repeats the edge pixels (a solid 0.5), it is not black
    "IDistort blur box outside|net.sf.openfx.IDistort|general|float|mean=0.5,0.5,0.5,1||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:-1000,0,4,1 -i Mask=none channelBlur=2 filter=1 black_outside=0 --frames 0 --window 300,300,302,302"
    # a blur of 128 square pixels (a Cubic kernel 1 + 128/PAR = 121 pixels wide along x, with a tap every
    # 4 pixels, each the mean of its 4-pixel cell centred on the tap), 40 pixels right of the edge of a
    # 512-pixel checker at x=512: the white part of the kernel is 0.8034 (another width gives another value)
    "IDistort blur 128|net.sf.openfx.IDistort|general|float|tol=1e-3+mean=0.8034,0.8034,0.8034,0.8034||-i Source=checker:512 -i UV=solid:0,0,128,1 -i Mask=none channelBlur=2 --frames 0 --window 552,300,553,301"
    # a blur of 64 square pixels on a 1-pixel checker: the kernel (61 pixels wide along x, 65 along y) is
    # sampled every 2 x 3 pixels, each tap the mean of its 2x3 cell (a summed-area table), so the output is
    # 0.5 everywhere (point samples every 2 pixels would read only white or only black)
    "IDistort blur 64 checker 1|net.sf.openfx.IDistort|general|float|range=0.499,0.501+mean=0.5,0.5,0.5,0.5||-i Source=checker:1 -i UV=solid:0,0,64,1 -i Mask=none channelBlur=2 --frames 0 --window 200,200,232,232"
    # Clamp with Black Outside: at x=0, a Rifman kernel 1 + 8/PAR = 8.5 pixels wide (its scale rounded to
    # 8/64: 8 pixels) reads 0.5625 of white
    # (the rest is the black outside), and the clamp to the values read (black included) keeps it
    "IDistort blur clamp black outside|net.sf.openfx.IDistort|general|float|tol=2e-3+mean=0.5625,0.5625,0.5625,0.5625||-i Source=solid:1,1,1,1 -i UV=solid:0,0,8,1 -i Mask=none channelBlur=2 filter=6 clamp=1 black_outside=1 --frames 0 --window 0,100,1,101"
    # Clamp with the separable blur: Rifman's negative lobes would ring around the edges; clamped, the
    # values are within the source's [0, 1]
    "IDistort blur clamp|net.sf.openfx.IDistort|general|float|range=0,1||-i Source=checker:16 -i UV=solid:0,0,6,1 -i Mask=none channelBlur=2 filter=6 clamp=1 --frames 0 --window 40,40,120,120"
    # a blur that varies (blur = 8 x the ramp), rendered in a window: the tile matches the full render
    "IDistort blur ramp tile|net.sf.openfx.IDistort|general|float|nonconst+tile||-i Source=checker:8 -i UV=ramp -i Mask=none channelBlur=2 blurScale=8 --frames 0 --window 100,100,300,250"
    # a blur that varies (B of the ramp x 40: about 20 here, 40 at the right edge) with large UV gradients
    # (U and V = 14400 x (ramp - 0.5): du/dx = dv/dx = 20), on a 1-pixel checker: the kernels here are about
    # 80 taps wide along x and y (the samples stay inside the image here), so they are strided (2 x 2 cells). Cells
    # read 0.5 (point taps with a stride of 2 would read one colour of the checker: 0 or 1), and the tile
    # matches the full render, whatever blur the rest of the frame has.
    "IDistort blur stride tile|net.sf.openfx.IDistort|general|float|tile+range=0.45,0.55+tol=0.02+mean=0.5,0.5,0.5,0.5||-i Source=checker:1 -i UV=ramp -i Mask=none uvOffset=0.5,0.5 uvScale=14400,14400 channelBlur=2 blurScale=40 --frames 0 --window 350,200,370,230"
    # Unpremult UV with a UV alpha of 0: no UV, transparent black (version 3; version 2 multiplies by 0)
    "IDistort unpremult alpha 0|net.sf.openfx.IDistort|general|float|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,0,0 -i Mask=none unpremultUV=1 --frames 0"
    "STMap unpremult alpha 0|net.sf.openfx.STMap|general|float|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i UV=solid:0,0,0,0 -i Mask=none unpremultUV=1 --frames 0"
    # STMap and IDistort 3 without a blur (#302): where the UV map minifies (U scaled by 3.3, a ramp UV: 3.3
    # source pixels per output pixel along x), the filter's kernel is stretched over the footprint, so a
    # one-pixel checker averages to about 0.5 (supersampling gave up to 0.12 away from it); tiled renders are
    # the same as the full render. Version 2 (obsolete) is unchanged.
    "STMap minify|net.sf.openfx.STMap|general|float|range=0.45,0.55||-i Source=checker:1 -i UV=ramp -i Mask=none uvScale=3.3,1 --frames 0 --window 50,250,150,260"
    "STMap minify tile|net.sf.openfx.STMap|general|float|nonconst+tile||-i Source=checker:3 -i UV=ramp -i Mask=none uvScale=3.3,1 filter=Keys --frames 0 --window 60,200,160,300"
    "IDistort minify|net.sf.openfx.IDistort|general|float|range=0.45,0.55||-i Source=checker:1 -i UV=ramp -i Mask=none uvScale=1650,0 --frames 0 --window 50,250,150,260"
    "IDistort minify tile|net.sf.openfx.IDistort|general|float|nonconst+tile||-i Source=checker:3 -i UV=ramp -i Mask=none uvScale=1650,300 filter=Keys --frames 0 --window 60,200,160,300"
    # IDistort 3.0 takes U in square pixels: U = 4 moves by 4/PAR = 3.75 pixels along x, so with the
    # Bilinear filter (filter=2) pixel 60 samples x = 64.25, 0.75 white; version 2 samples 64.5, white
    "IDistort U square pixels|net.sf.openfx.IDistort|general|float|mean=0.75,0.75,0.75,0.75||-i Source=checker:64 -i UV=solid:4,0,0,1 -i Mask=none filter=2 --frames 0 --window 60,10,61,11"
    "IDistort v2 U pixels|net.sf.openfx.IDistort|general|float|mean=1,1,1,1||-m 2 -i Source=checker:64 -i UV=solid:4,0,0,1 -i Mask=none filter=2 --frames 0 --window 60,10,61,11"
    # FrameBlend Min/Max where no frame is used (FgM > 0 everywhere): 0 (it was +-infinity)
    "FrameBlend Min no frame|net.sf.openfx.FrameBlend|filter|*|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i FgM=solid:1,1,1,1 -i Mask=none operation=Min"
    # Product where no frame is used: the initial value, white (it was almost black on byte and short images, issue #335)
    "FrameBlend Product no frame|net.sf.openfx.FrameBlend|filter|*|mean=1,1,1,1||-i Source=solid:0.5,0.5,0.5,1 -i FgM=solid:1,1,1,1 -i Mask=none operation=Product"
    "FrameBlend Max no frame|net.sf.openfx.FrameBlend|filter|*|mean=0,0,0,0||-i Source=solid:0.5,0.5,0.5,1 -i FgM=solid:1,1,1,1 -i Mask=none operation=Max"
    "FrameBlend Min|net.sf.openfx.FrameBlend|filter|*|mean=0.5,0.25,0.75,1||-i Source=solid:0.5,0.25,0.75,1 -i FgM=none -i Mask=none operation=Min frameRange=-2,0"
    # Retime (#309), general context, float, on hostDemo's default input (frames 0 to 25, red = t in
    # view 0, with the time drawn in black: tol=0.05). hostDemo has no parametric parameters, so the
    # time domain is set by Duration (the speed is used on hosts with a Warp curve, e.g. Natron).
    "Retime speed 1|net.sf.openfx.Retime|general|float|identity+timedomain=0,25+tol=0.05+mean=10,0,0,1||--frames 10"
    # (version 2's default filter is Box, which blends frames at speed 2: Linear here)
    "Retime speed 2|net.sf.openfx.Retime|general|float|identity+timedomain=0,25+tol=0.05+mean=20,0,0,1||filter=2 speed=2 --frames 10"
    "Retime duration 2|net.sf.openfx.Retime|general|float|identity+timedomain=0,50+tol=0.05+mean=5,0,0,1||filter=2 speed=0.5 duration=2 --frames 10"
    # Box filter (filter=3): the source frames from the output time to Shutter (default 1) frames after
    # it, source frame n covering n to n+1. At speed 1, the input frame; at speed 10, t=2 covers the
    # source frames 20 to 29, a tenth each, and 26 to 29 are transparent black (After = Black): the
    # mean is 0.1 x 0.998 x (20+...+25) = 13.47 (the time drawn in black: 0.998), alpha 0.6; at speed
    # 0.5, t=21 covers the source times 10.5 to 11: frame 10 only; with Shutter 2 at speed 2, t=5
    # covers the frames 10 to 13 (mean 11.5 x 0.998).
    "Retime Box speed 1|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=10,0,0,1||filter=3 --frames 10"
    "Retime Box speed 10|net.sf.openfx.Retime|general|float|tol=0.05+mean=13.47,0,0,0.6||filter=3 speed=10 after=2 --frames 2"
    "Retime Box speed 0.5|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=10,0,0,1||filter=3 speed=0.5 --frames 21"
    # Reverse input (version 2): the input reversed as whole frames, so Box at speed 1 and t = 0 covers source
    # times 25 to 26 (frame 25, mean 24.949); at speed 2, frames 24 and 25 (mean 24.5 x 0.998). Version 1
    # (Box not released) reverses around frame 25: t = 0 covers 24 to 25 (frame 24)
    "Retime reverse Box|net.sf.openfx.Retime|general|float|identity+tol=0.005+mean=24.949,0,0,1||filter=3 reverseInput=1 --frames 0"
    "Retime reverse Box speed 2|net.sf.openfx.Retime|general|float|notidentity+tol=0.05+mean=24.45,0,0,1||filter=3 speed=2 reverseInput=1 --frames 0"
    "Retime v1 reverse Box|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=23.95,0,0,1||-m 1 filter=3 reverseInput=1 --frames 0"
    "Retime Box shutter 2|net.sf.openfx.Retime|general|float|notidentity+tol=0.05+mean=11.48,0,0,1||filter=3 speed=2 shutter=2 --frames 5"
    # FrameHold Set to Current Frame (#307), on the same default input: pressed at t=7, First Frame is 7
    # and every frame shows frame 7; with First Frame animated (keys at 0 and 20), it adds a key at 7.
    # Pressed at t=40, the value is clamped to the last input frame, 25. (Setting First Frame when the Source
    # is connected is only done on Natron >= 2.6, and hostDemo connects no clip.)
    "FrameHold Set to Current Frame|net.sf.openfx.FrameHold|general|float|identity+tol=0.05+mean=7,0,0,1+value:firstFrame=7+keys:firstFrame=0+value:firstFrameInitialised=1||-s setToCurrentFrame@7"
    "FrameHold Set to Current Frame key|net.sf.openfx.FrameHold|general|float|identity+tol=0.05+mean=7,0,0,1+value:firstFrame=3+keys:firstFrame=3+keyrange:firstFrame=0,20||-s firstFrame@0=3 -s firstFrame@20=3 -s setToCurrentFrame@7 --frames 7"
    "FrameHold Set to Current Frame clamped|net.sf.openfx.FrameHold|general|float|identity+value:firstFrame=25||-s setToCurrentFrame@40"
    # animated and clamped: keys at 0 and 50 (value 3), pressed at t=40: a key at 40 (not at 25) with value
    # 25, so the render at t=40 is frame 25 (a key at 25 would give about 11.8 at t=40); the time drawn in
    # black makes the mean of frame 25 24.949
    "FrameHold Set to Current Frame key clamped|net.sf.openfx.FrameHold|general|float|identity+tol=0.005+mean=24.949,0,0,1+keys:firstFrame=3+keyrange:firstFrame=0,50||-s firstFrame@0=3 -s firstFrame@50=3 -s setToCurrentFrame@40 --frames 40"
    # FrameRange (#400): a Frame Range set by the user, or by Reset (the input range, 0 to 25), is marked as
    # set, so that connecting the Source never replaces it (only done on Natron >= 2.6)
    "FrameRange set|net.sf.openfx.FrameRange|general|float|value:frameRange=10,20+value:frameRangeInitialised=1||-s frameRange=10,20 --frames 15"
    "FrameRange Reset|net.sf.openfx.FrameRange|general|float|value:frameRange=0,25+value:frameRangeInitialised=1||-s reset --frames 15"
    # (hostDemo never sends the clip changed action, so the update on connect is not exercised here: the range
    # stays at its default, not marked as set)
    "FrameRange default|net.sf.openfx.FrameRange|general|float|value:frameRange=1,1+value:frameRangeInitialised=0||--frames 1"
    # TimeOffset (#306), version 2, offset 5.5 at t = 10: Whole Frames (the default) truncates it to 5, frame 5;
    # Fractional Time gives 4.5. Version 1 (an integer offset): 5 gives frame 5
    "TimeOffset Whole Frames|net.sf.openfx.timeOffset|general|float|identity+tol=0.05+mean=5,0,0,1||timeOffset=5.5 --frames 10"
    "TimeOffset Fractional Time|net.sf.openfx.timeOffset|general|float|identity+tol=0.05+mean=4.5,0,0,1||timeOffset=5.5 roundingMode=1 --frames 10"
    "TimeOffset v1|net.sf.openfx.timeOffset|general|float|identity+tol=0.05+mean=5,0,0,1||-m 1 timeOffset=5 --frames 10"
    # at a fractional time (motion blur, a retime downstream), Whole Frames does not round the time: offset 5
    # at t = 10.25 shows the input at 5.25 (as version 1)
    "TimeOffset fractional time|net.sf.openfx.timeOffset|general|float|identity+tol=0.05+mean=5.25,0,0,1||timeOffset=5 --frames 10.25"
    "TimeOffset v1 fractional time|net.sf.openfx.timeOffset|general|float|identity+tol=0.05+mean=5.25,0,0,1||-m 1 timeOffset=5 --frames 10.25"
    # Retime Nearest (#309), speed 0.5 at t = 21: source time 10.5; version 2 takes the frame at the middle of
    # the Box interval (10.5 to 11: frame 10), version 1 the nearest frame, half frames rounded up (frame 11);
    # at speed 2, t = 10: the interval 20 to 22, frame 21 (mean 21 x 0.998; version 1: frame 20)
    "Retime Nearest|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=10,0,0,1||filter=1 speed=0.5 --frames 21"
    "Retime v1 Nearest|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=11,0,0,1||-m 1 filter=1 speed=0.5 --frames 21"
    "Retime Nearest speed 2|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=20.96,0,0,1||filter=1 speed=2 --frames 10"
    "Retime v1 Nearest speed 2|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=20,0,0,1||-m 1 filter=1 speed=2 --frames 10"
    # the defaults: version 2, Box (frames 20 and 21 at speed 2, t = 10: not an identity) and After = Hold
    # (frame 25 at t = 30, whose time drawn in black makes the mean 24.949); version 1, Linear (frame 20 at
    # speed 2) and After = Original (frame 30)
    "Retime default Box|net.sf.openfx.Retime|general|float|notidentity+tol=0.05+mean=20.46,0,0,1||speed=2 --frames 10"
    "Retime v1 default Linear|net.sf.openfx.Retime|general|float|identity+tol=0.05+mean=20,0,0,1||-m 1 speed=2 --frames 10"
    "Retime default After Hold|net.sf.openfx.Retime|general|float|identity+tol=0.005+mean=24.949,0,0,1||--frames 30"
    "Retime v1 default After Original|net.sf.openfx.Retime|general|float|identity+tol=0.1+mean=30,0,0,1||-m 1 --frames 30"
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
    # SupportExt#185: with Mix < 1 and no mask, the RoD of TransformMasked and CornerPinMasked was the
    # transformed one, without the source, which the output blends with, and the RoI did not fetch
    # the source under the output. Translated by 200 (187.5 pixels at the PAR 768/720), the output is
    # now the whole project: 0.5 (half the source) on the left 187.5 pixels, 1 elsewhere, a mean of
    # 0.870 (it was 0.824 and 0.857: no left part, and only half the source on part of the right)
    "TransformMasked mix 0.5|net.sf.openfx.TransformMaskedPlugin|general|*|mean=0.870,0.870,0.870,0.870+tol=0.005||-i Source=solid:1,1,1,1 -i Mask=none translate=200,0 mix=0.5"
    "CornerPinMasked mix 0.5|net.sf.openfx.CornerPinMaskedPlugin|general|*|mean=0.870,0.870,0.870,0.870+tol=0.005||-i Source=solid:1,1,1,1 -i Mask=none to1=200,0 to2=920,0 to3=920,576 to4=200,576 mix=0.5"
    # SupportExt#184: DirBlur with Motion Blur 0 rendered 0.999 of the transform (a 337.16 pixel
    # translation instead of 337.5 here). The mean of the shifted ramp on the window is 162.5/720
    # (it was 0.226291)
    "DirBlur motion blur 0|net.sf.openfx.DirBlur|general|float|mean=0.225694,0.225694,0.225694,0.225694+tol=0.0001||-i Source=ramp -i Mask=none translate=360,0 motionBlur=0 --window 400,100,600,200"
    # SupportExt#205-#207 (part of #273): a huge shutter (hosts may not clamp to the 1000-frame range) gives an
    # infinite RoD; the RoD loop, which samples the shutter every quarter frame, did not end before #207
    "Transform shutter 1e30|net.sf.openfx.TransformPlugin|filter|float|nonconst||-i Source=ramp motionBlur=1 shutter=1e30"
    "TransformMasked shutter 1e30|net.sf.openfx.TransformMaskedPlugin|general|float|nonconst||-i Source=ramp -i Mask=none motionBlur=1 shutter=1e30"
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
    # #297: X and Y are the pixel coordinates in the full-size image, at any render scale and wherever the
    # processed region starts (x and y are buffer indices at the render scale). Pixel centres map to pixel
    # centres: at scale 0.5, X=2x+0.5, so the mean of X/720 on the 360 columns is 359.5/720, and of Y/576
    # on the 288 rows 287.5/576, as at scale 1 (x/720 would give 179.5/720). The expression also gives
    # alpha (c=3: Y).
    "CImgExpression X Y scale 0.5|net.sf.cimg.CImgExpression|filter|float|mean=0.499306,0.499132,0.499306,0.499132+tol=0.0001||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=if(c%2,Y/576,X/720) --scale 0.5"
    "CImgExpression X Y|net.sf.cimg.CImgExpression|filter|float|mean=0.499306,0.499132,0.499306,0.499132+tol=0.0001||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=if(c%2,Y/576,X/720)"
    # an input whose RoD starts at 100,50 (the 64x48 autocrop.pfm, opaque): CImgExpression does not
    # support multi-resolution, so hostDemo pads its input to the 720x576 project (openfx #359; with
    # --ofx11 too, where no clip has another size than the project): X and Y cover the project, and
    # the means of X/1000 and Y/1000 are 359.5/1000 and 287.5/1000 (an unpadded input would give
    # 131.5/1000 and 73.5/1000). CImgFilter's path for an input RoD that does not start at 0,0 (x1 != 0,
    # without multi-resolution) is no longer reachable with hostDemo.
    "CImgExpression X Y offset, padded|net.sf.cimg.CImgExpression|filter|float|mean=0.3595,0.2875,0.3595,0.2875+tol=0.0001||-i Source=file:autocrop.pfm@100,50 -i Mask=none expression=if(c%2,Y/1000,X/1000)"
    # an expression that defines its own X keeps it: no scalar X in front of a vector X (the
    # arguments are split on spaces, so "const X" cannot be given here)
    "CImgExpression vector X|net.sf.cimg.CImgExpression|filter|float|mean=0.5,0.5,0.5,0.5||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=X=[0.5,0.25];X[0]"
    # the '*' (parallel evaluation) prefix stays the first character of the expression
    "CImgExpression * prefix|net.sf.cimg.CImgExpression|filter|float|mean=0.5,0.5,0.5,0.5||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=*0.5"
    # Math parser fixes of openfx-gmic's CImg patches (CImg/CImgVersion.mk), on CImg 4.0.8.
    # cimg-fusion-guards.sh (openfx-gmic#232): '+', '-', '*' and '/' fused with the last opcode of an
    # operand that is an assignment, and so rewrote the variable: y must be 2x (mean 719 on the 720
    # columns; NaN without the patch), 2i (i without), and i/2 (i without)
    "CImgExpression assign in + operand|net.sf.cimg.CImgExpression|filter|float|mean=719,719,719,719||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=z=1+(y=x*2);y"
    "CImgExpression assign in - operand|net.sf.cimg.CImgExpression|filter|float|mean=0.5,1,1.5,2||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=z=1-(y=i*2);y"
    "CImgExpression assign in * operand|net.sf.cimg.CImgExpression|filter|float|mean=0.125,0.25,0.375,0.5||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=z=2*(y=i/2);y"
    "CImgExpression assign in / operand|net.sf.cimg.CImgExpression|filter|float|mean=0.125,0.25,0.375,0.5||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=z=(y=i/2)/3;y"
    # cimg-vector-assign.sh (openfx-gmic#245): '(V)=f(V,...)' wrote into V while f() read it (2,2,4,4
    # instead of 2,1,4,3), and '(A)=B' after 'B=sort(A)' took B's slot (size 0 instead of 3)
    "CImgExpression (V)= aliasing|net.sf.cimg.CImgExpression|filter|float|mean=0.5,0.25,1,0.75||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=A=[1,2,3,4];B=[0,1,1,0];(A)=mul(A,B,2);A[c]/4"
    "CImgExpression (V)= in place|net.sf.cimg.CImgExpression|filter|float|mean=0.75,0.75,0.75,0.75||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=A=[3,1,2];B=sort(A);(A)=B;size(B)/4"
    # cimg-same-reuse.sh (openfx-gmic#246): same() of two N-value vectors wrote its result into slot
    # N, which may be a live temporary: sin(1) + 1 (2 without the patch). The issue's vector36 is
    # slot 36 in a bare CImg parser; CImgExpression first defines T and K (X and Y only when used),
    # so a is slot 37 and sin(a) slot 38: vector38. T and K must stay small integers (render time 0,
    # scale 1), or they take constant slots and the case no longer reaches the bug
    "CImgExpression same() result slot|net.sf.cimg.CImgExpression|filter|float|mean=1.841471,1.841471,1.841471,1.841471+tol=0.0001||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none expression=a=1;sin(a)+same(vector38(a),vector38(a))"
    # symmetric_eigen() on the rank-deficient structure tensors of flat images and ramps
    # (GreycLab/gmic#221: NaN eigenvectors when the compiler contracts to FMAs): no NaN
    # output pixels (the float outputs are checked for NaN)
    "CImgSmooth flat|net.sf.cimg.CImgSmooth|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgSmooth ramp|net.sf.cimg.CImgSmooth|filter|float|mean=0.5,0.5,0.5,0.5+nonconst||-i Source=ramp -i Mask=none"
    "CImgSharpenShock flat|net.sf.cimg.CImgSharpenShock|filter|float|mean=0.25,0.5,0.75,1||-i Source=solid:0.25,0.5,0.75,1 -i Mask=none"
    "CImgSharpenShock ramp|net.sf.cimg.CImgSharpenShock|filter|float|nonconst||-i Source=ramp -i Mask=none"
    # EdgeDetectCImg on a white pixel at (32,24), Separate channels: Sobel (1-2-1 kernels / 8) gives 2/8
    # next to it and sqrt(2)/8 on the diagonal; Prewitt (1-1-1 kernels / 6, issue #314, appended
    # as filter 8) gives 1/6 and sqrt(2)/6. Threshold (issue #314, 0 by default) sets the edges
    # below it to 0.
    "EdgeDetectCImg Sobel impulse|eu.cimg.EdgeDetect|filter|float|tol=1e-4+mean=0.25,0.25,0.25,1||-i Source=file:impulse.pfm -i Mask=none filter=1 multiChannel=0 --frames 0 --window 33,24,34,25"
    "EdgeDetectCImg Prewitt impulse|eu.cimg.EdgeDetect|filter|float|tol=1e-4+mean=0.166667,0.166667,0.166667,1||-i Source=file:impulse.pfm -i Mask=none filter=8 multiChannel=0 --frames 0 --window 33,24,34,25"
    "EdgeDetectCImg Prewitt impulse diagonal|eu.cimg.EdgeDetect|filter|float|tol=1e-4+mean=0.235702,0.235702,0.235702,1||-i Source=file:impulse.pfm -i Mask=none filter=8 multiChannel=0 --frames 0 --window 33,25,34,26"
    "EdgeDetectCImg Sobel threshold|eu.cimg.EdgeDetect|filter|float|tol=1e-4+mean=0,0,0,1||-i Source=file:impulse.pfm -i Mask=none filter=1 multiChannel=0 threshold=0.3 --frames 0 --window 33,24,34,25"
    # threshold 0.2 on the two pixels (33,24) and (33,25): 1/6 is removed, sqrt(2)/6 is kept, so the
    # mean is sqrt(2)/12 (without the threshold, (1/6 + sqrt(2)/6) / 2 = 0.2012)
    "EdgeDetectCImg Prewitt threshold|eu.cimg.EdgeDetect|filter|float|tol=1e-4+mean=0.117851,0.117851,0.117851,1||-i Source=file:impulse.pfm -i Mask=none filter=8 multiChannel=0 threshold=0.2 --frames 0 --window 33,24,34,26"
    # FFT convolution of a 720x576 image (not a power of two: with FFTW or pocketfft, the FFT is
    # not padded to 1024x1024) by a 7x5 kernel: the exact linear convolution, without a CImg
    # "non 2^N dimension" error, whatever the FFT library (CImgFFTConvolve.h)
    "CImgConvolve 720x576|net.sf.cimg.CImgConvolve|filter|float|conv=convsrc.pfm,convk.pfm||--frames 0 -i Source=file:convsrc.pfm -i Filter=file:convk.pfm -i Mask=none filterCenter=2"
    # ZDefocus (NatronGitHub/openfx-misc#266). A sharp sky (disparity 0) with Disparity with
    # Offset and a Focus Point (the image center) on the sky: the output is the input (without
    # Blur Inside).
    "CImgZDefocus sky focus|net.sf.cimg.CImgZDefocus|general|float|conv=convsrc.pfm,delta.pfm||--frames 0 -i Source=file:convsrc.pfm -i Depth=solid:0,0,0,1 -i Filter=none depthType=2 useFocusPoint=1 blurInside=0"
    # The Focus Point default (0.5, 0.5 normalised) is the middle of the 768x576 (canonical)
    # project, also on a host without normalised defaults (OFX 1.1 hosts such as VEGAS; hostDemo
    # --no-normalised-defaults, and --ofx11, which implies it): SupportExt's
    # ofxsNormalisedDefaults.h (#66, #416)
    "CImgZDefocus focus point, no normalised defaults|net.sf.cimg.CImgZDefocus|general|float|value:focusPoint=384,288||--frames 0 -i Source=checker:32 -i Depth=solid:0,0,0,1 -i Filter=none --no-normalised-defaults"
    "CImgZDefocus focus point, normalised defaults|net.sf.cimg.CImgZDefocus|general|float|value:focusPoint=384,288||--frames 0 -i Source=checker:32 -i Depth=solid:0,0,0,1 -i Filter=none"
    "CImgZDefocus focus point, OFX 1.1 host|net.sf.cimg.CImgZDefocus|general|float|value:focusPoint=384,288||--frames 0 -i Source=checker:32 -i Depth=solid:0,0,0,1 -i Filter=none --ofx11"
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
    # TimeBlur (#273), Divisions 1, Shutter 1, Centered at t = 10: version 2 shows the input at the current
    # time (frame 10), version 1 at the start of the shutter range (t = 9.5)
    "TimeBlur divisions 1|net.sf.openfx.TimeBlur|filter|float|identity+tol=0.05+mean=10,0,0,1||division=1 shutter=1 shutterOffset=0 --frames 10"
    "TimeBlur 1 divisions 1|net.sf.openfx.TimeBlur|filter|float|identity+tol=0.05+mean=9.48,0,0,1||-m 1 division=1 shutter=1 shutterOffset=0 --frames 10"
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
    # quadratic fades over N frames (version 2): (1/(N+1))^2... (N/(N+1))^2; Fade In 3 at frames 1 and 3
    # (1/16 and 9/16), then not faded; version 1: linear, 1/(N+1)... N/(N+1)
    "AppendClip fade in 3 first|net.sf.openfx.AppendClip|general|*|mean=0.0625,0.0625,0.0625,0.0625||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 1"
    "AppendClip fade in 3 last|net.sf.openfx.AppendClip|general|*|mean=0.5625,0.5625,0.5625,0.5625||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 3"
    "AppendClip fade in 3 after|net.sf.openfx.AppendClip|general|*|identity+mean=1,1,1,1||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 4"
    "AppendClip 1 fade in 3 first|net.sf.openfx.AppendClip|general|*|mean=0.25,0.25,0.25,0.25||-m 1 -i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 1"
    "AppendClip 1 fade in 3 last|net.sf.openfx.AppendClip|general|*|mean=0.75,0.75,0.75,0.75||-m 1 -i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeIn=3 --frames 3"
    # Fade Out 2: frame 50 is not faded, frame 51 is at (2/3)^2, the last frame (52) at (1/3)^2 (version 1:
    # 2/3 and 1/3)
    "AppendClip fade out 2 before|net.sf.openfx.AppendClip|general|*|identity+mean=0.5,0.5,0.5,1||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 50"
    "AppendClip fade out 2 first|net.sf.openfx.AppendClip|general|*|mean=0.2222,0.2222,0.2222,0.4444||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 51"
    "AppendClip fade out 2 last|net.sf.openfx.AppendClip|general|*|mean=0.0556,0.0556,0.0556,0.1111||-i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 52"
    "AppendClip 1 fade out 2 first|net.sf.openfx.AppendClip|general|*|mean=0.3333,0.3333,0.3333,0.6667||-m 1 -i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 51"
    "AppendClip 1 fade out 2 last|net.sf.openfx.AppendClip|general|*|mean=0.1667,0.1667,0.1667,0.3333||-m 1 -i 1=solid:1,1,1,1 -i 2=solid:0.5,0.5,0.5,1 fadeOut=2 --frames 52"
    # Cross Dissolve 4: the last 4 frames of clip 1 (frames 23 to 26) overlap the first 4 of clip 2;
    # clip 2 is weighted by the smoothstep of 0.2 (0.104) at frame 23 and of 0.8 (0.896) at frame 26, clip 1
    # (its frame 22, then 25, mean 21.956 and 24.949) by 1 minus that; version 1: linear, 0.2 and 0.8
    "AppendClip dissolve 4 first|net.sf.openfx.AppendClip|general|float|mean=19.725,0.052,0.052,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 23"
    "AppendClip dissolve 4 last|net.sf.openfx.AppendClip|general|float|mean=3.043,0.448,0.448,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 26"
    "AppendClip 1 dissolve 4 first|net.sf.openfx.AppendClip|general|float|mean=17.665,0.1,0.1,1||-m 1 -i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 23"
    "AppendClip 1 dissolve 4 last|net.sf.openfx.AppendClip|general|float|mean=5.390,0.4,0.4,1||-m 1 -i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 26"
    # ContactSheet, 2x2 cells of 32x32 pixels (#305). The 720x576 inputs fit the width of a cell,
    # at its bottom left: the 8x8 windows are inside the image of a cell. Two inputs, top to
    # bottom: they fill the top row, and the bottom row is empty; with Center Rows, the row of
    # images is in the middle (y 16 to 48)
    "ContactSheet top row|net.sf.openfx.ContactSheetOFX|filter|float|mean=0.5,0.5,0.5,1||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1${cs_none2} resolution=64,64 rowsColumns=2,2 rowOrder=0 --frames 0 --window 36,36,44,44"
    "ContactSheet bottom row empty|net.sf.openfx.ContactSheetOFX|filter|float|mean=0,0,0,0||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1${cs_none2} resolution=64,64 rowsColumns=2,2 rowOrder=0 --frames 0 --window 4,4,12,12"
    "ContactSheet Center Rows|net.sf.openfx.ContactSheetOFX|filter|float|mean=1,1,1,1||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1${cs_none2} resolution=64,64 rowsColumns=2,2 rowOrder=0 centerRows=1 --frames 0 --window 4,20,12,28"
    "ContactSheet Center Rows right|net.sf.openfx.ContactSheetOFX|filter|float|mean=0.5,0.5,0.5,1||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1${cs_none2} resolution=64,64 rowsColumns=2,2 rowOrder=0 centerRows=1 --frames 0 --window 36,20,44,28"
    # Snake, bottom to top: inputs 0 and 1 in the bottom row from left to right, input 2 at the
    # right of the second row
    "ContactSheet Snake|net.sf.openfx.ContactSheetOFX|filter|float|mean=0.25,0.25,0.25,1||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1 -i 2=solid:0.25,0.25,0.25,1${cs_none3} resolution=64,64 rowsColumns=2,2 colOrder=2 --frames 0 --window 36,36,44,44"
    # the first row goes from left to right (RightLeft would put input 1, 0.5, at the bottom left)
    "ContactSheet Snake first row|net.sf.openfx.ContactSheetOFX|filter|float|mean=1,1,1,1||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1 -i 2=solid:0.25,0.25,0.25,1${cs_none3} resolution=64,64 rowsColumns=2,2 colOrder=2 --frames 0 --window 4,4,12,12"
    "ContactSheet Snake empty|net.sf.openfx.ContactSheetOFX|filter|float|mean=0,0,0,0||-i Source=solid:1,1,1,1 -i 1=solid:0.5,0.5,0.5,1 -i 2=solid:0.25,0.25,0.25,1${cs_none3} resolution=64,64 rowsColumns=2,2 colOrder=2 --frames 0 --window 4,36,12,44"
    # Gap (#305): in ContactSheet 2, the 2x2 cells are 28x28 pixels ((64 - 8) / 2), with the gap on their
    # left and bottom sides: the image of Source starts at x = 8 (it fits 20 pixels, scaled by the
    # cubic filter). ContactSheet 1 put half the gap on each side of 32x32 cells: it starts at x = 4.
    "ContactSheet gap|net.sf.openfx.ContactSheetOFX|filter|float|mean=0,0,0,0||-i Source=solid:1,1,1,1${cs_none1} resolution=64,64 rowsColumns=2,2 gap=8 --frames 0 --window 2,12,6,16"
    "ContactSheet gap image|net.sf.openfx.ContactSheetOFX|filter|float|mean=1,1,1,1||-i Source=solid:1,1,1,1${cs_none1} resolution=64,64 rowsColumns=2,2 gap=8 --frames 0 --window 10,12,14,16"
    # the same at render scale 0.5 (the image is x 4 to 14, y 4 to 12 there), which checks the RoI
    "ContactSheet gap image scale 0.5|net.sf.openfx.ContactSheetOFX|filter|float|mean=1,1,1,1||-i Source=solid:1,1,1,1${cs_none1} resolution=64,64 rowsColumns=2,2 gap=8 --frames 0 --scale 0.5 --window 6,6,12,10"
    # magnified by 2.4 (the 720x576 checker:1 fits 1728 pixels): the filter phase is rounded to 1/64 of an
    # input pixel (#305), which gives 0.1911 at (42, 42) (0.1991 without the rounding)
    "ContactSheet magnified 2.4|net.sf.openfx.ContactSheetOFX|filter|float|tol=0.002+mean=0.1911,0.1911,0.1911,0.1911||-i Source=checker:1${cs_none1} resolution=1728,1383 rowsColumns=1,1 --frames 0 --window 42,42,43,43"
    "ContactSheet 1 gap|net.sf.openfx.ContactSheetOFX|filter|float|mean=1,1,1,1||-m 1 -i Source=solid:1,1,1,1${cs1_none1} resolution=64,64 rowsColumns=2,2 gap=8 --frames 0 --window 4,12,8,16"
    # input 10 (#401): 4x4 cells of 32x32 pixels, top to bottom: input 10 is in the third row from the
    # top (y 32 to 64) and the third column (x 64 to 96). Its clip is "10" in ContactSheet 2, "01" in 1.
    "ContactSheet input 10|net.sf.openfx.ContactSheetOFX|filter|float|mean=0.5,0.5,0.5,1||-i Source=solid:1,1,1,1 -i 10=solid:0.5,0.5,0.5,1${cs_only10} resolution=128,128 rowsColumns=4,4 rowOrder=0 --frames 0 --window 68,36,76,44"
    "ContactSheet 1 input 10|net.sf.openfx.ContactSheetOFX|filter|float|mean=0.5,0.5,0.5,1||-m 1 -i Source=solid:1,1,1,1 -i 01=solid:0.5,0.5,0.5,1${cs1_only10} resolution=128,128 rowsColumns=4,4 rowOrder=0 --frames 0 --window 68,36,76,44"
    "AppendClip dissolve 4 after|net.sf.openfx.AppendClip|general|*|identity+mean=0.5,0.5,0.5,1||-i 1=default -i 2=solid:0.5,0.5,0.5,1 crossDissolve=4 --frames 27"
    # OpenGL renders (hostDemo --gpu=opengl; skipped if hostDemo has no OpenGL context here): every
    # render must be done with OpenGL. Shadertoy's default shader mixes iChannel0 with a sine
    # pattern whose blue follows iTime; TestOpenGL draws its red rectangle over the scaled Source.
    "Shadertoy OpenGL|net.sf.openfx.Shadertoy|general|*|mean=0.511,0.505,0.5,1+framesdiffer||--gpu=opengl -i iChannel0=ramp"
    "TestOpenGL OpenGL|net.sf.openfx.TestOpenGL|filter|*|mean=0.489,0.443,0.443,0.537+changed:Source||--gpu=opengl -i Source=ramp"
)

n=0 failed=() xfailed=() skipped=()
for t in "${tests[@]}"; do
    IFS='|' read -r label id ctx depths expect bug args <<< "$t"
    if [ -n "$RENDERTEST_ONLY" ] && ! echo "$label" | grep -E -e "$RENDERTEST_ONLY" > /dev/null; then
        continue
    fi
    [ "$depths" != '*' ] || depths="byte short float"
    case "$bug" in debug:*) [ $debug = 1 ] && bug=${bug#debug:} || bug= ;; esac
    expected_error=0
    case "$bug" in error:*) expected_error=1 bug=${bug#error:} ;; esac
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
        frames=2 views=2 scale=() nowindow=() gpu=0
        while [ $# -gt 0 ]; do
            case "$1" in
                --frames) f=${2//[^,]/}; frames=$(( ${#f} + 1 )) ;;
                # an OpenGL render (with Test and Shadertoy in the plugin path)
                --gpu=opengl) gpu=1 ;;
                # hostDemo --ofx14 and --ofx11 render one view
                --ofx14|--ofx11) views=1 ;;
                --scale) scale=(--scale "$2") ;;
            esac
            if [ "$1" = --window ]; then shift; else nowindow+=("$1"); fi
            shift
        done
        if [ $gpu = 1 ] && [ $gpu_ok = 0 ]; then
            printf 'SKIP  %-26s hostDemo does not render with OpenGL here\n' "$name"
            skipped+=("$name")
            continue
        fi
        refs=("frames=$frames" "views=$views" "gpu=$gpu") refok=1
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
        t0=$(date +%s)
        if ( [ $gpu = 0 ] || export OFX_PLUGIN_PATH=$gl_path
             # shellcheck disable=SC2086
             hostdemo run.log -p "$id" -c "$ctx" -d "$depth" --format pfm -o out --report report.json $args ); then
            refs+=("elapsed=$(( $(date +%s) - t0 ))")
            result=$(check report.json "$depth" "$expect" "${refs[@]}") && ok=1 || ok=0
        else
            status=$?
            ok=0
            result="hostDemo exit status $status"
            [ ! -f report.json ] || result="$result: $(check report.json "$depth" "$expect" "${refs[@]}" || true)"
        fi
        if [ -n "$bug" ] && [ $expected_error = 1 ]; then
            if [ $ok = 1 ]; then
                result="$result (expected error \"$bug\" not raised)"
                ok=0
            elif { echo "$result"; cat run.log; } | grep -E -e "$bugmsg" > /dev/null; then
                printf 'ok    %-26s expected error: %s\n' "$name" "$bug"
                continue
            else
                result="$result (expected error \"$bug\", but not the expected message \"$bugmsg\")"
            fi
        elif [ -n "$bug" ]; then
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

if [ $n = 0 ]; then
    echo "no render matches RENDERTEST_ONLY=$RENDERTEST_ONLY"
    exit 1
fi
echo
echo "rendertest: $((n - ${#failed[@]} - ${#xfailed[@]} - ${#skipped[@]})) passed, ${#xfailed[@]} known bugs (XFAIL), ${#failed[@]} failed, ${#skipped[@]} skipped (no OpenGL), $n renders in $(( $(date +%s) - start ))s"
if [ ${#failed[@]} -ne 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
