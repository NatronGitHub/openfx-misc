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
 * OFX MatteMonitor plugin.
 */

#include <cmath>
#include <cstring>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "MatteMonitor"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
    "A Matte Monitor: make alpha values that are strictly between 0 and 1 more visible.\n" \
    "After applying a Keyer, a scaling operation is usually applied to clean the matte. However, it is difficult to visualize on the output values that are very close to 0 or 1, but not equal. This plugin can be used to better visualize these values: connect it to the output of the scaling operator, then to a viewer, and visualize the alpha channel.\n" \
    "Alpha values lower or equal to 0 and greater or equal to 1 are leaved untouched, and alpha values in between are stretched " \
    "towards 0.5 (using the slope parameter), making them more visible.\n" \
    "The output of this plugin should not be used for firther processing, but only for viewing.\n" \
    "The Matte Monitor is described in \"Digital Compositing for Film and Video\" by Steve Wright (Sec. 3.1).\n" \
    "See also the video at http://www.vfxio.com/images/movies/Comp_Tip_2.mov\n"

#define kPluginIdentifier "net.sf.openfx.MatteMonitorPlugin"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamSlope      "slope"
#define kParamSlopeLabel "Slope"
#define kParamSlopeHint  "Slope applied to alpha values striuctly between 0 and 1."


class MatteMonitorProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    double _slope;

public:

    MatteMonitorProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _slope(0.5)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(double slope)
    {
        _slope = slope;
    }

private:
};


template <class PIX, int nComponents, int maxValue>
class MatteMonitorProcessor
    : public MatteMonitorProcessorBase
{
public:
    MatteMonitorProcessor(ImageEffect &instance)
        : MatteMonitorProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 4);
        assert(_dstImg);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                PIX alpha = srcPix ? (nComponents == 1 ? srcPix[0] : srcPix[3]) : PIX();

                if ( (alpha > 0) && (alpha < maxValue) ) {
                    alpha = maxValue / 2. + (alpha - maxValue / 2.) * _slope;
                }
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    dstPix[0] = alpha;
                } else if (nComponents == 4) {
                    dstPix[0] = srcPix ? srcPix[0] : PIX();
                    dstPix[1] = srcPix ? srcPix[1] : PIX();
                    dstPix[2] = srcPix ? srcPix[2] : PIX();
                    dstPix[3] = alpha;
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MatteMonitorPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    MatteMonitorPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha) );
        _slope = fetchDoubleParam(kParamSlope);
        assert(_slope);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(MatteMonitorProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    DoubleParam* _slope;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
MatteMonitorPlugin::setupAndProcess(MatteMonitorProcessorBase &processor,
                                    const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    double slope;
    _slope->getValueAtTime(args.time, slope);
    processor.setValues(slope);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
MatteMonitorPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            MatteMonitorProcessor<unsigned char, 4, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            MatteMonitorProcessor<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            MatteMonitorProcessor<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == ePixelComponentAlpha) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            MatteMonitorProcessor<unsigned char, 1, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            MatteMonitorProcessor<unsigned short, 1, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            MatteMonitorProcessor<float, 1, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // MatteMonitorPlugin::render

bool
MatteMonitorPlugin::isIdentity(const IsIdentityArguments &args,
                               Clip * &identityClip,
                               double & /*identityTime*/
                               , int& /*view*/, std::string& /*plane*/)
{
    double slope;

    _slope->getValueAtTime(args.time, slope);

    if (slope == 1.) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

mDeclarePluginFactory(MatteMonitorPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
MatteMonitorPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

void
MatteMonitorPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                             ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSlope);
        param->setLabel(kParamSlopeLabel);
        param->setHint(kParamSlopeHint);
        param->setDefault(0.5);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
MatteMonitorPluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    return new MatteMonitorPlugin(handle);
}

static MatteMonitorPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
