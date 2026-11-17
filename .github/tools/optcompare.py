#!/usr/bin/env python3
# Compare two smoketest.sh --out directories (e.g. -O2 and -O3 builds of the same sources):
# per plug-in, the failures at either level, and the floating-point differences of the
# outputs (max abs difference, number of differing pixels, image size).
# Usage: optcompare.py A_DIR B_DIR [label_a label_b]
import gzip, json, os, re, sys
from collections import defaultdict

try:
    import numpy as np
except ImportError:
    np = None
import array

a_dir, b_dir = sys.argv[1:3]
la, lb = sys.argv[3:5] if len(sys.argv) >= 5 else ("A", "B")


def jobs(d):
    r = {}
    for line in open(os.path.join(d, "jobs.txt")):
        f = line.rstrip("\n").split("|")
        r[f[0]] = f
    return r


def status(d, name):
    j = os.path.join(d, "jobs", name)
    st = open(os.path.join(j, "status")).read().split()[0]
    try:
        rep = json.load(open(os.path.join(j, "report.json")))
    except (OSError, ValueError):
        rep = None
    ok = st == "0" and rep is not None and rep.get("ok")
    why = ""
    if not ok:
        log = open(os.path.join(j, "run.log"), errors="replace").read().strip().splitlines()
        why = "exit %s: %s" % (st, (log[-1] if log else "")[:120])
    return ok, why, rep


def read_pfm(path):
    with (gzip.open(path, "rb") if path.endswith(".gz") else open(path, "rb")) as f:
        data = f.read()
    h = data.split(b"\n", 3)
    kind = h[0].strip()
    w, hh = (int(v) for v in h[1].split())
    scale = float(h[2])
    nc = 3 if kind == b"PF" else 1
    raw = h[3][:w * hh * nc * 4]
    if np is not None:
        a = np.frombuffer(raw, dtype="<f4" if scale < 0 else ">f4").reshape(hh, w, nc)
    else:
        a = array.array("f")
        a.frombytes(raw)
        if (sys.byteorder == "little") != (scale < 0):
            a.byteswap()
    return w, hh, nc, a


def diff(pa, pb):
    wa, ha, ca, a = read_pfm(pa)
    wb, hb, cb, b = read_pfm(pb)
    if (wa, ha, ca) != (wb, hb, cb):
        return None, None, (wa, ha), "size %dx%dx%d vs %dx%dx%d" % (wa, ha, ca, wb, hb, cb)
    if np is not None:
        d = np.abs(a.astype(np.float64) - b.astype(np.float64))
        nan_a, nan_b = np.isnan(a), np.isnan(b)
        d[nan_a & nan_b] = 0
        d[nan_a ^ nan_b] = np.inf
        px = int(np.count_nonzero(np.any(d > 0, axis=2)))
        return float(d.max()) if d.size else 0.0, px, (wa, ha), None
    m, px = 0.0, 0
    for i in range(0, len(a), ca):
        dd = 0.0
        for c in range(ca):
            x, y = a[i + c], b[i + c]
            if x != y and not (x != x and y != y):
                dd = max(dd, abs(x - y) if x == x and y == y else float("inf"))
        if dd:
            px += 1
            m = max(m, dd)
    return m, px, (wa, ha), None


ja, jb = jobs(a_dir), jobs(b_dir)
fails = []
plug = defaultdict(lambda: {"renders": 0, "files": 0, "difffiles": 0, "maxdiff": 0.0, "px": 0,
                            "pixels": 0, "size": None, "depths": defaultdict(float), "notes": []})
for name in sorted(set(ja) | set(jb)):
    f = ja.get(name) or jb.get(name)
    pid, major = f[2], f[3]
    tag = "%s v%s" % (pid, major)
    if name not in ja or name not in jb:
        fails.append((tag, name, "only in %s" % (la if name in ja else lb)))
        continue
    oka, whya, ra = status(a_dir, name)
    okb, whyb, rb = status(b_dir, name)
    if not oka or not okb:
        fails.append((tag, name, "%s: %s | %s: %s" % (la, "ok" if oka else whya, lb, "ok" if okb else whyb)))
    p = plug[tag]
    p["renders"] += 1
    if not (ra and rb):
        continue
    ka = {(k["time"], k.get("view")): k for k in ra.get("renders") or []}
    kb = {(k["time"], k.get("view")): k for k in rb.get("renders") or []}
    for key in sorted(set(ka) & set(kb)):
        oa, ob = ka[key].get("output"), kb[key].get("output")
        if not oa or not ob:
            continue
        for fa in oa.get("files") or []:
            p["files"] += 1
            if oa["crc32"] == ob["crc32"]:
                # identical output image (all its files)
                p["pixels"] += oa["w"] * oa["h"]
                p["size"] = p["size"] or (oa["w"], oa["h"])
                continue
            pa = os.path.join(a_dir, "jobs", name, fa + ".gz")
            pb = os.path.join(b_dir, "jobs", name, fa + ".gz")
            if not os.path.exists(pa):
                pa = pa[:-3]
                pb = pb[:-3]
            m, px, size, note = diff(pa, pb)
            p["size"] = p["size"] or size
            p["pixels"] += size[0] * size[1]
            if note:
                p["notes"].append("%s %s" % (fa, note))
                continue
            if px:
                p["difffiles"] += 1
                p["px"] += px
                p["maxdiff"] = max(p["maxdiff"], m)
                dep = f[5]
                p["depths"][dep] = max(p["depths"][dep], m)

# describe failures (no render job)
for d in sorted(set(os.listdir(os.path.join(a_dir, "describe"))) | set(os.listdir(os.path.join(b_dir, "describe")))):
    st = []
    for x in (a_dir, b_dir):
        try:
            st.append(open(os.path.join(x, "describe", d, "status")).read().strip())
        except OSError:
            st.append("missing")
    if st != ["0", "0"]:
        fails.append((d.rsplit("-v", 1)[0] + " v" + d.rsplit("-v", 1)[1], "describe",
                      "%s: describe exit %s | %s: describe exit %s" % (la, st[0], lb, st[1])))

print("## Failures (load, describe, render, crash, timeout) at either level")
same = defaultdict(list)
differ = []
for tag, name, why in fails:
    a, _, b = why.partition(" | ")
    if a.split(": ", 1)[-1] == b.split(": ", 1)[-1]:
        same[tag].append((name, a.split(": ", 1)[-1]))
    else:
        differ.append((tag, name, why))
print("Different at the two levels: %s" % ("none" if not differ else ""))
for tag, name, why in differ:
    print("- %s (%s): %s" % (tag, name, why))
print()
print("The same at both levels (hostDemo exit status, last log line; smoketest.sh reports these as"
      " SKIP, XFAIL, or not failures: identity, empty RoD):")
for tag in sorted(same):
    print("- %s: %d (%s)" % (tag, len(same[tag]), same[tag][0][1]))
print()
print("## Floating-point differences of the outputs (%s vs %s)" % (la, lb))
nd = [t for t, p in plug.items() if p["difffiles"]]
print("%d plug-ins rendered, %d with identical outputs at both levels, %d with differences"
      % (len(plug), len(plug) - len(nd), len(nd)))
print()
print("| plug-in | max abs diff | differing pixels | of pixels compared | image size | depths (max diff) |")
print("|---|---|---|---|---|---|")
for t in sorted(nd, key=lambda t: -plug[t]["maxdiff"]):
    p = plug[t]
    print("| %s | %.3g | %d | %d | %s | %s |" % (
        t, p["maxdiff"], p["px"], p["pixels"], "%dx%d" % p["size"] if p["size"] else "?",
        ", ".join("%s %.3g" % (k, v) for k, v in sorted(p["depths"].items()))))
notes = [(t, n) for t, p in plug.items() for n in p["notes"]]
if notes:
    print()
    print("Output size mismatches:")
    for t, n in notes:
        print("- %s: %s" % (t, n))
