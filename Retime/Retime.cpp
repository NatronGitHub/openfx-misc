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
 * OFX Retime plugin.
 * Change the timing of the input clip.
 */

/*
 TODO: this plugin has to be improved a lot
 - propose a "timewarp" curve (as ParametricParam)
 - selection of the integration filter (box or nearest) and shutter time
 - handle fielded input correctly
 
 - retiming based on optical flow computation will be done separately
 */

#include "Retime.h"

#include <cmath> // for floor
#include <cfloat> // for FLT_MAX
#include <cassert>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"

#define kPluginName "RetimeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Change the timing of the input clip."
#define kPluginIdentifier "net.sf.openfx.Retime"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamReverseInput "reverseInput"
#define kParamReverseInputLabel "Reverse input"
#define kParamReverseInputHint "Reverse the order of the input frames so that last one is first"

#define kParamSpeed "speed"
#define kParamSpeedLabel "Speed"
#define kParamSpeedHint "How much to changed the speed of the input clip"

#define kParamDuration "duration"
#define kParamDurationLabel "Duration"
#define kParamDurationHint "How long the output clip should be, as a proportion of the input clip's length."

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "How input images are combined to compute the output image."

#define kParamFilterOptionNone "None"
#define kParamFilterOptionNoneHint "Do not interpolate, ask for images with fractional time to the input effect. Useful if the input effect can interpolate itself."
#define kParamFilterOptionNearest "Nearest"
#define kParamFilterOptionNearestHint "Pick input image with nearest integer time."
#define kParamFilterOptionLinear "Linear"
#define kParamFilterOptionLinearHint "Blend the two nearest images with linear interpolation."
// TODO:
#define kParamFilterOptionBox "Box"
#define kParamFilterOptionBoxHint "Weighted average of images over the shutter time (shutter time is defined in the output sequence)." // requires shutter parameter

enum FilterEnum {
    eFilterNone,
    eFilterNearest,
    eFilterLinear,
    //eFilterBox,
};
#define kParamFilterDefault eFilterLinear

#define kPageTimeWarp "timeWarp"
#define kPageTimeWarpLabel "Time Warp"

#define kParamWarp "warp"
#define kParamWarpLabel "Warp"
#define kParamWarpHint "Curve that maps input range (after applying speed) to the output range. A low positive slope slows down the input clip, and a negative slope plays it backwards."

namespace OFX {
    extern ImageEffectHostDescription gHostDescription;
}
////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RetimePlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;            /**< @brief Mandated output clips */
    OFX::Clip *_srcClip;            /**< @brief Mandated input clips */

    OFX::BooleanParam  *_reverse_input;
    OFX::DoubleParam  *_sourceTime; /**< @brief mandated parameter, only used in the retimer context. */
    OFX::DoubleParam  *_speed;      /**< @brief only used in the filter or general context. */
    OFX::ParametricParam  *_warp;      /**< @brief only used in the filter or general context. */
    OFX::DoubleParam  *_duration;   /**< @brief how long the output should be as a proportion of input. General context only. */
    OFX::ChoiceParam  *_filter;   /**< @brief how images are interpolated (or not). */

public:
    /** @brief ctor */
    RetimePlugin(OfxImageEffectHandle handle, bool supportsParametricParameter)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _reverse_input(0)
    , _sourceTime(0)
    , _speed(0)
    , _warp(0)
    , _duration(0)
    , _filter(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);

        // What parameters we instantiate depend on the context
        if (getContext() == OFX::eContextRetimer) {
            // fetch the mandated parameter which the host uses to pass us the frame to retime to
            _sourceTime = fetchDoubleParam(kOfxImageEffectRetimerParamName);
            assert(_sourceTime);
        } else { // context == OFX::eContextFilter || context == OFX::eContextGeneral
            // filter context means we are in charge of how to retime, and our example is using a speed curve to do that
            _reverse_input = fetchBooleanParam(kParamReverseInput);
            _speed = fetchDoubleParam(kParamSpeed);
            assert(_speed);
            if (supportsParametricParameter) {
                _warp = fetchParametricParam(kParamWarp);
                assert(_warp);
            } else if (getContext() == OFX::eContextGeneral) {
                // fetch duration param for general context
                _duration = fetchDoubleParam(kParamDuration);
                assert(_duration);
            }
        }
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const OFX::RenderArguments &args, double sourceTime, FilterEnum filter, OFX::BitDepthEnum dstBitDepth);

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderBase &, const OFX::RenderArguments &args, double sourceTime, FilterEnum filter);
    
private:
    
    
    bool isIdentityInternal(OfxTime time, OFX::Clip* &identityClip, OfxTime &identityTime);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum       srcBitDepth    = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}

static void framesNeeded(double sourceTime, OFX::FieldEnum fieldToRender, double *fromTimep, double *toTimep, double *blendp)
{
    // figure the two images we are blending between
    double fromTime, toTime;
    double blend;

    if (fieldToRender == OFX::eFieldNone) {
        // unfielded, easy peasy
        fromTime = std::floor(sourceTime);
        toTime = fromTime + 1;
        blend = sourceTime - fromTime;
    } else {
        // Fielded clips, pook. We are rendering field doubled images,
        // and so need to blend between fields, not frames.
        double frac = sourceTime - std::floor(sourceTime);
        if (frac < 0.5) {
            // need to go between the first and second fields of this frame
            fromTime = std::floor(sourceTime); // this will get the first field
            toTime   = fromTime + 0.5;    // this will get the second field of the same frame
            blend    = frac * 2.0;        // and the blend is between those two
        } else { // frac > 0.5
            fromTime = std::floor(sourceTime) + 0.5; // this will get the second field of this frame
            toTime   = std::floor(sourceTime) + 1.0; // this will get the first field of the next frame
            blend    = (frac - 0.5) * 2.0;
        }
    }
    *fromTimep = fromTime;
    *toTimep = toTime;
    *blendp = blend;
}

/* set up and run a processor */
void
RetimePlugin::setupAndProcess(OFX::ImageBlenderBase &processor,
                              const OFX::RenderArguments &args,
                              double sourceTime,
                              FilterEnum filter)
{
    const double time = args.time;
    // get a dst image
    std::auto_ptr<OFX::Image>  dst(_dstClip->fetchImage(time));
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

    if (sourceTime == (int)sourceTime || filter == eFilterNone || filter == eFilterNearest) {
        // should have been caught by isIdentity...
        std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                            _srcClip->fetchImage(sourceTime) : 0);
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
        copyPixels(*this, args.renderWindow, src.get(), dst.get());
        return;
    }

    // figure the two images we are blending between
    double fromTime, toTime;
    double blend;
    framesNeeded(sourceTime, args.fieldToRender, &fromTime, &toTime, &blend);

    // fetch the two source images
    std::auto_ptr<OFX::Image> fromImg((_srcClip && _srcClip->isConnected()) ?
                                      _srcClip->fetchImage(fromTime) : 0);
    std::auto_ptr<OFX::Image> toImg((_srcClip && _srcClip->isConnected()) ?
                                    _srcClip->fetchImage(toTime) : 0);

    // make sure bit depths are sane
    if (fromImg.get()) {
        if (fromImg->getRenderScale().x != args.renderScale.x ||
            fromImg->getRenderScale().y != args.renderScale.y ||
            (fromImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && fromImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if (toImg.get()) {
        if (toImg->getRenderScale().x != args.renderScale.x ||
            toImg->getRenderScale().y != args.renderScale.y ||
            (toImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && toImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setFromImg(fromImg.get());
    processor.setToImg(toImg.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the blend between
    processor.setBlend((float)blend);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

void
RetimePlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                               OFX::FramesNeededSetter &frames)
{
    if (!_srcClip) {
        return;
    }
    const double time = args.time;
    double sourceTime;
    if (getContext() == OFX::eContextRetimer) {
        // the host is specifying it, so fetch it from the kOfxImageEffectRetimerParamName pseudo-param
        sourceTime = _sourceTime->getValueAtTime(time);
    } else {
        bool reverse_input;
        OfxRangeD srcRange = _srcClip->getFrameRange();
        _reverse_input->getValueAtTime(time, reverse_input);
        // we have our own param, which is a speed, so we integrate it to get the time we want
        if (reverse_input) {
            sourceTime = srcRange.max - _speed->integrate(srcRange.min, time);
        } else {
            sourceTime = srcRange.min + _speed->integrate(srcRange.min, time);
        }
        if (_warp) {
            double r = srcRange.max - srcRange.min;
            if (r != 0.) {
                sourceTime = srcRange.min + r * _warp->getValue(0, time, (sourceTime-srcRange.min)/r);
            }
        }
    }

    int filter_i;
    _filter->getValueAtTime(time, filter_i);
    FilterEnum filter = (FilterEnum)filter_i;

    OfxRangeD range;
    if (sourceTime == (int)sourceTime || filter == eFilterNone) {
        range.min = sourceTime;
        range.max = sourceTime;
    } else if (filter == eFilterNearest) {
        range.min = range.max = std::floor(sourceTime + 0.5);
    } else if (filter == eFilterLinear) {
        // figure the two images we are blending between
        double fromTime, toTime;
        double blend;
        // whatever the rendered field is, the frames are the same
        framesNeeded(sourceTime, OFX::eFieldNone, &fromTime, &toTime, &blend);
        range.min = fromTime;
        range.max = toTime;
    } else {
        assert(false);
    }
    frames.setFramesNeeded(*_srcClip, range);
}

bool
RetimePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OFX::Clip* identityClip;
    OfxTime identityTime;
    bool identity = isIdentityInternal(args.time, identityClip, identityTime);
    if (!identity) {
        return false;
    }
    rod = _srcClip->getRegionOfDefinition(identityTime, args.view);
    return true;
}

bool
RetimePlugin::isIdentityInternal(OfxTime time, OFX::Clip* &identityClip, OfxTime &identityTime)
{
    if (!_srcClip) {
        return false;
    }
    double sourceTime;
    if (getContext() == OFX::eContextRetimer) {
        // the host is specifying it, so fetch it from the kOfxImageEffectRetimerParamName pseudo-param
        sourceTime = _sourceTime->getValueAtTime(time);
    } else {
        bool reverse_input;
        OfxRangeD srcRange = _srcClip->getFrameRange();
        _reverse_input->getValueAtTime(time, reverse_input);
        // we have our own param, which is a speed, so we integrate it to get the time we want
        if (reverse_input) {
            sourceTime = srcRange.max - _speed->integrate(srcRange.min, time);
        } else {
            sourceTime = srcRange.min + _speed->integrate(srcRange.min, time);
        }
        if (_warp) {
            double r = srcRange.max - srcRange.min;
            if (r != 0.) {
                sourceTime = srcRange.min + r * _warp->getValue(0, time, (sourceTime-srcRange.min)/r);
            }
        }
    }
    int filter_i;
    _filter->getValueAtTime(time, filter_i);
    FilterEnum filter = (FilterEnum)filter_i;
    
    if (sourceTime == (int)sourceTime || filter == eFilterNone) {
        identityClip = _srcClip;
        identityTime = sourceTime;
        return true;
    }
    if (filter == eFilterNearest) {
        identityClip = _srcClip;
        identityTime = std::floor(sourceTime + 0.5);
        return true;
    }
    
    return false;
}

bool
RetimePlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    return isIdentityInternal(args.time, identityClip, identityTime);
}


/* override the time domain action, only for the general context */
bool
RetimePlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == OFX::eContextGeneral && _srcClip && _duration) {
        assert(!_warp);
        // If we are a general context, we can changed the duration of the effect, so have a param to do that
        // We need a separate param as it is impossible to derive this from a speed param and the input clip
        // duration (the speed may be animating or wired to an expression).
        double duration;
        _duration->getValue(duration); //don't animate

        // how many frames on the input clip
        OfxRangeD srcRange = _srcClip->getFrameRange();

        range.min = srcRange.min;
        range.max = srcRange.min + (srcRange.max-srcRange.min) * duration;

        return true;
    }

    // If there's a warp curve, the time domain could be determined from the intersections of the warp curve with y=0 and y=1.
    // for now, we prefer returning the input time domain.
    return false;
}

// the internal render function
template <int nComponents>
void
RetimePlugin::renderInternal(const OFX::RenderArguments &args,
                             double sourceTime,
                             FilterEnum filter,
                             OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            OFX::ImageBlender<unsigned char, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        case OFX::eBitDepthUShort: {
            OFX::ImageBlender<unsigned short, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        case OFX::eBitDepthFloat: {
            OFX::ImageBlender<float, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
RetimePlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    // figure the frame we should be retiming from
    double sourceTime = time;

    if (getContext() == OFX::eContextRetimer) {
        // the host is specifying it, so fetch it from the kOfxImageEffectRetimerParamName pseudo-param
        sourceTime = _sourceTime->getValueAtTime(time);
    } else if (_srcClip) {
        bool reverse_input;

        OfxRangeD srcRange = _srcClip->getFrameRange();
        _reverse_input->getValueAtTime(time, reverse_input);
        // we have our own param, which is a speed, so we integrate it to get the time we want
        if (reverse_input) {
            sourceTime = srcRange.max - _speed->integrate(srcRange.min, time);
        } else {
            sourceTime = srcRange.min + _speed->integrate(srcRange.min, time);
        }
        if (_warp) {
            double r = srcRange.max - srcRange.min;
            if (r != 0.) {
                sourceTime = srcRange.min + r * _warp->getValue(0, time, (sourceTime-srcRange.min)/r);
            }
        }
    }

    int filter_i;
    _filter->getValueAtTime(time, filter_i);
    FilterEnum filter = (FilterEnum)filter_i;

#ifdef DEBUG
    if (sourceTime == (int)sourceTime || filter == eFilterNone || filter == eFilterNearest) {
        // should be caught by isIdentity!
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host should not render");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderInternal<4>(args, sourceTime, filter, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderInternal<3>(args, sourceTime, filter, dstBitDepth);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderInternal<2>(args, sourceTime, filter, dstBitDepth);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderInternal<1>(args, sourceTime, filter, dstBitDepth);
    }
}

using namespace OFX;
mDeclarePluginFactory(RetimePluginFactory, ;, {});

void RetimePluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void RetimePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a transition context
    desc.addSupportedContext(OFX::eContextRetimer);
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    // Add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(true); // each field has to be rendered separately, since it may come from a different time
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void RetimePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum context)
{
    // we are a transition, so define the sourceTo input clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway
    dstClip->setSupportsTiles(kSupportsTiles);

    // make a page to put it in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // what param we have is dependant on the host
    if (context == OFX::eContextRetimer) {
        // Define the mandated kOfxImageEffectRetimerParamName param, note that we don't do anything with this other than.
        // describe it. It is not a true param but how the host indicates to the plug-in which frame
        // it wants you to retime to. It appears on no plug-in side UI, it is purely the host's to manage.
        DoubleParamDescriptor *param = desc.defineDoubleParam(kOfxImageEffectRetimerParamName);
        (void)param;
    }  else {
        // We are a general or filter context, define a speed param and a page of controls to put that in
        // reverse_input
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamReverseInput);
            param->setDefault(false);
            param->setHint(kParamReverseInputHint);
            param->setLabel(kParamReverseInputLabel);
            param->setAnimates(true);
            if (page) {
                page->addChild(*param);
            }
        }

        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSpeed);
            param->setLabel(kParamSpeedLabel);
            param->setHint(kParamSpeedHint);
            param->setDefault(1);
            param->setRange(-FLT_MAX, FLT_MAX);
            param->setIncrement(0.05);
            param->setDisplayRange(0.1, 10.);
            param->setAnimates(true); // can animate
            param->setDoubleType(eDoubleTypeScale);
            if (page) {
                page->addChild(*param);
            }
        }

        const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
        const bool supportsParametricParameter = (gHostDescription.supportsParametricParameter &&
                                                  !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                    (gHostDescription.versionMajor == 8 || gHostDescription.versionMajor == 9))); // Nuke 8 and 9 are known to *not* support Parametric
        if (supportsParametricParameter) {
            OFX::PageParamDescriptor* page = desc.definePageParam(kPageTimeWarp);
            if (page) {
                page->setLabel(kPageTimeWarpLabel);
            }
            {
                OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamWarp);
                assert(param);
                param->setLabel(kParamWarpLabel);
                param->setHint(kParamWarpHint);

                // define it as one dimensional
                param->setDimension(1);
                param->setDimensionLabel(kParamWarp, 0);

                const OfxRGBColourD blue  = {0.5, 0.5, 1};		//set blue color to blue curve
                param->setUIColour( 0, blue );

                // set the min/max parametric range to 0..1
                param->setRange(0.0, 1.0);

                param->setIdentity(0);

                // add param to page
                if (page) {
                    page->addChild(*param);
                }
            }
        } else if (context == OFX::eContextGeneral) {
            // If we are a general context, we can change the duration of the effect, so have a param to do that
            // We need a separate param as it is impossible to derive this from a speed param and the input clip
            // duration (the speed may be animating or wired to an expression).

            // This is not possible if there's a warp curve.

            // We are a general or filter context, define a speed param and a page of controls to put that in
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDuration);
            param->setLabel(kParamDurationLabel);
            param->setHint(kParamDurationHint);
            param->setDefault(1);
            param->setRange(0, 10);
            param->setIncrement(0.1);
            param->setDisplayRange(0, 10);
            param->setAnimates(false); // no animation here!
            param->setDoubleType(eDoubleTypeScale);

            // add param to page
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterNone);
        param->appendOption(kParamFilterOptionNone, kParamFilterOptionNoneHint);
        assert(param->getNOptions() == eFilterNearest);
        param->appendOption(kParamFilterOptionNearest, kParamFilterOptionNearestHint);
        assert(param->getNOptions() == eFilterLinear);
        param->appendOption(kParamFilterOptionLinear, kParamFilterOptionLinearHint);
        //assert(param->getNOptions() == eFilterBox);
        //param->appendOption(kParamFilterOptionBox, kParamFilterOptionBoxHint);
        param->setDefault((int)kParamFilterDefault);
        if (page) {
            page->addChild(*param);
        }
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* RetimePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = (gHostDescription.supportsParametricParameter &&
                                              !(gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                (gHostDescription.versionMajor == 8 || gHostDescription.versionMajor == 9)));
    return new RetimePlugin(handle, supportsParametricParameter);
}

void getRetimePluginID(OFX::PluginFactoryArray &ids)
{
    static RetimePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
