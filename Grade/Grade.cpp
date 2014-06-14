/*
 OFX Grade plugin.
 
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

#include "Grade.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"


#define kBlackPointParamName "BlackPoint"
#define kWhitePointParamName "WhitePoint"
#define kBlackParamName "Black"
#define kWhiteParamName "White"
#define kMultiplyParamName "Multiply"
#define kOffsetParamName "Offset"
#define kGammaParamName "Gamma"
#define kClampBlackParamName "Clamp Black"
#define kClampWhiteParamName "Clamp White"


using namespace OFX;


namespace {
    struct RGBAValues {
        double r,g,b,a;
    };
}

class GradeProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    bool   _doMasking;

public:
    
    GradeProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    ,_doMasking(false)
    {
        
    }
    
    void setSrcImg(OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(OFX::Image *v) {_maskImg = v;}
    
    void doMasking(bool v) {_doMasking = v;}
    
    void setValues(const RGBAValues& blackPoint,
                   const RGBAValues& whitePoint,
                   const RGBAValues& black,
                   const RGBAValues& white,
                   const RGBAValues& multiply,
                   const RGBAValues& offset,
                   const RGBAValues& gamma,
                   bool clampBlack,
                   bool clampWhite)
    {
        _blackPoint = blackPoint;
        _whitePoint = whitePoint;
        _black = black;
        _white = white;
        _multiply = multiply;
        _offset = offset;
        _gamma = gamma;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
    }

    void grade(float* v, float wp, float bp, float white, float black, float mutiply, float offset, float gamma)
    {
        float A = mutiply * (white - black) / (wp - bp);
        float B = offset + black - A * bp;
        *v = std::pow((A * *v) + B,1.f / gamma);
    }
    
    void grade(float *r,float *g,float *b)
    {
        grade(r, _whitePoint.r, _blackPoint.r, _white.r, _black.r, _multiply.r, _offset.r, _gamma.r);
        grade(g, _whitePoint.g, _blackPoint.g, _white.g, _black.g, _multiply.g, _offset.g, _gamma.g);
        grade(b, _whitePoint.b, _blackPoint.b, _white.b, _black.b, _multiply.b, _offset.b, _gamma.b);
        if (_clampBlack) {
            *r = std::max(0.f,*r);
            *g = std::max(0.f,*g);
            *b = std::max(0.f,*b);
        }
        if (_clampBlack) {
            *r = std::min(1.f,*r);
            *g = std::min(1.f,*g);
            *b = std::min(1.f,*b);
        }
    }

private:
    RGBAValues _blackPoint;
    RGBAValues _whitePoint;
    RGBAValues _black;
    RGBAValues _white;
    RGBAValues _multiply;
    RGBAValues _offset;
    RGBAValues _gamma;
    bool _clampBlack;
    bool _clampWhite;
};



template <class PIX, int nComponents, int maxValue>
class GradeProcessor : public GradeProcessorBase
{
public :
    GradeProcessor(OFX::ImageEffect &instance)
    : GradeProcessorBase(instance)
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
                    grade(&t_r, &t_g, &t_b);
                    n_r = t_r * maskScale + (1.f - maskScale) * n_r;
                    n_g = t_g * maskScale + (1.f - maskScale) * n_g;
                    n_b = t_b * maskScale + (1.f - maskScale) * n_b;
                    dstPix[0] = PIX(n_r * maxValue);
                    dstPix[1] = PIX(n_g * maxValue);
                    dstPix[2] = PIX(n_b * maxValue);
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


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class GradePlugin : public OFX::ImageEffect
{
public :
    /** @brief ctor */
    GradePlugin(OfxImageEffectHandle handle)
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
        _blackPoint = fetchRGBAParam(kBlackPointParamName);
        _whitePoint = fetchRGBAParam(kWhitePointParamName);
        _black = fetchRGBAParam(kBlackParamName);
        _white = fetchRGBAParam(kWhiteParamName);
        _multiply = fetchRGBAParam(kMultiplyParamName);
        _offset = fetchRGBAParam(kOffsetParamName);
        _gamma = fetchRGBAParam(kGammaParamName);
        _clampBlack = fetchBooleanParam(kClampBlackParamName);
        _clampWhite = fetchBooleanParam(kClampWhiteParamName);
    }
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(GradeProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    OFX::RGBAParam* _blackPoint;
    OFX::RGBAParam* _whitePoint;
    OFX::RGBAParam* _black;
    OFX::RGBAParam* _white;
    OFX::RGBAParam* _multiply;
    OFX::RGBAParam* _offset;
    OFX::RGBAParam* _gamma;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
GradePlugin::setupAndProcess(GradeProcessorBase &processor, const OFX::RenderArguments &args)
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
    if(src.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if(getContext() != OFX::eContextFilter)
    {
        processor.doMasking(true);
        processor.setMaskImg(mask.get());
    }
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    RGBAValues blackPoint,whitePoint,black,white,multiply,offset,gamma;
    _blackPoint->getValueAtTime(args.time, blackPoint.r, blackPoint.g, blackPoint.b, blackPoint.a);
    _whitePoint->getValueAtTime(args.time, whitePoint.r, whitePoint.g, whitePoint.b, whitePoint.a);
    _black->getValueAtTime(args.time, black.r, black.g, black.b, black.a);
    _white->getValueAtTime(args.time, white.r, white.g, white.b, white.a);
    _multiply->getValueAtTime(args.time, multiply.r, multiply.g, multiply.b, multiply.a);
    _offset->getValueAtTime(args.time, offset.r, offset.g, offset.b, offset.a);
    _gamma->getValueAtTime(args.time, gamma.r, gamma.g, gamma.b, gamma.a);
    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    processor.setValues(blackPoint, whitePoint, black, white, multiply, offset, gamma, clampBlack, clampWhite);
    processor.process();
}

// the overridden render function
void
GradePlugin::render(const OFX::RenderArguments &args)
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
                GradeProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                GradeProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                GradeProcessor<float,4,1> fred(*this);
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
                GradeProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                GradeProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                GradeProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


mDeclarePluginFactory(GradePluginFactory, {}, {});

void GradePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("GradeOFX", "GradeOFX", "GradeOFX");
    desc.setPluginGrouping("Color");
    desc.setPluginDescription("Modify the tonal spread of an image from the white and black points. "
                              "This node can also be used to match colors of 2 images: The darkest and lightest points of "
                              "the target image are converted to black and white using the blackpoint and whitepoint values. "
                              "These 2 values are then moved to new values using the black(for dark point) and white(for white point). "
                              "You can also apply multiply/offset/gamma for other color fixing you may need. "
                              "Here is the formula used: \n"
                              "A = multiply * (white - black) / (whitepoint - blackpoint) \n"
                              "B = offset + black - A * blackpoint \n"
                              "output = pow(A * input + B, 1 / gamma).");
    
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

void defineRGBAScaleParam(OFX::ImageEffectDescriptor &desc,
                          const std::string &name, const std::string &label, const std::string &hint,
                          PageParamDescriptor* page,double def , double min,double max)
{
    RGBAParamDescriptor *param = desc.defineRGBAParam(name);
    param->setLabels(label, label, label);
    param->setScriptName(name);
    param->setHint(hint);
    param->setDefault(def,def,def,def);
    param->setRange(min,min,min,min, max,max,max,max);
    param->setDisplayRange(min,min,min,min,max,max,max,max);
    page->addChild(*param);
}


void GradePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
    defineRGBAScaleParam(desc, kBlackPointParamName, kBlackPointParamName, "Set the color of the darkest pixels in the image", page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kWhitePointParamName, kWhitePointParamName, "Set the color of the brightest pixels in the image",
                         page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kBlackParamName, kBlackParamName, "Colors corresponding to the blackpoint are set to this value",
                         page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kWhiteParamName, kWhiteParamName, "Colors corresponding to the whitepoint are set to this value"
                         , page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kMultiplyParamName, kMultiplyParamName, "Multiplies the result by this value", page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kOffsetParamName, kOffsetParamName, "Adds this value to the result (this applies to black and white)",
                         page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kGammaParamName, kGammaParamName, "Final gamma correction", page, 1., 0.2, 5.);
    
    BooleanParamDescriptor *clampBlackParam = desc.defineBooleanParam(kClampBlackParamName);
    clampBlackParam->setLabels(kClampBlackParamName, kClampBlackParamName, kClampBlackParamName);
    clampBlackParam->setScriptName(kClampBlackParamName);
    clampBlackParam->setHint("All colors below 0 will be set to 0.");
    clampBlackParam->setDefault(true);
    clampBlackParam->setAnimates(true);
    page->addChild(*clampBlackParam);
    
    BooleanParamDescriptor *clampWhiteParam = desc.defineBooleanParam(kClampWhiteParamName);
    clampWhiteParam->setLabels(kClampWhiteParamName, kClampWhiteParamName, kClampWhiteParamName);
    clampWhiteParam->setScriptName(kClampWhiteParamName);
    clampWhiteParam->setHint("All colors above 1 will be set to 1.");
    clampWhiteParam->setDefault(true);
    clampWhiteParam->setAnimates(true);
    page->addChild(*clampWhiteParam);

}

OFX::ImageEffect* GradePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new GradePlugin(handle);
}

void getGradePluginID(OFX::PluginFactoryArray &ids)
{
    static GradePluginFactory p("net.sf.openfx:GradePlugin", /*pluginVersionMajor=*/1, /*pluginVersionMinor=*/0);
    ids.push_back(&p);
}

