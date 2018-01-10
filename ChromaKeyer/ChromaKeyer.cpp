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
 * OFX Chroma Keyer plugin.
 */

#include <algorithm>
#include <cmath>
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
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

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ChromaKeyerOFX"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
    "Simple chroma Keyer.\n" \
    "Algorithm description:\n" \
    "Keith Jack, \"Video Demystified\", Independent Pub Group (Computer), 1996, pp. 214-222, http://www.ee-techs.com/circuit/video-demy5.pdf\n" \
    "A simplified version is described in:\n" \
    "[2] High Quality Chroma Key, Michael Ashikhmin, http://www.cs.utah.edu/~michael/chroma/\n"

#define kPluginIdentifier "net.sf.openfx.ChromaKeyerPlugin"
// history:
// 1.0 initial version, works in YPbPr computed from linear RGB, with rec2020 Ypbpr equations
// 1.1 works in nonlinear Y'PbPr, with choice of colorspace
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

/*
   Simple Chroma Keyer.

   Algorithm description:
   [1] Keith Jack, "Video Demystified", Independent Pub Group (Computer), 1996, pp. 214-222, http://www.ee-techs.com/circuit/video-demy5.pdf

   A simplified version is described in:
   [2] High Quality Chroma Key, Michael Ashikhmin, http://www.cs.utah.edu/~michael/chroma/
 */

#define kParamYPbPrColorspace "colorspace"
#define kParamYPbPrColorspaceLabel "YCbCr Colorspace"
#define kParamYPbPrColorspaceHint "Formula used to compute YCbCr from RGB values."
#define kParamYPbPrColorspaceOptionCcir601 "CCIR 601", "Use CCIR 601 (SD footage).", "ccir601"
#define kParamYPbPrColorspaceOptionRec709 "Rec. 709", "Use Rec. 709 (HD footage).", "rec709"
#define kParamYPbPrColorspaceOptionRec2020 "Rec. 2020", "Use Rec. 2020 (UltraHD/4K footage).", "rec2020"

enum YPbPrColorspaceEnum
{
    eYPbPrColorspaceCcir601 = 0,
    eYPbPrColorspaceRec709,
    eYPbPrColorspaceRec2020,
};

#define kParamYPbPrColorspaceDefault eYPbPrColorspaceRec709

#define kParamLinear "linearProcessing"
#define kParamLinearLabel "Linear Processing"
#define kParamLinearHint \
    "Do not delinearize RGB values to compute the key value."

#define kParamKeyColor "keyColor"
#define kParamKeyColorLabel "Key Color"
#define kParamKeyColorHint \
    "Foreground key color; foreground areas containing the key color are replaced with the background image."

#define kParamAcceptanceAngle "acceptanceAngle"
#define kParamAcceptanceAngleLabel "Acceptance Angle"
#define kParamAcceptanceAngleHint \
    "Foreground colors are only suppressed inside the acceptance angle (alpha)."

#define kParamSuppressionAngle "suppressionAngle"
#define kParamSuppressionAngleLabel "Suppression Angle"
#define kParamSuppressionAngleHint \
    "The chrominance of foreground colors inside the suppression angle (beta) is set to zero on output, to deal with noise. Use no more than one third of acceptance angle. This has no effect on the alpha channel, or if the output is in Intermediate mode."

#define kParamKeyLift "keyLift"
#define kParamKeyLiftLabel "Key Lift"
#define kParamKeyLiftHint \
    "Raise it so that less pixels are classified as background. Makes a sharper transition between foreground and background. Defaults to 0."

#define kParamKeyGain "keyGain"
#define kParamKeyGainLabel "Key Gain"
#define kParamKeyGainHint \
    "Lower it to classify more colors as background. Defaults to 1."

#define kParamOutputMode "show"
#define kParamOutputModeLabel "Output Mode"
#define kParamOutputModeHint \
    "What image to output."
#define kParamOutputModeOptionIntermediate "Intermediate", "Color is the source color. Alpha is the foreground key. Use for multi-pass keying.", "intermediate"
#define kParamOutputModeOptionPremultiplied "Premultiplied", "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key.", "premultiplied"
#define kParamOutputModeOptionUnpremultiplied "Unpremultiplied", "Color is the Source color after key color suppression. Alpha is the foreground key.", "unpremultiplied"
#define kParamOutputModeOptionComposite "Composite", "Color is the composite of Source and Bg. Alpha is the foreground key.", "composite"

#define kParamSourceAlpha "sourceAlphaHandling"
#define kParamSourceAlphaLabel "Source Alpha"
#define kParamSourceAlphaHint \
    "How the alpha embedded in the Source input should be used"
#define kParamSourceAlphaOptionIgnore "Ignore", "Ignore the source alpha.", "ignore"
#define kParamSourceAlphaOptionAddToInsideMask "Add to Inside Mask", "Source alpha is added to the inside mask. Use for multi-pass keying.", "insidemask"
#define kParamSourceAlphaOptionNormal "Normal", "Foreground key is multiplied by source alpha when compositing.", "normal"

#define kClipSourceHint "The foreground image to key."
#define kClipBg "Bg"
#define kClipBgHint "The background image to replace the blue/green screen in the foreground."
#define kClipInsideMask "InM"
#define kClipInsideMaskHint "The Inside Mask, or holdout matte, or core matte, used to confirm areas that are definitely foreground."
#define kClipOutsidemask "OutM"
#define kClipOutsideMaskHint "The Outside Mask, or garbage matte, used to remove unwanted objects (lighting rigs, and so on) from the foreground. The Outside Mask has priority over the Inside Mask, so that areas where both are one are considered to be outside."

enum OutputModeEnum
{
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    eOutputModeComposite,
};

enum SourceAlphaEnum
{
    eSourceAlphaIgnore,
    eSourceAlphaAddToInsideMask,
    eSourceAlphaNormal,
};

static Color::LutManager<Mutex>* gLutManager;


class ChromaKeyerProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_bgImg;
    const Image *_inMaskImg;
    const Image *_outMaskImg;
    OfxRGBColourD _keyColor;
    const Color::Lut* _lut;
    void (*_to_ypbpr)(float r,
                      float g,
                      float b,
                      float *y,
                      float *pb,
                      float *pr);
    void (*_to_rgb)(float y,
                    float pb,
                    float pr,
                    float *r,
                    float *g,
                    float *b);

    bool _linear;
    double _acceptanceAngle;
    double _tan__acceptanceAngle2;
    double _suppressionAngle;
    double _tan__suppressionAngle2;
    double _keyLift;
    double _keyGain;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;
    double _sinKey, _cosKey, _xKey, _ys;

public:

    ChromaKeyerProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _bgImg(NULL)
        , _inMaskImg(NULL)
        , _outMaskImg(NULL)
        , _lut(NULL)
        , _to_ypbpr(NULL)
        , _to_rgb(NULL)
        , _linear(false)
        , _acceptanceAngle(0.)
        , _tan__acceptanceAngle2(0.)
        , _suppressionAngle(0.)
        , _tan__suppressionAngle2(0.)
        , _keyLift(0.)
        , _keyGain(1.)
        , _outputMode(eOutputModeComposite)
        , _sourceAlpha(eSourceAlphaIgnore)
        , _sinKey(0)
        , _cosKey(0)
        , _xKey(0)
        , _ys(0)
    {
        _keyColor.r = _keyColor.g = _keyColor.b = 0.;
    }

    void setSrcImgs(const Image *srcImg,
                    const Image *bgImg,
                    const Image *inMaskImg,
                    const Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }

    void setValues(const OfxRGBColourD& keyColor,
                   YPbPrColorspaceEnum colorspace,
                   bool linear,
                   double acceptanceAngle,
                   double suppressionAngle,
                   double keyLift,
                   double keyGain,
                   OutputModeEnum outputMode,
                   SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _acceptanceAngle = acceptanceAngle;
        _suppressionAngle = suppressionAngle;
        _keyLift = keyLift;
        _keyGain = keyGain;
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
        float y, cb, cr;
        if (linear) {
            _lut = NULL;
        } else {
            switch (colorspace) {
            case eYPbPrColorspaceCcir601:
            case eYPbPrColorspaceRec709:
            case eYPbPrColorspaceRec2020:
                _lut = gLutManager->Rec709Lut();
                break;
            }
        }
        switch (colorspace) {
        case eYPbPrColorspaceCcir601:
            _to_ypbpr = Color::rgb_to_ypbpr601;
            _to_rgb = Color::ypbpr_to_rgb601;
            break;
        case eYPbPrColorspaceRec709:
            _to_ypbpr = Color::rgb_to_ypbpr709;
            _to_rgb = Color::ypbpr_to_rgb709;
            break;
        case eYPbPrColorspaceRec2020:
            _to_ypbpr = Color::rgb_to_ypbpr2020;
            _to_rgb = Color::ypbpr_to_rgb2020;
            break;
        }
        assert(_to_rgb && _to_ypbpr);

        // delinearize RGB
        float r = _lut ? _lut->toColorSpaceFloatFromLinearFloat(keyColor.r) : keyColor.r;
        float g = _lut ? _lut->toColorSpaceFloatFromLinearFloat(keyColor.g) : keyColor.g;
        float b = _lut ? _lut->toColorSpaceFloatFromLinearFloat(keyColor.b) : keyColor.b;

        // convert to YPbPr
        _to_ypbpr(r, g, b, &y, &cb, &cr);
        if ( (cb == 0.) && (cr == 0.) ) {
            // no chrominance in the key is an error - default to blue screen
            cb = 1.;
        }
        // xKey is the norm of normalized chrominance (Cb',Cr') = 2 * (Cb,Cr)
        // 0 <= xKey <= 1
        _xKey = 2 * std::sqrt(cb * cb + cr * cr);
        _cosKey = 2 * cb / _xKey;
        _sinKey = 2 * cr / _xKey;
        _ys = _xKey == 0. ? 0. : y / _xKey;
        if (_acceptanceAngle < 180.) {
            _tan__acceptanceAngle2 = std::tan( (_acceptanceAngle / 2) * M_PI / 180 );
        }
        if (_suppressionAngle < 180.) {
            _tan__suppressionAngle2 = std::tan( (_suppressionAngle / 2) * M_PI / 180 );
        }
    } // setValues
};


template<class PIX, int maxValue>
static float
sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX
floatToSample(float value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return PIX();
    } else if (value >= 1.) {
        return PIX(maxValue);
    }

    return PIX(value * maxValue + 0.5);
}

template<class PIX, int maxValue>
static PIX
floatToSample(double value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return PIX();
    } else if (value >= 1.) {
        return PIX(maxValue);
    }

    return PIX(value * maxValue + 0.5);
}

template <class PIX, int nComponents, int maxValue>
class ChromaKeyerProcessor
    : public ChromaKeyerProcessorBase
{
public:
    ChromaKeyerProcessor(ImageEffect &instance)
        : ChromaKeyerProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const PIX *bgPix = (const PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                const PIX *inMaskPix = (const PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                const PIX *outMaskPix = (const PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);
                float inMask = inMaskPix ? sampleToFloat<PIX, maxValue>(*inMaskPix) : 0.f;
                if ( (_sourceAlpha == eSourceAlphaAddToInsideMask) && (nComponents == 4) && srcPix ) {
                    // take the max of inMask and the source Alpha
                    inMask = std::max( inMask, sampleToFloat<PIX, maxValue>(srcPix[3]) );
                }
                float outMask = outMaskPix ? sampleToFloat<PIX, maxValue>(*outMaskPix) : 0.f;
                float Kbg = 0.f;

                // clamp inMask and outMask in the [0,1] range
                inMask = std::max( 0.f, std::min(inMask, 1.f) );
                outMask = std::max( 0.f, std::min(outMask, 1.f) );

                // output of the foreground suppressor
                float fgr = srcPix ? sampleToFloat<PIX, maxValue>(srcPix[0]) : 0.;
                float fgg = srcPix ? sampleToFloat<PIX, maxValue>(srcPix[1]) : 0.;
                float fgb = srcPix ? sampleToFloat<PIX, maxValue>(srcPix[2]) : 0.;
                float bgr = bgPix ? sampleToFloat<PIX, maxValue>(bgPix[0]) : 0.;
                float bgg = bgPix ? sampleToFloat<PIX, maxValue>(bgPix[1]) : 0.;
                float bgb = bgPix ? sampleToFloat<PIX, maxValue>(bgPix[2]) : 0.;

                // we want to be able to play with the matte even if the background is not connected
                if (!srcPix) {
                    // no source, take only background
                    Kbg = 1.f;
                    fgr = fgg = fgb = 0.;
                } else if (outMask >= 1.) { // optimize
                    Kbg = 1.f;
                    fgr = fgg = fgb = 0.;
                } else {
                    // general case: compute Kbg from [1]

                    // first, we need to compute YCbCr coordinates.

                    // delinearize RGB
                    if (_lut) {
                        fgr =  _lut->toColorSpaceFloatFromLinearFloat(fgr);
                        fgg =  _lut->toColorSpaceFloatFromLinearFloat(fgg);
                        fgb =  _lut->toColorSpaceFloatFromLinearFloat(fgb);
                    }
                    float fgy, fgcb, fgcr;
                    _to_ypbpr(fgr, fgg, fgb, &fgy, &fgcb, &fgcr);
                    //assert(-0.5 <= fgcb && fgcb <= 0.5); // may crash on superblacks/superwhites
                    //assert(-0.5 <= fgcr && fgcr <= 0.5);

                    ///////////////////////
                    // STEP A: Key Generator

                    // First, we rotate (Cb, Cr) coordinate system by an angle defined by the key color to obtain (X,Z) coordinate system.

                    // normalize fgcb and fgcr (which are in [-0.5,0.5]) to the [-1,1] interval
                    double fgcbp = fgcb * 2;
                    double fgcrp = fgcr * 2;
                    //assert(-1. <= fgcbp && fgcbp <= 1.);
                    //assert(-1. <= fgcrp && fgcrp <= 1.);

                    /* Convert foreground to XZ coords where X direction is defined by
                       the key color */
                    double fgx = _cosKey * fgcbp + _sinKey * fgcrp;
                    double fgz = -_sinKey * fgcbp + _cosKey * fgcrp;
                    // Since Cb ́ and Cr ́ are normalized to have a range of ±1, X and Z have a range of ±1.

                    // Second, we use a parameter alfa (60 to 120 degrees were used for different images) to divide the color space into two regions, one where the processing will be applied and the one where foreground will not be changed (where Kbg = 0 and blue_backing_contrubution = 0 in eq.1 above).
                    /* WARNING: accept angle should never be set greater than "somewhat less
                       than 90 degrees" to avoid dealing with negative/infinite tg. In reality,
                       80 degrees should be enough if foreground is reasonable. If this seems
                       to be a problem, go to alternative ways of checking point position
                       (scalar product or line equations). This angle should not be too small
                       either to avoid infinite ctg (used to suppress foreground without use of
                       division)*/
                    double Kfg;

                    if ( (fgx <= 0) || ( (_acceptanceAngle >= 180.) && (fgx >= 0) ) || (std::abs(fgz) / fgx > _tan__acceptanceAngle2) ) {
                        /* keep foreground Kfg = 0*/
                        Kfg = 0.;
                    } else {
                        Kfg = _tan__acceptanceAngle2 > 0 ? (fgx - std::abs(fgz) / _tan__acceptanceAngle2) : 0.;
                    }
                    assert(Kfg >= 0.);
                    double fgx_scaled = fgx;
                    ///////////////
                    // STEP B: Nonadditive Mix

                    // nonadditive mix between the key generator and the garbage matte (outMask)

                    // The garbage matte is added to the foreground key signal (KFG) using a non-additive mixer (NAM). A nonadditive mixer takes the brighter of the two pictures, on a sample-by-sample basis, to generate the key signal. Matting is ideal for any source that generates its own keying signal, such as character generators, and so on.

                    // outside mask has priority over inside mask, treat inside first

                    // Here, Kfg is between 0 (foreground) and _xKey (background)
                    double Kfg_new = Kfg;
                    if ( (inMask > 0.) && (Kfg > 1. - inMask) ) {
                        Kfg_new = 1. - inMask;
                    }
                    if ( (outMask > 0.) && (Kfg < outMask) ) {
                        Kfg_new = outMask;
                    }
                    if (Kfg != 0.) {
                        // modify the fgx used for the suppression angle test
                        fgx_scaled = Kfg_new + std::abs(fgz) / _tan__acceptanceAngle2;
                    }
                    Kfg = Kfg_new;

                    //////////////////////
                    // STEP C: Foreground suppressor

                    if (_outputMode != eOutputModeIntermediate) {
                        // The foreground suppressor reduces foreground color information by implementing X = X – KFG, with the key color being clamped to the black level.

                        //fgx = fgx - Kfg;

                        // there seems to be an error in the book here: there should be primes (') in the formula:
                        // CbFG =Cb–KFG cosθ
                        // CrFG = Cr – KFG sin θ
                        // [FD] there is an error in the paper, which doesn't take into account chrominance denormalization:
                        // (X,Z) was computed from twice the chrominance, so subtracting Kfg from X means to
                        // subtract Kfg/2 from (Cb,Cr).
                        if ( (fgx_scaled > 0) && ( (_suppressionAngle >= 180.) || (fgx_scaled - std::abs(fgz) / _tan__suppressionAngle2 > 0.) ) ) {
                            fgcb = 0;
                            fgcr = 0;
                        } else {
                            fgcb = fgcb - Kfg * _cosKey / 2;
                            fgcr = fgcr - Kfg * _sinKey / 2;
                            fgcb = std::max( -0.5f, std::min(fgcb, 0.5f) );
                            fgcr = std::max( -0.5f, std::min(fgcr, 0.5f) );
                            //assert(-0.5 <= fgcb && fgcb <= 0.5);
                            //assert(-0.5 <= fgcr && fgcr <= 0.5);
                        }

                        // Foreground luminance, after being normalized to have a range of 0–1, is suppressed by:
                        // YFG = Y ́ – yS*KFG
                        // YFG = 0 if yS*KFG > Y ́
                        // [FD] the luminance is already normalized

                        // Y' = Y - y*Kfg, where y is such that Y' = 0 for the key color.
                        fgy = fgy - _ys * Kfg;
                        if (fgy < 0) {
                            fgy = fgr = fgg = fgb = 0;
                        } else {
                            // convert back to r g b
                            // (note: r,g,b is premultiplied since it should be added to the suppressed background)
                            _to_rgb(fgy, fgcb, fgcr, &fgr, &fgg, &fgb);
                            fgr = std::max( 0.f, std::min(fgr, 1.f) );
                            fgg = std::max( 0.f, std::min(fgg, 1.f) );
                            fgb = std::max( 0.f, std::min(fgb, 1.f) );
                            // linearize RGB
                            if (_lut) {
                                fgr =  _lut->fromColorSpaceFloatToLinearFloat(fgr);
                                fgg =  _lut->fromColorSpaceFloatToLinearFloat(fgg);
                                fgb =  _lut->fromColorSpaceFloatToLinearFloat(fgb);
                            }
                        }
                    }
                    /////////////////////
                    // STEP D: Key processor

                    // The key processor generates the initial background key signal (K ́BG) used to remove areas of the background image where the foreground is to be visible.
                    // [FD] we don't implement the key lift (kL), just the key gain (kG)
                    // kG = 1/_xKey, since Kbg should be 1 at the key color
                    // in our implementation, _keyGain is a multiplier of xKey (1 by default) and keylift is the fraction (from 0 to 1) of _keyGain*_xKey where the linear ramp begins
                    if (_keyGain <= 0.) {
                        if (Kfg > 0.) {
                            Kbg = 1.f;
                        } else {
                            Kbg = 0.f;
                        }
                    } else if (_keyLift >= 1.) {
                        if (Kfg >= _keyGain * _xKey) {
                            Kbg = 1.f;
                        } else {
                            Kbg = 0.f;
                        }
                    } else {
                        assert(_keyGain > 0. && 0. <= _keyLift && _keyLift < 1.);
                        Kbg = (float)( (Kfg / (_keyGain * _xKey) - _keyLift) / (1. - _keyLift) );
                    }
                    //Kbg = Kfg/_xKey; // if _keyGain = 1 and _keyLift = 0
                    if (Kbg > 1.) {
                        Kbg = 1.f;
                    } else if (Kbg < 0.) {
                        Kbg = 0.f;
                    }

                    // Additional controls may be implemented to enable the foreground and background signals to be controlled independently. Examples are adjusting the contrast of the foreground so it matches the background or fading the fore- ground in various ways (such as fading to the background to make a foreground object van- ish or fading to black to generate a silhouette).
                    // In the computer environment, there may be relatively slow, smooth edges—especially edges involving smooth shading. As smooth edges are easily distorted during the chroma keying process, a wide keying process is usu- ally used in these circumstances. During wide keying, the keying signal starts before the edge of the graphic object.
                }

                // At this point, we have Kbg,

                // set the alpha channel to the complement of Kbg
                double fga = 1. - Kbg;
                //double fga = Kbg;
                assert(fga >= 0. && fga <= 1.);
                double compAlpha = (_outputMode == eOutputModeComposite &&
                                    _sourceAlpha == eSourceAlphaNormal &&
                                    srcPix) ? sampleToFloat<PIX, maxValue>(srcPix[3]) : 1.;
                switch (_outputMode) {
                case eOutputModeIntermediate:
                    for (int c = 0; c < 3; ++c) {
                        dstPix[c] = srcPix ? srcPix[c] : 0;
                    }
                    break;
                case eOutputModePremultiplied:
                    dstPix[0] = floatToSample<PIX, maxValue>(fgr);
                    dstPix[1] = floatToSample<PIX, maxValue>(fgg);
                    dstPix[2] = floatToSample<PIX, maxValue>(fgb);
                    break;
                case eOutputModeUnpremultiplied:
                    if (fga == 0.) {
                        dstPix[0] = dstPix[1] = dstPix[2] = maxValue;
                    } else {
                        dstPix[0] = floatToSample<PIX, maxValue>(fgr / fga);
                        dstPix[1] = floatToSample<PIX, maxValue>(fgg / fga);
                        dstPix[2] = floatToSample<PIX, maxValue>(fgb / fga);
                    }
                    break;
                case eOutputModeComposite:
                    // [FD] not sure if this is the expected way to use compAlpha
                    dstPix[0] = floatToSample<PIX, maxValue>(compAlpha * (fgr + bgr * Kbg) + (1. - compAlpha) * bgr);
                    dstPix[1] = floatToSample<PIX, maxValue>(compAlpha * (fgg + bgg * Kbg) + (1. - compAlpha) * bgg);
                    dstPix[2] = floatToSample<PIX, maxValue>(compAlpha * (fgb + bgb * Kbg) + (1. - compAlpha) * bgb);
                    break;
                }
                if (nComponents == 4) {
                    dstPix[3] = floatToSample<PIX, maxValue>(fga);
                }
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ChromaKeyerPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ChromaKeyerPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _bgClip(NULL)
        , _inMaskClip(NULL)
        , _outMaskClip(NULL)
        , _keyColor(NULL)
        , _colorspace(NULL)
        , _linear(NULL)
        , _acceptanceAngle(NULL)
        , _suppressionAngle(NULL)
        , _keyLift(NULL)
        , _keyGain(NULL)
        , _outputMode(NULL)
        , _sourceAlpha(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _bgClip = fetchClip(kClipBg);
        assert( _bgClip && (!_bgClip->isConnected() || _bgClip->getPixelComponents() == ePixelComponentRGB || _bgClip->getPixelComponents() == ePixelComponentRGBA) );
        _inMaskClip = fetchClip(kClipInsideMask);;
        assert( _inMaskClip && (!_inMaskClip->isConnected() || _inMaskClip->getPixelComponents() == ePixelComponentAlpha) );
        _outMaskClip = fetchClip(kClipOutsidemask);;
        assert( _outMaskClip && (!_outMaskClip->isConnected() || _outMaskClip->getPixelComponents() == ePixelComponentAlpha) );
        _keyColor = fetchRGBParam(kParamKeyColor);
        _colorspace = fetchChoiceParam(kParamYPbPrColorspace);
        _linear = fetchBooleanParam(kParamLinear);
        _acceptanceAngle = fetchDoubleParam(kParamAcceptanceAngle);
        _suppressionAngle = fetchDoubleParam(kParamSuppressionAngle);
        _keyLift = fetchDoubleParam(kParamKeyLift);
        _keyGain = fetchDoubleParam(kParamKeyGain);
        _outputMode = fetchChoiceParam(kParamOutputMode);
        _sourceAlpha = fetchChoiceParam(kParamSourceAlpha);
        assert(_keyColor && _acceptanceAngle && _suppressionAngle && _keyLift && _keyGain && _outputMode && _sourceAlpha);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ChromaKeyerProcessorBase &, const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_bgClip;
    Clip *_inMaskClip;
    Clip *_outMaskClip;
    RGBParam* _keyColor;
    ChoiceParam* _colorspace;
    BooleanParam* _linear;
    DoubleParam* _acceptanceAngle;
    DoubleParam* _suppressionAngle;
    DoubleParam* _keyLift;
    DoubleParam* _keyGain;
    ChoiceParam* _outputMode;
    ChoiceParam* _sourceAlpha;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ChromaKeyerPlugin::setupAndProcess(ChromaKeyerProcessorBase &processor,
                                   const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    auto_ptr<const Image> bg( ( _bgClip && _bgClip->isConnected() ) ?
                                   _bgClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        //PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) { // ChromaKeyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    if ( bg.get() ) {
        BitDepthEnum srcBitDepth      = bg->getPixelDepth();
        //PixelComponentEnum srcComponents = bg->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) { // ChromaKeyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if ( (bg->getRenderScale().x != args.renderScale.x) ||
             ( bg->getRenderScale().y != args.renderScale.y) ||
             ( ( bg->getField() != eFieldNone) /* for DaVinci Resolve */ && ( bg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    // auto ptr for the masks.
    auto_ptr<const Image> inMask( ( _inMaskClip && _inMaskClip->isConnected() ) ?
                                       _inMaskClip->fetchImage(time) : 0 );
    if ( inMask.get() ) {
        if ( (inMask->getRenderScale().x != args.renderScale.x) ||
             ( inMask->getRenderScale().y != args.renderScale.y) ||
             ( ( inMask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( inMask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    auto_ptr<const Image> outMask( ( _outMaskClip && _outMaskClip->isConnected() ) ?
                                        _outMaskClip->fetchImage(time) : 0 );
    if ( outMask.get() ) {
        if ( (outMask->getRenderScale().x != args.renderScale.x) ||
             ( outMask->getRenderScale().y != args.renderScale.y) ||
             ( ( outMask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( outMask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    OfxRGBColourD keyColor;
    _keyColor->getValueAtTime(time, keyColor.r, keyColor.g, keyColor.b);
    double acceptanceAngle = _acceptanceAngle->getValueAtTime(time);
    double suppressionAngle = _suppressionAngle->getValueAtTime(time);
    double keyLift = _keyLift->getValueAtTime(time);
    double keyGain = _keyGain->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    SourceAlphaEnum sourceAlpha = (SourceAlphaEnum)_sourceAlpha->getValueAtTime(time);
    YPbPrColorspaceEnum colorspace = (YPbPrColorspaceEnum)_colorspace->getValueAtTime(time);
    bool linear = _linear->getValueAtTime(time);
    processor.setValues(keyColor, colorspace, linear, acceptanceAngle, suppressionAngle, keyLift, keyGain, outputMode, sourceAlpha);
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( src.get(), bg.get(), inMask.get(), outMask.get() );
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // ChromaKeyerPlugin::setupAndProcess

// the overridden render function
void
ChromaKeyerPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    if (dstComponents != ePixelComponentRGBA) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host dit not take into account output components");
        throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }

    switch (dstBitDepth) {
    //case eBitDepthUByte: {
    //    ChromaKeyerProcessor<unsigned char, 4, 255> fred(*this);
    //    setupAndProcess(fred, args);
    //    break;
    //}
    case eBitDepthUShort: {
        ChromaKeyerProcessor<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ChromaKeyerProcessor<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

/* Override the clip preferences */
void
ChromaKeyerPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();

    switch (outputMode) {
    case eOutputModeIntermediate:
    case eOutputModeUnpremultiplied:
    case eOutputModeComposite:
        clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        break;
    case eOutputModePremultiplied:
        clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
        break;
    }

    // Output is RGBA
    clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
    // note: ChromaKeyer handles correctly inputs with different components: it only uses RGB components from both clips
}

mDeclarePluginFactory(ChromaKeyerPluginFactory, { gLutManager = new Color::LutManager<Mutex>; ofxsThreadSuiteCheck(); }, { delete gLutManager; });
void
ChromaKeyerPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
ChromaKeyerPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                            ContextEnum /*context*/)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->setHint(kClipSourceHint);
    srcClip->addSupportedComponent( ePixelComponentRGBA );
    srcClip->addSupportedComponent( ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(false);

    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kClipInsideMask);
    inMaskClip->setHint(kClipInsideMaskHint);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(kSupportsTiles);
    inMaskClip->setIsMask(true);

    // outside mask clip (garbage matte)
    ClipDescriptor *outMaskClip =  desc.defineClip(kClipOutsidemask);
    outMaskClip->setHint(kClipOutsideMaskHint);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(kSupportsTiles);
    outMaskClip->setIsMask(true);

    ClipDescriptor* bgClip = desc.defineClip(kClipBg);
    bgClip->setHint(kClipBgHint);
    bgClip->addSupportedComponent( ePixelComponentRGBA );
    bgClip->addSupportedComponent( ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(kSupportsTiles);
    bgClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // key color
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamKeyColor);
        param->setLabel(kParamKeyColorLabel);
        param->setHint(kParamKeyColorHint);
        param->setDefault(0., 0., 0.);
        // the following should be the default
        double kmin = -DBL_MAX;
        double kmax = DBL_MAX;
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamYPbPrColorspace);
        param->setLabel(kParamYPbPrColorspaceLabel);
        param->setHint(kParamYPbPrColorspaceHint);
        assert(param->getNOptions() == (int)eYPbPrColorspaceCcir601);
        param->appendOption(kParamYPbPrColorspaceOptionCcir601);
        assert(param->getNOptions() == (int)eYPbPrColorspaceRec709);
        param->appendOption(kParamYPbPrColorspaceOptionRec709);
        assert(param->getNOptions() == (int)eYPbPrColorspaceRec2020);
        param->appendOption(kParamYPbPrColorspaceOptionRec2020);
        param->setDefault(kParamYPbPrColorspaceDefault);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamLinear);
        param->setLabel(kParamLinearLabel);
        param->setHint(kParamLinearHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // acceptance angle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAcceptanceAngle);
        param->setLabel(kParamAcceptanceAngleLabel);
        param->setHint(kParamAcceptanceAngleHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);
        param->setDefault(120.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // suppression angle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSuppressionAngle);
        param->setLabel(kParamSuppressionAngleLabel);
        param->setHint(kParamSuppressionAngleHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);
        param->setDefault(40.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // key lift
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyLift);
        param->setLabel(kParamKeyLiftLabel);
        param->setHint(kParamKeyLiftHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        param->setDefault(0.);
        param->setDigits(4);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // key gain
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyGain);
        param->setLabel(kParamKeyGainLabel);
        param->setHint(kParamKeyGainHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 2.);
        param->setIncrement(0.01);
        param->setDefault(1.);
        param->setDigits(4);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // output mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == (int)eOutputModeIntermediate);
        param->appendOption(kParamOutputModeOptionIntermediate);
        assert(param->getNOptions() == (int)eOutputModePremultiplied);
        param->appendOption(kParamOutputModeOptionPremultiplied);
        assert(param->getNOptions() == (int)eOutputModeUnpremultiplied);
        param->appendOption(kParamOutputModeOptionUnpremultiplied);
        assert(param->getNOptions() == (int)eOutputModeComposite);
        param->appendOption(kParamOutputModeOptionComposite);
        param->setDefault( (int)eOutputModeComposite );
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // source alpha
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSourceAlpha);
        param->setLabel(kParamSourceAlphaLabel);
        param->setHint(kParamSourceAlphaHint);
        assert(param->getNOptions() == (int)eSourceAlphaIgnore);
        param->appendOption(kParamSourceAlphaOptionIgnore);
        assert(param->getNOptions() == (int)eSourceAlphaAddToInsideMask);
        param->appendOption(kParamSourceAlphaOptionAddToInsideMask);
        assert(param->getNOptions() == (int)eSourceAlphaNormal);
        param->appendOption(kParamSourceAlphaOptionNormal);
        param->setDefault( (int)eSourceAlphaIgnore );
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // ChromaKeyerPluginFactory::describeInContext

ImageEffect*
ChromaKeyerPluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new ChromaKeyerPlugin(handle);
}

static ChromaKeyerPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
