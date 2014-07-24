/*
 Basic tracker with exhaustive search algorithm OFX plugin.
 
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
#include "TrackerPM.h"

#include <cmath>
#include <map>
#include <limits>

#include "ofxsProcessing.H"
#include "ofxsTracking.h"
#include "ofxsMerging.h"

#define kPluginName "TrackerPM"
#define kPluginGrouping "Transform"
#define kPluginDescription "Point tracker based on pattern matching using an exhaustive search within an image region"
#define kPluginIdentifier "net.sf.openfx:TrackerPMPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kScoreParamName "score"
#define kScoreParamLabel "Score"
#define kScoreParamHint "Correlation score computation method"
#define kScoreParamOptionSSD "SSD"
#define kScoreParamOptionSSDHint "Sum of Squared Differences"
#define kScoreParamOptionSAD "SAD"
#define kScoreParamOptionSADHint "Sum of Absolute Differences, more robust to occlusions"
#define kScoreParamOptionNCC "NCC"
#define kScoreParamOptionNCCHint "Normalized Cross-Correlation"
#define kScoreParamOptionZNCC "ZNCC"
#define kScoreParamOptionZNCCHint "Zero-mean Normalized Cross-Correlation, less sensitive to illumination changes"

using namespace OFX;

enum TrackerScoreEnum
{
    eTrackerSSD = 0,
    eTrackerSAD,
    eTrackerNCC,
    eTrackerZNCC
};

class TrackerPMProcessorBase;
////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TrackerPMPlugin : public GenericTrackerPlugin
{
public:
    /** @brief ctor */
    TrackerPMPlugin(OfxImageEffectHandle handle)
    : GenericTrackerPlugin(handle)
    , _score(0)
    {
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _score = fetchChoiceParam(kScoreParamName);
        assert(_score);
    }
    
    
private:
    /**
     * @brief Override to track the entire range between [first,last].
     * @param forward If true then it should track from first to last, otherwise it should track
     * from last to first.
     * @param currentTime The current time at which the track has been requested.
     **/
    virtual void trackRange(const OFX::TrackArguments& args);
    
    template <int nComponents>
    void trackInternal(OfxTime refTime, OfxTime otherTime);

    template <class PIX, int nComponents, int maxValue>
    void trackInternalForDepth(OfxTime refTime,
                               const OfxRectD& refBounds,
                               const OfxPointD& refCenter,
                               const OFX::Image* refImg,
                               const OFX::Image* maskImg,
                               OfxTime otherTime,
                               const OfxRectD& trackSearchBounds,
                               const OFX::Image* otherImg);

    /* set up and run a processor */
    void setupAndProcess(TrackerPMProcessorBase &processor,
                         OfxTime refTime,
                         const OfxRectD& refBounds,
                         const OfxPointD& refCenter,
                         const OFX::Image* refImg,
                         const OFX::Image* maskImg,
                         OfxTime otherTime,
                         const OfxRectD& trackSearchBounds,
                         const OFX::Image* otherImg);

    OFX::Clip *maskClip_;
    ChoiceParam* _score;
};


class TrackerPMProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_otherImg;
    OfxRectI _refRectPixel;
    OfxPointI _refCenterI;
    std::pair<OfxPointD,double> _bestMatch; //< the results for the current processor
    OFX::MultiThread::Mutex _bestMatchMutex; //< this is used so we can multi-thread the tracking and protect the shared results
    
public:
    TrackerPMProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _otherImg(0)
    , _refRectPixel()
    , _refCenterI()
    {
        _bestMatch.second = std::numeric_limits<double>::infinity();

    }

    virtual ~TrackerPMProcessorBase()
    {
    }

    /** @brief set the processing parameters. return false if processing cannot be done. */
    virtual bool setValues(const OFX::Image *ref, const OFX::Image *other, const OFX::Image *mask,
                           const OfxRectI& pattern, const OfxPointI& centeri) = 0;

    /**
     * @brief Retrieves the results of the track. Must be called once process() returns so it is thread safe.
     **/
    const OfxPointD& getBestMatch() const { return _bestMatch.first; }
    double getBestScore() const { return _bestMatch.second; }
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, TrackerScoreEnum scoreType>
class TrackerPMProcessor : public TrackerPMProcessorBase
{
protected:
    ImageMemory *_patternImg;
    PIX *_patternData;
    ImageMemory *_weightImg;
    float *_weightData;
    double _weightTotal;
public:
    TrackerPMProcessor(OFX::ImageEffect &instance)
    : TrackerPMProcessorBase(instance)
    , _patternImg(0)
    , _patternData(0)
    , _weightImg(0)
    , _weightData(0)
    , _weightTotal(0.)
    {
    }

    ~TrackerPMProcessor()
    {
        _patternImg->unlock();
        delete _patternImg;
        _weightImg->unlock();
        delete _weightImg;
    }
private:
    /** @brief set the processing parameters. return false if processing cannot be done. */
    virtual bool setValues(const OFX::Image *ref, const OFX::Image *other, const OFX::Image *mask,
                           const OfxRectI& pattern, const OfxPointI& centeri)
    {
        size_t rowsize = pattern.x2 - pattern.x1;
        size_t nPix = rowsize * (pattern.y2 - pattern.y1);
        _patternImg = new ImageMemory(sizeof(PIX) * nComponents * nPix, &_effect);
        _weightImg = new ImageMemory(sizeof(float) * nPix, &_effect);
        _otherImg = other;
        _refRectPixel = pattern;
        _refCenterI = centeri;

        _patternData = (PIX*)_patternImg->lock();
        _weightData = (float*)_weightImg->lock();

        // sliding pointers
        size_t patternIdx = 0; // sliding index
        PIX *patternPtr = _patternData;
        float *weightPtr = _weightData;
        _weightTotal = 0.;

        // extract ref and mask
        for (int i = _refRectPixel.y1; i < _refRectPixel.y2; ++i) {
            for (int j = _refRectPixel.x1; j < _refRectPixel.x2; ++j, ++weightPtr, patternPtr += nComponents, ++patternIdx) {
                assert(patternIdx == ((i - _refRectPixel.y1) * (_refRectPixel.x2 - _refRectPixel.x1) + (j - _refRectPixel.x1)));
                PIX *refPix = (PIX*) ref->getPixelAddress(_refCenterI.x + j, _refCenterI.y + i);

                if (!refPix) {
                    // no reference pixel, set weight to 0
                    *weightPtr = 0.;
                    for (int c = 0; c < nComponents; ++c) {
                        patternPtr[c] = PIX();
                    }
                } else {
                    if (!mask) {
                        // no mask, weight is uniform
                        *weightPtr = 1.;
                    } else {
                        PIX *maskPix = (PIX*) mask->getPixelAddress(_refCenterI.x + j, _refCenterI.y + i);
                        // weight is zero if there's a mask but we're outside of it
                        *weightPtr = maskPix ? (*maskPix/(double)maxValue) : 0.;
                    }
                    for (int c = 0; c < nComponents; ++c) {
                        patternPtr[c] = refPix[c];
                    }
                }
                _weightTotal += *weightPtr;
            }
        }
        return (_weightTotal > 0);
    }

    void multiThreadProcessImages(OfxRectI procWindow) {
        switch (scoreType) {
            case eTrackerSSD:
                return multiThreadProcessImagesForScore<eTrackerSSD>(procWindow);
            case eTrackerSAD:
                return multiThreadProcessImagesForScore<eTrackerSAD>(procWindow);
            case eTrackerNCC:
                return multiThreadProcessImagesForScore<eTrackerNCC>(procWindow);
            case eTrackerZNCC:
                return multiThreadProcessImagesForScore<eTrackerZNCC>(procWindow);
        }
    }

    template<enum TrackerScoreEnum scoreTypeE>
    double computeScore(int scoreComps, int x, int y, const double refMean[3])
    {
        double score = 0;
        double otherSsq = 0.;
        double otherMean[3];
        if (scoreTypeE == eTrackerZNCC) {
            for (int c = 0; c < scoreComps; ++c) {
                otherMean[c] = 0;
            }
            // sliding pointers
            size_t patternIdx = 0; // sliding index
            const PIX *patternPtr = _patternData;
            float *weightPtr = _weightData;
            for (int i = _refRectPixel.y1; i < _refRectPixel.y2; ++i) {
                for (int j = _refRectPixel.x1; j < _refRectPixel.x2; ++j, ++weightPtr, patternPtr += nComponents, ++patternIdx) {
                    assert(patternIdx == ((i - _refRectPixel.y1) * (_refRectPixel.x2 - _refRectPixel.x1) + (j - _refRectPixel.x1)));
                   // take nearest pixel in other image (more chance to get a track than with black)
                    int otherx = x + j;
                    int othery = y + i;
                    otherx = std::max(_otherImg->getBounds().x1,std::min(otherx,_otherImg->getBounds().x2-1));
                    othery = std::max(_otherImg->getBounds().y1,std::min(othery,_otherImg->getBounds().y2-1));
                    PIX *otherPix = (PIX *) _otherImg->getPixelAddress(otherx, othery);
                    for (int c = 0; c < scoreComps; ++c) {
                        otherMean[c] += *weightPtr * otherPix[c];
                    }
                }
            }
            for (int c = 0; c < scoreComps; ++c) {
                otherMean[c] /= _weightTotal;
            }
        }

        // sliding pointers
        size_t patternIdx = 0; // sliding index
        const PIX *patternPtr = _patternData;
        float *weightPtr = _weightData;

        for (int i = _refRectPixel.y1; i < _refRectPixel.y2; ++i) {
            for (int j = _refRectPixel.x1; j < _refRectPixel.x2; ++j, ++weightPtr, patternPtr += nComponents, ++patternIdx) {
                assert(patternIdx == ((i - _refRectPixel.y1) * (_refRectPixel.x2 - _refRectPixel.x1) + (j - _refRectPixel.x1)));
                const PIX * const refPix = patternPtr;

                const float weight = *weightPtr;

                // take nearest pixel in other image (more chance to get a track than with black)
                int otherx = x + j;
                int othery = y + i;
                otherx = std::max(_otherImg->getBounds().x1,std::min(otherx,_otherImg->getBounds().x2-1));
                othery = std::max(_otherImg->getBounds().y1,std::min(othery,_otherImg->getBounds().y2-1));
                PIX *otherPix = (PIX *) _otherImg->getPixelAddress(otherx, othery);

                ///the search window & pattern window have been intersected to the reference image's bounds
                assert(refPix && otherPix);
                for (int c = 0; c < scoreComps; ++c) {
                    switch (scoreTypeE) {
                        case eTrackerSSD:
                            // reference is squared in SSD, so is the weight
                            score += weight * weight * aggregateSD(refPix[c], otherPix[c]);
                            break;
                        case eTrackerSAD:
                            score += weight * aggregateAD(refPix[c], otherPix[c]);
                            break;
                        case eTrackerNCC:
                            score += weight * aggregateCC(refPix[c], otherPix[c]);
                            otherSsq -= weight * aggregateCC(otherPix[c], otherPix[c]);
                            break;
                        case eTrackerZNCC:
                            score += weight * aggregateNCC(refPix[c], refMean[c], otherPix[c], otherMean[c]);
                            otherSsq -= weight * aggregateNCC(otherPix[c], otherMean[c], otherPix[c], otherMean[c]);
                            break;
                    }
                }
            }
        }
        if (scoreTypeE == eTrackerNCC || scoreTypeE == eTrackerZNCC) {
            double sdev = std::sqrt(otherSsq);
            if (sdev != 0.) {
                score /= sdev;
            } else {
                score = std::numeric_limits<double>::infinity();
            }
        }
        return score;
    }

    template<enum TrackerScoreEnum scoreTypeE>
    void multiThreadProcessImagesForScore(const OfxRectI& procWindow)
    {
        assert(_patternImg && _patternData && _weightImg && _weightData && _otherImg);
        assert(scoreType == scoreTypeE);
        double bestScore = std::numeric_limits<double>::infinity();
        OfxPointI point;
        point.x = -1;
        point.y = -1;

        ///For every pixel in the sub window of the search area we find the pixel
        ///that minimize the sum of squared differences between the pattern in the ref image
        ///and the pattern in the other image.

        const int scoreComps = std::min(nComponents,3);
        double refMean[3];
        if (scoreTypeE == eTrackerZNCC) {
            for (int c = 0; c < scoreComps; ++c) {
                refMean[c] = 0;
            }
        }
        if (scoreTypeE == eTrackerZNCC) {
            // sliding pointers
            size_t patternIdx = 0; // sliding index
            const PIX *patternPtr = _patternData;
            float *weightPtr = _weightData;
            for (int i = _refRectPixel.y1; i < _refRectPixel.y2; ++i) {
                for (int j = _refRectPixel.x1; j < _refRectPixel.x2; ++j, ++weightPtr, patternPtr += nComponents, ++patternIdx) {
                    assert(patternIdx == ((i - _refRectPixel.y1) * (_refRectPixel.x2 - _refRectPixel.x1) + (j - _refRectPixel.x1)));
                    const PIX *refPix = patternPtr;
                    for (int c = 0; c < scoreComps; ++c) {
                        refMean[c] += *weightPtr * refPix[c];
                    }
                }
            }
            for (int c = 0; c < scoreComps; ++c) {
                refMean[c] /= _weightTotal;
            }
        }

        ///we're not interested in the alpha channel for RGBA images
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double score = computeScore<scoreTypeE>(scoreComps, x, y, refMean);
                if (score < bestScore) {
                    bestScore = score;
                    point.x = x;
                    point.y = y;
                }
            }
        }
        
        // do the subpixel refinement, only if the score is a possible winner
        // TODO: only do this for the best match
        double dx = 0.;
        double dy = 0.;

        _bestMatchMutex.lock();
        if (_bestMatch.second < bestScore) {
            _bestMatchMutex.unlock();
        } else {
            // don't block other threads
            _bestMatchMutex.unlock();
            // compute subpixel position.
            double scorepc = computeScore<scoreTypeE>(scoreComps, point.x - 1, point.y, refMean);
            double scorenc = computeScore<scoreTypeE>(scoreComps, point.x + 1, point.y, refMean);
            if (bestScore < scorepc && bestScore <= scorenc) {
                // don't simplify the denominator in the following expression,
                // 2*bestScore - scorenc - scorepc may cause an underflow.
                dx = 0.5 * (scorenc - scorepc) /((bestScore - scorenc) + (bestScore - scorepc));
                assert(-0.5 < dx && dx <= 0.5);
            }
            double scorecp = computeScore<scoreTypeE>(scoreComps, point.x, point.y - 1, refMean);
            double scorecn = computeScore<scoreTypeE>(scoreComps, point.x, point.y + 1, refMean);
            if (bestScore < scorecp && bestScore <= scorecn) {
                // don't simplify the denominator in the following expression,
                // 2*bestScore - scorenc - scorepc may cause an underflow.
                dy = 0.5 * (scorecn - scorecp) /((bestScore - scorecn) + (bestScore - scorecp));
                assert(-0.5 < dy && dy <= 0.5);
            }
            // check again...
            {
                OFX::MultiThread::AutoMutex lock(_bestMatchMutex);
                if (_bestMatch.second > bestScore) {
                    _bestMatch.second = bestScore;
                    _bestMatch.first.x = point.x + dx;
                    _bestMatch.first.y = point.y + dy;
                }
            }

        }
    }

    double aggregateSD(PIX refPix, PIX otherPix)
    {
        double d = (double)refPix - otherPix;
        return d * d;
    }

    double aggregateAD(PIX refPix, PIX otherPix)
    {
        return std::abs((double)refPix - otherPix);
    }

    double aggregateCC(PIX refPix, PIX otherPix)
    {
        return - (double)refPix * otherPix;
    }

    double aggregateNCC(PIX refPix, double refMean, PIX otherPix, double otherMean)
    {
        return - (refPix - refMean) * (otherPix - otherMean);
    }
};


void
TrackerPMPlugin::trackRange(const OFX::TrackArguments& args)
{
    // Although the following property has been there since OFX 1.0,
    // it's not in the HostSupport library.
    getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);

    //double t1, t2;
    // get the first and last times available on the effect's timeline
    //timeLineGetBounds(t1, t2);

    OfxTime t = args.first;
    bool changeTime = (args.reason == eChangeUserEdit && t == timeLineGetTime());
    std::string name;
    _instanceName->getValue(name);
    assert((args.forward && args.last >= args.first) || (!args.forward && args.last <= args.first));
    bool showProgress = std::abs(args.last - args.first) > 1;
    if (showProgress) {
        progressStart(name);
    }

    OfxPointD refCenter;
    _center->getValueAtTime(t, refCenter.x, refCenter.y);

    while (args.forward ? (t <= args.last) : (t >= args.last)) {
        OfxTime other = args.forward ? (t + 1) : (t - 1);
        
        OFX::PixelComponentEnum srcComponents  = srcClip_->getPixelComponents();
        assert(srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA ||
               srcComponents == OFX::ePixelComponentAlpha);
        
        if (srcComponents == OFX::ePixelComponentRGBA) {
            trackInternal<4>(t,other);
        } else if (srcComponents == OFX::ePixelComponentRGB) {
            trackInternal<3>(t,other);
        } else {
            assert(srcComponents == OFX::ePixelComponentAlpha);
            trackInternal<1>(t,other);
        }
        if (args.forward) {
            ++t;
        } else {
            --t;
        }
        if (changeTime ) {
            // set the timeline to a specific time
            timeLineGotoTime(t);
        }
        if (showProgress && !progressUpdate((t - args.first) / (args.last - args.first))) {
            progressEnd();
            return;
        }
    }
    if (showProgress) {
        progressEnd();
    }
    getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


static void
getRefBounds(const OfxRectD& refRect, const OfxPointD &refCenter, OfxRectD *bounds)
{
    bounds->x1 = refCenter.x + refRect.x1;
    bounds->x2 = refCenter.x + refRect.x2;
    bounds->y1 = refCenter.y + refRect.y1;
    bounds->y2 = refCenter.y + refRect.y2;

    // make the window at least 2 pixels high/wide
    // (this should never happen, of course)
    if (bounds->x2 < bounds->x1 + 2) {
        bounds->x1 = (bounds->x1 + bounds->x2) / 2 - 1;
        bounds->x2 = bounds->x1 + 2;
    }
    if (bounds->y2 < bounds->y1 + 2) {
        bounds->y1 = (bounds->y1 + bounds->y2) / 2 - 1;
        bounds->y2 = bounds->y1 + 2;
    }
}

void
getTrackSearchBounds(const OfxRectD& refRect, const OfxPointD &refCenter, const OfxRectD& searchRect, OfxRectD *bounds)
{
    // subtract the pattern window so that we don't check for pixels out of the search window
    bounds->x1 = refCenter.x + searchRect.x1 - refRect.x1;
    bounds->y1 = refCenter.y + searchRect.y1 - refRect.y1;
    bounds->x2 = refCenter.x + searchRect.x2 - refRect.x2;
    bounds->y2 = refCenter.y + searchRect.y2 - refRect.y2;

    // if the window is empty, make it at least 1 pixel high/wide
    if (bounds->x2 <= bounds->x1) {
        bounds->x1 = (bounds->x1 + bounds->x2) / 2;
        bounds->x2 = bounds->x1 + 1;
    }
    if (bounds->y2 <= bounds->y1) {
        bounds->y1 = (bounds->y1 + bounds->y2) / 2;
        bounds->y2 = bounds->y1 + 1;
    }
}

void
getOtherBounds(const OfxPointD &refCenter, const OfxRectD& searchRect, OfxRectD *bounds)
{
    // subtract the pattern window so that we don't check for pixels out of the search window
    bounds->x1 = refCenter.x + searchRect.x1;
    bounds->y1 = refCenter.y + searchRect.y1;
    bounds->x2 = refCenter.x + searchRect.x2;
    bounds->y2 = refCenter.y + searchRect.y2;

    // if the window is empty, make it at least 1 pixel high/wide
    if (bounds->x2 <= bounds->x1) {
        bounds->x1 = (bounds->x1 + bounds->x2) / 2;
        bounds->x2 = bounds->x1 + 1;
    }
    if (bounds->y2 <= bounds->y1) {
        bounds->y1 = (bounds->y1 + bounds->y2) / 2;
        bounds->y2 = bounds->y1 + 1;
    }
}

/* set up and run a processor */
void
TrackerPMPlugin::setupAndProcess(TrackerPMProcessorBase &processor,
                                 OfxTime refTime,
                                 const OfxRectD& refBounds,
                                 const OfxPointD& refCenter,
                                 const OFX::Image* refImg,
                                 const OFX::Image* maskImg,
                                 OfxTime otherTime,
                                 const OfxRectD& trackSearchBounds,
                                 const OFX::Image* otherImg)
{
    // set a dummy dstImg so that the processor does the job
    // (we don't use it anyway)
    processor.setDstImg((OFX::Image *)1);

    OfxRectI trackSearchBoundsPixel;
    trackSearchBoundsPixel.x1 = std::floor(trackSearchBounds.x1);
    trackSearchBoundsPixel.y1 = std::floor(trackSearchBounds.y1);
    trackSearchBoundsPixel.x2 = std::ceil(trackSearchBounds.x2);
    trackSearchBoundsPixel.y2 = std::ceil(trackSearchBounds.y2);

    // compute the pattern window in pixel coords
    OfxRectI refRectPixel;
    refRectPixel.x1 = std::floor(refBounds.x1);
    refRectPixel.y1 = std::floor(refBounds.y1);
    refRectPixel.x2 = std::ceil(refBounds.x2);
    refRectPixel.y2 = std::ceil(refBounds.y2);
    // round center to nearest pixel center
    OfxPointI refCenterI;
    refCenterI.x = std::floor(refCenter.x + 0.5);
    refCenterI.y = std::floor(refCenter.y + 0.5);

    //Clip the refRectPixel to the bounds of the ref image
    MergeImages2D::rectIntersection(refRectPixel, refImg->getBounds(), &refRectPixel);
    
    refRectPixel.x1 -= refCenterI.x;
    refRectPixel.x2 -= refCenterI.x;
    refRectPixel.y1 -= refCenterI.y;
    refRectPixel.y2 -= refCenterI.y;

    // set the render window
    processor.setRenderWindow(trackSearchBoundsPixel);
    
    bool canProcess = processor.setValues(refImg, otherImg, maskImg, refRectPixel, refCenterI);
    
    if (!canProcess) {
        // can't track: erase any existing track
        _center->deleteKeyAtTime(otherTime);
    } else {
        // Call the base class process member, this will call the derived templated process code
        processor.process();

        //////////////////////////////////
        // TODO: subpixel interpolation //
        //////////////////////////////////

        ///ok the score is now computed, update the center
        OfxPointD newCenter;
        if (processor.getBestScore() == std::numeric_limits<double>::infinity()) {
            // can't track: erase any existing track
            _center->deleteKeyAtTime(otherTime);
        } else {
            const OfxPointD& bestMatch = processor.getBestMatch();

            newCenter.x = refCenter.x + bestMatch.x - refCenterI.x;
            newCenter.y = refCenter.y + bestMatch.y - refCenterI.y;
            // create a keyframe at starting point
            _center->setValueAtTime(refTime, refCenter.x, refCenter.y);
            // create a keyframe at end point
            _center->setValueAtTime(otherTime, newCenter.x, newCenter.y);
        }
    }
}

template <class PIX, int nComponents, int maxValue>
void
TrackerPMPlugin::trackInternalForDepth(OfxTime refTime,
                                       const OfxRectD& refBounds,
                                       const OfxPointD& refCenter,
                                       const OFX::Image* refImg,
                                       const OFX::Image* maskImg,
                                       OfxTime otherTime,
                                       const OfxRectD& trackSearchBounds,
                                       const OFX::Image* otherImg)
{
    int scoreI;
    _score->getValueAtTime(refTime, scoreI);
    TrackerScoreEnum typeE = (TrackerScoreEnum)scoreI;

    switch (typeE) {
        case eTrackerSSD: {
            TrackerPMProcessor<PIX, nComponents, maxValue, eTrackerSSD> fred(*this);
            setupAndProcess(fred, refTime, refBounds, refCenter, refImg, maskImg, otherTime, trackSearchBounds, otherImg);
        }   break;
        case eTrackerSAD: {
            TrackerPMProcessor<PIX, nComponents, maxValue, eTrackerSAD> fred(*this);
            setupAndProcess(fred, refTime, refBounds, refCenter, refImg, maskImg, otherTime, trackSearchBounds, otherImg);
        }   break;
        case eTrackerNCC: {
            TrackerPMProcessor<PIX, nComponents, maxValue, eTrackerNCC> fred(*this);
            setupAndProcess(fred, refTime, refBounds, refCenter, refImg, maskImg, otherTime, trackSearchBounds, otherImg);
        }   break;
        case eTrackerZNCC: {
            TrackerPMProcessor<PIX, nComponents, maxValue, eTrackerZNCC> fred(*this);
            setupAndProcess(fred, refTime, refBounds, refCenter, refImg, maskImg, otherTime, trackSearchBounds, otherImg);
        }   break;
    }
}


// the internal render function
template <int nComponents>
void
TrackerPMPlugin::trackInternal(OfxTime refTime, OfxTime otherTime)
{
    OfxRectD refRect;
    _innerBtmLeft->getValueAtTime(refTime, refRect.x1, refRect.y1);
    _innerTopRight->getValueAtTime(refTime, refRect.x2, refRect.y2);
    OfxPointD refCenter;
    _center->getValueAtTime(refTime, refCenter.x, refCenter.y);
    OfxRectD searchRect;
    _outerBtmLeft->getValueAtTime(refTime, searchRect.x1, searchRect.y1);
    _outerTopRight->getValueAtTime(refTime, searchRect.x2, searchRect.y2);

    OfxRectD refBounds;
    getRefBounds(refRect, refCenter, &refBounds);

    OfxRectD otherBounds;
    getOtherBounds(refCenter, searchRect, &otherBounds);

    std::auto_ptr<const OFX::Image> srcRef(srcClip_->fetchImage(refTime, refBounds));
    std::auto_ptr<const OFX::Image> srcOther(srcClip_->fetchImage(otherTime, otherBounds));
    if (!srcRef.get() || !srcOther.get()) {
        return;
    }
    // renderScale should never be something else than 1 when called from ActionInstanceChanged
    if ((srcRef->getPixelDepth() != srcOther->getPixelDepth()) ||
        (srcRef->getPixelComponents() != srcOther->getPixelComponents()) ||
        srcRef->getRenderScale().x != 1. || srcRef->getRenderScale().y != 1 ||
        srcOther->getRenderScale().x != 1. || srcOther->getRenderScale().y != 1) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    OFX::BitDepthEnum srcBitDepth = srcRef->getPixelDepth();
    
    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((getContext() != OFX::eContextFilter) ? maskClip_->fetchImage(refTime) : 0);

    OfxRectD trackSearchBounds;
    getTrackSearchBounds(refRect, refCenter, searchRect, &trackSearchBounds);

    switch (srcBitDepth) {
        case OFX::eBitDepthUByte :
        {
            trackInternalForDepth<unsigned char, nComponents, 255>(refTime, refBounds, refCenter, srcRef.get(), mask.get(), otherTime, trackSearchBounds, srcOther.get());
        }   break;
        case OFX::eBitDepthUShort :
        {
            trackInternalForDepth<unsigned short, nComponents, 65535>(refTime, refBounds, refCenter, srcRef.get(), mask.get(), otherTime, trackSearchBounds, srcOther.get());
        }   break;
        case OFX::eBitDepthFloat :
        {
            trackInternalForDepth<float, nComponents, 1>(refTime, refBounds, refCenter, srcRef.get(), mask.get(), otherTime, trackSearchBounds, srcOther.get());
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}


using namespace OFX;

mDeclarePluginFactory(TrackerPMPluginFactory, {}, {});

void TrackerPMPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // description common to all trackers
    genericTrackerDescribe(desc);

    // add the additional supported contexts
    desc.addSupportedContext(eContextPaint); // this tracker can be masked

    // supported bit depths depend on the tracking algorithm.
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // single instance depends on the algorithm
    desc.setSingleInstance(false);

    // rendertwicealways must be set to true if the tracker cannot handle interlaced content (most don't)
    desc.setRenderTwiceAlways(true);
    desc.setOverlayInteractDescriptor(new TrackerRegionOverlayDescriptor);
}

void TrackerPMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    PageParamDescriptor* page = genericTrackerDescribeInContextBegin(desc, context);

    // description common to all trackers
    genericTrackerDescribePointParameters(desc, page);

    // this tracker can be masked
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }

    ChoiceParamDescriptor* score = desc.defineChoiceParam(kScoreParamName);
    score->setLabels(kScoreParamLabel, kScoreParamLabel, kScoreParamLabel);
    score->setHint(kScoreParamHint);
    assert(score->getNOptions() == eTrackerSSD);
    score->appendOption(kScoreParamOptionSSD, kScoreParamOptionSSDHint);
    assert(score->getNOptions() == eTrackerSAD);
    score->appendOption(kScoreParamOptionSAD, kScoreParamOptionSADHint);
    assert(score->getNOptions() == eTrackerNCC);
    score->appendOption(kScoreParamOptionNCC, kScoreParamOptionNCCHint);
    assert(score->getNOptions() == eTrackerZNCC);
    score->appendOption(kScoreParamOptionZNCC, kScoreParamOptionZNCCHint);
    score->setDefault((int)eTrackerSSD);
    page->addChild(*score);
}



OFX::ImageEffect* TrackerPMPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new TrackerPMPlugin(handle);
}





void getTrackerPMPluginID(OFX::PluginFactoryArray &ids)
{
    static TrackerPMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

