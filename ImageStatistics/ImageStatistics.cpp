/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * OFX ImageStatistics plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <climits>
#include <algorithm>
#include <limits>

#include "ofxsProcessing.H"
#include "ofxsRectangleInteract.h"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
typedef MultiThread::Mutex Mutex;
typedef MultiThread::AutoMutex AutoMutex;
}
#else
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
namespace {
typedef tthread::fast_mutex Mutex;
typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/gl.h>
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ImageStatisticsOFX"
#define kPluginGrouping "Other"
#define kPluginDescription \
    "Compute image statistics over the whole image or over a rectangle. " \
    "The statistics can be computed either on RGBA components, in the HSVL colorspace " \
    "(which is the HSV colorspace with an additional L component from HSL), or the " \
    "position and value of the pixels with the maximum and minimum luminance values can be computed.\n" \
    "The color values of the minimum and maximum luma pixels for an image sequence " \
    "can be used as black and white point in a Grade node to remove flicker from the same sequence."
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

#define kParamGroupLuma "Min/Max Luma"

#define kParamAnalyzeFrameLuma "analyzeFrameLuma"
#define kParamAnalyzeFrameLumaLabel "Analyze Frame"
#define kParamAnalyzeFrameLumaHint "Analyze current frame and set min/max luma values."

#define kParamAnalyzeSequenceLuma "analyzeSequenceLuma"
#define kParamAnalyzeSequenceLumaLabel "Analyze Sequence"
#define kParamAnalyzeSequenceLumaHint "Analyze all frames from the sequence aand set min/max luma values."

#define kParamClearFrameLuma "clearFrameLuma"
#define kParamClearFrameLumaLabel "Clear Frame"
#define kParamClearFrameLumaHint "Clear luma analysis for current frame."

#define kParamClearSequenceLuma "clearSequenceLuma"
#define kParamClearSequenceLumaLabel "Clear Sequence"
#define kParamClearSequenceLumaHint "Clear luma analysis for all frames from the sequence."

#define kParamLuminanceMath "luminanceMath"
#define kParamLuminanceMathLabel "Luminance Math"
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values."
#define kParamLuminanceMathOptionRec709 "Rec. 709", "Use Rec. 709 (0.2126r + 0.7152g + 0.0722b).", "rec709"
#define kParamLuminanceMathOptionRec2020 "Rec. 2020", "Use Rec. 2020 (0.2627r + 0.6780g + 0.0593b).", "rec2020"
#define kParamLuminanceMathOptionACESAP0 "ACES AP0", "Use ACES AP0 (0.3439664498r + 0.7281660966g + -0.0721325464b).", "acesap0"
#define kParamLuminanceMathOptionACESAP1 "ACES AP1", "Use ACES AP1 (0.2722287168r +  0.6740817658g +  0.0536895174b).", "acesap1"
#define kParamLuminanceMathOptionCcir601 "CCIR 601", "Use CCIR 601 (0.2989r + 0.5866g + 0.1145b).", "ccir601"
#define kParamLuminanceMathOptionAverage "Average", "Use average of r, g, b.", "average"
#define kParamLuminanceMathOptionMaximum "Max", "Use max or r, g, b.", "max"

enum LuminanceMathEnum
{
    eLuminanceMathRec709,
    eLuminanceMathRec2020,
    eLuminanceMathACESAP0,
    eLuminanceMathACESAP1,
    eLuminanceMathCcir601,
    eLuminanceMathAverage,
    eLuminanceMathMaximum,
};

#define kParamMaxLumaPix "maxLumaPix"
#define kParamMaxLumaPixLabel "Max Luma Pixel"
#define kParamMaxLumaPixHint "Position of the pixel with the maximum luma value."
#define kParamMaxLumaPixVal "maxLumaPixVal"
#define kParamMaxLumaPixValLabel "Max Luma Pixel Value"
#define kParamMaxLumaPixValHint "RGB value for the pixel with the maximum luma value."

#define kParamMinLumaPix "minLumaPix"
#define kParamMinLumaPixLabel "Min Luma Pixel"
#define kParamMinLumaPixHint "Position of the pixel with the minimum luma value."
#define kParamMinLumaPixVal "minLumaPixVal"
#define kParamMinLumaPixValLabel "Min Luma Pixel Value"
#define kParamMinLumaPixValHint "RGB value for the pixel with the minimum luma value."

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

#define POINT_TOLERANCE 6
#define POINT_SIZE 5


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

struct Results
{
    Results()
    : min( std::numeric_limits<double>::infinity() )
    , max( -std::numeric_limits<double>::infinity() )
    , mean(0.)
    , sdev( std::numeric_limits<double>::infinity() )
    , skewness( std::numeric_limits<double>::infinity() )
    , kurtosis( std::numeric_limits<double>::infinity() )
    , maxVal( -std::numeric_limits<double>::infinity() )
    , minVal( std::numeric_limits<double>::infinity() )
    {
        maxPos.x = maxPos.y = minPos.x = minPos.y = 0.;
    }

    RGBAValues min;
    RGBAValues max;
    RGBAValues mean;
    RGBAValues sdev;
    RGBAValues skewness;
    RGBAValues kurtosis;
    OfxPointD maxPos; // luma only
    RGBAValues maxVal; // luma only
    OfxPointD minPos; // luma only
    RGBAValues minVal; // luma only
};

class ImageStatisticsProcessorBase
    : public ImageProcessor
{
protected:
    Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    unsigned long _count;

public:
    ImageStatisticsProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _mutex()
        , _count(0)
    {
    }

    virtual ~ImageStatisticsProcessorBase()
    {
    }

    virtual void setPrevResults(double time, const Results &results) = 0;
    virtual void getResults(Results *results) = 0;

protected:

    template<class PIX, int nComponents, int maxValue>
    void toRGBA(const PIX *p,
                RGBAValues* rgba)
    {
        if (nComponents == 4) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = p[2] / (double)maxValue;
            rgba->a = p[3] / (double)maxValue;
        } else if (nComponents == 3) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = p[2] / (double)maxValue;
            rgba->a = 0;
        } else if (nComponents == 2) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = 0;
            rgba->a = 0;
        } else if (nComponents == 1) {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = p[0] / (double)maxValue;
        } else {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = 0;
        }
    }

    template<class PIX, int nComponents, int maxValue>
    void pixToHSVL(const PIX *p,
                   float hsvl[4])
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            float r, g, b;
            r = p[0] / (float)maxValue;
            g = p[1] / (float)maxValue;
            b = p[2] / (float)maxValue;
            Color::rgb_to_hsv(r, g, b, &hsvl[0], &hsvl[1], &hsvl[2]);
            hsvl[0] *= 360 / OFXS_HUE_CIRCLE;
            float min = std::min(std::min(r, g), b);
            float max = std::max(std::max(r, g), b);
            hsvl[3] = (min + max) / 2;
        } else {
            hsvl[0] = hsvl[1] = hsvl[2] = hsvl[3] = 0.f;
        }
    }

    template<class PIX, int nComponents, int maxValue>
    void toComponents(const RGBAValues& rgba,
                      PIX *p)
    {
        if (nComponents == 4) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[2] = rgba.b * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[3] = rgba.a * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 3) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[2] = rgba.b * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 2) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 1) {
            p[0] = rgba.a * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        }
    }
};


template <class PIX, int nComponents, int maxValue>
class ImageMinMaxMeanProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _min[nComponents];
    double _max[nComponents];
    double _sum[nComponents];

public:
    ImageMinMaxMeanProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill( _min, _min + nComponents, +std::numeric_limits<double>::infinity() );
        std::fill( _max, _max + nComponents, -std::numeric_limits<double>::infinity() );
        std::fill(_sum, _sum + nComponents, 0.);
    }

    ~ImageMinMaxMeanProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results & /*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nComponents, 1>(_min, &results->min);
            toRGBA<double, nComponents, 1>(_max, &results->max);
            double mean[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                mean[c] = _sum[c] / _count;
            }
            toRGBA<double, nComponents, 1>(mean, &results->mean);
        }
    }

private:

    void addResults(double min[nComponents],
                    double max[nComponents],
                    double sum[nComponents],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponents; ++c) {
            _min[c] = std::min(_min[c], min[c]);
            _max[c] = std::max(_max[c], max[c]);
            _sum[c] += sum[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double min[nComponents], max[nComponents], sum[nComponents];

        std::fill( min, min + nComponents, +std::numeric_limits<double>::infinity() );
        std::fill( max, max + nComponents, -std::numeric_limits<double>::infinity() );
        std::fill(sum, sum + nComponents, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
class ImageSDevProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponents];
    double _sum_p2[nComponents];

public:
    ImageSDevProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nComponents, 0.);
        std::fill(_sum_p2, _sum_p2 + nComponents, 0.);
    }

    ~ImageSDevProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponents, 1>(results.mean, _mean);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nComponents];
            for (int c = 0; c < nComponents; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt( std::max( 0., _sum_p2[c] / (_count - 1) ) );
            }
            toRGBA<double, nComponents, 1>(sdev, &results->sdev);
        }
    }

private:

    void addResults(double sum_p2[nComponents],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponents; ++c) {
            _sum_p2[c] += sum_p2[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double sum_p2[nComponents];

        std::fill(sum_p2, sum_p2 + nComponents, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
class ImageSkewnessKurtosisProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponents];
    double _sdev[nComponents];
    double _sum_p3[nComponents];
    double _sum_p4[nComponents];

public:
    ImageSkewnessKurtosisProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nComponents, 0.);
        std::fill(_sdev, _sdev + nComponents, 0.);
        std::fill(_sum_p3, _sum_p3 + nComponents, 0.);
        std::fill(_sum_p4, _sum_p4 + nComponents, 0.);
    }

    ~ImageSkewnessKurtosisProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponents, 1>(results.mean, _mean);
        toComponents<double, nComponents, 1>(results.sdev, _sdev);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 2) {
            double skewness[nComponents];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ( (double)_count * _count ) / ( (double)(_count - 1) * (_count - 2) );
            assert( !OFX::IsNaN(skewfac) );
            for (int c = 0; c < nComponents; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / _count;
            }
            toRGBA<double, nComponents, 1>(skewness, &results->skewness);
            assert( !OFX::IsNaN(results->skewness.r) && !OFX::IsNaN(results->skewness.g) && !OFX::IsNaN(results->skewness.b) && !OFX::IsNaN(results->skewness.a) );
        }
        if (_count > 3) {
            double kurtosis[nComponents];
            double kurtfac = ( (double)(_count + 1) * _count ) / ( (double)(_count - 1) * (_count - 2) * (_count - 3) );
            double kurtshift = -3 * ( (double)(_count - 1) * (_count - 1) ) / ( (double)(_count - 2) * (_count - 3) );
            assert( !OFX::IsNaN(kurtfac) && !OFX::IsNaN(kurtshift) );
            for (int c = 0; c < nComponents; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            toRGBA<double, nComponents, 1>(kurtosis, &results->kurtosis);
            assert( !OFX::IsNaN(results->kurtosis.r) && !OFX::IsNaN(results->kurtosis.g) && !OFX::IsNaN(results->kurtosis.b) && !OFX::IsNaN(results->kurtosis.a) );
        }
    }

private:

    void addResults(double sum_p3[nComponents],
                    double sum_p4[nComponents],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponents; ++c) {
            _sum_p3[c] += sum_p3[c];
            _sum_p4[c] += sum_p4[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double sum_p3[nComponents];
        double sum_p4[nComponents];

        std::fill(sum_p3, sum_p3 + nComponents, 0.);
        std::fill(sum_p4, sum_p4 + nComponents, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
                        double v = (*dstPix - _mean[c]) / _sdev[c];
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
class ImageHSVLMinMaxMeanProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _min[nComponentsHSVL];
    double _max[nComponentsHSVL];
    double _sum[nComponentsHSVL];

public:
    ImageHSVLMinMaxMeanProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill( _min, _min + nComponentsHSVL, +std::numeric_limits<double>::infinity() );
        std::fill( _max, _max + nComponentsHSVL, -std::numeric_limits<double>::infinity() );
        std::fill(_sum, _sum + nComponentsHSVL, 0.);
    }

    ~ImageHSVLMinMaxMeanProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results & /*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nComponentsHSVL, 1>(_min, &results->min);
            toRGBA<double, nComponentsHSVL, 1>(_max, &results->max);
            double mean[nComponentsHSVL];
            for (int c = 0; c < nComponentsHSVL; ++c) {
                mean[c] = _sum[c] / _count;
            }
            toRGBA<double, nComponentsHSVL, 1>(mean, &results->mean);
        }
    }

private:

    void addResults(double min[nComponentsHSVL],
                    double max[nComponentsHSVL],
                    double sum[nComponentsHSVL],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _min[c] = std::min(_min[c], min[c]);
            _max[c] = std::max(_max[c], max[c]);
            _sum[c] += sum[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double min[nComponentsHSVL], max[nComponentsHSVL], sum[nComponentsHSVL];

        std::fill( min, min + nComponentsHSVL, +std::numeric_limits<double>::infinity() );
        std::fill( max, max + nComponentsHSVL, -std::numeric_limits<double>::infinity() );
        std::fill(sum, sum + nComponentsHSVL, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
class ImageHSVLSDevProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponentsHSVL];
    double _sum_p2[nComponentsHSVL];

public:
    ImageHSVLSDevProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nComponentsHSVL, 0.);
        std::fill(_sum_p2, _sum_p2 + nComponentsHSVL, 0.);
    }

    ~ImageHSVLSDevProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponentsHSVL, 1>(results.mean, _mean);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nComponentsHSVL];
            for (int c = 0; c < nComponentsHSVL; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt( std::max( 0., _sum_p2[c] / (_count - 1) ) );
            }
            toRGBA<double, nComponentsHSVL, 1>(sdev, &results->sdev);
        }
    }

private:

    void addResults(double sum_p2[nComponentsHSVL],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _sum_p2[c] += sum_p2[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double sum_p2[nComponentsHSVL];

        std::fill(sum_p2, sum_p2 + nComponentsHSVL, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
class ImageHSVLSkewnessKurtosisProcessor
    : public ImageStatisticsProcessorBase
{
private:
    double _mean[nComponentsHSVL];
    double _sdev[nComponentsHSVL];
    double _sum_p3[nComponentsHSVL];
    double _sum_p4[nComponentsHSVL];

public:
    ImageHSVLSkewnessKurtosisProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nComponentsHSVL, 0.);
        std::fill(_sdev, _sdev + nComponentsHSVL, 0.);
        std::fill(_sum_p3, _sum_p3 + nComponentsHSVL, 0.);
        std::fill(_sum_p4, _sum_p4 + nComponentsHSVL, 0.);
    }

    ~ImageHSVLSkewnessKurtosisProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nComponentsHSVL, 1>(results.mean, _mean);
        toComponents<double, nComponentsHSVL, 1>(results.sdev, _sdev);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 2) {
            double skewness[nComponentsHSVL];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ( (double)_count * _count ) / ( (double)(_count - 1) * (_count - 2) );
            for (int c = 0; c < nComponentsHSVL; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / _count;
            }
            toRGBA<double, nComponentsHSVL, 1>(skewness, &results->skewness);
        }
        if (_count > 3) {
            double kurtosis[nComponentsHSVL];
            double kurtfac = ( (double)(_count + 1) * _count ) / ( (double)(_count - 1) * (_count - 2) * (_count - 3) );
            double kurtshift = -3 * ( (double)(_count - 1) * (_count - 1) ) / ( (double)(_count - 2) * (_count - 3) );
            for (int c = 0; c < nComponentsHSVL; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            toRGBA<double, nComponentsHSVL, 1>(kurtosis, &results->kurtosis);
        }
    }

private:

    void addResults(double sum_p3[nComponentsHSVL],
                    double sum_p4[nComponentsHSVL],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponentsHSVL; ++c) {
            _sum_p3[c] += sum_p3[c];
            _sum_p4[c] += sum_p4[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double sum_p3[nComponentsHSVL];
        double sum_p4[nComponentsHSVL];

        std::fill(sum_p3, sum_p3 + nComponentsHSVL, 0.);
        std::fill(sum_p4, sum_p4 + nComponentsHSVL, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
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
                        double v = (hsvl[c] - _mean[c]) / _sdev[c];
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

template <class PIX, int nComponents, int maxValue>
class ImageLumaProcessor
    : public ImageStatisticsProcessorBase
{
private:
    OfxPointD _maxPos;
    double _maxVal[nComponents];
    double _maxLuma;
    OfxPointD _minPos;
    double _minVal[nComponents];
    double _minLuma;
    LuminanceMathEnum _luminanceMath;

public:
    ImageLumaProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
        , _luminanceMath(eLuminanceMathRec709)
    {
        _maxPos.x = _maxPos.y = 0.;
        std::fill( _maxVal, _maxVal + nComponents, -std::numeric_limits<double>::infinity() );
        _maxLuma = -std::numeric_limits<double>::infinity();
        _minPos.x = _minPos.y = 0.;
        std::fill( _minVal, _minVal + nComponents, +std::numeric_limits<double>::infinity() );
        _minLuma = +std::numeric_limits<double>::infinity();
    }

    ImageLumaProcessor()
    {
    }

    void setPrevResults(double time,
                        const Results & /*results*/) OVERRIDE FINAL
    {
        ChoiceParam* luminanceMath = _effect.fetchChoiceParam(kParamLuminanceMath);

        assert(luminanceMath);
        _luminanceMath = (LuminanceMathEnum)luminanceMath->getValueAtTime(time);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        results->maxPos = _maxPos;
        toRGBA<double, nComponents, 1>(_maxVal, &results->maxVal);
        results->minPos = _minPos;
        toRGBA<double, nComponents, 1>(_minVal, &results->minVal);
    }

private:

    double luminance (const PIX *p)
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            float r, g, b;
            r = p[0] / (float)maxValue;
            g = p[1] / (float)maxValue;
            b = p[2] / (float)maxValue;
            switch (_luminanceMath) {
            case eLuminanceMathRec709:
            default:

                return Color::rgb709_to_y(r, g, b);
            case eLuminanceMathRec2020:     // https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf

                return Color::rgb2020_to_y(r, g, b);
            case eLuminanceMathACESAP0:     // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

                return Color::rgbACESAP0_to_y(r, g, b);
            case eLuminanceMathACESAP1:     // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

                return Color::rgbACESAP1_to_y(r, g, b);
            case eLuminanceMathCcir601:

                return 0.2989 * r + 0.5866 * g + 0.1145 * b;
            case eLuminanceMathAverage:

                return (r + g + b) / 3;
            case eLuminanceMathMaximum:

                return std::max(std::max(r, g), b);
            }
        }

        return 0.;
    }

    void addResults(const OfxPointD& maxPos,
                    double maxVal[nComponents],
                    double maxLuma,
                    const OfxPointD& minPos,
                    double minVal[nComponents],
                    double minLuma)
    {
        AutoMutex l (&_mutex);
        if (maxLuma > _maxLuma) {
            _maxPos = maxPos;
            for (int c = 0; c < nComponents; ++c) {
                _maxVal[c] = maxVal[c];
            }
            _maxLuma = maxLuma;
        }
        if (minLuma < _minLuma) {
            _minPos = minPos;
            for (int c = 0; c < nComponents; ++c) {
                _minVal[c] = minVal[c];
            }
            _minLuma = minLuma;
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        OfxPointD maxPos = {0., 0.};
        double maxVal[nComponents] = {0.};
        double maxLuma = -std::numeric_limits<double>::infinity();
        OfxPointD minPos = {0., 0.};
        double minVal[nComponents] = {0.};
        double minLuma = +std::numeric_limits<double>::infinity();

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double luma = luminance(dstPix);

                if (luma > maxLuma) {
                    maxPos.x = x;
                    maxPos.y = y;
                    for (int c = 0; c < nComponents; ++c) {
                        maxVal[c] = dstPix[c] / (double)maxValue;
                    }
                    maxLuma = luma;
                }
                if (luma < minLuma) {
                    minPos.x = x;
                    minPos.y = y;
                    for (int c = 0; c < nComponents; ++c) {
                        minVal[c] = dstPix[c] / (double)maxValue;
                    }
                    minLuma = luma;
                }

                dstPix += nComponents;
            }
        }

        addResults(maxPos, maxVal, maxLuma, minPos, minVal, minLuma);
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ImageStatisticsPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ImageStatisticsPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _btmLeft(NULL)
        , _size(NULL)
        , _interactive(NULL)
        , _restrictToRectangle(NULL)
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
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
        _maxLumaPix = fetchDouble2DParam(kParamMaxLumaPix);
        _maxLumaPixVal = fetchRGBAParam(kParamMaxLumaPixVal);
        _minLumaPix = fetchDouble2DParam(kParamMinLumaPix);
        _minLumaPixVal = fetchRGBAParam(kParamMinLumaPixVal);
        assert(_luminanceMath && _maxLumaPix && _maxLumaPixVal && _minLumaPix && _minLumaPixVal);
        // update visibility
        bool restrictToRectangle = _restrictToRectangle->getValue();
        _btmLeft->setIsSecretAndDisabled(!restrictToRectangle);
        _size->setIsSecretAndDisabled(!restrictToRectangle);
        bool doUpdate = _autoUpdate->getValue();
        _interactive->setIsSecretAndDisabled(!restrictToRectangle || !doUpdate);

        // honor kParamDefaultsNormalised
        if ( paramExists(kParamDefaultsNormalised) ) {
            // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
            // handle these ourselves!
            BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
            assert(param);
            bool normalised = param->getValue();
            if (normalised) {
                OfxPointD size = getProjectExtent();
                OfxPointD origin = getProjectOffset();
                OfxPointD p;
                // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
            }
        }
    }

private:
    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;


    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD & rod) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ImageStatisticsProcessorBase &processor, const Image* srcImg, double time, const OfxRectI &analysisWindow, const Results &prevResults, Results *results);

    // compute computation window in srcImg
    bool computeWindow(const Image* srcImg, double time, OfxRectI *analysisWindow);

    // update image statistics
    void update(const Image* srcImg, double time, const OfxRectI& analysisWindow);
    void updateHSVL(const Image* srcImg, double time, const OfxRectI& analysisWindow);
    void updateLuma(const Image* srcImg, double time, const OfxRectI& analysisWindow);

    template <template<class PIX, int nComponents, int maxValue> class Processor, class PIX, int nComponents, int maxValue>
    void updateSubComponentsDepth(const Image* srcImg,
                                  double time,
                                  const OfxRectI &analysisWindow,
                                  const Results& prevResults,
                                  Results* results)
    {
        Processor<PIX, nComponents, maxValue> fred(*this);
        setupAndProcess(fred, srcImg, time, analysisWindow, prevResults, results);
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor, int nComponents>
    void updateSubComponents(const Image* srcImg,
                             double time,
                             const OfxRectI &analysisWindow,
                             const Results& prevResults,
                             Results* results)
    {
        BitDepthEnum srcBitDepth = srcImg->getPixelDepth();

        switch (srcBitDepth) {
        case eBitDepthUByte: {
            updateSubComponentsDepth<Processor, unsigned char, nComponents, 255>(srcImg, time, analysisWindow, prevResults, results);
            break;
        }
        case eBitDepthUShort: {
            updateSubComponentsDepth<Processor, unsigned short, nComponents, 65535>(srcImg, time, analysisWindow, prevResults, results);
            break;
        }
        case eBitDepthFloat: {
            updateSubComponentsDepth<Processor, float, nComponents, 1>(srcImg, time, analysisWindow, prevResults, results);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor>
    void updateSub(const Image* srcImg,
                   double time,
                   const OfxRectI &analysisWindow,
                   const Results& prevResults,
                   Results* results)
    {
        PixelComponentEnum srcComponents  = srcImg->getPixelComponents();

        assert(srcComponents == ePixelComponentAlpha || srcComponents == ePixelComponentRGB || srcComponents == ePixelComponentRGBA);
        if (srcComponents == ePixelComponentAlpha) {
            updateSubComponents<Processor, 1>(srcImg, time, analysisWindow, prevResults, results);
        } else if (srcComponents == ePixelComponentRGBA) {
            updateSubComponents<Processor, 4>(srcImg, time, analysisWindow, prevResults, results);
        } else if (srcComponents == ePixelComponentRGB) {
            updateSubComponents<Processor, 3>(srcImg, time, analysisWindow, prevResults, results);
        } else {
            // coverity[dead_error_line]
            throwSuiteStatusException(kOfxStatErrUnsupported);
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
    ChoiceParam* _luminanceMath;
    Double2DParam* _maxLumaPix;
    RGBAParam* _maxLumaPixVal;
    Double2DParam* _minLumaPix;
    RGBAParam* _minLumaPixVal;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */


// the overridden render function
void
ImageStatisticsPlugin::render(const RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    copyPixels( *this, args.renderWindow, src.get(), dst.get() );

    if ( src.get() ) {
        bool autoUpdate = _autoUpdate->getValueAtTime(args.time);
        assert(autoUpdate); // render should only be called if autoUpdate is true: in other cases isIdentity returns true
        if (autoUpdate) {
            // check if there is already a Keyframe, if yes update it
            int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
            OfxRectI analysisWindow;
            bool intersect = computeWindow(src.get(), args.time, &analysisWindow);
            if (intersect) {
                if (k != -1) {
                    update(src.get(), args.time, analysisWindow);
                }
                k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
                if (k != -1) {
                    updateHSVL(src.get(), args.time, analysisWindow);
                }
                k = _maxLumaPix->getKeyIndex(args.time, eKeySearchNear);
                if (k != -1) {
                    updateLuma(src.get(), args.time, analysisWindow);
                }
            }
        }
    }
} // ImageStatisticsPlugin::render

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
ImageStatisticsPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                            RegionOfInterestSetter &rois)
{
    bool restrictToRectangle = _restrictToRectangle->getValueAtTime(args.time);

    if (restrictToRectangle) {
        OfxRectD regionOfInterest;
        _btmLeft->getValueAtTime(args.time, regionOfInterest.x1, regionOfInterest.y1);
        _size->getValueAtTime(args.time, regionOfInterest.x2, regionOfInterest.y2);
        regionOfInterest.x2 += regionOfInterest.x1;
        regionOfInterest.y2 += regionOfInterest.y1;
        // Union with output RoD, so that render works
        Coords::rectBoundingBox(args.regionOfInterest, regionOfInterest, &regionOfInterest);
        rois.setRegionOfInterest(*_srcClip, regionOfInterest);
    }
}

bool
ImageStatisticsPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                             OfxRectD & /*rod*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

bool
ImageStatisticsPlugin::isIdentity(const IsIdentityArguments &args,
                                  Clip * &identityClip,
                                  double & /*identityTime*/
                                  , int& /*view*/, std::string& /*plane*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    bool autoUpdate = _autoUpdate->getValueAtTime(time);

    if (!autoUpdate) {
        identityClip = _srcClip;

        return true;
    } else {
        return false;
    }
}

void
ImageStatisticsPlugin::changedParam(const InstanceChangedArgs &args,
                                    const std::string &paramName)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    bool doUpdate = false;
    bool doAnalyzeRGBA = false;
    bool doAnalyzeHSVL = false;
    bool doAnalyzeLuma = false;
    bool doAnalyzeSequenceRGBA = false;
    bool doAnalyzeSequenceHSVL = false;
    bool doAnalyzeSequenceLuma = false;
    OfxRectI analysisWindow;
    const double time = args.time;

    if (paramName == kParamRestrictToRectangle) {
        // update visibility
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);
        _btmLeft->setIsSecretAndDisabled(!restrictToRectangle);
        _size->setIsSecretAndDisabled(!restrictToRectangle);
        _interactive->setIsSecretAndDisabled(!restrictToRectangle);
        doUpdate = true;
    }
    if (paramName == kParamAutoUpdate) {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);
        doUpdate = _autoUpdate->getValueAtTime(time);
        _interactive->setIsSecretAndDisabled(!restrictToRectangle || !doUpdate);
    }
    if (//paramName == kParamRectangleInteractBtmLeft ||
        // only trigger on kParamRectangleInteractSize (the last one changed)
        paramName == kParamRectangleInteractSize) {
        doUpdate = _autoUpdate->getValueAtTime(time);
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
    if (paramName == kParamAnalyzeFrameLuma) {
        doAnalyzeLuma = true;
    }
    if (paramName == kParamAnalyzeSequenceLuma) {
        doAnalyzeSequenceLuma = true;
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
    if (paramName == kParamClearFrameLuma) {
        _maxLumaPix->deleteKeyAtTime(args.time);
        _maxLumaPixVal->deleteKeyAtTime(args.time);
        _minLumaPix->deleteKeyAtTime(args.time);
        _minLumaPixVal->deleteKeyAtTime(args.time);
    }
    if (paramName == kParamClearSequenceLuma) {
        _maxLumaPix->deleteAllKeys();
        _maxLumaPixVal->deleteAllKeys();
        _minLumaPix->deleteAllKeys();
        _minLumaPixVal->deleteAllKeys();
    }
    if (doUpdate) {
        // check if there is already a Keyframe, if yes update it
        int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeRGBA = (k != -1);
        k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeHSVL = (k != -1);
        k = _maxLumaPix->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeLuma = (k != -1);
    }
    // RGBA analysis
    if ( (doAnalyzeRGBA || doAnalyzeHSVL || doAnalyzeLuma) && _srcClip && _srcClip->isConnected() ) {
        auto_ptr<Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                  _srcClip->fetchImage(args.time) : 0 );
        if ( src.get() ) {
            if ( (src->getRenderScale().x != args.renderScale.x) ||
                 ( src->getRenderScale().y != args.renderScale.y) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
            bool intersect = computeWindow(src.get(), args.time, &analysisWindow);
            if (intersect) {
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#             endif
                beginEditBlock("analyzeFrame");
                if (doAnalyzeRGBA) {
                    update(src.get(), args.time, analysisWindow);
                }
                if (doAnalyzeHSVL) {
                    updateHSVL(src.get(), args.time, analysisWindow);
                }
                if (doAnalyzeLuma) {
                    updateLuma(src.get(), args.time, analysisWindow);
                }
                endEditBlock();
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#             endif
            }
        }
    }
    if ( (doAnalyzeSequenceRGBA || doAnalyzeSequenceHSVL || doAnalyzeSequenceLuma) && _srcClip && _srcClip->isConnected() ) {
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#     endif
        progressStart("Analyzing sequence...");
        beginEditBlock("analyzeSequence");
        OfxRangeD range = _srcClip->getFrameRange();
        //timeLineGetBounds(range.min, range.max); // wrong: we want the input frame range only
        int tmin = (int)std::ceil(range.min);
        int tmax = (int)std::floor(range.max);
        for (int t = tmin; t <= tmax; ++t) {
            auto_ptr<Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                      _srcClip->fetchImage(t) : 0 );
            if ( src.get() ) {
                if ( (src->getRenderScale().x != args.renderScale.x) ||
                     ( src->getRenderScale().y != args.renderScale.y) ) {
                    setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    throwSuiteStatusException(kOfxStatFailed);
                }
                bool intersect = computeWindow(src.get(), t, &analysisWindow);
                if (intersect) {
                    if (doAnalyzeSequenceRGBA) {
                        update(src.get(), t, analysisWindow);
                    }
                    if (doAnalyzeSequenceHSVL) {
                        updateHSVL(src.get(), t, analysisWindow);
                    }
                    if (doAnalyzeSequenceLuma) {
                        updateLuma(src.get(), t, analysisWindow);
                    }
                }
            }
            if (tmax != tmin) {
                if ( !progressUpdate( (t - tmin) / (double)(tmax - tmin) ) ) {
                    break;
                }
            }
        }
        progressEnd();
        endEditBlock();
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#     endif
    }
} // ImageStatisticsPlugin::changedParam

/* set up and run a processor */
void
ImageStatisticsPlugin::setupAndProcess(ImageStatisticsProcessorBase &processor,
                                       const Image* srcImg,
                                       double time,
                                       const OfxRectI &analysisWindow,
                                       const Results &prevResults,
                                       Results *results)
{
    // set the images
    processor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

    // set the render window
    processor.setRenderWindow(analysisWindow);

    processor.setPrevResults(time, prevResults);

    // Call the base class process member, this will call the derived templated process code
    processor.process();

    if ( !abort() ) {
        processor.getResults(results);
    }
}

bool
ImageStatisticsPlugin::computeWindow(const Image* srcImg,
                                     double time,
                                     OfxRectI *analysisWindow)
{
    OfxRectD regionOfInterest;
    bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);

    if (!restrictToRectangle && _srcClip) {
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
    Coords::toPixelEnclosing(regionOfInterest,
                             srcImg->getRenderScale(),
                             srcImg->getPixelAspectRatio(),
                             analysisWindow);

    return Coords::rectIntersection(*analysisWindow, srcImg->getBounds(), analysisWindow);
}

// update image statistics
void
ImageStatisticsPlugin::update(const Image* srcImg,
                              double time,
                              const OfxRectI &analysisWindow)
{
    // TODO: CHECK if checkDoubleAnalysis param is true and analysisWindow is the same as btmLeft/sizeAnalysis
    Results results;

    if ( !abort() ) {
        updateSub<ImageMinMaxMeanProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageSDevProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( abort() ) {
        return;
    }
    _statMin->setValueAtTime(time, results.min.r, results.min.g, results.min.b, results.min.a);
    _statMax->setValueAtTime(time, results.max.r, results.max.g, results.max.b, results.max.a);
    _statMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
    _statSDev->setValueAtTime(time, results.sdev.r, results.sdev.g, results.sdev.b, results.sdev.a);
    _statSkewness->setValueAtTime(time, results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    // printf("skewness = %g %g %g %g\n", results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    _statKurtosis->setValueAtTime(time, results.kurtosis.r, results.kurtosis.g, results.kurtosis.b, results.kurtosis.a);
}

void
ImageStatisticsPlugin::updateHSVL(const Image* srcImg,
                                  double time,
                                  const OfxRectI &analysisWindow)
{
    Results results;

    if ( !abort() ) {
        updateSub<ImageHSVLMinMaxMeanProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageHSVLSDevProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageHSVLSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( abort() ) {
        return;
    }
    _statHSVLMin->setValueAtTime(time, results.min.r, results.min.g, results.min.b, results.min.a);
    _statHSVLMax->setValueAtTime(time, results.max.r, results.max.g, results.max.b, results.max.a);
    _statHSVLMean->setValueAtTime(time, results.mean.r, results.mean.g, results.mean.b, results.mean.a);
    _statHSVLSDev->setValueAtTime(time, results.sdev.r, results.sdev.g, results.sdev.b, results.sdev.a);
    _statHSVLSkewness->setValueAtTime(time, results.skewness.r, results.skewness.g, results.skewness.b, results.skewness.a);
    _statHSVLKurtosis->setValueAtTime(time, results.kurtosis.r, results.kurtosis.g, results.kurtosis.b, results.kurtosis.a);
}

void
ImageStatisticsPlugin::updateLuma(const Image* srcImg,
                                  double time,
                                  const OfxRectI &analysisWindow)
{
    Results results;

    if ( !abort() ) {
        updateSub<ImageLumaProcessor>(srcImg, time, analysisWindow, results, &results);
    }
    if ( abort() ) {
        return;
    }
    _maxLumaPix->setValueAtTime(time, results.maxPos.x, results.maxPos.y);
    _maxLumaPixVal->setValueAtTime(time, results.maxVal.r, results.maxVal.g, results.maxVal.b, results.maxVal.a);
    _minLumaPix->setValueAtTime(time, results.minPos.x, results.minPos.y);
    _minLumaPixVal->setValueAtTime(time, results.minVal.r, results.minVal.g, results.minVal.b, results.minVal.a);
}

class ImageStatisticsInteract
    : public RectangleInteract
{
public:

    ImageStatisticsInteract(OfxInteractHandle handle,
                            ImageEffect* effect)
        : RectangleInteract(handle, effect)
        , _restrictToRectangle(NULL)
    {
        _restrictToRectangle = effect->fetchBooleanParam(kParamRestrictToRectangle);
        addParamToSlaveTo(_restrictToRectangle);
    }

private:

    // overridden functions from Interact to do things
    virtual bool draw(const DrawArgs &args) OVERRIDE FINAL
    {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(args.time);

        if (restrictToRectangle) {
            return RectangleInteract::draw(args);
        }

        return false;
    }

    virtual bool penMotion(const PenArgs &args) OVERRIDE FINAL
    {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(args.time);

        if (restrictToRectangle) {
            return RectangleInteract::penMotion(args);
        }

        return false;
    }

    virtual bool penDown(const PenArgs &args) OVERRIDE FINAL
    {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(args.time);

        if (restrictToRectangle) {
            return RectangleInteract::penDown(args);
        }

        return false;
    }

    virtual bool penUp(const PenArgs &args) OVERRIDE FINAL
    {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(args.time);

        if (restrictToRectangle) {
            return RectangleInteract::penUp(args);
        }

        return false;
    }

    //virtual bool keyDown(const KeyArgs &args) OVERRIDE;
    //virtual bool keyUp(const KeyArgs & args) OVERRIDE;
    //virtual void loseFocus(const FocusArgs &args) OVERRIDE FINAL;


    BooleanParam* _restrictToRectangle;
};

class ImageStatisticsOverlayDescriptor
    : public DefaultEffectOverlayDescriptor<ImageStatisticsOverlayDescriptor, ImageStatisticsInteract>
{
};

mDeclarePluginFactory(ImageStatisticsPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
ImageStatisticsPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

ImageEffect*
ImageStatisticsPluginFactory::createInstance(OfxImageEffectHandle handle,
                                             ContextEnum /*context*/)
{
    return new ImageStatisticsPlugin(handle);
}

void
ImageStatisticsPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                                ContextEnum /*context*/)
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
        if (page) {
            page->addChild(*param);
        }
    }

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setHint(kParamRectangleInteractBtmLeftHint);
        param->setDigits(0);
        param->setEvaluateOnChange(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabel(kParamRectangleInteractSizeLabel);
        param->setDoubleType(eDoubleTypeXY);
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(1., 1.);
        param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint(kParamRectangleInteractSizeHint);
        param->setDigits(0);
        param->setEvaluateOnChange(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // autoUpdate
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAutoUpdate);
        param->setLabel(kParamAutoUpdateLabel);
        param->setHint(kParamAutoUpdateHint);
        param->setDefault(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRectangleInteractInteractive);
        param->setLabel(kParamRectangleInteractInteractiveLabel);
        param->setHint(kParamRectangleInteractInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupRGBA);
        if (group) {
            group->setLabel(kParamGroupRGBA);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }
        // min
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMin);
            param->setLabel(kParamStatMinLabel);
            param->setHint(kParamStatMinHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statMax
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMax);
            param->setLabel(kParamStatMaxLabel);
            param->setHint(kParamStatMaxHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statMean
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatMean);
            param->setLabel(kParamStatMeanLabel);
            param->setHint(kParamStatMeanHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statSDev
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSDev);
            param->setLabel(kParamStatSDevLabel);
            param->setHint(kParamStatSDevHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statSkewness
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatSkewness);
            param->setLabel(kParamStatSkewnessLabel);
            param->setHint(kParamStatSkewnessHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statKurtosis
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatKurtosis);
            param->setLabel(kParamStatKurtosisLabel);
            param->setHint(kParamStatKurtosisHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeFrame
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrame);
            param->setLabel(kParamAnalyzeFrameLabel);
            param->setHint(kParamAnalyzeFrameHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeSequence
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequence);
            param->setLabel(kParamAnalyzeSequenceLabel);
            param->setHint(kParamAnalyzeSequenceHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearFrame
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrame);
            param->setLabel(kParamClearFrameLabel);
            param->setHint(kParamClearFrameHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearSequence
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequence);
            param->setLabel(kParamClearSequenceLabel);
            param->setHint(kParamClearSequenceHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupHSVL);
        if (group) {
            group->setLabel(kParamGroupHSVL);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        // min
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMin);
            param->setLabel(kParamStatHSVLMinLabel);
            param->setHint(kParamStatHSVLMinHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statHSVLMax
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMax);
            param->setLabel(kParamStatHSVLMaxLabel);
            param->setHint(kParamStatHSVLMaxHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statHSVLMean
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLMean);
            param->setLabel(kParamStatHSVLMeanLabel);
            param->setHint(kParamStatHSVLMeanHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statHSVLSDev
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLSDev);
            param->setLabel(kParamStatHSVLSDevLabel);
            param->setHint(kParamStatHSVLSDevHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statHSVLSkewness
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLSkewness);
            param->setLabel(kParamStatHSVLSkewnessLabel);
            param->setHint(kParamStatHSVLSkewnessHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // statHSVLKurtosis
        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamStatHSVLKurtosis);
            param->setLabel(kParamStatHSVLKurtosisLabel);
            param->setHint(kParamStatHSVLKurtosisHint);
            param->setDimensionLabels("h", "s", "v", "l");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeFrameHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrameHSVL);
            param->setLabel(kParamAnalyzeFrameHSVLLabel);
            param->setHint(kParamAnalyzeFrameHSVLHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeSequenceHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequenceHSVL);
            param->setLabel(kParamAnalyzeSequenceHSVLLabel);
            param->setHint(kParamAnalyzeSequenceHSVLHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearFrameHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrameHSVL);
            param->setLabel(kParamClearFrameHSVLLabel);
            param->setHint(kParamClearFrameHSVLHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearSequenceHSVL
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequenceHSVL);
            param->setLabel(kParamClearSequenceHSVLLabel);
            param->setHint(kParamClearSequenceHSVLHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }


    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupLuma);
        if (group) {
            group->setLabel(kParamGroupLuma);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLuminanceMath);
            param->setLabel(kParamLuminanceMathLabel);
            param->setHint(kParamLuminanceMathHint);
            assert(param->getNOptions() == eLuminanceMathRec709);
            param->appendOption(kParamLuminanceMathOptionRec709);
            assert(param->getNOptions() == eLuminanceMathRec2020);
            param->appendOption(kParamLuminanceMathOptionRec2020);
            assert(param->getNOptions() == eLuminanceMathACESAP0);
            param->appendOption(kParamLuminanceMathOptionACESAP0);
            assert(param->getNOptions() == eLuminanceMathACESAP1);
            param->appendOption(kParamLuminanceMathOptionACESAP1);
            assert(param->getNOptions() == eLuminanceMathCcir601);
            param->appendOption(kParamLuminanceMathOptionCcir601);
            assert(param->getNOptions() == eLuminanceMathAverage);
            param->appendOption(kParamLuminanceMathOptionAverage);
            assert(param->getNOptions() == eLuminanceMathMaximum);
            param->appendOption(kParamLuminanceMathOptionMaximum);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMaxLumaPix);
            param->setDoubleType(eDoubleTypeXYAbsolute);
            param->setUseHostNativeOverlayHandle(true);
            param->setLabel(kParamMaxLumaPixLabel);
            param->setHint(kParamMaxLumaPixHint);
            param->setDimensionLabels("x", "y");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMaxLumaPixVal);
            param->setLabel(kParamMaxLumaPixValLabel);
            param->setHint(kParamMaxLumaPixValHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamMinLumaPix);
            param->setDoubleType(eDoubleTypeXYAbsolute);
            param->setUseHostNativeOverlayHandle(true);
            param->setLabel(kParamMinLumaPixLabel);
            param->setHint(kParamMinLumaPixHint);
            param->setDimensionLabels("x", "y");
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMinLumaPixVal);
            param->setLabel(kParamMinLumaPixValLabel);
            param->setHint(kParamMinLumaPixValHint);
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeFrameLuma
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrameLuma);
            param->setLabel(kParamAnalyzeFrameLumaLabel);
            param->setHint(kParamAnalyzeFrameLumaHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeSequenceLuma
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequenceLuma);
            param->setLabel(kParamAnalyzeSequenceLumaLabel);
            param->setHint(kParamAnalyzeSequenceLumaHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearFrameLuma
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrameLuma);
            param->setLabel(kParamClearFrameLumaLabel);
            param->setHint(kParamClearFrameLumaHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearSequenceLuma
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequenceLuma);
            param->setLabel(kParamClearSequenceLumaLabel);
            param->setHint(kParamClearSequenceLumaHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }
} // ImageStatisticsPluginFactory::describeInContext

static ImageStatisticsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
