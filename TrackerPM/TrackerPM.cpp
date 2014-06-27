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
    {

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
    void trackInternal(OfxTime ref,OfxTime other);

    /* set up and run a processor */
    void setupAndProcess(TrackerPMProcessorBase &processor,
                         OfxTime refTime,
                         const OfxRectD& refBounds,
                         const OfxPointD& refCenter,
                         const OFX::Image* refImg,
                         OfxTime otherTime,
                         const OfxRectD& trackSearchBounds,
                         const OFX::Image* otherImg);
};


class TrackerPMProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_refImg;
    const OFX::Image *_otherImg;
    OfxRectI _refRectPixel;
    OfxPointI _refCenterI;
    std::pair<OfxPointD,double> _bestMatch; //< the results for the current processor
    OFX::MultiThread::Mutex _bestMatchMutex; //< this is used so we can multi-thread the tracking and protect the shared results
    
public:
    TrackerPMProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _refImg(0)
    , _otherImg(0)
    , _refRectPixel()
    {
        _bestMatch.second = std::numeric_limits<double>::infinity();

    }
    
    /** @brief set the src image */
    void setImages(const OFX::Image *ref, const OFX::Image *other)
    {
        _refImg = ref;
        _otherImg = other;
    }
    
    void setRefRectPixel(const OfxRectI& pattern)
    {
        _refRectPixel = pattern;
    }
    
    void setRefCenterI(const OfxPointI& centeri) {
        _refCenterI = centeri;
    }
    
    /**
     * @brief Retrieves the results of the track. Must be called once process() returns so it is thread safe.
     **/
    const OfxPointD& getBestMatch() const { return _bestMatch.first; }
    
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, TrackerScoreEnum scoreType>
class TrackerPMProcessor : public TrackerPMProcessorBase
{
public:
    TrackerPMProcessor(OFX::ImageEffect &instance)
    : TrackerPMProcessorBase(instance)
    {
    }
    
private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_refImg && _otherImg);
        assert(scoreType == eTrackerSSD);
        double bestScore = std::numeric_limits<double>::infinity();
        OfxPointI point;
        point.x = -1;
        point.y = -1;

        ///For every pixel in the sub window of the search area we find the pixel
        ///that minimize the sum of squared differences between the pattern in the ref image
        ///and the pattern in the other image.
        
        ///we're not interested in the alpha channel for RGBA images
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) break;
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                double score = 0;
                for (int i = _refRectPixel.y1; i < _refRectPixel.y2; ++i) {
                    for (int j = _refRectPixel.x1; j < _refRectPixel.x2; ++j) {
                        PIX *refPix = (PIX*) _refImg->getPixelAddress(_refCenterI.x + j, _refCenterI.y + i);

                        // take nearest pixel in other image (more chance to get a track than with black)
                        int otherx = x + j;
                        int othery = y + i;
                        otherx = std::max(_otherImg->getBounds().x1,std::min(otherx,_otherImg->getBounds().x2-1));
                        othery = std::max(_otherImg->getBounds().y1,std::min(othery,_otherImg->getBounds().y2-1));
                        PIX *otherPix = (PIX *) _otherImg->getPixelAddress(otherx, othery);

                        ///the search window & pattern window have been intersected to the reference image's bounds
                        assert(refPix && otherPix);

                        score = aggregateSSD(score, refPix, otherPix);
                    }
                }
                if (score < bestScore) {
                    bestScore = score;
                    point.x = x;
                    point.y = y;
                }
            }
        }
        
        {
            OFX::MultiThread::AutoMutex lock(_bestMatchMutex);
            if (_bestMatch.second > bestScore) {
                _bestMatch.second = bestScore;
                _bestMatch.first.x = point.x;
                _bestMatch.first.y = point.y;
            }
        }
    }

    double aggregateSSD(double score, PIX* refPix, PIX* otherPix)
    {
        if (nComponents == 1) {
            ///compare raw alpha distance
            return score + (*otherPix - *refPix) * (*otherPix - *refPix);
        } else {
            assert(nComponents >= 3);
            double r = refPix[0] - otherPix[0];
            double g = refPix[1] - otherPix[1];
            double b = refPix[2] - otherPix[2];

            return score + (r*r +  g*g + b*b);
        }
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
                                 OfxTime otherTime,
                                 const OfxRectD& trackSearchBounds,
                                 const OFX::Image* otherImg)
{
    // set a dummy dstImg so that the processor does the job
    // (we don't use it anyway)
    processor.setDstImg((OFX::Image *)1);
    processor.setImages(refImg, otherImg);
    
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
    OfxPointI refCenterI;
    refCenterI.x = std::floor(refCenter.x + 0.5);
    refCenterI.y = std::floor(refCenter.y + 0.5);

    refRectPixel.x1 -= refCenterI.x;
    refRectPixel.x2 -= refCenterI.x;
    refRectPixel.y1 -= refCenterI.y;
    refRectPixel.y2 -= refCenterI.y;

    // set the render window
    processor.setRenderWindow(trackSearchBoundsPixel);
    
    processor.setRefRectPixel(refRectPixel);
    
    // round center to nearest pixel center
    processor.setRefCenterI(refCenterI);
    
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();

    //////////////////////////////////
    // TODO: subpixel interpolation //
    //////////////////////////////////

    ///ok the score is now computed, update the center
    OfxPointD newCenter;
    const OfxPointD& bestMatch = processor.getBestMatch();
    
    newCenter.x = refCenter.x + bestMatch.x - refCenterI.x;
    newCenter.y = refCenter.y + bestMatch.y - refCenterI.y;
    _center->setValueAtTime(otherTime, newCenter.x, newCenter.y);
}



// the internal render function
template <int nComponents>
void
TrackerPMPlugin::trackInternal(OfxTime ref, OfxTime other)
{
    OfxRectD refRect;
    _innerBtmLeft->getValueAtTime(ref, refRect.x1, refRect.y1);
    _innerTopRight->getValueAtTime(ref, refRect.x2, refRect.y2);
    OfxPointD refCenter;
    _center->getValueAtTime(ref, refCenter.x, refCenter.y);
    OfxRectD searchRect;
    _outerBtmLeft->getValueAtTime(ref, searchRect.x1, searchRect.y1);
    _outerTopRight->getValueAtTime(ref, searchRect.x2, searchRect.y2);

    OfxRectD refBounds;
    getRefBounds(refRect, refCenter, &refBounds);

    OfxRectD otherBounds;
    getOtherBounds(refCenter, searchRect, &otherBounds);

    std::auto_ptr<const OFX::Image> srcRef(srcClip_->fetchImage(ref, refBounds));
    std::auto_ptr<const OFX::Image> srcOther(srcClip_->fetchImage(other, otherBounds));
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
    
    OfxRectD trackSearchBounds;
    getTrackSearchBounds(refRect, refCenter, searchRect, &trackSearchBounds);

    switch (srcBitDepth) {
        case OFX::eBitDepthUByte :
        {
            TrackerPMProcessor<unsigned char, nComponents, 255, eTrackerSSD> fred(*this);
            setupAndProcess(fred, ref, refBounds, refCenter, srcRef.get(), other, trackSearchBounds, srcOther.get());
        }   break;
        case OFX::eBitDepthUShort :
        {
            TrackerPMProcessor<unsigned short, nComponents, 65535, eTrackerSSD> fred(*this);
            setupAndProcess(fred, ref, refBounds, refCenter, srcRef.get(), other, trackSearchBounds, srcOther.get());
        }   break;
        case OFX::eBitDepthFloat :
        {
            TrackerPMProcessor<float, nComponents, 1, eTrackerSSD> fred(*this);
            setupAndProcess(fred, ref, refBounds, refCenter, srcRef.get(), other, trackSearchBounds, srcOther.get());
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
    genericTrackerDescribe(desc);
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



OFX::ImageEffect* TrackerPMPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new TrackerPMPlugin(handle);
}




void TrackerPMPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    PageParamDescriptor* page = genericTrackerDescribeInContextBegin(desc, context);
    genericTrackerDescribePointParameters(desc, page);
    
    ChoiceParamDescriptor* type = desc.defineChoiceParam(kScoreParamName);
    type->setLabels(kScoreParamLabel, kScoreParamLabel, kScoreParamLabel);
    assert(type->getNOptions() == eTrackerSSD);
    type->appendOption(kScoreParamOptionSSD, kScoreParamOptionSSDHint);
    assert(type->getNOptions() == eTrackerSAD);
    type->appendOption(kScoreParamOptionSAD, kScoreParamOptionSADHint);
    assert(type->getNOptions() == eTrackerNCC);
    type->appendOption(kScoreParamOptionNCC, kScoreParamOptionNCCHint);
    assert(type->getNOptions() == eTrackerZNCC);
    type->appendOption(kScoreParamOptionZNCC, kScoreParamOptionZNCCHint);
    type->setDefault((int)eTrackerSSD);
    page->addChild(*type);
}

void getTrackerPMPluginID(OFX::PluginFactoryArray &ids)
{
    static TrackerPMPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

