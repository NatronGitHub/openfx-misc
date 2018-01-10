/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * OFX FrameRange plugin.
 */


#include <algorithm> // for std::max
#include <cmath> // for std::floor, std::ceil
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  define NOMINMAX
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "FrameRangeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Set the frame range for a clip. Useful in conjunction with AppendClipOFX."
#define kPluginIdentifier "net.sf.openfx.FrameRange"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint "Output frame range."

#define kParamReset "reset"
#define kParamResetLabel "Reset"
#define kParamResetHint "Resets the frame range to its initial value."

#define kParamBefore "before"
#define kParamBeforeLabel "Before"
#define kParamBeforeHint "What the plugin should return for frames before the first frame."
#define kParamAfter "after"
#define kParamAfterLabel "After"
#define kParamAfterHint "What the plugin should return for frames after the last frame."
#define kParamBeforeAfterOptionOriginal "Original", "Return the original frame from the source, even if it is out of the frame range.", "original"
#define kParamBeforeAfterOptionHold "Hold", "Return the nearest frame within the frame range.", "hold"
#define kParamBeforeAfterOptionBlack "Black", "Return an empty frame.", "black"

enum BeforeAfterEnum
{
    eBeforeAfterOriginal = 0,
    eBeforeAfterHold,
    eBeforeAfterBlack,
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameRangePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    FrameRangePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _frameRange(NULL)
        , _before(NULL)
        , _after(NULL)
        , _sublabel(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _frameRange = fetchInt2DParam(kParamFrameRange);
        _before = fetchChoiceParam(kParamBefore);
        _after = fetchChoiceParam(kParamAfter);
        assert(_frameRange && _before && _after);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_sublabel);

        OfxPointI range = _frameRange->getValue();
        refreshSubLabel(range.x, range.y);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const TransformArguments & args, Clip * &transformClip, double transformMatrix[9]) OVERRIDE FINAL;
#endif

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:

    void refreshSubLabel(int rangeMin, int rangeMax);
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Int2DParam *_frameRange;
    ChoiceParam *_before;
    ChoiceParam *_after;
    StringParam *_sublabel;
};


void
FrameRangePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // setting an image to black outside of the frame range means that the effect is frame varying
    BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();

    if (before == eBeforeAfterBlack) {
        clipPreferences.setOutputFrameVarying(true);
    } else {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after == eBeforeAfterBlack) {
            clipPreferences.setOutputFrameVarying(true);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


// the overridden render function
void
FrameRangePlugin::render(const RenderArguments &args)
{
    const double time = args.time;
    OfxPointI range = _frameRange->getValue();
    double srcTime = time;
    bool black = false;

    if (time < range.x) {
        BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();
        if (before == eBeforeAfterBlack) {
            black = true;
        } else if (before == eBeforeAfterHold) {
            srcTime = range.x;
        }
    } else if (time > range.y) {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after == eBeforeAfterBlack) {
            black = true;
        } else if (after == eBeforeAfterHold) {
            srcTime = range.y;
        }
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    auto_ptr<const Image> src( (_srcClip && _srcClip->isConnected() && !black) ?
                                    _srcClip->fetchImage(srcTime) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    if (black) {
        fillBlack( *this, args.renderWindow, dst.get() );
    } else {
        copyPixels( *this, args.renderWindow, src.get(), dst.get() );
    }
} // FrameRangePlugin::render

bool
FrameRangePlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    OfxPointI range = _frameRange->getValue();

    if (time < range.x) {
        BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();
        if (before == eBeforeAfterBlack) {
            return false;
        } else if (before == eBeforeAfterHold) {
            identityTime = range.x;
        }
    } else if (time > range.y) {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after == eBeforeAfterBlack) {
            return false;
        } else if (after == eBeforeAfterHold) {
            identityTime = range.y;
        }
    }
    identityClip = _srcClip;

    return true;
}

bool
FrameRangePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    if ( !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }
    const double time = args.time;
    OfxPointI range = _frameRange->getValue();
    if (time < range.x) {
        BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();
        if (before == eBeforeAfterBlack) {
            rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

            return true;
        } else if (before == eBeforeAfterHold) {
            rod = _srcClip->getRegionOfDefinition(range.x);

            return true;
        }
    } else if (time > range.y) {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after == eBeforeAfterBlack) {
            rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

            return true;
        } else if (after == eBeforeAfterHold) {
            rod = _srcClip->getRegionOfDefinition(range.y);

            return true;
        }
    }

    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool
FrameRangePlugin::getTransform(const TransformArguments &args,
                               Clip * &transformClip,
                               double transformMatrix[9])
{
    const double time = args.time;
    OfxPointI range = _frameRange->getValue();

    if (time < range.x) {
        BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();
        if (before != eBeforeAfterOriginal) {
            return false;
        }
    } else if (time > range.y) {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after != eBeforeAfterOriginal) {
            return false;
        }
    }
    transformClip = _srcClip;
    transformMatrix[0] = 1.;
    transformMatrix[1] = 0.;
    transformMatrix[2] = 0.;
    transformMatrix[3] = 0.;
    transformMatrix[4] = 1.;
    transformMatrix[5] = 0.;
    transformMatrix[6] = 0.;
    transformMatrix[7] = 0.;
    transformMatrix[8] = 1.;

    return true;
}

#endif


/** @brief called when a clip has just been changed in some way (a rewire maybe) */
void
FrameRangePlugin::changedClip(const InstanceChangedArgs &args,
                              const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         ( args.reason == eChangeUserEdit) &&
         _srcClip &&
         _srcClip->isConnected() ) {
        // if range is (1,1), i.e. the default value, set it to the input range
        int min, max;
        _frameRange->getValue(min, max);
        if ( (min == 1) && (max == 1) ) {
            OfxRangeD srcRange = _srcClip->getFrameRange();
            _frameRange->setValue( std::floor(srcRange.min), std::ceil(srcRange.max) );
        }
    }
}

void
FrameRangePlugin::refreshSubLabel(int rangeMin, int rangeMax)
{
    char label[80];
    snprintf(label, sizeof(label), "%d - %d", rangeMin, rangeMax);
    _sublabel->setValue(label);
}

void
FrameRangePlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if ( (paramName == kParamReset) && _srcClip && _srcClip->isConnected() && (args.reason == eChangeUserEdit) ) {
        OfxRangeD range = _srcClip->getFrameRange();
        _frameRange->setValue( (int)std::floor(range.min), (int)std::ceil(range.max) );
        refreshSubLabel((int)std::floor(range.min), (int)std::ceil(range.max));
    }
    if ( (paramName == kParamFrameRange) && (args.reason == eChangeUserEdit) ) {

        OfxPointI range = _frameRange->getValue();
        refreshSubLabel(range.x, range.y);
    }
}

/* override the time domain action, only for the general context */
bool
FrameRangePlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == eContextGeneral);
    int min, max;
    _frameRange->getValue(min, max);
    range.min = min;
    range.max = std::max(min, max);

    return true;
}

mDeclarePluginFactory(FrameRangePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
FrameRangePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only general, because the only useful action is getTimeDomain
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthNone);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthCustom);
#ifdef OFX_EXTENSIONS_VEGAS
    desc.addSupportedBitDepth(eBitDepthUByteBGRA);
    desc.addSupportedBitDepth(eBitDepthUShortBGRA);
    desc.addSupportedBitDepth(eBitDepthFloatBGRA);
#endif

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
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
FrameRangePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentNone);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->setCanTransform(true);
#endif
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // frameRange
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDefault(1, 1);
        param->setDimensionLabels("first", "last");
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }
    // reset
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamReset);
        param->setLabel(kParamResetLabel);
        param->setHint(kParamResetHint);
        if (page) {
            page->addChild(*param);
        }
    }
    // before
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBefore);
        param->setLabel(kParamBeforeLabel);
        param->setHint(kParamBeforeHint);
        assert(param->getNOptions() == (int)eBeforeAfterOriginal);
        param->appendOption(kParamBeforeAfterOptionOriginal);
        assert(param->getNOptions() == (int)eBeforeAfterHold);
        param->appendOption(kParamBeforeAfterOptionHold);
        assert(param->getNOptions() == (int)eBeforeAfterBlack);
        param->appendOption(kParamBeforeAfterOptionBlack);
        param->setDefault( (int)eBeforeAfterBlack );
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    // after
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamAfter);
        param->setLabel(kParamAfterLabel);
        param->setHint(kParamAfterHint);
        assert(param->getNOptions() == (int)eBeforeAfterOriginal);
        param->appendOption(kParamBeforeAfterOptionOriginal);
        assert(param->getNOptions() == (int)eBeforeAfterHold);
        param->appendOption(kParamBeforeAfterOptionHold);
        assert(param->getNOptions() == (int)eBeforeAfterBlack);
        param->appendOption(kParamBeforeAfterOptionBlack);
        param->setDefault( (int)eBeforeAfterBlack );
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    // sublabel
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(false);
        param->setEvaluateOnChange(false);
        param->setDefault("1 - 1");
    }
} // FrameRangePluginFactory::describeInContext

ImageEffect*
FrameRangePluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new FrameRangePlugin(handle);
}

static FrameRangePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
