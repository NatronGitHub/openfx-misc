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
 * OFX CornerPin plugin.
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

#include <cmath>
#include <cfloat> // DBL_MAX
#include <vector>
#ifdef DEBUG
#include <iostream>
#endif

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

#include "ofxsOGLTextRenderer.h"
#include "ofxsTransform3x3.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "CornerPinOFX"
#define kPluginMaskedName "CornerPinMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription \
    "Allows an image to fit another in translation, rotation and scale.\n" \
    "The resulting transform is a translation if 1 point is enabled, a " \
    "similarity if 2 are enabled, an affine transform if 3 are enabled, " \
    "and a homography if they are all enabled.\n" \
    "\n" \
    "An effect where an image transitions from a full-frame image to an image " \
    "placed on a billboard or a screen, or a crash zoom effect, can be obtained " \
    "by combining the Transform and CornerPin effects and using the Amount " \
    "parameter on both effects.\n" \
    "Apply a CornerPin followed by a Transform effect (the order is important) " \
    "and visualize the output superimposed on " \
    "the target image. While leaving the value of the Amount parameter at 1, " \
    "tune the Transform parameters (including Scale and Skew) so that the " \
    "transformed image is as close as possible to the desired target location.\n" \
    "Then, adjust the 'to' points of the CornerPin effect (which " \
    "should be affected by the Transform) so that the warped image perfectly matches the " \
    "desired target location. Link the Amount parameter of the Transform and " \
    "CornerPin effects.\n" \
    "Finally, by animating the Amount parameter of both effects from 0 to 1, " \
    "the image goes progressively, and with minimal deformations, from " \
    "full-frame to the target location, creating the desired effect (motion " \
    "blur can be added on the Transform node, too).\n" \
    "Note that if only the CornerPin effect is used instead of combining " \
    "CornerPin and Transform, the position of the CornerPin points is linearly " \
    "interpolated between their 'from' position and their 'to' position, which " \
    "may result in unrealistic image motion, where the image shrinks and " \
    "expands, especially when the image rotates.\n" \
    "\n" \
    "This plugin concatenates transforms.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=CornerPin"

#define kPluginIdentifier "net.sf.openfx.CornerPinPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.CornerPinMaskedPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define POINT_SIZE 5
#define POINT_TOLERANCE 6

#define kGroupTo "to"
#define kGroupToLabel "To"
static const char* const kParamTo[4] = {
    "to1",
    "to2",
    "to3",
    "to4"
};
static const char* const kParamEnable[4] = {
    "enable1",
    "enable2",
    "enable3",
    "enable4"
};
#define kParamEnableHint "Enables the point on the left."

#define kGroupFrom "from"
#define kGroupFromLabel "From"
static const char* const kParamFrom[4] = {
    "from1",
    "from2",
    "from3",
    "from4"
};


#define kParamCopyFrom "copyFrom"
#define kParamCopyFromLabel "Copy \"From\""
#define kParamCopyFromHint "Copy the contents (including animation) of the \"from\" points to the \"to\" points."

#define kParamCopyFromSingle "copyFromSingle"
#define kParamCopyFromSingleLabel "Copy \"From\" (Single)"
#define kParamCopyFromSingleHint "Copy the current values of the \"from\" points to the \"to\" points."

#define kParamCopyTo "copyTo"
#define kParamCopyToLabel "Copy \"To\""
#define kParamCopyToHint "Copy the contents (including animation) of the \"to\" points to the \"from\" points."

#define kParamCopyToSingle "copyToSingle"
#define kParamCopyToSingleLabel "Copy \"To\" (Single)"
#define kParamCopyToSingleHint "Copy the current values of the \"to\" points to the \"from\" points."

#define kParamCopyInputRoD "setToInputRod"
#define kParamCopyInputRoDLabel "Set to input rod"
#define kParamCopyInputRoDHint "Copy the values from the source region of definition into the \"from\" points."

#define kParamOverlayPoints "overlayPoints"
#define kParamOverlayPointsLabel "Overlay Points"
#define kParamOverlayPointsHint "Whether to display the \"from\" or the \"to\" points in the overlay"
#define kParamOverlayPointsOptionTo "To", "Display the \"to\" points.", "to"
#define kParamOverlayPointsOptionFrom "From", "Display the \"from\" points.", "from"

#define kParamTransformAmount "transformAmount"
#define kParamTransformAmountLabel "Amount"
#define kParamTransformAmountHint "Amount of transform to apply (excluding the extra matrix, which is always applied). 0 means the transform is identity, 1 means to apply the full transform. Intermediate transforms are computed by linear interpolation between the 'from' and the 'to' points. See the plugin description on how to use the amount parameter for a crash zoom effect."

#define kGroupExtraMatrix "transformMatrix"
#define kGroupExtraMatrixLabel "Extra Matrix"
#define kGroupExtraMatrixHint "This matrix gets concatenated to the transform defined by the other parameters."
#define kParamExtraMatrixRow1 "transform_row1"
#define kParamExtraMatrixRow2 "transform_row2"
#define kParamExtraMatrixRow3 "transform_row3"

#define kParamTransformInteractive "interactive"
#define kParamTransformInteractiveLabel "Interactive Update"
#define kParamTransformInteractiveHint "If checked, update the parameter values during interaction with the image viewer, else update the values when pen is released."

#define kParamSrcClipChanged "srcClipChanged"

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

#define POINT_INTERACT_LINE_SIZE_PIXELS 20

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CornerPinPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    CornerPinPlugin(OfxImageEffectHandle handle,
                    bool masked)
        : Transform3x3Plugin(handle, masked, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur)
        , _transformAmount(NULL)
        , _extraMatrixRow1(NULL)
        , _extraMatrixRow2(NULL)
        , _extraMatrixRow3(NULL)
        , _srcClipChanged(NULL)
    {
        // NON-GENERIC
        for (int i = 0; i < 4; ++i) {
            _to[i] = fetchDouble2DParam(kParamTo[i]);
            _enable[i] = fetchBooleanParam(kParamEnable[i]);
            _from[i] = fetchDouble2DParam(kParamFrom[i]);
            assert(_to[i] && _enable[i] && _from[i]);
        }
        _transformAmount = fetchDoubleParam(kParamTransformAmount);
        _extraMatrixRow1 = fetchDouble3DParam(kParamExtraMatrixRow1);
        _extraMatrixRow2 = fetchDouble3DParam(kParamExtraMatrixRow2);
        _extraMatrixRow3 = fetchDouble3DParam(kParamExtraMatrixRow3);
        assert(_extraMatrixRow1 && _extraMatrixRow2 && _extraMatrixRow3);

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
                for (int i = 0; i < 4; ++i) {
                    p = _to[i]->getValue();
                    _to[i]->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                    p = _from[i]->getValue();
                    _from[i]->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                }
                param->setValue(false);
                endEditBlock();
            }
        }
    }

private:

    Matrix3x3 getExtraMatrix(OfxTime time) const
    {
        Matrix3x3 ret;

        _extraMatrixRow1->getValueAtTime(time, ret(0,0), ret(0,1), ret(0,2));
        _extraMatrixRow2->getValueAtTime(time, ret(1,0), ret(1,1), ret(1,2));
        _extraMatrixRow3->getValueAtTime(time, ret(2,0), ret(2,1), ret(2,2));

        return ret;
    }

    bool getHomography(OfxTime time, const OfxPointD & scale,
                       bool inverseTransform,
                       const Point3D & p1,
                       const Point3D & p2,
                       const Point3D & p3,
                       const Point3D & p4,
                       Matrix3x3 & m);
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // NON-GENERIC
    Double2DParam* _to[4];
    BooleanParam* _enable[4];
    DoubleParam* _transformAmount;
    Double3DParam* _extraMatrixRow1;
    Double3DParam* _extraMatrixRow2;
    Double3DParam* _extraMatrixRow3;
    Double2DParam* _from[4];
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
};


bool
CornerPinPlugin::getInverseTransformCanonical(OfxTime time,
                                              int /*view*/,
                                              double amount,
                                              bool invert,
                                              Matrix3x3* invtransform) const
{
    // in this new version, both from and to are enabled/disabled at the same time
    bool enable[4];
    Point3D p[2][4];
    int f = invert ? 0 : 1;
    int t = invert ? 1 : 0;
    int k = 0;

    for (int i = 0; i < 4; ++i) {
        _enable[i]->getValueAtTime(time, enable[i]);
        if (enable[i]) {
            _from[i]->getValueAtTime(time, p[f][k].x, p[f][k].y);
            _to[i]->getValueAtTime(time, p[t][k].x, p[t][k].y);
            ++k;
        }
        p[0][i].z = p[1][i].z = 1.;
    }

    amount *= _transformAmount->getValueAtTime(time);

    if (amount != 1.) {
        int k = 0;
        for (int i = 0; i < 4; ++i) {
            if (enable[i]) {
                p[t][k].x = p[f][k].x + amount * (p[t][k].x - p[f][k].x);
                p[t][k].y = p[f][k].y + amount * (p[t][k].y - p[f][k].y);
                ++k;
            }
        }
    }

    // k contains the number of valid points
    Matrix3x3 homo3x3;
    bool success = false;

    assert(0 <= k && k <= 4);
    if (k == 0) {
        // no points, only apply extraMat;
        *invtransform = getExtraMatrix(time);

        return true;
    }

    switch (k) {
    case 4:
        success = homo3x3.setHomographyFromFourPoints(p[0][0], p[0][1], p[0][2], p[0][3], p[1][0], p[1][1], p[1][2], p[1][3]);
        break;
    case 3:
        success = homo3x3.setAffineFromThreePoints(p[0][0], p[0][1], p[0][2], p[1][0], p[1][1], p[1][2]);
        break;
    case 2:
        success = homo3x3.setSimilarityFromTwoPoints(p[0][0], p[0][1], p[1][0], p[1][1]);
        break;
    case 1:
        success = homo3x3.setTranslationFromOnePoint(p[0][0], p[1][0]);
        break;
    }
    if (!success) {
        ///cannot compute the homography when 3 points are aligned
        return false;
    }

    // make sure that p[0][0].xy transforms to a positive z. (before composing with the extra matrix)
    Point3D p0(p[0][0].x, p[0][0].y, 1.);
    if ( (homo3x3 * p0).z < 0.) {
        homo3x3 *= -1;
    }

    Matrix3x3 extraMat = getExtraMatrix(time);
    *invtransform = homo3x3 * extraMat;

    return true;
} // CornerPinPlugin::getInverseTransformCanonical

// overridden is identity
bool
CornerPinPlugin::isIdentity(double time)
{
    Matrix3x3 extraMat = getExtraMatrix(time);

    if ( !extraMat.isIdentity() ) {
        return false;
    }

    // extraMat is identity.

    // check if amount is zero
    if (_paramsType != eTransform3x3ParamsTypeDirBlur) {
        double amount = _transformAmount->getValueAtTime(time);
        if (amount == 0.) {
            return true;
        }
    }

    // The transform is identity either if no point is enabled, or if
    // all enabled from's are equal to their counterpart to
    for (int i = 0; i < 4; ++i) {
        bool enable;
        _enable[i]->getValueAtTime(time, enable);
        if (enable) {
            OfxPointD p, q;
            _from[i]->getValueAtTime(time, p.x, p.y);
            _to[i]->getValueAtTime(time, q.x, q.y);
            if ( (p.x != q.x) || (p.y != q.y) ) {
                return false;
            }
        }
    }

    return true;
}

static void
copyPoint(Double2DParam* from,
          Double2DParam* to)
{
    // because some hosts (e.g. Resolve) have a faulty paramCopy, we first copy
    // all keys and values
    OfxPointD p;
    unsigned int keyCount = from->getNumKeys();

    to->deleteAllKeys();
    for (unsigned int i = 0; i < keyCount; ++i) {
        OfxTime time = from->getKeyTime(i);
        from->getValueAtTime(time, p.x, p.y);
        to->setValueAtTime(time, p.x, p.y);
    }
    if (keyCount == 0) {
        from->getValue(p.x, p.y);
        to->setValue(p.x, p.y);
    }
    // OfxParameterSuiteV1::paramCopy (does not work under Resolve, returns kOfxStatErrUnknown under Catalyst Edit)
    try {
        to->copyFrom(*from, 0, NULL);
    } catch (const Exception::Suite& e) {
#ifdef DEBUG
        std::cout << "OfxParameterSuiteV1 threw exception: " << e.what() << std::endl;
#endif
    }
}

void
CornerPinPlugin::changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName)
{
    const double time = args.time;

    // If any parameter is set by the user, set srcClipChanged to true so that from/to is not reset when
    // connecting the input.
    //printf("srcClipChanged=%s\n", _srcClipChanged->getValue() ? "true" : "false");
    if (paramName == kParamCopyInputRoD) {
        if ( _srcClip && _srcClip->isConnected() ) {
            beginEditBlock(paramName);
            const OfxRectD & srcRoD = _srcClip->getRegionOfDefinition(time);
            _from[0]->setValue(srcRoD.x1, srcRoD.y1);
            _from[1]->setValue(srcRoD.x2, srcRoD.y1);
            _from[2]->setValue(srcRoD.x2, srcRoD.y2);
            _from[3]->setValue(srcRoD.x1, srcRoD.y2);
            changedTransform(args);
            if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
                _srcClipChanged->setValue(true);
            }
            endEditBlock();
        }
    } else if (paramName == kParamCopyFrom) {
        beginEditBlock(paramName);
        for (int i = 0; i < 4; ++i) {
            copyPoint(_from[i], _to[i]);
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    } else if (paramName == kParamCopyFromSingle) {
        beginEditBlock(paramName);
        for (int i = 0; i < 4; ++i) {
            _to[i]->setValue( _from[i]->getValueAtTime(time) );
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    } else if (paramName == kParamCopyTo) {
        beginEditBlock(paramName);
        for (int i = 0; i < 4; ++i) {
            copyPoint(_to[i], _from[i]);
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    } else if (paramName == kParamCopyToSingle) {
        beginEditBlock(paramName);
        for (int i = 0; i < 4; ++i) {
            _from[i]->setValue( _to[i]->getValueAtTime(time) );
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    } else if ( (paramName == kParamTo[0]) ||
                ( paramName == kParamTo[1]) ||
                ( paramName == kParamTo[2]) ||
                ( paramName == kParamTo[3]) ||
                ( paramName == kParamEnable[0]) ||
                ( paramName == kParamEnable[1]) ||
                ( paramName == kParamEnable[2]) ||
                ( paramName == kParamEnable[3]) ||
                ( paramName == kParamFrom[0]) ||
                ( paramName == kParamFrom[1]) ||
                ( paramName == kParamFrom[2]) ||
                ( paramName == kParamFrom[3]) ||
                ( paramName == kParamExtraMatrixRow1) ||
                ( paramName == kParamExtraMatrixRow2) ||
                ( paramName == kParamExtraMatrixRow3) ) {
        beginEditBlock(paramName);
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
} // CornerPinPlugin::changedParam

void
CornerPinPlugin::changedClip(const InstanceChangedArgs &args,
                             const std::string &clipName)
{
    const double time = args.time;

    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_srcClipChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        const OfxRectD & srcRoD = _srcClip->getRegionOfDefinition(time);
        beginEditBlock(clipName);
        _from[0]->setValue(srcRoD.x1, srcRoD.y1);
        _from[1]->setValue(srcRoD.x2, srcRoD.y1);
        _from[2]->setValue(srcRoD.x2, srcRoD.y2);
        _from[3]->setValue(srcRoD.x1, srcRoD.y2);
        _to[0]->setValue(srcRoD.x1, srcRoD.y1);
        _to[1]->setValue(srcRoD.x2, srcRoD.y1);
        _to[2]->setValue(srcRoD.x2, srcRoD.y2);
        _to[3]->setValue(srcRoD.x1, srcRoD.y2);
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
        endEditBlock();
    }
}

class CornerPinTransformInteract
    : public OverlayInteract
{
public:

    CornerPinTransformInteract(OfxInteractHandle handle,
                               ImageEffect* effect)
        : OverlayInteract(handle)
        , _plugin( dynamic_cast<CornerPinPlugin*>(effect) )
        , _invert(NULL)
        , _overlayPoints(NULL)
        , _interactive(NULL)
        , _dragging(-1)
        , _hovering(-1)
        , _lastMousePos()
    {
        assert(_plugin);
        for (int i = 0; i < 4; ++i) {
            _to[i] = _plugin->fetchDouble2DParam(kParamTo[i]);
            _from[i] = _plugin->fetchDouble2DParam(kParamFrom[i]);
            _enable[i] = _plugin->fetchBooleanParam(kParamEnable[i]);
            assert(_to[i] && _from[i] && _enable[i]);
            addParamToSlaveTo(_to[i]);
            addParamToSlaveTo(_from[i]);
            addParamToSlaveTo(_enable[i]);
        }
        _invert = _plugin->fetchBooleanParam(kParamTransform3x3Invert);
        addParamToSlaveTo(_invert);
        _overlayPoints = _plugin->fetchChoiceParam(kParamOverlayPoints);
        addParamToSlaveTo(_overlayPoints);
        _interactive = _plugin->fetchBooleanParam(kParamTransformInteractive);
        assert(_invert && _overlayPoints && _interactive);

        for (int i = 0; i < 4; ++i) {
            _toDrag[i].x = _toDrag[i].y = 0;
            _fromDrag[i].x = _fromDrag[i].y = 0;
            _enableDrag[i] = false;
        }
        _useFromDrag = false;
        _interactiveDrag = false;
    }

    // overridden functions from Interact to do things
    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL;
    virtual bool penMotion(const PenArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const PenArgs &args) OVERRIDE FINAL;
    virtual bool penUp(const PenArgs &args) OVERRIDE FINAL;
    //virtual bool keyDown(const KeyArgs &args) OVERRIDE FINAL;
    //virtual bool keyUp(const KeyArgs &args) OVERRIDE FINAL;
    virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;

private:

    /**
     * @brief Returns true if the points that should be used by the overlay are
     * the "from" points, otherwise the overlay is assumed to use the "to" points.
     **/
    /*
       bool isFromPoints(double time) const
       {
        int v;
        _overlayPoints->getValueAtTime(time, v);
        return v == 1;
       }
     */
    CornerPinPlugin* _plugin;
    Double2DParam* _to[4];
    Double2DParam* _from[4];
    BooleanParam* _enable[4];
    BooleanParam* _invert;
    ChoiceParam* _overlayPoints;
    BooleanParam* _interactive;
    int _dragging; // -1: idle, else dragging point number
    int _hovering; // -1: idle, else hovering point number
    OfxPointD _lastMousePos;
    OfxPointD _toDrag[4];
    OfxPointD _fromDrag[4];
    bool _enableDrag[4];
    bool _useFromDrag;
    bool _interactiveDrag;
};

static bool
isNearby(const OfxPointD & p,
         double x,
         double y,
         double tolerance,
         const OfxPointD & pscale)
{
    return std::fabs(p.x - x) <= tolerance * pscale.x &&  std::fabs(p.y - y) <= tolerance * pscale.y;
}

bool
CornerPinTransformInteract::draw(const DrawArgs &args)
{
#if 0 //def DEBUG
    const OfxPointD &pscale = args.pixelScale;
    // kOfxInteractPropPixelScale gives the size of a screen pixel in canonical coordinates.
    // - it is correct under Nuke and Natron
    // - it is always (1,1) under Resolve
    std::cout << "pixelScale: " << pscale.x << ',' << pscale.y << std::endl;
#endif
    const double time = args.time;
    OfxRGBColourD color = {
        0.8, 0.8, 0.8
    };
    getSuggestedColour(color);
    GLdouble projection[16];
    glGetDoublev( GL_PROJECTION_MATRIX, projection);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
#ifdef CORNERPIN_DEBUG_OPENGL
    GLdouble modelview[16];
    glGetDoublev( GL_MODELVIEW_MATRIX, modelview);
    std::cout << "modelview:\n";
    {
        GLdouble *m = modelview;
        std::cout << "[[" << m[0] << ',' << m[1] << ',' << m[2] << ',' << m[3] << "]\n [" << m[4] << ',' << m[5] << ',' << m[6] << ',' << m[7] << "]\n [" << m[8] << ',' << m[9] << ',' << m[10] << ',' << m[11] << "]\n [" << m[12] << ',' << m[13] << ',' << m[14] << ',' << m[15] << "]]\n";
    }
    std::cout << "projection:\n";
    {
        GLdouble *m = projection;
        std::cout << "[[" << m[0] << ',' << m[1] << ',' << m[2] << ',' << m[3] << "]\n [" << m[4] << ',' << m[5] << ',' << m[6] << ',' << m[7] << "]\n [" << m[8] << ',' << m[9] << ',' << m[10] << ',' << m[11] << "]\n [" << m[12] << ',' << m[13] << ',' << m[14] << ',' << m[15] << "]]\n";
    }
    {
        std::cout << "projection*modelview:\n";
        Matrix4x4 p(projection);
        Matrix4x4 m(modelview);
        Matrix4x4 pm = p * m;
        std::cout << "[[" << pm(0, 0) << ',' << pm(0, 1) << ',' << pm(0, 2) << ',' << pm(0, 3) << "]\n [" << pm(1, 0) << ',' << pm(1, 1) << ',' << pm(1, 2) << ',' << pm(1, 3) << "]\n [" << pm(2, 0) << ',' << pm(2, 1) << ',' << pm(2, 2) << ',' << pm(2, 3) << "]\n [" << pm(3, 0) << ',' << pm(3, 1) << ',' << pm(3, 2) << ',' << pm(3, 3) << "]]\n";
    }
    std::cout << "viewport:\n";
    {
        std::cout << "[" << viewport[0] << ',' << viewport[1] << ',' << viewport[2] << ',' << viewport[3] << "]\n";
    }
#endif
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    shadow.x = 2. / (projection[0] * viewport[2]);
    shadow.y = 2. / (projection[5] * viewport[3]);

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                q[i] = to[i];
            } else {
                q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs

    //glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    glLineWidth(1.5f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPointSize(POINT_SIZE);
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        glMatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        glTranslated(direction * shadow.x, -direction * shadow.y, 0);
        glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        glColor3f( (float)(color.r / 2) * l, (float)(color.g / 2) * l, (float)(color.b / 2) * l );
        glBegin(GL_LINES);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                glVertex2d(p[i].x, p[i].y);
                glVertex2d(q[i].x, q[i].y);
            }
        }
        glEnd();
        glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
        glBegin(GL_LINE_LOOP);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                glVertex2d(p[i].x, p[i].y);
            }
        }
        glEnd();
        glBegin(GL_POINTS);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                if ( (_hovering == i) || (_dragging == i) ) {
                    glColor3f(0.f * l, 1.f * l, 0.f * l);
                } else {
                    glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
                }
                glVertex2d(p[i].x, p[i].y);
            }
        }
        glEnd();
        glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                TextRenderer::bitmapString(p[i].x, p[i].y, useFrom ? kParamFrom[i] : kParamTo[i]);
            }
        }
    }

    //glPopAttrib();

    return true;
} // CornerPinTransformInteract::draw

bool
CornerPinTransformInteract::penMotion(const PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;
    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) { // mouse is released
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    //OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                //q[i] = to[i];
            } else {
                //q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    bool didSomething = false;
    bool valuesChanged = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;

    _hovering = -1;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if (_dragging == i) {
                if (useFrom) {
                    from[i].x += delta.x;
                    from[i].y += delta.y;
                    _fromDrag[i] = from[i];
                } else {
                    to[i].x += delta.x;
                    to[i].y += delta.y;
                    _toDrag[i] = to[i];
                }
                valuesChanged = true;
            } else if ( isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale) ) {
                _hovering = i;
                didSomething = true;
            }
        }
    }

    if ( (_dragging != -1) && _interactiveDrag && valuesChanged ) {
        // no need to redraw overlay since it is slave to the paramaters
        if (useFrom) {
            _from[_dragging]->setValue(from[_dragging].x, from[_dragging].y);
        } else {
            _to[_dragging]->setValue(to[_dragging].x, to[_dragging].y);
        }
    } else if (didSomething || valuesChanged) {
        _effect->redrawOverlays();
    }

    _lastMousePos = args.penPosition;

    return didSomething || valuesChanged;
} // CornerPinTransformInteract::penMotion

bool
CornerPinTransformInteract::penDown(const PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;
    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    if (_dragging == -1) {
        for (int i = 0; i < 4; ++i) {
            _to[i]->getValueAtTime(time, to[i].x, to[i].y);
            _from[i]->getValueAtTime(time, from[i].x, from[i].y);
            _enable[i]->getValueAtTime(time, enable[i]);
        }
        int v;
        _overlayPoints->getValueAtTime(time, v);
        useFrom = (v == 1);
        if (_interactive) {
            _interactive->getValueAtTime(time, _interactiveDrag);
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            to[i] = _toDrag[i];
            from[i] = _fromDrag[i];
            enable[i] = _enableDrag[i];
        }
        useFrom = _useFromDrag;
    }

    OfxPointD p[4];
    //OfxPointD q[4];
    int enableBegin = 4;
    int enableEnd = 0;
    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (useFrom) {
                p[i] = from[i];
                //q[i] = to[i];
            } else {
                //q[i] = from[i];
                p[i] = to[i];
            }
            if (i < enableBegin) {
                enableBegin = i;
            }
            if (i + 1 > enableEnd) {
                enableEnd = i + 1;
            }
        }
    }

    bool didSomething = false;

    for (int i = enableBegin; i < enableEnd; ++i) {
        if (enable[i]) {
            if ( isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale) ) {
                _dragging = i;
                didSomething = true;
            }
            _toDrag[i] = to[i];
            _fromDrag[i] = from[i];
            _enableDrag[i] = enable[i];
        }
    }
    _useFromDrag = useFrom;

    if (didSomething) {
        _effect->redrawOverlays();
    }

    _lastMousePos = args.penPosition;

    return didSomething;
} // CornerPinTransformInteract::penDown

bool
CornerPinTransformInteract::penUp(const PenArgs & /*args*/)
{
    bool didSomething = _dragging != -1;

    if ( !_interactiveDrag && (_dragging != -1) ) {
        // no need to redraw overlay since it is slave to the paramaters
        if (_useFromDrag) {
            _from[_dragging]->setValue(_fromDrag[_dragging].x, _fromDrag[_dragging].y);
        } else {
            _to[_dragging]->setValue(_toDrag[_dragging].x, _toDrag[_dragging].y);
        }
    } else if (didSomething) {
        _effect->redrawOverlays();
    }

    _dragging = -1;

    return didSomething;
}

/** @brief Called when the interact is loses input focus */
void
CornerPinTransformInteract::loseFocus(const FocusArgs & /*args*/)
{
    _dragging = -1;
    _hovering = -1;
    _interactiveDrag = false;
}

class CornerPinOverlayDescriptor
    : public DefaultEffectOverlayDescriptor<CornerPinOverlayDescriptor, CornerPinTransformInteract>
{
};

static void
defineCornerPinToDouble2DParam(ImageEffectDescriptor &desc,
                               PageParamDescriptor *page,
                               GroupParamDescriptor* group,
                               int i,
                               double x,
                               double y)
{
    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTo[i]);
        param->setLabel(kParamTo[i]);
        param->setAnimates(true);
        param->setIncrement(1.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setDoubleType(eDoubleTypeXYAbsolute);
        // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(x, y);
        param->setDimensionLabels("x", "y");
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    // enable
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamEnable[i]);
        param->setLabel(kParamEnable[i]);
        param->setDefault(true);
        param->setAnimates(true);
        param->setHint(kParamEnableHint);
        if (group) {
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
}

static void
defineCornerPinFromsDouble2DParam(ImageEffectDescriptor &desc,
                                  PageParamDescriptor *page,
                                  GroupParamDescriptor* group,
                                  int i,
                                  double x,
                                  double y)
{
    Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamFrom[i]);

    param->setLabel(kParamFrom[i]);
    param->setAnimates(true);
    param->setIncrement(1.);
    param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
    param->setDisplayRange(-10000, -10000, 10000, 10000);
    param->setDoubleType(eDoubleTypeXYAbsolute);
    // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
    if ( param->supportsDefaultCoordinateSystem() ) {
        param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
    } else {
        gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
    }
    param->setDefault(x, y);
    param->setDimensionLabels("x", "y");
    if (group) {
        param->setParent(*group);
    }
    if (page) {
        page->addChild(*param);
    }
}

static void
defineExtraMatrixRow(ImageEffectDescriptor &desc,
                     PageParamDescriptor *page,
                     GroupParamDescriptor* group,
                     const std::string & name,
                     int rowIndex,
                     double x,
                     double y,
                     double z)
{
    Double3DParamDescriptor* param = desc.defineDouble3DParam(name);

    if ( getImageEffectHostDescription()->isNatron && (getImageEffectHostDescription()->versionMajor >= 2) && (getImageEffectHostDescription()->versionMinor >= 1) ) {
        param->setLabel(kGroupExtraMatrixLabel);
    } else {
        param->setLabels("", "", "");
    }

    param->setMatrixRow(rowIndex);
    param->setAnimates(true);
    param->setDefault(x, y, z);
    param->setIncrement(0.01);
    if (group) {
        param->setParent(*group);
    }
    if (page) {
        page->addChild(*param);
    }
}

static void
CornerPinPluginDescribeInContext(ImageEffectDescriptor &desc,
                                 ContextEnum /*context*/,
                                 PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    // toPoints
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupTo);
        if (group) {
            group->setLabel(kGroupTo);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        defineCornerPinToDouble2DParam(desc, page, group, 0, 0, 0);
        defineCornerPinToDouble2DParam(desc, page, group, 1, 1, 0);
        defineCornerPinToDouble2DParam(desc, page, group, 2, 1, 1);
        defineCornerPinToDouble2DParam(desc, page, group, 3, 0, 1);

        // copyFrom
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyFrom);
            param->setLabel(kParamCopyFromLabel);
            param->setHint(kParamCopyFromHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // copyFromSingle
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyFromSingle);
            param->setLabel(kParamCopyFromSingleLabel);
            param->setHint(kParamCopyFromSingleHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    // fromPoints
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupFrom);
        if (group) {
            group->setLabel(kGroupFrom);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        defineCornerPinFromsDouble2DParam(desc, page, group, 0, 0, 0);
        defineCornerPinFromsDouble2DParam(desc, page, group, 1, 1, 0);
        defineCornerPinFromsDouble2DParam(desc, page, group, 2, 1, 1);
        defineCornerPinFromsDouble2DParam(desc, page, group, 3, 0, 1);

        // setToInput
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyInputRoD);
            param->setLabel(kParamCopyInputRoDLabel);
            param->setHint(kParamCopyInputRoDHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // copyTo
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyTo);
            param->setLabel(kParamCopyToLabel);
            param->setHint(kParamCopyToHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // copyToSingle
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamCopyToSingle);
            param->setLabel(kParamCopyToSingleLabel);
            param->setHint(kParamCopyToSingleHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    // amount
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformAmount);
        param->setLabel(kParamTransformAmountLabel);
        param->setHint(kParamTransformAmountHint);
        param->setDoubleType(eDoubleTypeScale);
        param->setDefault(1.);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        //if (group) {
        //    param->setParent(*group);
        //}
        if (page) {
            page->addChild(*param);
        }
    }

    // extraMatrix
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupExtraMatrix);
        if (group) {
            group->setLabel(kGroupExtraMatrixLabel);
            group->setHint(kGroupExtraMatrixHint);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
        }

        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow1, 1, 1, 0, 0);
        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow2, 2, 0, 1, 0);
        defineExtraMatrixRow(desc, page, group, kParamExtraMatrixRow3, 3, 0, 0, 1);
    }

    // overlayPoints
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOverlayPoints);
        param->setLabel(kParamOverlayPointsLabel);
        param->setHint(kParamOverlayPointsHint);
        param->appendOption(kParamOverlayPointsOptionTo);
        param->appendOption(kParamOverlayPointsOptionFrom);
        param->setDefault(0);
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
} // CornerPinPluginDescribeInContext

mDeclarePluginFactory(CornerPinPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
CornerPinPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void
CornerPinPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur);

    // srcClipChanged
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }
}

ImageEffect*
CornerPinPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, false);
}

mDeclarePluginFactory(CornerPinMaskedPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
CornerPinMaskedPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMaskedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void
CornerPinMaskedPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                                ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur);

    // srcClipChanged
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }
}

ImageEffect*
CornerPinMaskedPluginFactory::createInstance(OfxImageEffectHandle handle,
                                             ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, true);
}

static CornerPinPluginFactory p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static CornerPinMaskedPluginFactory p2(kPluginMaskedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
