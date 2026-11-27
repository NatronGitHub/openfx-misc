"""Checks the results of openfx_misc_tests.py, run in Resolve (README.md)."""

import os
import struct
import zlib

import pytest

import openfx_misc_tests
import resolve_harness

# One 8-bit step, plus one for rounding.
TOLERANCE = 2.0 / 255


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


def _case(results, name):
    for case in results.get("cases", []):
        if case.get("name") == name:
            return case
    pytest.fail("case %s is not in results.json" % name)
    return None


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
    path = results.get("ofx_plugin_path")
    assert path, "Resolve was not started with OFX_PLUGIN_PATH"
    dirs = [os.path.join(d, "") for d in path.split(":") if d]
    for case in results.get("cases", []):
        for tool in case.get("tools", []):
            bundles = [b for b, _ in tool.get("bundles", [])]
            assert bundles, "%s is not in Resolve's plug-in cache" % (
                tool["plugin"])
            foreign = [b for b in bundles
                       if not any(b.startswith(d) for d in dirs)]
            assert not foreign, "%s also loaded from %s" % (tool["plugin"],
                                                            foreign)


@pytest.mark.resolve
@pytest.mark.parametrize("case", openfx_misc_tests.CASES,
                         ids=[c["name"] for c in openfx_misc_tests.CASES])
def test_case(results, results_dir, case):
    """The exported frame has the expected pixel values."""
    result = _case(results, case["name"])
    assert not result.get("error"), result.get("error")
    for tool in result["tools"]:
        assert tool.get("reg_id"), tool
        for name, done in tool.get("set", {}).items():
            assert done.get("inputs") and None not in done.get("read", [None]), \
                "param %s of %s: %s" % (name, tool["plugin"], done)
    frame = result["frames"].get("png") or {}
    assert frame.get("success") and frame.get("file"), frame
    with open(os.path.join(results_dir, frame["file"]), "rb") as f:
        image = resolve_harness.decode_png(f.read())
    assert [image.width, image.height] == results["size"]
    for (x, y), rgb in case["checks"]:
        value = image.pixel(x, y)[:3]
        assert value == pytest.approx(list(rgb), abs=TOLERANCE), (
            "pixel (%d, %d): %s, expected %s" % (x, y, value, rgb))


@pytest.mark.resolve
def test_resolve_log(results_dir):
    """Resolve's log of the run has no termination signal or crash."""
    log = resolve_harness.copy_logs(results_dir)
    if log is None:
        pytest.skip("Resolve's log is older than the results: quit Resolve "
                    "first")
    crashes = resolve_harness.log_findings(log, resolve_harness.CRASH_PATTERNS)
    assert not crashes, "\n".join(crashes)
