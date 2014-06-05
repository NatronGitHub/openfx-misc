/*
 OFX Crop plugin.
 
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

#include "Crop.h"

#include <cmath>
#include <algorithm>


#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif


#include "ofxsProcessing.H"
#include "ofxsMerging.h"

#define kBtmLeftParamName "Bottom left"
#define kSizeParamName "Size"
#define kReformatParamName "Reformat"
#define kIntersectParamName "Intersect"
#define kBlackOutsideParamName "Black outside"
#define kSoftnessParamName "Softness"

using namespace OFX;

class CropProcessorBase : public OFX::ImageProcessor
{
   
    
protected:
    OFX::Image *_srcImg;
    
    double _softness;
    bool _blackOutside;
    OfxPointI _translation;
    OfxRectI _dstRoDPix;
    
public:
    CropProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(OFX::Image *v)
    {
        _srcImg = v;
    }

    
    void setValues(const OfxRectI& cropRect,const OfxRectI& dstRoDPix,bool bo,bool reformat,double softness)
    {
        _softness = softness;
        _blackOutside = bo;
        _dstRoDPix = dstRoDPix;
        if (reformat) {
            _translation.x = cropRect.x1;
            _translation.y = cropRect.y1;
        } else {
            _translation.x = 0;
            _translation.y = 0;
        }
    }

};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class CropProcessor : public CropProcessorBase
{
public:
    CropProcessor(OFX::ImageEffect &instance)
    : CropProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        
        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y)
        {
            if(_effect.abort()) break;
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            bool yblack = _blackOutside && (y == _dstRoDPix.y1 || y == (_dstRoDPix.y2 - 1));

            // distance to the nearest crop area horizontal edge
            int yDistance = _blackOutside + std::min(y - _dstRoDPix.y1, _dstRoDPix.y2 - 1 - y);
            // handle softness
            double yMultiplier = yDistance < _softness ? (double)yDistance / _softness : 1.;

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                bool xblack = _blackOutside && (x == _dstRoDPix.x1 || x == (_dstRoDPix.x2 - 1));
                // treat the black case separately
                if (xblack || yblack || !_srcImg) {
                    for (int k = 0; k < nComponents; ++k) {
                        dstPix[k] =  0.;
                    }
                } else {
                    // distance to the nearest crop area vertical edge
                    int xDistance = _blackOutside + std::min(x - _dstRoDPix.x1, _dstRoDPix.x2 - 1 - x);
                    // handle softness
                    double xMultiplier = xDistance < _softness ? (double)xDistance / _softness : 1.;

                    PIX *srcPix = (PIX*)_srcImg->getPixelAddress(x + _translation.x, y + _translation.y);
                    if (!srcPix) {
                        for (int k = 0; k < nComponents; ++k) {
                            dstPix[k] =  0.;
                        }
                    } else if (xMultiplier != 1. || yMultiplier != 1.) {
                        for (int k = 0; k < nComponents; ++k) {
                            dstPix[k] =  srcPix[k] * xMultiplier * yMultiplier;
                        }
                    } else {
                        for (int k = 0; k < nComponents; ++k) {
                            dstPix[k] =  srcPix[k];
                        }
                    }
                }
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CropPlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::DoubleParam* _softness;
    OFX::BooleanParam* _reformat;
    OFX::BooleanParam* _intersect;
    OFX::BooleanParam* _blackOutside;
    
public:
    /** @brief ctor */
    CropPlugin(OfxImageEffectHandle handle, bool masked)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , _btmLeft(0)
    , _size(0)
    , _softness(0)
    , _reformat(0)
    , _intersect(0)
    , _blackOutside(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
        
        _btmLeft = fetchDouble2DParam(kBtmLeftParamName);
        _size = fetchDouble2DParam(kSizeParamName);
        _softness = fetchDoubleParam(kSoftnessParamName);
        _reformat = fetchBooleanParam(kReformatParamName);
        _intersect = fetchBooleanParam(kIntersectParamName);
        _blackOutside = fetchBooleanParam(kBlackOutsideParamName);
        
        assert(_btmLeft && _size && _softness && _reformat && _intersect && _blackOutside);
    }
    
private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);
    
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(CropProcessorBase &, const OFX::RenderArguments &args);
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);
    
    void getCropRectangle_canonical(OfxTime time,bool useReformat,bool forceIntersect,OfxRectD& cropRect) const;
};

void
CropPlugin::getCropRectangle_canonical(OfxTime time,bool useReformat,bool forceIntersect,OfxRectD& cropRect) const
{
    
    bool intersect;
    if (!forceIntersect) {
        _intersect->getValue(intersect);
    } else {
        intersect = true;
    }
    
    bool reformat;
    if (useReformat) {
        _reformat->getValue(reformat);
    } else {
        reformat = false;
    }
    
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    
    if (reformat) {
        cropRect.x1 = cropRect.y1 = 0.;
    } else {
        _btmLeft->getValue(cropRect.x1, cropRect.y1);
    }
    
    double w,h;
    _size->getValue(w, h);
    cropRect.x2 = cropRect.x1 + w;
    cropRect.y2 = cropRect.y1 + h;
    
    if (blackOutside) {
        cropRect.x1 -= 1;
        cropRect.y1 -= 1;
        cropRect.x2 += 1;
        cropRect.y2 += 1;
    }
    
    if (intersect) {
        OfxRectD srcRoD = srcClip_->getRegionOfDefinition(time);
        MergeImages2D::rectangleIntersect(cropRect, srcRoD, &cropRect);
    }
    

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CropPlugin::setupAndProcess(CropProcessorBase &processor, const OFX::RenderArguments &args)
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
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    bool reformat;
    _reformat->getValue(reformat);
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    
    OfxRectD cropRectCanonical;
    getCropRectangle_canonical(args.time, false, false, cropRectCanonical);
    OfxRectI cropRectPixel;
    cropRectPixel.x1 = cropRectCanonical.x1;
    cropRectPixel.y1 = cropRectCanonical.y1;
    cropRectPixel.x2 = cropRectCanonical.x2;
    cropRectPixel.y2 = cropRectCanonical.y2;
    
    unsigned int mipMapLevel = MergeImages2D::getLevelFromScale(args.renderScale.x);
    cropRectPixel = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(cropRectPixel, mipMapLevel);
    
    double softness;
    _softness->getValue(softness);
    softness *= args.renderScale.x;
    
    OfxRectD dstRoD = dstClip_->getRegionOfDefinition(args.time);
    OfxRectI dstRoDPix;
    dstRoDPix.x1 = dstRoD.x1;
    dstRoDPix.y1 = dstRoD.y1;
    dstRoDPix.x2 = dstRoD.x2;
    dstRoDPix.y2 = dstRoD.y2;
    dstRoDPix = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(dstRoDPix, mipMapLevel);
   
    processor.setValues(cropRectPixel,dstRoDPix,blackOutside,reformat,softness);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}



// override the roi call
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
CropPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{

    OfxRectD cropRect;
    getCropRectangle_canonical(args.time, false, true, cropRect);
    
    // set it on the mask only if we are in an interesting context
    // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
    rois.setRegionOfInterest(*srcClip_, cropRect);
}


bool
CropPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    getCropRectangle_canonical(args.time, true, false, rod);
    return true;
}

// the internal render function
template <int nComponents>
void
CropPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
        {
            CropProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            CropProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            CropProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CropPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}


void
CropPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kReformatParamName) {
        bool reformat;
        _reformat->getValue(reformat);
        _btmLeft->setEnabled(!reformat);
    }
}


using namespace OFX;



class CropInteract : public OFX::OverlayInteract
{
    
    enum MouseState
    {
        eIdle = 0,
        eDraggingTopLeft,
        eDraggingTopRight,
        eDraggingBottomLeft,
        eDraggingBottomRight,
        eDraggingCenter,
        eDraggingMidTop,
        eDraggingMidRight,
        eDraggingMidBtm,
        eDraggingMidLeft
    };
    
    enum DrawState
    {
        eInactive = 0,
        eHoveringTopLeft,
        eHoveringTopRight,
        eHoveringBottomLeft,
        eHoveringBottomRight,
        eHoveringCenter,
        eHoveringMidTop,
        eHoveringMidRight,
        eHoveringMidBtm,
        eHoveringMidLeft
    };
    
public:
    
    CropInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _lastMousePos()
    , _ms(eIdle)
    , _ds(eInactive)
    , _btmLeft(0)
    , _size(0)
    {
        _btmLeft = effect->fetchDouble2DParam(kBtmLeftParamName);
        _size = effect->fetchDouble2DParam(kSizeParamName);
        addParamToSlaveTo(_btmLeft);
        addParamToSlaveTo(_size);
        assert(_btmLeft && _size);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    virtual bool keyDown(const OFX::KeyArgs &/*args*/) { return false; }
    virtual bool keyUp(const OFX::KeyArgs &/*args*/) { return false; }
    
private:
    

    bool isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    bool isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const;
    
    OfxPointD _lastMousePos;
    MouseState _ms;
    DrawState _ds;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    OfxPointD _btmLeftDragPos;
    OfxPointD _sizeDrag;
    
};

bool CropInteract::isNearbyTopLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD topLeft;
    topLeft.x = btmLeft.x;
    topLeft.y = btmLeft.y + size.y;
    if (pos.x >= (topLeft.x - tolerance) && pos.x <= (topLeft.x + tolerance) &&
        pos.y >= (topLeft.y - tolerance) && pos.y <= (topLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyTopRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD topRight;
    topRight.x = btmLeft.x + size.x;
    topRight.y = btmLeft.y + size.y;
    if (pos.x >= (topRight.x - tolerance) && pos.x <= (topRight.x + tolerance) &&
        pos.y >= (topRight.y - tolerance) && pos.y <= (topRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyBtmLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    if (pos.x >= (btmLeft.x - tolerance) && pos.x <= (btmLeft.x + tolerance) &&
        pos.y >= (btmLeft.y - tolerance) && pos.y <= (btmLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyBtmRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD btmRight;
    btmRight.x = btmLeft.x + size.x;
    btmRight.y = btmLeft.y ;
    if (pos.x >= (btmRight.x - tolerance) && pos.x <= (btmRight.x + tolerance) &&
        pos.y >= (btmRight.y - tolerance) && pos.y <= (btmRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyMidTop(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midTop;
    midTop.x = btmLeft.x + size.x / 2.;
    midTop.y = btmLeft.y + size.y;
    if (pos.x >= (midTop.x - tolerance) && pos.x <= (midTop.x + tolerance) &&
        pos.y >= (midTop.y - tolerance) && pos.y <= (midTop.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyMidRight(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midRight;
    midRight.x = btmLeft.x + size.x;
    midRight.y = btmLeft.y + size.y / 2.;
    if (pos.x >= (midRight.x - tolerance) && pos.x <= (midRight.x + tolerance) &&
        pos.y >= (midRight.y - tolerance) && pos.y <= (midRight.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyMidLeft(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midLeft;
    midLeft.x = btmLeft.x;
    midLeft.y = btmLeft.y + size.y /2.;
    if (pos.x >= (midLeft.x - tolerance) && pos.x <= (midLeft.x + tolerance) &&
        pos.y >= (midLeft.y - tolerance) && pos.y <= (midLeft.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::isNearbyMidBtm(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD midBtm;
    midBtm.x = btmLeft.x + size.x / 2.;
    midBtm.y = btmLeft.y ;
    if (pos.x >= (midBtm.x - tolerance) && pos.x <= (midBtm.x + tolerance) &&
        pos.y >= (midBtm.y - tolerance) && pos.y <= (midBtm.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}
bool CropInteract::isNearbyCenter(const OfxPointD& pos,double tolerance,const OfxPointD& size,const OfxPointD& btmLeft) const
{
    OfxPointD center;
    center.x = btmLeft.x + size.x / 2.;
    center.y = btmLeft.y + size.y / 2.;
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
        pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance)) {
        return true;
    } else {
        return false;
    }

}

bool CropInteract::draw(const OFX::DrawArgs &args)
{
    OfxPointD btmLeft,size;
    if (_ms != eIdle) {
        btmLeft = _btmLeftDragPos;
        size = _sizeDrag;
    } else {
        _btmLeft->getValue(btmLeft.x, btmLeft.y);
        _size->getValue(size.x, size.y);
    }
    
    OfxPointD topLeft,midTop,topRight,midRight,btmRight,midBtm,midLeft,center;
    topLeft.x = btmLeft.x;
    topLeft.y = btmLeft.y + size.y;
    midTop.x = btmLeft.x + size.x / 2.;
    midTop.y = topLeft.y;
    topRight.x = btmLeft.x + size.x;
    topRight.y = topLeft.y;
    midRight.x = topRight.x;
    midRight.y = btmLeft.y + size.y / 2.;
    btmRight.x = midRight.x;
    btmRight.y = btmLeft.y;
    midBtm.x = midTop.x;
    midBtm.y = btmLeft.y;
    midLeft.x = btmLeft.x;
    midLeft.y = midRight.y;
    center.x = midTop.x;
    center.y = midRight.y;
    
    glColor4f(0.9, 0.9, 0.9, 1.);
    glBegin(GL_LINE_STRIP);
    glVertex2d(btmLeft.x, btmLeft.y);
    glVertex2d(topLeft.x, topLeft.y);
    glVertex2d(topRight.x, topRight.y);
    glVertex2d(btmRight.x, btmRight.y);
    glVertex2d(btmLeft.x, btmLeft.y);
    glEnd();
    
    glPointSize(6);
    glBegin(GL_POINTS);
    if (_ds == eHoveringBottomLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(btmLeft.x, btmLeft.y);
    if (_ds == eHoveringMidLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(midLeft.x, midLeft.y);
    if (_ds == eHoveringTopLeft) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(topLeft.x, topLeft.y);
    if (_ds == eHoveringMidTop) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(midTop.x, midTop.y);
    if (_ds == eHoveringTopRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(topRight.x, topRight.y);
    if (_ds == eHoveringMidRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(midRight.x, midRight.y);
    if (_ds == eHoveringBottomRight) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(btmRight.x, btmRight.y);
    if (_ds == eHoveringMidBtm) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(midBtm.x, midBtm.y);
    glEnd();
    glPointSize(1);
    
    ///draw center cross hair
    double lineSizeX = 7 * args.pixelScale.x;
    double lineSizeY = 7 * args.pixelScale.y;
    glBegin(GL_LINES);
    if (_ds == eHoveringCenter) {
        glColor4f(0., 1., 0., 1.);
    } else {
        glColor4f(1, 1, 1, 1);
    }
    glVertex2d(center.x - lineSizeX, center.y);
    glVertex2d(center.x + lineSizeX, center.y);
    glVertex2d(center.x, center.y - lineSizeY);
    glVertex2d(center.x, center.y + lineSizeY);
    glEnd();
    
 
    return true;
}

bool CropInteract::penMotion(const OFX::PenArgs &args)
{
    bool didSomething = false;
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD size;
    _size->getValue(size.x, size.y);
    OfxPointD btmLeft;
    _btmLeft->getValue(btmLeft.x, btmLeft.y);
    
    bool lastStateWasHovered = _ds != eInactive;
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, size, btmLeft)) {
        _ds = eHoveringCenter;
        didSomething = true;
    } else {
        _ds = eInactive;
    }
    
    if (_ms == eDraggingBottomLeft) {
        OfxPointD topRight;
        topRight.x = _btmLeftDragPos.x + _sizeDrag.x;
        topRight.y = _btmLeftDragPos.y + _sizeDrag.y;
        _btmLeftDragPos.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.x = topRight.x - _btmLeftDragPos.x;
        _sizeDrag.y = topRight.y - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingTopLeft) {
        OfxPointD btmRight;
        btmRight.x = _btmLeftDragPos.x + _sizeDrag.x;
        btmRight.y = _btmLeftDragPos.y;
        _btmLeftDragPos.x += delta.x;
        _sizeDrag.y += delta.y;
        _sizeDrag.x = btmRight.x - _btmLeftDragPos.x;
        didSomething = true;
    } else if (_ms == eDraggingTopRight) {
        _sizeDrag.x += delta.x;
        _sizeDrag.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingBottomRight) {
        OfxPointD topLeft;
        topLeft.x = _btmLeftDragPos.x;
        topLeft.y = _btmLeftDragPos.y + _sizeDrag.y;
        _sizeDrag.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.y = topLeft.y - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingMidTop) {
        _sizeDrag.y += delta.y;
        didSomething = true;
    } else if (_ms == eDraggingMidRight) {
        _sizeDrag.x += delta.x;
        didSomething = true;
    } else if (_ms == eDraggingMidBtm) {
        double top = _btmLeftDragPos.y + _sizeDrag.y;
        _btmLeftDragPos.y += delta.y;
        _sizeDrag.y = top - _btmLeftDragPos.y;
        didSomething = true;
    } else if (_ms == eDraggingMidLeft) {
        double right = _btmLeftDragPos.x + _sizeDrag.x;
        _btmLeftDragPos.x += delta.x;
        _sizeDrag.x = right - _btmLeftDragPos.x;
        didSomething = true;
    } else if (_ms == eDraggingCenter) {
        _btmLeftDragPos.x += delta.x;
        _btmLeftDragPos.y += delta.y;
        didSomething = true;
    }

    
    //if size is negative shift bottom left
    if (_sizeDrag.x < 0) {
        if (_ms == eDraggingBottomLeft) {
            _ms = eDraggingBottomRight;
        } else if (_ms == eDraggingMidLeft) {
            _ms = eDraggingMidRight;
        } else if (_ms == eDraggingTopLeft) {
            _ms = eDraggingTopRight;
        } else if (_ms == eDraggingBottomRight) {
            _ms = eDraggingBottomLeft;
        } else if (_ms == eDraggingMidRight) {
            _ms = eDraggingMidLeft;
        } else if (_ms == eDraggingTopRight) {
            _ms = eDraggingTopLeft;
        }
        
        _btmLeftDragPos.x += _sizeDrag.x;
        _sizeDrag.x = - _sizeDrag.x;
    }
    if (_sizeDrag.y < 0) {
        if (_ms == eDraggingTopLeft) {
            _ms = eDraggingBottomLeft;
        } else if (_ms == eDraggingMidTop) {
            _ms = eDraggingMidBtm;
        } else if (_ms == eDraggingTopRight) {
            _ms = eDraggingBottomRight;
        } else if (_ms == eDraggingBottomLeft) {
            _ms = eDraggingTopLeft;
        } else if (_ms == eDraggingMidBtm) {
            _ms = eDraggingMidTop;
        } else if (_ms == eDraggingBottomRight) {
            _ms = eDraggingTopRight;
        }
        
        _btmLeftDragPos.y += _sizeDrag.y;
        _sizeDrag.y = - _sizeDrag.y;
    }
    
    ///forbid 0 pixels wide crop rectangles
    if (_sizeDrag.x < 1) {
        _sizeDrag.x = 1;
    }
    if (_sizeDrag.y < 1) {
        _sizeDrag.y = 1;
    }
    
    ///repaint if we toggled off a hovered handle
    if (lastStateWasHovered && !didSomething) {
        didSomething = true;
    }
    
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool CropInteract::penDown(const OFX::PenArgs &args)
{
    bool didSomething = false;
    
    double selectionTol = 15. * args.pixelScale.x;
    
    OfxPointD size;
    _size->getValue(size.x, size.y);
    OfxPointD btmLeft;
    _btmLeft->getValue(btmLeft.x, btmLeft.y);
    
    if (isNearbyBtmLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingBottomLeft;
        didSomething = true;
    } else if (isNearbyBtmRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingBottomRight;
        didSomething = true;
    } else if (isNearbyTopRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingTopRight;
        didSomething = true;
    } else if (isNearbyTopLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingTopLeft;
        didSomething = true;
    } else if (isNearbyMidTop(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingMidTop;
        didSomething = true;
    } else if (isNearbyMidRight(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingMidRight;
        didSomething = true;
    } else if (isNearbyMidBtm(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingMidBtm;
        didSomething = true;
    } else if (isNearbyMidLeft(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingMidLeft;
        didSomething = true;
    } else if (isNearbyCenter(args.penPosition, selectionTol, size, btmLeft)) {
        _ms = eDraggingCenter;
        didSomething = true;
    } else {
        _ms = eIdle;
    }
    _btmLeftDragPos = btmLeft;
    _sizeDrag = size;
    _lastMousePos = args.penPosition;
    return didSomething;
}

bool CropInteract::penUp(const OFX::PenArgs &args)
{
    bool didSmthing = false;
    if (_ms != eIdle) {
        _btmLeft->setValue(_btmLeftDragPos.x, _btmLeftDragPos.y);
        _size->setValue(_sizeDrag.x, _sizeDrag.y);
        didSmthing = true;
    }
    _ms = eIdle;
    return didSmthing;
}

class CropOverlayDescriptor : public DefaultEffectOverlayDescriptor<CropOverlayDescriptor, CropInteract> {};


void CropPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("CropOFX", "CropOFX", "CropOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Removes everything outside the defined rectangle and adds black edges so everything outside is black.");
    
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);
    

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);
    desc.setOverlayInteractDescriptor(new CropOverlayDescriptor);

}



OFX::ImageEffect* CropPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CropPlugin(handle, false);
}




void CropPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
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
    srcClip->setOptional(true);
    

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    
    Double2DParamDescriptor* btmLeft = desc.defineDouble2DParam(kBtmLeftParamName);
    btmLeft->setLabels(kBtmLeftParamName,kBtmLeftParamName,kBtmLeftParamName);
    btmLeft->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    btmLeft->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    btmLeft->setDefault(0., 0.);
    btmLeft->setIncrement(1.);
    btmLeft->setHint("Coordinates of the bottom left corner of the crop rectangle");
    btmLeft->setDigits(0);
    page->addChild(*btmLeft);
    
    Double2DParamDescriptor* size = desc.defineDouble2DParam(kSizeParamName);
    size->setLabels(kSizeParamName, kSizeParamName, kSizeParamName);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setDefault(1., 1.);
    size->setDimensionLabels("width", "height");
    size->setHint("Width and height of the crop rectangle");
    size->setIncrement(1.);
    size->setDigits(0);
    page->addChild(*size);
    
    DoubleParamDescriptor* softness = desc.defineDoubleParam(kSoftnessParamName);
    softness->setLabels(kSoftnessParamName, kSoftnessParamName, kSoftnessParamName);
    softness->setDefault(0);
    softness->setRange(0., 100.);
    softness->setHint("Size of the fade to black around edges to apply");
    page->addChild(*softness);
    
    BooleanParamDescriptor* reformat = desc.defineBooleanParam(kReformatParamName);
    reformat->setLabels(kReformatParamName, kReformatParamName, kReformatParamName);
    reformat->setHint("Translates the bottom left corner of the crop rectangle to be in (0,0).");
    reformat->setDefault(false);
    reformat->setAnimates(false);
    reformat->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*reformat);
    
    BooleanParamDescriptor* intersect = desc.defineBooleanParam(kIntersectParamName);
    intersect->setLabels(kIntersectParamName, kIntersectParamName, kIntersectParamName);
    intersect->setHint("Intersects the crop rectangle with the input region of definition instead of extending it");
    intersect->setLayoutHint(OFX::eLayoutHintNoNewLine);
    intersect->setDefault(false);
    intersect->setAnimates(false);
    page->addChild(*intersect);
    
    BooleanParamDescriptor* blackOutside = desc.defineBooleanParam(kBlackOutsideParamName);
    blackOutside->setLabels(kBlackOutsideParamName, kBlackOutsideParamName, kBlackOutsideParamName);
    blackOutside->setDefault(false);
    blackOutside->setAnimates(false);
    blackOutside->setHint("Add 1 black pixel to the region of definition so that all the area outside the crop rectangle is black");
    page->addChild(*blackOutside);
    
}

