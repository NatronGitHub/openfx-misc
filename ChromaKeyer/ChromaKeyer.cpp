/*
 OFX Chroma Keyer plugin.
 
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

#include "ChromaKeyer.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "../include/ofxsProcessing.H"

/*
  Simple Chroma Keyer.

  Algorithm description:

  - http://www.cs.utah.edu/~michael/chroma/
  - Keith Jack, "Video Demystified", Independent Pub Group (Computer), 1996, pp. 214-222, http://www.ee-techs.com/circuit/video-demy5.pdf
*/

#define kKeyColorParamName "Key Color"
#define kKeyColorParamHint "Foreground key color; foreground areas containing the key color are replaced with the background image."
#define kAcceptanceAngleParamName "Acceptance Angle"
#define kAcceptanceAngleParamHint "Foreground colors are only suppressed inside the acceptance angle."
#define kOutputModeParamName "Output Mode"
#define kOutputModeCompositeOption "Composite"
#define kOutputModeCompositeHint "Color is the composite of Source and Bg. Alpha is the foreground key."
#define kOutputModePremultipliedOption "Premultiplied"
#define kOutputModePremultipliedHint "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key."
#define kOutputModeUnpremultipliedOption "Unpremultiplied"
#define kOutputModeUnpremultipliedHint "Color is the Source color after key color suppression. Alpha is the foreground key."
#define kBgClipName "Bg"
#define kInsideMaskClipName "InM"
#define kOutsideMaskClipName "OutM"

enum OutputModeEnum {
    eOutputModeComposite,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
};

using namespace OFX;

class ChromaKeyerProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_bgImg;
    OFX::Image *_inMaskImg;
    OFX::Image *_outMaskImg;
    OfxRGBColourD _keyColor;
    double _acceptanceAngle;
    OutputModeEnum _outputmode;

public:
    
    ChromaKeyerProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _bgImg(0)
    , _inMaskImg(0)
    , _outMaskImg(0)
    , _outputmode(eOutputModeComposite)
    {
        
    }
    
    void setSrcImgs(OFX::Image *srcImg, OFX::Image *bgImg, OFX::Image *inMaskImg, OFX::Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }
    
    void setValues(const OfxRGBColourD& keyColor, double acceptanceAngle, OutputModeEnum outputmode)
    {
        _keyColor = keyColor;
        _acceptanceAngle = acceptanceAngle;
        _outputmode = outputmode;
    }
    
};



template <class PIX, int nComponents, int maxValue>
class ChromaKeyerProcessor : public ChromaKeyerProcessorBase
{
public :
    ChromaKeyerProcessor(OFX::ImageEffect &instance)
    : ChromaKeyerProcessorBase(instance)
    {
        
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                PIX *bgPix = (PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                PIX *inMaskPix = (PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                PIX *outMaskPix = (PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);
                
#pragma message ("TODO")
                for (int c = 0; c < nComponents; ++c) {
                    dstPix[c] = srcPix[c];
                }
            }
        }
    }
 
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ChromaKeyerPlugin : public OFX::ImageEffect
{
public :
    /** @brief ctor */
    ChromaKeyerPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , bgClip_(0)
    , inMaskClip_(0)
    , outMaskClip_(0)
    , keyColor_(0)
    , acceptanceAngle_(0)
    , outputMode_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        bgClip_ = fetchClip(kBgClipName);
        inMaskClip_ = fetchClip(kInsideMaskClipName);;
        outMaskClip_ = fetchClip(kOutsideMaskClipName);;
        keyColor_ = fetchRGBParam(kKeyColorParamName);
        acceptanceAngle_ = fetchDoubleParam(kAcceptanceAngleParamName);
        outputMode_ = fetchChoiceParam(kOutputModeParamName);
    }
 
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(ChromaKeyerProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *bgClip_;
    OFX::Clip *inMaskClip_;
    OFX::Clip *outMaskClip_;
    
    OFX::RGBParam* keyColor_;
    OFX::DoubleParam* acceptanceAngle_;
    OFX::ChoiceParam* outputMode_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ChromaKeyerPlugin::setupAndProcess(ChromaKeyerProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> bg(bgClip_->fetchImage(args.time));
    if(src.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    if(bg.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = bg->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = bg->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    // auto ptr for the masks.
    std::auto_ptr<OFX::Image> inMask(inMaskClip_ ? inMaskClip_->fetchImage(args.time) : 0);
    std::auto_ptr<OFX::Image> outMask(outMaskClip_ ? outMaskClip_->fetchImage(args.time) : 0);
    
    OfxRGBColourD keyColor;
    double acceptanceAngle;
    int outputModeI;
    OutputModeEnum outputMode;
    keyColor_->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
    acceptanceAngle_->getValueAtTime(args.time, acceptanceAngle);
    outputMode_->getValue(outputModeI);
    outputMode = (OutputModeEnum)outputModeI;
    processor.setValues(keyColor, acceptanceAngle, outputMode);
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(), bg.get(), inMask.get(), outMask.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
ChromaKeyerPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if(dstComponents == OFX::ePixelComponentRGBA)
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                ChromaKeyerProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                ChromaKeyerProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


void ChromaKeyerPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ChromaKeyerOFX", "ChromaKeyerOFX", "ChromaKeyerOFX");
    desc.setPluginGrouping("Keyer");
    desc.setPluginDescription("Apply chroma keying");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}


void ChromaKeyerPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setOptional(false);
    
    ClipDescriptor* bgClip = desc.defineClip(kBgClipName);
    bgClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    bgClip->addSupportedComponent( OFX::ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(true);
    bgClip->setOptional(true);

   // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);
    
    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kInsideMaskClipName);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(true);
    inMaskClip->setIsMask(true);
    
    ClipDescriptor *outMaskClip =  desc.defineClip(kOutsideMaskClipName);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(true);
    outMaskClip->setIsMask(true);
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
 
    RGBParamDescriptor* keyColor = desc.defineRGBParam(kKeyColorParamName);
    keyColor->setLabels(kKeyColorParamName, kKeyColorParamName, kKeyColorParamName);
    keyColor->setHint(kKeyColorParamHint);
    keyColor->setDefault(0.,1.,0.);
    keyColor->setAnimates(true);
    page->addChild(*keyColor);
    
    DoubleParamDescriptor* acceptanceAngle = desc.defineDoubleParam(kAcceptanceAngleParamName);
    acceptanceAngle->setLabels(kAcceptanceAngleParamName, kAcceptanceAngleParamName, kAcceptanceAngleParamName);
    acceptanceAngle->setHint(kAcceptanceAngleParamHint);
    acceptanceAngle->setDoubleType(eDoubleTypeAngle);;
    acceptanceAngle->setDefault(60.);
    acceptanceAngle->setAnimates(true);
    page->addChild(*acceptanceAngle);
    
    ChoiceParamDescriptor* outputMode = desc.defineChoiceParam(kOutputModeParamName);
    outputMode->setLabels(kOutputModeParamName, kOutputModeParamName, kOutputModeParamName);
    outputMode->appendOption(kOutputModeCompositeOption, kOutputModeCompositeHint);
    outputMode->appendOption(kOutputModePremultipliedOption, kOutputModePremultipliedHint);
    outputMode->appendOption(kOutputModeUnpremultipliedOption, kOutputModeUnpremultipliedHint);
    page->addChild(*outputMode);
}

OFX::ImageEffect* ChromaKeyerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ChromaKeyerPlugin(handle);
}

