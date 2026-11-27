"""openfx-misc tests, run inside DaVinci Resolve (Workspace > Scripts).

Resolve runs this file with its own Python 3 and the globals `resolve`,
`fusion` and `bmd` already defined (Developer/Scripting/README.md, "Internal
Scripting"). The script:

- creates a scratch project (openfx-misc-tests-<timestamp>);
- renders each case of CASES in a Fusion composition clip on its own
  timeline, and exports frame 0 with Timeline.ExportFrame;
- writes results.json (tool IDs, input IDs, what was set, the exported
  files, Resolve's plug-in cache entries) and the frames to
  <results root>/<timestamp>/;
- deletes the scratch project and reopens the project that was open.

It never quits Resolve. The results root is $OPENFX_MISC_RESOLVE_RESULTS,
or else ~/openfx-misc-resolve-results; a config.json there with
{"output_root": "/some/dir"} overrides it. tests/resolve/test_resolve.py
checks the results (see README.md).

This file is also imported by test_resolve.py, for CASES; nothing runs then.
"""

import datetime
import json
import os
import platform
import struct
import sys
import traceback
import xml.etree.ElementTree as ElementTree

DEFAULT_ROOT = os.path.expanduser("~/openfx-misc-resolve-results")
PLUGIN_CACHE = os.path.expanduser(
    "~/Library/Application Support/Blackmagic Design/DaVinci Resolve/"
    "OFXPluginCacheV2.xml")
SIZE = (256, 256)

CHECKERBOARD = "net.sf.openfx.CheckerBoardPlugin"
CONSTANT = "net.sf.openfx.ConstantPlugin"
INVERT = "net.sf.openfx.Invert"

# Each case: a chain of tools, first to last, the last one connected to
# MediaOut1. "params" are OFX param names and values; the script finds the
# Fusion inputs for them (see _set_param). "checks" are for test_resolve.py:
# (x, y) in exported-frame pixels (y = 0 is the top row) and the expected
# R, G, B values.
#
# CheckerBoard defaults: 64-pixel boxes from the centre (128, 128). In OFX
# coordinates (y up), the box right of and above the centre is color0 (0.1),
# left of it color1 (0.5), below-right color3 (0.5), below-left color2 (0.1).
CASES = [
    {
        "name": "checkerboard_defaults",
        "tools": [{"plugin": CHECKERBOARD, "params": {}}],
        "checks": [((160, 96), (0.1, 0.1, 0.1)),
                   ((96, 96), (0.5, 0.5, 0.5)),
                   ((160, 160), (0.5, 0.5, 0.5)),
                   ((96, 160), (0.1, 0.1, 0.1))],
        "formats": ["png", "tif", "exr"],
    },
    {
        "name": "constant_color",
        "tools": [{"plugin": CONSTANT,
                   "params": {"color": (0.25, 0.5, 0.75, 1.0)}}],
        "checks": [((128, 128), (0.25, 0.5, 0.75)),
                   ((10, 245), (0.25, 0.5, 0.75))],
        "formats": ["png"],
    },
    {
        "name": "invert_checkerboard",
        "tools": [{"plugin": CHECKERBOARD, "params": {}},
                  {"plugin": INVERT, "params": {"processA": False}}],
        "checks": [((160, 96), (0.9, 0.9, 0.9)),
                   ((96, 96), (0.5, 0.5, 0.5))],
        "formats": ["png"],
    },
]


def results_root():
    """The directory that holds one results directory per run."""
    root = os.environ.get("OPENFX_MISC_RESOLVE_RESULTS") or DEFAULT_ROOT
    config = os.path.join(root, "config.json")
    if os.path.isfile(config):
        with open(config) as f:
            root = os.path.expanduser(json.load(f).get("output_root", root))
    return root


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


def png_header(path):
    """Width, height, bit depth and colour type of a PNG file, or None."""
    try:
        with open(path, "rb") as f:
            head = f.read(26)
    except OSError:
        return None
    if head[:8] != b"\x89PNG\r\n\x1a\n" or head[12:16] != b"IHDR":
        return None
    width, height, depth, colour = struct.unpack(">IIBB", head[16:26])
    return {"width": width, "height": height, "depth": depth,
            "colour_type": colour}


def _attrs(inp):
    attrs = inp.GetAttrs() or {}
    return {key: attrs.get(key) for key in
            ("INPS_ID", "INPS_Name", "INPS_DataType", "INPN_MinAllowed",
             "INPN_MaxAllowed", "INPN_MinScale", "INPN_MaxScale")}


def _inputs(tool):
    """The tool's inputs in panel order, as dicts of their attributes."""
    inputs = tool.GetInputList() or {}
    return [_attrs(inputs[key]) for key in sorted(inputs)]


def _set_param(tool, inputs, name, value):
    """Sets an OFX param through the tool's Fusion input(s).

    A scalar goes to the input whose ID is the param name. A tuple goes to
    the inputs whose IDs start with the name (e.g. colorRed, colorGreen...),
    in panel order, if there are as many; else to the input itself.

    Returns:
        A dict: the input IDs used and the values read back.
    """
    ids = [i["INPS_ID"] for i in inputs if i["INPS_ID"]]
    if isinstance(value, bool):
        value = int(value)
    if isinstance(value, (tuple, list)):
        parts = [i for i in ids if i.startswith(name) and i != name]
        if len(parts) == len(value):
            for part, v in zip(parts, value):
                tool.SetInput(part, v)
            return {"inputs": parts,
                    "read": [tool.GetInput(part) for part in parts]}
        value = list(value)
    if name not in ids:
        return {"inputs": [], "error": "no input %s" % name}
    tool.SetInput(name, value)
    return {"inputs": [name], "read": [tool.GetInput(name)]}


def _image_input(inputs):
    """The ID of the main image input: Source if there is one."""
    images = [i["INPS_ID"] for i in inputs if i["INPS_DataType"] == "Image"]
    if "Source" in images:
        return "Source"
    return images[0] if images else None


def _jsonable(value):
    """Fusion values (e.g. Lua tables as dicts with int keys) for json."""
    if isinstance(value, dict):
        return {str(k): _jsonable(v) for k, v in value.items()}
    if isinstance(value, (list, tuple)):
        return [_jsonable(v) for v in value]
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return repr(value)


def run_case(project, case, out_dir, log):
    """Builds and exports one case; returns its result dict."""
    result = {"name": case["name"], "tools": [], "frames": {}}
    timeline = project.GetMediaPool().CreateEmptyTimeline(case["name"])
    if timeline is None or not project.SetCurrentTimeline(timeline):
        result["error"] = "cannot create a timeline"
        return result
    item = timeline.InsertFusionCompositionIntoTimeline()
    comp = item.GetFusionCompByIndex(1) if item else None
    if comp is None:
        result["error"] = "cannot insert a Fusion composition"
        return result
    previous = None
    for spec in case["tools"]:
        plugin = spec["plugin"]
        entry = {"plugin": plugin, "bundles": plugin_bundles(plugin)}
        result["tools"].append(entry)
        comp.SetActiveTool()  # nothing selected: AddTool connects nothing
        tool = comp.AddTool("ofx." + plugin)
        if tool is None:
            result["error"] = "no Fusion tool ofx.%s" % plugin
            return result
        entry["reg_id"] = (tool.GetAttrs() or {}).get("TOOLS_RegID")
        inputs = _inputs(tool)
        entry["inputs"] = inputs
        if previous is not None:
            source = _image_input(inputs)
            entry["source_input"] = source
            if source is not None:
                tool.ConnectInput(source, previous)
            if source is None or tool[source].GetConnectedOutput() is None:
                result["error"] = "cannot connect %s to %s" % (source, plugin)
                return result
        entry["set"] = {name: _set_param(tool, inputs, name, value)
                        for name, value in spec["params"].items()}
        previous = tool
    comp.MediaOut1.Input = previous
    for fmt in case["formats"]:
        path = os.path.join(out_dir, "%s.%s" % (case["name"], fmt))
        frame = timeline.ExportFrame(0, {"outputType": "file",
                                         "filePath": path}) or {}
        frame = {k: frame.get(k) for k in ("success", "error", "width",
                                           "height")}
        frame["file"] = os.path.basename(path) if os.path.isfile(path) else None
        if fmt == "png":
            frame["png"] = png_header(path)
        result["frames"][fmt] = frame
        log("%s: %s %s" % (case["name"], fmt, frame))
    return result


def main(resolve):
    """Runs every case and writes results.json; never quits Resolve."""
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(results_root(), stamp)
    os.makedirs(out_dir)
    lines = []

    def log(message):
        print("openfx-misc tests: " + message)
        lines.append(message)

    manager = resolve.GetProjectManager()
    current = manager.GetCurrentProject()
    previous_name = current.GetName() if current else None
    name = "openfx-misc-tests-" + stamp
    results = {
        "started": stamp,
        "resolve_version": resolve.GetVersionString(),
        "studio": resolve.IsStudio(),
        "python": sys.version,
        "platform": platform.platform(),
        "ofx_plugin_path": os.environ.get("OFX_PLUGIN_PATH"),
        "size": list(SIZE),
        "ofx_effects": sorted(
            e.get("id", "") for e in (resolve.GetEffectsList("video") or [])
            if e.get("category") == "ofx"),
        "cases": [],
    }
    project = manager.CreateProject(name)
    if project is None:
        results["error"] = "cannot create project %s" % name
    else:
        try:
            settings = {"colorScienceMode": "davinciYRGB",
                        "timelineResolutionWidth": str(SIZE[0]),
                        "timelineResolutionHeight": str(SIZE[1])}
            results["settings"] = {k: project.SetSetting(k, v)
                                   for k, v in settings.items()}
            for case in CASES:
                try:
                    results["cases"].append(run_case(project, case, out_dir,
                                                     log))
                except Exception:  # keep going: record it, next case
                    results["cases"].append({"name": case["name"],
                                             "error": traceback.format_exc()})
        finally:
            manager.CloseProject(project)
            results["project_deleted"] = manager.DeleteProject(name)
            if previous_name:
                results["previous_project_reloaded"] = bool(
                    manager.LoadProject(previous_name))
    with open(os.path.join(out_dir, "results.json"), "w") as f:
        json.dump(_jsonable(results), f, indent=1, sort_keys=True)
    with open(os.path.join(out_dir, "log.txt"), "w") as f:
        f.write("\n".join(lines) + "\n")
    log("results in %s" % out_dir)
    return out_dir


# Resolve defines `resolve` for scripts run from its menus; pytest only
# imports this file for CASES.
if "resolve" in globals():
    main(globals()["resolve"])
