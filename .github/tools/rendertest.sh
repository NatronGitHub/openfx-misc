#!/bin/bash
# Headless render smoke test used by the CI: build hostDemo (the OFX test host
# of the openfx submodule) and render a representative set of plugins (all the
# generators and some common filters) at the byte, short and float depths,
# with the *.ofx.bundle found under the given directories (default: the
# source tree). Linux and macOS only: hostDemo is built with the openfx
# Makefiles, and the checks need python3.
# Usage (from the top of the source tree): .github/tools/rendertest.sh [DIR...]
# Set HOSTDEMO to the path of an existing hostDemo binary to skip its build.
#
# hostDemo renders the frames t=0 and t=1 of a PAL SD project (720x576) on
# synthetic inputs (-i Clip=solid:r,g,b,a|ramp|checker:N|none), with the
# depth and components that the clip preferences of the plugin map each
# clip to. A render fails if:
# - hostDemo fails (exit status not 0): an action failed, an image fetch
#   failed, a float output pixel was not written (hostDemo fills the float
#   output images with NaN), or the plugin crashed;
# - the --report JSON file shows a failed render, an output depth other than
#   the requested one, or an error or fatal message from the plugin;
# - the output at t=0 does not have the expected mean value (mean=r,g,b,a,
#   normalized to [0,1]), or is constant though it should not be (nonconst).
# The output images are not compared to reference images.
set -eo pipefail

case "$(uname -s)" in
    Darwin|Linux) ;;
    *) echo "unsupported OS $(uname -s)"; exit 1 ;;
esac

top=$PWD
start=$(date +%s)
work=$(mktemp -d "${TMPDIR:-/tmp}/rendertest.XXXXXX")
trap 'rm -rf "$work"' EXIT

# Build hostDemo with the openfx Makefiles, as openfx's CI does
# (openfx/.github/scripts/make-build-test.sh). hostDemo is built without the
# OFX_EXTENSIONS_* flags, so it renders a single view.
if [ -z "$HOSTDEMO" ]; then
    hs=$top/openfx/HostSupport
    dst=$(uname -s)-release
    if [ ! -f "$hs/$dst/libexpat.a" ]; then
        # git checkout gives the files arbitrary mtimes, which may make the
        # bundled expat's Makefile try to re-run aclocal/automake
        ( cd "$hs"/expat-*/ && touch aclocal.m4 && sleep 1 &&
              touch configure Makefile.in */Makefile.in expat_config.h.in )
    fi
    jobs=$(getconf _NPROCESSORS_ONLN 2> /dev/null || echo 2)
    echo "Building hostDemo..."
    log=$work/hostdemo-build.log
    { make -C "$hs" -j"$jobs" && make -C "$hs/examples" -j"$jobs" "$dst/hostDemo"; } > "$log" 2>&1 ||
        { tail -n 50 "$log"; echo "hostDemo build failed"; exit 1; }
    HOSTDEMO=$hs/examples/$dst/hostDemo
    echo "hostDemo built in $(( $(date +%s) - start ))s"
fi
[ -x "$HOSTDEMO" ] || { echo "hostDemo not found: $HOSTDEMO"; exit 1; }

# The plugin path: the directories that hold our bundles, and nothing else
# (hostDemo --no-std-paths). Skip the openfx submodule (not our plugins),
# DebugProxy (debug builds only, it wraps the plugin named by
# $OFX_DEBUGPROXY_BINARY), Test (for developers) and Shadertoy (it only renders
# with OpenGL, which hostDemo does not provide).
dirs=()
while IFS= read -r d; do
    case "$d" in /*) ;; *) d=$top/${d#./} ;; esac
    dirs+=("$d")
done < <(cd "$top" && find "${@:-.}" \( -path ./openfx -o -path ./DebugProxy \
             -o -name Test.ofx.bundle -o -name Shadertoy.ofx.bundle \) -prune -o \
             -name '*.ofx.bundle' -print -prune | sed 's,/[^/]*$,,' | sort -u)
if [ ${#dirs[@]} -eq 0 ]; then
    echo "no *.ofx.bundle found"
    exit 1
fi
OFX_PLUGIN_PATH=$(IFS=:; echo "${dirs[*]}")
# debug builds (*-debug directories) have the plugins' assertions
case ":$OFX_PLUGIN_PATH:" in *-debug:*) debug=1 ;; *) debug=0 ;; esac
export OFX_PLUGIN_PATH
echo "OFX_PLUGIN_PATH=$OFX_PLUGIN_PATH"

cd "$work"

# check REPORT DEPTH EXPECT: check the --report JSON file of a hostDemo run
# (format: openfx/HostSupport/examples/python/README.md). Prints a short
# description of the output at t=0, then the reasons of the failure, if any
# (exit status 1).
check() {
    python3 -I - "$@" << 'EOF'
import json, sys
path, depth, expect = sys.argv[1:4]
with open(path) as f:
    r = json.load(f)
errors = []
if not r.get("ok") or r.get("exitStatus") != 0:
    errors.append("hostDemo failed (exit status %s)" % r.get("exitStatus"))
for m in r.get("messages") or []:
    if m.get("type") in ("OfxMessageError", "OfxMessageFatal"):
        errors.append("%s %s: %s" % (m.get("kind"), m.get("type"), m.get("text")))
renders = r.get("renders") or []
if len(renders) != 2:
    errors.append("%d renders, expected 2" % len(renders))
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
    if k.get("time") == 0:
        out0 = o
desc = ""
if out0:
    scale = {"byte": 255., "short": 65535., "float": 1.}[out0["depth"]]
    mean = [v / scale for v in out0["mean"]]
    desc = "%s %s mean=%s" % (out0["depth"], out0["comps"], ",".join("%.3g" % v for v in mean))
    if expect == "nonconst":
        if out0["min"] == out0["max"]:
            errors.append("t=0: constant output, expected an image")
    elif expect.startswith("mean="):
        want = [float(v) for v in expect[5:].split(",")]
        # 0.01: byte and short inputs and outputs are quantized
        if len(want) != len(mean) or any(abs(a - b) > 0.01 for a, b in zip(mean, want)):
            errors.append("t=0: mean %s, expected %s" % (",".join("%.4g" % v for v in mean), expect[5:]))
    elif expect != "any":
        errors.append("unknown expectation " + expect)
elif not errors:
    errors.append("no output at t=0")
print(desc)
for e in errors:
    print("  " + e)
sys.exit(1 if errors else 0)
EOF
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
# A render with a "known bug" (an issue or a short description) is expected to
# fail: it is reported as XFAIL and does not fail the test, but if it passes
# (XPASS) the test fails, so that the entry is updated when the bug is fixed.
# A "debug:" known bug (a failed assertion) is only expected in debug builds.
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
    "Invert checker|net.sf.openfx.Invert|filter|*|nonconst||-i Source=checker:16 -i Mask=none"
    "Grade solid|net.sf.openfx.GradePlugin|filter|*|mean=0.5,1,0.25,1||-i Source=solid:0.25,0.5,0.125,1 -i Mask=none multiply=2,2,2,2"
    "Grade ramp masked|net.sf.openfx.GradePlugin|filter|*|nonconst||-i Source=ramp -i Mask=checker:32 gamma=2,2,2,2"
    "Merge over solid|net.sf.openfx.MergePlugin|filter|*|mean=0.5,0,0.5,1||-i A=solid:0.5,0,0,0.5 -i B=solid:0,0,1,1 -i Mask=none"
    "Merge over checker|net.sf.openfx.MergePlugin|filter|*|nonconst||-i A=checker:16 -i B=ramp -i Mask=none"
    "CImgBlur|net.sf.cimg.CImgBlur|filter|float|nonconst||-i Source=checker:8 -i Mask=none size=5,5 expandRoD=0"
    "CImgBlur expandRoD|net.sf.cimg.CImgBlur|filter|float|nonconst|debug:CImgFilter asserts that the output RoD is the source RoD on hosts without multiresolution, but expandRoD enlarges it|-i Source=checker:8 -i Mask=none size=5,5"
    "Transform|net.sf.openfx.TransformPlugin|filter|*|nonconst||-i Source=checker:32 rotate=30 scale=0.8,0.8"
    "Crop|net.sf.openfx.CropPlugin|filter|*|nonconst||-i Source=ramp bottomLeft=100,100 size=400,300"
    "ColorCorrect|net.sf.openfx.ColorCorrectPlugin|filter|*|nonconst||-i Source=ramp -i Mask=none MasterSaturation=0.5,0.5,0.5,0.5"
    "Clamp|net.sf.openfx.Clamp|filter|*|mean=1,0,0.5,1||-i Source=solid:1.5,-0.5,0.5,1 -i Mask=none"
    # Shuffle (version 3) swaps R and B. The A and B inputs are the same here,
    # since Shuffle 3 takes A for B on hosts without the multiplane suites (see
    # the known bugs below).
    "Shuffle|net.sf.openfx.ShufflePlugin|general|*|mean=0.75,0.5,0.25,1||-i A=solid:0.25,0.5,0.75,1 -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle B|net.sf.openfx.ShufflePlugin|general|float|mean=0.75,0.5,0.25,1|Shuffle 3 without the multiplane suites renders A instead of B|-i A=ramp -i B=solid:0.25,0.5,0.75,1 outputR=8 outputB=6"
    "Shuffle filter|net.sf.openfx.ShufflePlugin|filter|float|mean=0.25,0.5,0.75,1|Shuffle in the filter context: crash in getRegionOfDefinition, render fails|-i Source=solid:0.25,0.5,0.75,1"
)

n=0 failed=() xfailed=()
for t in "${tests[@]}"; do
    IFS='|' read -r label id ctx depths expect bug args <<< "$t"
    [ "$depths" != '*' ] || depths="byte short float"
    case "$bug" in debug:*) [ $debug = 1 ] && bug=${bug#debug:} || bug= ;; esac
    for depth in $depths; do
        n=$((n + 1))
        name="$label ($depth)"
        rm -f out.* report.json
        # $args is split into words on purpose
        # shellcheck disable=SC2086
        if "$HOSTDEMO" --no-std-paths --no-cache --quiet -p "$id" -c "$ctx" -d "$depth" \
               --format pfm -o out --report report.json $args > run.log 2>&1; then
            result=$(check report.json "$depth" "$expect") && ok=1 || ok=0
        else
            status=$?
            ok=0
            result="hostDemo exit status $status"
            [ ! -f report.json ] || result="$result: $(check report.json "$depth" "$expect" || true)"
        fi
        if [ -n "$bug" ]; then
            if [ $ok = 1 ]; then
                printf 'XPASS %-26s %s\n' "$name" "$result"
                echo "  known bug \"$bug\" fixed? Update the test list of $0."
                failed+=("$name (XPASS)")
            else
                printf 'XFAIL %-26s %s\n' "$name" "$bug"
                xfailed+=("$name")
            fi
        elif [ $ok = 1 ]; then
            printf 'ok    %-26s %s\n' "$name" "$result"
        else
            printf 'FAIL  %-26s %s\n' "$name" "$result"
            sed 's/^/  | /' run.log
            echo "  | hostDemo -p $id -c $ctx -d $depth $args"
            failed+=("$name")
        fi
    done
done
rm -f out.* report.json

echo
echo "rendertest: $((n - ${#failed[@]} - ${#xfailed[@]})) passed, ${#xfailed[@]} known bugs (XFAIL), ${#failed[@]} failed, $n renders in $(( $(date +%s) - start ))s"
if [ ${#failed[@]} -ne 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
