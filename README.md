OpenFX-Misc [![Build Status](https://api.travis-ci.org/devernay/openfx-misc.png?branch=master)](https://travis-ci.org/devernay/openfx-misc) [![Bitdeli Badge](https://d2weczhvl823v0.cloudfront.net/devernay/openfx-misc/trend.png)](https://bitdeli.com/free "Bitdeli Badge")
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

* ConstantOFX: Generator for an image with a uniform color

### Draw

* RotoOFX: Create masks and shapes. Requires a host with mask editing
  capabilities (such as Natron).

### Time

* TimeOffsetOFX: Move the input clip forward or backward in time.  

### Channel

* ShuffleOFX: Rearrange channels from one or two inputs, and convert
  to a different bit depth (on hosts that support it)

### Color

* ColorCorrectOFX: Adjusts the saturation, constrast, gamma, gain and offset of an image.  

* GradeOFX: Modify the tonal spread of an image from the white and black points.  

* RGBLutOFX: Apply a parametric lookup curve to each channel  
  separately.  
   
### Keyer

* ChromaKeyerOFX: Apply chroma keying, as described in "Video Demystified" by Keith Jack.  

* DifferenceOFX: Produce a rough matte from the difference of two
  images.
  
### Merge

* MergeOFX: Pixel-by-pixel merge operation between the two inputs.

* SwitchOFX: Lets you switch between any number of inputs.

### Transform

* TransformOFX and TransformMaskedOFX: Translate / Rotate / Scale a 2D
  image.

* CornerPinOFX and CornerPinMaskedOFX: Fit an image to another in
  translation, rotation, scale, and shear

* CropOFX: Remove everything outside from the image of a rectangle.

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

Installation
------------

These plugins are distributed precompiled with each version of
[Natron](http://natron.inria.fr).

If you want to compile the plugins from source, you may either use the
provided Unix Makefile, the Xcode project, or the Visual Studio project.

### Unix/Linux/OS X, using Makefiles

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

RGBLut, Switch, TimeOffset, ChromaKeyer, Difference, Constant, Shuffle
are by Frederic Devernay.

Merge, ColorCorrect, Grade, Roto, Crop, CopyRectangle  are by
Alexandre Gauthier.

Transform and CornerPin are by Frederic Devernay and Alexandre Gauthier.
