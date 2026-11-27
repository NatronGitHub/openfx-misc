"""Renders openfx-misc plug-ins with DaVinci Resolve (see README.md)."""

import struct
import zlib

import pytest

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


@pytest.mark.resolve
def test_checkerboard_defaults(resolve_session):
    """CheckerBoard with its defaults: 64-pixel boxes from the centre.

    The boxes around the centre alternate between color0/color2 (0.1) and
    color1/color3 (0.5), with alpha 1. The pixels are 32 pixels from the
    centre, away from the centre lines.
    """
    image = resolve_session.render_generator(
        "net.sf.openfx.CheckerBoardPlugin", {})
    width, height = resolve_session.size
    assert (image.width, image.height) == (width, height)
    cx, cy = width // 2, height // 2
    corners = [image.pixel(cx + dx, cy + dy)
               for dx, dy in ((-32, -32), (32, -32), (32, 32), (-32, 32))]
    for value in corners:
        assert value[0] == pytest.approx(value[1], abs=TOLERANCE)
        assert value[0] == pytest.approx(value[2], abs=TOLERANCE)
        if image.channels == 4:
            assert value[3] == pytest.approx(1.0, abs=TOLERANCE)
    # Diagonal boxes have the same colour; neighbours differ.
    assert corners[0][0] == pytest.approx(corners[2][0], abs=TOLERANCE)
    assert corners[1][0] == pytest.approx(corners[3][0], abs=TOLERANCE)
    dark, light = sorted((corners[0][0], corners[1][0]))
    assert dark == pytest.approx(0.1, abs=TOLERANCE), corners
    assert light == pytest.approx(0.5, abs=TOLERANCE), corners
