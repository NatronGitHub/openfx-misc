"""Renders compatibility test cases with Nuke and hostDemo, and compares.

Each case renders the same small synthetic images through a Nuke node (in
Nuke's terminal mode, `nuke --nc -t`, one process per case: a Nuke
Non-Commercial session allows only 10 node objects) and through the
matching openfx-misc plug-in (with openfx's hostDemo), and compares the
pixel values. The cases and the input images are in cases.py.

hostDemo's project is PAL SD (768x576 canonical, 720x576 pixels, pixel
aspect ratio 768/720), so the Nuke formats get the same pixel aspect ratio:
OFX positions are canonical (x in pixels times the PAR), Nuke's in pixels.

$HOSTDEMO, if set, is used to run hostDemo (e.g. a wrapper), with
$HOSTDEMO_REAL as the real binary.
"""

import glob
import json
import math
import os
import re
import struct
import subprocess

import cases

CHANNEL_INDEX = {"r": 0, "g": 1, "b": 2, "a": 3}
NUKE_TIMEOUT = 300  # seconds, per case
HOSTDEMO_TIMEOUT = 300  # seconds, per case
FIRST_FRAME, LAST_FRAME = 0, 25  # hostDemo's clip frame range
PROJECT_STEP = 8  # compare one pixel in 8x8 over the project area


class CaseError(Exception):
    """A case could not be run (Nuke or hostDemo failed)."""


class NukeUnavailable(CaseError):
    """Nuke cannot run (not installed, or not signed in)."""


class PluginMissing(CaseError):
    """The plug-in is not in the install (e.g. a Debug-only plug-in)."""


def case_slug(case):
    """A file-name-safe version of the case name."""
    return re.sub(r"[^A-Za-z0-9]+", "_", case["name"])


def case_images(case):
    """The images of a case's inputs.

    Returns:
        (nuke_inputs, ofx_clips): the image name (or None) of each Nuke
        input, and a dict from OFX clip name to image name.
    """
    single = case.get("input")
    nuke_inputs = case.get("nuke_inputs", [single] if single else [])
    clips = case.get("clips", {"Source": single} if single else {})
    return nuke_inputs, clips


def case_region(case):
    """The compared pixels: (x1, y1, x2, y2, step)."""
    region = case.get("region")
    if region == "project":
        return (0, 0, cases.PROJECT[0], cases.PROJECT[1], PROJECT_STEP)
    if region:
        return tuple(region) + ((1,) if len(region) == 4 else ())
    return (0, 0, cases.WIDTH, cases.HEIGHT, 1)


def case_points(case):
    """The (x, y) pixel indices that are compared."""
    x1, y1, x2, y2, step = case_region(case)
    return [(x, y) for y in range(y1, y2, step)
            for x in range(x1, x2, step)]


def case_frames(case):
    """The frames to compare (whole frames: Nuke's sample() can't do others).

    Raises:
        CaseError: a frame is not a whole number.
    """
    frames = case.get("frames", [0])
    if any(f != int(f) for f in frames):
        raise CaseError("frames must be whole numbers: %r" % (frames,))
    return [int(f) for f in frames]


def case_views(case):
    """The views to compare: a list of "left" or "right"."""
    return case.get("views", ["left"])


def case_multiview(case):
    """Whether the Nuke script needs left and right views."""
    nuke_inputs, _ = case_images(case)
    return (case_views(case) != ["left"]
            or any(cases.IMAGES[i].get("views") for i in nuke_inputs if i))


def write_pfm(path, pixels, components):
    """Writes rows of (r, g, b, a) tuples as a little-endian PFM file.

    Args:
        path: the file to write.
        pixels: rows of (r, g, b, a), bottom row first.
        components: 3 (the RGB file, "PF") or 1 (the alpha file, "Pf").
    """
    height, width = len(pixels), len(pixels[0])
    with open(path, "wb") as f:
        f.write(("PF\n" if components == 3 else "Pf\n").encode())
        f.write(("%d %d\n-1.0\n" % (width, height)).encode())
        for row in pixels:
            for p in row:
                values = p[:3] if components == 3 else (p[3],)
                f.write(struct.pack("<%df" % len(values), *values))


def read_pfm(path):
    """Reads a PFM file.

    Returns:
        (width, height, components, rows): rows bottom to top, of tuples
        of `components` floats.
    """
    with open(path, "rb") as f:
        kind = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        components = 3 if kind == b"PF" else 1
        endian = "<" if scale < 0 else ">"
        count = width * components
        rows = []
        for _ in range(height):
            values = struct.unpack(endian + "%df" % count,
                                   f.read(4 * count))
            rows.append([values[i * components:(i + 1) * components]
                         for i in range(width)])
    return width, height, components, rows


def nuke_script(case, result_path):
    """A Python script for `nuke -t` that builds a case and samples it.

    The script writes to result_path a JSON dict from "<frame> <view>" to
    the list of [r, g, b, a] at case_points(case). Pixels outside the
    node's bounding box are 0, as Nuke's Viewer shows them, unless the case
    has "nuke_edges" (then they are what sample() gives: the edge pixels,
    as the nodes downstream see them).
    """
    nuke_inputs, _ = case_images(case)
    node_class, knobs = case["nuke"]
    # Generators and project-sized cases render the PAL project format.
    project = (case.get("region") == "project" or not any(nuke_inputs)
               or case.get("project_format"))
    multiview = case_multiview(case)
    lines = [
        "import json",
        "import nuke",
        "nuke.addFormat('%d %d %r nukecompare')"
        % (cases.WIDTH, cases.HEIGHT, cases.PAR),
        "nuke.addFormat('%d %d %r nukecompare_project')"
        % (cases.PROJECT[0], cases.PROJECT[1], cases.PAR),
        "fmt = %r" % ("nukecompare_project" if project else "nukecompare"),
        "root = nuke.root()",
        "root['format'].setValue(fmt)",
        "root['first_frame'].setValue(%d)" % FIRST_FRAME,
        "root['last_frame'].setValue(%d)" % LAST_FRAME,
    ]
    if multiview:
        lines.append("root['views'].fromScript("
                     "'left #ff0000\\nright #00ff00')")
    lines += [
        "c = nuke.nodes.Constant(format=fmt, color=0, first=%d, last=%d)"
        % (FIRST_FRAME, LAST_FRAME),
        "images = {}",
    ]
    constant = ("nuke.nodes.Constant(format=fmt, first=%d, last=%d)"
                % (FIRST_FRAME, LAST_FRAME))
    for name in sorted(set(i for i in nuke_inputs if i)):
        image = cases.IMAGES[name]
        if image.get("views"):
            left, right = image["views"]
            lines += [
                "l = " + constant,
                "l['color'].fromScript(%r)" % " ".join(
                    "{%s}" % e for e in left),
                "r = " + constant,
                "r['color'].fromScript(%r)" % " ".join(
                    "{%s}" % e for e in right),
                "images[%r] = nuke.nodes.JoinViews(inputs=[l, r])" % name,
            ]
        elif image.get("color"):
            lines += [
                "images[%r] = %s" % (name, constant),
                "images[%r]['color'].fromScript(%r)" % (name, " ".join(
                    "{%s}" % e for e in image["color"])),
            ]
        else:
            lines.append("images[%r] = nuke.nodes.Expression(inputs=[c], "
                         "expr0=%r, expr1=%r, expr2=%r, expr3=%r)"
                         % ((name,) + image["nuke"]))
            if image.get("box"):
                lines += [
                    "images[%r] = nuke.nodes.Crop(inputs=[images[%r]],"
                    " reformat=False, crop=False)" % (name, name),
                    "images[%r]['box'].setValue(%r)"
                    % (name, list(image["box"]))]
    lines.append("n = nuke.nodes.%s()" % node_class)
    for i, name in enumerate(nuke_inputs):
        if name:
            lines.append("n.setInput(%d, images[%r])" % (i, name))
    for knob, value in knobs.items():
        if isinstance(value, cases.Script):
            lines.append("n[%r].fromScript(%r)" % (knob, str(value)))
        else:
            lines.append("n[%r].setValue(%r)" % (knob, value))
    if case.get("nuke_post"):
        lines.append(case["nuke_post"])
    lines += [
        "points = %r" % (case_points(case),),
        "results = {}",
        "for view in %r:" % (case_views(case),),
        "    out = n",
        "    if %r:" % multiview,
        "        out = nuke.nodes.OneView(inputs=[n], view=view)",
        "    for frame in %r:" % (case_frames(case),),
        "        nuke.frame(frame)",  # sample()'s frame argument is ignored
        "        b = out.bbox()",
        "        bx, by, bw, bh = b.x(), b.y(), b.w(), b.h()",
        "        values = []",
        "        for x, y in points:",
        "            inside = %s or (bx <= x < bx + bw and by <= y < by + bh)"
        % bool(case.get("nuke_edges")),
        "            values.append([out.sample(ch, x + 0.5, y + 0.5)"
        " if inside else 0. for ch in",
        "                           ('rgba.red', 'rgba.green', 'rgba.blue',"
        " 'rgba.alpha')])",
        "        results['%s %s' % (frame, view)] = values",
        "with open(%r, 'w') as f:" % result_path,
        "    json.dump(results, f)",
    ]
    return "\n".join(lines) + "\n"


def find_nuke():
    """The Nuke executable: $NUKE, or the newest one in /Applications."""
    if os.environ.get("NUKE"):
        return os.environ["NUKE"]
    versions = []
    for path in glob.glob("/Applications/Nuke*/Nuke*.app/Contents/MacOS/*"):
        match = re.match(r"Nuke(\d+)\.(\d+)(?:v(\d+))?$",
                         os.path.basename(path))
        if match and os.access(path, os.X_OK):
            versions.append((tuple(int(v or 0) for v in match.groups()),
                             path))
    return max(versions)[1] if versions else None


def run_command(command, log, timeout, **kwargs):
    """Runs a command with its output in the file log.

    Returns:
        The exit status.

    Raises:
        CaseError: the command could not be run, or timed out.
    """
    try:
        with open(log, "w") as out:
            return subprocess.run(command, stdin=subprocess.DEVNULL,
                                  stdout=out, stderr=subprocess.STDOUT,
                                  timeout=timeout, check=False,
                                  **kwargs).returncode
    except subprocess.TimeoutExpired as ex:
        raise CaseError("%s timed out, see %s" % (command[0], log)) from ex
    except OSError as ex:
        raise CaseError("cannot run %s: %s" % (command[0], ex)) from ex


def run_nuke(nuke, case, work):
    """Renders a case with Nuke, in its own process.

    Nuke Non-Commercial allows only 10 node objects per Python session.

    Returns:
        A dict from (frame, view) to the list of [r, g, b, a] at
        case_points(case).
    """
    name = case_slug(case)
    script = os.path.join(work, name + ".nuke.py")
    result_path = os.path.join(work, name + ".nuke.json")
    if os.path.exists(result_path):
        os.remove(result_path)
    with open(script, "w") as f:
        f.write(nuke_script(case, result_path))
    log = os.path.join(work, name + ".nuke.log")
    status = run_command([nuke, "--nc", "-t", script], log, NUKE_TIMEOUT,
                         cwd=work)
    if status != 0 or not os.path.exists(result_path):
        raise CaseError("Nuke failed (exit status %d), see %s: are you "
                        "signed in to Nuke Non-Commercial (or licensed)?"
                        % (status, log))
    with open(result_path) as f:
        results = json.load(f)
    return {(float(k.split()[0]), k.split()[1]): v
            for k, v in results.items()}


def hostdemo_command(prefix):
    """The command that runs hostDemo, and its environment."""
    hostdemo = (os.environ.get("HOSTDEMO")
                or os.path.join(prefix, "bin", "hostDemo"))
    env = dict(os.environ,
               OFX_PLUGIN_PATH=os.path.join(prefix, "OFX", "Plugins"))
    if os.environ.get("HOSTDEMO") and not env.get("HOSTDEMO_REAL"):
        env["HOSTDEMO_REAL"] = os.path.join(prefix, "bin", "hostDemo")
    return [hostdemo, "--no-std-paths", "--no-cache", "--quiet"], env


# hostDemo --describe results by plug-in identifier, for the process (one
# pytest session or xdist worker): the contexts that it describes, or the
# PluginMissing error of a plug-in that is not installed.
_DESCRIPTIONS = {}


def _describe_contexts(prefix, plugin, work):
    """The contexts of a plug-in that hostDemo --describe can describe.

    Returns:
        A dict from context ("filter", "general", ...) to its --describe
        JSON object (with "clips" and "params").
    """
    if plugin in _DESCRIPTIONS:
        if isinstance(_DESCRIPTIONS[plugin], CaseError):
            raise _DESCRIPTIONS[plugin]
        return _DESCRIPTIONS[plugin]
    command, env = hostdemo_command(prefix)
    describe_dir = os.path.join(work, "describe")
    os.makedirs(describe_dir, exist_ok=True)
    path = os.path.join(describe_dir, "%s.%d.json" % (plugin, os.getpid()))
    status = run_command(command + ["-p", plugin, "--describe", path],
                         path + ".log", HOSTDEMO_TIMEOUT, cwd=work, env=env)
    if status != 0 or not os.path.exists(path):
        with open(path + ".log") as f:
            missing = 'plug-in "%s" not found' % plugin in f.read()
        if missing:
            _DESCRIPTIONS[plugin] = PluginMissing(
                "%s is not in %s" % (plugin, prefix))
            raise _DESCRIPTIONS[plugin]
        raise CaseError("hostDemo --describe %s failed, see %s.log"
                        % (plugin, path))
    with open(path) as f:
        contexts = json.load(f)["contexts"]
    result = {name.replace("OfxImageEffectContext", "").lower(): context
              for name, context in contexts.items() if context.get("ok")}
    if not result:
        raise CaseError("hostDemo cannot describe %s in any context"
                        % plugin)
    _DESCRIPTIONS[plugin] = result
    return result


def describe(prefix, plugin, work):
    """The clips of a plug-in's contexts, from hostDemo --describe.

    Returns:
        A dict from context ("filter", "general", ...) to a dict from clip
        name to its "optional" flag (Output excluded).
    """
    return {short: {c["name"]: c["optional"] for c in context["clips"]
                    if c["name"] != "Output"}
            for short, context in _describe_contexts(
                prefix, plugin, work).items()}


def describe_params(prefix, plugin, work):
    """The visible params of a plug-in, in definition order.

    Pages, groups and secret params are left out. The params are those of
    the context that the cases use by default (filter, else general, else
    generator, else the first one).

    Returns:
        A list of param names.
    """
    contexts = _describe_contexts(prefix, plugin, work)
    context = next((c for c in ("filter", "general", "generator")
                    if c in contexts), next(iter(contexts)))
    return [p["name"] for p in contexts[context]["params"]
            if p["type"] not in ("OfxParamTypePage", "OfxParamTypeGroup")
            and not p["secret"]]


def ofx_input_spec(name, case_dir):
    """The hostDemo -i spec of an image, writing its PFM files."""
    image = cases.IMAGES[name]
    if "ofx" in image:
        return image["ofx"]
    function = image["py"]
    x1, y1, x2, y2 = image.get("box", (0, 0, cases.WIDTH, cases.HEIGHT))
    pixels = [[function(x, y) for x in range(x1, x2)] for y in range(y1, y2)]
    rgb = name + ".pfm"
    write_pfm(os.path.join(case_dir, rgb), pixels, 3)
    write_pfm(os.path.join(case_dir, name + ".alpha.pfm"), pixels, 1)
    spec = "file:%s+%s.alpha.pfm" % (rgb, name)
    if (x1, y1) != (0, 0):
        spec += "@%d,%d" % (x1, y1)
    if image.get("premult"):
        spec += "#premult"
    return spec


def ofx_command(prefix, case, work, case_dir):
    """The hostDemo command line of a case."""
    _, clips = case_images(case)
    plugin, params = case["ofx"]
    contexts = describe(prefix, plugin, work)
    context = case.get("context") or next(
        c for c in ("filter", "general", "generator") if c in contexts)
    if context not in contexts:
        raise CaseError("%s has no %s context" % (plugin, context))
    for clip in clips:
        if clip not in contexts[context]:
            raise CaseError("%s has no clip %s in the %s context"
                            % (plugin, clip, context))
    command, _ = hostdemo_command(prefix)
    x1, y1, x2, y2, _ = case_region(case)
    command += ["-p", plugin, "-c", context, "-d", "float",
                "--format", "pfm", "--frames",
                ",".join("%d" % f for f in case_frames(case)),
                "--window", "%d,%d,%d,%d" % (x1, y1, x2, y2),
                "--report", "report.json", "-o", "out"]
    if "major" in case:
        command += ["-m", str(case["major"])]
    for clip, optional in contexts[context].items():
        image = clips.get(clip)
        if image:
            command += ["-i", "%s=%s" % (clip,
                                         ofx_input_spec(image, case_dir))]
        elif optional:
            command += ["-i", "%s=none" % clip]
        else:
            raise CaseError("no input for the mandatory clip %s" % clip)
    for name, value in params.items():
        if value is None:  # a push button
            command += ["-s", name]
            continue
        values = value if isinstance(value, list) else [value]
        command += ["-s", "%s=%s" % (name, ",".join(
            v if isinstance(v, str) else repr(v) for v in values))]
    return command


def read_render(case, case_dir, output):
    """The [r, g, b, a] at case_points(case) of one hostDemo output.

    Pixels outside the output bounds are 0, and so is the alpha of an RGB
    output.
    """
    x1, y1, x2, y2, _ = case_region(case)
    files = output["files"]
    bx1, by1, bx2, by2 = output["bounds"]
    width, height, _, color = read_pfm(os.path.join(case_dir, files[0]))
    if (width, height) != (bx2 - bx1, by2 - by1):
        raise CaseError("%s is %dx%d, but its bounds are %r"
                        % (files[0], width, height, output["bounds"]))
    if bx1 < x1 or by1 < y1 or bx2 > x2 or by2 > y2:
        raise CaseError("the output bounds %r exceed the window"
                        % (output["bounds"],))
    alpha = None
    if len(files) > 1:
        alpha = read_pfm(os.path.join(case_dir, files[1]))[3]
    values = []
    for x, y in case_points(case):
        if not (bx1 <= x < bx2 and by1 <= y < by2):
            values.append([0.] * 4)
            continue
        p = color[y - by1][x - bx1]
        if len(p) == 1:  # an Alpha output
            values.append([0., 0., 0., p[0]])
        else:
            values.append(list(p) + [alpha[y - by1][x - bx1][0]
                                     if alpha else 0.])
    return values


def run_ofx(prefix, case, work):
    """Renders a case with hostDemo.

    Returns:
        A dict from (frame, view) to the list of [r, g, b, a] at
        case_points(case).
    """
    case_dir = os.path.join(work, case_slug(case))
    os.makedirs(case_dir, exist_ok=True)
    for old in glob.glob(os.path.join(case_dir, "out.*")):
        os.remove(old)
    command = ofx_command(prefix, case, work, case_dir)
    _, env = hostdemo_command(prefix)
    status = run_command(command, os.path.join(case_dir, "hostdemo.log"),
                         HOSTDEMO_TIMEOUT, cwd=case_dir, env=env)
    report_path = os.path.join(case_dir, "report.json")
    if status != 0 or not os.path.exists(report_path):
        raise CaseError("hostDemo failed (exit status %d), see %s"
                        % (status, case_dir))
    with open(report_path) as f:
        report = json.load(f)
    results = {}
    for render in report.get("renders", []):
        view = "right" if render.get("view") == 1 else "left"
        output = render.get("output") or {}
        key = (float(render["time"]), view)
        if output.get("files"):
            results[key] = read_render(case, case_dir, output)
        else:  # an empty render window: all black
            results[key] = [[0.] * 4 for _ in case_points(case)]
    return results


def compare(case, nuke_results, ofx_results):
    """Compares the Nuke and openfx-misc renders of a case.

    NaN on both sides counts as equal; NaN on one side only is an infinite
    difference.

    Returns:
        (max abs difference, where): `where` describes the worst pixel.

    Raises:
        CaseError: a render is missing, or nothing was compared.
    """
    worst, where = 0., None
    compared = 0
    points = case_points(case)
    several = len(nuke_results) > 1
    for key in sorted(nuke_results):
        frame, view = key
        if key not in ofx_results:
            raise CaseError("hostDemo rendered no %s view at frame %g"
                            % (view, frame))
        nuke_values, ofx_values = nuke_results[key], ofx_results[key]
        for i, (x, y) in enumerate(points):
            for c in case["channels"]:
                compared += 1
                n = nuke_values[i][CHANNEL_INDEX[c]]
                o = ofx_values[i][CHANNEL_INDEX[c]]
                if math.isnan(n) and math.isnan(o):
                    continue
                d = (math.inf if math.isnan(n) or math.isnan(o)
                     else abs(n - o))
                if d > worst or where is None:
                    worst = max(worst, d)
                    where = "%sx=%d y=%d %s: Nuke %.6g, openfx-misc %.6g" % (
                        ("t=%g %s " % (frame, view)) if several else "",
                        x, y, c, n, o)
    if not compared:
        raise CaseError("nothing was compared (no frame, view, pixel or "
                        "channel)")
    return worst, where


def plugin_name(identifier):
    """The short name of a plug-in, for test ids.

    It is the last part of the OFX identifier, without "Plugin" and with
    "CImg" at the end (e.g. Merge, BlurCImg).
    """
    plugin = identifier.split(".")[-1]
    plugin = re.sub(r"Plugin$", "", plugin)
    if plugin.startswith("CImg"):
        plugin = plugin[len("CImg"):] + "CImg"
    return plugin[0].upper() + plugin[1:]


def case_id(case):
    """The test id of a case: "<plug-in>/<case name>" (see plugin_name)."""
    plugin = plugin_name(case["ofx"][0])
    name = case["name"].replace("[0, 1]", "0-1")
    first, _, rest = name.partition(" ")
    nuke_class = re.sub(r"\d+$", "", case["nuke"][0]).lower()
    if rest and first.lower() in (nuke_class, plugin.lower(),
                                  plugin.lower().replace("cimg", "")):
        name = rest
    name = re.sub(r"[\[\]]", "", name)
    name = re.sub(r" +", " ", name.replace(",", " "))
    return "%s/%s" % (plugin, name)


# The Nuke knobs that are not parameters of the node, by knob class: tabs,
# group and line separators, labels, obsolete and Python callback knobs.
NOT_PARAM_KNOBS = ("Tab_Knob", "Text_Knob", "Obsolete_Knob", "PythonKnob")
# Nuke's tab of the knobs that every node has (name, label, lifetime...).
COMMON_TAB = "Node"


def knob_order_script(node_classes, result_path):
    """A Python script for `nuke -t` that lists the knobs of nodes.

    The script creates one node of each class and writes to result_path a
    JSON dict from node class to the names of its knobs in panel order,
    without the knobs of the Node tab, the hidden ones, and those that are
    not parameters (NOT_PARAM_KNOBS).
    """
    return "\n".join([
        "import json",
        "import nuke",
        "results = {}",
        "for node_class in %r:" % (list(node_classes),),
        "    node = getattr(nuke.nodes, node_class)()",
        "    tab = ''",
        "    names = []",
        "    for knob in node.allKnobs():",
        "        if knob.Class() == 'Tab_Knob' and knob.label():",
        "            tab = knob.label()",
        "        if (tab == %r or knob.Class() in %r or not knob.name()"
        % (COMMON_TAB, NOT_PARAM_KNOBS),
        "                or knob.getFlag(nuke.INVISIBLE)"
        " or not knob.visible()):",
        "            continue",
        "        names.append(knob.name())",
        "    results[node_class] = names",
        "with open(%r, 'w') as f:" % result_path,
        "    json.dump(results, f)",
    ]) + "\n"


def nuke_knob_orders(nuke, node_classes, work):
    """The knobs of Nuke nodes in panel order, from one Nuke process.

    A Nuke Non-Commercial session allows only 10 node objects: pass at
    most about 8 classes.

    Returns:
        A dict from node class to a list of knob names (see
        knob_order_script).
    """
    name = "knobs_" + "_".join(node_classes)
    script = os.path.join(work, name + ".nuke.py")
    result_path = os.path.join(work, name + ".nuke.json")
    if os.path.exists(result_path):
        os.remove(result_path)
    with open(script, "w") as f:
        f.write(knob_order_script(node_classes, result_path))
    log = os.path.join(work, name + ".nuke.log")
    status = run_command([nuke, "--nc", "-t", script], log, NUKE_TIMEOUT,
                         cwd=work)
    if status != 0 or not os.path.exists(result_path):
        raise CaseError("Nuke failed (exit status %d), see %s"
                        % (status, log))
    with open(result_path) as f:
        return json.load(f)


def check_nuke(nuke, work):
    """Raises NukeUnavailable unless Nuke renders a one-node script."""
    if not nuke or not os.access(nuke, os.X_OK):
        raise NukeUnavailable("no Nuke executable: use --nuke or $NUKE")
    script = os.path.join(work, "check_nuke.py")
    result = os.path.join(work, "check_nuke.json")
    with open(script, "w") as f:
        f.write("import json\nimport nuke\n"
                "c = nuke.nodes.Constant(color=0.5)\n"
                "with open(%r, 'w') as f:\n"
                "    json.dump(c.sample('rgba.red', 0.5, 0.5), f)\n" % result)
    if os.path.exists(result):
        os.remove(result)
    status = run_command([nuke, "--nc", "-t", script],
                         os.path.join(work, "check_nuke.log"), 120,
                         cwd=work)
    if status != 0 or not os.path.exists(result):
        raise NukeUnavailable(
            "%s --nc -t failed (exit status %d, see %s): sign in to Nuke "
            "Non-Commercial (or use a licensed Nuke)"
            % (nuke, status, os.path.join(work, "check_nuke.log")))


def run_case(nuke, prefix, case, work):
    """Renders a case on both sides and compares them.

    hostDemo runs first, so that a case whose plug-in is not installed
    raises PluginMissing without starting Nuke.

    Returns:
        (max abs difference, where), as compare().
    """
    ofx_results = run_ofx(prefix, case, work)
    nuke_results = run_nuke(nuke, case, work)
    return compare(case, nuke_results, ofx_results)
