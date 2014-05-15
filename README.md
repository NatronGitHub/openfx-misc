OpenFX-Misc [![Build Status](https://api.travis-ci.org/devernay/openfx-misc.png?branch=master)](https://travis-ci.org/devernay/openfx-misc) [![Bitdeli Badge](https://d2weczhvl823v0.cloudfront.net/devernay/openfx-misc/trend.png)](https://bitdeli.com/free "Bitdeli Badge")
===========

Miscellaneous OFX / OpenFX / Open Effects plugins

Draw
----

* RotoOFX: Create masks and shapes. Requires a host with mask editing
  capabilities (such as Natron).

Time
----

* TimeOffsetOFX: Move the input clip forward or backward in time.  
 
Color
-----

* ColorCorrectOFX: Adjusts the saturation, constrast, gamma, gain and offset of an image.  

* GradeOFX: Modify the tonal spread of an image from the white and black points.  

* RGBLutOFX: Apply a parametric lookup curve to each channel  
  separately.  
   
Keyer
-----

* ChromaKeyerOFX: Apply chroma keying, as described in "Video Demystified" by Keith Jack.  
 
Merge
-----

* MergeOFX: Pixel-by-pixel merge operation between the two inputs.

* SwitchOFX: Lets you switch between any number of inputs.

Transform
---------

* TransformOFX and TransformMaskedOFX: Translate / Rotate / Scale a 2D image. 

Views
-----

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
  
Credits
-------

The stereoscopic plugins Anaglyph, JoinViews, MixViews, OneView,
ReConverge, SideBySide are by Frederic Devernay.

RGBLut (parametic LUTS for each channel), Switch (select one of of n
inputs), TimeOffset (shift time), ChromaKeyer are by Frederic Devernay

Merge, ColorCorrect, Grade are by Alexandre Gauthier.

Transform is by Frederic Devernay and Alexandre Gauthier.
