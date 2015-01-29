/*
 OFX ColorTransform plugin.
 
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

#include "ColorTransform.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"

#define kPluginRGBToHSVName "RGBToHSVOFX"
#define kPluginRGBToHSVDescription "Convert from RGB to HSV color model (as defined by A. R. Smith in 1978). H is in degrees, S and V are in the same units as RGB."
#define kPluginRGBToHSVIdentifier "net.sf.openfx.RGBToHSVPlugin"

#define kPluginHSVToRGBName "HSVToRGBOFX"
#define kPluginHSVToRGBDescription "Convert from HSV color model (as defined by A. R. Smith in 1978) to RGB. H is in degrees, S and V are in the same units as RGB."
#define kPluginHSVToRGBIdentifier "net.sf.openfx.HSVToRGBPlugin"

#define kPluginRGBToHSLName "RGBToHSLOFX"
#define kPluginRGBToHSLDescription "Convert from RGB to HSL color model (as defined by Joblove and Greenberg in 1978). H is in degrees, S and L are in the same units as RGB."
#define kPluginRGBToHSLIdentifier "net.sf.openfx.RGBToHSLPlugin"

#define kPluginHSLToRGBName "HSLToRGBOFX"
#define kPluginHSLToRGBDescription "Convert from HSL color model (as defined by Joblove and Greenberg in 1978) to RGB. H is in degrees, S and L are in the same units as RGB."
#define kPluginHSLToRGBIdentifier "net.sf.openfx.HSLToRGBPlugin"


#define kPluginRGBToXYZName "RGBToXYZOFX"
#define kPluginRGBToXYZDescription "Convert from RGB to XYZ color model (Rec.709 with D65 illuminant). X, Y and Z are in the same units as RGB."
#define kPluginRGBToXYZIdentifier "net.sf.openfx.RGBToXYZPlugin"

#define kPluginXYZToRGBName "XYZToRGBOFX"
#define kPluginXYZToRGBDescription "Convert from XYZ color model (Rec.709 with D65 illuminant) to RGB. X, Y and Z are in the same units as RGB."
#define kPluginXYZToRGBIdentifier "net.sf.openfx.XYZToRGBPlugin"

#define kPluginRGBToLabName "RGBToLabOFX"
#define kPluginRGBToLabDescription "Convert from RGB to Lab color model (Rec.709 with D65 illuminant)."
#define kPluginRGBToLabIdentifier "net.sf.openfx.RGBToLabPlugin"

#define kPluginLabToRGBName "LabToRGBOFX"
#define kPluginLabToRGBDescription "Convert from Lab color model (Rec.709 with D65 illuminant) to RGB.$"
#define kPluginLabToRGBIdentifier "net.sf.openfx.LabToRGBPlugin"

#define kPluginGrouping "Color/Transform"

#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamPremultRGBToXXXLabel "Unpremult"
#define kParamPremultRGBToXXXHint \
"Divide the image by the alpha channel before processing. " \
"Use if the input images are premultiplied."

#define kParamPremultXXXToRGBLabel "Premult"
#define kParamPremultXXXToRGBHint \
"Multiply the image by the alpha channel after processing. " \
"Use to get premultiplied output images."

using namespace OFX;

enum ColorTransformEnum {
    eColorTransformRGBToHSV,
    eColorTransformHSVToRGB,
    eColorTransformRGBToHSL,
    eColorTransformHSLToRGB,
    eColorTransformRGBToXYZ,
    eColorTransformXYZToRGB,
    eColorTransformRGBToLab,
    eColorTransformLabToRGB
};

#define toRGB(e)   ((e) == eColorTransformHSVToRGB || \
                    (e) == eColorTransformHSLToRGB || \
                    (e) == eColorTransformXYZToRGB || \
                    (e) == eColorTransformLabToRGB)

#define fromRGB(e) (!toRGB(e))

class ColorTransformProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    bool _premult;
    int _premultChannel;
    double _mix;

public:
    
    ColorTransformProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _premult(false)
    , _premultChannel(3)
    , _mix(1.)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
        
    void setValues(bool premult,
                   int premultChannel)
    {
        _premult = premult;
        _premultChannel = premultChannel;
    }


private:
};



template <class PIX, int nComponents, int maxValue, ColorTransformEnum transform>
class ColorTransformProcessor : public ColorTransformProcessorBase
{
public:
    ColorTransformProcessor(OFX::ImageEffect &instance)
    : ColorTransformProcessorBase(instance)
    {
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];
        const bool dounpremult = _premult && fromRGB(transform);
        const bool dopremult = _premult && toRGB(transform);
        
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, dounpremult, _premultChannel);
                switch (transform) {
                    case eColorTransformRGBToHSV:
                        OFX::Color::rgb_to_hsv(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformHSVToRGB:
                        OFX::Color::hsv_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformRGBToHSL:
                        OFX::Color::rgb_to_hsl(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformHSLToRGB:
                        OFX::Color::hsl_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformRGBToXYZ:
                        OFX::Color::rgb_to_xyz_rec709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformXYZToRGB:
                        OFX::Color::xyz_rec709_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformRGBToLab:
                        OFX::Color::rgb_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformLabToRGB:
                        OFX::Color::lab_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                }
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, dopremult, _premultChannel, x, y, srcPix, /*doMasking=*/false, /*maskImg=*/NULL, /*mix=*/1., /*maskInvert=*/false, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }

   }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template <ColorTransformEnum transform>
class ColorTransformPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ColorTransformPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(ColorTransformProcessorBase &, const OFX::RenderArguments &args);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::setupAndProcess(ColorTransformProcessorBase &processor, const OFX::RenderArguments &args)
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
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
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
    
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    
    processor.setValues(premult, premultChannel);
    processor.process();
}

// the overridden render function
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ColorTransformProcessor<unsigned char, 4, 255, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorTransformProcessor<unsigned short, 4, 65535, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorTransformProcessor<float, 4, 1, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ColorTransformProcessor<unsigned char, 3, 255, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorTransformProcessor<unsigned short, 3, 65535, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorTransformProcessor<float, 3, 1, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

/* Override the clip preferences */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{

    if (srcClip_->getPixelComponents() == ePixelComponentRGBA) {
        bool premult;
        _premult->getValue(premult);
        // set the premultiplication of dstClip_
        if (fromRGB(transform)) {
            // HSV is always unpremultiplied
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        } else {
            // RGB
            clipPreferences.setOutputPremultiplication(premult ? eImagePreMultiplied : eImageUnPreMultiplied);
        }
    }
}

template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        switch (srcClip_->getPreMultiplication()) {
            case eImageOpaque:
                _premult->setValue(false);
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
        }
    }
}

template <ColorTransformEnum transform>
class ColorTransformPluginFactory : public OFX::PluginFactoryHelper<ColorTransformPluginFactory<transform> >
{
public:
    ColorTransformPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<ColorTransformPluginFactory<transform> >(id, verMaj, verMin){}

    //virtual void load() OVERRIDE FINAL {};
    //virtual void unload() OVERRIDE FINAL {};
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    switch (transform) {
        case eColorTransformRGBToHSV:
            desc.setLabels(kPluginRGBToHSVName, kPluginRGBToHSVName, kPluginRGBToHSVName);
            desc.setPluginDescription(kPluginRGBToHSVDescription);
            break;

        case eColorTransformHSVToRGB:
            desc.setLabels(kPluginHSVToRGBName, kPluginHSVToRGBName, kPluginHSVToRGBName);
            desc.setPluginDescription(kPluginHSVToRGBDescription);
            break;

        case eColorTransformRGBToHSL:
            desc.setLabels(kPluginRGBToHSLName, kPluginRGBToHSLName, kPluginRGBToHSLName);
            desc.setPluginDescription(kPluginRGBToHSLDescription);
            break;

        case eColorTransformHSLToRGB:
            desc.setLabels(kPluginHSLToRGBName, kPluginHSLToRGBName, kPluginHSLToRGBName);
            desc.setPluginDescription(kPluginHSLToRGBDescription);
            break;

        case eColorTransformRGBToXYZ:
            desc.setLabels(kPluginRGBToXYZName, kPluginRGBToXYZName, kPluginRGBToXYZName);
            desc.setPluginDescription(kPluginRGBToXYZDescription);
            break;

        case eColorTransformXYZToRGB:
            desc.setLabels(kPluginXYZToRGBName, kPluginXYZToRGBName, kPluginXYZToRGBName);
            desc.setPluginDescription(kPluginXYZToRGBDescription);
            break;

        case eColorTransformRGBToLab:
            desc.setLabels(kPluginRGBToLabName, kPluginRGBToLabName, kPluginRGBToLabName);
            desc.setPluginDescription(kPluginRGBToLabDescription);
            break;

        case eColorTransformLabToRGB:
            desc.setLabels(kPluginLabToRGBName, kPluginLabToRGBName, kPluginLabToRGBName);
            desc.setPluginDescription(kPluginLabToRGBDescription);
            break;
    }
    desc.setPluginGrouping(kPluginGrouping);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
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

}

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           OFX::ContextEnum /*context*/)
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
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremult);
        if (fromRGB(transform)) {
            param->setLabels(kParamPremultRGBToXXXLabel, kParamPremultRGBToXXXLabel, kParamPremultRGBToXXXLabel);
            param->setHint(kParamPremultRGBToXXXHint);
        } else {
            param->setLabels(kParamPremultXXXToRGBLabel, kParamPremultXXXToRGBLabel, kParamPremultXXXToRGBLabel);
            param->setHint(kParamPremultXXXToRGBHint);
        }
        param->setLayoutHint(eLayoutHintNoNewLine);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }
    {
        // not yet implemented, for future use (whenever deep compositing is supported)
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPremultChannel);
        param->setLabels(kParamPremultChannelLabel, kParamPremultChannelLabel, kParamPremultChannelLabel);
        param->setHint(kParamPremultChannelHint);
        param->appendOption(kParamPremultChannelR, kParamPremultChannelRHint);
        param->appendOption(kParamPremultChannelG, kParamPremultChannelGHint);
        param->appendOption(kParamPremultChannelB, kParamPremultChannelBHint);
        param->appendOption(kParamPremultChannelA, kParamPremultChannelAHint);
        param->setDefault(3); // alpha
        param->setIsSecret(true); // not yet implemented
        page->addChild(*param);
    }
}

template <ColorTransformEnum transform>
OFX::ImageEffect*
ColorTransformPluginFactory<transform>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ColorTransformPlugin<transform>(handle);
}

void getColorTransformPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        // RGBtoHSV
        static ColorTransformPluginFactory<eColorTransformRGBToHSV> p(kPluginRGBToHSVIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // HSVtoRGB
        static ColorTransformPluginFactory<eColorTransformHSVToRGB> p(kPluginHSVToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // RGBtoHSL
        static ColorTransformPluginFactory<eColorTransformRGBToHSL> p(kPluginRGBToHSLIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // HSLtoRGB
        static ColorTransformPluginFactory<eColorTransformHSLToRGB> p(kPluginHSLToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // RGBtoXYZ
        static ColorTransformPluginFactory<eColorTransformRGBToXYZ> p(kPluginRGBToXYZIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // XYZtoRGB
        static ColorTransformPluginFactory<eColorTransformXYZToRGB> p(kPluginXYZToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // RGBtoLab
        static ColorTransformPluginFactory<eColorTransformRGBToLab> p(kPluginRGBToLabIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        // LabtoRGB
        static ColorTransformPluginFactory<eColorTransformLabToRGB> p(kPluginLabToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}

