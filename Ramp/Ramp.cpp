/*
 OFX Ramp plugin.
 
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
#include "Ramp.h"

#include <cmath>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsOGLTextRenderer.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#define POINT_TOLERANCE 6
#define POINT_SIZE 5


#define kPluginName "RampOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription \
"Draw a ramp between 2 edges.\n" \
"The ramp is composited with the source image using the 'over' operator."
#define kPluginIdentifier "net.sf.openfx.Ramp"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe


#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component"

#define kParamPoint0 "point0"
#define kParamPoint0Label "Point 0"

#define kParamColor0 "color0"
#define kParamColor0Label "Color 0"

#define kParamPoint1 "point1"
#define kParamPoint1Label "Point 1"

#define kParamColor1 "color1"
#define kParamColor1Label "Color 1"

#define kParamType "type"
#define kParamTypeLabel "Type"

enum RampTypeEnum
{
    eRampTypeLinear = 0,
    eRampTypeEaseIn,
    eRampTypeEaseOut,
    eRampTypeSmooth
};

namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };
}

// round to the closest int, 1/10 int, etc
// this make parameter editing easier
// pscale is args.pixelScale.x / args.renderScale.x;
// pscale10 is the power of 10 below pscale
static double fround(double val, double pscale)
{
    double pscale10 = std::pow(10.,std::floor(std::log10(pscale)));
    return pscale10 * std::floor(val/pscale10 + 0.5);
}

using namespace OFX;

class RampProcessorBase : public OFX::ImageProcessor
{
   
    
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;

    RampTypeEnum _type;
    RGBAValues _color0, _color1;
    OfxPointD _point0, _point1;

public:
    RampProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _processR(false)
    , _processG(false)
    , _processB(false)
    , _processA(false)
    , _type(eRampTypeLinear)
    {
        _point0.x = _point0.y = _point1.x = _point1.y = 0.;
        _color0.r = _color0.g = _color0.b = _color0.a = 0.;
        _color1.r = _color1.g = _color1.b = _color1.a = 0.;
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v)
    {
        _srcImg = v;
    }

    void setMaskImg(const OFX::Image *v, bool maskInvert)
    {
        _maskImg = v;
        _maskInvert = maskInvert;
    }

    void doMasking(bool v) {
        _doMasking = v;
    }

    void setValues(RampTypeEnum type,
                   const RGBAValues& color0,
                   const RGBAValues& color1,
                   const OfxPointD& point0,
                   const OfxPointD& point1,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA)
    {
        _type = type;
        _color0 = color0;
        _color1 = color1;
        _point0 = point0;
        _point1 = point1;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }
 };


template <class PIX, int nComponents, int maxValue>
class RampProcessor : public RampProcessorBase
{
public:
    RampProcessor(OFX::ImageEffect &instance)
    : RampProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        
        int todo = ((_processR ? 0xf000 : 0) | (_processG ? 0x0f00 : 0) | (_processB ? 0x00f0 : 0) | (_processA ? 0x000f : 0));
        if (nComponents == 1) {
            switch (todo) {
                case 0x0000:
                case 0x00f0:
                case 0x0f00:
                case 0x0ff0:
                case 0xf000:
                case 0xf0f0:
                case 0xff00:
                case 0xfff0:
                    return process<false,false,false,false>(procWindow);
                case 0x000f:
                case 0x00ff:
                case 0x0f0f:
                case 0x0fff:
                case 0xf00f:
                case 0xf0ff:
                case 0xff0f:
                case 0xffff:
                    return process<false,false,false,true >(procWindow);
            }
        } else if (nComponents == 3) {
            switch (todo) {
                case 0x0000:
                case 0x000f:
                    return process<false,false,false,false>(procWindow);
                case 0x00f0:
                case 0x00ff:
                    return process<false,false,true ,false>(procWindow);
                case 0x0f00:
                case 0x0f0f:
                    return process<false,true ,false,false>(procWindow);
                case 0x0ff0:
                case 0x0fff:
                    return process<false,true ,true ,false>(procWindow);
                case 0xf000:
                case 0xf00f:
                    return process<true ,false,false,false>(procWindow);
                case 0xf0f0:
                case 0xf0ff:
                    return process<true ,false,true ,false>(procWindow);
                case 0xff00:
                case 0xff0f:
                    return process<true ,true ,false,false>(procWindow);
                case 0xfff0:
                case 0xffff:
                    return process<true ,true ,true ,false>(procWindow);
            }
        } else if (nComponents == 4) {
            switch (todo) {
                case 0x0000:
                    return process<false,false,false,false>(procWindow);
                case 0x000f:
                    return process<false,false,false,true >(procWindow);
                case 0x00f0:
                    return process<false,false,true ,false>(procWindow);
                case 0x00ff:
                    return process<false,false,true, true >(procWindow);
                case 0x0f00:
                    return process<false,true ,false,false>(procWindow);
                case 0x0f0f:
                    return process<false,true ,false,true >(procWindow);
                case 0x0ff0:
                    return process<false,true ,true ,false>(procWindow);
                case 0x0fff:
                    return process<false,true ,true ,true >(procWindow);
                case 0xf000:
                    return process<true ,false,false,false>(procWindow);
                case 0xf00f:
                    return process<true ,false,false,true >(procWindow);
                case 0xf0f0:
                    return process<true ,false,true ,false>(procWindow);
                case 0xf0ff:
                    return process<true ,false,true, true >(procWindow);
                case 0xff00:
                    return process<true ,true ,false,false>(procWindow);
                case 0xff0f:
                    return process<true ,true ,false,true >(procWindow);
                case 0xfff0:
                    return process<true ,true ,true ,false>(procWindow);
                case 0xffff:
                    return process<true ,true ,true ,true >(procWindow);
            }
        }
    }

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        assert((!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4));
        assert(!processA || (nComponents == 1 || nComponents == 4));
        switch (_type) {
            case eRampTypeLinear:
                processForType<processR,processG,processB,processA,eRampTypeLinear>(procWindow);
                break;
            case eRampTypeEaseIn:
                processForType<processR,processG,processB,processA,eRampTypeEaseIn>(procWindow);
                break;
            case eRampTypeEaseOut:
                processForType<processR,processG,processB,processA,eRampTypeEaseOut>(procWindow);
                break;
            case eRampTypeSmooth:
                processForType<processR,processG,processB,processA,eRampTypeSmooth>(procWindow);
                break;
        }
    }
    
    
    template<bool processR, bool processG, bool processB, bool processA, RampTypeEnum type>
    void processForType(const OfxRectI& procWindow)
    {
        float tmpPix[4];

        const double norm2 = (_point1.x - _point0.x)*(_point1.x - _point0.x) + (_point1.y - _point0.y)*(_point1.y - _point0.y);
        const double nx = norm2 == 0. ? 0. : (_point1.x - _point0.x)/ norm2;
        const double ny = norm2 == 0. ? 0. : (_point1.y - _point0.y)/ norm2;

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                OfxPointI p_pixel;
                OfxPointD p;
                p_pixel.x = x;
                p_pixel.y = y;
                OFX::MergeImages2D::toCanonical(p_pixel, _dstImg->getRenderScale(), _dstImg->getPixelAspectRatio(), &p);
                double t = (p.x - _point0.x) * nx + (p.y - _point0.y) * ny;

                if (t <= 0) {
                    tmpPix[0] = _color0.r;
                    tmpPix[1] = _color0.g;
                    tmpPix[2] = _color0.b;
                    tmpPix[3] = _color0.a;

                } else if (t >= 1.) {
                    tmpPix[0] = _color1.r;
                    tmpPix[1] = _color1.g;
                    tmpPix[2] = _color1.b;
                    tmpPix[3] = _color1.a;

                } else {
                    switch (type) {
                        case eRampTypeEaseIn:
                            t *= t;
                            break;
                        case eRampTypeEaseOut:
                            t = - t * (t - 2);
                            break;
                        case eRampTypeSmooth:
                            t *= 2.;
                            if (t < 1) {
                                t = t * t / (2.);
                            } else {
                                --t;
                                t =  -0.5 * (t * (t - 2) - 1);
                            }
                        default:
                            break;
                    }
                    
                    tmpPix[0] = _color0.r * (1 - t) + _color1.r * t;
                    tmpPix[1] = _color0.g * (1 - t) + _color1.g * t;
                    tmpPix[2] = _color0.b * (1 - t) + _color1.b * t;
                    tmpPix[3] = _color0.a * (1 - t) + _color1.a * t;
                }
                double a = tmpPix[3];

                // ofxsMaskMixPix takes non-normalized values
                tmpPix[0] *= maxValue;
                tmpPix[1] *= maxValue;
                tmpPix[2] *= maxValue;
                tmpPix[3] *= maxValue;
                float srcPixRGBA[4] = {0, 0, 0, 0};
                if (srcPix) {
                    if (nComponents >= 3) {
                        srcPixRGBA[0] = srcPix[0];
                        srcPixRGBA[1] = srcPix[1];
                        srcPixRGBA[2] = srcPix[2];
                    }
                    if (nComponents == 1 || nComponents == 4) {
                        srcPixRGBA[3] = srcPix[nComponents-1];
                    }
                }
                if (processR) {
                    tmpPix[0] = tmpPix[0] + srcPixRGBA[0]*(1.-a);
                } else {
                    tmpPix[0] = srcPixRGBA[0];
                }
                if (processG) {
                    tmpPix[1] = tmpPix[1] + srcPixRGBA[1]*(1.-a);
                } else {
                    tmpPix[1] = srcPixRGBA[1];
                }
                if (processB) {
                    tmpPix[2] = tmpPix[2] + srcPixRGBA[2]*(1.-a);
                } else {
                    tmpPix[2] = srcPixRGBA[2];
                }
                if (processA) {
                    tmpPix[3] = tmpPix[3] + srcPixRGBA[3]*(1.-a);
                } else {
                    tmpPix[3] = srcPixRGBA[3];
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
            }
        }
    }

};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RampPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    RampPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , _processR(0)
    , _processG(0)
    , _processB(0)
    , _processA(0)
    , _point0(0)
    , _color0(0)
    , _point1(0)
    , _color1(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);

        assert(_processR && _processG && _processB && _processA);
        _point0 = fetchDouble2DParam(kParamPoint0);
        _point1 = fetchDouble2DParam(kParamPoint1);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        _type = fetchChoiceParam(kParamType);
        assert(_point0 && _point1 && _color0 && _color1 && _type);

        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    OfxRectD getRegionOfDefinitionForInteract(OfxTime time) const
    {
        return dstClip_->getRegionOfDefinition(time);
    }
    
private:
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* Override the clip preferences */
    void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RampProcessorBase &, const OFX::RenderArguments &args);
    
private:
    
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *dstClip_;
    Clip *srcClip_;
    Clip *maskClip_;

    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    Double2DParam* _point0;
    RGBAParam* _color0;
    Double2DParam* _point1;
    RGBAParam* _color1;
    ChoiceParam* _type;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RampPlugin::setupAndProcess(RampProcessorBase &processor, const OFX::RenderArguments &args)
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
    assert(srcClip_);
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
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                mask->getField() != args.fieldToRender) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    if (src.get() && dst.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    int type_i;
    _type->getValue(type_i);
    
    OfxPointD point0,point1;
    _point0->getValueAtTime(args.time, point0.x, point0.y);
    _point1->getValueAtTime(args.time, point1.x, point1.y);
    
    RGBAValues color0,color1;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(args.time, color1.r, color1.g, color1.b, color1.a);

    bool processR, processG, processB, processA;
    _processR->getValue(processR);
    _processG->getValue(processG);
    _processB->getValue(processB);
    _processA->getValue(processA);

    double mix;
    _mix->getValueAtTime(args.time, mix);

    processor.setValues((RampTypeEnum)type_i,
                        color0, color1,
                        point0, point1,
                        mix,
                        processR, processG, processB, processA);
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


// the internal render function
template <int nComponents>
void
RampPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            RampProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort: {
            RampProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat: {
            RampProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RampPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
RampPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                       OFX::Clip * &identityClip,
                       double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = srcClip_;
        return true;
    }

    bool processR;
    bool processG;
    bool processB;
    bool processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    if (!processR && !processG && !processB && !processA) {
        identityClip = srcClip_;
        return true;
    }

    RGBAValues color0,color1;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(args.time, color1.r, color1.g, color1.b, color1.a);
    if (color0.a == 0. && color1.a == 0.) {
        identityClip = srcClip_;
        return true;
    }

    return false;
}

/* Override the clip preferences */
void
RampPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of dstClip_ if alpha is affected and source is Opaque
    bool alpha;
    _processA->getValue(alpha);
    if (alpha && srcClip_->getPreMultiplication() == eImageOpaque) {
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

class RampInteract : public OFX::OverlayInteract
{
    enum InteractState
    {
        eInteractStateIdle = 0,
        eInteractStateDraggingPoint0,
        eInteractStateDraggingPoint1
    };
    
    Double2DParam* _point0;
    Double2DParam* _point1;
    OfxPointD _point0DragPos,_point1DragPos;
    OfxPointD _lastMousePos;
    InteractState _state;
    RampPlugin* _effect;
    
public:
    RampInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _point0(0)
    , _point1(0)
    , _point0DragPos()
    , _point1DragPos()
    , _lastMousePos()
    , _state(eInteractStateIdle)
    , _effect(0)
    {
        _point0 = effect->fetchDouble2DParam(kParamPoint0);
        _point1 = effect->fetchDouble2DParam(kParamPoint1);
        _effect = dynamic_cast<RampPlugin*>(effect);
        assert(_effect);
    }
    
    /** @brief the function called to draw in the interact */
    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL;
    
    /** @brief the function called to handle pen motion in the interact
     
     returns true if the interact trapped the action in some sense. This will block the action being passed to
     any other interact that may share the viewer.
     */
    virtual bool penMotion(const PenArgs &args) OVERRIDE FINAL;
    
    /** @brief the function called to handle pen down events in the interact
     
     returns true if the interact trapped the action in some sense. This will block the action being passed to
     any other interact that may share the viewer.
     */
    virtual bool penDown(const PenArgs &args) OVERRIDE FINAL;
    
    /** @brief the function called to handle pen up events in the interact
     
     returns true if the interact trapped the action in some sense. This will block the action being passed to
     any other interact that may share the viewer.
     */
    virtual bool penUp(const PenArgs &args) OVERRIDE FINAL;
  
};

//static void intersectToRoD(const OfxRectD& rod,const OfxPointD& p0)

static inline
void
crossProd(const Ofx3DPointD& u,
          const Ofx3DPointD& v,
          Ofx3DPointD* w)
{
    w->x = u.y*v.z - u.z*v.y;
    w->y = u.z*v.x - u.x*v.z;
    w->z = u.x*v.y - u.y*v.x;
}

bool
RampInteract::draw(const DrawArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    
    OfxPointD p[2];
    if (_state == eInteractStateDraggingPoint0) {
        p[0] = _point0DragPos;
    } else {
        _point0->getValueAtTime(args.time, p[0].x, p[0].y);
    }
    if (_state == eInteractStateDraggingPoint1) {
        p[1] = _point1DragPos;
    } else {
        _point1->getValueAtTime(args.time, p[1].x, p[1].y);
    }
    
    ///Clamp points to the rod
    OfxRectD rod = _effect->getRegionOfDefinitionForInteract(args.time);

    // A line is represented by a 3-vector (a,b,c), and its equation is (a,b,c).(x,y,1)=0
    // The intersection of two lines is given by their cross-product: (wx,wy,w) = (a,b,c)x(a',b',c').
    // The line passing through 2 points is obtained by their cross-product: (a,b,c) = (x,y,1)x(x',y',1)
    // The two lines passing through p0 and p1 and orthogonal to p0p1 are:
    // (p1.x - p0.x, p1.y - p0.y, -p0.x*(p1.x-p0.x) - p0.y*(p1.y-p0.y)) passing through p0
    // (p1.x - p0.x, p1.y - p0.y, -p1.x*(p1.x-p0.x) - p1.y*(p1.y-p0.y)) passing through p1
    // the four lines defining the RoD are:
    // (1,0,-x1) [x=x1]
    // (1,0,-x2) [x=x2]
    // (0,1,-y1) [x=y1]
    // (0,1,-y2) [y=y2]
    const Ofx3DPointD linex1 = {1, 0, -rod.x1};
    const Ofx3DPointD linex2 = {1, 0, -rod.x2};
    const Ofx3DPointD liney1 = {0, 1, -rod.y1};
    const Ofx3DPointD liney2 = {0, 1, -rod.y2};

    Ofx3DPointD line[2];
    OfxPointD pline1[2];
    OfxPointD pline2[2];

    // line passing through p0
    line[0].x = p[1].x - p[0].x;
    line[0].y = p[1].y - p[0].y;
    line[0].z = -p[0].x*(p[1].x-p[0].x) - p[0].y*(p[1].y-p[0].y);
    // line passing through p1
    line[1].x = p[1].x - p[0].x;
    line[1].y = p[1].y - p[0].y;
    line[1].z = -p[1].x*(p[1].x-p[0].x) - p[1].y*(p[1].y-p[0].y);
    // for each line...
    for (int i = 0; i < 2; ++i) {
        // compute the intersection with the four lines
        Ofx3DPointD interx1, interx2, intery1, intery2;

        crossProd(line[i], linex1, &interx1);
        crossProd(line[i], linex2, &interx2);
        crossProd(line[i], liney1, &intery1);
        crossProd(line[i], liney2, &intery2);
        if (interx1.z != 0. && interx2.z != 0.) {
            // initialize pline1 to the intersection with x=x1, pline2 with x=x2
            pline1[i].x = interx1.x/interx1.z;
            pline1[i].y = interx1.y/interx1.z;
            pline2[i].x = interx2.x/interx2.z;
            pline2[i].y = interx2.y/interx2.z;
            if ((pline1[i].y > rod.y2 && pline2[i].y > rod.y2) ||
                (pline1[i].y < rod.y1 && pline2[i].y < rod.y1)) {
                // line doesn't intersect rectangle, don't draw it.
                pline1[i].x = p[i].x;
                pline1[i].y = p[i].y;
                pline2[i].x = p[i].x;
                pline2[i].y = p[i].y;
            } else if (pline1[i].y < pline2[i].y) {
                // y is an increasing function of x, test the two other endpoints
                if (intery1.z != 0. && intery1.x/intery1.z > pline1[i].x) {
                    pline1[i].x = intery1.x/intery1.z;
                    pline1[i].y = intery1.y/intery1.z;
                }
                if (intery2.z != 0. && intery2.x/intery2.z < pline2[i].x) {
                    pline2[i].x = intery2.x/intery2.z;
                    pline2[i].y = intery2.y/intery2.z;
                }
            } else {
                // y is an decreasing function of x, test the two other endpoints
                if (intery2.z != 0. && intery2.x/intery2.z > pline1[i].x) {
                    pline1[i].x = intery2.x/intery2.z;
                    pline1[i].y = intery2.y/intery2.z;
                }
                if (intery1.z != 0. && intery1.x/intery1.z < pline2[i].x) {
                    pline2[i].x = intery1.x/intery1.z;
                    pline2[i].y = intery1.y/intery1.z;
                }
            }
        } else {
            // initialize pline1 to the intersection with y=y1, pline2 with y=y2
            pline1[i].x = intery1.x/intery1.z;
            pline1[i].y = intery1.y/intery1.z;
            pline2[i].x = intery2.x/intery2.z;
            pline2[i].y = intery2.y/intery2.z;
            if ((pline1[i].x > rod.x2 && pline2[i].x > rod.x2) ||
                (pline1[i].x < rod.x1 && pline2[i].x < rod.x1)) {
                // line doesn't intersect rectangle, don't draw it.
                pline1[i].x = p[i].x;
                pline1[i].y = p[i].y;
                pline2[i].x = p[i].x;
                pline2[i].y = p[i].y;
            }
        }
    }

    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT,GL_DONT_CARE);
    glLineWidth(1.5);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    glPointSize(POINT_SIZE);
    
    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        if (l == 0) {
            // translate (1,-1) pixels
            glTranslated(pscale.x, -pscale.y, 0);
        }

        for (int i = 0; i < 2; ++i) {
            bool dragging = _state == (i == 0 ? eInteractStateDraggingPoint0 : eInteractStateDraggingPoint1);
            glBegin(GL_POINTS);
            if (dragging) {
                glColor3f(0.*l, 1.*l, 0.*l);
            } else {
                glColor3f(0.8*l, 0.8*l, 0.8*l);
            }
            glVertex2d(p[i].x, p[i].y);
            glEnd();

            glLineStipple(2, 0xAAAA);
            glEnable(GL_LINE_STIPPLE);
            glBegin(GL_LINES);
            glColor3f(0.8*l, 0.8*l, 0.8*l);
            glVertex2d(pline1[i].x, pline1[i].y);
            glVertex2d(pline2[i].x, pline2[i].y);
            glEnd();

            double xoffset = 5 * pscale.x;
            double yoffset = 5 * pscale.y;
            TextRenderer::bitmapString(p[i].x + xoffset, p[i].y + yoffset, i == 0 ? kParamPoint0Label : kParamPoint1Label);
        }
        if (l == 0) {
            // translate (-1,1) pixels
            glTranslated(-pscale.x, pscale.y, 0);
        }
    }

    glPopAttrib();

    return true;
}

static bool isNearby(const OfxPointD& p, double x, double y, double tolerance, const OfxPointD& pscale)
{
    return std::fabs(p.x-x) <= tolerance*pscale.x &&  std::fabs(p.y-y) <= tolerance*pscale.y;
}


bool
RampInteract::penMotion(const PenArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    
    OfxPointD p0,p1;
    _point0->getValueAtTime(args.time, p0.x, p0.y);
    _point1->getValueAtTime(args.time, p1.x, p1.y);

    bool didSomething = false;
    
    OfxPointD delta;
    delta.x = args.penPosition.x - _lastMousePos.x;
    delta.y = args.penPosition.y - _lastMousePos.y;
    
    if (_state == eInteractStateDraggingPoint0) {
        _point0DragPos.x += delta.x;
        _point0DragPos.y += delta.y;
        didSomething = true;

    } else if (_state == eInteractStateDraggingPoint1) {
        _point1DragPos.x += delta.x;
        _point1DragPos.y += delta.y;
        didSomething = true;
    }
    
    _lastMousePos = args.penPosition;

    return didSomething;
}

bool
RampInteract::penDown(const PenArgs &args)
{
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;

    OfxPointD p0,p1;
    _point0->getValueAtTime(args.time, p0.x, p0.y);
    _point1->getValueAtTime(args.time, p1.x, p1.y);
    
    bool didSomething = false;
    
    if (isNearby(args.penPosition, p0.x, p0.y, POINT_TOLERANCE, pscale)) {
        _state = eInteractStateDraggingPoint0;
        didSomething = true;
    } else if (isNearby(args.penPosition, p1.x, p1.y, POINT_TOLERANCE, pscale)) {
        _state = eInteractStateDraggingPoint1;
        didSomething = true;
    } else {
        _state = eInteractStateIdle;
    }
    
    _point0DragPos = p0;
    _point1DragPos = p1;
    _lastMousePos = args.penPosition;

    return didSomething;
}

bool
RampInteract::penUp(const PenArgs &args)
{
    bool didSmthing = false;
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    if (_state == eInteractStateDraggingPoint0) {
        // round newx/y to the closest int, 1/10 int, etc
        // this make parameter editing easier
      
        _point0->setValue(fround(_point0DragPos.x, pscale.x), fround(_point0DragPos.y, pscale.y));
        didSmthing = true;
    } else if (_state == eInteractStateDraggingPoint1) {
        _point1->setValue(fround(_point1DragPos.x, pscale.x), fround(_point1DragPos.y, pscale.y));
        didSmthing = true;
    }
    _state = eInteractStateIdle;

    return didSmthing;
}

class RampOverlayDescriptor : public DefaultEffectOverlayDescriptor<RampOverlayDescriptor, RampInteract> {};



mDeclarePluginFactory(RampPluginFactory, {}, {});

void RampPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
    desc.setSupportsTiles(kSupportsTiles);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setOverlayInteractDescriptor(new RampOverlayDescriptor);

}



OFX::ImageEffect* RampPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new RampPlugin(handle);
}




void RampPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
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

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        page->addChild(*param);
    }
    
    // point0
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamPoint0);
        param->setLabels(kParamPoint0Label,kParamPoint0Label,kParamPoint0Label);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesCanonical);
        param->setDefault(100., 100.);
        page->addChild(*param);
    }
    
    
    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        param->setLabels(kParamColor0Label, kParamColor0Label, kParamColor0Label);
        param->setDefault(0, 0, 0, 0);
        page->addChild(*param);
    }

    // point1
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamPoint1);
        param->setLabels(kParamPoint1Label,kParamPoint1Label,kParamPoint1Label);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesCanonical);
        param->setDefault(100., 200.);
        page->addChild(*param);
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        param->setLabels(kParamColor1Label, kParamColor1Label, kParamColor1Label);
        param->setDefault(1., 1., 1., 1. );
        page->addChild(*param);
    }
    
    // type
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamType);
        param->setLabels(kParamTypeLabel, kParamTypeLabel, kParamTypeLabel);
        param->setHint("The type of interpolation used to generate the ramp");
        param->appendOption("Linear");
        param->appendOption("Ease-in");
        param->appendOption("Ease-out");
        param->appendOption("Smooth");
        param->setDefault(eRampTypeLinear);
        param->setAnimates(true);
        page->addChild(*param);
    }

    ofxsMaskMixDescribeParams(desc, page);
}

void getRampPluginID(OFX::PluginFactoryArray &ids)
{
    static RampPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

