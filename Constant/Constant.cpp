/*
 OFX Constant plugin.

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

#include "Constant.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif
#include <climits>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"

#define kPluginName "ConstantOFX"
#define kPluginGrouping "Image"
#define kPluginDescription "Generate an image with a constant color. A frame range may be specified for operators that need it."
#define kPluginIdentifier "net.sf.openfx.ConstantPlugin"
#define kPluginSolidName "SolidOFX"
#define kPluginSolidDescription "Generate an image with a constant opaque color. A frame range may be specified for operators that need it."
#define kPluginSolidIdentifier "net.sf.openfx.Solid"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamColor "color"
#define kParamColorLabel "Color"
#define kParamColorHint "Color to fill the image with."

#define kParamRange "frameRange"
#define kParamRangeLabel "Frame Range"
#define kParamRangeHint "Time domain."

#include "ofxNatron.h"

static bool gHostIsNatron   = false;

/** @brief  Base class used to blend two images together */
class ConstantProcessorBase : public OFX::ImageProcessor {
protected:
    OfxRGBAColourD _color;

public:
    /** @brief no arg ctor */
    ConstantProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    {
        _color.r = _color.g = _color.b = _color.a = 0.;
    }

    /** @brief set the color */
    void setColor(const OfxRGBAColourD &color)
    {
        _color = color;
    }
};

/** @brief templated class to blend between two images */
template <class PIX, int nComponents, int max>
class ConstantProcessor : public ConstantProcessorBase
{
public:
    // ctor
    ConstantProcessor(OFX::ImageEffect &instance)
    : ConstantProcessorBase(instance)
    {
    }

private:
    static void
    colorToPIX(const OfxRGBAColourD& color, PIX colorPix[nComponents])
    {
        float colorf[4];
        if (nComponents == 1) {
            // alpha
            colorf[0] = (float)color.a;
        } else if (nComponents == 2) {
            // xy
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
        } else if (nComponents == 3) {
            // rgb
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
            colorf[2] = (float)color.b;
        } else {
            assert(nComponents == 4);
            // rgba
            colorf[0] = (float)color.r;
            colorf[1] = (float)color.g;
            colorf[2] = (float)color.b;
            colorf[3] = (float)color.a;
        }


        if (max == 1) { // implies float, don't clamp
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = colorf[c];
            }
        } else {
            // color is supposed to be linear: delinearize first
            if (nComponents == 3 || nComponents == 4) {
                // don't delinearize alpha: it is always linear
                for (int c = 0; c < 3; ++c) {
                    if (max == 255) {
                        colorf[c] = OFX::Color::to_func_srgb(colorf[c]);
                    } else {
                        assert(max == 65535);
                        colorf[c] = OFX::Color::to_func_Rec709(colorf[c]);
                    }
                }
            }
            // clamp and convert to the destination type
            for (int c = 0; c < nComponents; ++c) {
                colorPix[c] = OFX::Color::floatToInt<max+1>(colorf[c]);
            }
        }
    }

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        PIX color[nComponents];
        colorToPIX(_color, color);

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                for (int c = 0; c < nComponents; ++c) {
                    dstPix[c] = color[c];
                }
                dstPix += nComponents;
            }
        }
    }

};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ConstantPlugin : public GeneratorPlugin
{
public:
    /** @brief ctor */
    ConstantPlugin(OfxImageEffectHandle handle, bool solid)
    : GeneratorPlugin(handle, true)
    , _color(0)
    , _colorRGB(0)
    , _range(0)
    {
        if (solid) {
            _colorRGB   = fetchRGBParam(kParamColor);
        } else {
            _color   = fetchRGBAParam(kParamColor);
        }
        _range   = fetchInt2DParam(kParamRange);
        assert((_color || _colorRGB) && _range);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ConstantProcessorBase &, const OFX::RenderArguments &args);
    
    virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                               OFX::Clip * &identityClip,
                               double &identityTime) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    OFX::RGBAParam  *_color;
    OFX::RGBParam *_colorRGB;
    OFX::Int2DParam  *_range;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ConstantPlugin::setupAndProcess(ConstantProcessorBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image>  dst(_dstClip->fetchImage(args.time));
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

    // set the images
    processor.setDstImg(dst.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    OfxRGBAColourD color;
    if (_colorRGB) {
        _colorRGB->getValueAtTime(args.time, color.r, color.g, color.b);
        color.a = 1.;
    } else {
        _color->getValueAtTime(args.time, color.r, color.g, color.b, color.a);
    }

    processor.setColor(color);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the internal render function
template <int nComponents>
void
ConstantPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            ConstantProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort: {
            ConstantProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            ConstantProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ConstantPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

/* override the time domain action, only for the general context */
bool
ConstantPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == OFX::eContextGeneral) {
        // how many frames on the input clip
        //OfxRangeD srcRange = _srcClip->getFrameRange();

        int min, max;
        _range->getValue(min, max);
        range.min = min;
        range.max = max;
        return true;
    }

    return false;
}

bool
ConstantPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                OFX::Clip * &identityClip,
                                double &identityTime)
{
    
    if (gHostIsNatron) {
        
        // only Natron supports setting the identityClip to the output clip
        
        int min, max;
        _range->getValue(min, max);
        
        int type_i;
        _type->getValue(type_i);
        GeneratorTypeEnum type = (GeneratorTypeEnum)type_i;
        bool paramsNotAnimated = ((_color && _color->getNumKeys() == 0) ||
                                  (_colorRGB && _colorRGB->getNumKeys() == 0));
        if (type == eGeneratorTypeSize) {
            ///If not animated and different than 'min' time, return identity on the min time.
            ///We need to check more parameters
            if (paramsNotAnimated && _size->getNumKeys() == 0 && _btmLeft->getNumKeys() == 0 && args.time != min) {
                identityClip = _dstClip;
                identityTime = min;
                return true;
            }
        } else {
            ///If not animated and different than 'min' time, return identity on the min time.
            if (paramsNotAnimated && args.time != min) {
                identityClip = _dstClip;
                identityTime = min;
                return true;
            }
        }
        
        
       
    }
    return false;
}

void
ConstantPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    GeneratorPlugin::getClipPreferences(clipPreferences);
    clipPreferences.setOutputPremultiplication(_colorRGB ? OFX::eImageOpaque : OFX::eImagePreMultiplied);
}

using namespace OFX;

template<bool solid>
class ConstantPluginFactory : public OFX::PluginFactoryHelper<ConstantPluginFactory<solid> >
{
public:
    ConstantPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<ConstantPluginFactory>(id, verMaj, verMin){}
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

template<bool solid>
void ConstantPluginFactory<solid>::describe(OFX::ImageEffectDescriptor &desc)
{
    if (solid) {
        desc.setLabel(kPluginSolidName);
        desc.setPluginDescription(kPluginSolidDescription);
    } else {
        desc.setLabel(kPluginName);
        desc.setPluginDescription(kPluginDescription);
    }
    desc.setPluginGrouping(kPluginGrouping);
    desc.addSupportedContext(eContextGenerator);
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
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(solid ? ePixelComponentRGB : ePixelComponentRGBA);
#endif

    generatorDescribe(desc);
}

template<bool solid>
void ConstantPluginFactory<solid>::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    gHostIsNatron = (OFX::getImageEffectHostDescription()->hostName == kNatronOfxHostName);
    
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    generatorDescribeInContext(page, desc, *dstClip, eGeneratorTypeDefault, true, context);

    // color
    if (solid) {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamColor);
        param->setLabel(kParamColorLabel);
        param->setHint(kParamColorHint);
        param->setDefault(0.0, 0.0, 0.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    } else {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor);
        param->setLabel(kParamColorLabel);
        param->setHint(kParamColorHint);
        param->setDefault(0.0, 0.0, 0.0, 0.0);
        param->setRange(INT_MIN, INT_MIN, INT_MIN, INT_MIN, INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 0, 0, 1, 1, 1, 1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // range
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamRange);
        param->setLabel(kParamRangeLabel);
        param->setHint(kParamRangeHint);
        param->setDefault(1, 1);
        param->setDimensionLabels("min", "max");
        param->setAnimates(false); // can not animate, because it defines the time domain
        if (page) {
            page->addChild(*param);
        }
    }
}

template<bool solid>
ImageEffect*
ConstantPluginFactory<solid>::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new ConstantPlugin(handle, solid);
}

void getConstantPluginID(OFX::PluginFactoryArray &ids)
{
    {
        static ConstantPluginFactory<false> p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static ConstantPluginFactory<true> p(kPluginSolidIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
