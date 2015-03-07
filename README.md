OpenFX-Misc [![Build Status](https://api.travis-ci.org/devernay/openfx-misc.png?branch=master)](https://travis-ci.org/devernay/openfx-misc) [![Coverity Scan Build Status](https://scan.coverity.com/projects/2945/badge.svg)](https://scan.coverity.com/projects/2945 "Coverity Badge")
===========

Miscellaneous OFX / OpenFX / Open Effects plugins.

These plugins were primarily developped for
[Natron](http://natron.inria.fr), but may be used with other
[OpenFX](http://openeffects.org) hosts.

More information about OpenFX Hosts, OpenFX Plugins (commercial or
free), and OpenFX documentation can be found at
<http://devernay.free.fr/hacks/openfx/>.

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

* NoiseOFX: Generate simple noise.
* NoiseCImg: Generate different kinds of noise.
* PlasmaCImg: Generate plasma noise.
* RadialOFX: Radial ramp.
* RampOFX: Draw a ramp between 2 edges.
* RectangleOFX: Draw a rectangle.
* RotoOFX: Create masks and shapes. Requires a host with mask editing
  capabilities (such as Natron).
  

### Time

* Deinterlace: Deinterlace input stream.
* FrameBlend: Blend frames.
* RetimeOFX: Change the timing of the input clip.
* TimeOffsetOFX: Move the input clip forward or backward in time.

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
* DenoiseCImg: Denoise selected images by non-local patch averaging.
* DilateCImg/ErodeCImg: Dilate/erode input stream by a rectangular structuring element of specified size and Neumann boundary conditions.
* DirBlurOFX: Directional blur.
* ErodeSmoothCImg: Erode or dilate input stream using a [normalized power-weighted filter](http://dx.doi.org/10.1109/ICPR.2004.1334273).
* GodRays: Average an image over a range of transforms, or create crepuscular rays.
* GuidedCImg: Blur image, with the [Guided Image filter](http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf).
* RollingGuidanceCImg: Filter out details under a given scale using the [Rolling Guidance filter](http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/).
* SharpenInvDiffCImg: Sharpen selected images by inverse diffusion.
* SharpenShockCImg: Sharpen selected images by shock filters.
* SmoothCImg: Smooth/Denoise input stream using anisotropic PDE-based smoothing.

### Keyer

* ChromaKeyerOFX: Apply chroma keying, as described in "Video Demystified" by Keith Jack.  
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

### Transform

* AdjustRoD: Enlarges the input image by a given amount of black and transparent pixels.
* CornerPinOFX and CornerPinMaskedOFX: Fit an image to another in
  translation, rotation, scale, and shear
* CropOFX: Remove everything outside from the image of a rectangle.
* MirrorOFX: Flip or flop the image.
* PositionOFX: Translate image by an integer number of pixels.
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

Although Nuke 8 & 9 claim via OpenFX that they support parametric parameters (i.e. adjustable parametric curves), these don't work (at least on OS X, and maybe on other platforms). The plugins cannot be instanciated, nothing seems to happen, and the following message appears on the console:

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

To compile an optimized version for a 64-bits machine: open a shell in
the toplevel directory, and type

	make DEBUGFLAG=-O3 BITS=64

Without the DEBUGFLAG flag, a debug version will be compiled, and use
BITS=32 to compile a 32-bits version.

The compiled plugins will be placed in subdiecories named after the
configuration, for example Linux-64-realease for a 64-bits Linux
compilation. In each of these directories, you will find a `*.bundle`
file, which has to be moved to the proper place (`/usr/OFX/Plugins`on
Linus, or `/Library/OFX/Plugins`on OS X), using a command like:
	sudo mv */*/*.bundle /usr/OFX/Plugins

### OS X, using Xcode

The plugins may be compiled by compiling the Xcode project called
`Misc.xcodeproj` in the toplevel directory. The bundles produced by
this project have to be moved to `/Library/OFX/Plugins`.

Alternatively, you can compile from the command-line using:

	xcodebuild -project Misc.xcodeproj -configuration Release install
	sudo mkdir /Library/OFX/Plugins
	sudo mv /tmp/Misc.dst/Library/OFX/Plugins/Misc /Library/OFX/Plugins

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
and the CImg plugins are by Frederic Devernay.

Merge, ColorCorrect, Grade, Roto, Crop, CopyRectangle  are by
Alexandre Gauthier.

Transform and CornerPin are by Frederic Devernay and Alexandre Gauthier.

Deinterlace/yadif was first ported to OFX by [George Yohng](http://yohng.com) and rewritten when yadif was relicensed to LGPL.
