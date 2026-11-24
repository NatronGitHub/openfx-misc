"""Parameter order: each plug-in against the Nuke node of the same name.

Maintainer rule (2026-11-24): the order of a plug-in's parameters stays as
close as possible to the knob order of the node with the same name in
Nuke. (No host should address parameters by index: the OFX API has none.)

For each plug-in that has compatibility cases (cases.py), the test lists
the Nuke node's knobs in panel order (without the Node tab, the hidden
knobs and the labels and separators) and the plug-in's params in
definition order (hostDemo --describe, without pages, groups and secret
params). Names are compared after the mapping below (NAMES, then
COMMON_NAMES), ignoring case, "_" and "."; params that map to the same
knob (e.g. the four NatronOfxParamProcess* and "channels") count once, at
the first one. The params common to both sides must be in the same
relative order.

A known difference is a strict expected failure whose reason is the
GitHub issue that records it (see KNOWN and ISSUES), as in
test_compatibility.py.
"""

import pytest

import cases
import harness

# The plug-ins and their Nuke node: the node class of the first case.
PLUGINS = {}
for _case in cases.CASES:
    PLUGINS.setdefault(_case["ofx"][0], _case["nuke"][0])

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
    "net.sf.openfx.MergePlugin": dict(
        [("AChannels" + _c, "Achannels") for _c in "RGBA"]
        + [("BChannels" + _c, "Bchannels") for _c in "RGBA"]
        + [("OutputChannels" + _c, "output") for _c in ("", "R", "G", "B",
                                                         "A")]),
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

# The known differences: plug-in -> the GitHub issue that records it.
KNOWN = {
    "net.sf.openfx.AddPlugin": 377,
    "net.sf.openfx.MultiplyPlugin": 377,
    "net.sf.openfx.GammaPlugin": 377,
    "net.sf.openfx.Clamp": 377,
    "net.sf.openfx.GradePlugin": 377,
    "net.sf.openfx.ColorCorrectPlugin": 377,
    "net.sf.openfx.ColorMatrixPlugin": 377,
    "net.sf.openfx.SaturationPlugin": 377,
    "net.sf.openfx.Invert": 377,
    "net.sf.openfx.Log2Lin": 377,
    "net.sf.openfx.PLogLin": 377,
    "net.sf.openfx.Quantize": 377,
    "net.sf.cimg.CImgExpression": 377,
    "net.sf.openfx.MergePlugin": 378,
    "net.sf.openfx.CopyRectanglePlugin": 379,
    "net.sf.openfx.KeyerPlugin": 380,
    "net.sf.openfx.ConstantPlugin": 381,
    "net.sf.openfx.ColorWheel": 382,
    "net.sf.openfx.Ramp": 383,
    "net.sf.openfx.Radial": 384,
    "net.sf.openfx.Rectangle": 384,
    "net.sf.openfx.CornerPinPlugin": 385,
    "net.sf.openfx.IDistort": 386,
    "net.sf.openfx.STMap": 386,
    "net.sf.openfx.LensDistortion": 387,
    "eu.cimg.EdgeDetect": 388,
    "net.sf.cimg.CImgZDefocus": 389,
    "net.sf.openfx.Retime": 390,
    "net.sf.openfx.Reformat": 376,  # its PR fixes the order
}

# The short titles of the issues (xfail reasons).
ISSUES = {
    376: "Reformat: Preserve BBox after the filter parameters",
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

for _table in (EXCLUDED, NAMES, KNOWN):
    for _plugin in _table:
        if _plugin not in PLUGINS:
            raise ValueError("%s has no compatibility case" % _plugin)
for _plugin in KNOWN:
    if _plugin in EXCLUDED or KNOWN[_plugin] not in ISSUES:
        raise ValueError("KNOWN: %s is excluded, or its issue has no "
                         "title" % _plugin)

# The Nuke classes, in batches of one Nuke process each (a Nuke
# Non-Commercial session allows only 10 node objects).
NUKE_BATCH_SIZE = 8
_CLASSES = sorted(set(c for p, c in PLUGINS.items() if p not in EXCLUDED))
NUKE_BATCHES = [_CLASSES[i:i + NUKE_BATCH_SIZE]
                for i in range(0, len(_CLASSES), NUKE_BATCH_SIZE)]


def _key(name):
    """The name used to compare a param and a knob."""
    return name.lower().replace("_", "").replace(".", "")


def common_orders(plugin, params, knobs):
    """The params and knobs that both sides have, each in its own order.

    Args:
        plugin: the OFX plug-in identifier.
        params: the plug-in's param names, in definition order.
        knobs: the Nuke node's knob names, in panel order.

    Returns:
        (ours, nuke): two lists of the same Nuke knob names, in our order
        and in Nuke's.
    """
    names = NAMES.get(plugin, {})
    by_key = {}
    for knob in knobs:
        by_key.setdefault(_key(knob), knob)
    ours = []
    for param in params:
        knob = by_key.get(_key(names.get(param,
                                         COMMON_NAMES.get(param, param))))
        if knob and knob not in ours:
            ours.append(knob)
    nuke = [k for k in knobs if k in ours]
    return ours, nuke


def first_inversion(ours, nuke):
    """The first pair (a, b) with a before b in ours but after it in nuke."""
    position = {k: i for i, k in enumerate(nuke)}
    for i, a in enumerate(ours):
        for b in ours[i + 1:]:
            if position[b] < position[a]:
                return a, b
    return None


def _param(plugin):
    """The pytest parameter of a plug-in, with its marks."""
    marks = [pytest.mark.nuke]
    if plugin in KNOWN:
        marks.append(pytest.mark.xfail(
            strict=True, raises=AssertionError,
            reason="#%d: %s" % (KNOWN[plugin], ISSUES[KNOWN[plugin]])))
    return pytest.param(plugin, id=harness.plugin_name(plugin), marks=marks)


@pytest.mark.parametrize(
    "plugin", [_param(p) for p in PLUGINS if p not in EXCLUDED])
def test_param_order(plugin, renderer):
    """The params common with the Nuke node are in the same order."""
    node_class = PLUGINS[plugin]
    batch = next(b for b in NUKE_BATCHES if node_class in b)
    knobs = renderer.knob_order(node_class, batch)
    params = renderer.param_order(plugin)
    ours, nuke = common_orders(plugin, params, knobs)
    assert len(ours) >= 2, (
        "fewer than two params in common with %s: map their names (NAMES) "
        "or exclude the plug-in (EXCLUDED)\n  ours: %s\n  Nuke: %s"
        % (node_class, " ".join(params), " ".join(knobs)))
    inversion = first_inversion(ours, nuke)
    assert inversion is None, (
        "%s comes before %s, but after it in %s\n"
        "  common, our order:  %s\n  common, Nuke order: %s\n"
        "  our params: %s\n  Nuke knobs: %s"
        % (inversion[0], inversion[1], node_class, " ".join(ours),
           " ".join(nuke), " ".join(params), " ".join(knobs)))
