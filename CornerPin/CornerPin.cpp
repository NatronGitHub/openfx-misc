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


#include "../Transform/TransformProcessor.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

#define kInvertParamName "Invert"
#define kTopLeftParamName "Top left"
#define kTopRightParamName "Top right"
#define kBtmLeftParamName "Bottom left"
#define kBtmRightParamName "Bottom right"

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
class CornerPinPlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

public:
    /** @brief ctor */
    CornerPinPlugin(OfxImageEffectHandle handle, bool masked)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , _topLeft(0)
    , _topRight(0)
    , _btmLeft(0)
    , _btmRight(0)
    , _extraMatrixRow1(0)
    , _extraMatrixRow2(0)
    , _extraMatrixRow3(0)
    , _invert(0)
    , _filter(0)
    , _clamp(0)
    , _blackOutside(0)
    , _masked(masked)
    , _domask(0)
    , _mix(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
        // name of mask clip depends on the context
        if (masked) {
            maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
            assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        }
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
        
        _invert = fetchBooleanParam(kInvertParamName);
        // GENERIC
        _filter = fetchChoiceParam(kFilterTypeParamName);
        _clamp = fetchBooleanParam(kFilterClampParamName);
        _blackOutside = fetchBooleanParam(kFilterBlackOutsideParamName);
        if (masked) {
            _domask = fetchBooleanParam(kFilterMaskParamName);
            _mix = fetchDoubleParam(kFilterMixParamName);
        }
    }
    
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
    
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime);
    
#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9]);
#endif

    
    /* internal render function */
    template <class PIX, int nComponents, int maxValue, bool masked>
    void renderInternalForBitDepth(const OFX::RenderArguments &args);
    
    template <int nComponents, bool masked>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(TransformProcessorBase&, const OFX::RenderArguments &args);

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
    OFX::BooleanParam* _invert;

    // GENERIC
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _clamp;
    OFX::BooleanParam* _blackOutside;
    bool _masked;
    OFX::BooleanParam* _domask;
    OFX::DoubleParam* _mix;
};

static void scalePoint(OfxPointD& p,const OfxPointD& scale)
{
    p.x *= scale.x;
    p.y *= scale.y;
}

bool CornerPinPlugin::getHomography(OfxTime time,const OfxPointD& scale,
                                    bool inverseTransform,
                                    const OFX::Point3D& p1,
                                    const OFX::Point3D& p2,
                                    const OFX::Point3D& p3,
                                    const OFX::Point3D& p4,
                                    OFX::Matrix3x3& m)
{
    OfxPointD topLeft,topRight,btmLeft,btmRight;
    _topLeft->getValueAtTime(time,topLeft.x, topLeft.y);
    _topRight->getValueAtTime(time,topRight.x, topRight.y);
    _btmLeft->getValueAtTime(time,btmLeft.x, btmLeft.y);
    _btmRight->getValueAtTime(time,btmRight.x, btmRight.y);
    
    scalePoint(topLeft,scale);
    scalePoint(topRight,scale);
    scalePoint(btmLeft,scale);
    scalePoint(btmRight,scale);
   
    OFX::Point3D q1,q2,q3,q4;
    
    bool topLeftEnabled,topRightEnabled,btmLeftEnabled,btmRightEnabled;
    _topLeftEnabled->getValue(topLeftEnabled);
    _topRightEnabled->getValue(topRightEnabled);
    _btmRightEnabled->getValue(btmRightEnabled);
    _btmLeftEnabled->getValue(btmLeftEnabled);
    
    if (topLeftEnabled) {
        q1.x = topLeft.x ; q1.y = topLeft.y; q1.z = 1;
    } else {
        q1 = p1;
    }
    
    if (topRightEnabled) {
        q2.x = topRight.x; q2.y = topRight.y ; q2.z = 1;
    } else {
        q2 = p2;
    }
    
    if (btmRightEnabled) {
        q3.x = btmRight.x ; q3.y = btmRight.y ; q3.z = 1;
    } else {
        q3 = p3;
    }
    
    if (btmLeftEnabled) {
        q4.x = btmLeft.x ; q4.y = btmLeft.y ; q4.z = 1;
    } else {
        q4 = p4;
    }
    
    OFX::Matrix3x3 homo3x3;
    bool success;
    if (inverseTransform) {
        success = homography_from_four_points(q1, q2, q3, q4, p1, p2, p3, p4, &homo3x3);
    } else {
        success = homography_from_four_points(p1, p2, p3, p4, q1, q2, q3, q4, &homo3x3);
    }

    if (!success) {
        ///cannot compute the homography when 3 points are aligned
        return false;
    }
    
    OFX::Matrix3x3 extraMat = getExtraMatrix(time);
    m = homo3x3 * extraMat;

    return true;

}



////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CornerPinPlugin::setupAndProcess(TransformProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);
        
        
    }
    
    OfxRectD srcRod = srcClip_->getRegionOfDefinition(args.time);
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // NON-GENERIC
    bool invert;
    // GENERIC
    //int filter;
    //bool clamp;
    bool blackOutside;
    double mix = 1.;

    // NON-GENERIC
    _invert->getValue(invert);
    
    // GENERIC
    //_filter->getValue(filter);
    //_clamp->getValue(clamp);
    _blackOutside->getValue(blackOutside);
    if (_masked) {
        _mix->getValueAtTime(args.time, mix);
    }

    OFX::Matrix3x3 invtransform;
    OFX::Point3D p1,p2,p3,p4;

    // FIXME: where are q1, q2, q3, q4???

    p1.x = srcRod.x1 ; p1.y = srcRod.y2 - 1 ; p1.z = 1; //top left
    p2.x = srcRod.x2 - 1; p2.y = srcRod.y2 -1 ; p2.z = 1; //top right
    p3.x = srcRod.x2 - 1; p3.y = srcRod.y1 ; p3.z = 1; //btm right
    p4.x = srcRod.x1 ; p4.y = srcRod.y1 ; p4.z = 1; //btm left
    
    bool success = getHomography(args.time, args.renderScale, !invert,p1,p2,p3,p4,invtransform);
    
    if (!success) {
        ///render nothing
        return;
    }

    processor.setValues(invtransform,
                        srcClip_->getPixelAspectRatio(),
                        args.renderScale, //!< 0.5 for a half-resolution image
                        args.fieldToRender,
                        //(FilterEnum)filter, clamp,
                        blackOutside, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

bool
CornerPinPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    
    OfxPointD topLeft,topRight,btmLeft,btmRight;
    
    bool topLeftEnabled,topRightEnabled,btmLeftEnabled,btmRightEnabled;
    _topLeftEnabled->getValue(topLeftEnabled);
    _topRightEnabled->getValue(topRightEnabled);
    _btmRightEnabled->getValue(btmRightEnabled);
    _btmLeftEnabled->getValue(btmLeftEnabled);
    
    if (topLeftEnabled) {
        _topLeft->getValue(topLeft.x, topLeft.y);
    } else {
        topLeft.x = srcRoD.x1; topLeft.y = srcRoD.y2 ;
    }
    
    if (topRightEnabled) {
        _topRight->getValue(topRight.x, topRight.y);
    } else {
        topRight.x = srcRoD.x2 ; topRight.y = srcRoD.y2 ;
    }
    
    if (btmLeftEnabled) {
        _btmLeft->getValue(btmLeft.x, btmLeft.y);
    } else {
        btmLeft.x = srcRoD.x1;  btmLeft.y = srcRoD.y1;
    }
    
    if (btmRightEnabled) {
        _btmRight->getValue(btmRight.x, btmRight.y);
    } else {
        btmRight.x = srcRoD.x2 ; btmRight.y = srcRoD.y1;
    }

    double l = std::min(std::min(topLeft.x, btmLeft.x),std::min(topRight.x,btmRight.x));
    double b = std::min(std::min(topLeft.y, btmLeft.y),std::min(topRight.y,btmRight.y));
    double r = std::max(std::max(topLeft.x, btmLeft.x),std::max(topRight.x,btmRight.x));
    double t = std::max(std::max(topLeft.y, btmLeft.y),std::max(topRight.y,btmRight.y));
    
    // GENERIC
    rod.x1 = l;
    rod.x2 = r;
    rod.y1 = b;
    rod.y2 = t;
    assert(rod.x1 < rod.x2 && rod.y1 < rod.y2);
    
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    
    ofxsFilterExpandRoD(this, dstClip_->getPixelAspectRatio(), args.renderScale, blackOutside, &rod);
    return true;
}


// override the roi call
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
CornerPinPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    const OfxRectD roi = args.regionOfInterest;
    
    bool invert;
    _invert->getValue(invert);
    
    OFX::Matrix3x3 homography;
    OFX::Point3D p1,p2,p3,p4;
    
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    
    p1.x = srcRoD.x1; p1.y = srcRoD.y2; p1.z = 1; //top left
    p2.x = srcRoD.x2 ; p2.y = srcRoD.y2 ; p2.z = 1; //top right
    p3.x = srcRoD.x2; p3.y = srcRoD.y1; p3.z = 1; //btm right
    p4.x = srcRoD.x1; p4.y = srcRoD.y1; p4.z = 1; //btm left
    
    bool success = getHomography(args.time, args.renderScale, invert, p1, p2, p3, p4, homography);
    
    if (!success) {
        ///cannot compute the corner pin
        setPersistentMessage(OFX::Message::eMessageError, "", "Cannot compute a corner pin when 3 points are aligned.");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    
    /// now transform the 4 corners of the source clip to the output image
    OFX::Point3D dstTopLeft = homography * OFX::Point3D(roi.x1,roi.y2,1);
    OFX::Point3D dstTopRight = homography * OFX::Point3D(roi.x2,roi.y2,1);
    OFX::Point3D dstBottomLeft = homography * OFX::Point3D(roi.x1,roi.y1,1);
    OFX::Point3D dstBottomRight = homography * OFX::Point3D(roi.x2,roi.y1,1);
    
    double l = std::min(std::min(dstTopLeft.x, dstBottomLeft.x),std::min(dstTopRight.x,dstBottomRight.x));
    double b = std::min(std::min(dstTopLeft.y, dstBottomLeft.y),std::min(dstTopRight.y,dstBottomRight.y));
    double r = std::max(std::max(dstTopLeft.x, dstBottomLeft.x),std::max(dstTopRight.x,dstBottomRight.x));
    double t = std::max(std::max(dstTopLeft.y, dstBottomLeft.y),std::max(dstTopRight.y,dstBottomRight.y));
    
    // GENERIC
    int filter;
    _filter->getValue(filter);
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    
    OfxRectD srcRoI;
    srcRoI.x1 = l;
    srcRoI.x2 = r;
    srcRoI.y1 = b;
    srcRoI.y2 = t;
    assert(srcRoI.x1 < srcRoI.x2 && srcRoI.y1 < srcRoI.y2);
    
    ofxsFilterExpandRoI(roi, srcClip_->getPixelAspectRatio(), args.renderScale, (FilterEnum)filter, false, 1, &srcRoI);
    
    rois.setRegionOfInterest(*srcClip_, roi);

}

// overridden is identity
bool CornerPinPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    
    OfxPointD topLeft,topRight,btmLeft,btmRight;
    _topLeft->getValue(topLeft.x, topLeft.y);
    _topRight->getValue(topRight.x, topRight.y);
    _btmLeft->getValue(btmLeft.x, btmLeft.y);
    _btmRight->getValue(btmRight.x, btmRight.y);
    
    OFX::Matrix3x3 extraMat = getExtraMatrix(args.time);
    
    OFX::Point3D p1,p2,p3,p4,q1,q2,q3,q4;
    
    p1.x = srcRoD.x1; p1.y = srcRoD.y2; p1.z = 1; //top left
    p2.x = srcRoD.x2; p2.y = srcRoD.y2; p2.z = 1; //top right
    p3.x = srcRoD.x2; p3.y = srcRoD.y1; p3.z = 1; //btm right
    p4.x = srcRoD.x1; p4.y = srcRoD.y1; p4.z = 1; //btm left
    
    q1.x = topLeft.x; q1.y = topLeft.y; q1.z = 1;
    q2.x = topRight.x; q2.y = topRight.y; q2.z = 1;
    q3.x = btmRight.x; q3.y = btmRight.y; q3.z = 1;
    q4.x = btmLeft.x; q4.y = btmLeft.y; q4.z = 1;
    
    if (p1 == q1 && p2 == q2 && p3 == q3 && p4 == q4 && extraMat.isIdentity()) {
        identityClip = srcClip_;
        identityTime = args.time;
        return true;
    }
    
    return false;
}


#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool CornerPinPlugin::getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9])
{
    
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    
    double extraMat[16];
    for (int i = 0; i < 16; ++i) {
        _extraMatrix[i]->getValue(extraMat[i]);
    }
    
    bool invert;
    _invert->getValue(invert);
    
    OFX::Matrix3x3 transform;
    OFX::Point3D p1,p2,p3,p4;
    
    p1.x = srcRoD.x1; p1.y = srcRoD.y2; p1.z = 1; //top left
    p2.x = srcRoD.x2; p2.y = srcRoD.y2; p2.z = 1; //top right
    p3.x = srcRoD.x2; p3.y = srcRoD.y1; p3.z = 1; //btm right
    p4.x = srcRoD.x1; p4.y = srcRoD.y1; p4.z = 1; //btm left
    
    bool success = getHomography(args.time, args.renderScale, !invert, p1, p2, p3, p4, transform);
    
    if (!success) {
        ///cannot compute the corner pin
        setPersistentMessage(OFX::Message::eMessageError, "", "Cannot compute a corner pin when 3 points are aligned.");
        return false;
    }

    transformClip = srcClip_;
    transformMatrix[0] = transform.a;
    transformMatrix[1] = transform.b;
    transformMatrix[2] = transform.c;
    transformMatrix[3] = transform.d;
    transformMatrix[4] = transform.e;
    transformMatrix[5] = transform.f;
    transformMatrix[6] = transform.g;
    transformMatrix[7] = transform.h;
    transformMatrix[8] = transform.i;
    return true;
}

#endif


template <class PIX, int nComponents, int maxValue, bool masked>
void
CornerPinPlugin::renderInternalForBitDepth(const OFX::RenderArguments &args)
{
    int filter;
    _filter->getValue(filter);
    bool clamp;
    _clamp->getValue(clamp);
    
    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch ((FilterEnum)filter) {
        case eFilterImpulse:
        {
            TransformProcessor<PIX, nComponents, maxValue, masked, eFilterImpulse, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterBilinear:
        {
            TransformProcessor<PIX, nComponents, maxValue, masked, eFilterBilinear, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterCubic:
        {
            TransformProcessor<PIX, nComponents, maxValue, masked, eFilterCubic, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterKeys:
            if (clamp) {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterKeys, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterKeys, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterSimon:
            if (clamp) {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterSimon, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterSimon, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterRifman:
            if (clamp) {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterRifman, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterRifman, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterMitchell:
            if (clamp) {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterMitchell, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                TransformProcessor<PIX, nComponents, maxValue, masked, eFilterMitchell, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterParzen:
        {
            TransformProcessor<PIX, nComponents, maxValue, masked, eFilterParzen, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterNotch:
        {
            TransformProcessor<PIX, nComponents, maxValue, masked, eFilterNotch, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
    }
}


// the internal render function
template <int nComponents, bool masked>
void
CornerPinPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
            renderInternalForBitDepth<unsigned char, nComponents, 255, masked>(args);
            break;
        case OFX::eBitDepthUShort :
            renderInternalForBitDepth<unsigned short, nComponents, 65535, masked>(args);
            break;
        case OFX::eBitDepthFloat :
            renderInternalForBitDepth<float, nComponents, 1, masked>(args);
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CornerPinPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        if (_masked) {
            renderInternal<4,true>(args, dstBitDepth);
        } else {
            renderInternal<4,false>(args, dstBitDepth);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        if (_masked) {
            renderInternal<3,true>(args, dstBitDepth);
        } else {
            renderInternal<3,false>(args, dstBitDepth);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        if (_masked) {
            renderInternal<1,true>(args, dstBitDepth);
        } else {
            renderInternal<1,false>(args, dstBitDepth);
        }
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
        _invert = effect->fetchBooleanParam(kInvertParamName);
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
    

    
    bool isNearbyTopLeft(const OfxPointD& pos,double tolerance) const;
    bool isNearbyTopRight(const OfxPointD& pos,double tolerance) const;
    bool isNearbyBtmLeft(const OfxPointD& pos,double tolerance) const;
    bool isNearbyBtmRight(const OfxPointD& pos,double tolerance) const;
    
    OFX::Double2DParam* _topLeft;
    OFX::Double2DParam* _topRight;
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _btmRight;
    OFX::BooleanParam* _invert;
    MouseState _ms;
    DrawState _ds;
    OfxPointD _lastMousePos;
    OfxPointD _lastPenDownPos;
    
    OfxPointD _topLeftDraggedPos;
    OfxPointD _topRightDraggedPos;
    OfxPointD _btmRightDraggedPos;
    OfxPointD _btmLeftDraggedPos;
    
};


bool CornerPinTransformInteract::isNearbyTopLeft(const OfxPointD& pos,double tolerance) const
{
    OfxPointD topLeft;
    _topLeft->getValue(topLeft.x, topLeft.y);
    if (pos.x >= (topLeft.x - tolerance) && pos.x <= (topLeft.x + tolerance) &&
        pos.y >= (topLeft.y - tolerance) && pos.y <= (topLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyTopRight(const OfxPointD& pos,double tolerance) const
{
    OfxPointD topRight;
    _topRight->getValue(topRight.x, topRight.y);
    if (pos.x >= (topRight.x - tolerance) && pos.x <= (topRight.x + tolerance) &&
        pos.y >= (topRight.y - tolerance) && pos.y <= (topRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyBtmLeft(const OfxPointD& pos,double tolerance) const
{
    OfxPointD btmLeft;
    _btmLeft->getValue(btmLeft.x, btmLeft.y);
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::isNearbyBtmRight(const OfxPointD& pos,double tolerance) const
{
    OfxPointD btmRight;
     _btmRight->getValue(btmRight.x, btmRight.y);
    if (pos.x >= (btmRight.x - tolerance) && pos.x <= (btmRight.x + tolerance) &&
        pos.y >= (btmRight.y - tolerance) && pos.y <= (btmRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }
}

bool CornerPinTransformInteract::draw(const OFX::DrawArgs &args)
{

    OfxPointD topLeft,topRight,btmLeft,btmRight;
    if (_ms == eDraggingTopLeft) {
        topLeft = _topLeftDraggedPos;
    } else {
        _topLeft->getValue(topLeft.x, topLeft.y);
    }
    
    if (_ms == eDraggingTopRight) {
        topRight = _topRightDraggedPos;
    } else {
        _topRight->getValue(topRight.x, topRight.y);
    }
    
    if (_ms == eDraggingBottomLeft) {
        btmLeft = _btmLeftDraggedPos;
    } else {
        _btmLeft->getValue(btmLeft.x, btmLeft.y);
    }
    
    if (_ms == eDraggingBottomRight) {
        btmRight = _btmRightDraggedPos;
    } else {
        _btmRight->getValue(btmRight.x, btmRight.y);
    }
    
    OfxPointD lineSize;
    lineSize.x = POINT_INTERACT_LINE_SIZE_PIXELS * args.pixelScale.x;
    lineSize.y = POINT_INTERACT_LINE_SIZE_PIXELS * args.pixelScale.y;
    
    glPointSize(5);
    glLineWidth(2);
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

    
    glBegin(GL_LINES);
    ///top left
    if (_ds == eHoveringTopLeft || _ms == eDraggingTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 0.8, 0., 1.);
    }
    glVertex2d(topLeft.x, topLeft.y - lineSize.y);
    glVertex2d(topLeft.x, topLeft.y);
    glVertex2d(topLeft.x, topLeft.y);
    glVertex2d(topLeft.x + lineSize.x, topLeft.y);
    
    ///top right
    if (_ds == eHoveringTopRight || _ms == eDraggingTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 0.8, 0., 1.);
    }
    glVertex2d(topRight.x, topRight.y - lineSize.y);
    glVertex2d(topRight.x, topRight.y);
    glVertex2d(topRight.x, topRight.y);
    glVertex2d(topRight.x - lineSize.x, topRight.y);
    
    ///bottom right
    if (_ds == eHoveringBottomRight || _ms == eDraggingBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 0.8, 0., 1.);
    }
    glVertex2d(btmRight.x, btmRight.y + lineSize.y);
    glVertex2d(btmRight.x, btmRight.y);
    glVertex2d(btmRight.x, btmRight.y);
    glVertex2d(btmRight.x - lineSize.x, btmRight.y);
    
    ///bottom left
    if (_ds == eHoveringBottomLeft || _ms == eDraggingBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1., 0.8, 0., 1.);
    }
    glVertex2d(btmLeft.x, btmLeft.y + lineSize.y);
    glVertex2d(btmLeft.x, btmLeft.y);
    glVertex2d(btmLeft.x, btmLeft.y);
    glVertex2d(btmLeft.x + lineSize.x, btmLeft.y);
    glEnd();
    
    glPointSize(1.);
    glLineWidth(1.);
    
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
    
    double selectionTol = 15. * args.pixelScale.x;
    if (isNearbyBtmLeft(args.penPosition, selectionTol)) {
        _ds = eHoveringBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol)) {
        _ds = eHoveringBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol)) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol)) {
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
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol)) {
        _ms = eDraggingBottomLeft;
        _btmLeftDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol)) {
        _ms = eDraggingBottomRight;
        _btmRightDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol)) {
        _ms = eDraggingTopRight;
        _topRightDraggedPos = args.penPosition;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol)) {
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
    
    
    if (_ms == eDraggingBottomLeft) {
        _btmLeft->setValue(_btmLeftDraggedPos.x,_btmLeftDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingBottomRight) {
        _btmRight->setValue(_btmRightDraggedPos.x,_btmRightDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingTopLeft) {
        _topLeft->setValue(_topLeftDraggedPos.x,_topLeftDraggedPos.y);
        didSomething = true;
    } else if (_ms == eDraggingTopRight) {
        _topRight->setValue(_topRightDraggedPos.x,_topRightDraggedPos.y);
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

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);


    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
    if (getImageEffectHostDescription()->canTransform) {
        std::cout << "kFnOfxImageEffectCanTransform (describe) =" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
    }
#endif
}

static void defineCornerPinDouble2DParam(OFX::ImageEffectDescriptor &desc,PageParamDescriptor *page,
                                         const std::string& name,double x,double y)
{

    Double2DParamDescriptor* size = desc.defineDouble2DParam(name);
    size->setLabels(name, name, name);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setAnimates(true);
    size->setDefault(x, y);
    size->setDimensionLabels("x", "y");
    size->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*size);

    std::string enableName("enable " + name);
    BooleanParamDescriptor* enable = desc.defineBooleanParam(enableName);
    enable->setLabels(enableName, enableName, enableName);
    enable->setDefault(true);
    enable->setAnimates(false);
    enable->setHint("Enables the point on the left.");
    page->addChild(*enable);
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

void CornerPinPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // NON-GENERIC PARAMETERS
    //
    defineCornerPinDouble2DParam(desc, page, kTopLeftParamName,0,1);
    defineCornerPinDouble2DParam(desc, page, kTopRightParamName,1,1);
    defineCornerPinDouble2DParam(desc, page, kBtmLeftParamName,0,0);
    defineCornerPinDouble2DParam(desc, page, kBtmRightParamName,1,0);

    GroupParamDescriptor* extraMatrix = desc.defineGroupParam("Extra matrix");
    extraMatrix->setHint("This matrix gets concatenated to the transform defined by the other parameters.");
    extraMatrix->setLabels("Extra matrix", "Extra matrix", "Extra matrix");
    extraMatrix->setOpen(false);

    defineExtraMatrixRow(desc, page, extraMatrix,"row1",1,0,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row2",0,1,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row3",0,0,1);


    BooleanParamDescriptor* invert = desc.defineBooleanParam(kInvertParamName);
    invert->setLabels(kInvertParamName, kInvertParamName, kInvertParamName);
    invert->setDefault(false);
    invert->setAnimates(false);
    page->addChild(*invert);

    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page);

    // NON-GENERIC (NON-MASKED)
    //
#ifdef OFX_EXTENSIONS_NUKE
    if (getImageEffectHostDescription()->canTransform) {
        std::cout << "kFnOfxImageEffectCanTransform in describeincontext(" << context << ")=" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
    }
#endif
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

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);

    // NON-GENERIC

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

    desc.setOverlayInteractDescriptor(new CornerPinOverlayDescriptor);
}

void CornerPinMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // GENERIC (MASKED)
    //
    // if general or paint context, define the mask clip
    if (context == eContextGeneral || context == eContextPaint) {
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // NON-GENERIC PARAMETERS
    //
    defineCornerPinDouble2DParam(desc, page, kTopLeftParamName,0,1);
    defineCornerPinDouble2DParam(desc, page, kTopRightParamName,1,1);
    defineCornerPinDouble2DParam(desc, page, kBtmLeftParamName,0,0);
    defineCornerPinDouble2DParam(desc, page, kBtmRightParamName,1,0);

    GroupParamDescriptor* extraMatrix = desc.defineGroupParam("Extra matrix");
    extraMatrix->setHint("This matrix gets concatenated to the transform defined by the other parameters.");
    extraMatrix->setLabels("Extra matrix", "Extra matrix", "Extra matrix");
    extraMatrix->setOpen(false);

    defineExtraMatrixRow(desc, page, extraMatrix,"row1",1,0,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row2",0,1,0);
    defineExtraMatrixRow(desc, page, extraMatrix,"row3",0,0,1);


    BooleanParamDescriptor* invert = desc.defineBooleanParam(kInvertParamName);
    invert->setLabels(kInvertParamName, kInvertParamName, kInvertParamName);
    invert->setDefault(false);
    invert->setAnimates(false);
    page->addChild(*invert);

    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page);

    // GENERIC (MASKED)
    //
    ofxsFilterDescribeParamsMaskMix(desc, page);
}

OFX::ImageEffect* CornerPinMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CornerPinPlugin(handle, true);
}


