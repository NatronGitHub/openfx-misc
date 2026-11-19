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
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION // Yes, we are still doing OpenGL 2.1
#endif
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    "can be used as black and white point in a Grade node to remove flicker from the same sequence.\n" \
    "The Auto Crop analysis computes the bounding box of the pixels in which the absolute value of one of the " \
    "values tested by Auto Crop Mode is above Auto Crop Threshold: by default, the pixels that are not black " \
    "(R, G or B not zero; alpha is ignored). The box is given as a Bottom Left corner and a Size in canonical " \
    "coordinates, the same parameters as the Crop node: link the Bottom Left and Size parameters of a Crop node to " \
    "Auto Crop Bottom Left and Auto Crop Size (or set expressions on them) to crop the image to that box. " \
    "Analyzing the sequence animates the box.\n" \
    "The Analyze Sequence buttons analyze the frames given by Sequence Range: the frame range of the input " \
    "(the default), the timeline (project) frame range, or a custom range, within the frame range of the input."
#define kPluginIdentifier "net.sf.openfx.ImageStatistics"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 2 // Increment this when you have fixed a bug or made it faster.
// 1.2: the S.Dev. of fewer than 2 pixels, and the skewness and kurtosis of fewer than 3 and 4 pixels,
//      are 0 instead of infinity (an infinite parameter value gives NaN when interpolated, NaN audit)

// With Auto Update, render() writes the statistics params (setValueAtTime), so it must not
// run concurrently on the same instance (on several tiles or frames): render whole images
// only, one render at a time per instance. The output is a copy of the input either way.
#define kSupportsTiles 0
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 0 // no renderscale support: statistics are computed at full resolution
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderInstanceSafe


#define kParamRestrictToRectangle "restrictToRectangle"
#define kParamRestrictToRectangleLabel "Restrict to Rectangle"
#define kParamRestrictToRectangleHint "Restrict statistics computation to a rectangle."

#define kParamAnalyzeFrame "analyzeFrame"
#define kParamAnalyzeFrameLabel "Analyze Frame"
#define kParamAnalyzeFrameHint "Analyze current frame and set values."

#define kParamAnalyzeSequence "analyzeSequence"
#define kParamAnalyzeSequenceLabel "Analyze Sequence"
#define kParamAnalyzeSequenceHint "Analyze all frames from the sequence range (see Sequence Range) and set values."

#define kParamClearFrame "clearFrame"
#define kParamClearFrameLabel "Clear Frame"
#define kParamClearFrameHint "Clear analysis for current frame."

#define kParamClearSequence "clearSequence"
#define kParamClearSequenceLabel "Clear Sequence"
#define kParamClearSequenceHint "Clear analysis for all frames from the sequence: delete all the keys, whatever the Sequence Range."

#define kParamAutoUpdate "autoUpdate"
#define kParamAutoUpdateLabel "Auto Update"
#define kParamAutoUpdateHint "Automatically update values when input or rectangle changes if an analysis was performed at current frame. If not checked, values are only updated if the plugin parameters change. "

#define kParamSequenceRange "sequenceRange"
#define kParamSequenceRangeLabel "Sequence Range"
#define kParamSequenceRangeHint "Frames analyzed by the Analyze Sequence buttons. The Timeline and Custom ranges are intersected with the frame range of the Source input: frames for which the input has no valid data are not analyzed."
#define kParamSequenceRangeOptionInput "Input", "The frame range of the Source input (as in versions before 1.1).", "input"
#define kParamSequenceRangeOptionTimeline "Timeline", "The frame range of the host timeline (e.g. the project frame range), within the input frame range. If the host does not give it, the frame range of the Source input.", "timeline"
#define kParamSequenceRangeOptionCustom "Custom", "From the first to the last frame of Frame Range, within the input frame range.", "custom"

enum SequenceRangeEnum
{
    eSequenceRangeInput = 0,
    eSequenceRangeTimeline,
    eSequenceRangeCustom,
};

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint "First and last frames analyzed by the Analyze Sequence buttons, if Sequence Range is Custom (both included, within the input frame range). Each time Custom is selected while Frame Range is 1-1 (its default), it is set to the input frame range: to analyze frame 1 only, set Frame Range to 1-1 after selecting Custom."

#define kParamInputRange "inputRange"
#define kParamInputRangeLabel "Input Range"
#define kParamInputRangeHint "Set the frame range to the frame range of the Source input (or to the timeline range if Source is not connected)."

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
#define kParamAnalyzeSequenceHSVLHint "Analyze all frames from the sequence range (see Sequence Range) as HSVL and set values."

#define kParamClearFrameHSVL "clearFrameHSVL"
#define kParamClearFrameHSVLLabel "Clear Frame"
#define kParamClearFrameHSVLHint "Clear HSVL analysis for current frame."

#define kParamClearSequenceHSVL "clearSequenceHSVL"
#define kParamClearSequenceHSVLLabel "Clear Sequence"
#define kParamClearSequenceHSVLHint "Clear HSVL analysis for all frames from the sequence: delete all the keys, whatever the Sequence Range."

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
#define kParamAnalyzeSequenceLumaHint "Analyze all frames from the sequence range (see Sequence Range) and set min/max luma values."

#define kParamClearFrameLuma "clearFrameLuma"
#define kParamClearFrameLumaLabel "Clear Frame"
#define kParamClearFrameLumaHint "Clear luma analysis for current frame."

#define kParamClearSequenceLuma "clearSequenceLuma"
#define kParamClearSequenceLumaLabel "Clear Sequence"
#define kParamClearSequenceLumaHint "Clear luma analysis for all frames from the sequence: delete all the keys, whatever the Sequence Range."

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

#define kParamGroupAutoCrop "Auto Crop"

#define kParamAutoCropMode "autoCropMode"
#define kParamAutoCropModeLabel "Auto Crop Mode"
#define kParamAutoCropModeHint "The values that are compared with Auto Crop Threshold: a pixel is in the auto crop box if the absolute value of one of them is strictly greater than the threshold. Integer images are normalized to [0,1]."
#define kParamAutoCropModeOptionRGB "RGB", "Tests R, G and B: the pixels that are not black, whatever their alpha (the default). Alpha images give an empty box.", "rgb"
#define kParamAutoCropModeOptionRGBA "RGBA", "Tests R, G, B and A: the pixels that are not zero. Opaque pixels (alpha not zero) are always in the box, so an opaque image gives the whole analyzed area.", "rgba"
#define kParamAutoCropModeOptionAlpha "Alpha", "Tests A only: the pixels that are not transparent. RGB images, which are opaque, give the whole analyzed area.", "alpha"
#define kParamAutoCropModeOptionLuminance "Luminance", "Tests the luminance of R, G and B, computed with Luminance Math (in the Min/Max Luma tab). Alpha images give an empty box.", "luminance"

enum AutoCropModeEnum
{
    eAutoCropModeRGB = 0,
    eAutoCropModeRGBA,
    eAutoCropModeAlpha,
    eAutoCropModeLuminance,
};

#define kParamAutoCropThreshold "autoCropThreshold"
#define kParamAutoCropThresholdLabel "Auto Crop Threshold"
#define kParamAutoCropThresholdHint "A pixel is in the auto crop box if the absolute value of one of the values given by Auto Crop Mode is strictly greater than this threshold. With 0, the box contains the pixels that are not zero."

#define kParamAutoCropBtmLeft "autoCropBottomLeft"
#define kParamAutoCropBtmLeftLabel "Auto Crop Bottom Left"
#define kParamAutoCropBtmLeftHint "Bottom left corner of the auto crop box, in canonical coordinates. Link the Bottom Left parameter of a Crop node to it (or set an expression on it) to crop to the box. If no pixel is above the threshold, the box is empty (zero size), at the bottom left corner of the analyzed area."

#define kParamAutoCropSize "autoCropSize"
#define kParamAutoCropSizeLabel "Auto Crop Size"
#define kParamAutoCropSizeHint "Width and height of the auto crop box, in canonical coordinates. Link the Size parameter of a Crop node to it (or set an expression on it) to crop to the box."

#define kParamAnalyzeFrameAutoCrop "analyzeFrameAutoCrop"
#define kParamAnalyzeFrameAutoCropLabel "Analyze Frame"
#define kParamAnalyzeFrameAutoCropHint "Analyze current frame and set the auto crop box."

#define kParamAnalyzeSequenceAutoCrop "analyzeSequenceAutoCrop"
#define kParamAnalyzeSequenceAutoCropLabel "Analyze Sequence"
#define kParamAnalyzeSequenceAutoCropHint "Analyze all frames from the sequence range (see Sequence Range) and set the auto crop box."

#define kParamClearFrameAutoCrop "clearFrameAutoCrop"
#define kParamClearFrameAutoCropLabel "Clear Frame"
#define kParamClearFrameAutoCropHint "Clear auto crop analysis for current frame."

#define kParamClearSequenceAutoCrop "clearSequenceAutoCrop"
#define kParamClearSequenceAutoCropLabel "Clear Sequence"
#define kParamClearSequenceAutoCropHint "Clear auto crop analysis for all frames from the sequence: delete all the keys, whatever the Sequence Range."

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
    // The min/max accumulators start at +-DBL_MAX, not at +-infinity, which is undefined behaviour with
    // -ffinite-math-only (-Ofast): the first finite value replaces them, as it replaced infinity.
    Results()
    : min( DBL_MAX )
    , max( -DBL_MAX )
    , mean(0.)
    // undefined for too few pixels: 0, never an infinite value (it would be written to the params, where
    // the host may interpolate it, giving NaN, and the skewness processor would divide by it)
    , sdev(0.)
    , skewness(0.)
    , kurtosis(0.)
    , maxVal( -DBL_MAX )
    , minVal( DBL_MAX )
    {
        maxPos.x = maxPos.y = minPos.x = minPos.y = 0.;
        // empty
        cropBox.x1 = cropBox.y1 = INT_MAX;
        cropBox.x2 = cropBox.y2 = INT_MIN;
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
    OfxRectI cropBox; // auto crop only, in pixel coordinates (empty if x1 >= x2 or y1 >= y2)
};

// The luminance of a pixel, normalized to [0,1].
inline double
luminanceRGB(LuminanceMathEnum luminanceMath,
             double r,
             double g,
             double b)
{
    switch (luminanceMath) {
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

        return (double)(0.2989f * r + 0.5866f * g + 0.1145f * b);
    case eLuminanceMathAverage:

        return (r + g + b) / 3;
    case eLuminanceMathMaximum:

        return (std::max)((std::max)(r, g), b);
    }
}

// Number of values per pixel for the HSVL statistics: hue, saturation, value, luminance.
#define nComponentsHSVL 4

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
            float min = (std::min)((std::min)(r, g), b);
            float max = (std::max)((std::max)(r, g), b);
            hsvl[3] = (min + max) / 2;
        } else {
            hsvl[0] = hsvl[1] = hsvl[2] = hsvl[3] = 0.f;
        }
    }

    // The statistics processors work on a vector of values per pixel: the nComponents
    // components normalized to [0,1] or, if hsvl is true, the nComponentsHSVL values
    // computed by pixToHSVL(). v must have room for that many values.
    template<class PIX, int nComponents, int maxValue, bool hsvl>
    void pixToValues(const PIX *p,
                     double *v)
    {
        if (hsvl) {
            float hsvlPix[nComponentsHSVL];
            pixToHSVL<PIX, nComponents, maxValue>(p, hsvlPix);
            std::copy(hsvlPix, hsvlPix + nComponentsHSVL, v);
        } else {
            for (int c = 0; c < nComponents; ++c) {
                v[c] = p[c] / (double)maxValue; // normalize integer images to [0,1]
            }
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


// The min/max/mean, sdev and skewness/kurtosis processors compute the statistics of
// the RGBA components (hsvl=false) or of the HSVL values (hsvl=true), see pixToValues().
template <class PIX, int nComponents, int maxValue, bool hsvl>
class ImageMinMaxMeanProcessorBase
    : public ImageStatisticsProcessorBase
{
private:
    enum { nValues = hsvl ? nComponentsHSVL : nComponents };
    double _min[nValues];
    double _max[nValues];
    double _sum[nValues];

public:
    ImageMinMaxMeanProcessorBase(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill( _min, _min + nValues, DBL_MAX );
        std::fill( _max, _max + nValues, -DBL_MAX );
        std::fill(_sum, _sum + nValues, 0.);
    }

    void setPrevResults(double /* time */,
                        const Results & /*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nValues, 1>(_min, &results->min);
            toRGBA<double, nValues, 1>(_max, &results->max);
            double mean[nValues];
            for (int c = 0; c < nValues; ++c) {
                mean[c] = _sum[c] / _count;
            }
            toRGBA<double, nValues, 1>(mean, &results->mean);
        }
    }

private:

    void addResults(double min[nValues],
                    double max[nValues],
                    double sum[nValues],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nValues; ++c) {
            _min[c] = (std::min)(_min[c], min[c]);
            _max[c] = (std::max)(_max[c], max[c]);
            _sum[c] += sum[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        double min[nValues], max[nValues], sum[nValues];

        std::fill( min, min + nValues, DBL_MAX );
        std::fill( max, max + nValues, -DBL_MAX );
        std::fill(sum, sum + nValues, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.x2 <= _dstImg->getBounds().x2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            double sumLine[nValues]; // partial sum to avoid underflows
            std::fill(sumLine, sumLine + nValues, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double v[nValues];
                pixToValues<PIX, nComponents, maxValue, hsvl>(dstPix, v);
                for (int c = 0; c < nValues; ++c) {
                    min[c] = (std::min)(min[c], v[c]);
                    max[c] = (std::max)(max[c], v[c]);
                    sumLine[c] += v[c];
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nValues; ++c) {
                sum[c] += sumLine[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(min, max, sum, count);
    }
};


template <class PIX, int nComponents, int maxValue, bool hsvl>
class ImageSDevProcessorBase
    : public ImageStatisticsProcessorBase
{
private:
    enum { nValues = hsvl ? nComponentsHSVL : nComponents };
    double _mean[nValues];
    double _sum_p2[nValues];

public:
    ImageSDevProcessorBase(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nValues, 0.);
        std::fill(_sum_p2, _sum_p2 + nValues, 0.);
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nValues, 1>(results.mean, _mean);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 1) {
            double sdev[nValues];
            for (int c = 0; c < nValues; ++c) {
                // sdev^2 is an unbiased estimator for the population variance
                sdev[c] = std::sqrt( (std::max)( 0., _sum_p2[c] / (_count - 1) ) );
            }
            toRGBA<double, nValues, 1>(sdev, &results->sdev);
        }
    }

private:

    void addResults(double sum_p2[nValues],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nValues; ++c) {
            _sum_p2[c] += sum_p2[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        double sum_p2[nValues];

        std::fill(sum_p2, sum_p2 + nValues, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.x2 <= _dstImg->getBounds().x2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            double sumLine_p2[nValues]; // partial sum to avoid underflows
            std::fill(sumLine_p2, sumLine_p2 + nValues, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double pv[nValues];
                pixToValues<PIX, nComponents, maxValue, hsvl>(dstPix, pv);
                for (int c = 0; c < nValues; ++c) {
                    double v = (pv[c] - _mean[c]);
                    sumLine_p2[c] += v * v;
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nValues; ++c) {
                sum_p2[c] += sumLine_p2[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(sum_p2, count);
    }
};


template <class PIX, int nComponents, int maxValue, bool hsvl>
class ImageSkewnessKurtosisProcessorBase
    : public ImageStatisticsProcessorBase
{
private:
    enum { nValues = hsvl ? nComponentsHSVL : nComponents };
    double _mean[nValues];
    double _sdev[nValues];
    double _sum_p3[nValues];
    double _sum_p4[nValues];

public:
    ImageSkewnessKurtosisProcessorBase(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
    {
        std::fill(_mean, _mean + nValues, 0.);
        std::fill(_sdev, _sdev + nValues, 0.);
        std::fill(_sum_p3, _sum_p3 + nValues, 0.);
        std::fill(_sum_p4, _sum_p4 + nValues, 0.);
    }

    void setPrevResults(double /* time */,
                        const Results &results) OVERRIDE FINAL
    {
        toComponents<double, nValues, 1>(results.mean, _mean);
        toComponents<double, nValues, 1>(results.sdev, _sdev);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        const unsigned long count = _count;
        if (count > 2) {
            double skewness[nValues];
            // factor for the adjusted Fisher-Pearson standardized moment coefficient G_1
            double skewfac = ( (double)count * count ) / ( (double)(count - 1) * (count - 2) );
            assert( !OFX::IsNaN(skewfac) );
            for (int c = 0; c < nValues; ++c) {
                skewness[c] = skewfac * _sum_p3[c] / count;
            }
            // NaN for NaN pixels (the sums come from the pixels): no assert on it
            toRGBA<double, nValues, 1>(skewness, &results->skewness);
        }
        if (count > 3) {
            double kurtosis[nValues];
            double kurtfac = ( (double)(count + 1) * count ) / ( (double)(count - 1) * (count - 2) * (count - 3) );
            double kurtshift = -3 * ( (double)(count - 1) * (count - 1) ) / ( (double)(count - 2) * (count - 3) );
            assert( !OFX::IsNaN(kurtfac) && !OFX::IsNaN(kurtshift) );
            for (int c = 0; c < nValues; ++c) {
                kurtosis[c] = kurtfac * _sum_p4[c] + kurtshift;
            }
            // NaN for NaN pixels (the sums come from the pixels): no assert on it
            toRGBA<double, nValues, 1>(kurtosis, &results->kurtosis);
        }
    }

private:

    void addResults(double sum_p3[nValues],
                    double sum_p4[nValues],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nValues; ++c) {
            _sum_p3[c] += sum_p3[c];
            _sum_p4[c] += sum_p4[c];
        }
        _count += count;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        double sum_p3[nValues];
        double sum_p4[nValues];

        std::fill(sum_p3, sum_p3 + nValues, 0.);
        std::fill(sum_p4, sum_p4 + nValues, 0.);
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.x2 <= _dstImg->getBounds().x2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            double sumLine_p3[nValues]; // partial sum to avoid underflows
            double sumLine_p4[nValues]; // partial sum to avoid underflows
            std::fill(sumLine_p3, sumLine_p3 + nValues, 0.);
            std::fill(sumLine_p4, sumLine_p4 + nValues, 0.);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                double pv[nValues];
                pixToValues<PIX, nComponents, maxValue, hsvl>(dstPix, pv);
                for (int c = 0; c < nValues; ++c) {
                    if (_sdev[c] > 0.) {
                        double v = (pv[c] - _mean[c]) / _sdev[c];
                        double v2 = v * v;
                        sumLine_p3[c] += v2 * v;
                        sumLine_p4[c] += v2 * v2;
                    }
                }
                dstPix += nComponents;
            }
            for (int c = 0; c < nValues; ++c) {
                sum_p3[c] += sumLine_p3[c];
                sum_p4[c] += sumLine_p4[c];
            }
            count += procWindow.x2 - procWindow.x1;
        }

        addResults(sum_p3, sum_p4, count);
    }
};

// updateSub() takes a processor template with <PIX, nComponents, maxValue> parameters,
// so each RGBA (hsvl=false) and HSVL (hsvl=true) processor gets its own name.
template <class PIX, int nComponents, int maxValue>
class ImageMinMaxMeanProcessor
    : public ImageMinMaxMeanProcessorBase<PIX, nComponents, maxValue, false>
{
public:
    ImageMinMaxMeanProcessor(ImageEffect &instance) : ImageMinMaxMeanProcessorBase<PIX, nComponents, maxValue, false>(instance) {}
};

template <class PIX, int nComponents, int maxValue>
class ImageSDevProcessor
    : public ImageSDevProcessorBase<PIX, nComponents, maxValue, false>
{
public:
    ImageSDevProcessor(ImageEffect &instance) : ImageSDevProcessorBase<PIX, nComponents, maxValue, false>(instance) {}
};

template <class PIX, int nComponents, int maxValue>
class ImageSkewnessKurtosisProcessor
    : public ImageSkewnessKurtosisProcessorBase<PIX, nComponents, maxValue, false>
{
public:
    ImageSkewnessKurtosisProcessor(ImageEffect &instance) : ImageSkewnessKurtosisProcessorBase<PIX, nComponents, maxValue, false>(instance) {}
};

template <class PIX, int nComponents, int maxValue>
class ImageHSVLMinMaxMeanProcessor
    : public ImageMinMaxMeanProcessorBase<PIX, nComponents, maxValue, true>
{
public:
    ImageHSVLMinMaxMeanProcessor(ImageEffect &instance) : ImageMinMaxMeanProcessorBase<PIX, nComponents, maxValue, true>(instance) {}
};

template <class PIX, int nComponents, int maxValue>
class ImageHSVLSDevProcessor
    : public ImageSDevProcessorBase<PIX, nComponents, maxValue, true>
{
public:
    ImageHSVLSDevProcessor(ImageEffect &instance) : ImageSDevProcessorBase<PIX, nComponents, maxValue, true>(instance) {}
};

template <class PIX, int nComponents, int maxValue>
class ImageHSVLSkewnessKurtosisProcessor
    : public ImageSkewnessKurtosisProcessorBase<PIX, nComponents, maxValue, true>
{
public:
    ImageHSVLSkewnessKurtosisProcessor(ImageEffect &instance) : ImageSkewnessKurtosisProcessorBase<PIX, nComponents, maxValue, true>(instance) {}
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
        std::fill( _maxVal, _maxVal + nComponents, -DBL_MAX );
        _maxLuma = -DBL_MAX;
        _minPos.x = _minPos.y = 0.;
        std::fill( _minVal, _minVal + nComponents, DBL_MAX );
        _minLuma = DBL_MAX;
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

    double
    luminance(const PIX *p)
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            return luminanceRGB(_luminanceMath, p[0] / (double)maxValue, p[1] / (double)maxValue, p[2] / (double)maxValue);
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

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        OfxPointD maxPos = {0., 0.};
        double maxVal[nComponents] = {0.};
        double maxLuma = -DBL_MAX;
        OfxPointD minPos = {0., 0.};
        double minVal[nComponents] = {0.};
        double minLuma = DBL_MAX;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.x2 <= _dstImg->getBounds().x2 &&
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

// The auto crop processor computes the bounding box, in pixel coordinates, of the pixels
// with a value (given by kParamAutoCropMode) whose absolute value is above kParamAutoCropThreshold.
template <class PIX, int nComponents, int maxValue>
class ImageAutoCropProcessor
    : public ImageStatisticsProcessorBase
{
private:
    OfxRectI _box; // empty if x1 >= x2
    AutoCropModeEnum _mode;
    double _threshold;
    LuminanceMathEnum _luminanceMath;

public:
    ImageAutoCropProcessor(ImageEffect &instance)
        : ImageStatisticsProcessorBase(instance)
        , _mode(eAutoCropModeRGB)
        , _threshold(0.)
        , _luminanceMath(eLuminanceMathRec709)
    {
        _box.x1 = _box.y1 = INT_MAX;
        _box.x2 = _box.y2 = INT_MIN;
    }

    void setPrevResults(double time,
                        const Results & /*results*/) OVERRIDE FINAL
    {
        ChoiceParam* mode = _effect.fetchChoiceParam(kParamAutoCropMode);
        DoubleParam* threshold = _effect.fetchDoubleParam(kParamAutoCropThreshold);
        ChoiceParam* luminanceMath = _effect.fetchChoiceParam(kParamLuminanceMath);

        assert(mode && threshold && luminanceMath);
        _mode = (AutoCropModeEnum)mode->getValueAtTime(time);
        _threshold = threshold->getValueAtTime(time);
        _luminanceMath = (LuminanceMathEnum)luminanceMath->getValueAtTime(time);
    }

    void getResults(Results *results) OVERRIDE FINAL
    {
        results->cropBox = _box;
    }

private:

    bool isAbove(double v) const
    {
        return std::abs(v) > _threshold;
    }

    // is the pixel in the box?
    bool isIn(const PIX *p) const
    {
        switch (_mode) {
        case eAutoCropModeRGBA:
            for (int c = 0; c < nComponents; ++c) {
                if ( isAbove(p[c] / (double)maxValue) ) {
                    return true;
                }
            }

            return false;
        case eAutoCropModeRGB:
            if ( (nComponents == 3) || (nComponents == 4) ) {
                for (int c = 0; c < 3; ++c) {
                    if ( isAbove(p[c] / (double)maxValue) ) {
                        return true;
                    }
                }
            }

            return false; // alpha images have no RGB
        case eAutoCropModeAlpha:
            if (nComponents == 4) {
                return isAbove(p[3] / (double)maxValue);
            } else if (nComponents == 1) {
                return isAbove(p[0] / (double)maxValue);
            }

            return isAbove(1.); // RGB images are opaque
        case eAutoCropModeLuminance:
            if ( (nComponents == 3) || (nComponents == 4) ) {
                return isAbove( luminanceRGB(_luminanceMath, p[0] / (double)maxValue, p[1] / (double)maxValue, p[2] / (double)maxValue) );
            }

            return false; // alpha images have no luminance
        }

        return false;
    }

    void addResults(const OfxRectI& box)
    {
        if ( (box.x1 >= box.x2) || (box.y1 >= box.y2) ) {
            return;
        }
        AutoMutex l (&_mutex);
        _box.x1 = (std::min)(_box.x1, box.x1);
        _box.x2 = (std::max)(_box.x2, box.x2);
        _box.y1 = (std::min)(_box.y1, box.y1);
        _box.y2 = (std::max)(_box.y2, box.y2);
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        OfxRectI box;
        box.x1 = box.y1 = INT_MAX;
        box.x2 = box.y2 = INT_MIN;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.x2 <= _dstImg->getBounds().x2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            const PIX *rowPix = (const PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            // the first pixel in the box, from the left
            int x1 = procWindow.x1;
            while ( x1 < procWindow.x2 && !isIn(rowPix + (x1 - procWindow.x1) * nComponents) ) {
                ++x1;
            }
            if (x1 == procWindow.x2) {
                continue; // no pixel in the box on this row
            }
            // the last pixel in the box, from the right (x1 is in the box)
            int x2 = procWindow.x2;
            while ( x2 - 1 > x1 && !isIn(rowPix + (x2 - 1 - procWindow.x1) * nComponents) ) {
                --x2;
            }
            box.x1 = (std::min)(box.x1, x1);
            box.x2 = (std::max)(box.x2, x2);
            box.y1 = (std::min)(box.y1, y);
            box.y2 = (std::max)(box.y2, y + 1);
        }

        addResults(box);
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
        , _hiDPI(NULL)
        , _restrictToRectangle(NULL)
        , _autoUpdate(NULL)
        , _sequenceRange(NULL)
        , _frameRange(NULL)
        , _inputRange(NULL)
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
        if ( paramExists(kParamHiDPI) ) {
            _hiDPI = fetchBooleanParam(kParamHiDPI);
            assert(_hiDPI);
        }
        _restrictToRectangle = fetchBooleanParam(kParamRestrictToRectangle);
        _autoUpdate = fetchBooleanParam(kParamAutoUpdate);
        assert(_btmLeft && _size && _interactive && _restrictToRectangle && _autoUpdate);
        _sequenceRange = fetchChoiceParam(kParamSequenceRange);
        _frameRange = fetchInt2DParam(kParamFrameRange);
        _inputRange = fetchPushButtonParam(kParamInputRange);
        assert(_sequenceRange && _frameRange && _inputRange);
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
        _autoCropBtmLeft = fetchDouble2DParam(kParamAutoCropBtmLeft);
        _autoCropSize = fetchDouble2DParam(kParamAutoCropSize);
        assert(_autoCropBtmLeft && _autoCropSize);

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
                //EditBlock eb(*this, kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
            }
        }

        // finally
        syncPrivateData();
    }

private:
    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;


    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD & rod) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        // update visibility
        bool restrictToRectangle = _restrictToRectangle->getValue();
        _btmLeft->setIsSecretAndDisabled(!restrictToRectangle);
        _size->setIsSecretAndDisabled(!restrictToRectangle);
        bool doUpdate = _autoUpdate->getValue();
        _interactive->setIsSecretAndDisabled(!restrictToRectangle || !doUpdate);
        if (_hiDPI) {
            _hiDPI->setIsSecretAndDisabled(!restrictToRectangle);
        }
        updateSequenceRangeVisibility();
    }

    void updateSequenceRangeVisibility()
    {
        bool custom = ( (SequenceRangeEnum)_sequenceRange->getValue() == eSequenceRangeCustom );
        _frameRange->setIsSecretAndDisabled(!custom);
        _inputRange->setIsSecretAndDisabled(!custom);
    }

    // the frames analyzed by the Analyze Sequence buttons (first and last included)
    void getSequenceRange(int *first, int *last);

    // set kParamFrameRange to the input range (or the timeline range if Source is not connected)
    void setFrameRangeToInputRange();

    /* set up and run a processor */
    void setupAndProcess(ImageStatisticsProcessorBase &processor, const Image* srcImg, double time, const OfxRectI &analysisWindow, const OfxPointD& renderScale, const Results &prevResults, Results *results);

    // compute the analyzed rectangle, in canonical coordinates
    void getAnalysisRectangle(double time, OfxRectD *rect);

    // compute the canonical region to fetch from the source for analysis
    bool computeFetchRect(double time, OfxRectD *fetchRect);

    // compute computation window in srcImg
    bool computeWindow(const Image* srcImg, const OfxPointD& renderScale, double time, OfxRectI *analysisWindow);

    // update image statistics
    void update(const Image* srcImg, double time, const OfxRectI& analysisWindow, const OfxPointD& renderScale);
    void updateHSVL(const Image* srcImg, double time, const OfxRectI& analysisWindow, const OfxPointD& renderScale);
    void updateLuma(const Image* srcImg, double time, const OfxRectI& analysisWindow, const OfxPointD& renderScale);
    void updateAutoCrop(const Image* srcImg, double time, const OfxRectI& analysisWindow, const OfxPointD& renderScale);

    template <template<class PIX, int nComponents, int maxValue> class Processor, class PIX, int nComponents, int maxValue>
    void updateSubComponentsDepth(const Image* srcImg,
                                  double time,
                                  const OfxRectI &analysisWindow,
                                  const OfxPointD& renderScale,
                                  const Results& prevResults,
                                  Results* results)
    {
        Processor<PIX, nComponents, maxValue> fred(*this);
        setupAndProcess(fred, srcImg, time, analysisWindow, renderScale, prevResults, results);
    }

    template <template<class PIX, int nComponents, int maxValue> class Processor, int nComponents>
    void updateSubComponents(const Image* srcImg,
                             double time,
                             const OfxRectI &analysisWindow,
                             const OfxPointD& renderScale,
                             const Results& prevResults,
                             Results* results)
    {
        BitDepthEnum srcBitDepth = srcImg->getPixelDepth();

        switch (srcBitDepth) {
        case eBitDepthUByte: {
            updateSubComponentsDepth<Processor, unsigned char, nComponents, 255>(srcImg, time, analysisWindow, renderScale, prevResults, results);
            break;
        }
        case eBitDepthUShort: {
            updateSubComponentsDepth<Processor, unsigned short, nComponents, 65535>(srcImg, time, analysisWindow, renderScale, prevResults, results);
            break;
        }
        case eBitDepthFloat: {
            updateSubComponentsDepth<Processor, float, nComponents, 1>(srcImg, time, analysisWindow, renderScale, prevResults, results);
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
                   const OfxPointD& renderScale,
                   const Results& prevResults,
                   Results* results)
    {
        PixelComponentEnum srcComponents  = srcImg->getPixelComponents();

        assert(srcComponents == ePixelComponentAlpha || srcComponents == ePixelComponentRGB || srcComponents == ePixelComponentRGBA);
        if (srcComponents == ePixelComponentAlpha) {
            updateSubComponents<Processor, 1>(srcImg, time, analysisWindow, renderScale, prevResults, results);
        } else if (srcComponents == ePixelComponentRGBA) {
            updateSubComponents<Processor, 4>(srcImg, time, analysisWindow, renderScale, prevResults, results);
        } else if (srcComponents == ePixelComponentRGB) {
            updateSubComponents<Processor, 3>(srcImg, time, analysisWindow, renderScale, prevResults, results);
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
    BooleanParam* _hiDPI;
    BooleanParam* _restrictToRectangle;
    BooleanParam* _autoUpdate;
    ChoiceParam* _sequenceRange;
    Int2DParam* _frameRange;
    PushButtonParam* _inputRange;
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
    Double2DParam* _autoCropBtmLeft;
    Double2DParam* _autoCropSize;
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

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
# endif

    copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );

    if ( src.get() ) {
        bool autoUpdate = _autoUpdate->getValueAtTime(args.time);
        assert(autoUpdate); // render should only be called if autoUpdate is true: in other cases isIdentity returns true
        if (autoUpdate) {
            // check if there is already a Keyframe, if yes update it
            int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
            OfxRectI analysisWindow;
            bool intersect = computeWindow(src.get(), args.renderScale, args.time, &analysisWindow);
            if (intersect) {
                if (k != -1) {
                    update(src.get(), args.time, analysisWindow, args.renderScale);
                }
                k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
                if (k != -1) {
                    updateHSVL(src.get(), args.time, analysisWindow, args.renderScale);
                }
                k = _maxLumaPix->getKeyIndex(args.time, eKeySearchNear);
                if (k != -1) {
                    updateLuma(src.get(), args.time, analysisWindow, args.renderScale);
                }
                k = _autoCropSize->getKeyIndex(args.time, eKeySearchNear);
                if (k != -1) {
                    updateAutoCrop(src.get(), args.time, analysisWindow, args.renderScale);
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

    // render analyzes the whole rectangle (see computeWindow()), so it must be fetched entirely,
    // else only the current tile would be analyzed. Without a rectangle, this is the whole source
    // RoD, which is only analyzed when Auto Update is checked.
    if ( restrictToRectangle ||
         ( _autoUpdate->getValueAtTime(args.time) && _srcClip && _srcClip->isConnected() ) ) {
        OfxRectD regionOfInterest;
        getAnalysisRectangle(args.time, &regionOfInterest);
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
    bool doAnalyzeAutoCrop = false;
    bool doAnalyzeSequenceAutoCrop = false;
    OfxRectI analysisWindow;
    const double time = args.time;
    // ofxImageClip.rst: "Images may be fetched from an attached clip [...] in the
    // kOfxActionInstanceChanged and kOfxActionEndInstanceChanged actions with a
    // kOfxPropChangeReason of kOfxChangeUserEdited": analyze only on user edits, except for the
    // Auto Update of the rectangle (see below).
    const bool userEdit = (args.reason == eChangeUserEdit);

    if (paramName == kParamRestrictToRectangle) {
        // update visibility
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);
        _btmLeft->setIsSecretAndDisabled(!restrictToRectangle);
        _size->setIsSecretAndDisabled(!restrictToRectangle);
        _interactive->setIsSecretAndDisabled(!restrictToRectangle);
        if (_hiDPI) {
            _hiDPI->setIsSecretAndDisabled(!restrictToRectangle);
        }
        doUpdate = userEdit;
    }
    if (paramName == kParamAutoUpdate) {
        bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);
        doUpdate = _autoUpdate->getValueAtTime(time);
        _interactive->setIsSecretAndDisabled(!restrictToRectangle || !doUpdate);
        doUpdate = doUpdate && userEdit;
    }
    if (//paramName == kParamRectangleInteractBtmLeft ||
        // only trigger on kParamRectangleInteractSize (the last one changed)
        paramName == kParamRectangleInteractSize) {
        // Not restricted to user edits: the rectangle interact (plugin code) changes the
        // rectangle with kOfxChangePluginEdited, and btmLeft/size do not trigger a render
        // (setEvaluateOnChange(false)), so restricting this would stop Auto Update while
        // dragging the rectangle, as in version 1.0. Other reasons (time changes) are ignored.
        doUpdate = _autoUpdate->getValueAtTime(time) &&
                   (args.reason == eChangeUserEdit || args.reason == eChangePluginEdit);
    }
    if ( userEdit && ( (paramName == kParamAutoCropMode) || (paramName == kParamAutoCropThreshold) ) &&
         _autoUpdate->getValueAtTime(time) ) {
        // re-analyze the auto crop box if it has a key at this frame
        doAnalyzeAutoCrop = ( _autoCropSize->getKeyIndex(time, eKeySearchNear) != -1 );
    }
    if (userEdit) {
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
        if (paramName == kParamAnalyzeFrameAutoCrop) {
            doAnalyzeAutoCrop = true;
        }
        if (paramName == kParamAnalyzeSequenceAutoCrop) {
            doAnalyzeSequenceAutoCrop = true;
        }
    }
    if (paramName == kParamSequenceRange) {
        updateSequenceRangeVisibility();
        if ( userEdit && ( (SequenceRangeEnum)_sequenceRange->getValue() == eSequenceRangeCustom ) ) {
            // the first time Custom is selected, start from the input range
            int f1, f2;
            _frameRange->getValue(f1, f2);
            if ( (f1 == 1) && (f2 == 1) ) {
                setFrameRangeToInputRange();
            }
        }
    }
    if ( (paramName == kParamInputRange) && userEdit ) {
        setFrameRangeToInputRange();
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
    if (paramName == kParamClearFrameAutoCrop) {
        _autoCropBtmLeft->deleteKeyAtTime(args.time);
        _autoCropSize->deleteKeyAtTime(args.time);
    }
    if (paramName == kParamClearSequenceAutoCrop) {
        _autoCropBtmLeft->deleteAllKeys();
        _autoCropSize->deleteAllKeys();
    }
    if (doUpdate) {
        // check if there is already a Keyframe, if yes update it
        int k = _statMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeRGBA = (k != -1);
        k = _statHSVLMean->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeHSVL = (k != -1);
        k = _maxLumaPix->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeLuma = (k != -1);
        k = _autoCropSize->getKeyIndex(args.time, eKeySearchNear);
        doAnalyzeAutoCrop = (k != -1);
    }
    // RGBA analysis
    if ( (doAnalyzeRGBA || doAnalyzeHSVL || doAnalyzeLuma || doAnalyzeAutoCrop) && _srcClip && _srcClip->isConnected() ) {
        // only fetch the analyzed region
        OfxRectD fetchRect;
        bool fetch = computeFetchRect(args.time, &fetchRect);
        auto_ptr<Image> src( fetch ? _srcClip->fetchImage(args.time, fetchRect) : 0 );
        if ( src.get() ) {
            checkBadRenderScale(src, args);
            bool intersect = computeWindow(src.get(), args.renderScale, args.time, &analysisWindow);
            if (intersect) {
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#             endif
                EditBlock eb(*this, "analyzeFrame");
                if (doAnalyzeRGBA) {
                    update(src.get(), args.time, analysisWindow, args.renderScale);
                }
                if (doAnalyzeHSVL) {
                    updateHSVL(src.get(), args.time, analysisWindow, args.renderScale);
                }
                if (doAnalyzeLuma) {
                    updateLuma(src.get(), args.time, analysisWindow, args.renderScale);
                }
                if (doAnalyzeAutoCrop) {
                    updateAutoCrop(src.get(), args.time, analysisWindow, args.renderScale);
                }
#             ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
                getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#             endif
            }
        }
    }
    if ( (doAnalyzeSequenceRGBA || doAnalyzeSequenceHSVL || doAnalyzeSequenceLuma || doAnalyzeSequenceAutoCrop) && _srcClip && _srcClip->isConnected() ) {
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
#     endif
        progressStart("Analyzing sequence...");
        EditBlock eb(*this, "analyzeSequence");
        int tmin, tmax;
        getSequenceRange(&tmin, &tmax);
        // t is wider than int, so that ++t cannot overflow if tmax is INT_MAX
        for (long long t = tmin; t <= tmax; ++t) {
            // only fetch the analyzed region
            OfxRectD fetchRect;
            bool fetch = computeFetchRect(t, &fetchRect);
            auto_ptr<Image> src( fetch ? _srcClip->fetchImage(t, fetchRect) : 0 );
            if ( src.get() ) {
                checkBadRenderScale(src, args);
                bool intersect = computeWindow(src.get(), args.renderScale, t, &analysisWindow);
                if (intersect) {
                    if (doAnalyzeSequenceRGBA) {
                        update(src.get(), t, analysisWindow, args.renderScale);
                    }
                    if (doAnalyzeSequenceHSVL) {
                        updateHSVL(src.get(), t, analysisWindow, args.renderScale);
                    }
                    if (doAnalyzeSequenceLuma) {
                        updateLuma(src.get(), t, analysisWindow, args.renderScale);
                    }
                    if (doAnalyzeSequenceAutoCrop) {
                        updateAutoCrop(src.get(), t, analysisWindow, args.renderScale);
                    }
                }
            }
            if (tmax != tmin) {
                if ( !progressUpdate( ( (double)t - tmin ) / ( (double)tmax - tmin ) ) ) {
                    break;
                }
            }
        }
        progressEnd();
#     ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
#     endif
    }
} // ImageStatisticsPlugin::changedParam

void
ImageStatisticsPlugin::getSequenceRange(int *first,
                                        int *last)
{
    assert(_srcClip);
    // the frame range of the Source input (kOfxImageEffectPropFrameRange: "the first frame
    // [and] the last frame for which the clip can produce valid data")
    OfxRangeD range = _srcClip->getFrameRange();
    SequenceRangeEnum sequenceRange = (SequenceRangeEnum)_sequenceRange->getValue();

    switch (sequenceRange) {
    case eSequenceRangeInput:
        break;
    case eSequenceRangeTimeline:
        // OfxTimeLineSuiteV1::getTimeBounds gives the first and last times on the timeline,
        // "in the temporal coordinate system of the effect". If the host has no timeline suite
        // (or if the call fails: the support library then leaves the values unchanged), keep
        // the input range.
        if (getImageEffectHostDescription()->supportsTimeLineSuite) {
            double t1 = range.min, t2 = range.max;
            timeLineGetBounds(t1, t2);
            if (t1 <= t2) {
                range.min = t1;
                range.max = t2;
            }
        }
        break;
    case eSequenceRangeCustom: {
        int f1, f2;
        _frameRange->getValue(f1, f2);
        range.min = (std::min)(f1, f2);
        range.max = (std::max)(f1, f2);
        break;
    }
    }
    if (sequenceRange != eSequenceRangeInput) {
        // only analyze frames for which the input "can produce valid data"
        // (kOfxImageEffectPropFrameRange)
        OfxRangeD input = _srcClip->getFrameRange();
        range.min = (std::max)(range.min, input.min);
        range.max = (std::min)(range.max, input.max);
    }
    // clamp before converting to int (an empty range gives first > last)
    *first = (int)(std::max)( (double)INT_MIN, (std::min)( (double)INT_MAX, std::ceil(range.min) ) );
    *last = (int)(std::max)( (double)INT_MIN, (std::min)( (double)INT_MAX, std::floor(range.max) ) );
}

void
ImageStatisticsPlugin::setFrameRangeToInputRange()
{
    // as the Input Range button of FrameBlend
    OfxRangeD range;
    range.min = range.max = 1.; // if getTimeBounds fails or there is no timeline suite
    if ( _srcClip && _srcClip->isConnected() ) {
        range = _srcClip->getFrameRange();
    } else {
        timeLineGetBounds(range.min, range.max);
    }
    range.min = (std::max)( (double)INT_MIN, (std::min)( (double)INT_MAX, std::ceil(range.min) ) );
    range.max = (std::max)( (double)INT_MIN, (std::min)( (double)INT_MAX, std::floor(range.max) ) );
    _frameRange->setValue( (int)range.min, (int)range.max );
}

/* set up and run a processor */
void
ImageStatisticsPlugin::setupAndProcess(ImageStatisticsProcessorBase &processor,
                                       const Image* srcImg,
                                       double time,
                                       const OfxRectI &analysisWindow,
                                       const OfxPointD& renderScale,
                                       const Results &prevResults,
                                       Results *results)
{
    // set the images
    processor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

    // set the render window
    processor.setRenderWindow(analysisWindow, renderScale);

    processor.setPrevResults(time, prevResults);

    // Call the base class process member, this will call the derived templated process code
    processor.process();

    if ( !abort() ) {
        processor.getResults(results);
    }
}

void
ImageStatisticsPlugin::getAnalysisRectangle(double time,
                                            OfxRectD *rect)
{
    bool restrictToRectangle = _restrictToRectangle->getValueAtTime(time);

    if (!restrictToRectangle && _srcClip) {
        // use the src region of definition as rectangle, but avoid infinite rectangle
        *rect = _srcClip->getRegionOfDefinition(time);
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        if (rect->x1 <= kOfxFlagInfiniteMin) {
            rect->x1 = offset.x;
        }
        if (rect->x2 >= kOfxFlagInfiniteMax) {
            rect->x2 = offset.x + size.x;
        }
        if (rect->y1 <= kOfxFlagInfiniteMin) {
            rect->y1 = offset.y;
        }
        if (rect->y2 >= kOfxFlagInfiniteMax) {
            rect->y2 = offset.y + size.y;
        }
    } else {
        _btmLeft->getValueAtTime(time, rect->x1, rect->y1);
        _size->getValueAtTime(time, rect->x2, rect->y2);
        rect->x2 += rect->x1;
        rect->y2 += rect->y1;
    }
}

// Compute the canonical region to fetch for analysis: the analyzed rectangle (see
// getAnalysisRectangle()) intersected with the source RoD. Returns false if it is empty, in which
// case nothing has to be fetched.
bool
ImageStatisticsPlugin::computeFetchRect(double time,
                                        OfxRectD *fetchRect)
{
    assert(_srcClip);
    OfxRectD rect;
    getAnalysisRectangle(time, &rect);

    return Coords::rectIntersection(rect, _srcClip->getRegionOfDefinition(time), fetchRect);
}

bool
ImageStatisticsPlugin::computeWindow(const Image* srcImg,
                                     const OfxPointD& renderScale,
                                     double time,
                                     OfxRectI *analysisWindow)
{
    OfxRectD regionOfInterest;
    getAnalysisRectangle(time, &regionOfInterest);
    Coords::toPixelEnclosing(regionOfInterest,
                             renderScale,
                             srcImg->getPixelAspectRatio(),
                             analysisWindow);

    return Coords::rectIntersection(*analysisWindow, srcImg->getBounds(), analysisWindow);
}

// update image statistics
void
ImageStatisticsPlugin::update(const Image* srcImg,
                              double time,
                              const OfxRectI &analysisWindow,
                              const OfxPointD& renderScale)
{
    // TODO: CHECK if checkDoubleAnalysis param is true and analysisWindow is the same as btmLeft/sizeAnalysis
    Results results;

    if ( !abort() ) {
        updateSub<ImageMinMaxMeanProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageSDevProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
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
                                  const OfxRectI &analysisWindow,
                                  const OfxPointD& renderScale)
{
    Results results;

    if ( !abort() ) {
        updateSub<ImageHSVLMinMaxMeanProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageHSVLSDevProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( !abort() ) {
        updateSub<ImageHSVLSkewnessKurtosisProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
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
                                  const OfxRectI &analysisWindow,
                                  const OfxPointD& renderScale)
{
    Results results;

    if ( !abort() ) {
        updateSub<ImageLumaProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( abort() ) {
        return;
    }
    _maxLumaPix->setValueAtTime(time, results.maxPos.x, results.maxPos.y);
    _maxLumaPixVal->setValueAtTime(time, results.maxVal.r, results.maxVal.g, results.maxVal.b, results.maxVal.a);
    _minLumaPix->setValueAtTime(time, results.minPos.x, results.minPos.y);
    _minLumaPixVal->setValueAtTime(time, results.minVal.r, results.minVal.g, results.minVal.b, results.minVal.a);
}

void
ImageStatisticsPlugin::updateAutoCrop(const Image* srcImg,
                                      double time,
                                      const OfxRectI &analysisWindow,
                                      const OfxPointD& renderScale)
{
    Results results;

    if ( !abort() ) {
        updateSub<ImageAutoCropProcessor>(srcImg, time, analysisWindow, renderScale, results, &results);
    }
    if ( abort() ) {
        return;
    }
    OfxRectI box = results.cropBox;
    if ( (box.x1 >= box.x2) || (box.y1 >= box.y2) ) {
        // no pixel above the threshold: an empty box at the bottom left of the analyzed area
        box.x1 = box.x2 = analysisWindow.x1;
        box.y1 = box.y2 = analysisWindow.y1;
    }
    // pixel to canonical coordinates ("Mapping Between The Spatial Coordinate Systems" in the
    // OFX Programming Reference): X' = (X * PAR)/SX, Y' = Y/SY (unfielded images).
    // Not Coords::toCanonical(), which puts an empty box at (0,0).
    const double par = srcImg->getPixelAspectRatio();
    const double x1 = box.x1 * par / renderScale.x;
    const double y1 = box.y1 / renderScale.y;
    const double x2 = box.x2 * par / renderScale.x;
    const double y2 = box.y2 / renderScale.y;
    _autoCropBtmLeft->setValueAtTime(time, x1, y1);
    _autoCropSize->setValueAtTime(time, x2 - x1, y2 - y1);
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
        // the rectangle is hidden: give the cursor back, or a resize cursor could stick
        releaseCursor();

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
        // the rectangle was turned off during the drag (if any): abandon it
        abandonDrag();

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
    // The Analyze Sequence buttons fetch Source images at other times than the current one
    // (ofxImageClip.rst: "it is an error for a plugin to attempt random temporal image access if
    // it has not flagged that it wishes to do so"). Only flag it if the host supports it ("it is an
    // error for a plugin to attempt random temporal image access if the host does not support it"),
    // so that the plugin still loads on the other hosts.
    desc.setTemporalClipAccess(getImageEffectHostDescription()->temporalClipAccess);
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
    srcClip->setTemporalClipAccess(getImageEffectHostDescription()->temporalClipAccess); // see describe()
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

    // HiDPI
    hiDPIDescribeParams(desc, NULL, page);

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

    // sequenceRange
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSequenceRange);
        param->setLabel(kParamSequenceRangeLabel);
        param->setHint(kParamSequenceRangeHint);
        assert(param->getNOptions() == eSequenceRangeInput);
        param->appendOption(kParamSequenceRangeOptionInput);
        assert(param->getNOptions() == eSequenceRangeTimeline);
        param->appendOption(kParamSequenceRangeOptionTimeline);
        assert(param->getNOptions() == eSequenceRangeCustom);
        param->appendOption(kParamSequenceRangeOptionCustom);
        param->setDefault( (int)eSequenceRangeInput ); // the behaviour of version 1.0
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // frameRange
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDimensionLabels("first", "last");
        param->setDefault(1, 1);
        param->setRange(INT_MIN, INT_MIN, INT_MAX, INT_MAX);
        param->setDisplayRange(1, 1, 100, 100);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // inputRange
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamInputRange);
        param->setLabel(kParamInputRangeLabel);
        param->setHint(kParamInputRangeHint);
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

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamGroupAutoCrop);
        if (group) {
            group->setLabel(kParamGroupAutoCrop);
            group->setAsTab();
            if (page) {
                page->addChild(*group);
            }
        }

        // autoCropMode
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAutoCropMode);
            param->setLabel(kParamAutoCropModeLabel);
            param->setHint(kParamAutoCropModeHint);
            assert(param->getNOptions() == eAutoCropModeRGB);
            param->appendOption(kParamAutoCropModeOptionRGB);
            assert(param->getNOptions() == eAutoCropModeRGBA);
            param->appendOption(kParamAutoCropModeOptionRGBA);
            assert(param->getNOptions() == eAutoCropModeAlpha);
            param->appendOption(kParamAutoCropModeOptionAlpha);
            assert(param->getNOptions() == eAutoCropModeLuminance);
            param->appendOption(kParamAutoCropModeOptionLuminance);
            param->setDefault( (int)eAutoCropModeRGB );
            param->setEvaluateOnChange(false); // re-analyzed in changedParam()
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // autoCropThreshold
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAutoCropThreshold);
            param->setLabel(kParamAutoCropThresholdLabel);
            param->setHint(kParamAutoCropThresholdHint);
            param->setDefault(0.);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 1.);
            param->setIncrement(0.01);
            param->setDigits(4);
            param->setEvaluateOnChange(false); // re-analyzed in changedParam()
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // autoCropBottomLeft
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamAutoCropBtmLeft);
            param->setLabel(kParamAutoCropBtmLeftLabel);
            param->setHint(kParamAutoCropBtmLeftHint);
            param->setDoubleType(eDoubleTypeXYAbsolute); // canonical coordinates
            param->setDefault(0., 0.);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setDigits(2); // fractional if the pixel aspect ratio is not 1
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // autoCropSize
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamAutoCropSize);
            param->setLabel(kParamAutoCropSizeLabel);
            param->setHint(kParamAutoCropSizeHint);
            param->setDoubleType(eDoubleTypeXY); // canonical coordinates
            param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
            param->setDefault(0., 0.);
            param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setDigits(2); // fractional if the pixel aspect ratio is not 1
            param->setEvaluateOnChange(false);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeFrameAutoCrop
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeFrameAutoCrop);
            param->setLabel(kParamAnalyzeFrameAutoCropLabel);
            param->setHint(kParamAnalyzeFrameAutoCropHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // analyzeSequenceAutoCrop
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamAnalyzeSequenceAutoCrop);
            param->setLabel(kParamAnalyzeSequenceAutoCropLabel);
            param->setHint(kParamAnalyzeSequenceAutoCropHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearFrameAutoCrop
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearFrameAutoCrop);
            param->setLabel(kParamClearFrameAutoCropLabel);
            param->setHint(kParamClearFrameAutoCropHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        // clearSequenceAutoCrop
        {
            PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClearSequenceAutoCrop);
            param->setLabel(kParamClearSequenceAutoCropLabel);
            param->setHint(kParamClearSequenceAutoCropHint);
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
