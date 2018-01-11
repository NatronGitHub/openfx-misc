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
 * OFX VectorToColor plugin.
 */

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsLut.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "VectorToColorOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Convert x and y vector components to a color representation.\n" \
    "H (hue) gives the direction, S (saturation) is set to the amplitude/norm, and V is 1." \
    "The role of S and V can be switched." \
    "Output can be RGB or HSV, with H in degrees."
#define kPluginIdentifier "net.sf.openfx.VectorToColorPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamXChannel "xChannel"
#define kParamXChannelLabel "X channel"
#define kParamXChannelHint "Selects the X component of vectors"

#define kParamYChannel "yChannel"
#define kParamYChannelLabel "Y channel"
#define kParamYChannelHint "Selects the Y component of vectors"

#define kParamChannelOptionR "r", "R channel from input.", "r"
#define kParamChannelOptionG "g", "G channel from input.", "g"
#define kParamChannelOptionB "b", "B channel from input.", "b"
#define kParamChannelOptionA "a", "A channel from input.", "a"

enum InputChannelEnum
{
    eInputChannelR = 0,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
};

#define kParamOpposite "opposite"
#define kParamOppositeLabel "Opposite"
#define kParamOppositeHint "If checked, opposite of X and Y are used."

#define kParamInverseY "inverseY"
#define kParamInverseYLabel "Inverse Y"
#define kParamInverseYHint "If checked, opposite of Y is used (on by default, because most optical flow results are shown using a downward Y axis)."

#define kParamModulateV "modulateV"
#define kParamModulateVLabel "Modulate V"
#define kParamModulateVHint "If checked, modulate V using the vector amplitude, instead of S."

#define kParamHSVOutput "hsvOutput"
#define kParamHSVOutputLabel "HSV Output"
#define kParamHSVOutputHint "If checked, output is in the HSV color model."


class VectorToColorProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    InputChannelEnum _xChannel;
    InputChannelEnum _yChannel;
    bool _opposite;
    bool _inverseY;
    bool _modulateV;
    bool _hsvOutput;

public:

    VectorToColorProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _xChannel(eInputChannelR)
        , _yChannel(eInputChannelG)
        , _opposite(false)
        , _inverseY(false)
        , _modulateV(false)
        , _hsvOutput(false)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(InputChannelEnum xChannel,
                   InputChannelEnum yChannel,
                   bool opposite,
                   bool inverseY,
                   bool modulateV,
                   bool hsvOutput)
    {
        _xChannel = xChannel;
        _yChannel = yChannel;
        _opposite = opposite;
        _inverseY = inverseY;
        _modulateV = modulateV;
        _hsvOutput = hsvOutput;
    }

private:
};


template <class PIX, int nComponents>
static void
pixToVector(const PIX *p,
            float v[2],
            InputChannelEnum xChannel,
            InputChannelEnum yChannel)
{
    assert(nComponents == 3 || nComponents == 4);
    if (!p) {
        v[0] = v[1] = 0.f;

        return;
    }
    switch (xChannel) {
    case eInputChannelR:
        v[0] = p[0];
        break;
    case eInputChannelG:
        v[0] = p[1];
        break;
    case eInputChannelB:
        v[0] = p[2];
        break;
    case eInputChannelA:
        v[0] = (nComponents == 4) ? p[3] : 0.f;
        break;
    }
    switch (yChannel) {
    case eInputChannelR:
        v[1] = p[0];
        break;
    case eInputChannelG:
        v[1] = p[1];
        break;
    case eInputChannelB:
        v[1] = p[2];
        break;
    case eInputChannelA:
        v[1] = (nComponents == 4) ? p[3] : 0.f;
        break;
    }
}

template <class PIX, int nComponents, int maxValue>
class VectorToColorProcessor
    : public VectorToColorProcessorBase
{
public:
    VectorToColorProcessor(ImageEffect &instance)
        : VectorToColorProcessorBase(instance)
    {
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float vec[2];
        float h, s = 1.f, v = 1.f;
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                pixToVector<PIX, nComponents>(srcPix, vec, _xChannel, _yChannel);
                h = (float)( std::atan2(_inverseY ? -vec[1] : vec[1], vec[0]) * OFXS_HUE_CIRCLE / (2 * M_PI) );
                if (_opposite) {
                    h += OFXS_HUE_CIRCLE / 2.;
                }
                float norm = std::sqrt( std::max( (double)vec[0] * vec[0] + (double)vec[1] * vec[1], 0. ) );
                if (_modulateV) {
                    v = norm;
                } else {
                    s = norm;
                }
                if (_hsvOutput) {
                    dstPix[0] = h;
                    dstPix[1] = s;
                    dstPix[2] = v;
                } else {
                    Color::hsv_to_rgb(h, s, v, &dstPix[0], &dstPix[1], &dstPix[2]);
                }
                if (nComponents == 4) {
                    dstPix[3] = 1.f;
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class VectorToColorPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    VectorToColorPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _xChannel = fetchChoiceParam(kParamXChannel);
        _yChannel = fetchChoiceParam(kParamYChannel);
        _opposite = fetchBooleanParam(kParamOpposite);
        _inverseY = fetchBooleanParam(kParamInverseY);
        _modulateV = fetchBooleanParam(kParamModulateV);
        _hsvOutput = fetchBooleanParam(kParamHSVOutput);
        assert(_xChannel && _yChannel && _opposite && _modulateV && _hsvOutput);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(VectorToColorProcessorBase &, const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    ChoiceParam* _xChannel;
    ChoiceParam* _yChannel;
    BooleanParam* _opposite;
    BooleanParam* _inverseY;
    BooleanParam* _modulateV;
    BooleanParam* _hsvOutput;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
VectorToColorPlugin::setupAndProcess(VectorToColorProcessorBase &processor,
                                     const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
                                    _srcClip->fetchImage(time) : 0 );
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

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    InputChannelEnum xChannel = (InputChannelEnum)_xChannel->getValueAtTime(time);
    InputChannelEnum yChannel = (InputChannelEnum)_yChannel->getValueAtTime(time);
    bool opposite = _opposite->getValueAtTime(time);
    bool inverseY = _inverseY->getValueAtTime(time);
    bool modulateV = _modulateV->getValueAtTime(time);
    bool hsvOutput = _hsvOutput->getValueAtTime(time);
    processor.setValues(xChannel, yChannel, opposite, inverseY, modulateV, hsvOutput);
    processor.process();
} // VectorToColorPlugin::setupAndProcess

// the overridden render function
void
VectorToColorPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthFloat: {
            VectorToColorProcessor<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == ePixelComponentRGB);
        switch (dstBitDepth) {
        case eBitDepthFloat: {
            VectorToColorProcessor<float, 3, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

mDeclarePluginFactory(VectorToColorPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
VectorToColorPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
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
}

static void
addInputChannelOtions(ChoiceParamDescriptor* outputR,
                      InputChannelEnum def,
                      ContextEnum /*context*/)
{
    assert(outputR->getNOptions() == eInputChannelR);
    outputR->appendOption(kParamChannelOptionR);
    assert(outputR->getNOptions() == eInputChannelG);
    outputR->appendOption(kParamChannelOptionG);
    assert(outputR->getNOptions() == eInputChannelB);
    outputR->appendOption(kParamChannelOptionB);
    assert(outputR->getNOptions() == eInputChannelA);
    outputR->appendOption(kParamChannelOptionA);
    outputR->setDefault(def);
}

void
VectorToColorPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                              ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // xChannel
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamXChannel);
        param->setLabel(kParamXChannelLabel);
        param->setHint(kParamXChannelHint);
        addInputChannelOtions(param, eInputChannelR, context);
        if (page) {
            page->addChild(*param);
        }
    }

    // yChannel
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamYChannel);
        param->setLabel(kParamYChannelLabel);
        param->setHint(kParamYChannelHint);
        addInputChannelOtions(param, eInputChannelG, context);
        if (page) {
            page->addChild(*param);
        }
    }

    // opposite
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOpposite);
        param->setLabel(kParamOppositeLabel);
        param->setHint(kParamOppositeHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // inverseY
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamInverseY);
        param->setLabel(kParamInverseYLabel);
        param->setHint(kParamInverseYHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // modulateV
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamModulateV);
        param->setLabel(kParamModulateVLabel);
        param->setHint(kParamModulateVHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // hsvOutput
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamHSVOutput);
        param->setLabel(kParamHSVOutputLabel);
        param->setHint(kParamHSVOutputHint);
        if (page) {
            page->addChild(*param);
        }
    }
} // VectorToColorPluginFactory::describeInContext

ImageEffect*
VectorToColorPluginFactory::createInstance(OfxImageEffectHandle handle,
                                           ContextEnum /*context*/)
{
    return new VectorToColorPlugin(handle);
}

static VectorToColorPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
