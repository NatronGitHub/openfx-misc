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
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "GradeOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Modify the tonal spread of an image from the white and black points. " \
                          "This node can also be used to match colors of 2 images: The darkest and lightest points of " \
                          "the target image are converted to black and white using the blackpoint and whitepoint values. " \
                          "These 2 values are then moved to new values using the black(for dark point) and white(for white point). " \
                          "You can also apply multiply/offset/gamma for other color fixing you may need. " \
                          "Here is the formula used: \n" \
                          "A = multiply * (white - black) / (whitepoint - blackpoint) \n" \
                          "B = offset + black - A * blackpoint \n" \
                          "output = pow(A * input + B, 1 / gamma)."
#define kPluginIdentifier "net.sf.openfx.GradePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamBlackPoint "blackPoint"
#define kParamBlackPointLabel "Black Point"
#define kParamBlackPointHint "Set the color of the darkest pixels in the image"

#define kParamWhitePoint "whitePoint"
#define kParamWhitePointLabel "White Point"
#define kParamWhitePointHint "Set the color of the brightest pixels in the image"

#define kParamBlack "black"
#define kParamBlackLabel "Black"
#define kParamBlackHint "Colors corresponding to the blackpoint are set to this value"

#define kParamWhite "white"
#define kParamWhiteLabel "White"
#define kParamWhiteHint "Colors corresponding to the whitepoint are set to this value"

#define kParamMultiply "multiply"
#define kParamMultiplyLabel "Multiply"
#define kParamMultiplyHint "Multiplies the result by this value"

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "Adds this value to the result (this applies to black and white)"

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Final gamma correction"

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

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

using namespace OFX;


namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };
}

class GradeProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool   _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;

public:
    
    GradeProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _premult(false)
    , _premultChannel(3)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    , _processR(false)
    , _processG(false)
    , _processB(false)
    , _processA(false)
    , _clampBlack(true)
    , _clampWhite(true)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}
    
    void setValues(const RGBAValues& blackPoint,
                   const RGBAValues& whitePoint,
                   const RGBAValues& black,
                   const RGBAValues& white,
                   const RGBAValues& multiply,
                   const RGBAValues& offset,
                   const RGBAValues& gamma,
                   bool clampBlack,
                   bool clampWhite,
                   bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA)
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
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }

    void grade(double* v, double wp, double bp, double white, double black, double mutiply, double offset, double gamma)
    {
        double A = mutiply * (white - black) / (wp - bp);
        double B = offset + black - A * bp;
        *v = std::pow((A * *v) + B, 1. / gamma);
    }
    
    template<bool processR, bool processG, bool processB, bool processA>
    void grade(double *r, double *g, double *b,double *a)
    {
        if (processR) {
            grade(r, _whitePoint.r, _blackPoint.r, _white.r, _black.r, _multiply.r, _offset.r, _gamma.r);
        }
        if (processG) {
            grade(g, _whitePoint.g, _blackPoint.g, _white.g, _black.g, _multiply.g, _offset.g, _gamma.g);
        }
        if (processB) {
            grade(b, _whitePoint.b, _blackPoint.b, _white.b, _black.b, _multiply.b, _offset.b, _gamma.b);
        }
        if (processA) {
            grade(a, _whitePoint.a, _blackPoint.a, _white.a, _black.a, _multiply.a, _offset.a, _gamma.a);
        }
        if (_clampBlack) {
            if (processR) {
                *r = std::max(0.,*r);
            }
            if (processG) {
                *g = std::max(0.,*g);
            }
            if (processB) {
                *b = std::max(0.,*b);
            }
            if (processA) {
                *a = std::max(0.,*a);
            }
        }
        if (_clampWhite) {
            if (processR) {
                *r = std::min(1.,*r);
            }
            if (processG) {
                *g = std::min(1.,*g);
            }
            if (processB) {
                *b = std::min(1.,*b);
            }
            if (processA) {
                *a = std::min(1.,*a);
            }
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
public:
    GradeProcessor(OFX::ImageEffect &instance)
    : GradeProcessorBase(instance)
    {
    }
    
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
    

private:
    
    template<bool processR, bool processG, bool processB, bool processA>
    void process(OfxRectI procWindow)
    {
        assert((!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4));
        assert(!processA || (nComponents == 1 || nComponents == 4));
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
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
                double t_a = unpPix[3];
                grade<processR,processG,processB,processA>(&t_r,&t_g,&t_b,&t_a);
                tmpPix[0] = (float)t_r;
                tmpPix[1] = (float)t_g;
                tmpPix[2] = (float)t_b;
                tmpPix[3] = (float)t_a;
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class GradePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    GradePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _blackPoint = fetchRGBAParam(kParamBlackPoint);
        _whitePoint = fetchRGBAParam(kParamWhitePoint);
        _black = fetchRGBAParam(kParamBlack);
        _white = fetchRGBAParam(kParamWhite);
        _multiply = fetchRGBAParam(kParamMultiply);
        _offset = fetchRGBAParam(kParamOffset);
        _gamma = fetchRGBAParam(kParamGamma);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_blackPoint && _whitePoint && _black && _white && _multiply && _offset && _gamma && _clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);

    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(GradeProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    OFX::RGBAParam* _blackPoint;
    OFX::RGBAParam* _whitePoint;
    OFX::RGBAParam* _black;
    OFX::RGBAParam* _white;
    OFX::RGBAParam* _multiply;
    OFX::RGBAParam* _offset;
    OFX::RGBAParam* _gamma;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
GradePlugin::setupAndProcess(GradeProcessorBase &processor, const OFX::RenderArguments &args)
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
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
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
    std::auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) ?
                                         _maskClip->fetchImage(args.time) : 0);
    if (getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) {
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
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    RGBAValues blackPoint, whitePoint, black, white, multiply, offset, gamma;
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
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    
    bool processR, processG, processB, processA;
    _processR->getValue(processR);
    _processG->getValue(processG);
    _processB->getValue(processB);
    _processA->getValue(processA);
    
    processor.setValues(blackPoint, whitePoint, black, white, multiply, offset, gamma,
                        clampBlack, clampWhite, premult, premultChannel, mix,
                        processR, processG, processB, processA);
    processor.process();
}

// the overridden render function
void
GradePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                GradeProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                GradeProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                GradeProcessor<float, 4, 1> fred(*this);
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
                GradeProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                GradeProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                GradeProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


bool
GradePlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;
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
        identityClip = _srcClip;
        return true;
    }

    bool clampBlack,clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }
    RGBAValues blackPoint,whitePoint,black,white,multiply,offset,gamma;
    _blackPoint->getValueAtTime(args.time, blackPoint.r, blackPoint.g, blackPoint.b, blackPoint.a);
    _whitePoint->getValueAtTime(args.time, whitePoint.r, whitePoint.g, whitePoint.b, whitePoint.a);
    _black->getValueAtTime(args.time, black.r, black.g, black.b, black.a);
    _white->getValueAtTime(args.time, white.r, white.g, white.b, white.a);
    _multiply->getValueAtTime(args.time, multiply.r, multiply.g, multiply.b, multiply.a);
    _offset->getValueAtTime(args.time, offset.r, offset.g, offset.b, offset.a);
    _gamma->getValueAtTime(args.time, gamma.r, gamma.g, gamma.b, gamma.a);
    if (blackPoint.r == 0. && blackPoint.g == 0. && blackPoint.b == 0. && blackPoint.a == 0. &&
        whitePoint.r == 1. && whitePoint.g == 1. && whitePoint.b == 1. && whitePoint.a == 1. &&
        black.r == 0. && black.g == 0. && black.b == 0. && black.a == 0. &&
        white.r == 1. && white.g == 1. && white.b == 1. && white.a == 1. &&
        multiply.r == 1. && multiply.g == 1. && multiply.b == 1. && multiply.a == 1. &&
        offset.r == 0. && offset.g == 0. && offset.b == 0. && offset.a == 0. &&
        gamma.r == 1. && gamma.g == 1. && gamma.b == 1. && gamma.a == 1) {
        identityClip = _srcClip;
        return true;
    }
    return false;
}

void
GradePlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        switch (_srcClip->getPreMultiplication()) {
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

mDeclarePluginFactory(GradePluginFactory, {}, {});

void
GradePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
}

static
void defineRGBAScaleParam(OFX::ImageEffectDescriptor &desc,
                          const std::string &name, const std::string &label, const std::string &hint,
                          PageParamDescriptor* page,double def , double min,double max)
{
    RGBAParamDescriptor *param = desc.defineRGBAParam(name);
    param->setLabel(label);
    param->setHint(hint);
    param->setDefault(def,def,def,def);
    param->setDisplayRange(min,min,min,min,max,max,max,max);
    page->addChild(*param);
}


void
GradePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
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
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        page->addChild(*param);
    }
    
    
    defineRGBAScaleParam(desc, kParamBlackPoint, kParamBlackPointLabel, kParamBlackPointHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamWhitePoint, kParamWhitePointLabel, kParamWhitePointHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamBlack, kParamBlackLabel, kParamBlackHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamWhite, kParamWhiteLabel, kParamWhiteHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamMultiply, kParamMultiplyLabel, kParamMultiplyHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamOffset, kParamOffsetLabel, kParamOffsetHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamGamma, kParamGammaLabel, kParamGammaHint, page, 1., 0.2, 5.);

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabel(kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }
    
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect*
GradePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new GradePlugin(handle);
}

void getGradePluginID(OFX::PluginFactoryArray &ids)
{
    static GradePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

