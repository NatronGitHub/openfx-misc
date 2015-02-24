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
#include "ofxsLut.h"

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#ifdef _WINDOWS
#include <windows.h>
#define isnan _isnan
#else
using std::isnan;
#endif

#

#define POINT_TOLERANCE 6
#define POINT_SIZE 5


#define kPluginName "ImageStatisticsOFX"
#define kPluginGrouping "Other"
#define kPluginDescription \
"Compute image statistics over the whole image or over a rectangle. " \
"The statistics can be computed either on RGBA components or in the HSVL colorspace " \
"(which is the HSV coilorspace with an additional L component from HSL)."
#define kPluginIdentifier "net.sf.openfx.ImageStatistics"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 0 // no renderscale support: statistics are computed at full resolution
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
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

#define kParamClearFrame "clearFrame"
#define kParamClearFrameLabel "Clear Frame"
#define kParamClearFrameHint "Clear analysis for current frame."

#define kParamClearSequence "clearSequence"
#define kParamClearSequenceLabel "Clear Sequence"
#define kParamClearSequenceHint "Clear analysis for all frames from the sequence."

#define kParamAutoUpdate "autoUpdate"
#define kParamAutoUpdateLabel "Auto Update"
#define kParamAutoUpdateHint "Automatically update values when input or rectangle changes if an analysis was performed at current frame. If not checked, values are only updated if the plugin parameters change. "

#define kParamGroupRGBA "RGBA"

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


#define kParamGroupHSVL "HSVL"

#define kParamAnalyzeFrameHSVL "analyzeFrameHSVL"
#define kParamAnalyzeFrameHSVLLabel "Analyze Frame"
#define kParamAnalyzeFrameHSVLHint "Analyze current frame as HSVL and set values."

#define kParamAnalyzeSequenceHSVL "analyzeSequenceHSVL"
#define kParamAnalyzeSequenceHSVLLabel "Analyze Sequence"
#define kParamAnalyzeSequenceHSVLHint "Analyze all frames from the sequence as HSVL and set values."

#define kParamClearFrameHSVL "clearFrameHSVL"
#define kParamClearFrameHSVLLabel "Clear Frame"
#define kParamClearFrameHSVLHint "Clear HSVL analysis for current frame."

#define kParamClearSequenceHSVL "clearSequenceHSVL"
#define kParamClearSequenceHSVLLabel "Clear Sequence"
#define kParamClearSequenceHSVLHint "Clear HSVL analysis for all frames from the sequence."

#define kParamStatHSVLMin "statHSVLMin"
#define kParamStatHSVLMinLabel "HSVL Min."
#define kParamStatHSVLMinHint "Minimum value."

#define kParamStatHSVLMax "statHSVLMax"
#define kParamStatHSVLMaxLabel "HSVL Max."
#define kParamStatHSVLMaxHint "Maximum value."

#define kParamStatHSVLMean "statHSVLMean"
#define kParamStatHSVLMeanLabel "HSVL Mean"
#define kParamStatHSVLMeanHint "The mean is the average. Add up the values, and divide by the number of values."

#define kParamStatHSVLSDev "statHSVLSDev"
#define kParamStatHSVLSDevLabel "HSVL S.Dev."
#define kParamStatHSVLSDevHint "The standard deviation (S.Dev.) quantifies variability or scatter, and it is expressed in the same units as your data."

#define kParamStatHSVLKurtosis "statHSVLKurtosis"
#define kParamStatHSVLKurtosisLabel "HSVL Kurtosis"
#define kParamStatHSVLKurtosisHint \
"Kurtosis quantifies whether the shape of the data distribution matches the Gaussian distribution.\n" \
"•A Gaussian distribution has a kurtosis of 0.\n" \
"•A flatter distribution has a negative kurtosis,\n" \
"•A distribution more peaked than a Gaussian distribution has a positive kurtosis.\n" \
"•Kurtosis has no units.\n" \
"•The value that this plugin reports is sometimes called the excess kurtosis since the expected kurtosis for a Gaussian distribution is 0.0.\n" \
"•An alternative definition of kurtosis is computed by adding 3 to the value reported by this plugin. With this definition, a Gaussian distribution is expected to have a kurtosis of 3.0."

#define kParamStatHSVLSkewness "statHSVLSkewness"
#define kParamStatHSVLSkewnessLabel "HSVL Skewness"
#define kParamStatHSVLSkewnessHint \
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

    template<class PIX, int nComponents, int maxValue>
    void toRGBA(const PIX *p, RGBAValues* rgba)
    {
        if (nComponents == 4) {
            rgba->r = p[0]/(double)maxValue;
            rgba->g = p[1]/(double)maxValue;
            rgba->b = p[2]/(double)maxValue;
            rgba->a = p[3]/(double)maxValue;
        } else if (nComponents == 3) {
            rgba->r = p[0]/(double)maxValue;
            rgba->g = p[1]/(double)maxValue;
            rgba->b = p[2]/(double)maxValue;
            rgba->a = 0;
        } else if (nComponents == 1) {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = p[0]/(double)maxValue;
        } else {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = 0;
        }
    }

    template<class PIX, int nComponents, int maxValue>
    void
    pixToHSVL(const PIX *p, float hsvl[4])
    {
        if (nComponents == 4 || nComponents == 3) {
            float r, g, b;
            r = p[0]/(float)maxValue;
            g = p[1]/(float)maxValue;
            b = p[2]/(float)maxValue;
            OFX::Color::rgb_to_hsv(r, g, b, &hsvl[0], &hsvl[1], &hsvl[2]);
            float min = std::min(std::min(r, g), b);
            float max = std::max(std::max(r, g), b);
            hsvl[3] = (min + max)/2;
        } else {
            hsvl[0] = hsvl[1] = hsvl[2] = hsvl[3] = 0.f;
        }
    }

    template<class PIX, int nComponents, int maxValue>
    void toComponents(const RGBAValues& rgba, PIX *p)
    {
        if (nComponents == 4) {
            p[0] = rgba.r * maxValue + ((maxValue != 1) ? 0.5 : 0);
            p[1] = rgba.g * maxValue + ((maxValue != 1) ? 0.5 : 0);
            p[2] = rgba.b * maxValue + ((maxValue != 1) ? 0.5 : 0);
            p[3] = rgba.a * maxValue + ((maxValue != 1) ? 0.5 : 0);
        } else if (nComponents == 3) {
            p[0] = rgba.r * maxValue + ((maxValue != 1) ? 0.5 : 0);
            p[1] = rgba.g * maxValue + ((maxValue != 1) ? 0.5 : 0);
            p[2] = rgba.b * maxValue + ((maxValue != 1) ? 0.5 : 0);
        } else if (nComponents == 1) {
            p[0] = rgba.a * maxValue + ((maxValue != 1) ? 0.5 : 0);
        }
    }
};


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

    void setPrevResults(const Results &/*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nComponents, 1>(_min, &results->min);
            toRGBA<double, nComponents, 1>(_max, &results->max);
            double mean[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                mean[c] = _sum[c]/_count;
            }
            toRGBA<double, nComponents, 1>(mean, &results->mean);
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
        std::fill(min, min+nComponents, +std::numeric_limits<double>::infinity());
        std::fill(max, max+nComponents, -std::numeric_limits<double>::infinity());
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
        toComponents<double, nComponents, 1>(results.mean, _mean);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt(std::max(0., _sum_p2[c]/(_count-1)));
            }
            toRGBA<double, nComponents, 1>(sdev, &results->sdev);
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
        toComponents<double, nComponents, 1>(results.mean, _mean);
        toComponents<double, nComponents, 1>(results.sdev, _sdev);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 2) {
            double skewness[nComponents];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ((double)_count*_count) / ((double)(_count-1)*(_count-2));
            assert(!isnan(skewfac));
            for (int c = 0; c < nComponents; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / _count;
            }
            toRGBA<double, nComponents, 1>(skewness, &results->skewness);
            assert(!isnan(results->skewness.r) && !isnan(results->skewness.g) && !isnan(results->skewness.b) && !isnan(results->skewness.a));
        }
        if (_count > 3) {
            double kurtosis[nComponents];
            double kurtfac = ((double)(_count+1)*_count) / ((double)(_count-1)*(_count-2)*(_count-3));
            double kurtshift = -3 * ((double)(_count-1)*(_count-1)) / ((double)(_count-2)*(_count-3));
            assert(!isnan(kurtfac) && !isnan(kurtshift));
            for (int c = 0; c < nComponents; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            toRGBA<double, nComponents, 1>(kurtosis, &results->kurtosis);
            assert(!isnan(results->kurtosis.r) && !isnan(results->kurtosis.g) && !isnan(results->kurtosis.b) && !isnan(results->kurtosis.a));
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
            std::fill(sumLine_p3, sumLine_p3 + nComponents, 0.);
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

#define nComponentsHSVL 4

template <class PIX, int nComponents, int maxValue>
class ImageHSVLMinMaxMeanProcessor : public ImageStatisticsProcessorBase
{
private:
    double _min[nComponentsHSVL];
    double _max[nComponentsHSVL];
    double _sum[nComponentsHSVL];
public:
    ImageHSVLMinMaxMeanProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_min, _min+nComponentsHSVL, +std::numeric_limits<double>::infinity());
        std::fill(_max, _max+nComponentsHSVL, -std::numeric_limits<double>::infinity());
        std::fill(_sum, _sum+nComponentsHSVL, 0.);
    }

    ~ImageHSVLMinMaxMeanProcessor()
    {
    }

    void setPrevResults(const Results &/*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nComponentsHSVL, 1>(_min, &results->min);
            toRGBA<double, nComponentsHSVL, 1>(_max, &results->max);
            double mean[nComponentsHSVL];
            for (int c = 0; c < nComponentsHSVL; ++c) {
                mean[c] = _sum[c]/_count;
            }
            toRGBA<double, nComponentsHSVL, 1>(mean, &results->mean);
        }
    }

private:

    void addResults(double min[nComponentsHSVL], double max[nComponentsHSVL], double sum[nComponentsHSVL], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _min[c] = std::min(_min[c], min[c]);
            _max[c] = std::max(_max[c], max[c]);
            _sum[c] += sum[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double min[nComponentsHSVL], max[nComponentsHSVL], sum[nComponentsHSVL];
        std::fill(min, min+nComponentsHSVL, +std::numeric_limits<double>::infinity());
        std::fill(max, max+nComponentsHSVL, -std::numeric_limits<double>::infinity());
        std::fill(sum, sum + nComponentsHSVL, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine[nComponentsHSVL]; // partial sum to avoid underflows
            std::fill(sumLine, sumLine + nComponentsHSVL, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                float hsvl[nComponentsHSVL];
                pixToHSVL<PIX, nComponents, maxValue>(dstPix, hsvl);
                for (int c = 0; c < nComponentsHSVL; ++c) {
                    double v = hsvl[c];
                    min[c] = std::min(min[c], v);
                    max[c] = std::max(max[c], v);
                    sumLine[c] += v;
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nComponentsHSVL; ++c) {
                sum[c] += sumLine[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(min, max, sum, count);
    }
};




template <class PIX, int nComponents, int maxValue>
class ImageHSVLSDevProcessor : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponentsHSVL];
    double _sum_p2[nComponentsHSVL];
public:
    ImageHSVLSDevProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean+nComponentsHSVL, 0.);
        std::fill(_sum_p2, _sum_p2+nComponentsHSVL, 0.);
    }

    ~ImageHSVLSDevProcessor()
    {
    }

    void setPrevResults(const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponentsHSVL, 1>(results.mean, _mean);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nComponentsHSVL];
            for (int c = 0; c < nComponentsHSVL; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt(std::max(0., _sum_p2[c]/(_count-1)));
            }
            toRGBA<double, nComponentsHSVL, 1>(sdev, &results->sdev);
        }
    }

private:

    void addResults(double sum_p2[nComponentsHSVL], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _sum_p2[c] += sum_p2[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double sum_p2[nComponentsHSVL];
        std::fill(sum_p2, sum_p2 + nComponentsHSVL, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine_p2[nComponentsHSVL]; // partial sum to avoid underflows
            std::fill(sumLine_p2, sumLine_p2 + nComponentsHSVL, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                float hsvl[nComponentsHSVL];
                pixToHSVL<PIX, nComponents, maxValue>(dstPix, hsvl);
                for (int c = 0; c < nComponentsHSVL; ++c) {
                    double v = (hsvl[c] - _mean[c]);
                    sumLine_p2[c] += v * v;
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nComponentsHSVL; ++c) {
                sum_p2[c] += sumLine_p2[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(sum_p2, count);
    }
};




template <class PIX, int nComponents, int maxValue>
class ImageHSVLSkewnessKurtosisProcessor : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponentsHSVL];
    double _sdev[nComponentsHSVL];
    double _sum_p3[nComponentsHSVL];
    double _sum_p4[nComponentsHSVL];
public:
    ImageHSVLSkewnessKurtosisProcessor(OFX::ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean+nComponentsHSVL, 0.);
        std::fill(_sdev, _sdev+nComponentsHSVL, 0.);
        std::fill(_sum_p3, _sum_p3+nComponentsHSVL, 0.);
        std::fill(_sum_p4, _sum_p4+nComponentsHSVL, 0.);
    }

    ~ImageHSVLSkewnessKurtosisProcessor()
    {
    }

    void setPrevResults(const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponentsHSVL, 1>(results.mean, _mean);
        toComponents<double, nComponentsHSVL, 1>(results.sdev, _sdev);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 2) {
            double skewness[nComponentsHSVL];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ((double)_count*_count) / ((double)(_count-1)*(_count-2));
            for (int c = 0; c < nComponentsHSVL; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / _count;
            }
            toRGBA<double, nComponentsHSVL, 1>(skewness, &results->skewness);
        }
        if (_count > 3) {
            double kurtosis[nComponentsHSVL];
            double kurtfac = ((double)(_count+1)*_count) / ((double)(_count-1)*(_count-2)*(_count-3));
            double kurtshift = -3 * ((double)(_count-1)*(_count-1)) / ((double)(_count-2)*(_count-3));
            for (int c = 0; c < nComponentsHSVL; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            toRGBA<double, nComponentsHSVL, 1>(kurtosis, &results->kurtosis);
        }
    }

private:

    void addResults(double sum_p3[nComponentsHSVL], double sum_p4[nComponentsHSVL], unsigned long count) {
        _mutex.lock();
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _sum_p3[c] += sum_p3[c];
            _sum_p4[c] += sum_p4[c];
        }
        _count += count;
        _mutex.unlock();
    }


    void multiThreadProcessImages(OfxRectI procWindow)
    {
        double sum_p3[nComponentsHSVL];
        double sum_p4[nComponentsHSVL];
        std::fill(sum_p3, sum_p3 + nComponentsHSVL, 0.);
        std::fill(sum_p4, sum_p4 + nComponentsHSVL, 0.);
        unsigned long count = 0;
        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double sumLine_p3[nComponentsHSVL]; // partial sum to avoid underflows
            double sumLine_p4[nComponentsHSVL]; // partial sum to avoid underflows
            std::fill(sumLine_p3, sumLine_p3 + nComponentsHSVL, 0.);
            std::fill(sumLine_p4, sumLine_p4 + nComponentsHSVL, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                float hsvl[nComponentsHSVL];
                pixToHSVL<PIX, nComponents, maxValue>(dstPix, hsvl);
                for (int c = 0; c < nComponentsHSVL; ++c) {
                    if (_sdev[c] > 0.) {
                        double v = (hsvl[c] - _mean[c])/_sdev[c];
                        double v2 = v * v;
                        sumLine_p3[c] += v2 * v;
                        sumLine_p4[c] += v2 * v2;
                    }
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nComponentsHSVL; ++c) {
                sum_p3[c] += sumLine_p3[c];
                sum_p4[c] += sumLine_p4[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }
        
        addResults(sum_p3, sum_p4, count);
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
    , _interactive(0)
    , _restrictToRectangle(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));

        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _interactive = fetchBooleanParam(kParamRectangleInteractInteractive);
        _restrictToRectangle = fetchBooleanParam(kParamRestrictToRectangle);
        _autoUpdate = fetchBooleanParam(kParamAutoUpdate);
        assert(_btmLeft && _size && _interactive && _restrictToRectangle && _autoUpdate);
        _statMin = fetchRGBAParam(kParamStatMin);
        _statMax = fetchRGBAParam(kParamStatMax);
        _statMean = fetchRGBAParam(kParamStatMean);
        _statSDev = fetchRGBAParam(kParamStatSDev);
        _statSkewness = fetchRGBAParam(kParamStatSkewness);
        _statKurtosis = fetchRGBAParam(kParamStatKurtosis);
        assert(_statMin && _statMax && _statMean && _statSDev && _statSkewness);
        _analyzeFrame = fetchPushButtonParam(kParamAnalyzeFrame);
        _analyzeSequence = fetchPushButtonParam(kParamAnalyzeSequence);
        assert(_analyzeFrame && _analyzeSequence);
        _statHSVLMin = fetchRGBAParam(kParamStatHSVLMin);
        _statHSVLMax = fetchRGBAParam(kParamStatHSVLMax);
        _statHSVLMean = fetchRGBAParam(kParamStatHSVLMean);
        _statHSVLSDev = fetchRGBAParam(kParamStatHSVLSDev);
        _statHSVLSkewness = fetchRGBAParam(kParamStatHSVLSkewness);
        _statHSVLKurtosis = fetchRGBAParam(kParamStatHSVLKurtosis);
        assert(_statHSVLMin && _statHSVLMax && _statHSVLMean && _statHSVLSDev && _statHSVLSkewness);
        _analyzeFrameHSVL = fetchPushButtonParam(kParamAnalyzeFrameHSVL);
        _analyzeSequenceHSVL = fetchPushButtonParam(kParamAnalyzeSequenceHSVL);
        assert(_analyzeFrameHSVL && _analyzeSequenceHSVL);

        // update visibility
        bool restrictToRectangle;
        _restrictToRectangle->getValue(restrictToRectangle);
        _btmLeft->setEnabled(restrictToRectangle);
        _btmLeft->setIsSecret(!restrictToRectangle);
        _size->setEnabled(restrictToRectangle);
        _size->setIsSecret(!restrictToRectangle);
        bool doUpdate;
        _autoUpdate->getValue(doUpdate);
        _interactive->setEnabled(restrictToRectangle && doUpdate);
        _interactive->setIsSecret(!restrictToRectangle || !doUpdate);
    }

private:
    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;


    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD & rod) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ImageStatisticsProcessorBase &processor, const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow, const Results &prevResults, Results *results);

    // compute computation window in srcImg
    void computeWindow(const OFX::Image* srcImg, double time, OfxRectI *analysisWindow);

    // update image statistics
    void update(const OFX::Image* srcImg, double time, const OfxRectI& analysisWindow);
    void updateHSVL(const OFX::Image* srcImg, double time, const OfxRectI& analysisWindow);

    template <template<class PIX, int nComponents, int maxValue> class Processor, class PIX, int nComponents, int maxValue>
    void updateSubComponentsDepth(const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow, const Results& prevResults, Results* results)
    {
        Processor<PIX, nComponents, maxValue> fred(*this);
        setupAndProcess(fred, srcImg, time, analysisWindow, prevResults, results);
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor, int nComponents>
    void updateSubComponents(const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow, const Results& prevResults, Results* results)
    {
        OFX::BitDepthEnum srcBitDepth = srcImg->getPixelDepth();
        switch (srcBitDepth) {
            case OFX::eBitDepthUByte: {
                updateSubComponentsDepth<Processor, unsigned char, nComponents, 255>(srcImg, time, analysisWindow, prevResults, results);
                break;
            }
            case OFX::eBitDepthUShort: {
                updateSubComponentsDepth<Processor, unsigned short, nComponents, 65535>(srcImg, time, analysisWindow, prevResults, results);
                break;
            }
            case OFX::eBitDepthFloat: {
                updateSubComponentsDepth<Processor, float, nComponents, 1>(srcImg, time, analysisWindow, prevResults, results);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor>
    void updateSub(const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow, const Results& prevResults, Results* results)
    {
        OFX::PixelComponentEnum srcComponents  = srcImg->getPixelComponents();
        assert(srcComponents == OFX::ePixelComponentAlpha ||srcComponents == OFX::ePixelComponentRGB || srcComponents == OFX::ePixelComponentRGBA);
        if (srcComponents == OFX::ePixelComponentAlpha) {
            updateSubComponents<Processor, 1>(srcImg, time, analysisWindow, prevResults, results);
        } else if (srcComponents == OFX::ePixelComponentRGBA) {
            updateSubComponents<Processor, 4>(srcImg, time, analysisWindow, prevResults, results);
        } else if (srcComponents == OFX::ePixelComponentRGB) {
            updateSubComponents<Processor, 3>(srcImg, time, analysisWindow, prevResults, results);
        } else {
            // coverity[dead_error_line]
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }

private:

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;

    Double2DParam* _btmLeft;
    Double2DParam* _size;
    BooleanParam* _interactive;
    BooleanParam* _restrictToRectangle;
    BooleanParam* _autoUpdate;
    RGBAParam* _statMin;
    RGBAParam* _statMax;
    RGBAParam* _statMean;
    RGBAParam* _statSDev;
    RGBAParam* _statSkewness;
    RGBAParam* _statKurtosis;
    PushButtonParam* _analyzeFrame;
    PushButtonParam* _analyzeSequence;
    RGBAParam* _statHSVLMin;
    RGBAParam* _statHSVLMax;
    RGBAParam* _statHSVLMean;
    RGBAParam* _statHSVLSDev;
    RGBAParam* _statHSVLSkewness;
    RGBAParam* _statHSVLKurtosis;
    PushButtonParam* _analyzeFrameHSVL;
    PushButtonParam* _analyzeSequenceHSVL;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


// the overridden render function
void
ImageStatisticsPlugin::render(const OFX::RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    assert(kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // do the rendering
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
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

    if (src.get()) {
        bool autoUpdate;
        _autoUpdate->getValueAtTime(args.time, autoUpdate);
        assert(autoUpdate); // render should only be called if autoUpdate is true: in other cases isIdentity returns true
        if (autoUpdate) {
            // check if there is already a Keyframe, if yes update it
            int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
            OfxRectI analysisWindow;
            computeWindow(src.get(), args.time, &analysisWindow);
            if (k != -1) {
                update(src.get(), args.time, analysisWindow);
            }
            k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
            if (k != -1) {
                updateHSVL(src.get(), args.time, analysisWindow);
            }
        }
    }
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
ImageStatisticsPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    bool restrictToRectangle;
    _restrictToRectangle->getValueAtTime(args.time, restrictToRectangle);
    if (restrictToRectangle) {
        OfxRectD regionOfInterest;
        _btmLeft->getValueAtTime(args.time, regionOfInterest.x1, regionOfInterest.y1);
        _size->getValueAtTime(args.time, regionOfInterest.x2, regionOfInterest.y2);
        regionOfInterest.x2 += regionOfInterest.x1;
        regionOfInterest.y2 += regionOfInterest.y1;
        // Union with output RoD, so that render works
        MergeImages2D::rectBoundingBox(args.regionOfInterest, regionOfInterest, &regionOfInterest);
        rois.setRegionOfInterest(*_srcClip, regionOfInterest);
    }
}

bool
ImageStatisticsPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                             OfxRectD & /*rod*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

bool
ImageStatisticsPlugin::isIdentity(const OFX::IsIdentityArguments &args,
                                  OFX::Clip * &identityClip,
                                  double &/*identityTime*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

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
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    bool doUpdate = false;
    bool doAnalyzeRGBA = false;
    bool doAnalyzeHSVL = false;
    bool doAnalyzeSequenceRGBA = false;
    bool doAnalyzeSequenceHSVL = false;
    OfxRectI analysisWindow;

    if (paramName == kParamRestrictToRectangle) {
        // update visibility
        bool restrictToRectangle;
        _restrictToRectangle->getValue(restrictToRectangle);
        _btmLeft->setEnabled(restrictToRectangle);
        _btmLeft->setIsSecret(!restrictToRectangle);
        _size->setEnabled(restrictToRectangle);
        _size->setIsSecret(!restrictToRectangle);
        _interactive->setEnabled(restrictToRectangle);
        _interactive->setIsSecret(!restrictToRectangle);
        doUpdate = true;
    }
    if (paramName == kParamAutoUpdate) {
        bool restrictToRectangle;
        _restrictToRectangle->getValue(restrictToRectangle);
        _autoUpdate->getValue(doUpdate);
        _interactive->setEnabled(restrictToRectangle && doUpdate);
        _interactive->setIsSecret(!restrictToRectangle || !doUpdate);
    }
    if (//paramName == kParamRectangleInteractBtmLeft ||
        // only trigger on kParamRectangleInteractSize (the last one changed)
        paramName == kParamRectangleInteractSize) {
        _autoUpdate->getValue(doUpdate);
    }
    if (paramName == kParamAnalyzeFrame) {
        doAnalyzeRGBA = true;
    }
    if (paramName == kParamAnalyzeSequence) {
        doAnalyzeSequenceRGBA = true;
    }
    if (paramName == kParamAnalyzeFrameHSVL) {
        doAnalyzeHSVL = true;
    }
    if (paramName == kParamAnalyzeSequenceHSVL) {
        doAnalyzeSequenceHSVL = true;
    }
    if (paramName == kParamClearFrame) {
        _statMin->deleteKeyAtTime(args.time);
        _statMax->deleteKeyAtTime(args.time);
        _statMean->deleteKeyAtTime(args.time);
        _statSDev->deleteKeyAtTime(args.time);
        _statSkewness->deleteKeyAtTime(args.time);
        _statKurtosis->deleteKeyAtTime(args.time);
    }
    if (paramName == kParamClearSequence) {
        _statMin->deleteAllKeys();
        _statMax->deleteAllKeys();
        _statMean->deleteAllKeys();
        _statSDev->deleteAllKeys();
        _statSkewness->deleteAllKeys();
        _statKurtosis->deleteAllKeys();
    }
    if (paramName == kParamClearFrameHSVL) {
        _statHSVLMin->deleteKeyAtTime(args.time);
        _statHSVLMax->deleteKeyAtTime(args.time);
        _statHSVLMean->deleteKeyAtTime(args.time);
        _statHSVLSDev->deleteKeyAtTime(args.time);
        _statHSVLSkewness->deleteKeyAtTime(args.time);
        _statHSVLKurtosis->deleteKeyAtTime(args.time);
    }
    if (paramName == kParamClearSequenceHSVL) {
        _statHSVLMin->deleteAllKeys();
        _statHSVLMax->deleteAllKeys();
        _statHSVLMean->deleteAllKeys();
        _statHSVLSDev->deleteAllKeys();
        _statHSVLSkewness->deleteAllKeys();
        _statHSVLKurtosis->deleteAllKeys();
    }
    if (doUpdate) {
        // check if there is already a Keyframe, if yes update it
        int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeRGBA = (k != -1);
        k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeHSVL = (k != -1);
    }
    // RGBA analysis
    if ((doAnalyzeRGBA || doAnalyzeHSVL) && _srcClip && _srcClip->isConnected()) {
        std::auto_ptr<OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                      _srcClip->fetchImage(args.time) : 0);
        if (src.get()) {
            if (src->getRenderScale().x != args.renderScale.x ||
                src->getRenderScale().y != args.renderScale.y/* ||
                src->getField() != args.fieldToRender*/) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            computeWindow(src.get(), args.time, &analysisWindow);
            getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
            if (doAnalyzeRGBA) {
                update(src.get(), args.time, analysisWindow);
            }
            if (doAnalyzeHSVL) {
                updateHSVL(src.get(), args.time, analysisWindow);
            }
            getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
        }
    }
    if ((doAnalyzeSequenceRGBA || doAnalyzeSequenceHSVL) && _srcClip && _srcClip->isConnected()) {
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
        progressStart("Analyzing sequence...");
        OfxRangeD range = _srcClip->getFrameRange();
        //timeLineGetBounds(range.min, range.max); // wrong: we want the input frame range only
        int tmin = (int)std::ceil(range.min);
        int tmax = (int)std::floor(range.max);
        for (int t = tmin; t <= tmax; ++t) {
            std::auto_ptr<OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                          _srcClip->fetchImage(t) : 0);
            if (src.get()) {
                if (src->getRenderScale().x != args.renderScale.x ||
                    src->getRenderScale().y != args.renderScale.y/* ||
                    src->getField() != args.fieldToRender*/) {
                    setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                }
                computeWindow(src.get(), t, &analysisWindow);
                if (doAnalyzeSequenceRGBA) {
                    update(src.get(), t, analysisWindow);
                }
                if (doAnalyzeSequenceHSVL) {
                    updateHSVL(src.get(), t, analysisWindow);
                }
            }
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
ImageStatisticsPlugin::setupAndProcess(ImageStatisticsProcessorBase &processor, const OFX::Image* srcImg, double /*time*/, const OfxRectI &analysisWindow, const Results &prevResults, Results *results)
{

    // set the images
    processor.setDstImg(const_cast<OFX::Image*>(srcImg)); // not a bug: we only set dst

    // set the render window
    processor.setRenderWindow(analysisWindow);

    processor.setPrevResults(prevResults);

    // Call the base class process member, this will call the derived templated process code
    processor.process();

    if (!abort()) {
        processor.getResults(results);
    }
}

void
ImageStatisticsPlugin::computeWindow(const OFX::Image* srcImg, double time, OfxRectI *analysisWindow)
{
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
                                    srcImg->getRenderScale(),
                                    srcImg->getPixelAspectRatio(),
                                    analysisWindow);
    MergeImages2D::rectIntersection(*analysisWindow, srcImg->getBounds(), analysisWindow);
}
// update image statistics
void
ImageStatisticsPlugin::update(const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow)
{
    // TODO: CHECK if checkDoubleAnalysis param is true and analysisWindow is the same as btmLeft/sizeAnalysis
    Results results;
    if (!abort()) {
        updateSub<ImageMinMaxMeanProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (!abort()) {
        updateSub<ImageSDevProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (!abort()) {
        updateSub<ImageSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (abort()) {
        return;
    }
    beginEditBlock("updateStatisticsRGBA");
    _statMin->setValueAtTime(time, results.min.r, results.min.g, results.min.b, results.min.a);
    _statMax->setValueAtTime(time, results.max.r, results.max.g, results.max.b, results.max.a);
    _statMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
    _statSDev->setValueAtTime(time, results.sdev.r, results.sdev.g, results.sdev.b, results.sdev.a);
    _statSkewness->setValueAtTime(time, results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
   // printf("skewness = %g %g %g %g\n", results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    _statKurtosis->setValueAtTime(time, results.kurtosis.r, results.kurtosis.g, results.kurtosis.b, results.kurtosis.a);
    endEditBlock();
}

void
ImageStatisticsPlugin::updateHSVL(const OFX::Image* srcImg, double time, const OfxRectI &analysisWindow)
{
    Results results;
    if (!abort()) {
        updateSub<ImageHSVLMinMaxMeanProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (!abort()) {
        updateSub<ImageHSVLSDevProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (!abort()) {
        updateSub<ImageHSVLSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if (abort()) {
        return;
    }
    beginEditBlock("updateStatisticsHSVL");
    _statHSVLMin->setValueAtTime(time, results.min.r, results.min.g, results.min.b, results.min.a);
    _statHSVLMax->setValueAtTime(time, results.max.r, results.max.g, results.max.b, results.max.a);
    _statHSVLMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
    _statHSVLSDev->setValueAtTime(time, results.sdev.r, results.sdev.g, results.sdev.b, results.sdev.a);
    _statHSVLSkewness->setValueAtTime(time, results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    _statHSVLKurtosis->setValueAtTime(time, results.kurtosis.r, results.kurtosis.g, results.kurtosis.b, results.kurtosis.a);
    endEditBlock();
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
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
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




void ImageStatisticsPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // restrictToRectangle
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamRestrictToRectangle);
        param->setLabel(kParamRestrictToRectangleLabel);
        param->setHint(kParamRestrictToRectangleHint);
        param->setDefault(true);
        param->setAnimates(false);
        page->addChild(*param);
    }

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(OFX::eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(0., 0.);
        param->setIncrement(1.);
        param->setHint(kParamRectangleInteractBtmLeftHint);
        param->setDigits(0);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabel(kParamRectangleInteractSizeLabel);
        param->setDoubleType(OFX::eDoubleTypeXY);
        param->setDefaultCoordinateSystem(OFX::eCoordinatesNormalised);
        param->setDefault(1., 1.);
        param->setIncrement(1.);
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint(kParamRectangleInteractSizeHint);
        param->setDigits(0);
        param->setEvaluateOnChange(false);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // autoUpdate
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAutoUpdate);
        param->setLabel(kParamAutoUpdateLabel);
        param->setHint(kParamAutoUpdateHint);
        param->setDefault(true);
        param->setAnimates(false);
        page->addChild(*param);
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRectangleInteractInteractive);
        param->setLabel(kParamRectangleInteractInteractiveLabel);
        param->setHint(kParamRectangleInteractInteractiveHint);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupRGBA);
        group->setLabel(kParamGroupRGBA);
        group->setAsTab();
        // min
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMin);
            param->setLabel(kParamStatMinLabel);
            param->setHint(kParamStatMinHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statMax
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMax);
            param->setLabel(kParamStatMaxLabel);
            param->setHint(kParamStatMaxHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statMean
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMean);
            param->setLabel(kParamStatMeanLabel);
            param->setHint(kParamStatMeanHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statSDev
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSDev);
            param->setLabel(kParamStatSDevLabel);
            param->setHint(kParamStatSDevHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statSkewness
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSkewness);
            param->setLabel(kParamStatSkewnessLabel);
            param->setHint(kParamStatSkewnessHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statKurtosis
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatKurtosis);
            param->setLabel(kParamStatKurtosisLabel);
            param->setHint(kParamStatKurtosisHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // analyzeFrame
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrame);
            param->setLabel(kParamAnalyzeFrameLabel);
            param->setHint(kParamAnalyzeFrameHint);
            param->setLayoutHint(eLayoutHintNoNewLine);
            param->setParent(*group);
            page->addChild(*param);
        }

        // analyzeSequence
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequence);
            param->setLabel(kParamAnalyzeSequenceLabel);
            param->setHint(kParamAnalyzeSequenceHint);
            param->setParent(*group);
            page->addChild(*param);
        }

        // clearFrame
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrame);
            param->setLabel(kParamClearFrameLabel);
            param->setHint(kParamClearFrameHint);
            param->setLayoutHint(eLayoutHintNoNewLine);
            param->setParent(*group);
            page->addChild(*param);
        }

        // clearSequence
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequence);
            param->setLabel(kParamClearSequenceLabel);
            param->setHint(kParamClearSequenceHint);
            param->setParent(*group);
            page->addChild(*param);
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupHSVL);
        group->setLabel(kParamGroupHSVL);
        group->setAsTab();
        // min
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMin);
            param->setLabel(kParamStatHSVLMinLabel);
            param->setHint(kParamStatHSVLMinHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statHSVLMax
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMax);
            param->setLabel(kParamStatHSVLMaxLabel);
            param->setHint(kParamStatHSVLMaxHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statHSVLMean
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMean);
            param->setLabel(kParamStatHSVLMeanLabel);
            param->setHint(kParamStatHSVLMeanHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statHSVLSDev
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLSDev);
            param->setLabel(kParamStatHSVLSDevLabel);
            param->setHint(kParamStatHSVLSDevHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statHSVLSkewness
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLSkewness);
            param->setLabel(kParamStatHSVLSkewnessLabel);
            param->setHint(kParamStatHSVLSkewnessHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // statHSVLKurtosis
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLKurtosis);
            param->setLabel(kParamStatHSVLKurtosisLabel);
            param->setHint(kParamStatHSVLKurtosisHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            param->setParent(*group);
            page->addChild(*param);
        }

        // analyzeFrameHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrameHSVL);
            param->setLabel(kParamAnalyzeFrameHSVLLabel);
            param->setHint(kParamAnalyzeFrameHSVLHint);
            param->setLayoutHint(eLayoutHintNoNewLine);
            param->setParent(*group);
            page->addChild(*param);
        }

        // analyzeSequenceHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequenceHSVL);
            param->setLabel(kParamAnalyzeSequenceHSVLLabel);
            param->setHint(kParamAnalyzeSequenceHSVLHint);
            param->setParent(*group);
            page->addChild(*param);
        }

        // clearFrameHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrameHSVL);
            param->setLabel(kParamClearFrameHSVLLabel);
            param->setHint(kParamClearFrameHSVLHint);
            param->setLayoutHint(eLayoutHintNoNewLine);
            param->setParent(*group);
            page->addChild(*param);
        }

        // clearSequenceHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequenceHSVL);
            param->setLabel(kParamClearSequenceHSVLLabel);
            param->setHint(kParamClearSequenceHSVLHint);
            param->setParent(*group);
            page->addChild(*param);
        }
    }
}


void getImageStatisticsPluginID(OFX::PluginFactoryArray &ids)
{
    static ImageStatisticsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

