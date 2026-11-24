/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
#include <algorithm>
#include <vector>
#ifdef DEBUG
#include <iostream>
#endif

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION // Yes, we are still doing OpenGL 2.1
#endif
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#endif

#include "ofxsOGLTextRenderer.h"
#include "ofxsTransform3x3.h"
#include "ofxsThreadSuite.h"
#include "ofxsOGLHiDPI.h"
#include "ofxsPositionInteract.h"
#include "ofxsOverlayDraw.h"

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
    "In the viewer, click a point to select and drag it, shift-click to add a " \
    "point to the selection or remove it, and drag any selected point to move " \
    "all the selected points together. Escape clears the selection, or cancels " \
    "a drag in progress.\n" \
    "In Natron versions with the fixed selection rectangle " \
    "(NatronGitHub/Natron#1196), the viewer selection rectangle also selects the " \
    "points of the CornerPin whose overlay is shown (shift+drag adds them to the " \
    "selection), and a click on empty space clears the selection (also when the " \
    "overlay of another node is shown). In other Natron versions and in other " \
    "hosts, only click, shift-click and Escape select points.\n" \
    "\n" \
    "This plugin concatenates transforms.\n" \
    "See also: https://web.archive.org/web/20220627032200/http://www.opticalenquiry.com/nuke/index.php?title=CornerPin"

#define kPluginIdentifier "net.sf.openfx.CornerPinPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.CornerPinMaskedPlugin"
// SupportExt#179, in the current minor versions (not released yet, so no further bump): with the Box
//              filter, the thin band just outside the source image is black (Black Outside) or the
//              edge colour, not about 4x the edge value (garbage).
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.
// 2.1: overlay point selection, host selection rectangle param (Natron);
//      the inverse of a transform matrix with huge entries (e.g. a concatenated source transform) no longer overflows to NaN or 0 (SupportExt #81)
// 1.1 and 2.1 (unreleased, fixed in place): the Lanczos4, Lanczos6 and Sinc4 filters (appended options) and the
//      Prefilter Downscaling checkbox (appended, off) (issue #270); the default output is unchanged.

#define POINT_SIZE 5
// Pick tolerance in screen pixels, through kOfxInteractPropPixelScale. As in RectangleInteract and
// TransformInteractHelper, it is not scaled by the screen pixel ratio (unlike POINT_SIZE when drawing).
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
                    bool masked,
                    unsigned int majorVersion)
        : Transform3x3Plugin(handle, masked, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur)
        , _transformAmount(NULL)
        , _extraMatrixRow1(NULL)
        , _extraMatrixRow2(NULL)
        , _extraMatrixRow3(NULL)
        , _srcClipChanged(NULL)
        , _overlayPoints(NULL)
#ifdef OFX_EXTENSIONS_NATRON
        , _selectionRectangleState(NULL)
        , _selectionRectangleLastState(0)
#endif
        , _majorVersion(majorVersion)
    {
        for (int i = 0; i < 4; ++i) {
            _overlaySelected[i] = false;
#ifdef OFX_EXTENSIONS_NATRON
            _overlaySelectedOnRectangleStart[i] = false;
#endif
        }
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
        _overlayPoints = fetchChoiceParam(kParamOverlayPoints);
        assert(_overlayPoints);
#ifdef OFX_EXTENSIONS_NATRON
        // only declared on Natron, see CornerPinPluginDescribeInContext()
        if ( paramExists(kNatronOfxParamSelectionRectangleState) ) {
            _selectionRectangleState = fetchIntParam(kNatronOfxParamSelectionRectangleState);
            assert(_selectionRectangleState);
        }
#endif

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
                //EditBlock eb(*this, kParamDefaultsNormalised);
                for (int i = 0; i < 4; ++i) {
                    p = _to[i]->getValue();
                    _to[i]->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                    p = _from[i]->getValue();
                    _from[i]->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                }
                param->setValue(false);
            }
        }
    }

    /**
     * @brief The points selected in the viewer overlay (shared by the overlay
     * interacts of this instance, and updated by the host selection rectangle).
     **/
    bool* overlaySelected() { return _overlaySelected; }

private:
#ifdef OFX_EXTENSIONS_NATRON
    /// Update the overlay selection from the host selection rectangle (Natron extension).
    void selectionRectangleChanged(double time);
#endif

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
    ChoiceParam* _overlayPoints;
#ifdef OFX_EXTENSIONS_NATRON
    IntParam* _selectionRectangleState; // NULL if the host is not Natron
    int _selectionRectangleLastState; // the last value of _selectionRectangleState
    bool _overlaySelectedOnRectangleStart[4]; // the selection when the rectangle started
#endif
    bool _overlaySelected[4];
    unsigned int _majorVersion;
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
    Matrix3x3 extraMat = getExtraMatrix(time);
    // Before version 2, the transform used for rendering with "Invert" checked was homo3x3 * extraMat
    // (instead of inverse(extraMat) * homo3x3). Keep that render for old instances, and compute the
    // direct transform used for the RoD as its inverse, which means swapping the composition order.
    bool composeInverted = invert;
    if ( (_majorVersion < 2) && _invert && _invert->getValueAtTime(time) ) {
        composeInverted = !invert;
    }
    if (composeInverted) {
        // the direct transform is the inverse of homo3x3 * extraMat, i.e. inverse(extraMat) * inverse(homo3x3)
        Matrix3x3 extraMatInverse;
        if ( !extraMat.inverse(&extraMatInverse) ) {
            return false;
        }
        extraMat = extraMatInverse;
    }

    if (k == 0) {
        // no points, only apply extraMat;
        *invtransform = extraMat;

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

    if (composeInverted) {
        *invtransform = extraMat * homo3x3;
    } else {
        *invtransform = homo3x3 * extraMat;
    }

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

#ifdef OFX_EXTENSIONS_NATRON
    if (paramName == kNatronOfxParamSelectionRectangleState) {
        // not a rendering parameter: only the overlay selection changes
        if (_selectionRectangleState) {
            selectionRectangleChanged(time);
        }

        return;
    }
#endif

    // If any parameter is set by the user, set srcClipChanged to true so that from/to is not reset when
    // connecting the input.
    //printf("srcClipChanged=%s\n", _srcClipChanged->getValue() ? "true" : "false");
    if (paramName == kParamCopyInputRoD) {
        if ( _srcClip && _srcClip->isConnected() ) {
            EditBlock eb(*this, paramName);
            const OfxRectD & srcRoD = _srcClip->getRegionOfDefinition(time);
            _from[0]->setValue(srcRoD.x1, srcRoD.y1);
            _from[1]->setValue(srcRoD.x2, srcRoD.y1);
            _from[2]->setValue(srcRoD.x2, srcRoD.y2);
            _from[3]->setValue(srcRoD.x1, srcRoD.y2);
            changedTransform(args);
            if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
                _srcClipChanged->setValue(true);
            }
        }
    } else if (paramName == kParamCopyFrom) {
        EditBlock eb(*this, paramName);
        for (int i = 0; i < 4; ++i) {
            copyPoint(_from[i], _to[i]);
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
    } else if (paramName == kParamCopyFromSingle) {
        EditBlock eb(*this, paramName);
        for (int i = 0; i < 4; ++i) {
            _to[i]->setValue( _from[i]->getValueAtTime(time) );
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
    } else if (paramName == kParamCopyTo) {
        EditBlock eb(*this, paramName);
        for (int i = 0; i < 4; ++i) {
            copyPoint(_to[i], _from[i]);
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
    } else if (paramName == kParamCopyToSingle) {
        EditBlock eb(*this, paramName);
        for (int i = 0; i < 4; ++i) {
            _from[i]->setValue( _to[i]->getValueAtTime(time) );
        }
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
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
        EditBlock eb(*this, paramName);
        changedTransform(args);
        if ( (args.reason == eChangeUserEdit) && !_srcClipChanged->getValue() ) {
            _srcClipChanged->setValue(true);
        }
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
} // CornerPinPlugin::changedParam

#ifdef OFX_EXTENSIONS_NATRON
// ofxNatron.h, extension 7: "To advertise to the host that we are interested in the selection state of
// the rectangle, we declare a Int parameter with the name kNatronOfxParamSelectionRectangleState. The
// host notifies the plug-in of the selection state change via the kOfxActionInstanceChanged action,
// with kOfxPropChangeReason = kOfxChangeUserEdited (not kOfxChangePluginEdited).
// The value of the parameter can be 3-fold:
//     0 meaning that the selection has been cleared by the user
//     1 meaning that the selection is being actively edited by the user (e.g. during pen motion)
//     2 meaning that the selection is finalized by the user (e.g. on pen up)
// The property kNatronOfxImageEffectSelectionRectangle is updated on the effect instance before the
// host calls the kOfxActionInstanceChanged action for this parameter, indicating the region covered
// by the selection rectangle, in canonical coordinates (those of kOfxInteractPropPenPosition), with
// x1 <= x2 and y1 <= y2; it is (0,0,0,0) after a clear."
void
CornerPinPlugin::selectionRectangleChanged(double time)
{
    const int state = _selectionRectangleState->getValue();

    if (state == 0) {
        // The selection was cleared by the user: Natron sends 0 on a click or drag on empty
        // space, before the new rectangle starts. Natron#1196 sends it to every node (as for
        // RotoPaint), while the rectangle updates (1, 2) only go to the node whose overlay is
        // shown. As in Natron's RotoPaint, shift keeps the selection, so that the following
        // rectangle adds to it. ofxNatron.h, extension 7: "Keyboard modifiers: the
        // instanceChanged action gives none. A plug-in that keeps its selection on a clear when
        // a modifier is held [...] uses the modifiers of the last action of its overlay
        // interact [...]. The Support library's OFX::ImageEffect::getKeyModifiers() returns that
        // state, also during kOfxActionInstanceChanged", and "A host that supports 13) sends the
        // action that causes a clear to the displayed overlay interacts before it calls
        // kOfxActionInstanceChanged: the kOfxInteractActionPenDown of the pen down that starts
        // the rectangle (which no overlay caught)". So isShiftDown() is the state of the
        // overlay's last action, i.e. the pen down that started the rectangle (the host's
        // kNatronOfxInteractPropKeyModifiers, or the state Support tracked from the overlay key
        // actions on other hosts).
        if ( !isShiftDown() ) {
            for (int i = 0; i < 4; ++i) {
                _overlaySelected[i] = false;
            }
        }
    } else {
        if (_selectionRectangleLastState != 1) {
            // A new rectangle adds to the selection it starts with (empty after a 0 without
            // shift, so the rectangle replaces the selection).
            // If a rectangle ends without a 2 (e.g. the host abandons it), a next rectangle that
            // is not preceded by a 0 continues the stale one: it adds to the base of the abandoned
            // rectangle, not to its last result. A 0 resets this (Natron#1196 sends a 0 on every
            // press on empty space, so this should not happen there).
            for (int i = 0; i < 4; ++i) {
                _overlaySelectedOnRectangleStart[i] = _overlaySelected[i];
            }
        }
        // kNatronOfxImageEffectSelectionRectangle is a double x 4 property, in canonical coordinates,
        // read without truncation by ImageEffect::getSelectionRectangle().
        const OfxRectD r = getSelectionRectangle();
        const double x1 = (std::min)(r.x1, r.x2);
        const double x2 = (std::max)(r.x1, r.x2);
        const double y1 = (std::min)(r.y1, r.y2);
        const double y2 = (std::max)(r.y1, r.y2);
        int v;
        _overlayPoints->getValueAtTime(time, v);
        const bool useFrom = (v == 1);
        for (int i = 0; i < 4; ++i) {
            bool enable;
            _enable[i]->getValueAtTime(time, enable);
            OfxPointD p;
            (useFrom ? _from[i] : _to[i])->getValueAtTime(time, p.x, p.y);
            // a disabled point can't be selected
            const bool inside = enable && x1 <= p.x && p.x <= x2 && y1 <= p.y && p.y <= y2;
            _overlaySelected[i] = (_overlaySelectedOnRectangleStart[i] && enable) || inside;
        }
    }
    _selectionRectangleLastState = state;
    redrawOverlays();
}
#endif // OFX_EXTENSIONS_NATRON

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
        EditBlock eb(*this, clipName);
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
    }
}

// The viewer overlay of CornerPin.
//
// Selection and drag (NatronGitHub/openfx-misc#144):
// - click on a point: select it (and only it, unless it is already selected)
//   and drag the selected points;
// - shift-click on a point: add it to, or remove it from, the selection;
// - Escape: clear the selection;
// - a disabled point (the "enable" checkbox) can't be selected.
// Pen events on empty space are not trapped, so that the host can use them (e.g. Natron's
// viewer menu, user RoI and selection rectangle). On Natron, the host selection rectangle
// selects the points inside it (see CornerPinPlugin::selectionRectangleChanged()).
// The selection is stored in the plugin instance, so that it is shared by all the
// interacts (views) and the host selection rectangle.
//
// The OFX pen actions do not carry the keyboard modifiers (ofxInteract.h: the
// inArgs of kOfxInteractActionPenDown are the effect instance, pixel scale,
// background colour, time, render scale, pen position, pen viewport position and
// pen pressure). The shift state comes from ImageEffect::isShiftDown() (ofxNatron.h,
// extension 13): the host's kNatronOfxInteractPropKeyModifiers if it passes it, else
// the Support library tracks the modifier key events of the overlay interacts of the
// instance, and resets them on kOfxInteractActionLoseFocus. It is per instance, so
// shared by all the overlay interacts and the host selection rectangle.
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
        , _hiDPI(NULL)
        , _mouseState(eMouseStateIdle)
        , _dragging(-1)
        , _hovering(-1)
        , _selected(NULL)
        , _lastMousePos()
        , _dragMoved(false)
        , _penDownTrapped(false)
        , _drawMode( OFX::ofxsOverlayDrawModeForNewInteract() )
    {
        assert(_plugin);
        _selected = _plugin->overlaySelected();
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

        if ( effect->paramExists(kParamHiDPI) ) {
            _hiDPI = effect->fetchBooleanParam(kParamHiDPI);
            assert(_hiDPI);
            addParamToSlaveTo(_hiDPI);
        }
        assert(_invert && _overlayPoints && _interactive && _hiDPI);

        for (int i = 0; i < 4; ++i) {
            _toDrag[i].x = _toDrag[i].y = 0;
            _fromDrag[i].x = _fromDrag[i].y = 0;
            _toOrig[i] = _toDrag[i];
            _fromOrig[i] = _fromDrag[i];
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
    virtual bool keyDown(const KeyArgs &args) OVERRIDE FINAL;
    virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;

private:
    enum MouseStateEnum
    {
        eMouseStateIdle = 0,
        eMouseStateDraggingPoints, // dragging the selected points
    };

    /**
     * @brief Get the "to" and "from" points, their "enable" state, and whether the
     * overlay shows the "from" points: from the parameters at time, or from the
     * drag copies while dragging points.
     **/
    void getPoints(double time,
                   OfxPointD to[4],
                   OfxPointD from[4],
                   bool enable[4],
                   bool* useFrom) const
    {
        if (_mouseState != eMouseStateDraggingPoints) {
            for (int i = 0; i < 4; ++i) {
                _to[i]->getValueAtTime(time, to[i].x, to[i].y);
                _from[i]->getValueAtTime(time, from[i].x, from[i].y);
                _enable[i]->getValueAtTime(time, enable[i]);
            }
            int v;
            _overlayPoints->getValueAtTime(time, v);
            *useFrom = (v == 1);
        } else {
            for (int i = 0; i < 4; ++i) {
                to[i] = _toDrag[i];
                from[i] = _fromDrag[i];
                enable[i] = _enableDrag[i];
            }
            *useFrom = _useFromDrag;
        }
    }

    /// The points shown by the overlay ("to" or "from"). p[i] is only set if enable[i].
    static void getOverlayPoints(const OfxPointD to[4],
                                 const OfxPointD from[4],
                                 const bool enable[4],
                                 bool useFrom,
                                 OfxPointD p[4])
    {
        for (int i = 0; i < 4; ++i) {
            if (enable[i]) {
                p[i] = useFrom ? from[i] : to[i];
            } else {
                p[i].x = p[i].y = 0.;
            }
        }
    }

    bool hasSelection() const
    {
        return _selected[0] || _selected[1] || _selected[2] || _selected[3];
    }

    void clearSelection()
    {
        for (int i = 0; i < 4; ++i) {
            _selected[i] = false;
        }
    }

    /// Set the dragged points from the drag copies, in a single undo/redo block if there are several.
    void setDraggedValues()
    {
        int n = 0;
        for (int i = 0; i < 4; ++i) {
            if (_selected[i] && _enableDrag[i]) {
                ++n;
            }
        }
        if (n == 0) {
            return;
        }
        // ofxParam.h, paramEditBegin: "If a plugin calls paramSetValue/paramSetValueAtTime on one or
        // more parameters, either from custom GUI interaction or some analysis of imagery etc.. this is
        // used to indicate the start of a set of a parameter changes that should be considered part of
        // a single undo/redo block."
        // With Interactive Update, this is called on each pen motion: the block is not kept open
        // from pen down to pen up, because Natron's paramEditBegin also blocks evaluation (renders)
        // until paramEditEnd (KnobHolder::beginChanges()), which would defeat Interactive Update.
        ImageEffect::EditBlock eb(*_effect, "Move CornerPin points", n > 1);
        // no need to redraw overlay since it is slave to the paramaters
        for (int i = 0; i < 4; ++i) {
            if (_selected[i] && _enableDrag[i]) {
                if (_useFromDrag) {
                    _from[i]->setValue(_fromDrag[i].x, _fromDrag[i].y);
                } else {
                    _to[i]->setValue(_toDrag[i].x, _toDrag[i].y);
                }
            }
        }
    }

    /**
     * @brief Abandon a drag in progress, without changing the parameters, and
     * reset the hover state. Called when the interact loses
     * focus (the host may never send the pen up), or on a pen down during a drag.
     **/
    void abandonDrag()
    {
        _mouseState = eMouseStateIdle;
        _dragging = -1;
        _hovering = -1;
        _interactiveDrag = false;
        _dragMoved = false;
        _penDownTrapped = false;
    }

    /**
     * @brief Cancel a point drag in progress (Escape): the points go back to where
     * they were at pen down. Without Interactive Update, the parameters were not
     * changed yet; with it, they are set back to their values at pen down.
     * The selection is kept, and the matching pen up is still trapped.
     * With Interactive Update, the restore is itself a parameter change (an undo step,
     * in one block), and on animated parameters setValue() sets a key at the current
     * time: if the drag created that key, the key stays, with the pen-down value.
     **/
    void cancelDrag()
    {
        if ( _interactiveDrag && _dragMoved ) {
            for (int i = 0; i < 4; ++i) {
                _toDrag[i] = _toOrig[i];
                _fromDrag[i] = _fromOrig[i];
            }
            setDraggedValues();
        }
        const bool penDownTrapped = _penDownTrapped;
        abandonDrag();
        _penDownTrapped = penDownTrapped;
        _effect->redrawOverlays();
    }

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
    BooleanParam* _hiDPI;
    MouseStateEnum _mouseState;
    int _dragging; // -1: idle, else the point number under the pen when the drag started
    int _hovering; // -1: idle, else hovering point number
    bool* _selected; // the selected points (4), stored in the plugin instance
    OfxPointD _lastMousePos;
    OfxPointD _toDrag[4];
    OfxPointD _fromDrag[4];
    bool _enableDrag[4];
    bool _useFromDrag;
    bool _interactiveDrag;
    bool _dragMoved; // the selected points were moved since pen down
    bool _penDownTrapped; // the last pen down was trapped: trap the matching pen up
    OFX::OverlayDrawModeEnum _drawMode; // set by the descriptor that created the interact, see ofxsOverlayDraw.h
    OfxPointD _toOrig[4]; // the points at pen down, restored if the drag is cancelled
    OfxPointD _fromOrig[4];
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
    // all drawing goes through d: OpenGL in V1 draws, the DrawSuite in V2 draws
    OverlayDraw d(args, _drawMode);
    if ( !d.canDraw() ) {
        return false; // V2 draw without a draw context, or V1 draw without OpenGL
    }
    GLdouble projection[16];
    d.glGetDoublev( GL_PROJECTION_MATRIX, projection);
    GLint viewport[4];
    d.glGetIntegerv(GL_VIEWPORT, viewport);
#ifdef CORNERPIN_DEBUG_OPENGL
    if ( !d.usesDrawSuite() ) {
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
    }
#endif
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    // 0 for a degenerate viewport or projection (inf, then inf - inf = NaN)
    {
        const double denx = projection[0] * viewport[2];
        const double deny = projection[5] * viewport[3];
        shadow.x = (denx != 0.) ? 2. / denx : 0.;
        shadow.y = (deny != 0.) ? 2. / deny : 0.;
    }

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    getPoints(time, to, from, enable, &useFrom);

    bool hiDPI = _hiDPI ? _hiDPI->getValue() : false;
    double screenPixelRatio = hiDPI ? 2 : 1;
#ifdef OFX_EXTENSIONS_NATRON
    const double hostRatio = OverlayDraw::hostScreenPixelRatio(args); // 1 if the host does not set it
    screenPixelRatio *= hostRatio;
    hiDPI |= hostRatio > 1;
#endif
    screenPixelRatio = d.overlayScreenPixelRatio(screenPixelRatio); // DrawSuite: 1
    TextRenderer::Font font = hiDPI ? TextRenderer::FONT_TIMES_ROMAN_24 : TextRenderer::FONT_HELVETICA_12;

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
    d.glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    d.glEnable(GL_BLEND);
    d.glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    d.glLineWidth(1.5f * screenPixelRatio);
    d.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    d.glPointSize(POINT_SIZE * screenPixelRatio);
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    // with the DrawSuite, no shadow (the host's job): only the drawing pass
    for (int l = d.usesDrawSuite() ? 1 : 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        if ( !d.usesDrawSuite() ) {
            d.glMatrixMode(GL_PROJECTION);
            int direction = (l == 0) ? 1 : -1;
            // translate (1,-1) pixels
            d.glTranslated(direction * shadow.x, -direction * shadow.y, 0);
            d.glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke
        }

        // Draw the motion curve a bit darker
        const double darken = 0.8;
        d.glColor3d(color.r * darken * l, color.g * darken * l, color.b * darken * l);
        d.glPointSize(POINT_SIZE * screenPixelRatio);

        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                Double2DParam* p = useFrom ? _from[i] : _to[i];
                drawPointTrajectory(d, p);
            }
        }

        d.glColor3f( (float)(color.r / 2) * l, (float)(color.g / 2) * l, (float)(color.b / 2) * l );
        d.glBegin(GL_LINES);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                d.glVertex2d(p[i].x, p[i].y);
                d.glVertex2d(q[i].x, q[i].y);
            }
        }
        d.glEnd();
        d.glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
        d.glBegin(GL_LINE_LOOP);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                d.glVertex2d(p[i].x, p[i].y);
            }
        }
        d.glEnd();
        d.glBegin(GL_POINTS);
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                if ( (_hovering == i) || (_dragging == i) ) {
                    d.glColor3f(0.f * l, 1.f * l, 0.f * l);
                } else if (_selected[i]) {
                    // selected points are highlighted
                    d.glColor3f(1.f * l, 0.75f * l, 0.f * l);
                } else {
                    d.glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
                }
                d.glVertex2d(p[i].x, p[i].y);
            }
        }
        d.glEnd();
        d.glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );
        for (int i = enableBegin; i < enableEnd; ++i) {
            if (enable[i]) {
                // the label is POINT_SIZE screen pixels above the point, in canonical units
                // (kOfxInteractPropPixelScale: "The size of a real screen pixel under the
                // interact's canonical projection"; the screen pixel ratio is for framebuffer
                // sizes such as glPointSize, so it is not applied here, as for the handles)
                d.bitmapString(p[i].x, p[i].y + POINT_SIZE * args.pixelScale.y, useFrom ? kParamFrom[i] : kParamTo[i], font);
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

    getPoints(time, to, from, enable, &useFrom);

    OfxPointD p[4];
    getOverlayPoints(to, from, enable, useFrom, p);

    const bool draggingPoints = (_mouseState == eMouseStateDraggingPoints);
    bool valuesChanged = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    const bool moved = (delta.x != 0.) || (delta.y != 0.);

    int newHovering = -1;

    for (int i = 0; i < 4; ++i) {
        if (enable[i]) {
            if (draggingPoints && _selected[i]) {
                // all the selected points move by the same delta
                if (moved) {
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
                }
            } else if ( isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale) ) {
                newHovering = i;
            }
        }
    }
    if (valuesChanged) {
        _dragMoved = true;
    }
    bool redraw = _hovering != newHovering;
    _hovering = newHovering;

    if (draggingPoints && _interactiveDrag && valuesChanged) {
        // no need to redraw overlay since it is slave to the paramaters
        setDraggedValues();
    } else if (redraw || valuesChanged) {
        _effect->redrawOverlays();
    }

    _lastMousePos = args.penPosition;

#ifdef OFX_EXTENSIONS_NATRON
    // The host reads the cursor whatever penMotion returns.
    // setCursor() does nothing on hosts without interact cursors.
    const bool overPoint = draggingPoints || (_hovering != -1);
    setCursor(overPoint ? kOfxInteractCursorMove : kOfxInteractCursorDefault);
#endif

    // trap the pen motion during a drag
    return draggingPoints;
} // CornerPinTransformInteract::penMotion

bool
CornerPinTransformInteract::penDown(const PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;

    if (_mouseState != eMouseStateIdle) {
        // the host did not send the pen up of the previous drag
        abandonDrag();
    }

    OfxPointD to[4];
    OfxPointD from[4];
    bool enable[4];
    bool useFrom;

    getPoints(time, to, from, enable, &useFrom);
    if (_interactive) {
        _interactive->getValueAtTime(time, _interactiveDrag);
    }

    OfxPointD p[4];
    getOverlayPoints(to, from, enable, useFrom, p);

    // a disabled point can't be selected
    for (int i = 0; i < 4; ++i) {
        if (!enable[i]) {
            _selected[i] = false;
        }
    }

    // the point under the pen (the last one, if several points are nearby)
    int hit = -1;
    for (int i = 0; i < 4; ++i) {
        if ( enable[i] && isNearby(args.penPosition, p[i].x, p[i].y, POINT_TOLERANCE, pscale) ) {
            hit = i;
        }
    }

    const bool shift = _plugin->isShiftDown();
    _lastMousePos = args.penPosition;
    _useFromDrag = useFrom;
    _penDownTrapped = (hit != -1);

    if (hit != -1) {
        if (shift) {
            // shift-click toggles the point in the selection
            _selected[hit] = !_selected[hit];
            if (!_selected[hit]) {
                // the point was removed from the selection: nothing to drag
                _hovering = hit;
                _effect->redrawOverlays();

                return true;
            }
        } else if (!_selected[hit]) {
            // click on a point which is not selected: select only that point
            clearSelection();
            _selected[hit] = true;
        }
        // drag the selected points
        _mouseState = eMouseStateDraggingPoints;
        _dragging = hit;
        _dragMoved = false;
        for (int i = 0; i < 4; ++i) {
            _toDrag[i] = to[i];
            _fromDrag[i] = from[i];
            _toOrig[i] = to[i];
            _fromOrig[i] = from[i];
            _enableDrag[i] = enable[i];
        }
        _effect->redrawOverlays();

        return true;
    }

    // Pen down on empty space: not trapped, so that the host can use it (Natron: viewer menu,
    // user RoI handles, selection rectangle, color pickers...). On Natron, the host
    // selection rectangle clears or sets the selection (CornerPinPlugin::selectionRectangleChanged()).
    return false;
} // CornerPinTransformInteract::penDown

bool
CornerPinTransformInteract::penUp(const PenArgs & /*args*/)
{
    if (_mouseState != eMouseStateDraggingPoints) {
        // trap the pen up of a trapped pen down (shift-click deselect, cancelled drag)
        const bool trapped = _penDownTrapped;
        _penDownTrapped = false;

        return trapped;
    }

    if (!_interactiveDrag && _dragMoved) {
        // set all the moved points at once
        // no need to redraw overlay since it is slave to the paramaters
        setDraggedValues();
    } else {
        _effect->redrawOverlays();
    }

    _mouseState = eMouseStateIdle;
    _dragging = -1;
    _dragMoved = false;
    _penDownTrapped = false;

    return true;
}

// Escape cancels a drag or clears the selection
bool
CornerPinTransformInteract::keyDown(const KeyArgs &args)
{
    if (args.keySymbol == kOfxKey_Escape) {
        if (_mouseState == eMouseStateDraggingPoints) {
            // trapped, so that the host does not also clear its selection during the drag
            cancelDrag();

            return true;
        }
        if ( hasSelection() ) {
            clearSelection();
            _effect->redrawOverlays();

            return true;
        }
    }

    // modifiers are not "caught"
    return false;
}

/** @brief Called when the interact is loses input focus */
void
CornerPinTransformInteract::loseFocus(const FocusArgs & /*args*/)
{
    // a drag is abandoned if the focus is lost (the host may never send the
    // pen up), as in RectangleInteract and TransformInteractHelper. The
    // selection is kept.
    abandonDrag();
}

// draws through OverlayDraw: the DrawSuite in V2 draws, OpenGL (with shadows) in V1 draws
class CornerPinOverlayDescriptor
    : public DefaultEffectOverlayDescriptorV2<CornerPinOverlayDescriptor, CornerPinTransformInteract>
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

    // Natron >= 2.1 (testing major and minor separately would fail for 3.0)
    const ImageEffectHostDescription& host = *getImageEffectHostDescription();
    if ( host.isNatron &&
         ( (host.versionMajor > 2) || ( (host.versionMajor == 2) && (host.versionMinor >= 1) ) ) ) {
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

    // HiDPI
    hiDPIDescribeParams(desc, NULL, page);

#ifdef OFX_EXTENSIONS_NATRON
    // Use the host selection rectangle to select the overlay points (ofxNatron.h, extension 7).
    // Only on Natron: other hosts would show or save this parameter.
    if (getImageEffectHostDescription()->isNatron) {
        IntParamDescriptor* param = desc.defineIntParam(kNatronOfxParamSelectionRectangleState);
        param->setDefault(0);
        param->setRange(0, 2);
        param->setDisplayRange(0, 2);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false); // not used by the render: no re-render, no hash change
        param->setIsPersistent(false);
        param->setCanUndo(false);
        if (page) {
            page->addChild(*param);
        }
    }
#endif

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

mDeclarePluginFactoryVersioned(CornerPinPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
CornerPinPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

    ofxsSetOverlayInteractDescriptorV2(desc, new CornerPinOverlayDescriptor); // V2, and V1 with OpenGL (hasOpenGLDraw)
}

template<unsigned int majorVersion>
void
CornerPinPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur, eTransform3x3FilteringSincPrefilter); // opt-in (issue #270)

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

template<unsigned int majorVersion>
ImageEffect*
CornerPinPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, false, majorVersion);
}

mDeclarePluginFactoryVersioned(CornerPinMaskedPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
CornerPinMaskedPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMaskedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

    ofxsSetOverlayInteractDescriptorV2(desc, new CornerPinOverlayDescriptor); // V2, and V1 with OpenGL (hasOpenGLDraw)
}

template<unsigned int majorVersion>
void
CornerPinMaskedPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur, eTransform3x3FilteringSincPrefilter); // opt-in (issue #270)

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

template<unsigned int majorVersion>
ImageEffect*
CornerPinMaskedPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                             ContextEnum /*context*/)
{
    return new CornerPinPlugin(handle, true, majorVersion);
}

// Each major version needs its own factory class: FactoryMainEntryHelper stores
// the plugin UID in a static shared by all objects of the same class.
static CornerPinPluginFactory<kPluginVersionMajor> p1(kPluginIdentifier, kPluginVersionMinor);
static CornerPinMaskedPluginFactory<kPluginVersionMajor> p2(kPluginMaskedIdentifier, kPluginVersionMinor);
// version 1: "Invert" composes the extra matrix as homo3x3 * extraMat
// 1.1: overlay point selection, host selection rectangle param (Natron); the inverse of a transform matrix with huge entries (e.g. a concatenated source transform) no longer overflows to NaN or 0 (SupportExt #81)
static CornerPinPluginFactory<1> p1v1(kPluginIdentifier, 1);
static CornerPinMaskedPluginFactory<1> p2v1(kPluginMaskedIdentifier, 1);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p1v1)
mRegisterPluginFactoryInstance(p2v1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
