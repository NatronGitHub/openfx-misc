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
#include "ESATracker.h"

#include <cmath>
#include <map>

#include "ofxsProcessing.H"
#include "ofxsTracking.h"
#include "ofxsMerging.h"

#define kPluginName "ESATracker"
#define kPluginGrouping "Transform"
#define kPluginDescription ""
#define kPluginIdentifier "net.sf.openfx:ESATrackerPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.



using namespace OFX;




class ESATrackerProcessorBase;
////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ESATrackerPlugin : public GenericTrackerPlugin
{
public:
    /** @brief ctor */
    ESATrackerPlugin(OfxImageEffectHandle handle)
    : GenericTrackerPlugin(handle)
    {

    }
    
    void updateSSD(const OfxPointD& point,double ssd);
    
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
    void setupAndProcess(ESATrackerProcessorBase &,OfxTime refTime,OfxTime otherTime,OFX::Image* refImg,OFX::Image* otherImg);

    OfxRectD getTrackSearchWindowCanonical(OfxTime time) const;

    ///The pattern is in coordinates relative to the center point
    OfxRectD getPatternCanonical(OfxTime time) const;
    
    std::pair<OfxPointD,double> _ssd; //< the results for the current processor
    OFX::MultiThread::Mutex _lock; //< this is used so we can multi-thread the tracking and protect the shared results
};


class ESATrackerProcessorBase : public OFX::ImageProcessor
{
protected:
    
    OFX::Image *_refImg;
    OFX::Image *_otherImg;
    OfxRectI _patternWindow;
    OfxPointD _center;
    ESATrackerPlugin* _plugin;
    
public:
    ESATrackerProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _refImg(0)
    , _otherImg(0)
    , _patternWindow()
    , _center()
    , _plugin(dynamic_cast<ESATrackerPlugin*>(&instance))
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
    
    void setCenter(const OfxPointD& center) {
        _center = center;
    }
    
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class ESATrackerProcessor : public ESATrackerProcessorBase
{
public:
    ESATrackerProcessor(OFX::ImageEffect &instance)
    : ESATrackerProcessorBase(instance)
    {
    }
    
private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_refImg && _otherImg);
        double minSSD = INT_MAX;
        OfxPointD point;
        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) break;
            
            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                double ssd = 0;
                for (int i = _patternWindow.y1; i < _patternWindow.y2; ++i) {
                    for (int j = _patternWindow.x1; j < _patternWindow.x2; ++j) {
                        PIX *otherPix = (PIX *) _otherImg->getPixelAddress(x + j, y + i);
                        PIX *refPix = (PIX*) _refImg->getPixelAddress(_center.x + j, _center.y + i);
                        if (otherPix && refPix) {
                            for (int k = 0; k < nComponents; ++k) {
                                ssd += (otherPix[k] - refPix[k]) * (otherPix[k] - refPix[k]);
                            }
                        }
                    }
                }
                if (ssd < minSSD) {
                    minSSD = ssd;
                    point.x = x;
                    point.y = y;
                }
            }
        }

        _plugin->updateSSD(point, minSSD);
    }
};


void
ESATrackerPlugin::updateSSD(const OfxPointD& point,double ssd)
{
    OFX::MultiThread::AutoMutex lock(_lock);
    if (_ssd.second > ssd) {
        _ssd.second = ssd;
        _ssd.first.x = point.x;
        _ssd.first.y = point.y;
    }
}

void
ESATrackerPlugin::trackRange(const OFX::TrackArguments& args)
{
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

}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ESATrackerPlugin::setupAndProcess(ESATrackerProcessorBase &processor,OfxTime refTime,OfxTime otherTime,OFX::Image* refImg,OFX::Image* otherImg)
{
    
    // set an uninitialized image for the dst image
    OFX::Image* dstImg = refImg;
    processor.setDstImg(dstImg);
    processor.setImages(refImg, otherImg);
    
    OfxRectD searchWindowCanonical = getTrackSearchWindowCanonical(refTime);
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
    
    
    OfxRectD patternCanonical = getPatternCanonical(refTime);
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
    
    processor.setCenter(center);
    
    _ssd.second = INT_MAX;
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
    
    ///ok the ssd is now computed, update the center
    _center->setValueAtTime(otherTime, _ssd.first.x, _ssd.first.y);
}

OfxRectD
ESATrackerPlugin::getPatternCanonical(OfxTime time) const
{
    OfxRectD ret;
    OfxPointD innerBtmLeft,innerSize;
    _innerBtmLeft->getValueAtTime(time, innerBtmLeft.x, innerBtmLeft.y);
    _innerSize->getValueAtTime(time, innerSize.x, innerSize.y);
    ret.x1 = innerBtmLeft.x;
    ret.x2 = innerBtmLeft.x + innerSize.x;
    ret.y1 = innerBtmLeft.y;
    ret.y2 = innerBtmLeft.y + innerSize.y;
    return ret;
}

OfxRectD
ESATrackerPlugin::getTrackSearchWindowCanonical(OfxTime time) const
{
    OfxPointD outterBtmLeft,outterSize,center;
    _outterBtmLeft->getValueAtTime(time, outterBtmLeft.x, outterBtmLeft.y);
    _outterSize->getValueAtTime(time, outterSize.x, outterSize.y);
    _center->getValueAtTime(time, center.x, center.y);
    
    OfxRectD roi;
    roi.x1 = center.x + outterBtmLeft.x;
    roi.y1 = center.y + outterBtmLeft.y;
    roi.x2 = roi.x1 + outterSize.x;
    roi.y2 = roi.y1 + outterSize.y;
    return roi;
}


// override the roi call
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
ESATrackerPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    
    OfxRectD roi = getTrackSearchWindowCanonical(args.time);
    // set it on the mask only if we are in an interesting context
    // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
    rois.setRegionOfInterest(*srcClip_, roi);
}



// the internal render function
template <int nComponents>
void
ESATrackerPlugin::trackInternal(OfxTime ref,OfxTime other)
{
    std::auto_ptr<OFX::Image> srcRef(srcClip_->fetchImage(ref));
    std::auto_ptr<OFX::Image> srcOther(srcClip_->fetchImage(other));
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
            ESATrackerProcessor<unsigned char, nComponents, 255> fred(*this);
            setupAndProcess(fred,ref,other, srcRef.get(),srcOther.get());
        }   break;
        case OFX::eBitDepthUShort :
        {
            ESATrackerProcessor<unsigned short, nComponents, 65535> fred(*this);
            setupAndProcess(fred,ref,other, srcRef.get(),srcOther.get());
        }   break;
        case OFX::eBitDepthFloat :
        {
            ESATrackerProcessor<float, nComponents, 1> fred(*this);
            setupAndProcess(fred,ref,other,srcRef.get(),srcOther.get());
        }   break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}


using namespace OFX;

mDeclarePluginFactory(ESATrackerPluginFactory, {}, {});

void ESATrackerPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    genericTrackerDescribe(desc);
    desc.setOverlayInteractDescriptor(new TrackerRegionOverlayDescriptor);
}



OFX::ImageEffect* ESATrackerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new ESATrackerPlugin(handle);
}




void ESATrackerPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    PageParamDescriptor* page = genericTrackerDescribeInContextBegin(desc, context);
    genericTrackerDescribePointParameters(desc, page);

}

void getESATrackerPluginID(OFX::PluginFactoryArray &ids)
{
    static ESATrackerPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

