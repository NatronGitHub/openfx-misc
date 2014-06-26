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
    
    // FIXME: move _bestMatch to TrackerPMProcessorBase, where it belongs
    void updateBestMatch(const OfxPointI& point, double score);
    
private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);
    
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
    void setupAndProcess(TrackerPMProcessorBase &,OfxTime refTime,OfxTime otherTime,OFX::Image* refImg,OFX::Image* otherImg);

    void getTrackSearchWindowCanonical(OfxTime time, OfxRectD *bounds) const;

    ///The pattern is in coordinates relative to the center point
    void getPatternCanonical(OfxTime time, OfxRectD *bounds) const;

    // FIXME: move _bestMatch to TrackerPMProcessorBase, where it belongs
    std::pair<OfxPointD,double> _bestMatch; //< the results for the current processor
    OFX::MultiThread::Mutex _bestMatchMutex; //< this is used so we can multi-thread the tracking and protect the shared results
};


class TrackerPMProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_refImg;
    OFX::Image *_otherImg;
    OfxRectI _patternWindow;
    OfxPointI _centeri;
    OfxPointD _center;
    TrackerPMPlugin* _plugin;
    
public:
    TrackerPMProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _refImg(0)
    , _otherImg(0)
    , _patternWindow()
    , _center()
    , _plugin(dynamic_cast<TrackerPMPlugin*>(&instance))
    {
        assert(_plugin);
    }
    
    /** @brief set the src image */
    void setImages(OFX::Image *ref,OFX::Image *other)
    {
        _refImg = ref;
        _otherImg = other;
    }
    
    void setPatternWindow(const OfxRectI& pattern)
    {
        _patternWindow = pattern;
    }
    
    void setCenterI(const OfxPointI& centeri) {
        _centeri = centeri;
    }
    
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
                for (int i = _patternWindow.y1; i < _patternWindow.y2; ++i) {
                    for (int j = _patternWindow.x1; j < _patternWindow.x2; ++j) {
                        PIX *refPix = (PIX*) _refImg->getPixelAddress(_centeri.x + j, _centeri.y + i);

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

        _plugin->updateBestMatch(point, bestScore);
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
TrackerPMPlugin::updateBestMatch(const OfxPointI& point, double score)
{
    // FIXME: move _bestMatch to TrackerPMProcessorBase, where it belongs
    OFX::MultiThread::AutoMutex lock(_bestMatchMutex);
    if (_bestMatch.second > score) {
        _bestMatch.second = score;
        _bestMatch.first.x = point.x;
        _bestMatch.first.y = point.y;
    }
}

void
TrackerPMPlugin::trackRange(const OFX::TrackArguments& args)
{
    // Although the following property has been there since OFX 1.0,
    // it's not in the HostSupport library.
    getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
    OfxTime t = args.first;
    std::string name;
    _instanceName->getValue(name);
    bool showProgress = std::abs(args.last - args.first) > 1;
    if (showProgress) {
        progressStart(name);
    }
    while (t != args.last) {
        
        OfxTime other = args.forward ? t + 1 : t - 1;
        
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
        if (showProgress && !progressUpdate(std::abs(t - args.first) / std::abs(args.last - args.first))) {
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

/* set up and run a processor */
void
TrackerPMPlugin::setupAndProcess(TrackerPMProcessorBase &processor,OfxTime refTime,OfxTime otherTime,OFX::Image* refImg,OFX::Image* otherImg)
{
    
    // set an uninitialized image for the dst image
    OFX::Image* dstImg = refImg;
    processor.setDstImg(dstImg);
    processor.setImages(refImg, otherImg);
    
    OfxRectD searchWindowCanonical;
    getTrackSearchWindowCanonical(refTime, &searchWindowCanonical);
    OfxRectI searchWindowPixel;
    searchWindowPixel.x1 = std::floor(searchWindowCanonical.x1);
    searchWindowPixel.y1 = std::floor(searchWindowCanonical.y1);
    searchWindowPixel.x2 = std::ceil(searchWindowCanonical.x2);
    searchWindowPixel.y2 = std::ceil(searchWindowCanonical.y2);
    
    unsigned int mipMapLevel = MergeImages2D::getLevelFromScale(refImg->getRenderScale().x);
    if (mipMapLevel != 0) {
        searchWindowPixel = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(searchWindowPixel, mipMapLevel);
    }
    
    OfxRectI imageBounds = refImg->getBounds();
    if (!MergeImages2D::rectangleIntersect(imageBounds, searchWindowPixel, &searchWindowPixel)) {
        ///if the search window doesn't intersect the ref image bounds just return there's nothing to do
        return;
    }
    
    
    OfxRectD patternCanonical;
    getPatternCanonical(refTime, &patternCanonical);
    OfxRectI patternPixel;
    patternPixel.x1 = std::floor(patternCanonical.x1);
    patternPixel.y1 = std::floor(patternCanonical.y1);
    patternPixel.x2 = std::ceil(patternCanonical.x2);
    patternPixel.y2 = std::ceil(patternPixel.y2);
    if (mipMapLevel != 0) {
        patternPixel = MergeImages2D::downscalePowerOfTwoSmallestEnclosing(patternPixel, mipMapLevel);
    }
    
    OfxPointD center;
    _center->getValueAtTime(refTime, center.x, center.y);
    ///before intersection to the image bounds, convert to absolute coordinates the pattern window
    patternPixel.x1 += center.x;
    patternPixel.x2 += center.x;
    patternPixel.y1 += center.y;
    patternPixel.y2 += center.y;
    
    if (!MergeImages2D::rectangleIntersect(imageBounds, patternPixel, &patternPixel)) {
        return;
    }
    
    ///now convert back to coords relative to the center for the processing
    patternPixel.x1 -= center.x;
    patternPixel.x2 -= center.x;
    patternPixel.y1 -= center.y;
    patternPixel.y2 -= center.y;

    
    // set the render window
    processor.setRenderWindow(searchWindowPixel);
    
    processor.setPatternWindow(patternPixel);
    
    // round center to nearest pixel center
    OfxPointI centeri;
    centeri.x = std::floor(center.x + 0.5);
    centeri.y = std::floor(center.y + 0.5);
    processor.setCenterI(centeri);
    
    // FIXME: move _bestMatch to TrackerPMProcessorBase, where it belongs
    _bestMatch.second = std::numeric_limits<double>::infinity();
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();

    // TODO: subpixel interpolation

    ///ok the score is now computed, update the center
    OfxPointD newCenter;
    // FIXME: move _bestMatch to TrackerPMProcessorBase, where it belongs
    newCenter.x = center.x + _bestMatch.first.x - centeri.x;
    newCenter.y = center.y + _bestMatch.first.y - centeri.y;
    _center->setValueAtTime(otherTime, newCenter.x, newCenter.y);
}

void
TrackerPMPlugin::getPatternCanonical(OfxTime ref, OfxRectD *bounds) const
{
    OfxPointD innerBtmLeft, innerTopRight, center;
    _innerBtmLeft->getValueAtTime(ref, innerBtmLeft.x, innerBtmLeft.y);
    _innerTopRight->getValueAtTime(ref, innerTopRight.x, innerTopRight.y);
    _center->getValueAtTime(ref, center.x, center.y);
    bounds->x1 = center.x + innerBtmLeft.x;
    bounds->x2 = center.x + innerTopRight.x;
    bounds->y1 = center.y + innerBtmLeft.y;
    bounds->y2 = center.y + innerTopRight.y;
}

void
TrackerPMPlugin::getTrackSearchWindowCanonical(OfxTime other, OfxRectD *bounds) const
{
    OfxPointD innerBtmLeft,innerTopRight;
    _innerBtmLeft->getValueAtTime(other, innerBtmLeft.x, innerBtmLeft.y);
    _innerTopRight->getValueAtTime(other, innerTopRight.x, innerTopRight.y);
    OfxPointD outerBtmLeft, outerTopRight, center;
    _outerBtmLeft->getValueAtTime(other, outerBtmLeft.x, outerBtmLeft.y);
    _outerTopRight->getValueAtTime(other, outerTopRight.x, outerTopRight.y);
    _center->getValueAtTime(other, center.x, center.y);
    
    // subtract the pattern window so that we don't check for pixels out of the search window
    bounds->x1 = center.x + outerBtmLeft.x - innerBtmLeft.x;
    bounds->y1 = center.y + outerBtmLeft.y - innerBtmLeft.y;
    bounds->x2 = center.x + outerTopRight.x - innerTopRight.x;
    bounds->y2 = center.x + outerTopRight.y - innerTopRight.y;
}


// override the roi call
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
TrackerPMPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    
    OfxRectD roi;
    getTrackSearchWindowCanonical(args.time, &roi);
    // set it on the mask only if we are in an interesting context
    // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
    rois.setRegionOfInterest(*srcClip_, roi);
}



// the internal render function
template <int nComponents>
void
TrackerPMPlugin::trackInternal(OfxTime ref, OfxTime other)
{
    OfxRectD refBounds;
    getPatternCanonical(ref, &refBounds);

    OfxRectD otherBounds;
    getTrackSearchWindowCanonical(other, &otherBounds);

    std::auto_ptr<OFX::Image> srcRef(srcClip_->fetchImage(ref, refBounds));
    std::auto_ptr<OFX::Image> srcOther(srcClip_->fetchImage(other, otherBounds));
    if (!srcRef.get() || !srcOther.get()) {
        return;
    }
    if ((srcRef->getPixelDepth() != srcOther->getPixelDepth()) ||
        (srcRef->getPixelComponents() != srcOther->getPixelComponents())) {
        return;
    }

    OFX::BitDepthEnum srcBitDepth = srcRef->getPixelDepth();
    
    switch (srcBitDepth) {
        case OFX::eBitDepthUByte :
        {
            TrackerPMProcessor<unsigned char, nComponents, 255, eTrackerSSD> fred(*this);
            setupAndProcess(fred,ref,other, srcRef.get(),srcOther.get());
        }   break;
        case OFX::eBitDepthUShort :
        {
            TrackerPMProcessor<unsigned short, nComponents, 65535, eTrackerSSD> fred(*this);
            setupAndProcess(fred,ref,other, srcRef.get(),srcOther.get());
        }   break;
        case OFX::eBitDepthFloat :
        {
            TrackerPMProcessor<float, nComponents, 1, eTrackerSSD> fred(*this);
            setupAndProcess(fred,ref,other,srcRef.get(),srcOther.get());
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

