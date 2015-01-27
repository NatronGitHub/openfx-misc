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

#define kParamStatMin "statMin"
#define kParamStatMinLabel "Min."
#define kParamStatMinHint "Minimum value."

#define kParamStatMax "statMax"
#define kParamStatMaxLabel "Max."
#define kParamStatMaxHint "Maximum value."

#define kParamStatMean "statMean"
#define kParamStatMeanLabel "Mean"
#define kParamStatMeanHint "The mean is the average. Add up the values, and divide by the number of values."

#define kParamStatSDev "statSDev"
#define kParamStatSDevLabel "S.Dev."
#define kParamStatSDevHint "The standard deviation (S.Dev.) quantifies variability or scatter, and it is expressed in the same units as your data."

#define kParamStatKurtosis "statKurtosis"
#define kParamStatKurtosisLabel "Kurtosis"
#define kParamStatKurtosisHint \
"Kurtosis quantifies whether the shape of the data distribution matches the Gaussian distribution.\n" \
"•A Gaussian distribution has a kurtosis of 0.\n" \
"•A flatter distribution has a negative kurtosis,\n" \
"•A distribution more peaked than a Gaussian distribution has a positive kurtosis.\n" \
"•Kurtosis has no units.\n" \
"•The value that this plugin reports is sometimes called the excess kurtosis since the expected kurtosis for a Gaussian distribution is 0.0.\n" \
"•An alternative definition of kurtosis is computed by adding 3 to the value reported by this plugin. With this definition, a Gaussian distribution is expected to have a kurtosis of 3.0."

#define kParamStatSkewness "statSkewness"
#define kParamStatSkewnessLabel "Skewness"
#define kParamStatSkewnessHint \
"Skewness quantifies how symmetrical the distribution is.\n" \
"• A symmetrical distribution has a skewness of zero.\n" \
"• An asymmetrical distribution with a long tail to the right (higher values) has a positive skew.\n" \
"• An asymmetrical distribution with a long tail to the left (lower values) has a negative skew.\n" \
"• The skewness is unitless.\n" \
"• Any threshold or rule of thumb is arbitrary, but here is one: If the skewness is greater than 1.0 (or less than -1.0), the skewness is substantial and the distribution is far from symmetrical."

using namespace OFX;

namespace {
    struct RGBAValues {
        double r,g,b,a;
        RGBAValues(double v) : r(v), g(v), b(v), a(v) {}
        RGBAValues() : r(0), g(0), b(0), a(0) {}
    };

    struct Results {
        RGBAValues min;
        RGBAValues max;
        RGBAValues mean;
        RGBAValues sdev;
        RGBAValues skewness;
        RGBAValues kurtosis;
    };
}

class ImageStatisticsProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::MultiThread::Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    unsigned long _count;

public:
    ImageStatisticsProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _mutex()
    , _count(0)
    {
    }

    virtual ~ImageStatisticsProcessorBase()
    {
    }

    virtual void setPrevResults(const Results &results) = 0;

    virtual void getResults(Results *results) = 0;

protected:


    void toRGBA(const double *sum, int nComponents, RGBAValues* rgba)
    {
        if (nComponents == 4) {
            rgba->r = sum[0];
            rgba->g = sum[1];
            rgba->b = sum[2];
            rgba->a = sum[3];
        } else if (nComponents == 3) {
            rgba->r = sum[0];
            rgba->g = sum[1];
            rgba->b = sum[2];
            rgba->a = 0;
        } else if (nComponents == 1) {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = sum[0];
        } else {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = 0;
        }
    }

    void toComponents(const RGBAValues& rgba, double *sum, int nComponents)
    {
        if (nComponents == 4) {
            sum[0] = rgba.r;
            sum[1] = rgba.g;
            sum[2] = rgba.b;
            sum[3] = rgba.a;
        } else if (nComponents == 3) {
            sum[0] = rgba.r;
            sum[1] = rgba.g;
            sum[2] = rgba.b;
        } else if (nComponents == 1) {
            sum[0] = rgba.a;
        }
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
        _statMin = fetchRGBAParam(kParamStatMin);
        _statMax = fetchRGBAParam(kParamStatMax);
        _statMean = fetchRGBAParam(kParamStatMean);
        _statSDev = fetchRGBAParam(kParamStatSDev);
        _statSkewness = fetchRGBAParam(kParamStatSkewness);
        _statKurtosis = fetchRGBAParam(kParamStatKurtosis);
        assert(_statMin && _statMax && _statMean && _statSDev && _statSkewness);
        
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
    void setupAndProcess(ImageStatisticsProcessorBase &processor, OFX::Image* srcImg, double time, const Results &prevResults, Results *results);

    // update image statistics
    void update(OFX::Image* srcImg, double time);

    template <class PIX, int nComponents, int maxValue>
    void updateMinMaxMeanComponentsDepth(OFX::Image* srcImg, double time, Results* results);

    template <int nComponents>
    void updateMinMaxMeanComponents(OFX::Image* srcImg, double time, Results* results);

    void updateMinMaxMean(OFX::Image* srcImg, double time, Results* results);

    template <class PIX, int nComponents, int maxValue>
    void updateSDevComponentsDepth(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

    template <int nComponents>
    void updateSDevComponents(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

    void updateSDev(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

    template <class PIX, int nComponents, int maxValue>
    void updateSkewnessKurtosisComponentsDepth(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

    template <int nComponents>
    void updateSkewnessKurtosisComponents(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

    void updateSkewnessKurtosis(OFX::Image* srcImg, double time, const Results &prevResults, Results* results);

private:

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;

    Double2DParam* _btmLeft;
    Double2DParam* _size;
    BooleanParam* _restrictToRectangle;
    PushButtonParam* _update;
    BooleanParam* _autoUpdate;
    RGBAParam* _statMin;
    RGBAParam* _statMax;
    RGBAParam* _statMean;
    RGBAParam* _statSDev;
    RGBAParam* _statSkewness;
    RGBAParam* _statKurtosis;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


// the overridden render function
void
ImageStatisticsPlugin::render(const OFX::RenderArguments &args)
{
    // do the rendering
    std::auto_ptr<OFX::Image> srcImg(_srcClip->fetchImage(args.time));
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
        std::auto_ptr<OFX::Image> srcImg(_srcClip->fetchImage(args.time));
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
            std::auto_ptr<OFX::Image> srcImg(_srcClip->fetchImage(t));
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
ImageStatisticsPlugin::setupAndProcess(ImageStatisticsProcessorBase &processor, OFX::Image* srcImg, double time, const Results &prevResults, Results *results)
{
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
                                    srcImg->getPixelAspectRatio(),
                                    &renderWindow);
    // stay within bounds
    MergeImages2D::rectIntersection(renderWindow, srcImg->getBounds(), &renderWindow);

    // set the images
    processor.setDstImg(srcImg); // not a bug: we only set dst

    // set the render window
    processor.setRenderWindow(renderWindow);

    processor.setPrevResults(prevResults);

    // Call the base class process member, this will call the derived templated process code
    processor.process();

    if (!abort()) {
        processor.getResults(results);
    }
}

template <class PIX, int nComponents, int maxValue>
class ImageMinMaxMeanProcessor : public ImageStatisticsProcessorBase
{
private:
    double _min[nComponents];
    double _max[nComponents];
    double _sum[nComponents];
public:
    ImageMinMaxMeanProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_min, _min+nComponents, +std::numeric_limits<double>::infinity());
        std::fill(_max, _max+nComponents, -std::numeric_limits<double>::infinity());
        std::fill(_sum, _sum+nComponents, 0.);
    }

    ~ImageMinMaxMeanProcessor()
    {
    }

    void setPrevResults(const Results &results) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA(_min, nComponents, &results->min);
            toRGBA(_max, nComponents, &results->max);
            double mean[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                mean[c] = _sum[c]/_count;
            }
            toRGBA(mean, nComponents, &results->mean);
        }
    }

private:

    void addResults(double min[nComponents], double max[nComponents], double sum[nComponents], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponents; ++c) {
            _min[c] = std::min(_min[c], min[c]);
            _max[c] = std::max(_max[c], max[c]);
            _sum[c] += sum[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double min[nComponents], max[nComponents], sum[nComponents];
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
                    double v = *dstPix;
                    min[c] = std::min(min[c], v);
                    max[c] = std::max(max[c], v);
                    sumLine[c] += v;
                    ++dstPix;
                }
            }
            for (int c = 0; c < nComponents; ++c) {
                sum[c] += sumLine[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }
        
        addResults(min, max, sum, count);
    }
};

template <class PIX, int nComponents, int maxValue>
void
ImageStatisticsPlugin::updateMinMaxMeanComponentsDepth(OFX::Image* srcImg, double time, Results* results)
{
    ImageMinMaxMeanProcessor<PIX, nComponents, maxValue> fred(*this);
    setupAndProcess(fred, srcImg, time, *results, results);
}

template <int nComponents>
void
ImageStatisticsPlugin::updateMinMaxMeanComponents(OFX::Image* srcImg, double time, Results* results)
{
    OFX::BitDepthEnum       srcBitDepth    = srcImg->getPixelDepth();
    switch (srcBitDepth) {
        case OFX::eBitDepthUByte: {
            updateMinMaxMeanComponentsDepth<unsigned char, nComponents, 255>(srcImg, time, results);
            break;
        }
        case OFX::eBitDepthUShort: {
            updateMinMaxMeanComponentsDepth<unsigned short, nComponents, 65535>(srcImg, time, results);
            break;
        }
        case OFX::eBitDepthFloat: {
            updateMinMaxMeanComponentsDepth<float, nComponents, 1>(srcImg, time, results);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
ImageStatisticsPlugin::updateMinMaxMean(OFX::Image* srcImg, double time, Results* results)
{
    OFX::PixelComponentEnum srcComponents  = srcImg->getPixelComponents();
    assert(srcComponents == OFX::ePixelComponentAlpha ||srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA);
    if (srcComponents == OFX::ePixelComponentAlpha) {
        updateMinMaxMeanComponents<1>(srcImg, time, results);
    } else if (srcComponents == OFX::ePixelComponentRGBA) {
        updateMinMaxMeanComponents<4>(srcImg, time, results);
    } else if (srcComponents == OFX::ePixelComponentRGB) {
        updateMinMaxMeanComponents<3>(srcImg, time, results);
    } else {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}



template <class PIX, int nComponents, int maxValue>
class ImageSDevProcessor : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponents];
    double _sum_p2[nComponents];
public:
    ImageSDevProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean+nComponents, 0.);
        std::fill(_sum_p2, _sum_p2+nComponents, 0.);
    }

    ~ImageSDevProcessor()
    {
    }

    void setPrevResults(const Results &results) OVERRIDE FINAL
    {
        toComponents(results.mean, _mean, nComponents);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt(std::max(0., _sum_p2[c]/(_count-1)));
            }
            toRGBA(sdev, nComponents, &results->sdev);
        }
    }

private:

    void addResults(double sum_p2[nComponents], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponents; ++c) {
            _sum_p2[c] += sum_p2[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double sum_p2[nComponents];
        std::fill(sum_p2, sum_p2 + nComponents, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine_p2[nComponents]; // partial sum to avoid underflows
            std::fill(sumLine_p2, sumLine_p2 + nComponents, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                for (int c = 0; c < nComponents; ++c) {
                    double v = (*dstPix - _mean[c]);
                    sumLine_p2[c] += v * v;
                    ++dstPix;
                }
            }
            for (int c = 0; c < nComponents; ++c) {
                sum_p2[c] += sumLine_p2[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(sum_p2, count);
    }
};

template <class PIX, int nComponents, int maxValue>
void
ImageStatisticsPlugin::updateSDevComponentsDepth(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    ImageSDevProcessor<PIX, nComponents, maxValue> fred(*this);
    setupAndProcess(fred, srcImg, time, prevResults, results);
}

template <int nComponents>
void
ImageStatisticsPlugin::updateSDevComponents(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    OFX::BitDepthEnum       srcBitDepth    = srcImg->getPixelDepth();
    switch (srcBitDepth) {
        case OFX::eBitDepthUByte: {
            updateSDevComponentsDepth<unsigned char, nComponents, 255>(srcImg, time, prevResults, results);
            break;
        }
        case OFX::eBitDepthUShort: {
            updateSDevComponentsDepth<unsigned short, nComponents, 65535>(srcImg, time, prevResults, results);
            break;
        }
        case OFX::eBitDepthFloat: {
            updateSDevComponentsDepth<float, nComponents, 1>(srcImg, time, prevResults, results);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
ImageStatisticsPlugin::updateSDev(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    OFX::PixelComponentEnum srcComponents  = srcImg->getPixelComponents();
    assert(srcComponents == OFX::ePixelComponentAlpha ||srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA);
    if (srcComponents == OFX::ePixelComponentAlpha) {
        updateSDevComponents<1>(srcImg, time, prevResults, results);
    } else if (srcComponents == OFX::ePixelComponentRGBA) {
        updateSDevComponents<4>(srcImg, time, prevResults, results);
    } else if (srcComponents == OFX::ePixelComponentRGB) {
        updateSDevComponents<3>(srcImg, time, prevResults, results);
    } else {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}



template <class PIX, int nComponents, int maxValue>
class ImageSkewnessKurtosisProcessor : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponents];
    double _sdev[nComponents];
    double _sum_p3[nComponents];
    double _sum_p4[nComponents];
public:
    ImageSkewnessKurtosisProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean+nComponents, 0.);
        std::fill(_sdev, _sdev+nComponents, 0.);
        std::fill(_sum_p3, _sum_p3+nComponents, 0.);
        std::fill(_sum_p4, _sum_p4+nComponents, 0.);
    }

    ~ImageSkewnessKurtosisProcessor()
    {
    }

    void setPrevResults(const Results &results) OVERRIDE FINAL
    {
        toComponents(results.mean, _mean, nComponents);
        toComponents(results.sdev, _sdev, nComponents);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 2) {
            double skewness[nComponents];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ((double)_count*_count) / ((double)(_count-1)*(_count-2));
            for (int c = 0; c < nComponents; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / _count;
            }
            toRGBA(skewness, nComponents, &results->skewness);
        }
        if (_count > 3) {
            double kurtosis[nComponents];
            double kurtfac = ((double)(_count+1)*_count) / ((double)(_count-1)*(_count-2)*(_count-3));
            double kurtshift = -3 * ((double)(_count-1)*(_count-1)) / ((double)(_count-2)*(_count-3));
            for (int c = 0; c < nComponents; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            toRGBA(kurtosis, nComponents, &results->kurtosis);
        }
    }

private:

    void addResults(double sum_p3[nComponents], double sum_p4[nComponents], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponents; ++c) {
            _sum_p3[c] += sum_p3[c];
            _sum_p4[c] += sum_p4[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double sum_p3[nComponents];
        double sum_p4[nComponents];
        std::fill(sum_p3, sum_p3 + nComponents, 0.);
        std::fill(sum_p4, sum_p4 + nComponents, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine_p3[nComponents]; // partial sum to avoid underflows
            double sumLine_p4[nComponents]; // partial sum to avoid underflows
            std::fill(sumLine_p4, sumLine_p4 + nComponents, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                for (int c = 0; c < nComponents; ++c) {
                    if (_sdev[c] > 0.) {
                        double v = (*dstPix - _mean[c])/_sdev[c];
                        double v2 = v * v;
                        sumLine_p3[c] += v2 * v;
                        sumLine_p4[c] += v2 * v2;
                    }
                    ++dstPix;
                }
            }
            for (int c = 0; c < nComponents; ++c) {
                sum_p3[c] += sumLine_p3[c];
                sum_p4[c] += sumLine_p4[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(sum_p3, sum_p4, count);
    }
};

template <class PIX, int nComponents, int maxValue>
void
ImageStatisticsPlugin::updateSkewnessKurtosisComponentsDepth(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    ImageSkewnessKurtosisProcessor<PIX, nComponents, maxValue> fred(*this);
    setupAndProcess(fred, srcImg, time, prevResults, results);
}

template <int nComponents>
void
ImageStatisticsPlugin::updateSkewnessKurtosisComponents(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    OFX::BitDepthEnum       srcBitDepth    = srcImg->getPixelDepth();
    switch (srcBitDepth) {
        case OFX::eBitDepthUByte: {
            updateSkewnessKurtosisComponentsDepth<unsigned char, nComponents, 255>(srcImg, time, prevResults, results);
            break;
        }
        case OFX::eBitDepthUShort: {
            updateSkewnessKurtosisComponentsDepth<unsigned short, nComponents, 65535>(srcImg, time, prevResults, results);
            break;
        }
        case OFX::eBitDepthFloat: {
            updateSkewnessKurtosisComponentsDepth<float, nComponents, 1>(srcImg, time, prevResults, results);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
ImageStatisticsPlugin::updateSkewnessKurtosis(OFX::Image* srcImg, double time, const Results& prevResults, Results* results)
{
    OFX::PixelComponentEnum srcComponents  = srcImg->getPixelComponents();
    assert(srcComponents == OFX::ePixelComponentAlpha ||srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA);
    if (srcComponents == OFX::ePixelComponentAlpha) {
        updateSkewnessKurtosisComponents<1>(srcImg, time, prevResults, results);
    } else if (srcComponents == OFX::ePixelComponentRGBA) {
        updateSkewnessKurtosisComponents<4>(srcImg, time, prevResults, results);
    } else if (srcComponents == OFX::ePixelComponentRGB) {
        updateSkewnessKurtosisComponents<3>(srcImg, time, prevResults, results);
    } else {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// update image statistics
void
ImageStatisticsPlugin::update(OFX::Image* srcImg, double time)
{
    Results results;
    updateMinMaxMean(srcImg, time, &results);
    if (abort()) {
        return;
    }
    _statMin->setValueAtTime(time, results.min.r, results.min.g, results.min.b, results.min.a);
    _statMax->setValueAtTime(time, results.max.r, results.max.g, results.max.b, results.max.a);
    _statMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
    updateSDev(srcImg, time, results, &results);
    if (abort()) {
        return;
    }
    _statSDev->setValueAtTime(time, results.sdev.r, results.sdev.g, results.sdev.b, results.sdev.a);
    updateSkewnessKurtosis(srcImg, time, results, &results);
    if (abort()) {
        return;
    }
    _statSkewness->setValueAtTime(time, results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    _statKurtosis->setValueAtTime(time, results.kurtosis.r, results.kurtosis.g, results.kurtosis.b, results.kurtosis.a);
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

    // min
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMin);
        param->setLabels(kParamStatMinLabel, kParamStatMinLabel, kParamStatMinLabel);
        param->setHint(kParamStatMinHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // statMax
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMax);
        param->setLabels(kParamStatMaxLabel, kParamStatMaxLabel, kParamStatMaxLabel);
        param->setHint(kParamStatMaxHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // statMean
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMean);
        param->setLabels(kParamStatMeanLabel, kParamStatMeanLabel, kParamStatMeanLabel);
        param->setHint(kParamStatMeanHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // statSDev
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSDev);
        param->setLabels(kParamStatSDevLabel, kParamStatSDevLabel, kParamStatSDevLabel);
        param->setHint(kParamStatSDevHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // statSkewness
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSkewness);
        param->setLabels(kParamStatSkewnessLabel, kParamStatSkewnessLabel, kParamStatSkewnessLabel);
        param->setHint(kParamStatSkewnessHint);
        param->setEvaluateOnChange(false);
        param->setEnabled(false);
        page->addChild(*param);
    }

    // statKurtosis
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatKurtosis);
        param->setLabels(kParamStatKurtosisLabel, kParamStatKurtosisLabel, kParamStatKurtosisLabel);
        param->setHint(kParamStatKurtosisHint);
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

