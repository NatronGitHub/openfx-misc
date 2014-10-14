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
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "ColorCorrectOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Adjusts the saturation, constrast, gamma, gain and offset of an image. " \
                          "The ranges of the shadows, midtones and highlights are controlled by the curves " \
                          "in the \"Ranges\" tab. "
#define kPluginIdentifier "net.sf.openfx.ColorCorrectPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

////std strings because we need them in changedParam
static const std::string kGroupMaster = std::string("Master");
static const std::string kGroupShadows = std::string("Shadows");
static const std::string kGroupMidtones = std::string("Midtones");
static const std::string kGroupHighlights = std::string("Highlights");

static const std::string kParamSaturation = std::string("Saturation");
static const std::string kParamContrast = std::string("Contrast");
static const std::string kParamGamma = std::string("Gamma");
static const std::string kParamGain = std::string("Gain");
static const std::string kParamOffset = std::string("Offset");

#define kParamColorCorrectToneRanges "toneRanges"
#define kParamColorCorrectToneRangesLabel "Tone Ranges"
#define kParamColorCorrectToneRangesHint "Tone ranges lookup table"
#define kParamColorCorrectToneRangesDim0 "Shadow"
#define kParamColorCorrectToneRangesDim1 "Highlight"

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define LUT_MAX_PRECISION 100

// Rec.709 luminance:
//Y = 0.2126 R + 0.7152 G + 0.0722 B
static const double s_rLum = 0.2126;
static const double s_gLum = 0.7152;
static const double s_bLum = 0.0722;

using namespace OFX;


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
        double r, g, b;

        RGBPixel(double r_, double g_, double b_)
        : r(r_)
        , g(g_)
        , b(b_)
        {
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

    private:
        void applySaturation(const ColorControlValues &c)
        {
            double tmp_r ,tmp_g,tmp_b ;
            tmp_r = r *((1.f - c.r) * s_rLum + c.r) + g *((1.f-c.r) * s_gLum) + b *((1.f-c.r) * s_bLum);
            tmp_g = g *((1.f - c.g) * s_gLum + c.g) + r *((1.f-c.g) * s_rLum) + b *((1.f-c.g) * s_bLum);
            tmp_b = b *((1.f - c.b) * s_bLum + c.b) + g *((1.f-c.b) * s_gLum) + r *((1.f-c.b) * s_rLum);
            r = tmp_r;
            g = tmp_g;
            b = tmp_b;
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
                r = std::pow(r ,1. / c.r);
            }
            if (g > 0) {
                g = std::pow(g ,1. / c.g);
            }
            if (b > 0) {
                b = std::pow(b ,1. / c.b);
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

    };
}

class ColorCorrecterBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;

public:
    ColorCorrecterBase(OFX::ImageEffect &instance,const OFX::RenderArguments &args)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _premult(false)
    , _premultChannel(3)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _clampBlack(true)
    , _clampWhite(true)
    {
        // build the LUT
        OFX::ParametricParam  *lookupTable = instance.fetchParametricParam(kParamColorCorrectToneRanges);
        assert(lookupTable);
        for (int curve = 0; curve < 2; ++curve) {
            for (int position = 0; position <= LUT_MAX_PRECISION; ++position) {
                // position to evaluate the param at
                double parametricPos = double(position)/LUT_MAX_PRECISION;

                // evaluate the parametric param
                double value = lookupTable->getValue(curve, args.time, parametricPos);

                // set that in the lut
                _lookupTable[curve][position] = (float)std::max(0.,std::min(value*LUT_MAX_PRECISION+0.5, double(LUT_MAX_PRECISION)));
            }
        }
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) {_maskImg = v; _maskInvert = maskInvert;}

    void doMasking(bool v) {_doMasking = v;}

    void setColorControlValues(const ColorControlGroup& master,
                               const ColorControlGroup& shadow,
                               const ColorControlGroup& midtone,
                               const ColorControlGroup& hightlights,
                               bool clampBlack,
                               bool clampWhite,
                               bool premult,
                               int premultChannel,
                               double mix)
    {
        _masterValues = master;
        _shadowValues = shadow;
        _midtoneValues = midtone;
        _highlightsValues = hightlights;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

    void colorTransform(double *r, double *g, double *b)
    {
        double luminance = *r * s_rLum + *g * s_gLum + *b * s_bLum;
        double s_scale = interpolate(0, luminance);
        double h_scale = interpolate(1, luminance);
        double m_scale = 1.f - s_scale - h_scale;

        RGBPixel p(*r, *g, *b);
        p.applySMH(_shadowValues, s_scale,
                   _midtoneValues, m_scale,
                   _highlightsValues, h_scale,
                   _masterValues);
        *r = clamp(p.r);
        *g = clamp(p.g);
        *b = clamp(p.b);
    }

private:
    double clamp(double comp)
    {
        if (_clampBlack && comp < 0.) {
            comp = 0.;
        } else  if (_clampWhite && comp > 1.0) {
            comp = 1.0;
        }
        return comp;
    }

    float interpolate(int curve, double value)
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

private:
    ColorControlGroup _masterValues;
    ColorControlGroup _shadowValues;
    ColorControlGroup _midtoneValues;
    ColorControlGroup _highlightsValues;
    bool _clampBlack;
    bool _clampWhite;

    double _lookupTable[2][LUT_MAX_PRECISION + 1];
};



template <class PIX, int nComponents, int maxValue>
class ColorCorrecter : public ColorCorrecterBase
{
public:
    ColorCorrecter(OFX::ImageEffect &instance,const OFX::RenderArguments &args)
    : ColorCorrecterBase(instance,args)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                double t_r = unpPix[0];
                double t_g = unpPix[1];
                double t_b = unpPix[2];
                colorTransform(&t_r, &t_g, &t_b);
                tmpPix[0] = t_r;
                tmpPix[1] = t_g;
                tmpPix[2] = t_b;
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
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
public:
    
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
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        fetchColorControlGroup(kGroupMaster, &_masterParamsGroup);
        fetchColorControlGroup(kGroupShadows, &_shadowsParamsGroup);
        fetchColorControlGroup(kGroupMidtones, &_midtonesParamsGroup);
        fetchColorControlGroup(kGroupHighlights, &_highlightsParamsGroup);
        _rangesParam = fetchParametricParam(kParamColorCorrectToneRanges);
        assert(_rangesParam);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(ColorCorrecterBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    void fetchColorControlGroup(const std::string& groupName, ColorControlParamGroup* group) {
        assert(group);
        group->saturation = fetchRGBAParam(groupName + '.' + kParamSaturation);
        group->contrast = fetchRGBAParam(groupName + '.' + kParamContrast);
        group->gamma = fetchRGBAParam(groupName + '.' + kParamGamma);
        group->gain = fetchRGBAParam(groupName + '.' + kParamGain);
        group->offset = fetchRGBAParam(groupName + '.' + kParamOffset);
        assert(group->saturation && group->contrast && group->gamma && group->gain && group->offset);
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

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    ColorControlParamGroup _masterParamsGroup;
    ColorControlParamGroup _shadowsParamsGroup;
    ColorControlParamGroup _midtonesParamsGroup;
    ColorControlParamGroup _highlightsParamsGroup;
    OFX::ParametricParam* _rangesParam;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
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
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
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
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    ColorControlGroup masterValues,shadowValues,midtoneValues,highlightValues;
    getColorCorrectGroupValues(args.time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(args.time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(args.time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(args.time, &highlightValues, eGroupHighlight);
    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setColorControlValues(masterValues, shadowValues, midtoneValues, highlightValues, clampBlack, clampWhite, premult, premultChannel, mix);
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
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ColorCorrecter<unsigned char, 4, 255> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorCorrecter<unsigned short, 4, 65535> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorCorrecter<float,4,1> fred(*this,args);
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
                ColorCorrecter<unsigned char, 3, 255> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorCorrecter<unsigned short, 3, 65535> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorCorrecter<float,3,1> fred(*this,args);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

static bool
groupIsIdentity(const ColorControlGroup& group)
{
    return (group.saturation.r == 1. &&
            group.saturation.g == 1. &&
            group.saturation.b == 1. &&
            group.saturation.a == 1. &&
            group.contrast.r == 1. &&
            group.contrast.g == 1. &&
            group.contrast.b == 1. &&
            group.contrast.a == 1. &&
            group.gamma.r == 1. &&
            group.gamma.g == 1. &&
            group.gamma.b == 1. &&
            group.gamma.a == 1. &&
            group.gain.r == 1. &&
            group.gain.g == 1. &&
            group.gain.b == 1. &&
            group.gain.a == 1. &&
            group.offset.r == 0. &&
            group.offset.g == 0. &&
            group.offset.b == 0. &&
            group.offset.a == 0.);

}

bool
ColorCorrectPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    //bool red, green, blue, alpha;
    double mix;
    //_processR->getValueAtTime(args.time, red);
    //_processG->getValueAtTime(args.time, green);
    //_processB->getValueAtTime(args.time, blue);
    //_processA->getValueAtTime(args.time, alpha);
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!red && !green && !blue && !alpha)*/) {
        identityClip = srcClip_;
        return true;
    }

    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }

    ColorControlGroup masterValues,shadowValues,midtoneValues,highlightValues;
    getColorCorrectGroupValues(args.time, &masterValues,    eGroupMaster);
    getColorCorrectGroupValues(args.time, &shadowValues,    eGroupShadow);
    getColorCorrectGroupValues(args.time, &midtoneValues,   eGroupMidtone);
    getColorCorrectGroupValues(args.time, &highlightValues, eGroupHighlight);
    if (groupIsIdentity(masterValues) &&
        groupIsIdentity(shadowValues) &&
        groupIsIdentity(midtoneValues) &&
        groupIsIdentity(highlightValues)) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

void
ColorCorrectPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
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


mDeclarePluginFactory(ColorCorrectPluginFactory, {}, {});

void ColorCorrectPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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
    
    defineRGBAScaleParam(desc, groupName + '.' + kParamSaturation, kParamSaturation, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kParamContrast, kParamContrast, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kParamGamma, kParamGamma, hint, groupDesc, page, 1, 0.2, 5);
    defineRGBAScaleParam(desc, groupName + '.' + kParamGain, kParamGain, hint, groupDesc, page, 1, 0, 4);
    defineRGBAScaleParam(desc, groupName + '.' + kParamOffset, kParamOffset, hint, groupDesc, page, 0, -1, 1);
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
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    defineColorGroup(kGroupMaster, "", page, desc, true);
    defineColorGroup(kGroupShadows, "", page, desc, false);
    defineColorGroup(kGroupMidtones, "", page, desc, false);
    defineColorGroup(kGroupHighlights, "", page, desc, false);
    
    PageParamDescriptor* ranges = desc.definePageParam("Ranges");

    {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamColorCorrectToneRanges);
        assert(param);
        param->setLabels(kParamColorCorrectToneRangesLabel, kParamColorCorrectToneRangesLabel, kParamColorCorrectToneRangesLabel);
        param->setHint(kParamColorCorrectToneRangesHint);

        // define it as two dimensional
        param->setDimension(2);

        param->setDimensionLabel(kParamColorCorrectToneRangesDim0, 0);
        param->setDimensionLabel(kParamColorCorrectToneRangesDim1, 1);

        // set the UI colour for each dimension
        const OfxRGBColourD shadow   = {0.6,0.4,0.6};
        const OfxRGBColourD highlight  =  {0.8,0.7,0.6};
        param->setUIColour( 0, shadow );
        param->setUIColour( 1, highlight );

        // set the min/max parametric range to 0..1
        param->setRange(0.0, 1.0);

        param->addControlPoint(0, // curve to set
                                     0.0,   // time, ignored in this case, as we are not adding a key
                                     0.0,   // parametric position, zero
                                     1.0,   // value to be, 0
                                     false);   // don't add a key
        param->addControlPoint(0, 0.0, 0.09, 0.0,false);

        param->addControlPoint(1, 0.0, 0.5, 0.0,false);
        param->addControlPoint(1, 0.0, 1.0, 1.0,false);
        ranges->addChild(*param);
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabels(kParamClampBlackLabel, kParamClampBlackLabel, kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabels(kParamClampWhiteLabel, kParamClampWhiteLabel, kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* ColorCorrectPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ColorCorrectPlugin(handle);
}

void getColorCorrectPluginID(OFX::PluginFactoryArray &ids)
{
    static ColorCorrectPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

