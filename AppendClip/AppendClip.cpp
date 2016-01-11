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
 * OFX AppendClip plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxNatron.h"

#define kPluginName "AppendClipOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Append one clip to another."
#define kPluginIdentifier "net.sf.openfx.AppendClip"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamFadeIn "fadeIn"
#define kParamFadeInLabel "Fade In"
#define kParamFadeInHint "Number of frames to fade in from black at the beginning of the first clip."

#define kParamFadeOut "fadeOut"
#define kParamFadeOutLabel "Fade Out"
#define kParamFadeOutHint "Number of frames to fade out to black at the end of the last clip."

#define kParamCrossDissolve "crossDissolve"
#define kParamCrossDissolveLabel "Cross Dissolve"
#define kParamCrossDissolveHint "Number of frames to cross-dissolve between clips."

#define kParamCrossDissolve "crossDissolve"
#define kParamCrossDissolveLabel "Cross Dissolve"
#define kParamCrossDissolveHint "Number of frames to cross-dissolve between clips."

#define kParamFirstFrame "firstFrame"
#define kParamFirstFrameLabel "First Frame"
#define kParamFirstFrameHint "Frame to start the first clip at."

#define kParamLastFrame "lastFrame"
#define kParamLastFrameLabel "Last Frame"
#define kParamLastFrameHint "Last frame of the assembled clip (read-only)."

#define kParamUpdateLastFrame "updateLastFrame"
#define kParamUpdateLastFrameLabel "Update"
#define kParamUpdateLastFrameHint "Update lastFrame."

#define kClipSourceCount 64
#define kClipSourceOffset 1 // clip numbers start

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AppendClipPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    AppendClipPlugin(OfxImageEffectHandle handle, bool numerousInputs)
        : ImageEffect(handle)
          , _dstClip(0)
          , _srcClip(numerousInputs ? kClipSourceCount : 2)
    , _fadeIn(0)
    , _fadeOut(0)
    , _crossDissolve(0)
    , _firstFrame(0)
    , _lastFrame(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha));
        for (unsigned i = 0; i < _srcClip.size(); ++i) {
            if (getContext() == OFX::eContextTransition && i < 2) {
                _srcClip[i] = fetchClip(i == 0 ? kOfxImageEffectTransitionSourceFromClipName : kOfxImageEffectTransitionSourceToClipName);
            } else {
                char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
                int clipNumber = i + kClipSourceOffset;
                name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
                name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
                _srcClip[i] = fetchClip(name);
            }
            assert(_srcClip[i] && (_srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGB || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentRGBA || _srcClip[i]->getPixelComponents() == OFX::ePixelComponentAlpha));
        }

        _fadeIn = fetchIntParam(kParamFadeIn);
        _fadeOut = fetchIntParam(kParamFadeOut);
        _crossDissolve = fetchIntParam(kParamCrossDissolve);
        _firstFrame = fetchIntParam(kParamFirstFrame);
        _lastFrame = fetchIntParam(kParamLastFrame);
        assert(_fadeIn && _fadeOut && _crossDissolve && _firstFrame && _lastFrame);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderBase &, const OFX::RenderArguments &args);

    void getSources(int firstFrame,
                    int fadeIn,
                    int fadeOut,
                    int crossDissolve,
                    double time,
                    int *clip0,
                    double *t0,
                    double *alpha0,
                    int *clip1,
                    double *t1,
                    double *alpha1,
                    int *lastFrame = NULL); // if non-NULL, this is the only returned value, and time is not taken into account

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    std::vector<OFX::Clip *> _srcClip;
    OFX::IntParam* _fadeIn;
    OFX::IntParam* _fadeOut;
    OFX::IntParam* _crossDissolve;
    OFX::IntParam* _firstFrame;
    OFX::IntParam* _lastFrame;
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
    OFX::BitDepthEnum srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( ( srcBitDepth != dstBitDepth) || ( srcComponents != dstComponents) ) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}

void
AppendClipPlugin::getSources(int firstFrame,
                             int fadeIn,
                             int fadeOut,
                             int crossDissolve,
                             double time,
                             int *clip0_p,
                             double *t0_p,
                             double *alpha0_p,
                             int *clip1_p,
                             double *t1_p,
                             double *alpha1_p,
                             int *lastFrame) // if non-NULL, this is the only returned value, and time is not taken into account
{
    if (lastFrame) {
        assert(!clip0_p && !t0_p && !alpha0_p && !clip1_p && !t1_p && !alpha1_p);
    }
    // if fadeIn = 0 return first frame of first connected clip if time <= firstFrame, else return back
    int clip0 = -1;
    double t0 = -1;
    double alpha0 = 0;
    int clip1 = -1;
    double t1 = -1;
    double alpha1 = 0.;
    // find the clips where we are (clip0 and clip1)
    int clip0Min = -1;
    int clip0Max = -1;
    int clip0OutMin = firstFrame;
    int clip0OutMax = firstFrame - 1;
    int clip1Min = -1;
    int clip1Max = -1;
    int clip1OutMin = firstFrame;
    int clip1OutMax = firstFrame - 1;
    int lastClip = _srcClip.size() - 1;
    while (lastClip >= 0 && !_srcClip[lastClip]->isConnected()) {
        --lastClip;
    }
    if (lastClip < 0) {
        // no clip, just be black, and lastFrame = firstFrame-1
        if (clip0_p) { *clip0_p = clip0; }
        if (t0_p) { *t0_p = t0; }
        if (alpha0_p) { *alpha0_p = alpha0; }
        if (clip1_p) { *clip1_p = clip1; }
        if (t1_p) { *t1_p = t1; }
        if (alpha1_p) { *alpha1_p = alpha1; }
        if (lastFrame) { *lastFrame = clip1OutMax; }
        return;
    }
    int firstClip = 0;
    while (firstClip < (int)_srcClip.size() && !_srcClip[firstClip]->isConnected()) {
        ++firstClip;
    }
    assert(firstClip < (int)_srcClip.size()); // there is at least one connected clip
    for (int i = firstClip; i <= lastClip; ++i) {
        // loop invariants:
        // *clip0 contains the index of last visited connected clip
        // clip0Min contains the first frame of the last visited and connected clip
        // clip0Max contains the last frame of the last visited and connected clip
        // clip0OutMin contains the output frame corresponding to clip0Min
        // clip0OutMax contains the output frame corresponding to clip0Max
        if (_srcClip[i]->isConnected()) {
            OfxRangeD r = _srcClip[i]->getFrameRange();
            // if this is the first connected clip, and time is before this clip,
            // return either black or this clip (no clip1), depending on fadeIn
            if (clip1 == -1) {
                if (!lastFrame && (time < firstFrame)) {
                    // before the first clip, the solution is trivial
                    clip0 = i;
                    // render clip0
                    t0 = r.min + (time - firstFrame);
                    alpha0 =  (fadeIn == 0);
                    if (clip0_p) { *clip0_p = clip0; }
                    if (t0_p) { *t0_p = t0; }
                    if (alpha0_p) { *alpha0_p = alpha0; }
                    if (clip1_p) { *clip1_p = clip1; }
                    if (t1_p) { *t1_p = t1; }
                    if (alpha1_p) { *alpha1_p = alpha1; }
                    assert(!lastFrame);

                    return;
                }
                clip1 = i;
                clip1Min = r.min;
                clip1Max = r.max;
                clip1OutMin = firstFrame;
                clip1OutMax = std::max(firstFrame + (int)(r.max - r.min),firstFrame);
                
                continue; // proceed to extract clip1 etc.
            }
            // get info for this clip
            int clip2 = i;
            int clip2Min = r.min;
            int clip2Max = r.max;
            assert(clip1OutMax >= clip0OutMax);
            int clip2OutMin = std::max(clip0OutMax + 1, // next clip must start after end of the forelast Clip (never more than 2 clips at the same time)
                                       clip1OutMax + 1 - crossDissolve);
            int clip2OutMax = std::max(clip1OutMax, // clip end should be at least the end of the previous clip
                                       clip2OutMin + (clip2Max - clip2Min));

            // if time is before this clip, we're done
            if (!lastFrame && time < clip2OutMin) {
                break;
            }

            // shift clip0 <- clip1 <- clip2
            clip0 = clip1;
            clip0Min = clip1Min;
            clip0Max = clip1Max;
            clip0OutMin = clip1OutMin;
            clip0OutMax = clip1OutMax;

            clip1 = clip2;
            clip1Min = clip2Min;
            clip1Max = clip2Max;
            clip1OutMin = clip2OutMin;
            clip1OutMax = clip2OutMax;
        }
    }
    // at this point, either:
    // - there is no clip connected: *clip0 == -1, *clip1 == -1 -> this case was treated before
    // - there is only one clip connected, or we are before the second clip: *clip0 == -1, *clip1 != -1
    // - general case: *clip0 != -1, *clip1 != -1: compute blend

    if (lastFrame) {
        // set last frame and exit (used in getFrameRange and changedClip/changedParam)
        *lastFrame = clip1OutMax;
        return;
    }

    // clips should be ordered
    assert(clip0 == -1|| clip1 == -1 || (clip0OutMin <= clip1OutMin && clip0OutMax <= clip1OutMax));
    assert(clip0 == -1|| (clip0Min <= clip0Max && clip0OutMin <= clip0OutMax));
    assert(clip1 == -1|| (clip1Min <= clip1Max && clip1OutMin <= clip1OutMax));
    if (clip0 == -1 && clip1 == -1) {
        assert(false); // treated above, should never happen!
        // no clip, just be black, and lastFrame = firstFrame-1
        if (clip0_p) { *clip0_p = clip0; }
        if (t0_p) { *t0_p = t0; }
        if (alpha0_p) { *alpha0_p = alpha0; }
        if (clip1_p) { *clip1_p = clip1; }
        if (t1_p) { *t1_p = t1; }
        if (alpha1_p) { *alpha1_p = alpha1; }
        return;
    }
    assert(clip1 != -1);
    if ((clip0 == -1) &&
        (clip1 != -1) && time < clip1OutMin) {
        assert(false); // treated above, should never happen!
        // before the first clip, the solution is trivial
        clip0 = clip1;
        clip0Min = clip1Min;
        clip0Max = clip1Max;
        clip0OutMin = clip1OutMin;
        clip0OutMax = clip1OutMax;

        clip1 = -1;
        clip1Min = -1;
        clip1Max = -1;
        clip1OutMin = firstFrame;
        clip1OutMax = firstFrame - 1;
        // render clip0
        t0 = clip1Min + (time - clip1OutMin);
        alpha0 = (fadeIn == 0);
        t1 = -1;
        alpha1 = 0.;
    } else if ((clip0 != -1) && clip0OutMin <= time && time <= clip0OutMax &&
               (clip1 != -1) && clip1OutMin <= time && time <= clip1OutMax) {
        // clip0 and clip1: cross-dissolve
        assert(clip1OutMin + crossDissolve - 1 >= clip0OutMax);
        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = 1. - (time + 1 - clip1OutMin)/(double)(crossDissolve+1);
        assert(0 < alpha0 && alpha0 < 1.);
        t1 = clip1Min + (time - clip1OutMin);
        alpha1 = std::max(0., 1.-alpha0);
    } else if ((clip0 != -1) && clip0OutMin <= time && time <= clip0OutMax) {
        // clip0 only
        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = 1.;
        clip1 = -1;
        t1 = 0.;
        alpha1 = 0.;
    } else if ((clip1 != -1) && clip1OutMin <= time && time <= clip1OutMax) {
        // clip1 only
        clip0 = clip1;
        clip0Min = clip1Min;
        clip0Max = clip1Max;
        clip0OutMin = clip1OutMin;
        clip0OutMax = clip1OutMax;

        clip1 = -1;
        clip1Min = -1;
        clip1Max = -1;
        clip1OutMin = firstFrame;
        clip1OutMax = firstFrame - 1;

        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = 1.;
        t1 = -1;
        alpha1 = 0.;
    } else if ((clip1 != -1) && clip1OutMax < time) {
        // after the last clip, the solution is trivial
        clip0 = clip1;
        clip0Min = clip1Min;
        clip0Max = clip1Max;
        clip0OutMin = clip1OutMin;
        clip0OutMax = clip1OutMax;

        clip1 = -1;
        clip1Min = -1;
        clip1Max = -1;
        clip1OutMin = firstFrame;
        clip1OutMax = firstFrame - 1;

        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = (fadeOut == 0);
        t1 = -1;
        alpha1 = 0.;
    } else {
        assert(false);
    }
    // clips should be ordered
    assert(clip0 == -1|| clip1 == -1 || (clip0OutMin <= clip1OutMin && clip0OutMax <= clip1OutMax));
    assert(clip0 == -1|| (clip0Min <= clip0Max && clip0OutMin <= clip0OutMax));
    assert(clip1 == -1|| (clip1Min <= clip1Max && clip1OutMin <= clip1OutMax));

    // now, check for fade in/fade out
    if (fadeIn != 0) {
        if (clip0 == firstClip && clip0OutMin <= time) {
            assert(clip0OutMin == firstFrame);
            // fadeIn = x means that the first x frames are modified
            if ((time - firstFrame) < fadeIn) {
                // fade in
                assert(alpha0 > 0.);
                alpha0 *= (time - firstFrame + 1)/(fadeIn+1);
                alpha1 *= (time - firstFrame + 1)/(fadeIn+1);
            }
        }
    }
    if (fadeOut != 0) {
        if (clip0 == lastClip && time <= clip0OutMax) {
            assert(clip1 == -1);
            // fadeOut = x means that the last x frames are modified
            if ((clip0OutMax - time) < fadeOut) {
                // fade out
                assert(alpha0 > 0.);
                alpha0 *= (clip0OutMax - time + 1)/(fadeOut+1);
                alpha1 *= (clip0OutMax - time + 1)/(fadeOut+1);
            }
        }
        if (clip1 == lastClip && time <= clip1OutMax) {
            assert(clip0 != lastClip);
            // fadeOut = x means that the last x frames are modified
            if ((clip1OutMax - time) < fadeOut) {
                // fade out
                assert(alpha0 > 0.);
                alpha0 *= (clip1OutMax - time + 1)/(fadeOut+1);
                alpha1 *= (clip1OutMax - time + 1)/(fadeOut+1);
            }
        }
    }
    assert(0 <= alpha0 && alpha0 <=1);
    assert(0 <= alpha1 && alpha1 <1);

    if (clip0_p) { *clip0_p = clip0; }
    if (t0_p) { *t0_p = t0; }
    if (alpha0_p) { *alpha0_p = alpha0; }
    if (clip1_p) { *clip1_p = clip1; }
    if (t1_p) { *t1_p = t1; }
    if (alpha1_p) { *alpha1_p = alpha1; }
}

/* set up and run a processor */
void
AppendClipPlugin::setupAndProcess(OFX::ImageBlenderBase &processor,
                                  const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( _dstClip->fetchImage(args.time) );
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

    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0;
    double t0;
    double alpha0;
    int clip1;
    double t1;
    double alpha1;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, &t0, &alpha0, &clip1, &t1, &alpha1, NULL);

    if ((clip0 == -1 && clip1 == -1) || (alpha0 == 0. && alpha1 == 0.)) {
        // no clip, just fill with black
        fillBlack(*this, args.renderWindow, dst.get());
        return;
    }
    assert(clip0 != clip1);
    if (clip1 == -1 && alpha0 == 1.) {
        // should never happen, since it's identity
        assert(0);
        std::auto_ptr<const OFX::Image> src((_srcClip[clip0] && _srcClip[clip0]->isConnected()) ?
                                            _srcClip[clip0]->fetchImage(t0) : 0);
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

    // fetch the two source images
    std::auto_ptr<const OFX::Image> fromImg((clip0 != -1 && _srcClip[clip0] && _srcClip[clip0]->isConnected()) ?
                                            _srcClip[clip0]->fetchImage(t0) : 0);
    std::auto_ptr<const OFX::Image> toImg((clip1 != -1 && _srcClip[clip1] && _srcClip[clip1]->isConnected()) ?
                                          _srcClip[clip1]->fetchImage(t1) : 0);

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
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the scales
    assert(0 < alpha0 && alpha0 <= 1 && 0 <= alpha1 && alpha1 < 1);
    assert(toImg.get() || (alpha1 == 0));
    assert(fromImg.get());
    processor.setBlend(1. - alpha0);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
AppendClipPlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        assert(kSupportsMultipleClipPARs   || _srcClip[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
        assert(kSupportsMultipleClipDepths || _srcClip[i]->getPixelDepth()       == _dstClip->getPixelDepth());
    }
    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args);
    }  else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
AppendClipPlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte:
            renderForBitDepth<unsigned char, nComponents, 255>(args);
            break;

        case OFX::eBitDepthUShort:
            renderForBitDepth<unsigned short, nComponents, 65535>(args);
            break;

        case OFX::eBitDepthFloat:
            renderForBitDepth<float, nComponents, 1>(args);
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
AppendClipPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    OFX::ImageBlender<PIX, nComponents> fred(*this);
    setupAndProcess(fred, args);
}

// overridden is identity
bool
AppendClipPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                             OFX::Clip * &identityClip,
                             double &identityTime)
{
    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0;
    double t0;
    double alpha0;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, &t0, &alpha0, NULL, NULL, NULL, NULL);
    assert(clip0 >= -1 && clip0 < (int)_srcClip.size());
    if (clip0 >= 0 && clip0 < (int)_srcClip.size() && alpha0 == 1.) {
        identityClip = _srcClip[clip0];
        identityTime = t0;

        return true;
    }
    // nope, identity we isnt
    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
AppendClipPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                       OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0, clip1;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, NULL, NULL, &clip1, NULL, NULL, NULL);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if ((int)i != clip0 && (int)i != clip1) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
}

void
AppendClipPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                                  OFX::FramesNeededSetter &frames)
{
    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0, clip1;
    double t0, t1;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, &t0, NULL, &clip1, &t1, NULL, NULL);
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        OfxRangeD range;
        if (i == (unsigned)clip0) {
            range.min = t0;
            range.max = t0;
        } else if (i == (unsigned)clip1) {
            range.min = t1;
            range.max = t1;
        } else {
            // empty range
            range.min = firstFrame;
            range.max = firstFrame - 1;
        }
        frames.setFramesNeeded(*_srcClip[i], range);
    }
}

void
AppendClipPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    OFX::PixelComponentEnum outputComps = _dstClip->getPixelComponents();
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        clipPreferences.setClipComponents(*_srcClip[i], outputComps);
    }
}

bool
AppendClipPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0;
    double t0;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, &t0, NULL, NULL, NULL, NULL, NULL);
    assert(clip0 >= -1 && clip0 < (int)_srcClip.size());
    if (clip0 >= 0 && clip0 < (int)_srcClip.size()) {
        rod = _srcClip[clip0]->getRegionOfDefinition(t0);
        return true;
    }
    return false;
}

void
AppendClipPlugin::changedClip(const OFX::InstanceChangedArgs &args, const std::string &/*clipName*/)
{
    const double time = args.time;
    int firstFrame;
    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int lastFrame;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, -1, NULL, NULL, NULL, NULL, NULL, NULL, &lastFrame);
    _lastFrame->setValue(lastFrame);
}

void
AppendClipPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    const double time = args.time;
    if (paramName != kParamLastFrame && args.reason == OFX::eChangeUserEdit) {
        int firstFrame;
        _firstFrame->getValueAtTime(time, firstFrame);
        int fadeIn;
        _fadeIn->getValueAtTime(time, fadeIn);
        int fadeOut;
        _fadeOut->getValueAtTime(time, fadeOut);
        int crossDissolve;
        _crossDissolve->getValueAtTime(time, crossDissolve);
        int lastFrame;
        getSources(firstFrame, fadeIn, fadeOut, crossDissolve, -1, NULL, NULL, NULL, NULL, NULL, NULL, &lastFrame);
        _lastFrame->setValue(lastFrame);
    }
}

/* override the time domain action, only for the general context */
bool
AppendClipPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == OFX::eContextGeneral);
    int firstFrame;
    _firstFrame->getValue(firstFrame);
    int fadeIn;
    _fadeIn->getValue(fadeIn);
    int fadeOut;
    _fadeOut->getValue(fadeOut);
    int crossDissolve;
    _crossDissolve->getValue(crossDissolve);
    int lastFrame;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, -1, NULL, NULL, NULL, NULL, NULL, NULL, &lastFrame);
    if (lastFrame < firstFrame) {
        return false;
    }
    range.min = firstFrame;
    range.max = lastFrame;

    return true;
}

mDeclarePluginFactory(AppendClipPluginFactory, {}, {}
                      );
using namespace OFX;

void
AppendClipPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
AppendClipPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    int clipSourceCount = numerousInputs ? kClipSourceCount : 2;

    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceFromClipName);
        } else {
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int i = 0;
            int clipNumber = i + kClipSourceOffset;
            name[0] = '0' + clipNumber;
            name[1] = 0;
            srcClip = desc.defineClip(name);
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentXY);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(true);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceToClipName);
        } else {
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int i = 1;
            int clipNumber = i + kClipSourceOffset;
            name[0] = '0' + clipNumber;
            name[1] = 0;
            srcClip = desc.defineClip(name);
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentXY);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(true);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    if (numerousInputs) {
        for (int i = 2; i < clipSourceCount; ++i) {
            assert(i < 100);
            ClipDescriptor *srcClip;
            char name[3] = { 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            int clipNumber = i + kClipSourceOffset;
            assert(clipNumber < 100);
            name[0] = (clipNumber < 10) ? ('0' + clipNumber) : ('0' + clipNumber / 10);
            name[1] = (clipNumber < 10) ?                  0 : ('0' + clipNumber % 10);
            srcClip = desc.defineClip(name);
            srcClip->setOptional(true);
            srcClip->addSupportedComponent(ePixelComponentNone);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentRGB);
            srcClip->addSupportedComponent(ePixelComponentXY);
            srcClip->addSupportedComponent(ePixelComponentAlpha);
            srcClip->setTemporalClipAccess(true );
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeIn);
        param->setLabel(kParamFadeInLabel);
        param->setHint(kParamFadeInHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeOut);
        param->setLabel(kParamFadeOutLabel);
        param->setHint(kParamFadeOutHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamCrossDissolve);
        param->setLabel(kParamCrossDissolveLabel);
        param->setHint(kParamCrossDissolveHint);
        param->setDisplayRange(0, 50);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setDefault(1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        param->setHint(kParamLastFrameHint);
        param->setDefault(0);
        param->setEnabled(false);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamUpdateLastFrame);
        param->setLabel(kParamUpdateLastFrameLabel);
        param->setHint(kParamUpdateLastFrameHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
AppendClipPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new AppendClipPlugin(handle, numerousInputs);
}


static AppendClipPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

