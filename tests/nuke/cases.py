"""The cases and input images of the compatibility tests (harness.py).

Images: IMAGES maps a name to a dict with
- "nuke": the four expressions (r, g, b, a) of a Nuke Expression node, in
  x and y (pixel indices from the bottom left, as in OFX images); or
  "color": the four expressions, in frame, of a uniform Nuke Constant (the
  Expression node's `frame` is the frame being viewed, not the one a time
  node asks for, but a Constant's colour expressions follow time nodes);
- "py": a function of (x, y) giving the same (r, g, b, a), written to the
  PFM files given to hostDemo; or "ofx": a hostDemo -i spec instead (for
  images that change with the frame or the view);
- "views": (left "color", right "color") instead of "nuke", for images
  that differ per view (two Constants and a JoinViews in Nuke);
- "premult": the OFX clip is premultiplied (#premult); "box": (x1, y1, x2,
  y2), the image is only defined there (a smaller PFM file at x1, y1 in
  OFX, whose RoD and format are the box; a Crop without black outside in
  Nuke, whose format stays WIDTH x HEIGHT).
The "noop" cases check that both sides get the same pixels.

Cases: CASES is a list of dicts with
- "name";
- "nuke": (node class, knobs): values are set with setValue, or fromScript
  for Script values; "nuke_post": Python lines run after that, with the
  node as `n`;
- "ofx": (plug-in identifier, params): hostDemo -s name=value (a list for
  a multi-dimensional param, None to press a push button); OFX positions
  are canonical: x in pixels times PAR;
- "input": the image of the only input (Nuke input 0, OFX clip Source), or
  "nuke_inputs" (the image of each Nuke input, None if disconnected) and
  "clips" (OFX clip name to image; the optional clips not listed are
  disconnected); no input for a generator;
- "context": the OFX context (default: filter, else general);
- "frames" (default [0]; whole frames only), "views" (default ["left"]);
- "nuke_edges": compare Nuke's pixels outside its bounding box as sample()
  gives them (the edge pixels repeated), not as 0;
- "region": the compared pixels, (x1, y1, x2, y2[, step]) or "project"
  (the PAL project, one pixel in 8x8); default the WIDTH x HEIGHT image;
  generators, "project" regions and "project_format" cases use the PAL
  project format in Nuke, the others WIDTH x HEIGHT;
- "channels": the compared channels, e.g. "rgb" or "rgba";
- "tolerance": the largest accepted absolute difference;
- "known": the GitHub issue of a known difference that the case confirms
  (the test is an expected failure, see ISSUES); None means the outputs
  must match.
"""

WIDTH = 32
HEIGHT = 32
PROJECT = (720, 576)  # hostDemo's PAL SD project, in pixels
PAR = 768. / 720.  # its pixel aspect ratio


class Script(str):
    """A knob value set with fromScript instead of setValue."""


def cx(x):
    """A canonical x coordinate (OFX) from a pixel one (Nuke)."""
    return x * PAR


def _u(x):
    return (x + .5) / WIDTH


def _v(y):
    return (y + .5) / HEIGHT


U = "((x + 0.5) / %d)" % WIDTH
V = "((y + 0.5) / %d)" % HEIGHT


def _clamp01(v):
    return min(max(v, 0.), 1.)


def _checker(x, y):
    return float((x // 4 + y // 4) % 2)


CHECKER = "fmod(floor(x / 4) + floor(y / 4), 2)"


def _const(r, g, b, a):
    def pixel(x, y):
        del x, y  # uniform
        return r, g, b, a
    return {"nuke": tuple(repr(float(c)) for c in (r, g, b, a)), "py": pixel}


# The pixel functions of the images (x, y: pixel indices from the bottom
# left); each one gives the same values as the image's Nuke expressions.
def _ramp(x, y):
    return -1. + 4. * _u(x), -.5 + 2.5 * _v(y), 1.5 - 2. * _u(x), 1.


def _premult(x, y):
    a = y / (HEIGHT - 1.)
    r, g, b, _ = _ramp(x, y)
    return r * a, g * a, b * a, a


def _impulse(x, y):
    if (x, y) == (WIDTH // 2, HEIGHT // 2):
        return 1., 1., 1., 1.
    return 0., 0., 0., 1.


def _hue_wheel(x, y, radius):
    dx, dy = x - 15.5, y - 15.5
    return (.5 + dx / radius,
            .5 + (-.5 * dx + .8660254 * dy) / radius,
            .5 + (-.5 * dx - .8660254 * dy) / radius, 1.)


def _hues(x, y):
    return _hue_wheel(x, y, 16.)


def _hues01(x, y):
    return _hue_wheel(x, y, 48.)


def _checker_image(x, y):
    c = _checker(x, y)
    return -.25 + 1.75 * c, 1. - 1.25 * c, .5 * c, 1.


def _merge_a(x, y, alpha=1.):
    del y  # A varies along x
    return -1. + 4. * _u(x), 2. - 3. * _u(x), .5, alpha


def _merge_b(x, y, alpha=1.):
    del x  # B varies along y
    return -1. + 4. * _v(y), 2. - 3. * _v(y), .25, alpha


def _merge_aa(x, y):
    return _merge_a(x, y, (y - 8.) / 16.)


def _merge_ba(x, y):
    return _merge_b(x, y, (x - 8.) / 16.)


def _merge_aa01(x, y):
    return _merge_a(x, y, _clamp01((y - 8.) / 16.))


def _merge_ba01(x, y):
    return _merge_b(x, y, _clamp01((x - 8.) / 16.))


def _mask(x, y):
    del y  # along x
    return (_clamp01((x - 8.) / 16.),) * 4


def _grey3(x, y):
    del y  # along x
    v = .1 if x < 11 else .5 if x < 22 else .9
    return v, v, v, 1.


def _uvwarp(x, y, alpha=1.):
    return (y - 16.) / 8., (x - 16.) / 16., 1.5 - 2. * _u(x), alpha


def _uvwarp_a(x, y):
    return _uvwarp(x, y, .5)


def _stmap(x, y):
    return .25 + .5 * _u(x) + .1 * _v(y), .2 + .6 * _v(y), 1., 1.


def _stmap_a(x, y):
    u, v, _, _ = _stmap(x, y)
    return .5 * u, .5 * v, .5, .5


def _stmap_a0(x, y):
    if x < 8:
        return 0., 0., 0., 0.
    return _stmap(x, y)


def _kernel(x, y):
    return (float((x - 14) + 3 * (y - 14) + 1),) * 4


def _kernel_rgb(x, y):
    return (float((x, y) == (15, 15)), float((x, y) == (14, 15)),
            float((x, y) == (15, 16)), float((x, y) == (15, 15)))


IMAGES = {
    # Every channel goes outside [0, 1]: R from -1 to 3 (left to right),
    # G from -0.5 to 2 (bottom to top), B from 1.5 down to -0.5; opaque.
    "ramp": {
        "nuke": ("-1 + 4 * " + U, "-0.5 + 2.5 * " + V, "1.5 - 2 * " + U, "1"),
        "py": _ramp,
    },
    # The ramp's colours premultiplied by an alpha from 0 (bottom row) to 1
    # (top row); flagged premultiplied in OFX.
    "premult": {
        "nuke": ("(-1 + 4 * %s) * y / %d" % (U, HEIGHT - 1),
                 "(-0.5 + 2.5 * %s) * y / %d" % (V, HEIGHT - 1),
                 "(1.5 - 2 * %s) * y / %d" % (U, HEIGHT - 1),
                 "y / %d" % (HEIGHT - 1)),
        "py": _premult,
        "premult": True,
    },
    # One white pixel at the centre, on black; opaque.
    "impulse": {
        "nuke": ("(x == %d && y == %d) ? 1 : 0" % (WIDTH // 2, HEIGHT // 2),)
        * 3 + ("1",),
        "py": _impulse,
    },
    # All hues around the centre, the saturation growing with the radius,
    # going outside [0, 1] in the corners; r + g + b = 1.5; opaque.
    "hues": {
        "nuke": ("0.5 + (x - 15.5) / 16",
                 "0.5 + (-0.5 * (x - 15.5) + 0.8660254 * (y - 15.5)) / 16",
                 "0.5 + (-0.5 * (x - 15.5) - 0.8660254 * (y - 15.5)) / 16",
                 "1"),
        "py": _hues,
    },
    # 4x4 checks of (-0.25, 1, 0) and (1.5, -0.25, 0.5); opaque.
    "checker": {
        "nuke": ("-0.25 + 1.75 * " + CHECKER, "1 - 1.25 * " + CHECKER,
                 "0.5 * " + CHECKER, "1"),
        "py": _checker_image,
    },
    # Merge inputs: A varies along x, B along y, so that each pixel is
    # another (A, B) pair, from -1 to 3. The "a" versions have an alpha
    # from -0.5 to 1.44 (A along y, B along x), 0 at y = 8 (A) or x = 8 (B);
    # the "a01" versions the same alpha clamped to [0, 1].
    "A": {
        "nuke": ("-1 + 4 * " + U, "2 - 3 * " + U, "0.5", "1"),
        "py": _merge_a,
    },
    "B": {
        "nuke": ("-1 + 4 * " + V, "2 - 3 * " + V, "0.25", "1"),
        "py": _merge_b,
    },
    "Aa": {
        "nuke": ("-1 + 4 * " + U, "2 - 3 * " + U, "0.5", "(y - 8) / 16"),
        "py": _merge_aa,
    },
    "Ba": {
        "nuke": ("-1 + 4 * " + V, "2 - 3 * " + V, "0.25", "(x - 8) / 16"),
        "py": _merge_ba,
    },
    "Aa01": {
        "nuke": ("-1 + 4 * " + U, "2 - 3 * " + U, "0.5",
                 "min(max((y - 8) / 16, 0), 1)"),
        "py": _merge_aa01,
    },
    "Ba01": {
        "nuke": ("-1 + 4 * " + V, "2 - 3 * " + V, "0.25",
                 "min(max((x - 8) / 16, 0), 1)"),
        "py": _merge_ba01,
    },
    # A mask: alpha from 0 (x <= 8) to 1 (x >= 24), RGB the same.
    "mask": {
        "nuke": ("min(max((x - 8) / 16, 0), 1)",) * 4,
        "py": _mask,
    },
    # Three grey columns: 0.1, 0.5, 0.9 (keyer polarity).
    "grey3": {
        "nuke": ("x < 11 ? 0.1 : (x < 22 ? 0.5 : 0.9)",) * 3 + ("1",),
        "py": _grey3,
    },
    # A uniform image: red = the frame number (hostDemo's default input,
    # view 0), green = blue = 0, opaque.
    "frame": {
        "color": ("frame", "0", "0", "1"),
        "ofx": "default",
    },
    # The right view of "views", in both views (JoinViews' right input).
    "frame_gb": {
        "color": ("0", "frame", "frame", "1"),
        "ofx": "default",
    },
    # Two views: left (frame, 0, 0, 1), right (0, frame, frame, 1), as
    # hostDemo's default input.
    "views": {
        "views": (("frame", "0", "0", "1"), ("0", "frame", "frame", "1")),
        "ofx": "default",
    },
    # All hues, as "hues", but within [0.06, 0.94].
    "hues01": {
        "nuke": ("0.5 + (x - 15.5) / 48",
                 "0.5 + (-0.5 * (x - 15.5) + 0.8660254 * (y - 15.5)) / 48",
                 "0.5 + (-0.5 * (x - 15.5) - 0.8660254 * (y - 15.5)) / 48",
                 "1"),
        "py": _hues01,
    },
    # IDistort: R and G are offsets in pixels (as UV), B a ramp; opaque, or
    # alpha 0.5.
    "uvwarp": {
        "nuke": ("(y - 16) / 8", "(x - 16) / 16", "1.5 - 2 * " + U, "1"),
        "py": _uvwarp,
    },
    "uvwarp_a": {
        "nuke": ("(y - 16) / 8", "(x - 16) / 16", "1.5 - 2 * " + U, "0.5"),
        "py": _uvwarp_a,
    },
    # STMap: R and G are normalised source positions, a zoom on the centre
    # with a twist; B and A 1.
    "stmap": {
        "nuke": ("0.25 + 0.5 * %s + 0.1 * %s" % (U, V), "0.2 + 0.6 * " + V,
                 "1", "1"),
        "py": _stmap,
    },
    # the same, premultiplied by alpha 0.5
    "stmap_a": {
        "nuke": ("0.5 * (0.25 + 0.5 * %s + 0.1 * %s)" % (U, V),
                 "0.5 * (0.2 + 0.6 * %s)" % V, "0.5", "0.5"),
        "py": _stmap_a,
    },
    # the same, with alpha 0 (and UV 0) on the 8 left columns
    "stmap_a0": {
        "nuke": ("x < 8 ? 0 : 0.25 + 0.5 * %s + 0.1 * %s" % (U, V),
                 "x < 8 ? 0 : 0.2 + 0.6 * %s" % V, "x < 8 ? 0 : 1",
                 "x < 8 ? 0 : 1"),
        "py": _stmap_a0,
    },
    # A 3x3 asymmetric kernel (Convolve's filter input), in a box.
    "kernel": {
        "nuke": ("(x - 14) + 3 * (y - 14) + 1",) * 4,
        "py": _kernel,
        "box": (14, 14, 17, 17),
    },
    # The same kernel with a different colour per channel.
    "kernel_rgb": {
        "nuke": ("x == 15 && y == 15 ? 1 : 0", "x == 14 && y == 15 ? 1 : 0",
                 "x == 15 && y == 16 ? 1 : 0", "x == 15 && y == 15 ? 1 : 0"),
        "py": _kernel_rgb,
        "box": (14, 14, 17, 17),
    },
    # Uniform images.
    "grey05_a0": _const(.5, .5, .5, 0.),
    "neg": _const(-.5, -.25, .5, 1.),
    "red": _const(.9, .1, .1, 1.),
    "orange": _const(.8, .4, .2, 1.),
    "keymix_A": _const(.2, .2, .2, .3),
    "keymix_B": _const(.7, .7, .7, .9),
    "half_mask": _const(0., 0., 0., .5),
    "shuffle_A": _const(.1, .2, .3, .4),
    "shuffle_B": _const(.6, .7, .8, .9),
    "white": _const(1., 1., 1., 1.),
    "clear": _const(0., 0., 0., 0.),
    "black": _const(0., 0., 0., 1.),
    "depth2": _const(2., 2., 2., 1.),
    "screen": _const(.1, .2, .8, 1.),
}
# An element on the left of the image (Mirror).
IMAGES["element"] = dict(IMAGES["ramp"], box=(2, 4, 12, 20))



def case(name, nuke, ofx, **kwargs):
    """A case with the defaults: RGB compared, tolerance 1e-5, no known."""
    result = {"name": name, "nuke": nuke, "ofx": ofx, "channels": "rgb",
              "tolerance": 1e-5, "known": None}
    result.update(kwargs)
    return result


def ofx_id(name):
    """The identifier of an openfx-misc plug-in of the Misc bundle."""
    return "net.sf.openfx." + name


def rgba(*values):
    """An OFX RGBA param value: one value for all, or four."""
    return list(values) * 4 if len(values) == 1 else list(values)


# Nuke uses the mask input's alpha when it is connected (maskChannelMask
# rgba.alpha by default); maskChannelInput would take it from the source.
MASKED = {}

CASES = []

# Self-tests: Nuke's Expression and the PFM files give the same pixels.
for _name, _image in sorted(IMAGES.items()):
    if "py" in _image:
        CASES.append(case(
            "noop " + _name, ("NoOp", {}), (ofx_id("NoOpPlugin"), {}),
            input=_name, channels="rgba", tolerance=1e-6))
CASES += [
    case("noop frame", ("NoOp", {}), (ofx_id("NoOpPlugin"), {}),
         input="frame", frames=[0, 3, 7], channels="rgba", tolerance=1e-6),
    case("noop views", ("NoOp", {}), (ofx_id("NoOpPlugin"), {}),
         input="views", views=["left", "right"], frames=[0, 4],
         channels="rgba", tolerance=1e-6),
]

# ---------------------------------------------------------------- Color
# Saturation outputs differ by up to 5e-4 here: tolerance 1e-3.
SATURATION_TOLERANCE = 1e-3
CASES += [
    case("Add defaults", ("Add", {}), (ofx_id("AddPlugin"), {}),
         input="Aa", channels="rgba"),
    case("Add 0.5", ("Add", {"value": 0.5}),
         (ofx_id("AddPlugin"), {"value": rgba(.5)}), input="ramp"),
    case("Add 0.1, alpha (all channels)", ("Add", {"value": .1}),
         (ofx_id("AddPlugin"), {"value": rgba(.1)}), input="Aa",
         channels="a", known=271),
    case("Add 0.1 alpha processed", ("Add", {"value": .1}),
         (ofx_id("AddPlugin"), {"value": rgba(.1),
                                "NatronOfxParamProcessA": 1}),
         input="Aa", channels="rgba"),
    case("Add 0.2 premultiplied input, (Un)premult off",
         ("Add", {"value": .2}),
         (ofx_id("AddPlugin"), {"value": rgba(.2), "premult": 0}),
         input="premult"),
    case("Add 0.2 unpremult by alpha", ("Add", {"value": .2,
                                                "unpremult": "rgba.alpha"}),
         (ofx_id("AddPlugin"), {"value": rgba(.2), "premult": 1}),
         input="premult", region=(0, 1, WIDTH, HEIGHT)),
    case("Add 0.3 masked, mix 0.5",
         ("Add", dict(MASKED, value=.3, mix=.5)),
         (ofx_id("AddPlugin"), {"value": rgba(.3), "mix": .5}),
         nuke_inputs=["ramp", "mask"], clips={"Source": "ramp",
                                              "Mask": "mask"}),
    case("Multiply defaults", ("Multiply", {}),
         (ofx_id("MultiplyPlugin"), {}), input="Aa", channels="rgba"),
    case("Multiply 2", ("Multiply", {"value": 2}),
         (ofx_id("MultiplyPlugin"), {"value": rgba(2)}), input="ramp"),
    case("Multiply 2, alpha (all channels)",
         ("Multiply", {"value": 2}),
         (ofx_id("MultiplyPlugin"), {"value": rgba(2)}), input="Aa",
         channels="a", known=271),
    case("Gamma defaults", ("Gamma", {}), (ofx_id("GammaPlugin"), {}),
         input="ramp", channels="rgba"),
    case("Gamma 0.7", ("Gamma", {"value": 0.7}),
         (ofx_id("GammaPlugin"), {"value": rgba(.7)}), input="ramp",
         tolerance=1e-4),
    case("Gamma 2 (negatives)", ("Gamma", {"value": 2.}),
         (ofx_id("GammaPlugin"), {"value": rgba(2.)}), input="ramp",
         tolerance=1e-4),
    case("Gamma 0.5, alpha (all channels)",
         ("Gamma", {"value": .5}),
         (ofx_id("GammaPlugin"), {"value": rgba(.5)}), input="Aa",
         channels="a", tolerance=1e-4,
         known=271),
    case("Clamp defaults", ("Clamp", {}), (ofx_id("Clamp"), {}),
         input="Aa", channels="rgba"),
    case("Clamp -0.5..2 with clamp to 0.1..0.9",
         ("Clamp", {"minimum": -.5, "maximum": 2., "MinClampTo": .1,
                    "MinClampTo_enable": True, "MaxClampTo": .9,
                    "MaxClampTo_enable": True}),
         (ofx_id("Clamp"), {"minimum": rgba(-.5), "maximum": rgba(2.),
                            "minClampTo": rgba(.1), "minClampToEnable": 1,
                            "maxClampTo": rgba(.9),
                            "maxClampToEnable": 1}),
         input="ramp"),
    case("Grade defaults", ("Grade", {}), (ofx_id("GradePlugin"), {}),
         input="ramp"),
    case("Grade gamma 0.7", ("Grade", {"gamma": 0.7}),
         (ofx_id("GradePlugin"), {"gamma": rgba(.7)}), input="ramp",
         tolerance=1e-4),
    case("Grade gamma 2, black clamp off (negatives)",
         ("Grade", {"gamma": 2., "black_clamp": False}),
         (ofx_id("GradePlugin"), {"gamma": rgba(2.), "clampBlack": 0}),
         input="ramp", region=(0, 0, 8, HEIGHT), channels="r",
         tolerance=1e-4),
    case("Grade gamma 2 reverse, black clamp off",
         ("Grade", {"gamma": 2., "black_clamp": False, "reverse": True}),
         (ofx_id("GradePlugin"), {"gamma": rgba(2.), "clampBlack": 0,
                                  "reverse": 1}),
         input="ramp", tolerance=1e-4),
    case("Grade black/white points, lift, gain, multiply, offset",
         ("Grade", {"blackpoint": .1, "whitepoint": .9, "black": .05,
                    "white": 1.2, "multiply": 1.1, "add": -.02,
                    "black_clamp": False}),
         (ofx_id("GradePlugin"), {"blackPoint": rgba(.1),
                                  "whitePoint": rgba(.9),
                                  "black": rgba(.05), "white": rgba(1.2),
                                  "multiply": rgba(1.1),
                                  "offset": rgba(-.02), "clampBlack": 0}),
         input="ramp", tolerance=1e-4),
    case("Grade white clamp", ("Grade", {"white_clamp": True}),
         (ofx_id("GradePlugin"), {"clampWhite": 1}), input="ramp"),
    case("Grade fringe on a mask", ("Grade", dict(MASKED, multiply=2.,
                                                  fringe=True)),
         (ofx_id("GradePlugin"), {"multiply": rgba(2.)}),
         nuke_inputs=["ramp", "mask"], clips={"Source": "ramp",
                                              "Mask": "mask"},
         known=271),
    case("Grade multiply 2 on a mask", ("Grade", dict(MASKED, multiply=2.)),
         (ofx_id("GradePlugin"), {"multiply": rgba(2.)}),
         nuke_inputs=["ramp", "mask"], clips={"Source": "ramp",
                                              "Mask": "mask"}),
    case("Grade invert (un)premult (1 - alpha)",
         ("Grade", {"multiply": 2., "unpremult": "rgba.alpha",
                    "invert_unpremult": True}),
         (ofx_id("GradePlugin"), {"multiply": rgba(2.), "premult": 1}),
         input="Aa", region=(0, 9, WIDTH, HEIGHT),
         known=271),
    case("ColorCorrect defaults (negatives)", ("ColorCorrect", {}),
         (ofx_id("ColorCorrectPlugin"), {}), input="ramp",
         known=272),
    case("ColorCorrect defaults, clamp black off", ("ColorCorrect", {}),
         (ofx_id("ColorCorrectPlugin"), {"clampBlack": 0}), input="ramp"),
    case("ColorCorrect contrast 2", ("ColorCorrect", {"contrast": 2.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(2.),
                                         "clampBlack": 0}),
         input="ramp", tolerance=1e-4,
         known=289),
    case("ColorCorrect contrast 2, positive values",
         ("ColorCorrect", {"contrast": 2.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(2.),
                                         "clampBlack": 0}),
         input="ramp", region=(8, 7, 24, HEIGHT), tolerance=1e-4),
    case("ColorCorrect shadows gain 2, highlights gain 4",
         ("ColorCorrect", {"shadows.gain": 2., "highlights.gain": 4.}),
         (ofx_id("ColorCorrectPlugin"), {"ShadowsGain": rgba(2.),
                                         "HighlightsGain": rgba(4.),
                                         "clampBlack": 0}),
         input="ramp", tolerance=1e-4,
         known=289),
    case("ColorCorrect all master knobs",
         ("ColorCorrect", {"saturation": .5, "contrast": 1.5, "gamma": 1.2,
                           "gain": 1.1, "offset": .05}),
         (ofx_id("ColorCorrectPlugin"), {
             "MasterSaturation": rgba(.5), "MasterContrast": rgba(1.5),
             "MasterGamma": rgba(1.2), "MasterGain": rgba(1.1),
             "MasterOffset": rgba(.05), "clampBlack": 0}),
         input="hues01", tolerance=SATURATION_TOLERANCE),
    case("ColorCorrect all master knobs, values outside [0, 1]",
         ("ColorCorrect", {"saturation": .5, "contrast": 1.5, "gamma": 1.2,
                           "gain": 1.1, "offset": .05}),
         (ofx_id("ColorCorrectPlugin"), {
             "MasterSaturation": rgba(.5), "MasterContrast": rgba(1.5),
             "MasterGamma": rgba(1.2), "MasterGain": rgba(1.1),
             "MasterOffset": rgba(.05), "clampBlack": 0}),
         input="hues", tolerance=1e-4,
         known=289),
    case("ColorCorrect midtones saturation 2, gamma 0.8",
         ("ColorCorrect", {"midtones.saturation": 2., "midtones.gamma": .8}),
         (ofx_id("ColorCorrectPlugin"), {"MidtonesSaturation": rgba(2.),
                                         "MidtonesGamma": rgba(.8),
                                         "clampBlack": 0}),
         input="hues", tolerance=1e-4,
         known=289),
    case("ColorMatrix identity (negatives)",
         ("ColorMatrix", {"matrix": Script("{1 0 0} {0 1 0} {0 0 1}")}),
         (ofx_id("ColorMatrixPlugin"), {}), input="ramp",
         known=272),
    case("ColorMatrix Rec.709 to XYZ, clamp black off",
         ("ColorMatrix", {"matrix": Script(
             "{0.4124 0.3576 0.1805} {0.2126 0.7152 0.0722}"
             " {0.0193 0.1192 0.9505}")}),
         (ofx_id("ColorMatrixPlugin"), {
             "outputRed": [.4124, .3576, .1805, 0.],
             "outputGreen": [.2126, .7152, .0722, 0.],
             "outputBlue": [.0193, .1192, .9505, 0.], "clampBlack": 0}),
         input="ramp", tolerance=1e-4),
    case("ColorMatrix invert",
         ("ColorMatrix", {"matrix": Script("{2 0 0} {0 1 0} {0 0 1}"),
                          "invert": True}),
         (ofx_id("ColorMatrixPlugin"), {"outputRed": [2., 0., 0., 0.],
                                        "clampBlack": 0}),
         input="ramp", known=291),
    case("Saturation 0.5", ("Saturation", {"saturation": 0.5}),
         (ofx_id("SaturationPlugin"), {"saturation": 0.5}), input="ramp",
         tolerance=1e-4, known=272),
    case("Saturation 0.5, clamp black off",
         ("Saturation", {"saturation": 0.5}),
         (ofx_id("SaturationPlugin"), {"saturation": 0.5, "clampBlack": 0}),
         input="ramp", tolerance=SATURATION_TOLERANCE),
    case("Saturation 2 on (.9, .1, .1)", ("Saturation", {"saturation": 2.}),
         (ofx_id("SaturationPlugin"), {"saturation": 2.}), input="red",
         tolerance=1e-4, known=272),
    case("Saturation 2, clamp black off, hues",
         ("Saturation", {"saturation": 2.}),
         (ofx_id("SaturationPlugin"), {"saturation": 2., "clampBlack": 0}),
         input="hues", tolerance=SATURATION_TOLERANCE),
    case("Saturation -1 (negative)", ("Saturation", {"saturation": -1.}),
         (ofx_id("SaturationPlugin"), {"saturation": -1., "clampBlack": 0}),
         input="orange", tolerance=SATURATION_TOLERANCE),
    case("Saturation mode Maximum, 0.3",
         ("Saturation", {"saturation": .3, "mode": "Maximum"}),
         (ofx_id("SaturationPlugin"), {"saturation": .3, "clampBlack": 0,
                                       "luminanceMath": "Max"}),
         input="hues", tolerance=1e-4),
    case("HSVTool defaults (negatives)", ("HSVTool", {}),
         (ofx_id("HSVToolPlugin"), {}), input="ramp", channels="rgba",
         known=272),
    case("HSVTool defaults, clamp black off", ("HSVTool", {}),
         (ofx_id("HSVToolPlugin"), {"clampBlack": 0}), input="hues",
         channels="rgba", tolerance=1e-4),
    case("HSVTool hue rotation 90, saturation -0.3, brightness 0.2",
         ("HSVTool", {"huerotation": 90., "saturation": -.3,
                      "brightness": .2}),
         (ofx_id("HSVToolPlugin"), {"hueRotation": 90.,
                                    "saturationAdjustment": -.3,
                                    "brightnessAdjustment": .2,
                                    "clampBlack": 0}),
         input="hues", tolerance=1e-4),
    case("HSVTool force saturation 0.2", ("HSVTool", {
        "setsaturation": True, "saturation": .2}),
         (ofx_id("HSVToolPlugin"), {"saturationAdjustment": .2,
                                    "saturationAdjustmentGain": 0.,
                                    "clampBlack": 0}),
         input="hues", tolerance=1e-4, known=293),
    case("Invert defaults", ("Invert", {}), (ofx_id("Invert"), {}),
         input="Aa", channels="rgba"),
    case("Invert clamp", ("Invert", {"clamp": True}), (ofx_id("Invert"), {}),
         input="ramp", known=294),
    case("Premult defaults", ("Premult", {}), (ofx_id("Premult"), {}),
         input="Aa01", channels="rgba"),
    case("Premult, alpha outside [0, 1]", ("Premult", {}),
         (ofx_id("Premult"), {}), input="Aa", channels="rgba"),
    case("Unpremult defaults", ("Unpremult", {}), (ofx_id("Unpremult"), {}),
         input="premult", channels="rgba"),
    case("Unpremult zero alpha", ("Unpremult", {}),
         (ofx_id("Unpremult"), {}), input="grey05_a0", channels="rgba"),
    case("Premult invert", ("Premult", {"invert": True}),
         (ofx_id("Premult"), {}), input="Aa",
         known=295),
    # Code values above 1023 give huge values: only up to 1.
    case("Log2Lin defaults", ("Log2Lin", {}), (ofx_id("Log2Lin"), {}),
         input="ramp", region=(0, 0, 16, 19), tolerance=1e-3),
    case("Log2Lin lin2log", ("Log2Lin", {"operation": "lin2log"}),
         (ofx_id("Log2Lin"), {"operation": "Lin to Log"}), input="ramp",
         tolerance=1e-4),
    case("Log2Lin lin2log, positive values",
         ("Log2Lin", {"operation": "lin2log"}),
         (ofx_id("Log2Lin"), {"operation": "Lin to Log"}), input="ramp",
         region=(8, 7, 24, HEIGHT), tolerance=1e-4),
    case("PLogLin defaults", ("PLogLin", {}), (ofx_id("PLogLin"), {}),
         input="ramp", region=(0, 0, 16, 19), tolerance=1e-3),
    case("PLogLin lin to log", ("PLogLin", {"operation": "lin to log"}),
         (ofx_id("PLogLin"), {"operation": "Lin to Log"}), input="ramp",
         tolerance=1e-4),
    case("Posterize (Quantize) defaults", ("Posterize", {}),
         (ofx_id("Quantize"), {}), input="ramp"),
    case("Colorspace linear to HSV (RGBToHSV)",
         ("Colorspace", {"colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace linear to HSL (RGBToHSL)",
         ("Colorspace", {"colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace linear to YCbCr (RGBToYCbCr709)",
         ("Colorspace", {"colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to HSV (RGBToHSV)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to HSL (RGBToHSL)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to YCbCr (RGBToYCbCr709)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace rec709 to YCbCr (RGBToYCbCr709)",
         ("Colorspace", {"colorspace_in": "rec709",
                         "colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace linear to HSV, values outside [0, 1]",
         ("Colorspace", {"colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {}), input="hues", tolerance=1e-4),
    case("Colorspace linear to CIE-XYZ (RGB709ToXYZ)",
         ("Colorspace", {"colorspace_out": "CIE-XYZ"}),
         (ofx_id("RGB709ToXYZ"), {}), input="hues", tolerance=5e-4),
    case("Colorspace linear to CIE-Lab (RGB709ToLab)",
         ("Colorspace", {"colorspace_out": "CIE-Lab"}),
         (ofx_id("RGB709ToLab"), {}), input="hues", tolerance=5e-3),
    case("Expression r, input alpha 0.5 (CImgExpression i)",
         ("Expression", {"expr0": "r", "expr1": "g", "expr2": "b"}),
         ("net.sf.cimg.CImgExpression", {}), input="keymix_A",
         channels="rgba"),
    case("Expression x/32 (CImgExpression x/32)",
         ("Expression", {"expr0": "x/32", "expr1": "x/32", "expr2": "x/32"}),
         ("net.sf.cimg.CImgExpression", {"expression": "x/32"}),
         input="ramp"),
    # CImgExpression's x and y are buffer indices from the lower-left corner of the RoD, at the render
    # scale; X and Y are the full-size pixel coordinates (#297).
    case("Expression x/32 on an element not at 0,0 (CImgExpression X/32)",
         ("Expression", {"expr0": "x/32", "expr1": "y/32", "expr2": "0"}),
         ("net.sf.cimg.CImgExpression",
          {"expression": "c==0?X/32:c==1?Y/32:0"}),
         input="element"),
]

# ---------------------------------------------------------------- Merge
MERGE_OPERATORS = [
    "atop", "average", "color-burn", "color-dodge", "conjoint-over", "copy",
    "difference", "disjoint-over", "divide", "exclusion", "from",
    "geometric", "hard-light", "hypot", "in", "mask", "matte", "max", "min",
    "minus", "multiply", "out", "over", "overlay", "plus", "screen",
    "soft-light", "stencil", "under", "xor"]
# Operators whose renders differ for these inputs (Merge issue).
MERGE_KNOWN = {
    "color-burn", "color-dodge", "divide", "exclusion", "geometric",
    "hard-light", "overlay", "screen", "soft-light"}
MERGE_KNOWN_ALPHA = set()
# Operators that use alpha: compared with alpha outside [0, 1] too.
MERGE_ALPHA_OPERATORS = [
    "atop", "conjoint-over", "disjoint-over", "in", "mask", "matte", "out",
    "over", "stencil", "under", "xor"]
for _op in MERGE_OPERATORS:
    CASES.append(case(
        "Merge %s, opaque" % _op, ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["B", "A"], clips={"Source": "B", "A": "A"},
        channels="rgba", tolerance=1e-4,
        known=277 if _op in MERGE_KNOWN else None))
    CASES.append(case(
        "Merge %s, alpha" % _op, ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["Ba01", "Aa01"], clips={"Source": "Ba01", "A": "Aa01"},
        channels="rgba", tolerance=1e-4,
        known=277 if _op in MERGE_KNOWN | MERGE_KNOWN_ALPHA else None))
for _op in MERGE_ALPHA_OPERATORS:
    CASES.append(case(
        "Merge %s, alpha outside [0, 1]" % _op,
        ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "A": "Aa"},
        channels="rgba", tolerance=1e-4))
for _op in ("multiply", "min", "overlay", "divide", "over"):
    CASES.append(case(
        "Merge %s, alpha masking" % _op,
        ("Merge2", {"operation": _op, "screen_alpha": True}),
        (ofx_id("MergePlugin"), {"operation": _op, "screenAlpha": 1}),
        nuke_inputs=["Ba01", "Aa01"], clips={"Source": "Ba01", "A": "Aa01"},
        channels="rgba", tolerance=1e-4,
        known=277 if _op != "over" else None))
CASES += [
    case("Merge over, mask and mix 0.5",
         ("Merge2", dict(MASKED, mix=.5)), (ofx_id("MergePlugin"),
                                            {"mix": .5}),
         nuke_inputs=["Ba01", "Aa01", "mask"],
         clips={"Source": "Ba01", "A": "Aa01", "Mask": "mask"},
         channels="rgba", tolerance=1e-4),
    case("Merge plus, A channels rgb only (Achannels)",
         ("Merge2", {"operation": "plus", "Achannels": "rgb"}),
         (ofx_id("MergePlugin"), {"operation": "plus", "AChannelsA": 0}),
         nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "A": "Aa"},
         channels="rgba", tolerance=1e-4),
    case("Keymix defaults (constants)", ("Keymix", {}),
         (ofx_id("KeyMix"), {}),
         nuke_inputs=["keymix_B", "keymix_A", "half_mask"],
         clips={"Source": "keymix_B", "A": "keymix_A", "Mask": "half_mask"},
         channels="rgba", tolerance=1e-5),
    case("Keymix defaults (ramps)", ("Keymix", {}), (ofx_id("KeyMix"), {}),
         nuke_inputs=["Ba", "Aa", "mask"],
         clips={"Source": "Ba", "A": "Aa", "Mask": "mask"},
         channels="rgba", tolerance=1e-5),
    case("Keymix invert mask, mix 0.7",
         ("Keymix", {"invertMask": True, "mix": .7}),
         (ofx_id("KeyMix"), {"maskInvert": 1, "mix": .7}),
         nuke_inputs=["Ba", "Aa", "mask"],
         clips={"Source": "Ba", "A": "Aa", "Mask": "mask"},
         channels="rgba", tolerance=1e-5),
    case("Dissolve 0.3", ("Dissolve", {"which": .3}),
         (ofx_id("DissolvePlugin"), {"which": .3}),
         nuke_inputs=["Ba", "Aa"], clips={"0": "Ba", "1": "Aa"},
         channels="rgba", tolerance=1e-5),
    case("Dissolve 0.6 with a mask", ("Dissolve", dict(MASKED, which=.6)),
         (ofx_id("DissolvePlugin"), {"which": .6}),
         nuke_inputs=["Ba", "Aa", "mask"],
         clips={"0": "Ba", "1": "Aa", "Mask": "mask"},
         channels="rgba", tolerance=1e-5),
    case("Switch 1", ("Switch", {"which": 1}),
         (ofx_id("switchPlugin"), {"which": 1}),
         nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "1": "Aa"},
         channels="rgba"),
    case("CopyRectangle area 8,8-24,20",
         ("CopyRectangle", {"area": [8, 8, 24, 20]}),
         (ofx_id("CopyRectanglePlugin"), {"bottomLeft": [cx(8), 8],
                                          "size": [cx(16), 12]}),
         nuke_inputs=["Ba", "Aa"], clips={"B": "Ba", "A": "Aa"},
         channels="rgba"),
    case("CopyRectangle area 6,6-26,26 softness 4",
         ("CopyRectangle", {"area": [6, 6, 26, 26], "softness": 4}),
         (ofx_id("CopyRectanglePlugin"), {"bottomLeft": [cx(6), 6],
                                          "size": [cx(20), 20],
                                          "softness": 4}),
         nuke_inputs=["black", "white"], clips={"B": "black", "A": "white"},
         channels="rgba", tolerance=1e-4),
    case("CopyRectangle area 6,6-26,26 softness 8",
         ("CopyRectangle", {"area": [6, 6, 26, 26], "softness": 8}),
         (ofx_id("CopyRectanglePlugin"), {"bottomLeft": [cx(6), 6],
                                          "size": [cx(20), 20],
                                          "softness": 8}),
         nuke_inputs=["black", "white"], clips={"B": "black", "A": "white"},
         channels="rgba", tolerance=1e-4),
    # Fractional edges; at the top and right edges, the last pixel of the
    # fade is A.
    case("CopyRectangle area 6.3,6.7-25.6,25.2 softness 4",
         ("CopyRectangle", {"area": [6.3, 6.7, 25.6, 25.2], "softness": 4}),
         (ofx_id("CopyRectanglePlugin"), {"bottomLeft": [cx(6.3), 6.7],
                                          "size": [cx(19.3), 18.5],
                                          "softness": 4}),
         nuke_inputs=["black", "white"], clips={"B": "black", "A": "white"},
         channels="rgba", tolerance=1e-4),
    case("CopyRectangle area 6.3,6.7-25.6,25.2 softness 0",
         ("CopyRectangle", {"area": [6.3, 6.7, 25.6, 25.2]}),
         (ofx_id("CopyRectanglePlugin"), {"bottomLeft": [cx(6.3), 6.7],
                                          "size": [cx(19.3), 18.5]}),
         nuke_inputs=["black", "white"], clips={"B": "black", "A": "white"},
         channels="rgba", tolerance=1e-4),
    # ShuffleCopy's input 0 is "2" (B), input 1 is "1" (A).
    case("ShuffleCopy defaults", ("ShuffleCopy", {}),
         (ofx_id("ShufflePlugin"), {}),
         nuke_inputs=["shuffle_B", "shuffle_A"],
         clips={"A": "shuffle_A", "B": "shuffle_B"}, channels="rgba",
         context="general"),
    case("Shuffle2 defaults", ("Shuffle2", {}),
         (ofx_id("ShufflePlugin"), {}), input="Aa", channels="rgba"),
    case("Difference defaults", ("Difference", {}),
         (ofx_id("DifferencePlugin"), {}),
         nuke_inputs=["B", "A"], clips={"A": "A", "B": "B"},
         channels="a", tolerance=1e-4),
    case("Difference, RGB passes through", ("Difference", {}),
         (ofx_id("DifferencePlugin"), {}),
         nuke_inputs=["B", "A"], clips={"A": "A", "B": "B"},
         channels="rgb"),
    case("Difference gain 2 offset 0.01", ("Difference", {"gain": 2.,
                                                          "offset": .01}),
         (ofx_id("DifferencePlugin"), {"gain": 2., "offset": .01}),
         nuke_inputs=["B", "A"], clips={"A": "A", "B": "B"},
         channels="a", tolerance=1e-4),
]

# ---------------------------------------------------------------- Keyers
# Nuke's range A, B, C, D = 0.2, 0.4, 0.6, 0.8, with the conversion of
# openfx-misc's Keyer description.
KEYER_ABCD = {"softnessLower": -.2, "toleranceLower": -.1, "center": .5,
              "toleranceUpper": .1, "softnessUpper": .2}
CASES += [
    case("Keyer luminance key, same A, B, C, D (polarity)",
         ("Keyer", {"operation": "luminance key",
                    "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), dict(KEYER_ABCD, show="Unpremultiplied")),
         input="grey3", channels="a", tolerance=1e-4,
         known=280),
    case("Keyer luminance key, inverted",
         ("Keyer", {"operation": "luminance key", "invert": True,
                    "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), dict(KEYER_ABCD, show="Unpremultiplied")),
         input="ramp", channels="a",
         tolerance=5e-3),  # outputs differ by up to 3e-3 here
    case("PIK C-blue on IBK (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue"}),
         (ofx_id("PIK"), {"screenType": "C-Blue", "show": "Premultiplied"}),
         nuke_inputs=["hues01", "screen"],
         clips={"Source": "hues01", "C": "screen"},
         channels="rgba", tolerance=1e-3),
]

# ------------------------------------------------------------ Generators
# Compared over the PAL project, one pixel in 8x8. Nuke positions are in
# pixels, OFX ones canonical.
GEN = {"region": "project", "channels": "rgba"}
CASES += [
    case("Constant defaults", ("Constant", {}),
         (ofx_id("ConstantPlugin"), {}), **GEN),
    case("Constant (0.25, -0.5, 1.5, 0.75)",
         ("Constant", {"color": [.25, -.5, 1.5, .75]}),
         (ofx_id("ConstantPlugin"), {"color": [.25, -.5, 1.5, .75]}), **GEN),
    case("ColorBars defaults", ("ColorBars", {}), (ofx_id("ColorBars"), {}),
         **dict(GEN, known=286)),
    case("ColorWheel defaults", ("ColorWheel", {}),
         (ofx_id("ColorWheel"), {}), **GEN),
    # Inside the disc (its anti-aliased edge differs).
    case("ColorWheel gamma 1", ("ColorWheel", {"gamma": 1.}),
         (ofx_id("ColorWheel"), {"gamma": 1.}),
         **dict(GEN, region=(232, 160, 488, 416, 8), tolerance=5e-3)),
    case("ColorWheel gamma 0.45", ("ColorWheel", {"gamma": .45}),
         (ofx_id("ColorWheel"), {"gamma": .45}),
         **dict(GEN, region=(232, 160, 488, 416, 8), tolerance=5e-3)),
    case("ColorWheel gamma 1, whole disc", ("ColorWheel", {"gamma": 1.}),
         (ofx_id("ColorWheel"), {"gamma": 1.}), **dict(GEN, tolerance=5e-3)),
    case("Ramp defaults", ("Ramp", {}), (ofx_id("Ramp"), {}),
         **dict(GEN, tolerance=1e-4)),
]
# CheckerBoard: every pixel around the centre and in the bottom-left
# corner (lines are thinner than the 8-pixel steps). Both box sizes are
# in square pixels (divided by the PAR in x).
CHECKER_REGIONS = {"centre": (328, 256, 392, 320), "corner": (0, 0, 64, 64)}
for _where, _region in sorted(CHECKER_REGIONS.items()):
    CASES += [
        # Only the centre lines differ (centerline width 3 vs 1).
        case("CheckerBoard defaults, %s" % _where, ("CheckerBoard2", {}),
             (ofx_id("CheckerBoardPlugin"), {}), region=_region,
             channels="rgba",
             known=285 if _where == "centre" else None),
        case("CheckerBoard centerline 3, %s" % _where,
             ("CheckerBoard2", {}),
             (ofx_id("CheckerBoardPlugin"), {"centerlineWidth": 3}),
             region=_region, channels="rgba"),
        case("CheckerBoard box 10, no centerline, four colours, %s" % _where,
             ("CheckerBoard2", {"boxsize": 10, "centerlinewidth": 0,
                                "color0": [1, 0, 0, 1], "color1": [0, 1, 0, 1],
                                "color2": [0, 0, 1, 1],
                                "color3": [1, 1, 0, 1]}),
             (ofx_id("CheckerBoardPlugin"), {
                 "boxSize": [10, 10], "centerlineWidth": 0,
                 "color0": [1, 0, 0, 1], "color1": [0, 1, 0, 1],
                 "color2": [0, 0, 1, 1], "color3": [1, 1, 0, 1]}),
             region=_region, channels="rgba",
             known=285),
    ]
for _nuke_type, _ofx_type in (("linear", "Linear"), ("plinear", "PLinear"),
                              ("smooth0", "Ease-in"), ("smooth1", "Ease-out"),
                              ("smooth", "Smooth")):
    CASES.append(case(
        "Ramp %s, horizontal" % _nuke_type,
        ("Ramp", {"type": _nuke_type, "p0": [100, 288], "p1": [600, 288]}),
        (ofx_id("Ramp"), {"type": _ofx_type, "point0": [cx(100), 288],
                          "point1": [cx(600), 288]}),
        **dict(GEN, tolerance=1e-4)))
CASES += [
    case("Radial area 100,100-600,476 softness 1",
         ("Radial", {"area": [100, 100, 600, 476]}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376]}),
         **dict(GEN, tolerance=1e-4)),
    case("Radial softness 0.3, plinear",
         ("Radial", {"area": [100, 100, 600, 476], "softness": .3,
                     "plinear": True}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376], "softness": .3,
                             "plinear": 1}),
         **dict(GEN, tolerance=1e-4)),
    case("Rectangle area 100,100-600,476",
         ("Rectangle", {"area": [100, 100, 600, 476]}),
         (ofx_id("Rectangle"), {"bottomLeft": [cx(100), 100],
                                "size": [cx(500), 376]}),
         **dict(GEN, tolerance=1e-4)),
    # Softness is canonical: 40 is 37.5 pixels in x.
    case("Rectangle softness 40",
         ("Rectangle", {"area": [100, 100, 600, 476], "softness": 40}),
         (ofx_id("Rectangle"), {"bottomLeft": [cx(100), 100],
                                "size": [cx(500), 376], "softness": 40}),
         **dict(GEN, tolerance=1e-4)),
]

# ------------------------------------------------------------- Transform
TRANSFORM = ofx_id("TransformPlugin")
CENTRE = {"nuke": [16, 16], "ofx": [cx(16), 16]}
CASES += [
    case("Transform defaults", ("Transform", {"center": CENTRE["nuke"]}),
         (TRANSFORM, {"center": CENTRE["ofx"]}), input="checker",
         channels="rgba"),
    case("Transform translate 3,-2 impulse",
         ("Transform", {"translate": [3, -2], "filter": "impulse"}),
         (TRANSFORM, {"translate": [cx(3), -2], "filter": "Impulse"}),
         input="checker", channels="rgba"),
    case("Transform translate 0.5,0.25 cubic, edges",
         ("Transform", {"translate": [.5, .25]}),
         (TRANSFORM, {"translate": [cx(.5), .25]}), input="ramp",
         channels="rgba", tolerance=1e-3),
]
# Geometry: a linear ramp, which every filter reproduces, away from the
# edges (outside the RoD, openfx-misc filters against black; the reference
# repeats the edge pixels).
INNER = (5, 5, 27, 27)
for _name, _nuke, _ofx in (
        ("translate 0.5,0.25", {"translate": [.5, .25]},
         {"translate": [cx(.5), .25]}),
        ("rotate 30", {"rotate": 30}, {"rotate": 30}),
        ("scale 1.5,0.8", {"scale": [1.5, .8]}, {"scale": [1.5, .8]}),
        ("skewX 0.3", {"skewX": .3}, {"skewX": .3}),
        ("skewY 0.3", {"skewY": .3}, {"skewY": .3}),
        ("translate, scale, skew, rotate (order)",
         {"translate": [3, -2], "scale": [1.5, .8], "skewX": .3,
          "rotate": 30},
         {"translate": [cx(3), -2], "scale": [1.5, .8], "skewX": .3,
          "rotate": 30})):
    CASES.append(case(
        "Transform %s" % _name,
        ("Transform", dict(_nuke, center=CENTRE["nuke"])),
        (TRANSFORM, dict(_ofx, center=CENTRE["ofx"])), input="ramp",
        region=INNER, channels="rgba", tolerance=1e-3))
# Filters: a translation by a fraction of a pixel of the checker (no
# minification), away from the edges.
for _nuke_filter, _ofx_filter in (("linear", "Bilinear"), ("cubic", "Cubic"),
                                  ("Keys", "Keys"), ("Simon", "Simon"),
                                  ("Rifman", "Rifman"),
                                  ("Mitchell", "Mitchell"),
                                  ("Parzen", "Parzen"), ("notch", "Notch")):
    CASES.append(case(
        "Transform filter %s, translate 0.3,0.6" % _ofx_filter,
        ("Transform", {"translate": [.3, .6], "filter": _nuke_filter}),
        (TRANSFORM, {"translate": [cx(.3), .6], "filter": _ofx_filter}),
        input="checker", region=INNER, channels="rgba", tolerance=1e-3))
CASES.append(case(
    "Transform cubic, scale 0.5 (minification)",
    ("Transform", {"scale": .5, "center": CENTRE["nuke"]}),
    (TRANSFORM, {"scale": [.5, .5], "center": CENTRE["ofx"]}),
    input="checker", region=(9, 9, 23, 23), channels="rgba",
    tolerance=1e-3))
CASES += [
    case("TransformMasked translate 3,-2 with a mask",
         ("TransformMasked", dict(MASKED, translate=[3, -2],
                                  filter="impulse")),
         (ofx_id("TransformMaskedPlugin"), {"translate": [cx(3), -2],
                                            "filter": "Impulse"}),
         nuke_inputs=["checker", "mask"],
         clips={"Source": "checker", "Mask": "mask"}, channels="rgba"),
    case("CornerPin2D four pins",
         ("CornerPin2D", {"from1": [0, 0], "from2": [32, 0],
                          "from3": [32, 32], "from4": [0, 32],
                          "to1": [2, 1], "to2": [30, 3], "to3": [29, 31],
                          "to4": [1, 30]}),
         (ofx_id("CornerPinPlugin"), {
             "from1": [0, 0], "from2": [cx(32), 0], "from3": [cx(32), 32],
             "from4": [0, 32], "to1": [cx(2), 1], "to2": [cx(30), 3],
             "to3": [cx(29), 31], "to4": [cx(1), 30]}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
]
for _pins in (1, 2, 3):
    _enables = {"enable%d" % i: i <= _pins for i in range(1, 5)}
    CASES.append(case(
        "CornerPin2D %d pin(s) enabled" % _pins,
        ("CornerPin2D", dict(_enables, **{
            "from1": [0, 0], "from2": [32, 0], "from3": [32, 32],
            "from4": [0, 32], "to1": [2, 1], "to2": [30, 3],
            "to3": [29, 31], "to4": [1, 30]})),
        (ofx_id("CornerPinPlugin"), dict(
            {k: int(v) for k, v in _enables.items()}, **{
                "from1": [0, 0], "from2": [cx(32), 0], "from3": [cx(32), 32],
                "from4": [0, 32], "to1": [cx(2), 1], "to2": [cx(30), 3],
                "to3": [cx(29), 31], "to4": [cx(1), 30]})),
        input="ramp", region=INNER, channels="rgba", tolerance=1e-3))
CASES += [
    case("Crop 8,8-24,20 defaults",
         ("Crop", {"box": [8, 8, 24, 20]}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(8), 8],
                                 "size": [cx(16), 12]}),
         input="ramp", channels="rgba"),
    case("Crop 8,8-24,20, edges seen downstream",
         ("Crop", {"box": [8, 8, 24, 20]}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(8), 8],
                                 "size": [cx(16), 12]}),
         input="ramp", channels="rgba", nuke_edges=True),
    # Softness is canonical: 4 is 3.75 pixels in x.
    case("Crop softness 4", ("Crop", {"box": [4, 4, 28, 28], "softness": 4}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(4), 4],
                                 "size": [cx(24), 24], "softness": 4}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("Crop softness 8", ("Crop", {"box": [4, 4, 28, 28], "softness": 8}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(4), 4],
                                 "size": [cx(24), 24], "softness": 8}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("Crop softness 1", ("Crop", {"box": [4, 4, 28, 28], "softness": 1}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(4), 4],
                                 "size": [cx(24), 24], "softness": 1}),
         input="ramp", channels="rgba", tolerance=1e-3),
    # Fractional edges: the fade starts half a pixel outside the box.
    case("Crop 4.3,4.7-27.6,27.2 softness 4",
         ("Crop", {"box": [4.3, 4.7, 27.6, 27.2], "softness": 4}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(4.3), 4.7],
                                 "size": [cx(23.3), 22.5], "softness": 4}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("Crop softness 4, no black outside",
         ("Crop", {"box": [4, 4, 28, 28], "softness": 4, "crop": False}),
         (ofx_id("CropPlugin"), {"bottomLeft": [cx(4), 4],
                                 "size": [cx(24), 24], "softness": 4,
                                 "blackOutside": 0}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("Reformat scale 0.5 defaults",
         ("Reformat", {"type": "scale", "scale": .5}),
         (ofx_id("Reformat"), {"reformatType": "Scale",
                               "reformatScale": [.5, .5]}),
         input="checker", channels="rgba", tolerance=1e-3),
    case("Reformat scale 0.5, black outside",
         ("Reformat", {"type": "scale", "scale": .5, "black_outside": True}),
         (ofx_id("Reformat"), {"reformatType": "Scale",
                               "reformatScale": [.5, .5],
                               "black_outside": 1}),
         input="checker", channels="rgba", tolerance=1e-3),
    case("Reformat scale 0.5 impulse",
         ("Reformat", {"type": "scale", "scale": .5, "filter": "impulse"}),
         (ofx_id("Reformat"), {"reformatType": "Scale",
                               "reformatScale": [.5, .5],
                               "filter": "Impulse"}),
         input="checker", channels="rgba", tolerance=1e-3),
    case("Reformat to box 20x12 fit, box PAR = PAR",
         ("Reformat", {"type": "to box", "box_width": 20, "box_height": 12,
                       "box_fixed": True, "resize": "fit",
                       "box_pixel_aspect": PAR}),
         (ofx_id("Reformat"), {"reformatType": "To Box",
                               "boxSize": [20, 12], "boxFixed": 1,
                               "boxPar": PAR, "resize": "Fit"}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("Reformat to box 20x12 fit, reference black outside",
         ("Reformat", {"type": "to box", "box_width": 20, "box_height": 12,
                       "box_fixed": True, "resize": "fit",
                       "box_pixel_aspect": PAR, "black_outside": True}),
         (ofx_id("Reformat"), {"reformatType": "To Box",
                               "boxSize": [20, 12], "boxFixed": 1,
                               "boxPar": PAR, "resize": "Fit"}),
         input="ramp", channels="rgba", tolerance=1e-3),
    case("AdjBBox defaults (AdjustRoD)", ("AdjBBox", {}),
         (ofx_id("AdjustRoDPlugin"), {}), input="ramp", channels="rgba",
         region=(0, 0, 64, 64), nuke_edges=False),
    # Shrinking: whole pixels, truncated toward zero (-4 / PAR: 3 in x).
    case("AdjBBox -4 (AdjustRoD -4)", ("AdjBBox", {"numpixels": -4}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [-4, -4]}),
         input="ramp", channels="rgba", nuke_edges=False),
    case("AdjBBox -5.6 (AdjustRoD -5.6)", ("AdjBBox", {"numpixels": -5.6}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [-5.6, -5.6]}),
         input="ramp", channels="rgba", nuke_edges=False),
    # Both amounts are in square pixels: 16 / (768 / 720) = 15 pixels in
    # x; the renders differ by one column there, which x < 26 avoids.
    case("AdjBBox 16, AdjustRoD 16 (both divided by the PAR in x)",
         ("AdjBBox", {"numpixels": 16}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [16, 16]}),
         input="element", channels="rgba", region=(0, 0, 26, 40)),
    case("Position 3,-2", ("Position", {"translate": [3, -2]}),
         (ofx_id("Position"), {"translate": [cx(3), -2]}), input="ramp",
         channels="rgba"),
    case("Position 3,4 on an element, edges seen downstream",
         ("Position", {"translate": [3, 4]}),
         (ofx_id("Position"), {"translate": [cx(3), 4]}), input="element",
         channels="rgba", nuke_edges=True,
         known=300),
    case("Mirror2 flip", ("Mirror2", {"flip": True}),
         (ofx_id("Mirror"), {"flip": 1}), input="ramp", channels="rgba"),
    case("Mirror2 flop", ("Mirror2", {"flop": True}),
         (ofx_id("Mirror"), {"flop": 1}), input="ramp", channels="rgba"),
    case("Mirror2 flop on an element (axis)", ("Mirror2", {"flop": True}),
         (ofx_id("Mirror"), {"flop": 1}), input="element", channels="rgba",
         known=301),
    case("Card3D defaults", ("Card3D", {}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32]}),
         input="checker", channels="rgba", tolerance=1e-3),
    case("Card3D rotate z 30",
         ("Card3D", {"rotate": [0, 0, 30]}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32],
                             "cardRotate": [0, 0, 30]}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("Card3D rotate y 20, z 30",
         ("Card3D", {"rotate": [0, 20, 30]}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32],
                             "cardRotate": [0, 20, 30]}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("Card3D skew x 20, rotate z 30",
         ("Card3D", {"skew": [20, 0, 0], "rotate": [0, 0, 30]}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32],
                             "cardSkew": [20, 0, 0],
                             "cardRotate": [0, 0, 30]}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("IDistort uv = rgb",
         ("IDistort", {"uv": "rgb"}),
         (ofx_id("IDistort"), {}), clips={"Source": "uvwarp",
                                          "UV": "uvwarp"},
         nuke_inputs=["uvwarp"], channels="rgba", tolerance=1e-3),
    case("IDistort uv = rgb, uv scale 2, impulse",
         ("IDistort", {"uv": "rgb", "uv_scale": 2, "filter": "impulse"}),
         (ofx_id("IDistort"), {"uvScale": [2, 2], "filter": "Impulse"}),
         clips={"Source": "uvwarp", "UV": "uvwarp"},
         nuke_inputs=["uvwarp"], channels="rgba", tolerance=1e-3),
    case("IDistort, alpha 0.5",
         ("IDistort", {"uv": "rgb"}),
         (ofx_id("IDistort"), {}), clips={"Source": "uvwarp_a",
                                          "UV": "uvwarp_a"},
         nuke_inputs=["uvwarp_a"], channels="rgba", tolerance=1e-3),
    case("STMap uv = rgb", ("STMap", {"uv": "rgb"}),
         (ofx_id("STMap"), {}), nuke_inputs=["ramp", "stmap"],
         clips={"Source": "ramp", "UV": "stmap"}, channels="rgba",
         tolerance=2e-3),
    # UV premultiplied by alpha 0.5 (Unpremult UV): the offsets are doubled
    case("IDistort, alpha 0.5, premultiplied",
         ("IDistort", {"uv": "rgb", "premultiplied": True}),
         (ofx_id("IDistort"), {"unpremultUV": 1}),
         clips={"Source": "uvwarp_a", "UV": "uvwarp_a"},
         nuke_inputs=["uvwarp_a"], channels="rgba", tolerance=1e-3),
    case("STMap uv = rgb, premultiplied, alpha 0.5",
         ("STMap", {"uv": "rgb", "premultiplied": True}),
         (ofx_id("STMap"), {"unpremultUV": 1}),
         nuke_inputs=["ramp", "stmap_a"],
         clips={"Source": "ramp", "UV": "stmap_a"}, channels="rgba",
         tolerance=2e-3),
    case("STMap uv = rgb, premultiplied, alpha 0 on the left",
         ("STMap", {"uv": "rgb", "premultiplied": True}),
         (ofx_id("STMap"), {"unpremultUV": 1}),
         nuke_inputs=["ramp", "stmap_a0"],
         clips={"Source": "ramp", "UV": "stmap_a0"}, channels="rgba",
         tolerance=2e-3),
    # Blur channel: B of uvwarp goes from 1.47 (left) to -0.47 (right)
    case("IDistort uv = rgb, blur = blue",
         ("IDistort", {"uv": "rgb", "blur": "rgba.blue"}),
         (ofx_id("IDistort"), {"channelBlur": 2}),
         clips={"Source": "uvwarp", "UV": "uvwarp"},
         nuke_inputs=["uvwarp"], channels="rgba", tolerance=1e-3),
    case("IDistort uv = rgb, blur = blue, blur scale 4",
         ("IDistort", {"uv": "rgb", "blur": "rgba.blue", "blur_scale": 4}),
         (ofx_id("IDistort"), {"channelBlur": 2, "blurScale": 4}),
         clips={"Source": "uvwarp", "UV": "uvwarp"},
         nuke_inputs=["uvwarp"], channels="rgba", tolerance=1e-3),
    case("STMap uv = rgb, blur = blue (1), blur scale 3",
         ("STMap", {"uv": "rgb", "blur": "rgba.blue", "blur_scale": 3}),
         (ofx_id("STMap"), {"channelBlur": 2, "blurScale": 3}),
         nuke_inputs=["ramp", "stmap"],
         clips={"Source": "ramp", "UV": "stmap"}, channels="rgba",
         tolerance=2e-3),
    case("LensDistortion k 0.1, undistort",
         ("LensDistortion2", {"output": "Undistort",
                              "distortionNumerator0": .1}),
         (ofx_id("LensDistortion"), {"k1": .1, "direction": "Undistort"}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("LensDistortion k 0.1, undistort (k1 -0.1)",
         ("LensDistortion2", {"output": "Undistort",
                              "distortionNumerator0": .1}),
         (ofx_id("LensDistortion"), {"k1": -.1, "direction": "Undistort"}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("LensDistortion k 0.1, redistort (distort)",
         ("LensDistortion2", {"output": "Redistort",
                              "distortionNumerator0": .1}),
         (ofx_id("LensDistortion"), {"k1": .1, "direction": "Distort"}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
]
for _steps in (1, 2, 3, 5):
    CASES.append(case(
        "GodRays scale 2, steps %d" % _steps,
        ("GodRays", {"scale": 2, "steps": _steps, "center": [16, 16]}),
        (ofx_id("GodRays"), {"scale": [2, 2], "steps": _steps,
                             "center": [cx(16), 16]}),
        input="ramp", channels="rgba", tolerance=2e-2,
        clips={"Source": "ramp", "Mask": "white"}))

# ---------------------------------------------------------------- Filter
# Sharpen and Soften size 3, with x in pixels (the size is canonical).
SHARPEN_SIZE = {"size": [cx(3), 3], "uniform": 0}
CIMG = "net.sf.cimg."
CASES += [
    case("Blur defaults (size 0)", ("Blur", {}), (CIMG + "CImgBlur", {}),
         input="ramp", channels="rgba"),
    # The impulse response peaks at about 0.01: a tight tolerance.
    case("Blur size 10, impulse (BlurCImg size 10)",
         ("Blur", {"size": 10}), (CIMG + "CImgBlur", {"size": [10, 10]}),
         input="impulse", tolerance=1e-4),
    case("Blur size 10, impulse (BlurCImg size x times PAR)",
         ("Blur", {"size": 10}),
         (CIMG + "CImgBlur", {"size": [cx(10), 10], "uniform": 0}),
         input="impulse", tolerance=1e-4),
    case("Blur size 4 on the ramp (edges)", ("Blur", {"size": 4}),
         (CIMG + "CImgBlur", {"size": [cx(4), 4], "uniform": 0}),
         input="ramp", channels="rgba", tolerance=2e-3),
    case("Blur size 4 on the ramp, BlurCImg boundary Nearest",
         ("Blur", {"size": 4}),
         (CIMG + "CImgBlur", {"size": [cx(4), 4], "uniform": 0,
                              "boundary": "Nearest"}),
         input="ramp", channels="rgba", tolerance=2e-3),
    case("Laplacian size 3", ("Laplacian", {"size": 3}),
         (CIMG + "CImgLaplacian", {"size": [cx(3), 3], "uniform": 0}),
         input="impulse", tolerance=0.03),
    case("Laplacian size 3, tail (IIR Gaussian)", ("Laplacian", {"size": 3}),
         (CIMG + "CImgLaplacian", {"size": [cx(3), 3], "uniform": 0}),
         input="impulse", tolerance=1e-3,
         known=312),
    # Sharpen and Soften: RGB away from the edges (SharpenCImg and
    # SoftenCImg leave alpha alone by default; the edges are #318).
    case("Sharpen defaults on the checker", ("Sharpen", {}),
         (CIMG + "CImgSharpen", SHARPEN_SIZE), input="checker",
         region=INNER, tolerance=2e-3, known=313),
    case("Sharpen defaults, values in [0, 1]", ("Sharpen", {}),
         (CIMG + "CImgSharpen", SHARPEN_SIZE), input="mask",
         region=INNER, tolerance=2e-3, known=312),
    case("Sharpen minimum -10, maximum 10",
         ("Sharpen", {"minimum": -10, "maximum": 10}),
         (CIMG + "CImgSharpen", SHARPEN_SIZE), input="checker",
         region=INNER, tolerance=2e-3),
    case("Soften defaults on the checker", ("Soften", {}),
         (CIMG + "CImgSoften", SHARPEN_SIZE), input="checker",
         region=INNER, tolerance=2e-3, known=313),
    case("Soften minimum -10, maximum 10",
         ("Soften", {"minimum": -10, "maximum": 10}),
         (CIMG + "CImgSoften", SHARPEN_SIZE), input="checker",
         region=INNER, tolerance=2e-3),
    # The reference writes its edges into alpha, EdgeDetectCImg into RGB.
    case("EdgeDetect sobel", ("EdgeDetect", {}),
         ("eu.cimg.EdgeDetect", {"filter": "Sobel"}), input="checker",
         channels="rgba", tolerance=1e-3,
         known=314),
    case("Dilate size 1 (DilateCImg 1)", ("Dilate", {"size": 1}),
         (CIMG + "CImgDilate", {"size": [1, 1]}), input="impulse",
         channels="rgba"),
    case("Dilate size -1 (ErodeCImg 1)", ("Dilate", {"size": -1}),
         (CIMG + "CImgErode", {"size": [1, 1]}), input="checker",
         channels="rgba"),
    case("Dilate size 1.5 (DilateCImg 2)", ("Dilate", {"size": 1.5}),
         (CIMG + "CImgDilate", {"size": [2, 2]}), input="impulse",
         channels="rgba", known=315),
    case("Dilate defaults (DilateCImg defaults)", ("Dilate", {}),
         (CIMG + "CImgDilate", {}), input="impulse", channels="rgba",
         known=315),
    case("Erode (blur) defaults (ErodeBlur)", ("Erode", {}),
         ("eu.cimg.ErodeBlur", {}), input="mask", channels="rgba",
         tolerance=1e-3),
    case("Erode (blur) size -3, blur 0.5 (ErodeBlur -3)",
         ("Erode", {"size": -3, "blur": .5}),
         ("eu.cimg.ErodeBlur", {"size": -3, "blur": .5}), input="mask",
         channels="rgba", tolerance=1e-3),
    case("Erode (blur) size 3, blur 0.5", ("Erode", {"size": 3,
                                                     "blur": .5}),
         ("eu.cimg.ErodeBlur", {"size": 3, "blur": .5}), input="mask",
         channels="rgba", tolerance=1e-3),
    case("FilterErode size 1 box (ErodeSmoothCImg)",
         ("FilterErode", {"size": 1}),
         (CIMG + "CImgErodeSmooth", {"size": [1, 1], "filter": "Box",
                                     "NatronOfxParamProcessR": 0,
                                     "NatronOfxParamProcessG": 0,
                                     "NatronOfxParamProcessB": 0,
                                     "NatronOfxParamProcessA": 1}),
         input="mask", channels="rgba", tolerance=1e-3),
    case("Median size 3 (MedianCImg 1)", ("Median", {}),
         (CIMG + "CImgMedian", {}), input="checker", channels="rgb"),
    case("Median size 3 on an impulse", ("Median", {}),
         (CIMG + "CImgMedian", {}), input="impulse", channels="rgb"),
    case("Median size 5 (MedianCImg 2)", ("Median", {"size": 5}),
         (CIMG + "CImgMedian", {"size": 2}), input="checker",
         region=(3, 3, 29, 29), channels="rgb",
         known=315),
    case("Convolve2 3x3 kernel, edges", ("Convolve2", {}),
         (CIMG + "CImgConvolve", {}), nuke_inputs=["ramp", "kernel"],
         clips={"Source": "ramp", "Filter": "kernel"}, channels="rgba",
         tolerance=1e-3),
    case("Convolve2 3x3 kernel (ConvolveCImg)", ("Convolve2", {}),
         (CIMG + "CImgConvolve", {}), nuke_inputs=["ramp", "kernel"],
         clips={"Source": "ramp", "Filter": "kernel"}, channels="rgba",
         region=(2, 2, 30, 30), tolerance=1e-3),
    case("Convolve2 coloured kernel (per channel in ConvolveCImg)",
         ("Convolve2", {}), (CIMG + "CImgConvolve", {}),
         nuke_inputs=["checker", "kernel_rgb"],
         clips={"Source": "checker", "Filter": "kernel_rgb"},
         channels="rgba",
         region=(2, 2, 30, 30), tolerance=1e-3,
         known=316),
    case("Matrix defaults (MatrixCImg 3x3)", ("Matrix", {}),
         ("eu.cimg.CImgMatrix3x3", {}), input="ramp", channels="rgba"),
    # The first matrix row of the reference is MatrixCImg's matrix3x.
    case("Matrix element 0 = 1 (matrix31)",
         ("Matrix", {"matrix": Script("{1 0 0} {0 0 0} {0 0 0}")}),
         ("eu.cimg.CImgMatrix3x3", {"matrix31": 1}), input="impulse",
         channels="rgba"),
    case("Matrix Laplacian, normalize off",
         ("Matrix", {"matrix": Script("{0 -1 0} {-1 4 -1} {0 -1 0}")}),
         ("eu.cimg.CImgMatrix3x3", {"matrix12": -1, "matrix21": -1,
                                    "matrix22": 4, "matrix23": -1,
                                    "matrix32": -1}),
         input="ramp", channels="rgba", tolerance=1e-4),
    # #269 changes ZDefocusCImg's semantics (#266); its new cases will
    # replace these two.
    case("ZDefocus2 depth 2, focus 1, size 20 (ZDefocusCImg)",
         ("ZDefocus2", {"math": "depth", "center": 1., "size": 20.,
                        "max_size": 40.}),
         (CIMG + "CImgZDefocus", {"depthType": "Depth", "focus": 1.,
                                  "size": 20., "maxSize": 40.,
                                  "useFocusPoint": 0, "aperture": "Disc"}),
         nuke_inputs=["impulse"],
         clips={"Source": "impulse", "Depth": "depth2"},
         nuke_post=("d = nuke.nodes.Expression(inputs=[images['impulse']],"
                    " expr0='2')\n"
                    "d['channel0'].fromScript('depth.Z')\n"
                    "n.setInput(0, d)"),
         channels="rgb", tolerance=1e-3, known=266),
    case("ZDefocus2 defaults, depth 2 (ZDefocusCImg defaults)",
         ("ZDefocus2", {}), (CIMG + "CImgZDefocus", {}),
         nuke_inputs=["impulse"],
         clips={"Source": "impulse", "Depth": "depth2"},
         nuke_post=("d = nuke.nodes.Expression(inputs=[images['impulse']],"
                    " expr0='2')\n"
                    "d['channel0'].fromScript('depth.Z')\n"
                    "n.setInput(0, d)"),
         channels="rgb", tolerance=1e-3,
         known=266),
]

# ------------------------------------------------------------------ Time
# "frame" has red = the frame number; hostDemo's clips have frames 0-25.
TIME = {"input": "frame", "channels": "rgba", "tolerance": 1e-4}
CASES += [
    case("TimeOffset 5", ("TimeOffset", {"time_offset": 5}),
         (ofx_id("timeOffset"), {"timeOffset": 5}),
         frames=[0, 3, 10, 25], **TIME),
    # A fractional offset: Whole Frames (the default) truncates it toward
    # zero; Fractional Time uses it (the frame range uses the truncated one).
    case("TimeOffset 5.5, whole frames", ("TimeOffset", {"time_offset": 5.5}),
         (ofx_id("timeOffset"), {"timeOffset": 5.5}),
         frames=[0, 3, 10, 25], **TIME),
    case("TimeOffset -5.5, fractional time",
         ("TimeOffset", {"time_offset": -5.5,
                         "rounding_mode": "Fractional time"}),
         (ofx_id("timeOffset"), {"timeOffset": -5.5,
                                 "roundingMode": "Fractional Time"}),
         frames=[0, 3, 10, 19], **TIME),
    case("TimeOffset 5, reverse input",
         ("TimeOffset", {"time_offset": 5, "reverse_input": True}),
         (ofx_id("timeOffset"), {"timeOffset": 5, "reverseInput": 1}),
         frames=[0, 4, 10, 17, 25], **TIME),
    # FrameHold is not compared: in `nuke -t`, sampling FrameHold returns
    # its input unchanged, so the reference cannot be rendered.
    case("FrameRange 10-20", ("FrameRange", {"first_frame": 10,
                                             "last_frame": 20}),
         (ofx_id("FrameRange"), {"frameRange": [10, 20]}),
         frames=[5, 15, 25], **dict(
             TIME, known=308)),
    case("FrameRange 10-20, before/after Original",
         ("FrameRange", {"first_frame": 10, "last_frame": 20}),
         (ofx_id("FrameRange"), {"frameRange": [10, 20],
                                 "before": "Original",
                                 "after": "Original"}),
         frames=[5, 15, 25], **TIME),
    case("Retime speed 0.5, filter none",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "filter": "none"}),
         (ofx_id("Retime"), {"speed": .5, "filter": "None"}),
         frames=[0, 1, 3, 7, 10], **TIME),
    case("Retime speed 0.5, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": .5, "filter": "Nearest"}),
         frames=[0, 1, 3, 7, 10], **TIME),
    case("Retime speed 2, default filters (box vs linear)",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2.}),
         (ofx_id("Retime"), {"speed": 2.}),
         frames=[0, 1, 3, 7, 10], **dict(TIME, known=309)),
    case("FrameBlend defaults", ("FrameBlend", {}),
         (ofx_id("FrameBlend"), {}), frames=[10],
         **dict(TIME, known=310)),
    case("FrameBlend 6 frames (FrameBlend defaults)",
         ("FrameBlend", {"numframes": 6}), (ofx_id("FrameBlend"), {}),
         frames=[10], **TIME),
    case("TimeBlur defaults", ("TimeBlur", {}), (ofx_id("TimeBlur"), {}),
         frames=[10], **TIME),
    case("TimeBlur divisions 2, shutter 1, start",
         ("TimeBlur", {"divisions": 2, "shutter": 1.,
                       "shutteroffset": "start"}),
         (ofx_id("TimeBlur"), {"division": 2, "shutter": 1.,
                               "shutterOffset": "Start"}),
         frames=[10], **TIME),
    case("TimeBlur shutter 3",
         ("TimeBlur", {"divisions": 4, "shutter": 3.,
                       "shutteroffset": "start"}),
         (ofx_id("TimeBlur"), {"division": 4, "shutter": 3.,
                               "shutterOffset": "Start"}),
         frames=[10], **TIME),  # hostDemo does not enforce the maximum
    case("TimeDissolve in 1, out 10",
         ("TimeDissolve", {"in": 1, "out": 10}),
         (ofx_id("TimeDissolvePlugin"), {}),
         nuke_inputs=["black", "white"],
         clips={"Source": "black", "A": "white"},
         frames=[0, 1, 2, 5, 9, 10, 11], channels="rgba", tolerance=1e-4),
    case("AppendClip defaults", ("AppendClip", {}),
         (ofx_id("AppendClip"), {}), nuke_inputs=["frame", "white"],
         clips={"1": "frame", "2": "white"},
         frames=[0, 1, 25, 26, 27, 40], channels="rgba", tolerance=1e-4),
    case("AppendClip fade in 3", ("AppendClip", {"fadeIn": 3}),
         (ofx_id("AppendClip"), {"fadeIn": 3}),
         nuke_inputs=["frame", "white"], clips={"1": "frame", "2": "white"},
         frames=[0, 1, 2, 3, 4, 5], channels="rgba", tolerance=1e-4),
    case("AppendClip dissolve 4", ("AppendClip", {"dissolve": 4}),
         (ofx_id("AppendClip"), {"crossDissolve": 4}),
         nuke_inputs=["frame", "white"], clips={"1": "frame", "2": "white"},
         frames=[20, 22, 24, 25, 26, 28], channels="rgba", tolerance=1e-4),
]

# ----------------------------------------------------------------- Views
VIEWS = {"input": "views", "views": ["left", "right"], "frames": [4],
         "channels": "rgba", "tolerance": 1e-4}
CASES += [
    # Outputs differ by up to 4e-4 here: tolerance 1e-3.
    case("Anaglyph defaults", ("Anaglyph", {}),
         (ofx_id("anaglyphPlugin"), {}),
         **dict(VIEWS, views=["left"], tolerance=1e-3)),
    case("Anaglyph amount of colour 0.5", ("Anaglyph", {"amtcolour": .5}),
         (ofx_id("anaglyphPlugin"), {"amtcolor": .5}),
         **dict(VIEWS, views=["left"], tolerance=1e-3)),
    case("MixViews 0.3", ("MixViews", {"mix": .3}),
         (ofx_id("mixViewsPlugin"), {"mix": .3}), **VIEWS),
    # PFM inputs are the same in both views.
    case("JoinViews", ("JoinViews", {}), (ofx_id("joinViewsPlugin"), {}),
         nuke_inputs=["keymix_A", "keymix_B"],
         clips={"Left": "keymix_A", "Right": "keymix_B"},
         **dict(VIEWS, input=None)),
]

# ---------------------------------------------------------- ContactSheet
for _center in (False, True):
    CASES.append(case(
        "ContactSheet 2 inputs, 2x2 cells of 32x32, center %s"
        % ("on" if _center else "off"),
        ("ContactSheet", {"width": 64, "height": 64, "rows": 2,
                          "columns": 2, "center": _center,
                          "roworder": "TopBottom"}),
        (ofx_id("ContactSheetOFX"), {"resolution": [64, 64],
                                     "rowsColumns": [2, 2],
                                     "center": int(_center),
                                     "rowOrder": "TopBottom"}),
        nuke_inputs=["ramp", "checker"],
        clips={"Source": "ramp", "1": "checker"}, region=(0, 0, 64, 64),
        channels="rgba", tolerance=1e-3,
        known=305 if _center else None))

# ------------------------------------------------------------ ReConverge
# The cases cover the convergence modes (shift right, left, both), the
# offset, converge points inside and outside the image, no disparity, and
# non-integer shifts (see #267 for the expected behaviour). Only the
# Source channels are compared. Not compared: three views (hostDemo renders
# two) and proxy scale.

# The disparity (disparityR.x in the reference, red of the OFX Disparity
# clip), the same in both views, non-integer: d(13, 9) = 4.75, d(20, 3) = 6.75.
_DISP_EXPR = "(x - 8) * 0.5 + y * 0.25"


def _disparity(x, y):
    return (x - 8) * .5 + y * .25, 0., 0., 1.


IMAGES["disparity"] = {
    "nuke": (_DISP_EXPR, "0", "0", "1"),  # unused: see _RECONVERGE_DISP
    "py": _disparity,
}
# The reference node has one input: the disparity goes in its disparityR.x.
_RECONVERGE_DISP = (
    "d = nuke.nodes.Expression(inputs=[n.input(0)],"
    " channel0='disparityR.x', expr0=%r)\n"
    "for _i in (1, 2, 3):\n"
    "    d['channel%%d' %% _i].setValue('none')\n"
    "n.setInput(0, d)" % _DISP_EXPR)


def _reconverge(name, knobs, params, disparity=True):
    """A ReConverge case: both views, RGBA; params are the OFX ones."""
    extra = ({"nuke_post": _RECONVERGE_DISP,
              "clips": {"Source": "ramp", "Disparity": "disparity"}}
             if disparity else {})
    return case(name, ("ReConverge", knobs),
                (ofx_id("reConvergePlugin"), params), input="ramp",
                context="general", views=["left", "right"], channels="rgba",
                tolerance=1e-6, **extra)


def _point(x, y):
    """The converge point knobs: Nuke's in pixels, OFX's canonical."""
    return {"convergepoint": [x, y]}, {"convergePoint": [cx(x), y]}


# (name, x, y, Nuke knobs, OFX params, disparity); convergeMode is the
# index (0 shift right, 1 shift left, 2 shift both).
_RECONVERGE = [
    ("shift right", 13.5, 9.5, {"convergemode": "shift right"},
     {"convergeMode": 0}, True),
    ("shift left", 13.5, 9.5, {"convergemode": "shift left"},
     {"convergeMode": 1}, True),
    ("shift both", 13.5, 9.5, {"convergemode": "shift both"},
     {"convergeMode": 2}, True),
    ("offset 2.6", 13.5, 9.5, {"offset": 2.6}, {"offset": 2.6}, True),
    ("shift both offset 3", 13.5, 9.5,
     {"offset": 3, "convergemode": "shift both"},
     {"offset": 3, "convergeMode": 2}, True),
    ("converge point 20.9,3.2", 20.9, 3.2, {}, {}, True),
    ("converge point outside", -5, 40, {}, {}, True),
    ("no disparity, offset -3", None, None, {"offset": -3},
     {"offset": -3}, False),
    ("no disparity, shift left, offset 5.9", None, None,
     {"offset": 5.9, "convergemode": "shift left"},
     {"offset": 5.9, "convergeMode": 1}, False),
    # Truncation: d(30, 0) = 11, d(10, 0) = 1, d(2, 1) = -2.75.
    ("shift both d=11 o=3", 30.5, .5,
     {"offset": 3, "convergemode": "shift both"},
     {"offset": 3, "convergeMode": 2}, True),
    ("shift both d=1 o=1", 10.5, .5,
     {"offset": 1, "convergemode": "shift both"},
     {"offset": 1, "convergeMode": 2}, True),
    ("shift right d=-2.75 o=2.6", 2.5, 1.5, {"offset": 2.6},
     {"offset": 2.6}, True),
    ("shift both d=-2.75 o=-3", 2.5, 1.5,
     {"offset": -3, "convergemode": "shift both"},
     {"offset": -3, "convergeMode": 2}, True),
]
for _name, _x, _y, _knobs, _params, _disparity in _RECONVERGE:
    _nuke_point, _ofx_point = _point(_x, _y) if _x is not None else ({}, {})
    CASES.append(_reconverge("ReConverge " + _name,
                             dict(_nuke_point, **_knobs),
                             dict(_ofx_point, **_params), _disparity))


# ---------------------------------------------- More common-use cases
CASES += [
    case("FilterErode size 0 (ErodeSmoothCImg size 0)",
         ("FilterErode", {"size": 0}),
         (CIMG + "CImgErodeSmooth", {}), input="mask", channels="rgba"),
    case("Radial softness 0",
         ("Radial", {"area": [100, 100, 600, 476], "softness": 0}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376], "softness": 0}),
         **dict(GEN, tolerance=1e-4)),
    case("Retime speed 0.5, filter none, whole source frames",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "filter": "none"}),
         (ofx_id("Retime"), {"speed": .5, "filter": "None"}),
         frames=[0, 2, 4, 10], **TIME),
    case("TimeDissolve in 1, out 10, after the In frame",
         ("TimeDissolve", {"in": 1, "out": 10}),
         (ofx_id("TimeDissolvePlugin"), {}),
         nuke_inputs=["black", "white"],
         clips={"Source": "black", "A": "white"},
         frames=[0, 2, 5, 9, 10, 11], channels="rgba", tolerance=1e-4),
    case("AppendClip defaults, after the first frames",
         ("AppendClip", {}), (ofx_id("AppendClip"), {}),
         nuke_inputs=["frame", "white"], clips={"1": "frame", "2": "white"},
         frames=[2, 25, 26, 27, 40], channels="rgba", tolerance=1e-4),
]


# ----------------------------------------------- Known divergences, by case
# The differences the runs found, with the issue that records each one
# (DOCUMENTED: differences that are intentional or come from OFX, e.g.
# black outside the RoD where the reference repeats the edge pixels).
DOCUMENTED = 318
KNOWN = {
    270: [
        "Transform rotate 30", "Transform scale 1.5,0.8",
        "Transform translate, scale, skew, rotate (order)",
        "Transform cubic, scale 0.5 (minification)",
        "CornerPin2D four pins", "CornerPin2D 2 pin(s) enabled",
        "CornerPin2D 3 pin(s) enabled", "Reformat scale 0.5 defaults",
        "Reformat scale 0.5, black outside", "Card3D rotate z 30",
        "Card3D rotate y 20, z 30", "Card3D skew x 20, rotate z 30"] + [
            "Transform filter %s, translate 0.3,0.6" % _f for _f in (
                "Bilinear", "Cubic", "Keys", "Simon", "Rifman", "Mitchell",
                "Parzen", "Notch")],
    273: ["TimeBlur defaults"],
    277: ["Merge %s, alpha outside [0, 1]" % _op
                             for _op in MERGE_ALPHA_OPERATORS],
    279: ["ShuffleCopy defaults"],
    281: ["PIK C-blue on IBK (IBKGizmoV3)"],
    287: ["ColorWheel defaults"],
    288: [
        "Radial area 100,100-600,476 softness 1",
        "Radial softness 0.3, plinear", "Rectangle softness 40"],
    295: ["Premult, alpha outside [0, 1]"],
    296: [
        "Colorspace linear to HSV (RGBToHSV)",
        "Colorspace linear to HSL (RGBToHSL)",
        "Colorspace linear to YCbCr (RGBToYCbCr709)",
        "Colorspace sRGB to YCbCr (RGBToYCbCr709)",
        "Colorspace rec709 to YCbCr (RGBToYCbCr709)",
        "Colorspace linear to HSV, values outside [0, 1]"],
    302: [
        "LensDistortion k 0.1, undistort",
        "LensDistortion k 0.1, undistort (k1 -0.1)",
        "LensDistortion k 0.1, redistort (distort)"],
    352: [
        "IDistort, alpha 0.5, premultiplied",
        "STMap uv = rgb, premultiplied, alpha 0.5",
        "STMap uv = rgb, premultiplied, alpha 0 on the left",
        "IDistort uv = rgb, blur = blue",
        "IDistort uv = rgb, blur = blue, blur scale 4",
        "STMap uv = rgb, blur = blue (1), blur scale 3"],
    305: [
        "ContactSheet 2 inputs, 2x2 cells of 32x32, center off"],
    309: ["Retime speed 0.5, filter none"],
    312: [
        "Blur size 10, impulse (BlurCImg size 10)",
        "Blur size 10, impulse (BlurCImg size x times PAR)",
        "Blur size 4 on the ramp, BlurCImg boundary Nearest"],
    313: [
        "Sharpen minimum -10, maximum 10", "Soften minimum -10, maximum 10"],
    315: [
        "Erode (blur) defaults (ErodeBlur)",
        "Erode (blur) size -3, blur 0.5 (ErodeBlur -3)",
        "Erode (blur) size 3, blur 0.5",
        "FilterErode size 1 box (ErodeSmoothCImg)"],
    319: ["AppendClip defaults",
                                  "AppendClip fade in 3",
                                  "AppendClip dissolve 4"],
    320: ["TimeDissolve in 1, out 10"],
    DOCUMENTED: [
        "Log2Lin lin2log", "ColorWheel gamma 1, whole disc",
        "Ramp defaults", "Transform translate 0.5,0.25 cubic, edges",
        "Reformat to box 20x12 fit, box PAR = PAR",
        "Reformat to box 20x12 fit, reference black outside",
        "Blur size 4 on the ramp (edges)", "Convolve2 3x3 kernel, edges",
        "Radial softness 0"] + [
            "Ramp %s, horizontal" % _t
            for _t in ("linear", "plinear", "smooth0", "smooth1", "smooth")],
}
_BY_NAME = {_c["name"]: _c for _c in CASES}
for _known, _names in KNOWN.items():
    for _name in _names:
        if _name not in _BY_NAME or _BY_NAME[_name]["known"] is not None:
            raise ValueError("KNOWN: no case %r, or it already has a known "
                             "issue" % _name)
        _BY_NAME[_name]["known"] = _known


# The short titles of the issues of known differences (xfail reasons).
ISSUES = {
    266: 'ZDefocus semantics',
    270: 'Transform family: missing sharpening filters, no prefilte...',
    271: 'Color and mask nodes: no fringe, no (un)premult invert, n...',
    272: 'ColorCorrect, ColorMatrix, Saturation, HSVTool: Clamp Bla...',
    273: 'TimeBlur and motion blur: shutter limited to 0-2 frames;...',
    274: 'DirBlur, EdgeExtend, ChromaKeyer, SmoothPatchBased: say w...',
    275: 'Difference: with RGB input the matte overwrites blue',
    276: 'Wrong or misleading hints, descriptions and comments',
    277: 'Merge: alpha masking changes RGB, alpha outside [0, 1] is...',
    278: 'KeyMix: no bounding box choice',
    279: 'Shuffle: one output layer; the ShuffleCopy-style default...',
    280: 'Keyer: the matte is inverted with respect to its A, B, C,...',
    281: 'PIK: luminance match and autolevels are not implemented;...',
    282: 'CopyRectangle: softness ramp shape; whole-image default area',
    283: 'Roto: nothing is drawn by default (Process A off); no pre...',
    284: 'Constant: finite region of definition',
    285: 'CheckerBoard: centerline width default 1, and the four-co...',
    286: 'ColorBars: SMPTE HD bars with an IRE intensity; no PAL op...',
    287: 'ColorWheel: gamma default 0.45; no area control',
    288: 'Radial and Rectangle: softness falloff curve; missing Dra...',
    289: 'ColorCorrect: tonal range weighting, negative values unde...',
    290: 'ColorLookup: master curve combination, and the master cur...',
    291: 'ColorMatrix: no invert option',
    292: 'Saturation: negative saturation cannot be entered',
    293: 'HSVTool: no Set Saturation / Set Brightness, no per-compo...',
    294: 'Invert: no clamp option',
    295: 'Premult and Unpremult: no invert option; Premult does not...',
    296: 'ColorTransform nodes: say that HSV/HSL work on the values...',
    297: 'CImgExpression: x and y are buffer indices at render scale',
    298: 'Crop: softness falloff curve',
    299: 'AdjustRoD: cannot shrink the RoD; default amount 0',
    300: 'Position: no edge-pixel fill',
    301: 'Mirror: flips around the input RoD instead of the format',
    302: 'LensDistortion: the model does not follow its documented...',
    304: 'GodRays: translate and skew combine with the other controls',
    305: 'ContactSheet: placement in partially filled sheets; no Sn...',
    306: 'TimeOffset: no rounding mode',
    307: 'FrameHold: First Frame defaults to 0; no Set to Current F...',
    308: 'FrameRange: frames outside the range are black by default',
    309: 'Retime: output range ignores the speed; no Box filter; No...',
    310: 'FrameBlend: blends 6 frames by default',
    311: 'Anaglyph, MixViews, SideBySide, JoinViews: limited to the...',
    312: "BlurCImg family: the IIR Gaussian's shape (peak and tail)",
    313: 'SharpenCImg, SoftenCImg: no minimum/maximum controls',
    314: 'EdgeDetectCImg: no Prewitt, no threshold, no single alpha...',
    315: 'ErodeCImg/DilateCImg, ErodeBlurCImg, ErodeSmoothCImg, Med...',
    316: 'ConvolveCImg: per-channel kernels by default',
    317: 'Compatibility tests that cannot be automated yet',
    318: 'Document the behaviours that come from OFX: black outside...',
    319: 'AppendClip: first output frame, fade curve, cross-dissolve',
    320: 'TimeDissolve: the dissolve starts before the In frame',
    352: 'IDistort, STMap: Unpremult UV, and the blur near edges and...',
}
