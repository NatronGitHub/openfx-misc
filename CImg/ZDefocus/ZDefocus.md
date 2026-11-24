# ZDefocusCImg: model, algorithm and limitations

This document describes the ZDefocusCImg plug-in (`CImg/ZDefocus/CImgZDefocus.cpp`, identifier
`net.sf.cimg.CImgZDefocus`, version 1.0): its thin-lens model, its algorithm, the artifacts of
slice-based depth of field and how the plug-in handles them, a review of the literature and of
the open-source implementations, the improvements made before its release, how its
parameters map onto those of Foundry's documented ZDefocus, and a survey of faster methods. The plug-in comes from issue
[#32](https://github.com/NatronGitHub/openfx-misc/issues/32); the fixes and features of
[#266](https://github.com/NatronGitHub/openfx-misc/issues/266) (the blur formula, depth types,
soft slices, background fill, blur inside, Gaussian size, radii and defaults) were made before
its first release.

**TL;DR**

- The blur is the thin-lens circle of confusion, which is linear in inverse depth: the blur of a
  pixel is proportional to its inverse-depth (or disparity) distance to the focus, and `Size` is
  the blur radius at infinity (with no Depth of Focus). Slices evenly spaced in blur size are
  evenly spaced in inverse depth, which is the right choice.
- Six depth types: Depth, Disparity (zero at infinity), Disparity with Offset (stereo or
  monocular estimates, whose offset is unknown), Direct (a map that directly controls the
  blur), and Negated Depth and Negated Disparity.
- Each pixel is shared by its two neighbouring slices, so a depth gradient shows no bands, and
  the background hidden by each slice in front of the focal plane is filled before the slice is
  merged, so a blurred foreground has no hard inner edge over a sharp background.
- Remaining artifacts: fringes at antialiased depth edges (3.6), a filled background that can
  look smeared (3.2), and a blend of two blur sizes instead of the exact one between slices (3.1).
- Faster methods (section 8): the cheapest large gain is the FFT for the Disc aperture at large
  radii; a GPU path for the same layered method comes next.

## 1. The model

### 1.1 Thin-lens circle of confusion

A thin lens of focal length f and aperture diameter A = f/N (N is the f-number) is focused at
distance z_f: the sensor is at the image distance s_f = f*z_f/(z_f - f). A point at distance z
is imaged at s = f*z/(z - f), and its light cone cuts the sensor in a disc, the circle of
confusion (CoC) [1, 1981]. By similar triangles its diameter is

    c = A * |s - s_f| / s

and replacing s and s_f:

    s_f/s = z_f*(z - f) / (z*(z_f - f))
    1 - s_f/s = f*(z_f - z) / (z*(z_f - f))
    c = A*f/(z_f - f) * |1 - z_f/z|

(on the sensor, in sensor units). Three consequences:

- At infinity, c_inf = A*f/(z_f - f) = f^2/(N*(z_f - f)). So c = c_inf * |1 - z_f/z|: the blur
  is c_inf times a *normalized defocus* |z_f/z - 1|, which is 0 at the focal plane, 1 at
  infinity, and grows without bound for near objects (it is 1 at z = z_f/2, 2 at z = z_f/3).
- c = c_inf*z_f * |1/z - 1/z_f|: the blur is linear in inverse depth. With z_f >> f,
  c_inf*z_f ~ A*f, so c ~ A*f*|1/z - 1/z_f|.
- A rectified stereo pair with baseline b and focal length f_px (in pixels) has disparity
  d = b*f_px/z, so the blur in pixels is c_px ~ (A/b) * |d - d_f|: the blur is the disparity
  difference times the ratio of the aperture to the baseline. This is why synthetic defocus
  from stereo or dual-pixel data works directly in disparity [51, 2015], [54, 2018].

The sign of (z_f/z - 1) tells whether the point is in front of (positive) or behind (negative)
the focal plane. For a real lens the CoC of a point in front of the focal plane is the aperture
image rotated by 180 degrees with respect to the CoC of a point behind it (the light cone crosses
its apex before the sensor). This only matters for asymmetric apertures (odd blade count,
cat's eye).

### 1.2 What "Size" means physically

`Size` is a radius, half of c_inf converted to pixels: Size = f^2/(2*N*(z_f - f)) * (image width
in pixels / sensor width). A radius is the distance from the center of a point's blur to its
edge, the natural unit of a circle of confusion, and it does not depend on the rounding of an
odd or even kernel width. Example: 50 mm at f/2 focused at 2 m on a 36 mm wide sensor, 1920
pixels wide: c_inf = 2500/(2*1950) mm * 1920/36 = 34 pixels, so Size = 17. The kernel diameter
of a blur radius r is 2r.

Two practical consequences:

- For a fixed lens and aperture, c_inf depends on the focus distance (it is roughly proportional
  to 1/z_f). Animating Focus with a constant Size is therefore *not* a focus pull with a fixed
  aperture: the blur at infinity stays constant while it should grow when focusing nearer. The
  parametrization by "blur at infinity" is the one of Foundry's ZDefocus [87, 2026] ("size: Sets the
  size of the blur at infinite depth"); the physical one,
  blur = K*|1/z - 1/z_f| with K = A*f in pixel-depth units, or K*|d - d_f| for disparity, is the
  Disparity with Offset type (1.3).
- Only the ratio z_f/z matters, so the depth map can be in any unit, as long as Focus and Depth of
  Focus are in the same unit.

### 1.3 Depth types

`BlurModel` gives the signed blur radius of a pixel from its Depth value v (negative behind the
focus, positive in front of it), clamped to [-Maximum Size, Maximum Size]; its kernel diameter
is twice this radius.

| Depth Type | Signed defocus s | Blur radius |
|---|---|---|
| Depth | focus/v - 1 | Size*max(0, \|s\| - dof/2) |
| Disparity | v/focus - 1 | Size*max(0, \|s\| - dof/2) |
| Disparity with Offset | v - focus | Size*max(0, \|s\| - dof/2) |
| Direct | focus - v (larger values are farther) | Size*max(0, \|s\| - dof/2) |
| Negated Depth, Negated Disparity | Depth and Disparity of -v, with a focus of -focus | |

For Depth and Disparity, the ratio focus/v (v/focus) is 0 when it is not positive or is NaN: a
depth <= 0, a disparity <= 0, a negative focus and NaN values are at infinity. A pixel outside
the Depth image has the value 0. With the same Size, Disparity with Offset and Direct give the
same blur radius, Size*|v - focus|; they only differ by the order of the slices: larger values
are nearer for Disparity with Offset and farther for Direct. A NaN value is in focus for them.

- **Depth and Disparity.** With no Depth of Focus, the blur radius is Size*|1 - z_f/z|, the
  thin-lens formula of 1.1, and Size is the blur at infinity. A Depth of Focus only subtracts a
  dead zone (1.4), so the blur at infinity is then Size*(1 - dof/2), less than Size. A Disparity
  map must be zero at infinity (d = k/z). The negated types are for maps of -depth and
  -1/depth. The formula holds for every focus. With a focus of 0, a depth map is blurred by Size
  everywhere (the ratio is 0), and a disparity map by Maximum Size wherever the disparity is
  above 0 (the ratio is infinite), and by Size at a disparity of 0 (0/0 counts as 0). So neither
  can keep a sky sharp: a sharp sky (a disparity of 0) is Disparity with Offset with a focus of
  0.
- **Disparity with Offset.** Stereo disparity maps are often relative to the convergence plane
  (d = b*f_px/z - d0), and monocular depth estimators such as MiDaS or Depth Anything predict
  *affine-invariant* inverse depth (unknown scale and shift) [84, 2022], [85, 2024]. d/d_f - 1 is wrong for
  them, but Size*|d - d_f| is exact whatever the offset, because the offset cancels. Size is the
  blur per unit of disparity (K of 1.2, with the scale of the map).
- **Direct.** The value directly controls the blur: the radius is Size*|v - focus|, and larger
  values are farther. For example a depth-like map that is linear in the blur. Depth of Focus is
  a dead zone in the same units. A renderer's unsigned defocus (CoC) pass is not a Direct map: it
  does not say which side of the focal plane a pixel is on, which the slice order needs.

### 1.4 Depth of focus

`Depth of Focus` is a depth slice around the focus that is entirely in focus (the definition of
Foundry's documentation [87, 2026]: "Sets a depth slice around the focus plane that is entirely in
focus"). For Depth and Disparity (and the negated types), it is in units of the normalized
defocus: the blur radius is Size*max(0, |s| - dof/2) (1.3), i.e. the in-focus range is
[w_f*(1 - dof/2), w_f*(1 + dof/2)] in inverse depth w (with dof >= 2, everything behind the
focus is in focus). For Disparity with Offset and Direct, it is in the units of the input:
[focus - dof/2, focus + dof/2]. Inside, the blur is 0; outside, the blur grows linearly with the
distance to the range, so it is continuous at the limits of the depth of focus. A Depth of
Focus never adds blur, and the blur stays monotonic in depth (without Blur Inside).

With **Blur Inside** (on by default; "When enabled, a small amount of blur is applied to the
in-focus region" [87, 2026]), the pixels with no blur (in the depth of focus, or exactly at the focus)
get a fixed 3x3 separable kernel [a, b, a] = [0.125, 0.75, 0.125]: a variance of 2a = 0.25,
i.e. a standard deviation of 0.5 pixel, the smallest blur that softens one-pixel edges. By
design, only the in-focus region is blurred: the pixels that are out of focus, even slightly,
are not affected, so the blur is discontinuous at the edge of the in-focus region.

Physically, the depth of field is the range where c is below an acceptable CoC c_max: in inverse
depth it is symmetric, |1/z - 1/z_f| < c_max/(c_inf*z_f), and in depth it is asymmetric (more
room behind than in front). The plug-in's dead zone for depth and disparity maps is a range of
normalized defocus, so it is symmetric in inverse depth and corresponds to a constant
acceptable CoC, c_max = Size*dof (a diameter).

### 1.5 Approximations

1. **Asymmetric apertures.** The custom kernel is used with the same orientation in front of and
   behind the focal plane. A real lens flips it (1.1). This only shows with asymmetric kernels.
2. **Pixel aspect ratio.** Blur sizes are in pixels in both directions: on non-square-pixel
   footage the bokeh is circular in pixels, so elliptic on display. There is no aspect ratio
   parameter (Foundry's ZDefocus has one [87, 2026]).
3. **Focus point.** The focus is a single depth sample (`getFocusValue()`), read at the render
   scale: on a depth edge or a noisy depth map it is unstable, and a proxy render may read a
   different (downscaled) value than the full-resolution render.
4. **Thin-lens limits.** No vignetting or cat's-eye bokeh, no aberrations, no focus breathing, no
   diffraction, a kernel that is the same everywhere in the image: these are the usual
   assumptions of post-process depth of field [1, 1981], [12, 2003].

## 2. The algorithm as implemented

### 2.1 Steps

All in `ZDefocusPlugin::render()` unless noted.

1. **Parameters.** In draft mode or at a reduced render scale the aperture is forced to
   Gaussian. A Custom aperture without a usable Filter image falls back to Disc.
2. **Custom kernel** (`getCustomKernel()`): the Filter alpha (or luminance) within its RoD,
   cropped to the bounding box of the pixels above 1/1000 of the maximum.
3. **Focus** (`getFocusValue()`): the Focus parameter, or the Depth value at the Focus Point
   pixel.
4. **Blur model** (`BlurModel`): the in-focus range and the slope of 1.3 and 1.4.
5. **Processing rectangle**: the render window padded by the kernel half-size of the largest
   kernel diameter, 2*Maximum Size (`blurPadding()` of a diameter d: 0.9*d + 3, i.e. 3.6
   standard deviations, for the Gaussian, d/2 + 1 otherwise), clipped to the Source bounds. `getRegionsOfInterest()` requests the same padding
   (`renderPadding()`).
6. **Load and classify.** The Source is premultiplied if it is not. For each pixel: the signed
   blur radius b (1.3) gives the slice coordinate q = b*n/Max in [-n, n] (n = Focus Slices,
   `BlurModel::sliceCoordinate()`). With k0 = floor(q), the pixel has the weight 1 in slice k0 and
   q - k0 in slice k0 + 1 (`sliceWeight()`, 6.2).
7. **Slice bounding boxes**: each pixel is in the boxes of the slices where its weight is not 0.
8. **Per slice, from k = -n (farthest, most blurred background) to k = +n (nearest)**:
   - kernel diameter s_k = 2*|k|*Max/n, times the render scale;
   - crop the slice bounding box grown by twice the kernel half-size;
   - build a layer with C + 2 channels (`setWeightedImage()`): the premultiplied color of the
     slice pixels times their weight, the weight (the slice mask M_k), and a constant 1 channel.
     For a slice in front of the focal plane (k > 0) with Fill Background on, C + 1 more
     channels: the premultiplied color behind the slice and its weight (`behindWeight()`:
     clamp(k - q, 0, 1), the part of the pixel that is not in slice k or nearer, times
     clamp(k + 1 - |q|, 0, 1), which leaves out the pixels blurrier than slice k);
   - blur all channels with the aperture (`apertureBlur()`), with zero boundary conditions:
     Gaussian (`gaussianBlur()`: a separable Gaussian of standard deviation r/sqrt(8),
     truncated to |dx|, |dy| <= r, a direct convolution for small kernels and the FFT for
     large ones), antialiased elliptic disc (`discBlur()`, row prefix sums: a
     pixel at a distance d from the center has the weight clamp(r - d, 0, 1)), or custom kernel
     (`customBlur()`, direct convolution or FFT);
   - fill the hidden background (`fillHiddenBackground()`, 3.2): where the normalized blurred
     slice mask a is above a tenth of the mask of a single pixel (1/(kernel area)), the uncovered
     part (1 - coverage) of the result gets the blurred color behind the slice divided by its
     blurred weight, in the proportion min(1, blurred weight/(1 - a)) of the part of the kernel
     that is not the slice;
   - with Blur Inside, at k = 0: the in-focus pixels (q = 0) are a separate layer, blurred by the
     Blur Inside kernel (1.4; a Gaussian of the same standard deviation at a reduced render
     scale), merged after the rest of slice 0;
   - normalize by the blurred 1 channel ("ones"), which is the normalized convolution of
     [6, 1993]: pixels outside the processed rectangle are ignored instead of being black, so
     the image edges are not darkened;
   - merge over the accumulated result (`mergeOver()`), with a = clamp(blur(M_k)/ones, 0, 1):
     result = blur(C_k)/ones + (1 - a)*result, coverage = a + (1 - a)*coverage. This is the
     Porter-Duff "over" [5, 1984].
9. **Output**: result/coverage, then unpremultiplied if the Source was unpremultiplied.

The final division by coverage is what removes the dark halos: where a far slice has a hole
(the pixels that belong to a nearer slice), its blurred color and its blurred mask are both
attenuated, and their ratio is the average color of the visible part of the slice. This is
equivalent to filling the hole with a normalized convolution of the far slice, at the size of the
far slice's blur. The background fill of step 8 does the same before each near slice, at the
size of the near slice's blur, where the near slice is semi-transparent.

### 2.2 Complexity

Let N be the number of pixels in the processing rectangle, C the number of channels (1 to 4) and
n the number of slices on each side. Each non-empty slice blurs C + 2 channels over its (grown)
bounding box (2C + 3 for the slices in front of the focal plane with Fill Background), so in the
worst case (every slice spans the image):

- Gaussian: O((C+2) * N * s) per slice for small kernels (two passes of s + 1 taps), and
  O((C+2)/2 * F log F) with the FFT for large ones (2.3), so the cost stops growing with the
  blur size.
- Disc: O((C+2) * N * s) per slice, with s the slice blur size in pixels (one prefix-sum
  lookup per kernel row, about s + 1 rows), so O((C+2) * N * n * Max) over all slices in the
  worst case.
- Custom, direct: O((C+2) * N * s^2) per slice.
- Custom, FFT: O((C+2)/2 * F log F) per slice, with F the padded FFT size (two channels per
  complex FFT, plus one FFT of the kernel).

Sharing the pixels between two slices keeps the number of blurs, but a depth gradient makes the
slice boxes about twice as wide, and Fill Background blurs C + 1 more channels in the slices in
front of the focal plane. The measured render times are in #269.

Plus the tile overhead: each tile is processed with a margin of the maximum kernel half-size on
each side, so small tiles cost (tile + 2*pad)^2 / tile^2 more.

Memory: the source (C floats per pixel), the slice coordinates, the result (C + 1) and one layer
(C + 2, or 2C + 3), plus the FFT buffers.

### 2.3 FFT/direct switch

`customBlur()` uses the direct convolution when K*W*H < 100*F, where K is the number of kernel
pixels and F the area of the zero-padded FFT, assuming 1 ns per pixel and kernel pixel for the
direct convolution and 100 ns per FFT pixel (this cost model is out of date, #266). Both paths
give the same results up to rounding and FFT ringing (the mask is clamped to [0, 1] for that),
and both compute a true convolution (CImg's `convolve()` mirrors the kernel, as the FFT product
does), which is the right operation for a scattering point spread function.

The Gaussian (`truncatedGaussianBlur()`) uses the FFT, with the 2D product of its two 1D
kernels, when the direct cost (the taps of the two 1D kernels per pixel) exceeds 100 times the
area of the padded FFT image relative to the image (`cimgFFTSize()` sizes, measured with
pocketfft): for kernels of radius above about 25 pixels on a 1920x1080 frame.

## 3. Artifacts and limitations of slice-based depth of field

Layered (slice-based) depth of field goes back to Scofield's 2.5D method [7, 1992] and was
analysed by Barsky et al. [13, 2003], [15, 2005]. For each known artifact: is ZDefocusCImg
affected?

### 3.1 Discretization (banding between slices)

If all pixels of a slice get the same blur, a continuous depth gradient (a floor, a wall seen at
an angle) gets kernel diameter steps of 2*Max/n pixels: 5 pixels with the defaults (Maximum
Size 50, 20 slices). The eye is most sensitive to these steps near focus, where the relative
blur change is largest.

*Handled* by sharing each pixel between its two neighbouring slices (6.2): its blur is a blend
of the two slice blur sizes, which changes continuously with depth. On a disparity ramp with a
blur from 0 to 40 pixels (disc, 10 slices), the largest step of the residual contrast between
adjacent columns goes from 0.038 (hard slices) to 0.0036. The remaining error is that a blend of
two discs is not the disc of the intermediate size (its edge is softer), which is relatively
larger near focus; more slices reduce it at a linear cost. The final normalization also removes
the dark seams ("cracks") that unnormalized layered methods show [15, 2005].

### 3.2 Partial occlusion (missing background behind a blurred foreground)

A blurred foreground object should be semi-transparent near its edges, showing the background
behind it. A single image does not contain this hidden background.

Take a blurred foreground (slice k > 0) over a sharp background (slice 0). The background slice
has a hole where the foreground is. Without a fill, inside the foreground silhouette near its
edge the background contributes nothing (blur 0, mask 0), so the coverage there is the
foreground's blurred mask a < 1, and the final division makes the pixel pure foreground color.
Just outside the silhouette the coverage is 1 and the pixel is a*F + (1 - a)*B. With a ~ 0.5 on
both sides of the silhouette, the output jumps from F to (F + B)/2: the blurred foreground has a
hard inner edge, and its blur only spreads outwards. The effect is weaker when the background is
also blurred (its hole is partly filled by its own blur). This is the "partial occlusion"
problem [13, 2003], [15, 2005], [26, 2009], [31, 2012], [69, 2022]; the "fill foreground" option of Foundry's ZDefocus
targets it [87, 2026].

*Handled* by Fill Background (on by default, 6.3): before slice k > 0 is merged, the part of the
result that it covers but that is not covered yet gets an estimate of the hidden background: the
source pixels behind slice k that are not blurrier than it, blurred with slice k's kernel (in
the same pass as the slice) and normalized by their blurred weight. The foreground is then
merged over a full background, and fades out on both sides of its silhouette. The background
pixels that are blurrier than slice k are left out of the estimate: blurred with the smaller
kernel of slice k, they would show as a ring of sharp texture around the foreground. Where the
background around is only partly known, the fill is partial, and the final normalization by the
coverage fills the rest with the visible background, as without the fill. The threshold on the
slice mask scales with the kernel area, so that a foreground speck of one pixel is filled too. In the example above (a 20-pixel disc blur), the
largest step between adjacent pixels of the profile goes from 0.53 to 0.065 of the foreground
to background difference, the slope of the blur itself.

Limitations of the estimate: it is the background blurred at the foreground's blur size, not at
its own (a background slightly less blurred than the foreground shows a slightly sharper texture
through the foreground's fringe); a background blurrier than the foreground is not filled; the
fill only reaches as far into the hole as the kernel (which is where the foreground is
semi-transparent); it can show smeared texture; and the slices behind the focal plane are not
filled (their holes are behind sharper, nearer slices, and are already filled by their own blur).
The estimate only reads the source within the slice's kernel, so the render window padding does
not change.

### 3.3 Intensity leakage and halos at depth edges

Gather methods that blur the color image with a per-pixel kernel let a sharp foreground leak
into the blurred background (a halo of foreground color), or a blurred background bleed over the
sharp foreground [17, 2004], [23, 2007], [21, 2007].

*Not affected* in the usual cases. A sharp foreground is in another slice than the blurred
background, so its color never enters the background blur, and the normalization by coverage
fills the background's hole with background colors only (no dark halo). A background is always
composited under nearer slices, so it cannot bleed over them. The remaining issue is the one of
3.2 (the hidden background is estimated by the visible background around it, which is what most
methods do).

### 3.4 Image borders

*Handled.* Every blur is a normalized convolution over the processed rectangle (the "ones"
channel), so the borders are neither darkened nor brightened. The bokeh of a highlight near the
border is renormalized (it is cut, and its visible part gets brighter), which is a reasonable
choice for a full-frame plate. The output RoD is the Source RoD, so an element with a tight RoD
over transparency does not fade out beyond it (#266).

### 3.5 Non-premultiplied inputs and alpha

The Source is converted to premultiplied and the output is unpremultiplied back. *Handled.* The source alpha is blurred like any color channel, which is right
for a premultiplied image.

But the slice masks ignore alpha: a transparent pixel occludes the slices behind it like an
opaque one. In a single flattened image this does not matter (there is nothing behind a pixel),
but on a CG element with transparent areas and a depth value there, transparent pixels at a near
depth cut holes in the farther slices of the same element, which are then refilled by the
normalization.

### 3.6 Depth-map aliasing and depth/color misalignment

Each pixel has a single depth. At a depth edge an antialiased depth map gives the edge pixels an
intermediate depth, so they go to an intermediate slice and form a thin outline with an
intermediate blur. Even with a non-antialiased depth map, the antialiased color edge pixels mix
foreground and background colors but belong to only one slice, so a fringe of foreground color
gets the background blur, or the opposite. Estimated depth maps (stereo, monocular) are usually
misaligned with the color edges by a few pixels, and often blurry.

*Affected*, as all single-layer methods. Foundry's ZDefocus documentation recommends a non-antialiased depth map
[87, 2026]. Mobile portrait-mode pipelines refine the depth with edge-aware filtering and
matting before rendering [51, 2015], [54, 2018].

### 3.7 Highlights and bokeh

Bright bokeh discs only appear if the input is scene-linear and not clipped (the plug-in works in
float, so HDR input works). There is no bloom or gamma option for display-referred input, as
Foundry's ZDefocus has [87, 2026]. With the Gaussian aperture there are no bokeh shapes at all.

### 3.8 Other limitations

- The kernel is the same over the whole slice: no cat's-eye or vignetting, no lens aberrations
  [33, 2013].
- The focus is a single pixel sample (1.5, item 3).
- No mask input, no mix, no "focal plane setup" view (Foundry's ZDefocus has these [87, 2026]).
- Pixels outside the Depth RoD have the value 0 (1.3): with a depth or disparity map smaller than
  the Source, the pixels outside it get the blur at infinity.

## 4. Bibliographical review

Every reference in section 9 was checked online (Crossref, arXiv, the ACM Digital Library, the
publishers' or authors' pages) on 2026-11-23. Entries with an incomplete check are marked
"(partially verified)" in the list, with what is uncertain. The short notes on each method come
from the abstracts and from our reading; check the papers before quoting details. Within each
topic, the methods are listed in chronological order; the order across bullets is by topic.

### 4.1 Foundations

- **Thin lens and post-process blur.** Potmesil and Chakravarty introduced the lens and aperture
  camera model and the first post-process depth of field: each pixel of a pinhole rendering is
  spread by its circle of confusion (a scatter) [1, 1981], [2, 1982]. It is the model of
  section 1; it has no partial occlusion and leaks intensity.
- **Ground truth.** Distributed ray tracing samples the lens and gets visibility right
  [3, 1984]; the accumulation buffer averages renderings from many lens positions [4, 1990].
  Both are references for what an image-space method approximates; neither applies to a single
  image plus depth.
- **Compositing and normalization.** The "over" operator and premultiplied alpha [5, 1984] are
  the merge of section 2; dividing a blurred signal by the blurred certainty (our "ones" and
  coverage channels) is the normalized convolution of Knutsson and Westin [6, 1993].

### 4.2 Layered (slice-based) image-space methods: our family

- Scofield splits the image into depth layers, blurs each with an FFT and composites them back
  to front [7, 1992]: the direct ancestor of ZDefocusCImg.
- Barsky and colleagues surveyed camera models and image-based techniques [11, 2003], [12, 2003]
  and analysed the two artifacts of layered methods [13, 2003], [15, 2005]: *discretization*
  (seams and dark or transparent bands between depth slices, 3.1) and *occlusion* (missing
  hidden background, 3.2), with fixes by object identification and by extending slices under
  their occluders. Their vision-realistic rendering uses the same depth-sliced blur with measured
  eye PSFs [14, 2004]. Barsky and Kosloff later surveyed DOF algorithms [16, 2008].
- Kraus and Strengert split the image into sub-images by depth with smooth matting functions,
  blur them with a pyramid (down- and up-sampling) normalized by alpha, fill the disoccluded
  regions by pyramidal interpolation, and composite them [21, 2007]. It is the closest published
  relative of our design; it already contains the two improvements 6.2 (smooth assignment) and
  6.3 (hole filling).
- Kosloff, Tao and Barsky spread rectangles in constant time per pixel on layered scenes
  [27, 2009]. Schedl and Wimmer render separate depth layers and blur them with recursive
  Gaussians to solve partial occlusion [31, 2012], [32, 2013]; Selgrad et al. filter depth-peeled
  layers [38, 2015]; Franke et al. splat on multiple layers with tiles [43, 2018]. These assume
  a renderer that can output hidden layers, which a compositor rarely has.

### 4.3 Gather and scatter on a single layer

- **Gather** (each output pixel averages its neighbours): Rokita used repeated small
  convolutions [8, 1993], [9, 1996]; Demers' survey classifies the approaches (ray tracing, accumulation
  buffer, layered, forward-mapped "scatter" and reverse-mapped "gather" z-buffer) [17, 2004]; Hammon
  blends sharp, small-blur and large-blur images with a smoothed near-field CoC [23, 2007]; Zhou et al.
  weight a separable gather by depth to reduce leakage [22, 2007]; Lee et al. gather from
  anisotropically filtered mipmaps [25, 2009].
- **Scatter** (each input pixel spreads its CoC): Shinya's ray distribution buffer stores
  per-pixel lens-ray visibility to fix the visibility errors of post-filtering [10, 1994]; Lee,
  Kim and Choi splat points on per-pixel layers [24, 2008]; Leimkühler et al. splat sparse
  Laplacian kernels and integrate, at a constant cost per pixel [42, 2018].
- **Scatter-as-gather in games**: separate near and far fields, tile-based maximum CoC,
  dilation, and gathering only neighbours whose CoC reaches the pixel [34, 2013], [35, 2014], [40, 2018]; a single-pass spiral gather [41, 2018]. These are fast and smooth but use
  heuristics for occlusion; the hidden background of the near field is guessed from neighbours.
- **Bokeh shapes**: polygonal apertures with separable or skewed box passes [30, 2012], spectral
  bokeh with lens stops and aberrations [33, 2013], variable circular kernels [36, 2014], low-rank separable
  approximations of arbitrary kernels [37, 2015], and circular bokeh with complex separable kernels
  [39, 2017].

### 4.4 Diffusion

Depth of field as heat diffusion with a depth-dependent conductivity avoids leakage across depth
edges: Bertalmío et al. [18, 2004], Kass, Lefohn and Owens with a GPU tridiagonal solver
[19, 2006], and Kosloff and Barsky for generalized focus surfaces [20, 2007]. Diffusion gives a
Gaussian-like blur, no bokeh shapes.

### 4.5 Multiview, light fields and layered representations

- Lee, Eisemann and Seidel ray-trace a layered depth image from many lens samples, which gives
  partial occlusion close to the accumulation buffer [26, 2009], and extend it to lens aberrations
  and focus control [28, 2010]. Yu, Wang and Yu synthesize and filter a light field from an image
  and its depth [29, 2010].
- Light-field refocusing gives correct partial occlusion with real data [45, 1996], [47, 2000], [48, 2005]; the Lumigraph introduced the pull-push hole filling we suggest in 6.3 [44, 1996].
- Layered depth images keep several surfaces per pixel [46, 1998]. Multiplane images (RGBA planes
  evenly spaced in inverse depth, composited with "over") [55, 2018], [56, 2019] are exactly our
  slice structure; Mildenhall et al. also give sampling guidelines for the number of planes in
  disparity. Shih et al. inpaint color and depth behind occluders in a layered depth image
  [59, 2020]. Telea's fast-marching inpainting [50, 2004] is a cheap classical alternative.

### 4.6 Computational photography: synthetic defocus from depth or stereo

- Bae and Durand magnify the existing defocus of a photograph from an estimated blur map
  [49, 2007].
- Google's Lens Blur computes edge-aware stereo depth in bilateral space [51, 2015]; Portrait
  Mode on Pixel phones combines person segmentation and dual-pixel depth with a fast physically
  inspired renderer [54, 2018]. Both show how much depth refinement at edges matters (3.6).
- Hach et al. (ARRI) render cinematic bokeh on live-action plates with depth and measured lens
  kernels [52, 2015]: the most production-oriented reference. Yang et al. synthesize dynamic depth
  of field on mobile [53, 2016]; Zhang et al. render synthetic defocus on RGB-D video and choose
  the focus with a learned look-ahead [57, 2019]; Busam et al. refocus from stereo with depth
  planes [58, 2019].
- Monocular depth estimators [84, 2022], [85, 2024], [86, 2024] now make depth available for any
  plate, but their output is relative inverse depth (unknown scale and shift), see 1.3.

### 4.7 Learning-based bokeh

- **End-to-end networks without depth**: the AIM 2019 challenge [63], the EBB! dataset and
  PyNET [64, 2020], the AIM 2020 challenge [65], DMSHN [66, 2021], the Mobile AI 2022 challenge [70], and a
  vision transformer preprint [83, 2022]. **With depth**: DeepLens (depth estimation, lens blur network
  and super-resolution) [60, 2018], DeepFocus for near-eye displays [61, 2018], dense dynamic filtering [62, 2019],
  defocus and radiance priors [71, 2023], lens-to-lens transformation [72, 2023], [73, 2023].
- **Classical plus neural**: Peng et al.'s interactive portrait bokeh system [67, 2021]; the same
  group's BokehMe renders with a classical scatter driven by disparity (blur = K*(d - d_f), the
  Disparity with Offset type of 1.3) and fixes the depth edges with a network [68, 2022], [75, 2025].
- **Layered with inpainting**: MPIB builds a multiplane image from one image and its disparity,
  inpaints the hidden layers, blurs each and composites them [69, 2022]; Dr.Bokeh is a
  differentiable occlusion-aware layered renderer with background inpainting and explicitly
  targets color bleeding and partial occlusion [74, 2024]. These are our method plus 6.3 with a
  learned inpainting.
- **Diffusion models** (2023-2026): generative bokeh [76, 2023], defocus control in text-to-image
  generation [77, 2025], one-step diffusion lens blur with a CoC constraint [78, 2025], video
  refocusing [79, 2025], controllable apertures [80, 2025], efficient diffusion bokeh [81, 2026],
  and any-to-any bokeh editing [82, 2026]. They give the most realistic partial occlusion today,
  but they hallucinate content, are not deterministic or tile-able, and their licenses are often
  non-commercial: not suitable for an OFX plug-in, but useful as quality references.

## 5. Open-source implementations

Licenses were read from the repositories (LICENSE files or SPDX headers) on 2026-11-23.
openfx-misc is GPLv2-or-later: MIT, BSD, CeCILL (v2.1), CeCILL-B and GPL-2.0-or-later code can
be copied; Apache-2.0, LGPL-3.0+ and GPL-3.0 code would force the combined work to GPLv3, so
reimplement from the idea instead; non-commercial or unlicensed code cannot be used. Ideas from
papers and talks can always be reimplemented.

| Implementation | License | Method | What we can learn or reuse |
|---|---|---|---|
| Blender compositor Defocus node (`source/blender/nodes/composite/nodes/node_composite_defocus.cc`, `compositor/shaders/compositor_defocus_*.glsl`, `cached_resources/intern/bokeh_kernel.cc`), https://projects.blender.org/blender/blender | GPL-2.0-or-later | Per-pixel variable-size gather. Radius from depth with the Potmesil formula (f-stop, focal length, sensor), or Z used directly as a radius times a scale. A neighbour contributes only if the offset is within min(own radius, neighbour radius); weights from the bokeh kernel image, flipped; normalized. The radius map is morphologically filtered first. | The min rule prevents leakage both ways, but it also prevents a blurred foreground from spreading over a sharp background (no partial occlusion): our layering is better there. Reusable: the physical CoC formula (a possible lens-based mode, #266) and the bokeh kernel generator (blades, rotation, roundness, catadioptric, lens shift). |
| Blender EEVEE depth of field (`source/blender/draw/engines/eevee/eevee_depth_of_field.cc` and shaders) | GPL-2.0-or-later | After [40, 2018]: half resolution, min/max CoC tiles with dilation, separate foreground and background gathers, hole-fill gather for foreground disocclusion, median filter, scatter of bright pixels only, bokeh LUT. | Scatter of highlights only, on top of a cheaper blur, for clean bokeh shapes. |
| G'MIC `fx_blur_dof` "Blur [Depth-of-Field]" (`src/gmic_stdlib.gmic`), https://github.com/GreycLab/gmic | CeCILL-C or CeCILL 2.1 | Depth quantized into N levels, incremental Gaussian blurs (sigma_new^2 - sigma_old^2), each level weighted by a softened mask into an accumulator divided by the weights. No occlusion ordering. | Same normalization idea; incremental blurs do not apply to our slices (each slice has its own mask). |
| G'MIC community `fx_tk_dof` "DOF Manipulation" (`include/tom_keil.gmic`), https://github.com/GreycLab/gmic-community | CeCILL-B | Focus mask from a depth threshold, background inpainted behind the foreground before blurring, highlight boost. | Inpainting behind the foreground (6.3). |
| natron-plugins `Filter/zDefocus` PyPlug, https://github.com/NatronGitHub/natron-plugins | per-PyPlug, not verified | Node graph: Keyer depth bands, blurs and merges, three layers (front, focus, back). | A crude version of our method; ZDefocusCImg supersedes it. `Filter/Defocus` is CC BY-NC-SA (not usable). |
| GEGL `gegl:lens-blur` (`operations/common-cxx/lens-blur.cc`), `gegl:variable-blur`, `gegl:focus-blur` (GIMP Focus Blur) | LGPL-3.0-or-later | Lens blur: a scatter of discs with a per-pixel radius from a mask, weights increased exponentially for highlights. Variable blur: log2(R)+3 Gaussian levels spaced with a gamma, blended per pixel. No depth ordering. | Exponential highlight weighting, level spacing with a gamma. Ideas only (LGPL-3). |
| darktable `src/iop/blurs.c` | GPL-3.0-or-later | Uniform blur with a parametric lens kernel (blades, concavity, linearity, rotation), direct convolution. No depth. | The closed-form polygon kernel; reimplement (GPL-3). |
| Krita `plugins/filters/blur/kis_lens_blur_filter.cpp` | GPL-2.0-or-later | Uniform polygonal iris kernel. No depth. | Little. |
| ImageMagick `-compose Blur` | ImageMagick license (Apache-2.0 style) | Variable elliptic Gaussian gather from a map. | Little. |
| BokehMe, https://github.com/JuewenPeng/BokehMe (`classical_renderer/scatter.py`) | Apache-2.0 | CUDA scatter of soft discs, weight (0.5 + 0.5*tanh(4*(r - dist)))/(r^2 + 0.2), color and weight accumulated and divided; blur = K*(d - d_f); gamma round trip for highlights; dilated defocus map to find the edges given to the network. | The offset-free disparity mode (1.3), the gamma highlight trick, the dilated defocus map for edge handling. Ideas only for GPLv2. |
| MPIB, https://github.com/JuewenPeng/MPIB | Apache-2.0 | Multiplane image with learned inpainting of hidden layers, then layered rendering [69, 2022]. | Confirms 6.3 as the main quality lever. |
| Dr.Bokeh, https://github.com/ShengCN/DrBokeh-Src | no license (all rights reserved) | Differentiable occlusion-aware layered rendering [74, 2024]. | Ideas only. |
| DeepFocus (facebookresearch), PyNET-Bokeh (aiff22) | CC BY-NC 4.0, CC BY-NC-SA 4.0 | Neural networks. | Not usable. |
| Godot `servers/rendering/renderer_rd/shaders/effects/bokeh_dof.glsl`, https://github.com/godotengine/godot | MIT | Box and hexagon modes with separable depth-aware passes; circular mode with the golden-angle spiral gather [41, 2018], sample sizes clamped relative to the center. | A fast CPU preview for draft renders instead of the Gaussian (copyable). |
| Filament `filament/src/materials/dof/`, https://github.com/google/filament | Apache-2.0 | Background ring gather with ring occlusion merging [40, 2018], foreground two-layer accumulation [35, 2014], CoC tiles, dilation, median. | Reference for a gather method; ideas only. |
| three.js `examples/jsm/shaders/BokehShader2.js` | MIT | Ring-sample gather with f-stop, focal length, highlight threshold and gain, fringe. | Highlight threshold and gain. |
| O3DE `Gems/Atom/Feature/Common/Assets/Shaders/PostProcessing/DepthOfField*.azsl` | Apache-2.0 or MIT (user's choice) | Game DOF (not inspected). | Usable under MIT if needed. |
| Unity HDRP/URP, Unreal Diaphragm DOF | Unity Companion License; Epic EULA | Not open source. | Use the public talks only [40, 2018]. |

Not found (by file-name search, so not exhaustive): depth-based defocus in openfx-arena,
openfx-io, openfx-gmic, RawTherapee, ART, digiKam, Olive, MLT, frei0r, Kdenlive and OpenCV.
No public code was found for SteReFo [58, 2019] or DeepLens [60, 2018].

Summary: the open-source compositors (Blender) use per-pixel gathers that do not handle partial
occlusion; the research code that does (MPIB, Dr.Bokeh) is layered like ours and adds learned
inpainting. Nothing in the GPLv2-compatible code is a better drop-in for our layered method;
the reusable pieces are Blender's physical CoC formula and bokeh kernel generator (GPL-2.0+)
and Godot's spiral gather (MIT).

## 6. Improvements made before the first release

These improvements of #266 were made in place, at version 1.0, since the plug-in was not
released yet. After a release, a bug fix that changes the output gets a minor version bump, and
a change of a legitimate output a new major version (or a new option whose default keeps the
old output). Further improvements are listed in #266.

### 6.1 Blur model

The blur follows the documented formula (1.3): one formula for depth and disparity maps, for
every focus value, and a Depth of Focus that is only a dead zone. With no Depth of Focus, the
output is the thin-lens formula Size*|z_f/z - 1|.

### 6.2 Soft (two-slice) assignment: no banding at the same number of slices

Each pixel has a continuous slice coordinate q = blur*n/Max in [-n, n], with k = floor(q) and
t = q - k. It is *fully* (mask 1) in slice k, which is the farther of the two in compositing
order, and with mask t (and premultiplied color t*C) in slice k + 1. Because slice k + 1 is
composited over slice k, the result at that pixel is t*C + (1 - t)*1*C with coverage
t + (1 - t) = 1: an exact linear blend of the two blur sizes, so the blur changes continuously
with depth. This is the spirit of the smooth matting functions of Kraus and Strengert [21, 2007] and
of interpolating between neighbouring layers in layered methods [31, 2012], [38, 2015].

- Gain: no banding (3.1); fewer slices are needed for the same quality.
- Cost: the same number of blurs, but each slice's bounding box grows (about twice as wide on a
  depth gradient): see 2.2 for the measured times.

### 6.3 Hole filling of the background before each nearer slice

Before slice k > 0 is composited over the accumulated result, the parts of the result that slice
k covers and that are not fully covered yet are filled with an estimate of the hidden background
(3.2). The estimate is a normalized convolution, with slice k's kernel, of the source pixels
behind slice k that are not blurrier than it. These pixels are blurred in the same pass as the
slice (C + 1 more channels), so there is no extra blur call. The estimate only reads source
pixels within the slice's kernel, so the render window padding does not change, and a tile is
the same as the full render (up to FFT rounding with large custom kernels). The blurred
foreground is then composited over a plausible background, which removes the hard inner edge of
3.2 and lets the foreground become transparent near its edges. This is what layered methods with
"hidden surface" approximations do [24, 2008], [26, 2009], [21, 2007], [31, 2012], and what the "fill foreground" option
of Foundry's ZDefocus documents: "When enabled, Nuke attempts to compensate for missing
information by filling regions in the foreground which are revealed when the foreground goes
out of focus" [87, 2026]. Like that option, Fill Background can be switched off, since the filled background can show false detail.

An alternative is a normalized convolution (or a pull-push pyramid [44, 1996], [21, 2007]) of the
accumulated (color, coverage) result instead of the source: the fill would have the blur of the
visible background, but it reads the result up to two kernel half-sizes away, which needs twice
the render window padding to stay tile-consistent.

### 6.4 Gaussian and disc kernels

The Gaussian aperture of blur radius r has sigma = r/sqrt(8), truncated to |dx|, |dy| <= r. With
this sigma, the Gaussian at the radius is exp(-4) = 1.8% of its peak, so the truncation, which
gives the Gaussian the support of the disc (and an exact tile: no infinite tails), changes it
little. It spreads less than the disc (0.72 to 0.76 times its standard deviation), so draft and
proxy renders, which always use it, look slightly sharper than a Disc render. It is a separable
direct convolution for small kernels, and an FFT of the same kernel for large ones.

The disc gives a pixel at a distance d from its center the weight clamp(r - d, 0, 1): a linear
antialiasing over the last pixel inside the radius, so that the disc never reaches beyond its
radius and its kernel fits the padding of half the diameter; a radius of 1 pixel or less is
sharp.

### 6.5 Depth types: Direct and Disparity with Offset

- **Direct**: blur radius = Size*|v - focus|, larger values farther; the slices are evenly
  spaced in this value.
- **Negated Depth** and **Negated Disparity**: the Depth and Disparity types of -v.
- **Disparity with Offset**: blur radius = Size*|d - d_f|, Size in pixels per disparity
  unit. Exact for stereo disparity with any convergence offset and for affine-invariant inverse
  depth from monocular estimators [84, 2022], [85, 2024], and continuous at d_f = 0.

## 7. Mapping onto Foundry's ZDefocus

ZDefocusCImg's parameters follow the definitions of Foundry's ZDefocus documentation [87, 2026]
(https://learn.foundry.com/nuke/content/reference_guide/filter_nodes/zdefocus.html); the
quotes below are from that page. The algorithm itself is not documented beyond the controls.

### 7.1 Math (depth interpretation)

| math (documented) | Meaning (quoted) | ZDefocusCImg |
|---|---|---|
| direct | "The Z value in the depth channel directly controls blur." | Depth Type = Direct; subtracting the focus (radius Size*\|v - focus\|) is our extension: with a focus of 0, the size is multiplied by the depth value |
| depth | "The Z value in the depth channel is the distance between the camera and whatever is in the image at that pixel." | Depth Type = Depth |
| far = 0 | "The Z value in the depth channel is equal to 1/distance." | Depth Type = Disparity |
| far = 1 | "Near plane = 0, far plane = 1." | missing (#266) |
| -direct | "As with the direct mode, the Z value in the depth channel directly controls blur." | Depth Type = Disparity with Offset (larger values in front) |
| -depth | "The Z value in the depth channel is -distance in front of the camera." | Depth Type = Negated Depth |
| far = -0 | "The Z value in the depth channel is equal to -1/distance." | Depth Type = Negated Disparity |
| far = -1 | "Near plane = 0, far plane = -1." | missing (#266) |

### 7.2 Controls

| Control (documented) | Definition (quoted) | ZDefocusCImg |
|---|---|---|
| size | "Sets the size of the blur at infinite depth. Blur nearer the camera than the focus plane may be larger." | Size, a blur radius (the kernel diameter is 2*Size) |
| maximum | "The filter size is clipped at this maximum value." | Maximum Size, a radius |
| focus plane (C) | "Sets the Z depth of areas in the image that are entirely in focus." | Focus |
| focal point | "Controls the position of the focal point widget in the Viewer." | Focus Point, used if Use Focus Point is on |
| depth of field | "Sets a depth slice around the focus plane that is entirely in focus." | Depth of Focus (1.4) |
| blur inside | "When enabled, a small amount of blur is applied to the in-focus region." | Blur Inside (1.4) |
| filter type, filter shape | filter type "disc", "bladed" or "image"; filter shape "Dissolves the filter shape between Gaussian at 0 and disc at 1." | Aperture: Gaussian (filter shape 0), Disc (filter shape 1), Custom (image) |
| fill foreground | "When enabled, Nuke attempts to compensate for missing information by filling regions in the foreground which are revealed when the foreground goes out of focus." | Fill Background (3.2, 6.3) |
| depth layers, automatic layer spacing, layer curve | "The number of depth layers to use for the blur."; "ZDefocus automatically works out how many depth layers to use, based on the maximum blur size"; "The curve to apply to the layer spacing." | Focus Slices on each side, evenly spaced in blur size, each pixel shared by two slices |

### 7.3 Defaults

ZDefocusCImg uses the defaults documented on Foundry's page [87, 2026]:

| Control | Documented default | ZDefocusCImg default |
|---|---|---|
| math / Depth Type | depth | Depth |
| focus plane (C) / Focus | 0 | 0, Use Focus Point off |
| depth of field / Depth of Focus | 0 | 0 |
| blur inside / Blur Inside | enabled | on |
| size / Size | 25 | 25 |
| maximum / Maximum Size | 50 | 50 |
| filter type, filter shape / Aperture | disc, filter shape 0: a Gaussian ("between Gaussian at 0 and disc at 1") | Gaussian |
| depth layers / Focus Slices | 20 (automatic layer spacing enabled) | 20 on each side |
| fill foreground / Fill Background | enabled | on |

### 7.4 What we lack

- **far = 1** and **far = -1** math (#266).
- **Layer control**: automatic spacing and a layer curve.
- **Filters**: a disc-to-Gaussian blend (filter shape), a built-in bladed iris (blades,
  roundness, rotation, inner size/feather/brightness, catadioptric), an aspect ratio, and a
  resampling filter choice for image kernels. We have a disc, a Gaussian and an image kernel; the
  bladed iris can be made with the DiscBlades generator into the Filter input.
- **Outputs**: focal plane setup, layer setup, filter shape setup.
- **gamma correction and bloom** for display-referred input.
- **mask**, **mix**, **GPU**.

What we have that the documentation does not describe: normalized convolution at the image
edges, the coverage normalization that avoids dark halos behind in-focus foregrounds, a
large-kernel FFT path, and a disparity mode for maps with an unknown offset.

## 8. Faster methods with equal or better realism

This section asks which published methods look at least as good as ours and run much faster. Our
realism criteria are partial occlusion, no banding, bokeh shape and highlights.

Our reference speed (#269) uses 1920x1080 RGBA, 10 slices, Fill Background on and no Blur Inside,
on an 18-core Apple M5 Pro shared with other jobs. One frame takes:

- Gaussian aperture: 0.9 to 1.3 s, for a maximum radius of 25 to 200 pixels (the FFT keeps it
  flat);
- Disc aperture: 2.6 s at radius 25, 5.7 s at 50, 17 s at 100 and 42 s at 200.

The published speeds below come from the papers or slides cited. Each is on its own hardware,
and none was re-measured here. "Partially verified" marks a speed or detail that we could not
read in the source (paywalled, or not stated there).

### 8.1 Tiled scatter-as-gather (game engines)

- **Methods:** Sousa (2013) [34], Jimenez (2014) [35] and Abadie (2018) [40].
- **How they work:** the image is split into a near and a far field. A tile pass finds the
  minimum and maximum CoC, and a dilation pass spreads them. Each pixel then gathers only the
  neighbours whose CoC reaches it, on a ring or spiral pattern at half resolution. Abadie adds a
  scatter of the bright pixels (highlights), scatter occlusion, hole filling and a bokeh lookup
  table (blade count and shape).
- **Quality compared with ours:**
  - No banding: the CoC is continuous.
  - Bokeh shape and highlights are better: any shape, and bright pixels are scattered as sprites.
  - Partial occlusion is about equal. The background hidden by a blurred foreground is guessed
    from neighbours, as our Fill Background does (6.3).
  - The weak points are the half-resolution gather and the sparse taps (49 taps in Jimenez's
    talk). At large radii these need median and reconstruction filters, so fine detail and exact
    kernel energy are lost. Halos at depth edges are handled by heuristics.
- **Published speed** (GPU):
  - Abadie: 1.59 ms (fast 2-ring gather) to 5.50 ms (4 rings with foreground and background
    scattering, hole filling and bokeh simulation), on a PS4 Pro at 1080p (slides 134-148).
  - Jimenez: 1.27 ms on Xbox One (slide 132). Partially verified: the resolution is not stated.
  - Sousa: 0.52 ms for the reconstruction filter only, at 1080p on an AMD 7970 (slide 54).

  That is two to three orders of magnitude faster than our CPU render, on game-console GPUs.
- **CPU or GPU:** GPU. A CPU port is possible, because the cost is a fixed number of taps per
  pixel. However, the quality depends on the GPU-specific half-resolution and median passes.
- **OFX fit:**
  - Tiles and render scale are easy: the gather is local, so a tile needs a margin of the maximum
    CoC.
  - GPU rendering through the OFX OpenGL render suite (`kOfxImageEffectPropOpenGLRenderSupported`
    = "true", with the CPU path kept as the fallback) works in Natron.
  - Resolve does not call OpenGL render (README-hosts.txt). It would need the CUDA, OpenCL or
    Metal render properties of `ofxGPURender.h`, which are separate code paths.
- **Effort:** large, about 3 to 5 weeks for a full-quality port, including the CPU fallback and
  tests.
- **Licences:**
  - Blender EEVEE (GPL-2.0-or-later) follows Abadie and can be copied.
  - O3DE (Apache-2.0 or MIT, at the user's choice) uses a ring gather and can be used under MIT.
  - Filament (Apache-2.0) follows Abadie: ideas only, see section 5.

### 8.2 Separable and low-rank bokeh kernels

- **Methods:** McIntosh, Riecke and DiPaola (2012) [30] build polygonal apertures from
  separable or skewed box passes. McGraw (2015) [37] uses low-rank separable approximations
  of arbitrary kernels. Garcia (2017) [39] builds circular bokeh from a few complex
  separable kernels. The Bevy engine has a two-pass hexagonal blur.
- **Quality:** same as the kernel they replace. They change only how the slice kernel is
  computed, not the occlusion model, so they fit inside our layered method.
- **Speed:** no published timing.
  - McIntosh et al. only state "much better frame rates than a naive non-separable approach".
  - Garcia says "magnitudes faster than brute-force".
  - McGraw's timing is partially verified: the paper is paywalled.
- **What it buys us:** little on the CPU. Our disc is already O(s) per pixel (row prefix sums,
  2.1), the same order as a separable pass. These kernels matter mainly for the GPU (8.5) and for
  shaped apertures without the FFT.
- **Licences:**
  - Garcia's kernel generator is MIT (github.com/kecho/CircularDofFilterGenerator).
  - Bevy is MIT or Apache-2.0 (`crates/bevy_post_process/src/dof/`).
  - Effort to add one kernel type: 2 to 4 days.

### 8.3 Summed-area tables and constant-time spreading

- **Methods:**
  - Crow (1984) [88] introduced summed-area tables.
  - Hensley et al. (2005) [89] build them on the GPU.
  - Kosloff, Tao and Barsky (2009) [27] spread rectangles in constant time per pixel on
    layered scenes, with partial occlusion between layers.
- **Quality:** layered like ours, so partial occlusion and no banding are the same. The basic
  kernel is a rectangle. A round or polygonal bokeh needs several rectangles, or a sum of boxes
  that approximates the shape.
- **Published speed:** Kosloff et al. report 45 frames per second, with two layers at 800x600 on
  an ATI HD4870 (p. 43). Their CPU timings are relative only. Hensley et al.'s demonstrations and
  timings are partially verified: the paper could not be read.
- **CPU or GPU:** both.
- **Fit and effort:** small, about 1 week. A 2D summed-area table per slice makes a box blur O(1)
  per pixel. Three boxes approximate a Gaussian well, but the disc would lose its round shape.
  Double precision, or a per-tile offset, is needed to keep the sums accurate on large frames.
- **Licence:** no code is needed.

### 8.4 Multi-layer GPU methods (most need the 3D scene)

The methods in the list below need the hidden layers or the geometry that a renderer can output
(depth peeling, rasterization), so they cannot run on a single image plus depth. They show what is possible when a renderer provides those
layers:

- Lee, Eisemann and Seidel (2010) [28]: ray tracing of a layered image with lens
  aberrations, 10 to 31 ms at 800x600 on a GTX 285, with 100 lens rays and 4 layers (Table 1).
- Schedl and Wimmer (2012) [31]: 98 to 102 ms at 1024x1024 on a GTX 480, of which 40 to
  46 ms is depth peeling (Table 1). The blur is Gaussian.
- Selgrad et al. (2015) [38] and Franke et al. (2018) [43]: partially verified, no
  timing could be read.
- Jeong et al. (2022) [90]: 22 to 27 ms per frame at 1920x1080 on an RTX 2080 Ti, with
  lens aberrations and highlights. Each highlight is rasterized from the scene, with multi-layer
  pinhole images for the rest. Partially verified: read in the authors' preprint, not in the
  published version.

The OFX fit would need several image-and-depth layers as inputs; no current host provides that.

Leimkühler, Seidel and Ritschel (2018) [42] are the exception: their Laplacian kernel splatting
takes labeled pixels, from a single pinhole image with depth as well as from layered images,
and splats them into global depth layers that are composited back to front, as our slices are.
So it works on one image plus depth, with arbitrary PSFs (bokeh shapes, cat-eye, chromatic
aberration) and per-pixel splats, so there is no banding.

- Published speed: 65.9 to 163 ms for synthesis at 1024x1024 on a GTX 980 Ti (Table 2), in
  scenes that combine depth of field with motion blur. Each PSF needs about 20 s of
  precomputation.
- OFX fit: GPU only in the paper. Splats reach far, so a tile needs a margin of the maximum
  blur, as ours does.
- Effort: large, about 4 weeks.
- Licence: not checked (no code is needed to reimplement it).

### 8.5 A GPU port of our own method

Our method would move to the GPU through the OFX OpenGL render suite, with the CPU path kept as the
fallback. This keeps our model, so quality is the same, and our tests remain the reference.

- Per slice, the work would be separable passes (8.2) or GPU FFTs, then the coverage
  normalization and "over" compositing of 2.1.
- No published timing applies, and we have not measured one. 8.1 suggests that each blur pass
  costs a few milliseconds at 1080p, but that is not a measurement.
- Fit: the same as 8.1 for Natron and Resolve.
- Effort: about 2 to 3 weeks.
- Licence: our own code.

### 8.6 Neural and hybrid real-time bokeh

- **BokehMe (Peng et al., 2022)** [68]: a classical scatter plus a network for the depth
  edges.
  - Published speed (Table 1), on the 1920x1080 BLB dataset with a GTX 1080 Ti: 0.16 to 0.68 s
    for the full pipeline, and 0.03 to 0.52 s for the classical renderer alone, as the blur grows
    from 10 to 50.
  - Its later version, BokehMe++ [75, 2025], adds highlights and cat-eye bokeh. It has no
    published timing (partially verified: paywalled).
- **Dr.Bokeh (Sheng et al., 2024)** [74]: layered and occlusion-aware, with background
  inpainting. 19 ms on average at 256x256 with 2 layers and R = 21, in CUDA. Partially verified:
  the GPU is not stated.
- **MPIB (Peng et al., 2022)** [69]: no runtime in the paper; the supplementary material
  was not checked.
- **Mobile AI & AIM 2022 challenge** [70]: end-to-end networks without depth. The best
  run took 28.1 ms at 1024x1024 on the Mali GPU of a Kirin 9000, against 3512 ms for the PyNET-V2
  baseline (Table 1). These methods give no control over focus or aperture, so they cannot
  replace a depth-driven effect.
- **Seizinger et al. (2023)** [72]: no runtime in the paper.
- **Quality:** the layered ones (MPIB, Dr.Bokeh) fill the hidden background with learned
  inpainting, which is better than our Fill Background on large occluders.
- **OFX fit:** poor, because these methods need the following:
  - a network runtime (ONNX Runtime or LibTorch) and model weights in the bundle;
  - a whole-image context, so tiles give different results;
  - fixed training resolutions, so render scale changes the output;
  - GPU memory.
- **Licences:** BokehMe and MPIB code is Apache-2.0, ideas only for GPLv2. Dr.Bokeh has no
  licence.
- **Effort:** several weeks, plus packaging on three operating systems.

### 8.7 Recommendation

1. **Disc aperture through the FFT on large slices (CPU, small effort, same output up to float rounding).** This is
   not a new method but the largest measured gap. Our Gaussian already switches to the FFT and
   stays at about 1 s at every radius, while the direct disc grows to 42 s at radius 200. Giving
   the antialiased disc the Gaussian's FFT path (2.3), with the same cost switch, should bring
   the disc to the Gaussian's time at large radii. That is an estimate from the Gaussian timings,
   to be measured. The output is the same up to float rounding (tiles consistent to about 1e-6),
   and tiles, render scale and the exact model are kept. Effort: 2 to 4 days.
2. **A GPU path for our layered method (8.5),** through the OFX OpenGL render suite, with the CPU
   path kept as the fallback. Same quality, and Natron would use it; Resolve would need a
   separate CUDA, OpenCL or Metal path. Effort: 2 to 3 weeks.
3. **Highlight scatter on top of our slices,** taken from 8.1: scatter only the bright pixels as
   bokeh sprites (EEVEE, GPL-2.0-or-later). It improves the bokeh, the one area where the game
   methods are more realistic than ours. Effort: about 1 week.
4. **A full tiled scatter-as-gather (8.1)** only as a separate fast preview mode. It is faster on
   the GPU but less exact than our slices at large radii.
5. **Summed-area tables (8.3)** only for a box or Gaussian-like preview.
6. **Neural methods (8.6):** not for the plug-in. They are useful as quality references for
   hidden-background filling.

## 9. References

1. M. Potmesil, I. Chakravarty. A lens and aperture camera model for synthetic image generation. SIGGRAPH '81, Computer Graphics 15(3):297-305, 1981. https://doi.org/10.1145/965161.806818
2. M. Potmesil, I. Chakravarty. Synthetic image generation with a lens and aperture camera model. ACM Transactions on Graphics 1(2):85-108, 1982. https://doi.org/10.1145/357299.357300
3. R. L. Cook, T. Porter, L. Carpenter. Distributed ray tracing. SIGGRAPH '84, Computer Graphics 18(3):137-145, 1984. https://doi.org/10.1145/964965.808590
4. P. Haeberli, K. Akeley. The accumulation buffer: hardware support for high-quality rendering. SIGGRAPH '90, Computer Graphics 24(4):309-318, 1990. https://doi.org/10.1145/97880.97913
5. T. Porter, T. Duff. Compositing digital images. SIGGRAPH '84, Computer Graphics 18(3):253-259, 1984. https://doi.org/10.1145/964965.808606
6. H. Knutsson, C.-F. Westin. Normalized and differential convolution. Proc. IEEE CVPR 1993, pp. 515-523. https://doi.org/10.1109/CVPR.1993.341081
7. C. Scofield. 2 1/2-D depth-of-field simulation for computer animation. In Graphics Gems III (D. Kirk, ed.), Academic Press, 1992, pp. 36-38. https://doi.org/10.1016/B978-0-08-050755-2.50018-X (partially verified: the editor is not shown by the DOI record)
8. P. Rokita. Fast generation of depth of field effects in computer graphics. Computers & Graphics 17(5):593-595, 1993. https://doi.org/10.1016/0097-8493(93)90010-7
9. P. Rokita. Generating depth-of-field effects in virtual reality applications. IEEE Computer Graphics and Applications 16(2):18-21, 1996. https://doi.org/10.1109/38.486676
10. M. Shinya. Post-filtering for depth of field simulation with ray distribution buffer. Proc. Graphics Interface '94, pp. 59-66, 1994. https://doi.org/10.20380/GI1994.08
11. B. A. Barsky, D. R. Horn, S. A. Klein, J. A. Pang, M. Yu. Camera models and optical systems used in computer graphics: Part I, object-based techniques. ICCSA 2003, LNCS 2669, pp. 246-255. https://doi.org/10.1007/3-540-44842-X_26
12. B. A. Barsky, D. R. Horn, S. A. Klein, J. A. Pang, M. Yu. Camera models and optical systems used in computer graphics: Part II, image-based techniques. ICCSA 2003, LNCS 2669, pp. 256-265. https://doi.org/10.1007/3-540-44842-X_27
13. B. A. Barsky (and co-authors). Investigating occlusion and discretization problems in image-based blurring techniques. Vision, Video, and Graphics (VVG) 2003, Eurographics. https://doi.org/10.2312/vvg.20031013 (partially verified: the record lists only Barsky, as a keynote, while the paper is usually cited with M. J. Tobias, D. R. Horn and D. P. Chu; no pages)
14. B. A. Barsky. Vision-realistic rendering: simulation of the scanned foveal image from wavefront data of human subjects. Proc. APGV 2004, pp. 73-81. https://doi.org/10.1145/1012551.1012564
15. B. A. Barsky, M. J. Tobias, D. P. Chu, D. R. Horn. Elimination of artifacts due to occlusion and discretization problems in image space blurring techniques. Graphical Models 67(6):584-599, 2005. https://doi.org/10.1016/j.gmod.2005.01.009
16. B. A. Barsky, T. J. Kosloff. Algorithms for rendering depth of field effects in computer graphics. Proc. 12th WSEAS International Conference on Computers (ICCOMP'08), 2008, pp. 999-1010. (partially verified: found in OpenAlex only, no DOI)
17. J. Demers. Depth of field: a survey of techniques. In GPU Gems (R. Fernando, ed.), chapter 23, Addison-Wesley, 2004. https://developer.nvidia.com/gpugems/gpugems/part-iv-image-processing/chapter-23-depth-field-survey-techniques (partially verified: the editor is not on the page)
18. M. Bertalmío, P. Fort, D. Sánchez-Crespo. Real-time, accurate depth of field using anisotropic diffusion and programmable graphics cards. Proc. 3DPVT 2004, pp. 767-773. https://doi.org/10.1109/TDPVT.2004.1335393
19. M. Kass, A. Lefohn, J. Owens. Interactive depth of field using simulated diffusion on a GPU. Pixar Technical Memo #06-01, 2006. https://web.archive.org/web/2016/http://graphics.pixar.com/library/DepthOfField/ (partially verified: the Pixar page now redirects; confirmed from an archived copy and OpenAlex)
20. T. J. Kosloff, B. A. Barsky. An algorithm for rendering generalized depth of field effects based on simulated heat diffusion. ICCSA 2007, LNCS 4705, pp. 1124-1140. https://doi.org/10.1007/978-3-540-74484-9_99
21. M. Kraus, M. Strengert. Depth-of-field rendering by pyramidal image processing. Computer Graphics Forum 26(3):645-654, 2007 (Eurographics 2007). https://doi.org/10.1111/j.1467-8659.2007.01088.x
22. T. Zhou, J. X. Chen, M. Pullen. Accurate depth of field simulation in real time. Computer Graphics Forum 26(1):15-23, 2007. https://doi.org/10.1111/j.1467-8659.2007.00935.x
23. E. Hammon Jr. Practical post-process depth of field. In GPU Gems 3 (H. Nguyen, ed.), chapter 28, Addison-Wesley, 2007. https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-28-practical-post-process-depth-field (partially verified: the year and editor are not on the page)
24. S. Lee, G. J. Kim, S. Choi. Real-time depth-of-field rendering using point splatting on per-pixel layers. Computer Graphics Forum 27(7):1955-1962, 2008 (Pacific Graphics). https://doi.org/10.1111/j.1467-8659.2008.01344.x
25. S. Lee, G. J. Kim, S. Choi. Real-time depth-of-field rendering using anisotropically filtered mipmap interpolation. IEEE Transactions on Visualization and Computer Graphics 15(3):453-464, 2009. https://doi.org/10.1109/TVCG.2008.106
26. S. Lee, E. Eisemann, H.-P. Seidel. Depth-of-field rendering with multiview synthesis. ACM Transactions on Graphics 28(5), article 134, 2009 (SIGGRAPH Asia). https://doi.org/10.1145/1618452.1618480
27. T. J. Kosloff, M. W. Tao, B. A. Barsky. Depth of field postprocessing for layered scenes using constant-time rectangle spreading. Proc. Graphics Interface 2009, pp. 39-46. https://dl.acm.org/doi/10.5555/1555880.1555897
28. S. Lee, E. Eisemann, H.-P. Seidel. Real-time lens blur effects and focus control. ACM Transactions on Graphics 29(4), article 65, 2010 (SIGGRAPH). https://doi.org/10.1145/1778765.1778802
29. X. Yu, R. Wang, J. Yu. Real-time depth of field rendering via dynamic light field generation and filtering. Computer Graphics Forum 29(7):2099-2107, 2010 (Pacific Graphics). https://doi.org/10.1111/j.1467-8659.2010.01797.x
30. L. McIntosh, B. E. Riecke, S. DiPaola. Efficiently simulating the bokeh of polygonal apertures in a post-process depth of field shader. Computer Graphics Forum 31(6):1810-1822, 2012. https://doi.org/10.1111/j.1467-8659.2012.02097.x
31. D. Schedl, M. Wimmer. A layered depth-of-field method for solving partial occlusion. Journal of WSCG 20(3):239-246, 2012. https://www.cg.tuwien.ac.at/research/publications/2012/schedl-2012-dof/
32. D. Schedl, M. Wimmer. Simulating partial occlusion in post-processing depth-of-field methods. In GPU Pro 4, CRC Press, 2013, pp. 187-200. https://doi.org/10.1201/b14077-16
33. J. Wu, C. Zheng, X. Hu, F. Xu. Rendering realistic spectral bokeh due to lens stops and aberrations. The Visual Computer 29(1):41-52, 2013. https://doi.org/10.1007/s00371-012-0673-4
34. T. Sousa. Graphics gems from CryENGINE 3. SIGGRAPH 2013 course "Advances in Real-Time Rendering in Games". https://advances.realtimerendering.com/s2013/
35. J. Jimenez. Next generation post processing in Call of Duty: Advanced Warfare. SIGGRAPH 2014 course "Advances in Real-Time Rendering in Games". https://advances.realtimerendering.com/s2014/
36. J. Moersch, H. J. Hamilton. Variable-sized, circular bokeh depth of field effects. Proc. Graphics Interface 2014, pp. 103-107. https://dl.acm.org/doi/10.5555/2619648.2619665
37. T. McGraw. Fast bokeh effects using low-rank linear filters. The Visual Computer 31(5):601-611, 2015. https://doi.org/10.1007/s00371-014-0986-6
38. K. Selgrad, C. Reintges, D. Penk, P. Wagner, M. Stamminger. Real-time depth of field using multi-layer filtering. Proc. ACM I3D 2015, pp. 121-127. https://doi.org/10.1145/2699276.2699288
39. K. Garcia. Circular separable convolution depth of field. ACM SIGGRAPH 2017 Talks, article 16. https://doi.org/10.1145/3084363.3085022
40. G. Abadie. A life of a bokeh. SIGGRAPH 2018 course "Advances in Real-Time Rendering in Games". https://advances.realtimerendering.com/s2018/
41. D. Gustafsson. Bokeh depth of field in a single pass. Blog post, 2018 (2018-05-04). https://blog.voxagon.se/2018/05/04/bokeh-depth-of-field-in-single-pass.html
42. T. Leimkühler, H.-P. Seidel, T. Ritschel. Laplacian kernel splatting for efficient depth-of-field and motion blur synthesis or reconstruction. ACM Transactions on Graphics 37(4), article 55, 2018 (SIGGRAPH). https://doi.org/10.1145/3197517.3201379
43. L. Franke, N. Hofmann, M. Stamminger, K. Selgrad. Multi-layer depth of field rendering with tiled splatting. Proc. ACM on Computer Graphics and Interactive Techniques 1(1), 2018 (I3D). https://doi.org/10.1145/3203200
44. S. J. Gortler, R. Grzeszczuk, R. Szeliski, M. F. Cohen. The Lumigraph. SIGGRAPH '96, pp. 43-54, 1996. https://doi.org/10.1145/237170.237200
45. M. Levoy, P. Hanrahan. Light field rendering. SIGGRAPH '96, pp. 31-42, 1996. https://doi.org/10.1145/237170.237199
46. J. Shade, S. Gortler, L.-w. He, R. Szeliski. Layered depth images. SIGGRAPH '98, pp. 231-242, 1998. https://doi.org/10.1145/280814.280882
47. A. Isaksen, L. McMillan, S. J. Gortler. Dynamically reparameterized light fields. SIGGRAPH 2000, pp. 297-306. https://doi.org/10.1145/344779.344929
48. R. Ng, M. Levoy, M. Brédif, G. Duval, M. Horowitz, P. Hanrahan. Light field photography with a hand-held plenoptic camera. Stanford University Computer Science Tech Report CSTR 2005-02, 2005. https://graphics.stanford.edu/papers/lfcamera/
49. S. Bae, F. Durand. Defocus magnification. Computer Graphics Forum 26(3):571-579, 2007 (Eurographics 2007). https://doi.org/10.1111/j.1467-8659.2007.01080.x
50. A. Telea. An image inpainting technique based on the fast marching method. Journal of Graphics Tools 9(1):23-34, 2004. https://doi.org/10.1080/10867651.2004.10487596
51. J. T. Barron, A. Adams, Y. Shih, C. Hernández. Fast bilateral-space stereo for synthetic defocus. Proc. IEEE CVPR 2015, pp. 4466-4474. https://doi.org/10.1109/CVPR.2015.7299076
52. T. Hach, J. Steurer, A. Amruth, A. Pappenheim. Cinematic bokeh rendering for real scenes. Proc. 12th European Conference on Visual Media Production (CVMP 2015). https://doi.org/10.1145/2824840.2824842
53. Y. Yang, H. Lin, Z. Yu, S. Paris, J. Yu. Virtual DSLR: high quality dynamic depth-of-field synthesis on mobile platforms. Electronic Imaging 28(18), 2016, DPMI-031. https://doi.org/10.2352/ISSN.2470-1173.2016.18.DPMI-031
54. N. Wadhwa, R. Garg, D. E. Jacobs, B. E. Feldman, N. Kanazawa, R. Carroll, Y. Movshovitz-Attias, J. T. Barron, Y. Pritch, M. Levoy. Synthetic depth-of-field with a single-camera mobile phone. ACM Transactions on Graphics 37(4), 2018 (SIGGRAPH). https://doi.org/10.1145/3197517.3201329, arXiv:1806.04171
55. T. Zhou, R. Tucker, J. Flynn, G. Fyffe, N. Snavely. Stereo magnification: learning view synthesis using multiplane images. ACM Transactions on Graphics 37(4), 2018 (SIGGRAPH). https://doi.org/10.1145/3197517.3201323
56. B. Mildenhall, P. P. Srinivasan, R. Ortiz-Cayon, N. K. Kalantari, R. Ramamoorthi, R. Ng, A. Kar. Local light field fusion: practical view synthesis with prescriptive sampling guidelines. ACM Transactions on Graphics 38(4), 2019 (SIGGRAPH). https://doi.org/10.1145/3306346.3322980
57. X. Zhang, K. Matzen, V. Nguyen, D. Yao, Y. Zhang, R. Ng. Synthetic defocus and look-ahead autofocus for casual videography. ACM Transactions on Graphics 38(4), 2019 (SIGGRAPH). https://doi.org/10.1145/3306346.3323015, arXiv:1905.06326
58. B. Busam, M. Hog, S. McDonagh, G. Slabaugh. SteReFo: efficient image refocusing with stereo vision. ICCV Workshops 2019, pp. 3295-3304. https://doi.org/10.1109/ICCVW.2019.00411, arXiv:1909.13395
59. M.-L. Shih, S.-Y. Su, J. Kopf, J.-B. Huang. 3D photography using context-aware layered depth inpainting. Proc. IEEE/CVF CVPR 2020, pp. 8025-8035. https://doi.org/10.1109/CVPR42600.2020.00805
60. L. Wang, X. Shen, J. Zhang, O. Wang, Z. Lin, C.-Y. Hsieh, S. Kong, H. Lu. DeepLens: shallow depth of field from a single image. ACM Transactions on Graphics 37(6), 2018 (SIGGRAPH Asia). https://doi.org/10.1145/3272127.3275013, arXiv:1810.08100
61. L. Xiao, A. Kaplanyan, A. Fix, M. Chapman, D. Lanman. DeepFocus: learned image synthesis for computational displays. ACM Transactions on Graphics 37(6), 2018 (SIGGRAPH Asia). https://doi.org/10.1145/3272127.3275032
62. K. Purohit, M. Suin, P. Kandula, A. N. Rajagopalan. Depth-guided dense dynamic filtering network for bokeh effect rendering. ICCV Workshops 2019, pp. 3417-3426. https://doi.org/10.1109/ICCVW.2019.00424
63. A. Ignatov, J. Patel, R. Timofte, et al. AIM 2019 challenge on bokeh effect synthesis: methods and results. ICCV Workshops 2019, pp. 3591-3598. https://doi.org/10.1109/ICCVW.2019.00444
64. A. Ignatov, J. Patel, R. Timofte. Rendering natural camera bokeh effect with deep learning. CVPR Workshops 2020, pp. 1676-1686. https://doi.org/10.1109/CVPRW50498.2020.00217, arXiv:2006.05698
65. A. Ignatov, R. Timofte, et al. AIM 2020 challenge on rendering realistic bokeh. ECCV 2020 Workshops. arXiv:2011.04988 (partially verified: the LNCS DOI was not found)
66. S. Dutta, S. D. Das, N. A. Shah, A. K. Tiwari. Stacked deep multi-scale hierarchical network for fast bokeh effect rendering from a single image. CVPR Workshops 2021, pp. 2398-2407. https://doi.org/10.1109/CVPRW53098.2021.00272
67. J. Peng, X. Luo, K. Xian, Z. Cao. Interactive portrait bokeh rendering system. Proc. IEEE ICIP 2021, pp. 2923-2927. https://doi.org/10.1109/ICIP42928.2021.9506674
68. J. Peng, Z. Cao, X. Luo, H. Lu, K. Xian, J. Zhang. BokehMe: when neural rendering meets classical rendering. Proc. IEEE/CVF CVPR 2022, pp. 16262-16271. https://doi.org/10.1109/CVPR52688.2022.01580, arXiv:2206.12614
69. J. Peng, J. Zhang, X. Luo, H. Lu, K. Xian, Z. Cao. MPIB: an MPI-based bokeh rendering framework for realistic partial occlusion effects. ECCV 2022, LNCS, pp. 590-607. https://doi.org/10.1007/978-3-031-20068-7_34, arXiv:2207.08403
70. A. Ignatov, R. Timofte, et al. Realistic bokeh effect rendering on mobile GPUs, Mobile AI & AIM 2022 challenge: report. ECCV 2022 Workshops, LNCS, pp. 153-173, 2023. https://doi.org/10.1007/978-3-031-25066-8_7, arXiv:2211.06769
71. X. Luo, J. Peng, K. Xian, Z. Wu, Z. Cao. Defocus to focus: photo-realistic bokeh rendering by fusing defocus and radiance priors. Information Fusion 89:320-335, 2023. https://doi.org/10.1016/j.inffus.2022.08.023
72. T. Seizinger, M. V. Conde, M. Kolmet, T. E. Bishop, R. Timofte. Efficient multi-lens bokeh effect rendering and transformation. CVPR Workshops 2023, pp. 1633-1642. https://doi.org/10.1109/CVPRW59228.2023.00165
73. M. V. Conde, M. Kolmet, T. Seizinger, T. E. Bishop, R. Timofte, et al. Lens-to-lens bokeh effect transformation. NTIRE 2023 challenge report. CVPR Workshops 2023, pp. 1643-1659. https://doi.org/10.1109/CVPRW59228.2023.00166
74. Y. Sheng, Z. Yu, L. Ling, Z. Cao, X. Zhang, X. Lu, K. Xian, H. Lin, B. Benes. Dr.Bokeh: DiffeRentiable occlusion-aware bokeh rendering. Proc. IEEE/CVF CVPR 2024, pp. 4515-4525. https://doi.org/10.1109/CVPR52733.2024.00432, arXiv:2308.08843
75. J. Peng, Z. Cao, X. Luo, K. Xian, W. Tang, J. Zhang, G. Lin. BokehMe++: harmonious fusion of classical and neural rendering for versatile bokeh creation. IEEE TPAMI 47(3):1530-1547, 2025. https://doi.org/10.1109/TPAMI.2024.3501739
76. J. Deng, X. Zhou, H. Tian, Z. Pan, D. Aguiar. GBSD: generative bokeh with stage diffusion. arXiv:2306.08251, 2023 (short version at ICASSP 2024). https://arxiv.org/abs/2306.08251
77. A. Fortes, T. Wei, S. Zhou, X. Pan. Bokeh Diffusion: defocus blur control in text-to-image diffusion models. SIGGRAPH Asia 2025 Conference Papers, article 160. arXiv:2503.08434. https://arxiv.org/abs/2503.08434
78. C. Zhu, Q. Fan, Q. Zhang, J. Chen, H. Zhang, C. Xu, B. Shi. BokehDiff: neural lens blur with one-step diffusion. ICCV 2025. arXiv:2507.18060. https://arxiv.org/abs/2507.18060
79. Y. Yang, S. Zheng, Q. Yang, J. Chen, B. Wu, X. He, D. Cai, B. Li, P.-T. Jiang. Any-to-Bokeh: arbitrary-subject video refocusing with video diffusion model. arXiv:2505.21593, 2025. https://arxiv.org/abs/2505.21593
80. T. Seizinger, F.-A. Vasluianu, M. V. Conde, Z. Wu, R. Timofte. Bokehlicious: photorealistic bokeh rendering with controllable apertures. arXiv:2503.16067, 2025. https://arxiv.org/abs/2503.16067
81. L. Shi, S. Zheng, Z. Wang, H. Zhang, J. Chen, B. Li, S. Chen, P.-T. Jiang. Towards photorealistic and efficient bokeh rendering via diffusion framework. CVPR 2026. arXiv:2605.07429. https://arxiv.org/abs/2605.07429
82. X. Hou, X. Li, Z. Yue, C. C. Loy. AnyBokeh: physics-guided any-to-any bokeh editing with optical fingerprint transfer. arXiv:2606.31959, 2026. https://arxiv.org/abs/2606.31959
83. H. Nagasubramaniam, R. Younes. Bokeh effect rendering with vision transformers. TechRxiv preprint, 2022. https://doi.org/10.36227/techrxiv.17714849 (partially verified: preprint, no peer-reviewed venue found)
84. R. Ranftl, K. Lasinger, D. Hafner, K. Schindler, V. Koltun. Towards robust monocular depth estimation: mixing datasets for zero-shot cross-dataset transfer. IEEE TPAMI 44(3):1623-1637, 2022. https://doi.org/10.1109/TPAMI.2020.3019967, arXiv:1907.01341
85. L. Yang, B. Kang, Z. Huang, X. Xu, J. Feng, H. Zhao. Depth Anything: unleashing the power of large-scale unlabeled data. Proc. IEEE/CVF CVPR 2024, pp. 10371-10381. https://doi.org/10.1109/CVPR52733.2024.00987, arXiv:2401.10891
86. L. Yang, B. Kang, Z. Huang, Z. Zhao, X. Xu, J. Feng, H. Zhao. Depth Anything V2. NeurIPS 2024 (Advances in Neural Information Processing Systems 37), pp. 21875-21911. arXiv:2406.09414. https://arxiv.org/abs/2406.09414
87. Foundry. ZDefocus. Nuke Reference Guide, Filter nodes, 2026 (undated online page, so the year is that of access). https://learn.foundry.com/nuke/content/reference_guide/filter_nodes/zdefocus.html (fetched 2026-11-23)
88. F. C. Crow. Summed-area tables for texture mapping. SIGGRAPH '84, Computer Graphics 18(3):207-212, 1984. https://doi.org/10.1145/964965.808600
89. J. Hensley, T. Scheuermann, G. Coombe, M. Singh, A. Lastra. Fast summed-area table generation and its applications. Computer Graphics Forum 24(3):547-555, 2005 (Eurographics 2005). https://doi.org/10.1111/j.1467-8659.2005.00880.x (partially verified: the record is confirmed, the full text could not be read)
90. Y. Jeong, S. Y. Baek, Y. Seok, G. B. Lee, S. Lee. Real-time dynamic bokeh rendering with efficient look-up table sampling. IEEE Transactions on Visualization and Computer Graphics 28(2):1373-1384, 2022. https://doi.org/10.1109/TVCG.2020.3014474
