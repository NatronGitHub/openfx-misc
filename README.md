OpenFX-Misc [![Build Status](https://api.travis-ci.org/devernay/openfx-misc.png?branch=master)](https://travis-ci.org/devernay/openfx-misc) [![Coverity Scan Build Status](https://scan.coverity.com/projects/2945/badge.svg)](https://scan.coverity.com/projects/2945 "Coverity Badge")
===========

Miscellaneous OFX / OpenFX / Open Effects plugins.

These plugins were primarily developped for
[Natron](http://natron.inria.fr), but may be used with other
[OpenFX](http://openeffects.org) hosts.

More information about OpenFX Hosts, OpenFX Plugins (commercial or
free), and OpenFX documentation can be found at
<http://devernay.free.fr/hacks/openfx/>.

License
-------

<!-- BEGIN LICENSE BLOCK -->
Copyright (C) 2015 INRIA

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

* CheckerBoardOFX: Generator for an image with a checkerboard color
* ConstantOFX: Generator for an image with a uniform color
* SolidOFX: Generator for an image with a uniform opaque color

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

### Channel

* ShuffleOFX: Rearrange channels from one or two inputs, and convert
  to a different bit depth (on hosts that support it).

### Color

* ClampOFX: Clamp values to a given interval.
* ColorCorrectOFX: Adjusts the saturation, constrast, gamma, gain and
offset of an image.
* ColorLookupOFX: Apply a parametric lookup curve to each channel 
separately. 
* EqualizeCImg: Equalize the histogram.
* GradeOFX: Modify the tonal spread of an image from the white and
black points.
* HistEqCImg: Equalize the luminance histogram. 
* HSVToolOFX: Adjust hue, saturation and brightnes, or perform color replacement.
* InvertOFX: Inverse the selected channels.
* Math/AddOFX: Add a constant to the selected channels.
* Math/ClipTestOFX: Draw zebra stripes on all pixels outside of the specified range.
* Math/ColorMatrixOFX: Multiply the RGBA channels by an arbitrary 4x4
matrix.
* Math/GammaOFX: Apply gamma function to the selected channels.
* Math/MultiplyOFX: Multiply the selected channels by a constant.
* SaturationOFX: Modify the color saturation of an image.
* Transform/RGBToHSV and HSVToRGB: Convert to/from HSV color representation.
* Transform/RGBToHSL and HSLToRGB: Convert to/from HSL color representation. 
* Transform/RGBToHSI and HSIToRGB: Convert to/from HSI color representation. 
* Transform/RGBToYCbCr and YCbCrToRGB: Convert to/from YCbCr color representation. 
* Transform/RGBToYUV and YUVToRGB: Convert to/from YUV color representation. 
* Transform/RGBToLab and LabToRGB: Convert to/from Lab color representation.
* Transform/RGBToXYZ and XYZToRGB: Convert to/from XYZ color representation.
* VectorToColor: Convert x and y vector components to a color representation.

### Filter

* BilateralCImg: Blur input stream by bilateral filtering.
* BilateralGuidedCImg: Apply joint/cross bilateral filtering on image A, guided by the intensity differences of image B.
* BlurCImg: Blur input stream by a quasi-Gaussian or Gaussian filter (recursive implementation), or compute derivatives.
* ChromaBlurCImg: Blur the chrominance components (usually to prep strongly compressed and chroma subsampled footage for keying).
* DenoiseCImg: Denoise selected images by non-local patch averaging.
* DilateCImg/ErodeCImg: Dilate/erode input stream by a rectangular structuring element of specified size and Neumann boundary conditions.
* DirBlurOFX: Directional blur.
* ErodeSmoothCImg: Erode or dilate input stream using a [normalized power-weighted filter](http://dx.doi.org/10.1109/ICPR.2004.1334273).
* GMICExpr: Quickly generate or process image from mathematical formula evaluated for each pixel.
* GodRays: Average an image over a range of transforms, or create crepuscular rays.
* GuidedCImg: Blur image, with the [Guided Image filter](http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf).
* MedianCImg: Apply a [median filter](https://en.wikipedia.org/wiki/Median_filter) to input images.
* RollingGuidanceCImg: Filter out details under a given scale using the [Rolling Guidance filter](http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/).
* SharpenInvDiffCImg: Sharpen selected images by inverse diffusion.
* SharpenShockCImg: Sharpen selected images by shock filters.
* SmoothCImg: Smooth/Denoise input stream using anisotropic PDE-based smoothing.

### Keyer

* ChromaKeyerOFX: Apply chroma keying, as described in "Video Demystified" by Keith Jack.  
* MatteMonitor: A Matte Monitor, as described in "Digital Compositing for Film and Video" by Steve Wright.
* DifferenceOFX: Produce a rough matte from the difference of two
  images.
* KeyerOFX: A collection of simple keyers. 
* HSVToolOFX (in the "Color" section) can also be used as a keyer.

### Merge

* CopyRectangleOFX: Copies a rectangle from the input A to the input B in output.
* DissolveOFX: Weighted average of two inputs.
* MergeOFX: Pixel-by-pixel merge operation between the two inputs.
* PreMultOFX/UnpremultOFX: Multiply/divide the selected channels by
alpha (or another channel).
* SwitchOFX: Lets you switch between any number of inputs.
* TimeDissolve: Dissolve from input A to input B over time.

### Transform

* AdjustRoD: Enlarges the input image by a given amount of black and transparent pixels.
* CornerPinOFX and CornerPinMaskedOFX: Fit an image to another in
  translation, rotation, scale, and shear
* CropOFX: Remove everything outside from the image of a rectangle.
* IDistortOFX: Distort an image, based on a displacement map.
* LensDistortionOFX: Apply nonlinear lens distortion.
* MirrorOFX: Flip or flop the image.
* PositionOFX: Translate image by an integer number of pixels.
* STMapOFX: Move pixels around an image, based on a UVmap.
* TrackerPM: Point tracker based on pattern matching using an exhaustive search within an image region.
* TransformOFX and TransformMaskedOFX: Translate / Rotate / Scale a 2D 
  image. 

### Views

These plugin use the Sony Vegas multiview extension (they don't work
with Nuke).

* JoinViewsOFX: JoinView inputs to make a stereo output.
* OneViewOFX: Takes one view from the input.
* Stereo/AnaglyphOFX: Make an anaglyph image out of the two views of the input.
* Stereo/MixViewsOFX: Mix two views together. 
* Stereo/ReConvergeOFX: Shift convergence so that a tracked point
  appears at screen-depth.
* Stereo/SideBySideOFX: Put the left and right view of the input next
  to each other.

### Other

* ImageStatisticsOFX: Compute statistics over an image or a rectangular area.
* NoOpOFX: Copies the input to the ouput. Useful for inspecting the properties of input and output clips.
* TestRenderOFX: Test some rendering features of the OFX host.

Notes & Caveats
---------------

### Transform and CornerPin produce aliasing when scaling down images

The transform nodes may produce aliasing artifacts when downscaling by a factor of 2 or more.

This can be avoided by blurring (using BlurCImg) or resizing (using ResizeOIIO) the image before downscaling it.

There are several solutions to this problem, which may be implemented in the future (you can help!):

* Trilinear mipmapping (as implemented by OpenGL) still produces artifacts when scaling is anisotropic (i.e. the scaling factor is different along two directions)
* [Feline (McCormack, 1999)](http://www.hpl.hp.com/techreports/Compaq-DEC/WRL-99-1.pdf), which is close to what is proposed in [OpenGL's anisotropic texture filter](http://www.opengl.org/registry/specs/EXT/texture_filter_anisotropic.txt) is probably 4-5 times slower than mipmapping, but produces less artifacts
* [EWA (Heckbert 1989)](https://www.cs.cmu.edu/~ph/texfund/texfund.pdf) would give the highest quality, but is probably 20 times slower than mipmapping.

A sample implementation of the three methods is given in [Mesa 3D](http://mesa3d.org/)'s [software rasterizer, src/mesa/swrast/s_texfilter.c](http://cgit.freedesktop.org/mesa/mesa/tree/src/mesa/swrast/s_texfilter.c).

### What does the Roto plugin do?

If you use the Roto plugin in any other host than [Natron](http://natron.inria.fr), you will notice that it doesn't do much. Its role is just to provide an entry point for a host-based rotoscoping tool, which provides a roto mask to this plugin.

### ColorLookup does not work on Nuke 8/9, DaVinci Resolve...

Although Nuke 8 & 9 claim via OpenFX that they support parametric parameters (i.e. adjustable parametric curves), these don't work (at least on OS X, and maybe on other platforms). The plugin appears in the plugin list, but cannot be instanciated. Nothing seems to happen, and the following message appears on the console (on OS X):

    Exception thrown
      basic_string::_S_construct NULL not valid

The same happens with other plugins using parametric parameters, such as [TuttleHistogramKeyer](http://www.tuttleofx.org/).

Parametric parameters work in older versions of Nuke (at least in Nuke 6 & 7).

DaVinci Resolve does not support parametric parameters.

Please [file an issue](https://github.com/devernay/openfx-misc/issues) if you think openfx-misc is doing something wrong, or you know of other hosts which have problems with parametric paremeters.

Although ColorCorrect uses parametric parameters, it can still be instanciated on Nuke 8 & 9 and on DaVinci Resolve, but the curve ranges are not adjustable (shadows are decreasing linearly from 0 to 0.09, and highlights are increasing linearly from 0.5 to 1.0).

Installation
------------

These plugins are included in the binary distributions of [Natron](http://natron.inria.fr).

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
Retime, SlitScan and the CImg plugins are by Frederic Devernay.

Merge, ColorCorrect, Grade, Roto, Crop, CopyRectangle  are by
Alexandre Gauthier.

Transform and CornerPin are by Frederic Devernay and Alexandre Gauthier.

Deinterlace/yadif was first ported to OFX by [George Yohng](http://yohng.com) and rewritten by Frederic Devernay when yadif was relicensed to LGPL.
