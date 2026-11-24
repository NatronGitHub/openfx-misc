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
 * OFX FrameHold plugin.
 */

#include <algorithm>
#include <cmath>
#include <string>
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
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "FrameHoldOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Hold a given frame for the input clip indefinitely, or use a subsample of the input frames and hold them for several frames."
#define kPluginIdentifier "net.sf.openfx.FrameHold"
// History:
// version 1.0: initial version
// version 1.1: render copies the Source at the source time (it wrote nothing): the spec says that the
//              host should not call render after isIdentity returns true, but some hosts (e.g. hostDemo) do;
//              a NULL Source image (disconnected input) renders black instead of crashing (SupportExt copyPixels);
//              the sublabel shows the frame as the host displays it (kOfxImageClipPropDisplayTimeOffset, display only);
//              a "Set to Current Frame" button sets First Frame; on Natron >= 2.6, connecting the Source sets First Frame
//              to the current frame if it was never set (#307)
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"
#define kParamFirstFrameHint "Reference input frame (the frame to hold if increment is 0). On Natron 2.6 and later, connecting the Source sets it to the current frame if it was never set."
#define kParamFirstFrameDefault 0
// Whether First Frame was set: by the user, by Set to Current Frame, or from the current frame when the Source
// was connected. Secret, saved with the project.
#define kParamFirstFrameInitialised "firstFrameInitialised"
#define kParamSetToCurrentFrame "setToCurrentFrame"
#define kParamSetToCurrentFrameLabel "Set to Current Frame"
#define kParamSetToCurrentFrameHint "Set First Frame to the current frame, within the frame range of the Source (a key at that frame if First Frame is animated)."
#define kParamIncrement "increment"
#define kParamIncrementLabel "Increment"
#define kParamIncrementHint "If increment is 0, only the \"firstFrame\" will be held. If it is positive, every multiple of \"increment\" plus \"firstFrame\" will be held for \"increment\" frames afterwards (before if it is negative)."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameHoldPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    FrameHoldPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _firstFrame(NULL)
        , _increment(NULL)
        , _firstFrameInitialised(NULL)
        , _sublabel(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );

        _firstFrame = fetchIntParam(kParamFirstFrame);
        _increment = fetchIntParam(kParamIncrement);
        _firstFrameInitialised = fetchBooleanParam(kParamFirstFrameInitialised);
        _sublabel = fetchStringParam(kNatronOfxParamStringSublabelName);
        assert(_firstFrame && _increment && _firstFrameInitialised && _sublabel);

        // finally
        syncPrivateData();
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        updateSublabel(0.);
    }

private:
    double getSourceTime(double time) const;

    void updateSublabel(double time);

    void setFirstFrameToCurrentFrame(double time);

    static bool hostSkipsChangedClipOnLoad();

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;            /**< @brief Mandated output clips */
    Clip *_srcClip;            /**< @brief Mandated input clips */
    IntParam  *_firstFrame;
    IntParam  *_increment;
    BooleanParam *_firstFrameInitialised;
    StringParam *_sublabel;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// figure the frame we should be retiming from
double
FrameHoldPlugin::getSourceTime(double t) const
{
    int firstFrame, increment;

    _firstFrame->getValueAtTime(t, firstFrame);
    _increment->getValueAtTime(t, increment);

    if (increment == 0) {
        return firstFrame;
    }

    return firstFrame + increment * std::floor( (t - firstFrame) / increment );
}

void
FrameHoldPlugin::updateSublabel(double time)
{
    char label[80];
    int firstFrame, increment;

    _firstFrame->getValueAtTime(time, firstFrame);
    _increment->getValueAtTime(time, increment);
    // First Frame is stored in the OFX time domain of the Source clip (never convert it), but it is shown
    // as the host displays it: add the display time offset of the clip the time refers to (the Source).
    // It is 0 on hosts that do not set kOfxImageClipPropDisplayTimeOffset (OFX 1.4 hosts).
    Clip* clip = _srcClip ? _srcClip : _dstClip;
    double displayFirstFrame = firstFrame + clip->getDisplayTimeOffset();
    if (increment == 0) {
        snprintf(label, sizeof(label), "frame %.10g", displayFirstFrame);
    } else {
        snprintf(label, sizeof(label), "frame %.10g+n*%d", displayFirstFrame, increment);
    }
    _sublabel->setValue(label);
}

// Set First Frame to the frame at time (the current frame of the host, from an instance changed action),
// rounded to the nearest frame and clamped to the frame range of the Source if it is connected (as
// FrameRange does). If First Frame is animated, the key is at the current frame (rounded, not clamped),
// with the clamped value.
void
FrameHoldPlugin::setFirstFrameToCurrentFrame(double time)
{
    const int currentFrame = static_cast<int>( std::floor(time + 0.5) );
    int frame = currentFrame;

    if ( _srcClip && _srcClip->isConnected() ) {
        const OfxRangeD range = _srcClip->getFrameRange();
        const double first = std::ceil(range.min);
        const double last = std::floor(range.max);
        if (first <= last) {
            frame = static_cast<int>( std::max( first, std::min(last, static_cast<double>(frame)) ) );
        }
    }
    if (_firstFrame->getNumKeys() > 0) {
        _firstFrame->setValueAtTime(currentFrame, frame);
    } else {
        _firstFrame->setValue(frame);
    }
    _firstFrameInitialised->setValue(true);
    // the instance changed action of this plugin edit does not update the sublabel
    updateSublabel(time);
}

// Whether the host does not call the clip changed action while it loads a project, so that changedClip only
// sees connections made by the user. The OFX spec counts "loading values from files or presets" as user edits
// (kOfxChangeUserEdited, ofxCore.h), and an old project has no firstFrameInitialised to protect its First
// Frame: only Natron 2.6 and later are known to skip it.
bool
FrameHoldPlugin::hostSkipsChangedClipOnLoad()
{
    const ImageEffectHostDescription& host = *getImageEffectHostDescription();

    return host.hostName == "fr.inria.Natron" &&
           ( (host.versionMajor > 2) || ( (host.versionMajor == 2) && (host.versionMinor >= 6) ) );
}

void
FrameHoldPlugin::getFramesNeeded(const FramesNeededArguments &args,
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
FrameHoldPlugin::render(const RenderArguments &args)
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
FrameHoldPlugin::isIdentity(const IsIdentityArguments &args,
                            Clip * &identityClip,
                            double &identityTime
                            , int& /*view*/, std::string& /*plane*/)
{
    identityClip = _srcClip;
    identityTime = getSourceTime(args.time);

    return true;
}

bool
FrameHoldPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    rod = _srcClip->getRegionOfDefinition( getSourceTime(args.time) );

    return true;
}

void
FrameHoldPlugin::changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName)
{
    if ( (paramName == kParamFirstFrame) && (args.reason == eChangeUserEdit) ) {
        // a value set by the user (or loaded with a project) is never replaced on connect
        if ( !_firstFrameInitialised->getValue() ) {
            _firstFrameInitialised->setValue(true);
        }
        updateSublabel(args.time);
    } else if ( (paramName == kParamIncrement) && (args.reason == eChangeUserEdit) ) {
        updateSublabel(args.time);
    } else if ( (paramName == kParamSetToCurrentFrame) && (args.reason == eChangeUserEdit) ) {
        setFirstFrameToCurrentFrame(args.time);
    }
}

void
FrameHoldPlugin::changedClip(const InstanceChangedArgs &args,
                             const std::string &clipName)
{
    if ( (clipName != kOfxImageEffectSimpleSourceClipName) || (args.reason != eChangeUserEdit) ) {
        return;
    }
    // As FrameRange does with its range: when the Source is connected and First Frame was never set, hold the
    // current frame rather than frame 0, which is usually outside the input range (#307). Loading a project
    // is a user edit too (ofxCore.h, kOfxChangeUserEdited), so this only runs on hosts that do not call
    // changedClip while loading, and firstFrameInitialised keeps a First Frame that was set. First Frame must
    // also still be its default: an old project has no flag, and a value set from a script, an expression or
    // a link does not set it.
    if ( hostSkipsChangedClipOnLoad() && _srcClip && _srcClip->isConnected() &&
         !_firstFrameInitialised->getValue() && (_firstFrame->getNumKeys() == 0) &&
         (_firstFrame->getValue() == kParamFirstFrameDefault) ) {
        setFirstFrameToCurrentFrame(args.time);

        return;
    }
    // the display time offset is a clip property: refresh the sublabel when the Source changes
    updateSublabel(args.time);
}

mDeclarePluginFactory(FrameHoldPluginFactory,; , {});
void
FrameHoldPluginFactory::load()
{
    ofxsThreadSuiteCheck();
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!getImageEffectHostDescription()->temporalClipAccess) {
        throw Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void
FrameHoldPluginFactory::describe(ImageEffectDescriptor &desc)
{
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
void
FrameHoldPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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

    // firstFrame
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setDefault(kParamFirstFrameDefault);
        param->setRange(INT_MIN, INT_MAX);
        param->setDisplayRange(INT_MIN, INT_MAX);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // setToCurrentFrame
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamSetToCurrentFrame);
        param->setLabel(kParamSetToCurrentFrameLabel);
        param->setHint(kParamSetToCurrentFrameHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // firstFrameInitialised
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFirstFrameInitialised);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true); // always secret
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // increment
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIncrement);
        param->setLabel(kParamIncrementLabel);
        param->setHint(kParamIncrementHint);
        param->setDefault(0);
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, INT_MAX);
        param->setAnimates(true);
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
        param->setDefault("frame 0");
        if (page) {
            page->addChild(*param);
        }
    }
} // FrameHoldPluginFactory::describeInContext

/** @brief The create instance function, the plugin must return an object derived from the \ref ImageEffect class */
ImageEffect*
FrameHoldPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new FrameHoldPlugin(handle);
}

static FrameHoldPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
