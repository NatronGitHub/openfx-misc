/*
 OFX Roto plugin.
 
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

#include "CornerPin.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsOGLTextRenderer.h"
#include "ofxsTransform3x3.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

#define kToParamGroupName "To"
#define kTopLeftParamName "to1"
#define kTopRightParamName "to2"
#define kBtmLeftParamName "to3"
#define kBtmRightParamName "to4"

#define kFromParamGroupName "From"
#define kFrom1ParamName "from1"
#define kFrom2ParamName "from2"
#define kFrom3ParamName "from3"
#define kFrom4ParamName "from4"

#define kCopyFromParamName "Copy \"From\" points"
#define kCopyToParamName "Copy \"To\" points"
#define kCopyInputRoDParamName "Set to input rod"

#define kOverlayPointsParamName "Overlay points"

#define POINT_INTERACT_LINE_SIZE_PIXELS 20

using namespace OFX;


/**
 * \brief Compute the cross-product of two vectors
 *
 */
inline OFX::Point3D
crossprod(const OFX::Point3D& a, const OFX::Point3D& b)
{
    OFX::Point3D c;
    c.x = a.y * b.z - a.z * b.y;
    c.y = a.z * b.x - a.x * b.z;
    c.z = a.x * b.y - a.y * b.x;
    return c;
}

/**
 * brief Constructs a 3x3  matrix
 **/
inline OFX::Matrix3x3
matrix33_from_columns(const OFX::Point3D &m0, const OFX::Point3D &m1, const OFX::Point3D &m2) {
    OFX::Matrix3x3 M;
    M.a = m0.x; M.b = m1.x; M.c = m2.x;
    M.d = m0.y; M.e = m1.y; M.f = m2.y;
    M.g = m0.z; M.h = m1.z; M.i = m2.z;
    return M;
}

/**
 * \brief Compute a homography from 4 points correspondences
 * \param p1 source point
 * \param p2 source point
 * \param p3 source point
 * \param p4 source point
 * \param q1 target point
 * \param q2 target point
 * \param q3 target point
 * \param q4 target point
 * \return the homography matrix that maps pi's to qi's
 *
 Using four point-correspondences pi ↔ pi^, we can set up an equation system to solve for the homography matrix H.
 An algorithm to obtain these parameters requiring only the inversion of a 3 × 3 equation system is as follows.
 From the four point-correspondences pi ↔ pi^ with (i ∈ {1, 2, 3, 4}),
 compute h1 = (p1 × p2 ) × (p3 × p4 ), h2 = (p1 × p3 ) × (p2 × p4 ), h3 = (p1 × p4 ) × (p2 × p3 ).
 Also compute h1^ , h2^ , h3^ using the same principle from the points pi^.
 Now, the homography matrix H can be obtained easily from
 H · [h1 h2 h3] = [h1^ h2^ h3^],
 which only requires the inversion of the matrix [h1 h2 h3].
 
 Algo from:
 http://www.dirk-farin.net/publications/phd/text/AB_EfficientComputationOfHomographiesFromFourCorrespondences.pdf
 */
inline bool
homography_from_four_points(const OFX::Point3D &p1, const OFX::Point3D &p2, const OFX::Point3D &p3, const OFX::Point3D &p4,
                            const OFX::Point3D &q1, const OFX::Point3D &q2, const OFX::Point3D &q3, const OFX::Point3D &q4,
                            OFX::Matrix3x3 *H)
{
    OFX::Matrix3x3 invHp;
    OFX::Matrix3x3 Hp = matrix33_from_columns(crossprod(crossprod(p1,p2),crossprod(p3,p4)),
                                        crossprod(crossprod(p1,p3),crossprod(p2,p4)),
                                        crossprod(crossprod(p1,p4),crossprod(p2,p3)));
    double detHp = ofxsMatDeterminant(Hp);
    if (detHp == 0.) {
        return false;
    }
    OFX::Matrix3x3  Hq = matrix33_from_columns(crossprod(crossprod(q1,q2),crossprod(q3,q4)),
                                        crossprod(crossprod(q1,q3),crossprod(q2,q4)),
                                        crossprod(crossprod(q1,q4),crossprod(q2,q3)));
    double detHq = ofxsMatDeterminant(Hq);
    if (detHq == 0.) {
        return false;
    }
    invHp = ofxsMatInverse(Hp,detHp);
    *H = Hq * invHp;
    return true;
}






////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CornerPinPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    CornerPinPlugin(OfxImageEffectHandle handle, bool masked)
    : Transform3x3Plugin(handle, masked)
    , _topLeft(0)
    , _topRight(0)
    , _btmLeft(0)
    , _btmRight(0)
    , _extraMatrixRow1(0)
    , _extraMatrixRow2(0)
    , _extraMatrixRow3(0)
    , _from1(0)
    , _from2(0)
    , _from3(0)
    , _from4(0)
    , _copyFromButton(0)
    , _copyToButton(0)
    , _copyInputButton(0)
    {
        // NON-GENERIC
        _topLeft = fetchDouble2DParam(kTopLeftParamName);
        _topRight = fetchDouble2DParam(kTopRightParamName);
        _btmLeft = fetchDouble2DParam(kBtmLeftParamName);
        _btmRight = fetchDouble2DParam(kBtmRightParamName);
        assert(_topLeft && _topRight && _btmLeft && _btmRight);
        
        _topLeftEnabled = fetchBooleanParam(std::string("enable ") + kTopLeftParamName);
        _topRightEnabled = fetchBooleanParam(std::string("enable ") + kTopRightParamName);
        _btmLeftEnabled = fetchBooleanParam(std::string("enable ") + kBtmLeftParamName);
        _btmRightEnabled = fetchBooleanParam(std::string("enable ") + kBtmRightParamName);
        assert(_topLeftEnabled && _topRightEnabled && _btmLeftEnabled && _btmRightEnabled);
        
        _extraMatrixRow1 = fetchDouble3DParam("row1");
        _extraMatrixRow2 = fetchDouble3DParam("row2");
        _extraMatrixRow3 = fetchDouble3DParam("row3");
        assert(_extraMatrixRow1 && _extraMatrixRow2 && _extraMatrixRow3);
        
        _from1 = fetchDouble2DParam(kFrom1ParamName);
        _from2 = fetchDouble2DParam(kFrom2ParamName);
        _from3 = fetchDouble2DParam(kFrom3ParamName);
        _from4 = fetchDouble2DParam(kFrom4ParamName);
        assert(_from1 && _from2 && _from3 && _from4);
        
        _copyFromButton = fetchPushButtonParam(kCopyFromParamName);
        _copyToButton = fetchPushButtonParam(kCopyToParamName);
        _copyInputButton = fetchPushButtonParam(kCopyInputRoDParamName);
        assert(_copyInputButton && _copyToButton && _copyFromButton);
    }


    virtual bool isIdentity(double time) /*OVERRIDE FINAL*/;

    virtual bool getInverseTransformCanonical(double time, bool invert, OFX::Matrix3x3* invtransform) /*OVERRIDE FINAL*/;

private:
    
    OFX::Matrix3x3 getExtraMatrix(OfxTime time)
    {
        OFX::Matrix3x3 ret;
        _extraMatrixRow1->getValueAtTime(time,ret.a, ret.b, ret.c);
        _extraMatrixRow2->getValueAtTime(time,ret.d, ret.e, ret.f);
        _extraMatrixRow3->getValueAtTime(time,ret.g, ret.h, ret.i);
        return ret;
    }
    
    bool getHomography(OfxTime time,const OfxPointD& scale,
                       bool inverseTransform,
                       const OFX::Point3D& p1,
                       const OFX::Point3D& p2,
                       const OFX::Point3D& p3,
                       const OFX::Point3D& p4,
                       OFX::Matrix3x3& m);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) /*OVERRIDE FINAL*/;

private:
    // NON-GENERIC
    OFX::Double2DParam* _topLeft;
    OFX::Double2DParam* _topRight;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _btmRight;
    OFX::BooleanParam* _topLeftEnabled;
    OFX::BooleanParam* _topRightEnabled;
    OFX::BooleanParam* _btmLeftEnabled;
    OFX::BooleanParam* _btmRightEnabled;
    OFX::Double3DParam* _extraMatrixRow1;
    OFX::Double3DParam* _extraMatrixRow2;
    OFX::Double3DParam* _extraMatrixRow3;
    OFX::Double2DParam* _from1;
    OFX::Double2DParam* _from2;
    OFX::Double2DParam* _from3;
    OFX::Double2DParam* _from4;
    
    OFX::PushButtonParam* _copyFromButton;
    OFX::PushButtonParam* _copyToButton;
    OFX::PushButtonParam* _copyInputButton;
};


bool CornerPinPlugin::getInverseTransformCanonical(OfxTime time, bool invert, OFX::Matrix3x3* invtransform)
{
    OFX::Point3D p1,p2,p3,p4;
    _from1->getValueAtTime(time, p1.x, p1.y);
    _from2->getValueAtTime(time, p2.x, p2.y);
    _from3->getValueAtTime(time, p3.x, p3.y);
    _from4->getValueAtTime(time, p4.x, p4.y);
    p1.z = 1.;
    p2.z = 1.;
    p3.z = 1.;
    p4.z = 1.;

    bool topLeftEnabled,topRightEnabled,btmLeftEnabled,btmRightEnabled;
    _topLeftEnabled->getValue(topLeftEnabled);
    _topRightEnabled->getValue(topRightEnabled);
    _btmRightEnabled->getValue(btmRightEnabled);
    _btmLeftEnabled->getValue(btmLeftEnabled);

    OFX::Point3D q1,q2,q3,q4;
    if (topLeftEnabled) {
        OfxPointD topLeft;
        _topLeft->getValueAtTime(time,topLeft.x, topLeft.y);
        q1.x = topLeft.x;
        q1.y = topLeft.y;
        q1.z = 1.;
    } else {
        q1 = p1;
    }
    
    if (topRightEnabled) {
        OfxPointD topRight;
        _topRight->getValueAtTime(time,topRight.x, topRight.y);
        q2.x = topRight.x;
        q2.y = topRight.y;
        q2.z = 1.;
    } else {
        q2 = p2;
    }
    
    if (btmRightEnabled) {
        OfxPointD btmRight;
        _btmRight->getValueAtTime(time,btmRight.x, btmRight.y);
        q3.x = btmRight.x;
        q3.y = btmRight.y;
        q3.z = 1.;
    } else {
        q3 = p3;
    }
    
    if (btmLeftEnabled) {
        OfxPointD btmLeft;
        _btmLeft->getValueAtTime(time,btmLeft.x, btmLeft.y);
        q4.x = btmLeft.x;
        q4.y = btmLeft.y;
        q4.z = 1.;
    } else {
        q4 = p4;
    }
    
    
    OFX::Matrix3x3 homo3x3;
    bool success;
    if (invert) {
        success = homography_from_four_points(p1, p2, p3, p4, q1, q2, q3, q4, &homo3x3);
    } else {
        success = homography_from_four_points(q1, q2, q3, q4, p1, p2, p3, p4, &homo3x3);
    }

    if (!success) {
        ///cannot compute the homography when 3 points are aligned
        return false;
    }
    
    OFX::Matrix3x3 extraMat = getExtraMatrix(time);
    *invtransform = homo3x3 * extraMat;

    return true;
}


// overridden is identity
bool CornerPinPlugin::isIdentity(double time)
{
    
    OfxPointD topLeft,topRight,btmLeft,btmRight;
    _topLeft->getValue(topLeft.x, topLeft.y);
    _topRight->getValue(topRight.x, topRight.y);
    _btmLeft->getValue(btmLeft.x, btmLeft.y);
    _btmRight->getValue(btmRight.x, btmRight.y);
    
    OFX::Matrix3x3 extraMat = getExtraMatrix(time);
    
    OfxPointD p1,p2,p3,p4;
    _from1->getValueAtTime(time,p1.x, p1.y);
    _from2->getValueAtTime(time,p2.x, p2.y);
    _from3->getValueAtTime(time,p3.x, p3.y);
    _from4->getValueAtTime(time,p4.x, p4.y);

    bool topLeftEnabled,topRightEnabled,btmLeftEnabled,btmRightEnabled;
    _topLeftEnabled->getValue(topLeftEnabled);
    _topRightEnabled->getValue(topRightEnabled);
    _btmRightEnabled->getValue(btmRightEnabled);
    _btmLeftEnabled->getValue(btmLeftEnabled);

    if ((!topLeftEnabled || (p1.x == topLeft.x && p1.y == topLeft.y)) &&
        (!topRightEnabled || (p2.x == topRight.x && p2.y == topRight.y)) &&
        (!btmRightEnabled || (p3.x == btmRight.x && p3.y == btmRight.y)) &&
        (!btmLeftEnabled || (p4.x == btmLeft.x && p4.y == btmLeft.y)) &&
        extraMat.isIdentity()) {
        return true;
    }
    
    return false;
}

static void copyPoint(OFX::Double2DParam* from,OFX::Double2DParam* to)
{
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

}

void CornerPinPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kCopyInputRoDParamName) {
        const OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
        _from1->setValue(srcRoD.x1, srcRoD.y2);
        _from2->setValue(srcRoD.x2, srcRoD.y2);
        _from3->setValue(srcRoD.x2, srcRoD.y1);
        _from4->setValue(srcRoD.x1, srcRoD.y1);
    } else if (paramName == kCopyFromParamName) {
        copyPoint(_from1,_topLeft);
        copyPoint(_from2,_topRight);
        copyPoint(_from3,_btmRight);
        copyPoint(_from4,_btmLeft);
    } else if (paramName == kCopyToParamName) {
        copyPoint(_topLeft,_from1);
        copyPoint(_topRight,_from2);
        copyPoint(_btmRight,_from3);
        copyPoint(_btmLeft,_from4);
    }
}


class CornerPinTransformInteract : public OFX::OverlayInteract
{
    
    enum MouseState
    {
        eIdle = 0,
        eDraggingTopLeft,
        eDraggingTopRight,
        eDraggingBottomLeft,
        eDraggingBottomRight
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringTopLeft,
        eHoveringTopRight,
        eHoveringBottomLeft,
        eHoveringBottomRight
    };
    
public:
    
    CornerPinTransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _topLeft(0)
    , _topRight(0)
    , _btmLeft(0)
    , _btmRight(0)
    , _invert(0)
    , _from1(0)
    , _from2(0)
    , _from3(0)
    , _from4(0)
    , _overlayChoice(0)
    , _ms(eIdle)
    , _ds(eInactive)
    , _lastMousePos()
    , _lastPenDownPos()
    , _topLeftDraggedPos()
    , _topRightDraggedPos()
    , _btmRightDraggedPos()
    , _btmLeftDraggedPos()
    {
        _topLeft = effect->fetchDouble2DParam(kTopLeftParamName);
        _topRight = effect->fetchDouble2DParam(kTopRightParamName);
        _btmRight = effect->fetchDouble2DParam(kBtmRightParamName);
        _btmLeft = effect->fetchDouble2DParam(kBtmLeftParamName);
        _invert = effect->fetchBooleanParam(kTransform3x3InvertParamName);
        
        _from1 = effect->fetchDouble2DParam(kFrom1ParamName);
        _from2 = effect->fetchDouble2DParam(kFrom2ParamName);
        _from3 = effect->fetchDouble2DParam(kFrom3ParamName);
        _from4 = effect->fetchDouble2DParam(kFrom4ParamName);
        
        _overlayChoice = effect->fetchChoiceParam(kOverlayPointsParamName);
        
        addParamToSlaveTo(_topLeft);
        addParamToSlaveTo(_topRight);
        addParamToSlaveTo(_btmRight);
        addParamToSlaveTo(_btmLeft);
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
    
    /**
     * @brief Returns true if the points that should be used by the overlay are
     * the "from" points, otherwise the overlay is assumed to use the "to" points.
     **/
    bool isFromPoints() const
    {
        int v;
        _overlayChoice->getValue(v);
        return v == 1;
    }
    
    bool isNearbyTopLeft(const OfxPointD& pos,bool useFromPoints,double tolerance) const;
    bool isNearbyTopRight(const OfxPointD& pos,bool useFromPoints,double tolerance) const;
    bool isNearbyBtmLeft(const OfxPointD& pos,bool useFromPoints,double tolerance) const;
    bool isNearbyBtmRight(const OfxPointD& pos,bool useFromPoints,double tolerance) const;
    
    OFX::Double2DParam* _topLeft;
    OFX::Double2DParam* _topRight;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _btmRight;
    OFX::BooleanParam* _invert;
    
    OFX::Double2DParam* _from1;
    OFX::Double2DParam* _from2;
    OFX::Double2DParam* _from3;
    OFX::Double2DParam* _from4;
    
    OFX::ChoiceParam* _overlayChoice;
    
    MouseState _ms;
    DrawState _ds;
    OfxPointD _lastMousePos;
    OfxPointD _lastPenDownPos;
    
    OfxPointD _topLeftDraggedPos;
    OfxPointD _topRightDraggedPos;
    OfxPointD _btmRightDraggedPos;
    OfxPointD _btmLeftDraggedPos;
    
};


bool CornerPinTransformInteract::isNearbyTopLeft(const OfxPointD& pos,bool useFromPoints,double tolerance) const
{
    OfxPointD topLeft;
    useFromPoints ? _from1->getValue(topLeft.x, topLeft.y) : _topLeft->getValue(topLeft.x, topLeft.y);
    if (pos.x >= (topLeft.x - tolerance) && pos.x <= (topLeft.x + tolerance) &&
        pos.y >= (topLeft.y - tolerance) && pos.y <= (topLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyTopRight(const OfxPointD& pos,bool useFromPoints,double tolerance) const
{
    OfxPointD topRight;
    useFromPoints ? _from2->getValue(topRight.x, topRight.y) : _topRight->getValue(topRight.x, topRight.y);
    if (pos.x >= (topRight.x - tolerance) && pos.x <= (topRight.x + tolerance) &&
        pos.y >= (topRight.y - tolerance) && pos.y <= (topRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyBtmLeft(const OfxPointD& pos,bool useFromPoints,double tolerance) const
{
    OfxPointD btmLeft;
    useFromPoints ? _from4->getValue(btmLeft.x, btmLeft.y) :_btmLeft->getValue(btmLeft.x, btmLeft.y);
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyBtmRight(const OfxPointD& pos,bool useFromPoints,double tolerance) const
{
    OfxPointD btmRight;
    useFromPoints ? _from3->getValue(btmRight.x, btmRight.y) : _btmRight->getValue(btmRight.x, btmRight.y);
    if (pos.x >= (btmRight.x - tolerance) && pos.x <= (btmRight.x + tolerance) &&
        pos.y >= (btmRight.y - tolerance) && pos.y <= (btmRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::draw(const OFX::DrawArgs &args)
{

    bool useFrom = isFromPoints();
    
    OfxPointD topLeft,topRight,btmLeft,btmRight;
    if (_ms == eDraggingTopLeft) {
        topLeft = _topLeftDraggedPos;
    } else {
        useFrom ? _from1->getValue(topLeft.x, topLeft.y) : _topLeft->getValue(topLeft.x, topLeft.y);
    }
    
    if (_ms == eDraggingTopRight) {
        topRight = _topRightDraggedPos;
    } else {
        useFrom ? _from2->getValue(topRight.x, topRight.y) :_topRight->getValue(topRight.x, topRight.y);
    }
    
    if (_ms == eDraggingBottomLeft) {
        btmLeft = _btmLeftDraggedPos;
    } else {
        useFrom ? _from4->getValue(btmLeft.x, btmLeft.y) :_btmLeft->getValue(btmLeft.x, btmLeft.y);
    }
    
    if (_ms == eDraggingBottomRight) {
        btmRight = _btmRightDraggedPos;
    } else {
        useFrom ? _from3->getValue(btmRight.x, btmRight.y) : _btmRight->getValue(btmRight.x, btmRight.y);
    }
    
    glPointSize(5);
    glBegin(GL_POINTS);
    
    if (_ds == eHoveringTopLeft || _ms == eDraggingTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 1., 1., 1.);
    }
    glVertex2d(topLeft.x, topLeft.y);
    
    if (_ds == eHoveringTopRight || _ms == eDraggingTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 1., 1., 1.);
    }
    glVertex2d(topRight.x, topRight.y);
    
    if (_ds == eHoveringBottomLeft || _ms == eDraggingBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 1., 1., 1.);
    }
    
    glVertex2d(btmLeft.x, btmLeft.y);
    
    if (_ds == eHoveringBottomRight || _ms == eDraggingBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 1., 1., 1.);
    }
    glVertex2d(btmRight.x, btmRight.y);
    glEnd();

    double offset = 10 * args.pixelScale.x;
    TextRenderer::bitmapString(topLeft.x - offset,topLeft.y + offset,useFrom ? kFrom1ParamName : kTopLeftParamName);
    TextRenderer::bitmapString(topRight.x + offset,topRight.y + offset,useFrom ? kFrom2ParamName : kTopRightParamName);
    TextRenderer::bitmapString(btmRight.x + offset,btmRight.y - offset,useFrom ? kFrom3ParamName : kBtmRightParamName);
    TextRenderer::bitmapString(btmLeft.x - offset,btmLeft.y - offset,useFrom ? kFrom4ParamName : kBtmLeftParamName);

    glPointSize(1.);
    
    bool inverted;
    _invert->getValue(inverted);
    if (inverted) {
        double arrowXPosition = 0;
        double arrowXHalfSize = 10 * args.pixelScale.x;
        double arrowHeadOffsetX = 3 * args.pixelScale.x;
        double arrowHeadOffsetY = 3 * args.pixelScale.x;
        
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

    return true;
}
bool CornerPinTransformInteract::penMotion(const OFX::PenArgs &args)
{

    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    bool useFrom = isFromPoints();
    
    double selectionTol = 15. * args.pixelScale.x;
    if (isNearbyBtmLeft(args.penPosition, useFrom,selectionTol)) {
        _ds = eHoveringBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, useFrom,selectionTol)) {
        _ds = eHoveringBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, useFrom,selectionTol)) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, useFrom,selectionTol)) {
        _ds = eHoveringTopLeft;
        didSomething = true;
    } else {
        _ds = eInactive;
        didSomething = true;
    }
    
    if (_ms == eDraggingBottomLeft) {
        _btmLeftDraggedPos.x += delta.x;
        _btmLeftDraggedPos.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingBottomRight) {
        _btmRightDraggedPos.x += delta.x;
        _btmRightDraggedPos.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingTopLeft) {
        _topLeftDraggedPos.x += delta.x;
        _topLeftDraggedPos.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingTopRight) {
        _topRightDraggedPos.x += delta.x;
        _topRightDraggedPos.y += delta.y;
        didSomething = true;
    }
    _lastMousePos = args.penPosition;
    return didSomething;
}
bool CornerPinTransformInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    double selectionTol = 15. * args.pixelScale.x;
    bool useFrom = isFromPoints();
    
    if (isNearbyBtmLeft(args.penPosition,useFrom, selectionTol)) {
        _ms = eDraggingBottomLeft;
        _btmLeftDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition,useFrom, selectionTol)) {
        _ms = eDraggingBottomRight;
        _btmRightDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition,useFrom, selectionTol)) {
        _ms = eDraggingTopRight;
        _topRightDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition,useFrom, selectionTol)) {
        _ms = eDraggingTopLeft;
        _topLeftDraggedPos = args.penPosition;
        didSomething = true;
    }
    
    _lastMousePos = args.penPosition;
    return didSomething;
}
bool CornerPinTransformInteract::penUp(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    bool useFrom = isFromPoints();
    
    if (_ms == eDraggingBottomLeft) {
        useFrom ? _from4->setValue(_btmLeftDraggedPos.x, _btmLeftDraggedPos.y)
        : _btmLeft->setValue(_btmLeftDraggedPos.x,_btmLeftDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingBottomRight) {
        useFrom ? _from3->setValue(_btmRightDraggedPos.x, _btmRightDraggedPos.y)
        : _btmRight->setValue(_btmRightDraggedPos.x,_btmRightDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingTopLeft) {
        useFrom ? _from1->setValue(_topLeftDraggedPos.x, _topLeftDraggedPos.y)
        :_topLeft->setValue(_topLeftDraggedPos.x,_topLeftDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingTopRight) {
        useFrom ? _from2->setValue(_topRightDraggedPos.x, _topRightDraggedPos.y)
        :_topRight->setValue(_topRightDraggedPos.x,_topRightDraggedPos.y);
        didSomething = true;
    }
    
    _ms = eIdle;
    return didSomething;
}
bool CornerPinTransformInteract::keyDown(const OFX::KeyArgs &args)
{
    bool didSomething = false;
    return didSomething;
}
bool CornerPinTransformInteract::keyUp(const OFX::KeyArgs &args)
{
    bool didSomething = false;
    return didSomething;
}



using namespace OFX;

class CornerPinOverlayDescriptor : public DefaultEffectOverlayDescriptor<CornerPinOverlayDescriptor, CornerPinTransformInteract> {};




void CornerPinPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("CornerPinOFX", "CornerPinOFX", "CornerPinOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Allows an image to fit another in translation,rotation and scale.");

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

static void defineCornerPinToDouble2DParam(OFX::ImageEffectDescriptor &desc,
                                           GroupParamDescriptor* group,
                                           const std::string& name,
                                           double x,
                                           double y)
{
    Double2DParamDescriptor* size = desc.defineDouble2DParam(name);
    size->setLabels(name, name, name);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setAnimates(true);
    size->setDefault(x, y);
    size->setDimensionLabels("x", "y");
    size->setLayoutHint(OFX::eLayoutHintNoNewLine);
    size->setParent(*group);

    std::string enableName("enable " + name);
    BooleanParamDescriptor* enable = desc.defineBooleanParam(enableName);
    enable->setLabels(enableName, enableName, enableName);
    enable->setDefault(true);
    enable->setAnimates(false);
    enable->setHint("Enables the point on the left.");
    enable->setParent(*group);
}

static void defineCornerPinFromsDouble2DParam(OFX::ImageEffectDescriptor &desc,
                                              GroupParamDescriptor* group,
                                              const std::string& name,
                                              double x,
                                              double y)
{
    Double2DParamDescriptor* size = desc.defineDouble2DParam(name);
    size->setLabels(name, name, name);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setAnimates(true);
    size->setDefault(x, y);
    size->setDimensionLabels("x", "y");
    size->setParent(*group);
}

static void defineExtraMatrixRow(OFX::ImageEffectDescriptor &desc,PageParamDescriptor *page,GroupParamDescriptor* group,const std::string& name,double x,double y,double z)
{
    Double3DParamDescriptor* row = desc.defineDouble3DParam(name);
    row->setLabels("", "", "");
    row->setScriptName(name);
    row->setAnimates(true);
    row->setDefault(x,y,z);
    row->setParent(*group);
}

static void
CornerPinPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    GroupParamDescriptor* toPoints = desc.defineGroupParam(kToParamGroupName);
    toPoints->setLabels(kToParamGroupName, kToParamGroupName, kToParamGroupName);
    toPoints->setAsTab();
    defineCornerPinToDouble2DParam(desc, toPoints, kTopLeftParamName,  0, 1);
    defineCornerPinToDouble2DParam(desc, toPoints, kTopRightParamName, 1, 1);
    defineCornerPinToDouble2DParam(desc, toPoints, kBtmRightParamName, 1, 0);
    defineCornerPinToDouble2DParam(desc, toPoints, kBtmLeftParamName,  0, 0);
    page->addChild(*toPoints);

    GroupParamDescriptor* fromPoints = desc.defineGroupParam(kFromParamGroupName);
    fromPoints->setLabels(kFromParamGroupName, kFromParamGroupName, kFromParamGroupName);
    fromPoints->setAsTab();
    defineCornerPinFromsDouble2DParam(desc, fromPoints, kFrom1ParamName, 0, 1);
    defineCornerPinFromsDouble2DParam(desc, fromPoints, kFrom2ParamName, 1, 1);
    defineCornerPinFromsDouble2DParam(desc, fromPoints, kFrom3ParamName, 1, 0);
    defineCornerPinFromsDouble2DParam(desc, fromPoints, kFrom4ParamName, 0, 0);
    page->addChild(*fromPoints);

    PushButtonParamDescriptor* copyFrom = desc.definePushButtonParam(kCopyFromParamName);
    copyFrom->setLabels(kCopyFromParamName, kCopyFromParamName, kCopyFromParamName);
    copyFrom->setHint("Copy the content from the \"to\" points to the \"from\" points.");
    page->addChild(*copyFrom);

    GroupParamDescriptor* extraMatrix = desc.defineGroupParam("Extra matrix");
    extraMatrix->setHint("This matrix gets concatenated to the transform defined by the other parameters.");
    extraMatrix->setLabels("Extra matrix", "Extra matrix", "Extra matrix");
    extraMatrix->setOpen(false);
    defineExtraMatrixRow(desc, page, extraMatrix,"row1",1,0,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row2",0,1,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row3",0,0,1);
    page->addChild(*extraMatrix);


    PushButtonParamDescriptor* setToInput = desc.definePushButtonParam(kCopyInputRoDParamName);
    setToInput->setLabels(kCopyInputRoDParamName, kCopyInputRoDParamName, kCopyInputRoDParamName);
    setToInput->setHint("Copy the values from the source region of definition into the \"to\" points.");
    setToInput->setParent(*fromPoints);
    setToInput->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*setToInput);

    PushButtonParamDescriptor* copyTo = desc.definePushButtonParam(kCopyToParamName);
    copyTo->setLabels(kCopyToParamName, kCopyToParamName, kCopyToParamName);
    copyTo->setHint("Copy the content from the \"from\" points to the \"to\" points.");
    copyTo->setParent(*fromPoints);
    page->addChild(*copyTo);

    ChoiceParamDescriptor* overlayChoice = desc.defineChoiceParam(kOverlayPointsParamName);
    overlayChoice->setHint("Whether to display the \"from\" or the \"to\" points in the overlay");
    overlayChoice->setLabels(kOverlayPointsParamName, kOverlayPointsParamName, kOverlayPointsParamName);
    overlayChoice->appendOption("To");
    overlayChoice->appendOption("From");
    overlayChoice->setDefault(0);
    overlayChoice->setAnimates(false);
    page->addChild(*overlayChoice);
}

void CornerPinPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false);
}


OFX::ImageEffect* CornerPinPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CornerPinPlugin(handle, false);
}


void CornerPinMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("CornerPinMaskedOFX", "CornerPinMaskedOFX", "CornerPinMaskedOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Allows an image to fit another in translation,rotation and scale.");

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void CornerPinMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    CornerPinPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true);
}

OFX::ImageEffect* CornerPinMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CornerPinPlugin(handle, true);
}


