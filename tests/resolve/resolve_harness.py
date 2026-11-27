"""Helpers of the DaVinci Resolve tests: results, Resolve's log, PNG files.

openfx_misc_tests.py runs inside Resolve and writes a results directory;
test_resolve.py checks it with these helpers (see README.md).
"""

import json
import os
import shutil
import struct
import zlib

SUPPORT = os.path.expanduser("~/Library/Application Support/"
                             "Blackmagic Design/DaVinci Resolve")
# Resolve's logs of its last launch.
LOGS = ("ResolveDebug.txt", "davinci_resolve.log")
# Lines of ResolveDebug.txt worth reporting.
LOG_PATTERNS = ("Caught termination signal", "[CRASH DUMP]",
                "uncaught exception", "not supported on this platform")
CRASH_PATTERNS = LOG_PATTERNS[:3]


def latest_results(root):
    """The newest results directory under root, or None.

    A results directory is a subdirectory with a results.json; their names
    are timestamps, so the newest sorts last.
    """
    try:
        names = sorted(os.listdir(root))
    except OSError:
        return None
    runs = [os.path.join(root, n) for n in names
            if os.path.isfile(os.path.join(root, n, "results.json"))]
    return runs[-1] if runs else None


def load_results(run):
    """The parsed results.json of a results directory."""
    with open(os.path.join(run, "results.json")) as f:
        return json.load(f)


def copy_logs(run):
    """Copies Resolve's logs into run if they are newer than results.json.

    Resolve writes the end of its log when it quits, after the script, so
    the logs are collected by the pytest side.

    Returns:
        The path of the copied ResolveDebug.txt, or None.
    """
    since = os.path.getmtime(os.path.join(run, "results.json"))
    copied = None
    for name in LOGS:
        src = os.path.join(SUPPORT, "logs", name)
        if os.path.isfile(src) and os.path.getmtime(src) >= since:
            dst = os.path.join(run, name)
            shutil.copy(src, dst)
            if name == LOGS[0]:
                copied = dst
    return copied


def log_findings(path, patterns=LOG_PATTERNS):
    """The lines of a Resolve log that contain one of patterns."""
    try:
        with open(path, errors="replace") as log:
            return [line.rstrip() for line in log
                    if any(p in line for p in patterns)]
    except OSError:
        return []


HOST_DESCRIPTION_START = "OFX host description follows"
HOST_DESCRIPTION_END = "OFX DebugProxy: host description finished"


def host_descriptions(text):
    """The OFX host descriptions in a log, in README-hosts.txt's format.

    Rectangle built with -DDEBUG_HOSTDESCRIPTION prints one in describe(),
    between HOST_DESCRIPTION_START and HOST_DESCRIPTION_END: "OFX API
    version X.Y" then key=value lines. The trailing comma of the suites line
    is dropped.

    Returns:
        The distinct descriptions, in order, each a string of lines.
    """
    found = []
    block = None
    for line in text.splitlines():
        line = line.rstrip()
        if line.endswith(HOST_DESCRIPTION_START):
            block = []
        elif block is not None and line.endswith(HOST_DESCRIPTION_END):
            desc = "\n".join(block) + "\n"
            if block and desc not in found:
                found.append(desc)
            block = None
        elif block is not None:
            if line.startswith("suites="):
                line = line.rstrip(",")
            block.append(line)
    return found


def host_description_entry(resolve_version, descriptions):
    """A README-hosts.txt entry: "Version N (X.Y.Z):", then the description(s)."""
    major = resolve_version.split(".")[0] if resolve_version else "?"
    head = "Version %s (%s):\n\n" % (major, resolve_version)
    return head + "\n".join(descriptions)


class Image:
    """An 8- or 16-bit image, as decoded from a PNG file."""

    def __init__(self, width, height, channels, maxval, rows):
        self.width = width
        self.height = height
        self.channels = channels
        self._maxval = maxval
        self._rows = rows  # rows[y] is a list of ints, top row first

    def pixel(self, x, y):
        """The values (0 to 1) of the pixel at x, y; y = 0 is the top row."""
        row = self._rows[y]
        first = x * self.channels
        return [v / self._maxval
                for v in row[first:first + self.channels]]


def paeth(a, b, c):
    """The PNG Paeth predictor of a (left), b (up) and c (upper left)."""
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def decode_png(data):
    """Decodes a non-interlaced grey, grey+alpha, RGB or RGBA PNG.

    Args:
        data: the PNG file contents (bytes).

    Returns:
        An Image.

    Raises:
        ValueError: the file is not a PNG this decoder reads.
    """
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    header = None
    idat = []
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat.append(chunk)
        elif kind == b"IEND":
            break
    if header is None:
        raise ValueError("PNG without IHDR")
    width, height, depth, colour, _, _, interlace = header
    channels = {0: 1, 2: 3, 4: 2, 6: 4}.get(colour)
    if channels is None or depth not in (8, 16) or interlace:
        raise ValueError("unsupported PNG: colour type %d, depth %d, "
                         "interlace %d" % (colour, depth, interlace))
    nbytes = depth // 8
    bpp = channels * nbytes
    stride = width * bpp
    raw = zlib.decompress(b"".join(idat))
    rows = []
    prev = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind = raw[start]
        line = bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = line[i - bpp] if i >= bpp else 0
            up = prev[i]
            upleft = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + left) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + up) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                line[i] = (line[i] + paeth(left, up, upleft)) & 0xFF
            elif kind != 0:
                raise ValueError("bad PNG filter type %d" % kind)
        if nbytes == 1:
            rows.append(list(line))
        else:
            rows.append(list(struct.unpack(">%dH" % (stride // 2), line)))
        prev = line
    return Image(width, height, channels, (1 << depth) - 1, rows)
