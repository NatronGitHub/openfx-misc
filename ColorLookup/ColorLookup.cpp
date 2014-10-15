/*
 OFX ColorLookup plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2013 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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

#include "ColorLookup.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

#define kPluginName "ColorLookupOFX"
#define kPluginGrouping "Color"
#define kPluginDescription \
"Apply a parametric lookup curve to each channel separately.\n" \
"The master curve is combined with the red, green and blue curves, but not with the alpha curve.\n" \
"Computation is faster for values that are within the given range."
#define kPluginIdentifier "net.sf.openfx.ColorLookupPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamLookupTable "lookupTable"
#define kParamLookupTableLabel "Lookup Table"
#define kParamLookupTableHint "Colour lookup table. The master curve is combined with the red, green and blue curves, but not with the alpha curve."

#define kParamSource "source"
#define kParamSourceLabel "Source"
#define kParamSourceHint "Source color for newly added points (x coordinate on the curve)."

#define kParamTarget "target"
#define kParamTargetLabel "Target"
#define kParamTargetHint "Target color for newly added points (y coordinate on the curve)."

#define kParamSetMaster "setMaster"
#define kParamSetMasterLabel "Set Master"
#define kParamSetMasterHint "Add a new control point mapping source to target to the master curve (the relative luminance 0.2126 R + 0.7152 G + 0.0722 B is used)."

#define kParamSetRGB "setRGB"
#define kParamSetRGBLabel "Set RGB"
#define kParamSetRGBHint "Add a new control point mapping source to target to the red, green, and blue curves."

#define kParamSetRGBA "setRGBA"
#define kParamSetRGBALabel "Set RGBA"
#define kParamSetRGBAHint "Add a new control point mapping source to target to the red, green, blue and alpha curves."

#define kParamSetA "setA"
#define kParamSetALabel "Set A"
#define kParamSetAHint "Add a new control point mapping source to target to the alpha curve"

#ifdef COLORLOOKUP_ADD
#define kParamAddCtrlPts "addCtrlPts"
#define kParamAddCtrlPtsLabel "Add Control Points"
#endif

#ifdef COLORLOOKUP_RESET
#define kParamResetCtrlPts "resetCtrlPts"
#define kParamResetCtrlPtsLabel "Reset"
#endif

#define kParamRange "range"
#define kParamRangeLabel "Range"
#define kParamRangeHint "Expected range for input values. Within this range, a lookup table is used for faster computation."

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kCurveMaster 0
#define kCurveRed 1
#define kCurveGreen 2
#define kCurveBlue 3
#define kCurveAlpha 4
#define kCurveNb 5

using namespace OFX;

class ColorLookupProcessorBase : public OFX::ImageProcessor {
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool   _doMasking;
    bool _clampBlack;
    bool _clampWhite;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    ColorLookupProcessorBase(OFX::ImageEffect &instance, bool clampBlack, bool clampWhite)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _doMasking(false)
    , _clampBlack(clampBlack)
    , _clampWhite(clampWhite)
    , _premult(false)
    , _premultChannel(3)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool premult,
                   int premultChannel,
                   double mix)
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }

protected:
    // clamp for integer types
    template<class PIX>
    float clamp(float value, int maxValue)
    {
        return std::max(0.f, std::min(value, float(maxValue)));
    }
};


// floats don't clamp
template<>
float ColorLookupProcessorBase::clamp<float>(float value, int maxValue)
{
    assert(maxValue == 1.);
    if (_clampBlack && value < 0.) {
        value = 0.;
    } else  if (_clampWhite && value > 1.0) {
        value = 1.0;
    }
    return value;
}

static inline int
componentToCurve(int comp)
{
    switch (comp) {
        case 0:
            return kCurveRed;
        case 1:
            return kCurveGreen;
        case 2:
            return kCurveBlue;
        case 3:
            return kCurveAlpha;
        default:
            return 0;
    }
}



// template to do the processing.
// nbValues is the number of values in the LUT minus 1. For integer types, it should be the same as
// maxValue
template <class PIX, int nComponents, int maxValue, int nbValues>
class ColorLookupProcessor : public ColorLookupProcessorBase
{
public:
    // ctor
    ColorLookupProcessor(OFX::ImageEffect &instance, const OFX::RenderArguments &args, OFX::ParametricParam  *lookupTableParam, double rangeMin, double rangeMax, bool clampBlack, bool clampWhite)
    : ColorLookupProcessorBase(instance, clampBlack, clampWhite)
    , _lookupTableParam(lookupTableParam)
    , _rangeMin(std::min(rangeMin,rangeMax))
    , _rangeMax(std::max(rangeMin,rangeMax))
    {
        // build the LUT
        assert(_lookupTableParam);
        _time = args.time;
        if (_rangeMin == _rangeMax) {
            // avoid divisions by zero
            _rangeMax = _rangeMin + 1.;
        }
        assert((PIX)maxValue == maxValue);
        // except for float, maxValue is the same as nbValues
        assert(maxValue == 1 || (maxValue == nbValues));
        for (int component = 0; component < nComponents; ++component) {
            _lookupTable[component].resize(nbValues+1);
            int lutIndex = nComponents == 1 ? kCurveAlpha : componentToCurve(component); // special case for components == alpha only
            bool applyMaster = lutIndex != kCurveAlpha;
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                float parametricPos = _rangeMin + (_rangeMax - _rangeMin) * float(position)/nbValues;

                // evaluate the parametric param
                double value = _lookupTableParam->getValue(lutIndex, _time, parametricPos);
                if (applyMaster) {
                    value += _lookupTableParam->getValue(kCurveMaster, _time, parametricPos) - parametricPos;
                }
                // set that in the lut
                _lookupTable[component][position] = clamp<PIX>(value, maxValue);
            }
        }
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float tmpPix[nComponents];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++)  {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                if (nComponents == 1 || nComponents == 3) {
                    // RGB and Alpha: don't premult/unpremult, just apply curves
                    // normalize/denormalize properly
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = interpolate(c, srcPix[c] / (double)maxValue) * maxValue;
                    }
                    // ofxsMaskMix expects denormalized input
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                } else {
                    //assert(nComponents == 4);
                    float unpPix[nComponents];
                    ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                    // ofxsUnPremult outputs normalized data
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = interpolate(c, unpPix[c]);
                    }
                    // ofxsPremultMaskMixPix expects normalized input
                    ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }

    // on input to interpolate, value should be normalized to the [0-1] range
    float interpolate(int component, float value) {
        if (value < _rangeMin || _rangeMax < value) {
            // slow version
            int lutIndex = nComponents == 1 ? kCurveAlpha : componentToCurve(component); // special case for components == alpha only
            bool applyMaster = lutIndex != kCurveAlpha;
            double ret = _lookupTableParam->getValue(lutIndex, _time, value);
            if (applyMaster) {
                ret += _lookupTableParam->getValue(kCurveMaster, _time, value) - value;
            }
            return ret;
        } else {
            double x = (value - _rangeMin) / (_rangeMax - _rangeMin);
            int i = (int)(x * nbValues);
            assert(0 <= i && i <= nbValues);
            float alpha = x * nbValues - i;
            assert(0 <= alpha && alpha < 1.);
            return _lookupTable[component][i] * (1.-alpha) + _lookupTable[component][i+1] * alpha;
        }
    }

private:
    std::vector<float> _lookupTable[nComponents];
    OFX::ParametricParam*  _lookupTableParam;
    double _time;
    double _rangeMin;
    double _rangeMax;
};

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorLookupPlugin : public OFX::ImageEffect
{
public:
    ColorLookupPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));

        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _lookupTable = fetchParametricParam(kParamLookupTable);
        _range = fetchDouble2DParam(kParamRange);
        assert(_lookupTable && _range);
        _source = fetchRGBAParam(kParamSource);
        _target = fetchRGBAParam(kParamTarget);
        assert(_source && _target);
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
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    void setupAndProcess(ColorLookupProcessorBase &, const OFX::RenderArguments &args);
    void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
    {
        if (paramName == kParamSetMaster) {
            double source[4];
            double target[4];
            _source->getValueAtTime(args.time, source[0], source[1], source[2], source[3]);
            _target->getValueAtTime(args.time, target[0], target[1], target[2], target[3]);

            double s = 0.2126 * source[0] + 0.7152 * source[1] + 0.0722 *source[2];
            double t = 0.2126 * target[0] + 0.7152 * target[1] + 0.0722 *target[2];
            _lookupTable->addControlPoint(kCurveMaster, // curve to set
                                          args.time,   // time, ignored in this case, as we are not adding a key
                                          s,   // parametric position
                                          t,   // value to be
                                          false);   // don't add a key
        }
        if (paramName == kParamSetRGB || paramName == kParamSetRGBA || paramName == kParamSetA) {
            double source[4];
            double target[4];
            _source->getValueAtTime(args.time, source[0], source[1], source[2], source[3]);
            _target->getValueAtTime(args.time, target[0], target[1], target[2], target[3]);

            int cbegin = (paramName == kParamSetA) ? 3 : 0;
            int cend = (paramName == kParamSetRGB) ? 3 : 4;
            for (int c = cbegin; c < cend; ++c) {
                int curve = componentToCurve(c);
                _lookupTable->addControlPoint(curve, // curve to set
                                              args.time,   // time, ignored in this case, as we are not adding a key
                                              source[c],   // parametric position
                                              target[c],   // value to be
                                              false);   // don't add a key
            }
        }
#ifdef COLORLOOKUP_ADD
        if (paramName == kParamAddCtrlPts) {
            for (int component = 0; component < kCurveNb; ++component) {
                int n = _lookupTable->getNControlPoints(component, args.time);
                if (n <= 1) {
                    // less than two points: add the two default control points
                    // add a control point at 0, value is 0
                    _lookupTable->addControlPoint(component, // curve to set
                                                 args.time,   // time, ignored in this case, as we are not adding a key
                                                 0.0,   // parametric position, zero
                                                 0.0,   // value to be, 0
                                                 false);   // don't add a key
                    // add a control point at 1, value is 1
                    _lookupTable->addControlPoint(component, args.time, 1.0, 1.0, false);
                } else {
                    std::pair<double, double> prev = _lookupTable->getNthControlPoint(component, args.time, 0);
                    std::list<std::pair<double, double> > newCtrlPts;

                    // compute new points, put them in a list
                    for (int i = 1; i < n; ++i) {
                        std::pair<double, double> next = _lookupTable->getNthControlPoint(component, args.time, i);
                        if (prev.first != next.first) { // don't create additional points if there is no space for one
                            // create a new control point between two existing control points
                            double parametricPos = (prev.first + next.first)/2.;
                            double parametricVal = _lookupTable->getValue(component, args.time, parametricPos);
                            newCtrlPts.push_back(std::make_pair(parametricPos, parametricVal));
                        }
                        prev = next;
                    }
                    // now add the new points
                    for (std::list<std::pair<double, double> >::const_iterator it = newCtrlPts.begin();
                         it != newCtrlPts.end();
                         ++it) {
                        _lookupTable->addControlPoint(component, // curve to set
                                                      args.time,   // time, ignored in this case, as we are not adding a key
                                                      it->first,   // parametric position
                                                      it->second,   // value to be, 0
                                                      false);
                    }
                }
            }
        }
#endif
#ifdef COLORLOOKUP_RESET
        if (paramName == kParamResetCtrlPts) {
            OFX::Message::MessageReplyEnum reply = sendMessage(OFX::Message::eMessageQuestion, "", "Delete all control points for all components?");
            // Nuke seems to always reply eMessageReplyOK, whatever the real answer was
            switch (reply) {
                case OFX::Message::eMessageReplyOK:
                    sendMessage(OFX::Message::eMessageMessage, "","OK");
                    break;
                case OFX::Message::eMessageReplyYes:
                    sendMessage(OFX::Message::eMessageMessage, "","Yes");
                    break;
                case OFX::Message::eMessageReplyNo:
                    sendMessage(OFX::Message::eMessageMessage, "","No");
                    break;
                case OFX::Message::eMessageReplyFailed:
                    sendMessage(OFX::Message::eMessageMessage, "","Failed");
                    break;
            }
            if (reply == OFX::Message::eMessageReplyYes) {
                for (int component = 0; component < kCurveNb; ++component) {
                    _lookupTable->deleteControlPoint(component);
                    // add a control point at 0, value is 0
                    _lookupTable->addControlPoint(component, // curve to set
                                                 args.time,   // time, ignored in this case, as we are not adding a key
                                                 0.0,   // parametric position, zero
                                                 0.0,   // value to be, 0
                                                 false);   // don't add a key
                    // add a control point at 1, value is 1
                    lookupTable->addControlPoint(component, args.time, 1.0, 1.0, false);
                }
            }
        }
#endif
    }

private:
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    OFX::ParametricParam  *_lookupTable;
    OFX::Double2DParam* _range;
    OFX::RGBAParam* _source;
    OFX::RGBAParam* _target;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


void
ColorLookupPlugin::setupAndProcess(ColorLookupProcessorBase &processor,
                              const OFX::RenderArguments &args)
{
    assert(dstClip_);
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

    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(premult, premultChannel, mix);
    processor.process();
}

// the internal render function
template <int nComponents>
void
ColorLookupPlugin::renderForComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    double rangeMin, rangeMax;
    bool clampBlack, clampWhite;
    _range->getValueAtTime(args.time, rangeMin, rangeMax);
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    switch(dstBitDepth) {
        case OFX::eBitDepthUByte: {
            ColorLookupProcessor<unsigned char, nComponents, 255, 255> fred(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthUShort: {
            ColorLookupProcessor<unsigned short, nComponents, 65535, 65535> fred(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
        }   break;
        case OFX::eBitDepthFloat: {
            ColorLookupProcessor<float, nComponents, 1, 1023> fred(*this, args, _lookupTable, rangeMin, rangeMax, clampBlack, clampWhite);
            setupAndProcess(fred, args);
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
ColorLookupPlugin::render(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args, dstBitDepth);
    }
}

void
ColorLookupPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
        switch (srcClip_->getPreMultiplication()) {
            case eImageOpaque:
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


using namespace OFX;

mDeclarePluginFactory(ColorLookupPluginFactory, {}, {});

void
ColorLookupPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // returning an error here crashes Nuke
    //if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
    //  throwHostMissingSuiteException(kOfxParametricParameterSuite);
    //}
}

void
ColorLookupPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    assert(dstClip);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
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

    // define it
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamRange);
        param->setLabels(kParamRangeLabel, kParamRangeLabel, kParamRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setHint(kParamRangeHint);
        param->setDefault(0., 1.);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamLookupTable);
        assert(param);
        param->setLabels(kParamLookupTableLabel, kParamLookupTableLabel, kParamLookupTableLabel);
        param->setHint(kParamLookupTableHint);

        // define it as three dimensional
        param->setDimension(kCurveNb);

        // label our dimensions are r/g/b
        param->setDimensionLabel("master", kCurveMaster);
        param->setDimensionLabel("red", kCurveRed);
        param->setDimensionLabel("green", kCurveGreen);
        param->setDimensionLabel("blue", kCurveBlue);
        param->setDimensionLabel("alpha", kCurveAlpha);

        // set the UI colour for each dimension
        const OfxRGBColourD master  = {0.9,0.9,0.9};
        // the following are magic colors, they all have the same luminance
        const OfxRGBColourD red   = {0.711519527404004, 0.164533420851110, 0.164533420851110};		//set red color to red curve
        const OfxRGBColourD green = {0., 0.546986106552894, 0.};		//set green color to green curve
        const OfxRGBColourD blue  = {0.288480472595996, 0.288480472595996, 0.835466579148890};		//set blue color to blue curve
        const OfxRGBColourD alpha  = {0.398979,0.398979,0.398979};
        param->setUIColour( kCurveRed, red );
        param->setUIColour( kCurveGreen, green );
        param->setUIColour( kCurveBlue, blue );
        param->setUIColour( kCurveAlpha, alpha );
        param->setUIColour( kCurveMaster, master );

        // set the min/max parametric range to 0..1
        param->setRange(0.0, 1.0);

        /*
         // set a default curve, this example sets identity
         for (int component = 0; component < 3; ++component) {
         // add a control point at 0, value is 0
         param->addControlPoint(component, // curve to set
         0.0,   // time, ignored in this case, as we are not adding a key
         0.0,   // parametric position, zero
         0.0,   // value to be, 0
         false);   // don't add a key
         // add a control point at 1, value is 1
         param->addControlPoint(component, 0.0, 1.0, 1.0, false);
         }
         */
        param->setIdentity();

        page->addChild(*param);
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamSource);
        param->setLabels(kParamSourceLabel, kParamSourceLabel, kParamSourceLabel);
        param->setHint(kParamSourceHint);
        param->setEvaluateOnChange(false);
        param->setIsPersistant(false);
        page->addChild(*param);
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamTarget);
        param->setLabels(kParamTargetLabel, kParamTargetLabel, kParamTargetLabel);
        param->setHint(kParamTargetHint);
        param->setEvaluateOnChange(false);
        param->setIsPersistant(false);
        page->addChild(*param);
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetMaster);
        param->setLabels(kParamSetMasterLabel, kParamSetMasterLabel, kParamSetMasterLabel);
        param->setHint(kParamSetMasterHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGB);
        param->setLabels(kParamSetRGBLabel, kParamSetRGBLabel, kParamSetRGBLabel);
        param->setHint(kParamSetRGBHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGBA);
        param->setLabels(kParamSetRGBALabel, kParamSetRGBALabel, kParamSetRGBALabel);
        param->setHint(kParamSetRGBAHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        page->addChild(*param);
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetA);
        param->setLabels(kParamSetALabel, kParamSetALabel, kParamSetALabel);
        param->setHint(kParamSetAHint);
        page->addChild(*param);
    }
#ifdef COLORLOOKUP_ADD
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamAddCtrlPts);
        param->setLabels(kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel);
        page->addChild(*param);
    }
#endif
#ifdef COLORLOOKUP_RESET
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetCtrlPts);
        param->setLabels(kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel);
        page->addChild(*param);
    }
#endif
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

OFX::ImageEffect*
ColorLookupPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ColorLookupPlugin(handle);
}

void getColorLookupPluginID(OFX::PluginFactoryArray &ids)
{
    static ColorLookupPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

