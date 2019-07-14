OpenFX-Misc [![GPL2 License](http://img.shields.io/:license-gpl2-blue.svg?style=flat-square)](https://github.com/devernay/openfx-misc/blob/master/LICENSE) [![Open Hub](https://www.openhub.net/p/openfx-misc/widgets/project_thin_badge?format=gif&ref=Thin+badge)](https://www.openhub.net/p/openfx-misc?ref=Thin+badge) [![Build Status](https://api.travis-ci.org/devernay/openfx-misc.png?branch=master)](https://travis-ci.org/devernay/openfx-misc) [![Coverity Scan Build Status](https://scan.coverity.com/projects/2945/badge.svg)](https://scan.coverity.com/projects/2945 "Coverity Badge")
===========

Miscellaneous OFX / OpenFX / Open Effects plugins.

These plugins were primarily developped for
[Natron](http://natron.inria.fr), but may be used with other
[OpenFX](http://openeffects.org) hosts.

More information about OpenFX Hosts, OpenFX Plugins (commercial or
free), and OpenFX documentation can be found at
<http://devernay.free.fr/hacks/openfx/>.

Downloads
---------

### Source

To compile openfx-misc from source, see the Installation section below. There is no official source release: the openfx-misc source repository is composed of a stable set of plugins, and new plugins are introduced as beta features until they are considered stable.

### Binaries

Windows binaries compiled with Visual Studio 2017 (which should be compatible with most OpenFX hosts including [DaVinci Resolve](https://www.blackmagicdesign.com/products/davinciresolve/)) are available as part of the [openfx-misc project on AppVeyor](https://ci.appveyor.com/project/NatronGitHub/openfx-misc/build/artifacts).

Windows binaries compiled with MinGW MinGW-w64 are available as part of the portable Natron binary distributions for Windows.


License
-------

<!-- BEGIN LICENSE BLOCK -->
Copyright (C) 2013-2018 INRIA

openfx-misc is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

openfx-misc is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
<!-- END LICENSE BLOCK -->

Contents
--------

Below is a short description of each plugin. The title of each section
is the plugin grouping (the OpenFX host may classify plugins by
grouping).

### Image

* CheckerBoardOFX: Generate an image with a checkerboard
* ColorBarsOFX: Generate an image with SMPTE color bars
* ColorWheelOFX: Generate an image with a color wheel
* ConstantOFX: Generate an image with a uniform color
* SolidOFX: Generate an image with a uniform opaque color

### Draw

* Rand: Generate a random field of noise.
* NoiseCImg: Generate different kinds of noise.
* PlasmaCImg: Generate plasma noise.
* RadialOFX: Radial ramp.
* RampOFX: Draw a ramp between 2 edges.
* RectangleOFX: Draw a rectangle.
* RotoOFX: Create masks and shapes. Requires a host with mask editing
  capabilities (such as Natron).
  

### Time

* AppendClipOFX: Put one clip after another.
* Deinterlace: Deinterlace input stream.
* FrameBlendOFX: Blend frames.
* FrameHoldOFX: Hold a frame, or subsample the input frames.
* FrameRangeOFX: Change the frame range of a clip. Useful with AppendClip
* RetimeOFX: Change the timing of the input clip.
* SlitScan: Per-pixel retiming.
* TimeOffsetOFX: Move the input clip forward or backward in time.
* TimeBlurOFX: Average frames over a fractional shutter period.
* NoTimeBlurOFX: Round fractional frames to integer values.
* TimeBufferRead/TimeBufferWrite: Read from an buffer written during a previous render.

### Channel

* ShuffleOFX: Rearrange channels from one or two inputs, and convert to a different bit depth (on hosts that support it).

### Color

* ClampOFX: Clamp values to a given interval.
* ColorCorrectOFX: Adjusts the saturation, contrast, gamma, gain and offset of an image.
* ColorLookupOFX: Apply a parametric lookup curve to each channel separately. 
* ColorSuppress: Remove a color/tint, or create a mask from that color.
* EqualizeCImg: Equalize the histogram.
* GradeOFX: Modify the tonal spread of an image from the white and black points.
* HistEqCImg: Equalize the luminance histogram. 
* HSVToolOFX: Adjust hue, saturation and brightnes, or perform color replacement.
* HueCorrectOFX: Apply hue-dependent color adjustments using lookup curves.
* InvertOFX: Inverse the selected channels.
* Log2LinOFX: Convert from/to the logarithmic space used by Cineon files
* Math/AddOFX: Add a constant to the selected channels.
* Math/ClipTestOFX: Draw zebra stripes on all pixels outside of the specified range.
* Math/ColorMatrixOFX: Multiply the RGBA channels by an arbitrary 4x4 matrix.
* Math/GammaOFX: Apply gamma function to the selected channels.
* Math/MultiplyOFX: Multiply the selected channels by a constant.
* PLogLinOFX: Convert between linear and log representations using the Josh Pines log conversion.
* Quantize: Reduce the number of color levels with posterization or dithering.
* SaturationOFX: Modify the color saturation of an image.
* Threshold: Threshold the selected channels to bring them within the 0-1 range.
* Transform/RGBToHSV and HSVToRGB: Convert to/from HSV color representation.
* Transform/RGBToHSL and HSLToRGB: Convert to/from HSL color representation. 
* Transform/RGBToHSI and HSIToRGB: Convert to/from HSI color representation. 
* Transform/RGBToYCbCr601, YCbCrToRGB601, RGBToYCbCr709 and YCbCrToRGB709: Convert to/from YCbCr color representation. 
* Transform/RGBToYPbPr601, YPbPrToRGB601, RGBToYPbPr709 and YPbPrToRGB709: Convert to/from YPbPr color representation. 
* Transform/RGBToYUV601, YUVToRGB601, RGBToYUV709 and YUVToRGB709: Convert to/from YUV color representation. 
* Transform/RGB709ToLab and LabToRGB709: Convert to/from Lab color representation.
* Transform/RGB709ToXYZ and XYZToRGB709: Convert to/from XYZ color representation.
* VectorToColor: Convert x and y vector components to a color representation.

### Filter

* BlurCImg: Blur input stream by a quasi-Gaussian or Gaussian filter (recursive implementation), or compute derivatives.
* ChromaBlurCImg: Blur the chrominance components (usually to prep strongly compressed and chroma subsampled footage for keying).
* DenoiseSharpen: Denoise and/or sharpen images using wavelet-based algorithms.
* DilateCImg/ErodeCImg: Dilate/erode input stream by a rectangular structuring element of specified size and Neumann (a.k.a. nearest) boundary conditions.
* DirBlurOFX: Directional blur.
* Distance: Compute the distance from each pixel to the closest zero-valued pixel.
* EdgeDetectCImg: Perform edge detection by computing the image gradient magnitude.
* EdgeExtend: Fill a matte (i.e. a non-opaque color image with an alpha channel) by extending the edges of the matte.
* ErodeBlurCImg: Erode or dilate a mask by smoothing.
* ErodeSmoothCImg: Erode or dilate input stream using a [normalized power-weighted filter](http://dx.doi.org/10.1109/ICPR.2004.1334273).
* GMICExpr: Quickly generate or process image from mathematical formula evaluated for each pixel.
* GodRays: Average an image over a range of transforms, or create crepuscular rays.
* InpaintCImg: Inpaint the areas indicated by the Mask input using patch-based inpainting.
* Matrix3x3 and Matrix5x5: Apply a filter given by a 3x3 or 5x5 matrix.
* Shadertoy: Apply a [Shadertoy](http://www.shadertoy.com) fragment shaders (multipass shaders are not supported).
* SharpenCImg: Sharpen the input stream by enhancing its Laplacian.
* SharpenInvDiffCImg: Sharpen selected images by inverse diffusion.
* SharpenShockCImg: Sharpen selected images by shock filters.
* SmoothAnisotropicCImg: Smooth/Denoise input stream using anisotropic PDE-based smoothing.
* SmoothBilateralCImg: Blur input stream by bilateral filtering.
* SmoothBilateralGuidedCImg: Apply joint/cross bilateral filtering on image A, guided by the intensity differences of image B.
* SmoothGuidedCImg: Blur image, with the [Guided Image filter](http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf).
* SmoothMedianCImg: Apply a [median filter](https://en.wikipedia.org/wiki/Median_filter) to input images.
* SmoothPatchBasedCImg: Denoise selected images by non-local patch averaging.
* SmoothRollingGuidanceCImg: Filter out details under a given scale using the [Rolling Guidance filter](http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/).
* SoftenCImg: Soften the input stream by reducing its Laplacian.

### Keyer

* ChromaKeyerOFX: Apply chroma keying, as described in "Video Demystified" by Keith Jack.  
* Despill: Remove the unwanted color contamination of the foreground (spill) caused by the reflected color of the bluescreen/greenscreen, as described in "Digital Compositing for Film and Video" by Steve Wright.
* DifferenceOFX: Produce a rough matte from the difference of two images.
* HueKeyerOFX: Compute a key depending on hue value.
* KeyerOFX: A collection of simple keyers. 
* MatteMonitor: A Matte Monitor, as described in "Digital Compositing for Film and Video" by Steve Wright.
* PIK: A per-pixel color difference keyer that uses a mix operation instead of a max operation to combine the non-backing screen channels.
* note that HSVToolOFX (in the "Color" section) can also be used as a keyer.

### Merge

* ContactSheetOFX: Make a contact sheet from several inputs or frames.
* CopyRectangleOFX: Copies a rectangle from the input A to the input B in output.
* DissolveOFX: Weighted average of two inputs.
* KeyMixOFX: Copies A to B only where Mask is non-zero.
* LayerContactSheetOFX: Make a contact sheet from layers.
* MergeOFX: Pixel-by-pixel merge operation between the two inputs.
* PreMultOFX/UnpremultOFX: Multiply/divide the selected channels by alpha (or another channel).
* SwitchOFX: Lets you switch between any number of inputs.
* TimeDissolve: Dissolve from input A to input B over time.

### Transform

* AdjustRoD: Enlarges the input image by a given amount of black and transparent pixels.
* Card3DOFX: Transform and image as if it were projected on a 3D card.
* CornerPinOFX and CornerPinMaskedOFX: Fit an image to another in translation, rotation, scale, and shear
* CropOFX: Remove everything outside from the image of a rectangle.
* IDistortOFX: Distort an image, based on a displacement map.
* LensDistortionOFX: Apply nonlinear lens distortion.
* MirrorOFX: Flip or flop the image.
* PositionOFX: Translate image by an integer number of pixels.
* Reformat: Convert image to a different image format and size.
* SpriteSheet: Use an image as a sprite sheet.
* STMapOFX: Move pixels around an image, based on a UVmap.
* TrackerPM: Point tracker based on pattern matching using an exhaustive search within an image region.
* TransformOFX and TransformMaskedOFX: Translate / Rotate / Scale a 2D image.

### Views

These plugins are compatible with the Sony Vegas and Nuke [multiview extensions](http://openeffects.org/standard_changes/multi-view-effects).

* JoinViewsOFX: JoinView inputs to make a stereo output.
* OneViewOFX: Takes one view from the input.
* Stereo/AnaglyphOFX: Make an anaglyph image out of the two views of the input.
* Stereo/MixViewsOFX: Mix two views together. 
* Stereo/ReConvergeOFX: Shift convergence so that a tracked point appears at screen-depth.
* Stereo/SideBySideOFX: Put the left and right view of the input next to each other.

### Other

* ImageStatisticsOFX: Compute statistics over an image or a rectangular area.
* NoOpOFX: Copies the input to the output. Useful for inspecting the properties of input and output clips.
* TestRenderOFX: Test some rendering features of the OFX host.

Notes & Caveats
---------------

### What does the Roto plugin do?

If you use the Roto plugin in any other host than [Natron](http://natron.inria.fr), you will notice that it doesn't do much. Its role is just to provide an entry point for a host-based rotoscoping tool, which provides a roto mask to this plugin.

### The default parameters are too small on DaVinci Resolve

This is because Resolve does not support the `kOfxParamPropDefaultCoordinateSystem` property. A solution was implemented in CornerPin (look for the comment "Some hosts (e.g. Resolve) may not support normalized defaults"), but the following plugins still have to be fixed:

- CopyRectangle
- Crop
- HSVTool
- ImageStatistics
- ofxsGenerator
- ofxsPositionInteract
- ofxsTransformInteract
- Position
- ReConverge
- TestPosition
- TrackerPM

### ColorLookup does not work on Nuke 8/9, DaVinci Resolve...

Although Nuke 8 & 9 claim via OpenFX that they support parametric parameters (i.e. adjustable parametric curves), these don't work (at least on OS X, and maybe on other platforms). The plugin appears in the plugin list, but cannot be instanciated. Nothing seems to happen, and the following message appears on the console (on OS X):

    Exception thrown
      basic_string::_S_construct NULL not valid

The same happens with other plugins using parametric parameters, such as [TuttleHistogramKeyer](http://www.tuttleofx.org/).

Parametric parameters work in older versions of Nuke (at least in Nuke 6 & 7).

DaVinci Resolve does not support parametric parameters.

Please [file an issue](https://github.com/devernay/openfx-misc/issues) if you think openfx-misc is doing something wrong, or you know of other hosts which have problems with parametric paremeters.

Although ColorCorrect uses parametric parameters, it can still be instanciated on Nuke 8 & 9 and on DaVinci Resolve, but the curve ranges are not adjustable (shadows are decreasing linearly from 0 to 0.09, and highlights are increasing linearly from 0.5 to 1.0).


### Retime output does not contain motion blur, where is the "box" filter and the "shutter" parameter?

We should take the code from FrameBlend, simplify, and incorporate it in Retime.


Installation
------------

These plugins are included in the binary distributions of [Natron](https://natrongithub.github.io/).

If you want to compile the plugins from source, you may either use the
provided Unix Makefile, the Xcode project, or the Visual Studio project.

### Getting the sources from github

To fetch the latest sources from github, execute the following commands:

	git clone https://github.com/devernay/openfx-misc.git
	cd openfx-misc
	git submodule update -i -r

In order to get a specific tag, corresponding to a source release, do `git tag -l`
to get the list of tags, and then `git checkout tags/<tag_name>`
to checkout a given tag.

### Unix/Linux/FreeBSD/OS X, using Makefiles

On Unix-like systems, the plugins can be compiled by typing in a
terminal:
- `make [options]` to compile as a single combined plugin (see below
  for valid options).
- `make nomulti [options]` to compile as separate plugins (useful if
only a few plugins are is needed, for example). `make` can also be
executed in any plugin's directory.
- `make [options] CXXFLAGS_ADD=-fopenmp LDFLAGS_ADD=-fopenmp` to compile
  with OpenMP support (available for CImg-based plugins and DenoiseSharpen).

The most common options are `CONFIG=release` to compile a release
version, `CONFIG=debug` to compile a debug version. Or
`CONFIG=relwithdebinfo` to compile an optimized version with debugging
symbols.

Another common option is `BITS=32`for compiling a 32-bits version,
`BITS=64` for a 64-bits version, and `BITS=Universal` for a universal
binary (OS X only).

See the file `Makefile.master`in the toplevel directory for other useful
flags/variables.

The compiled plugins are placed in subdirectories named after the
configuration, for example Linux-64-realease for a 64-bits Linux
compilation. In each of these directories, a `*.bundle` directory is
created, which has to be moved to the proper place
(`/usr/OFX/Plugins`on Linux, or `/Library/OFX/Plugins`on OS X), using
a command like the following, with the *same* options used for
compiling:

	sudo make install [options]

### OS X, using Xcode

The latest version of Xcode should be installed in order to compile this plugin.

Open the "Terminal" application (use spotlight, or browse `/Applications/Utilities`), and paste the following lines one-by-one (an administrator password will be asked for after the second line):

	xcodebuild -configuration Release install
	sudo mkdir /Library/OFX/Plugins
	sudo mv /tmp/Misc.dst/Library/OFX/Plugins/Misc /Library/OFX/Plugins

The plugins may also be compiled by compiling the Xcode project called
`Misc.xcodeproj` in the toplevel directory. The bundles produced by
this project have to be moved to `/Library/OFX/Plugins`.

### MS Windows, using Visual Studio

Compile using the provided `Misc.vcxproj`project found in the `Misc`
directory.

32-bits plugins should be installed in the directory `c:\Program Files
(x86)\Common Files\OFX\Plugin`, 64-bits plugins should be installed in
`c:\Program Files\Common Files\OFX\Plugins`.

Credits
-------

The stereoscopic plugins Anaglyph, JoinViews, MixViews, OneView,
ReConverge, SideBySide are by Frederic Devernay.

ColorLookup, Switch, TimeOffset, ChromaKeyer, Difference, Constant,
Shuffle, Rectangle, Radial, HSVTool, ImageStatistics, CheckerBoard,
Retime, SlitScan, ColorWheel, the color transform plugins and the
CImg plugins are by Frederic Devernay.

Merge, ColorCorrect, Grade, Roto, Crop, CopyRectangle  are by
Alexandre Gauthier.

Transform and CornerPin are by Frederic Devernay and Alexandre Gauthier.

Deinterlace/yadif was first ported to OFX by [George Yohng](http://yohng.com) and rewritten by Frederic Devernay when yadif was relicensed to LGPL.
