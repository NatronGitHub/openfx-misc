/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX Distortion plugins.
 */

/* TODO:
 - optionally expand/contract RoD in LensDistortion,
    see PFBarrelCommon::calculateOutputRect (sample 10 points on each side of image rect)
 */
/*
   Although the indications from nuke/fnOfxExtensions.h were followed, and the
   kFnOfxImageEffectActionGetTransform action was implemented in the Support
   library, that action is never called by the Nuke host, so it cannot be tested.
   The code is left here for reference or for further extension.

   There is also an open question about how the last plugin in a transform chain
   may get the concatenated transform from upstream, the untransformed source image,
   concatenate its own transform and apply the resulting transform in its render
   action. Should the host be doing this instead?
 */
// This node concatenates transforms upstream.

#include <cmath>
#include <cfloat> // DBL_MAX
#include <cstdlib> // atoi
#include <cstdio> // fopen
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <limits>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/gl.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsFilter.h"
#include "ofxsMatrix2D.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsMultiPlane.h"
#include "ofxsGenerator.h"
#include "ofxsFormatResolution.h"

#include "DistortionModel.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginIDistortName "IDistortOFX"
#define kPluginIDistortGrouping "Transform"
#define kPluginIDistortDescription \
    "Distort an image, based on a displacement map.\n" \
    "The U and V channels give the offset in pixels in the destination image to the pixel where the color is taken. " \
    "For example, if at pixel (45,12) the UV value is (-1.5,3.2), then the color at this pixel is taken from (43.5,15.2) in the source image. " \
    "This plugin concatenates transforms upstream, so that if the nodes upstream output a 3x3 transform " \
    "(e.g. Transform, CornerPin, Dot, NoOp, Switch), the original image is sampled only once.\n" \
    "This plugin concatenates transforms upstream." \

#define kPluginIDistortIdentifier "net.sf.openfx.IDistort"

#define kPluginSTMapName "STMapOFX"
#define kPluginSTMapGrouping "Transform"
#define kPluginSTMapDescription \
    "Move pixels around an image, based on a UVmap.\n" \
    "The U and V channels give, for each pixel in the destination image, the normalized position of the pixel where the color is taken. " \
    "(0,0) is the bottom left corner of the input image, while (1,1) is the top right corner. " \
    "This plugin concatenates transforms upstream, so that if the nodes upstream output a 3x3 transform " \
    "(e.g. Transform, CornerPin, Dot, NoOp, Switch), the original image is sampled only once.\n" \
    "This plugin concatenates transforms upstream." \

#define kPluginSTMapIdentifier "net.sf.openfx.STMap"

#define kPluginLensDistortionName "LensDistortionOFX"
#define kPluginLensDistortionGrouping "Transform"
#define kPluginLensDistortionDescription \
    "Add or remove lens distortion, or produce an STMap that can be used to apply that transform.\n" \
    "The region of definition of the transformed image is computed from the region of definition of the Source input. If the input is defined outside of the project format, this may result in a very large region. A Crop effect may be inserted before LensDistortion to avoid this. If the input region of definition is inside the format, the Crop To Format parameter may be used to avoid expanding it.\n" \
    "LensDistortion can directly apply distortion/undistortion, but if the distortion parameters are not animated, the most efficient way to use LensDistortion and avoid repeated distortion function calculations is the following:\n" \
    "- If the footage size is not the same as the project size, insert a FrameHold plugin between the footage to distort or undistort and the Source input of LensDistortion. This connection is only used to get the size of the input footage.\n" \
    "- Set Output Mode to \"STMap\" in LensDistortion.\n" \
    "- feed the LensDistortion output into the UV input of STMap, and feed the footage into the Source input of STMap.\n" \
    "This plugin concatenates transforms upstream." \

#define kPluginLensDistortionIdentifier "net.sf.openfx.LensDistortion"

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

/* LensDistortion TODO:
   - cache the STmap for a set of input parameter and input image size
   - compute the inverse map and undistort
   - implement other distortion models (PFBarrel, OpenCV)
 */

// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 3.0: use format instead of rod as the default for distortion domain
// version 4.0: add the CropToFormat parameter
#define kPluginVersionLensDistortionMajor 4 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionLensDistortionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

enum DistortionPluginEnum
{
    eDistortionPluginSTMap,
    eDistortionPluginIDistort,
    eDistortionPluginLensDistortion,
};

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel, kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel, kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel, kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel, kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R", "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G", "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B", "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A", "Process alpha component."
#endif

#define kParamChannelU "channelU"
#define kParamChannelULabel "U Channel", "Input U channel from UV."

#define kParamChannelUChoice kParamChannelU "Choice"

#define kParamChannelV "channelV"
#define kParamChannelVLabel "V Channel", "Input V channel from UV."

#define kParamChannelVChoice kParamChannelV "Choice"

#define kParamChannelA "channelA"
#define kParamChannelALabel "Alpha Channel", "Input Alpha channel from UV. The Output alpha is set to this value. If \"Unpremult UV\" is checked, the UV values are divided by alpha."

#define kParamChannelAChoice kParamChannelA "Choice"

#define kParamChannelUnpremultUV "unpremultUV"
#define kParamChannelUnpremultUVLabel "Unpremult UV", "Unpremult UV by Alpha from UV. Check if UV values look small for small values of Alpha (3D software sometimes write premultiplied UV values)."

#define kParamPremultChanged "premultChanged"

#define kClipUV "UV"


enum InputChannelEnum
{
    eInputChannelR = 0,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
    eInputChannel0,
    eInputChannel1,
};

#define kParamWrapU "wrapU"
#define kParamWrapULabel "U Wrap Mode", "Wrap mode for U coordinate."

#define kParamWrapV "wrapV"
#define kParamWrapVLabel "V Wrap Mode", "Wrap mode for V coordinate."

#define kParamWrapOptionClamp "Clamp", "Texture edges are black (if blackOutside is checked) or stretched indefinitely.", "clamp"
#define kParamWrapOptionRepeat "Repeat", "Texture is repeated.", "repeat"
#define kParamWrapOptionMirror "Mirror", "Texture is mirrored alternatively.", "mirror"

enum WrapEnum
{
    eWrapClamp = 0,
    eWrapRepeat,
    eWrapMirror,
};


#define kParamUVOffset "uvOffset"
#define kParamUVOffsetLabel "UV Offset", "Offset to apply to the U and V channel (useful if these were stored in a file that cannot handle negative numbers)"

#define kParamUVScale "uvScale"
#define kParamUVScaleLabel "UV Scale", "Scale factor to apply to the U and V channel (useful if these were stored in a file that can only store integer values)"

#define kParamFormat kParamGeneratorExtent
#define kParamFormatLabel "Format", "Reference format for lens distortion."

#define kParamDistortionModel "model"
#define kParamDistortionModelLabel "Model", "Choice of the distortion model, i.e. the function that goes from distorted to undistorted image coordinates."
#define kParamDistortionModelOptionNuke "Nuke", "The model used in Nuke's LensDistortion plugin.", "nuke"
#define kParamDistortionModelOptionPFBarrel "PFBarrel", "The PFBarrel model used in PFTrack by PixelFarm.", "pfbarrel"
#define kParamDistortionModelOption3DEClassic "3DE Classic", "Degree-2 anamorphic and degree-4 radial mixed model, used in 3DEqualizer by Science-D-Visions. Works, but it is recommended to use 3DE4 Radial Standard Degree 4 or 3DE4 Anamorphic Standard Degree 4 instead.", "3declassic"
#define kParamDistortionModelOption3DEAnamorphic6 "3DE4 Anamorphic Degree 6", "Degree-6 anamorphic model, used in 3DEqualizer by Science-D-Visions.", "3deanamorphic6"
#define kParamDistortionModelOption3DEFishEye8 "3DE4 Radial Fisheye Degree 8", "Radial lens distortion model with equisolid-angle fisheye projection, used in 3DEqualizer by Science-D-Visions.", "3defisheye8"
#define kParamDistortionModelOption3DEStandard "3DE4 Radial Standard Degree 4", "Radial lens distortion model, a.k.a. radial decentered cylindric degree 4, which compensates for decentered lenses (and beam splitter artefacts in stereo rigs), used in 3DEqualizer by Science-D-Visions.", "3deradial4"
#define kParamDistortionModelOption3DEAnamorphic4 "3DE4 Anamorphic Standard Degree 4", "Degree-4 anamorphic model with anamorphic lens rotation, which handles 'human-touched' mounted anamorphic lenses, used in 3DEqualizer by Science-D-Visions.", "3deanamorphic4"
#define kParamDistortionModelOptionPanoTools "PanoTools", "The model used in PanoTools, PTGui, PTAssembler, Hugin. See http://wiki.panotools.org/Lens_correction_model", "panotools"

/*
   Possible distortion models:
   (see also <http://michaelkarp.net/distortion.htm>)

   From Oblique <https://github.com/madesjardins/Obq_Shaders/wiki/Obq_LensDistortion>
   PFBarrel: PFTrack's distortion model.
   Nuke: Nuke's distortion model.
   3DE Classic LD Model: Degree-2 anamorphic and degree-4 radial mixed model
 Science-D-Visions LDPK (3DEqualizer). see <http://www.3dequalizer.com/user_daten/tech_docs/pdf/ldpk.pdf>
   3DE4 Anamorphic, Degree 6:
   3DE4 Radial - Fisheye, Degree 8:
   3DE4 Radial - Standard, Degree 4: A deprecated model.
   3DE4 Radial - Decentered Cylindric, Degree 4:
   3DE4 Anamorphic Rotate Squeeze, Degree 4:

   From RV4 <http://www.tweaksoftware.com/static/documentation/rv/rv-4.0.17/html/rv_reference.html#RVLensWarp>
   “brown”, “opencv”, “pfbarrel”, “adobe”, “3de4_anamorphic_degree_6”

   Panorama Tools/PtGUI/Hugin/Card3D:
   http://wiki.panotools.org/Lens_correction_model
   http://www.ptgui.com/ptguihelp/main_lens.htm
   http://www.nukepedia.com/written-tutorials/the-lens-distortion-model-in-the-card-node-explained/
   https://web.archive.org/web/20010409044720/http://www.fh-furtwangen.de/~dersch/barrel/barrel.html

   OpenCV / Matlab Camera Calibration Toolbox:
   - https://docs.opencv.org/trunk/d9/d0c/group__calib3d.html
     intrinsic parameters are (fx, fy, cx, cy). fx,fy are the focal lengths expressed in pixel units. (cx,cy) is a principal point that is usually at the image center. These depend on the image resolution.
     distortion coefficients are a vector (k1,k2,p1,p2[,k3[,k4,k5,k6[,s1,s2,s3,s4[,τx,τy]]]]) of 4, 5, 8, 12 or 14 elements. These are independent of image resolution.

   OpenCV 3 Fisheye / Matlab Camera Calibration Toolbox Fisheye / Kannala-Brandt:
   - A Generic Camera Model and Calibration Method for Conventional, Wide-Angle, and Fish-Eye Lenses <http://www.ee.oulu.fi/~jkannala/publications/tpami2006.pdf>
     The "undocumented" fisheye model contained in the calibration toolbox follows the equidistance projection model described by equation (3) in this very nice paper. The distortion model follows equation (6), to the exception that k1=1 (otherwise indistinguishable from f).
   - https://docs.opencv.org/trunk/db/d58/group__calib3d__fisheye.html
     intrinsic parameters are (fx, fy, cx, cy). fx,fy are the focal lengths expressed in pixel units. (cx,cy) is a principal point that is usually at the image center. These depend on the image resolution.
     distortion coefficients are (k1,k2,k3,k4): θd=θ(1+k1θ^2+k2θ^4+k3θ^6+k4θ^8) (θ is the angle of the incoming ray)
    The distorted point coordinates are [x'; y'] where
      x′=(θd/r)a, y′=(θd/r)b (r = sqrt((x/z)^2+(y/z)^2)
    Finally, conversion into pixel coordinates: The final pixel coordinates vector [u; v] where:
      u=fx(x′+αy′)+cx, v=fyy′+cy
   - https://stackoverflow.com/questions/31089265/what-are-the-main-references-to-the-fish-eye-camera-model-in-opencv3-0-0dev
 */

enum DistortionModelEnum
{
    eDistortionModelNuke,
    eDistortionModelPFBarrel,
    eDistortionModel3DEClassic,
    eDistortionModel3DEAnamorphic6,
    eDistortionModel3DEFishEye8,
    eDistortionModel3DEStandard,
    eDistortionModel3DEAnamorphic4,
    eDistortionModelPanoTools,
};


#define kParamDistortionDirection "direction"
#define kParamDistortionDirectionLabel "Direction", "Should the output corrspond to applying or to removing distortion."
#define kParamDistortionDirectionOptionDistort "Distort", "The output corresponds to applying distortion."
#define kParamDistortionDirectionOptionUndistort "Undistort", "The output corresponds to removing distortion."

enum DirectionEnum {
    eDirectionDistort,
    eDirectionUndistort,
};

#define kParamDistortionOutputMode "outputMode"
#define kParamDistortionOutputModeLabel "Output Mode", "Choice of the output, which may be either a distorted/undistorted image, or a distortion/undistortion STMap."
#define kParamDistortionOutputModeOptionImage "Image", "The output is the distorted/undistorted Source."
#define kParamDistortionOutputModeOptionSTMap "STMap", "The output is a distortion/undistortion STMap. It is recommended to insert a FrameHold node at the Source input so that the STMap is computed only once if the parameters are not animated."

enum OutputModeEnum {
    eOutputModeImage,
    eOutputModeSTMap,
};

#define kParamK1 "k1"
#define kParamK1Label "K1", "Nuke: First radial distortion coefficient (coefficient for r^2)."

#define kParamK2 "k2"
#define kParamK2Label "K2", "Nuke: Second radial distortion coefficient (coefficient for r^4)."

#if 0
#define kParamK3 "k3"
#define kParamK3Label "K3", "Nuke: Third radial distortion coefficient (coefficient for r^6)."

#define kParamP1 "p1"
#define kParamP1Label "P1", "Nuke: First tangential distortion coefficient."

#define kParamP2 "p2"
#define kParamP2Label "P2", "Nuke: Second tangential distortion coefficient."
#endif

#define kParamCenter "center"
#define kParamCenterLabel "Center", "Nuke: Offset of the distortion center from the image center."

#define kParamSqueeze "anamorphicSqueeze"
#define kParamSqueezeLabel "Squeeze", "Nuke: Anamorphic squeeze (only for anamorphic lens)."

#define kParamAsymmetric "asymmetricDistortion"
#define kParamAsymmetricLabel "Asymmetric", "Nuke: Asymmetric distortion (only for anamorphic lens)."

#define kParamPFFile "pfFile"
#define kParamPFFileLabel "File", "The location of the PFBarrel .pfb file to use. Keyframes are set if present in the file."

#define kParamPFFileReload "pfReload"
#define kParamPFFileReloadLabel "Reload", "Click to reload the PFBarrel file"

#define kParamPFC3 "pfC3"
#define kParamPFC3Label "C3", "PFBarrel: Low order radial distortion coefficient."

#define kParamPFC5 "pfC5"
#define kParamPFC5Label "C5", "PFBarrel: Low order radial distortion coefficient."

#define kParamPFSqueeze "pfSqueeze"
#define kParamPFSqueezeLabel "Squeeze", "PFBarrel: Anamorphic squeeze (only for anamorphic lens)."

#define kParamPFP "pfP"
#define kParamPFPLabel "Center", "PFBarrel: The distortion center of the lens (specified as a factor rather than a pixel value)"

// 3D Equalizer 4

//Double_knob(callback,&_xa_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_xa_unit","fov left [unit coord]");
#define kParam3DE4_xa_fov_unit "tde4_field_of_view_xa_unit"
#define kParam3DE4_xa_fov_unitLabel "fov left [unit coord]", "3DE4: Field of view."

//Double_knob(callback,&_ya_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_ya_unit","fov bottom [unit coord]");
#define kParam3DE4_ya_fov_unit "tde4_field_of_view_ya_unit"
#define kParam3DE4_ya_fov_unitLabel "fov bottom [unit coord]", "3DE4: Field of view."

//Double_knob(callback,&_xb_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_xb_unit","fov right [unit coord]");
#define kParam3DE4_xb_fov_unit "tde4_field_of_view_xb_unit"
#define kParam3DE4_xb_fov_unitLabel "fov right [unit coord]", "3DE4: Field of view."

//Double_knob(callback,&_yb_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_yb_unit","fov top [unit coord]");
#define kParam3DE4_yb_fov_unit "tde4_field_of_view_yb_unit"
#define kParam3DE4_yb_fov_unitLabel "fov top [unit coord]", "3DE4: Field of view."

// First the seven built-in parameters, in this order.

//fl_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(0.5,50.0),"tde4_focal_length_cm","tde4 focal length [cm]");
#define kParam3DE4_focal_length_cm "tde4_focal_length_cm"
#define kParam3DE4_focal_length_cmLabel "tde4 focal length [cm]", "3DE4: Focal length."

//fd_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(10.0,1000.0),"tde4_custom_focus_distance_cm","tde4 focus distance [cm]");
#define kParam3DE4_custom_focus_distance_cm "tde4_custom_focus_distance_cm"
#define kParam3DE4_custom_focus_distance_cmLabel "tde4 focus distance [cm]", "3DE4: Focus distance."

//w_fb_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(.1,10.0),"tde4_filmback_width_cm","tde4 filmback width [cm]");
#define kParam3DE4_filmback_width_cm "tde4_filmback_width_cm"
#define kParam3DE4_filmback_width_cmLabel "tde4 filmback width [cm]", "3DE4: Filmback width."

//h_fb_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(.1,10.0),"tde4_filmback_height_cm","tde4 filmback height [cm]");
#define kParam3DE4_filmback_height_cm "tde4_filmback_height_cm"
#define kParam3DE4_filmback_height_cmLabel "tde4 filmback height [cm]", "3DE4: Filmback height."

//x_lco_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(-5.0,5.0),"tde4_lens_center_offset_x_cm","tde4 lens center offset x [cm]");
#define kParam3DE4_lens_center_offset_x_cm "tde4_lens_center_offset_x_cm"
#define kParam3DE4_lens_center_offset_x_cmLabel "tde4 lens center offset x [cm]", "3DE4: Lens center horizontal offset."

//y_lco_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(-5.0,5.0),"tde4_lens_center_offset_y_cm","tde4 lens center offset y [cm]");
#define kParam3DE4_lens_center_offset_y_cm "tde4_lens_center_offset_y_cm"
#define kParam3DE4_lens_center_offset_y_cmLabel "tde4 lens center offset y [cm]", "3DE4: Lens center vertical offset."

//pa: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(0.25,4.0),"tde4_pixel_aspect","tde4 pixel aspect");
#define kParam3DE4_pixel_aspect "tde4_pixel_aspect"
#define kParam3DE4_pixel_aspectLabel "tde4 pixel aspect", "3DE4: Pixel aspect ratio."


// 3DE_Classic_LD_Model
//"Distortion", -0.5 .. 0.5
#define kParam3DEDistortion "tde4_Distortion"
#define kParam3DEDistortionLabel "Distortion", "3DE Classic: Distortion."
//"Anamorphic Squeeze", 0.25 ... 4.
#define kParam3DEAnamorphicSqueeze "tde4_Anamorphic_Squeeze"
#define kParam3DEAnamorphicSqueezeLabel "Anamorphic Squeeze", "3DE Classic: Anamorphic Squeeze."

//"Curvature X", -0.5 .. 0.5
#define kParam3DECurvatureX "tde4_Curvature_X"
#define kParam3DECurvatureXLabel "Curvature X", "3DE Classic: Curvature X."

//"Curvature Y", -0.5 .. 0.5
#define kParam3DECurvatureY "tde4_Curvature_Y"
#define kParam3DECurvatureYLabel "Curvature Y", "3DE Classic: Curvature Y."

//"Quartic Distortion" -0.5 .. 0.5
#define kParam3DEQuarticDistortion "tde4_Quartic_Distortion"
#define kParam3DEQuarticDistortionLabel "Quartic Distortion", "3DE Classic: Quartic Distortion."

// 3DE4_Radial_Standard_Degree_4
#define kParam3DEDistortionDegree2 "tde4_Distortion_Degree_2"
#define kParam3DEDistortionDegree2Label "Distortion - Degree 2", "3DE Standard and Fisheye: Distortion."
#define kParam3DEUDegree2 "tde4_U_Degree_2"
#define kParam3DEUDegree2Label "U - Degree 2", "3DE Standard: U - Degree 2."
#define kParam3DEVDegree2 "tde4_V_Degree_2"
#define kParam3DEVDegree2Label "V - Degree 2", "3DE Standard: V - Degree 2."
#define kParam3DEQuarticDistortionDegree4 "tde4_Quartic_Distortion_Degree_4"
#define kParam3DEQuarticDistortionDegree4Label "Quartic Distortion - Degree 4", "3DE Standard and Fisheye: Quartic Distortion - Degree 4."
#define kParam3DEUDegree4 "tde4_U_Degree_4"
#define kParam3DEUDegree4Label "U - Degree 4", "3DE Standard: U - Degree 4."
#define kParam3DEVDegree4 "tde4_V_Degree_4"
#define kParam3DEVDegree4Label "V - Degree 4", "3DE Standard: V - Degree 4."
#define kParam3DEPhiCylindricDirection "tde4_Phi_Cylindric_Direction"
#define kParam3DEPhiCylindricDirectionLabel "Phi - Cylindric Direction", "3DE Standard: Phi - Cylindric Direction."
#define kParam3DEBCylindricBending "tde4_B_Cylindric_Bending"
#define kParam3DEBCylindricBendingLabel "B - Cylindric Bending", "3DE Standard: B - Cylindric Bending."

// 3DE4_Anamorphic_Standard_Degree_4
#define kParam3DECx02Degree2 "tde4_Cx02_Degree_2"
#define kParam3DECx02Degree2Label "Cx02 - Degree 2", "3DE Anamorphic 4 and 6: Cx02 - Degree 2."
#define kParam3DECy02Degree2 "tde4_Cy02_Degree_2"
#define kParam3DECy02Degree2Label "Cy02 - Degree 2", "3DE Anamorphic 4 and 6: Cy02 - Degree 2."
#define kParam3DECx22Degree2 "tde4_Cx22_Degree_2"
#define kParam3DECx22Degree2Label "Cx22 - Degree 2", "3DE Anamorphic 4 and 6: Cx22 - Degree 2."
#define kParam3DECy22Degree2 "tde4_Cy22_Degree_2"
#define kParam3DECy22Degree2Label "Cy22 - Degree 2", "3DE Anamorphic 4 and 6: Cy22 - Degree 2."
#define kParam3DECx04Degree4 "tde4_Cx04_Degree_4"
#define kParam3DECx04Degree4Label "Cx04 - Degree 4", "3DE Anamorphic 4 and 6: Cx04 - Degree 4."
#define kParam3DECy04Degree4 "tde4_Cy04_Degree_4"
#define kParam3DECy04Degree4Label "Cy04 - Degree 4", "3DE Anamorphic 4 and 6: Cy04 - Degree 4."
#define kParam3DECx24Degree4 "tde4_Cx24_Degree_4"
#define kParam3DECx24Degree4Label "Cx24 - Degree 4", "3DE Anamorphic 4 and 6: Cx24 - Degree 4."
#define kParam3DECy24Degree4 "tde4_Cy24_Degree_4"
#define kParam3DECy24Degree4Label "Cy24 - Degree 4", "3DE Anamorphic 4 and 6: Cy24 - Degree 4."
#define kParam3DECx44Degree4 "tde4_Cx44_Degree_4"
#define kParam3DECx44Degree4Label "Cx44 - Degree 4", "3DE Anamorphic 4 and 6: Cx44 - Degree 4."
#define kParam3DECy44Degree4 "tde4_Cy44_Degree_4"
#define kParam3DECy44Degree4Label "Cy44 - Degree 4", "3DE Anamorphic 4 and 6: Cy44 - Degree 4."
#define kParam3DELensRotation "tde4_Lens_Rotation"
#define kParam3DELensRotationLabel "Lens Rotation 4", "3DE Anamorphic 4: Lens Rotation 4."
#define kParam3DESqueezeX "tde4_Squeeze_X"
#define kParam3DESqueezeXLabel "Squeeze-X", "3DE Anamorphic 4: Squeeze-X."
#define kParam3DESqueezeY "tde4_Squeeze_Y"
#define kParam3DESqueezeYLabel "Squeeze-Y", "3DE Anamorphic 4: Squeeze-Y."

// 3DE4_Anamorphic_Degree_6
#define kParam3DECx06Degree6 "tde4_Cx06_Degree_6"
#define kParam3DECx06Degree6Label "Cx06 - Degree 6", "3DE Anamorphic 6: Cx06 - Degree 6."
#define kParam3DECy06Degree6 "tde4_Cy06_Degree_6"
#define kParam3DECy06Degree6Label "Cy06 - Degree 6", "3DE Anamorphic 6: Cy06 - Degree 6."
#define kParam3DECx26Degree6 "tde4_Cx26_Degree_6"
#define kParam3DECx26Degree6Label "Cx26 - Degree 6", "3DE Anamorphic 6: Cx26 - Degree 6."
#define kParam3DECy26Degree6 "tde4_Cy26_Degree_6"
#define kParam3DECy26Degree6Label "Cy26 - Degree 6", "3DE Anamorphic 6: Cy26 - Degree 6."
#define kParam3DECx46Degree6 "tde4_Cx46_Degree_6"
#define kParam3DECx46Degree6Label "Cx46 - Degree 6", "3DE Anamorphic 6: Cx46 - Degree 6."
#define kParam3DECy46Degree6 "tde4_Cy46_Degree_6"
#define kParam3DECy46Degree6Label "Cy46 - Degree 6", "3DE Anamorphic 6: Cy46 - Degree 6."
#define kParam3DECx66Degree6 "tde4_Cx66_Degree_6"
#define kParam3DECx66Degree6Label "Cx66 - Degree 6", "3DE Anamorphic 6: Cx66 - Degree 6."
#define kParam3DECy66Degree6 "tde4_Cy66_Degree_6"
#define kParam3DECy66Degree6Label "Cy66 - Degree 6", "3DE Anamorphic 6: Cy66 - Degree 6."

// 3DE4_Radial_Fisheye_Degree_8
#define kParam3DEDegree6 "tde4_Degree_6"
#define kParam3DEDegree6Label "Degree 6", "3DE Fisheye: Degree 6."
#define kParam3DEDegree8 "tde4_Degree_8"
#define kParam3DEDegree8Label "Degree 8", "3DE Fisheye: Degree 8."

// PanoTools
#define kParamPanoToolsA "pt_a"
#define kParamPanoToolsALabel "a", "PanoTools: Radial lens distortion 3rd degree coefficient a."
#define kParamPanoToolsB "pt_b"
#define kParamPanoToolsBLabel "b", "PanoTools: Radial lens distortion 2nd degree coefficient b."
#define kParamPanoToolsC "pt_c"
#define kParamPanoToolsCLabel "c", "PanoTools: Radial lens distortion 1st degree coefficient c."
#define kParamPanoToolsD "pt_d"
#define kParamPanoToolsDLabel "d", "PanoTools: Horizontal lens shift (in pixels)."
#define kParamPanoToolsE "pt_e"
#define kParamPanoToolsELabel "e", "PanoTools: Vertical lens shift (in pixels)."
#define kParamPanoToolsG "pt_g"
#define kParamPanoToolsGLabel "g", "PanoTools: Vertical lens shear (in pixels). Use to remove slight misalignment of the line scanner relative to the film transport."
#define kParamPanoToolsT "pt_t"
#define kParamPanoToolsTLabel "t", "PanoTools: Horizontal lens shear (in pixels)."

#define kParamCropToFormat "cropToFormat"
#define kParamCropToFormatLabel "Crop To Format"
#define kParamCropToFormatHint "If the source is inside the format and the effect extends it outside of the format, crop it to avoid unnecessary calculations. To avoid unwanted crops, only the borders that were inside of the format in the source clip will be cropped."



// PFBarrel file reader

// Copyright (C) 2011 The Pixel Farm Ltd
// The class that implements compositor-neutral functionality

class PFBarrelCommon
{

public:

    class FileReader
    {

    public:

        FileReader(const std::string &filename);
        ~FileReader();

        std::string readRawLine(void);
        std::string readLine(void);
        double readDouble(void);
        int readInt(void);

        void dump(void);

        FILE *f_;
        std::string error_;

        int version_;
        int orig_w_, orig_h_;
        double orig_pa_;
        int undist_w_, undist_h_;
        int model_;
        double squeeze_;
        int nkeys_;
        std::vector<int> frame_;
        std::vector<double> c3_;
        std::vector<double> c5_;
        std::vector<double> xp_;
        std::vector<double> yp_;
    };
};

PFBarrelCommon::FileReader::FileReader(const std::string &filename)
{
    std::string ln;

    version_= -1;
    error_= "";
    orig_w_= -1;
    orig_h_= -1;
    orig_pa_ = 1.;
    undist_w_= -1;
    undist_h_= -1;
    model_= -1;
    squeeze_= -1;
    nkeys_= 0;

    f_= std::fopen(filename.c_str(), "r");

    if (!f_) {
        error_= "Failed to open file";
        return;
    }

    ln= readRawLine();
    if (ln=="#PFBarrel 2011 v1") {
        version_= 1;
    } else if (ln=="#PFBarrel 2011 v2") {
        version_= 2;
    } else {
        error_= "Bad header";
        return;
    }

    orig_w_= readInt(); if (error_!="") return;
    orig_h_= readInt(); if (error_!="") return;

    if (version_==2) {
        orig_pa_= readDouble(); if (error_!="") return;
    } else {
        orig_pa_= 1.0;
    }

    undist_w_= readInt(); if (error_!="") return;
    undist_h_= readInt(); if (error_!="") return;

    ln= readLine(); if (error_!="") return;

    if (ln=="Low Order") {
        model_= 0;
    } else if (ln=="High Order") {
        model_= 1;
    } else {
        error_= "Bad model";
        return;
    }

    squeeze_= readDouble(); if (error_!="") return;
    nkeys_= readInt(); if (error_!="") return;

    for (int i=0; i<nkeys_; i++) {
        frame_.push_back(readInt()); if (error_!="") return;
        c3_.push_back(readDouble()); if (error_!="") return;

        double c5= readDouble(); if (error_!="") return;
        if (model_==0)
            c5_.push_back(0.0);
        else
            c5_.push_back(c5);

        xp_.push_back(readDouble()); if (error_!="") return;
        yp_.push_back(readDouble()); if (error_!="") return;
    }
}



PFBarrelCommon::FileReader::~FileReader()
{
    if (f_) {
        std::fclose(f_);
        f_ = NULL;
    }
}



std::string PFBarrelCommon::FileReader::readLine(void)
{
    std::string rv;
    while (error_=="" && (rv=="" || rv[0]=='#')) {
        rv= readRawLine();
    }

    return rv;
}



double PFBarrelCommon::FileReader::readDouble(void)
{
    return std::atof(readLine().c_str());
}



int PFBarrelCommon::FileReader::readInt(void)
{
    return std::atoi(readLine().c_str());
}



std::string PFBarrelCommon::FileReader::readRawLine(void)
{
    std::string rv;

    char buf[512];

    char* s = std::fgets(buf, sizeof(buf), f_);
    if (s != NULL) {
        rv= s;
        rv= rv.erase(rv.length()-1);
    } else {
        error_= "Parse error";
    }

    return rv;
}


#ifdef DEBUG
void PFBarrelCommon::FileReader::dump(void)
{
    std::printf("VERSION [%i]\n", version_);
    std::printf("ERROR [%s]\n", error_.c_str());
    std::printf("ORIG WH %i %i PA %f\n", orig_w_, orig_h_, orig_pa_);
    std::printf("UNDIST WH %i %i\n", undist_w_, undist_h_);
    std::printf("MODEL %i\n", model_);
    std::printf("SQUEEZE %f\n", squeeze_);
    std::printf("NKEYS %i\n", nkeys_);
    
    for (int i = 0; i < nkeys_; i++) {
        std::printf("KEY %i FRAME %i\n", i, frame_[i]);
        std::printf("KEY %i C3 %f\n", i, c3_[i]);
        std::printf("KEY %i C5 %f\n", i, c5_[i]);
        std::printf("KEY %i XP %f\n", i, xp_[i]);
        std::printf("KEY %i YP %f\n", i, yp_[i]);
    }
}
#endif



static bool gIsMultiPlaneV1;
static bool gIsMultiPlaneV2;

struct InputPlaneChannel
{
    Image* img;
    int channelIndex;
    bool fillZero;

    InputPlaneChannel() : img(NULL), channelIndex(-1), fillZero(true) {}
};

class DistortionProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    bool _transformIsIdentity;
    Matrix3x3 _srcTransformInverse;
    OfxRectD _format;
    std::vector<InputPlaneChannel> _planeChannels;
    bool _unpremultUV;
    double _uOffset;
    double _vOffset;
    double _uScale;
    double _vScale;
    WrapEnum _uWrap;
    WrapEnum _vWrap;
    OfxPointD _renderScale;
    const DistortionModel* _distortionModel;
    DirectionEnum _direction;
    OutputModeEnum _outputMode;
    bool _blackOutside;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:

    DistortionProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _transformIsIdentity(true)
        , _srcTransformInverse()
        , _planeChannels()
        , _unpremultUV(true)
        , _uOffset(0.)
        , _vOffset(0.)
        , _uScale(1.)
        , _vScale(1.)
        , _uWrap(eWrapClamp)
        , _vWrap(eWrapClamp)
        , _distortionModel(NULL)
        , _direction(eDirectionDistort)
        , _outputMode(eOutputModeImage)
        , _blackOutside(false)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
    {
        _renderScale.x = _renderScale.y = 1.;
        _format.x1 = _format.y1 = 0.;
        _format.x2 = _format.y2 = 1.;
    }

    void setSrcImgs(const Image *src) {_srcImg = src; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   bool transformIsIdentity,
                   const Matrix3x3 &srcTransformInverse,
                   const OfxRectD& format,
                   const std::vector<InputPlaneChannel>& planeChannels,
                   bool unpremultUV,
                   double uOffset,
                   double vOffset,
                   double uScale,
                   double vScale,
                   WrapEnum uWrap,
                   WrapEnum vWrap,
                   const OfxPointD& renderScale,
                   const DistortionModel* distortionModel,
                   DirectionEnum direction,
                   OutputModeEnum outputMode,
                   bool blackOutside,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _transformIsIdentity = transformIsIdentity;
        _srcTransformInverse = srcTransformInverse;
        _format = format;
        _planeChannels = planeChannels;
        _unpremultUV = unpremultUV;
        _uOffset = uOffset;
        _vOffset = vOffset;
        _uScale = uScale;
        _vScale = vScale;
        _uWrap = uWrap;
        _vWrap = vWrap;
        _renderScale = renderScale;
        _distortionModel = distortionModel;
        _direction = direction;
        _outputMode = outputMode;
        _blackOutside = blackOutside;
        _mix = mix;
    }

private:
};




// The "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin, FilterEnum filter, bool clamp>
class DistortionProcessor
    : public DistortionProcessorBase
{
public:
    DistortionProcessor(ImageEffect &instance)
        : DistortionProcessorBase(instance)
    {
    }

private:


    static inline double wrap(double x,
                              WrapEnum wrap)
    {
        switch (wrap) {
        default:
        case eWrapClamp:

            return x;
        case eWrapRepeat:

            return x - std::floor(x);
        case eWrapMirror: {
            double x2 = x / 2 - std::floor(x / 2);

            return (x2 <= 0.5) ? (2 * x2) : (2 - 2 * x2);
        }
        }
    }

    const PIX * getPix(unsigned channel,
                       int x,
                       int y)
    {
        return (const PIX *)  (_planeChannels[channel].img ? _planeChannels[channel].img->getPixelAddress(x, y) : 0);
    }

    double getVal(unsigned channel,
                  const PIX* p,
                  const PIX* pp)
    {
        if (!_planeChannels[channel].img) {
            return _planeChannels[channel].fillZero ? 0. : 1.;
        }
        if (!p) {
            return pp ? pp[_planeChannels[channel].channelIndex] : 0.;
        }

        return p[_planeChannels[channel].channelIndex];
    }

    void unpremult(double a,
                   double *u,
                   double *v)
    {
        if ( _unpremultUV && (a != 0.) ) {
            *u /= a;
            *v /= a;
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow);
};

template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin, FilterEnum filter, bool clamp>
void
DistortionProcessor<PIX, nComponents, maxValue, plugin, filter, clamp>::multiThreadProcessImages(OfxRectI procWindow)
{
    assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
    assert(_dstImg);
    assert(!(plugin == eDistortionPluginSTMap || plugin == eDistortionPluginIDistort) || _planeChannels.size() == 3);

    // required for STMap and LensDistortion
    //if (plugin == eDistortionPluginSTMap || _outputMode == eOutputModeSTMap) {
    int srcx1 = _format.x1;
    int srcx2 = _format.x2;
    int srcy1 = _format.y1;
    int srcy2 = _format.y2;
    //}
    float tmpPix[4];
    for (int y = procWindow.y1; y < procWindow.y2; y++) {
        if ( _effect.abort() ) {
            break;
        }

        PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

        for (int x = procWindow.x1; x < procWindow.x2; x++) {
            double sx, sy, sxx, sxy, syx, syy; // the source pixel coordinates and their derivatives
            double a = 1.;

            switch (plugin) {
            case eDistortionPluginSTMap:
            case eDistortionPluginIDistort: {
                const PIX *uPix    = getPix(0, x, y  );
                const PIX *uPix_xn = getPix(0, x + 1, y  );
                const PIX *uPix_xp = getPix(0, x - 1, y  );
                const PIX *uPix_yn = getPix(0, x, y + 1);
                const PIX *uPix_yp = getPix(0, x, y - 1);
                const PIX *vPix, *vPix_xn, *vPix_xp, *vPix_yn, *vPix_yp;
                if (_planeChannels[1].img == _planeChannels[0].img) {
                    vPix    = uPix;
                    vPix_xn = uPix_xn;
                    vPix_xp = uPix_xp;
                    vPix_yn = uPix_yn;
                    vPix_yp = uPix_yp;
                } else {
                    vPix =    getPix(1, x, y  );
                    vPix_xn = getPix(1, x + 1, y  );
                    vPix_xp = getPix(1, x - 1, y  );
                    vPix_yn = getPix(1, x, y + 1);
                    vPix_yp = getPix(1, x, y - 1);
                }
                const PIX *aPix, *aPix_xn, *aPix_xp, *aPix_yn, *aPix_yp;
                if (_planeChannels[2].img == _planeChannels[0].img) {
                    aPix = uPix;
                    aPix_xn = uPix_xn;
                    aPix_xp = uPix_xp;
                    aPix_yn = uPix_yn;
                    aPix_yp = uPix_yp;
                } else if (_planeChannels[2].img == _planeChannels[1].img) {
                    aPix = vPix;
                    aPix_xn = vPix_xn;
                    aPix_xp = vPix_xp;
                    aPix_yn = vPix_yn;
                    aPix_yp = vPix_yp;
                } else {
                    aPix =    getPix(2, x, y  );
                    aPix_xn = getPix(2, x + 1, y  );
                    aPix_xp = getPix(2, x - 1, y  );
                    aPix_yn = getPix(2, x, y + 1);
                    aPix_yp = getPix(2, x, y - 1);
                }
                // compute gradients before wrapping
                double u = getVal(0, uPix, NULL);
                double v = getVal(1, vPix, NULL);
                a = getVal(2, aPix, NULL);
                unpremult(a, &u, &v);

                double ux, uy, vx, vy;
                {
                    double u_xn = getVal(0, uPix_xn, uPix);
                    double u_xp = getVal(0, uPix_xp, uPix);
                    double u_yn = getVal(0, uPix_yn, uPix);
                    double u_yp = getVal(0, uPix_yp, uPix);
                    double v_xn = getVal(1, vPix_xn, vPix);
                    double v_xp = getVal(1, vPix_xp, vPix);
                    double v_yn = getVal(1, vPix_yn, vPix);
                    double v_yp = getVal(1, vPix_yp, vPix);
                    if (_unpremultUV) {
                        unpremult(getVal(2, aPix_xn, aPix), &u_xn, &v_xn);
                        unpremult(getVal(2, aPix_xp, aPix), &u_xp, &v_xp);
                        unpremult(getVal(2, aPix_yn, aPix), &u_yn, &v_yn);
                        unpremult(getVal(2, aPix_yp, aPix), &u_yp, &v_yp);
                    }
                    ux = (u_xn - u_xp) / 2.;
                    vx = (v_xn - v_xp) / 2.;
                    uy = (u_yn - u_yp) / 2.;
                    vy = (v_yn - v_yp) / 2.;
                }
                u = (u - _uOffset) * _uScale;
                ux *= _uScale;
                uy *= _uScale;
                v = (v - _vOffset) * _vScale;
                vx *= _vScale;
                vy *= _vScale;
                switch (plugin) {
                    case eDistortionPluginSTMap:
                        // wrap u and v
                        u = wrap(u, _uWrap);
                        v = wrap(v, _vWrap);
                        sx = srcx1 + u * (srcx2 - srcx1);
                        sy = srcy1 + v * (srcy2 - srcy1);         // 0,0 corresponds to the lower left corner of the first pixel
                        // scale gradients by (srcx2 - srcx1)
                        if (filter != eFilterImpulse) {
                            sxx = ux * (srcx2 - srcx1);
                            sxy = uy * (srcx2 - srcx1);
                            syx = vx * (srcy2 - srcy1);
                            syy = vy * (srcy2 - srcy1);
                        }
                        break;
                    case eDistortionPluginIDistort:
                        // 0,0 corresponds to the lower left corner of the first pixel, so we have to add 0.5
                        // (x,y) = (0,0) and (u,v) = (0,0) means to pick color at (0.5,0.5)
                        sx = x + u + 0.5;
                        sy = y + v + 0.5;
                        if (filter != eFilterImpulse) {
                            sxx = 1 + ux;
                            sxy = uy;
                            syx = vx;
                            syy = 1 + vy;
                        }
                        break;
                    default:
                        assert(false);
                        break;
                }
                break;
            }
            case eDistortionPluginLensDistortion: {
                assert(_distortionModel);
                // undistort/distort take pixel coordinates, do not divide by renderScale
                if (_direction == eDirectionDistort) {
                    _distortionModel->undistort( x + 0.5,
                                                 y + 0.5,
                                                 &sx, &sy);
                } else {
                    _distortionModel->distort( x + 0.5,
                                               y + 0.5,
                                               &sx, &sy);
                }
                sxx = 1;     // TODO: Jacobian
                sxy = 0;
                syx = 0;
                syy = 1;
                break;
            }
            } // switch
            double Jxx = 1., Jxy = 0., Jyx = 0., Jyy = 1.;
            if (_transformIsIdentity) {
                if (filter != eFilterImpulse) {
                    Jxx = sxx;
                    Jxy = sxy;
                    Jyx = syx;
                    Jyy = syy;
                }
            } else {
                const Matrix3x3 & H = _srcTransformInverse;
                double transformedx = H(0,0) * sx + H(0,1) * sy + H(0,2);
                double transformedy = H(1,0) * sx + H(1,1) * sy + H(1,2);
                double transformedz = H(2,0) * sx + H(2,1) * sy + H(2,2);
                if (transformedz == 0) {
                    sx = sy = std::numeric_limits<double>::infinity();
                } else {
                    sx = transformedx / transformedz;
                    sy = transformedy / transformedz;
                    if (filter != eFilterImpulse) {
                        Jxx = (H(0,0) * transformedz - transformedx * H(2,0)) / (transformedz * transformedz);
                        Jxy = (H(0,1) * transformedz - transformedx * H(2,1)) / (transformedz * transformedz);
                        Jyx = (H(1,0) * transformedz - transformedy * H(2,0)) / (transformedz * transformedz);
                        Jyy = (H(1,1) * transformedz - transformedy * H(2,1)) / (transformedz * transformedz);
                    }
                }
            }
            
            if (_outputMode == eOutputModeSTMap) {
                // 0,0 corresponds to the lower left corner of the first pixel
                tmpPix[0] = (sx - srcx1) / (srcx2 - srcx1); // u
                tmpPix[1] = (sy - srcy1) / (srcy2 - srcy1); // v
                tmpPix[2] = 1.; // a
                tmpPix[3] = 1.; // be opaque
            } else {
                if (filter == eFilterImpulse) {
                    ofxsFilterInterpolate2D<PIX, nComponents, filter, clamp>(sx, sy, _srcImg, _blackOutside, tmpPix);
                } else {
                    ofxsFilterInterpolate2DSuper<PIX, nComponents, filter, clamp>(sx, sy, Jxx, Jxy, Jyx, Jyy, _srcImg, _blackOutside, tmpPix);
                }
                for (unsigned c = 0; c < nComponents; ++c) {
                    tmpPix[c] *= a;
                }
            }
            ofxsMaskMix<PIX, nComponents, maxValue, true>(tmpPix, x, y, _srcImg, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
            // copy back original values from unprocessed channels
            if (nComponents == 1) {
                if (!_processA) {
                    const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                    dstPix[0] = srcPix ? srcPix[0] : PIX();
                }
            } else if ( (nComponents == 3) || (nComponents == 4) ) {
                const PIX *srcPix = 0;
                if ( !_processR || !_processG || !_processB || ( !_processA && (nComponents == 4) ) ) {
                    srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                }
                if (!_processR) {
                    dstPix[0] = srcPix ? srcPix[0] : PIX();
                }
                if (!_processG) {
                    dstPix[1] = srcPix ? srcPix[1] : PIX();
                }
                if (!_processB) {
                    dstPix[2] = srcPix ? srcPix[2] : PIX();
                }
                if ( !_processA && (nComponents == 4) ) {
                    dstPix[3] = srcPix ? srcPix[3] : PIX();
                }
            }
            // increment the dst pixel
            dstPix += nComponents;
        }
    }
} // multiThreadProcessImages


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DistortionPlugin
    : public MultiPlane::MultiPlaneEffect
{
public:
    /** @brief ctor */
    DistortionPlugin(OfxImageEffectHandle handle,
                     int majorVersion,
                     DistortionPluginEnum plugin)
        : MultiPlane::MultiPlaneEffect(handle)
        , _majorVersion(majorVersion)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _uvClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _uvChannels()
        , _unpremultUV(NULL)
        , _uvOffset(NULL)
        , _uvScale(NULL)
        , _uWrap(NULL)
        , _vWrap(NULL)
        , _extent(NULL)
        , _format(NULL)
        , _formatSize(NULL)
        , _formatPar(NULL)
        , _btmLeft(NULL)
        , _size(NULL)
        , _recenter(NULL)
        , _distortionModel(NULL)
        , _direction(NULL)
        , _outputMode(NULL)
        , _k1(NULL)
        , _k2(NULL)
        , _center(NULL)
        , _squeeze(NULL)
        , _asymmetric(NULL)
        , _pfFile(NULL)
        , _pfReload(NULL)
        , _pfC3(NULL)
        , _pfC5(NULL)
        , _pfSqueeze(NULL)
        , _pfP(NULL)
        , _xa_fov_unit(NULL)
        , _ya_fov_unit(NULL)
        , _xb_fov_unit(NULL)
        , _yb_fov_unit(NULL)
        , _fl_cm(NULL)
        , _fd_cm(NULL)
        , _w_fb_cm(NULL)
        , _h_fb_cm(NULL)
        , _x_lco_cm(NULL)
        , _y_lco_cm(NULL)
        , _pa(NULL)
        , _ld(NULL)
        , _sq(NULL)
        , _cx(NULL)
        , _cy(NULL)
        , _qu(NULL)
        , _c2(NULL)
        , _u1(NULL)
        , _v1(NULL)
        , _c4(NULL)
        , _u3(NULL)
        , _v3(NULL)
        , _phi(NULL)
        , _b(NULL)
        , _cx02(NULL)
        , _cy02(NULL)
        , _cx22(NULL)
        , _cy22(NULL)
        , _cx04(NULL)
        , _cy04(NULL)
        , _cx24(NULL)
        , _cy24(NULL)
        , _cx44(NULL)
        , _cy44(NULL)
        , _a4phi(NULL)
        , _a4sqx(NULL)
        , _a4sqy(NULL)
        , _cx06(NULL)
        , _cy06(NULL)
        , _cx26(NULL)
        , _cy26(NULL)
        , _cx46(NULL)
        , _cy46(NULL)
        , _cx66(NULL)
        , _cy66(NULL)
        , _c6(NULL)
        , _c8(NULL)
        , _pta(NULL)
        , _ptb(NULL)
        , _ptc(NULL)
        , _ptd(NULL)
        , _pte(NULL)
        , _ptg(NULL)
        , _ptt(NULL)
        , _filter(NULL)
        , _clamp(NULL)
        , _blackOutside(NULL)
        , _cropToFormat(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
        , _plugin(plugin)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        if ( (_plugin == eDistortionPluginIDistort) || (_plugin == eDistortionPluginSTMap) ) {
            _uvClip = fetchClip(kClipUV);
            assert( _uvClip && (_uvClip->getPixelComponents() == ePixelComponentRGB || _uvClip->getPixelComponents() == ePixelComponentRGBA || _uvClip->getPixelComponents() == ePixelComponentAlpha) );
        }
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        if ( (plugin == eDistortionPluginIDistort) || (plugin == eDistortionPluginSTMap) ) {
            _uvChannels[0] = fetchChoiceParam(kParamChannelU);
            _uvChannels[1] = fetchChoiceParam(kParamChannelV);
            _uvChannels[2] = fetchChoiceParam(kParamChannelA);
            if (gIsMultiPlaneV1 || gIsMultiPlaneV2) {

                {
                    FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                    args.dependsClips.push_back(_uvClip);
                    fetchDynamicMultiplaneChoiceParameter(kParamChannelU, args);
                }
                {
                    FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                    args.dependsClips.push_back(_uvClip);
                    fetchDynamicMultiplaneChoiceParameter(kParamChannelV, args);
                }
                {
                    FetchChoiceParamOptions args = FetchChoiceParamOptions::createFetchChoiceParamOptionsForInputChannel();
                    args.dependsClips.push_back(_uvClip);
                    fetchDynamicMultiplaneChoiceParameter(kParamChannelA, args);
                }
        
                onAllParametersFetched();
            }
            _unpremultUV = fetchBooleanParam(kParamChannelUnpremultUV);
            _uvOffset = fetchDouble2DParam(kParamUVOffset);
            _uvScale = fetchDouble2DParam(kParamUVScale);
            assert(_uvChannels[0] && _uvChannels[1] && _uvChannels[2] && _uvOffset && _uvScale);
            if (plugin == eDistortionPluginSTMap) {
                _uWrap = fetchChoiceParam(kParamWrapU);
                _vWrap = fetchChoiceParam(kParamWrapV);
                assert(_uWrap && _vWrap);
            }
        } else {
            _uvChannels[0] = _uvChannels[1] = _uvChannels[2] = NULL;
        }

        /* if (gIsMultiPlane) {
             _outputLayer = fetchChoiceParam(kParamOutputChannels);
             _outputLayerStr = fetchStringParam(kParamOutputChannelsChoice);
             assert(_outputLayer && _outputLayerStr);
           }*/
        if (_plugin == eDistortionPluginLensDistortion) {
            _extent = fetchChoiceParam(kParamGeneratorExtent);
            _format = fetchChoiceParam(kParamGeneratorFormat);
            _formatSize = fetchInt2DParam(kParamGeneratorSize);
            _formatPar= fetchDoubleParam(kParamGeneratorPAR);
            _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
            _size = fetchDouble2DParam(kParamRectangleInteractSize);
            _recenter = fetchPushButtonParam(kParamGeneratorCenter);

            _distortionModel = fetchChoiceParam(kParamDistortionModel);
            _direction = fetchChoiceParam(kParamDistortionDirection);
            _outputMode = fetchChoiceParam(kParamDistortionOutputMode);

            // Nuke
            _k1 = fetchDoubleParam(kParamK1);
            _k2 = fetchDoubleParam(kParamK2);
            _center = fetchDouble2DParam(kParamCenter);
            _squeeze = fetchDoubleParam(kParamSqueeze);
            _asymmetric = fetchDouble2DParam(kParamAsymmetric);
            assert(_k1 && _k2 && _center && _squeeze && _asymmetric);

            // PFBarrel
            _pfFile = fetchStringParam(kParamPFFile);
            if ( paramExists(kParamPFFileReload) ) {
                _pfReload = fetchPushButtonParam(kParamPFFileReload);
            }
            _pfC3 = fetchDoubleParam(kParamPFC3);
            _pfC5 = fetchDoubleParam(kParamPFC5);
            _pfSqueeze = fetchDoubleParam(kParamPFSqueeze);
            _pfP = fetchDouble2DParam(kParamPFP);

            // 3DEqualizer
            _xa_fov_unit = fetchDoubleParam(kParam3DE4_xa_fov_unit);
            _ya_fov_unit = fetchDoubleParam(kParam3DE4_ya_fov_unit);
            _xb_fov_unit = fetchDoubleParam(kParam3DE4_xb_fov_unit);
            _yb_fov_unit = fetchDoubleParam(kParam3DE4_yb_fov_unit);
            _fl_cm = fetchDoubleParam(kParam3DE4_focal_length_cm);
            _fd_cm = fetchDoubleParam(kParam3DE4_custom_focus_distance_cm);
            _w_fb_cm = fetchDoubleParam(kParam3DE4_filmback_width_cm);
            _h_fb_cm = fetchDoubleParam(kParam3DE4_filmback_height_cm);
            _x_lco_cm = fetchDoubleParam(kParam3DE4_lens_center_offset_x_cm);
            _y_lco_cm = fetchDoubleParam(kParam3DE4_lens_center_offset_y_cm);
            _pa = fetchDoubleParam(kParam3DE4_pixel_aspect);

            // 3DEclassic
            _ld = fetchDoubleParam(kParam3DEDistortion);
            _sq = fetchDoubleParam(kParam3DEAnamorphicSqueeze);
            _cx = fetchDoubleParam(kParam3DECurvatureX);
            _cy = fetchDoubleParam(kParam3DECurvatureY);
            _qu = fetchDoubleParam(kParam3DEQuarticDistortion);

            // 3DEStandard
            _c2 = fetchDoubleParam(kParam3DEDistortionDegree2);
            _u1 = fetchDoubleParam(kParam3DEUDegree2);
            _v1 = fetchDoubleParam(kParam3DEVDegree2);
            _c4 = fetchDoubleParam(kParam3DEQuarticDistortionDegree4);
            _u3 = fetchDoubleParam(kParam3DEUDegree4);
            _v3 = fetchDoubleParam(kParam3DEVDegree4);
            _phi = fetchDoubleParam(kParam3DEPhiCylindricDirection);
            _b = fetchDoubleParam(kParam3DEBCylindricBending);

            // 3DEAnamorphic4
            _cx02 = fetchDoubleParam(kParam3DECx02Degree2);
            _cy02 = fetchDoubleParam(kParam3DECy02Degree2);
            _cx22 = fetchDoubleParam(kParam3DECx22Degree2);
            _cy22 = fetchDoubleParam(kParam3DECy22Degree2);
            _cx04 = fetchDoubleParam(kParam3DECx04Degree4);
            _cy04 = fetchDoubleParam(kParam3DECy04Degree4);
            _cx24 = fetchDoubleParam(kParam3DECx24Degree4);
            _cy24 = fetchDoubleParam(kParam3DECy24Degree4);
            _cx44 = fetchDoubleParam(kParam3DECx44Degree4);
            _cy44 = fetchDoubleParam(kParam3DECy44Degree4);
            _a4phi = fetchDoubleParam(kParam3DELensRotation);
            _a4sqx = fetchDoubleParam(kParam3DESqueezeX);
            _a4sqy = fetchDoubleParam(kParam3DESqueezeY);

            // 3DEAnamorphic6
            _cx06 = fetchDoubleParam(kParam3DECx06Degree6);
            _cy06 = fetchDoubleParam(kParam3DECy06Degree6);
            _cx26 = fetchDoubleParam(kParam3DECx26Degree6);
            _cy26 = fetchDoubleParam(kParam3DECy26Degree6);
            _cx46 = fetchDoubleParam(kParam3DECx46Degree6);
            _cy46 = fetchDoubleParam(kParam3DECy46Degree6);
            _cx66 = fetchDoubleParam(kParam3DECx66Degree6);
            _cy66 = fetchDoubleParam(kParam3DECy66Degree6);

            // 3DEFishEye8
            _c6 = fetchDoubleParam(kParam3DEDegree6);
            _c8 = fetchDoubleParam(kParam3DEDegree8);

            // PanoTools
            _pta = fetchDoubleParam(kParamPanoToolsA);
            _ptb = fetchDoubleParam(kParamPanoToolsB);
            _ptc = fetchDoubleParam(kParamPanoToolsC);
            _ptd = fetchDoubleParam(kParamPanoToolsD);
            _pte = fetchDoubleParam(kParamPanoToolsE);
            _ptg = fetchDoubleParam(kParamPanoToolsG);
            _ptt = fetchDoubleParam(kParamPanoToolsT);
            assert(_pta && _ptb && _ptc && _ptd && _pte && _ptg && _ptt);
        }
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);
        assert(_filter && _clamp && _blackOutside);
        if ( paramExists(kParamCropToFormat) ) {
            _cropToFormat = fetchBooleanParam(kParamCropToFormat);
            assert(_cropToFormat);
        } else {
            assert( !paramExists(kParamCropToFormat) );
        }
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        updateVisibility();

        // honor kParamDefaultsNormalised
        if ( plugin == eDistortionPluginLensDistortion && paramExists(kParamDefaultsNormalised) ) {
            // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
            // handle these ourselves!
            BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
            assert(param);
            bool normalised = param->getValue();
            if (normalised) {
                OfxPointD size = getProjectExtent();
                OfxPointD origin = getProjectOffset();
                OfxPointD p;
                // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
            }
        }
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NUKE
    virtual OfxStatus getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
#endif

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* internal render function */
    template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin>
    void renderInternalForBitDepth(const RenderArguments &args);

    template <int nComponents, DistortionPluginEnum plugin>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(DistortionProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateVisibility();

    DistortionModel* getDistortionModel(const OfxRectD& format, const OfxPointD& renderScale, double time);

    bool getLensDistortionFormat(double time, const OfxPointD& renderScale, OfxRectD *format, double *par);

private:
    int _majorVersion;

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_uvClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    ChoiceParam* _uvChannels[3];
    BooleanParam* _unpremultUV;
    Double2DParam *_uvOffset;
    Double2DParam *_uvScale;
    ChoiceParam* _uWrap;
    ChoiceParam* _vWrap;

    ///////////////////
    // LensDistortion

    // format params
    ChoiceParam* _extent;
    ChoiceParam* _format;
    Int2DParam* _formatSize;
    DoubleParam* _formatPar;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    PushButtonParam *_recenter;

    ChoiceParam* _distortionModel;
    ChoiceParam* _direction;
    ChoiceParam* _outputMode;

    // Nuke
    DoubleParam* _k1;
    DoubleParam* _k2;
    Double2DParam* _center;
    DoubleParam* _squeeze;
    Double2DParam* _asymmetric;

    // PFBarrel
    StringParam* _pfFile;
    PushButtonParam* _pfReload;
    DoubleParam* _pfC3;
    DoubleParam* _pfC5;
    DoubleParam* _pfSqueeze;
    Double2DParam* _pfP;

    // 3DEqualizer
    // fov parameters
    DoubleParam* _xa_fov_unit;
    DoubleParam* _ya_fov_unit;
    DoubleParam* _xb_fov_unit;
    DoubleParam* _yb_fov_unit;
    // seven builtin parameters
    DoubleParam* _fl_cm;
    DoubleParam* _fd_cm;
    DoubleParam* _w_fb_cm;
    DoubleParam* _h_fb_cm;
    DoubleParam* _x_lco_cm;
    DoubleParam* _y_lco_cm;
    DoubleParam* _pa;
    // 3DEClassic model
    DoubleParam* _ld;
    DoubleParam* _sq;
    DoubleParam* _cx;
    DoubleParam* _cy;
    DoubleParam* _qu;
    // 3DEStandard model
    DoubleParam* _c2;
    DoubleParam* _u1;
    DoubleParam* _v1;
    DoubleParam* _c4;
    DoubleParam* _u3;
    DoubleParam* _v3;
    DoubleParam* _phi;
    DoubleParam* _b;
    // 3DEAnamorphic4 model
    DoubleParam* _cx02;
    DoubleParam* _cy02;
    DoubleParam* _cx22;
    DoubleParam* _cy22;
    DoubleParam* _cx04;
    DoubleParam* _cy04;
    DoubleParam* _cx24;
    DoubleParam* _cy24;
    DoubleParam* _cx44;
    DoubleParam* _cy44;
    DoubleParam* _a4phi;
    DoubleParam* _a4sqx;
    DoubleParam* _a4sqy;

    // 3DEAnamorphic6 model
    DoubleParam* _cx06;
    DoubleParam* _cy06;
    DoubleParam* _cx26;
    DoubleParam* _cy26;
    DoubleParam* _cx46;
    DoubleParam* _cy46;
    DoubleParam* _cx66;
    DoubleParam* _cy66;

    // 3DEFishEye8 model
    DoubleParam* _c6;
    DoubleParam* _c8;

    // PanoTools model
    DoubleParam* _pta;
    DoubleParam* _ptb;
    DoubleParam* _ptc;
    DoubleParam* _ptd;
    DoubleParam* _pte;
    DoubleParam* _ptg;
    DoubleParam* _ptt;

    ChoiceParam* _filter;
    BooleanParam* _clamp;
    BooleanParam* _blackOutside;
    BooleanParam* _cropToFormat;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    DistortionPluginEnum _plugin;
};


void
DistortionPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    //We have to do this because the processing code does not support varying components for uvClip and srcClip
    PixelComponentEnum dstPixelComps = getDefaultOutputClipComponents();

    if (_srcClip) {
        clipPreferences.setClipComponents(*_srcClip, dstPixelComps);
    }
    if (gIsMultiPlaneV2 && _uvClip) {
        MultiPlaneEffect::getClipPreferences(clipPreferences);
    }
    if (_plugin == eDistortionPluginLensDistortion) {
        OfxRectD format;
        double par;
        OfxPointD rs1 = {1., 1.};

        // we pass 0 as time, since anyway the input RoD is never used, thanks to the test on the return value
        bool setFormat = getLensDistortionFormat(0, rs1, &format, &par);
        if (setFormat) {
            OfxRectI formatI;
            // round to nearest
            formatI.x1 = std::floor(format.x1 + 0.5);
            formatI.y1 = std::floor(format.y1 + 0.5);
            formatI.x2 = std::floor(format.x2 + 0.5);
            formatI.y2 = std::floor(format.y2 + 0.5);
            clipPreferences.setOutputFormat(formatI);
            clipPreferences.setPixelAspectRatio(*_dstClip, par);
        }
    } else if ( _plugin == eDistortionPluginSTMap && _uvClip && _uvClip->isConnected() ) {
        OfxRectI uvFormat;
        _uvClip->getFormat(uvFormat);
        double par = _uvClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(uvFormat) ) {
            // no format is available, use the RoD instead
            const OfxRectD& srcRod = _uvClip->getRegionOfDefinition(0.);
            const OfxPointD rs1 = {1., 1.};
            Coords::toPixelNearest(srcRod, rs1, par, &uvFormat);
        }
        clipPreferences.setOutputFormat(uvFormat);
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


class InputImagesHolder_RAII
{
    std::vector<Image*> images;

public:

    InputImagesHolder_RAII()
        : images()
    {
    }

    void appendImage(Image* img)
    {
        images.push_back(img);
    }

    ~InputImagesHolder_RAII()
    {
        for (std::size_t i = 0; i < images.size(); ++i) {
            delete images[i];
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from
static
int
getChannelIndex(InputChannelEnum e,
                PixelComponentEnum comps)
{
    int retval;

    switch (e) {
    case eInputChannelR: {
        if (
#ifdef OFX_EXTENSIONS_NATRON
            comps == ePixelComponentXY ||
#endif
            comps == ePixelComponentRGB || comps == ePixelComponentRGBA) {
            retval = 0;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelG: {
        if (
#ifdef OFX_EXTENSIONS_NATRON
            comps == ePixelComponentXY ||
#endif
            comps == ePixelComponentRGB || comps == ePixelComponentRGBA) {
            retval = 1;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelB: {
        if ( ( comps == ePixelComponentRGB) || ( comps == ePixelComponentRGBA) ) {
            retval = 2;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannelA: {
        if (comps == ePixelComponentAlpha) {
            return 0;
        } else if (comps == ePixelComponentRGBA) {
            retval = 3;
        } else {
            retval = -1;
        }
        break;
    }
    case eInputChannel0:
    case eInputChannel1:
    default: {
        retval = -1;
        break;
    }
    } // switch

    return retval;
} // getChannelIndex

// renderScale should be:
// 1,1 when calling from getRoD
// rs when calling from getRoI
// rs when calling from render
// format is in pixels (but may be non-integer)
DistortionModel*
DistortionPlugin::getDistortionModel(const OfxRectD& format, const OfxPointD& renderScale, double time)
{
    if (_plugin != eDistortionPluginLensDistortion) {
        return NULL;
    }
    DistortionModelEnum distortionModelE = (DistortionModelEnum)_distortionModel->getValueAtTime(time);
    switch (distortionModelE) {
    case eDistortionModelNuke: {
        double par = 1.;
        if (_srcClip) {
            par = _srcClip->getPixelAspectRatio();
        }
        double k1 = _k1->getValueAtTime(time);
        double k2 = _k2->getValueAtTime(time);
        double cx, cy;
        _center->getValueAtTime(time, cx, cy);
        double squeeze = std::max(0.001, _squeeze->getValueAtTime(time));
        double ax, ay;
        _asymmetric->getValueAtTime(time, ax, ay);
        return new DistortionModelNuke(format,
                                       par,
                                       k1,
                                       k2,
                                       cx,
                                       cy,
                                       squeeze,
                                       ax,
                                       ay);
        break;
    }
    case eDistortionModelPFBarrel: {
        //double par = 1.;
        //if (_srcClip) {
        //    par = _srcClip->getPixelAspectRatio();
        //}
        double c3 = _pfC3->getValueAtTime(time);
        double c5 = _pfC5->getValueAtTime(time);
        double xp, yp;
        _pfP->getValueAtTime(time, xp, yp);
        double squeeze = _pfSqueeze->getValueAtTime(time);
        return new DistortionModelPFBarrel(format,
                                           renderScale,
                                           //par,
                                           c3,
                                           c5,
                                           xp,
                                           yp,
                                           squeeze);
        break;
    }
    case eDistortionModel3DEClassic: {
        //double pa = 1.;
        //if (_srcClip) {
        //    pa = _srcClip->getPixelAspectRatio();
        //}
        double xa_fov_unit = _xa_fov_unit->getValueAtTime(time);
        double ya_fov_unit = _ya_fov_unit->getValueAtTime(time);
        double xb_fov_unit = _xb_fov_unit->getValueAtTime(time);
        double yb_fov_unit = _yb_fov_unit->getValueAtTime(time);
        double fl_cm = _fl_cm->getValueAtTime(time);
        double fd_cm = _fd_cm->getValueAtTime(time);
        double w_fb_cm = _w_fb_cm->getValueAtTime(time);
        double h_fb_cm = _h_fb_cm->getValueAtTime(time);
        double x_lco_cm = _x_lco_cm->getValueAtTime(time);
        double y_lco_cm = _y_lco_cm->getValueAtTime(time);
        double pa = _pa->getValueAtTime(time);

        double ld = _ld->getValueAtTime(time);
        double sq = _sq->getValueAtTime(time);
        double cx = _cx->getValueAtTime(time);
        double cy = _cy->getValueAtTime(time);
        double qu = _qu->getValueAtTime(time);
        return new DistortionModel3DEClassic(format,
                                             renderScale,
                                             xa_fov_unit,
                                             ya_fov_unit,
                                             xb_fov_unit,
                                             yb_fov_unit,
                                             fl_cm,
                                             fd_cm,
                                             w_fb_cm,
                                             h_fb_cm,
                                             x_lco_cm,
                                             y_lco_cm,
                                             pa,
                                             ld,
                                             sq,
                                             cx,
                                             cy,
                                             qu);
        break;
    }
    case eDistortionModel3DEAnamorphic6: {
        //double pa = 1.;
        //if (_srcClip) {
        //    pa = _srcClip->getPixelAspectRatio();
        //}
        double xa_fov_unit = _xa_fov_unit->getValueAtTime(time);
        double ya_fov_unit = _ya_fov_unit->getValueAtTime(time);
        double xb_fov_unit = _xb_fov_unit->getValueAtTime(time);
        double yb_fov_unit = _yb_fov_unit->getValueAtTime(time);
        double fl_cm = _fl_cm->getValueAtTime(time);
        double fd_cm = _fd_cm->getValueAtTime(time);
        double w_fb_cm = _w_fb_cm->getValueAtTime(time);
        double h_fb_cm = _h_fb_cm->getValueAtTime(time);
        double x_lco_cm = _x_lco_cm->getValueAtTime(time);
        double y_lco_cm = _y_lco_cm->getValueAtTime(time);
        double pa = _pa->getValueAtTime(time);

        double cx02 = _cx02->getValueAtTime(time);
        double cy02 = _cy02->getValueAtTime(time);
        double cx22 = _cx22->getValueAtTime(time);
        double cy22 = _cy22->getValueAtTime(time);
        double cx04 = _cx04->getValueAtTime(time);
        double cy04 = _cy04->getValueAtTime(time);
        double cx24 = _cx24->getValueAtTime(time);
        double cy24 = _cy24->getValueAtTime(time);
        double cx44 = _cx44->getValueAtTime(time);
        double cy44 = _cy44->getValueAtTime(time);
        double cx06 = _cx06->getValueAtTime(time);
        double cy06 = _cy06->getValueAtTime(time);
        double cx26 = _cx26->getValueAtTime(time);
        double cy26 = _cy26->getValueAtTime(time);
        double cx46 = _cx46->getValueAtTime(time);
        double cy46 = _cy46->getValueAtTime(time);
        double cx66 = _cx66->getValueAtTime(time);
        double cy66 = _cy66->getValueAtTime(time);
        return new DistortionModel3DEAnamorphic6(format,
                                                 renderScale,
                                                 xa_fov_unit,
                                                 ya_fov_unit,
                                                 xb_fov_unit,
                                                 yb_fov_unit,
                                                 fl_cm,
                                                 fd_cm,
                                                 w_fb_cm,
                                                 h_fb_cm,
                                                 x_lco_cm,
                                                 y_lco_cm,
                                                 pa,
                                                 cx02,
                                                 cy02,
                                                 cx22,
                                                 cy22,
                                                 cx04,
                                                 cy04,
                                                 cx24,
                                                 cy24,
                                                 cx44,
                                                 cy44,
                                                 cx06,
                                                 cy06,
                                                 cx26,
                                                 cy26,
                                                 cx46,
                                                 cy46,
                                                 cx66,
                                                 cy66);
        break;
    }
    case eDistortionModel3DEFishEye8: {
        //double pa = 1.;
        //if (_srcClip) {
        //    pa = _srcClip->getPixelAspectRatio();
        //}
        double xa_fov_unit = _xa_fov_unit->getValueAtTime(time);
        double ya_fov_unit = _ya_fov_unit->getValueAtTime(time);
        double xb_fov_unit = _xb_fov_unit->getValueAtTime(time);
        double yb_fov_unit = _yb_fov_unit->getValueAtTime(time);
        double fl_cm = _fl_cm->getValueAtTime(time);
        double fd_cm = _fd_cm->getValueAtTime(time);
        double w_fb_cm = _w_fb_cm->getValueAtTime(time);
        double h_fb_cm = _h_fb_cm->getValueAtTime(time);
        double x_lco_cm = _x_lco_cm->getValueAtTime(time);
        double y_lco_cm = _y_lco_cm->getValueAtTime(time);
        double pa = _pa->getValueAtTime(time);

        double c2 = _c2->getValueAtTime(time);
        double c4 = _c4->getValueAtTime(time);
        double c6 = _c6->getValueAtTime(time);
        double c8 = _c8->getValueAtTime(time);
        return new DistortionModel3DEFishEye8(format,
                                              renderScale,
                                              xa_fov_unit,
                                              ya_fov_unit,
                                              xb_fov_unit,
                                              yb_fov_unit,
                                              fl_cm,
                                              fd_cm,
                                              w_fb_cm,
                                              h_fb_cm,
                                              x_lco_cm,
                                              y_lco_cm,
                                              pa,
                                              c2,
                                              c4,
                                              c6,
                                              c8);
        break;
    }
    case eDistortionModel3DEStandard: {
        //double pa = 1.;
        //if (_srcClip) {
        //    pa = _srcClip->getPixelAspectRatio();
        //}
        double xa_fov_unit = _xa_fov_unit->getValueAtTime(time);
        double ya_fov_unit = _ya_fov_unit->getValueAtTime(time);
        double xb_fov_unit = _xb_fov_unit->getValueAtTime(time);
        double yb_fov_unit = _yb_fov_unit->getValueAtTime(time);
        double fl_cm = _fl_cm->getValueAtTime(time);
        double fd_cm = _fd_cm->getValueAtTime(time);
        double w_fb_cm = _w_fb_cm->getValueAtTime(time);
        double h_fb_cm = _h_fb_cm->getValueAtTime(time);
        double x_lco_cm = _x_lco_cm->getValueAtTime(time);
        double y_lco_cm = _y_lco_cm->getValueAtTime(time);
        double pa = _pa->getValueAtTime(time);

        double c2 = _c2->getValueAtTime(time);
        double u1 = _u1->getValueAtTime(time);
        double v1 = _v1->getValueAtTime(time);
        double c4 = _c4->getValueAtTime(time);
        double u3 = _u3->getValueAtTime(time);
        double v3 = _v3->getValueAtTime(time);
        double phi = _phi->getValueAtTime(time);
        double b = _b->getValueAtTime(time);
        return new DistortionModel3DEStandard(format,
                                              renderScale,
                                              xa_fov_unit,
                                              ya_fov_unit,
                                              xb_fov_unit,
                                              yb_fov_unit,
                                              fl_cm,
                                              fd_cm,
                                              w_fb_cm,
                                              h_fb_cm,
                                              x_lco_cm,
                                              y_lco_cm,
                                              pa,
                                              c2,
                                              u1,
                                              v1,
                                              c4,
                                              u3,
                                              v3,
                                              phi,
                                              b);
        break;
    }
    case eDistortionModel3DEAnamorphic4: {
        //double pa = 1.;
        //if (_srcClip) {
        //    pa = _srcClip->getPixelAspectRatio();
        //}
        double xa_fov_unit = _xa_fov_unit->getValueAtTime(time);
        double ya_fov_unit = _ya_fov_unit->getValueAtTime(time);
        double xb_fov_unit = _xb_fov_unit->getValueAtTime(time);
        double yb_fov_unit = _yb_fov_unit->getValueAtTime(time);
        double fl_cm = _fl_cm->getValueAtTime(time);
        double fd_cm = _fd_cm->getValueAtTime(time);
        double w_fb_cm = _w_fb_cm->getValueAtTime(time);
        double h_fb_cm = _h_fb_cm->getValueAtTime(time);
        double x_lco_cm = _x_lco_cm->getValueAtTime(time);
        double y_lco_cm = _y_lco_cm->getValueAtTime(time);
        double pa = _pa->getValueAtTime(time);

        double cx02 = _cx02->getValueAtTime(time);
        double cy02 = _cy02->getValueAtTime(time);
        double cx22 = _cx22->getValueAtTime(time);
        double cy22 = _cy22->getValueAtTime(time);
        double cx04 = _cx04->getValueAtTime(time);
        double cy04 = _cy04->getValueAtTime(time);
        double cx24 = _cx24->getValueAtTime(time);
        double cy24 = _cy24->getValueAtTime(time);
        double cx44 = _cx44->getValueAtTime(time);
        double cy44 = _cy44->getValueAtTime(time);
        double phi = _a4phi->getValueAtTime(time);
        double sqx = _a4sqx->getValueAtTime(time);
        double sqy = _a4sqy->getValueAtTime(time);
        return new DistortionModel3DEAnamorphic4(format,
                                                 renderScale,
                                                 xa_fov_unit,
                                                 ya_fov_unit,
                                                 xb_fov_unit,
                                                 yb_fov_unit,
                                                 fl_cm,
                                                 fd_cm,
                                                 w_fb_cm,
                                                 h_fb_cm,
                                                 x_lco_cm,
                                                 y_lco_cm,
                                                 pa,
                                                 cx02,
                                                 cy02,
                                                 cx22,
                                                 cy22,
                                                 cx04,
                                                 cy04,
                                                 cx24,
                                                 cy24,
                                                 cx44,
                                                 cy44,
                                                 phi,
                                                 sqx,
                                                 sqy);
        break;
    }
    case eDistortionModelPanoTools: {
        double par = 1.;
        if (_srcClip) {
            par = _srcClip->getPixelAspectRatio();
        }
        double a = _pta->getValueAtTime(time);
        double b = _ptb->getValueAtTime(time);
        double c = _ptc->getValueAtTime(time);
        double d = _ptd->getValueAtTime(time);
        double e = _pte->getValueAtTime(time);
        double g = _ptg->getValueAtTime(time);
        double t = _ptt->getValueAtTime(time);
        return new DistortionModelPanoTools(format,
                                            renderScale,
                                            par,
                                            a, b, c,
                                            d, e,
                                            g, t);
        break;
    }

    }
    assert(false);
}

// returns true if fixed format (i.e. not the input RoD) and setFormat can be called in getClipPrefs
bool
DistortionPlugin::getLensDistortionFormat(double time,
                                          const OfxPointD& renderScale,
                                          OfxRectD *format,
                                          double *par)
{
    assert(_plugin == eDistortionPluginLensDistortion);

    GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();

    switch (extent) {
        case eGeneratorExtentFormat: {
            int w, h;
            _formatSize->getValue(w, h);
            *par = _formatPar->getValue();
            format->x1 = format->y1 = 0;
            format->x2 = w * renderScale.x;
            format->y2 = h * renderScale.y;

            return true;
            break;
        }
        case eGeneratorExtentSize: {
            OfxRectD rod;
            _size->getValue(rod.x2, rod.y2);
            _btmLeft->getValue(rod.x1, rod.y1);
            rod.x2 += rod.x1;
            rod.y2 += rod.y1;
            *par = _srcClip ? _srcClip->getPixelAspectRatio() : 1.;
            // Coords::toPixelNearest(rod, renderScale, *par, format);
            format->x1 = rod.x1 * renderScale.x / *par;
            format->y1 = rod.y1 * renderScale.y;
            format->x2 = rod.x2 * renderScale.x / *par;
            format->y2 = rod.y2 * renderScale.y;

            return true;
            break;
        }
        case eGeneratorExtentProject: {
            OfxRectD rod;
            OfxPointD siz = getProjectSize();
            OfxPointD off = getProjectOffset();
            rod.x1 = off.x;
            rod.x2 = off.x + siz.x;
            rod.y1 = off.y;
            rod.y2 = off.y + siz.y;
            *par = getProjectPixelAspectRatio();
            // Coords::toPixelNearest(rod, renderScale, *par, format);
            format->x1 = rod.x1 * renderScale.x / *par;
            format->y1 = rod.y1 * renderScale.y;
            format->x2 = rod.x2 * renderScale.x / *par;
            format->y2 = rod.y2 * renderScale.y;

            return true;
            break;
        }
        case eGeneratorExtentDefault:
            if ( _srcClip && _srcClip->isConnected() ) {
                OfxRectI formatI;
                formatI.x1 = formatI.y1 = formatI.x2 = formatI.y2 = 0; // default value
                if (_majorVersion >= 3) {
                    // before version 3, LensDistortion was only using RoD
                    _srcClip->getFormat(formatI);
                }
                *par = _srcClip->getPixelAspectRatio();
                if ( OFX::Coords::rectIsEmpty(formatI) ) {
                    // no format is available, use the RoD instead
                    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
                    // Coords::toPixelNearest(srcRod, renderScale, *par, format);
                    format->x1 = srcRod.x1 * renderScale.x / *par;
                    format->y1 = srcRod.y1 * renderScale.y;
                    format->x2 = srcRod.x2 * renderScale.x / *par;
                    format->y2 = srcRod.y2 * renderScale.y;
                } else {
                    format->x1 = formatI.x1 * renderScale.x;
                    format->y1 = formatI.y1 * renderScale.y;
                    format->x2 = formatI.x2 * renderScale.x;
                    format->y2 = formatI.y2 * renderScale.y;
                }
            } else {
                // default to Project Size
                OfxRectD srcRod;
                OfxPointD siz = getProjectSize();
                OfxPointD off = getProjectOffset();
                srcRod.x1 = off.x;
                srcRod.x2 = off.x + siz.x;
                srcRod.y1 = off.y;
                srcRod.y2 = off.y + siz.y;
                *par = getProjectPixelAspectRatio();
                // Coords::toPixelNearest(srcRod, renderScale, *par, format);
                format->x1 = srcRod.x1 * renderScale.x / *par;
                format->y1 = srcRod.y1 * renderScale.y;
                format->x2 = srcRod.x2 * renderScale.x / *par;
                format->y2 = srcRod.y2 * renderScale.y;
            }

            return false;
            break;
    }
    return false;
}

/* set up and run a processor */
void
DistortionPlugin::setupAndProcess(DistortionProcessorBase &processor,
                                  const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    OutputModeEnum outputMode = _outputMode ? (OutputModeEnum)_outputMode->getValue() : eOutputModeImage;

    auto_ptr<const Image> src( ( (outputMode == eOutputModeImage) && _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
            ( src->getRenderScale().y != args.renderScale.y) ||
            ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    InputImagesHolder_RAII imagesHolder;
    std::vector<InputPlaneChannel> planeChannels;

    if (_uvClip) {
        if (gIsMultiPlaneV1 || gIsMultiPlaneV2) {
            BitDepthEnum srcBitDepth = eBitDepthNone;
            std::map<Clip*, std::map<std::string, Image*> > fetchedPlanes;
            for (int i = 0; i < 3; ++i) {
                InputPlaneChannel p;
                p.channelIndex = i;
                p.fillZero = false;
                //if (_uvClip) {
                Clip* clip = 0;
                MultiPlane::ImagePlaneDesc plane;
                MultiPlane::MultiPlaneEffect::GetPlaneNeededRetCodeEnum stat = getPlaneNeeded(_uvChannels[i]->getName(), &clip, &plane, &p.channelIndex);
                if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeFailed) {
                    setPersistentMessage(Message::eMessageError, "", "Cannot find requested channels in input");
                    throwSuiteStatusException(kOfxStatFailed);
                }

                p.img = 0;
                if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedConstant0 ||
                    (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedPlane && plane.getNumComponents() == 0)) {
                    p.fillZero = true;
                } else if (stat == MultiPlane::MultiPlaneEffect::eGetPlaneNeededRetCodeReturnedConstant1) {
                    p.fillZero = false;
                } else {
                    std::map<std::string, Image*>& clipPlanes = fetchedPlanes[clip];
                    std::map<std::string, Image*>::iterator foundPlane = clipPlanes.find(plane.getPlaneID());
                    if ( foundPlane != clipPlanes.end() ) {
                        p.img = foundPlane->second;
                    } else {
#ifdef OFX_EXTENSIONS_NUKE
                        p.img = clip->fetchImagePlane( time, args.renderView, plane.getPlaneID().c_str() );
#else
                        p.img = ( clip && clip->isConnected() ) ? clip->fetchImage(time) : 0;
#endif
                        if (p.img) {
                            clipPlanes.insert( std::make_pair(plane.getPlaneID(), p.img) );
                            imagesHolder.appendImage(p.img);
                        }
                    }
                }

                if (p.img) {
                    if ( (p.img->getRenderScale().x != args.renderScale.x) ||
                        ( p.img->getRenderScale().y != args.renderScale.y) ||
                        ( ( p.img->getField() != eFieldNone) /* for DaVinci Resolve */ && ( p.img->getField() != args.fieldToRender) ) ) {
                        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                        throwSuiteStatusException(kOfxStatFailed);
                    }
                    if (srcBitDepth == eBitDepthNone) {
                        srcBitDepth = p.img->getPixelDepth();
                    } else {
                        // both input must have the same bit depth and components
                        if ( (srcBitDepth != eBitDepthNone) && ( srcBitDepth != p.img->getPixelDepth() ) ) {
                            throwSuiteStatusException(kOfxStatErrImageFormat);
                        }
                    }
                    // If the channel is unavailabe in the image, fill with 0 (1 for Alpha)
                    // This may happen if the user selected  the hard-coded Alpha channel and the input is RGB
                    if (p.channelIndex >= p.img->getPixelComponentCount()) {
                        p.img = 0;
                        p.fillZero = p.channelIndex != 3;
                    }
                }


                //}
                planeChannels.push_back(p);
            }
        } else { //!gIsMultiPlane
            InputChannelEnum uChannel = eInputChannelR;
            InputChannelEnum vChannel = eInputChannelG;
            InputChannelEnum aChannel = eInputChannelA;
            if (_uvChannels[0]) {
                uChannel = (InputChannelEnum)_uvChannels[0]->getValueAtTime(time);
            }
            if (_uvChannels[1]) {
                vChannel = (InputChannelEnum)_uvChannels[1]->getValueAtTime(time);
            }
            if (_uvChannels[2]) {
                aChannel = (InputChannelEnum)_uvChannels[2]->getValueAtTime(time);
            }

            Image* uv = NULL;
            if ( ( ( (uChannel != eInputChannel0) && (uChannel != eInputChannel1) ) ||
                  ( (vChannel != eInputChannel0) && (vChannel != eInputChannel1) ) ||
                  ( (aChannel != eInputChannel0) && (aChannel != eInputChannel1) ) ) &&
                ( _uvClip && _uvClip->isConnected() ) ) {
                uv =  _uvClip->fetchImage(time);
            }

            PixelComponentEnum uvComponents = ePixelComponentNone;
            if (uv) {
                imagesHolder.appendImage(uv);
                if ( (uv->getRenderScale().x != args.renderScale.x) ||
                    ( uv->getRenderScale().y != args.renderScale.y) ||
                    ( ( uv->getField() != eFieldNone) /* for DaVinci Resolve */ && ( uv->getField() != args.fieldToRender) ) ) {
                    setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    throwSuiteStatusException(kOfxStatFailed);
                }
                BitDepthEnum uvBitDepth      = uv->getPixelDepth();
                uvComponents = uv->getPixelComponents();
                // only eBitDepthFloat is supported for now (other types require special processing for uv values)
                if ( (uvBitDepth != eBitDepthFloat) /*|| (uvComponents != dstComponents)*/ ) {
                    throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }

            // fillZero is only used when the channelIndex is -1 (i.e. it does not exist), and in this case:
            // - it is true if the inputchannel is 0, R, G or B
            // - it is false if the inputchannel is 1, A (images without alpha are considered opaque)
            {
                InputPlaneChannel u;
                u.channelIndex = getChannelIndex(uChannel, uvComponents);
                u.img = (u.channelIndex >= 0) ? uv : NULL;
                u.fillZero = (u.channelIndex >= 0) ? false : !(uChannel == eInputChannel1 || uChannel == eInputChannelA);
                planeChannels.push_back(u);
            }
            {
                InputPlaneChannel v;
                v.channelIndex = getChannelIndex(vChannel, uvComponents);
                v.img = (v.channelIndex >= 0) ? uv : NULL;
                v.fillZero = (v.channelIndex >= 0) ? false : !(vChannel == eInputChannel1 || vChannel == eInputChannelA);
                planeChannels.push_back(v);
            }
            {
                InputPlaneChannel a;
                a.channelIndex = getChannelIndex(aChannel, uvComponents);
                a.img = (a.channelIndex >= 0) ? uv : NULL;
                a.fillZero = (a.channelIndex >= 0) ? false : !(aChannel == eInputChannel1 || aChannel == eInputChannelA);
                planeChannels.push_back(a);
            }
        }
    } // if (_uvClip)"


    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);
    bool processA = _processA->getValueAtTime(time);
    bool unpremultUV = false;
    double uScale = 1., vScale = 1.;
    double uOffset = 0., vOffset = 0.;
    WrapEnum uWrap = eWrapClamp;
    WrapEnum vWrap = eWrapClamp;
    if ( (_plugin == eDistortionPluginIDistort) || (_plugin == eDistortionPluginSTMap) ) {
        unpremultUV = _unpremultUV->getValueAtTime(time);
        _uvOffset->getValueAtTime(time, uOffset, vOffset);
        _uvScale->getValueAtTime(time, uScale, vScale);
        if (_plugin == eDistortionPluginSTMap) {
            uWrap = (WrapEnum)_uWrap->getValueAtTime(time);
            vWrap = (WrapEnum)_vWrap->getValueAtTime(time);
        }
    }
    bool blackOutside = _blackOutside->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);

    bool transformIsIdentity = true;
    Matrix3x3 srcTransformInverse;
#ifdef OFX_EXTENSIONS_NUKE
    if ( src.get() ) {
        transformIsIdentity = src->getTransformIsIdentity();
    }
    if (!transformIsIdentity) {
        double srcTransform[9]; // transform to apply to the source image, in pixel coordinates, from source to destination
        src->getTransform(srcTransform);
        Matrix3x3 srcTransformMat;
        srcTransformMat(0,0) = srcTransform[0];
        srcTransformMat(0,1) = srcTransform[1];
        srcTransformMat(0,2) = srcTransform[2];
        srcTransformMat(1,0) = srcTransform[3];
        srcTransformMat(1,1) = srcTransform[4];
        srcTransformMat(1,2) = srcTransform[5];
        srcTransformMat(2,0) = srcTransform[6];
        srcTransformMat(2,1) = srcTransform[7];
        srcTransformMat(2,2) = srcTransform[8];
        // invert it
        if ( !srcTransformMat.inverse(&srcTransformInverse) ) {
            transformIsIdentity = true; // no transform
        }
    }
#endif
    if (_plugin == eDistortionPluginIDistort) {
        // in IDistort, displacement is given in full-scale pixels
        uScale *= args.renderScale.x;
        vScale *= args.renderScale.y;
    }
    OfxRectD format = {0, 0, 1, 1};
    if (_plugin == eDistortionPluginLensDistortion) {
        double par = 1.;
        getLensDistortionFormat(time, args.renderScale, &format, &par);
    } else if (_srcClip && _srcClip->isConnected()) {
        OfxRectI formatI;
        formatI.x1 = formatI.y1 = formatI.x2 = formatI.y2 = 0; // default value
        _srcClip->getFormat(formatI);
        double par = _srcClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(formatI) ) {
            // no format is available, use the RoD instead
            const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
            // Coords::toPixelNearest(srcRod, renderScale, *par, format);
            format.x1 = srcRod.x1 * args.renderScale.x / par;
            format.y1 = srcRod.y1 * args.renderScale.y;
            format.x2 = srcRod.x2 * args.renderScale.x / par;
            format.y2 = srcRod.y2 * args.renderScale.y;
        } else {
            format.x1 = formatI.x1 * args.renderScale.x;
            format.y1 = formatI.y1 * args.renderScale.y;
            format.x2 = formatI.x2 * args.renderScale.x;
            format.y2 = formatI.y2 * args.renderScale.y;
        }
    }

    DirectionEnum direction = _direction ? (DirectionEnum)_direction->getValue() : eDirectionDistort;
    auto_ptr<DistortionModel> distortionModel( getDistortionModel(format, args.renderScale, time) );
    processor.setValues(processR, processG, processB, processA,
                        transformIsIdentity, srcTransformInverse,
                        format,
                        planeChannels,
                        unpremultUV,
                        uOffset, vOffset,
                        uScale, vScale,
                        uWrap, vWrap,
                        args.renderScale,
                        distortionModel.get(),
                        direction,
                        outputMode,
                        blackOutside, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DistortionPlugin::setupAndProcess

template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin>
void
DistortionPlugin::renderInternalForBitDepth(const RenderArguments &args)
{
    const double time = args.time;
    FilterEnum filter = args.renderQualityDraft ? eFilterImpulse : eFilterCubic;

    if (!args.renderQualityDraft && _filter) {
        filter = (FilterEnum)_filter->getValueAtTime(time);
    }
    bool clamp = false;
    if (_clamp) {
        clamp = _clamp->getValueAtTime(time);
    }

    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch (filter) {
    case eFilterImpulse: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterImpulse, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterBox: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterBox, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterBilinear: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterBilinear, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterCubic: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterCubic, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterKeys:
        if (clamp) {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterKeys, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterKeys, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterSimon:
        if (clamp) {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterSimon, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterSimon, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterRifman:
        if (clamp) {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterRifman, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterRifman, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterMitchell:
        if (clamp) {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterMitchell, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterMitchell, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterParzen: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterParzen, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterNotch: {
        DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterNotch, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    } // switch
} // renderInternalForBitDepth

// the internal render function
template <int nComponents, DistortionPluginEnum plugin>
void
DistortionPlugin::renderInternal(const RenderArguments &args,
                                 BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderInternalForBitDepth<unsigned char, nComponents, 255, plugin>(args);
        break;
    case eBitDepthUShort:
        renderInternalForBitDepth<unsigned short, nComponents, 65535, plugin>(args);
        break;
    case eBitDepthFloat:
        renderInternalForBitDepth<float, nComponents, 1, plugin>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
DistortionPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
#ifdef OFX_EXTENSIONS_NATRON
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
#else
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
#endif
    if (dstComponents == ePixelComponentRGBA) {
        switch (_plugin) {
        case eDistortionPluginSTMap:
            renderInternal<4, eDistortionPluginSTMap>(args, dstBitDepth);
            break;
        case eDistortionPluginIDistort:
            renderInternal<4, eDistortionPluginIDistort>(args, dstBitDepth);
            break;
        case eDistortionPluginLensDistortion:
            renderInternal<4, eDistortionPluginLensDistortion>(args, dstBitDepth);
            break;
        }
    } else if (dstComponents == ePixelComponentRGB) {
        switch (_plugin) {
        case eDistortionPluginSTMap:
            renderInternal<3, eDistortionPluginSTMap>(args, dstBitDepth);
            break;
        case eDistortionPluginIDistort:
            renderInternal<3, eDistortionPluginIDistort>(args, dstBitDepth);
            break;
        case eDistortionPluginLensDistortion:
            renderInternal<3, eDistortionPluginLensDistortion>(args, dstBitDepth);
            break;
        }
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        switch (_plugin) {
        case eDistortionPluginSTMap:
            renderInternal<2, eDistortionPluginSTMap>(args, dstBitDepth);
            break;
        case eDistortionPluginIDistort:
            renderInternal<2, eDistortionPluginIDistort>(args, dstBitDepth);
            break;
        case eDistortionPluginLensDistortion:
            renderInternal<2, eDistortionPluginLensDistortion>(args, dstBitDepth);
            break;
        }
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        switch (_plugin) {
        case eDistortionPluginSTMap:
            renderInternal<1, eDistortionPluginSTMap>(args, dstBitDepth);
            break;
        case eDistortionPluginIDistort:
            renderInternal<1, eDistortionPluginIDistort>(args, dstBitDepth);
            break;
        case eDistortionPluginLensDistortion:
            renderInternal<1, eDistortionPluginLensDistortion>(args, dstBitDepth);
            break;
        }
    }
} // DistortionPlugin::render

bool
DistortionPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double & /*identityTime*/
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;

    if ( (_plugin == eDistortionPluginIDistort) || (_plugin == eDistortionPluginSTMap) ) {
        if ( !_uvClip || !_uvClip->isConnected() ) {
            identityClip = _srcClip;

            return true;
        }
    }
    if (_plugin == eDistortionPluginLensDistortion) {
        OutputModeEnum outputMode = _outputMode ? (OutputModeEnum)_outputMode->getValue() : eOutputModeImage;
        if (outputMode == eOutputModeSTMap) {
            return false;
        }
        bool identity = false;
        DistortionModelEnum distortionModel = (DistortionModelEnum)_distortionModel->getValueAtTime(time);
        switch (distortionModel) {
        case eDistortionModelNuke: {
            double k1 = _k1->getValueAtTime(time);
            double k2 = _k2->getValueAtTime(time);
            double ax, ay;
            _asymmetric->getValueAtTime(time, ax, ay);
            identity = (k1 == 0.) && (k2 == 0.) && (ax == 0.) && (ay == 0.);
            break;
        }
        case eDistortionModelPFBarrel: {
            double pfC3 = _pfC3->getValueAtTime(time);
            double pfC5 = _pfC5->getValueAtTime(time);
            identity = (pfC3 == 0.) && (pfC5 == 0.);
            break;
        }
        case eDistortionModel3DEClassic: {
            double ld = _ld->getValueAtTime(time);
            double cx = _cx->getValueAtTime(time);
            double cy = _cy->getValueAtTime(time);
            double qu = _qu->getValueAtTime(time);
            identity = (ld == 0.) && (cx == 0.) && (cy == 0.) && (qu == 0.);
            break;
        }
        case eDistortionModel3DEAnamorphic6: {
            double cx02 = _cx02->getValueAtTime(time);
            double cy02 = _cy02->getValueAtTime(time);
            double cx22 = _cx22->getValueAtTime(time);
            double cy22 = _cy22->getValueAtTime(time);
            double cx04 = _cx04->getValueAtTime(time);
            double cy04 = _cy04->getValueAtTime(time);
            double cx24 = _cx24->getValueAtTime(time);
            double cy24 = _cy24->getValueAtTime(time);
            double cx44 = _cx44->getValueAtTime(time);
            double cy44 = _cy44->getValueAtTime(time);
            double cx06 = _cx06->getValueAtTime(time);
            double cy06 = _cy06->getValueAtTime(time);
            double cx26 = _cx26->getValueAtTime(time);
            double cy26 = _cy26->getValueAtTime(time);
            double cx46 = _cx46->getValueAtTime(time);
            double cy46 = _cy46->getValueAtTime(time);
            double cx66 = _cx66->getValueAtTime(time);
            double cy66 = _cy66->getValueAtTime(time);
            identity = ( (cx02 == 0.) && (cy02 == 0.) && (cx22 == 0.) && (cy22 == 0.) &&
                         (cx04 == 0.) && (cy04 == 0.) && (cx24 == 0.) && (cy24 == 0.) && (cx44 == 0.) && (cy44 == 0.) &&
                         (cx06 == 0.) && (cy06 == 0.) && (cx26 == 0.) && (cy26 == 0.) && (cx46 == 0.) && (cy46 == 0.) && (cx66 == 0.) && (cy66 == 0.) );
            break;
        }
        case eDistortionModel3DEFishEye8: {
            // fisheye is never identity
            break;
        }
        case eDistortionModel3DEStandard: {
            double c2 = _c2->getValueAtTime(time);
            double u1 = _u1->getValueAtTime(time);
            double v1 = _v1->getValueAtTime(time);
            double c4 = _c4->getValueAtTime(time);
            double u3 = _u3->getValueAtTime(time);
            double v3 = _v3->getValueAtTime(time);
            double b = _b->getValueAtTime(time);
            identity = ( (c2 == 0.) && (u1 == 0.) && (v1 == 0.) && (c4 == 0.) &&
                         (u3 == 0.) && (v3 == 0.) && (b == 0.) );
            break;
        }
        case eDistortionModel3DEAnamorphic4: {
            double cx02 = _cx02->getValueAtTime(time);
            double cy02 = _cy02->getValueAtTime(time);
            double cx22 = _cx22->getValueAtTime(time);
            double cy22 = _cy22->getValueAtTime(time);
            double cx04 = _cx04->getValueAtTime(time);
            double cy04 = _cy04->getValueAtTime(time);
            double cx24 = _cx24->getValueAtTime(time);
            double cy24 = _cy24->getValueAtTime(time);
            double cx44 = _cx44->getValueAtTime(time);
            double cy44 = _cy44->getValueAtTime(time);
            double phi = _a4phi->getValueAtTime(time);
            double sqx = _a4sqx->getValueAtTime(time);
            double sqy = _a4sqy->getValueAtTime(time);
            identity = ( (cx02 == 0.) && (cy02 == 0.) && (cx22 == 0.) && (cy22 == 0.) &&
                         (cx04 == 0.) && (cy04 == 0.) && (cx24 == 0.) && (cy24 == 0.) && (cx44 == 0.) && (cy44 == 0.) &&
                         (phi == 0.) && (sqx == 0.) && (sqy == 0.) );
            break;
        }
        case eDistortionModelPanoTools: {
            double a = _pta->getValueAtTime(time);
            double b = _ptb->getValueAtTime(time);
            double c = _ptc->getValueAtTime(time);
            double d = _ptd->getValueAtTime(time);
            double e = _pte->getValueAtTime(time);
            double g = _ptg->getValueAtTime(time);
            double t = _ptt->getValueAtTime(time);
            identity = (a == 0.) && (b == 0.) && (c == 0.) && (d == 0.) && (e == 0.) && (g == 0.) && (t == 0.);
            break;
        }
        } // switch (distortionModel) {

        if (identity) {
            identityClip = _srcClip;

            return true;
        }
    }
    double mix;
    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR, processG, processB, processA;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // DistortionPlugin::isIdentity

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DistortionPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                       RegionOfInterestSetter &rois)
{
    const double time = args.time;

    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }

    if (_plugin == eDistortionPluginLensDistortion) {
        OfxRectD format = {0, 1, 0, 1};
        double par = 1.;
        getLensDistortionFormat(time, args.renderScale, &format, &par);

        DirectionEnum direction = _direction ? (DirectionEnum)_direction->getValue() : eDirectionDistort;
        auto_ptr<DistortionModel> distortionModel( getDistortionModel(format, args.renderScale, time) );

        OfxRectD roiPixel = { std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
        assert( OFX::Coords::rectIsEmpty(roiPixel) );

        OfxRectI renderWinPixel;
        OFX::Coords::toPixelEnclosing(args.regionOfInterest, args.renderScale, par, &renderWinPixel);
        OfxRectD renderWin;
        renderWin.x1 = renderWinPixel.x1;
        renderWin.y1 = renderWinPixel.y1;
        renderWin.x2 = renderWinPixel.x2;
        renderWin.y2 = renderWinPixel.y2;

        const int step = 10;
        const double w= (renderWin.x2 - renderWin.x1);
        const double h= (renderWin.y2 - renderWin.y1);
        const double xstep = w / step;
        const double ystep= h / step;
        for (int i = 0; i <= step; ++i) {
            for (int j = 0; j < 2; ++j) {
                double x = renderWin.x1 + xstep * i;
                double y = renderWin.y1 + h * j;
                double xi, yi;
                // undistort/distort take pixel coordinates, do not divide by renderScale
                if (direction == eDirectionDistort) {
                    distortionModel->undistort(x, y, &xi, &yi); // inverse of getRoD
                } else {
                    distortionModel->distort(x, y, &xi, &yi); // inverse of getRoD
                }
                roiPixel.x1 = std::min(roiPixel.x1, xi);
                roiPixel.x2 = std::max(roiPixel.x2, xi);
                roiPixel.y1 = std::min(roiPixel.y1, yi);
                roiPixel.y2 = std::max(roiPixel.y2, yi);
            }
        }
        for (int i = 0; i < 2; ++i) {
            for (int j = 1; j < step; ++j) {
                double x = renderWin.x1 + w * i;
                double y = renderWin.y1 + ystep * j;
                double xi, yi;
                // undistort/distort take pixel coordinates, do not divide by renderScale
                if (direction == eDirectionDistort) {
                    distortionModel->undistort(x, y, &xi, &yi); // inverse of getRoD
                } else {
                    distortionModel->distort(x, y, &xi, &yi); // inverse of getRoD
                }
                roiPixel.x1 = std::min(roiPixel.x1, xi);
                roiPixel.x2 = std::max(roiPixel.x2, xi);
                roiPixel.y1 = std::min(roiPixel.y1, yi);
                roiPixel.y2 = std::max(roiPixel.y2, yi);
            }
        }
        assert( !OFX::Coords::rectIsEmpty(roiPixel) );
        // Slight extra margin, just in case.
        roiPixel.x1 -= 2;
        roiPixel.x2 += 2;
        roiPixel.y1 -= 2;
        roiPixel.y2 += 2;

        OfxRectD roi;
        OFX::Coords::toCanonical(roiPixel, args.renderScale, par, &roi);
        assert( !OFX::Coords::rectIsEmpty(roi) );
        rois.setRegionOfInterest(*_srcClip, roi);
        /*
        printf("getRegionsOfInterest: rs=(%g,%g) rw=(%g,%g,%g,%g) rwp=(%d,%d,%d,%d) -> roiPixel(%g,%g,%g,%g) roiCanonical=(%g,%g,%g,%g)\n",
               args.renderScale.x, args.renderScale.y,
               renderWin.x1, renderWin.y1, renderWin.x2, renderWin.y2,
               renderWinPixel.x1, renderWinPixel.y1, renderWinPixel.x2, renderWinPixel.y2,
               roiPixel.x1, roiPixel.y1, roiPixel.x2, roiPixel.y2,
               roi.x1, roi.y1, roi.x2, roi.y2);
        */

        return;
    }

    // ask for full RoD of srcClip
    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
    rois.setRegionOfInterest(*_srcClip, srcRod);
    // only ask for the renderWindow (intersected with the RoD) from uvClip
    if (_uvClip) {
        OfxRectD uvRoI = _uvClip->getRegionOfDefinition(time);
        Coords::rectIntersection(uvRoI, args.regionOfInterest, &uvRoI);
        rois.setRegionOfInterest(*_uvClip, uvRoI);
    }
}

bool
DistortionPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double time = args.time;

    switch (_plugin) {
    case eDistortionPluginSTMap: {
        if (_uvClip) {
            // IDistort: RoD is the same as uv map
            rod = _uvClip->getRegionOfDefinition(time);

            return true;
        }
        break;
    }
    case eDistortionPluginIDistort: {
        if (_srcClip) {
            // IDistort: RoD is the same as srcClip
            rod = _srcClip->getRegionOfDefinition(time);

            return true;
        }
        break;
    }
    case eDistortionPluginLensDistortion: {
        if (_majorVersion < 3) {
             return false; // use source RoD
        }
        OfxRectD format = {0, 1, 0, 1};
        double par = 1.;
        getLensDistortionFormat(time, args.renderScale, &format, &par);

        DirectionEnum direction = _direction ? (DirectionEnum)_direction->getValue() : eDirectionDistort;
        auto_ptr<DistortionModel> distortionModel( getDistortionModel(format, args.renderScale, time) );

        OfxRectD rodPixel = { std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
        assert( OFX::Coords::rectIsEmpty(rodPixel) );

        OfxRectD srcRodPixel;
        if (_srcClip && _srcClip->isConnected()) {
            OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
            double srcPar = _srcClip->getPixelAspectRatio();
            srcRodPixel.x1 = srcRod.x1 * args.renderScale.x / srcPar;
            srcRodPixel.y1 = srcRod.y1 * args.renderScale.y;
            srcRodPixel.x2 = srcRod.x2 * args.renderScale.x / srcPar;
            srcRodPixel.y2 = srcRod.y2 * args.renderScale.y;
        } else {
            srcRodPixel.x1 = format.x1;
            srcRodPixel.y1 = format.y1;
            srcRodPixel.x2 = format.x2;
            srcRodPixel.y2 = format.y2;
        }
        if (OFX::Coords::rectIsEmpty(srcRodPixel)) {
            return false;
        }
        const int step = 10;
        const double w= (srcRodPixel.x2 - srcRodPixel.x1);
        const double h= (srcRodPixel.y2 - srcRodPixel.y1);
        const double xstep = w / step;
        const double ystep= h / step;
        for (int i = 0; i <= step; ++i) {
            for (int j = 0; j < 2; ++j) {
                double x = srcRodPixel.x1 + xstep * i;
                double y = srcRodPixel.y1 + h * j;
                double xo, yo;
                if (direction == eDirectionDistort) {
                    distortionModel->distort(x, y, &xo, &yo);
                } else {
                    distortionModel->undistort(x, y, &xo, &yo);
                }
                rodPixel.x1 = std::min(rodPixel.x1, xo);
                rodPixel.x2 = std::max(rodPixel.x2, xo);
                rodPixel.y1 = std::min(rodPixel.y1, yo);
                rodPixel.y2 = std::max(rodPixel.y2, yo);
            }
        }
        for (int i = 0; i < 2; ++i) {
            for (int j = 1; j < step; ++j) {
                double x = srcRodPixel.x1 + w * i;
                double y = srcRodPixel.y1 + ystep * j;
                double xo, yo;
                if (direction == eDirectionDistort) {
                    distortionModel->distort(x, y, &xo, &yo);
                } else {
                    distortionModel->undistort(x, y, &xo, &yo);
                }
                rodPixel.x1 = std::min(rodPixel.x1, xo);
                rodPixel.x2 = std::max(rodPixel.x2, xo);
                rodPixel.y1 = std::min(rodPixel.y1, yo);
                rodPixel.y2 = std::max(rodPixel.y2, yo);
            }
        }
        assert( !OFX::Coords::rectIsEmpty(rodPixel) );
        // extra margin for blackOutside
        if ( _blackOutside->getValueAtTime(time) ) {
            rodPixel.x1 -= 1;
            rodPixel.x2 += 1;
            rodPixel.y1 -= 1;
            rodPixel.y2 += 1;
        }
        // Slight extra margin, just in case.
        rodPixel.x1 -= 2;
        rodPixel.x2 += 2;
        rodPixel.y1 -= 2;
        rodPixel.y2 += 2;

        OFX::Coords::toCanonical(rodPixel, args.renderScale, par, &rod);
        assert( !OFX::Coords::rectIsEmpty(rod) );
        /*
        printf("getRegionOfDefinition: rs=(%g,%g) srcrodp=(%g,%g,%g,%g) -> rodPixel(%g,%g,%g,%g) rodCanonical=(%g,%g,%g,%g)\n",
               args.renderScale.x, args.renderScale.y,
               srcRod.x1, srcRod.y1, srcRod.x2, srcRod.y2,
               rodPixel.x1, rodPixel.y1, rodPixel.x2, rodPixel.y2,
               rod.x1, rod.y1, rod.x2, rod.y2);
         */

        if ( _cropToFormat && _cropToFormat->getValueAtTime(time) ) {
            // crop to format works by clamping the output rod to the format wherever the input rod was inside the format
            // this avoids unwanted crops (cropToFormat is checked by default)
            OfxRectI srcFormat;
            _srcClip->getFormat(srcFormat);
            OfxRectI srcFormatEnclosing;
            srcFormatEnclosing.x1 = std::floor(srcFormat.x1 * args.renderScale.x);
            srcFormatEnclosing.x2 = std::ceil(srcFormat.x2 * args.renderScale.x);
            srcFormatEnclosing.y1 = std::floor(srcFormat.y1 * args.renderScale.y);
            srcFormatEnclosing.y2 = std::ceil(srcFormat.y2 * args.renderScale.y);
            srcFormat.x1 = std::ceil(srcFormat.x1 * args.renderScale.x);
            srcFormat.x2 = std::floor(srcFormat.x2 * args.renderScale.x);
            srcFormat.y1 = std::ceil(srcFormat.y1 * args.renderScale.y);
            srcFormat.y2 = std::floor(srcFormat.y2 * args.renderScale.y);
            if (! Coords::rectIsEmpty(srcFormat) ) {
                if (rodPixel.x1 < srcFormat.x1 && srcRodPixel.x1 >= srcFormatEnclosing.x1) {
                    rod.x1 = srcFormat.x1 / args.renderScale.x * par;
                }
                if (rodPixel.x2 > srcFormat.x2 && srcRodPixel.x2 <= srcFormatEnclosing.x2) {
                    rod.x2 = srcFormat.x2 / args.renderScale.x * par;
                }
                if (rodPixel.y1 < srcFormat.y1 && srcRodPixel.y1 >= srcFormatEnclosing.y1) {
                    rod.y1 = srcFormat.y1 / args.renderScale.y;
                }
                if (rodPixel.y2 > srcFormat.y2 && srcRodPixel.y2 <= srcFormatEnclosing.y2) {
                    rod.y2 = srcFormat.y2 / args.renderScale.y;
                }
            }
        }
        return true;
        //return false;     // use source RoD
        break;
    }
    } // switch (_plugin)

    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
OfxStatus
DistortionPlugin::getClipComponents(const ClipComponentsArguments& args,
                                    ClipComponentsSetter& clipComponents)
{
    assert(gIsMultiPlaneV2);

    OfxStatus stat = kOfxStatReplyDefault;
    if (_uvClip) {
        stat = MultiPlaneEffect::getClipComponents(args, clipComponents);
        clipComponents.setPassThroughClip(_srcClip, args.time, args.view);
    }
    return stat;
} // getClipComponents

#endif

void
DistortionPlugin::updateVisibility()
{
    if (_plugin == eDistortionPluginLensDistortion) {
        {
            GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();
            bool hasFormat = (extent == eGeneratorExtentFormat);
            bool hasSize = (extent == eGeneratorExtentSize);

            _format->setIsSecretAndDisabled(!hasFormat);
            _size->setIsSecretAndDisabled(!hasSize);
            _recenter->setIsSecretAndDisabled(!hasSize);
            _btmLeft->setIsSecretAndDisabled(!hasSize);
        }

        DistortionModelEnum distortionModel = (DistortionModelEnum)_distortionModel->getValue();

        _k1->setIsSecretAndDisabled(distortionModel != eDistortionModelNuke);
        _k2->setIsSecretAndDisabled(distortionModel != eDistortionModelNuke);
        _center->setIsSecretAndDisabled(distortionModel != eDistortionModelNuke);
        _squeeze->setIsSecretAndDisabled(distortionModel != eDistortionModelNuke);
        _asymmetric->setIsSecretAndDisabled(distortionModel != eDistortionModelNuke);

        _pfFile->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);
        if (_pfReload) {
            _pfReload->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);
        }
        _pfC3->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);
        _pfC5->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);
        _pfSqueeze->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);
        _pfP->setIsSecretAndDisabled(distortionModel != eDistortionModelPFBarrel);

        bool distortionModel3DE = (distortionModel == eDistortionModel3DEClassic ||
                                   distortionModel == eDistortionModel3DEAnamorphic6 ||
                                   distortionModel == eDistortionModel3DEFishEye8 ||
                                   distortionModel == eDistortionModel3DEStandard ||
                                   distortionModel == eDistortionModel3DEAnamorphic4);
        _xa_fov_unit->setIsSecretAndDisabled(!distortionModel3DE);
        _ya_fov_unit->setIsSecretAndDisabled(!distortionModel3DE);
        _xb_fov_unit->setIsSecretAndDisabled(!distortionModel3DE);
        _yb_fov_unit->setIsSecretAndDisabled(!distortionModel3DE);
        _fl_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _fd_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _w_fb_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _h_fb_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _x_lco_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _y_lco_cm->setIsSecretAndDisabled(!distortionModel3DE);
        _pa->setIsSecretAndDisabled(!distortionModel3DE);

        _ld->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEClassic);
        _sq->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEClassic);
        _cx->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEClassic);
        _cy->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEClassic);
        _qu->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEClassic);

        _c2->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard &&
                                    distortionModel != eDistortionModel3DEFishEye8);
        _u1->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _v1->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _c4->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard &&
                                    distortionModel != eDistortionModel3DEFishEye8);
        _u3->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _v3->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _phi->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _b->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);

        _cx02->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cy02->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cx22->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cy22->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cx04->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cy04->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cx24->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cy24->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cx44->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cy44->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4 &&
                                      distortionModel != eDistortionModel3DEAnamorphic6);
        _cx06->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cy06->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cx26->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cy26->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cx46->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cy46->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cx66->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _cy66->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic6);
        _a4phi->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4);
        _a4sqx->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4);
        _a4sqy->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEAnamorphic4);

        _c6->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEFishEye8);
        _c8->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEFishEye8);

        _pta->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _ptb->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _ptc->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _ptd->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _pte->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _ptg->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
        _ptt->setIsSecretAndDisabled(distortionModel != eDistortionModelPanoTools);
    }
}

void
DistortionPlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if (_plugin == eDistortionPluginLensDistortion) {
        if ( (paramName == kParamDistortionModel) && (args.reason == eChangeUserEdit) ) {
            updateVisibility();
        } else if ( (paramName == kParamPFFileReload) ||
            ( (paramName == kParamPFFile) && (args.reason == eChangeUserEdit) ) ) {
            std::string filename;
            PFBarrelCommon::FileReader f(filename);

            beginEditBlock(kParamPFFile);
            _pfC3->deleteAllKeys();
            _pfC5->deleteAllKeys();
            _pfP->deleteAllKeys();
            if (f.model_ == 0) {
                _pfC5->setValue(0.);
            }
            if (f.nkeys_ == 1) {
                _pfC3->setValue(f.c3_[0]);
                _pfC5->setValue(f.c5_[0]);
                _pfP->setValue(f.xp_[0], f.yp_[0]);
            } else {
                for (int i = 0; i < f.nkeys_; ++i) {
                    _pfC3->setValueAtTime(f.frame_[i], f.c3_[0]);
                    if (f.model_ == 1) {
                        _pfC5->setValueAtTime(f.frame_[i], f.c5_[0]);
                    }
                    _pfP->setValueAtTime(f.frame_[i], f.xp_[0], f.yp_[0]);
                }
            }
            endEditBlock();
        } else if (paramName == kParamGeneratorExtent) {
            updateVisibility();
        } else if (paramName == kParamGeneratorFormat) {
            //the host does not handle the format itself, do it ourselves
            EParamFormat format = (EParamFormat)_format->getValue();
            int w = 0, h = 0;
            double par = -1;
            getFormatResolution(format, &w, &h, &par);
            assert(par != -1);
            _formatPar->setValue(par);
            _formatSize->setValue(w, h);
        } else if (paramName == kParamGeneratorCenter) {
            Clip* srcClip = _srcClip;
            OfxRectD srcRoD;
            if ( srcClip && srcClip->isConnected() ) {
                srcRoD = srcClip->getRegionOfDefinition(args.time);
            } else {
                OfxPointD siz = getProjectSize();
                OfxPointD off = getProjectOffset();
                srcRoD.x1 = off.x;
                srcRoD.x2 = off.x + siz.x;
                srcRoD.y1 = off.y;
                srcRoD.y2 = off.y + siz.y;
            }
            OfxPointD center;
            center.x = (srcRoD.x2 + srcRoD.x1) / 2.;
            center.y = (srcRoD.y2 + srcRoD.y1) / 2.;

            OfxRectD rectangle;
            _size->getValue(rectangle.x2, rectangle.y2);
            _btmLeft->getValue(rectangle.x1, rectangle.y1);
            rectangle.x2 += rectangle.x1;
            rectangle.y2 += rectangle.y1;

            OfxRectD newRectangle;
            newRectangle.x1 = center.x - (rectangle.x2 - rectangle.x1) / 2.;
            newRectangle.y1 = center.y - (rectangle.y2 - rectangle.y1) / 2.;
            newRectangle.x2 = newRectangle.x1 + (rectangle.x2 - rectangle.x1);
            newRectangle.y2 = newRectangle.y1 + (rectangle.y2 - rectangle.y1);
            
            _size->setValue(newRectangle.x2 - newRectangle.x1, newRectangle.y2 - newRectangle.y1);
            _btmLeft->setValue(newRectangle.x1, newRectangle.y1);
        }
        return;
    }
    if (_plugin == eDistortionPluginIDistort ||
        _plugin == eDistortionPluginSTMap) {
        if (gIsMultiPlaneV2) {
            MultiPlaneEffect::changedParam(args, paramName);
        }
    }
}

//mDeclarePluginFactory(DistortionPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<DistortionPluginEnum plugin, int majorVersion>
class DistortionPluginFactory
    : public PluginFactoryHelper<DistortionPluginFactory<plugin, majorVersion> >
{
public:
    DistortionPluginFactory<plugin, majorVersion>(const std::string & id, unsigned int verMaj, unsigned int verMin)
    : PluginFactoryHelper<DistortionPluginFactory>(id, verMaj, verMin)
    {
    }
    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();}
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};

template<DistortionPluginEnum plugin, int majorVersion>
void
DistortionPluginFactory<plugin, majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    switch (plugin) {
    case eDistortionPluginSTMap:
        desc.setLabel(kPluginSTMapName);
        desc.setPluginGrouping(kPluginSTMapGrouping);
        desc.setPluginDescription(kPluginSTMapDescription);
        break;
    case eDistortionPluginIDistort:
        desc.setLabel(kPluginIDistortName);
        desc.setPluginGrouping(kPluginIDistortGrouping);
        desc.setPluginDescription(kPluginIDistortDescription);
        break;
    case eDistortionPluginLensDistortion:
        desc.setLabel(kPluginLensDistortionName);
        desc.setPluginGrouping(kPluginLensDistortionGrouping);
        desc.setPluginDescription(kPluginLensDistortionDescription);
        break;
    }

    //desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
    switch (plugin) {
    case eDistortionPluginSTMap:
        //desc.addSupportedBitDepth(eBitDepthUByte); // not yet supported (requires special processing for uv clip values)
        //desc.addSupportedBitDepth(eBitDepthUShort);
        desc.addSupportedBitDepth(eBitDepthFloat);
        break;
    case eDistortionPluginIDistort:
        //desc.addSupportedBitDepth(eBitDepthUByte); // not yet supported (requires special processing for uv clip values)
        //desc.addSupportedBitDepth(eBitDepthUShort);
        desc.addSupportedBitDepth(eBitDepthFloat);
        break;
    case eDistortionPluginLensDistortion:
        desc.addSupportedBitDepth(eBitDepthUByte);
        desc.addSupportedBitDepth(eBitDepthUShort);
        desc.addSupportedBitDepth(eBitDepthFloat);
        break;
    }


    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setSupportsRenderQuality(true);
#ifdef OFX_EXTENSIONS_NUKE
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif


    gIsMultiPlaneV1 = false;
    gIsMultiPlaneV2 = false;
#if defined(OFX_EXTENSIONS_NUKE)
    gIsMultiPlaneV1 = getImageEffectHostDescription()->isMultiPlanar;
#endif
#if defined(OFX_EXTENSIONS_NUKE) && defined(OFX_EXTENSIONS_NATRON)
    gIsMultiPlaneV2 = getImageEffectHostDescription()->supportsDynamicChoices && gIsMultiPlaneV1;
    if ((gIsMultiPlaneV1 || gIsMultiPlaneV2) && (plugin == eDistortionPluginSTMap || plugin == eDistortionPluginIDistort)) {
        // This enables fetching different planes from the input.
        // Generally the user will read a multi-layered EXR file in the Reader node and then use the shuffle
        // to redirect the plane's channels into RGBA color plane.

        desc.setIsMultiPlanar(true);
    }
#endif
    if (plugin == eDistortionPluginLensDistortion) {
        desc.setOverlayInteractDescriptor(new GeneratorOverlayDescriptor);
    }
    if ( (plugin == eDistortionPluginLensDistortion && this->getMajorVersion() < kPluginVersionLensDistortionMajor) ||
         (plugin != eDistortionPluginLensDistortion && this->getMajorVersion() < kPluginVersionMajor) ) {
        desc.setIsDeprecated(true);
    }
} // >::describe

static void
addWrapOptions(ChoiceParamDescriptor* channel,
               WrapEnum def)
{
    assert(channel->getNOptions() == eWrapClamp);
    channel->appendOption(kParamWrapOptionClamp);
    assert(channel->getNOptions() == eWrapRepeat);
    channel->appendOption(kParamWrapOptionRepeat);
    assert(channel->getNOptions() == eWrapMirror);
    channel->appendOption(kParamWrapOptionMirror);
    channel->setDefault(def);
}

template<DistortionPluginEnum plugin, int majorVersion>
void
DistortionPluginFactory<plugin, majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                   ContextEnum context)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( gIsMultiPlaneV2 && !fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    } else if (gIsMultiPlaneV1 && !fetchSuite(kFnOfxImageEffectPlaneSuite, 1, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }
#endif

    if (plugin ==  eDistortionPluginSTMap) {
        // create the uv clip
        // uv clip is defined first, because the output format is taken from the RoD of the first clip in Nuke
        ClipDescriptor *uvClip = desc.defineClip(kClipUV);
        uvClip->addSupportedComponent(ePixelComponentRGBA);
        uvClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
        uvClip->addSupportedComponent(ePixelComponentXY);
#endif
        uvClip->addSupportedComponent(ePixelComponentAlpha);
        uvClip->setTemporalClipAccess(false);
        uvClip->setSupportsTiles(kSupportsTiles);
        uvClip->setIsMask(false);
        uvClip->setOptional(false);
    }
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->setCanTransform(true); // we can concatenate transforms upwards on srcClip only
#endif
    srcClip->setIsMask(false);
    if (plugin == eDistortionPluginLensDistortion) {
        // in LensDistrortion, if Output Mode is set to STMap, the size is taken from the project size
        srcClip->setOptional(true);
    }
    if (plugin == eDistortionPluginIDistort) {
        // create the uv clip
        ClipDescriptor *uvClip = desc.defineClip(kClipUV);
        uvClip->addSupportedComponent(ePixelComponentRGBA);
        uvClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
        uvClip->addSupportedComponent(ePixelComponentXY);
#endif
        uvClip->addSupportedComponent(ePixelComponentAlpha);
        uvClip->setTemporalClipAccess(false);
        uvClip->setSupportsTiles(kSupportsTiles);
        uvClip->setIsMask(false);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NUKE
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabelAndHint(kParamProcessRLabel);
        param->setDefault(true);
#ifdef OFX_EXTENSIONS_NUKE
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabelAndHint(kParamProcessGLabel);
        param->setDefault(true);
#ifdef OFX_EXTENSIONS_NUKE
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabelAndHint(kParamProcessBLabel);
        param->setDefault(true);
#ifdef OFX_EXTENSIONS_NUKE
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabelAndHint(kParamProcessALabel);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    if ( (plugin == eDistortionPluginIDistort) ||
         ( plugin == eDistortionPluginSTMap) ) {
        std::vector<std::string> clipsForChannels(1);
        clipsForChannels[0] = kClipUV;

        if (gIsMultiPlaneV2) {
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamChannelU, kParamChannelULabel);
#ifdef OFX_EXTENSIONS_NUKE
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
                param->setDefault(eInputChannelR);
            }
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamChannelV, kParamChannelVLabel);
                param->setDefault(eInputChannelG);
            }
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddPlaneChannelChoice(desc, page, clipsForChannels, kParamChannelA, kParamChannelALabel);
                param->setDefault(eInputChannelA);
            }
        } else {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelU);
                param->setLabelAndHint(kParamChannelULabel);
#ifdef OFX_EXTENSIONS_NUKE
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, !gIsMultiPlaneV1 /*addOnlyColorPlane*/);
                param->setDefault(eInputChannelR);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelV);
                param->setLabelAndHint(kParamChannelVLabel);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, !gIsMultiPlaneV1 /*addOnlyColorPlane*/);
                param->setDefault(eInputChannelG);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelA);
                param->setLabelAndHint(kParamChannelALabel);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true /*addConstants*/, !gIsMultiPlaneV1 /*addOnlyColorPlane*/);
                param->setDefault(eInputChannelA);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamChannelUnpremultUV);
            param->setLabelAndHint(kParamChannelUnpremultUVLabel);
            param->setDefault(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVOffset);
            param->setLabelAndHint(kParamUVOffsetLabel);
            param->setDefault(0., 0.);
            param->setDoubleType(eDoubleTypePlain);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 0., 1., 1.);
            param->setDimensionLabels("U", "V");
            param->setUseHostNativeOverlayHandle(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVScale);
            param->setLabelAndHint(kParamUVScaleLabel);
            param->setDoubleType(eDoubleTypeScale);
            param->setDefault(1., 1.);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 0., 100., 100.);
            param->setDimensionLabels("U", "V");
            param->setUseHostNativeOverlayHandle(false);
            if (page) {
                page->addChild(*param);
            }
        }

        if (plugin == eDistortionPluginSTMap) {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamWrapU);
                param->setLabelAndHint(kParamWrapULabel);
#ifdef OFX_EXTENSIONS_NUKE
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
                addWrapOptions(param, eWrapClamp);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamWrapV);
                param->setLabelAndHint(kParamWrapVLabel);
                addWrapOptions(param, eWrapClamp);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

    if (plugin == eDistortionPluginLensDistortion) {
        { // Frame format
            // extent
            {
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
                param->setLabelAndHint(kParamFormatLabel);
                assert(param->getNOptions() == eGeneratorExtentFormat);
                param->appendOption(kParamGeneratorExtentOptionFormat);
                assert(param->getNOptions() == eGeneratorExtentSize);
                param->appendOption(kParamGeneratorExtentOptionSize);
                assert(param->getNOptions() == eGeneratorExtentProject);
                param->appendOption(kParamGeneratorExtentOptionProject);
                assert(param->getNOptions() == eGeneratorExtentDefault);
                param->appendOption(kParamGeneratorExtentOptionDefault);
                param->setDefault(eGeneratorExtentDefault);
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
                param->setAnimates(false);
                desc.addClipPreferencesSlaveParam(*param);
                if (page) {
                    page->addChild(*param);
                }
            }

            // recenter
            {
                PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamGeneratorCenter);
                param->setLabel(kParamGeneratorCenterLabel);
                param->setHint(kParamGeneratorCenterHint);
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
                if (page) {
                    page->addChild(*param);
                }
            }

            // format
            {
                ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamGeneratorFormat);
                param->setLabel(kParamGeneratorFormatLabel);
                assert(param->getNOptions() == eParamFormatPCVideo);
                param->appendOption(kParamFormatPCVideoLabel, "", kParamFormatPCVideo);
                assert(param->getNOptions() == eParamFormatNTSC);
                param->appendOption(kParamFormatNTSCLabel, "", kParamFormatNTSC);
                assert(param->getNOptions() == eParamFormatPAL);
                param->appendOption(kParamFormatPALLabel, "", kParamFormatPAL);
                assert(param->getNOptions() == eParamFormatNTSC169);
                param->appendOption(kParamFormatNTSC169Label, "", kParamFormatNTSC169);
                assert(param->getNOptions() == eParamFormatPAL169);
                param->appendOption(kParamFormatPAL169Label, "", kParamFormatPAL169);
                assert(param->getNOptions() == eParamFormatHD720);
                param->appendOption(kParamFormatHD720Label, "", kParamFormatHD720);
                assert(param->getNOptions() == eParamFormatHD);
                param->appendOption(kParamFormatHDLabel, "", kParamFormatHD);
                assert(param->getNOptions() == eParamFormatUHD4K);
                param->appendOption(kParamFormatUHD4KLabel, "", kParamFormatUHD4K);
                assert(param->getNOptions() == eParamFormat1kSuper35);
                param->appendOption(kParamFormat1kSuper35Label, "", kParamFormat1kSuper35);
                assert(param->getNOptions() == eParamFormat1kCinemascope);
                param->appendOption(kParamFormat1kCinemascopeLabel, "", kParamFormat1kCinemascope);
                assert(param->getNOptions() == eParamFormat2kSuper35);
                param->appendOption(kParamFormat2kSuper35Label, "", kParamFormat2kSuper35);
                assert(param->getNOptions() == eParamFormat2kCinemascope);
                param->appendOption(kParamFormat2kCinemascopeLabel, "", kParamFormat2kCinemascope);
                assert(param->getNOptions() == eParamFormat2kDCP);
                param->appendOption(kParamFormat2kDCPLabel, "", kParamFormat2kDCP);
                assert(param->getNOptions() == eParamFormat4kSuper35);
                param->appendOption(kParamFormat4kSuper35Label, "", kParamFormat4kSuper35);
                assert(param->getNOptions() == eParamFormat4kCinemascope);
                param->appendOption(kParamFormat4kCinemascopeLabel, "", kParamFormat4kCinemascope);
                assert(param->getNOptions() == eParamFormat4kDCP);
                param->appendOption(kParamFormat4kDCPLabel, "", kParamFormat4kDCP);
                assert(param->getNOptions() == eParamFormatSquare256);
                param->appendOption(kParamFormatSquare256Label, "", kParamFormatSquare256);
                assert(param->getNOptions() == eParamFormatSquare512);
                param->appendOption(kParamFormatSquare512Label, "", kParamFormatSquare512);
                assert(param->getNOptions() == eParamFormatSquare1k);
                param->appendOption(kParamFormatSquare1kLabel, "", kParamFormatSquare1k);
                assert(param->getNOptions() == eParamFormatSquare2k);
                param->appendOption(kParamFormatSquare2kLabel, "", kParamFormatSquare2k);
                param->setDefault(eParamFormatPCVideo);
                param->setHint(kParamGeneratorFormatHint);
                param->setAnimates(false);
                desc.addClipPreferencesSlaveParam(*param);
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                int w = 0, h = 0;
                double par = -1.;
                getFormatResolution(eParamFormatPCVideo, &w, &h, &par);
                assert(par != -1);
                {
                    Int2DParamDescriptor* param = desc.defineInt2DParam(kParamGeneratorSize);
                    param->setLabelAndHint(kParamGeneratorSizeLabel, kParamGeneratorSizeHint);
                    param->setIsSecretAndDisabled(true);
                    param->setDefault(w, h);
                    if (page) {
                        page->addChild(*param);
                    }
                }

                {
                    DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGeneratorPAR);
                    param->setLabelAndHint(kParamGeneratorPARLabel, kParamGeneratorPARHint);
                    param->setIsSecretAndDisabled(true);
                    param->setRange(0., DBL_MAX);
                    param->setDisplayRange(0.5, 2.);
                    param->setDefault(par);
                    if (page) {
                        page->addChild(*param);
                    }
                }
            }

            // btmLeft
            {
                Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
                param->setLabelAndHint(kParamRectangleInteractBtmLeftLabel, "Coordinates of the bottom left corner of the size rectangle.");
                param->setDoubleType(eDoubleTypeXYAbsolute);
                if ( param->supportsDefaultCoordinateSystem() ) {
                    param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
                } else {
                    gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
                }
                param->setDefault(0., 0.);
                param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
                param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
                param->setIncrement(1.);
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
                param->setDigits(0);
                if (page) {
                    page->addChild(*param);
                }
            }

            // size
            {
                Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
                param->setLabelAndHint(kParamRectangleInteractSizeLabel, "Width and height of the size rectangle.");
                param->setDoubleType(eDoubleTypeXY);
                if ( param->supportsDefaultCoordinateSystem() ) {
                    param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
                } else {
                    gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
                }
                param->setDefault(1., 1.);
                param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
                param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
                param->setIncrement(1.);
                param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
                param->setIncrement(1.);
                param->setDigits(0);
                if (page) {
                    page->addChild(*param);
                }
            }

        }

        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionModel);
            param->setLabelAndHint(kParamDistortionModelLabel);
            assert(param->getNOptions() == eDistortionModelNuke);
            param->appendOption(kParamDistortionModelOptionNuke);
            assert(param->getNOptions() == eDistortionModelPFBarrel);
            param->appendOption(kParamDistortionModelOptionPFBarrel);
            assert(param->getNOptions() == eDistortionModel3DEClassic);
            param->appendOption(kParamDistortionModelOption3DEClassic);
            assert(param->getNOptions() == eDistortionModel3DEAnamorphic6);
            param->appendOption(kParamDistortionModelOption3DEAnamorphic6);
            assert(param->getNOptions() == eDistortionModel3DEFishEye8);
            param->appendOption(kParamDistortionModelOption3DEFishEye8);
            assert(param->getNOptions() == eDistortionModel3DEStandard);
            param->appendOption(kParamDistortionModelOption3DEStandard);
            assert(param->getNOptions() == eDistortionModel3DEAnamorphic4);
            param->appendOption(kParamDistortionModelOption3DEAnamorphic4);
            assert(param->getNOptions() == eDistortionModelPanoTools);
            param->appendOption(kParamDistortionModelOptionPanoTools);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionDirection);
            param->setLabelAndHint(kParamDistortionDirectionLabel);
            assert(param->getNOptions() == eDirectionDistort);
            param->appendOption(kParamDistortionDirectionOptionDistort);
            assert(param->getNOptions() == eDirectionUndistort);
            param->appendOption(kParamDistortionDirectionOptionUndistort);
            param->setDefault((int)eDirectionDistort); // same default as in Nuke, and also the most straightforward (non-iterative) transform for most models
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionOutputMode);
            param->setLabelAndHint(kParamDistortionOutputModeLabel);
            assert(param->getNOptions() == eOutputModeImage);
            param->appendOption(kParamDistortionOutputModeOptionImage);
            assert(param->getNOptions() == eOutputModeSTMap);
            param->appendOption(kParamDistortionOutputModeOptionSTMap);
            if (page) {
                page->addChild(*param);
            }
        }

        // Nuke
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK1);
            param->setLabelAndHint(kParamK1Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.3, 0.3);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK2);
            param->setLabelAndHint(kParamK2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.1, 0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamCenter);
            param->setLabelAndHint(kParamCenterLabel);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setUseHostNativeOverlayHandle(false);
            param->setDoubleType(eDoubleTypePlain);
            param->setDisplayRange(-1, -1, 1, 1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSqueeze);
            param->setLabelAndHint(kParamSqueezeLabel);
            param->setDefault(1.);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamAsymmetric);
            param->setLabelAndHint(kParamAsymmetricLabel);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDoubleType(eDoubleTypePlain);
            param->setDisplayRange(-0.5, -0.5, 0.5, 0.5);
            param->setUseHostNativeOverlayHandle(false);
            if (page) {
                page->addChild(*param);
            }
        }

        ////////////
        // PFBarrel
        {
            StringParamDescriptor *param = desc.defineStringParam(kParamPFFile);
            param->setLabelAndHint(kParamPFFileLabel);
            param->setStringType(eStringTypeFilePath);
            param->setFilePathExists(true);
#ifdef OFX_EXTENSIONS_NUKE
            if (!getImageEffectHostDescription()->isNatron) {
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
            }
#endif
            if (page) {
                page->addChild(*param);
            }
        }
        if (!getImageEffectHostDescription()->isNatron) {
            // Natron has its own reload button
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamPFFileReload);
            param->setLabelAndHint(kParamPFFileReloadLabel);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPFC3);
            param->setLabelAndHint(kParamPFC3Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPFC5);
            param->setLabelAndHint(kParamPFC5Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamPFP);
            param->setLabelAndHint(kParamPFPLabel);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 0., 1., 1.);
            param->setDefault(0.5, 0.5);
            param->setDoubleType(eDoubleTypePlain);
            param->setUseHostNativeOverlayHandle(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPFSqueeze);
            param->setLabelAndHint(kParamPFSqueezeLabel);
            param->setRange(0.0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 3.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }

        ////////////////
        // 3DEqualizer
        // fov parameters
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_xa_fov_unit);
            param->setLabelAndHint(kParam3DE4_xa_fov_unitLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_ya_fov_unit);
            param->setLabelAndHint(kParam3DE4_ya_fov_unitLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_xb_fov_unit);
            param->setLabelAndHint(kParam3DE4_xb_fov_unitLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_yb_fov_unit);
            param->setLabelAndHint(kParam3DE4_yb_fov_unitLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        // seven builtin parameters
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_focal_length_cm);
            param->setLabelAndHint(kParam3DE4_focal_length_cmLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.5, 50.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_custom_focus_distance_cm);
            param->setLabelAndHint(kParam3DE4_custom_focus_distance_cmLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(10, 1000.);
            param->setDefault(100.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_filmback_width_cm);
            param->setLabelAndHint(kParam3DE4_filmback_width_cmLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 10.);
            param->setDefault(0.8);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_filmback_height_cm);
            param->setLabelAndHint(kParam3DE4_filmback_height_cmLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 10.);
            param->setDefault(0.6);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_lens_center_offset_x_cm);
            param->setLabelAndHint(kParam3DE4_lens_center_offset_x_cmLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-5., 5.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_lens_center_offset_y_cm);
            param->setLabelAndHint(kParam3DE4_lens_center_offset_y_cmLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-5., 5.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_pixel_aspect);
            param->setLabelAndHint(kParam3DE4_pixel_aspectLabel);
            param->setRange(0., DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.25, 4.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        // 3DE Classic model
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEDistortion);
            param->setLabelAndHint(kParam3DEDistortionLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEAnamorphicSqueeze);
            param->setLabelAndHint(kParam3DEAnamorphicSqueezeLabel);
            param->setRange(0., DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.25, 4.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECurvatureX);
            param->setLabelAndHint(kParam3DECurvatureXLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECurvatureY);
            param->setLabelAndHint(kParam3DECurvatureYLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEQuarticDistortion);
            param->setLabelAndHint(kParam3DEQuarticDistortionLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        // 3DE Radial Standard Degree 4
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEDistortionDegree2);
            param->setLabelAndHint(kParam3DEDistortionDegree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEUDegree2);
            param->setLabelAndHint(kParam3DEUDegree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEVDegree2);
            param->setLabelAndHint(kParam3DEVDegree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEQuarticDistortionDegree4);
            param->setLabelAndHint(kParam3DEQuarticDistortionDegree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEUDegree4);
            param->setLabelAndHint(kParam3DEUDegree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEVDegree4);
            param->setLabelAndHint(kParam3DEVDegree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEPhiCylindricDirection);
            param->setLabelAndHint(kParam3DEPhiCylindricDirectionLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-90., 90.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEBCylindricBending);
            param->setLabelAndHint(kParam3DEBCylindricBendingLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.1, 0.1);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        // 3DE4_Anamorphic_Standard_Degree_4 and 3DE4_Anamorphic_Degree_6
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx02Degree2);
            param->setLabelAndHint(kParam3DECx02Degree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy02Degree2);
            param->setLabelAndHint(kParam3DECy02Degree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx22Degree2);
            param->setLabelAndHint(kParam3DECx22Degree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy22Degree2);
            param->setLabelAndHint(kParam3DECy22Degree2Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx04Degree4);
            param->setLabelAndHint(kParam3DECx04Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy04Degree4);
            param->setLabelAndHint(kParam3DECy04Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx24Degree4);
            param->setLabelAndHint(kParam3DECx24Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy24Degree4);
            param->setLabelAndHint(kParam3DECy24Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx44Degree4);
            param->setLabelAndHint(kParam3DECx44Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy44Degree4);
            param->setLabelAndHint(kParam3DECy44Degree4Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx06Degree6);
            param->setLabelAndHint(kParam3DECx06Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy06Degree6);
            param->setLabelAndHint(kParam3DECy06Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx26Degree6);
            param->setLabelAndHint(kParam3DECx26Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy26Degree6);
            param->setLabelAndHint(kParam3DECy26Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx46Degree6);
            param->setLabelAndHint(kParam3DECx46Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy46Degree6);
            param->setLabelAndHint(kParam3DECy46Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECx66Degree6);
            param->setLabelAndHint(kParam3DECx66Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECy66Degree6);
            param->setLabelAndHint(kParam3DECy66Degree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DELensRotation);
            param->setLabelAndHint(kParam3DELensRotationLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-2., 2.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DESqueezeX);
            param->setLabelAndHint(kParam3DESqueezeXLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.9, 1.1);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DESqueezeY);
            param->setLabelAndHint(kParam3DESqueezeYLabel);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.9, 1.1);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        // 3DEFishEye8
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEDegree6);
            param->setLabelAndHint(kParam3DEDegree6Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEDegree8);
            param->setLabelAndHint(kParam3DEDegree8Label);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }

        // PanoTools
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsA);
            param->setLabelAndHint(kParamPanoToolsALabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.2, 0.2);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsB);
            param->setLabelAndHint(kParamPanoToolsBLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.2, 0.2);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsC);
            param->setLabelAndHint(kParamPanoToolsCLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.2, 0.2);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsD);
            param->setLabelAndHint(kParamPanoToolsDLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-1000., 1000.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsE);
            param->setLabelAndHint(kParamPanoToolsELabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-1000., 1000.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsG);
            param->setLabelAndHint(kParamPanoToolsGLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-1000., 1000.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPanoToolsT);
            param->setLabelAndHint(kParamPanoToolsTLabel);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-1000., 1000.);
            if (page) {
                page->addChild(*param);
            }
        }

    }

    ofxsFilterDescribeParamsInterpolate2D( desc, page, (plugin == eDistortionPluginSTMap) );
#ifdef OFX_EXTENSIONS_NATRON
    if (plugin == eDistortionPluginLensDistortion && majorVersion >= 4 && getImageEffectHostDescription()->isNatron) {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamCropToFormat);
        param->setLabel(kParamCropToFormatLabel);
        param->setHint(kParamCropToFormatHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

template<DistortionPluginEnum plugin, int majorVersion>
ImageEffect*
DistortionPluginFactory<plugin, majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                ContextEnum /*context*/)
{
    return new DistortionPlugin(handle, this->getMajorVersion(), plugin);
}

static DistortionPluginFactory<eDistortionPluginIDistort,kPluginVersionMajor> pIDistort(kPluginIDistortIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginSTMap,kPluginVersionMajor> pSTMap(kPluginSTMapIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginLensDistortion,2> pLensDistortion2(kPluginLensDistortionIdentifier, 2, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginLensDistortion,3> pLensDistortion3(kPluginLensDistortionIdentifier, 3, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginLensDistortion,kPluginVersionLensDistortionMajor> pLensDistortion4(kPluginLensDistortionIdentifier, kPluginVersionLensDistortionMajor, kPluginVersionLensDistortionMinor);
mRegisterPluginFactoryInstance(pIDistort)
mRegisterPluginFactoryInstance(pSTMap)
mRegisterPluginFactoryInstance(pLensDistortion2)
mRegisterPluginFactoryInstance(pLensDistortion3)
mRegisterPluginFactoryInstance(pLensDistortion4)

OFXS_NAMESPACE_ANONYMOUS_EXIT
