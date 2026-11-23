#!/usr/bin/env python3
"""Compare openfx-misc plug-ins with the Nuke nodes they clone (local tool, not run in CI).

Each case renders the same small synthetic image through a Nuke node (in Nuke's terminal mode,
`nuke -t`, one process per case, which works with a Nuke Non-Commercial licence once you are
signed in) and through the
matching openfx-misc plug-in (with openfx's hostDemo), and compares the pixel values. The inputs
go well outside [0, 1] (down to -1 and up to 3), so that differences in how a node treats
negative values or values above 1 show up (e.g. Grade's gamma above 1, issue #261).

Usage:
    .github/tools/nukecompare.py [--nuke NUKE] [--only REGEX] [--keep DIR] [--list] PREFIX

PREFIX is a CMake install prefix of openfx-misc configured with -DOFX_BUILD_HOSTDEMO=ON (bundles
in PREFIX/OFX/Plugins, hostDemo in PREFIX/bin). NUKE is the Nuke executable (default: $NUKE, or
the newest /Applications/Nuke*/Nuke*.app/Contents/MacOS/Nuke* on macOS). $HOSTDEMO, if set, is
used to run hostDemo (e.g. a wrapper), with $HOSTDEMO_REAL as the real binary.

Exit status: 0 if every case matches within its tolerance or is a known divergence that still
diverges; 1 otherwise (a new divergence, or a known one that now matches: update the case).
"""

import argparse
import glob
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

WIDTH = 32
HEIGHT = 32

# Input images: (Nuke Expression node expressions, Python function of (x, y) -> (r, g, b, a)).
# x and y are pixel indices from the bottom left, as in Nuke's Expression node and OFX images.
# The expressions and the functions must give the same values.
INPUTS = {
    # Every channel goes outside [0, 1]: R from -1 to 3 (left to right), G from -0.5 to 2
    # (bottom to top), B from 1.5 down to -0.5; opaque.
    "ramp": (
        ("-1 + 4 * (x + 0.5) / %d" % WIDTH,
         "-0.5 + 2.5 * (y + 0.5) / %d" % HEIGHT,
         "1.5 - 2 * (x + 0.5) / %d" % WIDTH,
         "1"),
        lambda x, y: (-1. + 4. * (x + .5) / WIDTH,
                      -.5 + 2.5 * (y + .5) / HEIGHT,
                      1.5 - 2. * (x + .5) / WIDTH,
                      1.),
    ),
    # One white pixel at the centre, on black; opaque.
    "impulse": (
        ("(x == %d && y == %d) ? 1 : 0" % (WIDTH // 2, HEIGHT // 2),) * 3 + ("1",),
        lambda x, y: (1., 1., 1., 1.) if (x, y) == (WIDTH // 2, HEIGHT // 2) else (0., 0., 0., 1.),
    ),
}

# The cases. "nuke": the node class and its knobs (nuke.nodes.<Class>(**knobs)).
# "ofx": the plug-in identifier and its params (hostDemo -s name=value; a list for a
# multi-dimensional param). "channels": which channels to compare. "tolerance": the largest
# accepted absolute difference. "known": the issue of a known divergence (the case is expected
# to differ); "known": None means the outputs must match.
CASES = [
    {"name": "Grade gamma 0.7", "input": "ramp",
     "nuke": ("Grade", {"gamma": 0.7}),
     "ofx": ("net.sf.openfx.GradePlugin", {"gamma": [0.7, 0.7, 0.7, 0.7]}),
     "channels": "rgb", "tolerance": 1e-4, "known": 261},
    {"name": "Grade defaults", "input": "ramp",
     "nuke": ("Grade", {}),
     "ofx": ("net.sf.openfx.GradePlugin", {}),
     "channels": "rgb", "tolerance": 1e-5, "known": None},
    {"name": "Gamma 0.7", "input": "ramp",
     "nuke": ("Gamma", {"value": 0.7}),
     "ofx": ("net.sf.openfx.GammaPlugin", {"value": [0.7, 0.7, 0.7, 0.7]}),
     "channels": "rgb", "tolerance": 1e-4, "known": None},
    {"name": "Multiply 2", "input": "ramp",
     "nuke": ("Multiply", {"value": 2}),
     "ofx": ("net.sf.openfx.MultiplyPlugin", {"value": [2, 2, 2, 2]}),
     "channels": "rgb", "tolerance": 1e-5, "known": None},
    {"name": "Add 0.5", "input": "ramp",
     "nuke": ("Add", {"value": 0.5}),
     "ofx": ("net.sf.openfx.AddPlugin", {"value": [0.5, 0.5, 0.5, 0.5]}),
     "channels": "rgb", "tolerance": 1e-5, "known": None},
    {"name": "Clamp defaults", "input": "ramp",
     "nuke": ("Clamp", {}),
     "ofx": ("net.sf.openfx.Clamp", {}),
     "channels": "rgb", "tolerance": 1e-5, "known": None},
    {"name": "Invert", "input": "ramp",
     "nuke": ("Invert", {}),
     "ofx": ("net.sf.openfx.Invert", {}),
     "channels": "rgb", "tolerance": 1e-5, "known": None},
    {"name": "Saturation 0.5", "input": "ramp",
     "nuke": ("Saturation", {"saturation": 0.5}),
     "ofx": ("net.sf.openfx.SaturationPlugin", {"saturation": 0.5}),
     "channels": "rgb", "tolerance": 1e-4, "known": None},
    {"name": "Laplacian size 3", "input": "impulse",
     "nuke": ("Laplacian", {"size": 3}),
     "ofx": ("net.sf.cimg.CImgLaplacian", {"size": [3, 3]}),
     "channels": "rgb", "tolerance": 0.03, "known": None},
]

CHANNEL_INDEX = {"r": 0, "g": 1, "b": 2, "a": 3}


def write_pfm(path, pixels, components):
    """Write rows (bottom to top) of (r, g, b, a) tuples as a little-endian PFM file.

    components is 3 (the RGB file, "PF") or 1 (the alpha file, "Pf").
    """
    with open(path, "wb") as f:
        f.write(("PF\n" if components == 3 else "Pf\n").encode())
        f.write(("%d %d\n-1.0\n" % (WIDTH, HEIGHT)).encode())
        for row in pixels:
            for p in row:
                values = p[:3] if components == 3 else (p[3],)
                f.write(struct.pack("<%df" % len(values), *values))


def read_pfm(path):
    """Read a PFM file; returns (width, height, components, rows bottom to top of tuples)."""
    with open(path, "rb") as f:
        kind = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        components = 3 if kind == b"PF" else 1
        endian = "<" if scale < 0 else ">"
        count = width * components
        rows = []
        for _ in range(height):
            values = struct.unpack(endian + "%df" % count, f.read(4 * count))
            rows.append([values[i * components:(i + 1) * components] for i in range(width)])
    return width, height, components, rows


def nuke_script(cases, result_path):
    """A Python script for `nuke -t` that builds the cases and samples their output."""
    lines = [
        "import json",
        "import nuke",
        "nuke.addFormat('%d %d 1 nukecompare')" % (WIDTH, HEIGHT),
        "results = {}",
        "def sample_all(node):",
        "    rows = []",
        "    for y in range(%d):" % HEIGHT,
        "        rows.append([[node.sample(c, x + 0.5, y + 0.5) for c in",
        "                      ('rgba.red', 'rgba.green', 'rgba.blue', 'rgba.alpha')]",
        "                     for x in range(%d)])" % WIDTH,
        "    return rows",
    ]
    for case in cases:
        exprs = INPUTS[case["input"]][0]
        node_class, knobs = case["nuke"]
        lines += [
            "try:",
            "    c = nuke.nodes.Constant(format='nukecompare', color=0)",
            "    e = nuke.nodes.Expression(inputs=[c], expr0=%r, expr1=%r, expr2=%r, expr3=%r)" % exprs,
            "    n = nuke.nodes.%s(inputs=[e])" % node_class,
        ]
        for knob, value in knobs.items():
            lines.append("    n[%r].setValue(%r)" % (knob, value))
        lines += [
            "    results[%r] = sample_all(n)" % case["name"],
            "except Exception as ex:",
            "    results[%r] = 'error: %%s' %% ex" % case["name"],
        ]
    lines.append("json.dump(results, open(%r, 'w'))" % result_path)
    return "\n".join(lines) + "\n"


def find_nuke():
    """The Nuke executable: $NUKE, or the newest one in /Applications (macOS)."""
    if os.environ.get("NUKE"):
        return os.environ["NUKE"]
    candidates = [p for p in glob.glob("/Applications/Nuke*/Nuke*.app/Contents/MacOS/Nuke*")
                  if os.access(p, os.X_OK) and "Non-Commercial" not in p and "Studio" not in p
                  and "Assist" not in p and "Indie" not in p]
    return sorted(candidates)[-1] if candidates else None


def run_nuke(nuke, case, work):
    """Run one case in its own Nuke process (Nuke Non-Commercial allows only 10 node objects
    per Python session); returns the rows of (r, g, b, a), or an error string."""
    name = re.sub(r"[^A-Za-z0-9]+", "_", case["name"])
    script = os.path.join(work, name + ".nuke.py")
    result_path = os.path.join(work, name + ".nuke.json")
    with open(script, "w") as f:
        f.write(nuke_script([case], result_path))
    log = os.path.join(work, name + ".nuke.log")
    with open(log, "w") as out:
        status = subprocess.call([nuke, "--nc", "-t", script], stdin=subprocess.DEVNULL,
                                 stdout=out, stderr=subprocess.STDOUT, cwd=work)
    if status != 0 or not os.path.exists(result_path):
        return ("error: Nuke failed (exit status %d), see %s: are you signed in to Nuke "
                "Non-Commercial (or licensed)?" % (status, log))
    with open(result_path) as f:
        return json.load(f)[case["name"]]


def run_ofx(prefix, case, work):
    """Render the case with hostDemo; returns rows of (r, g, b, a), or an error string."""
    case_dir = os.path.join(work, re.sub(r"[^A-Za-z0-9]+", "_", case["name"]))
    os.makedirs(case_dir, exist_ok=True)
    function = INPUTS[case["input"]][1]
    pixels = [[function(x, y) for x in range(WIDTH)] for y in range(HEIGHT)]
    write_pfm(os.path.join(case_dir, "in.pfm"), pixels, 3)
    write_pfm(os.path.join(case_dir, "in.alpha.pfm"), pixels, 1)
    plugin, params = case["ofx"]
    hostdemo = os.environ.get("HOSTDEMO") or os.path.join(prefix, "bin", "hostDemo")
    command = [hostdemo, "--no-std-paths", "--no-cache", "--quiet", "-p", plugin,
               "-c", "filter", "-d", "float", "--format", "pfm", "--frames", "0",
               "-i", "Source=file:in.pfm+in.alpha.pfm", "-o", "out"]
    for name, value in params.items():
        values = value if isinstance(value, list) else [value]
        command += ["-s", "%s=%s" % (name, ",".join(repr(v) for v in values))]
    env = dict(os.environ, OFX_PLUGIN_PATH=os.path.join(prefix, "OFX", "Plugins"))
    if os.environ.get("HOSTDEMO") and not env.get("HOSTDEMO_REAL"):
        env["HOSTDEMO_REAL"] = os.path.join(prefix, "bin", "hostDemo")
    with open(os.path.join(case_dir, "hostdemo.log"), "w") as out:
        status = subprocess.call(command, stdin=subprocess.DEVNULL, stdout=out,
                                 stderr=subprocess.STDOUT, cwd=case_dir, env=env)
    rgb = sorted(glob.glob(os.path.join(case_dir, "out.0*.pfm")))
    rgb = [p for p in rgb if not p.endswith(".alpha.pfm")]
    if status != 0 or not rgb:
        return "error: hostDemo failed (exit status %d), see %s" % (status, case_dir)
    _, _, _, color = read_pfm(rgb[0])
    alpha_path = rgb[0][:-len(".pfm")] + ".alpha.pfm"
    alpha = read_pfm(alpha_path)[3] if os.path.exists(alpha_path) else None
    return [[tuple(color[y][x]) + ((alpha[y][x][0],) if alpha else (1.,))
             for x in range(WIDTH)] for y in range(HEIGHT)]


def compare(case, nuke_rows, ofx_rows):
    """Returns (max abs difference, where) over the compared channels."""
    worst, where = 0., None
    for y in range(HEIGHT):
        for x in range(WIDTH):
            for c in case["channels"]:
                i = CHANNEL_INDEX[c]
                d = abs(nuke_rows[y][x][i] - ofx_rows[y][x][i])
                if d > worst:
                    worst = d
                    where = "x=%d y=%d %s: Nuke %.6g, openfx-misc %.6g" % (
                        x, y, c, nuke_rows[y][x][i], ofx_rows[y][x][i])
    return worst, where


def main():
    parser = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    parser.add_argument("prefix", nargs="?", help="openfx-misc install prefix")
    parser.add_argument("--nuke", help="the Nuke executable")
    parser.add_argument("--only", help="only the cases whose name matches this regular expression")
    parser.add_argument("--keep", help="keep the work files in this directory")
    parser.add_argument("--list", action="store_true", help="list the cases and exit")
    args = parser.parse_args()
    cases = [c for c in CASES if not args.only or re.search(args.only, c["name"])]
    if args.list:
        for case in cases:
            print("%s: Nuke %s vs %s" % (case["name"], case["nuke"][0], case["ofx"][0]))
        return 0
    if not args.prefix:
        parser.error("PREFIX is required")
    nuke = args.nuke or find_nuke()
    if not nuke:
        sys.exit("nukecompare: no Nuke found: use --nuke or $NUKE")
    work = args.keep or tempfile.mkdtemp(prefix="nukecompare.")
    os.makedirs(work, exist_ok=True)
    failures = 0
    for case in cases:
        nuke_rows = run_nuke(nuke, case, work)
        ofx_rows = run_ofx(args.prefix, case, work)
        known = ("known divergence #%d" % case["known"]) if case["known"] else ""
        if isinstance(nuke_rows, str) or isinstance(ofx_rows, str):
            print("ERROR  %s: %s" % (case["name"], nuke_rows if isinstance(nuke_rows, str) else ofx_rows))
            failures += 1
            continue
        worst, where = compare(case, nuke_rows, ofx_rows)
        same = worst <= case["tolerance"]
        if same and not case["known"]:
            print("ok     %s (max diff %.3g)" % (case["name"], worst))
        elif not same and case["known"]:
            print("XFAIL  %s (%s; max diff %.3g at %s)" % (case["name"], known, worst, where))
        elif same:
            print("XPASS  %s (%s, but now matches: update the case)" % (case["name"], known))
            failures += 1
        else:
            print("DIFF   %s (max diff %.3g at %s)" % (case["name"], worst, where))
            failures += 1
    if not args.keep:
        shutil.rmtree(work, ignore_errors=True)
    print("nukecompare: %d cases, %d unexpected" % (len(cases), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
