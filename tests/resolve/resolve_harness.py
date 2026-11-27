"""Helpers of the DaVinci Resolve tests (see README.md).

openfx_misc_tests.lua runs inside Resolve and prints its results as a JSON
line between RESULTS_BEGIN and RESULTS_END on Resolve's stdout
(resolve-stdout.log). prepare_run() turns the newest block into a results
directory, which test_resolve.py checks with the other helpers.
"""

import json
import os
import shutil
import struct
import xml.etree.ElementTree as ElementTree
import zlib

SUPPORT = os.path.expanduser("~/Library/Application Support/"
                             "Blackmagic Design/DaVinci Resolve")
PLUGIN_CACHE = os.path.join(SUPPORT, "OFXPluginCacheV2.xml")
# Resolve's logs of its last launch.
LOGS = ("ResolveDebug.txt", "davinci_resolve.log")
# Lines of ResolveDebug.txt worth reporting.
LOG_PATTERNS = ("Caught termination signal", "[CRASH DUMP]",
                "uncaught exception", "not supported on this platform")
CRASH_PATTERNS = LOG_PATTERNS[:3]
# Resolve writes its logs when it quits, like the end of resolve-stdout.log:
# logs written within this many seconds of it are from the same session.
SAME_SESSION = 600

RESULTS_BEGIN = "OPENFX-MISC-RESULTS-BEGIN"
RESULTS_END = "OPENFX-MISC-RESULTS-END"
STDOUT_LOG = "resolve-stdout.log"
# Pixel values: one 8-bit step, plus one for rounding.
TOLERANCE = 2.0 / 255


def config_dir():
    """$OPENFX_MISC_RESOLVE_RESULTS, or ~/openfx-misc-resolve-results."""
    return (os.environ.get("OPENFX_MISC_RESOLVE_RESULTS")
            or os.path.expanduser("~/openfx-misc-resolve-results"))


def results_blocks(text):
    """The results that openfx_misc_tests.lua printed, oldest first.

    Returns:
        A list of dicts, one per complete BEGIN/END block.

    Raises:
        ValueError: a block is not valid JSON.
    """
    blocks = []
    body = None
    for line in text.splitlines():
        if line.startswith(RESULTS_BEGIN):
            body = []
        elif line.startswith(RESULTS_END) and body is not None:
            blocks.append(json.loads("\n".join(body)))
            body = None
        elif body is not None:
            body.append(line)
    return blocks


def newest_results(log):
    """The newest results block of a log file, or None."""
    try:
        with open(log, errors="replace") as f:
            blocks = results_blocks(f.read())
    except OSError:
        return None
    return blocks[-1] if blocks else None


def plugin_bundles(plugin, cache=PLUGIN_CACHE):
    """[bundle path, major version] pairs of plugin in Resolve's cache."""
    try:
        root = ElementTree.parse(cache).getroot()
    except (OSError, ElementTree.ParseError):
        return []
    found = set()
    for bundle in root.iter("bundle"):
        binary = bundle.find("binary")
        path = binary.get("bundle_path", "") if binary is not None else ""
        for desc in bundle.iter("plugin"):
            if desc.get("name") == plugin:
                found.add((path, desc.get("major_version", "")))
    return [list(item) for item in sorted(found)]


def _as_list(value):
    """The Lua encoder writes empty tables as {}; read them as lists."""
    return value if isinstance(value, list) else []


def prepare_run(log, cache=PLUGIN_CACHE):
    """Makes the results directory of the newest block of log.

    The directory is <root>/<stamp> (root and stamp from the block). It gets
    a copy of the log, the exported frames (<case>.<ext>, from the paths the
    script wrote), and results.json: the block, with each frame's "file" and
    each tool's plug-in cache "bundles" added. Running it again is harmless.

    Returns:
        The directory, or None if the log has no results block.
    """
    results = newest_results(log)
    if results is None:
        return None
    run = os.path.join(results["root"], results["stamp"])
    os.makedirs(run, exist_ok=True)
    shutil.copy2(log, os.path.join(run, STDOUT_LOG))  # keeps its mtime
    for case in _as_list(results.get("cases")):
        frames = case.get("frames") or {}
        for fmt, frame in frames.items():
            path = frame.get("path")
            frame["file"] = None
            if path and os.path.isfile(path):
                name = "%s.%s" % (case["name"], fmt)
                shutil.copy(path, os.path.join(run, name))
                frame["file"] = name
        for tool in _as_list(case.get("tools")):
            tool["bundles"] = plugin_bundles(tool["plugin"], cache)
    with open(os.path.join(run, "results.json"), "w") as f:
        json.dump(results, f, indent=1, sort_keys=True)
    return run


def load_results(run):
    """The parsed results.json of a results directory."""
    with open(os.path.join(run, "results.json")) as f:
        return json.load(f)


def plugin_problems(results):
    """Plug-ins missing from Resolve's cache, or loaded from elsewhere than
    OFX_PLUGIN_PATH: a list of messages."""
    path = results.get("ofx_plugin_path")
    if not path:
        return ["Resolve was not started with OFX_PLUGIN_PATH"]
    dirs = [os.path.join(d, "") for d in path.split(":") if d]
    problems = []
    for case in _as_list(results.get("cases")):
        for tool in _as_list(case.get("tools")):
            bundles = [b for b, _ in tool.get("bundles", [])]
            if not bundles:
                problems.append("%s is not in Resolve's plug-in cache"
                                % tool["plugin"])
            foreign = [b for b in bundles
                       if not any(b.startswith(d) for d in dirs)]
            if foreign:
                problems.append("%s also loaded from %s"
                                % (tool["plugin"], ", ".join(foreign)))
    return problems


def case_problems(result, run, size):
    """What is wrong with one case's result: a list of messages.

    Args:
        result: the case's entry in results.json.
        run: the results directory.
        size: the expected [width, height] of the frame.
    """
    if result.get("error"):
        return [result["error"]]
    problems = []
    for tool in _as_list(result.get("tools")):
        if not tool.get("reg_id"):
            problems.append("no RegID for %s" % tool.get("plugin"))
        for name, done in (tool.get("set") or {}).items():
            read = _as_list(done.get("read"))
            if not _as_list(done.get("inputs")) or None in read or not read:
                problems.append("param %s of %s: %s"
                                % (name, tool["plugin"], done))
    frame = (result.get("frames") or {}).get("png") or {}
    if not frame.get("success") or not frame.get("file"):
        return problems + ["no PNG frame: %s" % frame]
    with open(os.path.join(run, frame["file"]), "rb") as f:
        image = decode_png(f.read())
    if [image.width, image.height] != list(size):
        return problems + ["frame is %dx%d, expected %dx%d"
                           % (image.width, image.height, size[0], size[1])]
    for x, y, rgb in _as_list(result.get("checks")):
        value = image.pixel(x, y)[:3]
        if any(abs(v - e) > TOLERANCE for v, e in zip(value, rgb)):
            problems.append("pixel (%d, %d) is %s, expected %s"
                            % (x, y, [round(v, 4) for v in value], rgb))
    return problems


def copy_logs(run):
    """Copies Resolve's logs of the run's session into run.

    They are from the same session if written within SAME_SESSION seconds
    of the run's resolve-stdout.log.

    Returns:
        The path of the copied ResolveDebug.txt, or None.
    """
    since = os.path.getmtime(os.path.join(run, STDOUT_LOG))
    copied = None
    for name in LOGS:
        src = os.path.join(SUPPORT, "logs", name)
        if os.path.isfile(src) and abs(os.path.getmtime(src) - since) <= \
                SAME_SESSION:
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
