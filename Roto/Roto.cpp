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

#include "Roto.h"

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMacros.h"

#define kPluginName "RotoOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription "Create masks and shapes."
#define kPluginIdentifier "net.sf.openfx.RotoPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamPremult "premultiply"
#define kParamPremultLabel "Premultiply"
#define kParamPremultHint "Premultiply the red, green and blue channels with the alpha channel produced by the mask."


using namespace OFX;

class RotoProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_roto;
    PixelComponentEnum _srcComponents;

public:
    RotoProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _roto(0)
    , _srcComponents(ePixelComponentNone)
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

        // roto and dst should have the same number of components
        assert(!_roto ||
               (_roto->getPixelComponents() == ePixelComponentAlpha && dstNComponents == 1) ||
               (_roto->getPixelComponents() == ePixelComponentXY && dstNComponents == 2) ||
               (_roto->getPixelComponents() == ePixelComponentRGB && dstNComponents == 3) ||
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

                PIX srcAlpha = PIX();
                if (srcPix) {
                    if (srcNComponents == 1) {
                        srcAlpha = srcPix[0];
                    } else if (srcNComponents == 3 || srcNComponents == 2) {
                        srcAlpha = 0;
                    } else if (srcNComponents == 4) {
                        srcAlpha = srcPix[3];
                    }
                }
                PIX maskAlpha;
                if (dstNComponents == 1) {
                    maskAlpha = maskPix ? maskPix[0] : 0;
                } else if (dstNComponents == 4) {
                    maskAlpha = maskPix ? maskPix[dstNComponents-1] : 0;
                } else {
                    maskAlpha = 1;
                }
                

                PIX srcVal[dstNComponents];
                // fill srcVal (hopefully the compiler will optimize this)
                if (!srcPix) {
                    for (int c = 0; c < dstNComponents; ++c) {
                        srcVal[c] = 0;
                    }
                } else if (dstNComponents == 1) {
                    srcVal[0] = srcAlpha;
                } else if (dstNComponents == 2) {
                    if (srcNComponents == 1) {
                        for (int c = 0; c < dstNComponents; ++c) {
                            srcVal[c] = 0;
                        }
                    } else {
                        for (int c = 0; c < dstNComponents; ++c) {
                            srcVal[c] = srcPix[c];
                        }
                    }
                } else if (dstNComponents == 3) {
                    if (srcNComponents == 1) {
                        for (int c = 0; c < dstNComponents; ++c) {
                            srcVal[c] = 0;
                        }
                    } else {
                        assert(srcNComponents == 3 || srcNComponents == 4);
                        for (int c = 0; c < dstNComponents; ++c) {
                            srcVal[c] = srcPix[c];
                        }
                    }
                } else if (dstNComponents == 4) {
                    if (srcNComponents == 3) {
                        for (int c = 0; c < dstNComponents-1; ++c) {
                            srcVal[c] = srcPix[c];
                        }
                        srcVal[dstNComponents-1] = PIX();
                    } else if (srcNComponents == 4) {
                        for (int c = 0; c < dstNComponents; ++c) {
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
                    dstPix[c] = OFX::MergeImages2D::overFunctor<PIX,maxValue>(maskPix ? maskPix[c] : PIX(), srcVal[c], maskAlpha, srcAlpha);
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
  RotoPlugin(OfxImageEffectHandle handle, bool /*masked*/)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _rotoClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        // name of mask clip depends on the context
        _rotoClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Roto");
        assert(_rotoClip && (_rotoClip->getPixelComponents() == ePixelComponentAlpha || _rotoClip->getPixelComponents() == ePixelComponentRGBA));
    }
    
private:
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    template <int srcNComponents, int dstNComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    template <int dstNComponents>
    void renderInternalNComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RotoProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_rotoClip;
};



////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RotoPlugin::setupAndProcess(RotoProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get() && dst.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
        if (srcBitDepth != dstBitDepth)
            OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    
    // auto ptr for the mask.
    std::auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _rotoClip && _rotoClip->isConnected()) ?
                                         _rotoClip->fetchImage(args.time) : 0);
    
    // do we do masking
    if (getContext() != OFX::eContextFilter && _rotoClip && _rotoClip->isConnected()) {
        if (!mask.get()) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        assert(mask->getPixelComponents() == OFX::ePixelComponentRGBA || mask->getPixelComponents() == OFX::ePixelComponentAlpha
               || mask->getPixelComponents() == OFX::ePixelComponentRGB || mask->getPixelComponents() == OFX::ePixelComponentXY);
        if (mask->getPixelComponents() != dst->getPixelComponents()) {
            OFX::throwSuiteStatusException(kOfxStatErrFormat);
        }
        // Set it in the processor
        processor.setRotoImg(mask.get());
    }


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
    if (!(_srcClip && _srcClip->isConnected())) {
        return false;
    } else {
        rod = _srcClip->getRegionOfDefinition(args.time);
        OfxRectD rotoRod;
        try {
            rotoRod = _rotoClip->getRegionOfDefinition(args.time);
        } catch (...) {
            ///If an exception is thrown, that is because the RoD of the roto is NULL (i.e there isn't any shape)
            ///Don't fail getRoD, just take the RoD of the source instead so that in RGBA mode is still displays the source
            ///image
            return true;
        }
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
        case OFX::eBitDepthUByte: {
            RotoProcessor<unsigned char, srcNComponents, dstNComponents, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            RotoProcessor<unsigned short, srcNComponents, dstNComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            RotoProcessor<float, srcNComponents, dstNComponents, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
template <int dstNComponents>
void
RotoPlugin::renderInternalNComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       srcBitDepth    = _srcClip->getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = _srcClip->getPixelComponents();

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
    assert(_srcClip && _dstClip);
    if (!_srcClip || !_dstClip) {
        throwSuiteStatusException(kOfxStatErrBadHandle);
    }
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentXY);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternalNComponents<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternalNComponents<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternalNComponents<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternalNComponents<1>(args, dstBitDepth);
    }
}


bool
RotoPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    if (_rotoClip && _rotoClip->isConnected()) {
        OfxRectI rotoRoD;
        OFX::MergeImages2D::toPixelEnclosing(_rotoClip->getRegionOfDefinition(args.time), args.renderScale, _rotoClip->getPixelAspectRatio(), &rotoRoD);
        // effect is identity if the renderWindow doesn't intersect the roto RoD
        if (!OFX::MergeImages2D::rectIntersection<OfxRectI>(args.renderWindow, rotoRoD, 0)) {
            identityClip = _srcClip;
            return true;
        }
    }

    return false;
}

void
RotoPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PreMultiplicationEnum srcPremult = _srcClip->getPreMultiplication();
    if (srcPremult == eImageOpaque) {
        // we're changing alpha, the image becomes UnPremultiplied
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

using namespace OFX;

mDeclarePluginFactory(RotoPluginFactory, {}, {});

void
RotoPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
    desc.setSupportsTiles(kSupportsTiles);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentAlpha);
#endif
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
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);
    
    // if general or paint context, define the mask clip
    if (context == eContextGeneral || context == eContextPaint) {
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Roto") : desc.defineClip("Brush");
        maskClip->setTemporalClipAccess(false);
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        if (context == eContextGeneral) {
            maskClip->addSupportedComponent(ePixelComponentRGBA);
            maskClip->addSupportedComponent(ePixelComponentXY);
            //maskClip->addSupportedComponent(ePixelComponentRGB);
            maskClip->setOptional(false);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(context == eContextPaint); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);



}

void getRotoPluginID(OFX::PluginFactoryArray &ids)
{
    static RotoPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

