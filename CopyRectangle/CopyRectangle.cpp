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
#include "CopyRectangle.h"

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsRectangleInteract.h"
#include "ofxsFilter.h"

#define kSrcClipAName "A"
#define kSrcClipBName "B"
#define kSoftnessParamName "Softness"
#define kChannelsParamName "Channels"


enum ChannelChoice
{
    CHANNEL_R = 0,
    CHANNEL_G,
    CHANNEL_B,
    CHANNEL_A,
    CHANNEL_RGB,
    CHANNEL_RGBA
};

using namespace OFX;

class CopyRectangleProcessorBase : public OFX::ImageProcessor
{
   
    
protected:
    OFX::Image *_srcImgA;
    OFX::Image *_srcImgB;
    double _softness;
    double _mix;
    ChannelChoice _channels;
    OfxRectI _rectangle;
public:
    CopyRectangleProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    {
    }

    /** @brief set the src image */
    void setSrcImgs(OFX::Image *A,OFX::Image* B)
    {
        _srcImgA = A;
        _srcImgB = B;
    }
    
    void setValues(const OfxRectI& rectangle,double softness,ChannelChoice channels,double mix)
    {
        _rectangle = rectangle;
        _softness = softness;
        _channels = channels;
        _mix = mix;
    }

};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class CopyRectangleProcessor : public CopyRectangleProcessorBase
{
public:
    CopyRectangleProcessor(OFX::ImageEffect &instance)
    : CopyRectangleProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double yMultiplier,xMultiplier;

        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y)
        {
            if(_effect.abort()) break;
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            

            // distance to the nearest rectangle area horizontal edge
            int yDistance =  std::min(y - _rectangle.y1, _rectangle.y2 - 1 - y);
            
            // handle softness
            if (y >= _rectangle.y1 && y < _rectangle.y2) {
                ///apply softness only within the rectangle
                yMultiplier = yDistance < _softness ? (double)yDistance / _softness : 1.;
            } else {
                yMultiplier = 1.;
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                
                // distance to the nearest rectangle area vertical edge
                int xDistance =  std::min(x - _rectangle.x1, _rectangle.x2 - 1 - x);
                // handle softness
                if (x >= _rectangle.x1 && x < _rectangle.x2) {
                    ///apply softness only within the rectangle
                    xMultiplier = xDistance < _softness ? (double)xDistance / _softness : 1.;
                } else {
                    xMultiplier = 1.;
                }
                
                PIX *srcPixA = (PIX*)_srcImgA->getPixelAddress(x, y);
                PIX *srcPixB = (PIX*)_srcImgB->getPixelAddress(x, y);
                
                for (int k = 0; k < nComponents; ++k) {
                    PIX A = srcPixA ? *srcPixA : 0.;
                    PIX B = srcPixB ? *srcPixB : 0.;
                    dstPix[k] = (A * (1. - _mix) + B * _mix) * xMultiplier * yMultiplier;
                }
                
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CopyRectanglePlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClipA_;
    OFX::Clip *srcClipB_;
    
    OFX::Double2DParam* _btmLeft;
    OFX::Double2DParam* _size;
    OFX::DoubleParam* _softness;
    OFX::ChoiceParam* _channels;
    OFX::DoubleParam* _mix;
    
public:
    /** @brief ctor */
    CopyRectanglePlugin(OfxImageEffectHandle handle, bool masked)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClipA_(0)
    , srcClipB_(0)
    , _btmLeft(0)
    , _size(0)
    , _softness(0)
    , _channels(0)
    , _mix(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClipA_ = fetchClip(kSrcClipAName);
        assert(srcClipA_->getPixelComponents() == ePixelComponentAlpha || srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA);
        srcClipB_ = fetchClip(kSrcClipBName);
        assert(srcClipB_->getPixelComponents() == ePixelComponentAlpha || srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA);
        
        _btmLeft = fetchDouble2DParam(kRectInteractBtmLeftParamName);
        _size = fetchDouble2DParam(kRectInteractSizeParamName);
        _softness = fetchDoubleParam(kSoftnessParamName);
        _channels = fetchChoiceParam(kChannelsParamName);
        _mix = fetchDoubleParam(kFilterMixParamName);
        
        assert(_btmLeft && _size && _softness && _channels && _mix);
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
    void setupAndProcess(CopyRectangleProcessorBase &, const OFX::RenderArguments &args);
    
    void getRectanglecanonical(OfxTime time,OfxRectD& rect) const;
};

void
CopyRectanglePlugin::getRectanglecanonical(OfxTime time,OfxRectD& rect) const
{
    _btmLeft->getValueAtTime(time, rect.x1, rect.y1);
    double w,h;
    _size->getValueAtTime(time, w, h);
    rect.x2 = rect.x1 + w;
    rect.y2 = rect.y1 + h;

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CopyRectanglePlugin::setupAndProcess(CopyRectangleProcessorBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> srcA(srcClipA_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> srcB(srcClipB_->fetchImage(args.time));
    if (srcA.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (srcB.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImgs(srcA.get(),srcB.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    OfxRectD rectangle;
    getRectanglecanonical(args.time, rectangle);
    OfxRectI rectanglePixel;
    rectanglePixel.x1 = rectangle.x1;
    rectanglePixel.y1 = rectangle.y1;
    rectanglePixel.x2 = rectangle.x2;
    rectanglePixel.y2 = rectangle.y2;
    
    unsigned int mipMapLevel = MergeImages2D::getLevelFromScale(args.renderScale.x);
    rectanglePixel = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(rectanglePixel, mipMapLevel);
    
    double softness;
    _softness->getValueAtTime(args.time, softness);
    softness *= args.renderScale.x;
    
    OfxRectD dstRoD = dstClip_->getRegionOfDefinition(args.time);
    OfxRectI dstRoDPix;
    dstRoDPix.x1 = dstRoD.x1;
    dstRoDPix.y1 = dstRoD.y1;
    dstRoDPix.x2 = dstRoD.x2;
    dstRoDPix.y2 = dstRoD.y2;
    dstRoDPix = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(dstRoDPix, mipMapLevel);
   
    int channels;
    _channels->getValueAtTime(args.time,channels);
    
    double mix;
    _mix->getValueAtTime(args.time, mix);
    
    processor.setValues(rectanglePixel, softness, (ChannelChoice)channels, mix);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}



// override the roi call
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
CopyRectanglePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{

    OfxRectD rectangle;
    getRectanglecanonical(args.time, rectangle);
    OfxRectD srcB_rod = srcClipB_->getRegionOfDefinition(args.time);
    
    // set it on the mask only if we are in an interesting context
    // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
    rois.setRegionOfInterest(*srcClipB_, srcB_rod);
    rois.setRegionOfInterest(*srcClipA_, rectangle);
}


bool
CopyRectanglePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    getRectanglecanonical(args.time, rod);
    OfxRectD srcB_rod = srcClipB_->getRegionOfDefinition(args.time);
    rod = MergeImages2D::rectanglesBoundingBox(rod, srcB_rod);
    return true;
}

// the internal render function
template <int nComponents>
void
CopyRectanglePlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
        {
            CopyRectangleProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            CopyRectangleProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            CopyRectangleProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
CopyRectanglePlugin::render(const OFX::RenderArguments &args)
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



using namespace OFX;



mDeclarePluginFactory(CopyRectanglePluginFactory, {}, {});

void CopyRectanglePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("CopyRectangleOFX", "CopyRectangleOFX", "CopyRectangleOFX");
    desc.setPluginGrouping("Merge");
    desc.setPluginDescription("Copies a rectangle from the input A to the input B in output. It can be used to limit an effect "
                              "to a rectangle of the original image by plugging the original image into the input B.");
    
    desc.addSupportedContext(eContextGeneral);

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
    desc.setOverlayInteractDescriptor(new RectangleOverlayDescriptor);

}



OFX::ImageEffect* CopyRectanglePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CopyRectanglePlugin(handle, false);
}




void CopyRectanglePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClipA = desc.defineClip(kSrcClipAName);
    srcClipA->addSupportedComponent(ePixelComponentRGBA);
    srcClipA->addSupportedComponent(ePixelComponentRGB);
    srcClipA->addSupportedComponent(ePixelComponentAlpha);
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(true);
    srcClipA->setIsMask(false);
    
    ClipDescriptor *srcClipB = desc.defineClip(kSrcClipBName);
    srcClipB->addSupportedComponent(ePixelComponentRGBA);
    srcClipB->addSupportedComponent(ePixelComponentRGB);
    srcClipB->addSupportedComponent(ePixelComponentAlpha);
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(true);
    srcClipB->setIsMask(false);
    

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    
    Double2DParamDescriptor* btmLeft = desc.defineDouble2DParam(kRectInteractBtmLeftParamName);
    btmLeft->setLabels(kRectInteractBtmLeftParamLabel,kRectInteractBtmLeftParamLabel,kRectInteractBtmLeftParamLabel);
    btmLeft->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    btmLeft->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    btmLeft->setDefault(0., 0.);
    btmLeft->setIncrement(1.);
    btmLeft->setHint("Coordinates of the bottom left corner of the rectangle");
    btmLeft->setDigits(0);
    page->addChild(*btmLeft);
    
    Double2DParamDescriptor* size = desc.defineDouble2DParam(kRectInteractSizeParamName);
    size->setLabels(kRectInteractSizeParamLabel, kRectInteractSizeParamLabel, kRectInteractSizeParamLabel);
    size->setDoubleType(OFX::eDoubleTypeXYAbsolute);
    size->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
    size->setDefault(1., 1.);
    size->setDimensionLabels("width", "height");
    size->setHint("Width and height of the rectangle");
    size->setIncrement(1.);
    size->setDigits(0);
    page->addChild(*size);
    
    ChoiceParamDescriptor* channels = desc.defineChoiceParam(kChannelsParamName);
    channels->setLabels(kChannelsParamName, kChannelsParamName, kChannelsParamName);
    channels->setHint("The channels that will be copied to the B image.");
    channels->appendOption("R");
    channels->appendOption("G");
    channels->appendOption("B");
    channels->appendOption("A");
    channels->appendOption("RGB");
    channels->appendOption("RGBA");
    channels->setDefault((ChannelChoice)CHANNEL_RGBA);
    page->addChild(*channels);
    
    DoubleParamDescriptor* softness = desc.defineDoubleParam(kSoftnessParamName);
    softness->setLabels(kSoftnessParamName, kSoftnessParamName, kSoftnessParamName);
    softness->setDefault(0);
    softness->setRange(0., 100.);
    softness->setHint("Size of the fade around edges of the rectangle to apply");
    page->addChild(*softness);
    
    ofxsFilterDescribeParamsMaskMix(desc,page);
}

void getCopyRectanglePluginID(OFX::PluginFactoryArray &ids)
{
    static CopyRectanglePluginFactory p("net.sf.openfx:CopyRectanglePlugin", /*pluginVersionMajor=*/1, /*pluginVersionMinor=*/0);
    ids.push_back(&p);
}

