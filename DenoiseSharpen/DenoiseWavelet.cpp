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
 * OFX DenoiseWavelet plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
//#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"
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
typedef MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DenoiseWavelet[Beta]"
#define kPluginGrouping "Filter"
#define kPluginDescription "This plugin allows the separate denoising of image channels in multiple color spaces using wavelets."
#define kPluginIdentifier "net.sf.openfx.DenoiseWavelet"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1 // dynamic
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

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

#define kParamColorModel "colorModel"
#define kParamColorModelLabel "Color Model"
#define kParamColorModelHint "The colorspace where denoising is performed."
#define kParamColorModelOptionYCbCr "Y'CbCr(A)", "The YCbCr color model has one luminance channel (Y) which contains most of the detail information of an image (such as brightness and contrast) and two chroma channels (Cb = blueness, Cr = reddness) that hold the color information. Note that this choice drastically affects the result.", "ycbcr"
#define kParamColorModelOptionLab "CIE L*a*b(A)", "CIE L*a*b* is a color model in which chrominance is separated from lightness and color distances are perceptually uniform. Note that this choice drastically affects the result.", "cielab"
#define kParamColorModelOptionRGB "R'G'B'(A)", "The R'G'B' color model (gamma-corrected RGB) separates an image into channels of red, green, and blue. Note that this choice drastically affects the result.", "gammargb"
#define kParamColorModelOptionLinearRGB "RGB(A)", "The Linear RGB color model processes the raw linear components.", "linearrgb"
enum ColorModelEnum
{
    eColorModelYCbCr = 0,
    eColorModelLab,
    eColorModelRGB,
    eColorModelLinearRGB,
};

#define kParamThresholdHint "Adjusts the threshold for denoising of the selected channel in a range from 0.0 (none) to 10.0. The threshold is the value below which everything is considered noise. 0.4 is a reasonable value for noisy channels."
#define kParamSoftnessHint "This adjusts the softness of the thresholding (soft as opposed to hard thresholding). The higher the softness the more noise remains in the image. Default is 0.0."
#define kGroupSettings "channelSettings"
#define kGroupSettingsLabel "Channel Settings"
#define kParamYLRThreshold "ylrThreshold"
#define kParamYLRThresholdLabel "Y/L/R Threshold"
#define kParamYThresholdLabel "Y Threshold"
#define kParamLThresholdLabel "L Threshold"
#define kParamRThresholdLabel "R Threshold"
#define kParamYLRSoftness "ylrSoftness"
#define kParamYLRSoftnessLabel "Y/L/R Softness"
#define kParamYSoftnessLabel "Y Softness"
#define kParamLSoftnessLabel "L Softness"
#define kParamRSoftnessLabel "R Softness"
#define kParamCbAGThreshold "cbagThreshold"
#define kParamCbAGThresholdLabel "Cb/A/G Threshold"
#define kParamCbThresholdLabel "Cb Threshold"
#define kParamAThresholdLabel "A Threshold"
#define kParamGThresholdLabel "G Threshold"
#define kParamCbAGSoftness "cbagSoftness"
#define kParamCbAGSoftnessLabel "Cb/A/G Softness"
#define kParamCbSoftnessLabel "Cb Softness"
#define kParamASoftnessLabel "A Softness"
#define kParamGSoftnessLabel "G Softness"
#define kParamCrBBThreshold "crbbThreshold"
#define kParamCrBBThresholdLabel "Cr/B/B Threshold"
#define kParamCrThresholdLabel "Cr Threshold"
#define kParamBThresholdLabel "B Threshold"
#define kParamCrBBSoftness "crbbSoftness"
#define kParamCrBBSoftnessLabel "Cr/B/B Softness"
#define kParamCrSoftnessLabel "Cr Softness"
#define kParamBSoftnessLabel "B Softness"

#define kParamAlphaThreshold "alphaThreshold"
#define kParamAlphaThresholdLabel "Alpha Threshold"
#define kParamAlphaSoftness "alphaSoftness"
#define kParamAlphaSoftnessLabel "Alpha Softness"

#define kParamAdaptive "adaptiveDenoising"
#define kParamAdaptiveLabel "Adaptive Denoising"
#define kParamAdaptiveHint "When enabled, an intensity-dependent noise model is estimated and used for noise suppression (as implemented in the GIMP wavelet denoise plugin), else a fixed threshold is used (as implemented in dcraw/UFRaw/LibRaw). Adaptive denoising requires to process the whole image at once and does not support tiled rendering."

#define kGroupSharpen "sharpen"
#define kGroupSharpenLabel "Sharpen"

#define kParamSharpenAmount "sharpenAmount"
#define kParamSharpenAmountLabel "Sharpen Amount"
#define kParamSharpenAmountHint "Adjusts the amount of sharpening applied."

#define kParamSharpenRadius "sharpenRadius"
#define kParamSharpenRadiusLabel "Sharpen Radius"
#define kParamSharpenRadiusHint "Adjusts the radius of the sharpening. For very unsharp images it is recommended to use higher values. Default is 0.5."

#define kParamSharpenLuminance "sharpenLuminance"
#define kParamSharpenLuminanceLabel "Sharpen Y Only"
#define kParamSharpenLuminanceHint "Sharpens luminance only (if colormodel is R'G'B', sharpen only RGB). This avoids color artifacts to appear. Colour sharpness in natural images is not critical for the human eye."

#define kParamPremultChanged "premultChanged"

#define kLevelMax 4 // 7 // maximum level for denoising

#ifdef _OPENMP
#define abort_test() if ( !omp_get_thread_num() && abort() ) \
        throwSuiteStatusException(kOfxStatFailed)
#else
#define abort_test() if ( abort() ) \
        throwSuiteStatusException(kOfxStatFailed)
#endif

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

// h = (0.25,0.5,0.25) linear Lagrange interpolation, with some hocus-pocus at the edges.
// could be made edge-aware, maybe?
// - https://www.darktable.org/wp-content/uploads/2011/11/hdl11_talk.pdf
// - https://jo.dreggn.org/home/2011_atrous.pdf
// for the edge-avoiding a trous, just multiply the side coefficients by
// exp(-(dist2/(2.f*sigma_r*sigma_r)));
// where dist2 is the squared color distance with the center, and sigma_r = 0.1
static
void
hat_transform (float *temp, //!< output vector
               const float *base, //!< input vector
               int st, //!< input stride (1 for line, iwidth for column)
               int size, //!< vector size
               int sc) //!< scale
{
    int i;

    for (i = 0; i < sc; i++) {
        temp[i] = 2 * base[st * i] + base[st * (sc - i)] + base[st * (i + sc)];
    }
    for (; i + sc < size; i++) {
        temp[i] = 2 * base[st * i] + base[st * (i - sc)] + base[st * (i + sc)];
    }
    for (; i < size; i++) {
        temp[i] = 2 * base[st * i] + base[st * (i - sc)] + base[st * ( 2 * size - 2 - (i + sc) )];
    }
}

// "A trous" algorithm with a linear interpolation filter.
// from dcraw/UFRaw/LibRaw, with enhancements from GIMP wavelet denoise
// https://sourceforge.net/p/ufraw/mailman/message/24069162/
static
void
wavelet_denoise(float *fimg[3], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] and fimg[2] are working space images of the same size
                unsigned int iwidth, //!< width of the image
                unsigned int iheight, //!< height of the image
                float threshold, //!< threshold parameter
                double low, //!< softness parameter
                bool adaptive, //!< true if we use the adaptive threshold
                double amount, //!< constrast boost amount
                double radius, //!< contrast boost radius
                int startLevel,
                float a, // progress amount at start
                float b) // progress increment
{
    // not sure where these thesholds come from...
    // maybe these could be replaced by BayesShrink (as in <https://jo.dreggn.org/home/2011_atrous.pdf>)
    // or SureShrink <http://statweb.stanford.edu/~imj/WEBLIST/1995/ausws.pdf>
    //
    // BayesShrink:
    // compute sigma_n using the MAD (median absolute deviation at the finest level:
    // sigma_n = median(|d_0|)/0.6745 (could be computed in an analysis step from the first detail subband)
    // The soft shrinkage threshold is
    // T = \sigma_{n,i}^2 / \sqrt{max(0,\sigma_{y,i}^2 - \sigma_{n,i}^2)}
    // with
    // \sigma_{y,i}^2 = 1/N \sum{p} d_i(p)^2 (standard deviation of the signal with the noise for this detail subband)
    // \sigma_{n,i} = \sigma_n . 2^{-i} (standard deviation of the noise)
    // S. G. Chang, Bin Yu and M. Vetterli, "Adaptive wavelet thresholding for image denoising and compression," in IEEE Transactions on Image Processing, vol. 9, no. 9, pp. 1532-1546, Sep 2000. doi: 10.1109/83.862633
    //
    // SureShrink:
    // Donoho, D. L., & Johnstone, I. M. (1995). Adapting to unknown smoothness via wavelet shrinkage. Journal of the american statistical association, 90(432), 1200-1224. doi: 10.1080/01621459.1995.10476626

    // http://www.csee.wvu.edu/~xinl/courses/ee565/TIP2000.pdf

    static const float noise[] = { 0.8002, 0.2735, 0.1202, 0.0585, 0.0291, 0.0152, 0.0080, 0.0044 };

    assert( ( 1 + sizeof(noise) / sizeof(*noise) ) >= kLevelMax );

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

    if ( (threshold <= 0.) && (amount <= 0.) ) {
        return;
    }

    int maxLevel = kLevelMax - startLevel;
    if (maxLevel < 0) {
        return;
    }

    unsigned int size = iheight * iwidth;

    {
        float *temp = (float*)malloc( std::max(iheight, iwidth) * sizeof *fimg );
        int hpass = 0.;
        int lpass;
        for (int lev = 0; lev <= maxLevel; lev++) {
            abort_test();
            if (b != 0) {
                //progressUpdate(a + b * lev / 5.0);
            }
            lpass = ( (lev & 1) + 1 );

            // smooth fimg[hpass], result is in fimg[lpass]:
            // a- smooth rows, result is in fimg[lpass]
#ifdef _OPENMP
#pragma omp parallel for
#endif
            for (unsigned int row = 0; row < iheight; ++row) {
                hat_transform (temp, fimg[hpass] + row * iwidth, 1, iwidth, 1 << lev);
                for (unsigned int col = 0; col < iwidth; ++col) {
                    fimg[lpass][row * iwidth + col] = temp[col] * 0.25;
                }
            }
            abort_test();
            if (b != 0) {
                //progressUpdate(a + b * (lev + 0.25) / 5.0);
            }

            // b- smooth cols, result is in fimg[lpass]
#ifdef _OPENMP
#pragma omp parallel for
#endif
            for (unsigned int col = 0; col < iwidth; ++col) {
                hat_transform (temp, fimg[lpass] + col, iwidth, iheight, 1 << lev);
                for (unsigned int row = 0; row < iheight; ++row) {
                    fimg[lpass][row * iwidth + col] = temp[row] * 0.25;
                }
            }
            if (b != 0) {
                //progressUpdate(a + b * (lev + 0.5) / 5.0);
            }

            if (adaptive) {
                // threshold (adaptive)

                // a priori threshold to compute signal stdev
                float thold = 5.0 / (1 << 6) * exp ( -2.6 * sqrt (lev + startLevel + 1) ) * 0.8002 / exp (-2.6);

                // initialize stdev values for all intensities
                // http://www.fredosaurus.com/notes-cpp/arrayptr/array-initialization.html
                // If an explicit array size is specified, but an shorter initiliazation list is specified, the unspecified elements are set to zero.
                double stdev[5] = { 0. };
                unsigned int samples[5] = { 0 };

                // calculate stdevs for all intensities
                for (unsigned int i = 0; i < size; ++i) {
                    // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                    fimg[hpass][i] -= fimg[lpass][i];

                    //
                    if ( (fimg[hpass][i] < thold) && (fimg[hpass][i] > -thold) ) {
#define MULTIRANGE
#ifdef MULTIRANGE
                        if (fimg[lpass][i] > 0.8) {
                            stdev[4] += fimg[hpass][i] * fimg[hpass][i];
                            samples[4]++;
                        } else if (fimg[lpass][i] > 0.6) {
                            stdev[3] += fimg[hpass][i] * fimg[hpass][i];
                            samples[3]++;
                        }       else if (fimg[lpass][i] > 0.4) {
                            stdev[2] += fimg[hpass][i] * fimg[hpass][i];
                            samples[2]++;
                        }       else if (fimg[lpass][i] > 0.2) {
                            stdev[1] += fimg[hpass][i] * fimg[hpass][i];
                            samples[1]++;
                        } else
#endif
                        {
                            stdev[0] += fimg[hpass][i] * fimg[hpass][i];
                            samples[0]++;
                        }
                    }
                }
                stdev[0] = std::sqrt( std::max(stdev[0] / (samples[0] + 1), 0.) );
#ifdef MULTIRANGE
                stdev[1] = std::sqrt( std::max(stdev[1] / (samples[1] + 1), 0.) );
                stdev[2] = std::sqrt( std::max(stdev[2] / (samples[2] + 1), 0.) );
                stdev[3] = std::sqrt( std::max(stdev[3] / (samples[3] + 1), 0.) );
                stdev[4] = std::sqrt( std::max(stdev[4] / (samples[4] + 1), 0.) );
#endif
                //printf("thold(%d) = %g\n", lev, thold);
                //printf("stdev(%d) = %g\n", lev, stdev[0]);

                if (b != 0) {
                    //progressUpdate(a + b * (lev + 0.75) / 5.0);
                }

                double beta = 1.;
                if (amount > 0.) {
                    beta += amount * exp (-( (lev + startLevel) - radius ) * ( (lev + startLevel) - radius ) / 1.5);
                }

                /* do thresholding */
                for (unsigned int i = 0; i < size; ++i) {
                    if ( (threshold > 0.) && (low != 1.) ) {
#ifdef MULTIRANGE
                        if (fimg[lpass][i] > 0.8) {
                            thold = threshold * stdev[4];
                        } else if (fimg[lpass][i] > 0.6) {
                            thold = threshold * stdev[3];
                        } else if (fimg[lpass][i] > 0.4) {
                            thold = threshold * stdev[2];
                        } else if (fimg[lpass][i] > 0.2) {
                            thold = threshold * stdev[1];
                        } else
#endif
                        {
                            thold = threshold * stdev[0];
                        }

                        // apply smooth threshold
                        if (fimg[hpass][i] < -thold) {
                            fimg[hpass][i] += thold - thold * low;
                        } else if (fimg[hpass][i] > thold) {
                            fimg[hpass][i] -= thold - thold * low;
                        } else {
                            fimg[hpass][i] *= low;
                        }
                    }
                    // add the denoised band to the final image
                    if (hpass) {
                        fimg[0][i] += beta * fimg[hpass][i];
                    }
                }
            } else {
                // threshold (non-adaptive)
                float thold = 5.0 / (1 << 7) * threshold * noise[lev + startLevel]; // another magic number, no that adaptive and nn-adaptive correspond roughly to the same threshold

                double beta = 1.;
                if (amount > 0.) {
                    beta += amount * exp (-( (lev + startLevel) - radius ) * ( (lev + startLevel) - radius ) / 1.5);
                }

#ifdef _OPENMP
#pragma omp for
#endif
                for (unsigned int i = 0; i < size; ++i) {
                    // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                    fimg[hpass][i] -= fimg[lpass][i];

                    if ( (threshold > 0.) && (low != 1.) ) {
                        // apply smooth threshold
                        if (fimg[hpass][i] < -thold) {
                            fimg[hpass][i] += thold - thold * low;
                        } else if (fimg[hpass][i] >  thold) {
                            fimg[hpass][i] -= thold - thold * low;
                        } else {
                            fimg[hpass][i] *= low;
                        }
                    }
                    // add the denoised band to the final image
                    if (hpass) {
                        // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                        // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5) + 1

                        fimg[0][i] += beta * fimg[hpass][i];
                    }
                }
            }
            hpass = lpass;
        } // for(lev)

        // add the last smoothed image to the image
#ifdef _OPENMP
#pragma omp for
#endif
        for (unsigned int i = 0; i < size; ++i) {
            fimg[0][i] += fimg[lpass][i];
        }
        free(temp);
    } /* end omp parallel */
} // wavelet_denoise

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DenoiseWaveletPlugin
    : public ImageEffect
{
    struct Params;

public:

    /** @brief ctor */
    DenoiseWaveletPlugin(OfxImageEffectHandle handle,
                         const Color::LutBase* lut)
        : ImageEffect(handle)
        , _lut(lut)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _colorModel(NULL)
        , _ylrThreshold(NULL)
        , _ylrSoftness(NULL)
        , _cbagThreshold(NULL)
        , _cbagSoftness(NULL)
        , _crbbThreshold(NULL)
        , _crbbSoftness(NULL)
        , _alphaThreshold(NULL)
        , _alphaSoftness(NULL)
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

        // TODO: fetch noise parameters

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

        _colorModel = fetchChoiceParam(kParamColorModel);
        _ylrThreshold = fetchDoubleParam(kParamYLRThreshold);
        _ylrSoftness = fetchDoubleParam(kParamYLRSoftness);
        _cbagThreshold = fetchDoubleParam(kParamCbAGThreshold);
        _cbagSoftness = fetchDoubleParam(kParamCbAGSoftness);
        _crbbThreshold = fetchDoubleParam(kParamCrBBThreshold);
        _crbbSoftness = fetchDoubleParam(kParamCrBBSoftness);
        _alphaThreshold = fetchDoubleParam(kParamAlphaThreshold);
        _alphaSoftness = fetchDoubleParam(kParamAlphaSoftness);
        _adaptive = fetchBooleanParam(kParamAdaptive);
        _sharpenAmount = fetchDoubleParam(kParamSharpenAmount);
        _sharpenRadius = fetchDoubleParam(kParamSharpenRadius);
        _sharpenLuminance = fetchBooleanParam(kParamSharpenLuminance);

        assert(_colorModel && _ylrThreshold && _ylrSoftness && _cbagThreshold && _cbagSoftness && _crbbThreshold && _crbbSoftness && _alphaThreshold && _alphaSoftness && _adaptive && _sharpenAmount && _sharpenRadius && _sharpenLuminance);

        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        // adaptive denoising does not support tiles
        setSupportsTiles( !_adaptive->getValue() );

        // update the channel labels
        updateLabels();
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

    void updateLabels();

private:
    struct Params
    {
        bool doMasking;
        bool maskInvert;
        bool premult;
        int premultChannel;
        double mix;
        ColorModelEnum colorModel;
        bool adaptive;
        int startLevel;
        bool process[4];
        double threshold[4];
        double softness[4];
        double amount[4];
        double radius;
        OfxRectI srcWindow;

        Params()
            : doMasking(false)
            , maskInvert(false)
            , premult(false)
            , premultChannel(3)
            , mix(1.)
            , colorModel(eColorModelYCbCr)
            , adaptive(false)
            , startLevel(0)
            , radius(0.5)
        {
            process[0] = process[1] = process[2] = process[3] = true;
            threshold[0] = threshold[1] = threshold[2] = threshold[3] = 0.;
            softness[0] = softness[1] = softness[2] = softness[3] = 0.;
            amount[0] = amount[1] = amount[2] = amount[3] = 0.;
        }
    };

    const Color::LutBase* _lut;

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    ChoiceParam* _colorModel;
    DoubleParam* _ylrThreshold;
    DoubleParam* _ylrSoftness;
    DoubleParam* _cbagThreshold;
    DoubleParam* _cbagSoftness;
    DoubleParam* _crbbThreshold;
    DoubleParam* _crbbSoftness;
    DoubleParam* _alphaThreshold;
    DoubleParam* _alphaSoftness;
    BooleanParam* _adaptive;
    DoubleParam* _sharpenAmount;
    DoubleParam* _sharpenRadius;
    BooleanParam* _sharpenLuminance;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from
// the overridden render function
void
DenoiseWaveletPlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
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
        //std::cout << "components usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
DenoiseWaveletPlugin::renderForComponents(const RenderArguments &args)
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
        //std::cout << "depth usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

void
DenoiseWaveletPlugin::setup(const RenderArguments &args,
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
    p.maskInvert = p.doMasking ? _maskInvert->getValueAtTime(time) : false;

    p.premult = _premult->getValueAtTime(time);
    p.premultChannel = _premultChannel->getValueAtTime(time);
    p.mix = _mix->getValueAtTime(time);

    p.process[0] = _processR->getValueAtTime(time);
    p.process[1] = _processG->getValueAtTime(time);
    p.process[2] = _processB->getValueAtTime(time);
    p.process[3] = _processA->getValueAtTime(time);

    // fetch parameter values
    p.colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    p.adaptive = _adaptive->getValueAtTime(time);
    p.startLevel = startLevelFromRenderScale(args.renderScale);
    p.threshold[0] = _ylrThreshold->getValueAtTime(time);
    p.threshold[1] = _cbagThreshold->getValueAtTime(time);
    p.threshold[2] = _crbbThreshold->getValueAtTime(time);
    p.threshold[3] = _alphaThreshold->getValueAtTime(time);
    p.softness[0] = _ylrSoftness->getValueAtTime(time);
    p.softness[1] = _cbagSoftness->getValueAtTime(time);
    p.softness[2] = _crbbSoftness->getValueAtTime(time);
    p.softness[3] = _alphaSoftness->getValueAtTime(time);
    p.amount[0] = _sharpenAmount->getValueAtTime(time);
    p.radius = _sharpenRadius->getValueAtTime(time);
    bool sharpenLuminance = _sharpenLuminance->getValueAtTime(time);

    if (!sharpenLuminance) {
        p.amount[1] = p.amount[2] = p.amount[3] = p.amount[0];
    } else if ( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) {
        p.amount[1] = p.amount[2] = p.amount[0]; // cannot sharpen luminance only
    }

    if ( (p.colorModel == eColorModelRGB) || (p.colorModel == eColorModelLinearRGB) ) {
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && ( (p.threshold[c] > 0 && p.softness[c] != 1.) || p.amount[c] > 0. );
        }
    } else {
        bool processcolor = false;
        for (int c = 0; c < 3; ++c) {
            processcolor = processcolor || ( (p.threshold[c] > 0 && p.softness[c] != 1.) || p.amount[c] > 0. );
        }
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && processcolor;
        }
    }
    p.process[3] = p.process[3] && ( (p.threshold[3] > 0 && p.softness[3] != 1.) || p.amount[3] > 0. );

    // compute the number of levels (max is 4, which adds 1<<4 = 16 pixels on each side)
    int maxLev = std::max( 0, kLevelMax - startLevelFromRenderScale(args.renderScale) );
    // hat_transform gets the pixel at x+-(1<<maxLev), which is computex from x+-(1<<(maxLev-1)), etc...
    // We thus need pixels at x +- (1<<(maxLev+1))-1
    int border = ( 1 << (maxLev + 1) ) - 1;
    p.srcWindow.x1 = args.renderWindow.x1 - border;
    p.srcWindow.y1 = args.renderWindow.y1 - border;
    p.srcWindow.x2 = args.renderWindow.x2 + border;
    p.srcWindow.y2 = args.renderWindow.y2 + border;

    // intersect with srcBounds
    Coords::rectIntersection(p.srcWindow, src->getBounds(), &p.srcWindow);
} // DenoiseWaveletPlugin::setup

template <class PIX, int nComponents, int maxValue>
void
DenoiseWaveletPlugin::renderForBitDepth(const RenderArguments &args)
{
    auto_ptr<const Image> src;
    auto_ptr<Image> dst;
    auto_ptr<const Image> mask;
    Params p;

    setup(args, src, dst, mask, p);

    const OfxRectI& procWindow = args.renderWindow;


    // temporary buffers: one for each channel plus 2 for processing
    unsigned int iwidth = p.srcWindow.x2 - p.srcWindow.x1;
    unsigned int iheight = p.srcWindow.y2 - p.srcWindow.y1;
    unsigned int isize = iwidth * iheight;
    auto_ptr<ImageMemory> tmpData( new ImageMemory(sizeof(float) * isize * (nComponents + 2), this) );
    float* tmpPixelData = (float*)tmpData->lock();
    float* fimgcolor[3] = { NULL, NULL, NULL };
    float* fimgalpha = NULL;
    float *fimgtmp[2] = { NULL, NULL };
    fimgcolor[0] = (nComponents != 1) ? tmpPixelData : NULL;
    fimgcolor[1] = (nComponents != 1) ? tmpPixelData + isize : NULL;
    fimgcolor[2] = (nComponents != 1) ? tmpPixelData + 2 * isize : NULL;
    fimgalpha = (nComponents == 1) ? tmpPixelData : ( (nComponents == 4) ? tmpPixelData + 3 * isize : NULL );
    fimgtmp[0] = tmpPixelData + nComponents * isize;
    fimgtmp[1] = tmpPixelData + (nComponents + 1) * isize;

    // - extract the color components and convert them to the appropriate color model
    //
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int y = p.srcWindow.y1; y < p.srcWindow.y2; y++) {
        abort_test();
        if ( abort() ) {
            continue;
        }

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
                    unpPix[0] = unpPix[0] / 116.0 + 0 * 16 * 27 / 24389.0;
                    unpPix[1] = unpPix[1] / 500.0 / 2.0 + 0.5;
                    unpPix[2] = unpPix[2] / 200.0 / 2.2 + 0.5;
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
                        unpPix[1] += 0.5;
                        unpPix[2] += 0.5;
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
                float* fimg[3] = { fimgcolor[c], fimgtmp[0], fimgtmp[1] };
                wavelet_denoise(fimg, iwidth, iheight, p.threshold[c], p.softness[c], p.adaptive, p.amount[c], p.radius, p.startLevel, (float)c / nComponents, 1.f / nComponents);
            }
        }
    }
    if ( (nComponents != 3) && p.process[3] ) {
        assert(fimgalpha);
        // process alpha
        float* fimg[3] = { fimgalpha, fimgtmp[0], fimgtmp[1] };
        wavelet_denoise(fimg, iwidth, iheight, p.threshold[3], p.softness[3], p.adaptive, p.amount[3], p.radius, p.startLevel, (float)(nComponents - 1) / nComponents, 1.f / nComponents);
    }

    // store back into the result

#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (int y = procWindow.y1; y < procWindow.y2; y++) {
        abort_test();
        if ( abort() ) {
            continue;
        }

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
                    // back to normal Lab
                    tmpPix[0] = (tmpPix[0] - 0 * 16 * 27 / 24389.0) * 116;
                    tmpPix[1] = (tmpPix[1] - 0.5) * 500 * 2;
                    tmpPix[2] = (tmpPix[2] - 0.5) * 200 * 2.2;

                    Color::lab_to_rgb709(tmpPix[0], tmpPix[1], tmpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    if (sizeof(PIX) == 1.) {
                        // convert from linear
                        for (int c = 0; c < 3; ++c) {
                            tmpPix[c] = _lut->toColorSpaceFloatFromLinearFloat(tmpPix[c]);
                        }
                    }
                } else {
                    if (p.colorModel == eColorModelYCbCr) {
                        // bring to the -0.5-0.5 range
                        tmpPix[1] -= 0.5;
                        tmpPix[2] -= 0.5;
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
            // increment the dst pixel
            dstPix += nComponents;
        }
    }
    abort_test();
} // DenoiseWaveletPlugin::renderForBitDepth

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DenoiseWaveletPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                           RegionOfInterestSetter &rois)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( Coords::rectIsEmpty(srcRod) || Coords::rectIsEmpty(args.regionOfInterest) ) {
        return;
    }

    if ( _adaptive->getValueAtTime(args.time) ) {
        // adaptive denoising requires the full image to compute stats
        rois.setRegionOfInterest(*_srcClip, srcRod);

        return;
    }

    double par = _srcClip->getPixelAspectRatio();
    const OfxRectD& regionOfInterest = args.regionOfInterest;
    OfxRectI regionOfInterestPixels;
    Coords::toPixelEnclosing(regionOfInterest, args.renderScale, par, &regionOfInterestPixels);

    // compute the number of levels (max is 4, which adds 1<<4 = 16 pixels on each side)
    int maxLev = std::max( 0, startLevelFromRenderScale(args.renderScale) );
    // hat_transform gets the pixel at x+-(1<<maxLev), which is computex from x+-(1<<(maxLev-1)), etc...
    // We thus need pixels at x +- (1<<(maxLev+1))-1
    int border = ( 1 << (maxLev + 1) ) - 1;
    regionOfInterestPixels.x1 -= border;
    regionOfInterestPixels.y1 -= border;
    regionOfInterestPixels.x2 += border;
    regionOfInterestPixels.y2 += border;

    OfxRectD srcRoI;
    Coords::toCanonical(regionOfInterestPixels, args.renderScale, par, &srcRoI);

    // intersect with srcRoD
    Coords::rectIntersection(srcRoI, srcRod, &srcRoI);
    rois.setRegionOfInterest(*_srcClip, srcRoI);
}

bool
DenoiseWaveletPlugin::isIdentity(const IsIdentityArguments &args,
                                 Clip * &identityClip,
                                 double & /*identityTime*/
                                 , int& /*view*/, std::string& /*plane*/)
{
    //std::cout << "isIdentity!\n";
    const double time = args.time;

    if (kLevelMax - startLevelFromRenderScale(args.renderScale) < 0) {
        // renderScale is too low for denoising
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

    if ( processA && (_alphaThreshold->getValueAtTime(time) > 0.) ) {
        return false;
    }

    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    double ylrThreshold = _ylrThreshold->getValueAtTime(time);
    double cbagThreshold = _cbagThreshold->getValueAtTime(time);
    double crbbThreshold = _crbbThreshold->getValueAtTime(time);
    double alphaThreshold = _alphaThreshold->getValueAtTime(time);
    double ylrSoftness = _ylrSoftness->getValueAtTime(time);
    double cbagSoftness = _cbagSoftness->getValueAtTime(time);
    double crbbSoftness = _crbbSoftness->getValueAtTime(time);
    double alphaSoftness = _alphaSoftness->getValueAtTime(time);
    double sharpenAmount = _sharpenAmount->getValueAtTime(time);
    if ( ( (colorModel == eColorModelRGB) || (colorModel == eColorModelLinearRGB) ) &&
         ( !processR || (ylrThreshold <= 0.) || (ylrSoftness == 1.) ) &&
         ( !processG || (cbagThreshold <= 0.) || (cbagSoftness == 1.) ) &&
         ( !processR || (crbbThreshold <= 0.) || (crbbSoftness == 1.) ) &&
         ( !processA || (alphaThreshold <= 0.) || (alphaSoftness == 1.) ) &&
         (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    } else if ( ( (!processR && !processG && !processB) ||
                  ( (ylrThreshold <= 0.) &&
                    (cbagThreshold <= 0.) &&
                    (crbbThreshold <= 0.) ) ||
                  ( (ylrSoftness == 1.) &&
                    (cbagSoftness == 1.) &&
                    (crbbSoftness == 1.) ) ) &&
                ( !processA || (alphaThreshold <= 0.) || (alphaSoftness == 1.) ) &&
                (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert = _maskInvert->getValueAtTime(time);
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

    //std::cout << "isIdentity! false\n";
    return false;
} // DenoiseWaveletPlugin::isIdentity

void
DenoiseWaveletPlugin::changedClip(const InstanceChangedArgs &args,
                                  const std::string &clipName)
{
    //std::cout << "changedClip!\n";
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
    //std::cout << "changedClip OK!\n";
}

void
DenoiseWaveletPlugin::changedParam(const InstanceChangedArgs &args,
                                   const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    } else if (paramName == kParamColorModel) {
        updateLabels();
    } else if (paramName == kParamAdaptive) {
        // adaptive denoising does not support tiles
        setSupportsTiles( !_adaptive->getValueAtTime(args.time) );
    }
}

void
DenoiseWaveletPlugin::updateLabels()
{
    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValue();

    switch (colorModel) {
    case eColorModelYCbCr: {
        _ylrThreshold->setLabel(kParamYThresholdLabel);
        _ylrSoftness->setLabel(kParamYSoftnessLabel);
        _cbagThreshold->setLabel(kParamCbThresholdLabel);
        _cbagSoftness->setLabel(kParamCbSoftnessLabel);
        _crbbThreshold->setLabel(kParamCrThresholdLabel);
        _crbbSoftness->setLabel(kParamCrSoftnessLabel);
        break;
    }
    case eColorModelLab: {
        _ylrThreshold->setLabel(kParamLThresholdLabel);
        _ylrSoftness->setLabel(kParamLSoftnessLabel);
        _cbagThreshold->setLabel(kParamAThresholdLabel);
        _cbagSoftness->setLabel(kParamASoftnessLabel);
        _crbbThreshold->setLabel(kParamBThresholdLabel);
        _crbbSoftness->setLabel(kParamBSoftnessLabel);
        break;
    }
    case eColorModelRGB:
    case eColorModelLinearRGB: {
        _ylrThreshold->setLabel(kParamRThresholdLabel);
        _ylrSoftness->setLabel(kParamRSoftnessLabel);
        _cbagThreshold->setLabel(kParamGThresholdLabel);
        _cbagSoftness->setLabel(kParamGSoftnessLabel);
        _crbbThreshold->setLabel(kParamBThresholdLabel);
        _crbbSoftness->setLabel(kParamBSoftnessLabel);
        break;
    }
    }
}

class DenoiseWaveletPluginFactory
    : public PluginFactoryHelper<DenoiseWaveletPluginFactory>
{
public:

    DenoiseWaveletPluginFactory(const std::string& id,
                                unsigned int verMaj,
                                unsigned int verMin)
        : PluginFactoryHelper<DenoiseWaveletPluginFactory>(id, verMaj, verMin)
        , _lut(NULL)
    {
    }

    virtual void load() OVERRIDE FINAL { _lut = Color::LutManager<Mutex>::Rec709Lut(); ofxsThreadSuiteCheck(); }

    virtual void unload()
    {
        Color::LutManager<Mutex>::releaseLut( _lut->getName() );
    }

    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context);
    virtual void describe(ImageEffectDescriptor &desc);
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context);

private:
    const Color::LutBase* _lut;
};

void
DenoiseWaveletPluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
DenoiseWaveletPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                               ContextEnum context)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

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
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
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
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // describe plugin params
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamColorModel);
        param->setLabel(kParamColorModelLabel);
        param->setHint(kParamColorModelHint);
        param->setAnimates(false);
        assert(param->getNOptions() == (int)eColorModelYCbCr);
        param->appendOption(kParamColorModelOptionYCbCr, kParamColorModelOptionYCbCrHint);
        assert(param->getNOptions() == (int)eColorModelLab);
        param->appendOption(kParamColorModelOptionLab, kParamColorModelOptionLabHint);
        assert(param->getNOptions() == (int)eColorModelRGB);
        param->appendOption(kParamColorModelOptionRGB, kParamColorModelOptionRGBHint);
        assert(param->getNOptions() == (int)eColorModelLinearRGB);
        param->appendOption(kParamColorModelOptionLinearRGB, kParamColorModelOptionLinearRGBHint);
        param->setDefault( (int)eColorModelYCbCr );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupSettings);
        if (group) {
            group->setLabel(kGroupSettingsLabel);
            //group->setHint(kGroupSettingsHint);
            group->setEnabled(true);
            if (page) {
                page->addChild(*group);
            }
        }

        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamYLRThreshold);
            param->setLabel(kParamYLRThresholdLabel);
            param->setHint(kParamThresholdHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamYLRSoftness);
            param->setLabel(kParamYLRSoftnessLabel);
            param->setHint(kParamSoftnessHint);
            param->setRange(0, 1.);
            param->setDisplayRange(0, 1.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCbAGThreshold);
            param->setLabel(kParamCbAGThresholdLabel);
            param->setHint(kParamThresholdHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCbAGSoftness);
            param->setLabel(kParamCbAGSoftnessLabel);
            param->setHint(kParamSoftnessHint);
            param->setRange(0, 1.);
            param->setDisplayRange(0, 1.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCrBBThreshold);
            param->setLabel(kParamCrBBThresholdLabel);
            param->setHint(kParamThresholdHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCrBBSoftness);
            param->setLabel(kParamCrBBSoftnessLabel);
            param->setHint(kParamSoftnessHint);
            param->setRange(0, 1.);
            param->setDisplayRange(0, 1.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAlphaThreshold);
            param->setLabel(kParamAlphaThresholdLabel);
            param->setHint(kParamThresholdHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAlphaSoftness);
            param->setLabel(kParamAlphaSoftnessLabel);
            param->setHint(kParamSoftnessHint);
            param->setRange(0, 1.);
            param->setDisplayRange(0, 1.);
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
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAdaptive);
        param->setLabel(kParamAdaptiveLabel);
        param->setHint(kParamAdaptiveHint);
        param->setDefault(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
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
            param->setDisplayRange(0, 10.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }

        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSharpenRadius);
            param->setLabel(kParamSharpenRadiusLabel);
            param->setHint(kParamSharpenRadiusHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 2.);
            param->setDefault(0.5);
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
        if (page) {
            page->addChild(*param);
        }
    }
    //std::cout << "describeInContext! OK\n";
} // DenoiseWaveletPluginFactory::describeInContext

ImageEffect*
DenoiseWaveletPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            ContextEnum /*context*/)
{
    return new DenoiseWaveletPlugin(handle, _lut);
}

static DenoiseWaveletPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
