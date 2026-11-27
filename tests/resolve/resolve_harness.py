"""Starts DaVinci Resolve and renders OFX plug-ins through its scripting API.

The scripting API is documented in Resolve's install, in
/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/
(Scripting/README.md, Scripting/DaVinciResolveScript.pyi and
Fusion/FusionToolReference.md). See README.md for what the tests need.
"""

import base64
import importlib.util
import os
import struct
import subprocess
import sys
import time
import zlib

DEFAULT_APP = "/Applications/DaVinci Resolve/DaVinci Resolve.app"
SCRIPT_MODULE = ("/Library/Application Support/Blackmagic Design/"
                 "DaVinci Resolve/Developer/Scripting/Modules/"
                 "DaVinciResolveScript.py")

# Seconds to wait for Resolve to accept a scripting connection, and to quit.
CONNECT_TIMEOUT = 180
QUIT_TIMEOUT = 60


class ResolveUnavailable(Exception):
    """Resolve, its scripting module or its scripting connection is missing."""


class PluginMissing(Exception):
    """Resolve did not load the plug-in."""


class RenderError(Exception):
    """Resolve failed to build or render a case."""


def find_executable(app=None):
    """Returns the Resolve executable.

    Args:
        app: the "DaVinci Resolve.app" bundle, its executable, or None for
            the default install.

    Raises:
        ResolveUnavailable: Resolve is not installed there.
    """
    if sys.platform != "darwin":
        raise ResolveUnavailable("the Resolve tests only run on macOS")
    app = app or DEFAULT_APP
    if app.endswith(".app"):
        app = os.path.join(app, "Contents", "MacOS", "Resolve")
    if not os.access(app, os.X_OK):
        raise ResolveUnavailable("no DaVinci Resolve at %s: use --resolve "
                                 "or $RESOLVE" % app)
    return app


def load_script_module(executable):
    """Imports DaVinciResolveScript for the Resolve of executable.

    This loads Resolve's fusionscript library; it does not start Resolve.

    Raises:
        ResolveUnavailable: the module or its library is missing, or fails
            to load.
    """
    contents = os.path.dirname(os.path.dirname(executable))
    lib = os.path.join(contents, "Libraries", "Fusion", "fusionscript.so")
    for path in (SCRIPT_MODULE, lib):
        if not os.path.isfile(path):
            raise ResolveUnavailable("no Resolve scripting module: %s is "
                                     "missing" % path)
    # DaVinciResolveScript.py reads the library path from this variable.
    os.environ["RESOLVE_SCRIPT_LIB"] = lib
    name = "DaVinciResolveScript"
    if name in sys.modules:
        return sys.modules[name]
    try:
        spec = importlib.util.spec_from_file_location(name, SCRIPT_MODULE)
        if spec is None or spec.loader is None:
            raise ImportError("no import spec for %s" % SCRIPT_MODULE)
        module = importlib.util.module_from_spec(spec)
        sys.modules[name] = module
        spec.loader.exec_module(module)
    except (ImportError, OSError) as ex:
        sys.modules.pop(name, None)
        raise ResolveUnavailable("cannot load the Resolve scripting module: "
                                 "%s" % ex) from ex
    # The module replaces itself with fusionscript in sys.modules.
    return sys.modules[name]


def is_running():
    """Whether a Resolve process is running (for this user or another)."""
    return subprocess.run(["pgrep", "-x", "Resolve"],
                          stdout=subprocess.DEVNULL,
                          check=False).returncode == 0


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


class Session:
    """A Resolve process started for the tests, and its scripting handle.

    Use it as a context manager: on exit, the scratch project is deleted
    and Resolve quits.
    """

    def __init__(self, executable, prefix, work, nogui=True, size=(256, 256)):
        """Prepares a session; nothing runs until start().

        Args:
            executable: the Resolve executable (see find_executable).
            prefix: the openfx-misc install prefix (with OFX/Plugins).
            work: a directory for the log and the rendered images.
            nogui: start Resolve with -nogui.
            size: the (width, height) of the project's timelines.
        """
        self.executable = executable
        self.size = size
        self.prefix = prefix
        self.work = work
        self.nogui = nogui
        self.resolve = None
        self._script = None
        self._proc = None
        self._log = None
        self._project = None
        self._project_name = "openfx-misc-tests-%d" % os.getpid()
        self._cases = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, *exc):
        self.stop()
        return False

    def start(self):
        """Starts Resolve and waits for its scripting connection.

        Raises:
            ResolveUnavailable: Resolve is already running, exits, or does
                not accept a scripting connection in time.
        """
        self._script = load_script_module(self.executable)
        if is_running():
            raise ResolveUnavailable("Resolve is already running: the tests "
                                     "start their own, quit it first")
        env = dict(os.environ)
        env["OFX_PLUGIN_PATH"] = os.path.join(self.prefix, "OFX", "Plugins")
        args = [self.executable] + (["-nogui"] if self.nogui else [])
        self._log = open(os.path.join(self.work, "resolve.log"), "wb")
        self._proc = subprocess.Popen(args, env=env, stdin=subprocess.DEVNULL,
                                      stdout=self._log,
                                      stderr=subprocess.STDOUT,
                                      start_new_session=True)
        deadline = time.monotonic() + CONNECT_TIMEOUT
        while time.monotonic() < deadline:
            status = self._proc.poll()
            if status is not None:
                self._close_log()
                raise ResolveUnavailable("Resolve exited with status %d (see "
                                         "%s/resolve.log)"
                                         % (status, self.work))
            resolve = self._script.scriptapp("Resolve")
            if resolve is not None and resolve.GetProjectManager():
                self.resolve = resolve
                break
            time.sleep(2)
        if self.resolve is None:
            pid = self._proc.pid
            self.stop()
            raise ResolveUnavailable(
                "no scripting connection to Resolve after %d s: allow "
                "external scripting once in Resolve (Preferences > System > "
                "General > External scripting using: Local), if the free "
                "version offers it. Resolve (pid %d) can't be asked to quit "
                "without it: quit it by hand" % (CONNECT_TIMEOUT, pid))
        try:
            self._new_project()
        except Exception:
            self.stop()
            raise

    def stop(self):
        """Deletes the scratch project, quits Resolve and waits for it.

        Resolve is only asked to quit by script. It is never sent a signal:
        Resolve 21.2 crashes at exit on SIGTERM (a shutdown race in its
        Fairlight page, which locks a destroyed mutex). Without a scripting
        connection, or if it does not quit in QUIT_TIMEOUT seconds, it is
        left running and reported.
        """
        if self.resolve is not None:
            manager = self.resolve.GetProjectManager()
            if self._project is not None and manager:
                manager.CloseProject(self._project)
                manager.DeleteProject(self._project_name)
                self._project = None
            quitting = self.resolve.Quit()
            self.resolve = None
            if self._proc is not None and quitting:
                try:
                    self._proc.wait(timeout=QUIT_TIMEOUT)
                except subprocess.TimeoutExpired:
                    pass
        if self._proc is not None:
            if self._proc.poll() is None:
                print("Resolve (pid %d) is still running: quit it by hand"
                      % self._proc.pid, file=sys.stderr)
            self._proc = None
        self._close_log()

    def _close_log(self):
        if self._log is not None:
            self._log.close()
            self._log = None

    def _new_project(self):
        """Creates the scratch project, without colour management."""
        if self.resolve is None:
            raise RenderError("not connected to Resolve")
        manager = self.resolve.GetProjectManager()
        project = manager.CreateProject(self._project_name)
        if project is None:
            raise RenderError("cannot create project %s"
                              % self._project_name)
        self._project = project
        # Values pass through unchanged without colour management.
        settings = (("colorScienceMode", "davinciYRGB"),
                    ("timelineResolutionWidth", str(self.size[0])),
                    ("timelineResolutionHeight", str(self.size[1])))
        for key, value in settings:
            if not project.SetSetting(key, value):
                raise RenderError("cannot set the project's %s to %s"
                                  % (key, value))

    def ofx_effects(self):
        """The ids of the OFX effects Resolve lists for timeline items."""
        if self.resolve is None:
            return []
        return sorted(e.get("id", "") for e in
                      (self.resolve.GetEffectsList("video") or [])
                      if e.get("category") == "ofx")

    def _write_inputs(self, tool):
        """Writes the tool's ID and inputs to caseN-inputs.txt, for --keep."""
        lines = ["RegID %s" % tool.GetAttrs().get("TOOLS_RegID")]
        for inp in (tool.GetInputList() or {}).values():
            attrs = inp.GetAttrs() or {}
            lines.append("%s\t%s\t%s\t%s..%s" % (
                attrs.get("INPS_ID"), attrs.get("INPS_Name"),
                attrs.get("INPS_DataType"), attrs.get("INPN_MinAllowed"),
                attrs.get("INPN_MaxAllowed")))
        path = os.path.join(self.work, "case%d-inputs.txt" % self._cases)
        with open(path, "w") as out:
            out.write("\n".join(lines) + "\n")

    def render_generator(self, plugin, inputs):
        """Renders frame 0 of an OFX generator in a Fusion comp.

        The generator is the only tool of a Fusion composition clip on a new
        timeline, connected to MediaOut1. ("A comp made with fu.NewComp()
        can't be rendered in Resolve", FusionToolReference.md, Saver.)

        Args:
            plugin: the OFX plug-in identifier.
            inputs: a dict of Fusion input IDs and values for the tool.

        Returns:
            An Image of the exported frame.

        Raises:
            PluginMissing: there is no Fusion tool for plugin.
            RenderError: a step failed.
        """
        self._cases += 1
        project = self._project
        if project is None:
            raise RenderError("no scratch project")
        timeline = project.GetMediaPool().CreateEmptyTimeline(
            "case%d" % self._cases)
        if timeline is None or not project.SetCurrentTimeline(timeline):
            raise RenderError("cannot create a timeline")
        item = timeline.InsertFusionCompositionIntoTimeline()
        comp = item.GetFusionCompByIndex(1) if item else None
        if comp is None:
            raise RenderError("cannot insert a Fusion composition")
        comp.SetActiveTool()  # nothing selected: AddTool connects nothing
        tool = comp.AddTool("ofx." + plugin)
        if tool is None:
            raise PluginMissing(
                "no Fusion tool ofx.%s; Resolve's OFX effects: %s"
                % (plugin, ", ".join(self.ofx_effects()) or "none"))
        self._write_inputs(tool)
        for key, value in inputs.items():
            tool.SetInput(key, value)
        comp.MediaOut1.Input = tool
        result = timeline.ExportFrame(0, {"outputType": "encodedString",
                                          "encodedImageFormat": "png"})
        if not result or not result.get("success"):
            raise RenderError("ExportFrame failed: %s"
                              % (result or {}).get("error"))
        png = base64.b64decode(result["data"])
        with open(os.path.join(self.work, "case%d.png" % self._cases),
                  "wb") as out:
            out.write(png)
        return decode_png(png)
