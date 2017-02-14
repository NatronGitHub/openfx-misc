/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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

#include <cmath>
#include <iostream>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsTransform3x3.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

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

////////////////////////////////////////////////////////////////////////////////
// BEGIN PosMatParm

#define kParamPosMatTransformOrder "XformOrder"
#define kParamPosMatTransformOrderLabel "Transform Order", "Order in which scale (S), rotation (R) and translation (T) are applied."
#define kParamPosMatTransformOrderOptionSRT "SRT", "Scale, Rotation, Translation."
#define kParamPosMatTransformOrderOptionSTR "STR", "Scale, Translation, Rotation."
#define kParamPosMatTransformOrderOptionRST "RST", "Rotation, Scale, Translation."
#define kParamPosMatTransformOrderOptionRTS "RTS", "Rotation, Translation, Scale."
#define kParamPosMatTransformOrderOptionTSR "TSR", "Translation, Scale, Rotation."
#define kParamPosMatTransformOrderOptionTRS "TRS", "Translation, Rotation, Scale."
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
#define kParamPosMatRotationOrderOptionXYZ "XYZ"
#define kParamPosMatRotationOrderOptionXZY "XZR"
#define kParamPosMatRotationOrderOptionYXZ "YXZ"
#define kParamPosMatRotationOrderOptionYZX "YZX"
#define kParamPosMatRotationOrderOptionZXY "ZXY"
#define kParamPosMatRotationOrderOptionZYX "ZYX"
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
    std::string _prefix;
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
    bool _enabled;

public:
    PosMatParam(OFX::ImageEffect* parent, const std::string& prefix)
    : _prefix(prefix)
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
    , _enabled(true)
    {
        _transformOrder = parent->fetchChoiceParam(prefix + kParamPosMatTransformOrder);
        _rotationOrder = parent->fetchChoiceParam(prefix + kParamPosMatRotationOrder);
        _translate = parent->fetchDouble3DParam(prefix + kParamPosMatTranslate);
        _rotate = parent->fetchDouble3DParam(prefix + kParamPosMatRotate);
        _scale = parent->fetchDouble3DParam(prefix + kParamPosMatScale);
        _uniformScale = parent->fetchDoubleParam(prefix + kParamPosMatUniformScale);
        _skew = parent->fetchDouble3DParam(prefix + kParamPosMatSkew);
        _pivot = parent->fetchDouble3DParam(prefix + kParamPosMatPivot);
        _localMatrix = parent->fetchGroupParam(prefix + kGroupPosMatLocalMatrix);
        _useMatrix = parent->fetchBooleanParam(prefix + kParamPosMatUseMatrix);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                _matrix[i][j] = parent->fetchDoubleParam(prefix + kParamPosMatMatrix + (char)('1' + i) + (char)('1' + j));
            }
        }
        update();
    }

    void changedParam(const InstanceChangedArgs &args, const std::string &paramName);

    void getMatrix(double time, Matrix4x4* mat) const;

    static void define(ImageEffectDescriptor &desc,
                       PageParamDescriptor *page,
                       GroupParamDescriptor *group,
                       const std::string prefix,
                       bool isCard); //!< affects the default z translation

    void setEnabled(bool enabled) { _enabled = enabled; update(); }

private:

    /// update visibility/enabledness
    void update()
    {
        bool useMatrix = _useMatrix->getValue();
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
    }

};



void
PosMatParam::define(ImageEffectDescriptor &desc,
                    PageParamDescriptor *page,
                    GroupParamDescriptor *group,
                    const std::string prefix,
                    bool isCard)
{
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
        param->setDefault(0, 0, isCard ? -1 : 0.);
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
        subgroup->setLabel(kGroupPosMatLocalMatrixLabel);
        subgroup->setOpen(false);
        if (group) {
            subgroup->setParent(*group);
        }
        if (page) {
            page->addChild(*subgroup);
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
    if ( paramName == _useMatrix->getName() ) {
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
// BEGIN CameraParm

// Camera Projection parameters


#define kParamCameraProjectionGroup "Projection"
#define kParamCameraProjectionGroupLabel "Projection"

#define kParamCameraProjectionMode kNukeOfxCameraParamProjectionMode
#define kParamCameraProjectionModeLabel "Projection"
#define kParamCameraProjectionModeOptionPerspective "Perspective"
#define kParamCameraProjectionModeOptionOrthographic "Orthographic"
#define kParamCameraProjectionModeOptionUV "UV"
#define kParamCameraProjectionModeOptionSpherical "Spherical"
enum CameraProjectionModeEnum {
    eCameraProjectionModePerspective = 0,
    eCameraProjectionModeOrthographic,
    eCameraProjectionModeUV,
    eCameraProjectionModeSpherical,
};
#define kParamCameraFocalLength kNukeOfxCameraParamFocalLength
#define kParamCameraFocalLengthLabel "Focal Length"
#define kParamCameraHorizontalAperture kNukeOfxCameraParamHorizontalAperture
#define kParamCameraHorizontalApertureLabel "Horiz. Aperture"
#define kParamCameraVerticalAperture kNukeOfxCameraParamVerticalAperture
#define kParamCameraVerticalApertureLabel "Vert. Aperture"
#define kParamCameraNear kNukeOfxCameraParamNear
#define kParamCameraNearLabel "Near"
#define kParamCameraFar kNukeOfxCameraParamFar
#define kParamCameraFarLabel "Far"
#define kParamCameraWindowTranslate kNukeOfxCameraParamWindowTranslate
#define kParamCameraWindowTranslateLabel "Window Translate"
#define kParamCameraWindowScale kNukeOfxCameraParamWindowScale
#define kParamCameraWindowScaleLabel "Window Scale"
#define kParamCameraWindowRoll kNukeOfxCameraParamWindowRoll
#define kParamCameraWindowRollLabel "Window Roll"
#define kParamCameraFocalPoint kNukeOfxCameraParamFocalPoint
#define kParamCameraFocalPointLabel "Focus Distance"
#define kParamCameraFStop "fstop"
#define kParamCameraFStopLabel "F-Stop"

class CameraParam {
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
        param->setLabel(kParamCameraFocalLengthLabel);
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
        param->setLabel(kParamCameraHorizontalApertureLabel);
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
    /*
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraVerticalAperture);
        param->setLabel(kParamCameraVerticalApertureLabel);
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
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraNear);
        param->setLabel(kParamCameraNearLabel);
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
        param->setLabel(kParamCameraFarLabel);
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
        param->setLabel(kParamCameraWindowTranslateLabel);
        param->setRange(-1, -1, 1, 1);
        param->setDisplayRange(-1, -1, 1, 1);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(prefix + kParamCameraWindowScale);
        param->setLabel(kParamCameraWindowScaleLabel);
        param->setRange(1e-8, 1e-8, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setDefault(1, 1);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(prefix + kParamCameraWindowRoll);
        param->setLabel(kParamCameraWindowRollLabel);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-45, 45);
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
        param->setLabel(kParamCameraFocalPointLabel);
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
        param->setLabel(kParamCameraFStopLabel);
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
    (*mat)(0,0) = pos(0,0); (*mat)(0,1) = pos(0,1); (*mat)(0,2) = pos(0,3);
    (*mat)(1,0) = pos(1,0); (*mat)(1,1) = pos(1,1); (*mat)(1,2) = pos(1,3);
    if (projectionMode == eCameraProjectionModePerspective) {
        // divide by Z
        (*mat)(2,0) = a * pos(2,0); (*mat)(2,1) = a * pos(2,1); (*mat)(2,2) = a * pos(2,3);
    } else {
        // orthographic
        (*mat)(2,0) = -a * pos(3,0); (*mat)(2,1) = -a * pos(3,1); (*mat)(2,2) = -a * pos(3,3);
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
        , _camProjectionGroup(NULL)
        , _camProjection(NULL)
        , _card(this, kGroupCard)
        , _lensInFocal(NULL)
        , _lensInHAperture(NULL)
#warning "TODO: params for output format"
    {
        if (getImageEffectHostDescription()->supportsCamera) {
            _axisCamera = fetchCamera(kCameraAxis);
            _camCamera = fetchCamera(kCameraCam);
        } else {
            _axisPosMat = new PosMatParam(this, kCameraAxis);
            _camEnable = fetchBooleanParam(kParamCamEnable);
            _camPosMat = new PosMatParam(this, kCameraCam);
            _camProjectionGroup = fetchGroupParam(kCameraCam kParamCameraProjectionGroup);
            _camProjection = new CameraParam(this, kCameraCam);
        }
        _lensInFocal = fetchDoubleParam(kParamLensInFocal);
        _lensInHAperture = fetchDoubleParam(kParamLensInHAperture);
#warning "TODO: params for output format"

        //_transformAmount = fetchDoubleParam(kParamTransformAmount);
        _interactive = fetchBooleanParam(kParamTransformInteractive);
        assert(_interactive);
        _srcClipChanged = fetchBooleanParam(kParamSrcClipChanged);
        assert(_srcClipChanged);

        if (_camEnable) {
            bool enabled = _camEnable->getValue();
            _camPosMat->setEnabled(enabled);
            _camProjectionGroup->setEnabled(enabled);
            _camProjection->setEnabled(enabled);
        }
    }

    ~Card3DPlugin()
    {
        delete _axisPosMat;
        delete _camPosMat;
        delete _camProjection;
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // NON-GENERIC
    //DoubleParam* _transformAmount;
    BooleanParam* _interactive;
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
    Camera* _axisCamera;
    Camera* _camCamera;
    PosMatParam* _axisPosMat;
    BooleanParam* _camEnable;
    PosMatParam* _camPosMat;
    GroupParam* _camProjectionGroup;
    CameraParam* _camProjection;
    PosMatParam _card;
    DoubleParam* _lensInFocal;
    DoubleParam* _lensInHAperture;
#warning "TODO: params for output format"
};

// overridden is identity
bool
Card3DPlugin::isIdentity(double time)
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
            camProjectionMode = (CameraProjectionModeEnum)projectionMode;
            _camCamera->getParameter(kNukeOfxCameraParamFocalLength, time, view, &camFocal, 1);
            _camCamera->getParameter(kNukeOfxCameraParamHorizontalAperture, time, view, &camHAperture, 1);
            _camCamera->getParameter(kNukeOfxCameraParamWindowTranslate, time, view, camWinTranslate, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowScale, time, view, camWinScale, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowRoll, time, view, &camWinRoll, 1);
        }
    } else if (_camEnable->getValueAtTime(time)) {
        _camPosMat->getMatrix(time, &axis);
        _camProjection->getValueAtTime(time, camProjectionMode, camFocal, camHAperture, camWinTranslate[0], camWinTranslate[1], camWinScale[0], camWinScale[1], camWinRoll);
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

#warning "TODO: apply in-lens aperture and focal"

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
#warning "TODO: params for output format"

    OfxRectD dstFormatCanonical = srcFormatCanonical;

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
Card3DPlugin::changedParam(const InstanceChangedArgs &args,
                           const std::string &paramName)
{
    const double time = args.time;
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _srcClipChanged->setValue(true);
    } else if (paramName == kParamCamEnable) {
        bool enabled = _camEnable->getValueAtTime(time);
        _camPosMat->setEnabled(enabled);
        _camProjectionGroup->setEnabled(enabled);
        _camProjection->setEnabled(enabled);
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
            PosMatParam::define(desc, page, group, kCameraAxis, false);
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

            PosMatParam::define(desc, page, group, kCameraCam, false);

            {
                GroupParamDescriptor* subgroup = desc.defineGroupParam(kCameraCam kParamCameraProjectionGroup);
                subgroup->setLabel(kCameraCamLabel" "kParamCameraProjectionGroupLabel);
                subgroup->setOpen(false);
                if (group) {
                    subgroup->setParent(*group);
                }
                if (page) {
                    page->addChild(*subgroup);
                }

                CameraParam::define(desc, page, group, kCameraCam);
            }
        }
    }

    PosMatParam::define(desc, page, /*group=*/NULL, /*prefix=*/kGroupCard, /*isCard=*/true);

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
        if (page) {
            page->addChild(*param);
        }
    }

#warning "TODO: params for output format"

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
