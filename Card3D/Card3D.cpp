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
 * OFX Transform & DirBlur plugins.
 */

#define _USE_MATH_DEFINES
#include <cmath> // tan, atan2
#include <cstring> // strerror
#include <cstdio> // fopen, fclose
#include <cstdlib> // atoi, atof
#include <cerrno> // errno
#include <iostream>

#include "ofxsTransform3x3.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"
#include "ofxsGenerator.h"
#include "ofxsFormatResolution.h"
#include "ofxsFileOpen.h"

using namespace OFX;
using std::string;

#define kCameraAxis "axis"
#define kCameraAxisLabel "Axis"

#define kCameraCam "cam"
#define kCameraCamLabel "Cam"

#define kGroupCard "card"
#define kGroupCardLabel "Card"


//#define kParamTransformAmount "transformAmount"
//#define kParamTransformAmountLabel "Amount"
//#define kParamTransformAmountHint "Amount of transform to apply (excluding the extra matrix, which is always applied). 0 means the transform is identity, 1 means to apply the full transform."

#define kParamTransformInteractive "interactive"
#define kParamTransformInteractiveLabel "Interactive Update"
#define kParamTransformInteractiveHint "If checked, update the parameter values during interaction with the image viewer, else update the values when pen is released."

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "Card3DOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Card3D.\n" \
    "This effect applies a transform that corresponds to projection the source image onto a 3D card in space. The 3D card is positionned with relative to the Axis position, and the Camera position may also be given. The Axis may be used to apply the same global motion to several cards.\n" \
    "This plugin concatenates transforms.\n" \
    "http://opticalenquiry.com/nuke/index.php?title=Card3D"

#define kPluginIdentifier "net.sf.openfx.Card3D"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamSrcClipChanged "srcClipChanged"

#define kParamCamEnable "camEnable"
#define kParamCamEnableLabel "Enable Camera", "Enable the camera projection parameters."

#define kParamLensInFocal "lensInFocal"
#define kParamLensInFocalLabel "Lens-In Focal", "The focal length of the camera that took the picture on the card. The card is scaled so that at distance 1 (which is the default card Z) it occupies the field of view corresponding to lensInFocal and lensInHAperture."

#define kParamLensInHAperture "lensInHAperture"
#define kParamLensInHApertureLabel "Lens-In H.Aperture", "The horizontal aperture (or sensor/film back width) of the camera that took the picture on the card. The card is scaled so that at distance 1 (which is the default card Z) it occupies the field of view corresponding to lensInFocal and lensInHAperture."

#define kParamOutputFormat "format"
#define kParamOutputFormatLabel "Output Format", "Desired format for the output sequence."

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

////////////////////////////////////////////////////////////////////////////////
// BEGIN CameraParm

// Camera Projection parameters


#define kParamCameraProjectionGroup "Projection"
#define kParamCameraProjectionGroupLabel "Projection"

#define kParamCameraProjectionMode kNukeOfxCameraParamProjectionMode
#define kParamCameraProjectionModeLabel "Projection"
#define kParamCameraProjectionModeOptionPerspective "Perspective", "Perspective projection.", "perspective"
#define kParamCameraProjectionModeOptionOrthographic "Orthographic", "Orthographic projection", "orthographic"
//#define kParamCameraProjectionModeOptionUV "UV", "UV projection", "uv" // used in Nuke to compute uv coordinates instead of pixel values
//#define kParamCameraProjectionModeOptionSpherical "Spherical", "Spherical projection." , "spherical"
enum CameraProjectionModeEnum {
    eCameraProjectionModePerspective = 0,
    eCameraProjectionModeOrthographic,
    eCameraProjectionModeUV,
    eCameraProjectionModeSpherical,
};
/* other camera projections from nona http://hugin.sourceforge.net/docs/nona/nona.txt
see also http://wiki.panotools.org/Projections http://wiki.panotools.org/Fisheye
#                  0 - rectilinear (for printing and viewing) [R = f.tan(theta)] http://wiki.panotools.org/Rectilinear_Projection
#                  1 - Cylindrical (for Printing and QTVR) http://wiki.panotools.org/Cylindrical_Projection
#                  2 - Equirectangular ( for Spherical panos), default http://wiki.panotools.org/Equirectangular_Projection
#                  3 - full-frame fisheye [R = f.theta], e.g. Peleng 8mm f/3.5 Fisheye 
#                  4 - Stereographic [R = 2f.tan(theta/2)], e.g. Samyang 8 mm f/3.5
#                  5 - Mercator http://wiki.panotools.org/Projections#Mercator_projection
#                  6 - Transverse Mercator
#                  7 - Sinusoidal http://wiki.panotools.org/Projections#Sinusoidal_projection
#                  8 - Lambert Cylindrical Equal Area
#                  9 - Lambert Equal Area Azimuthal
#                 10 - Albers Equal Area Conic
#                 11 - Miller Cylindrical http://wiki.panotools.org/Projections#Miller_projection
#                 12 - Panini (obsolete non-Panini projection)
#                 13 - Architectural (Miller above the horizon, Lambert Equal Area below
#                 14 - Orthographic [R = f.sin(theta)], e.g. Yasuhara - MADOKA 180 circle fisheye lens 
#                 15 - Equisolid (equal-area fisheye) [R=2f.sin(theta/2)], e. g. Sigma 8mm f/4.0 AF EX, Olympus 8mm F2.8, Nikon 6mm F2.8, Nikon 8mm F2.8 (also convex mirror) 
#                 16 - Equirectangular Panini (standard Panini, covered by Panini General)
#                 17 - Biplane
#                 18 - Triplane
#                 19 - Panini General http://wiki.panotools.org/The_General_Panini_Projection has 3 parameters:  http://wiki.panotools.org/The_General_Panini_Projection#Parameters
#                 20 - Thoby Projection [R = k1.f.sin(k2.theta) k1=1.47 k2=0.713], e.g. AF DX Fisheye-Nikkor 10.5mm f/2.8G ED 
#                 21 - Hammer-Aitoff Projection

lens may be:
#                  0 - rectilinear (normal lenses)
#                  1 - Panoramic (Scanning cameras like Noblex)
#                  2 - Circular fisheye
#                  3 - full-frame fisheye
#                  4 - PSphere, equirectangular
#                  8 - orthographic fisheye
#                 10 - stereographic fisheye
#                 21 - Equisolid fisheye
#                 20 - Fisheye Thoby (Nikkor 10.5)

lens supported by videostitch studio:
    2 - Equirectangular
    4 - Stereographic
    0 - Rectilinear
    ??? Circular fisheye
    3 - Full-frame fisheye

 */
#define kParamCameraFocalLength kNukeOfxCameraParamFocalLength
#define kParamCameraFocalLengthLabel "Focal Length", "The camera focal length, in arbitrary units (usually either millimeters or 35 mm equivalent focal length). haperture and vaperture must be expressed in the same units."
#define kParamCameraHorizontalAperture kNukeOfxCameraParamHorizontalAperture
#define kParamCameraHorizontalApertureLabel "Horiz. Aperture", "The camera horizontal aperture (or film back width), in the same units as the focal length. In the case of scanned film, this can be obtained as image_width * scanner_pitch."
#define kParamCameraVerticalAperture kNukeOfxCameraParamVerticalAperture
#define kParamCameraVerticalApertureLabel "Vert. Aperture", "The camera vertical aperture (or film back height), in the same units as the focal length. This does not affect the projection (which is computed from haperture and the image aspect ratio), but it is used to compute the focal length from vertical FOV when importing chan files, using the formula: focal = 0.5 * vaperture / tan(vfov/2). It is thus best set as: haperture = vaperture * image_width/image_height. In the case of scanned film, this can be obtained as image_height * scanner_pitch."
#define kParamCameraNear kNukeOfxCameraParamNear
#define kParamCameraNearLabel "Near"
#define kParamCameraFar kNukeOfxCameraParamFar
#define kParamCameraFarLabel "Far"
#define kParamCameraWindowTranslate kNukeOfxCameraParamWindowTranslate
#define kParamCameraWindowTranslateLabel "Window Translate", "The camera window (or film back) is translated by this fraction of the horizontal aperture, without changing the position of the camera center. This can be used to model tilt-shift or perspective-control lens."
#define kParamCameraWindowScale kNukeOfxCameraParamWindowScale
#define kParamCameraWindowScaleLabel "Window Scale", "Scale the camera window (or film back)."
#define kParamCameraWindowRoll kNukeOfxCameraParamWindowRoll
#define kParamCameraWindowRollLabel "Window Roll", "Rotation (in degrees) of the camera window (or film back) around the z axis."
#define kParamCameraFocalPoint kNukeOfxCameraParamFocalPoint
#define kParamCameraFocalPointLabel "Focus Distance", "Focus distance of the camera (used for depth-of-field effects)."
#define kParamCameraFStop "fstop"
#define kParamCameraFStopLabel "F-Stop", "F-stop (relative aperture) of the camera (used for depth-of-field effects)."

class CameraParam {
    friend class PosMatParam;

    std::string _prefix;
    ChoiceParam* _camProjectionMode;
    DoubleParam* _camFocalLength;
    DoubleParam* _camHAperture;
    DoubleParam* _camVAperture;
    DoubleParam* _camNear;
    DoubleParam* _camFar;
    Double2DParam* _camWinTranslate;
    Double2DParam* _camWinScale;
    DoubleParam* _camWinRoll;
    DoubleParam* _camFocusDistance;
    DoubleParam* _camFStop;
    bool _enabled;

public:
    CameraParam(OFX::ImageEffect* parent, const std::string& prefix)
    : _prefix(prefix)
    , _camProjectionMode(NULL)
    , _camFocalLength(NULL)
    , _camHAperture(NULL)
    , _camVAperture(NULL)
    , _camNear(NULL)
    , _camFar(NULL)
    , _camWinTranslate(NULL)
    , _camWinScale(NULL)
    , _camWinRoll(NULL)
    , _camFocusDistance(NULL)
    , _camFStop(NULL)
    , _enabled(true)
    {
        _camProjectionMode = parent->fetchChoiceParam(prefix + kParamCameraProjectionMode);
        _camFocalLength = parent->fetchDoubleParam(prefix + kParamCameraFocalLength);
        _camHAperture = parent->fetchDoubleParam(prefix + kParamCameraHorizontalAperture);
        if (parent->paramExists(prefix + kParamCameraVerticalAperture)) {
            _camVAperture = parent->fetchDoubleParam(prefix + kParamCameraVerticalAperture);
        }
        if (parent->paramExists(prefix + kParamCameraNear)) {
            _camNear = parent->fetchDoubleParam(prefix + kParamCameraNear);
        }
        if (parent->paramExists(prefix + kParamCameraFar)) {
            _camFar = parent->fetchDoubleParam(prefix + kParamCameraFar);
        }
        _camWinTranslate = parent->fetchDouble2DParam(prefix + kParamCameraWindowTranslate);
        _camWinScale = parent->fetchDouble2DParam(prefix + kParamCameraWindowScale);
        _camWinRoll = parent->fetchDoubleParam(prefix + kParamCameraWindowRoll);
        if (parent->paramExists(prefix + kParamCameraFocalPoint)) {
            _camFocusDistance = parent->fetchDoubleParam(prefix + kParamCameraFocalPoint);
        }
        if (parent->paramExists(prefix + kParamCameraFStop)) {
            _camFStop = parent->fetchDoubleParam(prefix + kParamCameraFStop);
        }
    }

    void getValueAtTime(double time,
                        CameraProjectionModeEnum& projectionMode,
                        double& focalLength,
                        double& hAperture,
                        double& winTranslateU,
                        double& winTranslateV,
                        double& winScaleU,
                        double& winScaleV,
                        double& winRoll) const;

    static void getMatrix(const Matrix4x4& pos,
                          CameraProjectionModeEnum projectionMode,
                          double focalLength,
                          double hAperture,
                          double winTranslateU,
                          double winTranslateV,
                          double winScaleU,
                          double winScaleV,
                          double winRoll,
                          Matrix3x3* mat);

    static void define(ImageEffectDescriptor &desc,
                       PageParamDescriptor *page,
                       GroupParamDescriptor *group,
                       const std::string prefix);

    void setEnabled(bool enabled) { _enabled = enabled; update(); }
    
private:

    void update()
    {
        _camProjectionMode->setEnabled(_enabled);
        _camFocalLength->setEnabled(_enabled);
        _camHAperture->setEnabled(_enabled);
        if (_camVAperture) {
            _camVAperture->setEnabled(_enabled);
        }
        if (_camNear) {
            _camNear->setEnabled(_enabled);
        }
        if (_camFar) {
            _camFar->setEnabled(_enabled);
        }
        _camWinTranslate->setEnabled(_enabled);
        _camWinScale->setEnabled(_enabled);
        _camWinRoll->setEnabled(_enabled);
        if (_camFocusDistance) {
            _camFocusDistance->setEnabled(_enabled);
        }
        if (_camFStop) {
            _camFStop->setEnabled(_enabled);
        }
    }
};



void
CameraParam::define(ImageEffectDescriptor &desc,
                    PageParamDescriptor *page,
                    GroupParamDescriptor *group,
                    const std::string prefix)
{
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(prefix + kParamCameraProjectionMode);
        param->setLabel(kParamCameraProjectionModeLabel);
        assert(param->getNOptions() == eCameraProjectionModePerspective);
        param->appendOption(kParamCameraProjectionModeOptionPerspective);
        assert(param->getNOptions() == eCameraProjectionModeOrthographic);
        param->appendOption(kParamCameraProjectionModeOptionOrthographic);
        /*
        assert(param->getNOptions() == eCameraProjectionModeUV);
        param->appendOption(kParamCameraProjectionModeOptionUV);
        assert(param->getNOptions() == eCameraProjectionModeSpherical);
        param->appendOption(kParamCameraProjectionModeOptionSpherical);
        //*/
        param->setDefault(eCameraProjectionModePerspective);
        param->setAnimates(false);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraFocalLength);
        param->setLabelAndHint(kParamCameraFocalLengthLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(5, 100);
        param->setDefault(50.);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraHorizontalAperture);
        param->setLabelAndHint(kParamCameraHorizontalApertureLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(0.1, 50);
        param->setDefault(24.576);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraVerticalAperture);
        param->setLabelAndHint(kParamCameraVerticalApertureLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(0.1, 50);
        param->setDefault(18.672);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    /*
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraNear);
        param->setLabelAndHint(kParamCameraNearLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(0.1, 10);
        param->setDefault(0.1);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraFar);
        param->setLabelAndHint(kParamCameraFarLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(11, 10000);
        param->setDefault(10000);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    //*/
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(prefix + kParamCameraWindowTranslate);
        param->setLabelAndHint(kParamCameraWindowTranslateLabel);
        param->setRange(-1, -1, 1, 1);
        param->setDisplayRange(-1, -1, 1, 1);
        param->setDoubleType(eDoubleTypePlain);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(prefix + kParamCameraWindowScale);
        param->setLabelAndHint(kParamCameraWindowScaleLabel);
        param->setRange(1e-8, 1e-8, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setDefault(1, 1);
        param->setDoubleType(eDoubleTypeScale);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraWindowRoll);
        param->setLabelAndHint(kParamCameraWindowRollLabel);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-45, 45);
        param->setDoubleType(eDoubleTypeAngle);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    /*
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraFocalPoint);
        param->setLabelAndHint(kParamCameraFocalPointLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(0.1, 10);
        param->setDefault(2);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*subgroup);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraFStop);
        param->setLabelAndHint(kParamCameraFStopLabel);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(0.1, 30);
        param->setDefault(16);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    //*/
}

void
CameraParam::getValueAtTime(double time,
                            CameraProjectionModeEnum& projectionMode,
                            double& focalLength,
                            double& hAperture,
                            double& winTranslateU,
                            double& winTranslateV,
                            double& winScaleU,
                            double& winScaleV,
                            double& winRoll) const
{
    projectionMode = (CameraProjectionModeEnum)_camProjectionMode->getValueAtTime(time);
    focalLength = _camFocalLength->getValueAtTime(time);
    hAperture = _camHAperture->getValueAtTime(time);
    _camWinTranslate->getValueAtTime(time, winTranslateU, winTranslateV);
    _camWinScale->getValueAtTime(time, winScaleU, winScaleV);
    winRoll = _camWinRoll->getValueAtTime(time);
}

void
CameraParam::getMatrix(const Matrix4x4& pos,
                       const CameraProjectionModeEnum projectionMode,
                       const double focalLength,
                       const double hAperture,
                       const double winTranslateU,
                       const double winTranslateV,
                       const double winScaleU,
                       const double winScaleV,
                       const double winRoll,
                       Matrix3x3* mat)
{
    // apply camera params
    double a = hAperture / std::max(1e-8, focalLength);
#pragma message WARN("make sure the standard camera matrix gives z > 0 in orthographic and projective cases")
    (*mat)(0,0) = -pos(0,0); (*mat)(0,1) = -pos(0,1); (*mat)(0,2) = -pos(0,3);
    (*mat)(1,0) = -pos(1,0); (*mat)(1,1) = -pos(1,1); (*mat)(1,2) = -pos(1,3);
    if (projectionMode == eCameraProjectionModePerspective) {
        // divide by Z
        (*mat)(2,0) = -a * pos(2,0); (*mat)(2,1) = -a * pos(2,1); (*mat)(2,2) = -a * pos(2,3);
    } else {
        // orthographic
        (*mat)(2,0) = a * pos(3,0); (*mat)(2,1) = a * pos(3,1); (*mat)(2,2) = a * pos(3,3);
    }
    // apply winTranslate
    (*mat)(0,0) += (*mat)(2,0) * winTranslateU / 2.;
    (*mat)(1,0) += (*mat)(2,0) * winTranslateV / 2.;
    (*mat)(0,1) += (*mat)(2,1) * winTranslateU / 2.;
    (*mat)(1,1) += (*mat)(2,1) * winTranslateV / 2.;
    (*mat)(0,2) += (*mat)(2,2) * winTranslateU / 2.;
    (*mat)(1,2) += (*mat)(2,2) * winTranslateV / 2.;
    // apply winScale
    (*mat)(0,0) /= winScaleU;
    (*mat)(0,1) /= winScaleU;
    (*mat)(0,2) /= winScaleU;
    (*mat)(1,0) /= winScaleV;
    (*mat)(1,1) /= winScaleV;
    (*mat)(1,2) /= winScaleV;
    // apply winRoll
    if (winRoll != 0.) {
        double s = std::sin(winRoll * M_PI / 180.);
        double c = std::cos(winRoll * M_PI / 180.);
        *mat = Matrix3x3(c, -s, 0,
                         s, c, 0,
                         0, 0, 1) * *mat;
    }

}

// END CameraParm
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// BEGIN PosMatParm

enum PosMatTypeEnum {
    ePosMatAxis = 0,
    ePosMatCamera,
    ePosMatCard,
};

#define kParamPosMatFile "File"
#define kParamPosMatFileLabel "File", "Import/export data"

#define kParamPosMatImportFile "ImportFile"
#define kParamPosMatImportFileLabel "Import", "Import a chan file created using 3D tracking software, or a txt file created using Boujou."
#define kParamPosMatImportFileReload "ImportFileReload"
#define kParamPosMatImportFileReloadLabel "Reload", "Reload the file."

#define kParamPosMatImportFormat "ImportFormat"
#define kParamPosMatImportFormatLabel "Import Format", "The format of the file to import."
#define kParamPosMatImportFormatOptionChan "chan", "Chan format, each line is FRAME TX TY TZ RX RY RZ VFOV. Can be created using Natron, Nuke, 3D-Equalizer, Maya and other 3D tracking software. Be careful that the rotation order must be exactly the same when exporting and importing the chan file.", "chan"
#define kParamPosMatImportFormatOptionBoujou "Boujou", "Boujou text export. In Boujou, after finishing the track and solving, go to Export > Export Camera Solve (Or press F12) > choose where to save the data and give it a name, click he drop down Export Type and make sure it will save as a .txt, then click Save. Each camera line is R(0,0) R(0,1) R(0,2) R(1,0) R(1,1) R(1,2) R(2,0) R(2,1) R(2,2) Tx Ty Tz F(mm).", "boujou"
enum ImportFormatEnum {
    eImportFormatChan = 0,
    eImportFormatBoujou,
};

#define kParamPosMatExportChan "ExportChan"
#define kParamPosMatExportChanLabel "Export", "Export a .chan file which can be used in Natron, Nuke or 3D tracking software, such as 3D-Equalizer, Maya, or Boujou. Be careful that the rotation order must be exactly the same when exporting and importing the chan file."

#define kParamPosMatExportChanRewrite "ExportChanRewrite"
#define kParamPosMatExportChanRewriteLabel "Rewrite", "Rewrite the .chan file."

#define kParamPosMatTransformOrder "XformOrder"
#define kParamPosMatTransformOrderLabel "Transform Order", "Order in which scale (S), rotation (R) and translation (T) are applied."
#define kParamPosMatTransformOrderOptionSRT "SRT", "Scale, Rotation, Translation.", "srt"
#define kParamPosMatTransformOrderOptionSTR "STR", "Scale, Translation, Rotation.", "str"
#define kParamPosMatTransformOrderOptionRST "RST", "Rotation, Scale, Translation.", "rst"
#define kParamPosMatTransformOrderOptionRTS "RTS", "Rotation, Translation, Scale.", "rts"
#define kParamPosMatTransformOrderOptionTSR "TSR", "Translation, Scale, Rotation.", "tsr"
#define kParamPosMatTransformOrderOptionTRS "TRS", "Translation, Rotation, Scale.", "trs"
enum PosMatTransformOrderEnum {
    ePosMatTransformOrderSRT = 0,
    ePosMatTransformOrderSTR,
    ePosMatTransformOrderRST,
    ePosMatTransformOrderRTS,
    ePosMatTransformOrderTSR,
    ePosMatTransformOrderTRS,
};
#define kParamPosMatTransformOrderDefault ePosMatTransformOrderSRT

#define kParamPosMatRotationOrder "RotOrder"
#define kParamPosMatRotationOrderLabel "Rotation Order", "Order in which Euler angles are applied in the rotation."
#define kParamPosMatRotationOrderOptionXYZ "XYZ", "Rotation over X axis, then Y and Z.", "xyz"
#define kParamPosMatRotationOrderOptionXZY "XZY", "Rotation over X axis, then Z and Y.", "xzy"
#define kParamPosMatRotationOrderOptionYXZ "YXZ", "Rotation over Y axis, then X and Z.", "yxz"
#define kParamPosMatRotationOrderOptionYZX "YZX", "Rotation over Y axis, then Z and X.", "yzx"
#define kParamPosMatRotationOrderOptionZXY "ZXY", "Rotation over Z axis, then X and Y.", "zxy"
#define kParamPosMatRotationOrderOptionZYX "ZYX", "Rotation over Z axis, then Y and X.", "zyx"
enum PosMatRotationOrderEnum {
    ePosMatRotationOrderXYZ = 0,
    ePosMatRotationOrderXZY,
    ePosMatRotationOrderYXZ,
    ePosMatRotationOrderYZX,
    ePosMatRotationOrderZXY,
    ePosMatRotationOrderZYX,
};
#define kParamPosMatRotationOrderDefault ePosMatRotationOrderZXY

#define kParamPosMatTranslate "Translate"
#define kParamPosMatTranslateLabel "Translate", "Translation component."

#define kParamPosMatRotate "Rotate"
#define kParamPosMatRotateLabel "Rotate", "Euler angles (in degrees)."

#define kParamPosMatScale "Scaling"
#define kParamPosMatScaleLabel "Scale", "Scale factor over each axis."

#define kParamPosMatUniformScale "UniformScale"
#define kParamPosMatUniformScaleLabel "Uniform Scale", "Scale factor over all axis. It is multiplied by the scale factor over each axis."

#define kParamPosMatSkew "Skew"
#define kParamPosMatSkewLabel "Skew", "Skew over each axis, in degrees."

#define kParamPosMatPivot "Pivot"
#define kParamPosMatPivotLabel "Pivot", "The position of the origin for position, scaling, skewing, and rotation."

#define kGroupPosMatLocalMatrix "LocalMatrix"
#define kGroupPosMatLocalMatrixLabel "Local Matrix"

#define kParamPosMatUseMatrix "UseMatrix"
#define kParamPosMatUseMatrixLabel "Specify Matrix", "Check to specify manually all the values for the position matrix."

#define kParamPosMatMatrix "Matrix"
#define kParamPosMatMatrixLabel "", "Matrix coefficient."


class PosMatParam {
    ImageEffect* _effect;
    Clip* _srcClip;
    std::string _prefix;
    GroupParam* _fileGroup;
    StringParam* _importFile;
    PushButtonParam* _importFileReload;
    ChoiceParam* _importFormat;
    StringParam* _exportChan;
    PushButtonParam* _exportChanRewrite;
    ChoiceParam* _transformOrder;
    ChoiceParam* _rotationOrder;
    Double3DParam* _translate;
    Double3DParam* _rotate;
    Double3DParam* _scale;
    DoubleParam* _uniformScale;
    Double3DParam* _skew;
    Double3DParam* _pivot;
    GroupParam* _localMatrix;
    BooleanParam* _useMatrix;
    DoubleParam* _matrix[4][4];
    GroupParam* _projectionGroup;
    CameraParam* _projection;
    bool _enabled;
    PosMatTypeEnum _type;

public:
    PosMatParam(OFX::ImageEffect* parent, const std::string& prefix, PosMatTypeEnum type)
    : _effect(parent)
    , _srcClip(NULL)
    , _prefix(prefix)
    , _fileGroup(NULL)
    , _importFile(NULL)
    , _importFileReload(NULL)
    , _importFormat(NULL)
    , _exportChan(NULL)
    , _exportChanRewrite(NULL)
    , _transformOrder(NULL)
    , _rotationOrder(NULL)
    , _translate(NULL)
    , _rotate(NULL)
    , _scale(NULL)
    , _uniformScale(NULL)
    , _skew(NULL)
    , _pivot(NULL)
    , _localMatrix(NULL)
    , _useMatrix(NULL)
    , _projectionGroup(NULL)
    , _projection(NULL)
    , _enabled(true)
    , _type(type)
    {
        _srcClip = _effect->fetchClip(kOfxImageEffectSimpleSourceClipName);
        _fileGroup = _effect->fetchGroupParam(prefix + kParamPosMatFile);
        _importFile = _effect->fetchStringParam(prefix + kParamPosMatImportFile);
        if (_effect->paramExists(prefix + kParamPosMatImportFileReload)) {
            _importFileReload = _effect->fetchPushButtonParam(prefix + kParamPosMatImportFileReload);
        }
        _importFormat = _effect->fetchChoiceParam(prefix + kParamPosMatImportFormat);
        _exportChan = _effect->fetchStringParam(prefix + kParamPosMatExportChan);
        if (_effect->paramExists(prefix + kParamPosMatExportChanRewrite)) {
            _exportChanRewrite = _effect->fetchPushButtonParam(prefix + kParamPosMatExportChanRewrite);
        }
        _transformOrder = _effect->fetchChoiceParam(prefix + kParamPosMatTransformOrder);
        _rotationOrder = _effect->fetchChoiceParam(prefix + kParamPosMatRotationOrder);
        _translate = _effect->fetchDouble3DParam(prefix + kParamPosMatTranslate);
        _rotate = _effect->fetchDouble3DParam(prefix + kParamPosMatRotate);
        _scale = _effect->fetchDouble3DParam(prefix + kParamPosMatScale);
        _uniformScale = _effect->fetchDoubleParam(prefix + kParamPosMatUniformScale);
        _skew = _effect->fetchDouble3DParam(prefix + kParamPosMatSkew);
        _pivot = _effect->fetchDouble3DParam(prefix + kParamPosMatPivot);
        _localMatrix = _effect->fetchGroupParam(prefix + kGroupPosMatLocalMatrix);
        _useMatrix = _effect->fetchBooleanParam(prefix + kParamPosMatUseMatrix);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                _matrix[i][j] = _effect->fetchDoubleParam(prefix + kParamPosMatMatrix + (char)('1' + i) + (char)('1' + j));
            }
        }
        if (_type == ePosMatCamera) {
            _projectionGroup = _effect->fetchGroupParam(kCameraCam kParamCameraProjectionGroup);
            _projection = new CameraParam(_effect, kCameraCam);
        }
        update();
    }

    virtual ~PosMatParam()
    {
        delete _projection;
    }

    void changedParam(const InstanceChangedArgs &args, const std::string &paramName);

    void getMatrix(double time, Matrix4x4* mat) const;

    static void define(ImageEffectDescriptor &desc,
                       PageParamDescriptor *page,
                       GroupParamDescriptor *group,
                       const std::string prefix,
                       PosMatTypeEnum type);

    void setEnabled(bool enabled) { _enabled = enabled; update(); }

    const CameraParam& getProjection() { assert(_projection); return *_projection; }

private:

    /// update visibility/enabledness
    void update()
    {
        bool useMatrix = _useMatrix->getValue();
        _fileGroup->setEnabled(_enabled && !useMatrix);
        _importFile->setEnabled(_enabled && !useMatrix);
        if (_importFileReload) {
            _importFileReload->setEnabled(_enabled && !useMatrix);
        }
        _exportChan->setEnabled(_enabled && !useMatrix);
        if (_exportChanRewrite) {
            _exportChanRewrite->setEnabled(_enabled && !useMatrix);
        }
        _transformOrder->setEnabled(_enabled && !useMatrix);
        _rotationOrder->setEnabled(_enabled && !useMatrix);
        _translate->setEnabled(_enabled && !useMatrix);
        _rotate->setEnabled(_enabled && !useMatrix);
        _scale->setEnabled(_enabled && !useMatrix);
        _uniformScale->setEnabled(_enabled && !useMatrix);
        _skew->setEnabled(_enabled && !useMatrix);
        _pivot->setEnabled(_enabled && !useMatrix);
        _localMatrix->setEnabled(_enabled);
        _useMatrix->setEnabled(_enabled);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                _matrix[i][j]->setEnabled(_enabled && useMatrix);
            }
        }
        if (_projectionGroup) {
            _projectionGroup->setEnabled(_enabled);
        }
        if (_projection) {
            _projection->setEnabled(_enabled);
        }
    }

    void importChan();

    void importBoujou();

    void exportChan();

    struct ChanLine {
        int frame;
        double tx, ty, tz, rx, ry, rz, vfov;

        ChanLine()
        : frame(-1)
        , tx(0)
        , ty(0)
        , tz(0)
        , rx(0)
        , ry(0)
        , rz(0)
        , vfov(0)
        {
        }
    };
};

static std::string
trim(std::string const & str)
{
    const std::string whitespace = " \t\f\v\n\r";
    std::size_t first = str.find_first_not_of(whitespace);

    // If there is no non-whitespace character, both first and last will be std::string::npos (-1)
    // There is no point in checking both, since if either doesn't work, the
    // other won't work, either.
    if (first == std::string::npos) {
        return "";
    }

    std::size_t last  = str.find_last_not_of(whitespace);

    return str.substr(first, last - first + 1);
}

void
PosMatParam::importChan()
{
    string filename;
    _importFile->getValue(filename);
    if ( filename.empty() ) {
        // no filename, do nothing
        return;
    }
    FILE* f = fopen_utf8(filename.c_str(), "r");
    if (!f) {
        _effect->sendMessage(Message::eMessageError, "", "Cannot read " + filename + ": " + std::strerror(errno), false);

        return;
    }
    std::list<ChanLine> lines;
    char buf[1024];

    while (std::fgets(buf, sizeof buf, f) != NULL) {
        const string bufstr( trim(buf) );
        if (bufstr.size() > 0 && bufstr[0] != '#') {
            const char* b = bufstr.c_str();
            ChanLine l;
            bool err = false;
            if (_type == ePosMatCamera) {
                int ret = std::sscanf(b, "%d%lf%lf%lf%lf%lf%lf%lf",
                                      &l.frame, &l.tx, &l.ty, &l.tz, &l.rx, &l.ry, &l.rz, &l.vfov);
                if (ret == 8) {
                    lines.push_back(l);
                } else {
                    err = true;
                }
            } else {
                int ret = std::sscanf(b, "%d%lf%lf%lf%lf%lf%lf",
                                      &l.frame, &l.tx, &l.ty, &l.tz, &l.rx, &l.ry, &l.rz);
                if (ret == 7) {
                    lines.push_back(l);
                } else {
                    err = true;
                }
            }
            if (err) {
                std::fclose(f);
                _effect->sendMessage(Message::eMessageError, "", "Chan import error: Cannot parse line from " + filename + ": '" + bufstr + "'", false);

                return;
            }
        }
    }
    std::fclose(f);
    _effect->beginEditBlock(kParamPosMatImportFile);
    _translate->deleteAllKeys();
    _rotate->deleteAllKeys();
    if (_type == ePosMatCamera && _projection && _projection->_camFocalLength) {
        _projection->_camFocalLength->deleteAllKeys();
    }
    for (std::list<ChanLine>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        _translate->setValueAtTime(it->frame, it->tx, it->ty, it->tz);
        _rotate->setValueAtTime(it->frame, it->rx, it->ry, it->rz);
        if (_type == ePosMatCamera && _projection && _projection->_camFocalLength) {
            double vaperture = _projection->_camVAperture->getValueAtTime(it->frame);
            double focal = 0.5 * vaperture / std::tan(0.5 * (it->vfov * M_PI / 180));

            _projection->_camFocalLength->setValueAtTime(it->frame, focal);
        }
    }
    _effect->endEditBlock();
}

// importBoujou.
//
// Credits:
// - Ivan Busquets' importBoujou.py
// http://www.nukepedia.com/python/import/export/importboujou
// - Blenders' import_boujou.py
// https://wiki.blender.org/index.php/Extensions:2.4/Py/Scripts/Manual/Import/Boujou
// https://sourceforge.net/projects/boujouimport/
void
PosMatParam::importBoujou()
{
    string filename;
    _importFile->getValue(filename);
    if ( filename.empty() ) {
        // no filename, do nothing
        return;
    }
    FILE* f = fopen_utf8(filename.c_str(), "r");
    if (!f) {
        _effect->sendMessage(Message::eMessageError, "", "Cannot read " + filename + ": " + std::strerror(errno), false);

        return;
    }
    bool foundOffset = false;
    int offsetFrame = 1;
    bool foundStart = false;
    int startFrame;
    double haperture = 0;
    double vaperture = 0;
    std::list<ChanLine> lines;
    char buf[1024];
    int i = 1; // line number

    while (std::fgets(buf, sizeof buf, f) != NULL) {
        if (i == 1) {
            const string b( trim(buf) );
            const string h("# boujou export: text");
            if (b != h) {
                _effect->sendMessage(Message::eMessageError, "", "Boujou import error: incorrect file header on first line, expected '" + h + "', got '" + b + "'", false);

                std::fclose(f);
                return;
            }
        }

        std::stringstream ss(buf); // Insert the string into a stream

        std::vector<string> line; // Create vector to hold our words

        // split using whitespace
        while (ss >> buf) {
            line.push_back(buf);
        }

        if (line.size() == 0) {
            continue;
        }
        // # boujou export: text
        // # Copyright (c) 2009, Vicon Motion Systems
        // # boujou Version: 5.0.0 47534
        // # Creation date : Thu Mar 17 17:30:38 2011
        // # The image sequence file name was C:/Users/Nate's/Videos/final.mp4
        // # boujou frame 0 is image sequence file 1000
        // # boujou frame 100 is image sequence file 1100
        // # One boujou frame for every image sequence frame
        // # Exporting camera data for boujou frames 65 to 100
        // # First boujou frame indexed to animation frame 1
        //
        //
        // #The Camera (One line per time frame)
        // #Image Size 1920 1080
        // #Filmback Size 14.7574 8.3007
        // #Line Format: Camera Rotation Matrix (9 numbers - 1st row, 2nd row, 3rd row) Camera Translation (3 numbers) Focal Length (mm)
        // #rotation applied before translation
        // #R(0,0) R(0,1) R(0,2) R(1,0) R(1,1) R(1,2) R(2,0) R(2,1) R(2,2) TxTy Tz F(mm)
        // ...
        //
        // #3D Scene Points
        // #x y z
        // ...
        // #End of boujou export file

        if (!foundOffset) {
            // # Exporting camera data for boujou frames 65 to 100
            if (line.size() == 10 && line[0] == "#" && line[1] == "Exporting" && line[line.size() - 2] == "to") {
                offsetFrame = std::atoi(line[line.size() - 3].c_str());
                foundOffset = true;
           }
        }
        if (!foundStart) {
            // # First boujou frame indexed to animation frame 1
            if (line.size() == 9 && line[0] == "#" && line[1] == "First" && line[2] == "boujou" && line[3] == "frame") {
                startFrame = std::atoi(line[line.size() - 1].c_str()) + offsetFrame;
                foundStart = true;
            }
        }

        // #Filmback Size 14.7574 8.3007
        if (line.size() == 4 && line[0] == "#Filmback") {
            haperture = std::atof(line[2].c_str());
            vaperture = std::atof(line[3].c_str());
        }

        if (foundOffset && foundStart && line.size() == 13 && buf[0] != '#') {
            ChanLine l;
            //bool err = false;
            l.frame = startFrame;
            l.vfov = std::atof(line[12].c_str());
            l.tx = std::atof(line[9].c_str());
            l.ty = std::atof(line[10].c_str());
            l.tz = std::atof(line[11].c_str());
            double rot00 = std::atof(line[0].c_str());
            double rot01 = std::atof(line[1].c_str());
            double rot02 = std::atof(line[2].c_str());
            double rot10 = std::atof(line[3].c_str());
            //double rot11 = std::atof(line[4].c_str());
            //double rot12 = std::atof(line[5].c_str());
            double rot20 = std::atof(line[6].c_str());
            double rot21 = std::atof(line[7].c_str());
            double rot22 = std::atof(line[8].c_str());
            if (rot00 == 0 && rot10 == 0) {
                l.rx = std::atan2(-rot01, -rot02) * 180. / M_PI;
                l.ry = 90.;
                l.rz = 0.;
            } else {
                l.rx = std::atan2(rot21, -rot22) * 180. / M_PI;
                l.ry = -std::asin(rot20) * 180. / M_PI;
                l.rz = std::atan2(rot10,rot00) * 180. / M_PI;
            }
            lines.push_back(l);
            ++startFrame;
        }
        ++i;
    }
    std::fclose(f);
    _effect->beginEditBlock(kParamPosMatImportFile);
    _translate->deleteAllKeys();
    _rotate->deleteAllKeys();
    if (_type == ePosMatCamera && _projection) {
        if (_projection->_camFocalLength) {
            _projection->_camFocalLength->deleteAllKeys();
        }
        if (_projection->_camHAperture && haperture != 0.) {
            _projection->_camHAperture->deleteAllKeys();
            _projection->_camHAperture->setValue(haperture);
        }
        if (_projection->_camVAperture && vaperture != 0.) {
            _projection->_camVAperture->deleteAllKeys();
            _projection->_camVAperture->setValue(vaperture);
        }
    }
    for (std::list<ChanLine>::const_iterator it = lines.begin(); it != lines.end(); ++it) {
        _translate->setValueAtTime(it->frame, it->tx, it->ty, it->tz);
        _rotate->setValueAtTime(it->frame, it->rx, it->ry, it->rz);
        if (_type == ePosMatCamera && _projection && _projection->_camFocalLength) {
            // it-vfov contains focal in Boujou
            double focal = it->vfov;
            //double vaperture = _projection->_camVAperture->getValueAtTime(it->frame);
            //double focal = 0.5 * vaperture / std::tan(0.5 * (it->vfov * M_PI / 180));

            _projection->_camFocalLength->setValueAtTime(it->frame, focal);
        }
    }
    _effect->endEditBlock();
}

void
PosMatParam::exportChan()
{
    string filename;
    _importFile->getValue(filename);
    if ( filename.empty() ) {
        // no filename, do nothing
        return;
    }
    FILE* f = fopen_utf8(filename.c_str(), "w");
    if (!f) {
        _effect->sendMessage(Message::eMessageError, "", "Cannot write " + filename + ": " + std::strerror(errno), false);
        return;
    }
    OfxRangeD r = _srcClip->getFrameRange();
    for (int t = (int)r.min; t <= (int)r.max; ++t) {
        ChanLine l;
        l.frame = t;
        _translate->getValueAtTime(t, l.tx, l.ty, l.tz);
        _rotate->getValueAtTime(t, l.rx, l.ry, l.rz);
        if (_type == ePosMatCamera && _projection && _projection->_camVAperture) {
            double vaperture = _projection->_camVAperture->getValueAtTime(t);
            double focal = _projection->_camFocalLength->getValueAtTime(t);
            l.vfov = 2 * std::atan2(0.5 * vaperture, focal) * 180 / M_PI;
            std::fprintf(f, "%d\t%g\t%g\t%g\t%g\t%g\t%g\t%g\n",
                         t, l.tx, l.ty, l.tz, l.rx, l.ry, l.rz, l.vfov);
        } else {
            std::fprintf(f, "%d\t%g\t%g\t%g\t%g\t%g\t%g\n",
                         t, l.tx, l.ty, l.tz, l.rx, l.ry, l.rz);
        }
    }
    std::fclose(f);
}

void
PosMatParam::define(ImageEffectDescriptor &desc,
                    PageParamDescriptor *page,
                    GroupParamDescriptor *group,
                    const std::string prefix,
                    PosMatTypeEnum type)
{
    {
        GroupParamDescriptor* subgroup = desc.defineGroupParam(prefix + kParamPosMatFile);
        subgroup->setLabelAndHint(kParamPosMatFileLabel);
        subgroup->setOpen(false);
        if (group) {
            subgroup->setParent(*group);
        }
        if (page) {
            page->addChild(*subgroup);
        }
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(prefix + kParamPosMatImportFormat);
            param->setLabelAndHint(kParamPosMatImportFormatLabel);
            assert(param->getNOptions() == eImportFormatChan);
            param->appendOption(kParamPosMatImportFormatOptionChan);
            assert(param->getNOptions() == eImportFormatBoujou);
            param->appendOption(kParamPosMatImportFormatOptionBoujou);
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            StringParamDescriptor* param = desc.defineStringParam(prefix + kParamPosMatImportFile);
            param->setLabelAndHint(kParamPosMatImportFileLabel);
            param->setStringType(eStringTypeFilePath);
            param->setFilePathExists(true);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (!OFX::getImageEffectHostDescription()->isNatron) { // Natron already has a reload button
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
            }
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }

        }
        if (!OFX::getImageEffectHostDescription()->isNatron) { // Natron already has a reload button
            PushButtonParamDescriptor* param = desc.definePushButtonParam(prefix + kParamPosMatImportFileReload);
            param->setLabelAndHint(kParamPosMatImportFileReloadLabel);
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }

        }
        {
            StringParamDescriptor* param = desc.defineStringParam(prefix + kParamPosMatExportChan);
            param->setLabelAndHint(kParamPosMatExportChanLabel);
            param->setStringType(eStringTypeFilePath);
            param->setFilePathExists(false);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (!OFX::getImageEffectHostDescription()->isNatron) { // Natron already has a rewrite button
                param->setLayoutHint(eLayoutHintNoNewLine, 1);
            }
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }

        }
        if (!OFX::getImageEffectHostDescription()->isNatron) { // Natron already has a rewrite button
            PushButtonParamDescriptor* param = desc.definePushButtonParam(prefix + kParamPosMatExportChanRewrite);
            param->setLabelAndHint(kParamPosMatExportChanRewriteLabel);
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }

        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(prefix + kParamPosMatTransformOrder);
        param->setLabelAndHint(kParamPosMatTransformOrderLabel);
        assert(param->getNOptions() == ePosMatTransformOrderSRT);
        param->appendOption(kParamPosMatTransformOrderOptionSRT);
        assert(param->getNOptions() == ePosMatTransformOrderSTR);
        param->appendOption(kParamPosMatTransformOrderOptionSTR);
        assert(param->getNOptions() == ePosMatTransformOrderRST);
        param->appendOption(kParamPosMatTransformOrderOptionRST);
        assert(param->getNOptions() == ePosMatTransformOrderRTS);
        param->appendOption(kParamPosMatTransformOrderOptionRTS);
        assert(param->getNOptions() == ePosMatTransformOrderTSR);
        param->appendOption(kParamPosMatTransformOrderOptionTSR);
        assert(param->getNOptions() == ePosMatTransformOrderTRS);
        param->appendOption(kParamPosMatTransformOrderOptionTRS);
        param->setDefault(kParamPosMatTransformOrderDefault);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(prefix + kParamPosMatRotationOrder);
        param->setLabelAndHint(kParamPosMatRotationOrderLabel);
        assert(param->getNOptions() == ePosMatRotationOrderXYZ);
        param->appendOption(kParamPosMatRotationOrderOptionXYZ);
        assert(param->getNOptions() == ePosMatRotationOrderXZY);
        param->appendOption(kParamPosMatRotationOrderOptionXZY);
        assert(param->getNOptions() == ePosMatRotationOrderYXZ);
        param->appendOption(kParamPosMatRotationOrderOptionYXZ);
        assert(param->getNOptions() == ePosMatRotationOrderYZX);
        param->appendOption(kParamPosMatRotationOrderOptionYZX);
        assert(param->getNOptions() == ePosMatRotationOrderZXY);
        param->appendOption(kParamPosMatRotationOrderOptionZXY);
        assert(param->getNOptions() == ePosMatRotationOrderZYX);
        param->appendOption(kParamPosMatRotationOrderOptionZYX);
        param->setDefault(kParamPosMatRotationOrderDefault);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double3DParamDescriptor* param = desc.defineDouble3DParam(prefix + kParamPosMatTranslate);
        param->setLabelAndHint(kParamPosMatTranslateLabel);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-10., -10., -10., 10., 10., 10.);
        param->setDefault(0, 0, (type == ePosMatCard) ? -1 : 0.);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double3DParamDescriptor* param = desc.defineDouble3DParam(prefix + kParamPosMatRotate);
        param->setLabelAndHint(kParamPosMatRotateLabel);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-180., -180., -180., 180., 180., 180.);
        param->setDefault(0., 0., 0.);
        param->setDoubleType(eDoubleTypeAngle);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double3DParamDescriptor* param = desc.defineDouble3DParam(prefix + kParamPosMatScale);
        param->setLabelAndHint(kParamPosMatScaleLabel);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0.01, 0.01, 0.01, 10., 10., 10.);
        param->setDefault(1., 1., 1.);
        param->setDoubleType(eDoubleTypeScale);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamPosMatUniformScale);
        param->setLabelAndHint(kParamPosMatUniformScaleLabel);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0.01, 10.);
        param->setDefault(1.);
        param->setDoubleType(eDoubleTypeScale);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double3DParamDescriptor* param = desc.defineDouble3DParam(prefix + kParamPosMatSkew);
        param->setLabelAndHint(kParamPosMatSkewLabel);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-1., -1., -1., 1., 1., 1.);
        param->setDefault(0., 0., 0.);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double3DParamDescriptor* param = desc.defineDouble3DParam(prefix + kParamPosMatPivot);
        param->setLabelAndHint(kParamPosMatPivotLabel);
        param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-10., -10., -10., 10., 10., 10.);
        param->setDefault(0., 0., 0.);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        GroupParamDescriptor* subgroup = desc.defineGroupParam(prefix + kGroupPosMatLocalMatrix);
        if (subgroup) {
            subgroup->setLabel(kGroupPosMatLocalMatrixLabel);
            subgroup->setOpen(false);
            if (group) {
                subgroup->setParent(*group);
            }
            if (page) {
                page->addChild(*subgroup);
            }
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(prefix + kParamPosMatUseMatrix);
            param->setLabelAndHint(kParamPosMatUseMatrixLabel);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (subgroup) {
                param->setParent(*subgroup);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamPosMatMatrix + (char)('1' + i) + (char)('1' + j));
                param->setLabelAndHint(kParamPosMatMatrixLabel);
                param->setRange(-DBL_MAX, DBL_MAX);
                param->setDisplayRange(-1., 1.);
                param->setDefault(i == j ? 1. : (i == 2 && j == 3) ? -1. : 0.);
                if (j < 3) {
                    param->setLayoutHint(eLayoutHintNoNewLine, 1);
                }
                if (subgroup) {
                    param->setParent(*subgroup);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }
    if (type == ePosMatCamera) {
        GroupParamDescriptor* subgroup = desc.defineGroupParam(kCameraCam kParamCameraProjectionGroup);
        if (subgroup) {
            subgroup->setLabel(kCameraCamLabel " " kParamCameraProjectionGroupLabel);
            subgroup->setOpen(false);
            if (group) {
                subgroup->setParent(*group);
            }
            if (page) {
                page->addChild(*subgroup);
            }
        }

        CameraParam::define(desc, page, subgroup, kCameraCam);
    }
}

void
PosMatParam::getMatrix(const double t, Matrix4x4* mat) const
{
    if (_useMatrix->getValueAtTime(t)) {
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                (*mat)(i,j) = _matrix[i][j]->getValueAtTime(t);
            }
        }

        return;
    }


    PosMatTransformOrderEnum transformOrder = (PosMatTransformOrderEnum)_transformOrder->getValueAtTime(t);
    PosMatRotationOrderEnum rotationOrder = (PosMatRotationOrderEnum)_rotationOrder->getValueAtTime(t);

    Matrix4x4 T;
    T(0,0) = T(1,1) = T(2,2) = T(3,3) = 1.;
    _translate->getValueAtTime(t, T(0,3), T(1,3), T(2,3));

    double theta[3];
    _rotate->getValueAtTime(t, theta[0], theta[1], theta[2]);
    Matrix4x4 R;
    if (theta[0] == 0. && theta[1] == 0. && theta[2] == 0.) {
        R(0,0) = R(1,1) = R(2,2) = R(3,3) = 1.;
    } else {
        Matrix4x4 Rx, Ry, Rz;
        Rx(3,3) = Ry(3,3) = Rz(3,3) = 1.;
        {
            double s = std::sin(theta[0] * M_PI / 180.);
            double c = std::cos(theta[0] * M_PI / 180.);
            Rx(0,0) = 1.;
            Rx(1,1) = c; Rx(1,2) = -s;
            Rx(2,1) = s; Rx(2,2) = c;
        }
        {
            double s = std::sin(theta[1] * M_PI / 180.);
            double c = std::cos(theta[1] * M_PI / 180.);
            Ry(1,1) = 1.;
            Ry(2,2) = c; Ry(2,0) = -s;
            Ry(0,2) = s; Ry(0,0) = c;
        }
        {
            double s = std::sin(theta[2] * M_PI / 180.);
            double c = std::cos(theta[2] * M_PI / 180.);
            Rz(2,2) = 1.;
            Rz(0,0) = c; Rz(0,1) = -s;
            Rz(1,0) = s; Rz(1,1) = c;
        }
        switch (rotationOrder) {
            case ePosMatRotationOrderXYZ:
                R = Rz * Ry * Rx;
                break;
            case ePosMatRotationOrderXZY:
                R = Ry * Rz * Rx;
                break;
            case ePosMatRotationOrderYXZ:
                R = Rz * Rx * Ry;
                break;
            case ePosMatRotationOrderYZX:
                R = Rx * Rz * Ry;
                break;
            case ePosMatRotationOrderZXY:
            default:
                R = Ry * Rx * Rz;
                break;
            case ePosMatRotationOrderZYX:
                R = Rx * Ry * Rz;
                break;
        }
    }

    // in Nuke, skew is just before the rotation, xhatever the RTS order is (strange, but true)
    double skew[3];
    _skew->getValueAtTime(t, skew[0], skew[1], skew[2]);
    if (skew[0] != 0. ||
        skew[1] != 0. ||
        skew[2] != 0.) {
        Matrix4x4 K;
        K(0,1) = std::tan(skew[0] * M_PI / 180.);
        K(1,0) = std::tan(skew[1] * M_PI / 180.);
        K(1,2) = std::tan(skew[2] * M_PI / 180.);
        K(0,0) = K(1,1) = K(2,2) = K(3,3) = 1;
        R = R * K;
    }

    Matrix4x4 S;
    S(3,3) = 1.;
    _scale->getValueAtTime(t, S(0,0), S(1,1), S(2,2));
    {
        double uniformScale = _uniformScale->getValueAtTime(t);
        S(0,0) *= uniformScale;
        S(1,1) *= uniformScale;
        S(2,2) *= uniformScale;

    }

    switch (transformOrder) {
        case ePosMatTransformOrderSRT:
        default:
            *mat = T * R * S;
            break;
        case ePosMatTransformOrderSTR:
            *mat = R * T * S;
            break;
        case ePosMatTransformOrderRST:
            *mat = T * S * R;
            break;
        case ePosMatTransformOrderRTS:
            *mat = S * T * R;
            break;
        case ePosMatTransformOrderTSR:
            *mat = R * S * T;
            break;
        case ePosMatTransformOrderTRS:
            *mat = S * R * T;
            break;
    }


    // pivot
    double pivot[3];
    _pivot->getValueAtTime(t, pivot[0], pivot[1], pivot[2]);
    if (pivot[0] != 0. ||
        pivot[1] != 0. ||
        pivot[2] != 0.) {
        // (reuse the T matrix)
        T(0,3) = pivot[0];
        T(1,3) = pivot[1];
        T(2,3) = pivot[2];
        Matrix4x4 P;
        P(0,3) = -pivot[0];
        P(1,3) = -pivot[1];
        P(2,3) = -pivot[2];
        P(0,0) = P(1,1) = P(2,2) = P(3,3) = 1;
        *mat = T * *mat * P;
    }
}

void
PosMatParam::changedParam(const InstanceChangedArgs &args,
                          const std::string &paramName)
{
    if (paramName.compare(0, _prefix.length(), _prefix) != 0) {
        return;
    }
    const double t = args.time;
    if ( args.reason == eChangeUserEdit &&
        (paramName == _importFile->getName() ||
          (_importFileReload && paramName == _importFileReload->getName()) ) ) {
        ImportFormatEnum format = (ImportFormatEnum)_importFormat->getValue();
        switch (format) {
        case eImportFormatChan:
            importChan();
            break;
        case eImportFormatBoujou:
            importBoujou();
            break;
        }
    } else if ( args.reason == eChangeUserEdit &&
                (paramName == _exportChan->getName() ||
                 (_exportChanRewrite && paramName == _exportChanRewrite->getName()) ) ) {
        exportChan();
    } else if ( paramName == _useMatrix->getName() ) {
        update();
    } else if (paramName == _transformOrder->getName() ||
               paramName == _rotationOrder->getName() ||
               paramName == _translate->getName() ||
               paramName == _rotate->getName() ||
               paramName == _scale->getName() ||
               paramName == _uniformScale->getName() ||
               paramName == _skew->getName() ||
               paramName == _pivot->getName() ||
               paramName == _useMatrix->getName()) {
        bool useMatrix = _useMatrix->getValueAtTime(t);
        if (!useMatrix) {
            Matrix4x4 mat;
            getMatrix(t, &mat);
            for (int i = 0; i < 4; ++i) {
                for (int j = 0; j < 4; ++j) {
                    _matrix[i][j]->setValue( mat(i,j) );
                }
            }
        }
    }
}

// END PosMatParm
////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class Card3DPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    Card3DPlugin(OfxImageEffectHandle handle)
        : Transform3x3Plugin(handle, false, eTransform3x3ParamsTypeMotionBlur)
        //, _transformAmount(NULL)
        , _interactive(NULL)
        , _srcClipChanged(NULL)
        , _axisCamera(NULL)
        , _camCamera(NULL)
        , _axisPosMat(NULL)
        , _camEnable(NULL)
        , _camPosMat(NULL)
        , _card(this, kGroupCard, ePosMatCard)
        , _lensInFocal(NULL)
        , _lensInHAperture(NULL)
        , _extent(NULL)
        , _format(NULL)
        , _formatSize(NULL)
        , _formatPar(NULL)
        , _btmLeft(NULL)
        , _size(NULL)
        , _recenter(NULL)
    {
        if (getImageEffectHostDescription()->supportsCamera) {
            _axisCamera = fetchCamera(kCameraAxis);
            _camCamera = fetchCamera(kCameraCam);
        } else {
            _axisPosMat = new PosMatParam(this, kCameraAxis, ePosMatAxis);
            _camEnable = fetchBooleanParam(kParamCamEnable);
            _camPosMat = new PosMatParam(this, kCameraCam, ePosMatCamera);
        }
        _lensInFocal = fetchDoubleParam(kParamLensInFocal);
        _lensInHAperture = fetchDoubleParam(kParamLensInHAperture);
        _extent = fetchChoiceParam(kParamOutputFormat);
        _format = fetchChoiceParam(kParamGeneratorFormat);
        _formatSize = fetchInt2DParam(kParamGeneratorSize);
        _formatPar= fetchDoubleParam(kParamGeneratorPAR);
        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _recenter = fetchPushButtonParam(kParamGeneratorCenter);

        //_transformAmount = fetchDoubleParam(kParamTransformAmount);
        _interactive = fetchBooleanParam(kParamTransformInteractive);
        assert(_interactive);
        _srcClipChanged = fetchBooleanParam(kParamSrcClipChanged);
        assert(_srcClipChanged);

        // honor kParamDefaultsNormalised
        if ( paramExists(kParamDefaultsNormalised) ) {
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

        updateVisibility();
    }

    ~Card3DPlugin()
    {
        delete _axisPosMat;
        delete _camPosMat;
    }

private:
    //virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    void updateVisibility();

    bool getOutputFormat(double time,
                         const OfxPointD& renderScale,
                         OfxRectI *format,
                         double *par) const;
    // NON-GENERIC
    //DoubleParam* _transformAmount;
    BooleanParam* _interactive;
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
    Camera* _axisCamera;
    Camera* _camCamera;
    PosMatParam* _axisPosMat;
    BooleanParam* _camEnable;
    PosMatParam* _camPosMat;
    PosMatParam _card;
    DoubleParam* _lensInFocal;
    DoubleParam* _lensInHAperture;

    // format params
    ChoiceParam* _extent;
    ChoiceParam* _format;
    Int2DParam* _formatSize;
    DoubleParam* _formatPar;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    PushButtonParam *_recenter;
};


// returns true if fixed format (i.e. not the input RoD) and setFormat can be called in getClipPrefs
bool
Card3DPlugin::getOutputFormat(double /*time*/,
                              const OfxPointD& renderScale,
                              OfxRectI *format,
                              double *par) const
{
    GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();

    switch (extent) {
        case eGeneratorExtentFormat: {
            int w, h;
            _formatSize->getValue(w, h);
            *par = _formatPar->getValue();
            format->x1 = format->y1 = 0;
            format->x2 = std::ceil(w * renderScale.x);
            format->y2 = std::ceil(h * renderScale.y);

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
            Coords::toPixelNearest(rod, renderScale, *par, format);

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
            Coords::toPixelNearest(rod, renderScale, *par, format);

            return true;
            break;
        }
        case eGeneratorExtentDefault:
        default:
            assert(false);
            /*
            if ( _srcClip && _srcClip->isConnected() ) {
                format->x1 = format->y1 = format->x2 = format->y2 = 0; // default value
                _srcClip->getFormat(*format);
                *par = _srcClip->getPixelAspectRatio();
                if ( OFX::Coords::rectIsEmpty(*format) ) {
                    // no format is available, use the RoD instead
                    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
                    Coords::toPixelNearest(srcRod, renderScale, *par, format);
                }
            } else {
                // default to Project Size
                OfxRectD srcRoD;
                OfxPointD siz = getProjectSize();
                OfxPointD off = getProjectOffset();
                srcRoD.x1 = off.x;
                srcRoD.x2 = off.x + siz.x;
                srcRoD.y1 = off.y;
                srcRoD.y2 = off.y + siz.y;
                *par = getProjectPixelAspectRatio();
                Coords::toPixelNearest(srcRoD, renderScale, *par, format);
            }
             */
            return false;
            break;
    }
    return false;
}

#if 0 // getRoD is done by ofxsTransform3x3
bool
Card3DPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    const double time = args.time;
    OfxRectI format = {0, 1, 0, 1};
    double par = 1.;
    getOutputFormat(time, args.renderScale, &format, &par);
    const OfxPointD rs1 = {1., 1.};
    OFX::Coords::toCanonical(format, rs1, par, &rod);

    return true;
}
#endif

void
Card3DPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    //We have to do this because the processing code does not support varying components for uvClip and srcClip
    PixelComponentEnum dstPixelComps = getDefaultOutputClipComponents();

    if (_srcClip) {
        clipPreferences.setClipComponents(*_srcClip, dstPixelComps);
    }
    OfxRectI format;
    double par;
    OfxPointD renderScale = {1., 1.};

    // we pass 0 as time, since anyway the input RoD is never used, thanks to the test on the return value
    bool setFormat = getOutputFormat(0, renderScale, &format, &par);
    if (setFormat) {
        clipPreferences.setOutputFormat(format);
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
    }
}

// overridden is identity
bool
Card3DPlugin::isIdentity(double /*time*/)
{
    // NON-GENERIC
    //double amount = _transformAmount->getValueAtTime(time);
    //if (amount == 0.) {
    //    return true;
    //}

    return false;
}

bool
Card3DPlugin::getInverseTransformCanonical(double time,
                                              int view,
                                              double /*amount*/,
                                              bool invert,
                                              Matrix3x3* invtransform) const
{
    Matrix4x4 axis;
    if (_axisCamera) {
        if (_axisCamera->isConnected()) {
            _axisCamera->getParameter(kNukeOfxCameraParamPositionMatrix, time, view, &axis(0,0), 16);
        } else {
            axis(0, 0) = axis(1,1) = axis(2,2) = axis(3,3) = 1.;
        }
    } else {
        _axisPosMat->getMatrix(time, &axis);
    }
    Matrix4x4 cam;
    CameraProjectionModeEnum camProjectionMode = eCameraProjectionModePerspective;
    double camFocal = 1.;
    double camHAperture = 1.; // only the ratio focal/haperture matters for card3d
    double camWinTranslate[2] = {0., 0.};
    double camWinScale[2] = {1., 1.};
    double camWinRoll = 0.;
    if (_camCamera) {
        if (_camCamera->isConnected()) {
            _camCamera->getParameter(kNukeOfxCameraParamPositionMatrix, time, view, &cam(0,0), 16);
            double projectionMode;
            _camCamera->getParameter(kNukeOfxCameraParamProjectionMode, time, view, &projectionMode, 1);
            camProjectionMode = (CameraProjectionModeEnum)((int)projectionMode);
            _camCamera->getParameter(kNukeOfxCameraParamFocalLength, time, view, &camFocal, 1);
            _camCamera->getParameter(kNukeOfxCameraParamHorizontalAperture, time, view, &camHAperture, 1);
            _camCamera->getParameter(kNukeOfxCameraParamWindowTranslate, time, view, camWinTranslate, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowScale, time, view, camWinScale, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowRoll, time, view, &camWinRoll, 1);
        }
    } else if (_camEnable->getValueAtTime(time)) {
        _camPosMat->getMatrix(time, &cam);
        _camPosMat->getProjection().getValueAtTime(time, camProjectionMode, camFocal, camHAperture, camWinTranslate[0], camWinTranslate[1], camWinScale[0], camWinScale[1], camWinRoll);
    } else {
        cam(0,0) = cam(1,1) = cam(2,2) = cam(3,3) = 1.;
    }
    Matrix4x4 card;
    _card.getMatrix(time, &card);


    // compose matrices
    Matrix4x4 invCam;
    if ( !cam.inverse(&invCam) ) {
        invCam(0,0) = invCam(1,1) = invCam(2,2) = invCam(3,3) = 1.;
    }
    Matrix4x4 pos = invCam * axis * card;

    // apply camera params
    Matrix3x3 mat;
    CameraParam::getMatrix(pos, camProjectionMode, camFocal, camHAperture, camWinTranslate[0], camWinTranslate[1], camWinScale[0], camWinScale[1], camWinRoll, &mat);

    double lensInFocal = _lensInFocal->getValueAtTime(time);
    double lensInHAperture = _lensInHAperture->getValueAtTime(time);
    double a = lensInHAperture / std::max(1e-8, lensInFocal);
    mat(0,0) *= a;
    mat(1,0) *= a;
    mat(2,0) *= a;
    mat(0,1) *= a;
    mat(1,1) *= a;
    mat(2,1) *= a;

    // mat is the direct transform, from source coords to output coords.
    // it is normalized for coordinates in (-0.5,0.5)x(-0.5*h/w,0.5*h/w) with y from to to bottom

    // get the input format (Natron only) or the input RoD (others)
    OfxRectD srcFormatCanonical;
    {
        OfxRectI srcFormat;
        _srcClip->getFormat(srcFormat);
        double par = _srcClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
            // no format is available, use the RoD instead
            srcFormatCanonical = _srcClip->getRegionOfDefinition(time);
        } else {
            const OfxPointD rs1 = {1., 1.};
            Coords::toCanonical(srcFormat, rs1, par, &srcFormatCanonical);
        }
    }

    OfxRectI dstFormat = {0, 1, 0, 1};
    double dstPar = 1.;
    const OfxPointD rs1 = {1., 1.};
    getOutputFormat(time, rs1, &dstFormat, &dstPar);
    OfxRectD dstFormatCanonical;
    OFX::Coords::toCanonical(dstFormat, rs1, dstPar, &dstFormatCanonical);

    Matrix3x3 N; // normalize source
    {
        double w = srcFormatCanonical.x2 - srcFormatCanonical.x1;
        //double h = srcFormatCanonical.y2 - srcFormatCanonical.y1;
        if (w == 0.) {
            return false;
        }
        N(0,0) = 1./w;
        N(0,2) = -(srcFormatCanonical.x1 + srcFormatCanonical.x2) / (2. * w);
        N(1,1) = 1./w;
        N(1,2) = -(srcFormatCanonical.y1 + srcFormatCanonical.y2) / (2. * w);
        N(2,2) = 1.;
    }

    Matrix3x3 D; // denormalize output
    {
        double w = dstFormatCanonical.x2 - dstFormatCanonical.x1;
        //double h = dstFormatCanonical.y2 - dstFormatCanonical.y1;
        D(0,0) = -w;
        D(0,2) = (dstFormatCanonical.x1 + dstFormatCanonical.x2) / 2.;
        D(1,1) = -w;
        D(1,2) = (dstFormatCanonical.y1 + dstFormatCanonical.y2) / 2.;
        D(2,2) = 1.;
    }

    mat = D * mat * N;

    if (invert) {
        (*invtransform) = mat;
    } else {
        if ( !mat.inverse(invtransform) ) {
            return false;
        }
    }

    return true;
} // Card3DPlugin::getInverseTransformCanonical


void
Card3DPlugin::updateVisibility()
{
    if (_camEnable) {
        bool enabled = _camEnable->getValue();
        _camPosMat->setEnabled(enabled);
    }
    {
        GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();
        bool hasFormat = (extent == eGeneratorExtentFormat);
        bool hasSize = (extent == eGeneratorExtentSize);

        _format->setIsSecretAndDisabled(!hasFormat);
        _size->setIsSecretAndDisabled(!hasSize);
        _recenter->setIsSecretAndDisabled(!hasSize);
        _btmLeft->setIsSecretAndDisabled(!hasSize);
    }
}

void
Card3DPlugin::changedParam(const InstanceChangedArgs &args,
                           const std::string &paramName)
{
    //const double time = args.time;
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _srcClipChanged->setValue(true);
    } else if (paramName == kParamCamEnable) {
        updateVisibility();
    } else if (paramName == kParamOutputFormat) {
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
    } else {
        if (_axisPosMat) {
            _axisPosMat->changedParam(args, paramName);
        }
        if (_camPosMat) {
            _camPosMat->changedParam(args, paramName);
        }
        _card.changedParam(args, paramName);
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
Card3DPlugin::changedClip(const InstanceChangedArgs &args,
                             const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         ( args.reason == eChangeUserEdit) ) {
        //resetCenter(args.time);
    }
}

mDeclarePluginFactory(Card3DPluginFactory, {ofxsThreadSuiteCheck();}, {});


void
Card3DPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    //desc.setOverlayInteractDescriptor(new TransformOverlayDescriptorOldParams);
}



void
Card3DPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    if (getImageEffectHostDescription()->supportsCamera) {
        {
            CameraDescriptor* camera = desc.defineCamera(kCameraCam);
            camera->setLabel(kCameraCamLabel);
            camera->setOptional(true);
        }
        {
            CameraDescriptor* camera = desc.defineCamera(kCameraAxis);
            camera->setLabel(kCameraAxisLabel);
            camera->setOptional(true);
        }
    } else {
        {
            GroupParamDescriptor* group = desc.defineGroupParam(kCameraAxis);
            group->setLabel(kCameraAxisLabel);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
            PosMatParam::define(desc, page, group, kCameraAxis, ePosMatAxis);
        }
        {
            GroupParamDescriptor* group = desc.defineGroupParam(kCameraCam);
            group->setLabel(kCameraCamLabel);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }

            {
                BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCamEnable);
                param->setLabelAndHint(kParamCamEnableLabel);
                param->setDefault(false);
                param->setAnimates(false);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }

            PosMatParam::define(desc, page, group, kCameraCam, ePosMatCamera);
        }
    }

    PosMatParam::define(desc, page, /*group=*/NULL, /*prefix=*/kGroupCard, ePosMatCard);

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLensInFocal);
        param->setLabelAndHint(kParamLensInFocalLabel);
        param->setDefault(1.);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(1e-8, 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLensInHAperture);
        param->setLabelAndHint(kParamLensInHApertureLabel);
        param->setDefault(1.);
        param->setRange(1e-8, DBL_MAX);
        param->setDisplayRange(1e-8, 1.);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }

    { // Frame format
        // extent
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputFormat);
            param->setLabelAndHint(kParamOutputFormatLabel);
            assert(param->getNOptions() == eGeneratorExtentFormat);
            param->appendOption(kParamGeneratorExtentOptionFormat);
            assert(param->getNOptions() == eGeneratorExtentSize);
            param->appendOption(kParamGeneratorExtentOptionSize);
            assert(param->getNOptions() == eGeneratorExtentProject);
            param->appendOption(kParamGeneratorExtentOptionProject);
            //assert(param->getNOptions() == eGeneratorExtentDefault);
            //param->appendOption(kParamGeneratorExtentOptionDefault);
            param->setDefault(eGeneratorExtentProject);
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
                param->setLabel(kParamGeneratorSizeLabel);
                param->setHint(kParamGeneratorSizeHint);
                param->setIsSecretAndDisabled(true);
                param->setDefault(w, h);
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGeneratorPAR);
                param->setLabel(kParamGeneratorPARLabel);
                param->setHint(kParamGeneratorPARHint);
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
            param->setLabel(kParamRectangleInteractBtmLeftLabel);
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
            param->setHint("Coordinates of the bottom left corner of the size rectangle.");
            param->setDigits(0);
            if (page) {
                page->addChild(*param);
            }
        }

        // size
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
            param->setLabel(kParamRectangleInteractSizeLabel);
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
            param->setHint("Width and height of the size rectangle.");
            param->setIncrement(1.);
            param->setDigits(0);
            if (page) {
                page->addChild(*param);
            }
        }

    }

    Transform3x3DescribeInContextEnd(desc, context, page, false, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformInteractive);
        param->setLabel(kParamTransformInteractiveLabel);
        param->setHint(kParamTransformInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
Card3DPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new Card3DPlugin(handle);
}


static Card3DPluginFactory p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
