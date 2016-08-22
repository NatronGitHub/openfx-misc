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

#ifdef _OPENMP
#include <omp.h>
#define _GLIBCXX_PARALLEL // enable libstdc++ parallel STL algorithm (eg nth_element, sort...)
#endif
#include <cmath>
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"
#include "ofxsMultiThread.h"
#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
typedef OFX::MultiThread::Mutex Mutex;
typedef OFX::MultiThread::AutoMutex AutoMutex;
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

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DenoiseSharpen"
#define kPluginGrouping "Filter"
#define kPluginDescriptionShort \
"Denoise and/or sharpen images using wavelet-based algorithms.\n" \
"This plugin allows the separate denoising of image channels in multiple color spaces using wavelets, using the BayesShrink algorithm, and can also sharpen the image details.\n" \
"Noise levels for each channel may be either set manually, or analyzed from the image data using the MAD (median absolute deviation) estimator.\n" \
"Noise analysis is based on a Gaussian noise assumption. If there is also speckle noise in the images, the Median or SmoothPatchBased filters may be more appropriate.\n" \
"The color model specifies the channels and the transforms used. Noise levels have to be re-adjusted or re-analyzed when changing the color model."

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

#define kSupportsTiles 0
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
#define kParamColorModelHint "The colorspace where denoising is performed. Noise levels must be re-adjusted, or the noise analysis must be re-run, when the color model is changed."
#define kParamColorModelOptionYCbCr "Y'CbCr(A)"
#define kParamColorModelOptionYCbCrHint "The YCbCr color model has one luminance channel (Y) which contains most of the detail information of an image (such as brightness and contrast) and two chroma channels (Cb = blueness, Cr = reddness) that hold the color information. Note that this choice drastically affects the result."
#define kParamColorModelOptionLab "CIE L*a*b(A)"
#define kParamColorModelOptionLabHint "CIE L*a*b* is a color model in which chrominance is separated from lightness and color distances are perceptually uniform. Note that this choice drastically affects the result."
#define kParamColorModelOptionRGB "R'G'B'(A)"
#define kParamColorModelOptionRGBHint "The R'G'B' color model (gamma-corrected RGB) separates an image into channels of red, green, and blue. Note that this choice drastically affects the result."
#define kParamColorModelOptionLinearRGB "RGB(A)"
#define kParamColorModelOptionLinearRGBHint "The Linear RGB color model processes the raw linear components. Usually a bad choice, except if RGB do not represent color data."
enum ColorModelEnum {
    eColorModelYCbCr = 0,
    eColorModelLab,
    eColorModelRGB,
    eColorModelLinearRGB,
};

#define kParamNoiseLevelHint "Adjusts the noise variance of the selected channel. May be estimated for image data by pressing the \"Analyze Noise\" button."
#define kParamNoiseLevelMax 0.3 // noise level is at most 1/sqrt(12) (stddev of a uniform distribution between 0 and 1)

#define kNoiseLevelBias (noise[0]) // on a signal with Gaussian additive noise with sigma = 1, the stddev measured in HH1 is 0.8002. We correct this bias so that the displayed Noise levels correspond to the standard deviation of the additive Gaussian noise. This value can also be found in the dcraw source code

#define kParamAmountHint "The amount of denoising to apply to the specify channel. Default is 1."
#define kGroupSettings "channelSettings"
#define kGroupSettingsLabel "Channel Settings"
#define kParamYLRNoiseLevel "ylrNoiseLevel"
#define kParamYLRNoiseLevelLabel "Y/L/R Noise Level"
#define kParamYNoiseLevelLabel "Y Noise Level"
#define kParamLNoiseLevelLabel "L Noise Level"
#define kParamRNoiseLevelLabel "R Noise Level"
#define kParamYLRAmount "ylrAmount"
#define kParamYLRAmountLabel "Y/L/R Amount"
#define kParamYAmountLabel "Y Amount"
#define kParamLAmountLabel "L Amount"
#define kParamRAmountLabel "R Amount"
#define kParamCbAGNoiseLevel "cbagNoiseLevel"
#define kParamCbAGNoiseLevelLabel "Cb/A/G Noise Level"
#define kParamCbNoiseLevelLabel "Cb Noise Level"
#define kParamANoiseLevelLabel "A Noise Level"
#define kParamGNoiseLevelLabel "G Noise Level"
#define kParamCbAGAmount "cbagAmount"
#define kParamCbAGAmountLabel "Cb/A/G Amount"
#define kParamCbAmountLabel "Cb Amount"
#define kParamAAmountLabel "A Amount"
#define kParamGAmountLabel "G Amount"
#define kParamCrBBNoiseLevel "crbbNoiseLevel"
#define kParamCrBBNoiseLevelLabel "Cr/B/B Noise Level"
#define kParamCrNoiseLevelLabel "Cr Noise Level"
#define kParamBNoiseLevelLabel "B Noise Level"
#define kParamCrBBAmount "crbbAmount"
#define kParamCrBBAmountLabel "Cr/B/B Amount"
#define kParamCrAmountLabel "Cr Amount"
#define kParamBAmountLabel "B Amount"

#define kParamAlphaNoiseLevel "alphaNoiseLevel"
#define kParamAlphaNoiseLevelLabel "Alpha Noise Level"
#define kParamAlphaAmount "alphaAmount"
#define kParamAlphaAmountLabel "Alpha Amount"

#define kParamAnalyzeNoiseLevels "analyzeNoiseLevels"
#define kParamAnalyzeNoiseLevelsLabel "Analyze Noise Levels"
#define kParamAnalyzeNoiseLevelsHint "Computes the noise levels from the current frame and current color model. To use the same settings for the whole sequence, analyze a frame that is representative of the sequence. If a mask is set, it is used to compute the noise levels from areas where the mask is non-zero. If there are keyframes on the noise level parameters, this sets a keyframe at the current frame. The noise levels can then be fine-tuned."

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


// those are the noise levels on HHi subands that correspond to a
// Gaussian noise, with the dcraw "a trous" wavelets.
static const float noise[] = { 0.8002,0.2735,0.1202,0.0585,0.0291,0.0152,0.0080,0.0044 };

#ifdef _OPENMP
#define abort_test() if (!omp_get_thread_num() && abort()) { OFX::throwSuiteStatusException(kOfxStatFailed); }
#define abort_test_loop() if (abort()) { if (!omp_get_thread_num()) OFX::throwSuiteStatusException(kOfxStatFailed); else continue; }
#define abort_test() ((void)0)
#define abort_test_loop() ((void)0)
#else
#define abort_test() if (abort()) { OFX::throwSuiteStatusException(kOfxStatFailed); }
#define abort_test_loop() abort_test()
#endif



////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DenoiseSharpenPlugin
    : public OFX::ImageEffect
{
    struct Params;
public:

    /** @brief ctor */
    DenoiseSharpenPlugin(OfxImageEffectHandle handle, const OFX::Color::LutBase* lut)
        : ImageEffect(handle)
        , _lut(lut)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _colorModel(0)
        , _ylrNoiseLevel(0)
        , _ylrAmount(0)
        , _cbagNoiseLevel(0)
        , _cbagAmount(0)
        , _crbbNoiseLevel(0)
        , _crbbAmount(0)
        , _alphaNoiseLevel(0)
        , _alphaAmount(0)
        , _premult(0)
        , _premultChannel(0)
        , _mix(0)
        , _maskApply(0)
        , _maskInvert(0)
        , _premultChanged(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // TODO: fetch noise parameters

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);

        _colorModel = fetchChoiceParam(kParamColorModel);
        _ylrNoiseLevel = fetchDoubleParam(kParamYLRNoiseLevel);
        _ylrAmount = fetchDoubleParam(kParamYLRAmount);
        _cbagNoiseLevel = fetchDoubleParam(kParamCbAGNoiseLevel);
        _cbagAmount = fetchDoubleParam(kParamCbAGAmount);
        _crbbNoiseLevel = fetchDoubleParam(kParamCrBBNoiseLevel);
        _crbbAmount = fetchDoubleParam(kParamCrBBAmount);
        _alphaNoiseLevel = fetchDoubleParam(kParamAlphaNoiseLevel);
        _alphaAmount = fetchDoubleParam(kParamAlphaAmount);
        _sharpenAmount = fetchDoubleParam(kParamSharpenAmount);
        _sharpenRadius = fetchDoubleParam(kParamSharpenRadius);
        _sharpenLuminance = fetchBooleanParam(kParamSharpenLuminance);

        assert(_colorModel && _ylrNoiseLevel && _ylrAmount && _cbagNoiseLevel && _cbagAmount && _crbbNoiseLevel && _crbbAmount && _alphaNoiseLevel && _alphaAmount && _sharpenAmount && _sharpenRadius && _sharpenLuminance);

        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        // update the channel labels
        updateLabels();
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    void setup(const OFX::RenderArguments &args,
               std::auto_ptr<const OFX::Image>& src,
               std::auto_ptr<OFX::Image>& dst,
               std::auto_ptr<const OFX::Image>& mask,
               Params& p);

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void analyzeNoiseLevels(const OFX::InstanceChangedArgs &args);

    template<int nComponents>
    void analyzeNoiseLevelsForComponents(const OFX::InstanceChangedArgs &args);

    template <class PIX, int nComponents, int maxValue>
    void analyzeNoiseLevelsForBitDepth(const OFX::InstanceChangedArgs &args);
    

    void updateLabels();

    void wavelet_denoise(float *fimg[3], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] and fimg[2] are working space images of the same size
                         unsigned int iwidth, //!< width of the image
                         unsigned int iheight, //!< height of the image
                         float noiselevel, //!< noiselevel parameter
                         double denoise_amount, //!< amount parameter
                         double sharpen_amount, //!< constrast boost amount
                         double sharpen_radius, //!< contrast boost radius
                         int startLevel,
                         float a, // progress amount at start
                         float b); // progress increment

    double sigma_mad(float *fimg[2], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] is a working space image of the same size
                     bool *bimgmask,
                     unsigned int iwidth, //!< width of the image
                     unsigned int iheight, //!< height of the image
                     float a, //!< progress amount at start
                     float b); //!< progress increment

private:
    struct Params
    {
        bool doMasking;
        bool maskInvert;
        bool premult;
        int premultChannel;
        double mix;
        ColorModelEnum colorModel;
        int startLevel;
        bool process[4];
        double noiseLevel[4];
        double denoise_amount[4];
        double sharpen_amount[4];
        double sharpen_radius;
        OfxRectI srcWindow;

        Params()
        : doMasking(false)
        , maskInvert(false)
        , premult(false)
        , premultChannel(3)
        , mix(1.)
        , colorModel(eColorModelYCbCr)
        , startLevel(0)
        , sharpen_radius(0.5)
        {
            process[0] = process[1] = process[2] = process[3] = true;
            noiseLevel[0] = noiseLevel[1] = noiseLevel[2] = noiseLevel[3] = 0.;
            denoise_amount[0] = denoise_amount[1] = denoise_amount[2] = denoise_amount[3] = 0.;
            sharpen_amount[0] = sharpen_amount[1] = sharpen_amount[2] = sharpen_amount[3] = 0.;
        }
    };

    const OFX::Color::LutBase* _lut;

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    ChoiceParam* _colorModel;
    DoubleParam* _ylrNoiseLevel;
    DoubleParam* _ylrAmount;
    DoubleParam* _cbagNoiseLevel;
    DoubleParam* _cbagAmount;
    DoubleParam* _crbbNoiseLevel;
    DoubleParam* _crbbAmount;
    DoubleParam* _alphaNoiseLevel;
    DoubleParam* _alphaAmount;
    DoubleParam* _sharpenAmount;
    DoubleParam* _sharpenRadius;
    BooleanParam* _sharpenLuminance;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
    OFX::BooleanParam* _premultChanged; // set to true the first time the user connects src
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
    for (i=0; i < sc; i++)
        temp[i] = 2*base[st*i] + base[st*(sc-i)] + base[st*(i+sc)];
    for (; i+sc < size; i++)
        temp[i] = 2*base[st*i] + base[st*(i-sc)] + base[st*(i+sc)];
    for (; i < size; i++)
        temp[i] = 2*base[st*i] + base[st*(i-sc)] + base[st*(2*size-2-(i+sc))];
}

// "A trous" algorithm with a linear interpolation filter.
// from dcraw/UFRaw/LibRaw, with enhancements from GIMP wavelet denoise
// https://sourceforge.net/p/ufraw/mailman/message/24069162/
void
DenoiseSharpenPlugin::wavelet_denoise(float *fimg[3], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] and fimg[2] are working space images of the same size
                                      unsigned int iwidth, //!< width of the image
                                      unsigned int iheight, //!< height of the image
                                      float noiselevel, //!< noiselevel parameter
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

    if ( (noiselevel <= 0. || denoise_amount <= 0.) && sharpen_amount <= 0. ) {
        return;
    }

    const unsigned int size = iheight * iwidth;

    int hpass = 0;
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
            abort_test_loop();
            float* temp = new float[iwidth];
            hat_transform (temp, fimg[hpass] + row * iwidth, 1, iwidth, 1 << lev);
            for (unsigned int col = 0; col < iwidth; ++col) {
                fimg[lpass][row * iwidth + col] = temp[col] * 0.25;
            }
            delete [] temp;
        }
        abort_test();
        if (b != 0) {
            //progressUpdate(a + b * (lev + 0.25) / 5.0);
        }

        // b- smooth cols, result is in fimg[lpass]
        // compute HHi + its variance
        double sumsq = 0.;
#ifdef _OPENMP
#pragma omp parallel for reduction (+:sumsq)
#endif
        for (unsigned int col = 0; col < iwidth; ++col) {
            abort_test_loop();
            float* temp = new float[iheight];
            hat_transform (temp, fimg[lpass] + col, iwidth, iheight, 1 << lev);
            double sumsqrow = 0.;
            for (unsigned int row = 0; row < iheight; ++row) {
                unsigned int i = row * iwidth + col;
                fimg[lpass][i] = temp[row] * 0.25;
                // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
                fimg[hpass][i] -= fimg[lpass][i];
                sumsqrow += fimg[hpass][i] * fimg[hpass][i];
            }
            sumsq += sumsqrow;
            delete [] temp;
        }
        abort_test();
        if (b != 0) {
            //progressUpdate(a + b * (lev + 0.5) / 5.0);
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
        double sigma_n_i = noiselevel * noise[lev + startLevel];
        double sigma_n_i_sq = sigma_n_i * sigma_n_i;

        float thold = sigma_n_i_sq / std::sqrt( std::max(1e-30, sumsq / size - sigma_n_i_sq) );

        // uncomment to check the values of the noise[] array
        //printf("level=%u stdev=%g noiselevel=%g\n", lev, std::sqrt(sumsq / size), noiselevel);

        // sharpen
        double beta = 1.;
        if (sharpen_amount > 0.) {
            beta += sharpen_amount * exp (-((lev + startLevel) - sharpen_radius) * ((lev + startLevel) - sharpen_radius) / 1.5);
        }

#ifdef _OPENMP
#pragma omp parallel for
#endif
        for (unsigned int i = 0; i < size; ++i) {
            abort_test_loop();
            // apply smooth threshold
            if (fimg[hpass][i] < -thold) {
                fimg[hpass][i] += thold * denoise_amount;
            } else if (fimg[hpass][i] >  thold) {
                fimg[hpass][i] -= thold * denoise_amount;
            } else {
                fimg[hpass][i] *= 1. - denoise_amount;
            }
            // add the denoised band to the final image
            if (hpass) {
                // note: local contrast boost could be applied here, by multiplying fimg[hpass][i] by a factor beta
                // GIMP's wavelet sharpen uses beta = amount * exp (-(lev - radius) * (lev - radius) / 1.5) + 1

                fimg[0][i] += beta * fimg[hpass][i];
            }
        }
        hpass = lpass;
    } // for(lev)

    // add the last smoothed image to the image
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (unsigned int i = 0; i < size; ++i) {
        fimg[0][i] += fimg[lpass][i];
    }
} // wavelet_denoise


double
DenoiseSharpenPlugin::sigma_mad(float *fimg[2], //!< fimg[0] is the channel to process with intensities between 0. and 1., of size iwidth*iheight, fimg[1] is a working space image of the same size
                                bool *bimgmask,
                                unsigned int iwidth, //!< width of the image
                                unsigned int iheight, //!< height of the image
                                float a, // progress amount at start
                                float b) // progress increment
{
    // compute sigma_n using the MAD (median absolute deviation at the finest level:
    // sigma_n = median(|d_0|)/0.6745 (could be computed in an analysis step from the first detail subband)

    const unsigned int size = iheight * iwidth;

    // smooth fimg[hpass], result is in fimg[lpass]:
    // a- smooth rows, result is in fimg[lpass]
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (unsigned int row = 0; row < iheight; ++row) {
        float* temp = new float[iwidth];
        abort_test_loop();
        hat_transform (temp, fimg[0] + row * iwidth, 1, iwidth, 1);
        for (unsigned int col = 0; col < iwidth; ++col) {
            unsigned int i = row * iwidth + col;
            fimg[1][i] = temp[col] * 0.25;
        }
        delete [] temp;
    }
    abort_test();
    if (b != 0) {
        //progressUpdate(a + b * 0.25);
    }

    // b- smooth cols, result is in fimg[lpass]
    // compute HH1
#ifdef _OPENMP
#pragma omp parallel for
#endif
    for (unsigned int col = 0; col < iwidth; ++col) {
        float* temp = new float[iheight];
        abort_test_loop();
        hat_transform (temp, fimg[1] + col, iwidth, iheight, 1);
        for (unsigned int row = 0; row < iheight; ++row) {
            unsigned int i = row * iwidth + col;
            // compute band-pass image as: (smoothed at this lev)-(smoothed at next lev)
            // take the absolute value to compute MAD
            fimg[1][i] = std::abs(fimg[0][i] - temp[row] * 0.25);
        }
        delete [] temp;
    }
    abort_test();
    if (b != 0) {
        //progressUpdate(a + b * 0.5);
    }
    unsigned int n = size;
    if (bimgmask) {
        n= 0;
        for (unsigned int i = 0; i < size; ++i) {
            if (bimgmask[i]) {
                fimg[1][n] = fimg[1][i];
                ++n;
            }
        }
    }
    abort_test();
    if (n != 0) {
        std::nth_element(&fimg[1][0], &fimg[1][n/2], &fimg[1][n]);
    }
    if (b != 0) {
        //progressUpdate(a + b);
    }

    return n == 0 ? 0. : fimg[1][size/2] / 0.6745;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from
// the overridden render function
void
DenoiseSharpenPlugin::render(const OFX::RenderArguments &args)
{
#ifdef _OPENMP
    // set the number of OpenMP threads to a reasonable value
    // (but remember that the OpenMP threads are not counted my the multithread suite)
    omp_set_num_threads( OFX::MultiThread::getNumCPUs() );
#endif

    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB /*|| dstComponents == OFX::ePixelComponentXY*/ || dstComponents == OFX::ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
    case OFX::ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case OFX::ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    //case OFX::ePixelComponentXY:
    //    renderForComponents<2>(args);
    //    break;
    case OFX::ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
    default:
        //std::cout << "components usupported\n";
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
DenoiseSharpenPlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case OFX::eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case OFX::eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case OFX::eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        //std::cout << "depth usupported\n";
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}


void
DenoiseSharpenPlugin::setup(const OFX::RenderArguments &args,
                            std::auto_ptr<const OFX::Image>& src,
                            std::auto_ptr<OFX::Image>& dst,
                            std::auto_ptr<const OFX::Image>& mask,
                            Params& p)
{
    const double time = args.time;
    dst.reset( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
        ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
        ( dst->getRenderScale().y != args.renderScale.y) ||
        ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    src.reset( ( _srcClip && _srcClip->isConnected() ) ?
              _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
            ( src->getRenderScale().y != args.renderScale.y) ||
            ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    p.doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    mask.reset(p.doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
            ( mask->getRenderScale().y != args.renderScale.y) ||
            ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    p.maskInvert = false;
    if (p.doMasking) {
        _maskInvert->getValueAtTime(time, p.maskInvert);
    }

    p.premult = _premult->getValueAtTime(time);
    p.premultChannel = _premultChannel->getValueAtTime(time);
    p.mix = _mix->getValueAtTime(time);

    p.process[0] = _processR->getValueAtTime(time);
    p.process[1] = _processG->getValueAtTime(time);
    p.process[2] = _processB->getValueAtTime(time);
    p.process[3] = _processA->getValueAtTime(time);

    // fetch parameter values
    p.colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    p.startLevel = startLevelFromRenderScale(args.renderScale);
    p.noiseLevel[0] = _ylrNoiseLevel->getValueAtTime(time);
    p.noiseLevel[1] = _cbagNoiseLevel->getValueAtTime(time);
    p.noiseLevel[2] = _crbbNoiseLevel->getValueAtTime(time);
    p.noiseLevel[3] = _alphaNoiseLevel->getValueAtTime(time);
    p.denoise_amount[0] = _ylrAmount->getValueAtTime(time);
    p.denoise_amount[1] = _cbagAmount->getValueAtTime(time);
    p.denoise_amount[2] = _crbbAmount->getValueAtTime(time);
    p.denoise_amount[3] = _alphaAmount->getValueAtTime(time);
    p.sharpen_amount[0] = _sharpenAmount->getValueAtTime(time);
    p.sharpen_radius = _sharpenRadius->getValueAtTime(time);
    bool sharpenLuminance = _sharpenLuminance->getValueAtTime(time);

    if (!sharpenLuminance) {
        p.sharpen_amount[1] = p.sharpen_amount[2] = p.sharpen_amount[3] = p.sharpen_amount[0];
    } else if (p.colorModel == eColorModelRGB || p.colorModel == eColorModelLinearRGB) {
        p.sharpen_amount[1] = p.sharpen_amount[2] = p.sharpen_amount[0]; // cannot sharpen luminance only
    }

    if (p.colorModel == eColorModelRGB || p.colorModel == eColorModelLinearRGB) {
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && ((p.noiseLevel[c] > 0 && p.denoise_amount[c] > 0.) || p.sharpen_amount[c] > 0.);
        }
    } else {
        bool processcolor = false;
        for (int c = 0; c < 3; ++c) {
            processcolor = processcolor || ((p.noiseLevel[c] > 0 && p.denoise_amount[c] > 0.) || p.sharpen_amount[c] > 0.);
        }
        for (int c = 0; c < 3; ++c) {
            p.process[c] = p.process[c] && processcolor;
        }
    }
    p.process[3] = p.process[3] && ((p.noiseLevel[3] > 0 && p.denoise_amount[3] > 0.) || p.sharpen_amount[3] > 0.);
    
    // compute the number of levels (max is 4, which adds 1<<4 = 16 pixels on each side)
    int maxLev = std::max(0, kLevelMax - startLevelFromRenderScale(args.renderScale));
    // hat_transform gets the pixel at x+-(1<<maxLev), which is computex from x+-(1<<(maxLev-1)), etc...
    // We thus need pixels at x +- (1<<(maxLev+1))-1
    int border = (1 << (maxLev+1)) - 1;
    p.srcWindow.x1 = args.renderWindow.x1 - border;
    p.srcWindow.y1 = args.renderWindow.y1 - border;
    p.srcWindow.x2 = args.renderWindow.x2 + border;
    p.srcWindow.y2 = args.renderWindow.y2 + border;

    // intersect with srcBounds
    OFX::Coords::rectIntersection(p.srcWindow, src->getBounds(), &p.srcWindow);
}

template <class PIX, int nComponents, int maxValue>
void
DenoiseSharpenPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    std::auto_ptr<const OFX::Image> src;
    std::auto_ptr<OFX::Image> dst;
    std::auto_ptr<const OFX::Image> mask;
    Params p;

    setup(args, src, dst, mask, p);

    const OfxRectI& procWindow = args.renderWindow;

    
    // temporary buffers: one for each channel plus 2 for processing
    unsigned int iwidth = p.srcWindow.x2 - p.srcWindow.x1;
    unsigned int iheight = p.srcWindow.y2 - p.srcWindow.y1;
    unsigned int isize = iwidth * iheight;
    std::auto_ptr<OFX::ImageMemory> tmpData( new OFX::ImageMemory(sizeof(float) * isize * (nComponents + 2), this) );
    float* tmpPixelData = (float*)tmpData->lock();
    float* fimgcolor[3] = { NULL, NULL, NULL };
    float* fimgalpha = NULL;
    float *fimgtmp[2] = { NULL, NULL };
    fimgcolor[0] = (nComponents != 1) ? tmpPixelData : NULL;
    fimgcolor[1] = (nComponents != 1) ? tmpPixelData + isize : NULL;
    fimgcolor[2] = (nComponents != 1) ? tmpPixelData + 2*isize : NULL;
    fimgalpha = (nComponents == 1) ? tmpPixelData : ((nComponents == 4) ? tmpPixelData + 3*isize : NULL);
    fimgtmp[0] = tmpPixelData + nComponents * isize;
    fimgtmp[1] = tmpPixelData + (nComponents + 1) * isize;

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
            if ( nComponents != 1 && (p.process[0] || p.process[1] || p.process[2]) ) {
                if (p.colorModel == eColorModelLab) {
                    if (sizeof(PIX) == 1) {
                        // convert to linear
                        for (int c = 0; c < 3; ++c) {
                            unpPix[c] = _lut->fromColorSpaceFloatToLinearFloat(unpPix[c]);
                        }
                    }
                    OFX::Color::rgb_to_lab(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
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
                        OFX::Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                        // bring to the 0-1 range
                        unpPix[1] += 0.5;
                        unpPix[2] += 0.5;
                    }
                }
                // store in tmpPixelData
                for (int c = 0; c < 3; ++c) {
                    if (!(p.colorModel == eColorModelRGB || p.colorModel == eColorModelLinearRGB) || p.process[c]) {
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

    if ( nComponents != 1 && (p.process[0] || p.process[1] || p.process[2]) ) {
        // process color channels
        for (int c = 0; c < 3; ++c) {
            if (!(p.colorModel == eColorModelRGB || p.colorModel == eColorModelLinearRGB) || p.process[c]) {
                assert(fimgcolor[c]);
                float* fimg[3] = { fimgcolor[c], fimgtmp[0], fimgtmp[1] };
                abort_test();
                wavelet_denoise(fimg, iwidth, iheight, p.noiseLevel[c], p.denoise_amount[c], p.sharpen_amount[c], p.sharpen_radius, p.startLevel, (float)c / nComponents, 1.f / nComponents);
            }
        }
    }
    if (nComponents != 3 && p.process[3]) {
        assert(fimgalpha);
        // process alpha
        float* fimg[3] = { fimgalpha, fimgtmp[0], fimgtmp[1] };
        abort_test();
        wavelet_denoise(fimg, iwidth, iheight, p.noiseLevel[3], p.denoise_amount[3], p.sharpen_amount[3], p.sharpen_radius, p.startLevel, (float)(nComponents-1) / nComponents, 1.f / nComponents);
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
                    // back to normal Lab
                    tmpPix[0] = (tmpPix[0] - 0 * 16 * 27 / 24389.0) * 116;
                    tmpPix[1] = (tmpPix[1] - 0.5) * 500 * 2;
                    tmpPix[2] = (tmpPix[2] - 0.5) * 200 * 2.2;

                    OFX::Color::lab_to_rgb(tmpPix[0], tmpPix[1], tmpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
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
                        OFX::Color::ypbpr709_to_rgb(tmpPix[0], tmpPix[1], tmpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
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
}


// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DenoiseSharpenPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                      OFX::RegionOfInterestSetter &rois)
{
    if (!_srcClip) {
        return;
    }
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(args.time);
    if ( OFX::Coords::rectIsEmpty(srcRod) || OFX::Coords::rectIsEmpty(args.regionOfInterest) ) {
        return;
    }

    // requires the full image to compute stats
    rois.setRegionOfInterest(*_srcClip, srcRod);
}

bool
DenoiseSharpenPlugin::isIdentity(const IsIdentityArguments &args,
                                 Clip * &identityClip,
                                 double & /*identityTime*/)
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

    if (processA && _alphaNoiseLevel->getValueAtTime(time) > 0.) {
        return false;
    }

    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);
    double ylrNoiseLevel = _ylrNoiseLevel->getValueAtTime(time);
    double cbagNoiseLevel = _cbagNoiseLevel->getValueAtTime(time);
    double crbbNoiseLevel = _crbbNoiseLevel->getValueAtTime(time);
    double alphaNoiseLevel = _alphaNoiseLevel->getValueAtTime(time);
    double ylrAmount = _ylrAmount->getValueAtTime(time);
    double cbagAmount = _cbagAmount->getValueAtTime(time);
    double crbbAmount = _crbbAmount->getValueAtTime(time);
    double alphaAmount = _alphaAmount->getValueAtTime(time);
    double sharpenAmount = _sharpenAmount->getValueAtTime(time);
    if ( (colorModel == eColorModelRGB || colorModel == eColorModelLinearRGB) &&
         (!processR || ylrNoiseLevel <= 0. || ylrAmount == 0.) &&
         (!processG || cbagNoiseLevel <= 0. || cbagAmount == 0.) &&
         (!processR || crbbNoiseLevel <= 0. || crbbAmount == 0.) &&
         (!processA || alphaNoiseLevel <= 0. || alphaAmount == 0.) &&
         (sharpenAmount <= 0.) ) {
        identityClip = _srcClip;

        return true;
    } else if ( ( (!processR && !processG && !processB) ||
                  ( (ylrNoiseLevel <= 0.) &&
                    (cbagNoiseLevel <= 0.) &&
                    (crbbNoiseLevel <= 0.) ) ||
                  ( (ylrAmount == 0.) &&
                    (cbagAmount == 0.) &&
                    (crbbAmount == 0.) ) ) &&
                (!processA || alphaNoiseLevel <= 0. || alphaAmount == 0.) &&
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
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // DenoiseSharpenPlugin::isIdentity

void
DenoiseSharpenPlugin::changedClip(const InstanceChangedArgs &args,
                                      const std::string &clipName)
{
    //std::cout << "changedClip!\n";
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == OFX::eChangeUserEdit) ) {
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
DenoiseSharpenPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                                       const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == OFX::eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    } else if (paramName == kParamColorModel) {
        updateLabels();
    } else if (paramName == kParamAnalyzeNoiseLevels) {
        analyzeNoiseLevels(args);
    }
}

void
DenoiseSharpenPlugin::analyzeNoiseLevels(const OFX::InstanceChangedArgs &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

#ifdef _OPENMP
    // set the number of OpenMP threads to a reasonable value
    // (but remember that the OpenMP threads are not counted my the multithread suite)
    omp_set_num_threads( OFX::MultiThread::getNumCPUs() );
#endif

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB /*|| dstComponents == OFX::ePixelComponentXY*/ || dstComponents == OFX::ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
        case OFX::ePixelComponentRGBA:
            analyzeNoiseLevelsForComponents<4>(args);
            break;
        case OFX::ePixelComponentRGB:
            analyzeNoiseLevelsForComponents<3>(args);
            break;
            //case OFX::ePixelComponentXY:
            //    renderForComponents<2>(args);
            //    break;
        case OFX::ePixelComponentAlpha:
            analyzeNoiseLevelsForComponents<1>(args);
            break;
        default:
            //std::cout << "components usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
            break;
    } // switch
    //std::cout << "render! OK\n";
}

template<int nComponents>
void
DenoiseSharpenPlugin::analyzeNoiseLevelsForComponents(const OFX::InstanceChangedArgs &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
        case OFX::eBitDepthUByte:
            analyzeNoiseLevelsForBitDepth<unsigned char, nComponents, 255>(args);
            break;

        case OFX::eBitDepthUShort:
            analyzeNoiseLevelsForBitDepth<unsigned short, nComponents, 65535>(args);
            break;

        case OFX::eBitDepthFloat:
            analyzeNoiseLevelsForBitDepth<float, nComponents, 1>(args);
            break;
        default:
            //std::cout << "depth usupported\n";
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}



template <class PIX, int nComponents, int maxValue>
void
DenoiseSharpenPlugin::analyzeNoiseLevelsForBitDepth(const OFX::InstanceChangedArgs &args)
{
    const double time = args.time;

    std::auto_ptr<const OFX::Image> src;
    std::auto_ptr<const OFX::Image> mask;

    src.reset( ( _srcClip && _srcClip->isConnected() ) ?
              _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
            ( src->getRenderScale().y != args.renderScale.y) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    mask.reset(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
            ( mask->getRenderScale().y != args.renderScale.y) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    bool maskInvert = doMasking ? _maskInvert->getValueAtTime(time) : false;
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValueAtTime(time);

    const OfxRectI& srcWindow = src->getBounds();


    // temporary buffers: one for each channel plus 2 for processing
    unsigned int iwidth = srcWindow.x2 - srcWindow.x1;
    unsigned int iheight = srcWindow.y2 - srcWindow.y1;
    unsigned int isize = iwidth * iheight;
    std::auto_ptr<OFX::ImageMemory> tmpData( new OFX::ImageMemory(sizeof(float) * isize * (nComponents + 1), this) );
    float* tmpPixelData = (float*)tmpData->lock();
    float* fimgcolor[3] = { NULL, NULL, NULL };
    float* fimgalpha = NULL;
    float *fimgtmp = NULL;
    fimgcolor[0] = (nComponents != 1) ? tmpPixelData : NULL;
    fimgcolor[1] = (nComponents != 1) ? tmpPixelData + isize : NULL;
    fimgcolor[2] = (nComponents != 1) ? tmpPixelData + 2*isize : NULL;
    fimgalpha = (nComponents == 1) ? tmpPixelData : ((nComponents == 4) ? tmpPixelData + 3*isize : NULL);
    fimgtmp = tmpPixelData + nComponents * isize;
    std::auto_ptr<OFX::ImageMemory> maskData( doMasking ? new OFX::ImageMemory(sizeof(bool) * isize, this) : NULL );
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
            if ( nComponents != 1 ) {
                if (colorModel == eColorModelLab) {
                    if (sizeof(PIX) == 1) {
                        // convert to linear
                        for (int c = 0; c < 3; ++c) {
                            unpPix[c] = _lut->fromColorSpaceFloatToLinearFloat(unpPix[c]);
                        }
                    }
                    OFX::Color::rgb_to_lab(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                    // bring each component in the 0..1 range
                    unpPix[0] = unpPix[0] / 116.0 + 0 * 16 * 27 / 24389.0;
                    unpPix[1] = unpPix[1] / 500.0 / 2.0 + 0.5;
                    unpPix[2] = unpPix[2] / 200.0 / 2.2 + 0.5;
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
                        OFX::Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &unpPix[0], &unpPix[1], &unpPix[2]);
                        // bring to the 0-1 range
                        unpPix[1] += 0.5;
                        unpPix[2] += 0.5;
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

    if ( nComponents != 1 ) {
        // process color channels
        for (int c = 0; c < 3; ++c) {
            assert(fimgcolor[c]);
            float* fimg[2] = { fimgcolor[c], fimgtmp };
            double sigma_n = sigma_mad(fimg, bimgmask, iwidth, iheight, (float)c / nComponents, 1.f / nComponents);
            if (c == 0) {
                _ylrNoiseLevel->setValue(sigma_n / kNoiseLevelBias);
            } else if (c == 1) {
                _cbagNoiseLevel->setValue(sigma_n / kNoiseLevelBias);
            } else if (c == 2) {
                _crbbNoiseLevel->setValue(sigma_n / kNoiseLevelBias);
            }
        }
    }
    if (nComponents != 3) {
        assert(fimgalpha);
        // process alpha
        float* fimg[2] = { fimgalpha, fimgtmp };
        double sigma_n = sigma_mad(fimg, bimgmask, iwidth, iheight, (float)(nComponents-1) / nComponents, 1.f / nComponents);
        _alphaNoiseLevel->setValue(sigma_n / kNoiseLevelBias);
    }
}



void
DenoiseSharpenPlugin::updateLabels()
{
    ColorModelEnum colorModel = (ColorModelEnum)_colorModel->getValue();
    switch (colorModel) {
    case eColorModelYCbCr: {
        _ylrNoiseLevel->setLabel(kParamYNoiseLevelLabel);
        _cbagNoiseLevel->setLabel(kParamCbNoiseLevelLabel);
        _crbbNoiseLevel->setLabel(kParamCrNoiseLevelLabel);
        _ylrAmount->setLabel(kParamYAmountLabel);
        _cbagAmount->setLabel(kParamCbAmountLabel);
        _crbbAmount->setLabel(kParamCrAmountLabel);
        break;
    }
    case eColorModelLab: {
        _ylrNoiseLevel->setLabel(kParamLNoiseLevelLabel);
        _cbagNoiseLevel->setLabel(kParamANoiseLevelLabel);
        _crbbNoiseLevel->setLabel(kParamBNoiseLevelLabel);
        _ylrAmount->setLabel(kParamLAmountLabel);
        _cbagAmount->setLabel(kParamAAmountLabel);
        _crbbAmount->setLabel(kParamBAmountLabel);
        break;
    }
    case eColorModelRGB:
    case eColorModelLinearRGB: {
        _ylrNoiseLevel->setLabel(kParamRNoiseLevelLabel);
        _cbagNoiseLevel->setLabel(kParamGNoiseLevelLabel);
        _crbbNoiseLevel->setLabel(kParamBNoiseLevelLabel);
        _ylrAmount->setLabel(kParamRAmountLabel);
        _cbagAmount->setLabel(kParamGAmountLabel);
        _crbbAmount->setLabel(kParamBAmountLabel);
        break;
    }
    }
}

class DenoiseSharpenPluginFactory : public OFX::PluginFactoryHelper<DenoiseSharpenPluginFactory>
{
public:

    DenoiseSharpenPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin)
    : OFX::PluginFactoryHelper<DenoiseSharpenPluginFactory>(id, verMaj, verMin)
    , _lut(0)
    {
    }

    virtual void load()
    {
        _lut = OFX::Color::LutManager<Mutex>::Rec709Lut();
    }

    virtual void unload()
    {
        OFX::Color::LutManager<Mutex>::releaseLut(_lut->getName());
    }

    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
private:
    const OFX::Color::LutBase* _lut;
};

void
DenoiseSharpenPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
DenoiseSharpenPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                               OFX::ContextEnum context)
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // describe plugin params
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamColorModel);
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
        param->setDefault((int)eColorModelYCbCr);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kGroupSettings);
        if (group) {
            group->setLabel(kGroupSettingsLabel);
            //group->setHint(kGroupSettingsHint);
            group->setEnabled(true);
        }

        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamYLRNoiseLevel);
            param->setLabel(kParamYLRNoiseLevelLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCbAGNoiseLevel);
            param->setLabel(kParamCbAGNoiseLevelLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCrBBNoiseLevel);
            param->setLabel(kParamCrBBNoiseLevelLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAlphaNoiseLevel);
            param->setLabel(kParamAlphaNoiseLevelLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamYLRAmount);
            param->setLabel(kParamYLRAmountLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCbAGAmount);
            param->setLabel(kParamCbAGAmountLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCrBBAmount);
            param->setLabel(kParamCrBBAmountLabel);
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
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAlphaAmount);
            param->setLabel(kParamAlphaAmountLabel);
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

    {
        OFX::GroupParamDescriptor* group = desc.defineGroupParam(kGroupSharpen);
        if (group) {
            group->setLabel(kGroupSharpenLabel);
            //group->setHint(kGroupSettingsHint);
            group->setEnabled(true);
            group->setOpen(false);
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
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSharpenLuminance);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
    //std::cout << "describeInContext! OK\n";
} // DenoiseSharpenPluginFactory::describeInContext

OFX::ImageEffect*
DenoiseSharpenPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum /*context*/)
{
    return new DenoiseSharpenPlugin(handle, _lut);
}

static DenoiseSharpenPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
