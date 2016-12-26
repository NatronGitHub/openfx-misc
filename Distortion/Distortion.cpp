/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
    "LensDistortion can directly apply distortion/undistortion, but if the distortion parameters are not animated, the most efficient way to use LensDistortion and avoid repeated distortion function calculations is the following:\n" \
    "- If the footage size is not the same as the project size, insert a FrameHold plugin between the footage to distort or undistort and the Source input of LensDistortion. This connection is only used to get the size of the input footage.\n" \
    "- Set Output Mode to \"STMap\" in LensDistortion.\n" \
    "- feed the LensDistortion output into the UV input of STMap, and feed the footage into the Source input of STMap.\n" \
    "This plugin concatenates transforms upstream." \

#define kPluginLensDistortionIdentifier "net.sf.openfx.LensDistortion"

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

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

enum DistortionPluginEnum
{
    eDistortionPluginSTMap,
    eDistortionPluginIDistort,
    eDistortionPluginLensDistortion,
};

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamChannelU "channelU"
#define kParamChannelULabel "U Channel"
#define kParamChannelUHint "Input U channel from UV."

#define kParamChannelUChoice kParamChannelU "Choice"

#define kParamChannelV "channelV"
#define kParamChannelVLabel "V Channel"
#define kParamChannelVHint "Input V channel from UV."

#define kParamChannelVChoice kParamChannelV "Choice"

#define kParamChannelA "channelA"
#define kParamChannelALabel "Alpha Channel"
#define kParamChannelAHint "Input Alpha channel from UV. The Output alpha is set to this value. If \"Unpremult UV\" is checked, the UV values are divided by alpha."

#define kParamChannelAChoice kParamChannelA "Choice"

#define kParamChannelUnpremultUV "unpremultUV"
#define kParamChannelUnpremultUVLabel "Unpremult UV"
#define kParamChannelUnpremultUVHint "Unpremult UV by Alpha from UV. Check if UV values look small for small values of Alpha (3D software sometimes write premultiplied UV values)."

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
#define kParamWrapULabel "U Wrap Mode"
#define kParamWrapUHint "Wrap mode for U coordinate."

#define kParamWrapV "wrapV"
#define kParamWrapVLabel "V Wrap Mode"
#define kParamWrapVHint "Wrap mode for V coordinate."

#define kParamWrapOptionClamp "Clamp"
#define kParamWrapOptionClampHint "Texture edges are black (if blackOutside is checked) or stretched indefinitely."
#define kParamWrapOptionRepeat "Repeat"
#define kParamWrapOptionRepeatHint "Texture is repeated."
#define kParamWrapOptionMirror "Mirror"
#define kParamWrapOptionMirrorHint "Texture is mirrored alternatively."

enum WrapEnum
{
    eWrapClamp = 0,
    eWrapRepeat,
    eWrapMirror,
};


#define kParamUVOffset "uvOffset"
#define kParamUVOffsetLabel "UV Offset"
#define kParamUVOffsetHint "Offset to apply to the U and V channel (useful if these were stored in a file that cannot handle negative numbers)"

#define kParamUVScale "uvScale"
#define kParamUVScaleLabel "UV Scale"
#define kParamUVScaleHint "Scale factor to apply to the U and V channel (useful if these were stored in a file that can only store integer values)"

#define kParamDistortionModel "model"
#define kParamDistortionModelLabel "Model"
#define kParamDistortionModelHint "Choice of the distortion model, i.e. the function that goes from distorted to undistorted image coordinates."
#define kParamDistortionModelOptionNuke "Nuke"
#define kParamDistortionModelOptionNukeHint "The model used in Nuke's LensDistortion plugin."
#define kParamDistortionModelOptionPFBarrel "PFBarrel"
#define kParamDistortionModelOptionPFBarrelHint "The PFBarrel model used in PFTrack by PixelFarm."
#define kParamDistortionModelOption3DEClassic "3DE Classic"
#define kParamDistortionModelOption3DEClassicHint "Degree-2 anamorphic and degree-4 radial mixed model used in 3DEqualizer by Science-D-Visions."
#define kParamDistortionModelOption3DEStandard "3DE Radial Standard Degree 4"
#define kParamDistortionModelOption3DEStandardHint "Radial lens distortion model, which compensates for decentered lenses (and beam splitter artefacts in stereo rigs) used in 3DEqualizer by Science-D-Visions."


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
 */

enum DistortionModelEnum
{
    eDistortionModelNuke,
    eDistortionModelPFBarrel,
    eDistortionModel3DEClassic,
    //eDistortionModel3DEAnamorphic6,
    //eDistortionModel3DEFishEye8,
    eDistortionModel3DEStandard,
    //eDistortionModel3DERadialDecenteredCylindric4,
    //eDistortionModel3DEAnamorphic4,
};


#define kParamDistortionDirection "direction"
#define kParamDistortionDirectionLabel "Direction"
#define kParamDistortionDirectionHint "Should the output corrspond to applying or to removing distortion."
#define kParamDistortionDirectionOptionDistort "Distort"
#define kParamDistortionDirectionOptionDistortHint "The output corresponds to applying distortion."
#define kParamDistortionDirectionOptionUndistort "Undistort"
#define kParamDistortionDirectionOptionUndistortHint "The output corresponds to removing distortion."

enum DirectionEnum {
    eDirectionDistort,
    eDirectionUndistort,
};

#define kParamDistortionOutputMode "outputMode"
#define kParamDistortionOutputModeLabel "Output Mode"
#define kParamDistortionOutputModeHint "Choice of the output, which may be either a distorted/undistorted image, or a distortion/undistortion STMap."
#define kParamDistortionOutputModeOptionImage "Image"
#define kParamDistortionOutputModeOptionImageHint "The output is the distorted/undistorted Source."
#define kParamDistortionOutputModeOptionSTMap "STMap"
#define kParamDistortionOutputModeOptionSTMapHint "The output is a distortion/undistortion STMap. It is recommended to insert a FrameHold node at the Source input so that the STMap is computed only once if the parameters are not animated."

enum OutputModeEnum {
    eOutputModeImage,
    eOutputModeSTMap,
};

#define kParamK1 "k1"
#define kParamK1Label "K1"
#define kParamK1Hint "First radial distortion coefficient (coefficient for r^2)."

#define kParamK2 "k2"
#define kParamK2Label "K2"
#define kParamK2Hint "Second radial distortion coefficient (coefficient for r^4)."

#if 0
#define kParamK3 "k3"
#define kParamK3Label "K3"
#define kParamK3Hint "Third radial distortion coefficient (coefficient for r^6)."

#define kParamP1 "p1"
#define kParamP1Label "P1"
#define kParamP1Hint "First tangential distortion coefficient."

#define kParamP2 "p2"
#define kParamP2Label "P2"
#define kParamP2Hint "Second tangential distortion coefficient."
#endif

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint "Offset of the distortion center from the image center."

#define kParamSqueeze "anamorphicSqueeze"
#define kParamSqueezeLabel "Squeeze"
#define kParamSqueezeHint "Anamorphic squeeze (only for anamorphic lens)."

#define kParamAsymmetric "asymmetricDistortion"
#define kParamAsymmetricLabel "Asymmetric"
#define kParamAsymmetricHint "Asymmetric distortion (only for anamorphic lens)."

#define kParamPFFile "pfFile"
#define kParamPFFileLabel "File"
#define kParamPFFileHint "The location of the PFBarrel .pfb file to use. Keyframes are set if present in the file."

#define kParamPFFileReload "pfReload"
#define kParamPFFileReloadLabel "Reload"
#define kParamPFFileReloadHint "Click to reread the PFBarrel file"

#define kParamPFC3 "pfC3"
#define kParamPFC3Label "C3"
#define kParamPFC3Hint "Low order radial distortion coefficient."

#define kParamPFC5 "pfC5"
#define kParamPFC5Label "C5"
#define kParamPFC5Hint "Low order radial distortion coefficient."

#define kParamPFSqueeze "pfSqueeze"
#define kParamPFSqueezeLabel "Squeeze"
#define kParamPFSqueezeHint "Anamorphic squeeze (only for anamorphic lens)."

#define kParamPFP "pfP"
#define kParamPFPLabel "Center"
#define kParamPFPHint "The distortion center of the lens (specified as a factor rather than a pixel value)"

// 3D Equalizer 4

//Double_knob(callback,&_xa_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_xa_unit","fov left [unit coord]");
#define kParam3DE4_xa_fov_unit "tde4_field_of_view_xa_unit"
#define kParam3DE4_xa_fov_unitLabel "fov left [unit coord]"

//Double_knob(callback,&_ya_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_ya_unit","fov bottom [unit coord]");
#define kParam3DE4_ya_fov_unit "tde4_field_of_view_ya_unit"
#define kParam3DE4_ya_fov_unitLabel "fov bottom [unit coord]"

//Double_knob(callback,&_xb_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_xb_unit","fov right [unit coord]");
#define kParam3DE4_xb_fov_unit "tde4_field_of_view_xb_unit"
#define kParam3DE4_xb_fov_unitLabel "fov right [unit coord]"

//Double_knob(callback,&_yb_fov_unit,DD::Image::IRange(0.0,1.0),"field_of_view_yb_unit","fov top [unit coord]");
#define kParam3DE4_yb_fov_unit "tde4_field_of_view_yb_unit"
#define kParam3DE4_yb_fov_unitLabel "fov top [unit coord]"

// First the seven built-in parameters, in this order.

//fl_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(0.5,50.0),"tde4_focal_length_cm","tde4 focal length [cm]");
#define kParam3DE4_focal_length_cm "tde4_focal_length_cm"
#define kParam3DE4_focal_length_cmLabel "tde4 focal length [cm]"

//fd_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(10.0,1000.0),"tde4_custom_focus_distance_cm","tde4 focus distance [cm]");
#define kParam3DE4_custom_focus_distance_cm "tde4_custom_focus_distance_cm"
#define kParam3DE4_custom_focus_distance_cmLabel "tde4 focus distance [cm]"

//w_fb_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(.1,10.0),"tde4_filmback_width_cm","tde4 filmback width [cm]");
#define kParam3DE4_filmback_width_cm "tde4_filmback_width_cm"
#define kParam3DE4_filmback_width_cmLabel "tde4 filmback width [cm]"

//h_fb_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(.1,10.0),"tde4_filmback_height_cm","tde4 filmback height [cm]");
#define kParam3DE4_filmback_height_cm "tde4_filmback_height_cm"
#define kParam3DE4_filmback_height_cmLabel "tde4 filmback height [cm]"

//x_lco_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(-5.0,5.0),"tde4_lens_center_offset_x_cm","tde4 lens center offset x [cm]");
#define kParam3DE4_lens_center_offset_x_cm "tde4_lens_center_offset_x_cm"
#define kParam3DE4_lens_center_offset_x_cmLabel "tde4 lens center offset x [cm]"

//y_lco_cm: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(-5.0,5.0),"tde4_lens_center_offset_y_cm","tde4 lens center offset y [cm]");
#define kParam3DE4_lens_center_offset_y_cm "tde4_lens_center_offset_y_cm"
#define kParam3DE4_lens_center_offset_y_cmLabel "tde4 lens center offset y [cm]"

//pa: Double_knob(callback,&_values_double[i_par_double++],DD::Image::IRange(0.25,4.0),"tde4_pixel_aspect","tde4 pixel aspect");
#define kParam3DE4_pixel_aspect "tde4_pixel_aspect"
#define kParam3DE4_pixel_aspectLabel "tde4 pixel aspect"


// 3DE_Classic_LD_Model
//"Distortion", -0.5 .. 0.5
#define kParam3DEDistortion "tde4_Distortion"
#define kParam3DEDistortionLabel "Distortion"
//"Anamorphic Squeeze", 0.25 ... 4.
#define kParam3DEAnamorphicSqueeze "tde4_Anamorphic_Squeeze"
#define kParam3DEAnamorphicSqueezeLabel "Anamorphic Squeeze"

//"Curvature X", -0.5 .. 0.5
#define kParam3DECurvatureX "tde4_Curvature_X"
#define kParam3DECurvatureXLabel "Curvature X"

//"Curvature Y", -0.5 .. 0.5
#define kParam3DECurvatureY "tde4_Curvature_Y"
#define kParam3DECurvatureYLabel "Curvature Y"

//"Quartic Distortion" -0.5 .. 0.5
#define kParam3DEQuarticDistortion "tde4_Quartic_Distortion"
#define kParam3DEQuarticDistortionLabel "Quartic Distortion"

// 3DE4_Radial_Standard_Degree_4
#define kParam3DEDistortionDegree2 "tde4_Distortion_Degree_2"
#define kParam3DEDistortionDegree2Label "Distortion - Degree 2"
#define kParam3DEUDegree2 "tde4_U_Degree_2"
#define kParam3DEUDegree2Label "U - Degree 2"
#define kParam3DEVDegree2 "tde4_V_Degree_2"
#define kParam3DEVDegree2Label "V - Degree 2"
#define kParam3DEQuarticDistortionDegree4 "tde4_Quartic_Distortion_Degree_4"
#define kParam3DEQuarticDistortionDegree4Label "Quartic Distortion - Degree 4"
#define kParam3DEUDegree4 "tde4_U_Degree_4"
#define kParam3DEUDegree4Label "U - Degree 4"
#define kParam3DEVDegree4 "tde4_V_Degree_4"
#define kParam3DEVDegree4Label "V - Degree 4"
#define kParam3DEPhiCylindricDirection "tde4_Phi_Cylindric_Direction"
#define kParam3DEPhiCylindricDirectionLabel "Phi - Cylindric Direction"
#define kParam3DEBCylindricBending "tde4_B_Cylindric_Bending"
#define kParam3DEBCylindricBendingLabel "B - Cylindric Bending"

// a generic distortion model abstract class (distortion parameters are added by the derived class)
class DistortionModel
{
protected:
    DistortionModel() {};

public:
    virtual ~DistortionModel() {};

private:  // noncopyable
    DistortionModel( const DistortionModel& );
    DistortionModel& operator=( const DistortionModel& );

public:
    // function used to distort a point or undistort an image
    virtual void distort(double xu, double yu, double* xd, double *yd) const = 0;

    // function used to undistort a point or distort an image
    virtual void undistort(double xd, double yd, double* xu, double *yu) const = 0;
};

// parameters for Newton method:
#define EPSJAC 1.e-3 // epsilon for Jacobian calculation
#define EPSCONV 1.e-4 // epsilon for convergence test

// a distortion model class where ony the undistort function is given, and distort is solved by Newton
class DistortionModelUndistort
: public DistortionModel
{
protected:
    DistortionModelUndistort() {};

    virtual ~DistortionModelUndistort() {};

private:
    // function used to distort a point or undistort an image
    virtual void distort(const double xu,
                         const double yu,
                         double* xd,
                         double *yd) const OVERRIDE FINAL
    {
        // build initial guess
        double x = xu;
        double y = yu;

        // always converges in a couple of iterations
        for (int iter= 0; iter< 10; iter++) {
            // calculate the function gradient at the current guess

            // TODO: analytic derivatives
            double x00, y00, x10, y10, x01, y01;
            undistort(x, y, &x00, &y00);
            undistort(x + EPSJAC, y, &x10, &y10);
            undistort(x, y + EPSJAC, &x01, &y01);

            // perform newton iteration
            x00 -= xu;
            y00 -= yu;
            x10 -= xu;
            y10 -= yu;
            x01 -= xu;
            y01 -= yu;

            x10 -= x00;
            y10 -= y00;
            x01 -= x00;
            y01 -= y00;

            // approximate using finite differences
            const double dx = std::sqrt(x10 * x10 + y10 * y10) / EPSJAC;
            const double dy = std::sqrt(x01 * x01 + y01 * y01) / EPSJAC;

            if (dx < DBL_EPSILON || dy < DBL_EPSILON) { // was dx == 0. || dy == 0.
                break;
            }

            // make a step towards the root
            const double x1 = x - x00 / dx;
            const double y1 = y - y00 / dy;

            x -= x1;
            y -= y1;

            const double dist= x * x + y * y;

            x = x1;
            y = y1;

            //printf("%d : %g,%g: dist= %g\n",iter,x,y,dist);

            // converged?
            if (dist < EPSCONV) {
                break;
            }
        }

        // default
        *xd = x;
        *yd = y;
    }

    // function used to undistort a point or distort an image
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE = 0;
};

class DistortionModelNuke
: public DistortionModelUndistort
{
public:
    DistortionModelNuke(const OfxRectI& srcRoDPixel,
                        double par,
                        double k1,
                        double k2,
                        double cx,
                        double cy,
                        double squeeze,
                        double ax,
                        double ay)
    : _par(par)
    , _k1(k1)
    , _k2(k2)
    , _cx(cx)
    , _cy(cy)
    , _squeeze(squeeze)
    , _ax(ax)
    , _ay(ay)
    {
        double fx = (srcRoDPixel.x2 - srcRoDPixel.x1) / 2.;
        double fy = (srcRoDPixel.y2 - srcRoDPixel.y1) / 2.;
        _f = std::max(fx, fy); // TODO: distortion scaling param for LensDistortion?
        _xSrcCenter = (srcRoDPixel.x1 + srcRoDPixel.x2) / 2.;
        _ySrcCenter = (srcRoDPixel.y1 + srcRoDPixel.y2) / 2.;
    }

    virtual ~DistortionModelNuke() {};

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL
    {
        double xdn = _par * (xd - _xSrcCenter) / _f;
        double ydn = (yd - _ySrcCenter) / _f;
        double sx, sy;
        undistort_nuke(xdn, ydn,
                       _k1, _k2, _cx, _cy, _squeeze, _ax, _ay,
                       &sx, &sy);
        sx /= _par;
        sx *= _f;
        sx += _xSrcCenter;
        sy *= _f;
        sy += _ySrcCenter;

        *xu = sx;
        *yu = sy;
    }

    // Nuke's distortion function, reverse engineered from the resulting images on a checkerboard (and a little science, too)
    // this function undistorts positions, but is also used to distort the image.
    // Similar to the function distortNuke in Obq_LensDistortion.h
    static inline void
    undistort_nuke(double xd,
                   double yd,            // distorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
                   double k1,
                   double k2,            // radial distortion
                   double cx,
                   double cy,            // distortion center, (0,0) at center of image
                   double squeeze, // anamorphic squeeze
                   double ax,
                   double ay,            // asymmetric distortion
                   double *xu,
                   double *yu)             // distorted position in normalized coordinates
    {
        // nuke?
        // k1 = radial distortion 1
        // k2 = radial distortion 2
        // squeeze = anamorphic squeeze
        // p1 = asymmetric distortion x
        // p2 = asymmetric distortion y
        double x = (xd - cx);
        double y = (yd - cy);
        double x2 = x * x, y2 = y * y;
        double r2 = x2 + y2;
        double k2r2pk1 = k2 * r2 + k1;
        //double kry = 1 + ((k2r2pk1 + ay)*x2 + k2r2pk1*y2);
        double kry = 1 + (k2r2pk1 * r2 + ay * x2);

        *yu = (y / kry) + cy;
        //double krx = 1 + (k2r2pk1*x2 + (k2r2pk1 + ax)*y2)/squeeze;
        double krx = 1 + (k2r2pk1 * r2 + ax * y2) / squeeze;
        *xu = (x / krx) + cx;
    }

    double _par;
    double _f;
    double _xSrcCenter;
    double _ySrcCenter;
    double _k1;
    double _k2;
    double _cx;
    double _cy;
    double _squeeze;
    double _ax;
    double _ay;
};


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

    if (std::fgets(buf, sizeof(buf), f_)) {
        rv= buf;
        rv= rv.erase(rv.length()-1);
    } else {
        error_= "Parse error";
    }

    return rv;
}



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

class DistortionModelPFBarrel
: public DistortionModelUndistort
{
public:
    DistortionModelPFBarrel(const OfxRectI& srcRoDPixel,
                            const OfxPointD& renderScale,
                            double c3,
                            double c5,
                            double xp,
                            double yp,
                            double squeeze)
    : _rs(renderScale)
    , _c3(c3)
    , _c5(c5)
    , _xp(xp)
    , _yp(yp)
    , _squeeze(squeeze)
    {
        /*
        double fx = (srcRoDPixel.x2 - srcRoDPixel.x1) / 2.;
        double fy = (srcRoDPixel.y2 - srcRoDPixel.y1) / 2.;
        _f = std::max(fx, fy); // TODO: distortion scaling param for LensDistortion?
        _xSrcCenter = (srcRoDPixel.x1 + srcRoDPixel.x2) / 2.;
        _ySrcCenter = (srcRoDPixel.y1 + srcRoDPixel.y2) / 2.;
         */
        _fw = srcRoDPixel.x2 - srcRoDPixel.x1;
        _fh = srcRoDPixel.y2 - srcRoDPixel.y1;
        _normx = std::sqrt(2.0/(_fw * _fw + _fh * _fh));
    }

    virtual ~DistortionModelPFBarrel() {};

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL
    {
        // PFBarrel model seems to apply to the corner of the corresponding full-res pixel
        // at least that's what the official PFBarrel Nuke plugin does
        xd -= 0.5 * _rs.x;
        yd -= 0.5 * _rs.y;

        double centx = _xp * _fw * _normx;
        double x = xd * _normx;
        // remove anamorphic squeeze
        double centy = _yp * _fh * _normx / _squeeze;
        double y = yd * _normx / _squeeze;

        // distort
        const double px = x - centx;
        const double py = y - centy;

        const double px2 = px * px;
        const double py2 = py * py;
        //const double r = std::sqrt(px2 + py2);
        const double r2 = px2 + py2;
        //#ifdef THREE_POWER
        //const double dr_r= r2*r*(C3C5.x+r2*C3C5.y);
        //#else
        const double dr_r= r2 * (_c3+ r2 * _c5);
        //#endif

        // re-apply squeeze and remove normalization
        x += px * dr_r;
        x /= _normx;
        y += py * dr_r;
        y *= _squeeze / _normx;

        x += 0.5 * _rs.x;
        y += 0.5 * _rs.y;

        *xu = x;
        *yu = y;
    }


    OfxPointD _rs;
    double _c3;
    double _c5;
    double _xp;
    double _yp;
    double _squeeze;
    double _normx;
    double _fw, _fh;
};



/////////////////////// 3DEqualizer


/// this base class handles the 4 fov parameters & the seven built-in parameters
class DistortionModel3DEBase
: public DistortionModelUndistort
{
protected:
    DistortionModel3DEBase(const OfxRectI& srcRoDPixel,
                           const OfxPointD& renderScale,
                           double xa_fov_unit,
                           double ya_fov_unit,
                           double xb_fov_unit,
                           double yb_fov_unit,
                           double fl_cm,
                           double fd_cm,
                           double w_fb_cm,
                           double h_fb_cm,
                           double x_lco_cm,
                           double y_lco_cm,
                           double pa)
    : _srcRoDPixel(srcRoDPixel)
    , _rs(renderScale)
    , _w(srcRoDPixel.x2 - srcRoDPixel.x1)
    , _h(srcRoDPixel.y2 - srcRoDPixel.y1)
    , _xa_fov_unit(xa_fov_unit)
    , _ya_fov_unit(ya_fov_unit)
    , _xb_fov_unit(xb_fov_unit)
    , _yb_fov_unit(yb_fov_unit)
    , _xd_fov_unit(_xb_fov_unit - _xa_fov_unit)
    , _yd_fov_unit(_yb_fov_unit - _ya_fov_unit)
    , _fl_cm(fl_cm)
    , _fd_cm(fd_cm)
    , _w_fb_cm(w_fb_cm)
    , _h_fb_cm(h_fb_cm)
    , _x_lco_cm(x_lco_cm)
    , _y_lco_cm(y_lco_cm)
    , _pa(pa)
    {
        _r_fb_cm = std::sqrt(w_fb_cm * w_fb_cm + h_fb_cm * h_fb_cm) / 2.0;
    }

    virtual ~DistortionModel3DEBase() {};

    // the following must be implemented by each model, and corresponds to operator () in ldpk
    // Remove distortion. p is a point in diagonally normalized coordinates.
    virtual void undistort_dn(double xd, double yd, double* xu, double *yu) const = 0;

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL
    {
        OfxPointD p_pix = {xd, yd};
        OfxPointD p_dn;
        map_pix_to_dn(p_pix, &p_dn);
        OfxPointD p_dn_u;
        undistort_dn(p_dn.x, p_dn.y, &p_dn_u.x, &p_dn_u.y);
        map_dn_to_pix(p_dn_u, &p_pix);
        *xu = p_pix.x;
        *yu = p_pix.y;
    }

private:
    void
    map_pix_to_dn(const OfxPointD& p_pix, OfxPointD* p_dn) const
    {
        OfxPointD p_unit;
        map_pix_to_unit(p_pix, &p_unit);
        map_unit_to_dn(p_unit, p_dn);
    }

    // The result already contains the (half,half) shift.
    void
    map_dn_to_pix(const OfxPointD& p_dn, OfxPointD* p_pix) const
    {
        OfxPointD p_unit;
        map_dn_to_unit(p_dn, &p_unit);
        map_unit_to_pix(p_unit, p_pix);
    }

    void
    map_unit_to_dn(const OfxPointD& p_unit, OfxPointD* p_dn) const
    {
        double p_cm_x = (p_unit.x - 1.0/2.0) * _w_fb_cm - _x_lco_cm;
        double p_cm_y = (p_unit.y - 1.0/2.0) * _h_fb_cm - _y_lco_cm;
        p_dn->x = p_cm_x / _r_fb_cm;
        p_dn->y = p_cm_y / _r_fb_cm;
    }

    void
    map_dn_to_unit(const OfxPointD& p_dn, OfxPointD* p_unit) const
    {
        double p_cm_x = p_dn.x * _r_fb_cm + _w_fb_cm / 2 + _x_lco_cm;
        double p_cm_y = p_dn.y * _r_fb_cm + _h_fb_cm / 2 + _y_lco_cm;
        p_unit->x = p_cm_x / _w_fb_cm;
        p_unit->y = p_cm_y / _h_fb_cm;
    }

#if 0
    // the following twho funcs are for when 0,0 is the center of the bottomleft pixel (see nuke plugin)
    void
    map_pix_to_unit(const OfxPointI& p_pix, OfxPointD* p_unit) const
    {
        // We construct (x_s,y_s) in a way, so that the image area is mapped to the unit interval [0,1]^2,
        // which is required by our 3DE4 lens distortion plugin class. Nuke's coordinates are pixel based,
        // (0,0) is the left lower corner of the left lower pixel, while (1,1) is the right upper corner
        // of that pixel. The center of any pixel (ix,iy) is (ix+0.5,iy+0.5), so we add 0.5 here.
        double x_s = (0.5 + p_pix.x) / _w;
        double y_s = (0.5 + p_pix.y) / _h;
        p_unit->x = map_in_fov_x(x_s);
        p_unit->y = map_in_fov_y(y_s);
    }

    void
    map_unit_to_pix(const OfxPointD& p_unit, OfxPointD* p_pix) const
    {
        // The result already contains the (half,half) shift. Reformulate in Nuke's coordinates. Weave "out" 3DE4's field of view.
        p_pix->x = map_out_fov_x(p_unit.x) * _w;
        p_pix->y = map_out_fov_y(p_unit.y) * _h;
    }
#endif

    void
    map_pix_to_unit(const OfxPointD& p_pix, OfxPointD* p_unit) const
    {
        double x_s = p_pix.x / _w;
        double y_s = p_pix.y / _h;
        p_unit->x = map_in_fov_x(x_s);
        p_unit->y = map_in_fov_y(y_s);
    }

    void
    map_unit_to_pix(const OfxPointD& p_unit, OfxPointD* p_pix) const
    {
        // The result already contains the (half,half) shift. Reformulate in Nuke's coordinates. Weave "out" 3DE4's field of view.
        p_pix->x = map_out_fov_x(p_unit.x) * _w;
        p_pix->y = map_out_fov_y(p_unit.y) * _h;
    }

    // Map x-coordinate from unit cordinates to fov coordinates.
    double
    map_in_fov_x(double x_unit) const
    {
        return  (x_unit - _xa_fov_unit) / _xd_fov_unit;
    }

    // Map y-coordinate from unit cordinates to fov coordinates.
    double
    map_in_fov_y(double y_unit) const
    {
        return  (y_unit - _ya_fov_unit) / _yd_fov_unit;
    }

    // Map x-coordinate from fov cordinates to unit coordinates.
    double
    map_out_fov_x(double x_fov) const
    {
        return x_fov * _xd_fov_unit + _xa_fov_unit;
    }

    // Map y-coordinate from fov cordinates to unit coordinates.
    double
    map_out_fov_y(double y_fov) const
    {
        return y_fov * _yd_fov_unit + _ya_fov_unit;
    }

protected:

    OfxRectI _srcRoDPixel;
    OfxPointD _rs;
        double _w;
        double _h;
    double _xa_fov_unit;
    double _ya_fov_unit;
    double _xb_fov_unit;
    double _yb_fov_unit;
    double _xd_fov_unit;
    double _yd_fov_unit;
    double _fl_cm;
    double _fd_cm;
    double _w_fb_cm;
    double _h_fb_cm;
    double _x_lco_cm;
    double _y_lco_cm;
    double _pa;
    double _r_fb_cm;
};

/// this class handles the Degree-2 anamorphic and degree-4 radial mixed model
class DistortionModel3DEClassic
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEClassic(const OfxRectI& srcRoDPixel,
                              const OfxPointD& renderScale,
                              double xa_fov_unit,
                              double ya_fov_unit,
                              double xb_fov_unit,
                              double yb_fov_unit,
                              double fl_cm,
                              double fd_cm,
                              double w_fb_cm,
                              double h_fb_cm,
                              double x_lco_cm,
                              double y_lco_cm,
                              double pa,
                              double ld,
                              double sq,
                              double cx,
                              double cy,
                              double qu)
    : DistortionModel3DEBase(srcRoDPixel, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
    , _ld(ld)
    , _sq(sq)
    , _cx(cx)
    , _cy(cy)
    , _qu(qu)
    , _cxx(_ld / _sq)
    , _cxy( (_ld + _cx) / _sq )
    , _cyx(_ld + _cy)
    , _cyy(_ld)
    , _cxxx(_qu / _sq)
    , _cxxy(2.0 * _qu / _sq)
    , _cxyy(_qu / _sq)
    , _cyxx(_qu)
    , _cyyx(2.0 * _qu)
    , _cyyy(_qu)
    {
    }

    virtual ~DistortionModel3DEClassic() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const
    {
        double p0_2 = xd * xd;
        double p1_2 = yd * yd;
        double p0_4 = p0_2 * p0_2;
        double p1_4 = p1_2 * p1_2;
        double p01_2 = p0_2 * p1_2;

        *xu = xd * (1 + _cxx * p0_2 + _cxy * p1_2 + _cxxx * p0_4 + _cxxy * p01_2 + _cxyy * p1_4);
        *yu = yd * (1 + _cyx * p0_2 + _cyy * p1_2 + _cyxx * p0_4 + _cyyx * p01_2 + _cyyy * p1_4);
    }

private:
    double _ld;
    double _sq;
    double _cx;
    double _cy;
    double _qu;
    double _cxx;
    double _cxy;
    double _cyx;
    double _cyy;
    double _cxxx;
    double _cxxy;
    double _cxyy;
    double _cyxx;
    double _cyyx;
    double _cyyy;
};

/// this class handles the Degree-2 anamorphic and degree-4 radial mixed model
class DistortionModel3DEStandard
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEStandard(const OfxRectI& srcRoDPixel,
                               const OfxPointD& renderScale,
                               double xa_fov_unit,
                               double ya_fov_unit,
                               double xb_fov_unit,
                               double yb_fov_unit,
                               double fl_cm,
                               double fd_cm,
                               double w_fb_cm,
                               double h_fb_cm,
                               double x_lco_cm,
                               double y_lco_cm,
                               double pa,
                               double c2,
                               double u1,
                               double v1,
                               double c4,
                               double u3,
                               double v3,
                               double phi,
                               double b)
    : DistortionModel3DEBase(srcRoDPixel, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
    , _c2(c2)
    , _u1(u1)
    , _v1(v1)
    , _c4(c4)
    , _u3(u3)
    , _v3(v3)
    , _phi(phi)
    , _b(b)
    {
    }

    virtual ~DistortionModel3DEStandard() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const
    {
        // _radial.eval(
        double x_dn,y_dn;
        double x = xd;
        double y = yd;
        double x2 = x * x;
        double y2 = y * y;
        double xy = x * y;
        double r2 = x2 + y2;
        double r4 = r2 * r2;
        x_dn = x * (1.0 + _c2 * r2 + _c4 * r4) + (r2 + 2.0 * x2) * (_u1 + _u3 * r2) + 2.0 * xy * (_v1 + _v3 * r2);
        y_dn = y * (1.0 + _c2 * r2 + _c4 * r4) + (r2 + 2.0 * y2) * (_v1 + _v3 * r2) + 2.0 * xy * (_u1 + _u3 * r2);

        // _cylindric.eval(
        // see cylindric_extender_2
        //calc_m()
        double q = std::sqrt(1.0 + _b);
        double c = std::cos(_phi * M_PI / 180.0);
        double s = std::sin(_phi * M_PI / 180.0);
        //mat2_type para = tensq(vec2_type(cos(_phi * M_PI / 180.0),sin(_phi * M_PI / 180.0)));
        //m = _b * para + mat2_type(1.0);
        // m = [[mxx, mxy],[myx,myy]] (m is symmetric)
        double mxx = c*c*q + s*s/q;
        double mxy = (q - 1.0/q)*c*s;
        double myy = c*c/q + s*s*q;
        //(xu,yu) = m * (x_dn, y_dn);
        *xu = mxx * x_dn + mxy * y_dn;
        *yu = mxy * x_dn + myy * y_dn;
    }

private:
    double _c2;
    double _u1;
    double _v1;
    double _c4;
    double _u3;
    double _v3;
    double _phi;
    double _b;
};




static bool gIsMultiPlane;
struct InputPlaneChannel
{
    Image* img;
    int channelIndex;
    bool fillZero;

    InputPlaneChannel() : img(0), channelIndex(-1), fillZero(true) {}
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
    OfxRectI _srcRoDPixel;
    std::vector<InputPlaneChannel> _planeChannels;
    bool _unpremultUV;
    double _uOffset;
    double _vOffset;
    double _uScale;
    double _vScale;
    WrapEnum _uWrap;
    WrapEnum _vWrap;
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
        , _srcImg(0)
        , _maskImg(0)
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
        _srcRoDPixel.x1 = _srcRoDPixel.y1 = 0.;
        _srcRoDPixel.x2 = _srcRoDPixel.y2 = 1.;
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
                   const OfxRectI& srcRoDPixel,
                   const std::vector<InputPlaneChannel>& planeChannels,
                   bool unpremultUV,
                   double uOffset,
                   double vOffset,
                   double uScale,
                   double vScale,
                   WrapEnum uWrap,
                   WrapEnum vWrap,
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
        _srcRoDPixel = srcRoDPixel;
        _planeChannels = planeChannels;
        _unpremultUV = unpremultUV;
        _uOffset = uOffset;
        _vOffset = vOffset;
        _uScale = uScale;
        _vScale = vScale;
        _uWrap = uWrap;
        _vWrap = vWrap;
        _distortionModel = distortionModel;
        _direction = direction;
        _outputMode = outputMode;
        _blackOutside = blackOutside;
        _mix = mix;
    }

private:
};



#if 0
// see https://github.com/Itseez/opencv/blob/master/modules/imgproc/src/undistort.cpp
static inline void
distort_opencv(double xu,
               double yu,            // undistorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
               double k1,
               double k2,
               double k3,
               double p1,
               double p2,
               double cx,
               double cy,
               double squeeze,
               double *xd,
               double *yd)      // distorted position in normalized coordinates
{
    // opencv
    const double k4 = 0.;
    const double k5 = 0.;
    const double k6 = 0.;
    const double s1 = 0.;
    const double s2 = 0.;
    const double s3 = 0.;
    const double s4 = 0.;
    double x = (xu - cx) * squeeze;
    double y = yu - cy;
    double x2 = x * x, y2 = y * y;
    double r2 = x2 + y2;
    double _2xy = 2 * x * y;
    double kr = (1 + ( (k3 * r2 + k2) * r2 + k1 ) * r2) / (1 + ( (k6 * r2 + k5) * r2 + k4 ) * r2);

    *xd = ( (x * kr + p1 * _2xy + p2 * (r2 + 2 * x2) + s1 * r2 + s2 * r2 * r2) ) / squeeze + cx;
    *yd = (y * kr + p1 * (r2 + 2 * y2) + p2 * _2xy + s3 * r2 + s4 * r2 * r2) + cy;
}

#endif

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

    // requires for STMap and LensDistortion
    //if (plugin == eDistortionPluginSTMap || _outputMode == eOutputModeSTMap) {
    int srcx1 = _srcRoDPixel.x1;
    int srcx2 = _srcRoDPixel.x2;
    int srcy1 = _srcRoDPixel.y1;
    int srcy2 = _srcRoDPixel.y2;
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
                if (_direction == eDirectionDistort) {
                    _distortionModel->undistort(x + 0.5, y + 0.5, &sx, &sy);
                } else {
                    _distortionModel->distort(x + 0.5, y + 0.5, &sx, &sy);
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
                double transformedx = H.a * sx + H.b * sy + H.c;
                double transformedy = H.d * sx + H.e * sy + H.f;
                double transformedz = H.g * sx + H.h * sy + H.i;
                if (transformedz == 0) {
                    sx = sy = std::numeric_limits<double>::infinity();
                } else {
                    sx = transformedx / transformedz;
                    sy = transformedy / transformedz;
                    if (filter != eFilterImpulse) {
                        Jxx = (H.a * transformedz - transformedx * H.g) / (transformedz * transformedz);
                        Jxy = (H.b * transformedz - transformedx * H.h) / (transformedz * transformedz);
                        Jyx = (H.d * transformedz - transformedy * H.g) / (transformedz * transformedz);
                        Jyy = (H.e * transformedz - transformedy * H.h) / (transformedz * transformedz);
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
                     DistortionPluginEnum plugin)
        : MultiPlane::MultiPlaneEffect(handle)
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
        , _filter(NULL)
        , _clamp(NULL)
        , _blackOutside(NULL)
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
            if (gIsMultiPlane) {
                fetchDynamicMultiplaneChoiceParameter(kParamChannelU, _uvClip);
                fetchDynamicMultiplaneChoiceParameter(kParamChannelV, _uvClip);
                fetchDynamicMultiplaneChoiceParameter(kParamChannelA, _uvClip);
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
            _ld = fetchDoubleParam(kParam3DEDistortion);
            _sq = fetchDoubleParam(kParam3DEAnamorphicSqueeze);
            _cx = fetchDoubleParam(kParam3DECurvatureX);
            _cy = fetchDoubleParam(kParam3DECurvatureY);
            _qu = fetchDoubleParam(kParam3DEQuarticDistortion);
            _c2 = fetchDoubleParam(kParam3DEDistortionDegree2);
            _u1 = fetchDoubleParam(kParam3DEUDegree2);
            _v1 = fetchDoubleParam(kParam3DEVDegree2);
            _c4 = fetchDoubleParam(kParam3DEQuarticDistortionDegree4);
            _u3 = fetchDoubleParam(kParam3DEUDegree4);
            _v3 = fetchDoubleParam(kParam3DEVDegree4);
            _phi = fetchDoubleParam(kParam3DEPhiCylindricDirection);
            _b = fetchDoubleParam(kParam3DEBCylindricBending);
            
        }
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);
        assert(_filter && _clamp && _blackOutside);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        updateVisibility();
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NUKE
    virtual void getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
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

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateVisibility();

private:
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
    // Classic model
    DoubleParam* _ld;
    DoubleParam* _sq;
    DoubleParam* _cx;
    DoubleParam* _cy;
    DoubleParam* _qu;
    // Standard model
    DoubleParam* _c2;
    DoubleParam* _u1;
    DoubleParam* _v1;
    DoubleParam* _c4;
    DoubleParam* _u3;
    DoubleParam* _v3;
    DoubleParam* _phi;
    DoubleParam* _b;

    ChoiceParam* _filter;
    BooleanParam* _clamp;
    BooleanParam* _blackOutside;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    DistortionPluginEnum _plugin;
};


void
DistortionPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    //We have to do this because the processing code does not support varying components for uvClip and srcClip
    PixelComponentEnum dstPixelComps = _dstClip->getPixelComponents();

    if (_srcClip) {
        clipPreferences.setClipComponents(*_srcClip, dstPixelComps);
    }
    if (gIsMultiPlane && _uvClip) {
        buildChannelMenus();
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

/* set up and run a processor */
void
DistortionPlugin::setupAndProcess(DistortionProcessorBase &processor,
                                  const RenderArguments &args)
{
    const double time = args.time;

    std::auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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

    std::auto_ptr<const Image> src( ( (outputMode == eOutputModeImage) && _srcClip && _srcClip->isConnected() ) ?
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
        if (gIsMultiPlane) {
            BitDepthEnum srcBitDepth = eBitDepthNone;
            std::map<Clip*, std::map<std::string, Image*> > fetchedPlanes;
            bool isCreatingAlpha;
            for (int i = 0; i < 3; ++i) {
                InputPlaneChannel p;
                p.channelIndex = i;
                p.fillZero = false;
                //if (_uvClip) {
                Clip* clip = 0;
                std::string plane, ofxComp;
                bool ok = getPlaneNeededForParam(time, _uvChannels[i]->getName(), &clip, &plane, &ofxComp, &p.channelIndex, &isCreatingAlpha);
                if (!ok) {
                    setPersistentMessage(Message::eMessageError, "", "Cannot find requested channels in input");
                    throwSuiteStatusException(kOfxStatFailed);
                }

                p.img = 0;
                if (ofxComp == kMultiPlaneParamOutputOption0) {
                    p.fillZero = true;
                } else if (ofxComp == kMultiPlaneParamOutputOption1) {
                    p.fillZero = false;
                } else {
                    std::map<std::string, Image*>& clipPlanes = fetchedPlanes[clip];
                    std::map<std::string, Image*>::iterator foundPlane = clipPlanes.find(plane);
                    if ( foundPlane != clipPlanes.end() ) {
                        p.img = foundPlane->second;
                    } else {
#ifdef OFX_EXTENSIONS_NUKE
                        p.img = clip->fetchImagePlane( time, args.renderView, plane.c_str() );
#else
                        p.img = ( clip && clip->isConnected() ) ? clip->fetchImage(time) : 0;
#endif
                        if (p.img) {
                            clipPlanes.insert( std::make_pair(plane, p.img) );
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
    std::auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
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
        srcTransformMat.a = srcTransform[0];
        srcTransformMat.b = srcTransform[1];
        srcTransformMat.c = srcTransform[2];
        srcTransformMat.d = srcTransform[3];
        srcTransformMat.e = srcTransform[4];
        srcTransformMat.f = srcTransform[5];
        srcTransformMat.g = srcTransform[6];
        srcTransformMat.h = srcTransform[7];
        srcTransformMat.i = srcTransform[8];
        // invert it
        double det = srcTransformMat.determinant();
        if (det != 0.) {
            srcTransformInverse = srcTransformMat.inverse(det);
        } else {
            transformIsIdentity = true; // no transform
        }
    }
#endif
    if (_plugin == eDistortionPluginIDistort) {
        // in IDistort, displacement is given in full-scale pixels
        uScale *= args.renderScale.x;
        vScale *= args.renderScale.y;
    }
    OfxRectI srcRoDPixel = {0, 1, 0, 1};
    if ( _srcClip && _srcClip->isConnected() ) {
        const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
        Coords::toPixelEnclosing(srcRod, args.renderScale, _srcClip->getPixelAspectRatio(), &srcRoDPixel);
    } else {
        // default to Project Size
        OfxRectD srcRoD;
        OfxPointD siz = getProjectSize();
        OfxPointD off = getProjectOffset();
        srcRoD.x1 = off.x;
        srcRoD.x2 = off.x + siz.x;
        srcRoD.y1 = off.y;
        srcRoD.y2 = off.y + siz.y;
        Coords::toPixelEnclosing(srcRoD, args.renderScale, getProjectPixelAspectRatio(), &srcRoDPixel);
    }

    DirectionEnum direction = _direction ? (DirectionEnum)_direction->getValue() : eDirectionDistort;
    std::auto_ptr<DistortionModel> distortionModel;
    if (_plugin == eDistortionPluginLensDistortion) {
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
            distortionModel.reset( new DistortionModelNuke(srcRoDPixel,
                                                           par,
                                                           k1,
                                                           k2,
                                                           cx,
                                                           cy,
                                                           squeeze,
                                                           ax,
                                                           ay) );
            break;
        }
        case eDistortionModelPFBarrel: {
            double par = 1.;
            if (_srcClip) {
                par = _srcClip->getPixelAspectRatio();
            }
            double c3 = _pfC3->getValueAtTime(time);
            double c5 = _pfC5->getValueAtTime(time);
            double xp, yp;
            _pfP->getValueAtTime(time, xp, yp);
            double squeeze = _pfSqueeze->getValueAtTime(time);
            distortionModel.reset( new DistortionModelPFBarrel(srcRoDPixel,
                                                               args.renderScale,
                                                               //par,
                                                               c3,
                                                               c5,
                                                               xp,
                                                               yp,
                                                               squeeze) );
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
            distortionModel.reset( new DistortionModel3DEClassic(srcRoDPixel,
                                                                 args.renderScale,
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
                                                                 qu) );
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
            distortionModel.reset( new DistortionModel3DEStandard(srcRoDPixel,
                                                                  args.renderScale,
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
                                                                  b) );
            break;
        }
        }
    }
    processor.setValues(processR, processG, processB, processA,
                        transformIsIdentity, srcTransformInverse,
                        srcRoDPixel,
                        planeChannels,
                        unpremultUV,
                        uOffset, vOffset,
                        uScale, vScale,
                        uWrap, vWrap,
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
                             double & /*identityTime*/)
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
#warning TODO
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

    if (!_srcClip) {
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

        return false;     // use source RoD
        break;
    }
    } // switch (_plugin)

    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
void
DistortionPlugin::getClipComponents(const ClipComponentsArguments& args,
                                    ClipComponentsSetter& clipComponents)
{
    assert(gIsMultiPlane);

    const double time = args.time;

    /*std::list<std::string> outputComponents = _dstClip->getComponentsPresent();
       std::string ofxPlane,ofxComp;
       getPlaneNeededInOutput(outputComponents, _outputLayer, &ofxPlane, &ofxComp);
       clipComponents.addClipComponents(*_dstClip, ofxComp);*/
    PixelComponentEnum dstPx = _dstClip->getPixelComponents();
    clipComponents.addClipComponents(*_dstClip, dstPx);
    clipComponents.addClipComponents(*_srcClip, dstPx);

    if (_uvClip) {
        std::map<Clip*, std::set<std::string> > clipMap;
        bool isCreatingAlpha;
        for (int i = 0; i < 2; ++i) {
            std::string ofxComp, ofxPlane;
            int channelIndex;
            Clip* clip = 0;
            bool ok = getPlaneNeededForParam(time, _uvChannels[i]->getName(), &clip, &ofxPlane, &ofxComp, &channelIndex, &isCreatingAlpha);
            if (!ok) {
                continue;
            }
            if ( (ofxComp == kMultiPlaneParamOutputOption0) || (ofxComp == kMultiPlaneParamOutputOption1) ) {
                continue;
            }
            assert(clip);

            std::map<Clip*, std::set<std::string> >::iterator foundClip = clipMap.find(clip);
            if ( foundClip == clipMap.end() ) {
                std::set<std::string> s;
                s.insert(ofxComp);
                clipMap.insert( std::make_pair(clip, s) );
                clipComponents.addClipComponents(*clip, ofxComp);
            } else {
                std::pair<std::set<std::string>::iterator, bool> ret = foundClip->second.insert(ofxComp);
                if (ret.second) {
                    clipComponents.addClipComponents(*clip, ofxComp);
                }
            }
        }
    }
} // getClipComponents

#endif

void
DistortionPlugin::updateVisibility()
{
    if (_plugin == eDistortionPluginLensDistortion) {
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
                                   distortionModel == eDistortionModel3DEStandard);
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

        _c2->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _u1->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _v1->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _c4->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _u3->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _v3->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _phi->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
        _b->setIsSecretAndDisabled(distortionModel != eDistortionModel3DEStandard);
}
}

void
DistortionPlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if (_plugin == eDistortionPluginLensDistortion) {
        if ( (paramName == kParamDistortionModel) && (args.reason == eChangeUserEdit) ) {
            updateVisibility();
        }
        if ( (paramName == kParamPFFileReload) ||
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
        }
        return;
    }
    if (_plugin == eDistortionPluginIDistort ||
        _plugin == eDistortionPluginSTMap) {
        if (gIsMultiPlane) {
            if ( handleChangedParamForAllDynamicChoices(paramName, args.reason) ) {
                return;
            }
        }
    }
}

//mDeclarePluginFactory(DistortionPluginFactory, {}, {});
template<DistortionPluginEnum plugin>
class DistortionPluginFactory
    : public PluginFactoryHelper<DistortionPluginFactory<plugin> >
{
public:
    DistortionPluginFactory<plugin>(const std::string & id, unsigned int verMaj, unsigned int verMin)
    : PluginFactoryHelper<DistortionPluginFactory>(id, verMaj, verMin)
    {
    }

    virtual void describe(ImageEffectDescriptor &desc);
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context);
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context);
};

template<DistortionPluginEnum plugin>
void
DistortionPluginFactory<plugin>::describe(ImageEffectDescriptor &desc)
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


    gIsMultiPlane = false;

#if defined(OFX_EXTENSIONS_NUKE) && defined(OFX_EXTENSIONS_NATRON)
    gIsMultiPlane = getImageEffectHostDescription()->supportsDynamicChoices && getImageEffectHostDescription()->isMultiPlanar;
    if (gIsMultiPlane) {
        // This enables fetching different planes from the input.
        // Generally the user will read a multi-layered EXR file in the Reader node and then use the shuffle
        // to redirect the plane's channels into RGBA color plane.

        desc.setIsMultiPlanar(true);
    }
#endif
} // >::describe

static void
addWrapOptions(ChoiceParamDescriptor* channel,
               WrapEnum def)
{
    assert(channel->getNOptions() == eWrapClamp);
    channel->appendOption(kParamWrapOptionClamp, kParamWrapOptionClampHint);
    assert(channel->getNOptions() == eWrapRepeat);
    channel->appendOption(kParamWrapOptionRepeat, kParamWrapOptionRepeatHint);
    assert(channel->getNOptions() == eWrapMirror);
    channel->appendOption(kParamWrapOptionMirror, kParamWrapOptionMirrorHint);
    channel->setDefault(def);
}

template<DistortionPluginEnum plugin>
void
DistortionPluginFactory<plugin>::describeInContext(ImageEffectDescriptor &desc,
                                                   ContextEnum context)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( gIsMultiPlane && !fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
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
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
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
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
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
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
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
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    if ( (plugin == eDistortionPluginIDistort) ||
         ( plugin == eDistortionPluginSTMap) ) {
        std::vector<std::string> clipsForChannels(1);
        clipsForChannels[0] = kClipUV;

        if (gIsMultiPlane) {
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelU, kParamChannelULabel, kParamChannelUHint);
#ifdef OFX_EXTENSIONS_NUKE
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
                param->setDefault(eInputChannelR);
            }
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelV, kParamChannelVLabel, kParamChannelVHint);
                param->setDefault(eInputChannelG);
            }
            {
                ChoiceParamDescriptor* param = MultiPlane::Factory::describeInContextAddChannelChoice(desc, page, clipsForChannels, kParamChannelA, kParamChannelALabel, kParamChannelAHint);
                param->setDefault(eInputChannelA);
            }
        } else {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelU);
                param->setLabel(kParamChannelULabel);
                param->setHint(kParamChannelUHint);
#ifdef OFX_EXTENSIONS_NUKE
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
                param->setDefault(eInputChannelR);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelV);
                param->setLabel(kParamChannelVLabel);
                param->setHint(kParamChannelVHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
                param->setDefault(eInputChannelG);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelA);
                param->setLabel(kParamChannelALabel);
                param->setHint(kParamChannelAHint);
                MultiPlane::Factory::addInputChannelOptionsRGBA(param, clipsForChannels, true);
                param->setDefault(eInputChannelA);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamChannelUnpremultUV);
            param->setLabel(kParamChannelUnpremultUVLabel);
            param->setHint(kParamChannelUnpremultUVHint);
            param->setDefault(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVOffset);
            param->setLabel(kParamUVOffsetLabel);
            param->setHint(kParamUVOffsetHint);
            param->setDefault(0., 0.);
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
            param->setLabel(kParamUVScaleLabel);
            param->setHint(kParamUVScaleHint);
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
                param->setLabel(kParamWrapULabel);
                param->setHint(kParamWrapUHint);
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
                param->setLabel(kParamWrapVLabel);
                param->setHint(kParamWrapVHint);
                addWrapOptions(param, eWrapClamp);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

    if (plugin == eDistortionPluginLensDistortion) {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionModel);
            param->setLabel(kParamDistortionModelLabel);
            param->setHint(kParamDistortionModelHint);
            assert(param->getNOptions() == eDistortionModelNuke);
            param->appendOption(kParamDistortionModelOptionNuke, kParamDistortionModelOptionNukeHint);
            assert(param->getNOptions() == eDistortionModelPFBarrel);
            param->appendOption(kParamDistortionModelOptionPFBarrel, kParamDistortionModelOptionPFBarrelHint);
            assert(param->getNOptions() == eDistortionModel3DEClassic);
            param->appendOption(kParamDistortionModelOption3DEClassic, kParamDistortionModelOption3DEClassicHint);
            assert(param->getNOptions() == eDistortionModel3DEStandard);
            param->appendOption(kParamDistortionModelOption3DEStandard, kParamDistortionModelOption3DEStandardHint);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionDirection);
            param->setLabel(kParamDistortionDirectionLabel);
            param->setHint(kParamDistortionDirectionHint);
            assert(param->getNOptions() == eDirectionDistort);
            param->appendOption(kParamDistortionDirectionOptionDistort, kParamDistortionDirectionOptionDistortHint);
            assert(param->getNOptions() == eDirectionUndistort);
            param->appendOption(kParamDistortionDirectionOptionUndistort, kParamDistortionDirectionOptionUndistortHint);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionOutputMode);
            param->setLabel(kParamDistortionOutputModeLabel);
            param->setHint(kParamDistortionOutputModeHint);
            assert(param->getNOptions() == eOutputModeImage);
            param->appendOption(kParamDistortionOutputModeOptionImage, kParamDistortionOutputModeOptionImageHint);
            assert(param->getNOptions() == eOutputModeSTMap);
            param->appendOption(kParamDistortionOutputModeOptionSTMap, kParamDistortionOutputModeOptionSTMapHint);
            if (page) {
                page->addChild(*param);
            }
        }

        // Nuke
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK1);
            param->setLabel(kParamK1Label);
            param->setHint(kParamK1Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.3, 0.3);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK2);
            param->setLabel(kParamK2Label);
            param->setHint(kParamK2Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.1, 0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamCenter);
            param->setLabel(kParamCenterLabel);
            param->setHint(kParamCenterHint);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setUseHostNativeOverlayHandle(false);
            param->setDisplayRange(-1, -1, 1, 1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSqueeze);
            param->setLabel(kParamSqueezeLabel);
            param->setHint(kParamSqueezeHint);
            param->setDefault(1.);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamAsymmetric);
            param->setLabel(kParamAsymmetricLabel);
            param->setHint(kParamAsymmetricHint);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
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
            param->setLabel(kParamPFFileLabel);
            param->setHint(kParamPFFileHint);
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
            param->setLabel(kParamPFFileReloadLabel);
            param->setHint(kParamPFFileReloadHint);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPFC3);
            param->setLabel(kParamPFC3Label);
            param->setHint(kParamPFC3Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamPFC5);
            param->setLabel(kParamPFC5Label);
            param->setHint(kParamPFC5Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamPFP);
            param->setLabel(kParamPFPLabel);
            param->setHint(kParamPFPHint);
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
            param->setLabel(kParamPFSqueezeLabel);
            param->setHint(kParamPFSqueezeHint);
            param->setRange(0.0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 0.3);
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
            param->setLabel(kParam3DE4_xa_fov_unitLabel);
            //param->setHint(kParam3DE4_xa_fov_unitHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_ya_fov_unit);
            param->setLabel(kParam3DE4_ya_fov_unitLabel);
            //param->setHint(kParam3DE4_ya_fov_unitHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_xb_fov_unit);
            param->setLabel(kParam3DE4_xb_fov_unitLabel);
            //param->setHint(kParam3DE4_xb_fov_unitHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0., 1.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_yb_fov_unit);
            param->setLabel(kParam3DE4_yb_fov_unitLabel);
            //param->setHint(kParam3DE4_yb_fov_unitHint);
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
            param->setLabel(kParam3DE4_focal_length_cmLabel);
            //param->setHint(kParam3DE4_focal_length_cmHint);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.5, 50.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_custom_focus_distance_cm);
            param->setLabel(kParam3DE4_custom_focus_distance_cmLabel);
            //param->setHint(kParam3DE4_custom_focus_distance_cmHint);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(10, 1000.);
            param->setDefault(100.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_filmback_width_cm);
            param->setLabel(kParam3DE4_filmback_width_cmLabel);
            //param->setHint(kParam3DE4_filmback_width_cmHint);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 10.);
            param->setDefault(0.8);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_filmback_height_cm);
            param->setLabel(kParam3DE4_filmback_height_cmLabel);
            //param->setHint(kParam3DE4_filmback_height_cmHint);
            param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.1, 10.);
            param->setDefault(0.6);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_lens_center_offset_x_cm);
            param->setLabel(kParam3DE4_lens_center_offset_x_cmLabel);
            //param->setHint(kParam3DE4_lens_center_offset_x_cmHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-5., 5.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_lens_center_offset_y_cm);
            param->setLabel(kParam3DE4_lens_center_offset_y_cmLabel);
            //param->setHint(kParam3DE4_lens_center_offset_y_cmHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-5., 5.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DE4_pixel_aspect);
            param->setLabel(kParam3DE4_pixel_aspectLabel);
            //param->setHint(kParam3DE4_pixel_aspectHint);
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
            param->setLabel(kParam3DEDistortionLabel);
            //param->setHint(kParam3DEDistortionHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEAnamorphicSqueeze);
            param->setLabel(kParam3DEAnamorphicSqueezeLabel);
            //param->setHint(kParam3DEAnamorphicSqueezeHint);
            param->setRange(0., DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0.25, 4.);
            param->setDefault(1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECurvatureX);
            param->setLabel(kParam3DECurvatureXLabel);
            //param->setHint(kParam3DECurvatureXHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DECurvatureY);
            param->setLabel(kParam3DECurvatureYLabel);
            //param->setHint(kParam3DECurvatureYHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEQuarticDistortion);
            param->setLabel(kParam3DEQuarticDistortionLabel);
            //param->setHint(kParam3DEQuarticDistortionHint);
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
            param->setLabel(kParam3DEDistortionDegree2Label);
            //param->setHint(kParam3DEDistortionDegree2Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEUDegree2);
            param->setLabel(kParam3DEUDegree2Label);
            //param->setHint(kParam3DEUDegree2Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEVDegree2);
            param->setLabel(kParam3DEVDegree2Label);
            //param->setHint(kParam3DEVDegree2Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEQuarticDistortionDegree4);
            param->setLabel(kParam3DEQuarticDistortionDegree4Label);
            //param->setHint(kParam3DEQuarticDistortionDegree4Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEUDegree4);
            param->setLabel(kParam3DEUDegree4Label);
            //param->setHint(kParam3DEUDegree4Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEVDegree4);
            param->setLabel(kParam3DEVDegree4Label);
            //param->setHint(kParam3DEVDegree4Hint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.5, 0.5);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEPhiCylindricDirection);
            param->setLabel(kParam3DEPhiCylindricDirectionLabel);
            //param->setHint(kParam3DEPhiCylindricDirectionHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-90., 90.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParam3DEBCylindricBending);
            param->setLabel(kParam3DEBCylindricBendingLabel);
            //param->setHint(kParam3DEBCylindricBendingHint);
            param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-0.1, 0.1);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    ofxsFilterDescribeParamsInterpolate2D( desc, page, (plugin == eDistortionPluginSTMap) );
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

template<DistortionPluginEnum plugin>
ImageEffect*
DistortionPluginFactory<plugin>::createInstance(OfxImageEffectHandle handle,
                                                ContextEnum /*context*/)
{
    return new DistortionPlugin(handle, plugin);
}

static DistortionPluginFactory<eDistortionPluginIDistort> p1(kPluginIDistortIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginSTMap> p2(kPluginSTMapIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static DistortionPluginFactory<eDistortionPluginLensDistortion> p3(kPluginLensDistortionIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)

OFXS_NAMESPACE_ANONYMOUS_EXIT
