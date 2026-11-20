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
 * OFX AppendClip plugin.
 */

#include <cmath>
#include <algorithm>
#ifdef DEBUG
#include <iostream>
#endif

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "AppendClipOFX"
#define kPluginGrouping "Time"
#define kPluginDescription \
    "Append one clip to another.\n" \
    "See also: https://web.archive.org/web/20220627030729/http://www.opticalenquiry.com/nuke/index.php?title=AppendClip"

#define kPluginIdentifier "net.sf.openfx.AppendClip"
// History:
// version 1.0: initial version
// version 2.0: during a cross-dissolve, weight the second clip by its own factor (including fade in/out),
//              instead of 1 minus the weight of the first clip, and include its RoD in the output RoD;
//              a NULL Source image (disconnected input) renders black instead of crashing (SupportExt copyPixels)
// version 1.1: (the deprecated version 1) a NULL Source image renders black instead of crashing
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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

static
std::string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    std::string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb = (char)( '0' + (j % 10) ) + nb;
    }

    return nb;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief base class for the weighted sum of two images: dst = weight0 * from + weight1 * to */
class WeightedBlenderBase
    : public ImageBlenderBase
{
protected:
    float _weight0;
    float _weight1;

public:
    WeightedBlenderBase(ImageEffect &instance)
        : ImageBlenderBase(instance)
        , _weight0(1.f)
        , _weight1(0.f)
    {
    }

    void setWeights(float weight0,
                    float weight1)
    {
        _weight0 = weight0;
        _weight1 = weight1;
    }
};

/** @brief weighted sum of two images (a missing image is black), without renormalization */
template <class PIX, int nComponents>
class WeightedBlender
    : public WeightedBlenderBase
{
public:
    WeightedBlender(ImageEffect &instance)
        : WeightedBlenderBase(instance)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        const float weight0 = _weight0;
        const float weight1 = _weight1;

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *fromPix = (const PIX *)  (_fromImg ? _fromImg->getPixelAddress(x, y) : 0);
                const PIX *toPix   = (const PIX *)  (_toImg   ? _toImg->getPixelAddress(x, y)   : 0);

                for (int c = 0; c < nComponents; c++) {
                    float v = 0.f;
                    if (fromPix) {
                        v += fromPix[c] * weight0;
                    }
                    if (toPix) {
                        v += toPix[c] * weight1;
                    }
                    dstPix[c] = PIX(v);
                }

                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class AppendClipPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    AppendClipPlugin(OfxImageEffectHandle handle,
                     bool numerousInputs,
                     unsigned int majorVersion)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(numerousInputs ? kClipSourceCount : 2)
        , _fadeIn(NULL)
        , _fadeOut(NULL)
        , _crossDissolve(NULL)
        , _firstFrame(NULL)
        , _lastFrame(NULL)
        , _legacyCrossDissolve(majorVersion < 2)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        for (unsigned j = 0; j < _srcClip.size(); ++j) {
            if ( (getContext() == eContextTransition) && (j < 2) ) {
                _srcClip[j] = fetchClip(j == 0 ? kOfxImageEffectTransitionSourceFromClipName : kOfxImageEffectTransitionSourceToClipName);
            } else {
                _srcClip[j] = fetchClip( unsignedToString(j + kClipSourceOffset) );
            }
            assert( _srcClip[j] && (_srcClip[j]->getPixelComponents() == ePixelComponentRGB || _srcClip[j]->getPixelComponents() == ePixelComponentRGBA || _srcClip[j]->getPixelComponents() == ePixelComponentAlpha) );
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
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    /* set up and run a processor */
    void setupAndProcess(ImageBlenderBase &, const RenderArguments &args);

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

    // getSources() with the parameter values at the given time
    void getSourcesAtTime(double time,
                          int *clip0,
                          double *t0,
                          double *alpha0,
                          int *clip1,
                          double *t1,
                          double *alpha1);

    // the last output frame, computed by getSources() with the parameter values at the given time
    int getLastFrameAtTime(double time);

private:

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    std::vector<Clip *> _srcClip;
    IntParam* _fadeIn;
    IntParam* _fadeOut;
    IntParam* _crossDissolve;
    IntParam* _firstFrame;
    IntParam* _lastFrame;
    bool _legacyCrossDissolve; // version 1 cross-dissolve: clip1 is weighted by 1-alpha0, and the RoD is clip0's only
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

#ifndef NDEBUG
// make sure components are sane
static void
checkComponents(const Image &src,
                BitDepthEnum dstBitDepth,
                PixelComponentEnum dstComponents)
{
    BitDepthEnum srcBitDepth     = src.getPixelDepth();
    PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( ( srcBitDepth != dstBitDepth) || ( srcComponents != dstComponents) ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}
#endif

// copy the getSources() results to the non-NULL output pointers
static void
setSources(int clip0,
           double t0,
           double alpha0,
           int clip1,
           double t1,
           double alpha1,
           int *clip0_p,
           double *t0_p,
           double *alpha0_p,
           int *clip1_p,
           double *t1_p,
           double *alpha1_p)
{
    if (clip0_p) {
        *clip0_p = clip0;
    }
    if (t0_p) {
        *t0_p = t0;
    }
    if (alpha0_p) {
        *alpha0_p = alpha0;
    }
    if (clip1_p) {
        *clip1_p = clip1;
    }
    if (t1_p) {
        *t1_p = t1;
    }
    if (alpha1_p) {
        *alpha1_p = alpha1;
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
    int lastClip = static_cast<int>( _srcClip.size() ) - 1;
    while ( lastClip >= 0 && !_srcClip[lastClip]->isConnected() ) {
        --lastClip;
    }
    if (lastClip < 0) {
        // no clip, just be black, and lastFrame = firstFrame-1
        setSources(clip0, t0, alpha0, clip1, t1, alpha1, clip0_p, t0_p, alpha0_p, clip1_p, t1_p, alpha1_p);
        if (lastFrame) {
            *lastFrame = clip1OutMax;
        }

        return;
    }
    int firstClip = 0;
    while ( firstClip < (int)_srcClip.size() && !_srcClip[firstClip]->isConnected() ) {
        ++firstClip;
    }
    assert( firstClip < (int)_srcClip.size() ); // there is at least one connected clip
    for (int i = firstClip; i <= lastClip; ++i) {
        // loop invariants:
        // *clip0 contains the index of last visited connected clip
        // clip0Min contains the first frame of the last visited and connected clip
        // clip0Max contains the last frame of the last visited and connected clip
        // clip0OutMin contains the output frame corresponding to clip0Min
        // clip0OutMax contains the output frame corresponding to clip0Max
        if ( _srcClip[i]->isConnected() ) {
            OfxRangeD r = _srcClip[i]->getFrameRange();
            // if this is the first connected clip, and time is before this clip,
            // return either black or this clip (no clip1), depending on fadeIn
            if (clip1 == -1) {
                if ( !lastFrame && (time < firstFrame) ) {
                    // before the first clip, the solution is trivial
                    clip0 = i;
                    // render clip0
                    t0 = r.min + (time - firstFrame);
                    alpha0 =  (fadeIn == 0);
                    setSources(clip0, t0, alpha0, clip1, t1, alpha1, clip0_p, t0_p, alpha0_p, clip1_p, t1_p, alpha1_p);
                    assert(!lastFrame);

                    return;
                }
                clip1 = i;
                clip1Min = (int)r.min;
                clip1Max = (int)r.max;
                clip1OutMin = firstFrame;
                clip1OutMax = (std::max)(firstFrame + (int)(r.max - r.min), firstFrame);

                continue; // proceed to extract clip1 etc.
            }
            // get info for this clip
            int clip2 = i;
            int clip2Min = (int)r.min;
            int clip2Max = (int)r.max;
            assert(clip1OutMax >= clip0OutMax);
            int clip2OutMin = (std::max)(clip0OutMax + 1, // next clip must start after end of the forelast Clip (never more than 2 clips at the same time)
                                       clip1OutMax + 1 - crossDissolve);
            int clip2OutMax = (std::max)( clip1OutMax, // clip end should be at least the end of the previous clip
                                        clip2OutMin + (clip2Max - clip2Min) );

            // if time is before this clip, we're done
            if ( !lastFrame && (time < clip2OutMin) ) {
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
    assert( clip0 == -1 || clip1 == -1 || (clip0OutMin <= clip1OutMin && clip0OutMax <= clip1OutMax) );
    assert( clip0 == -1 || (clip0Min <= clip0Max && clip0OutMin <= clip0OutMax) );
    assert( clip1 == -1 || (clip1Min <= clip1Max && clip1OutMin <= clip1OutMax) );
    // the first connected clip is always visited, and "no clip" and "before the first clip" were treated above
    assert(clip1 != -1);
    assert( (clip0 != -1) || (time >= clip1OutMin) );
    if ( (clip0 != -1) && (clip0OutMin <= time) && (time <= clip0OutMax) &&
                (clip1 != -1) && ( clip1OutMin <= time) && ( time <= clip1OutMax) ) {
        // clip0 and clip1: cross-dissolve
        assert(clip1OutMin + crossDissolve - 1 >= clip0OutMax);
        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = 1. - (time + 1 - clip1OutMin) / (double)(crossDissolve + 1);
        assert(0 < alpha0 && alpha0 < 1.);
        t1 = clip1Min + (time - clip1OutMin);
        alpha1 = (std::max)(0., 1. - alpha0);
    } else if ( (clip0 != -1) && (clip0OutMin <= time) && (time <= clip0OutMax) ) {
        // clip0 only
        t0 = clip0Min + (time - clip0OutMin);
        alpha0 = 1.;
        clip1 = -1;
        t1 = 0.;
        alpha1 = 0.;
    } else if ( (clip1 != -1) && (clip1OutMin <= time) && (time <= clip1OutMax) ) {
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
    } else if ( (clip1 != -1) && (clip1OutMax < time) ) {
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
    assert( clip0 == -1 || clip1 == -1 || (clip0OutMin <= clip1OutMin && clip0OutMax <= clip1OutMax) );
    assert( clip0 == -1 || (clip0Min <= clip0Max && clip0OutMin <= clip0OutMax) );
    assert( clip1 == -1 || (clip1Min <= clip1Max && clip1OutMin <= clip1OutMax) );

    // now, check for fade in/fade out
    if (fadeIn != 0) {
        if ( (clip0 == firstClip) && (clip0OutMin <= time) ) {
            assert(clip0OutMin == firstFrame);
            // fadeIn = x means that the first x frames are modified
            if ( (time - firstFrame) < fadeIn ) {
                // fade in
                assert(alpha0 > 0.);
                alpha0 *= (time - firstFrame + 1) / (fadeIn + 1);
                alpha1 *= (time - firstFrame + 1) / (fadeIn + 1);
            }
        }
    }
    if (fadeOut != 0) {
        if ( (clip0 == lastClip) && (time <= clip0OutMax) ) {
            assert(clip1 == -1);
            // fadeOut = x means that the last x frames are modified
            if ( (clip0OutMax - time) < fadeOut ) {
                // fade out
                assert(alpha0 > 0.);
                alpha0 *= (clip0OutMax - time + 1) / (fadeOut + 1);
                alpha1 *= (clip0OutMax - time + 1) / (fadeOut + 1);
            }
        }
        if ( (clip1 == lastClip) && (time <= clip1OutMax) ) {
            assert(clip0 != lastClip);
            // fadeOut = x means that the last x frames are modified
            if ( (clip1OutMax - time) < fadeOut ) {
                // fade out
                assert(alpha0 > 0.);
                alpha0 *= (clip1OutMax - time + 1) / (fadeOut + 1);
                alpha1 *= (clip1OutMax - time + 1) / (fadeOut + 1);
            }
        }
    }
    assert(0 <= alpha0 && alpha0 <= 1);
    assert(0 <= alpha1 && alpha1 < 1);

    setSources(clip0, t0, alpha0, clip1, t1, alpha1, clip0_p, t0_p, alpha0_p, clip1_p, t1_p, alpha1_p);
} // AppendClipPlugin::getSources

void
AppendClipPlugin::getSourcesAtTime(double time,
                                   int *clip0,
                                   double *t0,
                                   double *alpha0,
                                   int *clip1,
                                   double *t1,
                                   double *alpha1)
{
    int firstFrame = _firstFrame->getValueAtTime(time);
    int fadeIn = _fadeIn->getValueAtTime(time);
    int fadeOut = _fadeOut->getValueAtTime(time);
    int crossDissolve = _crossDissolve->getValueAtTime(time);

    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, clip0, t0, alpha0, clip1, t1, alpha1, NULL);
}

int
AppendClipPlugin::getLastFrameAtTime(double time)
{
    int firstFrame = _firstFrame->getValueAtTime(time);
    int fadeIn = _fadeIn->getValueAtTime(time);
    int fadeOut = _fadeOut->getValueAtTime(time);
    int crossDissolve = _crossDissolve->getValueAtTime(time);
    int lastFrame;

    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, -1, NULL, NULL, NULL, NULL, NULL, NULL, &lastFrame);

    return lastFrame;
}

/* set up and run a processor */
void
AppendClipPlugin::setupAndProcess(ImageBlenderBase &processor,
                                  const RenderArguments &args)
{
    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif
    
    const double time = args.time;
    int clip0;
    double t0;
    double alpha0;
    int clip1;
    double t1;
    double alpha1;
    getSourcesAtTime(time, &clip0, &t0, &alpha0, &clip1, &t1, &alpha1);

    if ( ( (clip0 == -1) && (clip1 == -1) ) || ( (alpha0 == 0.) && (alpha1 == 0.) ) ) {
        // no clip, just fill with black
        fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

        return;
    }
    assert(clip0 != clip1);
    if ( (clip1 == -1) && (alpha0 == 1.) ) {
        // should never happen, since it's identity, but it still may happen (Resolve)
        //assert(0);
        auto_ptr<const Image> src( ( _srcClip[clip0] && _srcClip[clip0]->isConnected() ) ?
                                        _srcClip[clip0]->fetchImage(t0) : 0 );
#     ifndef NDEBUG
        if ( src.get() ) {
            checkBadRenderScaleOrField(src, args);
            BitDepthEnum srcBitDepth      = src->getPixelDepth();
            PixelComponentEnum srcComponents = src->getPixelComponents();
            if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
                throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
#     endif
        copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );

        return;
    }

    // fetch the two source images
    auto_ptr<const Image> fromImg( ( clip0 != -1 && _srcClip[clip0] && _srcClip[clip0]->isConnected() ) ?
                                        _srcClip[clip0]->fetchImage(t0) : 0 );
    auto_ptr<const Image> toImg( ( clip1 != -1 && _srcClip[clip1] && _srcClip[clip1]->isConnected() ) ?
                                      _srcClip[clip1]->fetchImage(t1) : 0 );

# ifndef NDEBUG
    // make sure bit depths are sane
    if ( fromImg.get() ) {
        checkBadRenderScaleOrField(fromImg, args);
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if ( toImg.get() ) {
        checkBadRenderScaleOrField(toImg, args);
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }
# endif

    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // set the scales
    assert(0 < alpha0 && alpha0 <= 1 && 0 <= alpha1 && alpha1 < 1);
    assert( toImg.get() || (alpha1 == 0) );
    assert( fromImg.get() );
    if (_legacyCrossDissolve) {
        // version 1 (kept so that old projects render the same): clip1 is weighted by 1-alpha0,
        // which ignores the fade in/out applied to alpha1
        processor.setBlend(static_cast<float>(1. - alpha0));
    } else {
        WeightedBlenderBase* weighted = dynamic_cast<WeightedBlenderBase*>(&processor);
        assert(weighted);
        if (!weighted) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        weighted->setWeights( static_cast<float>(alpha0), static_cast<float>(alpha1) );
    }

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // AppendClipPlugin::setupAndProcess

// the overridden render function
void
AppendClipPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        assert( kSupportsMultipleClipPARs   || !_srcClip[i] || !_srcClip[i]->isConnected() || _srcClip[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
        assert( kSupportsMultipleClipDepths || !_srcClip[i] || !_srcClip[i]->isConnected() || _srcClip[i]->getPixelDepth()       == _dstClip->getPixelDepth() );
    }
    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
    }  else {
#endif
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
AppendClipPlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
AppendClipPlugin::renderForBitDepth(const RenderArguments &args)
{
    // setupAndProcess() sets the blend factor of ImageBlender (version 1) or the weights of WeightedBlender
    if (_legacyCrossDissolve) {
        ImageBlender<PIX, nComponents> fred(*this);
        setupAndProcess(fred, args);
    } else {
        WeightedBlender<PIX, nComponents> fred(*this);
        setupAndProcess(fred, args);
    }
}

// overridden is identity
bool
AppendClipPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    int clip0;
    double t0;
    double alpha0;
    getSourcesAtTime(time, &clip0, &t0, &alpha0, NULL, NULL, NULL);
    assert( clip0 >= -1 && clip0 < (int)_srcClip.size() );
    if ( (clip0 >= 0) && ( clip0 < (int)_srcClip.size() ) && (alpha0 == 1.) ) {
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
AppendClipPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                       RegionOfInterestSetter &rois)
{
    const double time = args.time;
    int clip0, clip1;
    getSourcesAtTime(time, &clip0, NULL, NULL, &clip1, NULL, NULL);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if ( ( (int)i != clip0 ) && ( (int)i != clip1 ) ) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
}

void
AppendClipPlugin::getFramesNeeded(const FramesNeededArguments &args,
                                  FramesNeededSetter &frames)
{
    const double time = args.time;
    int clip0, clip1;
    double t0, t1;
    getSourcesAtTime(time, &clip0, &t0, NULL, &clip1, &t1, NULL);
    const int firstFrame = _firstFrame->getValueAtTime(time);
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
AppendClipPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        clipPreferences.setClipComponents(*_srcClip[i], outputComps);
    }
}

bool
AppendClipPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double time = args.time;
    int clip0, clip1;
    double t0, t1;
    double alpha1;
    getSourcesAtTime(time, &clip0, &t0, NULL, &clip1, &t1, &alpha1);
    assert( clip0 >= -1 && clip0 < (int)_srcClip.size() );
    if ( (clip0 >= 0) && ( clip0 < (int)_srcClip.size() ) ) {
        rod = _srcClip[clip0]->getRegionOfDefinition(t0);
        // during a cross-dissolve, the RoD also contains clip1's RoD (version 1 only used the RoD of clip0)
        if ( !_legacyCrossDissolve && ( clip1 >= 0) && ( clip1 < (int)_srcClip.size() ) && (alpha1 > 0.) &&
             _srcClip[clip1]->isConnected() ) {
            Coords::rectBoundingBox(_srcClip[clip1]->getRegionOfDefinition(t1), rod, &rod);
        }

        return true;
    }

    return false;
}

void
AppendClipPlugin::changedClip(const InstanceChangedArgs &args,
                              const std::string & /*clipName*/)
{
    _lastFrame->setValue( getLastFrameAtTime(args.time) );
}

void
AppendClipPlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    const double time = args.time;

    if ( (paramName != kParamLastFrame) && (args.reason == eChangeUserEdit) ) {
        _lastFrame->setValue( getLastFrameAtTime(time) );
    }
}

/* override the time domain action, only for the general context */
bool
AppendClipPlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == eContextGeneral);
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

mDeclarePluginFactoryVersioned(AppendClipPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
AppendClipPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

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

template<unsigned int majorVersion>
void
AppendClipPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
#ifdef OFX_EXTENSIONS_NATRON
    bool numerousInputs =  (getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);
#else
    bool numerousInputs = false;
#endif
    unsigned clipSourceCount = numerousInputs ? kClipSourceCount : 2;

    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceFromClipName);
        } else {
            unsigned i = 0;
            srcClip = desc.defineClip( unsignedToString(i + kClipSourceOffset) );
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
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
            unsigned i = 1;
            srcClip = desc.defineClip( unsignedToString(i + kClipSourceOffset) );
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(true);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    if (numerousInputs) {
        for (unsigned j = 2; j < clipSourceCount; ++j) {
            ClipDescriptor *srcClip;
            srcClip = desc.defineClip( unsignedToString(j + kClipSourceOffset) );
            srcClip->setOptional(true);
            srcClip->addSupportedComponent(ePixelComponentNone);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
            srcClip->addSupportedComponent(ePixelComponentXY);
#endif
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
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeIn);
        param->setLabel(kParamFadeInLabel);
        param->setHint(kParamFadeInHint);
        param->setRange(0, INT_MAX); // Resolve requires range and display range
        param->setDisplayRange(0, 50);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFadeOut);
        param->setLabel(kParamFadeOutLabel);
        param->setHint(kParamFadeOutHint);
        param->setRange(0, INT_MAX); // Resolve requires range and display range
        param->setDisplayRange(0, 50);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamCrossDissolve);
        param->setLabel(kParamCrossDissolveLabel);
        param->setHint(kParamCrossDissolveHint);
        param->setRange(0, INT_MAX); // Resolve requires range and display range
        param->setDisplayRange(0, 50);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFirstFrame);
        param->setLabel(kParamFirstFrameLabel);
        param->setHint(kParamFirstFrameHint);
        param->setRange(INT_MIN, INT_MAX); // Resolve requires range and display range
        param->setDisplayRange(INT_MIN, INT_MAX);
        param->setDefault(1);
        param->setAnimates(false); // used in getTimeDomain()
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamLastFrame);
        param->setLabel(kParamLastFrameLabel);
        param->setHint(kParamLastFrameHint);
        param->setRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(INT_MIN, INT_MAX);
        param->setDefault(0);
        param->setEnabled(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        param->setAnimates(false); // used in getTimeDomain()
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
} // AppendClipPluginFactory::describeInContext

template<unsigned int majorVersion>
ImageEffect*
AppendClipPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
#ifdef OFX_EXTENSIONS_NATRON
    bool numerousInputs =  (getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);
#else
    bool numerousInputs = false;
#endif

    return new AppendClipPlugin(handle, numerousInputs, majorVersion);
}

static AppendClipPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
// version 1: clip1 is weighted by 1-alpha0 during a cross-dissolve
static AppendClipPluginFactory<1> pv1(kPluginIdentifier, 1); // 1.1: NULL Source renders black (see History)
mRegisterPluginFactoryInstance(p)
mRegisterPluginFactoryInstance(pv1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
