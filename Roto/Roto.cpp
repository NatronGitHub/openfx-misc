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

#include "Roto.h"

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMacros.h"

#define kPluginName "RotoOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription "Create masks and shapes."
#define kPluginIdentifier "net.sf.openfx:RotoPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kPremultParamName "premultiply"
#define kPremultParamLabel "Premultiply"
#define kPremultParamHint "Premultiply the red,green and blue channels with the alpha channel produced by the mask."

#define kOutputCompsParamName "outputComponents"
#define kOutputCompsParamLabel "Output components"
#define kOutputCompsParamOptionAlpha "Alpha"
#define kOutputCompsParamOptionRGBA "RGBA"

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component"

using namespace OFX;

class RotoProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_roto;
    PixelComponentEnum _srcComponents;
    bool _red;
    bool _green;
    bool _blue;
    bool _alpha;

public:
    RotoProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _roto(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
        _srcComponents = v ? _srcImg->getPixelComponents() : ePixelComponentNone;
    }

    /** @brief set the optional mask image */
    void setRotoImg(const OFX::Image *v) {_roto = v;}

    void setValues(bool red, bool green, bool blue, bool alpha)
    {
        _red = red;
        _green = green;
        _blue = blue;
        _alpha = alpha;
    }
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int srcNComponents, int dstNComponents, int maxValue>
class RotoProcessor : public RotoProcessorBase
{
public:
    RotoProcessor(OFX::ImageEffect &instance)
    : RotoProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        bool proc[dstNComponents];
        if (dstNComponents == 1) {
            proc[0] = _alpha;
        } else if (dstNComponents == 4) {
            proc[0] = _red;
            proc[1] = _green;
            proc[2] = _blue;
            proc[3] = _alpha;
        }

        // roto and dst should have the same number of components
        assert((_roto->getPixelComponents() == ePixelComponentAlpha && dstNComponents == 1) ||
               (_roto->getPixelComponents() == ePixelComponentRGBA && dstNComponents == 4));
        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
      
            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += dstNComponents) {

                const PIX *srcPix = (const PIX*)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const PIX *maskPix = (const PIX*) (_roto ? _roto->getPixelAddress(x, y) : 0);

                PIX srcAlpha = 0.;
                if (srcPix) {
                    if (srcNComponents == 1) {
                            srcAlpha = srcPix[0];
                    } else if (srcNComponents == 3) {
                            srcAlpha = 0.;
                    } else if (srcNComponents == 4) {
                            srcAlpha = srcPix[3];
                    }
                }
                PIX maskAlpha = maskPix ? maskPix[dstNComponents-1] : 0.;

                PIX srcVal[dstNComponents];
                // fill srcVal (hopefully the compiler will optimize this)
                if (!srcPix) {
                    for (int c = 0; c < dstNComponents; ++c) {
                        srcVal[c] = 0;
                    }
                } else if (dstNComponents == 1) {
                    srcVal[0] = srcAlpha;
                } else if (dstNComponents == 4) {
                    if (srcNComponents == 3) {
                        for (int c = 0; c < srcNComponents; ++c) {
                            srcVal[c] = srcPix[c];
                        }
                        srcVal[dstNComponents-1] = 0.;
                    } else if (srcNComponents == 4) {
                        for (int c = 0; c < srcNComponents; ++c) {
                            srcVal[c] = srcPix[c];
                        }
                    } else {
                        for (int c = 0; c < dstNComponents-1; ++c) {
                            srcVal[c] = 0;
                        }
                        srcVal[dstNComponents-1] = srcAlpha;
                    }
                }

                // merge/over
                for (int c = 0; c < dstNComponents; ++c) {
                    dstPix[c] = proc[c] ? OFX::MergeImages2D::overFunctor<PIX,maxValue>(maskPix ? maskPix[c] : 0., srcVal[c], maskAlpha, srcAlpha) : srcVal[c];
                }
            }
        }
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RotoPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    RotoPlugin(OfxImageEffectHandle handle, bool masked)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , rotoClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        // name of mask clip depends on the context
        rotoClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Roto");
        assert(rotoClip_ && (rotoClip_->getPixelComponents() == ePixelComponentAlpha || rotoClip_->getPixelComponents() == ePixelComponentRGBA));
        _outputComps = fetchChoiceParam(kOutputCompsParamName);
        assert(_outputComps);
        _paramProcessR = fetchBooleanParam(kParamProcessR);
        _paramProcessG = fetchBooleanParam(kParamProcessG);
        _paramProcessB = fetchBooleanParam(kParamProcessB);
        _paramProcessA = fetchBooleanParam(kParamProcessA);
        assert(_paramProcessR && _paramProcessG && _paramProcessB && _paramProcessA);
    }
    
private:
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    template <int srcNComponents, int dstNComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    template <int dstNComponents>
    void renderInternalNComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RotoProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *rotoClip_;
    OFX::BooleanParam* _paramProcessR;
    OFX::BooleanParam* _paramProcessG;
    OFX::BooleanParam* _paramProcessB;
    OFX::BooleanParam* _paramProcessA;
    ChoiceParam* _outputComps;
};



////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RotoPlugin::setupAndProcess(RotoProcessorBase &processor, const OFX::RenderArguments &args)
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
    if (src.get() && dst.get()) {
        OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
        OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
        if (srcBitDepth != dstBitDepth)
            OFX::throwSuiteStatusException(kOfxStatFailed);
        
        
    }
    
    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? rotoClip_->fetchImage(args.time) : 0);
    
    // do we do masking
    if (getContext() != OFX::eContextFilter && rotoClip_->isConnected()) {
        if (!mask.get()) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        assert(mask->getPixelComponents() == OFX::ePixelComponentRGBA || mask->getPixelComponents() == OFX::ePixelComponentAlpha);
        if (mask->getPixelComponents() != dst->getPixelComponents()) {
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
        }
        // Set it in the processor
        processor.setRotoImg(mask.get());
    }

    bool red, green, blue, alpha;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    _paramProcessA->getValueAtTime(args.time, alpha);
    processor.setValues(red, green, blue, alpha);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}



// (see comments in Natron code about this feature being buggy)
bool
RotoPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
#ifdef NATRON_ROTO_INVERTIBLE
    // if NATRON_ROTO_INVERTIBLE is defined (but this is buggy anyway),
    // RoD should be union(defaultRoD,inputsRoD)
    // Natron does this if the RoD is infinite
    rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
    rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    return true;
#else
    // if source is not connected, use the Mask RoD (i.e. the default RoD)
    // else use the union of Source and Mask RoD (Source is optional)
    if (!srcClip_->isConnected()) {
        return false;
    } else {
        rod = srcClip_->getRegionOfDefinition(args.time);
        OfxRectD rotoRod = rotoClip_->getRegionOfDefinition(args.time);
        rod.x1 = std::min(rod.x1, rotoRod.x1);
        rod.x2 = std::max(rod.x2, rotoRod.x2);
        rod.y1 = std::min(rod.y1, rotoRod.y1);
        rod.y2 = std::max(rod.y2, rotoRod.y2);
        return true;
    }
#endif
}

// the internal render function
template <int srcNComponents, int dstNComponents>
void
RotoPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte :
        {
            RotoProcessor<unsigned char, srcNComponents, dstNComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            RotoProcessor<unsigned short, srcNComponents, dstNComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            RotoProcessor<float, srcNComponents, dstNComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
template <int dstNComponents>
void
RotoPlugin::renderInternalNComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       srcBitDepth    = srcClip_->getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = srcClip_->getPixelComponents();

    assert(srcComponents == OFX::ePixelComponentRGBA || srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentAlpha);
    assert(srcBitDepth == dstBitDepth);

    if (srcComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4,dstNComponents>(args, dstBitDepth);
    } else if (srcComponents == OFX::ePixelComponentRGB) {
        renderInternal<3,dstNComponents>(args, dstBitDepth);
    } else {
        assert(srcComponents == OFX::ePixelComponentAlpha);
        renderInternal<1,dstNComponents>(args, dstBitDepth);
    }
}

// the overridden render function
void
RotoPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternalNComponents<4>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternalNComponents<1>(args, dstBitDepth);
    }
}

void
RotoPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    int index;
    _outputComps->getValue(index);
    PixelComponentEnum outputComponents;
    switch (index) {
        case 0:
            outputComponents = ePixelComponentAlpha;
            break;
        case 1:
            outputComponents = ePixelComponentRGBA;
            break;
            
        default:
            assert(false);
            break;
    }
    clipPreferences.setClipComponents(*rotoClip_, outputComponents);
    clipPreferences.setClipComponents(*dstClip_, outputComponents);
}

bool
RotoPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    OFX::PixelComponentEnum srcComponents  = srcClip_->getPixelComponents();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    if (srcComponents != dstComponents) {
        return false;
    }

    bool alpha;
    _paramProcessA->getValueAtTime(args.time, alpha);

    if (srcComponents == ePixelComponentAlpha && !alpha) {
        identityClip = srcClip_;
        return true;
    }
    bool red, green, blue;
    _paramProcessR->getValueAtTime(args.time, red);
    _paramProcessG->getValueAtTime(args.time, green);
    _paramProcessB->getValueAtTime(args.time, blue);
    if (srcComponents == ePixelComponentRGBA && !red && !green && !blue && !alpha) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

using namespace OFX;

mDeclarePluginFactory(RotoPluginFactory, {}, {});

void
RotoPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    
    desc.addSupportedContext(eContextPaint);
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
    
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

}



OFX::ImageEffect*
RotoPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new RotoPlugin(handle, false);
}




void
RotoPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
    
    // if general or paint context, define the mask clip
    if (context == eContextGeneral || context == eContextPaint) {
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Roto") : desc.defineClip("Brush");
        maskClip->setTemporalClipAccess(false);
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        if (context == eContextGeneral) {
            maskClip->addSupportedComponent(ePixelComponentRGBA); //< our brush can output RGBA
            maskClip->setOptional(false);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(context == eContextPaint); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    ChoiceParamDescriptor* outputComps = desc.defineChoiceParam(kOutputCompsParamName);
    outputComps->setLabels(kOutputCompsParamLabel, kOutputCompsParamLabel, kOutputCompsParamLabel);
    outputComps->setAnimates(false);
    outputComps->appendOption(kOutputCompsParamOptionAlpha);
    outputComps->appendOption(kOutputCompsParamOptionRGBA);
    outputComps->setDefault(0);
    desc.addClipPreferencesSlaveParam(*outputComps);
    page->addChild(*outputComps);

    OFX::BooleanParamDescriptor* processR = desc.defineBooleanParam(kParamProcessR);
    processR->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
    processR->setHint(kParamProcessRHint);
    processR->setDefault(true);
    processR->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processR);

    OFX::BooleanParamDescriptor* processG = desc.defineBooleanParam(kParamProcessG);
    processG->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
    processG->setHint(kParamProcessGHint);
    processG->setDefault(true);
    processG->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processG);

    OFX::BooleanParamDescriptor* processB = desc.defineBooleanParam( kParamProcessB );
    processB->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
    processB->setHint(kParamProcessBHint);
    processB->setDefault(true);
    processB->setLayoutHint(eLayoutHintNoNewLine);
    page->addChild(*processB);

    OFX::BooleanParamDescriptor* processA = desc.defineBooleanParam( kParamProcessA );
    processA->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
    processA->setHint(kParamProcessAHint);
    processA->setDefault(true);
    page->addChild(*processA);
}

void getRotoPluginID(OFX::PluginFactoryArray &ids)
{
    static RotoPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

