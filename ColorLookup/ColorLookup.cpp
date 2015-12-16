/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
 *
 * openfx-misc is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-misc is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX ColorLookup plugin
 */

#include "ColorLookup.h"

#include <cmath>
#include <algorithm>
#ifdef _WINDOWS
#include <windows.h>
#define isnan _isnan
#else
using std::isnan;
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
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
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
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
    bool  _doMasking;
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
    // clamp for integer types
    template<class PIX>
    double clamp(double value, int maxValue)
    {
        return std::max(0., std::min(value, double(maxValue)));
    }
};


// floats don't clamp
template<>
float ColorLookupProcessorBase::clamp<float>(float value, int maxValue)
{
    assert(maxValue == 1.);
    if (_clampBlack && value < 0.) {
        value = 0.f;
    } else  if (_clampWhite && value > 1.0) {
        value = 1.0f;
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
            for (int position = 0; position <= nbValues; ++position) {
                // position to evaluate the param at
                double parametricPos = _rangeMin + (_rangeMax - _rangeMin) * double(position)/nbValues;

                // evaluate the parametric param
                double value = _lookupTableParam->getValue(lutIndex, _time, parametricPos);
                if (nComponents != 1 && lutIndex != kCurveAlpha) {
                    value += _lookupTableParam->getValue(kCurveMaster, _time, parametricPos) - parametricPos;
                }
                // set that in the lut
                _lookupTable[component][position] = (float)clamp<PIX>(value, maxValue);
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
                        tmpPix[c] = (float)interpolate(c, srcPix ? (srcPix[c] / (float)maxValue) : 0.f) * maxValue;
                        assert((!srcPix || (!isnan(srcPix[c]) && !isnan(srcPix[c]))) &&
                               !isnan(tmpPix[c]) && !isnan(tmpPix[c]));
                    }
                    // ofxsMaskMix expects denormalized input
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                } else {
                    //assert(nComponents == 4);
                    float unpPix[nComponents];
                    ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                    // ofxsUnPremult outputs normalized data
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = interpolate(c, unpPix[c]);
                        assert(!isnan(unpPix[c]) && !isnan(unpPix[c]) &&
                               !isnan(tmpPix[c]) && !isnan(tmpPix[c]));
                    }
                    // ofxsPremultMaskMixPix expects normalized input
                    ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
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
            double ret = _lookupTableParam->getValue(lutIndex, _time, value);
            if (nComponents != 1 && lutIndex != kCurveAlpha) {
                ret += _lookupTableParam->getValue(kCurveMaster, _time, value) - value;
            }
            return (float)clamp<PIX>(ret, maxValue);;
        } else {
            float x = (float)(value - _rangeMin) / (float)(_rangeMax - _rangeMin);
            int i = (int)(x * nbValues);
            assert(0 <= i && i <= nbValues);
            float alpha = std::max(0.f,std::min(x * nbValues - i, 1.f));
            float a = _lookupTable[component][i];
            float b = (i  < nbValues) ? _lookupTable[component][i+1] : 0.f;
            return a * (1.f - alpha) + b * alpha;
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
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _srcClipChanged(false)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha ||
                            _dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));

        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
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
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
     }

private:
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    template <int nComponents>
    void renderForComponents(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    void setupAndProcess(ColorLookupProcessorBase &, const OFX::RenderArguments &args);
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL
    {
        if (paramName == kParamSetMaster && args.reason == eChangeUserEdit) {
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
        if ((paramName == kParamSetRGB || paramName == kParamSetRGBA || paramName == kParamSetA) && args.reason == eChangeUserEdit) {
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
        if (paramName == kParamAddCtrlPts && args.reason == eChangeUserEdit) {
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
                            double parametricVal = _lookupTable->getValueAtTime(time, component, args.time, parametricPos);
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
        if (paramName == kParamResetCtrlPts && args.reason == eChangeUserEdit) {
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
        if (paramName == kParamRange && args.reason == eChangeUserEdit) {
            double rmin, rmax;
            _range->getValueAtTime(args.time, rmin, rmax);
            if (rmax < rmin) {
                _range->setValue(rmax, rmin);
            }
        }
    }

private:
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    OFX::ParametricParam  *_lookupTable;
    OFX::Double2DParam* _range;
    OFX::RGBAParam* _source;
    OFX::RGBAParam* _target;
    OFX::BooleanParam* _clampBlack;
    OFX::BooleanParam* _clampWhite;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    bool _srcClipChanged; // set to true the first time the user connects src
};


void
ColorLookupPlugin::setupAndProcess(ColorLookupProcessorBase &processor,
                              const OFX::RenderArguments &args)
{
    assert(_dstClip);
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
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if (doMasking) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
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
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args, dstBitDepth);
    }
}

bool
ColorLookupPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClip;
                return true;
            }
        }
    }

    return false;
}

void
ColorLookupPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName &&
        _srcClip && _srcClip->isConnected() &&
        !_srcClipChanged &&
        args.reason == OFX::eChangeUserEdit) {
        switch (_srcClip->getPreMultiplication()) {
            case eImageOpaque:
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
        }
        _srcClipChanged = true;
    }
}


using namespace OFX;

mDeclarePluginFactory(ColorLookupPluginFactory, {}, {});

void
ColorLookupPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // returning an error here crashes Nuke
    //if (!OFX::getImageEffectHostDescription()->supportsParametricParameter) {
    //  throwHostMissingSuiteException(kOfxParametricParameterSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
ColorLookupPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = (gHostDescription.supportsParametricParameter &&
                                              !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                (gHostDescription.versionMajor == 8 || gHostDescription.versionMajor == 9))); // Nuke 8 and 9 are known to *not* support Parametric
    if (!supportsParametricParameter) {
        throwHostMissingSuiteException(kOfxParametricParameterSuite);
    }

    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    assert(dstClip);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // define it
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamRange);
        param->setLabel(kParamRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setHint(kParamRangeHint);
        param->setDefault(0., 1.);
        param->setDisplayRange(0., 0., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamLookupTable);
        assert(param);
        param->setLabel(kParamLookupTableLabel);
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
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamSource);
        param->setLabel(kParamSourceLabel);
        param->setHint(kParamSourceHint);
        param->setDisplayRange(0., 0., 0., 0., 4., 4., 4., 4.);
        param->setEvaluateOnChange(false);
        param->setIsPersistant(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::RGBAParamDescriptor* param = desc.defineRGBAParam(kParamTarget);
        param->setLabel(kParamTargetLabel);
        param->setHint(kParamTargetHint);
        param->setDisplayRange(0., 0., 0., 0., 4., 4., 4., 4.);
        param->setEvaluateOnChange(false);
        param->setIsPersistant(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetMaster);
        param->setLabel(kParamSetMasterLabel);
        param->setHint(kParamSetMasterHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGB);
        param->setLabel(kParamSetRGBLabel);
        param->setHint(kParamSetRGBHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetRGBA);
        param->setLabel(kParamSetRGBALabel);
        param->setHint(kParamSetRGBAHint);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamSetA);
        param->setLabel(kParamSetALabel);
        param->setHint(kParamSetAHint);
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef COLORLOOKUP_ADD
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamAddCtrlPts);
        param->setLabel(kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel, kParamAddCtrlPtsLabel);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
#ifdef COLORLOOKUP_RESET
    {
        OFX::PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamResetCtrlPts);
        param->setLabel(kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel, kParamResetCtrlPtsLabel);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabel(kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
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

