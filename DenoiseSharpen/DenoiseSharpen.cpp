/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX DenoiseSharpen plugin.
 */

/*
   TODO:
   - add "Luminance Blend [0.7]" and "Chrominance Blend [1.0]" settings to YCbCr and Lab, which is like "mix", but only on luminance or chrominance.
   - bandlets (see fig 25 of http://www.cmap.polytechnique.fr/~mallat/papiers/07-NumerAlgo-MallatPeyre-BandletsReview.pdf )
   - edge-aware version
   - estimate a per-intensity noise gain, based on analysis of the HH1 subband, in conjunction with the first smoothed level. analyze the noise in one channel only (luminance or green).
   is this is film, analyze at 9 values from 0.1 to 1.0, with a geometric progression, thus x = 0.1*a^i for i = 0..8, whith a = (1/0.1)^(1./8) = 1.13314845307
   if this is digital, use an arithmetic progression, x=0.1 + 0.1*i

   Notes on edge-aware version:

   - multiply side weights (which are 1 for now) by exp(-||c_i(p)-c_i(q)||^2/(2sigma_r^2) and normalize the total weight
   - the problem is that the filtering order (column, then rows, or rows, then columns) has a strong influence on the result. Check
   - can be color or luminance difference

   - Hanika et al. test several values of sigma_r and

   - In the context of denoising by bilateral filtering,
   Liu et al. [41] show that adapting the range parameter σr to estimates of the local noise level yields
   more satisfying results. The authors recommend a linear dependence:
   σ_r = 1.95 σ_n, where σ_n is the local noise level estimate.
   [41]  C. Liu, W. T. Freeman, R. Szeliski, and S. Kang, “Noise estimation from a single image,” in Proceedings of the Conference on IEEE Computer Vision and Pattern Recognition, volume 1, pp. 901–908, 2006.

   - as an estimate of sigma_n, we can use the
 */

#define kUseMultithread // define to use the multithread suite
//#define DEBUG_STDOUT // output debugging messages on stdout (for Resolve)

#if defined(_OPENMP)
#include <omp.h>
#define _GLIBCXX_PARALLEL // enable libstdc++ parallel STL algorithm (eg nth_element, sort...)
#endif
#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm> // max
#ifdef DEBUG_STDOUT
#include <iostream>
#define DBG(x) (x)
#else
#define DBG(x) (void)0
#endif

#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"
#include "ofxsRectangleInteract.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#include "ofxsCopier.h"
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

#ifndef M_LN2
#define M_LN2       0.693147180559945309417232121458176568  /* loge(2)        */
#endif

using namespace OFX;
#ifdef DEBUG_STDOUT
using std::cout;
using std::endl;
#endif

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DenoiseSharpen"
#define kPluginGrouping "Filter"
#define kPluginDescriptionShort \
    "Denoise and/or sharpen images using wavelet-based algorithms.\n" \
    "\n" \
    "## Description\n" \
    "\n" \
    "This plugin allows the separate denoising of image channels in multiple color spaces using wavelets, using the BayesShrink algorithm, and can also sharpen the image details.\n" \
    "\n" \
    "Noise levels for each channel may be either set manually, or analyzed from the image data in each wavelet subband using the MAD (median absolute deviation) estimator.\n" \
    "Noise analysis is based on the assuption that the noise is Gaussian and additive (it is not intensity-dependent). If there is speckle or salt-and-pepper noise in the images, the Median or SmoothPatchBased filters may be more appropriate.\n" \
    "The color model specifies the channels and the transforms used. Noise levels have to be re-adjusted or re-analyzed when changing the color model.\n" \
    "\n" \
    "## Basic Usage\n" \
    "\n" \
    "The input image should be in linear RGB.\n" \
    "\n" \
    "For most footage, the effect works best by keeping the default Y'CbCr color model. The color models are made to work with Rec.709 data, but DenoiseSharpen will still work if the input is in another colorspace, as long as the input is linear RGB:\n" \
    "\n" \
    "- The Y'CbCr color model uses the Rec.709 opto-electronic transfer function to convert from RGB to R'G'B' and the the Rec.709 primaries to convert from R'G'B' to Y'CbCr.\n" \
    "- The L * a * b color model uses the Rec.709 RGB primaries to convert from RGB to L * a * b.\n" \
    "- The R'G'B' color model uses the Rec.709 opto-electronic transfer function to convert from RGB to R'G'B'.\n" \
    "- The RGB color model (linear) makes no assumption about the RGB color space, and works directly on the RGB components, assuming additive noise. If, say, the noise is known to be multiplicative, one can convert the images to Log before denoising, use this option, and convert back to linear after denoising.\n" \
    "- The Alpha channel, if processed, is always considered to be linear.\n" \
    "\n" \
    "The simplest way to use this plugin is to leave the noise analysis area to the whole image, and click \"Analyze Noise Levels\". Once the analysis is done, \"Lock Noise Analysis\" is checked in order to avoid modifying the essential parameters by mistake.\n" \
    "\n" \
    "If the image has many textured areas, it may be preferable to select an analysis area with flat colors, free from any details, shadows or hightlights, to avoid considering texture as noise. The AnalysisMask input can be used to mask the analysis, if the rectangular area is not appropriate. Any non-zero pixels in the mask are taken into account. A good option for the AnalysisMask would be to take the inverse of the output of an edge detector and clamp it correctly so that all pixels near the edges have a value of zero..\n" \
    "\n" \
    "If the sequence to be denoised does not have enough flat areas, you can also connect a reference footage with the same kind of noise to the AnalysisSource input: that source will be used for the analysis only. If no source with flat areas is available, and noise analysis can only be performed on areas which also contain details, it is often preferable to disable very low, low, and sometimes medium frequencies in the \"Frequency Tuning\" parameters group, or at least to lower their gain, since they may be misestimated by the noise analysis process.\n" \
    "If the noise is IID (independent and identically distributed), such as digital sensor noise, only \"Denoise High Frequencies\" should be checked. If the noise has some grain (i.e. it commes from lossy compression of noisy images by a camera, or it is scanned film), then you may want to enable medium frequencies as well. If low and very low frequencies are enabled, but the analysis area is not a flat zone, the signal itself (i.e. the noise-free image) could be considered as noise, and the result may exhibit low contrast and blur.\n" \
    "\n" \
    "To check what details have been kept after denoising, you can raise the Sharpen Amount to something like 10, and then adjust the Noise Level Gain to get the desired denoising amount, until no noise is left and only image details remain in the sharpened image. You can then reset the Sharpen Amount to zero, unless you actually want to enhance the contrast of your denoised footage.\n" \
    "\n" \
    "You can also check what was actually removed from the original image by selecting the \"Noise\" Output mode (instead of \"Result\"). If too many image details are visible in the noise, noise parameters may need to be tuned.\n"


#ifdef _OPENMP
#define kPluginDescription kPluginDescriptionShort "\nThis plugin was compiled with OpenMP support."
#else
#define kPluginDescription kPluginDescriptionShort
#endif
#define kPluginIdentifier "net.sf.openfx.DenoiseSharpen"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipSourceHint "The footage to be denoised. If nothing is connected to the AnalysisSource input, this is also used for noise analysis."
#define kClipMaskHint "An optional image to use as a mask. By default, the effect is limited to the non-black areas of the mask."
#define kClipAnalysisSource "AnalysisSource"
#define kClipAnalysisSourceHint "An optional noise source. If connected, this is used instead of the Source input for the noise analysis. This is used to analyse noise from some footage by apply it on another footage, in case the footage to be denoised does not have enough flat areas."
#define kClipAnalysisMask "AnalysisMask"
#define kClipAnalysisMaskHint "An optional mask for the analysis area. This mask is intersected with the Analysis Rectangle. Non-zero pixels are taken into account in the noise analysis phase."

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamOutputMode "outputMode"
#define kParamOutputModeLabel "Output"
#define kParamOutputModeHint "Select which image is output when analysis is locked. When analysis is not locked, the effect does nothing (the output is the source image)."
#define kParamOutputModeOptionResult "Result", "The result of denoising and sharpening the Source image.", "result"
#define kParamOutputModeOptionNoise "Noise", "An image containing what would be added to the image to denoise it. If 'Denoise Amount' is zero, this image should be black. Only noise should be visible in this image. If you can see a lot of picture detail in the noise output, it means the current settings are denoising too hard and remove too much of the image, which leads to a smoothed result. Try to lower the noise levels or the noise level gain.", "noise"
#define kParamOutputModeOptionSharpen "Sharpen", "An image containing what would be added to the image to sharpen it. If 'Sharpen Amount' is zero, this image should be black. Only image details should be visible in this image. If you can see a lot of noise in the sharpen output, it means the current settings are denoising not enough, which leads to a noisy result. Try to raise the noise levels or the noise level gain.", "sharpen"
enum OutputModeEnum
{
    eOutputModeResult = 0,
    eOutputModeNoise,
    eOutputModeSharpen,
};

#define kParamColorModel "colorModel"
#define kParamColorModelLabel "Color Model"
#define kParamColorModelHint "The colorspace where denoising is performed. These colorspaces assume that input and output use the Rec.709/sRGB chromaticities and the D65 illuminant, but should tolerate other input colorspaces (the output colorspace will always be the same as the input colorspace). Noise levels are reset when the color model is changed."
#define kParamColorModelOptionYCbCr "Y'CbCr(A)", "The YCbCr color model has one luminance channel (Y) which contains most of the detail information of an image (such as brightness and contrast) and two chroma channels (Cb = blueness, Cr = reddness) that hold the color information. Note that this choice drastically affects the result. Uses the Rec.709 opto-electronic transfer function to convert from RGB to R'G'B' and the the Rec.709 primaries to convert from R'G'B' to Y'CbCr.", "ycbcr"
#define kParamColorModelOptionLab "CIE L*a*b(A)", "CIE L*a*b* is a color model in which chrominance is separated from lightness and color distances are perceptually uniform. Note that this choice drastically affects the result. Uses the Rec.709 primaries to convert from RGB to L*a*b.", "cielab"
#define kParamColorModelOptionRGB "R'G'B'(A)", "The R'G'B' color model (gamma-corrected RGB) separates an image into channels of red, green, and blue. Note that this choice drastically affects the result. Uses the Rec.709 opto-electronic transfer function to convert from RGB to R'G'B'.", "gammargb"
#define kParamColorModelOptionLinearRGB "RGB(A)", "The Linear RGB color model processes the raw linear components. Usually a bad choice, except when denoising non-color data (e.g. depth or motion vectors). No assumption is made about the RGB color space.", "linearrgb"
enum ColorModelEnum
{
    eColorModelYCbCr = 0,
    eColorModelLab,
    eColorModelRGB,
    eColorModelLinearRGB,
    eColorModelAny, // used for channelLabel()
};

#define kGroupAnalysis "analysis"
#define kGroupAnalysisLabel "Analysis"
#define kParamAnalysisLock "analysisLock"
#define kParamAnalysisLockLabel "Lock Analysis and Apply"
#define kParamAnalysisLockHint "Lock all noise analysis parameters and apply denoising. When the analysis is not locked, the source image is output."
#define kParamB3 "useB3Spline"
#define kParamB3Label "B3 Spline Interpolation"
#define kParamB3Hint "For wavelet decomposition, use a 5x5 filter based on B3 spline interpolation rather than a 3x3 Lagrange linear filter. Noise levels are reset when this setting is changed. The influence of this parameter is minimal, and it should not be changed."
#define kParamAnalysisFrame "analysisFrame"
#define kParamAnalysisFrameLabel "Analysis Frame"
#define kParamAnalysisFrameHint "The frame number where the noise levels were analyzed."

#define kGroupNoiseLevels "noiseLevels"
#define kGroupNoiseLevelsLabel "Noise Levels"
#define kParamNoiseLevelHint "Adjusts the noise variance of the selected channel for the given noise frequency. May be estimated from image data by pressing the \"Analyze Noise\" button."
#define kParamNoiseLevelMax 0.05 // noise level is at most 1/sqrt(12) ~=0.29 (stddev of a uniform distribution between 0 and 1)
#define kParamYLRNoiseLevel "ylrNoiseLevel"
#define kParamYLRNoiseLevelLabel "Y/L/R Level"
#define kParamYNoiseLevelLabel "Y Level"
#define kParamLNoiseLevelLabel "L Level"
#define kParamRNoiseLevelLabel "R Level"
#define kParamCbAGNoiseLevel "cbagNoiseLevel"
#define kParamCbAGNoiseLevelLabel "Cb/A/G Level"
#define kParamCbNoiseLevelLabel "Cb Level"
#define kParamANoiseLevelLabel "A Level"
#define kParamGNoiseLevelLabel "G Level"
#define kParamCrBBNoiseLevel "crbbNoiseLevel"
#define kParamCrBBNoiseLevelLabel "Cr/B/B Level"
#define kParamCrNoiseLevelLabel "Cr Level"
#define kParamBNoiseLevelLabel "B Level"
#define kParamAlphaNoiseLevel "alphaNoiseLevel"
#define kParamAlphaNoiseLevelLabel "Alpha Level"
#define kParamHigh "High"
#define kParamNoiseLevelHighLabel " (High)"
#define kParamMedium "Medium"
#define kParamNoiseLevelMediumLabel " (Medium)"
#define kParamLow "Low"
#define kParamNoiseLevelLowLabel " (Low)"
#define kParamVeryLow "VeryLow"
#define kParamNoiseLevelVeryLowLabel " (Very Low)"
#define kParamAnalyzeNoiseLevels "analyzeNoiseLevels"
#define kParamAnalyzeNoiseLevelsLabel "Analyze Noise Levels"
#define kParamAnalyzeNoiseLevelsHint "Computes the noise levels from the current frame and current color model. To use the same settings for the whole sequence, analyze a frame that is representative of the sequence. If a mask is set, it is used to compute the noise levels from areas where the mask is non-zero. If there are keyframes on the noise level parameters, this sets a keyframe at the current frame. The noise levels can then be fine-tuned."

#define kParamNoiseLevelGain "noiseLevelGain"
#define kParamNoiseLevelGainLabel "Noise Level Gain"
#define kParamNoiseLevelGainHint "Global gain to apply to the noise level thresholds. 0 means no denoising, 1 means use the estimated thresholds multiplied by the per-frequency gain and the channel gain. The default value (1.0) is rather conservative (it does not destroy any kind of signal). Values around 1.1 or 1.2 usually give more pleasing results."

#define kParamDenoiseAmount "denoiseAmount"
#define kParamDenoiseAmountLabel "Denoise Amount"
#define kParamDenoiseAmountHint "The amount of denoising to apply. 0 means no denoising (which may be useful to sharpen without denoising), between 0 and 1 does a soft thresholding of below the thresholds, thus keeping some noise, and 1 applies the threshold strictly and removes everything below the thresholds. This should be used only if you want to keep some noise, for example for noise matching. This value is multiplied by the per-channel amount se in the 'Channel Tuning' group. Remember that the thresholds are multiplied by the per-frequency gain, the channel gain, and the Noise Level Gain first."

#define kGroupTuning "freqTuning"
#define kGroupTuningLabel "Frequency Tuning"
#define kParamEnable "enableFreq"
#define kParamGain "gainFreq"
#define kParamEnableHighLabel "Denoise High Frequencies"
#define kParamEnableHighHint "Check to enable the high frequency noise level thresholds. It is recommended to always leave this checked."
#define kParamGainHighLabel "High Gain"
#define kParamGainHighHint "Gain to apply to the high frequency noise level thresholds. 0 means no denoising, 1 means use the estimated thresholds multiplied by the channel Gain and the Noise Level Gain."
#define kParamEnableMediumLabel "Denoise Medium Frequencies"
#define kParamEnableMediumHint "Check to enable the medium frequency noise level thresholds. Can be disabled if the analysis area contains high frequency texture, or if the the noise is known to be IID (independent and identically distributed), for example if this is only sensor noise and lossless compression is used, and not grain or compression noise."
#define kParamGainMediumLabel "Medium Gain"
#define kParamGainMediumHint "Gain to apply to the medium frequency noise level thresholds. 0 means no denoising, 1 means use the estimated thresholds multiplied by the channel Gain and the Noise Level Gain."
#define kParamEnableLowLabel "Denoise Low Frequencies"
#define kParamEnableLowHint "Check to enable the low frequency noise level thresholds. Must be disabled if the analysis area contains texture, or if the noise is known to be IID (independent and identically distributed), for example if this is only sensor noise and lossless compression is used, and not grain or compression noise."
#define kParamGainLowLabel "Low Gain"
#define kParamGainLowHint "Gain to apply to the low frequency noise level thresholds. 0 means no denoising, 1 means use the estimated thresholds multiplied by the channel Gain and the Noise Level Gain."
#define kParamEnableVeryLowLabel "Denoise Very Low Frequencies"
#define kParamEnableVeryLowHint "Check to enable the very low frequency noise level thresholds. Can be disabled in most cases. Must be disabled if the analysis area contains texture, or if the noise is known to be IID (independent and identically distributed), for example if this is only sensor noise and lossless compression is used, and not grain or compression noise."
#define kParamGainVeryLowLabel "Very Low Gain"
#define kParamGainVeryLowHint "Gain to apply to the very low frequency noise level thresholds. 0 means no denoising, 1 means use the estimated thresholds multiplied by the channel Gain and the global Noise Level Gain."

#define kParamAdaptiveRadius "adaptiveRadius"
#define kParamAdaptiveRadiusLabel "Adaptive Radius"
#define kParamAdaptiveRadiusHint "Radius of the window where the signal level is analyzed at each scale. If zero, the signal level is computed from the whole image, which may excessively blur the edges if the image has many flat color areas. A reasonable value should to be in the range 2-4."
#define kParamAdaptiveRadiusDefault 4

#define kGroupChannelTuning "channelTuning"
#define kGroupChannelTuningLabel "Channel Tuning"
#define kParamChannelGainHint "Gain to apply to the thresholds for this channel. 0 means no denoising, 1 means use the estimated thresholds multiplied by the per-frequency gain and the global Noise Level Gain."
#define kParamYLRGain "ylrGain"
#define kParamYLRGainLabel "Y/L/R Gain"
#define kParamYGainLabel "Y Gain"
#define kParamLGainLabel "L Gain"
#define kParamRGainLabel "R Gain"
#define kParamCbAGGain "cbagGain"
#define kParamCbAGGainLabel "Cb/A/G Gain"
#define kParamCbGainLabel "Cb Gain"
#define kParamAGainLabel "A Gain"
#define kParamGGainLabel "G Gain"
#define kParamCrBBGain "crbbGain"
#define kParamCrBBGainLabel "Cr/B/B Gain"
#define kParamCrGainLabel "Cr Gain"
#define kParamBGainLabel "B Gain"
#define kParamAlphaGain "alphaGain"
#define kParamAlphaGainLabel "Alpha Gain"

#define kParamAmountHint "The amount of denoising to apply to the specified channel. 0 means no denoising, between 0 and 1 does a soft thresholding of below the thresholds, thus keeping some noise, and 1 applies the threshold strictly and removes everything below the thresholds. This should be used only if you want to keep some noise, for example for noise matching. This value is multiplied by the global Denoise Amount. Remember that the thresholds are multiplied by the per-frequency gain, the channel gain, and the Noise Level Gain first."
#define kParamYLRAmount "ylrAmount"
#define kParamYLRAmountLabel "Y/L/R Amount"
#define kParamYAmountLabel "Y Amount"
#define kParamLAmountLabel "L Amount"
#define kParamRAmountLabel "R Amount"
#define kParamCbAGAmount "cbagAmount"
#define kParamCbAGAmountLabel "Cb/A/G Amount"
#define kParamCbAmountLabel "Cb Amount"
#define kParamAAmountLabel "A Amount"
#define kParamGAmountLabel "G Amount"
#define kParamCrBBAmount "crbbAmount"
#define kParamCrBBAmountLabel "Cr/B/B Amount"
#define kParamCrAmountLabel "Cr Amount"
#define kParamBAmountLabel "B Amount"
#define kParamAlphaAmount "alphaAmount"
#define kParamAlphaAmountLabel "Alpha Amount"


#define kGroupSharpen "sharpen"
#define kGroupSharpenLabel "Sharpen"

#define kParamSharpenAmount "sharpenAmount"
#define kParamSharpenAmountLabel "Sharpen Amount"
#define kParamSharpenAmountHint "Adjusts the amount of sharpening applied. Be careful that only components that are above the noise levels are enhanced, so the noise level gain parameters are very important for proper sharpening. For example, if 'Noise Level Gain' is set to zero (0), then noise is sharpened as well as signal. If the 'Noise Level Gain' is set to one (1), only signal is sharpened. In order to sharpen without denoising, set the 'Denoise Amount' parameter to zero (0)."

// see setup() for the difference between this and the GIMP wavelet sharpen's radius
#define kParamSharpenSize "sharpenSize"
#define kParamSharpenSizeLabel "Sharpen Size"
#define kParamSharpenSizeHint "Adjusts the size of the sharpening. For very unsharp images it is recommended to use higher values. Default is 10."

#define kParamSharpenLuminance "sharpenLuminance"
#define kParamSharpenLuminanceLabel "Sharpen Y Only"
#define kParamSharpenLuminanceHint "Sharpens luminance only (if colormodel is R'G'B', sharpen only RGB). This avoids color artifacts to appear. Colour sharpness in natural images is not critical for the human eye."

#define kParamPremultChanged "premultChanged"

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

#define kLevelMax 4 // 7 // maximum level for denoising

#define kProgressAnalysis // define to enable progress for analysis
//#define kProgressRender // define to enable progress for render


#ifdef kProgressAnalysis
#define progressStartAnalysis(x) progressStart(x)
#define progressUpdateAnalysis(x) progressUpdate(x)
#define progressEndAnalysis() progressEnd()
#else
#define progressStartAnalysis(x) unused(x)
#define progressUpdateAnalysis(x) unused(x)
#define progressEndAnalysis() ( (void)0 )
#endif

#ifdef kProgressRender
#define progressStartRender(x) progressStart(x)
#define progressUpdateRender(x) progressUpdate(x)
#define progressEndRender() progressEnd()
#else
#define progressStartRender(x) unused(x)
#define progressUpdateRender(x) unused(x)
#define progressEndRender() ( (void)0 )
#endif

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

// those are the noise levels on HHi subands that correspond to a
// Gaussian noise, with the dcraw "a trous" wavelets.
// dcraw's version:
//static const float noise[] = { 0.8002,   0.2735,   0.1202,   0.0585,    0.0291,    0.0152,    0.0080,     0.0044 };
// my version (use a NoiseCImg with sigma=1 on input, and uncomment the printf below to get stdev
//static const float noise[] = { 0.800519, 0.272892, 0.119716, 0.0577944, 0.0285969, 0.0143022, 0.00723830, 0.00372276 };
//static const float noise[] = { 0.800635, 0.272677, 0.119736, 0.0578772, 0.0288094, 0.0143987, 0.00715343, 0.00360457 };
//static const float noise[] = { 0.800487, 0.272707, 0.11954,  0.0575443, 0.0285203, 0.0142214, 0.00711241, 0.00362163 };
//static const float noise[] = { 0.800539, 0.273004, 0.119987, 0.0578018, 0.0285823, 0.0143751, 0.00710835, 0.00360699 };
//static const float noise[] = { 0.800521, 0.272831, 0.119881, 0.0578049, 0.0287941, 0.0144411, 0.00739661, 0.00370236 };
//static const float noise[] = { 0.800543, 0.272880, 0.119764, 0.0577759, 0.0285594, 0.0143134, 0.00717619, 0.00366561 };
//static const float noise[] = { 0.800370, 0.272859, 0.119750, 0.0577506, 0.0285429, 0.0144341, 0.00733049, 0.00362141 };
static const float noise[] = { 0.8005,   0.2729,   0.1197,   0.0578,    0.0286,    0.0144,    0.0073,     0.0037 };

// for B3 Splines, the noise levels are different
//static const float noise_b3[] = { 0.890983, 0.200605, 0.0855252, 0.0412078, 0.0204200, 0.0104461, 0.00657528, 0.00447530 };
//static const float noise_b3[] = { 0.890774, 0.200587, 0.0855374, 0.0411216, 0.0205889, 0.0104974, 0.00661727, 0.00445607 };
//static const float noise_b3[] = { 0.890663, 0.200599, 0.0854052, 0.0412852, 0.0207739, 0.0104784, 0.00634701, 0.00447869  };
//static const float noise_b3[] = { 0.890611, 0.200791, 0.0856202, 0.0412572, 0.0206385, 0.0103060, 0.00653794, 0.00458579  };
//static const float noise_b3[] = { 0.890800, 0.200619, 0.0856033, 0.0412239, 0.0206324, 0.0104488, 0.00664716, 0.00440302  };
//static const float noise_b3[] = { 0.890912, 0.200739, 0.0856778, 0.0412566, 0.0205922, 0.0103516, 0.00650336, 0.00445504  };
static const float noise_b3[] = { 0.8908,   0.2007,   0.0855,    0.0412,    0.0206,    0.0104,    0.0065,     0.0045  };

#if defined(_OPENMP)
#define abort_test() if ( !omp_get_thread_num() && abort() ) { throwSuiteStatusException(kOfxStatFailed); }
#define abort_test_loop() if ( abort() ) { if ( !omp_get_thread_num() ) {throwSuiteStatusException(kOfxStatFailed);} \
                                           else { continue;} \
}
#else
#define abort_test() if ( abort() ) { throwSuiteStatusException(kOfxStatFailed); }
#define abort_test_loop() abort_test()
#endif

static Color::LutManager<Mutex>* gLutManager;

template<typename T>
static inline void
unused(const T&) {}

static
const char*
fToParam(unsigned f)
{
    switch (f) {
    case 0:

        return kParamHigh;
    case 1:

        return kParamMedium;
    case 2:

        return kParamLow;
    case 3:

        return kParamVeryLow;
    default:

        return "";
    }
}

static
const char*
fToLabel(unsigned f)
{
    switch (f) {
    case 0:

        return kParamNoiseLevelHighLabel;
    case 1:

        return kParamNoiseLevelMediumLabel;
    case 2:

        return kParamNoiseLevelLowLabel;
    case 3:

        return kParamNoiseLevelVeryLowLabel;
    default:

        return "";
    }
}

static
std::string
channelParam(unsigned c,
             unsigned f)
{
    const char* fstr = fToParam(f);

    if (c == 3) {
    }
    switch (c) {
    case 0:

        return std::string(kParamYLRNoiseLevel) + fstr;
    case 1:

        return std::string(kParamCbAGNoiseLevel) + fstr;
    case 2:

        return std::string(kParamCrBBNoiseLevel) + fstr;
    case 3:

        return std::string(kParamAlphaNoiseLevel) + fstr;
    default:
        break;
    }
    assert(false);

    return std::string();
}

static
std::string
enableParam(unsigned f)
{
    const char* fstr = fToParam(f);

    return std::string(kParamEnable) + fstr;
}

static
std::string
gainParam(unsigned f)
{
    const char* fstr = fToParam(f);

    return std::string(kParamGain) + fstr;
}

static
std::string
channelLabel(ColorModelEnum e,
             unsigned c,
             unsigned f)
{
    const char* fstr = fToLabel(f);

    if (c == 3) {
        return std::string(kParamAlphaNoiseLevelLabel) + fstr;
    }
    switch (e) {
    case eColorModelYCbCr:
        switch (c) {
        case 0:

            return std::string(kParamYNoiseLevelLabel) + fstr;
        case 1:

            return std::string(kParamCbNoiseLevelLabel) + fstr;
        case 2:

            return std::string(kParamCrNoiseLevelLabel) + fstr;
        default:
            break;
        }
        break;

    case eColorModelLab:
        switch (c) {
        case 0:

            return std::string(kParamLNoiseLevelLabel) + fstr;
        case 1:

            return std::string(kParamANoiseLevelLabel) + fstr;
        case 2:

            return std::string(kParamBNoiseLevelLabel) + fstr;
        default:
            break;
        }
        break;

    case eColorModelRGB:
    case eColorModelLinearRGB:
        switch (c) {
        case 0:

            return std::string(kParamRNoiseLevelLabel) + fstr;
        case 1:

            return std::string(kParamGNoiseLevelLabel) + fstr;
        case 2:

            return std::string(kParamBNoiseLevelLabel) + fstr;
        default:
            break;
        }
        break;

    case eColorModelAny:
        switch (c) {
        case 0:

            return std::string(kParamYLRNoiseLevelLabel) + fstr;
        case 1:

            return std::string(kParamCbAGNoiseLevelLabel) + fstr;
        case 2:

            return std::string(kParamCrBBNoiseLevelLabel) + fstr;
        default:
            break;
        }
        break;
    } // switch
    assert(false);

    return std::string();
} // channelLabel

static
const char*
amountLabel(ColorModelEnum e,
            unsigned c)
{
    if (c == 3) {
        return kParamAlphaAmountLabel;
    }
    switch (e) {
    case eColorModelYCbCr:
        switch (c) {
        case 0:

            return kParamYAmountLabel;
        case 1:

            return kParamCbAmountLabel;
        case 2:

            return kParamCrAmountLabel;
        default:
            break;
        }
        break;

    case eColorModelLab:
        switch (c) {
        case 0:

            return kParamLAmountLabel;
        case 1:

            return kParamAAmountLabel;
        case 2:

            return kParamBAmountLabel;
        default:
            break;
        }
        break;

    case eColorModelRGB:
    case eColorModelLinearRGB:
        switch (c) {
        case 0:

            return kParamRAmountLabel;
        case 1:

            return kParamGAmountLabel;
        case 2:

            return kParamBAmountLabel;
        default:
            break;
        }
        break;

    case eColorModelAny:
        switch (c) {
        case 0:

            return kParamYLRAmountLabel;
        case 1:

            return kParamCbAGAmountLabel;
        case 2:

            return kParamCrBBAmountLabel;
        default:
            break;
        }
        break;
    } // switch
    assert(false);

    return "";
} // amountLabel

static
const char*
channelGainLabel(ColorModelEnum e,
                 unsigned c)
{
    if (c == 3) {
        return kParamAlphaGainLabel;
    }
    switch (e) {
    case eColorModelYCbCr:
        switch (c) {
        case 0:

            return kParamYGainLabel;
        case 1:

            return kParamCbGainLabel;
        case 2:

            return kParamCrGainLabel;
        default:
            break;
        }
        break;

    case eColorModelLab:
        switch (c) {
        case 0:

            return kParamLGainLabel;
        case 1:

            return kParamAGainLabel;
        case 2:

            return kParamBGainLabel;
        default:
            break;
        }
        break;

    case eColorModelRGB:
    case eColorModelLinearRGB:
        switch (c) {
        case 0:

            return kParamRGainLabel;
        case 1:

            return kParamGGainLabel;
        case 2:

            return kParamBGainLabel;
        default:
            break;
        }
        break;

    case eColorModelAny:
        switch (c) {
        case 0:

            return kParamYLRGainLabel;
        case 1:

            return kParamCbAGGainLabel;
        case 2:

            return kParamCrBBGainLabel;
        default:
            break;
        }
        break;
    } // switch
    assert(false);

    return "";
} // channelGainLabel

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DenoiseSharpenPlugin
    : public ImageEffect
{
    struct Params;

public:

    /** @brief ctor */
    DenoiseSharpenPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _lut( gLutManager->Rec709Lut() ) // TODO: work in different colorspaces
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _analysisSrcClip(NULL)
        , _analysisMaskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _colorModel(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
        , _premultChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _analysisSrcClip = fetchClip(kClipAnalysisSource);
        assert( ( _analysisSrcClip && (_analysisSrcClip->getPixelComponents() == ePixelComponentRGB ||
                                       _analysisSrcClip->getPixelComponents() == ePixelComponentRGBA ||
                                       _analysisSrcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _analysisMaskClip = fetchClip(kClipAnalysisMask);
        assert(!_analysisMaskClip || _analysisMaskClip->getPixelComponents() == ePixelComponentAlpha);

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);


        // fetch noise parameters
        _outputMode = fetchChoiceParam(kParamOutputMode);
        _colorModel = fetchChoiceParam(kParamColorModel);
        _analysisLock = fetchBooleanParam(kParamAnalysisLock);
        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _analysisFrame = fetchIntParam(kParamAnalysisFrame);
        _analyze = fetchPushButtonParam(kParamAnalyzeNoiseLevels);

        // noise levels
        for (unsigned f = 0; f < 4; ++f) {
            for (unsigned c = 0; c < 4; ++c) {
                _noiseLevel[c][f] = fetchDoubleParam( channelParam(c, f) );
            }
        }

        _adaptiveRadius = fetchIntParam(kParamAdaptiveRadius);

        _noiseLevelGain = fetchDoubleParam(kParamNoiseLevelGain);

        _denoiseAmount = fetchDoubleParam(kParamDenoiseAmount);

        // frequency tuning
        for (unsigned int f = 0; f < 4; ++f) {
            _enableFreq[f] = fetchBooleanParam( enableParam(f) );
            _gainFreq[f] = fetchDoubleParam( gainParam(f) );
        }

        // channel tuning
        for (unsigned c = 0; c < 4; ++c) {
            _channelGain[c] = fetchDoubleParam( ( c == 0 ? kParamYLRGain :
                                                  ( c == 1 ? kParamCbAGGain :
                                                    (c == 2 ? kParamCrBBGain :
                                                     kParamAlphaGain) ) ) );
            _amount[c] = fetchDoubleParam( ( c == 0 ? kParamYLRAmount :
                                             ( c == 1 ? kParamCbAGAmount :
                                               (c == 2 ? kParamCrBBAmount :
                                                kParamAlphaAmount) ) ) );
        }

        // sharpen
        _sharpenAmount = fetchDoubleParam(kParamSharpenAmount);
        _sharpenSize = fetchDoubleParam(kParamSharpenSize);
        _sharpenLuminance = fetchBooleanParam(kParamSharpenLuminance);

        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        _b3 = fetchBooleanParam(kParamB3);

        // update the channel labels
        updateLabels();
        updateSecret();
        analysisLock();

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
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    void setup(const RenderArguments &args,
               auto_ptr<const Image>& src,
               auto_ptr<Image>& dst,
               auto_ptr<const Image>& mask,
               Params& p);

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void analyzeNoiseLevels(const InstanceChangedArgs &args);

    template<int nComponents>
    void analyzeNoiseLevelsForComponents(const InstanceChangedArgs &args);

    template <class PIX, int nComponents, int maxValue>
    void analyzeNoiseLevelsForBitDepth(const InstanceChangedArgs &args);

    void updateLabels();

    void updateSecret();

    void wavelet_denoise(float *fimg[3], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] and fimg[2] are working space images of the same size
                         unsigned int iwidth, //!< width of the image
                         unsigned int iheight, //!< height of the image
                         bool b3,
                         const double noiselevels[4], //!< noise levels for high/medium/low/very low frequencies
                         int adaptiveRadius,
                         double denoise_amount, //!< amount parameter
                         double sharpen_amount, //!< constrast boost amount
                         double sharpen_radius, //!< contrast boost radius
                         int startLevel,
                         float a, // progress amount at start
                         float b); // progress increment

    void sigma_mad(float *fimg[2], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] is a working space image of the same size
                   bool *bimgmask,
                   unsigned int iwidth, //!< width of the image
                   unsigned int iheight, //!< height of the image
                   bool b3,
                   double noiselevels[4], //!< output: the sigma for each frequency
                   float a, //!< progress amount at start
                   float b); //!< progress increment

    void analysisLock()
    {
        bool locked = _analysisLock->getValue();

        // unlock the output mode
        _outputMode->setEnabled( locked );
        // lock the color model
        _colorModel->setEnabled( !locked );
        _b3->setEnabled( !locked );
        // disable the interact
        _btmLeft->setEnabled( !locked );
        _size->setEnabled( !locked );
        // lock the noise levels
        for (unsigned f = 0; f < 4; ++f) {
            for (unsigned c = 0; c < 4; ++c) {
                _noiseLevel[c][f]->setEnabled( !locked );
            }
        }
        _analyze->setEnabled( !locked );
    }

private:
    struct Params
    {
        bool doMasking;
        bool maskInvert;
        bool analysisLock;
        bool premult;
        int premultChannel;
        double mix;
        OutputModeEnum outputMode;
        ColorModelEnum colorModel;
        bool b3;
        int startLevel;
        bool process[4];
        double noiseLevel[4][4]; // first index: channel second index: frequency
        int adaptiveRadius;
        double denoise_amount[4];
        double sharpen_amount[4];
        double sharpen_radius;
        OfxRectI srcWindow;

        Params()
            : doMasking(false)
            , maskInvert(false)
            , analysisLock(false)
            , premult(false)
            , premultChannel(3)
            , mix(1.)
            , outputMode(eOutputModeResult)
            , colorModel(eColorModelYCbCr)
            , b3(false)
            , startLevel(0)
            , adaptiveRadius(0)
            , sharpen_radius(0.5)
        {
            for (unsigned int c = 0; c < 4; ++c) {
                process[c] = true;
                for (unsigned int f = 0; f < 4; ++f) {
                    noiseLevel[c][f] = 0.;
                }
                denoise_amount[c] = 0.;
                sharpen_amount[c] = 0.;
            }
            srcWindow.x1 = srcWindow.x2 = srcWindow.y1 = srcWindow.y2 = 0;
        }
    };

    const Color::Lut* _lut;

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    Clip *_analysisSrcClip;
    Clip *_analysisMaskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    ChoiceParam* _outputMode;
    ChoiceParam* _colorModel;
    BooleanParam* _analysisLock;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    IntParam* _analysisFrame;
    PushButtonParam* _analyze;
    DoubleParam* _noiseLevel[4][4];
    IntParam* _adaptiveRadius;
    DoubleParam* _noiseLevelGain;
    DoubleParam* _denoiseAmount;
    BooleanParam* _enableFreq[4];
    DoubleParam* _gainFreq[4];
    DoubleParam* _channelGain[4];
    DoubleParam* _amount[4];
    DoubleParam* _sharpenAmount;
    DoubleParam* _sharpenSize;
    BooleanParam* _sharpenLuminance;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
    BooleanParam* _b3;
};

// compute the maximum level used in wavelet_denoise (not the number of levels)
inline
int
startLevelFromRenderScale(const OfxPointD& renderScale)
{
    double s = std::min(renderScale.x, renderScale.y);

    assert(0. < s && s <= 1.);
    int retval = -(int)std::floor(std::log(s) / M_LN2);
    assert(retval >= 0);

    return retval;
}

// functions hat_transform and wavelet_denoise are from LibRaw 0.17.2 (LGPL 2.1) with local modifications.

// h = (0.25,0.5,0.25) linear Lagrange interpolation, with mirroring at the edges.
// could be made edge-aware, maybe?
// - https://www.darktable.org/wp-content/uploads/2011/11/hdl11_talk.pdf
// - https://jo.dreggn.org/home/2011_atrous.pdf
// for the edge-avoiding a trous, just multiply the side coefficients by
// exp(-(dist2/(2.f*sigma_r*sigma_r)));
// where dist2 is the squared color distance with the center, and sigma_r = 0.1
static
void
hat_transform_linear (float *temp, //!< output vector
                      const float *base, //!< input vector
                      int st, //!< input stride (1 for line, iwidth for column)
                      int size, //!< vector size
                      int sc) //!< scale
{
    assert(sc - 1 + sc < size);
    int i;
    for (i = 0; i < sc; ++i) {
        temp[i] = (2 * base[st * i] + base[st * (sc - i)] + base[st * (i + sc)]) / 4;
    }
    for (; i + sc < size; ++i) {
        temp[i] = (2 * base[st * i] + base[st * (i - sc)] + base[st * (i + sc)]) / 4;
    }
    for (; i < size; ++i) {
        temp[i] = (2 * base[st * i] + base[st * (i - sc)] + base[st * ( 2 * size - 2 - (i + sc) )]) / 4;
    }
}

// h = (1/16, 1/4, 3/8, 1/4, 1/16) (Murtagh F.:  Multiscale transform methods in data analysis)
static
void
hat_transform_b3 (float *temp, //!< output vector
                  const float *base, //!< input vector
                  int st, //!< input stride (1 for line, iwidth for column)
                  int size, //!< vector size
                  int sc) //!< scale
{
    assert(2 * sc - 1 + 2 * sc < size);
    int i;
    for (i = 0; i < sc; ++i) {
        temp[i] = (6 * base[st * i] + 4 * base[st * (sc - i)] + 4 * base[st * (i + sc)] + 1 * base[st * (2 * sc - i)] + 1 * base[st * (i + 2 * sc)]) / 16;
    }
    for (; i < 2 * sc; ++i) {
        temp[i] = (6 * base[st * i] + 4 * base[st * (i - sc)] + 4 * base[st * (i + sc)] + 1 * base[st * (2 * sc - i)] + 1 * base[st * (i + 2 * sc)]) / 16;
    }
    for (; i + 2 * sc < size; ++i) {
        temp[i] = (6 * base[st * i] + 4 * base[st * (i - sc)] + 4 * base[st * (i + sc)] + 1 * base[st * (i - 2 * sc)] + 1 * base[st * (i + 2 * sc)]) / 16;
    }
    for (; i + sc < size; ++i) {
        temp[i] = (6 * base[st * i] + 4 * base[st * (i - sc)] + 4 * base[st * (i + sc)] + 1 * base[st * (i - 2 * sc)] + 1 * base[st * ( 2 * size - 2 - (i + 2 * sc) )]) / 16;
    }
    for (; i < size; ++i) {
        temp[i] = (6 * base[st * i] + 4 * base[st * (i - sc)] + 4 * base[st * ( 2 * size - 2 - (i + sc) )] + 1 * base[st * (i - 2 * sc)] + 1 * base[st * ( 2 * size - 2 - (i + 2 * sc) )]) / 16;
    }
}

static
void
hat_transform (float *temp, //!< output vector
               const float *base, //!< input vector
               int st, //!< input stride (1 for line, iwidth for column)
               int size, //!< vector size
               bool b3,
               int sc) //!< scale
{
    if (b3) {
        hat_transform_b3(temp, base, st, size, sc);
    } else {
        hat_transform_linear(temp, base, st, size, sc);
    }
}

#ifdef kUseMultithread

// multithread processing classes for various stages of the algorithm
template<bool rows>
class ProcessRowsColsBase
    : public MultiThread::Processor
{
public:
    ProcessRowsColsBase(ImageEffect &instance,
                        float* fimg_hpass,
                        float* fimg_lpass,
                        unsigned int iwidth,
                        unsigned int iheight,
                        bool b3,
                        int sc) // 1 << lev
        : _effect(instance)
        , _fimg_hpass(fimg_hpass)
        , _fimg_lpass(fimg_lpass)
        , _iwidth(iwidth)
        , _iheight(iheight)
        , _b3(b3)
        , _sc(sc)
    {
        assert(_fimg_hpass && _fimg_lpass && _iwidth > 0 && _iheight > 0 && sc > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU and at least 1 line par CPU
        unsigned int nCPUs = ( std::min(rows ? _iwidth : _iheight, 4096u) * (rows ? _iheight : _iwidth) ) / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

protected:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float * const _fimg_hpass;
    float * const _fimg_lpass;
    unsigned int const _iwidth;
    unsigned int const _iheight;
    bool const _b3;
    int const _sc;
};

class SmoothRows
    : public ProcessRowsColsBase<true>
{
public:
    SmoothRows(ImageEffect &instance,
               float* fimg_hpass,
               float* fimg_lpass,
               unsigned int iwidth,
               unsigned int iheight,
               bool b3,
               int sc) // 1 << lev
        : ProcessRowsColsBase<true>(instance, fimg_hpass, fimg_lpass, iwidth, iheight, b3, sc)
    {
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int row_begin = 0;
        int row_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iheight, &row_begin, &row_end);
        if (row_end <= row_begin) {
            return;
        }
        std::vector<float> temp(_iwidth);
        for (int row = row_begin; row < row_end; ++row) {
            if ( _effect.abort() ) {
                return;
            }
            hat_transform (&temp[0], _fimg_hpass + row * _iwidth, 1, _iwidth, _b3, _sc);
            for (unsigned int col = 0; col < _iwidth; ++col) {
                unsigned int i = row * _iwidth + col;
                _fimg_lpass[i] = temp[col];
            }
        }
    }
};

class SmoothColsSumSq
    : public ProcessRowsColsBase<false>
{
public:
    SmoothColsSumSq(ImageEffect &instance,
                    float* fimg_hpass,
                    float* fimg_lpass,
                    unsigned int iwidth,
                    unsigned int iheight,
                    bool b3,
                    int sc, // 1 << lev
                    double* sumsq)
        : ProcessRowsColsBase<false>(instance, fimg_hpass, fimg_lpass, iwidth, iheight, b3, sc)
        , _sumsq(sumsq)
    {
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int col_begin = 0;
        int col_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iwidth, &col_begin, &col_end);
        if (col_end <= col_begin) {
            return;
        }
        std::vector<float> temp(_iheight);
        for (int col = col_begin; col < col_end; ++col) {
            if ( _effect.abort() ) {
                return;
            }
            hat_transform (&temp[0], _fimg_lpass + col, _iwidth, _iheight, _b3, _sc);
            double sumsqrow = 0.;
            for (unsigned int row = 0; row < _iheight; ++row) {
                unsigned int i = row * _iwidth + col;
                _fimg_lpass[i] = temp[row];
                // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                _fimg_hpass[i] -= _fimg_lpass[i];
                sumsqrow += _fimg_hpass[i] * _fimg_hpass[i];
            }
            {
                AutoMutex l(&_sumsq_mutex);
                *_sumsq += sumsqrow;
            }
        }
    }

    Mutex _sumsq_mutex;
    double *_sumsq;
};


class SmoothCols
    : public ProcessRowsColsBase<false>
{
public:
    SmoothCols(ImageEffect &instance,
               float* fimg_hpass,
               float* fimg_lpass,
               unsigned int iwidth,
               unsigned int iheight,
               bool b3,
               int sc) // 1 << lev
        : ProcessRowsColsBase<false>(instance, fimg_hpass, fimg_lpass, iwidth, iheight, b3, sc)
    {
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int col_begin = 0;
        int col_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iwidth, &col_begin, &col_end);
        if (col_end <= col_begin) {
            return;
        }
        std::vector<float> temp(_iheight);
        for (int col = col_begin; col < col_end; ++col) {
            if ( _effect.abort() ) {
                return;
            }
            hat_transform (&temp[0], _fimg_lpass + col, _iwidth, _iheight, _b3, _sc);
            for (unsigned int row = 0; row < _iheight; ++row) {
                unsigned int i = row * _iwidth + col;
                _fimg_lpass[i] = temp[row];
                // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                _fimg_hpass[i] -= _fimg_lpass[i];
            }
        }
    }
};

class ApplyThreshold
    : public MultiThread::Processor
{
public:
    ApplyThreshold(ImageEffect &instance,
                   float* fimg_hpass,
                   float* fimg_0,
                   unsigned int size,
                   float thold,
                   double denoise_amount,
                   double beta)
        : _effect(instance)
        , _fimg_hpass(fimg_hpass)
        , _fimg_0(fimg_0)
        , _size(size)
        , _thold(thold)
        , _denoise_amount(denoise_amount)
        , _beta(beta)
    {
        assert(_fimg_hpass && _size > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU
        unsigned int nCPUs = _size / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int i_begin = 0;
        int i_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _size, &i_begin, &i_end);
        if (i_end <= i_begin) {
            return;
        }
        if ( _effect.abort() ) {
            return;
        }
        for (int i = i_begin; i < i_end; ++i) {
            float fimg_denoised = _fimg_hpass[i];

            // apply smooth threshold
            if (_fimg_hpass[i] < -_thold) {
                _fimg_hpass[i] += _thold * _denoise_amount;
                fimg_denoised += _thold;
            } else if (_fimg_hpass[i] >  _thold) {
                _fimg_hpass[i] -= _thold * _denoise_amount;
                fimg_denoised -= _thold;
            } else {
                _fimg_hpass[i] *= 1. - _denoise_amount;
                fimg_denoised = 0.;
            }
            // add the denoised band to the final image
            if (_fimg_0) { // if (hpass != 0)
                // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5)

                _fimg_0[i] += _fimg_hpass[i] + _beta * fimg_denoised;
            }
        }
    }

private:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float * const _fimg_hpass;
    float * const _fimg_0;
    unsigned int const _size;
    float const _thold;
    double const _denoise_amount;
    double const _beta;
};


class AddLowPass
    : public MultiThread::Processor
{
public:
    AddLowPass(ImageEffect &instance,
               float* fimg_0,
               float* fimg_lpass,
               unsigned int size)
        : _effect(instance)
        , _fimg_0(fimg_0)
        , _fimg_lpass(fimg_lpass)
        , _size(size)
    {
        assert(_fimg_0 && _fimg_lpass && _size > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU
        unsigned int nCPUs = _size / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int i_begin = 0;
        int i_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _size, &i_begin, &i_end);
        if (i_end <= i_begin) {
            return;
        }
        if ( _effect.abort() ) {
            return;
        }
        for (int i = i_begin; i < i_end; ++i) {
            _fimg_0[i] += _fimg_lpass[i];
        }
    }

private:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float * const _fimg_0;
    float * const _fimg_lpass;
    unsigned int const _size;
};


// integral images computation

class IntegralRows
    : public MultiThread::Processor
{
public:
    IntegralRows(ImageEffect &instance,
                 float const* fimg, // img
                 //float* fimgsumrow, // sum along rows
                 float* fimgsumsqrow, // sum along rows
                 unsigned int iwidth,
                 unsigned int iheight) // 1 << lev
        : _effect(instance)
        , _fimg(fimg)
        //, _fimgsumrow(fimgsumrow)
        , _fimgsumsqrow(fimgsumsqrow)
        , _iwidth(iwidth)
        , _iheight(iheight)
    {
        assert(_fimg && /*_fimgsumrow &&*/ _fimgsumsqrow && _iwidth > 0 && _iheight > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU and at least 1 line par CPU
        unsigned int nCPUs = ( std::min(_iwidth, 4096u) * _iheight ) / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int row_begin = 0;
        int row_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iheight, &row_begin, &row_end);
        if (row_end <= row_begin) {
            return;
        }
        for (int row = row_begin; row < row_end; ++row) {
            if ( _effect.abort() ) {
                return;
            }
            //float prev = 0.;
            float prevsq = 0.;
            for (unsigned int col = 0; col < _iwidth; ++col) {
                unsigned int i = row * _iwidth + col;
                //prev += _fimg[i];
                //_fimgsumrow[i] = prev;
                prevsq += _fimg[i] * _fimg[i];
                _fimgsumsqrow[i] = prevsq;
            }
        }
    }

private:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float const * const _fimg;
    //float * const _fimgsumrow;
    float * const _fimgsumsqrow;
    unsigned int const _iwidth;
    unsigned int const _iheight;
};

class IntegralCols
    : public MultiThread::Processor
{
public:
    IntegralCols(ImageEffect &instance,
                 float const* fimgsumrow, // sum along rows
                 float* fimgsum, // integral image
                 unsigned int iwidth,
                 unsigned int iheight) // 1 << lev
        : _effect(instance)
        , _fimgsumrow(fimgsumrow)
        , _fimgsum(fimgsum)
        , _iwidth(iwidth)
        , _iheight(iheight)
    {
        assert(_fimgsumrow && _fimgsum && _iwidth > 0 && _iheight > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU and at least 1 column per CPU
        unsigned int nCPUs = ( std::min(_iheight, 4096u) * _iwidth ) / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int col_begin = 0;
        int col_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iwidth, &col_begin, &col_end);
        if (col_end <= col_begin) {
            return;
        }
        for (int col = col_begin; col < col_end; ++col) {
            if ( _effect.abort() ) {
                return;
            }
            float prev = 0.;
            for (unsigned int row = 0; row < _iheight; ++row) {
                unsigned int i = row * _iwidth + col;
                prev += _fimgsumrow[i];
                _fimgsum[i] = prev;
            }
        }
    }

private:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float const * const _fimgsumrow;
    float * const _fimgsum;
    unsigned int const _iwidth;
    unsigned int const _iheight;
};

class ApplyThresholdAdaptive
    : public MultiThread::Processor
{
public:
    ApplyThresholdAdaptive(ImageEffect &instance,
                           float* fimg_hpass,
                           float* fimg_0,
                           float* fimg_sat, // summed area table of squared values
                           unsigned int iwidth,
                           unsigned int iheight,
                           int adaptiveRadiusPixel,
                           double sigma_n_i_sq,
                           double denoise_amount,
                           double beta)
        : _effect(instance)
        , _fimg_hpass(fimg_hpass)
        , _fimg_0(fimg_0)
        , _fimg_sat(fimg_sat)
        , _iwidth(iwidth)
        , _iheight(iheight)
        , _adaptiveRadiusPixel(adaptiveRadiusPixel)
        , _sigma_n_i_sq(sigma_n_i_sq)
        , _denoise_amount(denoise_amount)
        , _beta(beta)
    {
        assert(_fimg_hpass && _fimg_sat && _iwidth > 0 && _iheight > 0);
    }

    /** @brief called to process everything */
    void process(void)
    {
        // make sure there are at least 4096 pixels per CPU and at least 1 line par CPU
        unsigned int nCPUs = ( std::min(_iwidth, 4096u) * _iheight ) / 4096u;

        // make sure the number of CPUs is valid (and use at least 1 CPU)
        nCPUs = std::max( 1u, std::min( nCPUs, MultiThread::getNumCPUs() ) );

        // call the base multi threading code, should put a pre & post thread calls in too
        multiThread(nCPUs);
    }

private:
    /** @brief function that will be called in each thread. ID is from 0..nThreads-1 nThreads are the number of threads it is being run over */
    virtual void multiThreadFunction(unsigned int threadID,
                                     unsigned int nThreads) OVERRIDE FINAL
    {
        int row_begin = 0;
        int row_end = 0;

        MultiThread::getThreadRange(threadID, nThreads, 0, _iheight, &row_begin, &row_end);
        if (row_end <= row_begin) {
            return;
        }
        for (int row = row_begin; row < row_end; ++row) {
            if ( _effect.abort() ) {
                return;
            }
            // summed area table (sat) rows
            int row_sat_up = std::max(row - 1 - _adaptiveRadiusPixel, -1);
            int row_sat_down = std::min(row + _adaptiveRadiusPixel, (int)_iheight - 1);
            int row_sat_size = row_sat_down - row_sat_up;
            for (unsigned int col = 0; col < _iwidth; ++col) {
                int col_sat_left = std::max( (int)col - 1 - _adaptiveRadiusPixel, -1 );
                int col_sat_right = std::min( (int)col + _adaptiveRadiusPixel, (int)_iwidth - 1 );
                int col_sat_size = col_sat_right - col_sat_left;
                double sumsq = ( _fimg_sat[row_sat_down * _iwidth + col_sat_right]
                                 - (row_sat_up >= 0 ? _fimg_sat[row_sat_up * _iwidth + col_sat_right] : 0.)
                                 - (col_sat_left >= 0 ? _fimg_sat[row_sat_down * _iwidth + col_sat_left] : 0.)
                                 + ( (row_sat_up >= 0 && col_sat_left >= 0) ? _fimg_sat[row_sat_up * _iwidth + col_sat_left] : 0. ) );
                int sumsqsize = row_sat_size * col_sat_size;
                unsigned int i = row * _iwidth + col;
                float fimg_denoised = _fimg_hpass[i];

                // apply smooth threshold
                float thold = _sigma_n_i_sq / std::sqrt( std::max(1e-30, sumsq / sumsqsize - _sigma_n_i_sq) );

                if (_fimg_hpass[i] < -thold) {
                    _fimg_hpass[i] += thold * _denoise_amount;
                    fimg_denoised += thold;
                } else if (_fimg_hpass[i] >  thold) {
                    _fimg_hpass[i] -= thold * _denoise_amount;
                    fimg_denoised -= thold;
                } else {
                    _fimg_hpass[i] *= 1. - _denoise_amount;
                    fimg_denoised = 0.;
                }
                // add the denoised band to the final image
                if (_fimg_0) { // if (hpass != 0)
                    // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                    // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5)

                    _fimg_0[i] += _fimg_hpass[i] + _beta * fimg_denoised;
                }
            }
        }
    } // multiThreadFunction

private:
    ImageEffect &_effect;      /**< @brief effect to render with */
    float * const _fimg_hpass;
    float * const _fimg_0;
    float * const _fimg_sat;
    unsigned int const _iwidth;
    unsigned int const _iheight;
    int _adaptiveRadiusPixel;
    double _sigma_n_i_sq;
    double const _denoise_amount;
    double const _beta;
};


#endif // ifdef kUseMultithread


// "A trous" algorithm with a linear interpolation filter.
// from dcraw/UFRaw/LibRaw, with enhancements from GIMP wavelet denoise
// https://sourceforge.net/p/ufraw/mailman/message/24069162/
void
DenoiseSharpenPlugin::wavelet_denoise(float *fimg[4], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] and fimg[2] are working space images of the same size, fimg[3] is a working image of the same size used when adaptiveRadius > 0
                                      unsigned int iwidth, //!< width of the image
                                      unsigned int iheight, //!< height of the image
                                      bool b3,
                                      const double noiselevels[4], //!< noise levels for high/medium/low/very low frequencies
                                      int adaptiveRadius,
                                      double denoise_amount, //!< amount parameter
                                      double sharpen_amount, //!< constrast boost amount
                                      double sharpen_radius, //!< contrast boost radius
                                      int startLevel,
                                      float a, // progress amount at start
                                      float b) // progress increment
{
    //
    // BayesShrink (as describred in <https://jo.dreggn.org/home/2011_atrous.pdf>):
    // compute sigma_n using the MAD (median absolute deviation at the finest level:
    // sigma_n = median(|d_0|)/0.6745 (could be computed in an analysis step from the first detail subband)
    // The soft shrinkage threshold is
    // T = \sigma_{n,i}^2 / \sqrt{max(0,\sigma_{y,i}^2 - \sigma_{n,i}^2)}
    // with
    // \sigma_{y,i}^2 = 1/N \sum{p} d_i(p)^2 (standard deviation of the signal with the noise for this detail subband)
    // \sigma_{n,i} = \sigma_n . 2^{-i} (standard deviation of the noise)
    //
    // S. G. Chang, Bin Yu and M. Vetterli, "Adaptive wavelet thresholding for image denoising and compression," in IEEE Transactions on Image Processing, vol. 9, no. 9, pp. 1532-1546, Sep 2000. doi: 10.1109/83.862633
    // http://www.csee.wvu.edu/~xinl/courses/ee565/TIP2000.pdf


    // a single channel of the original image is in fimg[0],
    // with intensities between 0 and 1.
    // The channel is from either gamma-compressed R'G'B', CIE L*a*b,
    // or gamma-compressed Y'CbCr.
    // fimg[1] and fimg[2] are used as temporary images.
    // at each outer iteration (for lev= 0 to 4):
    // 1. the hpass image (initialy the image itself) is smoothed using
    //    hat_transform, and the result is put in image lpass.
    // 2. image lpass is subtracted from hpass, so that lpass contains
    //    a smoothed image, and hpass contains the details at level lev
    //    (which may be positive r negative). The original image is
    //    destroyed, but can be reconstructed as hpass+lpass.
    // 3. a threshold thold is computed as:
    //    5.0 / (1 << 6) * exp (-2.6 * sqrt (lev + 1)) * 0.8002 / exp (-2.6)
    //    This a priori noise threshold is only used to exclude noisy values
    //    from the statistics computed hereafter.
    // 4. the standard deviation of pixels of hpass that are outside of
    //    [-thold,thold] is computed, classified by the intensity range of the
    //    smoothed image lpass:
    //    0: ]-oo,0.2], 1: ]0.2,0.4], 2: ]0.4,0.6], 3: ]0.6,0.8], 4: ]0.8,+oo[
    //    This means that is a given pixel of lpass is between 0.6 and 0.8,
    //    the corresponding pixel of hpass is added to the statitics for sdev[3].
    // 5. for each pixel of hpass, a threshold is computed, depending on the
    //    intensity range r of lpass as: thold = threshold*sdev[r]
    //    (threshold is an external parameter between 0 and 10).
    //    Depending on the value of this pixel:
    //    - between -thold and thold, it is multiplied by low (low is the
    //      softwess parameter between 0. and 1. which drives how much noise
    //      should be kept).
    //    - below -thold , thold*(1-low) is added
    //    - above thold , thold*(1-low) is subtracted
    // 6. if lev is more than 0, hpass is added to the level-0
    //    high-pass image fimg[0]
    // 7. hpass is then set to the smoothed (lpass image), and lpass is set to
    //    the other temporary image.

    int maxLevel = kLevelMax - startLevel;

    if (maxLevel < 0) {
        return;
    }

    if ( ( ( (noiselevels[0] <= 0.) && (noiselevels[1] <= 0.) && (noiselevels[2] <= 0.) && (noiselevels[3] <= 0.) ) || (denoise_amount <= 0.) ) && (sharpen_amount <= 0.) ) {
        return;
    }

    const unsigned int size = iheight * iwidth;
    int hpass = 0;
    int lpass;
    for (int lev = 0; lev <= maxLevel; lev++) {
        abort_test();
        if (b != 0) {
            progressUpdateRender( a + b * lev / (maxLevel + 1.) );
        }
        lpass = ( (lev & 1) + 1 );

        // smooth fimg[hpass], result is in fimg[lpass]:
        // a- smooth rows, result is in fimg[lpass]
#ifdef kUseMultithread
        {
            SmoothRows processor(*this, fimg[hpass], fimg[lpass], iwidth, iheight, b3, 1 << lev);
            processor.process();
        }
#else
        // SmoothRows
#       ifdef _OPENMP
#       pragma omp parallel for
#       endif
        for (unsigned int row = 0; row < iheight; ++row) {
            abort_test_loop();
            float* temp = new float[iwidth];
            hat_transform (temp, fimg[hpass] + row * iwidth, 1, iwidth, b3, 1 << lev);
            for (unsigned int col = 0; col < iwidth; ++col) {
                unsigned int i = row * iwidth + col;
                fimg[lpass][i] = temp[col];
            }
            delete [] temp;
        }
#endif
        abort_test();
        if (b != 0) {
            progressUpdateRender( a + b * (lev + 0.25) / (maxLevel + 1.) );
        }

        // b- smooth cols, result is in fimg[lpass]
        // compute HHi + its variance
        double sumsq = 0.;
        unsigned int sumsqsize = 0;
#ifdef kUseMultithread
        if (adaptiveRadius <= 0) {
            SmoothColsSumSq processor(*this, fimg[hpass], fimg[lpass], iwidth, iheight, b3, 1 << lev, &sumsq);
            processor.process();
            sumsqsize = size;
        } else {
            SmoothCols processor(*this, fimg[hpass], fimg[lpass], iwidth, iheight, b3, 1 << lev);
            processor.process();
        }
#else // !kUseMultithread
        if (adaptiveRadius <= 0) {
            // SmoothColsSumSq
#           ifdef _OPENMP
#           pragma omp parallel for reduction (+:sumsq)
#           endif
            for (unsigned int col = 0; col < iwidth; ++col) {
                abort_test_loop();
                float* temp = new float[iheight];
                hat_transform (temp, fimg[lpass] + col, iwidth, iheight, b3, 1 << lev);
                double sumsqrow = 0.;
                for (unsigned int row = 0; row < iheight; ++row) {
                    unsigned int i = row * iwidth + col;
                    fimg[lpass][i] = temp[row];
                    // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                    fimg[hpass][i] -= fimg[lpass][i];
                    sumsqrow += fimg[hpass][i] * fimg[hpass][i];
                }
                sumsq += sumsqrow;
                delete [] temp;
            }
            sumsqsize = size;
        } else {
            // SmoothCols
#           ifdef _OPENMP
#           pragma omp parallel for reduction (+:sumsq)
#           endif
            for (unsigned int col = 0; col < iwidth; ++col) {
                abort_test_loop();
                float* temp = new float[iheight];
                hat_transform (temp, fimg[lpass] + col, iwidth, iheight, b3, 1 << lev);
                for (unsigned int row = 0; row < iheight; ++row) {
                    unsigned int i = row * iwidth + col;
                    fimg[lpass][i] = temp[row];
                    // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                    fimg[hpass][i] -= fimg[lpass][i];
                }
                delete [] temp;
            }
        }
#endif // !kUseMultithread
        abort_test();
        if (b != 0) {
            progressUpdateRender( a + b * (lev + 0.5) / (maxLevel + 1.) );
        }


        // threshold
        // The soft shrinkage threshold is
        // T = \sigma_{n,i}^2 / \sqrt{max(0,\sigma_{y,i}^2 - \sigma_{n,i}^2)}
        // with
        // \sigma_{y,i}^2 = 1/N \sum{p} d_i(p)^2 (standard deviation of the signal with the noise for this detail subband)
        // \sigma_{n,i} = \sigma_n . 2^{-i} (standard deviation of the noise)

        // The following corresponds to <https://jo.dreggn.org/home/2011_atrous.pdf>:
        //double sigma_n_i = ( noiselevel * noise[0] / ( 1 << (lev + startLevel) ) );
        // The following uses levels obtained by filtering an actual Gaussian noise:
        double sigma_n_i_sq = 0;
        // sum up the noise from different frequencies
        for (unsigned f = 0; f < 4; ++f) {
            if (lev + startLevel >= (int)f) {
                double k = b3 ? noise_b3[lev + startLevel] : noise[lev + startLevel];
                double sigma_n_i = noiselevels[f] * k;
                sigma_n_i_sq += sigma_n_i * sigma_n_i;
            }
        }

        // uncomment to check the values of the noise[] array
        //printf("width=%u level=%u stdev=%g sigma_n_i=%g\n", iwidth, lev, std::sqrt(sumsq / sumsqsize), std::sqrt(sigma_n_i_sq));

        // sharpen
        double beta = 0.;
        if (sharpen_amount > 0.) {
            beta = sharpen_amount * exp (-( (lev + startLevel) - sharpen_radius ) * ( (lev + startLevel) - sharpen_radius ) / 1.5);
        }

        if (adaptiveRadius <= 0) {
            assert(sumsqsize > 0);
            // use the signal level computed from the whole image
            float thold = sigma_n_i_sq / std::sqrt( std::max(1e-30, sumsq / sumsqsize - sigma_n_i_sq) );

#ifdef kUseMultithread
            {
                ApplyThreshold processor(*this, fimg[hpass], hpass ? fimg[0] : NULL, size, thold, denoise_amount, beta);
                processor.process();
            }
#else
            // ApplyThreshold
#           ifdef _OPENMP
#           pragma omp parallel for
#           endif
            for (unsigned int i = 0; i < size; ++i) {
                float fimg_denoised = fimg[hpass][i];

                // apply smooth threshold
                if (fimg[hpass][i] < -thold) {
                    fimg[hpass][i] += thold * denoise_amount;
                    fimg_denoised += _thold;
                } else if (fimg[hpass][i] >  thold) {
                    fimg[hpass][i] -= thold * denoise_amount;
                    fimg_denoised -= _thold;
                } else {
                    fimg[hpass][i] *= 1. - denoise_amount;
                    fimg_denoised = 0.;
                }
                // add the denoised band to the final image
                if (hpass != 0) {
                    // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                    // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5)

                    fimg[0][i] += fimg[hpass][i] + beta * fimg_denoised;
                }
            }
#endif
        } else { // adaptiveRadius > 0
            // use the local image level
            assert(fimg[3] != NULL);
            int adaptiveRadiusPixel = ( adaptiveRadius + (b3 ? 2 : 1) ) * (1 << lev);
#ifdef kUseMultithread
            {
                IntegralRows processor(*this, fimg[hpass], fimg[3], iwidth, iheight);
                processor.process();
            }
            {
                IntegralCols processor(*this, fimg[3], fimg[3], iwidth, iheight);
                processor.process();
            }

            {
                ApplyThresholdAdaptive processor(*this, fimg[hpass], hpass ? fimg[0] : NULL, fimg[3], iwidth, iheight, adaptiveRadiusPixel, sigma_n_i_sq, denoise_amount, beta);
                processor.process();
            }
#else
            float *fimg_sat = fimg[3];
            // IntegralRows
#           ifdef _OPENMP
#           pragma omp parallel for
#           endif
            for (unsigned int row = 0; row < iheight; ++row) {
                abort_test_loop();
                //float prev = 0.;
                float prevsq = 0.;
                for (unsigned int col = 0; col < iwidth; ++col) {
                    unsigned int i = row * iwidth + col;
                    //prev += fimg[hpass];
                    //_fimgsumrow[i] = prev;
                    prevsq += fimg[hpass][i] * fimg[hpass][i];
                    fimg_sat[i] = prevsq;
                }
            }
            // IntegralCols
#           ifdef _OPENMP
#           pragma omp parallel for
#           endif
            for (unsigned int col = 0; col < iwidth; ++col) {
                abort_test_loop();
                float prev = 0.;
                for (unsigned int row = 0; row < iheight; ++row) {
                    unsigned int i = row * iwidth + col;
                    prev += fimg_sat[i];
                    fimg_sat[i] = prev;
                }
            }
            // ApplyThresholdAdaptive
#           ifdef _OPENMP
#           pragma omp parallel for
#           endif
            for (unsigned int row = 0; row < iheight; ++row) {
                abort_test_loop();
                // summed area table (sat) rows
                int row_sat_up = std::max( (int)row - 1 - adaptiveRadiusPixel, -1 );
                int row_sat_down = std::min( (int)row + adaptiveRadiusPixel, (int)iheight - 1 );
                int row_sat_size = row_sat_down - row_sat_up;
                for (unsigned int col = 0; col < iwidth; ++col) {
                    int col_sat_left = std::max( (int)col - 1 - adaptiveRadiusPixel, -1 );
                    int col_sat_right = std::min( (int)col + adaptiveRadiusPixel, (int)iwidth - 1 );
                    int col_sat_size = col_sat_right - col_sat_left;
                    double sumsq = ( fimg_sat[row_sat_down * iwidth + col_sat_right]
                                     - (row_sat_up >= 0 ? fimg_sat[row_sat_up * iwidth + col_sat_right] : 0.)
                                     - (col_sat_left >= 0 ? fimg_sat[row_sat_down * iwidth + col_sat_left] : 0.)
                                     + ( (row_sat_up >= 0 && col_sat_left >= 0) ? fimg_sat[row_sat_up * iwidth + col_sat_left] : 0. ) );
                    int sumsqsize = row_sat_size * col_sat_size;
                    unsigned int i = row * iwidth + col;
                    float fimg_denoised = fimg[hpass][i];

                    // apply smooth threshold
                    float thold = sigma_n_i_sq / std::sqrt( std::max(1e-30, sumsq / sumsqsize - sigma_n_i_sq) );

                    if (fimg[hpass][i] < -thold) {
                        fimg[hpass][i] += thold * denoise_amount;
                        fimg_denoised += thold;
                    } else if (fimg[hpass][i] >  thold) {
                        fimg[hpass][i] -= thold * denoise_amount;
                        fimg_denoised -= thold;
                    } else {
                        fimg[hpass][i] *= 1. - denoise_amount;
                        fimg_denoised = 0.;
                    }
                    // add the denoised band to the final image
                    if (hpass != 0) {
                        // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                        // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5)

                        fimg[0][i] += fimg[hpass][i] + beta * fimg_denoised;
                    }
                }
            }
#endif // ifdef kUseMultithread
        }
        hpass = lpass;
    } // for(lev)

    abort_test();
    // add the last smoothed image to the image
#ifdef kUseMultithread
    {
        AddLowPass processor(*this, fimg[0], fimg[lpass], size);
        processor.process();
    }
#else
#   ifdef _OPENMP
#   pragma omp parallel for
#   endif
    for (unsigned int i = 0; i < size; ++i) {
        fimg[0][i] += fimg[lpass][i];
    }
#endif
} // wavelet_denoise

void
DenoiseSharpenPlugin::sigma_mad(float *fimg[4], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1-3] are working space images of the same size
                                bool *bimgmask,
                                unsigned int iwidth, //!< width of the image
                                unsigned int iheight, //!< height of the image
                                bool b3,
                                double noiselevels[4], //!< output: the sigma for each frequency
                                float a, // progress amount at start
                                float b) // progress increment
{
    // compute sigma_n using the MAD (median absolute deviation at the finest level:
    // sigma_n = median(|d_0|)/0.6745 (could be computed in an analysis step from the first detail subband)

    const unsigned int size = iheight * iwidth;
    const int maxLevel = 3;
    double noiselevel_prev_fullres = 0.;
    int hpass = 0;
    int lpass;

    for (int lev = 0; lev <= maxLevel; lev++) {
        abort_test();
        if (b != 0) {
            progressUpdateAnalysis( a + b * lev / (maxLevel + 1.) );
        }
        lpass = ( (lev & 1) + 1 );

        // smooth fimg[hpass], result is in fimg[lpass]:
        // a- smooth rows, result is in fimg[lpass]
#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (unsigned int row = 0; row < iheight; ++row) {
            float* temp = new float[iwidth];
            abort_test_loop();
            hat_transform (temp, fimg[hpass] + row * iwidth, 1, iwidth, b3, 1 << lev);
            for (unsigned int col = 0; col < iwidth; ++col) {
                unsigned int i = row * iwidth + col;
                fimg[lpass][i] = temp[col];
            }
            delete [] temp;
        }
        abort_test();
        if (b != 0) {
            progressUpdateAnalysis( a + b * (lev + 0.25) / (maxLevel + 1.) );
        }

        // b- smooth cols, result is in fimg[lpass]
        // compute HHlev
#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (unsigned int col = 0; col < iwidth; ++col) {
            float* temp = new float[iheight];
            abort_test_loop();
            hat_transform (temp, fimg[lpass] + col, iwidth, iheight, b3, 1 << lev);
            for (unsigned int row = 0; row < iheight; ++row) {
                unsigned int i = row * iwidth + col;
                fimg[lpass][i] = temp[row];
                // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                fimg[hpass][i] -= fimg[lpass][i];
            }
            delete [] temp;
        }
        abort_test();
        if (b != 0) {
            progressUpdateAnalysis( a + b * (lev + 0.5) / (maxLevel + 1.) );
        }
        // take the absolute value to compute MAD, and extract points within the mask
        unsigned int n;
        if (bimgmask) {
            n = 0;
            for (unsigned int i = 0; i < size; ++i) {
                if (bimgmask[i]) {
                    fimg[3][n] = std::abs(fimg[hpass][i]);
                    ++n;
                }
            }
        } else {
            for (unsigned int i = 0; i < size; ++i) {
                fimg[3][i] = std::abs(fimg[hpass][i]);
            }
            n = size;
        }
        abort_test();
        if (n != 0) {
            std::nth_element(&fimg[3][0], &fimg[3][n / 2], &fimg[3][n]);
        }

        double sigma_this = (n == 0) ? 0. : fimg[3][n / 2] / 0.6745;
        // compute the sigma at image resolution
        double k = b3 ? noise_b3[lev] : noise[lev];
        double sigma_fullres = sigma_this / k;
        if (noiselevel_prev_fullres <= 0.) {
            noiselevels[lev] = sigma_fullres;
            noiselevel_prev_fullres = sigma_fullres;
        } else if (sigma_fullres > noiselevel_prev_fullres) {
            // subtract the contribution from previous levels
            noiselevels[lev] = std::sqrt(sigma_fullres * sigma_fullres - noiselevel_prev_fullres * noiselevel_prev_fullres);
            noiselevel_prev_fullres = sigma_fullres;
        } else {
            noiselevels[lev] = 0.;
            // cumulated noiselevel is unchanged
            //noiselevel_prev_fullres = noiselevel_prev_fullres;
        }
        hpass = lpass;
    }
} // DenoiseSharpenPlugin::sigma_mad

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from
// the overridden render function
void
DenoiseSharpenPlugin::render(const RenderArguments &args)
{
#ifdef _OPENMP
    // set the number of OpenMP threads to a reasonable value
    // (but remember that the OpenMP threads are not counted my the multithread suite)
    omp_set_num_threads( std::max(1u, MultiThread::getNumCPUs() ) );
#endif
    DBG(cout << "render! with " << MultiThread::getNumCPUs() << " CPUs\n");

    progressStartRender(kPluginName " (render)");

    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB /*|| dstComponents == ePixelComponentXY*/ || dstComponents == ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
    case ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    //case ePixelComponentXY:
    //    renderForComponents<2>(args);
    //    break;
    case ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
    default:
        DBG(std::cout << "components usupported\n");
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
    progressEndRender();

    DBG(cout << "render! OK\n");
}

template<int nComponents>
void
DenoiseSharpenPlugin::renderForComponents(const RenderArguments &args)
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
        DBG(cout << "depth usupported\n");
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

static int
borderSize(int adaptiveRadius,
           bool b3,
           int nlevels)
{
    // hat_transform gets the pixel at x+-(1<<maxLev), which is computex from x+-(1<<(maxLev-1)), etc...

    // We thus need pixels at x +- (1<<(maxLev+1))-1
    return ( adaptiveRadius + (b3 ? 2 : 1) ) * (1 << nlevels) - 1;
}

void
DenoiseSharpenPlugin::setup(const RenderArguments &args,
                            auto_ptr<const Image>& src,
                            auto_ptr<Image>& dst,
                            auto_ptr<const Image>& mask,
                            Params& p)
{
    const double time = args.time;

    dst.reset( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    src.reset( ( _srcClip && _srcClip->isConnected() ) ?
               _srcClip->fetchImage(time) : 0 );
    if ( !src.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
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
    p.doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    mask.reset(p.doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    clearPersistentMessage();

    p.maskInvert = false;
    if (p.doMasking) {
        _maskInvert->getValueAtTime(time, p.maskInvert);
    }

    // fetch parameter values
    p.analysisLock = _analysisLock->getValueAtTime(time);
    if ( !p.analysisLock ) {
        // all we have to do is copy pixels
        DBG(cout << "render called although analysis not locked and isidentity=true\n");
        copyPixels(*this,
                   args.renderWindow,
                   src.get(),
                   dst.get());
        return;

    }
    p.premult = _premult->getValueAtTime(time);
    p.premultChannel = _premultChannel->getValueAtTime(time);
    p.mix = _mix->getValueAtTime(time);

    p.process[0] = _processR->getValueAtTime(time);
    p.process[1] = _processG->getValueAtTime(time);
    p.process[2] = _processB->getValueAtTime(time);
    p.process[3] = _processA->getValueAtTime(time);

    p.outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    p.colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    p.b3 = _b3->getValueAtTime(time);
    p.startLevel = startLevelFromRenderScale(args.renderScale);
    p.adaptiveRadius = _adaptiveRadius->getValueAtTime(time);

    double noiseLevelGain = _noiseLevelGain->getValueAtTime(time);
    double gainFreq[4];
    for (unsigned int f = 0; f < 4; ++f) {
        if ( _enableFreq[f]->getValueAtTime(time) ) {
            gainFreq[f] = noiseLevelGain * _gainFreq[f]->getValueAtTime(time);
        } else {
            gainFreq[f] = 0;
        }
    }

    double denoiseAmount = _denoiseAmount->getValueAtTime(time);
    for (unsigned int c = 0; c < 4; ++c) {
        double channelGain = _channelGain[c]->getValueAtTime(time);
        for (unsigned int f = 0; f < 4; ++f) {
            p.noiseLevel[c][f] = channelGain * gainFreq[f] * _noiseLevel[c][f]->getValueAtTime(time);
        }
        p.denoise_amount[c] = (p.outputMode == eOutputModeSharpen) ? 0. : denoiseAmount * _amount[c]->getValueAtTime(time);
    }
    p.sharpen_amount[0] = (p.outputMode == eOutputModeNoise) ? 0. : _sharpenAmount->getValueAtTime(time);
    double sharpenSize = _sharpenSize->getValueAtTime(time);
    // The GIMP's wavelet sharpen uses a sharpen radius parameter which is counter-intuitive
    // and points to a level number. We convert from the Sharpen Size (simililar to the size in the
    // Laplacian or Sharpen plugins) to the radius using the following heuristic formula (radius=0 seems to correspond to size=8)
    p.sharpen_radius = std::log(sharpenSize) / M_LN2 - 3.; // log(8)/log(2) = 3.
    bool sharpenLuminance = _sharpenLuminance->getValueAtTime(time);

    if (!sharpenLuminance) {
        p.sharpen_amount[1] = p.sharpen_amount[2] = p.sharpen_amount[3] = p.sharpen_amount[0];
    } else if ( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) {
        p.sharpen_amount[1] = p.sharpen_amount[2] = p.sharpen_amount[0]; // cannot sharpen luminance only
    }

    if ( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) {
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && ( ((p.noiseLevel[c][0] > 0 ||
                                               p.noiseLevel[c][1] > 0 ||
                                               p.noiseLevel[c][2] > 0 ||
                                               p.noiseLevel[c][3] > 0) && p.denoise_amount[c] > 0.) || p.sharpen_amount[c] > 0. );
        }
    } else {
        bool processcolor = false;
        for (int c = 0; c < 3; ++c) {
            processcolor = processcolor || ( ((p.noiseLevel[c][0] > 0 ||
                                               p.noiseLevel[c][1] > 0 ||
                                               p.noiseLevel[c][2] > 0 ||
                                               p.noiseLevel[c][3] > 0) && p.denoise_amount[c] > 0.) || p.sharpen_amount[c] > 0. );
        }
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && processcolor;
        }
    }
    p.process[3] = p.process[3] && ( ((p.noiseLevel[3][0] > 0 ||
                                       p.noiseLevel[3][1] > 0 ||
                                       p.noiseLevel[3][2] > 0 ||
                                       p.noiseLevel[3][3] > 0) && p.denoise_amount[3] > 0.) || p.sharpen_amount[3] > 0. );

    // compute the number of levels (max is 4, which adds 1<<4 = 16 pixels on each side)
    int maxLev = std::max( 0, kLevelMax - startLevelFromRenderScale(args.renderScale) );
    int border = borderSize(p.adaptiveRadius, p.b3, maxLev + 1);
    p.srcWindow.x1 = args.renderWindow.x1 - border;
    p.srcWindow.y1 = args.renderWindow.y1 - border;
    p.srcWindow.x2 = args.renderWindow.x2 + border;
    p.srcWindow.y2 = args.renderWindow.y2 + border;

    // intersect with srcBounds
    bool nonempty = Coords::rectIntersection(p.srcWindow, src->getBounds(), &p.srcWindow);
    unused(nonempty);
} // DenoiseSharpenPlugin::setup

template <class PIX, int nComponents, int maxValue>
void
DenoiseSharpenPlugin::renderForBitDepth(const RenderArguments &args)
{
    auto_ptr<const Image> src;
    auto_ptr<Image> dst;
    auto_ptr<const Image> mask;
    Params p;

    setup(args, src, dst, mask, p);
    if ( !p.analysisLock ) {
        // we copied pixels to dst already
        return;
    }

    const OfxRectI& procWindow = args.renderWindow;


    // temporary buffers: one for each channel plus 2 for processing
    unsigned int iwidth = p.srcWindow.x2 - p.srcWindow.x1;
    unsigned int iheight = p.srcWindow.y2 - p.srcWindow.y1;
    unsigned int isize = iwidth * iheight;
    auto_ptr<ImageMemory> tmpData( new ImageMemory(sizeof(float) * isize * ( nComponents + 2 + ( (p.adaptiveRadius > 0) ? 1 : 0 ) ), this) );
    float* tmpPixelData = tmpData.get() ? (float*)tmpData->lock() : NULL;
    float* fimgcolor[3] = { NULL, NULL, NULL };
    float* fimgalpha = NULL;
    float *fimgtmp[3] = { NULL, NULL, NULL };
    fimgcolor[0] = (nComponents != 1) ? tmpPixelData : NULL;
    fimgcolor[1] = (nComponents != 1) ? tmpPixelData + isize : NULL;
    fimgcolor[2] = (nComponents != 1) ? tmpPixelData + 2 * isize : NULL;
    fimgalpha = (nComponents == 1) ? tmpPixelData : ( (nComponents == 4) ? tmpPixelData + 3 * isize : NULL );
    fimgtmp[0] = tmpPixelData + nComponents * isize;
    fimgtmp[1] = tmpPixelData + (nComponents + 1) * isize;
    if (p.adaptiveRadius > 0) {
        fimgtmp[2] = tmpPixelData + (nComponents + 2) * isize;
    }
    // - extract the color components and convert them to the appropriate color model
    //
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int y = p.srcWindow.y1; y < p.srcWindow.y2; y++) {
        abort_test_loop();

        for (int x = p.srcWindow.x1; x < p.srcWindow.x2; x++) {
            const PIX *srcPix = (const PIX *)  (src.get() ? src->getPixelAddress(x, y) : 0);
            float unpPix[4] = {0., 0., 0., 0.};
            ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, p.premult, p.premultChannel);
            unsigned int pix = (x - p.srcWindow.x1) + (y - p.srcWindow.y1) * iwidth;
            // convert to the appropriate color model and store in tmpPixelData
            if ( (nComponents != 1) && (p.process[0] || p.process[1] || p.process[2]) ) {
                if (p.colorModel == eColorModelLab) {
                    if (sizeof(PIX) == 1) {
                        // convert to linear
                        for (int c = 0; c < 3; ++c) {
                            unpPix[c] = _lut->fromColorSpaceFloatToLinearFloat(unpPix[c]);
                        }
                    }
                    Color::rgb709_to_lab(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                    // bring each component in the 0..1 range
                    //unpPix[0] = unpPix[0] / 116.0 + 0 * 16 * 27 / 24389.0;
                    //unpPix[1] = unpPix[1] / 500.0 / 2.0 + 0.5;
                    //unpPix[2] = unpPix[2] / 200.0 / 2.2 + 0.5;
                } else {
                    if (p.colorModel != eColorModelLinearRGB) {
                        if (sizeof(PIX) != 1) {
                            // convert to rec709
                            for (int c = 0; c < 3; ++c) {
                                unpPix[c] = _lut->toColorSpaceFloatFromLinearFloat(unpPix[c]);
                            }
                        }
                    }

                    if (p.colorModel == eColorModelYCbCr) {
                        Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                        // bring to the 0-1 range
                        //unpPix[1] += 0.5;
                        //unpPix[2] += 0.5;
                    }
                }
                // store in tmpPixelData
                for (int c = 0; c < 3; ++c) {
                    if (!( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) || p.process[c]) {
                        fimgcolor[c][pix] = unpPix[c];
                    }
                }
            }
            if (nComponents != 3) {
                assert(fimgalpha);
                fimgalpha[pix] = unpPix[3];
            }
        }
    }

    // denoise

    if ( (nComponents != 1) && (p.process[0] || p.process[1] || p.process[2]) ) {
        // process color channels
        for (int c = 0; c < 3; ++c) {
            if (!( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) || p.process[c]) {
                assert(fimgcolor[c]);
                float* fimg[4] = { fimgcolor[c], fimgtmp[0], fimgtmp[1], (p.adaptiveRadius > 0) ? fimgtmp[2] : NULL};
                abort_test();
                wavelet_denoise(fimg, iwidth, iheight, p.b3, p.noiseLevel[c], p.adaptiveRadius, p.denoise_amount[c], p.sharpen_amount[c], p.sharpen_radius, p.startLevel, (float)c / nComponents, 1.f / nComponents);
            }
        }
    }
    if ( (nComponents != 3) && p.process[3] ) {
        assert(fimgalpha);
        // process alpha
        float* fimg[4] = { fimgalpha, fimgtmp[0], fimgtmp[1], (p.adaptiveRadius > 0) ? fimgtmp[2] : NULL };
        abort_test();
        wavelet_denoise(fimg, iwidth, iheight, p.b3, p.noiseLevel[3], p.adaptiveRadius, p.denoise_amount[3], p.sharpen_amount[3], p.sharpen_radius, p.startLevel, (float)(nComponents - 1) / nComponents, 1.f / nComponents);
    }

    // store back into the result

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int y = procWindow.y1; y < procWindow.y2; y++) {
        abort_test_loop();

        PIX *dstPix = (PIX *) dst->getPixelAddress(procWindow.x1, y);
        for (int x = procWindow.x1; x < procWindow.x2; x++) {
            const PIX *srcPix = (const PIX *)  (src.get() ? src->getPixelAddress(x, y) : 0);
            unsigned int pix = (x - p.srcWindow.x1) + (y - p.srcWindow.y1) * iwidth;
            float tmpPix[4] = {0., 0., 0., 1.};
            // get values from tmpPixelData
            if (nComponents != 3) {
                assert(fimgalpha);
                tmpPix[3] = fimgalpha[pix];
            }
            if (nComponents != 1) {
                // store in tmpPixelData
                for (int c = 0; c < 3; ++c) {
                    tmpPix[c] = fimgcolor[c][pix];
                }

                if (p.colorModel == eColorModelLab) {
                    // back from 0..1 range to normal Lab
                    //tmpPix[0] = (tmpPix[0] - 0 * 16 * 27 / 24389.0) * 116;
                    //tmpPix[1] = (tmpPix[1] - 0.5) * 500 * 2;
                    //tmpPix[2] = (tmpPix[2] - 0.5) * 200 * 2.2;

                    Color::lab_to_rgb709(tmpPix[0], tmpPix[1], tmpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    if (sizeof(PIX) == 1.) {
                        // convert from linear
                        for (int c = 0; c < 3; ++c) {
                            tmpPix[c] = _lut->toColorSpaceFloatFromLinearFloat(tmpPix[c]);
                        }
                    }
                } else {
                    if (p.colorModel == eColorModelYCbCr) {
                        // bring from 0..1 to the -0.5-0.5 range
                        //tmpPix[1] -= 0.5;
                        //tmpPix[2] -= 0.5;
                        Color::ypbpr_to_rgb709(tmpPix[0], tmpPix[1], tmpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    }
                    if (p.colorModel != eColorModelLinearRGB) {
                        if (sizeof(PIX) != 1) {
                            // convert from rec709
                            for (int c = 0; c < 3; ++c) {
                                tmpPix[c] = _lut->fromColorSpaceFloatToLinearFloat(tmpPix[c]);
                            }
                        }
                    }
                }
            }

            ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, p.premult, p.premultChannel, x, y, srcPix, p.doMasking, mask.get(), p.mix, p.maskInvert, dstPix);
            if ( (p.outputMode == eOutputModeNoise) || (p.outputMode == eOutputModeSharpen) ) {
                // if Output=Noise or Output=Sharpen, the unchecked channels should be zero on output
                if (srcPix) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = p.process[c] ? (dstPix[c] - srcPix[c]) : 0;
                    }
                }
            } else {
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!p.process[3]) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if ( (nComponents == 3) || (nComponents == 4) ) {
                    for (int c = 0; c < 3; ++c) {
                        if (!p.process[c]) {
                            dstPix[c] = srcPix ? srcPix[c] : PIX();
                        }
                    }
                    if ( !p.process[3] && (nComponents == 4) ) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
            }
            // increment the dst pixel
            dstPix += nComponents;
        }
    }
} // DenoiseSharpenPlugin::renderForBitDepth

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DenoiseSharpenPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                           RegionOfInterestSetter &rois)
{
    const double time = args.time;

    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
    if ( Coords::rectIsEmpty(srcRod) || Coords::rectIsEmpty(args.regionOfInterest) ) {
        return;
    }

    int adaptiveRadius = _adaptiveRadius->getValueAtTime(time);
    if (adaptiveRadius <= 0) {
        // requires the full image to compute standard deviation of the signal
        rois.setRegionOfInterest(*_srcClip, srcRod);

        return;
    }
    bool b3 = _b3->getValueAtTime(time);
    double par = _srcClip->getPixelAspectRatio();
    OfxRectI roiPixel;
    Coords::toPixelEnclosing(args.regionOfInterest, args.renderScale, par, &roiPixel);
    int levels = kLevelMax - startLevelFromRenderScale(args.renderScale);
    int radiusPixel = borderSize(adaptiveRadius, b3, levels);
    roiPixel.x1 -= radiusPixel;
    roiPixel.x2 += radiusPixel;
    roiPixel.y1 -= radiusPixel;
    roiPixel.y2 += radiusPixel;
#ifndef NDEBUG
    int sc = 1 << levels;
    if (b3) {
        assert( (2 * sc - 1 + 2 * sc) < (roiPixel.x2 - roiPixel.x1) );
        assert( (2 * sc - 1 + 2 * sc) < (roiPixel.y2 - roiPixel.y1) );
    } else {
        assert( sc - 1 + sc < (roiPixel.x2 - roiPixel.x1) );
        assert( sc - 1 + sc < (roiPixel.y2 - roiPixel.y1) );
    }
#endif
    OfxRectD roi;
    Coords::toCanonical(roiPixel, args.renderScale, par, &roi);

    Coords::rectIntersection<OfxRectD>(roi, srcRod, &roi);
    rois.setRegionOfInterest(*_srcClip, roi);

    // if analysis is locked, we do not need the analysis inputs
    if ( _analysisLock->getValueAtTime(time) ) {
        OfxRectD emptyRoi = {0., 0., 0., 0.};
        rois.setRegionOfInterest(*_analysisSrcClip, emptyRoi);
        rois.setRegionOfInterest(*_analysisMaskClip, emptyRoi);
    }
}

bool
DenoiseSharpenPlugin::isIdentity(const IsIdentityArguments &args,
                                 Clip * &identityClip,
                                 double & /*identityTime*/
                                 , int& /*view*/, std::string& /*plane*/)
{
    DBG(cout << "isIdentity!\n");

    const double time = args.time;

    if (kLevelMax - startLevelFromRenderScale(args.renderScale) < 0) {
        // renderScale is too low for denoising
        identityClip = _srcClip;

        return true;
    }

    if ( !_analysisLock->getValue() ) {
        // analysis not locked, always return source image
        identityClip = _srcClip;

        return true;
    }

    double mix = _mix->getValueAtTime(time);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    bool processR = _processR->getValueAtTime(time);
    bool processG = _processG->getValueAtTime(time);
    bool processB = _processB->getValueAtTime(time);
    bool processA = _processA->getValueAtTime(time);
    if (!processR && !processG && !processB && !processA) {
        identityClip = _srcClip;

        return true;
    }

    // which plugin parameter values give identity?

    if ( ( (OutputModeEnum)_outputMode->getValueAtTime(time) == eOutputModeNoise ) ||
         ( (OutputModeEnum)_outputMode->getValueAtTime(time) == eOutputModeSharpen ) ) {
        return false;
    }

    if ( processA && !( (_noiseLevel[3][0]->getValueAtTime(time) <= 0.) &&
                        ( _noiseLevel[3][1]->getValueAtTime(time) <= 0.) &&
                        ( _noiseLevel[3][2]->getValueAtTime(time) <= 0.) &&
                        ( _noiseLevel[3][3]->getValueAtTime(time) <= 0.) ) ) {
        return false;
    }

    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    double noiseLevelGain = _noiseLevelGain->getValueAtTime(time);
    double gainFreq[4];;
    for (unsigned int f = 0; f < 4; ++f) {
        if ( _enableFreq[f]->getValueAtTime(time) ) {
            gainFreq[f] = noiseLevelGain * _gainFreq[f]->getValueAtTime(time);
        } else {
            gainFreq[f] = 0;
        }
    }
    double denoiseAmount = _denoiseAmount->getValueAtTime(time);
    bool denoise[4];
    for (unsigned int c = 0; c < 4; ++c) {
        denoise[c] = false;
        double denoise_amount = _amount[c]->getValueAtTime(time) * denoiseAmount;
        for (unsigned int f = 0; f < 4; ++f) {
            double noiseLevel = gainFreq[f] * _noiseLevel[c][f]->getValueAtTime(time);
            denoise[c] |= (noiseLevel > 0. && denoise_amount > 0.);
        }
    }
    double sharpenAmount = _sharpenAmount->getValueAtTime(time);
    if ( (noiseLevelGain <= 0.) &&
         (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    } else if ( ( (colorModel == eColorModelRGB) || (colorModel == eColorModelLinearRGB) ) &&
                (!processR || !denoise[0]) &&
                (!processG || !denoise[1]) &&
                (!processR || !denoise[2]) &&
                (!processA || !denoise[3]) &&
                (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    } else if ( ( (!processR && !processG && !processB) ||
                  ( !denoise[0] &&
                    !denoise[1] &&
                    !denoise[2] ) ) &&
                (!processA || !denoise[3]) &&
                (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    DBG(cout << "isIdentity! false\n");

    return false;
} // DenoiseSharpenPlugin::isIdentity

void
DenoiseSharpenPlugin::changedClip(const InstanceChangedArgs &args,
                                  const std::string &clipName)
{
    DBG(cout << "changedClip!\n");

    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _premult->setValue(false);
        } else {
            switch ( _srcClip->getPreMultiplication() ) {
            case eImageOpaque:
                _premult->setValue(false);
                break;
            case eImagePreMultiplied:
                _premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                _premult->setValue(false);
                break;
            }
        }
    }
    DBG(cout << "changedClip OK!\n");
}

void
DenoiseSharpenPlugin::changedParam(const InstanceChangedArgs &args,
                                   const std::string &paramName)
{
    const double time = args.time;

    if ( ( (paramName == kParamProcessR) ||
           ( paramName == kParamProcessG) ||
           ( paramName == kParamProcessB) ||
           ( paramName == kParamProcessA) ) && (args.reason == eChangeUserEdit) ) {
        updateSecret();
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    } else if ( (paramName == kParamColorModel) || (paramName == kParamB3) ) {
        updateLabels();
        if (args.reason == eChangeUserEdit) {
            beginEditBlock(kParamColorModel);
            for (unsigned int c = 0; c < 4; ++c) {
                for (unsigned int f = 0; f < 4; ++f) {
                    _noiseLevel[c][f]->setValue(0.);
                }
            }
            endEditBlock();
        }
    } else if (paramName == kParamAnalysisLock) {
        analysisLock();
    } else if (paramName == kParamAnalyzeNoiseLevels) {
        analyzeNoiseLevels(args);
    } else if (paramName == kParamAdaptiveRadius) {
        // if adaptiveRadius <= 0, we need to render the whole image anyway, so disable tiles support
        int adaptiveRadius = _adaptiveRadius->getValueAtTime(time);
        setSupportsTiles(adaptiveRadius > 0);
    }
}

void
DenoiseSharpenPlugin::analyzeNoiseLevels(const InstanceChangedArgs &args)
{
    DBG(cout << "analysis!\n");

    assert(args.renderScale.x == 1. && args.renderScale.y == 1.);

    progressStartAnalysis(kPluginName " (noise analysis)");
    beginEditBlock(kParamAnalyzeNoiseLevels);

    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( !_analysisLock->getValue() );

#ifdef _OPENMP
    // set the number of OpenMP threads to a reasonable value
    // (but remember that the OpenMP threads are not counted my the multithread suite)
    omp_set_num_threads( std::max(1u, MultiThread::getNumCPUs() ) );
#endif

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB /*|| dstComponents == ePixelComponentXY*/ || dstComponents == ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
    case ePixelComponentRGBA:
        analyzeNoiseLevelsForComponents<4>(args);
        break;
    case ePixelComponentRGB:
        analyzeNoiseLevelsForComponents<3>(args);
        break;
    //case ePixelComponentXY:
    //    renderForComponents<2>(args);
    //    break;
    case ePixelComponentAlpha:
        analyzeNoiseLevelsForComponents<1>(args);
        break;
    default:
#ifdef DEBUG_STDOUT
        std::cout << "components usupported\n";
#endif
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
    _analysisFrame->setValue( (int)args.time );

    // lock values
    _analysisLock->setValue(true);
    endEditBlock();
    progressEndAnalysis();

    DBG(cout << "analysis! OK\n");
}

template<int nComponents>
void
DenoiseSharpenPlugin::analyzeNoiseLevelsForComponents(const InstanceChangedArgs &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        analyzeNoiseLevelsForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        analyzeNoiseLevelsForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        analyzeNoiseLevelsForBitDepth<float, nComponents, 1>(args);
        break;
    default:
#ifdef DEBUG_STDOUT
        std::cout << "depth usupported\n";
#endif
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
DenoiseSharpenPlugin::analyzeNoiseLevelsForBitDepth(const InstanceChangedArgs &args)
{
    assert(args.renderScale.x == 1. && args.renderScale.y == 1.);
    const double time = args.time;

    auto_ptr<const Image> src;
    auto_ptr<const Image> mask;

    if ( _analysisSrcClip && _analysisSrcClip->isConnected() ) {
        src.reset( _analysisSrcClip->fetchImage(time) );
    } else {
        src.reset( ( _srcClip && _srcClip->isConnected() ) ?
                   _srcClip->fetchImage(time) : 0 );
    }
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    bool doMasking = _analysisMaskClip && _analysisMaskClip->isConnected();
    mask.reset(doMasking ? _analysisMaskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if ( !src.get() ) {
        setPersistentMessage(Message::eMessageError, "", "No Source image to analyze");
        throwSuiteStatusException(kOfxStatFailed);
    }

    bool maskInvert = doMasking ? _maskInvert->getValueAtTime(time) : false;
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    bool b3 = _b3->getValueAtTime(time);
    OfxRectD cropRect;
    _btmLeft->getValueAtTime(time, cropRect.x1, cropRect.y1);
    double w, h;
    _size->getValueAtTime(time, w, h);
    cropRect.x2 = cropRect.x1 + w;
    cropRect.y2 = cropRect.y1 + h;

    OfxRectI cropRectI;
    cropRectI.x1 = std::ceil(cropRect.x1);
    cropRectI.x2 = std::floor(cropRect.x2);
    cropRectI.y1 = std::ceil(cropRect.y1);
    cropRectI.y2 = std::floor(cropRect.y2);

    OfxRectI srcWindow;
    bool intersect = Coords::rectIntersection(src->getBounds(), cropRectI, &srcWindow);
    if ( !intersect || ( (srcWindow.x2 - srcWindow.x1) < 80 ) || ( (srcWindow.y2 - srcWindow.y1) < 80 ) ) {
        setPersistentMessage(Message::eMessageError, "", "The analysis window must be at least 80x80 pixels.");
        throwSuiteStatusException(kOfxStatFailed);
    }
    clearPersistentMessage();

    // temporary buffers: one for each channel plus 2 for processing
    unsigned int iwidth = srcWindow.x2 - srcWindow.x1;
    unsigned int iheight = srcWindow.y2 - srcWindow.y1;
    unsigned int isize = iwidth * iheight;
    auto_ptr<ImageMemory> tmpData( new ImageMemory(sizeof(float) * isize * (nComponents + 3), this) );
    float* tmpPixelData = (float*)tmpData->lock();
    float* fimgcolor[3] = { NULL, NULL, NULL };
    float* fimgalpha = NULL;
    float *fimgtmp[3] = { NULL, NULL, NULL };
    fimgcolor[0] = (nComponents != 1) ? tmpPixelData : NULL;
    fimgcolor[1] = (nComponents != 1) ? tmpPixelData + isize : NULL;
    fimgcolor[2] = (nComponents != 1) ? tmpPixelData + 2 * isize : NULL;
    fimgalpha = (nComponents == 1) ? tmpPixelData : ( (nComponents == 4) ? tmpPixelData + 3 * isize : NULL );
    fimgtmp[0] = tmpPixelData + nComponents * isize;
    fimgtmp[1] = tmpPixelData + (nComponents + 1) * isize;
    fimgtmp[2] = tmpPixelData + (nComponents + 2) * isize;
    auto_ptr<ImageMemory> maskData( doMasking ? new ImageMemory(sizeof(bool) * isize, this) : NULL );
    bool* bimgmask = doMasking ? (bool*)maskData->lock() : NULL;


    // - extract the color components and convert them to the appropriate color model
    //
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int y = srcWindow.y1; y < srcWindow.y2; y++) {
        abort_test_loop();

        for (int x = srcWindow.x1; x < srcWindow.x2; x++) {
            const PIX *srcPix = (const PIX *)  (src.get() ? src->getPixelAddress(x, y) : 0);
            float unpPix[4] = {0., 0., 0., 0.};
            ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, premult, premultChannel);
            unsigned int pix = (x - srcWindow.x1) + (y - srcWindow.y1) * iwidth;
            // convert to the appropriate color model and store in tmpPixelData
            if (nComponents != 1) {
                if (colorModel == eColorModelLab) {
                    if (sizeof(PIX) == 1) {
                        // convert to linear
                        for (int c = 0; c < 3; ++c) {
                            unpPix[c] = _lut->fromColorSpaceFloatToLinearFloat(unpPix[c]);
                        }
                    }
                    Color::rgb709_to_lab(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                    // bring each component in the 0..1 range
                    //unpPix[0] = unpPix[0] / 116.0 + 0 * 16 * 27 / 24389.0;
                    //unpPix[1] = unpPix[1] / 500.0 / 2.0 + 0.5;
                    //unpPix[2] = unpPix[2] / 200.0 / 2.2 + 0.5;
                } else {
                    if (colorModel != eColorModelLinearRGB) {
                        if (sizeof(PIX) != 1) {
                            // convert to rec709
                            for (int c = 0; c < 3; ++c) {
                                unpPix[c] = _lut->toColorSpaceFloatFromLinearFloat(unpPix[c]);
                            }
                        }
                    }
                    if (colorModel == eColorModelYCbCr) {
                        Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                        // bring to the 0-1 range
                        //unpPix[1] += 0.5;
                        //unpPix[2] += 0.5;
                    }
                }
                // store in tmpPixelData
                for (int c = 0; c < 3; ++c) {
                    fimgcolor[c][pix] = unpPix[c];
                }
            }
            if (nComponents != 3) {
                assert(fimgalpha);
                fimgalpha[pix] = unpPix[3];
            }
            if (doMasking) {
                assert(bimgmask);
                const PIX *maskPix = (const PIX *)  (mask.get() ? mask->getPixelAddress(x, y) : 0);
                bool mask = maskPix ? (*maskPix != 0) : false;
                bimgmask[pix] = maskInvert ? !mask : mask;
            }
        }
    }

    // set noise levels

    if (nComponents != 1) {
        // process color channels
        for (int c = 0; c < 3; ++c) {
            assert(fimgcolor[c]);
            float* fimg[4] = { fimgcolor[c], fimgtmp[0], fimgtmp[1], fimgtmp[2] };
            double sigma_n[4];
            sigma_mad(fimg, bimgmask, iwidth, iheight, b3, sigma_n, (float)c / nComponents, 1.f / nComponents);
            for (unsigned f = 0; f < 4; ++f) {
                _noiseLevel[c][f]->setValue(sigma_n[f]);
            }
        }
    }
    if (nComponents != 3) {
        assert(fimgalpha);
        // process alpha
        float* fimg[4] = { fimgalpha, fimgtmp[0], fimgtmp[1], fimgtmp[2] };
        double sigma_n[4];
        sigma_mad(fimg, bimgmask, iwidth, iheight, b3, sigma_n, (float)(nComponents - 1) / nComponents, 1.f / nComponents);
        for (unsigned f = 0; f < 4; ++f) {
            _noiseLevel[3][f]->setValue(sigma_n[f]);
        }
    }
} // DenoiseSharpenPlugin::analyzeNoiseLevelsForBitDepth

void
DenoiseSharpenPlugin::updateLabels()
{
    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValue();

    for (unsigned c = 0; c < 4; ++c) {
        for (unsigned f = 0; f < 4; ++f) {
            _noiseLevel[c][f]->setLabel( channelLabel(colorModel, c, f) );
        }
        _channelGain[c]->setLabel( channelGainLabel(colorModel, c) );
        _amount[c]->setLabel( amountLabel(colorModel, c) );
    }
}

void
DenoiseSharpenPlugin::updateSecret()
{
    bool process[4];

    process[0] = _processR->getValue();
    process[1] = _processG->getValue();
    process[2] = _processB->getValue();
    process[3] = _processA->getValue();

    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValue();
    if ( (colorModel == eColorModelYCbCr) || (colorModel == eColorModelLab) ) {
        bool processColor = process[0] || process[1] || process[2];
        process[0] = process[1] = process[2] = processColor;
    }
    for (unsigned c = 0; c < 4; ++c) {
        for (unsigned f = 0; f < 4; ++f) {
            _noiseLevel[c][f]->setIsSecretAndDisabled(!process[c]);
        }
        _channelGain[c]->setIsSecretAndDisabled(!process[c]);
        _amount[c]->setIsSecretAndDisabled(!process[c]);
    }
}

class DenoiseSharpenOverlayDescriptor
    : public DefaultEffectOverlayDescriptor<DenoiseSharpenOverlayDescriptor, RectangleInteract>
{
};

mDeclarePluginFactory(DenoiseSharpenPluginFactory, { gLutManager = new Color::LutManager<Mutex>; ofxsThreadSuiteCheck(); }, { delete gLutManager; });

void
DenoiseSharpenPluginFactory::describe(ImageEffectDescriptor &desc)
{
    DBG(cout << "describe!\n");

    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    desc.setDescriptionIsMarkdown(true);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    // For hosts that don't support setting kSupportsTiles on the plugin instance (it appeared
    // in OFX 1.4, see <https://groups.google.com/d/msg/ofxa-members/MgvKUWlMljg/LoJeGgWZRDcJ>),
    // the plugin descriptor has this property set to false.
    desc.setSupportsTiles(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setOverlayInteractDescriptor(new DenoiseSharpenOverlayDescriptor);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
    DBG(cout << "describe! OK\n");
}

void
DenoiseSharpenPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                               ContextEnum context)
{
    DBG(cout << "describeInContext!\n");

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->setHint(kClipSourceHint);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->setHint(kClipMaskHint);
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    ClipDescriptor *analysisSrcClip = desc.defineClip(kClipAnalysisSource);
    analysisSrcClip->setHint(kClipAnalysisSourceHint);
    analysisSrcClip->addSupportedComponent(ePixelComponentRGBA);
    analysisSrcClip->addSupportedComponent(ePixelComponentRGB);
    //analysisSrcClip->addSupportedComponent(ePixelComponentXY);
    analysisSrcClip->addSupportedComponent(ePixelComponentAlpha);
    analysisSrcClip->setTemporalClipAccess(false);
    analysisSrcClip->setOptional(true);
    analysisSrcClip->setSupportsTiles(kSupportsTiles);
    analysisSrcClip->setIsMask(false);

    ClipDescriptor *analysisMaskClip = desc.defineClip(kClipAnalysisMask);
    analysisMaskClip->setHint(kClipAnalysisMaskHint);
    analysisMaskClip->addSupportedComponent(ePixelComponentAlpha);
    analysisMaskClip->setTemporalClipAccess(false);
    analysisMaskClip->setOptional(true);
    analysisMaskClip->setSupportsTiles(kSupportsTiles);
    analysisMaskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    const GroupParamDescriptor* group = NULL;

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    // describe plugin params
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        param->setAnimates(false);
        assert(param->getNOptions() == (int)eOutputModeResult);
        param->appendOption(kParamOutputModeOptionResult);
        assert(param->getNOptions() == (int)eOutputModeNoise);
        param->appendOption(kParamOutputModeOptionNoise);
        assert(param->getNOptions() == (int)eOutputModeSharpen);
        param->appendOption(kParamOutputModeOptionSharpen);
        param->setDefault( (int)eOutputModeResult );
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamColorModel);
        param->setLabel(kParamColorModelLabel);
        param->setHint(kParamColorModelHint);
        param->setAnimates(false);
        assert(param->getNOptions() == (int)eColorModelYCbCr);
        param->appendOption(kParamColorModelOptionYCbCr);
        assert(param->getNOptions() == (int)eColorModelLab);
        param->appendOption(kParamColorModelOptionLab);
        assert(param->getNOptions() == (int)eColorModelRGB);
        param->appendOption(kParamColorModelOptionRGB);
        assert(param->getNOptions() == (int)eColorModelLinearRGB);
        param->appendOption(kParamColorModelOptionLinearRGB);
        param->setDefault( (int)eColorModelYCbCr );
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }


    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupAnalysis);
        if (group) {
            group->setLabel(kGroupAnalysisLabel);
            //group->setHint(kGroupAnalysisHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }


        // analysisLock
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAnalysisLock);
            param->setLabel(kParamAnalysisLockLabel);
            param->setHint(kParamAnalysisLockHint);
            param->setDefault(false);
            param->setEvaluateOnChange(true); // changes the output mode
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
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
            param->setDefault(0.1, 0.1);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setHint("Coordinates of the bottom left corner of the analysis rectangle. This rectangle is intersected with the AnalysisMask input, if connected.");
            param->setDigits(0);
            param->setEvaluateOnChange(false);
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
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
            param->setDefault(0.8, 0.8);
            param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
            param->setHint("Width and height of the analysis rectangle. This rectangle is intersected with the AnalysisMask input, if connected.");
            param->setIncrement(1.);
            param->setDigits(0);
            param->setEvaluateOnChange(false);
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamB3);
            param->setLabel(kParamB3Label);
            param->setHint(kParamB3Hint);
            param->setDefault(true);
            param->setAnimates(false);
            if (group) {
                // coverity[dead_error_line]
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            IntParamDescriptor* param = desc.defineIntParam(kParamAnalysisFrame);
            param->setLabel(kParamAnalysisFrameLabel);
            param->setHint(kParamAnalysisFrameHint);
            param->setEnabled(false);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            param->setDefault(-1);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamAnalyzeNoiseLevels);
            param->setLabel(kParamAnalyzeNoiseLevelsLabel);
            param->setHint(kParamAnalyzeNoiseLevelsHint);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupNoiseLevels);
        if (group) {
            group->setLabel(kGroupNoiseLevelsLabel);
            //group->setHint(kGroupNoiseLevelsHint);
            group->setOpen(false);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        for (unsigned f = 0; f < 4; ++f) {
            for (unsigned c = 0; c < 4; ++c) {
                DoubleParamDescriptor* param = desc.defineDoubleParam( channelParam(c, f) );
                param->setLabel( channelLabel(eColorModelAny, c, f) );
                param->setHint(kParamNoiseLevelHint);
                param->setRange(0, DBL_MAX);
                param->setDisplayRange(0, kParamNoiseLevelMax);
                param->setAnimates(true);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamNoiseLevelGain);
        param->setLabel(kParamNoiseLevelGainLabel);
        param->setHint(kParamNoiseLevelGainHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(0, 2.);
        param->setDefault(1.);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamDenoiseAmount);
        param->setLabel(kParamDenoiseAmountLabel);
        param->setHint(kParamDenoiseAmountHint);
        param->setRange(0, 1.);
        param->setDisplayRange(0, 1.);
        param->setDefault(1.);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupTuning);
        if (group) {
            group->setLabel(kGroupTuningLabel);
            //group->setHint(kGroupTuningHint);
            group->setOpen(false);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        for (unsigned int f = 0; f < 4; ++f) {
            {
                BooleanParamDescriptor* param = desc.defineBooleanParam( enableParam(f) );
                param->setLabel( f == 0 ? kParamEnableHighLabel :
                                 ( f == 1 ? kParamEnableMediumLabel :
                                   (f == 2 ? kParamEnableLowLabel :
                                    kParamEnableVeryLowLabel) ) );
                param->setHint( f == 0 ? kParamEnableHighHint :
                                ( f == 1 ? kParamEnableMediumHint :
                                  (f == 2 ? kParamEnableLowHint :
                                   kParamEnableVeryLowHint) ) );
                param->setDefault(true);
                param->setAnimates(false);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                DoubleParamDescriptor* param = desc.defineDoubleParam( gainParam(f) );
                param->setLabel( f == 0 ? kParamGainHighLabel :
                                 ( f == 1 ? kParamGainMediumLabel :
                                   (f == 2 ? kParamGainLowLabel :
                                    kParamGainVeryLowLabel) ) );
                param->setHint( f == 0 ? kParamGainHighHint :
                                ( f == 1 ? kParamGainMediumHint :
                                  (f == 2 ? kParamGainLowHint :
                                   kParamGainVeryLowHint) ) );
                param->setRange(0, DBL_MAX);
                param->setDisplayRange(0, 10.);
                param->setDefault(1.);
                param->setAnimates(true);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
        {
            IntParamDescriptor* param = desc.defineIntParam(kParamAdaptiveRadius);
            param->setLabel(kParamAdaptiveRadiusLabel);
            param->setHint(kParamAdaptiveRadiusHint);
            param->setRange(0, 10);
            param->setDisplayRange(0, 10);
            param->setDefault(kParamAdaptiveRadiusDefault);
            param->setAnimates(false);
            if (group) {
                // coverity[dead_error_line]
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupChannelTuning);
        if (group) {
            group->setLabel(kGroupChannelTuningLabel);
            //group->setHint(kGroupChannelTuningHint);
            group->setOpen(false);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        for (unsigned c = 0; c < 4; ++c) {
            {
                DoubleParamDescriptor* param = desc.defineDoubleParam( c == 0 ? kParamYLRGain :
                                                                       ( c == 1 ? kParamCbAGGain :
                                                                         (c == 2 ? kParamCrBBGain :
                                                                          kParamAlphaGain) ) );
                param->setLabel( channelGainLabel(eColorModelAny, c) );
                param->setHint(kParamChannelGainHint);
                param->setRange(0, DBL_MAX);
                param->setDisplayRange(0, 10.);
                param->setDefault(1.);
                param->setAnimates(true);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                DoubleParamDescriptor* param = desc.defineDoubleParam( c == 0 ? kParamYLRAmount :
                                                                       ( c == 1 ? kParamCbAGAmount :
                                                                         (c == 2 ? kParamCrBBAmount :
                                                                          kParamAlphaAmount) ) );
                param->setLabel( amountLabel(eColorModelAny, c) );
                param->setHint(kParamAmountHint);
                param->setRange(0, 1.);
                param->setDisplayRange(0, 1.);
                param->setDefault(1.);
                param->setAnimates(true);
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupSharpen);
        if (group) {
            group->setLabel(kGroupSharpenLabel);
            //group->setHint(kGroupSettingsHint);
            group->setEnabled(true);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
        }


        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSharpenAmount);
            param->setLabel(kParamSharpenAmountLabel);
            param->setHint(kParamSharpenAmountHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 10);
            param->setDefault(0.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSharpenSize);
            param->setLabel(kParamSharpenSizeLabel);
            param->setHint(kParamSharpenSizeHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(8, 32.);
            param->setDefault(10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSharpenLuminance);
            param->setLabel(kParamSharpenLuminanceLabel);
            param->setHint(kParamSharpenLuminanceHint);
            param->setDefault(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
    if (!gHostSupportsDefaultCoordinateSystem) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDefaultsNormalised);
        param->setDefault(true);
        param->setEvaluateOnChange(false);
        param->setIsSecretAndDisabled(true);
        param->setIsPersistent(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    DBG(cout << "describeInContext! OK\n");
} // DenoiseSharpenPluginFactory::describeInContext

ImageEffect*
DenoiseSharpenPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new DenoiseSharpenPlugin(handle);
}

static DenoiseSharpenPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
