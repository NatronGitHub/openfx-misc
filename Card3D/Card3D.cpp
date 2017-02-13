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

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "Card3DOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Card3D.\n" \
    "This plugin concatenates transforms.\n" \
    "See also http://opticalenquiry.com/nuke/index.php?title=Transform"

#define kPluginIdentifier "net.sf.openfx.Card3D"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamSrcClipChanged "srcClipChanged"


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
    BooleanParam* _useMatrix;
    DoubleParam* _matrix[4][4];

public:
    PosMatParam(OFX::ImageEffect* parent, const std::string& prefix)
    : _prefix(prefix)
    {
        _transformOrder = parent->fetchChoiceParam(prefix + kParamPosMatTransformOrder);
        _rotationOrder = parent->fetchChoiceParam(prefix + kParamPosMatRotationOrder);
        _translate = parent->fetchDouble3DParam(prefix + kParamPosMatTranslate);
        _rotate = parent->fetchDouble3DParam(prefix + kParamPosMatRotate);
        _scale = parent->fetchDouble3DParam(prefix + kParamPosMatScale);
        _uniformScale = parent->fetchDoubleParam(prefix + kParamPosMatUniformScale);
        _skew = parent->fetchDouble3DParam(prefix + kParamPosMatSkew);
        _pivot = parent->fetchDouble3DParam(prefix + kParamPosMatPivot);
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

    /// update visibility/enabledness
    void update()
    {
        bool useMatrix = _useMatrix->getValue();
        _transformOrder->setEnabled(!useMatrix);
        _rotationOrder->setEnabled(!useMatrix);
        _translate->setEnabled(!useMatrix);
        _rotate->setEnabled(!useMatrix);
        _scale->setEnabled(!useMatrix);
        _uniformScale->setEnabled(!useMatrix);
        _skew->setEnabled(!useMatrix);
        _pivot->setEnabled(!useMatrix);
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                _matrix[i][j]->setEnabled(useMatrix);
            }
        }
    }

    static void define(ImageEffectDescriptor &desc,
                       PageParamDescriptor *page,
                       GroupParamDescriptor *group,
                       const std::string prefix);

};



void
PosMatParam::define(ImageEffectDescriptor &desc,
                    PageParamDescriptor *page,
                    GroupParamDescriptor *group,
                    const std::string prefix)
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
        param->setDefault(0, 0, -1);
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
        , _camPosMat(NULL)
        , _card(this, kGroupCard)
    {
        if (getImageEffectHostDescription()->supportsCamera) {
            _axisCamera = fetchCamera(kCameraAxis);
            _camCamera = fetchCamera(kCameraCam);
        } else {
            _axisPosMat = new PosMatParam(this, kCameraAxis);
            _camPosMat = new PosMatParam(this, kCameraCam);
        }
        // NON-GENERIC
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
                /*
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
                 */
            }
        }
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
    PosMatParam* _camPosMat;
    // TODO: other camera parameters
    PosMatParam _card;
    // TODO: other card parameters (haperture, focal, format)
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
                                              double amount,
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
    bool camPerspective = true;
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
            camPerspective = (projectionMode == kNukeOfxCameraProjectionModePerspective); // all other projections are considered as ortho
            _camCamera->getParameter(kNukeOfxCameraParamFocalLength, time, view, &camFocal, 1);
            _camCamera->getParameter(kNukeOfxCameraParamHorizontalAperture, time, view, &camHAperture, 1);
            _camCamera->getParameter(kNukeOfxCameraParamWindowTranslate, time, view, camWinTranslate, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowScale, time, view, camWinScale, 2);
            _camCamera->getParameter(kNukeOfxCameraParamWindowRoll, time, view, &camWinRoll, 1);
        }
    } else {
        _camPosMat->getMatrix(time, &axis);
        //camPerspective = (eProjectionMode)_camProjectionMode->getValueAtTime(time) == eProjectionModePerspective;
    }
    Matrix4x4 card;
    _card.getMatrix(time, &card);

    // TODO: compose matrices

    // TODO: in-lens aperture and focal

    Matrix3x3 mat;
    mat(0,0) = card(0,0); mat(0,1) = card(0,1); mat(0,2) = card(0,3);
    mat(1,0) = card(1,0); mat(1,1) = card(1,1); mat(1,2) = card(1,3);
    mat(0,0) = card(0,0); mat(0,1) = card(0,1); mat(0,2) = card(0,3);
    //if (invert) {
    //    *inv
    //    (*invtransform)(0,0 = )

    // NON-GENERIC
    //if (_transformAmount) {
    //    amount *= _transformAmount->getValueAtTime(time);
    //}

    if (amount != 1.) {
        //translate.x *= amount;
        //...
    }

    if (!invert) {
        //*invtransform = ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    } else {
        //*invtransform = ofxsMatTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    }

    return true;
} // Card3DPlugin::getInverseTransformCanonical


void
Card3DPlugin::changedParam(const InstanceChangedArgs &args,
                           const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _srcClipChanged->setValue(true);
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
            PosMatParam::define(desc, page, group, kCameraAxis);
        }
        {
            GroupParamDescriptor* group = desc.defineGroupParam(kCameraCam);
            group->setLabel(kCameraCamLabel);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
            PosMatParam::define(desc, page, group, kCameraCam);
            // TODO: add other cam params, see camera.h
        }
    }

    PosMatParam::define(desc, page, /*group=*/NULL, /*prefix=*/kGroupCard);

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

    // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
    if (!gHostSupportsDefaultCoordinateSystem) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDefaultsNormalised);
        param->setDefault(true);
        param->setEvaluateOnChange(false);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(true);
        param->setAnimates(false);
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
