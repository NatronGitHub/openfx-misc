/*
 OFX ImageStatistics plugin.

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
#include "ImageStatistics.h"

#include <cmath>
#include <climits>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsMerging.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#define POINT_TOLERANCE 6
#define POINT_SIZE 5


#define kPluginName "ImageStatisticsOFX"
#define kPluginGrouping "Other"
#define kPluginDescription \
"Compute image statistics over the whole image or over a rectangle."
#define kPluginIdentifier "net.sf.openfx.ImageStatistics"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 0 // no renderscale support
#define kRenderThreadSafety eRenderFullySafe


#define kParamRestrictToRectangle "restrictToRectangle"
#define kParamRestrictToRectangleLabel "Restrict to Rectangle"
#define kParamRestrictToRectangleHint "Restrict statistics computation to a rectangle."

#define kParamAnalyzeFrame "analyzeFrame"
#define kParamAnalyzeFrameLabel "Analyze Frame"
#define kParamAnalyzeFrameHint "Analyze current frame and set values."

#define kParamAnalyzeSequence "analyzeSequence"
#define kParamAnalyzeSequenceLabel "Analyze Sequence"
#define kParamAnalyzeSequenceHint "Analyze all frames from the sequence and set values."

#define kParamAutoUpdate "autoUpdate"
#define kParamAutoUpdateLabel "Auto Update"
#define kParamAutoUpdateHint "Automatically update values when input changes. If not checked, values are only updated if the plugin parameters change."

#define kParamStatMean "statMean"
#define kParamStatMeanLabel "Mean"
#define kParamStatMeanHint "Mean value."

using namespace OFX;

namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };

    struct Results {
        RGBAValues mean;
    };
}

class ImageStatisticsProcessorBase : public OFX::ImageProcessor
{
protected:
    OfxRectI _rectangle;

    OFX::MultiThread::Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    RGBAValues _sum;
    unsigned long _count;

public:
    ImageStatisticsProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _sum()
    , _count(0)
    {
        _rectangle.x1 = _rectangle.x2 = _rectangle.y1 = _rectangle.y2 = 0;
    }

    virtual ~ImageStatisticsProcessorBase()
    {
    }

    bool setValues(OFX::Image *src, const OfxRectD & regionOfInterest)
    {
        OfxRectI rectangle;
        OfxPointD rsOne = { 1., 1.};
        MergeImages2D::toPixelEnclosing(regionOfInterest,
                                        rsOne,
                                        src->getPixelAspectRatio(),
                                        &rectangle);
        MergeImages2D::rectIntersection(rectangle, src->getBounds(), &rectangle);
        setRectangle(rectangle);
        setDstImg(src);
        return true;
    }

    void getResults(Results* results) const
    {
        assert(results);
        if (_count == 0) {
            results->mean.r = results->mean.g = results->mean.b = results->mean.a = 0.;
        } else {
            results->mean.r = _sum.r / _count;
            results->mean.g = _sum.g / _count;
            results->mean.b = _sum.b / _count;
            results->mean.a = _sum.a / _count;
        }
    }

protected:
    void addResults(const RGBAValues& sum, unsigned long count) {
        _mutex.lock();
        _sum.r += sum.r;
        _sum.g += sum.g;
        _sum.b += sum.b;
        _sum.a += sum.a;
        _count += count;
        _mutex.unlock();
    }

private:
    void setRectangle(const OfxRectI& rectangle) {
        _rectangle = rectangle;
    }
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class ImageStatisticsProcessor : public ImageStatisticsProcessorBase
{
protected:

public:
    ImageStatisticsProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
    }

    ~ImageStatisticsProcessor()
    {
    }
private:

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        ///we're not interested in the alpha channel for RGBA images
        double sum[nComponents];
        std::fill(sum, sum + nComponents, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine[nComponents]; // partial sum to avoid underflows
            std::fill(sumLine, sumLine + nComponents, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                for (int c = 0; c < nComponents; ++c) {
                    sumLine[c] += *dstPix;
                    ++dstPix;
                }
            }
            for (int c = 0; c < nComponents; ++c) {
                sum[c] += sumLine[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        RGBAValues rgba;
        if (nComponents == 4) {
            rgba.r = sum[0];
            rgba.g = sum[1];
            rgba.b = sum[2];
            rgba.a = sum[3];
        } else if (nComponents == 3) {
            rgba.r = sum[0];
            rgba.g = sum[1];
            rgba.b = sum[2];
            rgba.a = 0;
        } else if (nComponents == 1) {
            rgba.r = 0;
            rgba.g = 0;
            rgba.b = 0;
            rgba.a = sum[0];
        } else {
            rgba.r = 0;
            rgba.g = 0;
            rgba.b = 0;
            rgba.a = 0;
        }
        addResults(rgba, count);
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ImageStatisticsPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ImageStatisticsPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _btmLeft(0)
    , _size(0)
    , _restrictToRectangle(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));

        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _restrictToRectangle = fetchBooleanParam(kParamRestrictToRectangle);
        assert(_btmLeft && _size && _restrictToRectangle);
        _update = fetchPushButtonParam(kParamAnalyzeFrame);
        _autoUpdate = fetchBooleanParam(kParamAutoUpdate);
        assert(_update && _autoUpdate);
        _statMean = fetchRGBAParam(kParamStatMean);
        assert(_statMean);
        
        // update visibility
        bool restrictToRectangle;
        _restrictToRectangle->getValue(restrictToRectangle);
        _btmLeft->setEnabled(!restrictToRectangle);
        _btmLeft->setIsSecret(restrictToRectangle);
        _size->setEnabled(!restrictToRectangle);
        _size->setIsSecret(restrictToRectangle);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;


    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;


    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ImageStatisticsProcessorBase &processor, double time);

    // update image statistics
    void update(const OFX::Image* srcImg, double time);

private:

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;

    Double2DParam* _btmLeft;
    Double2DParam* _size;
    BooleanParam* _restrictToRectangle;
    PushButtonParam* _update;
    BooleanParam* _autoUpdate;
    RGBAParam* _statMean;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


// the overridden render function
void
ImageStatisticsPlugin::render(const OFX::RenderArguments &args)
{
    // do the rendering
    std::auto_ptr<const OFX::Image> srcImg(_srcClip->fetchImage(args.time));
    std::auto_ptr<OFX::Image> dstImg(_dstClip->fetchImage(args.time));
    copyPixels(*this, args.renderWindow, srcImg.get(), dstImg.get());

    // compute statistics if it is an interactive render
    //if (args.interactiveRenderStatus) {
    bool autoUpdate;
    _autoUpdate->getValueAtTime(args.time, autoUpdate);
    assert(autoUpdate); // render should only be called if autoUpdate is true: in other cases isIdentity returns true
    if (autoUpdate) {
        // check if there is already a Keyframe, if yes update it
        int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
        if (k != -1) {
            update(srcImg.get(), args.time);
        }
    }
    //}
}

bool
ImageStatisticsPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                  OFX::Clip * &identityClip,
                                  double &/*identityTime*/)
{
    bool autoUpdate;
    _autoUpdate->getValue(autoUpdate);

    if (!autoUpdate) {
        identityClip = _srcClip;
        return true;
    } else {
        return false;
    }
}


void
ImageStatisticsPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                    const std::string &paramName)
{
    bool doUpdate = false;
    bool doAnalyze = false;

    if (paramName == kParamRestrictToRectangle) {
        // update visibility
        bool restrictToRectangle;
        _restrictToRectangle->getValue(restrictToRectangle);
        _btmLeft->setEnabled(!restrictToRectangle);
        _btmLeft->setIsSecret(restrictToRectangle);
        _size->setEnabled(!restrictToRectangle);
        _size->setIsSecret(restrictToRectangle);
        doUpdate = true;
    }
    if (paramName == kParamRectangleInteractBtmLeft ||
        paramName == kParamRectangleInteractSize) {
        doUpdate = true;
    }
    if (paramName == kParamAnalyzeFrame) {
        doAnalyze = true;
    }
    if (doUpdate) {
        // check if there is already a Keyframe, if yes update it
        int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyze = (k != -1);
    }
    if (doAnalyze) {
        std::auto_ptr<const OFX::Image> srcImg(_srcClip->fetchImage(args.time));
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
        update(srcImg.get(), args.time);
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
    }
    if (paramName == kParamAnalyzeSequence) {
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
        progressStart("Analyzing sequence...");
        OfxRangeD range;
        getTimeDomain(range);
        int tmin = std::ceil(range.min);
        int tmax = std::floor(range.max);
        for (int t = tmin; t <= tmax; ++t) {
            std::auto_ptr<const OFX::Image> srcImg(_srcClip->fetchImage(t));
            update(srcImg.get(), t);
            if (tmax != tmin) {
                progressUpdate((t-tmin)/(double)(tmax-tmin));
            }
        }
        progressEnd();
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
    }
}

/* set up and run a processor */
void
ImageStatisticsPlugin::setupAndProcess(ImageStatisticsProcessorBase &processor, double time)
{
    // fetch main input image
    std::auto_ptr<OFX::Image> src(_srcClip->fetchImage(time));
    OfxRectI renderWindow;
    OfxPointD rsOne = { 1., 1.};

    OfxRectD regionOfInterest;
    bool restrictToRectangle;
    _restrictToRectangle->getValueAtTime(time, restrictToRectangle);
    if (!restrictToRectangle) {
        // use the src region of definition as rectangle, but avoid infinite rectangle
        regionOfInterest = _srcClip->getRegionOfDefinition(time);
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        if (regionOfInterest.x1 <= kOfxFlagInfiniteMin) {
            regionOfInterest.x1 = offset.x;
        }
        if (regionOfInterest.x2 >= kOfxFlagInfiniteMax) {
            regionOfInterest.x2 = offset.x + size.x;
        }
        if (regionOfInterest.y1 <= kOfxFlagInfiniteMin) {
            regionOfInterest.y1 = offset.y;
        }
        if (regionOfInterest.y2 >= kOfxFlagInfiniteMax) {
            regionOfInterest.y2 = offset.y + size.y;
        }
    } else {
        _btmLeft->getValueAtTime(time, regionOfInterest.x1, regionOfInterest.y1);
        _size->getValueAtTime(time, regionOfInterest.x2, regionOfInterest.y2);
        regionOfInterest.x2 += regionOfInterest.x1;
        regionOfInterest.y2 += regionOfInterest.y1;
    }
    MergeImages2D::toPixelEnclosing(regionOfInterest,
                                    rsOne,
                                    src->getPixelAspectRatio(),
                                    &renderWindow);
    // stay within bounds
    MergeImages2D::rectIntersection(renderWindow, src->getBounds(), &renderWindow);

    // set the images
    processor.setDstImg(src.get()); // not a bug: we only set dst

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();

    Results results;

    processor.getResults(&results);

    _statMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
}

// update image statistics
void
ImageStatisticsPlugin::update(const OFX::Image* srcImg, double time)
{
    // instantiate the render code based on the pixel depth of the src clip
    OFX::BitDepthEnum       srcBitDepth    = _srcClip->getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = _srcClip->getPixelComponents();

    Results results;

    assert(srcComponents == OFX::ePixelComponentAlpha ||srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA);
    if (srcComponents == OFX::ePixelComponentAlpha) {
        switch (srcBitDepth) {
            case OFX::eBitDepthUByte: {
                ImageStatisticsProcessor<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthUShort: {
                ImageStatisticsProcessor<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthFloat: {
                ImageStatisticsProcessor<float, 1, 1> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (srcComponents == OFX::ePixelComponentRGBA) {
        switch (srcBitDepth) {
            case OFX::eBitDepthUByte: {
                ImageStatisticsProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthUShort: {
                ImageStatisticsProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthFloat: {
                ImageStatisticsProcessor<float, 4, 1> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(srcComponents == OFX::ePixelComponentRGB);
        switch (srcBitDepth) {
            case OFX::eBitDepthUByte: {
                ImageStatisticsProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthUShort: {
                ImageStatisticsProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            case OFX::eBitDepthFloat: {
                ImageStatisticsProcessor<float, 3, 1> fred(*this);
                setupAndProcess(fred, time);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

class ImageStatisticsInteract : public RectangleInteract
{
public:

    ImageStatisticsInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : RectangleInteract(handle,effect)
    , _restrictToRectangle(0)
    {
        _restrictToRectangle = effect->fetchBooleanParam(kParamRestrictToRectangle);
        addParamToSlaveTo(_restrictToRectangle);
    }


private:

    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL {
        bool restrictToRectangle;
        _restrictToRectangle->getValueAtTime(args.time, restrictToRectangle);
        if (restrictToRectangle) {
            return RectangleInteract::draw(args);
        }
        return false;
    }
    virtual bool penMotion(const OFX::PenArgs &args) OVERRIDE FINAL {
        bool restrictToRectangle;
        _restrictToRectangle->getValueAtTime(args.time, restrictToRectangle);
        if (restrictToRectangle) {
            return RectangleInteract::penMotion(args);
        }
        return false;
    }
    virtual bool penDown(const OFX::PenArgs &args) OVERRIDE FINAL {
        bool restrictToRectangle;
        _restrictToRectangle->getValueAtTime(args.time, restrictToRectangle);
        if (restrictToRectangle) {
            return RectangleInteract::penDown(args);
        }
        return false;
    }
    virtual bool penUp(const OFX::PenArgs &args) OVERRIDE FINAL {
        bool restrictToRectangle;
        _restrictToRectangle->getValueAtTime(args.time, restrictToRectangle);
        if (restrictToRectangle) {
            return RectangleInteract::penUp(args);
        }
        return false;
    }
    //virtual bool keyDown(const OFX::KeyArgs &args) OVERRIDE;
    //virtual bool keyUp(const OFX::KeyArgs & args) OVERRIDE;
    //virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;


    OFX::BooleanParam* _restrictToRectangle;
};

class ImageStatisticsOverlayDescriptor : public DefaultEffectOverlayDescriptor<ImageStatisticsOverlayDescriptor, ImageStatisticsInteract> {};

mDeclarePluginFactory(ImageStatisticsPluginFactory, {}, {});

void ImageStatisticsPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setSupportsTiles(kSupportsTiles);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setOverlayInteractDescriptor(new ImageStatisticsOverlayDescriptor);
}



OFX::ImageEffect* ImageStatisticsPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ImageStatisticsPlugin(handle);
}




void ImageStatisticsPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setOptional(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // autoUpdate
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamRestrictToRectangle);
        param->setLabels(kParamRestrictToRectangleLabel, kParamRestrictToRectangleLabel, kParamRestrictToRectangleLabel);
        param->setHint(kParamRestrictToRectangleHint);
        param->setDefault(true);
        param->setAnimates(false);
        page->addChild(*param);
    }

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabels(kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel,kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setIncrement(1.);
        param->setHint(kParamRectangleInteractBtmLeftHint);
        param->setDigits(0);
        page->addChild(*param);
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabels(kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel, kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(1., 1.);
        param->setIncrement(1.);
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint(kParamRectangleInteractSizeHint);
        param->setDigits(0);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }

    // mean
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMean);
        param->setLabels(kParamStatMeanLabel, kParamStatMeanLabel, kParamStatMeanLabel);
        param->setHint(kParamStatMeanHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // analyzeFrame
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrame);
        param->setLabels(kParamAnalyzeFrameLabel, kParamAnalyzeFrameLabel, kParamAnalyzeFrameLabel);
        param->setHint(kParamAnalyzeFrameHint);
        page->addChild(*param);
    }

    // analyzeSequence
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequence);
        param->setLabels(kParamAnalyzeSequenceLabel, kParamAnalyzeSequenceLabel, kParamAnalyzeSequenceLabel);
        param->setHint(kParamAnalyzeSequenceHint);
        page->addChild(*param);
    }

    // autoUpdate
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAutoUpdate);
        param->setLabels(kParamAutoUpdateLabel, kParamAutoUpdateLabel, kParamAutoUpdateLabel);
        param->setHint(kParamAutoUpdateHint);
        param->setDefault(true);
        param->setAnimates(false);
        page->addChild(*param);
    }
}

void getImageStatisticsPluginID(OFX::PluginFactoryArray &ids)
{
    static ImageStatisticsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

