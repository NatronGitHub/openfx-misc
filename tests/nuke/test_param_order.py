"""Parameter order: each plug-in against the Nuke node of the same name.

Maintainer rule (2026-11-24): the order of a plug-in's parameters stays as
close as possible to the knob order of the node with the same name in
Nuke. (No host should address parameters by index: the OFX API has none.)

For each plug-in that has compatibility cases (cases.py), the test lists
the Nuke node's knobs in panel order (without the Node tab, the hidden
knobs and the labels and separators) and the plug-in's params in
definition order (hostDemo --describe lists them in that order; pages,
groups and secret params are left out). Names are compared after the
mapping below (NAMES, then COMMON_NAMES), ignoring case, "_" and "."
(two knobs whose names differ only so are an error); params that map to the same
knob (e.g. the four NatronOfxParamProcess* and "channels") count once, at
the first one. The params common to both sides must be in the same
relative order.

A known difference is a strict expected failure whose reason is the
GitHub issue that records it (see KNOWN and ISSUES), as in
test_compatibility.py; the first out-of-order pair must be the one in
KNOWN. Any other failure (a different pair, a stale mapping, a name
collision, fewer than two params in common) is a plain failure.
"""

import pytest

import cases
import harness

# The plug-ins that are not compared, with the reason.
EXCLUDED = {
    "net.sf.openfx.NoOpPlugin": "Nuke's NoOp has no knobs",
    "net.sf.openfx.joinViewsPlugin": "no params on either side",
    "net.sf.openfx.ShufflePlugin":
        "Nuke's Shuffle has one channel-routing knob, not one param per "
        "output channel",
    "net.sf.openfx.RGBToHSV": "Nuke's Colorspace is a different node",
    "net.sf.openfx.RGBToHSL": "Nuke's Colorspace is a different node",
    "net.sf.openfx.RGBToYCbCr709": "Nuke's Colorspace is a different node",
    "net.sf.openfx.RGB709ToXYZ": "Nuke's Colorspace is a different node",
    "net.sf.openfx.RGB709ToLab": "Nuke's Colorspace is a different node",
    "net.sf.openfx.switchPlugin": "one param in common (which)",
    "net.sf.openfx.AdjustRoDPlugin": "one param in common (numpixels)",
    "net.sf.openfx.Position": "one param in common (translate)",
    "net.sf.openfx.mixViewsPlugin": "one param in common (mix)",
}

# Our param name -> Nuke knob name, for all plug-ins, where they differ by
# more than case, "_" and ".".
COMMON_NAMES = {
    # The four "process this channel" checkboxes are Nuke's channels.
    "NatronOfxParamProcessR": "channels",
    "NatronOfxParamProcessG": "channels",
    "NatronOfxParamProcessB": "channels",
    "NatronOfxParamProcessA": "channels",
    # The output components of a generator.
    "outputComponents": "channels",
    "maskInvert": "invert_mask",
    # The (Un)premult checkbox is Nuke's "unpremult by" channel.
    "premult": "unpremult",
    "NatronParamFormatChoice": "format",
}

# A rectangle given by bottomLeft and size is Nuke's area (or box).
_AREA = {"bottomLeft": "area", "size": "area"}
# The output channels of a shape generator are Nuke's "output".
_OUTPUT = {"NatronOfxParamProcess" + _c: "output" for _c in "RGBA"}

# Our param name -> Nuke knob name, by plug-in (before COMMON_NAMES).
NAMES = {
    "net.sf.openfx.GradePlugin": {
        "offset": "add", "clampBlack": "black_clamp",
        "clampWhite": "white_clamp"},
    "net.sf.openfx.ColorCorrectPlugin": {
        # The Master group's params; the other groups' are named
        # e.g. ShadowsGain, Nuke's shadows.gain.
        "MasterSaturation": "saturation", "MasterContrast": "contrast",
        "MasterGamma": "gamma", "MasterGain": "gain",
        "MasterOffset": "offset",
        "range": "lookup"},  # the tonal ranges (Nuke: a curve)
    "net.sf.openfx.ColorMatrixPlugin": {
        "output" + _c: "matrix" for _c in ("Red", "Green", "Blue", "Alpha")},
    "net.sf.openfx.SaturationPlugin": {"luminanceMath": "mode"},
    "net.sf.openfx.HSVToolPlugin": {
        "hueRange": "huesrcs", "hueRangeRolloff": "huerolloffrange",
        "saturationRange": "satsrcs", "saturationAdjustment": "saturation",
        "saturationRangeRolloff": "satrolloffrange",
        "brightnessRange": "brtsrcs", "brightnessAdjustment": "brightness",
        "brightnessRangeRolloff": "brtrolloffrange",
        "outputAlpha": "output"},
    "net.sf.openfx.Premult": {"premultChannel": "alpha"},
    "net.sf.openfx.Unpremult": {"premultChannel": "alpha"},
    "net.sf.openfx.MergePlugin": {
        _side + "Channels" + _c: _knob
        for _side, _knob in (("A", "Achannels"), ("B", "Bchannels"),
                             ("Output", "output"))
        for _c in ("", "R", "G", "B", "A")},
    "net.sf.openfx.KeyMix": {"maskInvert": "invertMask"},
    "net.sf.openfx.CopyRectanglePlugin": _AREA,
    "net.sf.openfx.KeyerPlugin": dict(
        {"mode": "operation"},
        # The four points of the key are Nuke's range.
        **{_p: "range" for _p in ("softnessLower", "toleranceLower",
                                  "center", "toleranceUpper",
                                  "softnessUpper")}),
    "net.sf.openfx.PIK": {
        "screenType": "st", "color": "colour", "ubl": "ublu",
        "ubc": "ubcr"},
    "net.sf.openfx.ConstantPlugin": {"frameRange": "first"},
    "net.sf.openfx.ColorWheel": _AREA,
    "net.sf.openfx.Ramp": dict(_OUTPUT, point0="p0", point1="p1",
                               color1="color"),
    "net.sf.openfx.Radial": dict(_OUTPUT, color1="color", **_AREA),
    "net.sf.openfx.Rectangle": dict(_OUTPUT, color1="color", **_AREA),
    "net.sf.openfx.TransformPlugin": {"invert": "invert_matrix"},
    "net.sf.openfx.TransformMaskedPlugin": {"invert": "invert_matrix"},
    "net.sf.openfx.CornerPinPlugin": dict(
        {"setToInputRod": "set_to_input"},
        **{"transform_row%d" % _i: "transform_matrix" for _i in (1, 2, 3)}),
    "net.sf.openfx.CropPlugin": {
        "bottomLeft": "box", "size": "box", "blackOutside": "crop"},
    "net.sf.openfx.Reformat": {
        "reformatType": "type", "boxSize": "box_width",
        "boxPar": "box_pixel_aspect", "reformatScale": "scale",
        "reformatCentered": "center", "preserveBB": "pbb"},
    "net.sf.openfx.Card3D": dict(
        {"axisXformOrder": "xform_order", "axisRotOrder": "rot_order",
         "axisTranslate": "translate", "axisRotate": "rotate",
         "axisScaling": "scaling", "axisUniformScale": "uniform_scale",
         "axisSkew": "skew", "axisPivot": "pivot_translate",
         "axisUseMatrix": "useMatrix"},
        **{"axisMatrix%d%d" % (_i, _j): "matrix"
           for _i in range(1, 5) for _j in range(1, 5)}),
    "net.sf.openfx.IDistort": {"channelU": "uv", "channelV": "uv"},
    "net.sf.openfx.STMap": {"channelU": "uv", "channelV": "uv"},
    "net.sf.openfx.GodRays": {"skewX": "skew", "skewY": "skew"},
    "eu.cimg.EdgeDetect": {"filter": "edgedetector"},
    "eu.cimg.CImgMatrix3x3": {
        "matrix%d%d" % (_i, _j): "matrix"
        for _i in range(1, 4) for _j in range(1, 4)},
    "net.sf.cimg.CImgZDefocus": {
        "depthChannel": "z_channel", "depthType": "math",
        "focusPoint": "focal_point", "focus": "center",
        "depthOfFocus": "dof", "slices": "layers", "aperture": "shape",
        "blurInside": "blur_dof"},
    "net.sf.openfx.FrameRange": {"frameRange": "first_frame"},
    "net.sf.openfx.Retime": {"reverseInput": "reverse"},
    "net.sf.openfx.FrameBlend": {
        "frameRange": "startframe", "inputRange": "getinputrange"},
    "net.sf.openfx.TimeBlur": {"division": "divisions"},
    "net.sf.openfx.TimeDissolvePlugin": {
        "dissolveIn": "in", "dissolveOut": "out"},
    "net.sf.openfx.AppendClip": {"crossDissolve": "dissolve"},
    "net.sf.openfx.anaglyphPlugin": {"amtcolor": "amtcolour"},
    "net.sf.openfx.ContactSheetOFX": {
        "resolution": "width", "rowsColumns": "rows",
        "frameRange": "startframe"},
}

# The known differences: plug-in -> (the GitHub issue that records it, the
# first out-of-order pair of Nuke knob names: ours has the first before the
# second).
_PREMULT_MASK = (377, ("unpremult", "invert_mask"))
KNOWN = {
    "net.sf.openfx.AddPlugin": _PREMULT_MASK,
    "net.sf.openfx.MultiplyPlugin": _PREMULT_MASK,
    "net.sf.openfx.GammaPlugin": _PREMULT_MASK,
    "net.sf.openfx.Clamp": _PREMULT_MASK,
    "net.sf.openfx.GradePlugin": _PREMULT_MASK,
    "net.sf.openfx.ColorCorrectPlugin": _PREMULT_MASK,
    "net.sf.openfx.ColorMatrixPlugin": _PREMULT_MASK,
    "net.sf.openfx.SaturationPlugin": _PREMULT_MASK,
    "net.sf.openfx.Invert": _PREMULT_MASK,
    "net.sf.openfx.Log2Lin": _PREMULT_MASK,
    "net.sf.openfx.PLogLin": _PREMULT_MASK,
    "net.sf.openfx.Quantize": _PREMULT_MASK,
    "net.sf.cimg.CImgExpression": _PREMULT_MASK,
    "net.sf.openfx.MergePlugin": (378, ("bbox", "screen_alpha")),
    "net.sf.openfx.CopyRectanglePlugin": (379, ("area", "channels")),
    "net.sf.openfx.KeyerPlugin": (380, ("operation", "invert")),
    "net.sf.openfx.ConstantPlugin": (381, ("format", "channels")),
    "net.sf.openfx.ColorWheel": (382, ("area", "channels")),
    "net.sf.openfx.Ramp": (383, ("type", "p0")),
    "net.sf.openfx.Radial": (384, ("area", "invert_mask")),
    "net.sf.openfx.Rectangle": (384, ("area", "invert_mask")),
    "net.sf.openfx.CornerPinPlugin": (385, ("from1", "transform_matrix")),
    "net.sf.openfx.IDistort": (386, ("filter", "invert_mask")),
    "net.sf.openfx.STMap": (386, ("filter", "invert_mask")),
    "net.sf.openfx.LensDistortion": (387, ("format", "anamorphicSqueeze")),
    "eu.cimg.EdgeDetect": (388, ("channels", "edgedetector")),
    "net.sf.cimg.CImgZDefocus": (389, ("focal_point", "center")),
    "net.sf.openfx.Retime": (390, ("filter", "before")),
}

# The short titles of the issues (xfail reasons).
ISSUES = {
    377: "Colour filters: (Un)premult comes before the mask controls",
    378: "Merge: Bounding Box comes before Screen Alpha",
    379: "CopyRectangle: the rectangle comes before the channels",
    380: "Keyer: Invert comes last",
    381: "Constant: format and frame range come before the colour",
    382: "ColorWheel: the area comes before the wheel controls",
    383: "Ramp: Type comes before the points, mask controls last",
    384: "Radial, Rectangle: the mask controls come last",
    385: "CornerPin: the From points come before the filter controls",
    386: "IDistort, STMap: Filter comes before the mask controls",
    387: "LensDistortion: the output format comes first",
    388: "EdgeDetect: the channels come before the edge detector",
    389: "ZDefocusCImg: focus controls order",
    390: "Retime: Filter comes before Before and After",
}


def _plugins():
    """The plug-ins with cases: plug-in -> (Nuke node class, OFX context).

    Both come from the plug-in's first case.
    """
    plugins = {}
    for case in cases.CASES:
        plugins.setdefault(case["ofx"][0],
                           (case["nuke"][0], case.get("context")))
    return plugins


PLUGINS = _plugins()
COMPARED = [p for p in PLUGINS if p not in EXCLUDED]

# The Nuke classes are listed in batches of one Nuke process each (a Nuke
# Non-Commercial session allows only 10 node objects).
NUKE_BATCH_SIZE = 8


def _nuke_batches():
    """The Nuke node classes of COMPARED, in batches of NUKE_BATCH_SIZE."""
    classes = sorted(set(PLUGINS[p][0] for p in COMPARED))
    return [classes[i:i + NUKE_BATCH_SIZE]
            for i in range(0, len(classes), NUKE_BATCH_SIZE)]


NUKE_BATCHES = _nuke_batches()


def _key(name):
    """The name used to compare a param and a knob."""
    return name.lower().replace("_", "").replace(".", "")


def knobs_by_key(knobs):
    """The knobs by their _key.

    Raises:
        ValueError: two knobs have the same key.
    """
    by_key = {}
    for knob in knobs:
        other = by_key.setdefault(_key(knob), knob)
        if other != knob:
            raise ValueError("the knobs %s and %s have the same name key %s"
                             % (other, knob, _key(knob)))
    return by_key


def _mapped(plugin, param):
    """The Nuke knob name that a param maps to (NAMES, then COMMON_NAMES)."""
    return NAMES.get(plugin, {}).get(param, COMMON_NAMES.get(param, param))


def common_orders(plugin, params, knobs):
    """The params and knobs that both sides have, each in its own order.

    Args:
        plugin: the OFX plug-in identifier.
        params: the plug-in's param names, in definition order.
        knobs: the Nuke node's knob names, in panel order.

    Returns:
        (ours, nuke): two lists of the same Nuke knob names, in our order
        and in Nuke's.

    Raises:
        ValueError: two knobs have the same key.
    """
    by_key = knobs_by_key(knobs)
    ours = []
    for param in params:
        knob = by_key.get(_key(_mapped(plugin, param)))
        if knob and knob not in ours:
            ours.append(knob)
    nuke = [k for k in knobs if k in ours]
    return ours, nuke


def first_inversion(ours, nuke):
    """The first pair that is not in the same order on both sides.

    Args:
        ours: knob names, in our order.
        nuke: the same knob names, in Nuke's order.

    Returns:
        The first pair (a, b) of ours with a before b in ours but after it
        in nuke, or None if both orders are the same.
    """
    position = {k: i for i, k in enumerate(nuke)}
    for i, a in enumerate(ours):
        for b in ours[i + 1:]:
            if position[b] < position[a]:
                return a, b
    return None


def test_tables():
    """EXCLUDED, NAMES and KNOWN name plug-ins with cases, consistently."""
    for table in (EXCLUDED, NAMES, KNOWN):
        missing = sorted(set(table) - set(PLUGINS))
        assert not missing, "no compatibility case: %s" % missing
    excluded_known = sorted(set(KNOWN) & set(EXCLUDED))
    assert not excluded_known, "excluded but KNOWN: %s" % excluded_known
    untitled = sorted(set(i for i, _ in KNOWN.values()) - set(ISSUES))
    assert not untitled, "KNOWN issues without a title: %s" % untitled


def _param(plugin):
    """The pytest parameter of a plug-in, with its marks."""
    marks = [pytest.mark.nuke]
    if plugin in KNOWN:
        issue = KNOWN[plugin][0]
        marks.append(pytest.mark.xfail(
            strict=True, raises=AssertionError,
            reason="#%d: %s" % (issue, ISSUES[issue])))
    return pytest.param(plugin, id=harness.plugin_name(plugin), marks=marks)


def _orders(renderer, plugin):
    """The plug-in's params and its Nuke node's knobs."""
    node_class, context = PLUGINS[plugin]
    batch = next(b for b in NUKE_BATCHES if node_class in b)
    return (renderer.param_order(plugin, context),
            renderer.knob_order(node_class, batch))


@pytest.mark.parametrize("plugin", [_param(p) for p in COMPARED])
def test_param_order(plugin, renderer):
    """The params common with the Nuke node are in the same order.

    Only the order check raises AssertionError (the xfail of a KNOWN
    difference); the other checks fail with pytest.fail.
    """
    node_class = PLUGINS[plugin][0]
    params, knobs = _orders(renderer, plugin)
    try:
        by_key = knobs_by_key(knobs)
    except ValueError as ex:
        pytest.fail("%s: %s" % (node_class, ex))
    stale = sorted(p for p, k in NAMES.get(plugin, {}).items()
                   if p not in params or _key(k) not in by_key)
    if stale:
        pytest.fail("NAMES entries of %s without that param or knob: %s"
                    % (plugin, " ".join(stale)))
    ours, nuke = common_orders(plugin, params, knobs)
    if len(ours) < 2:
        pytest.fail("fewer than two params in common with %s: map their "
                    "names (NAMES) or exclude the plug-in (EXCLUDED)\n"
                    "  ours: %s\n  Nuke: %s"
                    % (node_class, " ".join(params), " ".join(knobs)))
    inversion = first_inversion(ours, nuke)
    expected = KNOWN.get(plugin, (None, None))[1]
    if inversion and expected and inversion != expected:
        pytest.fail("the first out-of-order pair is %s, %s, not %s, %s "
                    "(KNOWN)" % (inversion + expected))
    assert inversion is None, (
        "%s comes before %s, but after it in %s\n"
        "  common, our order:  %s\n  common, Nuke order: %s\n"
        "  our params: %s\n  Nuke knobs: %s"
        % (inversion[0], inversion[1], node_class, " ".join(ours),
           " ".join(nuke), " ".join(params), " ".join(knobs)))


@pytest.mark.nuke
def test_common_names_used(renderer):
    """Every COMMON_NAMES entry maps a param of a plug-in to a knob."""
    unused = set(COMMON_NAMES)
    for plugin in COMPARED:
        try:
            params, knobs = _orders(renderer, plugin)
        except pytest.skip.Exception:  # the plug-in is not installed
            continue
        keys = set(_key(k) for k in knobs)
        unused -= set(p for p in params if p in COMMON_NAMES
                      and p not in NAMES.get(plugin, {})
                      and _key(COMMON_NAMES[p]) in keys)
    assert not unused, ("COMMON_NAMES entries that match no param and "
                        "knob: %s" % " ".join(sorted(unused)))
