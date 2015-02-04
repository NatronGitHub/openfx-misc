/*
 OFX Transform plugin.
 
 Copyright (C) 2014 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
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
// Uncomment the following to enable the experimental host transform code.
#define ENABLE_HOST_TRANSFORM

#include "Transform.h"
#include "ofxsTransform3x3.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "TransformOFX"
#define kPluginMaskedName "TransformMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Translate / Rotate / Scale a 2D image."
#define kPluginIdentifier "net.sf.openfx.TransformPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.TransformMaskedPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

// NON-GENERIC
#define kParamTranslate "translate"
#define kParamTranslateLabel "Translate"
#define kParamRotate "rotate"
#define kParamRotateLabel "Rotate"
#define kParamScale "scale"
#define kParamScaleLabel "Scale"
#define kParamScaleUniform "uniform"
#define kParamScaleUniformLabel "Uniform"
#define kParamScaleUniformHint "Use the X scale for both directions"
#define kParamSkewX "skewX"
#define kParamSkewXLabel "Skew X"
#define kParamSkewY "skewY"
#define kParamSkewYLabel "Skew Y"
#define kParamSkewOrder "skewOrder"
#define kParamSkewOrderLabel "Skew Order"
#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamResetCenter "resetCenter"
#define kParamResetCenterLabel "Reset Center"
#define kParamResetCenterHint "Reset the position of the center to the center of the input region of definition"


#define CIRCLE_RADIUS_BASE 30.
#define CIRCLE_RADIUS_MIN 15.
#define CIRCLE_RADIUS_MAX 300.
#define POINT_SIZE 7.
#define ELLIPSE_N_POINTS 50.
#define SCALE_MIN 1e-8

using namespace OFX;

// round to the closest int, 1/10 int, etc
// this make parameter editing easier
// pscale is args.pixelScale.x / args.renderScale.x;
// pscale10 is the power of 10 below pscale
static double fround(double val, double pscale)
{
    double pscale10 = std::pow(10.,std::floor(std::log10(pscale)));
    return pscale10 * std::floor(val/pscale10 + 0.5);
}


static void getScale(const OfxPointD &scaleParam, bool scaleUniform, OfxPointD* scale)
{
    scale->x = scaleParam.x;
    if (std::fabs(scale->x) < SCALE_MIN) {
        scale->x = (scale->x >= 0) ? SCALE_MIN : -SCALE_MIN;
    }
    if (scaleUniform) {
        scale->y = scaleParam.x;
    } else {
        scale->y = scaleParam.y;
    }
    if (std::fabs(scale->y) < SCALE_MIN) {
        scale->y = (scale->y >= 0) ? SCALE_MIN : -SCALE_MIN;
    }
}


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle, bool masked)
    : Transform3x3Plugin(handle, masked)
    , _translate(0)
    , _rotate(0)
    , _scale(0)
    , _scaleUniform(0)
    , _skewX(0)
    , _skewY(0)
    , _skewOrder(0)
    , _center(0)
    {
        // NON-GENERIC
        _translate = fetchDouble2DParam(kParamTranslate);
        _rotate = fetchDoubleParam(kParamRotate);
        _scale = fetchDouble2DParam(kParamScale);
        _scaleUniform = fetchBooleanParam(kParamScaleUniform);
        _skewX = fetchDoubleParam(kParamSkewX);
        _skewY = fetchDoubleParam(kParamSkewY);
        _skewOrder = fetchChoiceParam(kParamSkewOrder);
        _center = fetchDouble2DParam(kParamCenter);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    void resetCenter(double time);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::DoubleParam* _skewX;
    OFX::DoubleParam* _skewY;
    OFX::ChoiceParam* _skewOrder;
    OFX::Double2DParam* _center;
};

// overridden is identity
bool
TransformPlugin::isIdentity(double time)
{
    // NON-GENERIC
    OfxPointD scale;
    OfxPointD translate;
    double rotate;
    double skewX, skewY;
    _scale->getValueAtTime(time, scale.x, scale.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    if (scaleUniform) {
        scale.y = scale.x;
    }
    _translate->getValueAtTime(time, translate.x, translate.y);
    _rotate->getValueAtTime(time, rotate);
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    
    if (scale.x == 1. && scale.y == 1. && translate.x == 0. && translate.y == 0. && rotate == 0. && skewX == 0. && skewY == 0.) {
        return true;
    }

    return false;
}

bool
TransformPlugin::getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double rotate;
    _rotate->getValueAtTime(time, rotate);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD scale;
    getScale(scaleParam, scaleUniform, &scale);

    double rot = OFX::ofxsToRadians(rotate);

    if (!invert) {
        *invtransform = OFX::ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    } else {
        *invtransform = OFX::ofxsMatTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    }
    return true;
}

void
TransformPlugin::resetCenter(double time)
{
    OfxRectD rod = srcClip_->getRegionOfDefinition(time);
    if (rod.x1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.x2 ||
        rod.y1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.y2) {
        return;
    }
    if (rod.x1 == 0. && rod.x2 == 0. && rod.y1 == 0. && rod.y2 == 0.) {
        OfxPointD offset = getProjectOffset();
        OfxPointD extent = getProjectExtent();
        rod.x1 = offset.x;
        rod.y1 = offset.y;
        rod.x2 = extent.x;
        rod.y2 = extent.y;
    }
    double currentRotation;
    _rotate->getValueAtTime(time, currentRotation);
    double rot = OFX::ofxsToRadians(currentRotation);

    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);

    OfxPointD scale;
    getScale(scaleParam, scaleUniform, &scale);

    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);

    OFX::Matrix3x3 Rinv = (ofxsMatRotation(-rot) *
                           ofxsMatSkewXY(skewX, skewY, skewOrder) *
                           ofxsMatScale(scale.x, scale.y));
    OfxPointD newCenter;
    newCenter.x = (rod.x1+rod.x2)/2;
    newCenter.y = (rod.y1+rod.y2)/2;
    double dxrot = newCenter.x - center.x;
    double dyrot = newCenter.y - center.y;
    OFX::Point3D dRot;
    dRot.x = dxrot;
    dRot.y = dyrot;
    dRot.z = 1;
    dRot = Rinv * dRot;
    if (dRot.z != 0) {
        dRot.x /= dRot.z;
        dRot.y /= dRot.z;
    }
    double dx = dRot.x;
    double dy = dRot.y;
    OfxPointD newTranslate;
    newTranslate.x = translate.x + dx - dxrot;
    newTranslate.y = translate.y + dy - dyrot;

    beginEditBlock("resetCenter");
    _center->setValue(newCenter.x, newCenter.y);
    _translate->setValue(newTranslate.x,newTranslate.y);
    endEditBlock();
}

void
TransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamResetCenter) {
        resetCenter(args.time);
    } else if (paramName == kParamTranslate ||
        paramName == kParamRotate ||
        paramName == kParamScale ||
        paramName == kParamScaleUniform ||
        paramName == kParamSkewX ||
        paramName == kParamSkewY ||
        paramName == kParamSkewOrder ||
        paramName == kParamCenter) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
TransformPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        resetCenter(args.time);
    }
}

////////////////////////////////////////////////////////////////////////////////
// stuff for the interact

class TransformInteract : public OFX::OverlayInteract
{
protected:
    enum DrawStateEnum {
        eInActive = 0, //< nothing happening
        eCircleHovered, //< the scale circle is hovered
        eLeftPointHovered, //< the left point of the circle is hovered
        eRightPointHovered, //< the right point of the circle is hovered
        eBottomPointHovered, //< the bottom point of the circle is hovered
        eTopPointHovered, //< the top point of the circle is hovered
        eCenterPointHovered, //< the center point of the circle is hovered
        eRotationBarHovered, //< the rotation bar is hovered
        eSkewXBarHoverered, //< the skew bar is hovered
        eSkewYBarHoverered //< the skew bar is hovered
    };
    
    enum MouseStateEnum {
        eReleased = 0,
        eDraggingCircle,
        eDraggingLeftPoint,
        eDraggingRightPoint,
        eDraggingTopPoint,
        eDraggingBottomPoint,
        eDraggingTranslation,
        eDraggingCenter,
        eDraggingRotationBar,
        eDraggingSkewXBar,
        eDraggingSkewYBar
    };
    
    enum OrientationEnum
    {
        eOrientationAllDirections = 0,
        eOrientationNotSet,
        eOrientationHorizontal,
        eOrientationVertical
    };
    
    DrawStateEnum _drawState;
    MouseStateEnum _mouseState;
    int _modifierStateCtrl;
    int _modifierStateShift;
    OrientationEnum _orientation;
    TransformPlugin* _plugin;
    OfxPointD _lastMousePos;
    
public:
    TransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _drawState(eInActive)
    , _mouseState(eReleased)
    , _modifierStateCtrl(0)
    , _modifierStateShift(0)
    , _orientation(eOrientationAllDirections)
    , _plugin(dynamic_cast<TransformPlugin*>(effect))
    , _lastMousePos()
    {
        
        assert(_plugin);
        _lastMousePos.x = _lastMousePos.y = 0.;
        // NON-GENERIC
        _translate = _plugin->fetchDouble2DParam(kParamTranslate);
        _rotate = _plugin->fetchDoubleParam(kParamRotate);
        _scale = _plugin->fetchDouble2DParam(kParamScale);
        _scaleUniform = _plugin->fetchBooleanParam(kParamScaleUniform);
        _skewX = _plugin->fetchDoubleParam(kParamSkewX);
        _skewY = _plugin->fetchDoubleParam(kParamSkewY);
        _skewOrder = _plugin->fetchChoiceParam(kParamSkewOrder);
        _center = _plugin->fetchDouble2DParam(kParamCenter);
        _invert = _plugin->fetchBooleanParam(kParamTransform3x3Invert);
        addParamToSlaveTo(_translate);
        addParamToSlaveTo(_rotate);
        addParamToSlaveTo(_scale);
        addParamToSlaveTo(_skewX);
        addParamToSlaveTo(_skewY);
        addParamToSlaveTo(_skewOrder);
        addParamToSlaveTo(_center);
        addParamToSlaveTo(_invert);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL;
    virtual bool penMotion(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool penUp(const OFX::PenArgs &args) OVERRIDE FINAL;
    virtual bool keyDown(const OFX::KeyArgs &args) OVERRIDE FINAL;
    virtual bool keyUp(const OFX::KeyArgs &args) OVERRIDE FINAL;

    /** @brief Called when the interact is loses input focus */
    virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;

private:
    void getTargetCenter(const OfxPointD &center, const OfxPointD &translate, OfxPointD *targetCenter)
    {
        targetCenter->x = center.x + translate.x;
        targetCenter->y = center.y + translate.y;
    }

    /*
    void getTargetCenter(double time, OfxPointD *center)
    {
        OfxPointD centerParam;
        _center->getValueAtTime(time, centerParam.x, centerParam.y);
        OfxPointD translate;
        _translate->getValueAtTime(time, translate.x, translate.y);

        return getTargetCenter(centerParam, translate, center);
    }
     */
    

    /*
    void getScale(double time, OfxPointD* scale)
    {
        OfxPointD scaleParam;
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
        bool scaleUniform;
        _scaleUniform->getValueAtTime(time, scaleUniform);

        return getScale(scaleParam, scaleUniform, scale);
    }
     */
    
    void getTargetRadius(const OfxPointD& scale, const OfxPointD& pixelScale, OfxPointD* targetRadius)
    {
        targetRadius->x = scale.x * CIRCLE_RADIUS_BASE;
        targetRadius->y = scale.y * CIRCLE_RADIUS_BASE;
        // don't draw too small. 15 pixels is the limit
        if (std::fabs(targetRadius->x) < CIRCLE_RADIUS_MIN && std::fabs(targetRadius->y) < CIRCLE_RADIUS_MIN) {
            targetRadius->x = targetRadius->x >= 0 ? CIRCLE_RADIUS_MIN : -CIRCLE_RADIUS_MIN;
            targetRadius->y = targetRadius->y >= 0 ? CIRCLE_RADIUS_MIN : -CIRCLE_RADIUS_MIN;
        } else if (std::fabs(targetRadius->x) > CIRCLE_RADIUS_MAX && std::fabs(targetRadius->y) > CIRCLE_RADIUS_MAX) {
            targetRadius->x = targetRadius->x >= 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
            targetRadius->y = targetRadius->y >= 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
        } else {
            if (std::fabs(targetRadius->x) < CIRCLE_RADIUS_MIN) {
                if (targetRadius->x == 0. && targetRadius->y != 0.) {
                    targetRadius->y = targetRadius->y > 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
                } else {
                    targetRadius->y *= std::fabs(CIRCLE_RADIUS_MIN/targetRadius->x);
                }
                targetRadius->x = targetRadius->x >= 0 ? CIRCLE_RADIUS_MIN : -CIRCLE_RADIUS_MIN;
            }
            if (std::fabs(targetRadius->x) > CIRCLE_RADIUS_MAX) {
                targetRadius->y *= std::fabs(CIRCLE_RADIUS_MAX/targetRadius->x);
                targetRadius->x = targetRadius->x > 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
            }
            if (std::fabs(targetRadius->y) < CIRCLE_RADIUS_MIN) {
                if (targetRadius->y == 0. && targetRadius->x != 0.) {
                    targetRadius->x = targetRadius->x > 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
                } else {
                    targetRadius->x *= std::fabs(CIRCLE_RADIUS_MIN/targetRadius->y);
                }
                targetRadius->y = targetRadius->y >= 0 ? CIRCLE_RADIUS_MIN : -CIRCLE_RADIUS_MIN;
            }
            if (std::fabs(targetRadius->y) > CIRCLE_RADIUS_MAX) {
                targetRadius->x *= std::fabs(CIRCLE_RADIUS_MAX/targetRadius->x);
                targetRadius->y = targetRadius->y > 0 ? CIRCLE_RADIUS_MAX : -CIRCLE_RADIUS_MAX;
            }
        }
        // the circle axes are not aligned with the images axes, so we cannot use the x and y scales separately
        double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
        targetRadius->x *= meanPixelScale;
        targetRadius->y *= meanPixelScale;
    }

    /*
    void getTargetRadius(double time, const OfxPointD& pixelScale, OfxPointD* targetRadius)
    {
        OfxPointD scale;
        getScale(time, &scale);

        return getTargetRadius(scale, pixelScale, targetRadius);
    }
     */
    
    void getTargetPoints(const OfxPointD& targetCenter,
                   const OfxPointD& targetRadius,
                   OfxPointD *left,
                   OfxPointD *bottom,
                   OfxPointD *top,
                   OfxPointD *right)
    {
        left->x = targetCenter.x - targetRadius.x ;
        left->y = targetCenter.y;
        right->x = targetCenter.x + targetRadius.x ;
        right->y = targetCenter.y;
        top->x = targetCenter.x;
        top->y = targetCenter.y + targetRadius.y ;
        bottom->x = targetCenter.x;
        bottom->y = targetCenter.y - targetRadius.y ;
    }

    /*
    void getTargetPoints(double time,
                   const OfxPointD& pixelScale,
                   OfxPointD *targetCenter,
                   OfxPointD *left,
                   OfxPointD *bottom,
                   OfxPointD *top,
                   OfxPointD *right)
    {
        ienter(time, center);
        OfxPointD targetRadius;
        getTargetRadius(time, pixelScale, &targetRadius);

        return getTargetPoints(*targetCenter, targetRadius, left, bottom, top, right);
    }
     */
    

    OfxRangeD getViewportSize()
    {
        OfxRangeD ret;
        ret.min = getProperties().propGetDouble(kOfxInteractPropViewportSize, 0);
        ret.max = getProperties().propGetDouble(kOfxInteractPropViewportSize, 1);
        return ret;
    }

private:
    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::DoubleParam* _skewX;
    OFX::DoubleParam* _skewY;
    OFX::ChoiceParam* _skewOrder;
    OFX::Double2DParam* _center;
    OFX::BooleanParam* _invert;
};

static void
drawSquare(const OfxRGBColourD& color,
           const OfxPointD& center,
           const OfxPointD& pixelScale,
           bool hovered,
           bool althovered,
           int l)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        if (althovered) {
            glColor3f(0.f*l, 1.f*l, 0.f*l);
        } else {
            glColor3f(1.f*l, 0.f*l, 0.f*l);
        }
    } else {
        glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
    }
    double halfWidth = (POINT_SIZE / 2.) * meanPixelScale;
    double halfHeight = (POINT_SIZE / 2.) * meanPixelScale;
    glPushMatrix();
    glTranslated(center.x, center.y, 0.);
    glBegin(GL_POLYGON);
    glVertex2d(- halfWidth, - halfHeight); // bottom left
    glVertex2d(- halfWidth, + halfHeight); // top left
    glVertex2d(+ halfWidth, + halfHeight); // bottom right
    glVertex2d(+ halfWidth, - halfHeight); // top right
    glEnd();
    glPopMatrix();
    
}

static void
drawEllipse(const OfxRGBColourD& color,
            const OfxPointD& center,
            const OfxPointD& targetRadius,
            bool hovered,
            int l)
{
    if (hovered) {
        glColor3f(1.f*l, 0.f*l, 0.f*l);
    } else {
        glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
    }

    glPushMatrix();
    //  center the oval at x_center, y_center
    glTranslatef((float)center.x, (float)center.y, 0.f);
    //  draw the oval using line segments
    glBegin(GL_LINE_LOOP);
    // we don't need to be pixel-perfect here, it's just an interact!
    // 40 segments is enough.
    for (int i = 0; i < 40; ++i) {
        double theta = i * 2 * OFX::ofxsPi() / 40.;
        glVertex2d(targetRadius.x * std::cos(theta), targetRadius.y * std::sin(theta));
    }
    glEnd();
    
    glPopMatrix();
}

static void
drawSkewBar(const OfxRGBColourD& color,
            const OfxPointD &center,
            const OfxPointD& pixelScale,
            double targetRadiusY,
            bool hovered,
            double angle,
            int l)
{
    if (hovered) {
        glColor3f(1.f*l, 0.f*l, 0.f*l);
    } else {
        glColor3f((float)color.r*l, (float)color.g*l, (float)color.b*l);
    }

    // we are not axis-aligned: use the mean pixel scale
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusY + 20. * meanPixelScale;

    glPushMatrix();
    glTranslatef((float)center.x, (float)center.y, 0.f);
    glRotated(angle, 0, 0, 1);
    
    glBegin(GL_LINES);
    glVertex2d(0., - barHalfSize);
    glVertex2d(0., + barHalfSize);
    
    if (hovered) {
        double arrowYPosition = targetRadiusY + 10. * meanPixelScale;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        ///draw the central bar
        glVertex2d(- arrowXHalfSize, - arrowYPosition);
        glVertex2d(+ arrowXHalfSize, - arrowYPosition);
        
        ///left triangle
        glVertex2d(- arrowXHalfSize, -  arrowYPosition);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, - arrowYPosition + arrowHeadOffsetY);
        
        glVertex2d(- arrowXHalfSize,- arrowYPosition);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, - arrowYPosition - arrowHeadOffsetY);
        
        ///right triangle
        glVertex2d(+ arrowXHalfSize,- arrowYPosition);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, - arrowYPosition + arrowHeadOffsetY);
        
        glVertex2d(+ arrowXHalfSize,- arrowYPosition);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, - arrowYPosition - arrowHeadOffsetY);
    }
    glEnd();
    glPopMatrix();
}


static void
drawRotationBar(const OfxRGBColourD& color,
                const OfxPointD& pixelScale,
                double targetRadiusX,
                bool hovered,
                bool inverted,
                int l)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        glColor3f(1.f*l, 0.f*l, 0.f*l);
    } else {
        glColor3f(color.r*l, color.g*l, color.b*l);
    }

    double barExtra = 30. * meanPixelScale;
    glBegin(GL_LINES);
    glVertex2d(0., 0.);
    glVertex2d(0. + targetRadiusX + barExtra, 0.);
    glEnd();

    if (hovered) {
        double arrowCenterX = targetRadiusX + barExtra / 2.;
        
        ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
        OfxPointD arrowRadius;
        arrowRadius.x = 5. * meanPixelScale;
        arrowRadius.y = 10. * meanPixelScale;
        
        glPushMatrix();
        //  center the oval at x_center, y_center
        glTranslatef((float)arrowCenterX, 0.f, 0);
        //  draw the oval using line segments
        glBegin(GL_LINE_STRIP);
        glVertex2d(0, arrowRadius.y);
        glVertex2d(arrowRadius.x, 0.);
        glVertex2d(0, -arrowRadius.y);
        glEnd();
        

        glBegin(GL_LINES);
        ///draw the top head
        glVertex2d(0., arrowRadius.y);
        glVertex2d(0., arrowRadius.y - 5. * meanPixelScale);
        
        glVertex2d(0., arrowRadius.y);
        glVertex2d(4. * meanPixelScale, arrowRadius.y - 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        ///draw the bottom head
        glVertex2d(0., -arrowRadius.y);
        glVertex2d(0., -arrowRadius.y + 5. * meanPixelScale);
        
        glVertex2d(0., -arrowRadius.y);
        glVertex2d(4. * meanPixelScale, -arrowRadius.y + 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        glEnd();

        glPopMatrix();
    }
    if (inverted) {
        double arrowXPosition = targetRadiusX + barExtra * 1.5;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        glPushMatrix();
        glTranslatef((float)arrowXPosition, 0, 0);

        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);

        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();

        glRotated(90., 0., 0., 1.);

        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);

        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();
        
        glPopMatrix();
    }

}

// draw the interact
bool
TransformInteract::draw(const OFX::DrawArgs &args)
{
    //std::cout << "pixelScale= "<<args.pixelScale.x << "," << args.pixelScale.y << " renderscale=" << args.renderScale.x << "," << args.renderScale.y << std::endl;
    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;
    OfxRGBColourD color = { 0.8, 0.8, 0.8 };
    getSuggestedColour(color);

    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double rotate;
    _rotate->getValueAtTime(time, rotate);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);
    bool inverted;
    _invert->getValueAtTime(time, inverted);

    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    getScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);


    GLdouble skewMatrix[16];
    skewMatrix[0] = (skewOrder ? 1. : (1.+skewX*skewY)); skewMatrix[1] = skewY; skewMatrix[2] = 0.; skewMatrix[3] = 0;
    skewMatrix[4] = skewX; skewMatrix[5] = (skewOrder ? (1.+skewX*skewY) : 1.); skewMatrix[6] = 0.; skewMatrix[7] = 0;
    skewMatrix[8] = 0.; skewMatrix[9] = 0.; skewMatrix[10] = 1.; skewMatrix[11] = 0;
    skewMatrix[12] = 0.; skewMatrix[13] = 0.; skewMatrix[14] = 0.; skewMatrix[15] = 1.;

    glPushAttrib(GL_ALL_ATTRIB_BITS);

    //glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    //glEnable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5f);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        glMatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        glTranslated(direction * pscale.x / 256, -direction * pscale.y / 256, 0);
        glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke
        
        glColor3f(color.r*l, color.g*l, color.b*l);

        glPushMatrix();
        glTranslated(targetCenter.x, targetCenter.y, 0.);

        glRotated(rotate, 0, 0., 1.);
        drawRotationBar(color, pscale, targetRadius.x, _mouseState == eDraggingRotationBar || _drawState == eRotationBarHovered, inverted, l);
        glMultMatrixd(skewMatrix);
        glTranslated(-targetCenter.x, -targetCenter.y, 0.);

        drawEllipse(color, targetCenter, targetRadius, _mouseState == eDraggingCircle || _drawState == eCircleHovered, l);

        // add 180 to the angle to draw the arrows on the other side. unfortunately, this requires knowing
        // the mouse position in the ellipse frame
        double flip = 0.;
        if (_drawState == eSkewXBarHoverered || _drawState == eSkewYBarHoverered) {
            double rot = OFX::ofxsToRadians(rotate);
            OFX::Matrix3x3 transformscale;
            transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);

            OFX::Point3D previousPos;
            previousPos.x = _lastMousePos.x;
            previousPos.y = _lastMousePos.y;
            previousPos.z = 1.;
            previousPos = transformscale * previousPos;
            if (previousPos.z != 0) {
                previousPos.x /= previousPos.z;
                previousPos.y /= previousPos.z;
            }
            if ((_drawState == eSkewXBarHoverered && previousPos.y > targetCenter.y) ||
                (_drawState == eSkewYBarHoverered && previousPos.x > targetCenter.x)) {
                flip = 180.;
            }
        }
        drawSkewBar(color, targetCenter, pscale, targetRadius.y, _mouseState == eDraggingSkewXBar || _drawState == eSkewXBarHoverered, flip, l);
        drawSkewBar(color, targetCenter, pscale, targetRadius.x, _mouseState == eDraggingSkewYBar || _drawState == eSkewYBarHoverered, flip - 90., l);


        drawSquare(color, targetCenter, pscale, _mouseState == eDraggingTranslation || _mouseState == eDraggingCenter || _drawState == eCenterPointHovered, _modifierStateCtrl, l);
        drawSquare(color, left, pscale, _mouseState == eDraggingLeftPoint || _drawState == eLeftPointHovered, false, l);
        drawSquare(color, right, pscale, _mouseState == eDraggingRightPoint || _drawState == eRightPointHovered, false, l);
        drawSquare(color, top, pscale, _mouseState == eDraggingTopPoint || _drawState == eTopPointHovered, false, l);
        drawSquare(color, bottom, pscale, _mouseState == eDraggingBottomPoint || _drawState == eBottomPointHovered, false, l);

        glPopMatrix();
    }
    glPopAttrib();

    return true;
}

static bool squareContains(const OFX::Point3D& pos,
                           const OfxRectD& rect,
                           double toleranceX= 0.,
                           double toleranceY = 0.)
{
    return (pos.x >= (rect.x1 - toleranceX) && pos.x < (rect.x2 + toleranceX)
            && pos.y >= (rect.y1 - toleranceY) && pos.y < (rect.y2 + toleranceY));
}

static bool isOnEllipseBorder(const OFX::Point3D& pos,
                              const OfxPointD& targetRadius,
                              const OfxPointD& targetCenter,
                              double epsilon = 0.1)
{

    double v = ((pos.x - targetCenter.x) * (pos.x - targetCenter.x) / (targetRadius.x * targetRadius.x) +
                (pos.y - targetCenter.y) * (pos.y - targetCenter.y) / (targetRadius.y * targetRadius.y));
    if (v <= (1. + epsilon) && v >= (1. - epsilon)) {
        return true;
    }
    return false;
}

static bool isOnSkewXBar(const OFX::Point3D& pos,
                         double targetRadiusY,
                         const OfxPointD& center,
                         const OfxPointD& pixelScale,
                         double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusY + (20. * meanPixelScale);
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
        pos.y >= (center.y - barHalfSize - tolerance) && pos.y <= (center.y + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnSkewYBar(const OFX::Point3D& pos,
                         double targetRadiusX,
                         const OfxPointD& center,
                         const OfxPointD& pixelScale,
                         double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = targetRadiusX + (20. * meanPixelScale);
    if (pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance) &&
        pos.x >= (center.x - barHalfSize - tolerance) && pos.x <= (center.x + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnRotationBar(const  OFX::Point3D& pos,
                            double targetRadiusX,
                            const OfxPointD& center,
                            const OfxPointD& pixelScale,
                            double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barExtra = 30. * meanPixelScale;
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + targetRadiusX + barExtra + tolerance) &&
        pos.y >= (center.y  - tolerance) && pos.y <= (center.y + tolerance)) {
        return true;
    }

    return false;
}

static OfxRectD rectFromCenterPoint(const OfxPointD& center,
                                    const OfxPointD& pixelScale)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    OfxRectD ret;
    ret.x1 = center.x - (POINT_SIZE / 2.) * meanPixelScale;
    ret.x2 = center.x + (POINT_SIZE / 2.) * meanPixelScale;
    ret.y1 = center.y - (POINT_SIZE / 2.) * meanPixelScale;
    ret.y2 = center.y + (POINT_SIZE / 2.) * meanPixelScale;
    return ret;
}


// overridden functions from OFX::Interact to do things
bool TransformInteract::penMotion(const OFX::PenArgs &args)
{
    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;

    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double currentRotation;
    _rotate->getValueAtTime(args.time, currentRotation);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrder);

    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    getScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(targetCenter, pscale);
    OfxRectD leftPoint = rectFromCenterPoint(left, pscale);
    OfxRectD rightPoint = rectFromCenterPoint(right, pscale);
    OfxRectD topPoint = rectFromCenterPoint(top, pscale);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom, pscale);
    

    //double dx = args.penPosition.x - _lastMousePos.x;
    //double dy = args.penPosition.y - _lastMousePos.y;
    
    double rot = OFX::ofxsToRadians(currentRotation);
    

    OFX::Point3D penPos, prevPenPos, rotationPos, transformedPos, previousPos, currentPos;
    penPos.x = args.penPosition.x;
    penPos.y = args.penPosition.y;
    penPos.z = 1.;
    prevPenPos.x = _lastMousePos.x;
    prevPenPos.y = _lastMousePos.y;
    prevPenPos.z = 1.;

    OFX::Matrix3x3 rotation, transform, transformscale;
    ////for the rotation bar/translation/center dragging we dont use the same transform, we don't want to undo the rotation transform
    if (_mouseState != eDraggingTranslation && _mouseState != eDraggingCenter) {
        ///undo skew + rotation to the current position
        rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, targetCenter.x, targetCenter.y);
        transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);
        transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);
    } else {
        rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, 0., targetCenter.x, targetCenter.y);
        transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, 0., targetCenter.x, targetCenter.y);
        transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrder, 0., targetCenter.x, targetCenter.y);
    }

    rotationPos = rotation * penPos;
    if (rotationPos.z != 0) {
        rotationPos.x /= rotationPos.z;
        rotationPos.y /= rotationPos.z;
    }
    
    transformedPos = transform * penPos;
    if (transformedPos.z != 0) {
        transformedPos.x /= transformedPos.z;
        transformedPos.y /= transformedPos.z;
    }
    
    previousPos = transformscale * prevPenPos;
    if (previousPos.z != 0) {
        previousPos.x /= previousPos.z;
        previousPos.y /= previousPos.z;
    }
    
    currentPos = transformscale * penPos;
    if (currentPos.z != 0) {
        currentPos.x /= currentPos.z;
        currentPos.y /= currentPos.z;
    }

    bool ret = true;
    if (_mouseState == eReleased) {
        // we are not axis-aligned
        double meanPixelScale = (pscale.x + pscale.y) / 2.;
        double hoverTolerance = (POINT_SIZE / 2.) * meanPixelScale;
        if (squareContains(transformedPos, centerPoint)) {
            _drawState = eCenterPointHovered;
        } else if (squareContains(transformedPos, leftPoint)) {
            _drawState = eLeftPointHovered;
        } else if (squareContains(transformedPos, rightPoint)) {
            _drawState = eRightPointHovered;
        } else if (squareContains(transformedPos, topPoint)) {
            _drawState = eTopPointHovered;
        } else if (squareContains(transformedPos, bottomPoint)) {
            _drawState = eBottomPointHovered;
        } else if (isOnEllipseBorder(transformedPos, targetRadius, targetCenter)) {
            _drawState = eCircleHovered;
        } else if (isOnRotationBar(rotationPos, targetRadius.x, targetCenter, pscale, hoverTolerance)) {
            _drawState = eRotationBarHovered;
        } else if (isOnSkewXBar(transformedPos, targetRadius.y, targetCenter, pscale, hoverTolerance)) {
            _drawState = eSkewXBarHoverered;
        } else if (isOnSkewYBar(transformedPos, targetRadius.x, targetCenter, pscale, hoverTolerance)) {
            _drawState = eSkewYBarHoverered;
        } else {
            _drawState = eInActive;
            ret = false;
        }
    } else if (_mouseState == eDraggingCircle) {
        double minX,minY,maxX,maxY;
        _scale->getRange(minX, minY, maxX, maxY);
        
        // we need to compute the backtransformed points with the scale
        
        // the scale ratio is the ratio of distances to the center
        double prevDistSq = (targetCenter.x - previousPos.x)*(targetCenter.x - previousPos.x) + (targetCenter.y - previousPos.y)*(targetCenter.y - previousPos.y);
        if (prevDistSq != 0.) {
            const double distSq = (targetCenter.x - currentPos.x)*(targetCenter.x - currentPos.x) + (targetCenter.y - currentPos.y)*(targetCenter.y - currentPos.y);
            const double distRatio = std::sqrt(distSq/prevDistSq);
            scale.x *= distRatio;
            scale.y *= distRatio;
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingLeftPoint || _mouseState == eDraggingRightPoint) {
        // avoid division by zero
        if (targetCenter.x != previousPos.x) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (targetCenter.x - currentPos.x)/(targetCenter.x - previousPos.x);
            OfxPointD newScale;
            newScale.x = scale.x * scaleRatio;
            newScale.x = std::max(minX, std::min(newScale.x, maxX));
            newScale.y = scaleUniform ? newScale.x : scale.y;
            _scale->setValue(newScale.x, newScale.y);
        }
    } else if (_mouseState == eDraggingTopPoint || _mouseState == eDraggingBottomPoint) {
        // avoid division by zero
        if (targetCenter.y != previousPos.y) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (targetCenter.y - currentPos.y)/(targetCenter.y - previousPos.y);
            OfxPointD newScale;
            newScale.y = scale.y * scaleRatio;
            newScale.y = std::max(minY, std::min(newScale.y, maxY));
            newScale.x = scaleUniform ? newScale.y : scale.x;
            _scale->setValue(newScale.x, newScale.y);
        }
    } else if (_mouseState == eDraggingTranslation) {
        double dx = args.penPosition.x - _lastMousePos.x;
        double dy = args.penPosition.y - _lastMousePos.y;
        
        if (_orientation == eOrientationNotSet && _modifierStateShift > 0) {
            _orientation = std::abs(dx) > std::abs(dy) ? eOrientationHorizontal : eOrientationVertical;
        }
        
        dx = _orientation == eOrientationVertical ? 0 : dx;
        dy = _orientation == eOrientationHorizontal ? 0 : dy;
        double newx = translate.x + dx;
        double newy = translate.y + dy;
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
        newx = fround(newx, pscale.x);
        newy = fround(newy, pscale.y);
        _translate->setValue(newx,newy);
    } else if (_mouseState == eDraggingCenter) {
        OfxPointD currentCenter;
        _center->getValueAtTime(args.time, currentCenter.x, currentCenter.y);
        OFX::Matrix3x3 R = ofxsMatScale(1. / scale.x, 1. / scale.y) * ofxsMatSkewXY(-skewX, -skewY, !skewOrder) * ofxsMatRotation(rot);

        double dx = args.penPosition.x - _lastMousePos.x;
        double dy = args.penPosition.y - _lastMousePos.y;
        
        if (_orientation == eOrientationNotSet && _modifierStateShift > 0) {
            _orientation = std::abs(dx) > std::abs(dy) ? eOrientationHorizontal : eOrientationVertical;
        }
        
        dx = _orientation == eOrientationVertical ? 0 : dx;
        dy = _orientation == eOrientationHorizontal ? 0 : dy;
        
        OFX::Point3D dRot;
        dRot.x = dx;
        dRot.y = dy;
        dRot.z = 1.;
        dRot = R * dRot;
        if (dRot.z != 0) {
            dRot.x /= dRot.z;
            dRot.y /= dRot.z;
        }
        double dxrot = dRot.x;
        double dyrot = dRot.y;
        double newx = currentCenter.x + dxrot;
        double newy = currentCenter.y + dyrot;
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
        newx = fround(newx, pscale.x);
        newy = fround(newy, pscale.y);
        _plugin->beginEditBlock("setCenter");
        _center->setValue(newx,newy);
        // recompute dxrot,dyrot after rounding
        double det = ofxsMatDeterminant(R);
        if (det != 0.) {
            OFX::Matrix3x3 Rinv = ofxsMatInverse(R, det);

            dxrot = newx - currentCenter.x;
            dyrot = newy - currentCenter.y;
            dRot.x = dxrot;
            dRot.y = dyrot;
            dRot.z = 1;
            dRot = Rinv * dRot;
            if (dRot.z != 0) {
                dRot.x /= dRot.z;
                dRot.y /= dRot.z;
            }
            dx = dRot.x;
            dy = dRot.y;
            OfxPointD newTranslation;
            newTranslation.x = translate.x + dx - dxrot;
            newTranslation.y = translate.y + dy - dyrot;
            _translate->setValue(newTranslation.x, newTranslation.y);
        }
        _plugin->endEditBlock();
    } else if (_mouseState == eDraggingRotationBar) {
        OfxPointD diffToCenter;
        ///the current mouse position (untransformed) is doing has a certain angle relative to the X axis
        ///which can be computed by : angle = arctan(opposite / adjacent)
        diffToCenter.y = rotationPos.y - targetCenter.y;
        diffToCenter.x = rotationPos.x - targetCenter.x;
        double angle = std::atan2(diffToCenter.y, diffToCenter.x);
        double angledegrees = currentRotation+OFX::ofxsToDegrees(angle);
        double closest90 = 90. * std::floor((angledegrees + 45.)/90.);
        if (std::fabs(angledegrees - closest90) < 5.) {
            // snap to closest multiple of 90.
            angledegrees = closest90;
        }
        _rotate->setValue(angledegrees);
        
    } else if (_mouseState == eDraggingSkewXBar) {
        // avoid division by zero
        if (scale.y != 0. && targetCenter.y != previousPos.y) {
            const double addSkew = (scale.x/scale.y)*(currentPos.x - previousPos.x)/(currentPos.y - targetCenter.y);
            _skewX->setValue(skewX + addSkew);
        }
    } else if (_mouseState == eDraggingSkewYBar) {
        // avoid division by zero
        if (scale.x != 0. && targetCenter.x != previousPos.x) {
            const double addSkew = (scale.y/scale.x)*(currentPos.y - previousPos.y)/(currentPos.x - targetCenter.x);
            _skewY->setValue(skewY + addSkew);
        }
    } else {
        assert(false);
    }
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
    
}

bool TransformInteract::penDown(const OFX::PenArgs &args)
{
    using OFX::Matrix3x3;

    const OfxPointD &pscale = args.pixelScale;
    const double time = args.time;

    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double currentRotation;
    _rotate->getValueAtTime(time, currentRotation);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD targetCenter;
    getTargetCenter(center, translate, &targetCenter);

    OfxPointD scale;
    getScale(scaleParam, scaleUniform, &scale);

    OfxPointD targetRadius;
    getTargetRadius(scale, pscale, &targetRadius);

    OfxPointD left, right, bottom, top;
    getTargetPoints(targetCenter, targetRadius, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(targetCenter, pscale);
    OfxRectD leftPoint = rectFromCenterPoint(left, pscale);
    OfxRectD rightPoint = rectFromCenterPoint(right, pscale);
    OfxRectD topPoint = rectFromCenterPoint(top, pscale);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom, pscale);
    

    OFX::Point3D transformedPos, rotationPos;
    transformedPos.x = args.penPosition.x;
    transformedPos.y = args.penPosition.y;
    transformedPos.z = 1.;
    
    double rot = OFX::ofxsToRadians(currentRotation);

    ///now undo skew + rotation to the current position
    OFX::Matrix3x3 rotation, transform;
    rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, targetCenter.x, targetCenter.y);
    transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrder, rot, targetCenter.x, targetCenter.y);

    rotationPos = rotation * transformedPos;
    if (rotationPos.z != 0) {
        rotationPos.x /= rotationPos.z;
        rotationPos.y /= rotationPos.z;
    }
    transformedPos = transform * transformedPos;
    if (transformedPos.z != 0) {
        transformedPos.x /= transformedPos.z;
        transformedPos.y /= transformedPos.z;
    }
    
    _orientation = eOrientationAllDirections;
    
    double pressToleranceX = 5 * pscale.x;
    double pressToleranceY = 5 * pscale.y;
    bool ret = true;
    if (squareContains(transformedPos, centerPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = _modifierStateCtrl ? eDraggingCenter : eDraggingTranslation;
        if (_modifierStateShift > 0) {
            _orientation = eOrientationNotSet;
        }
    } else if (squareContains(transformedPos, leftPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingLeftPoint;
    } else if (squareContains(transformedPos, rightPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingRightPoint;
    } else if (squareContains(transformedPos, topPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingTopPoint;
    } else if (squareContains(transformedPos, bottomPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingBottomPoint;
    } else if (isOnEllipseBorder(transformedPos, targetRadius, targetCenter)) {
        _mouseState = eDraggingCircle;
    } else if (isOnRotationBar(rotationPos, targetRadius.x, targetCenter, pscale, pressToleranceY)) {
        _mouseState = eDraggingRotationBar;
    } else if (isOnSkewXBar(transformedPos, targetRadius.y, targetCenter, pscale, pressToleranceY)) {
        _mouseState = eDraggingSkewXBar;
    } else if (isOnSkewYBar(transformedPos, targetRadius.x, targetCenter, pscale, pressToleranceX)) {
        _mouseState = eDraggingSkewYBar;
    } else {
        _mouseState = eReleased;
        ret =  false;
    }
    
    _lastMousePos = args.penPosition;
    
    _effect->redrawOverlays();
    return ret;
}

bool TransformInteract::penUp(const OFX::PenArgs &args)
{
    bool ret = _mouseState != eReleased;
    _mouseState = eReleased;
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
}

// keyDown just updates the modifier state
bool TransformInteract::keyDown(const OFX::KeyArgs &args)
{
    // Note that on the Mac:
    // cmd/apple/cloverleaf is kOfxKey_Control_L
    // ctrl is kOfxKey_Meta_L
    // alt/option is kOfxKey_Alt_L
    bool mustRedraw = false;

    // the two control keys may be pressed consecutively, be aware about this
    if (args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R) {
        mustRedraw = _modifierStateCtrl == 0;
        ++_modifierStateCtrl;
    }
    if (args.keySymbol == kOfxKey_Shift_L || args.keySymbol == kOfxKey_Shift_R) {
        mustRedraw = _modifierStateShift == 0;
        ++_modifierStateShift;
        if (_modifierStateShift > 0) {
            _orientation = eOrientationNotSet;
        }
    }
    if (mustRedraw) {
        _effect->redrawOverlays();
    }
    //std::cout << std::hex << args.keySymbol << std::endl;
    // modifiers are not "caught"
    return false;
}

// keyUp just updates the modifier state
bool TransformInteract::keyUp(const OFX::KeyArgs &args)
{
    bool mustRedraw = false;

    if (args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R) {
        // we may have missed a keypress
        if (_modifierStateCtrl > 0) {
            --_modifierStateCtrl;
            mustRedraw = _modifierStateCtrl == 0;
        }
    }
    if (args.keySymbol == kOfxKey_Shift_L || args.keySymbol == kOfxKey_Shift_R) {
        if (_modifierStateShift > 0) {
            --_modifierStateShift;
            mustRedraw = _modifierStateShift == 0;
        }
        if (_modifierStateShift == 0) {
            _orientation = eOrientationAllDirections;
        }
    }
    if (mustRedraw) {
        _effect->redrawOverlays();
    }
    // modifiers are not "caught"
    return false;
}

/** @brief Called when the interact is loses input focus */
void TransformInteract::loseFocus(const FocusArgs &/*args*/)
{
    // reset the modifiers state
    _modifierStateCtrl = 0;
    _modifierStateShift = 0;
}


using namespace OFX;

mDeclarePluginFactory(TransformPluginFactory, {}, {});

class TransformOverlayDescriptor : public DefaultEffectOverlayDescriptor<TransformOverlayDescriptor, TransformInteract> {};

static
void TransformPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    // translate
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTranslate);
        param->setLabels(kParamTranslateLabel, kParamTranslateLabel, kParamTranslateLabel);
        //param->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
        param->setDoubleType(eDoubleTypeXYAbsolute);
        //param->setDimensionLabels("x","y");
        param->setDefault(0, 0);
        param->setIncrement(10.);
        page->addChild(*param);
    }

    // rotate
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRotate);
        param->setLabels(kParamRotateLabel, kParamRotateLabel, kParamRotateLabel);
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0);
        //param->setRange(-180, 180); // the angle may be -infinity..+infinity
        param->setDisplayRange(-180, 180);
        param->setIncrement(0.1);
        page->addChild(*param);
    }

    // scale
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamScale);
        param->setLabels(kParamScaleLabel, kParamScaleLabel, kParamScaleLabel);
        param->setDoubleType(eDoubleTypeScale);
        //param->setDimensionLabels("w","h");
        param->setDefault(1,1);
        //param->setRange(0.1,0.1,10,10);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setIncrement(0.01);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    // scaleUniform
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamScaleUniform);
        param->setLabels(kParamScaleUniformLabel, kParamScaleUniformLabel, kParamScaleUniformLabel);
        param->setHint(kParamScaleUniformHint);
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // skewX
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSkewX);
        param->setLabels(kParamSkewXLabel, kParamSkewXLabel, kParamSkewXLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        page->addChild(*param);
    }

    // skewY
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSkewY);
        param->setLabels(kParamSkewYLabel, kParamSkewYLabel, kParamSkewYLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        page->addChild(*param);
    }

    // skewOrder
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSkewOrder);
        param->setLabels(kParamSkewOrderLabel, kParamSkewOrderLabel, kParamSkewOrderLabel);
        param->setDefault(0);
        param->appendOption("XY");
        param->appendOption("YX");
        param->setAnimates(true);
        page->addChild(*param);
    }

    // center
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamCenter);
        param->setLabels(kParamCenterLabel, kParamCenterLabel, kParamCenterLabel);
        //param->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
        param->setDoubleType(eDoubleTypeXYAbsolute);
        //param->setDimensionLabels("x","y");
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0.5, 0.5);
        param->setIncrement(1.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    // resetcenter
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetCenter);
        param->setLabels(kParamResetCenterLabel, kParamResetCenterLabel, kParamResetCenterLabel);
        param->setHint(kParamResetCenterHint);
        page->addChild(*param);
    }
}

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}

void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false);
}

OFX::ImageEffect* TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, false);
}


mDeclarePluginFactory(TransformMaskedPluginFactory, {}, {});

void TransformMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginMaskedName, kPluginMaskedName, kPluginMaskedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}


void TransformMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true);
}

OFX::ImageEffect* TransformMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true);
}

void getTransformPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        static TransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static TransformMaskedPluginFactory p(kPluginMaskedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
