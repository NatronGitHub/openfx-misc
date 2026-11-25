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
 * OFX FrameRange plugin.
 */


#include <algorithm> // for std::max
#include <cmath> // for std::floor, std::ceil
#include <stdio.h> // for snprintf & _snprintf
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  if defined(_MSC_VER) && _MSC_VER < 1900
#    define snprintf _snprintf
#  endif
#endif // defined(_WIN32) || defined(__WIN32__) || defined(WIN32)

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "FrameRangeBeforeAfter.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;
using namespace OFX::FrameRangeBeforeAfter;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "FrameRangeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Set the frame range for a clip. Useful in conjunction with AppendClipOFX.\n" \
    "By default (Before and After: Black), frames outside the range render black. To only change the frame range and let the input through, set Before and After to Original."
#define kPluginIdentifier "net.sf.openfx.FrameRange"
// History:
// version 1.0: initial version
// version 1.1: a NULL Source image (disconnected input; render is called although isIdentity returned true,
//              e.g. by hostDemo) renders black instead of crashing (SupportExt copyPixels);
//              the sublabel shows the range as the host displays it (kOfxImageClipPropDisplayTimeOffset, display only);
//              (#400) connecting the Source sets Frame Range to the input range only on Natron >= 2.6, and only
//              if Frame Range was never set (a secret, saved flag) and is still (1,1): a host that loads values
//              as user edits could replace a saved (1,1)
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint "Output frame range. On Natron 2.6 and later, connecting the Source sets it to the input frame range if it was never set."
#define kParamFrameRangeDefaultMin 1
#define kParamFrameRangeDefaultMax 1
// Whether Frame Range was set: by the user (or loaded with a project), by Reset, or from the input range when the
// Source was connected. Secret, saved with the project.
#define kParamFrameRangeInitialised "frameRangeInitialised"

#define kParamReset "reset"
#define kParamResetLabel "Reset"
#define kParamResetHint "Resets the frame range to its initial value."

#define kParamBeforeHint "What the plugin should return for frames before the first frame. The default, Black, renders those frames black; Original passes the input through, so that only the frame range changes."
#define kParamAfterHint "What the plugin should return for frames after the last frame. The default, Black, renders those frames black; Original passes the input through, so that only the frame range changes."

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
        , _frameRangeInitialised(NULL)
        , _before(NULL)
        , _after(NULL)
        , _sublabel(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _frameRange = fetchInt2DParam(kParamFrameRange);
        _frameRangeInitialised = fetchBooleanParam(kParamFrameRangeInitialised);
        _before = fetchChoiceParam(kParamBefore);
        _after = fetchChoiceParam(kParamAfter);
        assert(_frameRange && _frameRangeInitialised && _before && _after);
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
    void setFrameRangeToInputRange();
    static bool hostSkipsChangedClipOnLoad();
    BeforeAfterEnum getSourceTime(double time, double *srcTime);
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Int2DParam *_frameRange;
    BooleanParam *_frameRangeInitialised;
    ChoiceParam *_before;
    ChoiceParam *_after;
    StringParam *_sublabel;
};


void
FrameRangePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // setting an image to black outside of the frame range means that the effect is frame varying
    BeforeAfterEnum before = (BeforeAfterEnum)_before->getValue();

    if (before == eBeforeAfterBlack || before == eBeforeAfterLoop || before == eBeforeAfterBounce) {
        clipPreferences.setOutputFrameVarying(true);
    } else {
        BeforeAfterEnum after = (BeforeAfterEnum)_after->getValue();
        if (after == eBeforeAfterBlack || after == eBeforeAfterLoop || after == eBeforeAfterBounce) {
            clipPreferences.setOutputFrameVarying(true);
        }
    }
}

// Resolve the Before/After behaviour at the given output time.
// Returns the behaviour that applies (eBeforeAfterOriginal within the frame range)
// and sets *srcTime to the source frame to use (meaningless for eBeforeAfterBlack).
BeforeAfterEnum
FrameRangePlugin::getSourceTime(double time,
                                double *srcTime)
{
    OfxPointI range = _frameRange->getValue();

    return getBeforeAfterTime(range.x, range.y,
                              (BeforeAfterEnum)_before->getValue(), (BeforeAfterEnum)_after->getValue(),
                              time, srcTime);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


// the overridden render function
void
FrameRangePlugin::render(const RenderArguments &args)
{
    double srcTime;
    const bool black = (getSourceTime(args.time, &srcTime) == eBeforeAfterBlack);

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src( (_srcClip && _srcClip->isConnected() && !black) ?
                                    _srcClip->fetchImage(srcTime) : 0 );
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    if (black) {
        fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );
    } else {
        copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );
    }
} // FrameRangePlugin::render

bool
FrameRangePlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    double srcTime;
    BeforeAfterEnum mode = getSourceTime(args.time, &srcTime);

    if (mode == eBeforeAfterBlack) {
        return false;
    } else if (mode != eBeforeAfterOriginal) {
        identityTime = srcTime;
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
    double srcTime;
    BeforeAfterEnum mode = getSourceTime(args.time, &srcTime);
    if (mode == eBeforeAfterBlack) {
        rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;

        return true;
    } else if (mode != eBeforeAfterOriginal) {
        rod = _srcClip->getRegionOfDefinition(srcTime);

        return true;
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
    double srcTime;
    if (getSourceTime(args.time, &srcTime) != eBeforeAfterOriginal) {
        return false;
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


// Whether the host does not call the clip changed action while it loads a project, so that changedClip only
// sees connections made by the user. The OFX spec counts "loading values from files or presets" as user edits
// (kOfxChangeUserEdited, ofxCore.h), and an old project has no frameRangeInitialised to protect its Frame
// Range: only Natron 2.6 and later are known to skip it. (The same test as FrameHold's.)
bool
FrameRangePlugin::hostSkipsChangedClipOnLoad()
{
    const ImageEffectHostDescription& host = *getImageEffectHostDescription();

    return host.hostName == "fr.inria.Natron" &&
           ( (host.versionMajor > 2) || ( (host.versionMajor == 2) && (host.versionMinor >= 6) ) );
}

// Set Frame Range to the input frame range (Reset, or the Source connected), and mark it as set.
void
FrameRangePlugin::setFrameRangeToInputRange()
{
    const OfxRangeD range = _srcClip->getFrameRange();
    const int min = static_cast<int>( std::floor(range.min) );
    const int max = static_cast<int>( std::ceil(range.max) );

    _frameRange->setValue(min, max);
    _frameRangeInitialised->setValue(true);
    refreshSubLabel(min, max);
}

/** @brief called when a clip has just been changed in some way (a rewire maybe) */
void
FrameRangePlugin::changedClip(const InstanceChangedArgs &args,
                              const std::string &clipName)
{
    if ( (clipName != kOfxImageEffectSimpleSourceClipName) || (args.reason != eChangeUserEdit) ) {
        return;
    }
    // When the Source is connected and Frame Range was never set, set it to the input range. Loading a
    // project is a user edit too (ofxCore.h, kOfxChangeUserEdited), so this only runs on hosts that do not
    // call changedClip while loading, and frameRangeInitialised keeps a Frame Range that was set (#400).
    // Frame Range must also still be its default, (1,1): an old project has no flag, and a value set from
    // a script, an expression or a link does not set it.
    if ( hostSkipsChangedClipOnLoad() && _srcClip && _srcClip->isConnected() &&
         !_frameRangeInitialised->getValue() ) {
        int min, max;
        _frameRange->getValue(min, max);
        if ( (min == kParamFrameRangeDefaultMin) && (max == kParamFrameRangeDefaultMax) ) {
            setFrameRangeToInputRange();

            return;
        }
    }
    // the display time offset is a clip property, which may follow the input
    OfxPointI range = _frameRange->getValue();
    refreshSubLabel(range.x, range.y);
}

void
FrameRangePlugin::refreshSubLabel(int rangeMin, int rangeMax)
{
    char label[80];
    // Frame Range is stored in the OFX time domain (never convert it), but it is shown as the host
    // displays it: add the display time offset of the clip the times refer to (the Output, whose
    // frames are the ones kept). It is 0 on hosts that do not set kOfxImageClipPropDisplayTimeOffset
    // (OFX 1.4 hosts).
    double offset = _dstClip->getDisplayTimeOffset();
    snprintf(label, sizeof(label), "%.10g - %.10g", rangeMin + offset, rangeMax + offset);
    _sublabel->setValue(label);
}

void
FrameRangePlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if ( (paramName == kParamReset) && _srcClip && _srcClip->isConnected() && (args.reason == eChangeUserEdit) ) {
        setFrameRangeToInputRange();
    }
    if ( (paramName == kParamFrameRange) && (args.reason == eChangeUserEdit) ) {
        // a value set by the user (or loaded with a project) is never replaced on connect
        if ( !_frameRangeInitialised->getValue() ) {
            _frameRangeInitialised->setValue(true);
        }
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
    range.max = (std::max)(min, max);

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
        param->setDefault(kParamFrameRangeDefaultMin, kParamFrameRangeDefaultMax);
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
    // before, after (legacy Bounce option id, see FrameRangeBeforeAfter.h)
    defineBeforeAfterParam(desc, page, kParamBefore, kParamBeforeLabel, kParamBeforeHint, eBeforeAfterBlack, true);
    defineBeforeAfterParam(desc, page, kParamAfter, kParamAfterLabel, kParamAfterHint, eBeforeAfterBlack, true);
    // frameRangeInitialised (after the visible params: Frame Range and Reset share a line)
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFrameRangeInitialised);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true); // always secret
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
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
