/*
 OFX Clamp plugin.

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
 OFX Invert Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2007 The Open Effects Association Ltd
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

#include "Clamp.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"


#define kPluginName "ClampOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Clamp the values of the selected channels"
#define kPluginIdentifier "net.sf.openfx.Clamp"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Clamp red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Clamp green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Clamp blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Clamp alpha component"

#define kParamMinimum "minimum"
#define kParamMinimumLabel "Minimum"
#define kParamMinimumHint "If enabled, all values that are lower than this number are set to this value, or to the minClampTo value if minClampTo is enabled."

#define kParamMinimumEnable "minimumEnable"
#define kParamMinimumEnableLabel "Enable Minimum"
#define kParamMinimumEnableHint "Whether to clamp selected channels to a minimum value."

#define kParamMaximum "maximum"
#define kParamMaximumLabel "Maximum"
#define kParamMaximumHint "If enabled, all values that are higher than this number are set to this value, or to the maxClampTo value if maxClampTo is enabled."

#define kParamMaximumEnable "maximumEnable"
#define kParamMaximumEnableLabel "Enable Maximum"
#define kParamMaximumEnableHint "Whether to clamp selected channels to a maximum value."

#define kParamMinClampTo "minClampTo"
#define kParamMinClampToLabel "MinClampTo"
#define kParamMinClampToHint "The value to which values below minimum are clamped when minClampTo is enabled. Setting this to a custom color helps visualizing the clamped areas or create graphic effects."

#define kParamMinClampToEnable "minClampToEnable"
#define kParamMinClampToEnableLabel "Enable MinClampTo"
#define kParamMinClampToEnableHint "When enabled, all values below minimum are set to the minClampTo value.\nWhen disabled, all values below minimum are clamped to the minimum value."

#define kParamMaxClampTo "maxClampTo"
#define kParamMaxClampToLabel "MaxClampTo"
#define kParamMaxClampToHint "The value to which values above maximum are clamped when maxClampTo is enabled. Setting this to a custom color helps visualizing the clamped areas or create graphic effects."

#define kParamMaxClampToEnable "maxClampToEnable"
#define kParamMaxClampToEnableLabel "Enable MaxClampTo"
#define kParamMaxClampToEnableHint "When enabled, all values above maximum are set to the maxClampTo value.\nWhen disabled, all values above maximum are clamped to the maximum value."

using namespace OFX;

namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };
}

// Base class for the RGBA and the Alpha processor
class ClampBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    RGBAValues _minimum;
    bool _minimumEnable;
    RGBAValues _maximum;
    bool _maximumEnable;
    RGBAValues _minClampTo;
    bool _minClampToEnable;
    RGBAValues _maxClampTo;
    bool _maxClampToEnable;
    bool   _doMasking;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    /** @brief no arg ctor */
    ClampBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _processR(true)
    , _processG(true)
    , _processB(true)
    , _processA(false)
    , _minimum(0.)
    , _minimumEnable(true)
    , _maximum(1.)
    , _maximumEnable(true)
    , _minClampTo(0.)
    , _minClampToEnable(false)
    , _maxClampTo(1.)
    , _maxClampToEnable(false)
    , _doMasking(false)
    , _premult(false)
    , _premultChannel(3)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const RGBAValues& minimum,
                   bool minimumEnable,
                   const RGBAValues& maximum,
                   bool maximumEnable,
                   const RGBAValues& minClampTo,
                   bool minClampToEnable,
                   const RGBAValues& maxClampTo,
                   bool maxClampToEnable,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _minimum = minimum;
        _minimumEnable = minimumEnable;
        _maximum = maximum;
        _maximumEnable = maximumEnable;
        _minClampTo = minClampTo;
        _minClampToEnable = minClampToEnable;
        _maxClampTo = maxClampTo;
        _maxClampToEnable = maxClampToEnable;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class ImageClamper : public ClampBase
{
  public:
    // ctor
    ImageClamper(OFX::ImageEffect &instance)
            : ClampBase(instance)
    {
    }

  private:
    // and do some processing
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
    void process(const OfxRectI& procWindow)
    {
        if (_minimumEnable) {
            if (_maximumEnable) {
                processClamp<processR, processG, processB, processA, true, true>(procWindow);
            } else {
                processClamp<processR, processG, processB, processA, true, false>(procWindow);
            }
        } else {
            if (_maximumEnable) {
                processClamp<processR, processG, processB, processA, false, true>(procWindow);
            } else {
                processClamp<processR, processG, processB, processA, false, false>(procWindow);
            }
        }
    }

    template<bool processR, bool processG, bool processB, bool processA, bool minimumEnable, bool maximumEnable>
    void processClamp(const OfxRectI& procWindow)
    {
        if (minimumEnable && _minClampToEnable) {
            if (maximumEnable && _maxClampToEnable) {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, true, true>(procWindow);
            } else {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, true, false>(procWindow);
            }
        } else {
            if (maximumEnable && _maxClampToEnable) {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, false, true>(procWindow);
            } else {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, false, false>(procWindow);
            }
        }

    }

    template<bool processR, bool processG, bool processB, bool processA, bool minimumEnable, bool maximumEnable, bool minClampToEnable, bool maxClampToEnable>
    void processClampTo(const OfxRectI& procWindow)
    {
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {

                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                if (!processR) {
                    tmpPix[0] = unpPix[0];
                } else {
                    tmpPix[0] = clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[0],
                                                                                                        _minimum.r, _maximum.r,
                                                                                                        _minClampTo.r, _maxClampTo.r);
                }
                if (!processG) {
                    tmpPix[1] = unpPix[1];
                } else {
                    tmpPix[1] = clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[1],
                                                                                                        _minimum.g, _maximum.g,
                                                                                                        _minClampTo.g, _maxClampTo.g);
                }
                if (!processB) {
                    tmpPix[2] = unpPix[2];
                } else {
                    tmpPix[2] = clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[2],
                                                                                                        _minimum.b, _maximum.b,
                                                                                                        _minClampTo.b, _maxClampTo.b);
                }
                if (!processA) {
                    tmpPix[3] = unpPix[3];
                } else {
                    tmpPix[3] = clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[3],
                                                                                                        _minimum.a, _maximum.a,
                                                                                                        _minClampTo.a, _maxClampTo.a);
                }
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }

    template<bool minimumEnable, bool maximumEnable, bool minClampToEnable, bool maxClampToEnable>
    static double clamp(double value, double minimum, double maximum, double minClampTo, double maxClampTo)
    {
        if (minimumEnable && value < minimum) {
            return minClampToEnable ? minClampTo : minimum;
        }
        if (maximumEnable && value > maximum) {
            return maxClampToEnable ? maxClampTo : maximum;
        }
        return value;
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ClampPlugin : public OFX::ImageEffect
{
  public:
    /** @brief ctor */
    ClampPlugin(OfxImageEffectHandle handle)
            : ImageEffect(handle)
            , dstClip_(0)
            , srcClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA || srcClip_->getPixelComponents() == ePixelComponentAlpha));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _minimum = fetchRGBAParam(kParamMinimum);
        _minimumEnable = fetchBooleanParam(kParamMinimumEnable);
        assert(_minimum && _minimumEnable);
        _maximum = fetchRGBAParam(kParamMaximum);
        _maximumEnable = fetchBooleanParam(kParamMaximumEnable);
        assert(_maximum && _maximumEnable);
        _minClampTo = fetchRGBAParam(kParamMinClampTo);
        _minClampToEnable = fetchBooleanParam(kParamMinClampToEnable);
        assert(_minClampTo && _minClampToEnable);
        _maxClampTo = fetchRGBAParam(kParamMaxClampTo);
        _maxClampToEnable = fetchBooleanParam(kParamMaxClampToEnable);
        assert(_maxClampTo && _maxClampToEnable);
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
    void setupAndProcess(ClampBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

  private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::RGBAParam* _minimum;
    OFX::BooleanParam* _minimumEnable;
    OFX::RGBAParam* _maximum;
    OFX::BooleanParam* _maximumEnable;
    OFX::RGBAParam* _minClampTo;
    OFX::BooleanParam* _minClampToEnable;
    OFX::RGBAParam* _maxClampTo;
    OFX::BooleanParam* _maxClampToEnable;
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
ClampPlugin::setupAndProcess(ClampBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

    // fetch main input image
    std::auto_ptr<const OFX::Image> src(srcClip_->fetchImage(args.time));

    // make sure bit depths are sane
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((getContext() != OFX::eContextFilter) ? maskClip_->fetchImage(args.time) : 0);

    // do we do masking
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    RGBAValues minimum(0.);
    _minimum->getValueAtTime(args.time, minimum.r, minimum.g, minimum.b, minimum.a);
    bool minimumEnable;
    _minimumEnable->getValueAtTime(args.time, minimumEnable);
    RGBAValues maximum(1.);
    _maximum->getValueAtTime(args.time, maximum.r, maximum.g, maximum.b, maximum.a);
    bool maximumEnable;
    _maximumEnable->getValueAtTime(args.time, maximumEnable);
    RGBAValues minClampTo(0.);
    _minClampTo->getValueAtTime(args.time, minClampTo.r, minClampTo.g, minClampTo.b, minClampTo.a);
    bool minClampToEnable;
    _minClampToEnable->getValueAtTime(args.time, minClampToEnable);
    RGBAValues maxClampTo(1.);
    _maxClampTo->getValueAtTime(args.time, maxClampTo.r, maxClampTo.g, maxClampTo.b, maxClampTo.a);
    bool maxClampToEnable;
    _maxClampToEnable->getValueAtTime(args.time, maxClampToEnable);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(processR, processG, processB, processA,
                        minimum, minimumEnable, maximum, maximumEnable,
                        minClampTo, minClampToEnable, maxClampTo, maxClampToEnable,
                        premult, premultChannel, mix);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
ClampPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageClamper<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageClamper<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageClamper<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageClamper<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageClamper<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageClamper<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageClamper<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageClamper<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageClamper<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

bool
ClampPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    bool red, green, blue, alpha;
    double mix;
    _processR->getValueAtTime(args.time, red);
    _processG->getValueAtTime(args.time, green);
    _processB->getValueAtTime(args.time, blue);
    _processA->getValueAtTime(args.time, alpha);
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0. || (!red && !green && !blue && !alpha)) {
        identityClip = srcClip_;
        return true;
    }

    bool minimumEnable, maximumEnable;
    _minimumEnable->getValueAtTime(args.time, minimumEnable);
    _maximumEnable->getValueAtTime(args.time, maximumEnable);

    if (!minimumEnable && !maximumEnable) {
        identityClip = srcClip_;
        return true;
    }

    return false;
}

void
ClampPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
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

mDeclarePluginFactory(ClampPluginFactory, {}, {});

using namespace OFX;
void ClampPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);

    // add supported pixel depths
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

void ClampPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
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

    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMinimum);
        param->setLabels(kParamMinimumLabel, kParamMinimumLabel, kParamMinimumLabel);
        param->setHint(kParamMinimumHint);
        param->setDefault(0., 0., 0., 0.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMinimumEnable);
        param->setLabels(kParamMinimumEnableLabel, kParamMinimumEnableLabel, kParamMinimumEnableLabel);
        param->setHint(kParamMinimumEnableHint);
        param->setDefault(true);
        page->addChild(*param);
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMaximum);
        param->setLabels(kParamMaximumLabel, kParamMaximumLabel, kParamMaximumLabel);
        param->setHint(kParamMaximumHint);
        param->setDefault(1., 1., 1., 1.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMaximumEnable);
        param->setLabels(kParamMaximumEnableLabel, kParamMaximumEnableLabel, kParamMaximumEnableLabel);
        param->setHint(kParamMaximumEnableHint);
        param->setDefault(true);
        page->addChild(*param);
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMinClampTo);
        param->setLabels(kParamMinClampToLabel, kParamMinClampToLabel, kParamMinClampToLabel);
        param->setHint(kParamMinClampToHint);
        param->setDefault(0., 0., 0., 0.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMinClampToEnable);
        param->setLabels(kParamMinClampToEnableLabel, kParamMinClampToEnableLabel, kParamMinClampToEnableLabel);
        param->setHint(kParamMinClampToEnableHint);
        param->setDefault(false);
        page->addChild(*param);
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMaxClampTo);
        param->setLabels(kParamMaxClampToLabel, kParamMaxClampToLabel, kParamMaxClampToLabel);
        param->setHint(kParamMaxClampToHint);
        param->setDefault(1., 1., 1., 1.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMaxClampToEnable);
        param->setLabels(kParamMaxClampToEnableLabel, kParamMaxClampToEnableLabel, kParamMaxClampToEnableLabel);
        param->setHint(kParamMaxClampToEnableHint);
        param->setDefault(false);
        page->addChild(*param);
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* ClampPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ClampPlugin(handle);
}


void getClampPluginID(OFX::PluginFactoryArray &ids)
{
    static ClampPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

