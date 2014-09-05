/*
 OFX Chroma Keyer plugin.
 
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

#include "ChromaKeyer.h"

#include <cmath>
#include <limits>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

#define kPluginName "ChromaKeyerOFX"
#define kPluginGrouping "Keyer"
#define kPluginDescription "Apply chroma keying"
#define kPluginIdentifier "net.sf.openfx:ChromaKeyerPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
"The chrominance of foreground colors inside the suppression angle (beta) is set to zero on output, to deal with noise. Use no more than one third of acceptance angle."

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
#define kParamOutputModeOptionIntermediate "Intermediate"
#define kParamOutputModeOptionIntermediateHint "Color is the source color. Alpha is the foreground key. Use for multi-pass keying."
#define kParamOutputModeOptionPremultiplied "Premultiplied"
#define kParamOutputModeOptionPremultipliedHint "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key."
#define kParamOutputModeOptionUnpremultiplied "Unpremultiplied"
#define kParamOutputModeOptionUnpremultipliedHint "Color is the Source color after key color suppression. Alpha is the foreground key."
#define kParamOutputModeOptionComposite "Composite"
#define kParamOutputModeOptionCompositeHint "Color is the composite of Source and Bg. Alpha is the foreground key."

#define kParamSourceAlpha "sourceAlphaHandling"
#define kParamSourceAlphaLabel "Source Alpha"
#define kParamSourceAlphaHint \
"How the alpha embedded in the Source input should be used"
#define kParamSourceAlphaOptionIgnore "Ignore"
#define kParamSourceAlphaOptionIgnoreHint "Ignore the source alpha."
#define kParamSourceAlphaOptionAddToInsideMask "Add to Inside Mask"
#define kParamSourceAlphaOptionAddToInsideMaskHint "Source alpha is added to the inside mask. Use for multi-pass keying."
#define kSourceAlphaNormalOption "Normal"
#define kParamSourceAlphaOptionNormalHint "Foreground key is multiplied by source alpha when compositing."

#define kClipBg "Bg"
#define kClipInsideMask "InM"
#define kClipOutsidemask "OutM"

enum OutputModeEnum {
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    eOutputModeComposite,
};

enum SourceAlphaEnum {
    eSourceAlphaIgnore,
    eSourceAlphaAddToInsideMask,
    eSourceAlphaNormal,
};

using namespace OFX;

class ChromaKeyerProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_bgImg;
    const OFX::Image *_inMaskImg;
    const OFX::Image *_outMaskImg;
    OfxRGBColourD _keyColor;
    double _acceptanceAngle;
    double _tan_acceptanceAngle_2;
    double _suppressionAngle;
    double _tan_suppressionAngle_2;
    double _keyLift;
    double _keyGain;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;
    double _sinKey, _cosKey, _xKey, _ys;

public:
    
    ChromaKeyerProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _bgImg(0)
    , _inMaskImg(0)
    , _outMaskImg(0)
    , _acceptanceAngle(0.)
    , _tan_acceptanceAngle_2(0.)
    , _suppressionAngle(0.)
    , _tan_suppressionAngle_2(0.)
    , _keyLift(0.)
    , _keyGain(1.)
    , _outputMode(eOutputModeComposite)
    , _sourceAlpha(eSourceAlphaIgnore)
    , _sinKey(0)
    , _cosKey(0)
    , _xKey(0)
    , _ys(0)
    {
        
    }
    
    void setSrcImgs(const OFX::Image *srcImg, const OFX::Image *bgImg, const OFX::Image *inMaskImg, const OFX::Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }
    
    void setValues(const OfxRGBColourD& keyColor, double acceptanceAngle, double suppressionAngle, double keyLift, double keyGain, OutputModeEnum outputMode, SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _acceptanceAngle = acceptanceAngle;
        _suppressionAngle = suppressionAngle;
        _keyLift = keyLift;
        _keyGain = keyGain;
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
        double y, cb, cr;
        rgb2ycbcr(keyColor.r, keyColor.g, keyColor.b, &y, &cb, &cr);
        if (cb == 0. && cr == 0.) {
            // no chrominance in the key is an error - default to blue screen
            cb = 1.;
        }
        // xKey is the norm of normalized chrominance (Cb',Cr') = 2 * (Cb,Cr)
        // 0 <= xKey <= 1
        _xKey = 2*std::sqrt(cb*cb + cr*cr);
        _cosKey = 2*cb/_xKey;
        _sinKey = 2*cr/_xKey;
        _ys = _xKey == 0. ? 0. : y/_xKey;
        if (_acceptanceAngle < 180.) {
            _tan_acceptanceAngle_2 = std::tan((_acceptanceAngle/2) * M_PI / 180);
        }
        if (_suppressionAngle < 180.) {
            _tan_suppressionAngle_2 = std::tan((_suppressionAngle/2) * M_PI / 180);
        }
    }

    // from Rec.2020  http://www.itu.int/rec/R-REC-BT.2020-0-201208-I/en :
    // Y' = 0.2627R' + 0.6780G' + 0.0593B'
    // Cb' = (B'-Y')/1.8814
    // Cr' = (R'-Y')/1.4746
    //
    // or the "constant luminance" version
    // Yc' = (0.2627R + 0.6780G + 0.0593B)'
    // Cbc' = (B'-Yc')/1.9404 if -0.9702<=(B'-Y')<=0
    //        (B'-Yc')/1.5816 if 0<=(R'-Y')<=0.7908
    // Crc' = (R'-Yc')/1.7184 if -0.8592<=(B'-Y')<=0
    //        (R'-Yc')/0.9936 if 0<=(R'-Y')<=0.4968
    //
    // with
    // E' = 4.5E if 0 <=E<=beta
    //      alpha*E^(0.45)-(alpha-1) if beta<=E<=1
    // α = 1.099 and β = 0.018 for 10-bit system
    // α = 1.0993 and β = 0.0181 for 12-bit system
    //
    // For our purpose, we only work in the linear space (which is why
    // we don't allow UByte bit depth), and use the first set of formulas
    //
    void rgb2ycbcr(double r, double g, double b, double *y, double *cb, double *cr)
    {
        *y = 0.2627*r+0.6780*g+0.0593*b;
        *cb = (b-*y)/1.8814;
        *cr = (r-*y)/1.4746;
    }

    void ycbcr2rgb(double y, double cb, double cr, double *r, double *g, double *b)
    {
        *r = cr * 1.4746 + y;
        *b = cb  *1.8814 + y;
        *g = (y - 0.2627 * *r - 0.0593 * *b)/0.6780;
    }
};


template<class PIX, int maxValue>
static float sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX floatToSample(float value)
{
    if (maxValue == 1) {
        return value;
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }
    return value * maxValue + 0.5;
}

template <class PIX, int nComponents, int maxValue>
class ChromaKeyerProcessor : public ChromaKeyerProcessorBase
{
public:
    ChromaKeyerProcessor(OFX::ImageEffect &instance)
    : ChromaKeyerProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                const PIX *bgPix = (const PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                const PIX *inMaskPix = (const PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                const PIX *outMaskPix = (const PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);

                float inMask = inMaskPix ? *inMaskPix : 0.;
                if (_sourceAlpha == eSourceAlphaAddToInsideMask && nComponents == 4 && srcPix) {
                    // take the max of inMask and the source Alpha
                    inMask = std::max(inMask, sampleToFloat<PIX,maxValue>(srcPix[3]));
                }
                float outMask = outMaskPix ? *outMaskPix : 0.;
                float Kbg = 0.;

                // clamp inMask and outMask in the [0,1] range
                inMask = std::max(0.f,std::min(inMask,1.f));
                outMask = std::max(0.f,std::min(outMask,1.f));

                // output of the foreground suppressor
                double fgr = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[0]) : 0.;
                double fgg = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[1]) : 0.;
                double fgb = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[2]) : 0.;
                double bgr = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[0]) : 0.;
                double bgg = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[1]) : 0.;
                double bgb = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[2]) : 0.;
                const double fgr_orig = fgr;
                const double fgg_orig = fgg;
                const double fgb_orig = fgb;

                // we want to be able to play with the matte even if the background is not connected
                if (!srcPix) {
                    // no source, take only background
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else if (outMask >= 1.) { // optimize
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else {
                    // general case: compute Kbg from [1]

                    // first, we need to compute YCbCr coordinates.


                    double fgy, fgcb, fgcr;
                    rgb2ycbcr(fgr, fgg, fgb, &fgy, &fgcb, &fgcr);
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

                    if (fgx <= 0 || (_acceptanceAngle >= 180. && fgx >= 0) || std::abs(fgz)/fgx > _tan_acceptanceAngle_2) {
                        /* keep foreground Kfg = 0*/
                        Kfg = 0.;
                    } else {
                        Kfg = _tan_acceptanceAngle_2 > 0 ? (fgx - std::abs(fgz)/_tan_acceptanceAngle_2) : 0.;
                    }
                    assert(Kfg >= 0.);
#ifdef ORIGINAL_MIX // [FD] DON'T ACTIVATE original description in "Video demystified" did the nonadditive mix here, but it produces wrong foreground colors. we do it below (step E)
                    ///////////////
                    // STEP B: Nonadditive Mix

                    // nonadditive mix between the key generator and the garbage matte (outMask)

                    // The garbage matte is added to the foreground key signal (KFG) using a non-additive mixer (NAM). A nonadditive mixer takes the brighter of the two pictures, on a sample-by-sample basis, to generate the key signal. Matting is ideal for any source that generates its own keying signal, such as character generators, and so on.

                    // outside mask has priority over inside mask, treat inside first

                    // Here, Kfg is between 0 (foreground) and _xKey (background)
                    if (inMask > 0. && Kfg > 1.-inMask) {
                        Kfg = 1.-inMask;
                    }
                    if (outMask > 0. && Kfg < outMask) {
                        Kfg = outMask;
                    }
#endif

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
                        if (fgx > 0 && (_tan_suppressionAngle_2 >= 180. || std::abs(fgz)/fgx < _tan_suppressionAngle_2)) {
                            fgcb = 0;
                            fgcr = 0;
                        } else {
                            fgcb = fgcb - Kfg * _cosKey / 2;
                            fgcr = fgcr - Kfg * _sinKey / 2;
                            fgcb = std::max(-0.5,std::min(fgcb,0.5));
                            fgcr = std::max(-0.5,std::min(fgcr,0.5));
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
                            ycbcr2rgb(fgy, fgcb, fgcr, &fgr, &fgg, &fgb);
                            fgr = std::max(0.,std::min(fgr,1.));
                            fgg = std::max(0.,std::min(fgg,1.));
                            fgb = std::max(0.,std::min(fgb,1.));
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
                            Kbg = 1.;
                        } else {
                            Kbg = 0.;
                        }
                    } else if (_keyLift >= 1.) {
                        if (Kfg >= _keyGain*_xKey) {
                            Kbg = 1.;
                        } else {
                            Kbg = 0.;
                        }
                    } else {
                        assert(_keyGain > 0. && 0. <= _keyLift && _keyLift < 1.);
                        Kbg = (Kfg/(_keyGain*_xKey) -_keyLift)/ (1.-_keyLift);
                    }
                    //Kbg = Kfg/_xKey; // if _keyGain = 1 and _keyLift = 0
                    if (Kbg > 1.) {
                        Kbg = 1.;
                    } else if (Kbg < 0.) {
                        Kbg = 0.;
                    }

                    // Additional controls may be implemented to enable the foreground and background signals to be controlled independently. Examples are adjusting the contrast of the foreground so it matches the background or fading the fore- ground in various ways (such as fading to the background to make a foreground object van- ish or fading to black to generate a silhouette).
                    // In the computer environment, there may be relatively slow, smooth edges—especially edges involving smooth shading. As smooth edges are easily distorted during the chroma keying process, a wide keying process is usu- ally used in these circumstances. During wide keying, the keying signal starts before the edge of the graphic object.
                }

                // At this point, we have Kbg,

#ifndef ORIGINAL_MIX // [FD] it was at step B in the original description of the algorithm, but this gave bad artifacts in the foreground suppressor
                ///////////////
                // STEP E: Nonadditive Mix

                // nonadditive mix between the key generator and the garbage matte (outMask)

                // The garbage matte is added to the foreground key signal (KFG) using a non-additive mixer (NAM). A nonadditive mixer takes the brighter of the two pictures, on a sample-by-sample basis, to generate the key signal. Matting is ideal for any source that generates its own keying signal, such as character generators, and so on.

                // outside mask has priority over inside mask, treat inside first
                // Here, Kbg is between 0 (foreground) and 1 (background)
                double Kbg_new = Kbg;
                bool Kbg_changed = false;
                if (inMask > 0. && Kbg_new > 1.-inMask) {
                    Kbg_new = 1.-inMask;
                    Kbg_changed = true;
                }
                if (outMask > 0. && Kbg_new < outMask) {
                    Kbg_new = outMask;
                    Kbg_changed = true;
                }
                if (Kbg_changed) {
                    // since we change Kbg, we also have to change the foreground color accordingly (it is premultiplied)
                    if (Kbg_new >= 1.) {
                        fgr = 0.;
                        fgg = 0.;
                        fgb = 0.;
                    } else if (Kbg_new <= 0. || Kbg >= 1.) {
                        fgr = fgr_orig;
                        fgg = fgg_orig;
                        fgb = fgb_orig;
                    } else if (Kbg_new > Kbg) {
                        // keep the same color for foreground, but interpolate to black
                        double alpha = (1. - Kbg_new) / (1.-Kbg);
                        fgr *= alpha;
                        fgg *= alpha;
                        fgb *= alpha;
                    } else if (Kbg_new < Kbg && Kbg > 0.) {
                        // interpolate between foreground color and original foreground:
                        // Kbg_new = Kbg should give fgr,fgg,fgb
                        // Kbg_new = 0. should give fgr_orig,fgg_orig,fgb_orig
                        // Note: if Kbg = 0., fgr,fgg,fgb should not be changed
                        fgr = ((Kbg-Kbg_new)*fgr_orig + Kbg_new*fgr)/Kbg;
                        fgg = ((Kbg-Kbg_new)*fgg_orig + Kbg_new*fgg)/Kbg;
                        fgb = ((Kbg-Kbg_new)*fgb_orig + Kbg_new*fgb)/Kbg;
                    }
                    Kbg = Kbg_new;
                }
#endif

                // set the alpha channel to the complement of Kbg
                double fga = 1. - Kbg;
                //double fga = Kbg;
                assert(fga >= 0. && fga <= 1.);
                double compAlpha = (_outputMode == eOutputModeComposite &&
                                    _sourceAlpha == eSourceAlphaNormal &&
                                    srcPix) ? sampleToFloat<PIX,maxValue>(srcPix[3]) : 1.;
                switch (_outputMode) {
                    case eOutputModeIntermediate:
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = srcPix ? srcPix[c] : 0;
                        }
                        break;
                    case eOutputModePremultiplied:
                        dstPix[0] = floatToSample<PIX,maxValue>(fgr);
                        dstPix[1] = floatToSample<PIX,maxValue>(fgg);
                        dstPix[2] = floatToSample<PIX,maxValue>(fgb);
                        break;
                    case eOutputModeUnpremultiplied:
                        if (fga == 0.) {
                            dstPix[0] = dstPix[1] = dstPix[2] = maxValue;
                        } else {
                            dstPix[0] = floatToSample<PIX,maxValue>(fgr / fga);
                            dstPix[1] = floatToSample<PIX,maxValue>(fgg / fga);
                            dstPix[2] = floatToSample<PIX,maxValue>(fgb / fga);
                        }
                        break;
                    case eOutputModeComposite:
                        // [FD] not sure if this is the expected way to use compAlpha
                        dstPix[0] = floatToSample<PIX,maxValue>(compAlpha * (fgr + bgr * Kbg) + (1.-compAlpha) * bgr);
                        dstPix[1] = floatToSample<PIX,maxValue>(compAlpha * (fgg + bgg * Kbg) + (1.-compAlpha) * bgg);
                        dstPix[2] = floatToSample<PIX,maxValue>(compAlpha * (fgb + bgb * Kbg) + (1.-compAlpha) * bgb);
                        break;
                }
                if (nComponents == 4) {
                    dstPix[3] = floatToSample<PIX,maxValue>(fga);
                }
            }
        }
    }

};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ChromaKeyerPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ChromaKeyerPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , bgClip_(0)
    , inMaskClip_(0)
    , outMaskClip_(0)
    , keyColor_(0)
    , acceptanceAngle_(0)
    , suppressionAngle_(0)
    , keyLift_(0)
    , keyGain_(0)
    , outputMode_(0)
    , sourceAlpha_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        bgClip_ = fetchClip(kClipBg);
        assert(bgClip_ && (bgClip_->getPixelComponents() == ePixelComponentRGB || bgClip_->getPixelComponents() == ePixelComponentRGBA));
        inMaskClip_ = fetchClip(kClipInsideMask);;
        assert(inMaskClip_ && inMaskClip_->getPixelComponents() == ePixelComponentAlpha);
        outMaskClip_ = fetchClip(kClipOutsidemask);;
        assert(outMaskClip_ && outMaskClip_->getPixelComponents() == ePixelComponentAlpha);
        keyColor_ = fetchRGBParam(kParamKeyColor);
        acceptanceAngle_ = fetchDoubleParam(kParamAcceptanceAngle);
        suppressionAngle_ = fetchDoubleParam(kParamSuppressionAngle);
        keyLift_ = fetchDoubleParam(kParamKeyLift);
        keyGain_ = fetchDoubleParam(kParamKeyGain);
        outputMode_ = fetchChoiceParam(kParamOutputMode);
        sourceAlpha_ = fetchChoiceParam(kParamSourceAlpha);
        assert(keyColor_ && acceptanceAngle_ && suppressionAngle_ && keyLift_ && keyGain_ && outputMode_ && sourceAlpha_);
    }
 
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ChromaKeyerProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *bgClip_;
    OFX::Clip *inMaskClip_;
    OFX::Clip *outMaskClip_;
    
    OFX::RGBParam* keyColor_;
    OFX::DoubleParam* acceptanceAngle_;
    OFX::DoubleParam* suppressionAngle_;
    OFX::DoubleParam* keyLift_;
    OFX::DoubleParam* keyGain_;
    OFX::ChoiceParam* outputMode_;
    OFX::ChoiceParam* sourceAlpha_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ChromaKeyerPlugin::setupAndProcess(ChromaKeyerProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
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
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> bg(bgClip_->fetchImage(args.time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    
    if (bg.get()) {
        OFX::BitDepthEnum    srcBitDepth      = bg->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = bg->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (bg->getRenderScale().x != args.renderScale.x ||
            bg->getRenderScale().y != args.renderScale.y ||
            bg->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    
    // auto ptr for the masks.
    std::auto_ptr<OFX::Image> inMask(inMaskClip_ ? inMaskClip_->fetchImage(args.time) : 0);
    if (inMask.get()) {
        if (inMask->getRenderScale().x != args.renderScale.x ||
            inMask->getRenderScale().y != args.renderScale.y ||
            inMask->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    std::auto_ptr<OFX::Image> outMask(outMaskClip_ ? outMaskClip_->fetchImage(args.time) : 0);
    if (outMask.get()) {
        if (outMask->getRenderScale().x != args.renderScale.x ||
            outMask->getRenderScale().y != args.renderScale.y ||
            outMask->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    OfxRGBColourD keyColor;
    double acceptanceAngle;
    double suppressionAngle;
    double keyLift;
    double keyGain;
    int outputModeI;
    OutputModeEnum outputMode;
    int sourceAlphaI;
    SourceAlphaEnum sourceAlpha;
    keyColor_->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
    acceptanceAngle_->getValueAtTime(args.time, acceptanceAngle);
    suppressionAngle_->getValueAtTime(args.time, suppressionAngle);
    keyLift_->getValueAtTime(args.time, keyLift);
    keyGain_->getValueAtTime(args.time, keyGain);
    outputMode_->getValueAtTime(args.time, outputModeI);
    outputMode = (OutputModeEnum)outputModeI;
    sourceAlpha_->getValueAtTime(args.time, sourceAlphaI);
    sourceAlpha = (SourceAlphaEnum)sourceAlphaI;
    processor.setValues(keyColor, acceptanceAngle, suppressionAngle, keyLift, keyGain, outputMode, sourceAlpha);
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(), bg.get(), inMask.get(), outMask.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
ChromaKeyerPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA)
    {
        switch (dstBitDepth)
        {
            //case OFX::eBitDepthUByte :
            //{
            //    ChromaKeyerProcessor<unsigned char, 4, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth)
        {
            //case OFX::eBitDepthUByte :
            //{
            //    ChromaKeyerProcessor<unsigned char, 3, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort :
            {
                ChromaKeyerProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                ChromaKeyerProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


/* Override the clip preferences */
void
ChromaKeyerPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of dstClip_
    int outputModeI;
    OutputModeEnum outputMode;
    outputMode_->getValue(outputModeI);
    outputMode = (OutputModeEnum)outputModeI;

    switch(outputMode) {
        case eOutputModeIntermediate:
        case eOutputModeUnpremultiplied:
        case eOutputModeComposite:
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
            break;
        case eOutputModePremultiplied:
            clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
            break;
    }
}

mDeclarePluginFactory(ChromaKeyerPluginFactory, {}, {});

void ChromaKeyerPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
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
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}


void ChromaKeyerPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setOptional(false);
    
    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kClipInsideMask);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(true);
    inMaskClip->setIsMask(true);

    // outside mask clip (garbage matte)
    ClipDescriptor *outMaskClip =  desc.defineClip(kClipOutsidemask);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(true);
    outMaskClip->setIsMask(true);

    ClipDescriptor* bgClip = desc.defineClip(kClipBg);
    bgClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    bgClip->addSupportedComponent( OFX::ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(true);
    bgClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // key color
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamKeyColor);
        param->setLabels(kParamKeyColorLabel, kParamKeyColorLabel, kParamKeyColorLabel);
        param->setHint(kParamKeyColorHint);
        param->setDefault(0., 0., 0.);
        // the following should be the default
        double kmin = -std::numeric_limits<double>::max();
        double kmax = std::numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // acceptance angle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamAcceptanceAngle);
        param->setLabels(kParamAcceptanceAngleLabel, kParamAcceptanceAngleLabel, kParamAcceptanceAngleLabel);
        param->setHint(kParamAcceptanceAngleHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);
        param->setDefault(120.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // suppression angle
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSuppressionAngle);
        param->setLabels(kParamSuppressionAngleLabel, kParamSuppressionAngleLabel, kParamSuppressionAngleLabel);
        param->setHint(kParamSuppressionAngleHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);
        param->setDefault(40.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // key lift
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyLift);
        param->setLabels(kParamKeyLiftLabel, kParamKeyLiftLabel, kParamKeyLiftLabel);
        param->setHint(kParamKeyLiftHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        param->setDefault(0.);
        param->setDigits(4);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // key gain
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyGain);
        param->setLabels(kParamKeyGainLabel, kParamKeyGainLabel, kParamKeyGainLabel);
        param->setHint(kParamKeyGainHint);
        param->setRange(0., std::numeric_limits<double>::max());
        param->setDisplayRange(0., 2.);
        param->setIncrement(0.01);
        param->setDefault(1.);
        param->setDigits(4);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // output mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabels(kParamOutputModeLabel, kParamOutputModeLabel, kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == (int)eOutputModeIntermediate);
        param->appendOption(kParamOutputModeOptionIntermediate, kParamOutputModeOptionIntermediateHint);
        assert(param->getNOptions() == (int)eOutputModePremultiplied);
        param->appendOption(kParamOutputModeOptionPremultiplied, kParamOutputModeOptionPremultipliedHint);
        assert(param->getNOptions() == (int)eOutputModeUnpremultiplied);
        param->appendOption(kParamOutputModeOptionUnpremultiplied, kParamOutputModeOptionUnpremultipliedHint);
        assert(param->getNOptions() == (int)eOutputModeComposite);
        param->appendOption(kParamOutputModeOptionComposite, kParamOutputModeOptionCompositeHint);
        param->setDefault((int)eOutputModeComposite);
        param->setAnimates(true);
        desc.addClipPreferencesSlaveParam(*param);
        page->addChild(*param);
    }

    // source alpha
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSourceAlpha);
        param->setLabels(kParamSourceAlphaLabel, kParamSourceAlphaLabel, kParamSourceAlphaLabel);
        param->setHint(kParamSourceAlphaHint);
        assert(param->getNOptions() == (int)eSourceAlphaIgnore);
        param->appendOption(kParamSourceAlphaOptionIgnore, kParamSourceAlphaOptionIgnoreHint);
        assert(param->getNOptions() == (int)eSourceAlphaAddToInsideMask);
        param->appendOption(kParamSourceAlphaOptionAddToInsideMask, kParamSourceAlphaOptionAddToInsideMaskHint);
        assert(param->getNOptions() == (int)eSourceAlphaNormal);
        param->appendOption(kSourceAlphaNormalOption, kParamSourceAlphaOptionNormalHint);
        param->setDefault((int)eSourceAlphaIgnore);
        param->setAnimates(true);
        page->addChild(*param);
    }
}

OFX::ImageEffect* ChromaKeyerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ChromaKeyerPlugin(handle);
}

void getChromaKeyerPluginID(OFX::PluginFactoryArray &ids)
{
    static ChromaKeyerPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
