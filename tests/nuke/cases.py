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
- "ofx_times": the times hostDemo renders, one per frame, for a case whose
  "nuke_post" adds a time node (e.g. a Retime) that samples the node at
  those, maybe fractional, times (Nuke's sample() only takes whole frames);
- "major": the plug-in major version hostDemo uses (default: the newest);
- "nuke_warmup": sample the compared pixels of each frame once before the
  compared samples (in Nuke, the first sample of a ColorCorrect with Mix
  Luminance differs, whatever the pixel, and sampling that pixel again
  is not enough);
- "nuke_edges": compare Nuke's pixels outside its bounding box as sample()
  gives them (the edge pixels repeated), not as 0;
- "nuke_par": the pixel aspect ratio of the Nuke formats (default PAR),
  e.g. 1 for a node whose output always has square pixels, so that the
  images have the same pixels on both sides (hostDemo gives all the clips
  the PAR of the project);
- "region": the compared pixels, (x1, y1, x2, y2[, step]) or "project"
  (the PAL project, one pixel in 8x8); default the WIDTH x HEIGHT image;
  generators, "project" regions and "project_format" cases use the PAL
  project format in Nuke, the others WIDTH x HEIGHT;
- "channels": the compared channels, e.g. "rgb" or "rgba";
- "tolerance": the largest accepted absolute difference;
- "relative": the channels (e.g. "g") whose tolerance is relative for
  values larger than 1 in magnitude (e.g. near a pole of the conversion,
  where the float rounding of the inputs is amplified);
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


def _grey_ramp(x, y):
    v = -.5 + 2. * (x + WIDTH * y) / (WIDTH * HEIGHT - 1.)
    return v, v, v, 1.


def _premult(x, y):
    a = y / (HEIGHT - 1.)
    r, g, b, _ = _ramp(x, y)
    return r * a, g * a, b * a, a


def _impulse(x, y):
    if (x, y) == (WIDTH // 2, HEIGHT // 2):
        return 1., 1., 1., 1.
    return 0., 0., 0., 1.


# Pseudo-random values in [0, 1), a different pattern in each channel:
# (a x^2 + b y + c x y) mod m / m, exact in floats on both sides.
_HASH = ((7, 13, 3, 31), (5, 11, 2, 29), (3, 17, 5, 23))


def _hash(x, y):
    return tuple(((a * x * x + b * y + c * x * y) % m) / float(m)
                 for a, b, c, m in _HASH) + (1.,)


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


def _grey_checker(x, y):
    g = .25 + .5 * _checker(x, y)
    return g, g, g, g


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


def _rect(x, y):
    inside = 8 <= x < 24 and 8 <= y < 24
    return (1., 1., 1., 1.) if inside else (0., 0., 0., 0.)


def _rect_beyond(x, y):
    inside = 8 <= x < 40 and 8 <= y < 40
    return (1., 1., 1., 1.) if inside else (0., 0., 0., 0.)


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
    # Pseudo-random values (a difference from the blur that is never 0);
    # opaque.
    "hash": {
        "nuke": tuple("fmod(%d * x * x + %d * y + %d * x * y, %d) / %d"
                      % (a, b, c, m, m) for a, b, c, m in _HASH) + ("1",),
        "py": _hash,
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
    # 4x4 checks of 0.25 and 0.75, the same in R, G, B and A (an edge
    # detector that writes one matte into alpha gives the same edges).
    "greychecker": {
        "nuke": ("0.25 + 0.5 * " + CHECKER,) * 4,
        "py": _grey_checker,
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
    # Greys from -0.5 (bottom left) to 1.5 (top right), row by row; opaque.
    "grey_ramp": {
        "nuke": ("-0.5 + 2 * (x + %d * y) / %d" % (WIDTH, WIDTH * HEIGHT - 1),)
        * 3 + ("1",),
        "py": _grey_ramp,
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
    "screen_green": _const(.2, .8, .1, 1.),
    # The Roto shape of ROTO_RECT, as an OFX Roto input: white inside
    # [8, 24) x [8, 24), transparent black outside.
    "rect": {
        "nuke": ("x >= 8 && x < 24 && y >= 8 && y < 24 ? 1 : 0",) * 4,
        "py": _rect,
    },
}
# The shape of _roto_shape(40, 40), beyond the 32x32 format (Roto Clip To).
IMAGES["rect_beyond"] = {
    "nuke": ("x >= 8 && x < 40 && y >= 8 && y < 40 ? 1 : 0",) * 4,
    "py": _rect_beyond,
    "box": (0, 0, 48, 48),
}
# Aa01 on the bottom left quarter only (Roto Clip To).
IMAGES["Aa01_box"] = dict(IMAGES["Aa01"], box=(0, 0, 16, 16))
# An element on the left of the image (Mirror).
IMAGES["element"] = dict(IMAGES["ramp"], box=(2, 4, 12, 20))


# The inverse of the reference's sRGB and rec709 curves (Colorspace input
# sRGB or rec709), in Python and as a Nuke expression of v.
def _from_srgb(v):
    return v / 12.92 if v < 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def _from_rec709(v):
    return v / 4.5 if v < 0.081 else ((v + 0.099) / 1.099) ** (1 / 0.45)


_FROM_SRGB = ("(%(v)s < 0.04045 ? %(v)s / 12.92"
              " : pow((%(v)s + 0.055) / 1.055, 2.4))")
_FROM_REC709 = ("(%(v)s < 0.081 ? %(v)s / 4.5"
                " : pow((%(v)s + 0.099) / 1.099, 1 / 0.45))")


def _decoded(name, decode, expression):
    """The image `name` with its RGB decoded to linear (alpha unchanged)."""
    image = IMAGES[name]

    def pixel(x, y):
        r, g, b, a = image["py"](x, y)
        return decode(r), decode(g), decode(b), a
    return {"nuke": tuple(expression % {"v": "(%s)" % e}
                          for e in image["nuke"][:3]) + image["nuke"][3:],
            "py": pixel}


# "hues" and "hues01" as linear RGB, whose sRGB (or rec709) encoding is the
# image: with Transfer Function sRGB, the conversion of these matches the
# reference's Colorspace with an sRGB (or rec709) input on the image (#296).
IMAGES["hues_from_srgb"] = _decoded("hues", _from_srgb, _FROM_SRGB)
IMAGES["hues01_from_srgb"] = _decoded("hues01", _from_srgb, _FROM_SRGB)
IMAGES["hues01_from_rec709"] = _decoded("hues01", _from_rec709, _FROM_REC709)



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

# LensDistortion: the knobs and params of a few cases (see the cases).
LENS_K1K2C = {"distortionDenominator0": .1, "distortionDenominator1": .05,
              "centre": [.1, .2]}
OFX_LENS_K1K2C = {"k1": .1, "k2": .05, "center": [.1, .2]}
LENS_ANAMORPHIC = {"lens": "Anamorphic", "anamorphicSqueeze": 2,
                   "distortionDenominator0": .1,
                   "distortionDenominatorX00": .1,
                   "distortionDenominatorY00": .05}
OFX_LENS_ANAMORPHIC = {"k1": .1, "anamorphicSqueeze": 2,
                       "asymmetricDistortion": [.1, .05]}
# version 5: Twist and Scale too (with all the other params of the model)
LENS_TWIST = dict(LENS_ANAMORPHIC, distortionDenominator1=.05,
                  centre=[.1, .2], anamorphicTwist=20,
                  anamorphicScale=[1.2, .9])
OFX_LENS_TWIST = dict(OFX_LENS_ANAMORPHIC, k2=.05, center=[.1, .2],
                      anamorphicTwist=20, anamorphicScale=[1.2, .9])


def lens_map(layer):
    """The nuke_post that copies a LensDistortion2 STMap layer to R and G.

    Args:
        layer: "backward" (the map of Redistort) or "forward" (Undistort).

    Returns:
        The Python lines (a string) that replace `n` with a Copy node.
    """
    return ("copy = nuke.nodes.Copy(inputs=[n, n])\n"
            "copy['from0'].setValue('%s.u')\n"
            "copy['to0'].setValue('rgba.red')\n"
            "copy['from1'].setValue('%s.v')\n"
            "copy['to1'].setValue('rgba.green')\n"
            "n = copy" % (layer, layer))


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

# EdgeDetectCImg on alpha only (R, G and B are kept).
EDGE_ALPHA_ONLY = {"NatronOfxParamProcessR": 0, "NatronOfxParamProcessG": 0,
                   "NatronOfxParamProcessB": 0, "NatronOfxParamProcessA": 1}

# ---------------------------------------------------------------- Color
# Saturation outputs differ by up to 8e-4 here (the Rec. 709 luminance
# coefficients differ in the fourth decimal): tolerance 1e-3.
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
    # Clamp Black is off by default (#272); the obsolete major versions
    # (ColorCorrect 2, ColorMatrix 2, Saturation 2, HSVTool 1) clamp by
    # default, and match with it off.
    case("ColorCorrect defaults (negatives)", ("ColorCorrect", {}),
         (ofx_id("ColorCorrectPlugin"), {}), input="ramp"),
    case("ColorCorrect 2 defaults, clamp black off", ("ColorCorrect", {}),
         (ofx_id("ColorCorrectPlugin"), {"clampBlack": 0}), input="ramp",
         major=2),
    case("ColorCorrect contrast 2", ("ColorCorrect", {"contrast": 2.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(2.)}),
         input="ramp", tolerance=1e-4),
    case("ColorCorrect contrast 0.5, gamma 0.8 (negatives)",
         ("ColorCorrect", {"contrast": .5, "gamma": .8}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(.5),
                                         "MasterGamma": rgba(.8)}),
         input="ramp", tolerance=1e-4),
    # Gamma 0 gives 1 for positive values and keeps the negative ones; a
    # gamma below 0.001 is 0.001 (compared up to 1: beyond, the outputs
    # are infinite in Nuke, FLT_MAX in openfx-misc).
    case("ColorCorrect gamma 0, greys", ("ColorCorrect", {"gamma": 0.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterGamma": rgba(0.)}),
         input="grey_ramp", tolerance=1e-4),
    case("ColorCorrect gamma 1e-6, greys up to 1",
         ("ColorCorrect", {"gamma": 1e-6}),
         (ofx_id("ColorCorrectPlugin"), {"MasterGamma": rgba(1e-6)}),
         input="grey_ramp", region=(0, 0, WIDTH, 24), tolerance=1e-3),
    case("ColorCorrect gamma 0.01, greys up to 1",
         ("ColorCorrect", {"gamma": .01}),
         (ofx_id("ColorCorrectPlugin"), {"MasterGamma": rgba(.01)}),
         input="grey_ramp", region=(0, 0, WIDTH, 24), tolerance=1e-4),
    case("ColorCorrect contrast 2, gamma 0, negative red",
         ("ColorCorrect", {"contrast": 2., "gamma": 0.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(2.),
                                         "MasterGamma": rgba(0.)}),
         input="ramp", region=(0, 0, 8, HEIGHT), channels="r",
         tolerance=1e-4),
    case("ColorCorrect 2 contrast 2, positive values",
         ("ColorCorrect", {"contrast": 2.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterContrast": rgba(2.),
                                         "clampBlack": 0}),
         input="ramp", region=(8, 7, 24, HEIGHT), tolerance=1e-4, major=2),
    # The tone ranges are weighted by the luminance of the input; with
    # colours, the weights differ by up to 0.012 (the Rec. 709 luminance
    # coefficients differ in the fourth decimal).
    case("ColorCorrect shadows gain 2, highlights gain 4",
         ("ColorCorrect", {"shadows.gain": 2., "highlights.gain": 4.}),
         (ofx_id("ColorCorrectPlugin"), {"ShadowsGain": rgba(2.),
                                         "HighlightsGain": rgba(4.)}),
         input="ramp", tolerance=1e-4,
         known=289),
    case("ColorCorrect shadows, midtones and highlights offsets, greys",
         ("ColorCorrect", {"shadows.offset": Script("0.1 0 0 0"),
                           "midtones.offset": Script("0 0.1 0 0"),
                           "highlights.offset": Script("0 0 0.1 0")}),
         (ofx_id("ColorCorrectPlugin"), {
             "ShadowsOffset": [.1, 0., 0., 0.],
             "MidtonesOffset": [0., .1, 0., 0.],
             "HighlightsOffset": [0., 0., .1, 0.]}),
         input="grey_ramp", tolerance=1e-4),
    case("ColorCorrect all master knobs",
         ("ColorCorrect", {"saturation": .5, "contrast": 1.5, "gamma": 1.2,
                           "gain": 1.1, "offset": .05}),
         (ofx_id("ColorCorrectPlugin"), {
             "MasterSaturation": rgba(.5), "MasterContrast": rgba(1.5),
             "MasterGamma": rgba(1.2), "MasterGain": rgba(1.1),
             "MasterOffset": rgba(.05)}),
         input="hues01", tolerance=SATURATION_TOLERANCE),
    case("ColorCorrect all master knobs, values outside [0, 1]",
         ("ColorCorrect", {"saturation": .5, "contrast": 1.5, "gamma": 1.2,
                           "gain": 1.1, "offset": .05}),
         (ofx_id("ColorCorrectPlugin"), {
             "MasterSaturation": rgba(.5), "MasterContrast": rgba(1.5),
             "MasterGamma": rgba(1.2), "MasterGain": rgba(1.1),
             "MasterOffset": rgba(.05)}),
         input="hues", tolerance=SATURATION_TOLERANCE),
    case("ColorCorrect midtones saturation 2, gamma 0.8",
         ("ColorCorrect", {"midtones.saturation": 2., "midtones.gamma": .8}),
         (ofx_id("ColorCorrectPlugin"), {"MidtonesSaturation": rgba(2.),
                                         "MidtonesGamma": rgba(.8)}),
         input="hues", tolerance=1e-4,
         known=289),
    case("ColorCorrect gain 2, mix luminance 1",
         ("ColorCorrect", {"gain": 2., "mix_luminance": 1.}),
         (ofx_id("ColorCorrectPlugin"), {"MasterGain": rgba(2.),
                                         "mixLuminance": 1.}),
         input="hues01", nuke_warmup=True, tolerance=1e-4),
    case("ColorCorrect gain 2, offset -0.1, gamma 1.5, mix luminance 0.7",
         ("ColorCorrect", {"gain": 2., "offset": -.1, "gamma": 1.5,
                           "mix_luminance": .7}),
         (ofx_id("ColorCorrectPlugin"), {
             "MasterGain": rgba(2.), "MasterOffset": rgba(-.1),
             "MasterGamma": rgba(1.5), "mixLuminance": .7}),
         input="hues01", nuke_warmup=True, tolerance=1e-4),
    case("ColorCorrect test, greys", ("ColorCorrect", {"test": True}),
         (ofx_id("ColorCorrectPlugin"), {"test": 1}), input="grey_ramp",
         tolerance=1e-4),
    case("ColorMatrix identity (negatives)",
         ("ColorMatrix", {"matrix": Script("{1 0 0} {0 1 0} {0 0 1}")}),
         (ofx_id("ColorMatrixPlugin"), {}), input="ramp"),
    case("ColorMatrix Rec.709 to XYZ",
         ("ColorMatrix", {"matrix": Script(
             "{0.4124 0.3576 0.1805} {0.2126 0.7152 0.0722}"
             " {0.0193 0.1192 0.9505}")}),
         (ofx_id("ColorMatrixPlugin"), {
             "outputRed": [.4124, .3576, .1805, 0.],
             "outputGreen": [.2126, .7152, .0722, 0.],
             "outputBlue": [.0193, .1192, .9505, 0.]}),
         input="ramp", tolerance=1e-4),
    case("ColorMatrix 2 Rec.709 to XYZ, clamp black off",
         ("ColorMatrix", {"matrix": Script(
             "{0.4124 0.3576 0.1805} {0.2126 0.7152 0.0722}"
             " {0.0193 0.1192 0.9505}")}),
         (ofx_id("ColorMatrixPlugin"), {
             "outputRed": [.4124, .3576, .1805, 0.],
             "outputGreen": [.2126, .7152, .0722, 0.],
             "outputBlue": [.0193, .1192, .9505, 0.], "clampBlack": 0}),
         input="ramp", tolerance=1e-4, major=2),
    case("ColorMatrix invert",
         ("ColorMatrix", {"matrix": Script("{2 0 0} {0 1 0} {0 0 1}"),
                          "invert": True}),
         (ofx_id("ColorMatrixPlugin"), {"outputRed": [2., 0., 0., 0.],
                                        "invert": 1}),
         input="ramp"),
    case("Saturation 0.5", ("Saturation", {"saturation": 0.5}),
         (ofx_id("SaturationPlugin"), {"saturation": 0.5}), input="ramp",
         tolerance=SATURATION_TOLERANCE),
    case("Saturation 2 0.5, clamp black off",
         ("Saturation", {"saturation": 0.5}),
         (ofx_id("SaturationPlugin"), {"saturation": 0.5, "clampBlack": 0}),
         input="ramp", tolerance=SATURATION_TOLERANCE, major=2),
    case("Saturation 2 on (.9, .1, .1)", ("Saturation", {"saturation": 2.}),
         (ofx_id("SaturationPlugin"), {"saturation": 2.}), input="red",
         tolerance=SATURATION_TOLERANCE),
    case("Saturation 2, hues",
         ("Saturation", {"saturation": 2.}),
         (ofx_id("SaturationPlugin"), {"saturation": 2.}),
         input="hues", tolerance=SATURATION_TOLERANCE),
    case("Saturation -1 (negative)", ("Saturation", {"saturation": -1.}),
         (ofx_id("SaturationPlugin"), {"saturation": -1.}),
         input="orange", tolerance=SATURATION_TOLERANCE),
    case("Saturation mode Maximum, 0.3",
         ("Saturation", {"saturation": .3, "mode": "Maximum"}),
         (ofx_id("SaturationPlugin"), {"saturation": .3,
                                       "luminanceMath": "Max"}),
         input="hues", tolerance=1e-4),
    case("HSVTool defaults (negatives)", ("HSVTool", {}),
         (ofx_id("HSVToolPlugin"), {}), input="ramp", channels="rgba"),
    case("HSVTool 1 defaults, clamp black off", ("HSVTool", {}),
         (ofx_id("HSVToolPlugin"), {"clampBlack": 0}), input="hues",
         channels="rgba", tolerance=1e-4, major=1),
    case("HSVTool hue rotation 90, saturation -0.3, brightness 0.2",
         ("HSVTool", {"huerotation": 90., "saturation": -.3,
                      "brightness": .2}),
         (ofx_id("HSVToolPlugin"), {"hueRotation": 90.,
                                    "saturationAdjustment": -.3,
                                    "brightnessAdjustment": .2}),
         input="hues", tolerance=1e-4),
    case("HSVTool force saturation 0.2", ("HSVTool", {
        "setsaturation": True, "saturation": .2}),
         (ofx_id("HSVToolPlugin"), {"setSaturation": 1,
                                    "saturationAdjustment": .2}),
         input="hues", tolerance=1e-4),
    case("HSVTool force brightness 0.5", ("HSVTool", {
        "setbrightness": True, "brightness": .5}),
         (ofx_id("HSVToolPlugin"), {"setBrightness": 1,
                                    "brightnessAdjustment": .5}),
         input="hues", tolerance=1e-4),
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
         (ofx_id("Premult"), {"invert": 1}), input="Aa"),
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
    # RGBToHSV, RGBToHSL, the 709 Y'CbCr and Y'PbPr conversions and their
    # inverses (#296). Transfer Function sRGB gives the reference's
    # Colorspace conventions, from or to linear RGB; with an sRGB (or rec709)
    # Colorspace input, our input is the image decoded to linear
    # (*_from_srgb). HSV and HSL with Transfer Function None (the default)
    # match an sRGB input, on the same image, where the conventions agree.
    case("Colorspace linear to HSV (RGBToHSV sRGB)",
         ("Colorspace", {"colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {"transferFunction": "sRGB"}), input="hues01",
         tolerance=1e-4),
    case("Colorspace linear to HSL (RGBToHSL sRGB)",
         ("Colorspace", {"colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {"transferFunction": "sRGB"}), input="hues01",
         tolerance=1e-4),
    case("Colorspace linear to YCbCr (RGBToYCbCr709 sRGB)",
         ("Colorspace", {"colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {"transferFunction": "sRGB"}),
         input="hues01", tolerance=1e-4),
    case("Colorspace linear to YPbPr (RGBToYPbPr709 sRGB)",
         ("Colorspace", {"colorspace_out": "YPbPr"}),
         (ofx_id("RGBToYPbPr709"), {"transferFunction": "sRGB"}),
         input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to HSV (RGBToHSV)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to HSL (RGBToHSL)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace sRGB to YCbCr (RGBToYCbCr709 sRGB)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {"transferFunction": "sRGB"}),
         nuke_inputs=["hues01"], clips={"Source": "hues01_from_srgb"},
         tolerance=1e-4),
    case("Colorspace sRGB to YPbPr (RGBToYPbPr709 sRGB)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "YPbPr"}),
         (ofx_id("RGBToYPbPr709"), {"transferFunction": "sRGB"}),
         nuke_inputs=["hues01"], clips={"Source": "hues01_from_srgb"},
         tolerance=1e-4),
    case("Colorspace rec709 to YCbCr (RGBToYCbCr709 sRGB)",
         ("Colorspace", {"colorspace_in": "rec709",
                         "colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {"transferFunction": "sRGB"}),
         nuke_inputs=["hues01"], clips={"Source": "hues01_from_rec709"},
         tolerance=1e-4),
    # values outside [0, 1], negative ones included
    case("Colorspace linear to HSV, values outside [0, 1]",
         ("Colorspace", {"colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {"transferFunction": "sRGB"}), input="hues",
         tolerance=1e-4),
    # S (green) goes up to about 150 near L = 1 (the pole of (max - min) /
    # (2 - max - min)): relative tolerance for S
    case("Colorspace linear to HSL, values outside [0, 1]",
         ("Colorspace", {"colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {"transferFunction": "sRGB"}), input="hues",
         tolerance=1e-4, relative="g"),
    case("Colorspace linear to YCbCr, values outside [0, 1]",
         ("Colorspace", {"colorspace_out": "YCbCr"}),
         (ofx_id("RGBToYCbCr709"), {"transferFunction": "sRGB"}),
         input="hues", tolerance=1e-4),
    case("Colorspace sRGB to HSV, values outside [0, 1] (RGBToHSV)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSV"}),
         (ofx_id("RGBToHSV"), {}), input="hues", tolerance=1e-4),
    # Transfer Function None takes the saturation of colours outside [0, 1]
    # from the colour clamped to [0, 1] (#399): sRGB, on the decoded image
    case("Colorspace sRGB to HSL, values outside [0, 1] (RGBToHSL sRGB)",
         ("Colorspace", {"colorspace_in": "sRGB", "colorspace_out": "HSL"}),
         (ofx_id("RGBToHSL"), {"transferFunction": "sRGB"}),
         nuke_inputs=["hues"], clips={"Source": "hues_from_srgb"},
         tolerance=1e-4, relative="g"),
    case("Colorspace HSV to linear (HSVToRGB sRGB)",
         ("Colorspace", {"colorspace_in": "HSV"}),
         (ofx_id("HSVToRGB"), {"transferFunction": "sRGB"}), input="hues01",
         tolerance=1e-4),
    case("Colorspace HSL to linear (HSLToRGB sRGB)",
         ("Colorspace", {"colorspace_in": "HSL"}),
         (ofx_id("HSLToRGB"), {"transferFunction": "sRGB"}), input="hues01",
         tolerance=1e-4),
    case("Colorspace YCbCr to linear (YCbCrToRGB709 sRGB)",
         ("Colorspace", {"colorspace_in": "YCbCr"}),
         (ofx_id("YCbCrToRGB709"), {"transferFunction": "sRGB"}),
         input="hues01", tolerance=1e-4),
    case("Colorspace YPbPr to linear (YPbPrToRGB709 sRGB)",
         ("Colorspace", {"colorspace_in": "YPbPr"}),
         (ofx_id("YPbPrToRGB709"), {"transferFunction": "sRGB"}),
         input="hues01", tolerance=1e-4),
    case("Colorspace HSV to sRGB (HSVToRGB)",
         ("Colorspace", {"colorspace_in": "HSV", "colorspace_out": "sRGB"}),
         (ofx_id("HSVToRGB"), {}), input="hues01", tolerance=1e-4),
    case("Colorspace HSL to sRGB (HSLToRGB)",
         ("Colorspace", {"colorspace_in": "HSL", "colorspace_out": "sRGB"}),
         (ofx_id("HSLToRGB"), {}), input="hues01", tolerance=1e-4),
    # the Mask input and Mix (#296)
    case("Colorspace sRGB to HSV on a mask, mix 0.5 (RGBToHSV)",
         ("Colorspace", dict(MASKED, colorspace_in="sRGB",
                             colorspace_out="HSV", mix=.5)),
         (ofx_id("RGBToHSV"), {"mix": .5}),
         nuke_inputs=["hues01", "mask"],
         clips={"Source": "hues01", "Mask": "mask"}, tolerance=1e-4),
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
    # Outside the element, Nuke's Expression fills the whole format while
    # CImgExpression's RoD is its Source's; compare the element only.
    case("Expression x/32 on an element not at 0,0 (CImgExpression X/32)",
         ("Expression", {"expr0": "x/32", "expr1": "y/32", "expr2": "0"}),
         ("net.sf.cimg.CImgExpression",
          {"expression": "c==0?X/32:c==1?Y/32:0"}),
         input="element", region=(2, 4, 12, 20)),
]

# ---------------------------------------------------------------- Merge
MERGE_OPERATORS = [
    "atop", "average", "color-burn", "color-dodge", "conjoint-over", "copy",
    "difference", "disjoint-over", "divide", "exclusion", "from",
    "geometric", "hard-light", "hypot", "in", "mask", "matte", "max", "min",
    "minus", "multiply", "out", "over", "overlay", "plus", "screen",
    "soft-light", "stencil", "under", "xor"]
# Operators that use alpha: compared with alpha outside [0, 1] too.
MERGE_ALPHA_OPERATORS = [
    "atop", "conjoint-over", "disjoint-over", "in", "mask", "matte", "out",
    "over", "stencil", "under", "xor"]
for _op in MERGE_OPERATORS:
    CASES.append(case(
        "Merge %s, opaque" % _op, ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["B", "A"], clips={"Source": "B", "A": "A"},
        channels="rgba", tolerance=1e-4))
    CASES.append(case(
        "Merge %s, alpha" % _op, ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["Ba01", "Aa01"], clips={"Source": "Ba01", "A": "Aa01"},
        channels="rgba", tolerance=1e-4))
for _op in MERGE_ALPHA_OPERATORS:
    CASES.append(case(
        "Merge %s, alpha outside [0, 1]" % _op,
        ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "A": "Aa"},
        channels="rgba", tolerance=1e-4))
# Operators for which Alpha masking applies (MergeImages2D::isMaskable), and over (where it does not).
MERGE_MASKABLE_OPERATORS = [
    "average", "color-burn", "color-dodge", "difference", "divide",
    "exclusion", "from", "geometric", "hard-light", "hypot", "max", "min",
    "minus", "multiply", "overlay", "plus", "soft-light"]
# The other operators, with alpha outside [0, 1] (the alpha is processed as a color).
for _op in MERGE_OPERATORS:
    if _op in MERGE_ALPHA_OPERATORS:
        continue
    CASES.append(case(
        "Merge %s, alpha outside [0, 1]" % _op,
        ("Merge2", {"operation": _op}),
        (ofx_id("MergePlugin"), {"operation": _op}),
        nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "A": "Aa"},
        channels="rgba", tolerance=1e-4))
for _op in MERGE_MASKABLE_OPERATORS:
    CASES.append(case(
        "Merge %s, alpha masking, alpha outside [0, 1]" % _op,
        ("Merge2", {"operation": _op, "screen_alpha": True}),
        (ofx_id("MergePlugin"), {"operation": _op, "screenAlpha": 1}),
        nuke_inputs=["Ba", "Aa"], clips={"Source": "Ba", "A": "Aa"},
        channels="rgba", tolerance=1e-4))
for _op in MERGE_MASKABLE_OPERATORS + ["over"]:
    CASES.append(case(
        "Merge %s, alpha masking" % _op,
        ("Merge2", {"operation": _op, "screen_alpha": True}),
        (ofx_id("MergePlugin"), {"operation": _op, "screenAlpha": 1}),
        nuke_inputs=["Ba01", "Aa01"], clips={"Source": "Ba01", "A": "Aa01"},
        channels="rgba", tolerance=1e-4))
# A disconnected (#328): the checked output channels are B's where B's
# checkbox is on, else 0; the unchecked output channels are B's; Mask and
# Mix apply.
_B_NONE = {"BChannelsR": 0, "BChannelsG": 0, "BChannelsB": 0, "BChannelsA": 0}
_OUT_ALPHA = {"OutputChannelsR": 0, "OutputChannelsG": 0, "OutputChannelsB": 0}
for _major in (None, 2, 1):
    _version = "" if _major is None else " %d" % _major
    _kw = {} if _major is None else {"major": _major}
    CASES += [
        case("Merge%s over, A disconnected, B channels none" % _version,
             ("Merge2", {"Bchannels": "none"}),
             (ofx_id("MergePlugin"), dict(_B_NONE)),
             nuke_inputs=["Ba01"], clips={"Source": "Ba01"},
             channels="rgba", **_kw),
        case("Merge%s over, A disconnected, B channels rgb" % _version,
             ("Merge2", {"Bchannels": "rgb"}),
             (ofx_id("MergePlugin"), {"BChannelsA": 0}),
             nuke_inputs=["Ba01"], clips={"Source": "Ba01"},
             channels="rgba", **_kw),
        case("Merge%s over, A disconnected, output alpha, B channels none"
             % _version,
             ("Merge2", {"Bchannels": "none", "output": "alpha"}),
             (ofx_id("MergePlugin"), dict(_B_NONE, **_OUT_ALPHA)),
             nuke_inputs=["Ba01"], clips={"Source": "Ba01"},
             channels="rgba", **_kw),
        case("Merge%s over, A disconnected, B channels rgb, mask and mix 0.5"
             % _version,
             ("Merge2", dict(MASKED, Bchannels="rgb", mix=.5)),
             (ofx_id("MergePlugin"), {"BChannelsA": 0, "mix": .5}),
             nuke_inputs=["Ba01", None, "mask"],
             clips={"Source": "Ba01", "Mask": "mask"},
             channels="rgba", tolerance=1e-4, **_kw),
    ]
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
# The default A, B, C, D = 0, 1, 1, 1 (setting the key color changes them).
KEYER_DEFAULT_ABCD = {"softnessLower": -1., "toleranceLower": 0.,
                      "center": 1., "toleranceUpper": 0., "softnessUpper": 0.}
# Keyer's luminance (Rec. 709) differs from the reference by up to 3e-3 on
# non-grey colours: tolerance 5e-3.
KEYER_LUMINANCE_TOLERANCE = 5e-3
CASES += [
    # Nuke's knob default Keyer.keyer luminance (knobdefaults.py; keyer is
    # an obsolete knob that sets operation) makes it the luminance key. The
    # harness applies it too; it is pinned so that the case states it.
    case("Keyer defaults", ("Keyer", {"operation": "luminance key"}),
         (ofx_id("KeyerPlugin"), {}), input="ramp", channels="rgba",
         tolerance=KEYER_LUMINANCE_TOLERANCE),
    case("Keyer luminance key, same A, B, C, D (polarity)",
         ("Keyer", {"operation": "luminance key",
                    "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), {**KEYER_ABCD, "show": "Unpremultiplied"}),
         input="grey3", channels="a", tolerance=1e-4),
    case("Keyer luminance key, inverted",
         ("Keyer", {"operation": "luminance key", "invert": True,
                    "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), {**KEYER_ABCD, "show": "Unpremultiplied",
                                  "invert": 1}),
         input="ramp", channels="a", tolerance=KEYER_LUMINANCE_TOLERANCE),
    # Version 1 has the opposite polarity: its Invert gives the reference.
    case("Keyer version 1, inverted, luminance key",
         ("Keyer", {"operation": "luminance key",
                    "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), {**KEYER_ABCD, "show": "Unpremultiplied",
                                  "invert": 1}),
         input="ramp", channels="a", tolerance=KEYER_LUMINANCE_TOLERANCE,
         major=1),
    case("Keyer saturation key", ("Keyer", {"operation": "saturation key"}),
         (ofx_id("KeyerPlugin"), {"mode": "Saturation"}), input="ramp",
         channels="a", tolerance=1e-5),
    # The mode first: params are set in order.
    case("Keyer min keyer, range 0.2, 0.4, 0.6, 0.8",
         ("Keyer", {"operation": "min keyer", "range": [.2, .4, .6, .8]}),
         (ofx_id("KeyerPlugin"), {"mode": "Min", **KEYER_ABCD}),
         input="hues01", channels="a", tolerance=1e-5),
    case("Keyer max keyer", ("Keyer", {"operation": "max keyer"}),
         (ofx_id("KeyerPlugin"), {"mode": "Max"}), input="ramp",
         channels="a", tolerance=1e-5),
    # Setting the key color makes it transparent (alpha 1 - r here, while
    # the reference's red keyer gives r): A, B, C, D back to the defaults.
    case("Keyer red keyer (Color, key color red)",
         ("Keyer", {"operation": "red keyer"}),
         (ofx_id("KeyerPlugin"), {"mode": "Color", "keyColor": [1., 0., 0.],
                                  **KEYER_DEFAULT_ABCD}),
         input="ramp", channels="a", tolerance=1e-5),
    case("Keyer greenscreen (Screen, key color green)",
         ("Keyer", {"operation": "greenscreen"}),
         (ofx_id("KeyerPlugin"), {"mode": "Screen", "keyColor": [0., 1., 0.]}),
         input="hues01", channels="a", tolerance=1e-3, known=280),
]

# ------------------------------------------------------------------- PIK
# The IBKGizmoV3 gizmo, with its inputs fg and c; PIK's Fg is Source.
PIK_INPUTS = {"nuke_inputs": ["hues01", "screen"],
              "clips": {"Source": "hues01", "C": "screen"},
              "channels": "rgba", "tolerance": 1e-3}
PIK_GREEN_INPUTS = {**PIK_INPUTS, "nuke_inputs": ["hues01", "screen_green"],
                    "clips": {"Source": "hues01", "C": "screen_green"}}
PIK_BLUE = {"screenType": "C-Blue", "show": "Premultiplied"}
PIK_GREEN = {"screenType": "C-Green", "show": "Premultiplied"}
CASES += [
    case("PIK C-blue on IBK (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue"}), (ofx_id("PIK"), PIK_BLUE),
         **PIK_INPUTS),
    # RGBA Legal is on in the gizmo's IBK node, without a knob of its own.
    case("PIK C-blue, RGBA legal off (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue"}),
         (ofx_id("PIK"), {**PIK_BLUE, "rgbal": 0}),
         nuke_post="n.node('IBK1')['rgbal'].setValue(False)", **PIK_INPUTS),
    case("PIK C-green (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-green"}), (ofx_id("PIK"), PIK_GREEN),
         **PIK_GREEN_INPUTS),
    case("PIK C-blue, no screen subtraction (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "ss": False}),
         (ofx_id("PIK"), {**PIK_BLUE, "ss": 0}), **PIK_INPUTS),
    case("PIK C-blue, luminance match (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "lm_enable": True}),
         (ofx_id("PIK"), {**PIK_BLUE, "lmEnable": 1}), **PIK_INPUTS),
    case("PIK C-blue, luminance match, screen range 0.5 (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "lm_enable": True, "level": .5}),
         (ofx_id("PIK"), {**PIK_BLUE, "lmEnable": 1, "level": .5}),
         **PIK_INPUTS),
    case("PIK C-blue, luminance level 0.5 (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "lm_enable": True, "luma": .5,
                         "ll_enable": True}),
         (ofx_id("PIK"), {**PIK_BLUE, "lmEnable": 1, "luma": .5,
                          "llEnable": 1}),
         known=281, **PIK_INPUTS),
    case("PIK C-blue, autolevels (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "autolevels": True}),
         (ofx_id("PIK"), {**PIK_BLUE, "autolevels": 1}), **PIK_INPUTS),
    case("PIK C-blue, autolevels, cyan and magenta (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "autolevels": True, "cyan": True,
                         "magenta": True}),
         (ofx_id("PIK"), {**PIK_BLUE, "autolevels": 1, "cyan": 1,
                          "magenta": 1}), **PIK_INPUTS),
    case("PIK C-green, autolevels (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-green", "autolevels": True}),
         (ofx_id("PIK"), {**PIK_GREEN, "autolevels": 1}),
         **PIK_GREEN_INPUTS),
    case("PIK C-green, autolevels, yellow and cyan (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-green", "autolevels": True,
                         "yellow": True, "cyan": True}),
         (ofx_id("PIK"), {**PIK_GREEN, "autolevels": 1, "yellow": 1,
                          "cyan": 1}), **PIK_GREEN_INPUTS),
    case("PIK C-blue, luminance match and autolevels (IBKGizmoV3)",
         ("IBKGizmoV3", {"st": "C-blue", "lm_enable": True, "level": .7,
                         "autolevels": True}),
         (ofx_id("PIK"), {**PIK_BLUE, "lmEnable": 1, "level": .7,
                          "autolevels": 1}), **PIK_INPUTS),
]

# ------------------------------------------------------------------ Roto
# A Roto shape on [8, 24) x [8, 24) (exact pixel coverage); the OFX Roto
# input is the "rect" image. Nuke's knob default output is alpha
# (knobdefaults.py; the built-in default is rgba): the harness applies it
# too, and it is pinned so that the cases state it. Nuke's Clip To default
# is format; Roto's is No Clip (Natron's RotoPaint uses it internally):
# Format is set explicitly.
def _roto_shape(x2, y2):
    return "\n".join([
        "import nuke.rotopaint as rp",
        "shape = rp.Shape(n['curves'])",
        "for px, py in ((8, 8), (%d, 8), (%d, %d), (8, %d)):" % (x2, x2, y2,
                                                                  y2),
        "    shape.append([px, py])",
        "n['curves'].rootLayer.append(shape)"])


ROTO = {"nuke_inputs": ["Aa01"], "clips": {"Source": "Aa01", "Roto": "rect"},
        "nuke_post": _roto_shape(24, 24), "channels": "rgba"}
ROTO_BOX = {**ROTO, "nuke_inputs": ["Aa01_box"],
            "clips": {"Source": "Aa01_box", "Roto": "rect"}}
# A shape beyond the 32x32 format, up to (40, 40), compared up to 48.
ROTO_BEYOND = {**ROTO, "clips": {"Source": "Aa01", "Roto": "rect_beyond"},
               "nuke_post": _roto_shape(40, 40), "region": (0, 0, 48, 48)}
ROTO_FORMAT = {"clipType": "Format"}
CASES += [
    case("Roto defaults, clip to format", ("Roto", {"output": "alpha"}),
         (ofx_id("RotoPlugin"), ROTO_FORMAT), **ROTO),
    case("Roto output rgba", ("Roto", {"output": "rgba"}),
         (ofx_id("RotoPlugin"), {**ROTO_FORMAT, "NatronOfxParamProcessR": 1,
                                 "NatronOfxParamProcessG": 1,
                                 "NatronOfxParamProcessB": 1}), **ROTO),
    case("Roto premultiply rgb",
         ("Roto", {"output": "alpha", "premultiply": "rgb"}),
         (ofx_id("RotoPlugin"), {**ROTO_FORMAT, "premultiply": 1}), **ROTO),
    case("Roto replace", ("Roto", {"output": "alpha", "replace": True}),
         (ofx_id("RotoPlugin"), {**ROTO_FORMAT, "replace": 1}), **ROTO),
    case("Roto premultiply rgb and replace",
         ("Roto", {"output": "alpha", "premultiply": "rgb",
                   "replace": True}),
         (ofx_id("RotoPlugin"), {**ROTO_FORMAT, "premultiply": 1,
                                 "replace": 1}), **ROTO),
    # Format and No Clip differ beyond the format.
    case("Roto clip to format, shape beyond the format",
         ("Roto", {"output": "alpha"}),
         (ofx_id("RotoPlugin"), ROTO_FORMAT), **ROTO_BEYOND),
    case("Roto clip to bbox, Source in a box",
         ("Roto", {"output": "alpha", "cliptype": "bbox"}),
         (ofx_id("RotoPlugin"), {"clipType": "BBox"}), **ROTO_BOX),
    case("Roto clip to intersect bbox+format, Source in a box",
         ("Roto", {"output": "alpha", "cliptype": "intersect bbox+format"}),
         (ofx_id("RotoPlugin"), {"clipType": "Intersect BBox+Format"}),
         **ROTO_BOX),
    case("Roto no clip, Source in a box",
         ("Roto", {"output": "alpha", "cliptype": "none"}),
         (ofx_id("RotoPlugin"), {"clipType": "No Clip"}), **ROTO_BOX),
]

# ------------------------------------------------------------ Generators
# Compared over the PAL project, one pixel in 8x8. Nuke positions are in
# pixels, OFX ones canonical.
GEN = {"region": "project", "channels": "rgba"}
CASES += [
    # Nuke's channels rgb leaves alpha out, and Nuke treats it as 0. Constant
    # keeps RGBA output with default alpha 0, which gives the same result
    # downstream in every host, also in those that fill alpha 1 when they
    # convert RGB images to RGBA (Natron).
    case("Constant defaults", ("Constant", {}),
         (ofx_id("ConstantPlugin"), {}), **GEN),
    # With the default channels, Nuke ignores the colour's alpha, while the
    # RGBA output has it: compare rgb only. The next case sets our alpha to 0
    # and compares rgba.
    case("Constant (0.25, -0.5, 1.5, 0.75), default channels",
         ("Constant", {"color": [.25, -.5, 1.5, .75]}),
         (ofx_id("ConstantPlugin"), {"color": [.25, -.5, 1.5, .75]}),
         **dict(GEN, channels="rgb")),
    # Nuke drops the colour's 0.75 alpha; our colour with alpha 0 matches it.
    case("Constant (0.25, -0.5, 1.5, 0.75), default channels, our alpha 0",
         ("Constant", {"color": [.25, -.5, 1.5, .75]}),
         (ofx_id("ConstantPlugin"), {"color": [.25, -.5, 1.5, 0.]}), **GEN),
    case("Constant rgba (0.25, -0.5, 1.5, 0.75)",
         ("Constant", {"channels": "rgba", "color": [.25, -.5, 1.5, .75]}),
         (ofx_id("ConstantPlugin"), {"color": [.25, -.5, 1.5, .75]}), **GEN),
    # With channels alpha, the colour knob has one value, the alpha.
    case("Constant alpha 0.75",
         ("Constant", {"channels": "alpha", "color": .75}),
         (ofx_id("ConstantPlugin"), {"outputComponents": "Alpha",
                                     "color": [.25, -.5, 1.5, .75]}), **GEN),
    case("ColorBars defaults", ("ColorBars", {}), (ofx_id("ColorBars"), {}),
         **GEN),
    case("ColorBars PAL", ("ColorBars", {"PAL": True}),
         (ofx_id("ColorBars"), {"PAL": 1}), **GEN),
    case("ColorBars intensity 1", ("ColorBars", {"barintensity": 1.}),
         (ofx_id("ColorBars"), {"barIntensity": 1.}), **GEN),
    case("ColorBars PAL, intensity 0.75",
         ("ColorBars", {"PAL": True, "barintensity": .75}),
         (ofx_id("ColorBars"), {"PAL": 1, "barIntensity": .75}), **GEN),
    # Inside the disc (its anti-aliased edge differs, see the whole disc
    # cases). With the knob defaults, both gammas are 0.45.
    case("ColorWheel defaults", ("ColorWheel", {}),
         (ofx_id("ColorWheel"), {}),
         **dict(GEN, region=(232, 160, 488, 416, 8), tolerance=5e-3)),
    # Inside the disc too.
    case("ColorWheel gamma 1", ("ColorWheel", {"gamma": 1.}),
         (ofx_id("ColorWheel"), {"gamma": 1.}),
         **dict(GEN, region=(232, 160, 488, 416, 8), tolerance=5e-3)),
    # Inside the disc too.
    case("ColorWheel gamma 0.45", ("ColorWheel", {"gamma": .45}),
         (ofx_id("ColorWheel"), {"gamma": .45}),
         **dict(GEN, region=(232, 160, 488, 416, 8), tolerance=5e-3)),
    case("ColorWheel defaults, whole disc", ("ColorWheel", {}),
         (ofx_id("ColorWheel"), {}), **dict(GEN, tolerance=5e-3)),
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
        case("CheckerBoard defaults, %s" % _where, ("CheckerBoard2", {}),
             (ofx_id("CheckerBoardPlugin"), {}), region=_region,
             channels="rgba"),
        case("CheckerBoard centerline 1, %s" % _where,
             ("CheckerBoard2", {"centerlinewidth": 1}),
             (ofx_id("CheckerBoardPlugin"), {"centerlineWidth": 1}),
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
             region=_region, channels="rgba"),
    ]
    CASES += [
        # The boxes (45x48 pixels) tile each half of the frame an even
        # number of times, so a grid from the bottom-left corner is the same
        # as from the centre.
        case("CheckerBoard box 48, four colours, grid origin %s, %s"
             % (_origin_name, _where),
             ("CheckerBoard2", {"boxsize": 48, "centerlinewidth": 0,
                                "color0": [1, 0, 0, 1], "color1": [0, 1, 0, 1],
                                "color2": [0, 0, 1, 1],
                                "color3": [1, 1, 0, 1]}),
             (ofx_id("CheckerBoardPlugin"), {
                 "boxSize": [48, 48], "centerlineWidth": 0,
                 "gridOrigin": _origin,
                 "color0": [1, 0, 0, 1], "color1": [0, 1, 0, 1],
                 "color2": [0, 0, 1, 1], "color3": [1, 1, 0, 1]}),
             region=_region, channels="rgba")
        for _origin, _origin_name in ((0, "centre"), (1, "bottom left"))
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
# Radial and Rectangle 3 (#288): Smooth, the default Falloff, is the curve of the reference (the cases above); version 2
# (obsolete) is compared too, and a tiny Softness.
CASES += [
    case("Radial 2 area 100,100-600,476 softness 1",
         ("Radial", {"area": [100, 100, 600, 476]}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376]}),
         major=2, **dict(GEN, tolerance=1e-4)),
    case("Radial 2 softness 0.3, plinear",
         ("Radial", {"area": [100, 100, 600, 476], "softness": .3,
                     "plinear": True}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376], "softness": .3,
                             "plinear": 1}),
         major=2, **dict(GEN, tolerance=1e-4)),
    case("Radial softness 0.001",
         ("Radial", {"area": [100, 100, 600, 476], "softness": .001}),
         (ofx_id("Radial"), {"bottomLeft": [cx(100), 100],
                             "size": [cx(500), 376], "softness": .001}),
         **dict(GEN, tolerance=1e-4)),
    case("Rectangle 2 softness 40",
         ("Rectangle", {"area": [100, 100, 600, 476], "softness": 40}),
         (ofx_id("Rectangle"), {"bottomLeft": [cx(100), 100],
                                "size": [cx(500), 376], "softness": 40}),
         major=2, **dict(GEN, tolerance=1e-4)),
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
# The sinc-based filters (Lanczos4, Lanczos6, Sinc4), as the filters above.
for _filter in ("Lanczos4", "Lanczos6", "Sinc4"):
    CASES.append(case(
        "Transform filter %s, translate 0.3,0.6" % _filter,
        ("Transform", {"translate": [.3, .6], "filter": _filter}),
        (TRANSFORM, {"translate": [cx(.3), .6], "filter": _filter}),
        input="checker", region=INNER, channels="rgba", tolerance=1e-3))
# Minification: Prefilter Downscaling (on by default from version 2, #270)
# widens the kernel by the scale-down factor (with it, a scale by 0.5 with
# Cubic or Mitchell matches); version 1 (off by default) samples the kernel
# at its unscaled size.
_MINIFY = dict(input="checker", region=(9, 9, 23, 23), channels="rgba",
               tolerance=1e-3)
for _name, _nuke, _ofx in (
        ("scale 0.4,0.7", {"scale": [.4, .7]}, {"scale": [.4, .7]}),
        ("scale 1.5,0.8", {"scale": [1.5, .8]}, {"scale": [1.5, .8]}),
        ("Lanczos4 scale 0.5", {"scale": .5, "filter": "Lanczos4"},
         {"scale": [.5, .5], "filter": "Lanczos4"}),
        ("Mitchell scale 0.5", {"scale": .5, "filter": "Mitchell"},
         {"scale": [.5, .5], "filter": "Mitchell"})):
    CASES.append(case(
        "Transform %s, checker (minification)" % _name,
        ("Transform", dict(_nuke, center=CENTRE["nuke"])),
        (TRANSFORM, dict(_ofx, center=CENTRE["ofx"])), **_MINIFY))
CASES.append(case(
    "Transform 1 cubic, scale 0.5 (minification, no prefilter)",
    ("Transform", {"scale": .5, "center": CENTRE["nuke"]}),
    (TRANSFORM, {"scale": [.5, .5], "center": CENTRE["ofx"]}), major=1,
    **_MINIFY))
# Shear: Prefilter Downscaling widens the kernel (see KNOWN).
CASES.append(case(
    "Transform skewX 1, checker",
    ("Transform", {"skewX": 1, "center": CENTRE["nuke"]}),
    (TRANSFORM, {"skewX": 1, "center": CENTRE["ofx"]}), **_MINIFY))
CASES += [
    case("Reformat 2 scale 0.5 (no prefilter)",
         ("Reformat", {"type": "scale", "scale": .5}),
         (ofx_id("Reformat"), {"reformatType": "Scale",
                               "reformatScale": [.5, .5]}),
         input="checker", channels="rgba", tolerance=1e-3, major=2),
    # a perspective that shrinks the image
    case("CornerPin2D shrink, checker",
         ("CornerPin2D", {"from1": [0, 0], "from2": [32, 0],
                          "from3": [32, 32], "from4": [0, 32],
                          "to1": [8, 9], "to2": [22, 8], "to3": [24, 23],
                          "to4": [9, 21]}),
         (ofx_id("CornerPinPlugin"), {
             "from1": [0, 0], "from2": [cx(32), 0], "from3": [cx(32), 32],
             "from4": [0, 32], "to1": [cx(8), 9], "to2": [cx(22), 8],
             "to3": [cx(24), 23], "to4": [cx(9), 21]}),
         input="checker", region=(10, 10, 21, 20), channels="rgba",
         tolerance=1e-3),
    case("Card3D scale 0.5",
         ("Card3D", {"scaling": [.5, .5, 1]}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32],
                             "cardScaling": [.5, .5, 1]}),
         input="checker", region=(9, 9, 23, 23), channels="rgba",
         tolerance=1e-3),
    case("Card3D 1 scale 0.5 (no prefilter)",
         ("Card3D", {"scaling": [.5, .5, 1]}),
         (ofx_id("Card3D"), {"format": "Size", "size": [cx(32), 32],
                             "cardScaling": [.5, .5, 1]}),
         input="checker", region=(9, 9, 23, 23), channels="rgba",
         tolerance=1e-3, major=1),
]
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
    case("AdjBBox -16 (AdjustRoD -16)", ("AdjBBox", {"numpixels": -16}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [-16, -16]}),
         input="ramp", channels="rgba", nuke_edges=False),
    case("AdjBBox -5.6 (AdjustRoD -5.6)", ("AdjBBox", {"numpixels": -5.6}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [-5.6, -5.6]}),
         input="ramp", channels="rgba", nuke_edges=False),
    # Both amounts are in square pixels: 16 / (768 / 720) = 15 pixels in
    # x; the renders differ by one column there, which x < 26 avoids.
    case("AdjBBox 16, AdjustRoD 16 (both divided by the PAR in x)",
         ("AdjBBox", {"numpixels": 16}),
         (ofx_id("AdjustRoDPlugin"), {"addPixels": [16, 16]}),
         input="element", channels="rgba", region=(0, 0, 26, 32)),
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
    # Mirror Around Format (#301) is not compared: hostDemo gives a file input (the element) the format of its
    # box, so that the format and the RoD are the same.
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
    # LensDistortion, Nuke model: K1 and K2 are the first and second
    # radial denominator coefficients of the reference's NukeX Classic model
    # (distortionDenominator0, 1), Center its centre, Squeeze and Asymmetric
    # its anamorphic squeeze and X00, Y00 denominator coefficients (with the
    # Anamorphic lens type). Undistort is the reference's Mode when it is
    # created from the menu (nuke.nodes gives STMap, so the cases set it).
    # Redistort: tolerance 2e-3, as STMap (the reference rounds the sub-pixel
    # sample positions).
    case("LensDistortion k1 0.1 (default: undistort)",
         ("LensDistortion2", {"output": "Undistort",
                              "distortionDenominator0": .1}),
         (ofx_id("LensDistortion"), {"k1": .1}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("LensDistortion k1 -0.1, undistort",
         ("LensDistortion2", {"output": "Undistort",
                              "distortionDenominator0": -.1}),
         (ofx_id("LensDistortion"), {"k1": -.1, "direction": "Undistort"}),
         input="ramp", region=INNER, channels="rgba", tolerance=1e-3),
    case("LensDistortion k1 0.1, redistort (distort)",
         ("LensDistortion2", {"output": "Redistort",
                              "distortionDenominator0": .1}),
         (ofx_id("LensDistortion"), {"k1": .1, "direction": "Distort"}),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    # version 4 (obsolete): Direction defaults to Distort
    case("LensDistortion 4 k1 0.1 (default: distort)",
         ("LensDistortion2", {"output": "Redistort",
                              "distortionDenominator0": .1}),
         (ofx_id("LensDistortion"), {"k1": .1}), major=4,
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    case("LensDistortion k1 0.1, k2 0.05, center 0.1,0.2, redistort",
         ("LensDistortion2", dict(LENS_K1K2C, output="Redistort")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_K1K2C,
                                         direction="Distort")),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    case("LensDistortion squeeze 2, asymmetric 0.1,0.05, redistort",
         ("LensDistortion2", dict(LENS_ANAMORPHIC, output="Redistort")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_ANAMORPHIC,
                                         direction="Distort")),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    # The STMap output mode against the reference's backward (Distort) and
    # forward (Undistort) maps, copied to red and green: the model itself,
    # without the filter. The reference's forward map is not the exact
    # inverse of its backward map (see KNOWN).
    case("LensDistortion STMap distort, k1 0.1, k2 0.05, center 0.1,0.2",
         ("LensDistortion2", dict(LENS_K1K2C, output="STMap")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_K1K2C, direction="Distort",
                                         outputMode="STMap")),
         nuke_post=lens_map("backward"),
         input="ramp", region=INNER, channels="rg", tolerance=1e-5),
    case("LensDistortion STMap undistort, k1 0.1, k2 0.05, center 0.1,0.2",
         ("LensDistortion2", dict(LENS_K1K2C, output="STMap")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_K1K2C,
                                         direction="Undistort",
                                         outputMode="STMap")),
         nuke_post=lens_map("forward"),
         input="ramp", region=INNER, channels="rg", tolerance=2e-4),
    case("LensDistortion STMap distort, squeeze 2, asymmetric 0.1,0.05",
         ("LensDistortion2", dict(LENS_ANAMORPHIC, output="STMap")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_ANAMORPHIC,
                                         direction="Distort",
                                         outputMode="STMap")),
         nuke_post=lens_map("backward"),
         input="ramp", region=INNER, channels="rg", tolerance=1e-5),
    case("LensDistortion STMap distort, twist 20, scale 1.2,0.9",
         ("LensDistortion2", dict(LENS_TWIST, output="STMap")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_TWIST, direction="Distort",
                                         outputMode="STMap")),
         nuke_post=lens_map("backward"),
         input="ramp", region=INNER, channels="rg", tolerance=1e-5),
    # Undistort (the default) with Twist and Scale
    case("LensDistortion twist 45, scale 2,0.5, undistort",
         ("LensDistortion2", dict(LENS_TWIST, output="Undistort",
                                  anamorphicTwist=45,
                                  anamorphicScale=[2, .5])),
         (ofx_id("LensDistortion"), dict(OFX_LENS_TWIST, anamorphicTwist=45,
                                         anamorphicScale=[2, .5])),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    case("LensDistortion twist 20, scale 1.2,0.9, undistort",
         ("LensDistortion2", dict(LENS_TWIST, output="Undistort")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_TWIST)),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
    case("LensDistortion twist 20, scale 1.2,0.9, redistort",
         ("LensDistortion2", dict(LENS_TWIST, output="Redistort")),
         (ofx_id("LensDistortion"), dict(OFX_LENS_TWIST, direction="Distort")),
         input="ramp", region=INNER, channels="rgba", tolerance=2e-3),
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
CIMG = "net.sf.cimg."
CASES += [
    case("Blur defaults (size 0)", ("Blur", {}), (CIMG + "CImgBlur", {}),
         input="ramp", channels="rgba"),
    # The impulse response peaks at about 0.01: a tight tolerance.
    case("Blur size 10, impulse (BlurCImg size 10)",
         ("Blur", {"size": 10}), (CIMG + "CImgBlur", {"size": [10, 10]}),
         input="impulse", tolerance=1e-4),
    case("Blur size 3, impulse (BlurCImg size 3)",
         ("Blur", {"size": 3}), (CIMG + "CImgBlur", {"size": [3, 3]}),
         input="impulse", tolerance=1e-5),
    case("Blur size 1, impulse (BlurCImg size 1)",
         ("Blur", {"size": 1}), (CIMG + "CImgBlur", {"size": [1, 1]}),
         input="impulse", tolerance=1e-5),
    # Both sizes are canonical: the x size in pixels is the size / PAR.
    case("Blur size 4 on the ramp (edges)", ("Blur", {"size": 4}),
         (CIMG + "CImgBlur", {"size": [4, 4]}),
         input="ramp", channels="rgba", tolerance=2e-3),
    case("Blur size 4 on the ramp, BlurCImg boundary Nearest",
         ("Blur", {"size": 4}),
         (CIMG + "CImgBlur", {"size": [4, 4], "boundary": "Nearest"}),
         input="ramp", channels="rgba", tolerance=2e-3),
    case("Laplacian size 3, tail", ("Laplacian", {"size": 3}),
         (CIMG + "CImgLaplacian", {"size": [3, 3]}),
         input="impulse", tolerance=1e-5),
    case("Laplacian defaults on the checker", ("Laplacian", {}),
         (CIMG + "CImgLaplacian", {}), input="checker", channels="rgb",
         tolerance=1e-5),
    # Sharpen and Soften: RGB (SharpenCImg and SoftenCImg leave alpha
    # alone by default); the edges are repeated on both sides.
    case("Sharpen defaults on the checker", ("Sharpen", {}),
         (CIMG + "CImgSharpen", {}), input="checker", channels="rgb",
         tolerance=1e-5),
    case("Sharpen defaults, values in [0, 1]", ("Sharpen", {}),
         (CIMG + "CImgSharpen", {}), input="mask", channels="rgb",
         tolerance=1e-5),
    # The difference is 0 away from the impulse: with a negative Minimum,
    # a flat area whose difference rounds to +-1e-8 would get +-10.
    case("Sharpen minimum -10, maximum 10",
         ("Sharpen", {"minimum": -10, "maximum": 10}),
         (CIMG + "CImgSharpen", {"minimum": -10, "maximum": 10}),
         input="impulse", channels="rgb", tolerance=1e-5),
    case("Sharpen minimum 0.2, maximum 0.7",
         ("Sharpen", {"minimum": .2, "maximum": .7}),
         (CIMG + "CImgSharpen", {"minimum": .2, "maximum": .7}),
         input="checker", channels="rgb", tolerance=1e-5),
    # A negative Maximum gives a change of -Maximum against the sign of the
    # difference; a negative Minimum adds -Minimum to its magnitude.
    case("Sharpen maximum -0.2", ("Sharpen", {"maximum": -.2}),
         (CIMG + "CImgSharpen", {"maximum": -.2}), input="hash",
         channels="rgb", tolerance=1e-5),
    case("Sharpen minimum -0.1, maximum 0.3",
         ("Sharpen", {"minimum": -.1, "maximum": .3}),
         (CIMG + "CImgSharpen", {"minimum": -.1, "maximum": .3}),
         input="hash", channels="rgb", tolerance=1e-5),
    case("Sharpen maximum 0", ("Sharpen", {"maximum": 0}),
         (CIMG + "CImgSharpen", {"maximum": 0}), input="hash",
         channels="rgb", tolerance=1e-5),
    case("Sharpen amount 2, size 6",
         ("Sharpen", {"amount": 2, "size": 6}),
         (CIMG + "CImgSharpen", {"amount": 2, "size": [6, 6]}),
         input="checker", channels="rgb", tolerance=1e-5),
    case("Soften defaults on the checker", ("Soften", {}),
         (CIMG + "CImgSoften", {}), input="checker", channels="rgb",
         tolerance=1e-5),
    case("Soften minimum -10, maximum 10",
         ("Soften", {"minimum": -10, "maximum": 10}),
         (CIMG + "CImgSoften", {"minimum": -10, "maximum": 10}),
         input="impulse", channels="rgb", tolerance=1e-5),
    case("Soften minimum 0.2, maximum 0.7",
         ("Soften", {"minimum": .2, "maximum": .7}),
         (CIMG + "CImgSoften", {"minimum": .2, "maximum": .7}),
         input="checker", channels="rgb", tolerance=1e-5),
    # The reference writes one edge matte into alpha and keeps RGB; on an
    # image whose R, G, B and A are the same, EdgeDetectCImg on alpha only
    # should give the same matte. Its scale differs (e.g. 4.5 against 0.25
    # for Sobel next to a 0.5 step), so Threshold differs too (#314).
    case("EdgeDetect sobel, grey checker, alpha",
         ("EdgeDetect", {"edgedetector": "sobel"}),
         ("eu.cimg.EdgeDetect", dict(EDGE_ALPHA_ONLY, filter="Sobel")),
         input="greychecker", channels="rgba", tolerance=1e-3, known=314),
    case("EdgeDetect prewitt, grey checker, alpha",
         ("EdgeDetect", {"edgedetector": "prewitt"}),
         ("eu.cimg.EdgeDetect", dict(EDGE_ALPHA_ONLY, filter="Prewitt")),
         input="greychecker", channels="rgba", tolerance=1e-3, known=314),
    case("EdgeDetect sobel, threshold 0.2, grey checker, alpha",
         ("EdgeDetect", {"edgedetector": "sobel", "threshold": .2}),
         ("eu.cimg.EdgeDetect", dict(EDGE_ALPHA_ONLY, filter="Sobel",
                                     threshold=.2)),
         input="greychecker", channels="rgba", tolerance=1e-3, known=314),
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
    # A fractional size s is round(s / PAR) in x and round(s) in y.
    case("Dilate size 1.5 (DilateCImg 1, 2)", ("Dilate", {"size": 1.5}),
         (CIMG + "CImgDilate", {"size": [1, 2]}), input="impulse",
         channels="rgba"),
    case("Dilate size 2.5 (DilateCImg 2, 3)", ("Dilate", {"size": 2.5}),
         (CIMG + "CImgDilate", {"size": [2, 3]}), input="impulse",
         channels="rgba"),
    case("Dilate size -1.5 (ErodeCImg 1, 2)", ("Dilate", {"size": -1.5}),
         (CIMG + "CImgErode", {"size": [1, 2]}), input="checker",
         channels="rgba"),
    case("Dilate defaults (DilateCImg defaults)", ("Dilate", {}),
         (CIMG + "CImgDilate", {}), input="impulse", channels="rgba"),
    # The reference's "Erode (fast)" menu entry creates a Dilate with its
    # defaults (size 0): ErodeCImg's default is 0 too.
    case("ErodeCImg defaults (Erode (fast): Dilate defaults)", ("Dilate", {}),
         (CIMG + "CImgErode", {}), input="checker", channels="rgba"),
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


def retime_post(speed):
    """A "nuke_post" that samples the node through a Retime.

    Args:
        speed: the Retime speed: Nuke frame f samples the node at the time
            f * speed (filter none, input and output ranges from 0).

    Returns:
        The Python lines, for `nuke -t`.
    """
    return "\n".join([
        "n = nuke.nodes.Retime(inputs=[n])",
        "for _k, _v in (('input.first', 0), ('input.last', 40),",
        "               ('output.first', 0), ('speed', %r),"
        % speed,
        "               ('filter', 'none')):",
        "    n[_k].setValue(_v)"])

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
    # Fractional output times (motion blur, a retime downstream): a Retime
    # with the None filter samples the node at frame * speed.
    case("TimeOffset 5 at fractional times, whole frames",
         ("TimeOffset", {"time_offset": 5}),
         (ofx_id("timeOffset"), {"timeOffset": 5}),
         nuke_post=retime_post(0.25),
         frames=[41, 42, 43], ofx_times=[10.25, 10.5, 10.75],
         **TIME),
    case("TimeOffset 5.5 at fractional times, whole frames",
         ("TimeOffset", {"time_offset": 5.5}),
         (ofx_id("timeOffset"), {"timeOffset": 5.5}),
         nuke_post=retime_post(0.5),
         frames=[21, 23], ofx_times=[10.5, 11.5], **TIME),
    case("TimeOffset 5.5 at fractional times, fractional time",
         ("TimeOffset", {"time_offset": 5.5,
                         "rounding_mode": "Fractional time"}),
         (ofx_id("timeOffset"), {"timeOffset": 5.5,
                                 "roundingMode": "Fractional Time"}),
         nuke_post=retime_post(0.5),
         frames=[21, 23], ofx_times=[10.5, 11.5], **TIME),
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
         frames=[0, 1, 3, 7, 10], **TIME),
    # Box averages the input frames from the output time to Shutter frames
    # after it, mapped to the input (input frame n covers n to n+1);
    # Nearest takes the frame at the middle of that interval.
    case("Retime speed 0.5, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5}),
         (ofx_id("Retime"), {"speed": .5}),
         frames=[0, 1, 2, 3, 7, 10], **TIME),
    case("Retime speed 0.4, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .4}),
         (ofx_id("Retime"), {"speed": .4}),
         frames=[0, 2, 3, 7, 12], **TIME),
    case("Retime speed 1.5, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 1.5}),
         (ofx_id("Retime"), {"speed": 1.5}),
         frames=[0, 2, 3, 9, 10], **TIME),
    # frame 8 covers input frames 24 to 26: frame 26 is after the input,
    # and After Hold reads frame 25 instead
    case("Retime speed 3, box, last frames",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 3.}),
         (ofx_id("Retime"), {"speed": 3.}),
         frames=[0, 2, 7, 8], **TIME),
    case("Retime speed 2, shutter 2",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "shutter": 2.}),
         (ofx_id("Retime"), {"speed": 2., "shutter": 2.}),
         frames=[0, 2, 5, 12], **TIME),
    case("Retime speed 2, shutter 0.5",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "shutter": .5}),
         (ofx_id("Retime"), {"speed": 2., "shutter": .5}),
         frames=[0, 2, 5], **TIME),
    case("Retime speed 0.5, shutter 2",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "shutter": 2.}),
         (ofx_id("Retime"), {"speed": .5, "shutter": 2.}),
         frames=[2, 3, 4, 9], **TIME),
    case("Retime speed 2, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": 2., "filter": "Nearest"}),
         frames=[0, 2, 3, 12], **TIME),
    case("Retime speed 1.5, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 1.5, "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": 1.5, "filter": "Nearest"}),
         frames=[2, 3, 4, 5], **TIME),
    case("Retime speed 0.4, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .4, "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": .4, "filter": "Nearest"}),
         frames=[2, 3, 4, 7, 9], **TIME),
    # Reverse input: the input reversed as whole frames (output time 0 is
    # the end of input frame 25); None reads the start of the reversed
    # interval of the output frame, Box and Nearest cover it.
    case("Retime speed 0.5, reverse, filter none",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "reverse": True, "filter": "none"}),
         (ofx_id("Retime"), {"speed": .5, "reverseInput": 1,
                             "filter": "None"}),
         frames=[0, 1, 2, 3, 40, 41, 42], **TIME),
    case("Retime speed 1, reverse, filter none",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 1., "reverse": True, "filter": "none"}),
         (ofx_id("Retime"), {"speed": 1., "reverseInput": 1,
                             "filter": "None"}),
         frames=[0, 1, 24, 25, 26], **TIME),
    case("Retime speed 2, reverse, filter none",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "reverse": True, "filter": "none"}),
         (ofx_id("Retime"), {"speed": 2., "reverseInput": 1,
                             "filter": "None"}),
         frames=[0, 1, 11, 12, 13], **TIME),
    case("Retime speed 0.5, reverse, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "reverse": True}),
         (ofx_id("Retime"), {"speed": .5, "reverseInput": 1}),
         frames=[0, 1, 2, 3, 41, 42], **TIME),
    case("Retime speed 1, reverse, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 1., "reverse": True}),
         (ofx_id("Retime"), {"speed": 1., "reverseInput": 1}),
         frames=[0, 1, 24, 25, 26], **TIME),
    case("Retime speed 2, reverse, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "reverse": True}),
         (ofx_id("Retime"), {"speed": 2., "reverseInput": 1}),
         frames=[0, 1, 2, 12, 13], **TIME),
    case("Retime speed 0.5, reverse, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": .5, "reverse": True, "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": .5, "reverseInput": 1,
                             "filter": "Nearest"}),
         frames=[0, 1, 2, 3, 41, 42], **TIME),
    case("Retime speed 2, reverse, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "reverse": True, "filter": "nearest"}),
         (ofx_id("Retime"), {"speed": 2., "reverseInput": 1,
                             "filter": "Nearest"}),
         frames=[0, 1, 12, 13], **TIME),
    # a negative speed plays backwards from the first frame: Before Hold
    case("Retime speed -1, box",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": -1.}),
         (ofx_id("Retime"), {"speed": -1.}),
         frames=[0, 2, 5], **TIME),
    case("Retime speed 2, shutter 2, filter nearest",
         ("Retime", {"input.first": 0, "input.last": 25, "output.first": 0,
                     "speed": 2., "filter": "nearest", "shutter": 2.}),
         (ofx_id("Retime"), {"speed": 2., "filter": "Nearest",
                             "shutter": 2.}),
         frames=[2, 5, 9], **TIME),
    case("FrameBlend defaults", ("FrameBlend", {}),
         (ofx_id("FrameBlend"), {}), frames=[10],
         **dict(TIME, known=310)),
    case("FrameBlend 6 frames (FrameBlend defaults)",
         ("FrameBlend", {"numframes": 6}), (ofx_id("FrameBlend"), {}),
         frames=[10], **TIME),
    # Nuke's knob defaults (knobdefaults.py) are shutteroffset start and
    # shuttercustomoffset 0 (the built-in ones: custom, -0.25). The harness
    # applies them too; they are pinned so that the cases state them.
    case("TimeBlur defaults",
         ("TimeBlur", {"shutteroffset": "start", "shuttercustomoffset": 0.}),
         (ofx_id("TimeBlur"), {}), frames=[10], **TIME),
    case("TimeBlur defaults at fractional times",
         ("TimeBlur", {"shutteroffset": "start", "shuttercustomoffset": 0.}),
         (ofx_id("TimeBlur"), {}), nuke_post=retime_post(0.5),
         frames=[21, 23], ofx_times=[10.5, 11.5], **TIME),
    case("TimeBlur divisions 4, shutter 1, centred",
         ("TimeBlur", {"divisions": 4, "shutter": 1.,
                       "shutteroffset": "centred"}),
         (ofx_id("TimeBlur"), {"division": 4, "shutter": 1.,
                               "shutterOffset": "Centered"}),
         frames=[10], **TIME),
    case("TimeBlur divisions 3, shutter 1, end",
         ("TimeBlur", {"divisions": 3, "shutter": 1.,
                       "shutteroffset": "end"}),
         (ofx_id("TimeBlur"), {"division": 3, "shutter": 1.,
                               "shutterOffset": "End"}),
         frames=[10], **TIME),
    case("TimeBlur divisions 4, shutter 2, custom offset -0.3",
         ("TimeBlur", {"divisions": 4, "shutter": 2.,
                       "shutteroffset": "custom",
                       "shuttercustomoffset": -.3}),
         (ofx_id("TimeBlur"), {"division": 4, "shutter": 2.,
                               "shutterOffset": "Custom",
                               "shutterCustomOffset": -.3}),
         frames=[10], **TIME),
    # one division: the input at the current time, whatever the shutter
    case("TimeBlur divisions 1, centred",
         ("TimeBlur", {"divisions": 1, "shutter": 1.,
                       "shutteroffset": "centred"}),
         (ofx_id("TimeBlur"), {"division": 1, "shutter": 1.,
                               "shutterOffset": "Centered"}),
         frames=[10], **TIME),
    case("TimeBlur divisions 1, end",
         ("TimeBlur", {"divisions": 1, "shutter": 1.,
                       "shutteroffset": "end"}),
         (ofx_id("TimeBlur"), {"division": 1, "shutter": 1.,
                               "shutterOffset": "End"}),
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
    case("TimeDissolve in 5, out 5",
         ("TimeDissolve", {"in": 5, "out": 5}),
         (ofx_id("TimeDissolvePlugin"), {"dissolveIn": 5, "dissolveOut": 5}),
         nuke_inputs=["black", "white"],
         clips={"Source": "black", "A": "white"},
         frames=[4, 5, 6], channels="rgba", tolerance=1e-4),
    case("TimeDissolve in 1, out 10, at fractional times",
         ("TimeDissolve", {"in": 1, "out": 10}),
         (ofx_id("TimeDissolvePlugin"), {}),
         nuke_inputs=["black", "white"],
         clips={"Source": "black", "A": "white"},
         nuke_post=retime_post(0.5),
         frames=[1, 3, 13, 21], ofx_times=[0.5, 1.5, 6.5, 10.5],
         channels="rgba", tolerance=1e-4),
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
    # Fades are quadratic ((k/(N+1))^2), the cross-dissolve is a smoothstep
    # of k/(N+1); fades only apply at the start and at the end of the whole
    # sequence (two clips of 26 frames: output frames 1 to 52).
    case("AppendClip fade out 3", ("AppendClip", {"fadeOut": 3}),
         (ofx_id("AppendClip"), {"fadeOut": 3}),
         nuke_inputs=["white", "white"], clips={"1": "white", "2": "white"},
         frames=[26, 27, 49, 50, 51, 52, 53], channels="rgba",
         tolerance=1e-4),
    case("AppendClip fade in 4, fade out 4",
         ("AppendClip", {"fadeIn": 4, "fadeOut": 4}),
         (ofx_id("AppendClip"), {"fadeIn": 4, "fadeOut": 4}),
         nuke_inputs=["white", "white"], clips={"1": "white", "2": "white"},
         frames=[0, 1, 4, 5, 26, 27, 48, 49, 52, 53], channels="rgba",
         tolerance=1e-4),
    case("AppendClip dissolve 4, black to white",
         ("AppendClip", {"dissolve": 4}),
         (ofx_id("AppendClip"), {"crossDissolve": 4}),
         nuke_inputs=["black", "white"], clips={"1": "black", "2": "white"},
         frames=[22, 23, 24, 25, 26, 27], channels="rgba", tolerance=1e-4),
    case("AppendClip dissolve 1, black to white",
         ("AppendClip", {"dissolve": 1}),
         (ofx_id("AppendClip"), {"crossDissolve": 1}),
         nuke_inputs=["black", "white"], clips={"1": "black", "2": "white"},
         frames=[25, 26, 27], channels="rgba", tolerance=1e-4),
    case("AppendClip first frame 0", ("AppendClip", {"firstFrame": 0}),
         (ofx_id("AppendClip"), {"firstFrame": 0}),
         nuke_inputs=["frame", "white"], clips={"1": "frame", "2": "white"},
         frames=[0, 1, 25, 26, 51], channels="rgba", tolerance=1e-4),
    case("AppendClip dissolve 4, fade in 2, fade out 2",
         ("AppendClip", {"dissolve": 4, "fadeIn": 2, "fadeOut": 2}),
         (ofx_id("AppendClip"), {"crossDissolve": 4, "fadeIn": 2,
                                 "fadeOut": 2}),
         nuke_inputs=["frame", "white"], clips={"1": "frame", "2": "white"},
         frames=[1, 2, 3, 24, 47, 48, 49], channels="rgba", tolerance=1e-4),
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

# The clean plate without a C input is transparent black (#328). (After the
# IBKGizmoV3 cases: the first case of a plug-in gives the node whose knob
# order test_param_order.py compares.)
CASES.append(case(
    "PIK Clean Plate, C disconnected (IBKColourV3, no input)",
    ("IBKColourV3", {"screen_type": "blue"}),
    (ofx_id("PIK"), {"screenType": "C-Blue", "show": "Clean Plate"}),
    nuke_inputs=[None], clips={"Source": "hues01"}, channels="rgba"))

# ---------------------------------------------------------- ContactSheet
# ContactSheet 2 makes square output pixels where the host supports multiple
# clip PARs; hostDemo does not, and gives the output the PAR of the inputs:
# the Nuke side uses square pixels (nuke_par), so that both sides fit the
# same pixels in the same cells. The cells are a whole number of pixels,
# (size - gap) / count rounded, with the gap on their left and bottom sides.
def _contact_sheet(name, width, height, rows, columns, inputs, gap=0,
                   center=False, row_order="BottomTop",
                   column_order="LeftRight", **kwargs):
    """A ContactSheet case: inputs is the image of each input (or None)."""
    clips = {("Source" if i == 0 else str(i)): image
             for i, image in enumerate(inputs) if image}
    return case(
        "ContactSheet " + name,
        ("ContactSheet", {"width": width, "height": height, "rows": rows,
                          "columns": columns, "gap": gap, "center": center,
                          "roworder": row_order, "colorder": column_order}),
        (ofx_id("ContactSheetOFX"), {
            "resolution": [width, height], "rowsColumns": [rows, columns],
            "gap": gap, "center": int(center), "rowOrder": row_order,
            "colOrder": column_order}),
        nuke_inputs=list(inputs), clips=clips,
        channels="rgba", tolerance=1e-4, nuke_par=1.,
        **dict({"region": (0, 0, width, height)}, **kwargs))


for _center in (False, True):
    CASES.append(_contact_sheet(
        "2 inputs, 2x2 cells of 32x32, center %s" % ("on" if _center else "off"),
        64, 64, 2, 2, ["ramp", "checker"], center=_center,
        row_order="TopBottom"))
CASES += [
    # cells of 20x20 with a gap of 4: the 32x32 images are scaled by 0.5.
    # Not compared: the right margin (x 60 to 64, outside the cells), which
    # ContactSheet leaves black; it differs there.
    _contact_sheet("2x3 cells, gap 4, scale 0.5", 64, 44, 2, 3,
                   ["ramp", None, "checker", "checker", "ramp"], gap=4,
                   region=(0, 0, 60, 44)),
    _contact_sheet("2x3 cells, gap 4, Snake, top to bottom", 64, 44, 2, 3,
                   ["ramp", "checker", None, "ramp", "checker"], gap=4,
                   row_order="TopBottom", column_order="Snake",
                   region=(0, 0, 60, 44)),
    # cells of 23x20 (69 / 3 rounded): with Center, the images are 4.5
    # pixels from the left of the area after the gap
    _contact_sheet("2x3 cells of 23x20, gap 4, center", 73, 44, 2, 3,
                   ["ramp", "checker", "ramp"], gap=4, center=True),
    # magnification: the 32x32 images fill cells of 64x64, scaled by 2
    _contact_sheet("1x2 cells, scale 2", 128, 64, 1, 2,
                   ["checker", "ramp"]),
    # cells of 64x70: centred vertically, 3 pixels from the bottom
    _contact_sheet("1x2 cells, scale 2, center", 128, 70, 1, 2,
                   ["ramp", "checker"], center=True),
    _contact_sheet("1x1 cell, scale 29/32", 29, 29, 1, 1, ["checker"]),
    # magnifications other than by 2: the filter phase is rounded to 1/64
    # of an input pixel (#305)
    _contact_sheet("1x1 cell, scale 2.4", 77, 77, 1, 1, ["checker"]),
    _contact_sheet("1x1 cell, scale 1.25", 40, 40, 1, 1, ["checker"]),
    _contact_sheet("1x2 cells, scale 1.59, center", 102, 60, 1, 2,
                   ["ramp", "checker"], center=True),
]

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
]


# ----------------------------------------------- Known divergences, by case
# The differences the runs found, with the issue that records each one
# (DOCUMENTED: differences that are intentional or come from OFX, e.g.
# black outside the RoD where the reference repeats the edge pixels).
DOCUMENTED = 318
KNOWN = {
    270: [
        # rotations: about 0.01 on the ramp (0.64 on the checker at 30
        # degrees, none at 90)
        "Transform rotate 30", "Card3D rotate z 30",
        "Card3D skew x 20, rotate z 30",
        # the same, plus the soft edge of the transformed image (black
        # outside); inside the image they differ by 0.016 at most
        "Transform translate, scale, skew, rotate (order)",
        "Card3D rotate y 20, z 30",
        # 1.2e-3 to 1.6e-3, not explained yet
        "CornerPin2D four pins", "CornerPin2D 2 pin(s) enabled",
        "CornerPin2D 3 pin(s) enabled",
        # shears: version 1 matches (0); version 2 (Prefilter Downscaling
        # widens the kernel) differs by 0.003 (skew 0.3) and 0.31 (skew 1)
        "Transform skewX 0.3", "Transform skewY 0.3",
        "Transform skewX 1, checker",
        # minification on a hard-edged checker, version 1 (no prefilter) /
        # version 2: 0.30 / 0.022, 0.23 / 0.014, 0.057 / 0.0020, 0.18 / 0.059
        "Transform scale 0.4,0.7, checker (minification)",
        "Transform scale 1.5,0.8, checker (minification)",
        "Transform Lanczos4 scale 0.5, checker (minification)",
        "CornerPin2D shrink, checker"] + [
            # a fractional translation: 0.005 to 0.016 (Lanczos4 0.028,
            # Lanczos6 0.012, Sinc4 0.16), not explained yet
            "Transform filter %s, translate 0.3,0.6" % _f for _f in (
                "Bilinear", "Cubic", "Keys", "Simon", "Rifman", "Mitchell",
                "Parzen", "Notch", "Lanczos4", "Lanczos6", "Sinc4")],
    279: ["ShuffleCopy defaults"],
    288: [
        # version 2 (obsolete): the Softness at the pixel corner nearest to the center (Radial) or without the
        # extra pixel (Rectangle), and Quadratic by default
        "Radial 2 area 100,100-600,476 softness 1", "Radial 2 softness 0.3, plinear",
        "Rectangle 2 softness 40"],
    # the reference's Undistort is not the exact inverse of its Redistort:
    # its forward map differs by 0.02 to 0.1 pixel away from the corners, and
    # by pixels near them (see the STMap case)
    302: [
        "LensDistortion k1 0.1 (default: undistort)",
        "LensDistortion k1 -0.1, undistort",
        "LensDistortion STMap undistort, k1 0.1, k2 0.05, center 0.1,0.2",
        # the map matches (see the STMap case), but where it minifies (Scale
        # 1.2), the reference's Cubic resampling differs by up to 0.1 pixel
        "LensDistortion twist 20, scale 1.2,0.9, redistort",
        # Undistort with Twist and Scale: as above, and where the model has no
        # inverse ours is transparent black, the reference's is finite
        "LensDistortion twist 45, scale 2,0.5, undistort",
        "LensDistortion twist 20, scale 1.2,0.9, undistort"],
    352: [
        "IDistort, alpha 0.5, premultiplied",
        "STMap uv = rgb, premultiplied, alpha 0.5",
        "STMap uv = rgb, premultiplied, alpha 0 on the left",
        "IDistort uv = rgb, blur = blue",
        "IDistort uv = rgb, blur = blue, blur scale 4",
        "STMap uv = rgb, blur = blue (1), blur scale 3"],
    315: [
        "Erode (blur) defaults (ErodeBlur)",
        "Erode (blur) size -3, blur 0.5 (ErodeBlur -3)",
        "Erode (blur) size 3, blur 0.5",
        "FilterErode size 1 box (ErodeSmoothCImg)"],
    DOCUMENTED: [
        # the previous major versions of the Transform family (Prefilter
        # Downscaling off by default, #270) alias when they scale down
        "Transform 1 cubic, scale 0.5 (minification, no prefilter)",
        "Reformat 2 scale 0.5 (no prefilter)",
        "Card3D 1 scale 0.5 (no prefilter)",
        "Log2Lin lin2log", "ColorWheel defaults, whole disc",
        "ColorWheel gamma 1, whole disc",
        "Ramp defaults", "Transform translate 0.5,0.25 cubic, edges",
        "Reformat to box 20x12 fit, box PAR = PAR",
        "Reformat to box 20x12 fit, reference black outside",
        "Blur size 4 on the ramp (edges)", "Convolve2 3x3 kernel, edges",
        "Radial softness 0", "Radial softness 0.001", "Roto no clip, Source in a box"] + [
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
    314: 'EdgeDetectCImg: no Prewitt, no threshold, no single alpha...',
    315: 'ErodeCImg/DilateCImg, ErodeBlurCImg, ErodeSmoothCImg, Med...',
    316: 'ConvolveCImg: per-channel kernels by default',
    317: 'Compatibility tests that cannot be automated yet',
    318: 'Document the behaviours that come from OFX: black outside...',
    319: 'AppendClip: first output frame, fade curve, cross-dissolve',
    320: 'TimeDissolve: the dissolve starts before the In frame',
    352: 'IDistort, STMap: Unpremult UV, and the blur near edges and...',
}
