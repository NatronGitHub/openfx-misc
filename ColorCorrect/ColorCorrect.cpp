/*
 OFX ColorCorrect plugin.
 
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

#include "ColorCorrect.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"


////std strings because we need them in changedParam
static const std::string kColorCorrectMasterGroupName = std::string("Master");
static const std::string kColorCorrectShadowsGroupName = std::string("Shadows");
static const std::string kColorCorrectMidtonesGroupName = std::string("Midtones");
static const std::string kColorCorrectHighlightsGroupName = std::string("Highlights");

static const std::string kColorCorrectSaturationName = std::string("Saturation");
static const std::string kColorCorrectContrastName = std::string("Contrast");
static const std::string kColorCorrectGammaName = std::string("Gamma");
static const std::string kColorCorrectGainName = std::string("Gain");
static const std::string kColorCorrectOffsetName = std::string("Offset");

#define kColorCorrectToneRangesParamName "toneRanges"
#define kColorCorrectToneRangesParamLabel "Tone Ranges"
#define kColorCorrectToneRangesParamHint "Tone ranges lookup table"

#define LUT_MAX_PRECISION 100

// Rec.709 luminance:
//Y = 0.2126 R + 0.7152 G + 0.0722 B
static const double s_rLum = 0.2126;
static const double s_gLum = 0.7152;
static const double s_bLum = 0.0722;

using namespace OFX;


template <class T> inline T
Clamp(T v, int min, int max)
{
    if(v < T(min)) return T(min);
    if(v > T(max)) return T(max);
    return v;
}

namespace {
    struct ColorControlValues {
        double r;
        double g;
        double b;
        double a;

        void getValueFrom(double time, OFX::RGBAParam* p)
        {
            p->getValueAtTime(time, r, g, b, a);
        }
    };

    struct ColorControlGroup {
        ColorControlValues saturation;
        ColorControlValues contrast;
        ColorControlValues gamma;
        ColorControlValues gain;
        ColorControlValues offset;
    };

    struct RGBPixel {
        float r, g, b;

        RGBPixel(float r_, float g_, float b_)
        : r(r_)
        , g(g_)
        , b(b_)
        {
        }

        void applySaturation(const ColorControlValues &c)
        {
            r = r *((1.f - c.r) * s_rLum + c.r) + g *((1.0-c.r) * s_gLum) + b *((1.0-c.r) * s_bLum);
            g = g *((1.f - c.g) * s_gLum + c.g) + r *((1.0-c.g) * s_rLum) + b *((1.0-c.g) * s_bLum);
            b = b *((1.f - c.b) * s_bLum + c.b) + g *((1.0-c.b) * s_gLum) + r *((1.0-c.b) * s_rLum);
        }

        void applyContrast(const ColorControlValues &c)
        {
            r = (r - 0.5f) * c.r  + 0.5f;
            g = (g - 0.5f) * c.g  + 0.5f;
            b = (b - 0.5f) * c.b  + 0.5f;
        }

        void applyGain(const ColorControlValues &c)
        {
            r = r * c.r;
            g = g * c.g;
            b = b * c.b;
        }

        void applyGamma(const ColorControlValues &c)
        {
            if (r > 0) {
                r = std::pow(r ,1.f / (float)c.r);
            }
            if (g > 0) {
                g = std::pow(g ,1.f / (float)c.g);
            }
            if (b > 0) {
                b = std::pow(b ,1.f / (float)c.b);
            }
        }

        void applyOffset(const ColorControlValues &c)
        {
            r = r + c.r;
            g = g + c.g;
            b = b + c.b;
        }

        void applyGroup(const ColorControlGroup& group)
        {
            applySaturation(group.saturation);
            applyContrast(group.contrast);
            applyGamma(group.gamma);
            applyGain(group.gain);
            applyOffset(group.offset);
        }

        void applySMH(const ColorControlGroup& sValues,
                      double s_scale,
                      const ColorControlGroup& mValues,
                      double m_scale,
                      const ColorControlGroup& hValues,
                      double h_scale,
                      const ColorControlGroup& masterValues)
        {
            RGBPixel s = *this;
            RGBPixel m = *this;
            RGBPixel h = *this;
            s.applyGroup(sValues);
            m.applyGroup(mValues);
            h.applyGroup(hValues);

            r = s.r * s_scale + m.r * m_scale + h.r * h_scale;
            g = s.g * s_scale + m.g * m_scale + h.g * h_scale;
            b = s.b * s_scale + m.b * m_scale + h.b * h_scale;

            applyGroup(masterValues);
        }
    };
}

class ColorCorrecterBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    bool   _doMasking;

public :
    ColorCorrecterBase(OFX::ImageEffect &instance,const OFX::RenderArguments &args)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    {
        // build the LUT
        OFX::ParametricParam  *lookupTable = instance.fetchParametricParam(kColorCorrectToneRangesParamName);
        assert(lookupTable);
        for(int curve = 0; curve < 2; ++curve) {
            for(int position = 0; position <= LUT_MAX_PRECISION; ++position) {
                // position to evaluate the param at
                float parametricPos = float(position)/LUT_MAX_PRECISION;

                // evaluate the parametric param
                double value = lookupTable->getValue(curve, args.time, parametricPos);

                // set that in the lut
                _lookupTable[curve][position] = std::max(0.f,std::min(float(value*LUT_MAX_PRECISION+0.5), float(LUT_MAX_PRECISION)));
            }
        }
    }

    void setSrcImg(OFX::Image *v) {_srcImg = v;}

    void setMaskImg(OFX::Image *v) {_maskImg = v;}

    void doMasking(bool v) {_doMasking = v;}

    void setColorControlValues(const ColorControlGroup& master,
                               const ColorControlGroup& shadow,
                               const ColorControlGroup& midtone,
                               const ColorControlGroup& hightlights)
    {
        _masterValues = master;
        _shadowValues = shadow;
        _midtoneValues = midtone;
        _highlightsValues = hightlights;
    }

    void colorTransform(float *r,float *g,float *b)
    {
        float luminance = *r * s_rLum + *g * s_gLum + *b * s_bLum;
        float s_scale = interpolate(0, luminance);
        float h_scale = interpolate(1, luminance);
        float m_scale = 1.f - s_scale - h_scale;

        RGBPixel p(*r, *g, *b);
        p.applySMH(_shadowValues, s_scale,
                   _midtoneValues, m_scale,
                   _highlightsValues, h_scale,
                   _masterValues);
        *r = p.r;
        *g = p.g;
        *b = p.b;
    }

private:
    float interpolate(int curve, float value)
    {
        if (value < 0.) {
            return _lookupTable[curve][0];
        } else if (value >= 1.) {
            return _lookupTable[curve][LUT_MAX_PRECISION];
        } else {
            double i_d = std::floor(value * LUT_MAX_PRECISION);
            int i = (int)i_d;
            assert(i < LUT_MAX_PRECISION);
            double alpha = value * LUT_MAX_PRECISION - i_d;
            assert(0. <= alpha && alpha < 1.);
            return _lookupTable[curve][i] * (1.-alpha) + _lookupTable[curve][i] * alpha;
        }
    }

private :
    ColorControlGroup _masterValues;
    ColorControlGroup _shadowValues;
    ColorControlGroup _midtoneValues;
    ColorControlGroup _highlightsValues;

    float _lookupTable[2][LUT_MAX_PRECISION + 1];
};



template <class PIX, int nComponents, int maxValue>
class ColorCorrecter : public ColorCorrecterBase
{
public :
    ColorCorrecter(OFX::ImageEffect &instance,const OFX::RenderArguments &args)
    : ColorCorrecterBase(instance,args)
    {

    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {

        float maskScale = 1.0f;
        for(int y = procWindow.y1; y < procWindow.y2; y++)
        {
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for(int x = procWindow.x1; x < procWindow.x2; x++)
            {
                PIX *srcPix = (PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if(_doMasking)
                {
                    if(!_maskImg)
                        maskScale = 1.0f;
                    else
                    {
                        PIX *maskPix = (PIX *)  (_maskImg ? _maskImg->getPixelAddress(x, y) : 0);
                        maskScale = maskPix != 0 ? float(*maskPix)/float(maxValue) : 0.0f;
                    }
                }
                if(srcPix)
                {
                    PIX r = srcPix[0];
                    PIX g = srcPix[1];
                    PIX b = srcPix[2];
                    float n_r = float(r) / float(maxValue);
                    float n_g = float(g) / float(maxValue);
                    float n_b = float(b) / float(maxValue);
                    float t_r = n_r,t_g = n_g,t_b = n_b;
                    colorTransform(&t_r, &t_g, &t_b);
                    n_r = t_r * maskScale + (1.f - maskScale) * n_r;
                    n_g = t_g * maskScale + (1.f - maskScale) * n_g;
                    n_b = t_b * maskScale + (1.f - maskScale) * n_b;
                    dstPix[0] = PIX(Clamp(n_r * maxValue,0,maxValue));
                    dstPix[1] = PIX(Clamp(n_g * maxValue,0,maxValue));
                    dstPix[2] = PIX(Clamp(n_b * maxValue,0,maxValue));
                    if (nComponents == 4) {
                        dstPix[3] = srcPix[3];
                    }
                }
                else
                {
                    for(int c = 0; c < nComponents; c++)
                        dstPix[c] = 0;
                }
                dstPix += nComponents;
            }
        }
    }
};

namespace {
    struct ColorControlParamGroup {
        OFX::RGBAParam* saturation;
        OFX::RGBAParam* contrast;
        OFX::RGBAParam* gamma;
        OFX::RGBAParam* gain;
        OFX::RGBAParam* offset;
    };
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorCorrectPlugin : public OFX::ImageEffect
{
public :
    
    enum ColorCorrectGroupType {
        eGroupMaster = 0,
        eGroupShadow,
        eGroupMidtone,
        eGroupHighlight
    };
    
    /** @brief ctor */
    ColorCorrectPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        fetchColorControlGroup(kColorCorrectMasterGroupName, &_masterParamsGroup);
        fetchColorControlGroup(kColorCorrectShadowsGroupName, &_shadowsParamsGroup);
        fetchColorControlGroup(kColorCorrectMidtonesGroupName, &_midtonesParamsGroup);
        fetchColorControlGroup(kColorCorrectHighlightsGroupName, &_highlightsParamsGroup);
        _rangesParam = fetchParametricParam(kColorCorrectToneRangesParamName);
    }
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(ColorCorrecterBase &, const OFX::RenderArguments &args);
    
private:
    void fetchColorControlGroup(const std::string& groupName, ColorControlParamGroup* group) {
        group->saturation = fetchRGBAParam(groupName + '.' + kColorCorrectSaturationName);
        group->contrast = fetchRGBAParam(groupName + '.' + kColorCorrectContrastName);
        group->gamma = fetchRGBAParam(groupName + '.' + kColorCorrectGammaName);
        group->gain = fetchRGBAParam(groupName + '.' + kColorCorrectGainName);
        group->offset = fetchRGBAParam(groupName + '.' + kColorCorrectOffsetName);
    }
    
    void getColorCorrectGroupValues(double time, ColorControlGroup* groupValues, ColorCorrectGroupType type);

    ColorControlParamGroup& getGroup(ColorCorrectGroupType type) {
        switch (type) {
            case eGroupMaster:
                return _masterParamsGroup;
            case eGroupShadow:
                return _shadowsParamsGroup;
            case eGroupMidtone:
                return _midtonesParamsGroup;
            case eGroupHighlight:
                return _highlightsParamsGroup;
            default:
                assert(false);
                break;
        }
    }

private :
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    ColorControlParamGroup _masterParamsGroup;
    ColorControlParamGroup _shadowsParamsGroup;
    ColorControlParamGroup _midtonesParamsGroup;
    ColorControlParamGroup _highlightsParamsGroup;
    OFX::ParametricParam* _rangesParam;
};


void ColorCorrectPlugin::getColorCorrectGroupValues(double time, ColorControlGroup* groupValues, ColorCorrectGroupType type)
{
    ColorControlParamGroup& group = getGroup(type);
    groupValues->saturation.getValueFrom(time, group.saturation);
    groupValues->contrast.getValueFrom(time, group.contrast);
    groupValues->gamma.getValueFrom(time, group.gamma);
    groupValues->gain.getValueFrom(time, group.gain);
    groupValues->offset.getValueFrom(time, group.offset);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorCorrectPlugin::setupAndProcess(ColorCorrecterBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (mask.get()) {
        if (mask->getRenderScale().x != args.renderScale.x ||
            mask->getRenderScale().y != args.renderScale.y ||
            mask->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        processor.doMasking(true);
        processor.setMaskImg(mask.get());
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    ColorControlGroup masterValues,shadowValues,midtoneValues,highlightValues;
    getColorCorrectGroupValues(args.time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(args.time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(args.time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(args.time, &highlightValues, eGroupHighlight);
    processor.setColorControlValues(masterValues, shadowValues, midtoneValues, highlightValues);
    processor.process();
}

// the overridden render function
void
ColorCorrectPlugin::render(const OFX::RenderArguments &args)
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
                ColorCorrecter<unsigned char, 4, 255> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ColorCorrecter<unsigned short, 4, 65535> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ColorCorrecter<float,4,1> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch(dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                ColorCorrecter<unsigned char, 3, 255> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                ColorCorrecter<unsigned short, 3, 65535> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ColorCorrecter<float,3,1> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


mDeclarePluginFactory(ColorCorrectPluginFactory, {}, {});

void ColorCorrectPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("ColorCorrectOFX", "ColorCorrectOFX", "ColorCorrectOFX");
    desc.setPluginGrouping("Color");
    desc.setPluginDescription("Adjusts the saturation, constrast, gamma, gain and offset of an image. "
                              "The ranges of the shadows, midtones and highlights are controlled by the curves "
                              "in the \"Ranges\" tab. ");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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

static void
defineRGBAScaleParam(OFX::ImageEffectDescriptor &desc,
                     const std::string &name,
                     const std::string &label,
                     const std::string &hint,
                     GroupParamDescriptor *parent,
                     PageParamDescriptor* page,
                     double def,
                     double min,
                     double max)
{
    RGBAParamDescriptor *param = desc.defineRGBAParam(name);
    param->setLabels(label, label, label);
    param->setScriptName(name);
    param->setHint(hint);
    param->setDefault(def,def,def,def);
    param->setRange(min,min,min,min, max,max,max,max);
    param->setDisplayRange(min,min,min,min,max,max,max,max);
    if (parent) {
        param->setParent(*parent);
    }
    page->addChild(*param);
}

static void
defineColorGroup(const std::string& groupName,
                 const std::string& hint,
                 PageParamDescriptor* page,
                 OFX::ImageEffectDescriptor &desc,
                 bool open)
 {
    GroupParamDescriptor* groupDesc = 0;
    groupDesc = desc.defineGroupParam(groupName);
    groupDesc->setLabels(groupName, groupName, groupName);
    groupDesc->setHint(hint);
    groupDesc->setOpen(open);
    
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectSaturationName, kColorCorrectSaturationName, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectContrastName, kColorCorrectContrastName, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectGammaName, kColorCorrectGammaName, hint, groupDesc, page, 1, 0.2, 5);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectGainName, kColorCorrectGainName, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kColorCorrectOffsetName, kColorCorrectOffsetName, hint, groupDesc, page, 0, -1, 1);
     page->addChild(*groupDesc);
}

void ColorCorrectPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if(context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    defineColorGroup(kColorCorrectMasterGroupName,"", page, desc,true);
    defineColorGroup(kColorCorrectShadowsGroupName,"" ,page, desc,false);
    defineColorGroup(kColorCorrectMidtonesGroupName, "",page, desc,false);
    defineColorGroup(kColorCorrectHighlightsGroupName, "",page, desc,false);
    
    PageParamDescriptor* ranges = desc.definePageParam("Ranges");
    
    OFX::ParametricParamDescriptor* lookupTable = desc.defineParametricParam(kColorCorrectToneRangesParamName);
    assert(lookupTable);
    lookupTable->setLabels(kColorCorrectToneRangesParamLabel, kColorCorrectToneRangesParamLabel, kColorCorrectToneRangesParamLabel);
    lookupTable->setHint(kColorCorrectToneRangesParamHint);

    // define it as two dimensional
    lookupTable->setDimension(2);
    
    lookupTable->setDimensionLabel("Shadow", 0);
    lookupTable->setDimensionLabel("Highlight", 1);
    
    // set the UI colour for each dimension
    const OfxRGBColourD shadow   = {0.6,0.4,0.6};
    const OfxRGBColourD highlight  =  {0.8,0.7,0.6};
    lookupTable->setUIColour( 0, shadow );
    lookupTable->setUIColour( 1, highlight );
    
    // set the min/max parametric range to 0..1
    lookupTable->setRange(0.0, 1.0);
    
    lookupTable->addControlPoint(0, // curve to set
                                 0.0,   // time, ignored in this case, as we are not adding a key
                                 0.0,   // parametric position, zero
                                 1.0,   // value to be, 0
                                 false);   // don't add a key
    lookupTable->addControlPoint(0, 0.0, 0.09, 0.0,false);
    
    lookupTable->addControlPoint(1, 0.0, 0.5, 0.0,false);
    lookupTable->addControlPoint(1, 0.0, 1.0, 1.0,false);
    
    ranges->addChild(*lookupTable);

    
}

OFX::ImageEffect* ColorCorrectPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ColorCorrectPlugin(handle);
}

void getColorCorrectPluginID(OFX::PluginFactoryArray &ids)
{
    static ColorCorrectPluginFactory p("net.sf.openfx:ColorCorrectPlugin", /*pluginVersionMajor=*/1, /*pluginVersionMinor=*/0);
    ids.push_back(&p);
}

