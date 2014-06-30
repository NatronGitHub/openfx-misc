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
//#define ENABLE_HOST_TRANSFORM

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
#define kPluginIdentifier "net.sf.openfx:TransformPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx:TransformMaskedPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

// NON-GENERIC
#define kTranslateParamName "translate"
#define kTranslateParamLabel "Translate"
#define kRotateParamName "rotate"
#define kRotateParamLabel "Rotate"
#define kScaleParamName "scale"
#define kScaleParamLabel "Scale"
#define kScaleUniformParamName "uniform"
#define kScaleUniformParamLabel "Uniform"
#define kScaleUniformParamHint "Use same scale for both directions"
#define kSkewXParamName "skewX"
#define kSkewXParamLabel "Skew X"
#define kSkewYParamName "skewY"
#define kSkewYParamLabel "Skew Y"
#define kSkewOrderParamName "skewOrder"
#define kSkewOrderParamLabel "Skew Order"
#define kCenterParamName "center"
#define kCenterParamLabel "Center"


#define CIRCLE_RADIUS_BASE 30.0
#define POINT_SIZE 7.0
#define ELLIPSE_N_POINTS 50.0

using namespace OFX;





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
        _translate = fetchDouble2DParam(kTranslateParamName);
        _rotate = fetchDoubleParam(kRotateParamName);
        _scale = fetchDouble2DParam(kScaleParamName);
        _scaleUniform = fetchBooleanParam(kScaleUniformParamName);
        _skewX = fetchDoubleParam(kSkewXParamName);
        _skewY = fetchDoubleParam(kSkewYParamName);
        _skewOrder = fetchChoiceParam(kSkewOrderParamName);
        _center = fetchDouble2DParam(kCenterParamName);
    }

private:
    virtual bool isIdentity(double time) /*OVERRIDE FINAL*/;

    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const /*OVERRIDE FINAL*/;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) /*OVERRIDE FINAL*/;

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
bool TransformPlugin::isIdentity(double time)
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

bool TransformPlugin::getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) const
{
    double scaleX, scaleY;
    double translateX, translateY;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    double centerX, centerY;

    // NON-GENERIC
    _scale->getValueAtTime(time, scaleX, scaleY);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    if (scaleUniform) {
        scaleY = scaleX;
    }
    _translate->getValueAtTime(time, translateX, translateY);
    _rotate->getValueAtTime(time, rotate);
    rotate = OFX::ofxsToRadians(rotate);

    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    _center->getValueAtTime(time, centerX, centerY);

    if (!invert) {
        *invtransform = OFX::ofxsMatInverseTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    } else {
        *invtransform = OFX::ofxsMatTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    }
    return true;
}

void TransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kTranslateParamName ||
        paramName == kRotateParamName ||
        paramName == kScaleParamName ||
        paramName == kScaleUniformParamName ||
        paramName == kSkewXParamName ||
        paramName == kSkewYParamName ||
        paramName == kSkewOrderParamName ||
        paramName == kCenterParamName) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
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
    
    DrawStateEnum _drawState;
    MouseStateEnum _mouseState;
    int _modifierStateCtrl;
    TransformPlugin* _plugin;
    OfxPointD _lastMousePos;
    
public:
    TransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _drawState(eInActive)
    , _mouseState(eReleased)
    , _modifierStateCtrl(0)
    , _plugin(dynamic_cast<TransformPlugin*>(effect))
    , _lastMousePos()
    {
        
        assert(_plugin);
        _lastMousePos.x = _lastMousePos.y = 0.;
        // NON-GENERIC
        _translate = _plugin->fetchDouble2DParam(kTranslateParamName);
        _rotate = _plugin->fetchDoubleParam(kRotateParamName);
        _scale = _plugin->fetchDouble2DParam(kScaleParamName);
        _scaleUniform = _plugin->fetchBooleanParam(kScaleUniformParamName);
        _skewX = _plugin->fetchDoubleParam(kSkewXParamName);
        _skewY = _plugin->fetchDoubleParam(kSkewYParamName);
        _skewOrder = _plugin->fetchChoiceParam(kSkewOrderParamName);
        _center = _plugin->fetchDouble2DParam(kCenterParamName);
        _invert = _plugin->fetchBooleanParam(kTransform3x3InvertParamName);
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
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    virtual bool keyDown(const OFX::KeyArgs &args);
    virtual bool keyUp(const OFX::KeyArgs &args);

private:
    void getCenter(double time, OfxPointD *center)
    {
        OfxPointD translate;
        _center->getValueAtTime(time, center->x, center->y);
        _translate->getValueAtTime(time, translate.x, translate.y);
        center->x += translate.x;
        center->y += translate.y;
    }
    
    void getScale(double time, OfxPointD* scale)
    {
        _scale->getValueAtTime(time, scale->x, scale->y);
        bool scaleUniform;
        _scaleUniform->getValueAtTime(time, scaleUniform);
        if (scaleUniform) {
            scale->y = scale->x;
        }
    }
    
    void getCircleRadius(double time, const OfxPointD& pixelScale, OfxPointD* radius)
    {
        OfxPointD scale;
        getScale(time, &scale);
        radius->x = scale.x * CIRCLE_RADIUS_BASE;
        radius->y = scale.y * CIRCLE_RADIUS_BASE;
        // don't draw too small. 15 pixels is the limit
        if (std::fabs(radius->x) < 15) {
            radius->y *= std::fabs(15./radius->x);
            radius->x = radius->x > 0 ? 15. : -15.;
        }
        if (std::fabs(radius->y) < 15) {
            radius->x *= std::fabs(15./radius->y);
            radius->y = radius->y > 0 ? 15. : -15.;
        }
        // the circle axes are not aligned with the images axes, so we cannot use the x and y scales separately
        double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
        radius->x *= meanPixelScale;
        radius->y *= meanPixelScale;
    }
    
    void getPoints(double time,
                   const OfxPointD& pixelScale,
                   OfxPointD *center,
                   OfxPointD *left,
                   OfxPointD *bottom,
                   OfxPointD *top,
                   OfxPointD *right)
    {
        getCenter(time, center);
        OfxPointD radius;
        getCircleRadius(time, pixelScale, &radius);
        left->x = center->x - radius.x ;
        left->y = center->y;
        right->x = center->x + radius.x ;
        right->y = center->y;
        top->x = center->x;
        top->y = center->y + radius.y ;
        bottom->x = center->x;
        bottom->y = center->y - radius.y ;
    }
    

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
drawSquare(const OfxPointD& center,
           const OfxPointD& pixelScale,
           bool hovered,
           bool althovered = false)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        if (althovered) {
            glColor4f(0.0, 1.0, 0.0, 1.0);
        } else {
            glColor4f(1.0, 0.0, 0.0, 1.0);
        }
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
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
drawEllipse(const OfxPointD& center,
                               const OfxPointD& radius,
                               const OfxPointD& pixelScale,
                               bool hovered)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    glPushMatrix ();
    //  center the oval at x_center, y_center
    glTranslatef (center.x, center.y, 0);
    //  draw the oval using line segments
    glBegin (GL_LINE_LOOP);
    // we don't need to be pixel-perfect here, it's just an interact!
    // 40 segments is enough.
    for (int i = 0; i < 40; ++i) {
        double theta = i * 2 * OFX::ofxsPi() / 40.;
        glVertex2f (radius.x * std::cos(theta), radius.y * std::sin(theta));
    }
    glEnd ();
    
    glPopMatrix ();
}

static void
drawSkewBar(const OfxPointD &center,
                               const OfxPointD& pixelScale,
                               double radiusY,
                               bool hovered,
                               double angle)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    // we are not axis-aligned: use the mean pixel scale
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusY + 20. * meanPixelScale;

    glPushMatrix ();
    glTranslatef (center.x, center.y, 0);
    glRotated(angle, 0, 0, 1);
    
    glBegin(GL_LINES);
    glVertex2d(0., - barHalfSize);
    glVertex2d(0., + barHalfSize);
    
    if (hovered) {
        double arrowYPosition = radiusY + 10. * meanPixelScale;
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
drawRotationBar(const OfxPointD& pixelScale,
                double radiusX,
                bool hovered,
                bool inverted)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    double barExtra = 30. * meanPixelScale;
    glBegin(GL_LINES);
    glVertex2d(0., 0.);
    glVertex2d(0. + radiusX + barExtra, 0.);
    glEnd();
    
    if (hovered) {
        double arrowCenterX = radiusX + barExtra / 2.;
        
        ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
        OfxPointD arrowRadius;
        arrowRadius.x = 5. * meanPixelScale;
        arrowRadius.y = 10. * meanPixelScale;
        
        glPushMatrix ();
        //  center the oval at x_center, y_center
        glTranslatef (arrowCenterX, 0., 0);
        //  draw the oval using line segments
        glBegin (GL_LINE_STRIP);
        glVertex2f (0, arrowRadius.y);
        glVertex2f (arrowRadius.x, 0.);
        glVertex2f (0, -arrowRadius.y);
        glEnd ();
        

        glBegin(GL_LINES);
        ///draw the top head
        glVertex2f(0., arrowRadius.y);
        glVertex2f(0., arrowRadius.y - 5. * meanPixelScale);
        
        glVertex2f(0., arrowRadius.y);
        glVertex2f(4. * meanPixelScale, arrowRadius.y - 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        ///draw the bottom head
        glVertex2f(0., -arrowRadius.y);
        glVertex2f(0., -arrowRadius.y + 5. * meanPixelScale);
        
        glVertex2f(0., -arrowRadius.y);
        glVertex2f(4. * meanPixelScale, -arrowRadius.y + 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        glEnd();

        glPopMatrix ();
    }
    if (inverted) {
        double arrowXPosition = radiusX + barExtra * 1.5;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        glPushMatrix ();
        glTranslatef (arrowXPosition, 0., 0);

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
        
        glPopMatrix ();
    }

}

// draw the interact
bool
TransformInteract::draw(const OFX::DrawArgs &args)
{
    //std::cout << "pixelScale= "<<args.pixelScale.x << "," << args.pixelScale.y << " renderscale=" << args.renderScale.x << "," << args.renderScale.y << std::endl;
    OfxPointD center, left, right, bottom, top;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(args.time, pscale, &center, &left, &bottom, &top, &right);

    double angle;
    _rotate->getValueAtTime(args.time, angle);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrderYX);

    bool inverted;
    _invert->getValueAtTime(args.time, inverted);

    OfxPointD radius;
    getCircleRadius(args.time, pscale, &radius);

    glPushMatrix();
    glTranslated(center.x, center.y, 0.);

    GLdouble skewMatrix[16];
    skewMatrix[0] = (skewOrderYX ? 1. : (1.+skewX*skewY)); skewMatrix[1] = skewY; skewMatrix[2] = 0.; skewMatrix[3] = 0;
    skewMatrix[4] = skewX; skewMatrix[5] = (skewOrderYX ? (1.+skewX*skewY) : 1.); skewMatrix[6] = 0.; skewMatrix[7] = 0;
    skewMatrix[8] = 0.; skewMatrix[9] = 0.; skewMatrix[10] = 1.; skewMatrix[11] = 0;
    skewMatrix[12] = 0.; skewMatrix[13] = 0.; skewMatrix[14] = 0.; skewMatrix[15] = 1.;

    glRotated(angle, 0, 0., 1.);
    drawRotationBar(pscale, radius.x, _mouseState == eDraggingRotationBar || _drawState == eRotationBarHovered, inverted);
    glMultMatrixd(skewMatrix);
    glTranslated(-center.x, -center.y, 0.);
    
    drawEllipse(center, radius, pscale, _mouseState == eDraggingCircle || _drawState == eCircleHovered);
    
    // add 180 to the angle to draw the arrows on the other side. unfortunately, this requires knowing
    // the mouse position in the ellipse frame
    double flip = 0.;
    if (_drawState == eSkewXBarHoverered || _drawState == eSkewYBarHoverered) {
        OfxPointD scale;
        _scale->getValueAtTime(args.time, scale.x, scale.y);
        bool scaleUniform;
        _scaleUniform->getValueAtTime(args.time, scaleUniform);
        if (scaleUniform) {
            scale.y = scale.x;
        }
        double rot = OFX::ofxsToRadians(angle);
        OFX::Matrix3x3 transformscale;
        transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
        
        OFX::Point3D previousPos;
        previousPos.x = _lastMousePos.x;
        previousPos.y = _lastMousePos.y;
        previousPos.z = 1.;
        previousPos = transformscale * previousPos;
        if (previousPos.z != 0) {
            previousPos.x /= previousPos.z;
            previousPos.y /= previousPos.z;
        }
        if ((_drawState == eSkewXBarHoverered && previousPos.y > center.y) ||
            (_drawState == eSkewYBarHoverered && previousPos.x > center.x)) {
            flip = 180.;
        }
    }
    drawSkewBar(center, pscale, radius.y, _mouseState == eDraggingSkewXBar || _drawState == eSkewXBarHoverered, flip);
    drawSkewBar(center, pscale, radius.x, _mouseState == eDraggingSkewYBar || _drawState == eSkewYBarHoverered, flip - 90.);
    
    
    drawSquare(center, pscale, _mouseState == eDraggingTranslation || _mouseState == eDraggingCenter || _drawState == eCenterPointHovered, _modifierStateCtrl);
    drawSquare(left, pscale, _mouseState == eDraggingLeftPoint || _drawState == eLeftPointHovered);
    drawSquare(right, pscale, _mouseState == eDraggingRightPoint || _drawState == eRightPointHovered);
    drawSquare(top, pscale, _mouseState == eDraggingTopPoint || _drawState == eTopPointHovered);
    drawSquare(bottom, pscale, _mouseState == eDraggingBottomPoint || _drawState == eBottomPointHovered);
    
    glPopMatrix();
    return true;
}

static bool squareContains(const OFX::Point3D& pos,const OfxRectD& rect,double toleranceX= 0.,double toleranceY = 0.)
{
    return (pos.x >= (rect.x1 - toleranceX) && pos.x < (rect.x2 + toleranceX)
            && pos.y >= (rect.y1 - toleranceY) && pos.y < (rect.y2 + toleranceY));
}

static bool isOnEllipseBorder(const OFX::Point3D& pos,const OfxPointD& radius,const OfxPointD& center,double epsilon = 0.1)
{

    double v = (((pos.x - center.x) * (pos.x - center.x)) / (radius.x * radius.x)) +
    (((pos.y - center.y) * (pos.y - center.y)) / (radius.y * radius.y));
    if (v <= (1. + epsilon) && v >= (1. - epsilon)) {
        return true;
    }
    return false;
}

static bool isOnSkewXBar(const OFX::Point3D& pos,double radiusY,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusY + (20. * meanPixelScale);
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
        pos.y >= (center.y - barHalfSize - tolerance) && pos.y <= (center.y + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnSkewYBar(const OFX::Point3D& pos,double radiusX,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusX + (20. * meanPixelScale);
    if (pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance) &&
        pos.x >= (center.x - barHalfSize - tolerance) && pos.x <= (center.x + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnRotationBar(const  OFX::Point3D& pos,double radiusX,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barExtra = 30. * meanPixelScale;
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + radiusX + barExtra + tolerance) &&
        pos.y >= (center.y  - tolerance) && pos.y <= (center.y + tolerance)) {
        return true;
    }

    return false;
}

static OfxRectD rectFromCenterPoint(const OfxPointD& center)
{
    OfxRectD ret;
    ret.x1 = center.x - POINT_SIZE / 2.;
    ret.x2 = center.x + POINT_SIZE / 2.;
    ret.y1 = center.y - POINT_SIZE / 2.;
    ret.y2 = center.y + POINT_SIZE / 2.;
    return ret;
}


// overridden functions from OFX::Interact to do things
bool TransformInteract::penMotion(const OFX::PenArgs &args)
{
    OfxPointD center, left, right, top, bottom;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(args.time, pscale, &center, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(args.time, pscale, &ellipseRadius);
    
    //double dx = args.penPosition.x - _lastMousePos.x;
    //double dy = args.penPosition.y - _lastMousePos.y;
    
    double currentRotation;
    _rotate->getValueAtTime(args.time, currentRotation);
    double rot = OFX::ofxsToRadians(currentRotation);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrderYX);
    
    OfxPointD scale;
    _scale->getValueAtTime(args.time, scale.x, scale.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(args.time, scaleUniform);
    if (scaleUniform) {
        scale.y = scale.x;
    }

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
        rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, center.x, center.y);
        transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
        transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
    } else {
        rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, 0., center.x, center.y);
        transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, 0., center.x, center.y);
        transformscale = OFX::ofxsMatInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, 0., center.x, center.y);
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
        } else if (squareContains(transformedPos, leftPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eLeftPointHovered;
        } else if (squareContains(transformedPos, rightPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eRightPointHovered;
        } else if (squareContains(transformedPos, topPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eTopPointHovered;
        } else if (squareContains(transformedPos, bottomPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eBottomPointHovered;
        } else if (isOnEllipseBorder(transformedPos, ellipseRadius, center)) {
            _drawState = eCircleHovered;
        } else if (isOnRotationBar(rotationPos, ellipseRadius.x, center, pscale, hoverTolerance)) {
            _drawState = eRotationBarHovered;
        } else if (isOnSkewXBar(transformedPos,ellipseRadius.y,center,pscale,hoverTolerance)) {
            _drawState = eSkewXBarHoverered;
        } else if (isOnSkewYBar(transformedPos,ellipseRadius.x,center,pscale,hoverTolerance)) {
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
        double prevDistSq = (center.x - previousPos.x)*(center.x - previousPos.x) + (center.y - previousPos.y)*(center.y - previousPos.y);
        if (prevDistSq != 0.) {
            const double distSq = (center.x - currentPos.x)*(center.x - currentPos.x) + (center.y - currentPos.y)*(center.y - currentPos.y);
            const double distRatio = std::sqrt(distSq/prevDistSq);
            scale.x *= distRatio;
            scale.y *= distRatio;
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingLeftPoint || _mouseState == eDraggingRightPoint) {
        // avoid division by zero
        if (center.x != previousPos.x) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (center.x - currentPos.x)/(center.x - previousPos.x);
            scale.x *= scaleRatio;
            scale.x = std::max(minX, std::min(scale.x, maxX));
            if (scaleUniform) {
                scale.y = scale.x;
            }
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingTopPoint || _mouseState == eDraggingBottomPoint) {
        // avoid division by zero
        if (center.y != previousPos.y) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (center.y - currentPos.y)/(center.y - previousPos.y);
            scale.y *= scaleRatio;
            scale.y = std::max(minY, std::min(scale.y, maxY));
            if (scaleUniform) {
                scale.x = scale.y;
            }
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingTranslation) {
        OfxPointD currentTranslation;
        _translate->getValueAtTime(args.time, currentTranslation.x, currentTranslation.y);
        
        double dx = args.penPosition.x - _lastMousePos.x;
        double dy = args.penPosition.y - _lastMousePos.y;
        double newx = currentTranslation.x + dx;
        double newy = currentTranslation.y + dy;
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
        // pscale10 is the power of 10 below pscale
        OfxPointD pscale10;
        pscale10.x = std::pow(10.,std::floor(std::log10(pscale.x)));
        pscale10.y = std::pow(10.,std::floor(std::log10(pscale.y)));
        newx = pscale10.x * std::floor(newx/pscale10.x + 0.5);
        newy = pscale10.y * std::floor(newy/pscale10.y + 0.5);
        _translate->setValue(newx,newy);
    } else if (_mouseState == eDraggingCenter) {
        OfxPointD currentTranslation;
        _translate->getValueAtTime(args.time, currentTranslation.x, currentTranslation.y);
        OfxPointD currentCenter;
        _center->getValueAtTime(args.time, currentCenter.x, currentCenter.y);
        OFX::Matrix3x3 R = ofxsMatRotation(rot);

        double dx = args.penPosition.x - _lastMousePos.x;
        double dy = args.penPosition.y - _lastMousePos.y;
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
        // pscale10 is the power of 10 below pscale
        OfxPointD pscale10;
        pscale10.x = std::pow(10.,std::floor(std::log10(pscale.x)));
        pscale10.y = std::pow(10.,std::floor(std::log10(pscale.y)));
        newx = pscale10.x * std::floor(newx/pscale10.x + 0.5);
        newy = pscale10.y * std::floor(newy/pscale10.y + 0.5);
        _center->setValue(newx,newy);
        // recompure dxrot,dyrot after rounding
        dxrot = newx - currentCenter.x;
        dyrot = newy - currentCenter.y;
        currentTranslation.x += dx - dxrot;
        currentTranslation.y += dy - dyrot;
        _translate->setValue(currentTranslation.x,currentTranslation.y);
    } else if (_mouseState == eDraggingRotationBar) {
        OfxPointD diffToCenter;
        ///the current mouse position (untransformed) is doing has a certain angle relative to the X axis
        ///which can be computed by : angle = arctan(opposite / adjacent)
        diffToCenter.y = rotationPos.y - center.y;
        diffToCenter.x = rotationPos.x - center.x;
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
        if (scale.y != 0. && center.y != previousPos.y) {
            const double addSkew = (scale.x/scale.y)*(currentPos.x - previousPos.x)/(currentPos.y - center.y);
            _skewX->setValue(skewX + addSkew);
        }
    } else if (_mouseState == eDraggingSkewYBar) {
        // avoid division by zero
        if (scale.x != 0. && center.x != previousPos.x) {
            const double addSkew = (scale.y/scale.x)*(currentPos.y - previousPos.y)/(currentPos.x - center.x);
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
    
    OfxPointD center,left,right,top,bottom;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(args.time, pscale, &center, &left, &bottom, &top, &right);
    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(args.time, pscale, &ellipseRadius);
    
    
    double currentRotation;
    _rotate->getValueAtTime(args.time, currentRotation);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrderYX);
    
    OFX::Point3D transformedPos, rotationPos;
    transformedPos.x = args.penPosition.x;
    transformedPos.y = args.penPosition.y;
    transformedPos.z = 1.;
    
    double rot = OFX::ofxsToRadians(currentRotation);

    ///now undo skew + rotation to the current position
    OFX::Matrix3x3 rotation, transform;
    rotation = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, center.x, center.y);
    transform = OFX::ofxsMatInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);

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
    
    double pressToleranceX = 5 * pscale.x;
    double pressToleranceY = 5 * pscale.y;
    bool ret = true;
    if (squareContains(transformedPos, centerPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = _modifierStateCtrl ? eDraggingCenter : eDraggingTranslation;
    } else if (squareContains(transformedPos, leftPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingLeftPoint;
    } else if (squareContains(transformedPos, rightPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingRightPoint;
    } else if (squareContains(transformedPos, topPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingTopPoint;
    } else if (squareContains(transformedPos, bottomPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingBottomPoint;
    } else if (isOnEllipseBorder(transformedPos,ellipseRadius, center)) {
        _mouseState = eDraggingCircle;
    } else if (isOnRotationBar(rotationPos, ellipseRadius.x, center, pscale, pressToleranceY)) {
        _mouseState = eDraggingRotationBar;
    } else if (isOnSkewXBar(transformedPos,ellipseRadius.y,center,pscale,pressToleranceY)) {
        _mouseState = eDraggingSkewXBar;
    } else if (isOnSkewYBar(transformedPos,ellipseRadius.x,center,pscale,pressToleranceX)) {
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

bool TransformInteract::keyDown(const OFX::KeyArgs &args)
{
    // Note that on the Mac:
    // cmd/apple/cloverleaf is kOfxKey_Control_L
    // ctrl is kOfxKey_Meta_L
    // alt/option is kOfxKey_Alt_L

    // the two control keys may be pressed consecutively, be aware about this
    _modifierStateCtrl += args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R;
    //std::cout << std::hex << args.keySymbol << std::endl;
    // modifiers are not "caught"
    return false;
}

bool TransformInteract::keyUp(const OFX::KeyArgs &args)
{
    _modifierStateCtrl -= args.keySymbol == kOfxKey_Control_L || args.keySymbol == kOfxKey_Control_R;
    if (_modifierStateCtrl < 0) {
        // we may have missed a keypress
        _modifierStateCtrl = 0;
    }

    // modifiers are not "caught"
    return false;
}


using namespace OFX;

mDeclarePluginFactory(TransformPluginFactory, {}, {});

class TransformOverlayDescriptor : public DefaultEffectOverlayDescriptor<TransformOverlayDescriptor, TransformInteract> {};

static
void TransformPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    Double2DParamDescriptor* translate = desc.defineDouble2DParam(kTranslateParamName);
    translate->setLabels(kTranslateParamLabel, kTranslateParamLabel, kTranslateParamLabel);
    //translate->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    translate->setDoubleType(eDoubleTypeXYAbsolute);
    translate->setDimensionLabels("x","y");
    translate->setDefault(0, 0);
    page->addChild(*translate);

    DoubleParamDescriptor* rotate = desc.defineDoubleParam(kRotateParamName);
    rotate->setLabels(kRotateParamLabel, kRotateParamLabel, kRotateParamLabel);
    rotate->setDoubleType(eDoubleTypeAngle);
    rotate->setDefault(0);
    //rotate->setRange(-180, 180); // the angle may be -infinity..+infinity
    rotate->setDisplayRange(-180, 180);
    page->addChild(*rotate);

    Double2DParamDescriptor* scale = desc.defineDouble2DParam(kScaleParamName);
    scale->setLabels(kScaleParamLabel, kScaleParamLabel, kScaleParamLabel);
    scale->setDoubleType(eDoubleTypeScale);
    scale->setDimensionLabels("w","h");
    scale->setDefault(1,1);
    //scale->setRange(0.1,0.1,10,10);
    scale->setDisplayRange(0.1, 0.1, 10, 10);
    scale->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*scale);

    BooleanParamDescriptor* scaleUniform = desc.defineBooleanParam(kScaleUniformParamName);
    scaleUniform->setLabels(kScaleUniformParamName, kScaleUniformParamName, kScaleUniformParamName);
    scaleUniform->setHint(kScaleUniformParamHint);
    scaleUniform->setDefault(true);
    scaleUniform->setAnimates(true);
    page->addChild(*scaleUniform);

    DoubleParamDescriptor* skewX = desc.defineDoubleParam(kSkewXParamName);
    skewX->setLabels(kSkewXParamName, kSkewXParamName, kSkewXParamName);
    skewX->setDefault(0);
    skewX->setDisplayRange(-1,1);
    page->addChild(*skewX);

    DoubleParamDescriptor* skewY = desc.defineDoubleParam(kSkewYParamName);
    skewY->setLabels(kSkewYParamLabel, kSkewYParamLabel, kSkewYParamLabel);
    skewY->setDefault(0);
    skewY->setDisplayRange(-1,1);
    page->addChild(*skewY);

    ChoiceParamDescriptor* skewOrder = desc.defineChoiceParam(kSkewOrderParamName);
    skewOrder->setLabels(kSkewOrderParamLabel, kSkewOrderParamLabel, kSkewOrderParamLabel);
    skewOrder->setDefault(0);
    skewOrder->appendOption("XY");
    skewOrder->appendOption("YX");
    skewOrder->setAnimates(true);
    page->addChild(*skewOrder);

    Double2DParamDescriptor* center = desc.defineDouble2DParam(kCenterParamName);
    center->setLabels(kCenterParamLabel, kCenterParamLabel, kCenterParamLabel);
    //center->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDimensionLabels("x","y");
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    page->addChild(*center);
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

OFX::ImageEffect* TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
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

OFX::ImageEffect* TransformMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
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
