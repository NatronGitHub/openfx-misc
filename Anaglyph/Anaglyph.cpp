/*
 OFX Anaglyph plugin.
 Make an anaglyph image out of the inputs.

 Copyright (C) 2013 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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

#include "Anaglyph.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

#define kPluginName "AnaglyphOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription "Make an anaglyph image out of the two views of the input."
#define kPluginIdentifier "net.sf.openfx.anaglyphPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamAmtColour "amtcolor"
#define kParamAmtColourLabel "Color Amount"
#define kParamAmtColourHint "Amount of colour in the anaglyph: 0 = grayscale anaglyph, 1 = full-color anaglyph. Fusion is more difficult with full-color anaglyphs."

#define kParamSwap "swap"
#define kParamSwapLabel "(right=red)"
#define kParamSwapHint "Swap left and right views"

#define kParamOffset "offset"
#define kParamOffsetLabel "Horizontal Offset"
#define kParamOffsetHint "Horizontal offset. " \
"The red view is shifted to the left by half this amount, " \
"and the cyan view is shifted to the right by half this amount (in pixels)."  // rounded up // rounded down

// Base class for the RGBA and the Alpha processor
class AnaglyphBase : public OFX::ImageProcessor {
protected:
    const OFX::Image *_srcLeftImg;
    const OFX::Image *_srcRightImg;
    double _amtcolour;
    bool _swap;
    int _offset;

public:
    /** @brief no arg ctor */
    AnaglyphBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcLeftImg(0)
    , _srcRightImg(0)
    , _amtcolour(0.)
    , _swap(false)
    , _offset(0)
    {
    }

    /** @brief set the left src image */
    void setSrcLeftImg(const OFX::Image *v) {_srcLeftImg = v;}

    /** @brief set the right src image */
    void setSrcRightImg(const OFX::Image *v) {_srcRightImg = v;}

    /** @brief set the amount of colour */
    void setAmtColour(double v) {_amtcolour = v;}

    /** @brief set view swap */
    void setSwap(bool v) {_swap = v;}

    /** @brief set view offset */
    void setOffset(int v) {_offset = v;}
};

// template to do the RGBA processing
template <class PIX, int max>
class ImageAnaglypher : public AnaglyphBase
{
public:
    // ctor
    ImageAnaglypher(OFX::ImageEffect &instance)
    : AnaglyphBase(instance)
    {}

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const OFX::Image *srcRedImg = _srcLeftImg;
        const OFX::Image *srcCyanImg = _srcRightImg;
        if (_swap) {
            std::swap(srcRedImg, srcCyanImg);
        }
        const OfxRectI& srcRedBounds = srcRedImg->getBounds();
        const OfxRectI& srcCyanBounds = srcCyanImg->getBounds();


        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // clamp x to avoid black borders
                int xRed = std::min(std::max(srcRedBounds.x1,x+(_offset+1)/2),srcRedBounds.x2-1);
                int xCyan = std::min(std::max(srcCyanBounds.x1,x-_offset/2),srcCyanBounds.x2-1);

                const PIX *srcRedPix = (const PIX *)(srcRedImg ? srcRedImg->getPixelAddress(xRed, y) : 0);
                const PIX *srcCyanPix = (const PIX *)(srcCyanImg ? srcCyanImg->getPixelAddress(xCyan, y) : 0);

                dstPix[3] = 0; // start with transparent
                if (srcRedPix) {
                    PIX srcLuminance = luminance(srcRedPix[0],srcRedPix[1],srcRedPix[2]);
                    dstPix[0] = srcLuminance*(1.-_amtcolour) + srcRedPix[0]*_amtcolour;
                    dstPix[3] += 0.5*srcRedPix[3];
                } else {
                    // no src pixel here, be black and transparent
                    dstPix[0] = 0;
                }
                if (srcCyanPix) {
                    PIX srcLuminance = luminance(srcCyanPix[0],srcCyanPix[1],srcCyanPix[2]);
                    dstPix[1] = srcLuminance*(1.-_amtcolour) + srcCyanPix[1]*_amtcolour;
                    dstPix[2] = srcLuminance*(1.-_amtcolour) + srcCyanPix[2]*_amtcolour;
                    dstPix[3] += 0.5*srcCyanPix[3];
                } else {
                    // no src pixel here, be black and transparent
                    dstPix[1] = 0;
                    dstPix[2] = 0;
                }

                // increment the dst pixel
                dstPix += 4;
            }
        }
    }

private:
    /** @brief luminance from linear RGB according to Rec.709.
     See http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html#RTFToC9 */
    static PIX luminance(PIX red, PIX green, PIX blue) {
        return  PIX(0.2126*red + 0.7152*green + 0.0722*blue);
    }
};

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AnaglyphPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    AnaglyphPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , amtcolour_(0)
    , swap_(0)
    , offset_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && srcClip_->getPixelComponents() == ePixelComponentRGBA);
        amtcolour_  = fetchDoubleParam(kParamAmtColour);
        swap_ = fetchBooleanParam(kParamSwap);
        offset_ = fetchIntParam(kParamOffset);
        assert(amtcolour_ && swap_ && offset_);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(AnaglyphBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

    OFX::DoubleParam  *amtcolour_;
    OFX::BooleanParam *swap_;
    OFX::IntParam     *offset_;
    
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
AnaglyphPlugin::setupAndProcess(AnaglyphBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
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
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

    // fetch main input image
    std::auto_ptr<const OFX::Image> srcLeft(srcClip_->fetchStereoscopicImage(args.time,0));
    if (srcLeft.get()) {
        if (srcLeft->getRenderScale().x != args.renderScale.x ||
            srcLeft->getRenderScale().y != args.renderScale.y ||
            srcLeft->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    std::auto_ptr<const OFX::Image> srcRight(srcClip_->fetchStereoscopicImage(args.time,1));
    if (srcRight.get()) {
        if (srcRight->getRenderScale().x != args.renderScale.x ||
            srcRight->getRenderScale().y != args.renderScale.y ||
            srcRight->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // make sure bit depths are sane
    if (srcLeft.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcLeft->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcLeft->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    if (srcRight.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcRight->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcRight->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    double amtcolour = amtcolour_->getValueAtTime(args.time);
    bool swap = swap_->getValueAtTime(args.time);
    int offset = offset_->getValueAtTime(args.time);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcLeftImg(srcLeft.get());
    processor.setSrcRightImg(srcRight.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the parameters
    processor.setAmtColour(amtcolour);
    processor.setSwap(swap);
    processor.setOffset(offset);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
AnaglyphPlugin::render(const OFX::RenderArguments &args)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    assert(dstComponents == OFX::ePixelComponentRGBA);

    switch (dstBitDepth) {
        case OFX::eBitDepthUByte : {
            ImageAnaglypher<unsigned char, 255> fred(*this);
            setupAndProcess(fred, args);
        }
            break;

        case OFX::eBitDepthUShort : {
            ImageAnaglypher<unsigned short, 65535> fred(*this);
            setupAndProcess(fred, args);
        }
            break;

        case OFX::eBitDepthFloat : {
            ImageAnaglypher<float, 1> fred(*this);
            setupAndProcess(fred, args);
        }
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}


using namespace OFX;

mDeclarePluginFactory(AnaglyphPluginFactory, ;, {});

void AnaglyphPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void AnaglyphPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only filter at the moment
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // returning an error here crashes Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void AnaglyphPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmtColour);
        param->setLabels(kParamAmtColourLabel, kParamAmtColourLabel, kParamAmtColourLabel);
        param->setHint(kParamAmtColourHint);
        param->setDefault(0.);
        param->setRange(0., 1.);
        param->setIncrement(0.01);
        param->setDisplayRange(0., 1.);
        param->setDoubleType(eDoubleTypeScale);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamSwap);
        param->setLabels(kParamSwapLabel, kParamSwapLabel, kParamSwapLabel);
        param->setDefault(false);
        param->setHint(kParamSwapHint);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamOffset);
        param->setLabels(kParamOffsetLabel, kParamOffsetLabel, kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDefault(0);
        param->setRange(-1000, 1000);
        param->setDisplayRange(-100, 100);
        param->setAnimates(true);
        page->addChild(*param);
    }
}

OFX::ImageEffect* AnaglyphPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new AnaglyphPlugin(handle);
}

void getAnaglyphPluginID(OFX::PluginFactoryArray &ids)
{
    static AnaglyphPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

