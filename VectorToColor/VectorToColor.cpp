/*
 OFX VectorToColor plugin.
 
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

#include "VectorToColor.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsLut.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

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

#define kParamChannelOptionR "r"
#define kParamChannelOptionRHint "R channel from input"
#define kParamChannelOptionG "g"
#define kParamChannelOptionGHint "G channel from input"
#define kParamChannelOptionB "b"
#define kParamChannelOptionBHint "B channel from input"
#define kParamChannelOptionA "a"
#define kParamChannelOptionAHint "A channel from input"

enum InputChannelEnum {
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

using namespace OFX;


class VectorToColorProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    InputChannelEnum _xChannel;
    InputChannelEnum _yChannel;
    bool _opposite;
    bool _inverseY;
    bool _modulateV;
    bool _hsvOutput;

 public:
    
    VectorToColorProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _xChannel(eInputChannelR)
    , _yChannel(eInputChannelG)
    , _opposite(false)
    , _inverseY(false)
    , _modulateV(false)
    , _hsvOutput(false)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
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
pixToVector(const PIX *p, float v[2], InputChannelEnum xChannel, InputChannelEnum yChannel)
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
class VectorToColorProcessor : public VectorToColorProcessorBase
{
public:
    VectorToColorProcessor(OFX::ImageEffect &instance)
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
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                pixToVector<PIX, nComponents>(srcPix, vec, _xChannel, _yChannel);
                h = (float)(std::atan2(_inverseY?-vec[1]:vec[1], vec[0]) * 180. / M_PI);
                if (_opposite) {
                    h += 180;
                }
                float norm = std::sqrt(vec[0] * vec[0] + vec[1] *  vec[1]);
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
                    OFX::Color::hsv_to_rgb(h, s, v, &dstPix[0], &dstPix[1], &dstPix[2]);
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
class VectorToColorPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    VectorToColorPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));

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
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(VectorToColorProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::ChoiceParam* _xChannel;
    OFX::ChoiceParam* _yChannel;
    OFX::BooleanParam* _opposite;
    OFX::BooleanParam* _inverseY;
    OFX::BooleanParam* _modulateV;
    OFX::BooleanParam* _hsvOutput;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
VectorToColorPlugin::setupAndProcess(VectorToColorProcessorBase &processor, const OFX::RenderArguments &args)
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
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    int xChannel_i, yChannel_i;
    _xChannel->getValueAtTime(args.time, xChannel_i);
    _yChannel->getValueAtTime(args.time, yChannel_i);
    InputChannelEnum xChannel = (InputChannelEnum)xChannel_i;
    InputChannelEnum yChannel = (InputChannelEnum)yChannel_i;
    bool opposite;
    _opposite->getValueAtTime(args.time, opposite);
    bool inverseY;
    _inverseY->getValueAtTime(args.time, inverseY);
    bool modulateV;
    _modulateV->getValueAtTime(args.time, modulateV);
    bool hsvOutput;
    _hsvOutput->getValueAtTime(args.time, hsvOutput);
    processor.setValues(xChannel, yChannel, opposite, inverseY, modulateV, hsvOutput);
    processor.process();
}

// the overridden render function
void
VectorToColorPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthFloat: {
                VectorToColorProcessor<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthFloat: {
                VectorToColorProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

mDeclarePluginFactory(VectorToColorPluginFactory, {}, {});

void
VectorToColorPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
addInputChannelOtions(ChoiceParamDescriptor* outputR, InputChannelEnum def, OFX::ContextEnum /*context*/)
{
    assert(outputR->getNOptions() == eInputChannelR);
    outputR->appendOption(kParamChannelOptionR,kParamChannelOptionRHint);
    assert(outputR->getNOptions() == eInputChannelG);
    outputR->appendOption(kParamChannelOptionG,kParamChannelOptionGHint);
    assert(outputR->getNOptions() == eInputChannelB);
    outputR->appendOption(kParamChannelOptionB,kParamChannelOptionBHint);
    assert(outputR->getNOptions() == eInputChannelA);
    outputR->appendOption(kParamChannelOptionA,kParamChannelOptionAHint);
    outputR->setDefault(def);
}

void
VectorToColorPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
        page->addChild(*param);
    }

    // yChannel
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamYChannel);
        param->setLabel(kParamYChannelLabel);
        param->setHint(kParamYChannelHint);
        addInputChannelOtions(param, eInputChannelG, context);
        page->addChild(*param);
    }

    // opposite
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOpposite);
        param->setLabel(kParamOppositeLabel);
        param->setHint(kParamOppositeHint);
        page->addChild(*param);
    }

    // inverseY
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamInverseY);
        param->setLabel(kParamInverseYLabel);
        param->setHint(kParamInverseYHint);
        param->setDefault(true);
        page->addChild(*param);
    }

    // modulateV
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamModulateV);
        param->setLabel(kParamModulateVLabel);
        param->setHint(kParamModulateVHint);
        page->addChild(*param);
    }

    // hsvOutput
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamHSVOutput);
        param->setLabel(kParamHSVOutputLabel);
        param->setHint(kParamHSVOutputHint);
        page->addChild(*param);
    }
}

OFX::ImageEffect*
VectorToColorPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new VectorToColorPlugin(handle);
}

void getVectorToColorPluginID(OFX::PluginFactoryArray &ids)
{
    static VectorToColorPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

