/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX Anaglyph plugin.
 * Make an anaglyph image out of the inputs.
 */

#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif
#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "AnaglyphOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription "Make an anaglyph image out of the two views of the input."
#define kPluginIdentifier "net.sf.openfx.anaglyphPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
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
    "and the cyan view is shifted to the right by half this amount (in pixels)." // rounded up // rounded down

// Base class for the RGBA and the Alpha processor
class AnaglyphBase
    : public ImageProcessor
{
protected:
    const Image *_srcLeftImg;
    const Image *_srcRightImg;
    double _amtcolour;
    bool _swap;
    int _offset;

public:
    /** @brief no arg ctor */
    AnaglyphBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcLeftImg(NULL)
        , _srcRightImg(NULL)
        , _amtcolour(0.)
        , _swap(false)
        , _offset(0)
    {
    }

    /** @brief set the left src image */
    void setSrcLeftImg(const Image *v) {_srcLeftImg = v; }

    /** @brief set the right src image */
    void setSrcRightImg(const Image *v) {_srcRightImg = v; }

    /** @brief set the amount of colour */
    void setAmtColour(double v) {_amtcolour = v; }

    /** @brief set view swap */
    void setSwap(bool v) {_swap = v; }

    /** @brief set view offset */
    void setOffset(int v) {_offset = v; }
};

// template to do the RGBA processing
template <class PIX, int max>
class ImageAnaglypher
    : public AnaglyphBase
{
public:
    // ctor
    ImageAnaglypher(ImageEffect &instance)
        : AnaglyphBase(instance)
    {}

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        const Image *srcRedImg = _srcLeftImg;
        const Image *srcCyanImg = _srcRightImg;

        if (_swap) {
            std::swap(srcRedImg, srcCyanImg);
        }
        const OfxRectI& srcRedBounds = srcRedImg->getBounds();
        const OfxRectI& srcCyanBounds = srcCyanImg->getBounds();


        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // clamp x to avoid black borders
                int xRed = (std::min)((std::max)(srcRedBounds.x1, x + (_offset + 1) / 2), srcRedBounds.x2 - 1);
                int xCyan = (std::min)((std::max)(srcCyanBounds.x1, x - _offset / 2), srcCyanBounds.x2 - 1);
                const PIX *srcRedPix = (const PIX *)(srcRedImg ? srcRedImg->getPixelAddress(xRed, y) : 0);
                const PIX *srcCyanPix = (const PIX *)(srcCyanImg ? srcCyanImg->getPixelAddress(xCyan, y) : 0);

                dstPix[3] = 0; // start with transparent
                if (srcRedPix) {
                    PIX srcLuminance = luminance(srcRedPix[0], srcRedPix[1], srcRedPix[2]);
                    dstPix[0] = (PIX)(srcLuminance * (1.f - (float)_amtcolour) + srcRedPix[0] * (float)_amtcolour);
                    dstPix[3] += (PIX)(0.5f * srcRedPix[3]);
                } else {
                    // no src pixel here, be black and transparent
                    dstPix[0] = 0;
                }
                if (srcCyanPix) {
                    PIX srcLuminance = luminance(srcCyanPix[0], srcCyanPix[1], srcCyanPix[2]);
                    dstPix[1] = (PIX)(srcLuminance * (1.f - (float)_amtcolour) + srcCyanPix[1] * (float)_amtcolour);
                    dstPix[2] = (PIX)(srcLuminance * (1.f - (float)_amtcolour) + srcCyanPix[2] * (float)_amtcolour);
                    dstPix[3] += (PIX)(0.5f * srcCyanPix[3]);
                } else {
                    // no src pixel here, be black and transparent
                    dstPix[1] = 0;
                    dstPix[2] = 0;
                }

                // increment the dst pixel
                dstPix += 4;
            }
        }
    } // multiThreadProcessImages

private:
    /** @brief luminance from linear RGB according to Rec.709.
       See http://www.poynton.com/notes/colour_and_gamma/ColorFAQ.html#RTFToC9 */
    static PIX luminance(PIX red,
                         PIX green,
                         PIX blue)
    {
        return PIX(0.2126 * red + 0.7152 * green + 0.0722 * blue);
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AnaglyphPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    AnaglyphPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _amtcolour(NULL)
        , _swap(NULL)
        , _offset(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _amtcolour  = fetchDoubleParam(kParamAmtColour);
        _swap = fetchBooleanParam(kParamSwap);
        _offset = fetchIntParam(kParamOffset);
        assert(_amtcolour && _swap && _offset);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(AnaglyphBase &, const RenderArguments &args);

    virtual void getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    DoubleParam  *_amtcolour;
    BooleanParam *_swap;
    IntParam     *_offset;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
AnaglyphPlugin::setupAndProcess(AnaglyphBase &processor,
                                const RenderArguments &args)
{
    // get a dst image
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( ( dstComponents != _dstClip->getPixelComponents() ) || (dstComponents != ePixelComponentRGBA) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    auto_ptr<const Image> srcLeft( ( _srcClip && _srcClip->isConnected() ) ?
                                        _srcClip->fetchImagePlane(args.time, 0, kFnOfxImagePlaneColour) : 0 );
    if ( srcLeft.get() ) {
        if ( (srcLeft->getRenderScale().x != args.renderScale.x) ||
             ( srcLeft->getRenderScale().y != args.renderScale.y) ||
             ( ( srcLeft->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcLeft->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    auto_ptr<const Image> srcRight( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImagePlane(args.time, 1, kFnOfxImagePlaneColour) : 0 );
    if ( srcRight.get() ) {
        if ( (srcRight->getRenderScale().x != args.renderScale.x) ||
             ( srcRight->getRenderScale().y != args.renderScale.y) ||
             ( ( srcRight->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcRight->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // make sure bit depths are sane
    if ( srcLeft.get() ) {
        BitDepthEnum srcBitDepth      = srcLeft->getPixelDepth();
        PixelComponentEnum srcComponents = srcLeft->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    if ( srcRight.get() ) {
        BitDepthEnum srcBitDepth      = srcRight->getPixelDepth();
        PixelComponentEnum srcComponents = srcRight->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    double amtcolour = _amtcolour->getValueAtTime(args.time);
    bool swap = _swap->getValueAtTime(args.time);
    int offset = _offset->getValueAtTime(args.time);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcLeftImg( srcLeft.get() );
    processor.setSrcRightImg( srcRight.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the parameters
    processor.setAmtColour(amtcolour);
    processor.setSwap(swap);
    processor.setOffset( (int)std::floor(offset * args.renderScale.x + 0.5) );

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // AnaglyphPlugin::setupAndProcess

void
AnaglyphPlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args,
                                    FrameViewsNeededSetter& frameViews)
{
    OfxRangeD range;

    range.min = range.max = args.time;

    frameViews.addFrameViewsNeeded(*_srcClip, range, 0);
    frameViews.addFrameViewsNeeded(*_srcClip, range, 1);
}

// the overridden render function
void
AnaglyphPlugin::render(const RenderArguments &args)
{
    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    assert(dstComponents == ePixelComponentRGBA);

    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ImageAnaglypher<unsigned char, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }

    case eBitDepthUShort: {
        ImageAnaglypher<unsigned short, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }

    case eBitDepthFloat: {
        ImageAnaglypher<float, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

mDeclarePluginFactory(AnaglyphPluginFactory, {ofxsThreadSuiteCheck();}, {});
#if 0
void
AnaglyphPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}
#endif

void
AnaglyphPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // returning an error here crashes Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}

    //We're using the view calls (i.e: getFrameViewsNeeded)
    desc.setIsViewAware(true);

    //We render the same thing on all views
    desc.setIsViewInvariant(eViewInvarianceAllViewsInvariant);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
AnaglyphPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum /*context*/)
{
    if ( !fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
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
        param->setLabel(kParamAmtColourLabel);
        param->setHint(kParamAmtColourHint);
        param->setDefault(0.);
        param->setRange(0., 1.);
        param->setIncrement(0.01);
        param->setDisplayRange(0., 1.);
        param->setDoubleType(eDoubleTypeScale);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamSwap);
        param->setLabel(kParamSwapLabel);
        param->setDefault(false);
        param->setHint(kParamSwapHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDefault(0);
        param->setRange(-1000, 1000);
        param->setDisplayRange(-100, 100);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // AnaglyphPluginFactory::describeInContext

ImageEffect*
AnaglyphPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new AnaglyphPlugin(handle);
}

static AnaglyphPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
