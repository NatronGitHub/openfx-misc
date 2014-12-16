/*
 OFX Rectangle plugin.
 
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
#include "Rectangle.h"

#include <cmath>
#include <climits>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMaskMix.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#define POINT_TOLERANCE 6
#define POINT_SIZE 5


#define kPluginName "RectangleOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription \
"Draw a rectangle.\n" \
"The rectangle is composited with the source image using the 'over' operator."
#define kPluginIdentifier "net.sf.openfx.Rectangle"
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

#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Softness of the rectangle edges. 0 is a hard edge."

#define kParamColor0 "color0"
#define kParamColor0Label "Color 0"

#define kParamColor1 "color1"
#define kParamColor1Label "Color 1"


namespace {
    struct RGBAValues {
        double r,g,b,a;
    };
}

static inline
double
rampSmooth(double t)
{
    t *= 2.;
    if (t < 1) {
        return t * t / (2.);
    } else {
        t -= 1.;
        return -0.5 * (t * (t - 2) - 1);
    }
}

using namespace OFX;

class RectangleProcessorBase : public OFX::ImageProcessor
{


protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;
    bool _red, _green, _blue, _alpha;

    OfxPointD _btmLeft, _size;
    double _softness;
    RGBAValues _color0, _color1;

public:
    RectangleProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _red(false)
    , _green(false)
    , _blue(false)
    , _softness(0.)
    {
        _btmLeft.x = _btmLeft.y = _size.x = _size.y = 0.;
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

    void setValues(const OfxPointD& btmLeft,
                   const OfxPointD& size,
                   double softness,
                   const RGBAValues& color0,
                   const RGBAValues& color1,
                   double mix,
                   bool red,
                   bool green,
                   bool blue,
                   bool alpha)
    {
        _btmLeft = btmLeft;
        _size = size;
        _softness = std::max(0.,softness);
        _color0 = color0;
        _color1 = color1;
        _mix = mix;
        _red = red;
        _green = green;
        _blue = blue;
        _alpha = alpha;
    }
 };


template <class PIX, int nComponents, int maxValue>
class RectangleProcessor : public RectangleProcessorBase
{
public:
    RectangleProcessor(OFX::ImageEffect &instance)
    : RectangleProcessorBase(instance)
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

        float tmpPix[4];

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

                double dx = std::min(p.x - _btmLeft.x, _btmLeft.x + _size.x - p.x);
                double dy = std::min(p.y - _btmLeft.y, _btmLeft.y + _size.y - p.y);

                if (dx <=0 || dy <= 0) {
                    // outside of the rectangle
                    tmpPix[0] = _color0.r;
                    tmpPix[1] = _color0.g;
                    tmpPix[2] = _color0.b;
                    tmpPix[3] = _color0.a;
                } else if (_softness == 0 || (dx >= _softness && dy >= _softness)) {
                    // inside of the rectangle
                    tmpPix[0] = _color1.r;
                    tmpPix[1] = _color1.g;
                    tmpPix[2] = _color1.b;
                    tmpPix[3] = _color1.a;
                } else {
                    double tx, ty;
                    if (dx >= _softness) {
                        tx = 1.;
                    } else {
                        tx = rampSmooth(dx / _softness);
                    }
                    if (dy >= _softness) {
                        ty = 1.;
                    } else {
                        ty = rampSmooth(dy / _softness);
                    }
                    double t = tx * ty;
                    if (t >= 1) {
                        tmpPix[0] = _color1.r;
                        tmpPix[1] = _color1.g;
                        tmpPix[2] = _color1.b;
                        tmpPix[3] = _color1.a;
                    } else {
                        //if (_plinear) {
                        //    // it seems to be the way Nuke does it... I could understand t*t, but why t*t*t?
                        //    t = t*t*t;
                        //}
                        tmpPix[0] = _color0.r * (1 - t) + _color1.r * t;
                        tmpPix[1] = _color0.g * (1 - t) + _color1.g * t;
                        tmpPix[2] = _color0.b * (1 - t) + _color1.b * t;
                        tmpPix[3] = _color0.a * (1 - t) + _color1.a * t;
                    }
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
                if (dored) {
                    tmpPix[0] = tmpPix[0] + srcPixRGBA[0]*(1.-a);
                } else {
                    tmpPix[0] = srcPixRGBA[0];
                }
                if (dogreen) {
                    tmpPix[1] = tmpPix[1] + srcPixRGBA[1]*(1.-a);
                } else {
                    tmpPix[1] = srcPixRGBA[1];
                }
                if (doblue) {
                    tmpPix[2] = tmpPix[2] + srcPixRGBA[2]*(1.-a);
                } else {
                    tmpPix[2] = srcPixRGBA[2];
                }
                if (doalpha) {
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
class RectanglePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    RectanglePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , _processR(0)
    , _processG(0)
    , _processB(0)
    , _processA(0)
    , _btmLeft(0)
    , _size(0)
    , _color0(0)
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
        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _softness = fetchDoubleParam(kParamSoftness);
        _color0 = fetchRGBAParam(kParamColor0);
        _color1 = fetchRGBAParam(kParamColor1);
        assert(_btmLeft && _size && _softness && _color0 && _color1);

        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* Override the clip preferences */
    void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(RectangleProcessorBase &, const OFX::RenderArguments &args);
    
private:
    
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *dstClip_;
    Clip *srcClip_;
    Clip *maskClip_;

    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    DoubleParam* _softness;
    RGBAParam* _color0;
    RGBAParam* _color1;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
RectanglePlugin::setupAndProcess(RectangleProcessorBase &processor, const OFX::RenderArguments &args)
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

    OfxPointD btmLeft, size;
    _btmLeft->getValueAtTime(args.time, btmLeft.x, btmLeft.y);
    _size->getValueAtTime(args.time, size.x, size.y);

    double softness;
    _softness->getValueAtTime(args.time, softness);

    RGBAValues color0, color1;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    _color1->getValueAtTime(args.time, color1.r, color1.g, color1.b, color1.a);

    bool doR, doG, doB, doA;
    _processR->getValue(doR);
    _processG->getValue(doG);
    _processB->getValue(doB);
    _processA->getValue(doA);

    double mix;
    _mix->getValueAtTime(args.time, mix);

    processor.setValues(btmLeft, size,
                        softness,
                        color0, color1,
                        mix,
                        doR, doG, doB, doA);
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


// the internal render function
template <int nComponents>
void
RectanglePlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth)
    {
        case OFX::eBitDepthUByte :
        {
            RectangleProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort :
        {
            RectangleProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat :
        {
            RectangleProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RectanglePlugin::render(const OFX::RenderArguments &args)
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
RectanglePlugin::isIdentity(const OFX::IsIdentityArguments &args,
                       OFX::Clip * &identityClip,
                       double &/*identityTime*/)
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

    RGBAValues color0, color1;
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
RectanglePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of dstClip_ if alpha is affected and source is Opaque
    bool alpha;
    _processA->getValue(alpha);
    if (alpha && srcClip_->getPreMultiplication() == eImageOpaque) {
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
    }
}

bool
RectanglePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);
    if (mix != 1.) {
        // default region of definition
        return false;
    }
    RGBAValues color0;
    _color0->getValueAtTime(args.time, color0.r, color0.g, color0.b, color0.a);
    if (color0.a != 0.) {
        // default region of definition
        return false;
    }
    OfxPointD btmLeft, size;
    _btmLeft->getValueAtTime(args.time, btmLeft.x, btmLeft.y);
    _size->getValueAtTime(args.time, size.x, size.y);

    rod.x1 = btmLeft.x;
    rod.y1 = btmLeft.y;
    rod.x2 = rod.x1 + size.x;
    rod.y2 = rod.y1 + size.y;

    return true;
}

mDeclarePluginFactory(RectanglePluginFactory, {}, {});

void RectanglePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setOverlayInteractDescriptor(new RectangleOverlayDescriptor);
}



OFX::ImageEffect* RectanglePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new RectanglePlugin(handle);
}




void RectanglePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
    
    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabels(kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0.25, 0.25);
        param->setIncrement(1.);
        param->setHint("Coordinates of the bottom left corner of the effect rectangle");
        param->setDigits(0);
        page->addChild(*param);
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabels(kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0.5, 0.5);
        param->setIncrement(1.);
        param->setDimensionLabels("width", "height");
        param->setHint("Width and height of the effect rectangle");
        param->setDigits(0);
        page->addChild(*param);
    }

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabels(kParamSoftnessLabel, kParamSoftnessLabel, kParamSoftnessLabel);
        param->setHint(kParamSoftnessHint);
        param->setDefault(0.);
        param->setIncrement(0.01);
        param->setRange(0., INT_MAX);
        param->setDisplayRange(0., 100.);
        param->setDigits(2);
        page->addChild(*param);
    }

    // color0
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor0);
        param->setLabels(kParamColor0Label, kParamColor0Label, kParamColor0Label);
        param->setDefault(0, 0, 0, 0);
        page->addChild(*param);
    }

    // color1
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamColor1);
        param->setLabels(kParamColor1Label, kParamColor1Label, kParamColor1Label);
        param->setDefault(1., 1., 1., 1. );
        page->addChild(*param);
    }
    
    ofxsMaskMixDescribeParams(desc, page);
}

void getRectanglePluginID(OFX::PluginFactoryArray &ids)
{
    static RectanglePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

