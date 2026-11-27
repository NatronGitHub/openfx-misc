"""Checks the results of openfx_misc_tests.lua, run in Resolve (README.md)."""

import os
import struct
import subprocess
import zlib

import pytest

import resolve_harness

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURES = os.path.join(HERE, "fixtures")
FUSCRIPT = ("/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/"
            "Libraries/Fusion/fuscript")
CACHE = """<cache version="ResolveHostV1">
<bundle><binary bundle_path="{prefix}/OFX/Plugins/Misc.ofx.bundle" path="x" />
<plugin name="net.sf.openfx.CheckerBoardPlugin" major_version="2" />
<plugin name="net.sf.openfx.ConstantPlugin" major_version="1" />
<plugin name="net.sf.openfx.Invert" major_version="2" />
</bundle></cache>
"""


def _png(width, height, rows, filters):
    """Encodes 8-bit RGB rows, each with the given PNG filter type."""
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    bpp = 3
    raw = bytearray()
    prev = bytes(width * bpp)
    for row, kind in zip(rows, filters):
        line = bytes(row)
        out = bytearray()
        for i, value in enumerate(line):
            left = line[i - bpp] if i >= bpp else 0
            upleft = prev[i - bpp] if i >= bpp else 0
            pred = (0, left, prev[i], (left + prev[i]) // 2,
                    resolve_harness.paeth(left, prev[i], upleft))[kind]
            out.append((value - pred) & 0xFF)
        raw += bytes([kind]) + out
        prev = line
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw))) + chunk(b"IEND", b""))


def test_decode_png():
    """The PNG decoder reads back every filter type (no Resolve needed)."""
    width, height = 4, 5
    rows = [[(17 * x + 31 * y + 5 * c) % 256 for x in range(width)
             for c in range(3)] for y in range(height)]
    image = resolve_harness.decode_png(_png(width, height, rows,
                                            [0, 1, 2, 3, 4]))
    assert (image.width, image.height, image.channels) == (4, 5, 3)
    for y in range(height):
        for x in range(width):
            assert image.pixel(x, y) == [v / 255 for v in
                                         rows[y][3 * x:3 * x + 3]]


def _write_frames(results, size):
    """Writes the PNG of each case: black, with its check pixels set."""
    for case in results["cases"]:
        frame = case["frames"].get("png")
        if not frame or not frame.get("success"):
            continue
        width, height = size
        rows = [[0] * (3 * width) for _ in range(height)]
        for x, y, rgb in case["checks"]:
            rows[y][3 * x:3 * x + 3] = [round(v * 255) for v in rgb]
        with open(frame["path"], "wb") as f:
            f.write(_png(width, height, rows, [0] * height))


def _check_sample_run(tmp_path, log_text):
    """prepare_run and the checks on a log, with frames and a cache made up."""
    root = tmp_path / "results"
    root.mkdir()
    prefix = str(tmp_path / "prefix")
    log = tmp_path / resolve_harness.STDOUT_LOG
    log.write_text(log_text.replace("@ROOT@", str(root))
                   .replace("@PREFIX@", prefix))
    cache = tmp_path / "cache.xml"
    cache.write_text(CACHE.format(prefix=prefix))
    _write_frames(resolve_harness.newest_results(str(log)), (256, 256))
    run = resolve_harness.prepare_run(str(log), str(cache))
    results = resolve_harness.load_results(run)
    assert resolve_harness.plugin_problems(results) == []
    for case in results["cases"]:
        assert resolve_harness.case_problems(case, run, results["size"]) == [], \
            case["name"]
    return run, results


def test_sample_log(tmp_path):
    """The results block and host description of a sample log (no Resolve)."""
    with open(os.path.join(FIXTURES, "sample-stdout.txt")) as f:
        text = f.read()
    run, results = _check_sample_run(tmp_path, text)
    assert results["stamp"] == "20261128-000000"
    assert [c["name"] for c in results["cases"]] == [
        "checkerboard_defaults", "constant_color", "invert_checkerboard"]
    assert os.path.isfile(os.path.join(run, "checkerboard_defaults.png"))
    descriptions = resolve_harness.host_descriptions(text)
    assert len(descriptions) == 1  # the repeated dump is merged
    with open(os.path.join(FIXTURES, "sample-host-description.txt")) as f:
        expected = f.read()
    assert resolve_harness.host_description_entry(
        results["resolve_version"], descriptions) == expected


def test_sample_wrong_pixel(tmp_path):
    """A wrong pixel, a foreign bundle and a missing frame are reported."""
    with open(os.path.join(FIXTURES, "sample-stdout.txt")) as f:
        text = f.read()
    run, results = _check_sample_run(tmp_path, text)
    case = results["cases"][0]
    with open(os.path.join(run, case["frames"]["png"]["file"]), "wb") as f:
        f.write(_png(256, 256, [[0] * 768] * 256, [0] * 256))
    assert any("pixel" in p for p in
               resolve_harness.case_problems(case, run, results["size"]))
    case["tools"][0]["bundles"].append(["/elsewhere/Misc.ofx.bundle", "1"])
    assert any("also loaded" in p for p in
               resolve_harness.plugin_problems(results))
    case["frames"]["png"]["file"] = None
    assert any("no PNG" in p for p in
               resolve_harness.case_problems(case, run, results["size"]))


@pytest.mark.skipif(not os.access(FUSCRIPT, os.X_OK),
                    reason="no Resolve fuscript to run Lua")
def test_lua_script_with_fake_resolve(tmp_path):
    """openfx_misc_tests.lua runs with a fake Resolve (fuscript, offline)."""
    root = tmp_path / "results"
    root.mkdir()
    env = dict(os.environ, OPENFX_MISC_RESOLVE_RESULTS=str(root),
               OFX_PLUGIN_PATH="@PREFIX@/OFX/Plugins")
    proc = subprocess.run(
        [FUSCRIPT, "-l", "lua", "-q", "fake_resolve.lua",
         "openfx_misc_tests.lua"], cwd=HERE, env=env, check=False,
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, timeout=60)
    text = proc.stdout.decode("utf-8", "replace")
    assert proc.returncode == 0, text
    assert "openfx-misc tests: done" in text, text
    sub = tmp_path / "check"
    sub.mkdir()
    _, results = _check_sample_run(sub, text.replace(str(root), "@ROOT@"))
    assert results["project_deleted"] is True


@pytest.mark.resolve
def test_run(results):
    """The scratch project was set up and deleted."""
    assert not results.get("error"), results.get("error")
    assert results.get("settings") and all(results["settings"].values()), \
        results.get("settings")
    assert results.get("project_deleted") is True


@pytest.mark.resolve
def test_plugins_from_prefix(results):
    """Each plug-in was loaded only from OFX_PLUGIN_PATH (Resolve's cache)."""
    problems = resolve_harness.plugin_problems(results)
    assert not problems, "\n".join(problems)


@pytest.mark.resolve
def test_case(results, results_dir, case):
    """A case's tools were set up and its frame has the expected pixels."""
    if case is None:
        pytest.skip("no openfx_misc_tests results")
    found = [c for c in results.get("cases", []) if c.get("name") == case]
    assert found, "case %s is not in the results" % case
    problems = resolve_harness.case_problems(found[0], results_dir,
                                             results["size"])
    assert not problems, "\n".join(problems)


@pytest.mark.resolve
def test_resolve_log(results_dir):
    """Resolve's log of the session has no termination signal or crash."""
    log = resolve_harness.copy_logs(results_dir)
    if log is None:
        pytest.skip("Resolve's log is not from this session (quit Resolve "
                    "before running pytest)")
    crashes = resolve_harness.log_findings(log, resolve_harness.CRASH_PATTERNS)
    assert not crashes, "\n".join(crashes)


@pytest.mark.resolve
def test_host_description(results, results_dir):
    """Writes Resolve's OFX host description to host-description.txt."""
    log = os.path.join(results_dir, resolve_harness.STDOUT_LOG)
    with open(log, errors="replace") as f:
        descriptions = resolve_harness.host_descriptions(f.read())
    if not descriptions:
        pytest.skip("no host description in %s: build the prefix with "
                    "-DCMAKE_CXX_FLAGS=-DDEBUG_HOSTDESCRIPTION (README.md)"
                    % resolve_harness.STDOUT_LOG)
    entry = resolve_harness.host_description_entry(
        results.get("resolve_version"), descriptions)
    with open(os.path.join(results_dir, "host-description.txt"), "w") as f:
        f.write(entry)
    assert all(d.startswith("OFX API version ") for d in descriptions), entry
