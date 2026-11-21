OpenFX-Misc [![GPL2 License](http://img.shields.io/:license-gpl2-blue.svg?style=flat-square)](https://github.com/NatronGitHub/openfx-misc/blob/master/LICENSE) [![Open Hub](https://www.openhub.net/p/openfx-misc/widgets/project_thin_badge?format=gif&ref=Thin+badge)](https://www.openhub.net/p/openfx-misc?ref=Thin+badge) [![CI](https://github.com/NatronGitHub/openfx-misc/actions/workflows/ci.yml/badge.svg?branch=master)](https://github.com/NatronGitHub/openfx-misc/actions/workflows/ci.yml)
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

The easiest way to get these plugins is to install
[Natron](https://natrongithub.github.io/): they are included in all Natron binary
distributions, so there is nothing else to install.

### Binary downloads

To use the plugins with another OpenFX host, download them from the
[GitHub releases](https://github.com/NatronGitHub/openfx-misc/releases) (built by
`.github/workflows/release.yml` from the release tags). There are four downloads for
each platform (Linux x86_64, built on Rocky Linux 9; Windows x86_64; macOS 14 or later,
universal):

- `openfx-misc-<version>-<platform>`: the full set, the same bundles as in Natron:
  `Misc.ofx.bundle`, `CImg.ofx.bundle` and `Shadertoy.ofx.bundle`. Shadertoy is the
  OSMesa variant (it can also render on the CPU, when the host has no OpenGL rendering),
  linked with a prebuilt static OSMesa and LLVM from
  [osmesa-install](https://github.com/devernay/osmesa-install).
- `openfx-misc-Shadertoy-OpenGL-<version>-<platform>`: `Shadertoy.ofx.bundle` alone,
  OpenGL only.
- `openfx-misc-Shadertoy-OSMesa-<version>-<platform>`:
  `Shadertoy.ofx.bundle` alone, OpenGL + OSMesa: the bundle of the full set.
- `openfx-misc-DenoiseSharpen-<version>-<platform>`: `net.sf.openfx.DenoiseSharpen.ofx.bundle`, with
  only the DenoiseSharpen plugin. Older releases named it `DenoiseSharpen.ofx.bundle`: remove that
  old bundle from your plugins directory.

The Linux bundles are built on Rocky Linux 9 (Natron 2.6's baseline), and need glibc 2.34
or later, the system's libstdc++ (GCC 11 or later), `libGL.so.1` (OpenGL) and `libgomp.so.1`
(GCC's OpenMP runtime, needed by `Misc.ofx` (DenoiseSharpen), `CImg.ofx` and the standalone
`net.sf.openfx.DenoiseSharpen.ofx`): these system libraries are never bundled. Any other library is
inside the bundle (`<bundle>.ofx.bundle/Libraries`).

**Install either the full set or standalone bundles, not both**: the standalone
bundles contain the same plugins, with the same identifiers and versions, as the full
set (DenoiseSharpen is also in `Misc.ofx.bundle`), and a host must not find a plugin
twice. The two Shadertoy downloads are mutually exclusive too (both are
`Shadertoy.ofx.bundle`). Each archive has a `README.txt` that tells where to install
it; the steps are the same as steps 3 to 6 below. Its `LICENSE.txt` is openfx-misc's license
(GPLv2 or later), and `LICENSES/` has the licenses of the third-party code in the bundles
(openfx's Support library; freeglut's text renderer and pixman's blend modes, from SupportExt,
in Misc; CImg, with the CeCILL-C text, and pocketfft; and Mesa and LLVM in the OSMesa Shadertoy, which
also carries them in `Shadertoy.ofx.bundle/Contents/Resources/licenses`), and TinyThread++
(zlib) in every bundle. Misc also contains `Misc/randomGenerator.cpp` (MT19937, GNU Library
GPL v2 or later) and the Yadif code of `Deinterlace/Deinterlace.cpp` (from FFmpeg, LGPL 2.1
or later), distributed under the GPL as part of Misc. Distortion's PFBarrel (`.pfb`) file
reader contains code "Copyright (C) 2011 The Pixel Farm Ltd", distributed under the GPL as part
of Misc.

### Development builds

Binaries are also built by the continuous integration. They are not kept for every
change, only for pre-release
tags (tags that start with `Natron-` and contain `-rc`, `-beta` or `-alpha`, in any
case, such as `Natron-2.6.0-RC1` or `Natron-2.6.0-beta1`; not final releases such
as `Natron-2.5.0`) and for builds that a maintainer starts by hand:

1. Go to the
   [GitHub Actions page](https://github.com/NatronGitHub/openfx-misc/actions/workflows/ci.yml)
   and click on the latest successful run that has artifacts (a pre-release tag such
   as `Natron-2.6.0-RC1`, or a run started with "Run workflow").
2. At the bottom of the page, in the "Artifacts" section, download the archive for
   your system (you must be logged in to GitHub):
   - `openfx-misc-build-ubuntu_24-release`: Linux (64-bit) plugins, built on
     Ubuntu 24.04.
   - `openfx-misc-build-macos-release`: macOS plugins for Apple silicon (arm64),
     macOS 14 or later.
   - `openfx-misc-build-windows-release`: Windows (64-bit, MSYS2 UCRT64) plugins,
     Windows 10 or later (they use the Universal C Runtime).

   Windows nightly builds, made with MSVC, are also kept for 30 days on the
   [nightly page](https://github.com/NatronGitHub/openfx-misc/actions/workflows/nightly.yml):
   `openfx-misc-nightly-windows-msvc-x64` (x64) and `openfx-misc-nightly-windows-msvc-arm64x`
   (Windows on Arm: Arm64X binaries in `Contents/Win-arm64ec`, for Arm64EC hosts, and ARM64
   ones in `Contents/Win-arm64`, for native ARM64 hosts). Each holds the zip files of a
   release: the full set (with an OpenGL-only Shadertoy), Shadertoy and DenoiseSharpen alone.

   Artifacts are deleted by GitHub after 90 days. If there is none, you can
   [compile the plugins](#installation) yourself (`make nomulti`, or CMake's
   `-DOFX_PLUGIN_BUNDLES=SEPARATE`, builds one bundle per plugin, if you only need a few
   of them).
3. Unzip the archive. It contains directories named `*.ofx.bundle` (for example
   `Misc.ofx.bundle`, `CImg.ofx.bundle`, `Shadertoy.ofx.bundle`).
4. Copy these `*.ofx.bundle` directories into the standard OpenFX plugins
   directory for your system (create it if it does not exist). Copy each whole
   directory without renaming it: the host needs the complete bundle, not only the
   `.ofx` file inside it.
   - Linux: `/usr/OFX/Plugins`
   - macOS: `/Library/OFX/Plugins`
   - Windows: `C:\Program Files\Common Files\OFX\Plugins`
5. On macOS, the downloaded plugins are not signed, and macOS may refuse to load
   them. To allow them, run in a Terminal, for each bundle:
   `sudo xattr -dr com.apple.quarantine /Library/OFX/Plugins/Misc.ofx.bundle`
6. Restart your OpenFX host.

License
-------

<!-- BEGIN LICENSE BLOCK -->
Copyright (C) 2018-2020 The Natron Developers

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

Third-party code: the CImg plugins use [CImg](https://github.com/GreycLab/CImg)
(CeCILL-C or CeCILL), downloaded at build time (see below), and, for their Fourier transforms
(ConvolveCImg, ZDefocusCImg), [pocketfft](https://github.com/mreineck/pocketfft)
(BSD-3-Clause, header-only, downloaded at build time; the default), or optionally
[FFTW](https://www.fftw.org) (GPL, opt-in). Builds with pocketfft include its licence,
`Licence_pocketfft.txt`, in `CImg.ofx.bundle/Contents/Resources`.

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

* DiscBladesOFX: Generate a lens aperture shape (disc, or polygon with curved blades), to be used e.g. as the Filter input of ConvolveCImg.
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

* LayerRenameOFX: Rename the layers of the input with find and replace (on hosts that support multiple planes, e.g. Natron).
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
* Transform/RGBToYIQ, YIQToRGB, RGBToYDbDr and YDbDrToRGB: Convert to/from YIQ (NTSC) and YDbDr (SECAM) color representations.
* Transform/RGB709ToLab and LabToRGB709: Convert to/from Lab color representation.
* Transform/RGB709ToXYZ and XYZToRGB709: Convert to/from XYZ color representation.
* VectorToColor: Convert x and y vector components to a color representation.
* WhiteBalanceOFX: Correct the white balance by a chromatic adaptation (Bradford, CAT02...) from a source white (color temperature and tint, or a picked neutral color) to a target white, in linear RGB.

### Filter

* BlurCImg: Blur input stream by a quasi-Gaussian or Gaussian filter (recursive implementation), or compute derivatives.
* ChromaBlurCImg: Blur the chrominance components (usually to prep strongly compressed and chroma subsampled footage for keying).
* ConvolveCImg: Convolve the input by an arbitrary kernel image (e.g. an aperture shape, for bokeh effects), using the FFT.
* DenoiseSharpen: Denoise and/or sharpen images using wavelet-based algorithms.
* DilateCImg/ErodeCImg: Dilate/erode input stream by a rectangular structuring element of specified size and Neumann (a.k.a. nearest) boundary conditions.
* DirBlurOFX: Directional blur.
* Distance: Compute the distance from each pixel to the closest zero-valued pixel.
* EdgeDetectCImg: Perform edge detection by computing the image gradient magnitude.
* EdgeExtend: Fill a matte (i.e. a non-opaque color image with an alpha channel) by extending the edges of the matte.
* ErodeBlurCImg: Erode or dilate a mask by smoothing.
* ErodeSmoothCImg: Erode or dilate input stream using a [normalized power-weighted filter](http://dx.doi.org/10.1109/ICPR.2004.1334273).
* G'MIC Expression: Quickly generate or process image from mathematical formula evaluated for each pixel.
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
* ZDefocusCImg: Depth-dependent defocus (depth of field), using a depth or disparity map, with Gaussian, antialiased disc, or custom (bokeh kernel from the Filter input, e.g. made with DiscBlades) aperture.

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

* ImageStatisticsOFX: Compute statistics over an image or a rectangular area, over a frame or a frame range, and the auto crop box: the bounding box of the pixels that are not black (by default), not zero, not transparent, or above a luminance threshold.
* NoOpOFX: Copies the input to the output. Useful for inspecting the properties of input and output clips.
* TestRenderOFX: Test some rendering features of the OFX host: tiles, multiresolution and render scale (Test bundle, debug builds only).
* TestIdentityLeftHalfOFX: Test the renderWindow passed to isIdentity. The right half must always be inverted, even after zooming on the left half and panning right (Test bundle).
* TestGarbageOutsideRenderWindowOFX: A buggy plugin that writes magenta outside its renderWindow. Magenta in the result is a host bug (Test bundle, debug builds only).
* TestInfiniteRoIOFX: Asks for an infinite region of interest. The host must not hang, run out of memory or crash (Test bundle).
* TestInfiniteRoDOFX: Returns an infinite region of definition. The checkerboard around the input must be clipped by the host, typically to the project extent (Test bundle).

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

The same happens with other plugins using parametric parameters, such as TuttleHistogramKeyer.

Parametric parameters work in older versions of Nuke (at least in Nuke 6 & 7).

DaVinci Resolve does not support parametric parameters.

Please [file an issue](https://github.com/NatronGitHub/openfx-misc/issues) if you think openfx-misc is doing something wrong, or you know of other hosts which have problems with parametric paremeters.

Although ColorCorrect uses parametric parameters, it can still be instanciated on Nuke 8 & 9 and on DaVinci Resolve, but the curve ranges are not adjustable (shadows are decreasing linearly from 0 to 0.09, and highlights are increasing linearly from 0.5 to 1.0).


### Retime output does not contain motion blur, where is the "box" filter and the "shutter" parameter?

We should take the code from FrameBlend, simplify, and incorporate it in Retime.


Installation
------------

To install prebuilt binaries, see [Binaries](#binaries) above. This section
explains how to compile the plugins from source.

The plugins can be compiled with CMake or with the provided Makefiles, on Linux,
macOS, and Windows (MSYS2/MinGW-w64; CMake also supports Visual Studio). Both build
the same bundles; Natron is moving all its builds to CMake (see
[Natron#1181](https://github.com/NatronGitHub/Natron/issues/1181)). The Xcode project
and the Visual Studio project are alternatives, which may not always be up to date.

Whichever method you use, the compiled `*.ofx.bundle` directories must end up in
the standard OpenFX plugins directory (listed in step 4 of [Binaries](#binaries)).

### Getting the sources from github

To fetch the latest sources from github, execute the following commands:

	git clone https://github.com/NatronGitHub/openfx-misc.git
	cd openfx-misc
	git submodule update --init --recursive

In order to get a specific tag, corresponding to a source release, do `git tag -l`
to get the list of tags, and then `git checkout tags/<tag_name>`
to checkout a given tag.

### Unix/Linux/FreeBSD/OS X, using Makefiles

On Unix-like systems, the plugins can be compiled by typing in a
terminal:
- `make [options]` to compile as a single combined plugin (see below
  for valid options).
- `make nomulti [options]` to compile as separate plugins (useful if
only a few plugins are needed, for example). `make` can also be
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

The Makefiles do not choose a C++ standard: they use the compiler's
default (`gnu++17` for GCC >= 11 and Clang >= 16, but `gnu++14` for
Apple clang), whereas the CMake, Xcode and Visual Studio builds use
C++17 by default. The code must compile as C++17 and C++20. To choose
the standard, add it to the compiler command or to the flags, for
example `make CXX="g++ -std=c++20"` (as Natron's build scripts do) or
`make CXXFLAGS_ADD=-std=c++20`. On macOS, pass `-std=c++17` (or
`-std=c++20`) so that make builds match the supported standards.
`CXXFLAGS_ADD` given on the command line replaces any other value
(e.g. `-fopenmp`), so the two must be combined:
`make CXXFLAGS_ADD="-fopenmp -std=c++20" LDFLAGS_ADD=-fopenmp`.

See the file `Makefile.master` in the toplevel directory for other useful
flags/variables.

The CImg plugins also take `FFT=pocketfft|pow2|fftw3`, the library of the Fourier
transforms of ConvolveCImg, ZDefocusCImg and the `fft()` of ExpressionCImg (see
`CImg/license.mk`):
- `pocketfft` (default): header-only, downloaded at a pinned commit, any image size.
  It has no dependency and no global state shared with other plugins, and it was faster
  than FFTW in our benchmarks. On Windows it is single-threaded (`POCKETFFT_NO_MULTITHREADING`,
  as openfx-gmic#94): unloading the plugin deadlocked when its thread pool joined its
  threads under the loader lock.
- `pow2`: CImg's own FFT, with no library. The images are padded to a power of two,
  which is slower and uses more memory.
- `fftw3`: FFTW, opt-in and GPL only. It needs FFTW >= 3.3.5, found with
  `pkg-config fftw3`, plus `fftw3_threads` (MacPorts' `fftw-3`, MSYS2's
  `${MINGW_PACKAGE_PREFIX}-fftw`, Ubuntu's `libfftw3-dev`). FFTW is a shared library
  with a single global planner that other plugins in the same host (e.g. openfx-gmic)
  also use, so the plugins make its planner thread-safe at load.

`FFT` is an option of the Makefiles and of CMake only. The Xcode and Visual Studio
projects (`Misc.xcodeproj`, `Misc/CImg.vcxproj`) define neither `cimg_use_pocketfft`
nor `cimg_use_fftw3`, and do not fetch pocketfft: their CImg plugins always use `pow2`,
whatever the `FFT` of the make or CMake build that downloaded `CImg.h`.

`LICENSE=GPL` (default) or `LICENSE=COMMERCIAL` (Natron's `NATRON_LICENSE`, as in
openfx-gmic) only matters for FFTW: COMMERCIAL builds refuse `FFT=fftw3`.

`CImg/CImg.h` (and `CImg/Inpaint/inpaint.h`) are downloaded by `make -C CImg sources`
(run by the top-level `make`) at the version given in `CImg/CImgVersion.mk`. `CImg.h`
is checked against `CIMG_SHA256` there (the build fails on a mismatch), then patched the
same way as in [openfx-gmic](https://github.com/NatronGitHub/openfx-gmic), which uses the same CImg
version: `CImg/patches/cimg-*.sh` are copies of openfx-gmic's `patches/cimg-*.sh` (the
source of truth), at the openfx-gmic commit `CIMG_PATCHES_FROM` of
`CImg/CImgVersion.mk`, applied in the same order, so that both get the same `CImg.h`
(the same for every `FFT`: CImg 4.0.8 has pocketfft support itself, `cimg_use_pocketfft`,
and includes `pocketfft_hdronly.h` from the `CImg` directory). They need a POSIX `sh`,
`awk` and `wc`. A nightly CI job (`.github/tools/check-cimg-patches.sh`) reports when
openfx-gmic's scripts or G'MIC version change.

The compiled plugins are placed in subdirectories named after the
configuration, for example `Linux-64-release` for a 64-bits Linux
compilation. In each of these directories, a `*.bundle` directory is
created, which has to be moved to the proper place
(`/usr/OFX/Plugins`on Linux, or `/Library/OFX/Plugins`on OS X), using
a command like the following, with the *same* options used for
compiling:

	sudo make install [options]

### Using CMake

CMake builds the same bundles as the Makefiles (`make nomulti`: `OFX_PLUGIN_BUNDLES=SEPARATE`
below): `Misc`, `CImg` and `Shadertoy`, plus `Test` and
`DebugProxy` in Debug builds. It needs CMake 3.21 or later (openfx's `cmake/OpenFX.cmake`). Only out-of-tree
builds are allowed. `CImg/CImg.h` and `CImg/Inpaint/inpaint.h` are downloaded (and
`CImg.h` patched as openfx-gmic's, see above) at configure time if missing (at the
version given in `CImg/CImgVersion.mk`, again when it, the patches or `FFT` change).
The bundles are assembled in `<build>/Plugins`, and `cmake --install` copies them into `<prefix>/OFX/Plugins` (openfx's `PLUGIN_INSTALLDIR`, relative to
`CMAKE_INSTALL_PREFIX`). For the OpenFX plugins directory (as `make install`), use the
prefix `/usr` on Linux and `/Library` on macOS:

	cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
	cmake --build build --parallel
	sudo cmake --install build --prefix /usr     # Linux: /usr/OFX/Plugins
	sudo cmake --install build --prefix /Library # macOS: /Library/OFX/Plugins

On Windows, use `--prefix "C:/Program Files/Common Files"`. This is a change from the
earlier CMake build, which installed the bundles at the root of `CMAKE_INSTALL_PREFIX`
(`/usr/local` by default); `-DPLUGIN_INSTALLDIR=<dir>` sets another directory, relative
to the prefix (or absolute). The main options are:

- `-DCMAKE_BUILD_TYPE=Release` (default; CMake's `-O3 -DNDEBUG`, where `make` uses
  `-O2`: add `-DOFX_OPTFLAG=-O2` (openfx's option: the plugins and openfx::Support), or
  `-DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG"` (all targets), for that), `Debug` or
  `RelWithDebInfo` (`make CONFIG=...`).
- `-DOFX_CXX_STANDARD=17` (default) or `20`.
- `-DOFX_USE_OPENMP=ON` (default): OpenMP in the CImg plugins and DenoiseSharpen, if
  found (`make OPENMP=1`). With Apple clang, install libomp with MacPorts
  (`sudo port install libomp`), or give the prefix of another libomp with
  `-DOFX_LIBOMP_ROOT=<prefix>`. With MSYS2, install libgomp
  (`pacman -S ${MINGW_PACKAGE_PREFIX}-libgomp`); the bundles then need `libgomp-1.dll`
  even with `-static`, so the CI builds Windows without OpenMP. MSVC
  builds do not use OpenMP.
- `-DOFX_DEPLOY_BUNDLES=ON`: at install, copy the non-system libraries into the bundles
  (on macOS, `osxDeploy.sh`, which needs GNU sed (`sudo port install gsed`), as
  `make CONFIG=release` does). The bundles then also need these libraries' minimum
  macOS version (e.g. MacPorts' libomp is built for the build machine's macOS).
- `-DBUILD_CIMG_PLUGINS=OFF` (`make HAVE_CIMG=0`), `-DBUILD_SHADERTOY=OFF`,
  `-DBUILD_MISC=OFF` (with `-DBUILD_CIMG_PLUGINS=OFF`: only `Shadertoy.ofx.bundle`).
- `-DOFX_PLUGIN_BUNDLES=SEPARATE` (openfx's option; default `COMBINED`): instead of
  `Misc`, `CImg` and `Shadertoy`, one bundle per plugin directory (as `make nomulti`),
  named after its OFX identifier (e.g. `net.sf.openfx.Invert.ofx.bundle`,
  `net.sf.cimg.CImgBlur.ofx.bundle`), with that directory's icons only. Each is an
  install component named after the directory, with a `CImg` prefix for the CImg plugins
  (`cmake --install build --component Invert`, `--component CImgBlur`). The plugins are listed in `cmake/OFXMiscPlugins.cmake` (one line each). Install
  either set, never both: they have the same plugins.
- `-DOFXMISC_STANDALONE_BUNDLES="DenoiseSharpen"`: with the combined bundles, also build
  these Misc plugins as their own bundle (`net.sf.openfx.DenoiseSharpen.ofx.bundle`), for
  the binary releases. They have the same plugins as `Misc.ofx.bundle`: install one or
  the other.
- `-DFFT=pocketfft|pow2|fftw3` (`make FFT=...`): the FFT of the CImg plugins, see above
  (default `pocketfft`; `fftw3` is opt-in), and `-DOFX_LICENSE=GPL` (default) or
  `COMMERCIAL` (`make LICENSE=...`, as openfx-gmic's option), which refuses `fftw3`.
  With MSVC, the CImg.h patches need a POSIX `sh` in the `PATH` (e.g. Git for
  Windows'), or `-DCIMG_PATCH_SH=<path>`.
- `-DBUILD_TESTING=ON` (`make TESTS=1`) and `-DBUILD_DEBUGPROXY=ON`: the Test and
  DebugProxy bundles. Their default is ON in Debug builds, at the first configure of a
  build directory: set them explicitly when changing `CMAKE_BUILD_TYPE` there.
- MSVC: `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` links the C and C++ runtime
  statically (no Visual C++ redistributable needed), as the nightly builds
  (`.github/tools/msvc-build.sh`). Arm64X (one binary for Arm64EC and ARM64 hosts):
  an ARM64 build with `-DOFXMISC_ARM64X=ARM64 -DOFXMISC_ARM64X_DIR=<dir>`, then an
  Arm64EC build (`CFLAGS=-arm64EC CXXFLAGS=-arm64EC`) with `-DOFXMISC_ARM64X=ARM64EC` and the
  same `<dir>` (see `cmake/OFXMiscArm64X.cmake`, and `msvc-build.sh arm64x`).
- MinGW: `-DCMAKE_MODULE_LINKER_FLAGS=-static` links the MinGW runtime (libstdc++,
  libgcc, winpthread) statically, as the CI does, for hosts that do not ship it.
- `-DUSE_OSMESA=ON` (`make -C Shadertoy OSMESA=1`): Shadertoy CPU rendering with a
  mangled OSMesa in `OSMESA_PATH` (default `/opt/osmesa`) and LLVM in `LLVM_PATH`
  (default `/opt/llvm`). With `-DOSMESA_PKGCONFIG_STATIC=ON` (as the binary releases),
  the static `libMangledOSMesa32.a` is linked with the flags of `pkg-config --static`
  (the `Libs.private` of `OSMESA_PATH`'s `osmesa.pc`); by default, those of
  `pkg-config --libs`.
- `-DOFX_BUILD_HOSTDEMO=ON`: also build openfx's test host `hostDemo` and install it into
  `<prefix>/bin`, for the render tests: `.github/tools/rendertest.sh <prefix>` (checks
  the outputs of a few plugins) and `.github/tools/smoketest.sh <prefix>` (renders every
  plugin with its default parameters). hostDemo uses openfx's HostSupport library, which
  needs the system expat: `libexpat1-dev` (Debian, Ubuntu), `expat-devel` (Rocky, Fedora),
  MSYS2 `mingw-w64-ucrt-x86_64-expat`, vcpkg `expat`; on macOS, MacPorts' `expat`
  (`sudo port install expat`) or else the SDK's. The bundles, including DebugProxy, do not
  need expat.
- `-DOFXMISC_SUPPORTEXT_DIR=<dir>`: build with the openfx-supportext sources in `<dir>`
  instead of the `SupportExt` submodule (`smoketest.sh --supportext <dir> <prefix>`
  builds and tests that).

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
