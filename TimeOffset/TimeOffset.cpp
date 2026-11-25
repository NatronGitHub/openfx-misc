/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
 * OFX TimeOffset plugin.
 * Move the input clip forward or backward in time.
 * This can also reverse the order of the input frames so that last one is first.
 */

#include <cmath> // std::trunc
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TimeOffsetOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Move the input clip forward or backward in time. " \
    "This can also reverse the order of the input frames so that last one is first.\n" \
    "See also https://web.archive.org/web/20220627025738/http://www.opticalenquiry.com/nuke/index.php?title=TimeOffset"

#define kPluginIdentifier "net.sf.openfx.timeOffset"
// History:
// version 1.0: initial version
// version 1.1: render copies the Source at the source time (it wrote nothing): the spec says that the
//              host should not call render after isIdentity returns true, but some hosts (e.g. hostDemo) do;
//              a NULL Source image (disconnected input) renders black instead of crashing (SupportExt copyPixels)
// version 2.0: Time Offset is fractional, and a Mode parameter (first) says how it is used: Whole Frames (the
//              default) truncates it toward zero, Fractional Time uses it as is; the frame range is offset by the
//              truncated offset (#306). Version 1 (an integer offset) is kept, obsolete, for old projects.
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV1 1 // the minor version of the obsolete version 1

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamRoundingMode "roundingMode"
#define kParamRoundingModeLabel "Mode"
#define kParamRoundingModeHint "How Time Offset is used."
#define kParamRoundingModeOptionWhole "Whole Frames", "Truncate Time Offset toward zero to a whole number of frames (e.g. 5.7 gives 5, -5.5 gives -5).", "whole"
#define kParamRoundingModeOptionFractional "Fractional Time", "Use the exact Time Offset, which may ask the input for times between frames (the frame range is still offset by the truncated value).", "fractional"

// The major versions (see the History)
enum class TimeOffsetVersion
{
    e1 = 1, // an integer offset
    e2 = 2, // a fractional offset, and Mode
};

enum RoundingModeEnum
{
    eRoundingModeWhole = 0,
    eRoundingModeFractional,
};

#define kParamRoundingModeDefault eRoundingModeWhole

#define kParamTimeOffset "timeOffset"
#define kParamTimeOffsetLabel "Time Offset (Frames)"
#define kParamTimeOffsetHint "Offset in frames (frame f from the input will be at f+offset)"

#define kParamReverseInput "reverseInput"
#define kParamReverseInputLabel "Reverse Input"
#define kParamReverseInputHint "Reverse the order of the input frames so that last one is first"

#define kParamClipToInputRange "clipToInputRange"
#define kParamClipToInputRangeLabel "Clip to Input Range"
#define kParamClipToInputRangeHint "Never ask for frames outside of the input frame range."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeOffsetPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    TimeOffsetPlugin(OfxImageEffectHandle handle, TimeOffsetVersion version);

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    double getSourceTime(double time) const;
    // the offset applied to the time (Mode: Whole Frames truncates it), and to the frame range (truncated)
    double getTimeOffset(double time) const;
    double getFrameRangeOffset(double time) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;            /**< @brief Mandated output clips */
    Clip *_srcClip;            /**< @brief Mandated input clips */
    IntParam  *_time_offset;      /**< @brief version 1: the offset, in frames. */
    DoubleParam *_time_offset_v2; /**< @brief version 2: the offset, maybe fractional. */
    ChoiceParam *_roundingMode;   /**< @brief version 2 */
    BooleanParam  *_reverse_input;
    BooleanParam  *_clip;
};

TimeOffsetPlugin::TimeOffsetPlugin(OfxImageEffectHandle handle,
                                   TimeOffsetVersion version)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClip(NULL)
    , _time_offset(NULL)
    , _time_offset_v2(NULL)
    , _roundingMode(NULL)
    , _reverse_input(NULL)
    , _clip(NULL)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip);
    _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert( (!_srcClip && getContext() == eContextGenerator) ||
            ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                           _srcClip->getPixelComponents() == ePixelComponentRGB ||
                           _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

    if (version >= TimeOffsetVersion::e2) {
        _roundingMode = fetchChoiceParam(kParamRoundingMode);
        _time_offset_v2 = fetchDoubleParam(kParamTimeOffset);
    } else {
        _time_offset   = fetchIntParam(kParamTimeOffset);
    }
    _reverse_input = fetchBooleanParam(kParamReverseInput);
    _clip = fetchBooleanParam(kParamClipToInputRange);
    assert( (_time_offset || (_time_offset_v2 && _roundingMode)) && _reverse_input && _clip );
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

double
TimeOffsetPlugin::getTimeOffset(double t) const
{
    if (_time_offset) {
        return _time_offset->getValueAtTime(t); // no animation
    }
    const double offset = _time_offset_v2->getValueAtTime(t); // no animation
    const bool whole = static_cast<RoundingModeEnum>( _roundingMode->getValue() ) == eRoundingModeWhole;

    return whole ? std::trunc(offset) : offset;
}

double
TimeOffsetPlugin::getFrameRangeOffset(double t) const
{
    if (_time_offset) {
        return _time_offset->getValueAtTime(t); // no animation
    }

    return std::trunc( _time_offset_v2->getValueAtTime(t) ); // no animation
}

// figure the frame we should be retiming from
double
TimeOffsetPlugin::getSourceTime(double t) const
{
    double sourceTime = t - getTimeOffset(t);

    if (!_srcClip || !_srcClip->isConnected()) {
        return sourceTime;
    }
    OfxRangeD range = _srcClip->getFrameRange();
    bool reverse_input = _reverse_input->getValueAtTime(t);
    if (reverse_input) {
        sourceTime = range.max - sourceTime + range.min;
    }
    if ( _clip->getValueAtTime(t) ) {
        // clip to min/max range
        if (sourceTime < range.min) {
            sourceTime = range.min;
        } else if (sourceTime > range.max) {
            sourceTime = range.max;
        }
    }

    return sourceTime;
}

/* override the time domain action, only for the general context */
bool
TimeOffsetPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    if (getContext() == eContextGeneral) {
        // how many frames on the input clip
        OfxRangeD srcRange = _srcClip->getFrameRange();

        range.min = srcRange.min + getFrameRangeOffset(srcRange.min);
        range.max = srcRange.max + getFrameRangeOffset(srcRange.max);

        return true;
    }

    return false;
}

void
TimeOffsetPlugin::getFramesNeeded(const FramesNeededArguments &args,
                                  FramesNeededSetter &frames)
{
    double sourceTime = getSourceTime(args.time);
    OfxRangeD range;

    range.min = sourceTime;
    range.max = sourceTime;
    frames.setFramesNeeded(*_srcClip, range);
}

// the overridden render function
void
TimeOffsetPlugin::render(const RenderArguments &args)
{
    // isIdentity is always true, and the spec says that the host should not call render after
    // isIdentity returns true, but some hosts (e.g. hostDemo) do: copy the identity result (the
    // Source at the source time), for robustness
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                               _srcClip->fetchImage( getSourceTime(args.time) ) : 0 );
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        if ( (src->getPixelDepth() != dst->getPixelDepth()) || (src->getPixelComponents() != dst->getPixelComponents()) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );
}

// overridden is identity
bool
TimeOffsetPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);

    return true;
}

bool
TimeOffsetPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double identityTime = getSourceTime(args.time);

    rod = _srcClip->getRegionOfDefinition(identityTime);

    return true;
}

mDeclarePluginFactoryVersioned(TimeOffsetPluginFactory,; , {});
template<unsigned int majorVersion>
void
TimeOffsetPluginFactory<majorVersion>::load()
{
    ofxsThreadSuiteCheck();
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
template<unsigned int majorVersion>
void
TimeOffsetPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // version 1 (Natron 2.5 and earlier) is kept for old projects
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a filer context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

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
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
template<unsigned int majorVersion>
void
TimeOffsetPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                         ContextEnum /*context*/)
{
    // we are a transition, so define the sourceTo input clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
    srcClip->setSupportsTiles(kSupportsTiles);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // roundingMode (version 2), first
    if (majorVersion >= 2) {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRoundingMode);
        param->setLabel(kParamRoundingModeLabel);
        param->setHint(kParamRoundingModeHint);
        assert(param->getNOptions() == eRoundingModeWhole);
        param->appendOption(kParamRoundingModeOptionWhole);
        assert(param->getNOptions() == eRoundingModeFractional);
        param->appendOption(kParamRoundingModeOptionFractional);
        param->setDefault( (int)kParamRoundingModeDefault );
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }

    // time_offset: fractional in version 2
    if (majorVersion >= 2) {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamTimeOffset);
        param->setLabel(kParamTimeOffsetLabel);
        param->setHint(kParamTimeOffsetHint);
        param->setDefault(0.);
        param->setDisplayRange(-100., 100.);
        param->setIncrement(1.);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    } else {
        IntParamDescriptor *param = desc.defineIntParam(kParamTimeOffset);
        param->setLabel(kParamTimeOffsetLabel);
        param->setHint(kParamTimeOffsetHint);
        param->setDefault(0);
        // keep default range (INT_MIN..INT_MAX)
        // no display range
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }

    // reverse_input
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamReverseInput);
        param->setDefault(false);
        param->setHint(kParamReverseInputHint);
        param->setLabel(kParamReverseInputLabel);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // clip
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClipToInputRange);
        param->setDefault(false);
        param->setHint(kParamClipToInputRangeHint);
        param->setLabel(kParamClipToInputRangeLabel);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // TimeOffsetPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
template<unsigned int majorVersion>
ImageEffect*
TimeOffsetPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                      ContextEnum /*context*/)
{
    return new TimeOffsetPlugin(handle, (majorVersion >= 2) ? TimeOffsetVersion::e2 : TimeOffsetVersion::e1);
}

static TimeOffsetPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
static TimeOffsetPluginFactory<1> p_v1(kPluginIdentifier, kPluginVersionMinorV1);
mRegisterPluginFactoryInstance(p_v1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
