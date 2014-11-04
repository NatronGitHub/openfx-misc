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
#define kPluginDescription "Draw a ramp between 2 edges."
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

#define kPoint0Param "point0"
#define kPoint0ParamLabel "Point 0"

#define kColor0Param "color0"
#define kColor0ParamLabel "Color 0"

#define kPoint1Param "point1"
#define kPoint1ParamLabel "Point 1"

#define kColor1Param "color1"
#define kColor1ParamLabel "Color 1"

#define kTypeParam "type"
#define kTypeParamLabel "Type"

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


/**
 * @brief Generates a point
 **/
template <typename POINT>
static void generateSegmentAlongNormal(const POINT& p0,
                                       const POINT& p1,
                                       double t,
                                       POINT &normal0,
                                       POINT &normal1)
{
    //Normal line intersecting P0
    OfxPointD normalV;
    normalV.x = p0.y - p1.y;
    normalV.y = p1.x - p0.x;

    double norm = sqrt((p1.x - p0.x) * (p1.x - p0.x) +
                       (p1.y - p0.y) * (p1.y - p0.y));

    ///Don't consider points that are equals
    if (norm == 0) {
        norm = 1.;
    }
    normalV.x /= norm;
    normalV.y /= norm;

    normal0.x = normalV.x * t + p0.x;
    normal0.y = normalV.y * t + p0.y;
    normal1.x = normalV.x * -t + p0.x;
    normal1.y = normalV.y * -t + p0.y;
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
    bool _red, _green, _blue, _alpha;

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
    {
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
                   bool red,
                   bool green,
                   bool blue,
                   bool alpha)
    {
        _type = type;
        _color0 = color0;
        _color1 = color1;
        _point0 = point0;
        _point1 = point1;
        _mix = mix;
        _red = red;
        _green = green;
        _blue = blue;
        _alpha = alpha;
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
        
        int todo = ((_red ? 0xf000 : 0) | (_green ? 0x0f00 : 0) | (_blue ? 0x00f0 : 0) | (_alpha ? 0x000f : 0));
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

    
private:
    
    template<bool dored, bool dogreen, bool doblue, bool doalpha>
    void process(const OfxRectI& procWindow)
    {
        assert((!dored && !dogreen && !doblue) || (nComponents == 3 || nComponents == 4));
        assert(!doalpha || (nComponents == 1 || nComponents == 4));
        switch (_type) {
            case eRampTypeLinear:
                processForType<dored,dogreen,doblue,doalpha,eRampTypeLinear>(procWindow);
                break;
            case eRampTypeEaseIn:
                processForType<dored,dogreen,doblue,doalpha,eRampTypeEaseIn>(procWindow);
                break;
            case eRampTypeEaseOut:
                processForType<dored,dogreen,doblue,doalpha,eRampTypeEaseOut>(procWindow);
                break;
            case eRampTypeSmooth:
                processForType<dored,dogreen,doblue,doalpha,eRampTypeSmooth>(procWindow);
                break;
        }
    }
    
    
    template<bool dored, bool dogreen, bool doblue, bool doalpha, RampTypeEnum type>
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
                if (nComponents >= 3) {
                    if (dored) {
                        tmpPix[0] = srcPix[0] * (1.-a) + tmpPix[0]*a;
                    } else {
                        tmpPix[0] = srcPix[0];
                    }
                    if (dogreen) {
                        tmpPix[1] = srcPix[1] * (1.-a) + tmpPix[1]*a;
                    } else {
                        tmpPix[1] = srcPix[1];
                    }
                    if (doblue) {
                        tmpPix[2] = srcPix[2] * (1.-a) + tmpPix[2]*a;
                    } else {
                        tmpPix[2] = srcPix[2];
                    }
                }
                if (!doalpha && (nComponents == 1 || nComponents == 4)) {
                    tmpPix[nComponents-1] = srcPix[nComponents-1];
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
        _point0 = fetchDouble2DParam(kPoint0Param);
        _point1 = fetchDouble2DParam(kPoint1Param);
        _color0 = fetchRGBAParam(kColor0Param);
        _color1 = fetchRGBAParam(kColor1Param);
        _type = fetchChoiceParam(kTypeParam);
        assert(_point0 && _point1 && _color0 && _color1 && _type);

        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    OfxRectD getRegionOfDefinitionForInteract(OfxTime time) const
    {
        return dstClip_->getRegionOfDefinition(time);
    }
    
    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &/*args*/, OfxRectD &rod) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE;
private:
    
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


/** @brief The get RoD action.  We flag an infinite rod */
bool
RampPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &/*args*/, OfxRectD &rod)
{
    // we can generate noise anywhere on the image plan, so set our RoD to be infinite
    rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
    rod.x2 = rod.y2 = kOfxFlagInfiniteMax;
    return true;
}

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
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(args.time) : 0);
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
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

    bool doR,doG,doB,doA;
    _processR->getValue(doR);
    _processG->getValue(doG);
    _processB->getValue(doB);
    _processA->getValue(doA);

    double mix;
    _mix->getValueAtTime(args.time, mix);

    processor.setValues((RampTypeEnum)type_i,
                        color0, color1,
                        point0, point1,
                        mix,
                        doR, doG, doB, doA);
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


// the internal render function
template <int nComponents>
void
RampPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth)
    {
        case OFX::eBitDepthUByte :
        {
            RampProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            RampProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            RampProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
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
                       double &identityTime)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. /*|| (!red && !green && !blue && !alpha)*/) {
        identityClip = srcClip_;
        return true;
    }

    bool red, green, blue, alpha;
    _processR->getValueAtTime(args.time, red);
    _processG->getValueAtTime(args.time, green);
    _processB->getValueAtTime(args.time, blue);
    _processA->getValueAtTime(args.time, alpha);
    if (!red && !green && !blue && !alpha) {
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
        _point0 = effect->fetchDouble2DParam(kPoint0Param);
        _point1 = effect->fetchDouble2DParam(kPoint1Param);
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

bool
RampInteract::draw(const DrawArgs &args)
{
    
    OfxPointD pscale;
    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    
    OfxPointD p0,p1;
    if (_state == eInteractStateDraggingPoint0) {
        p0 = _point0DragPos;
    } else {
        _point0->getValueAtTime(args.time, p0.x, p0.y);
    }
    if (_state == eInteractStateDraggingPoint1) {
        p1 = _point1DragPos;
    } else {
        _point1->getValueAtTime(args.time, p1.x, p1.y);
    }
    
    OfxRectD rod = _effect->getRegionOfDefinitionForInteract(args.time);
    
    double t = (rod.x2 - rod.x1) * 100;
    
    OfxPointD p0Normal0,p0Normal1,p1Normal0,p1Normal1;
    generateSegmentAlongNormal(p0, p1, t, p0Normal0, p0Normal1);
    generateSegmentAlongNormal(p1, p0, t, p1Normal0, p1Normal1);

    ///Clamp points to the rod
    
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    
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
        
        
        glBegin(GL_POINTS);
        if (_state == eInteractStateDraggingPoint0) {
            glColor3f(0.*l, 1.*l, 0.*l);
        } else {
            glColor3f(0.8*l, 0.8*l, 0.8*l);
        }
        glVertex2d(p0.x, p0.y);
        if (_state == eInteractStateDraggingPoint1) {
            glColor3f(0.*l, 1.*l, 0.*l);
        } else {
            glColor3f(0.8*l, 0.8*l, 0.8*l);
        }
        glVertex2d(p1.x, p1.y);

        glEnd();
        
        
        glLineStipple(2, 0xAAAA);
        glEnable(GL_LINE_STIPPLE);
        glBegin(GL_LINES);
        glColor3f(0.8*l, 0.8*l, 0.8*l);
        glVertex2d(p0Normal0.x, p0Normal0.y);
        glVertex2d(p0Normal1.x, p0Normal1.y);
        glVertex2d(p1Normal0.x, p1Normal0.y);
        glVertex2d(p1Normal1.x, p1Normal1.y);
        glEnd();

        double xoffset = 5 * pscale.x;
        double yoffset = 5 * pscale.y;
        TextRenderer::bitmapString(p0.x + xoffset, p0.y + yoffset, kPoint0ParamLabel);
        TextRenderer::bitmapString(p1.x + xoffset, p1.y + yoffset, kPoint1ParamLabel);

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

    return true;
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
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kPoint0Param);
        param->setLabels(kPoint0ParamLabel,kPoint0ParamLabel,kPoint0ParamLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesCanonical);
        param->setDefault(100., 100.);
        page->addChild(*param);
    }
    
    
    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kColor0Param);
        param->setLabels(kColor0ParamLabel, kColor0ParamLabel, kColor0ParamLabel);
        param->setDefault(0, 0, 0, 0);
        page->addChild(*param);
    }

    // point1
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kPoint1Param);
        param->setLabels(kPoint1ParamLabel,kPoint1ParamLabel,kPoint1ParamLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesCanonical);
        param->setDefault(100., 200.);
        page->addChild(*param);
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kColor1Param);
        param->setLabels(kColor1ParamLabel, kColor1ParamLabel, kColor1ParamLabel);
        param->setDefault(1., 1., 1., 1. );
        page->addChild(*param);
    }
    
    // type
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kTypeParam);
        param->setLabels(kTypeParamLabel, kTypeParamLabel, kTypeParamLabel);
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

