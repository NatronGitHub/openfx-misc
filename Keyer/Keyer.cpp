/*
 OFX Keyer plugin.
 
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

#include "Keyer.h"

#include <cmath>
#include <limits>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

#define kPluginName "KeyerOFX"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
"A collection of simple keyers. These work by computing a foreground key from the RGB values of the input image (see the keyerMode parameter).\n" \
"This foreground key is is a scalar from 0 to 1. From the foreground key, a background key (or transparency) is computed.\n" \
"The function that maps the foreground key to the background key is piecewise linear:\n" \
"- it is 0 below (center-tolerance-softness)\n" \
"- it is linear between (center-tolerance-softness) and (center-tolerance)\n" \
" -it is 1 between (center-tolerance)and (center+tolerance)\n" \
"- it is linear between (center+tolerance) and (center+tolerance+softness)\n" \
"- it is 0 above (center+tolerance+softness)\n" \


#define kPluginIdentifier "net.sf.openfx.KeyerPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

/*
  Simple Luma/Color/Screen Keyer.
*/

#define kParamKeyColor "keyColor"
#define kParamKeyColorLabel "Key Color"
#define kParamKeyColorHint \
"Foreground key color. foreground areas containing the key color are replaced with the background image."

#define kParamKeyerMode "mode"
#define kParamKeyerModeLabel "Keyer Mode"
#define kParamKeyerModeHint \
"The operation used to compute the foreground key."
#define kParamKeyerModeOptionLuminance "Luminance"
#define kParamKeyerModeOptionLuminanceHint "Use the luminance for keying. The foreground key value is in luminance."
#define kParamKeyerModeOptionColor "Color"
#define kParamKeyerModeOptionColorHint "Use the color for keying. If the key color is pure green, this corresponds a green keyer, etc."
#define kParamKeyerModeOptionScreen "Screen"
#define kParamKeyerModeOptionScreenHint "Use the color minus the other components for keying. If the key color is pure green, this corresponds a greenscreen, etc. When in screen mode, the upper tolerance should be set to 1."
#define kParamKeyerModeOptionNone "None"
#define kParamKeyerModeOptionNoneHint "No keying, just despill color values. You can control despill areas using either set the inside mask, or use with 'Source Alpha' set to 'Add to Inside Mask'. If 'Output Mode' is set to 'Unpremultiplied', this despills the image even if no mask is present."
#define kParamKeyerModeDefault eKeyerModeLuminance
enum KeyerModeEnum {
    eKeyerModeLuminance,
    eKeyerModeColor,
    eKeyerModeScreen,
    eKeyerModeNone,
};

#define kParamSoftnessLower "softnessLower"
#define kParamSoftnessLowerLabel "Softness (lower)"
#define kParamSoftnessLowerHint "Width of the lower softness range [key-tolerance-softness,key-tolerance]. Background key value goes from 0 to 1 when foreground key is  over this range."

#define kParamToleranceLower "toleranceLower"
#define kParamToleranceLowerLabel "Tolerance (lower)"
#define kParamToleranceLowerHint "Width of the lower tolerance range [key-tolerance,key]. Background key value is 1 when foreground key is  over this range."

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint "Foreground key value forresponding to the key color, where the background key should be 1."

#define kParamToleranceUpper "toleranceUpper"
#define kParamToleranceUpperLabel "Tolerance (upper)"
#define kParamToleranceUpperHint "Width of the upper tolerance range [key,key+tolerance]. Background key value is 1 when foreground key is over this range. Ignored in Screen keyer mode."

#define kParamSoftnessUpper "softnessUpper"
#define kParamSoftnessUpperLabel "Softness (upper)"
#define kParamSoftnessUpperHint "Width of the upper softness range [key+tolerance,key+tolerance+softness]. Background key value goes from 1 to 0 when foreground key is  over this range. Ignored in Screen keyer mode."

#define kParamDespill "despill"
#define kParamDespillLabel "Despill"
#define kParamDespillHint "Reduces color spill on the foreground object (Screen mode only). Between 0 and 1, only mixed foreground/background regions are despilled. Above 1, foreground regions are despilled too."




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

// This is for Rec.709
// see http://www.poynton.com/notes/colour_and_gamma/GammaFAQ.html#luminance
static inline
double rgb2luminance(double r, double g, double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

class KeyerProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_bgImg;
    const OFX::Image *_inMaskImg;
    const OFX::Image *_outMaskImg;
    OfxRGBColourD _keyColor;
    KeyerModeEnum _keyerMode;
    double _softnessLower;
    double _toleranceLower;
    double _center;
    double _toleranceUpper;
    double _softnessUpper;
    double _despill;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;

public:
    
    KeyerProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _bgImg(0)
    , _inMaskImg(0)
    , _outMaskImg(0)
    , _keyerMode(eKeyerModeLuminance)
    , _softnessLower(-0.5)
    , _toleranceLower(0.)
    , _center(0.)
    , _toleranceUpper(0.)
    , _softnessUpper(0.5)
    , _despill(true)
    , _outputMode(eOutputModeComposite)
    , _sourceAlpha(eSourceAlphaIgnore)
    {
        
    }
    
    void setSrcImgs(const OFX::Image *srcImg, const OFX::Image *bgImg, const OFX::Image *inMaskImg, const OFX::Image *outMaskImg)
    {
        _srcImg = srcImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }
    
    void setValues(const OfxRGBColourD& keyColor, KeyerModeEnum keyerMode, double softnessLower, double toleranceLower, double center, double toleranceUpper, double softnessUpper, double despill, OutputModeEnum outputMode, SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _keyerMode = keyerMode;
        _softnessLower = softnessLower;
        _toleranceLower = toleranceLower;
        _center = center;
        if (_keyerMode == eKeyerModeScreen) {
            _toleranceUpper = 1.;
            _softnessUpper = 1.;
            _despill = despill;
        } else {
            _toleranceUpper = toleranceUpper;
            _softnessUpper = softnessUpper;
            _despill = (_keyerMode == eKeyerModeNone) ? despill : 0.;
        }
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
    }

    double key_bg(double Kfg)
    {
        if ((_center + _toleranceLower) <= 0. && Kfg <= 0.) { // special case: everything below 0 is 1. if center-toleranceLower<=0
            return 1.;
        } else if (Kfg < (_center + _toleranceLower + _softnessLower)) {
            return 0.;
        } else if (Kfg < (_center + _toleranceLower) && _softnessLower < 0.) {
            return (Kfg - (_center + _toleranceLower + _softnessLower)) / -_softnessLower;
        } else if (Kfg <= _center + _toleranceUpper) {
            return 1.;
        }  else if (1. <= (_center + _toleranceUpper) &&  1. <= Kfg) { // special case: everything above 1 is 1. if center+toleranceUpper>=1
            return 1.;
        } else if (Kfg < (_center + _toleranceUpper + _softnessUpper) && _softnessUpper > 0.) {
            return ((_center + _toleranceUpper + _softnessUpper) - Kfg) / _softnessUpper;
        } else {
            return 0.;
        }
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
class KeyerProcessor : public KeyerProcessorBase
{
public:
    KeyerProcessor(OFX::ImageEffect &instance)
    : KeyerProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        // for Color and Screen modes, how much the scalar product between RGB and the keyColor must be
        // multiplied by to get the foreground key value 1, which corresponds to the maximum
        // possible value, e.g. for (R,G,B)=(1,1,1)
        // Kfg = 1 = colorKeyFactor * (1,1,1)._keyColor (where "." is the scalar product)
        const double keyColor111 = _keyColor.r + _keyColor.g + _keyColor.b;
        // const double keyColorFactor = (keyColor111 == 0.) ? 1. : 1./keyColor111;
        // squared norm of keyColor, used for Screen mode
        const double keyColorNorm2 = (_keyColor.r*_keyColor.r) + (_keyColor.g*_keyColor.g) + (_keyColor.b*_keyColor.b);

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
                double Kbg = 0.;

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

                // we want to be able to play with the matte even if the background is not connected
                if (!srcPix) {
                    // no source, take only background
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else if (outMask >= 1.) { // optimize
                    Kbg = 1.;
                    fgr = fgg = fgb = 0.;
                } else {
                    // from fgr, fgg, fgb, compute Kbg and update fgr, fgg, fgb

                    double Kfg;
                    double scalarProd = 0.;
                    double norm2 = 0.;
                    double d = 0.;
                    switch (_keyerMode) {
                        case eKeyerModeLuminance: {
                            Kfg = rgb2luminance(fgr, fgg, fgb);
                            break;
                        }
                        case eKeyerModeColor: {
                            scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                            Kfg = (keyColor111 == 0) ? rgb2luminance(fgr, fgg, fgb) : (scalarProd / keyColor111);
                            break;
                        }
                        case eKeyerModeScreen: {
                            scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                            norm2 = fgr * fgr + fgg * fgg + fgb * fgb;
                            d = std::sqrt(norm2 - ((keyColorNorm2 == 0) ? 0. : (scalarProd * scalarProd / keyColorNorm2)));
                            Kfg = (keyColor111 == 0) ? rgb2luminance(fgr, fgg, fgb) : (scalarProd / keyColor111);
                            Kfg -= d;
                            break;
                        }
                        case eKeyerModeNone: {
                            scalarProd = fgr * _keyColor.r + fgg * _keyColor.g + fgb * _keyColor.b;
                            norm2 = fgr * fgr + fgg * fgg + fgb * fgb;
                            d = std::sqrt(norm2 - ((keyColorNorm2 == 0) ? 0. : (scalarProd * scalarProd / keyColorNorm2)));
                            break;
                        }
                    }

                    // compute Kbg from Kfg
                    if (_keyerMode == eKeyerModeNone) {
                        Kbg = 1.;
                    } else {
                        Kbg = key_bg(Kfg);
                    }
                    // nonadditive mix between the key generator and the garbage matte (outMask)
                    // note tha in Chromakeyer this is done before on Kfg instead of Kbg.
                    if (inMask > 0. && Kbg > 1.-inMask) {
                        Kbg = 1.-inMask;
                    }
                    if (outMask > 0. && Kbg < outMask) {
                        Kbg = outMask;
                    }


                    // despill fgr, fgg, fgb
                    if ((_despill > 0.) && (_keyerMode == eKeyerModeNone || _keyerMode == eKeyerModeScreen) && _outputMode != eOutputModeIntermediate && keyColorNorm2 > 0.) {
                        double keyColorNorm = std::sqrt(keyColorNorm2);
                        // color in the direction of keyColor
                        if (scalarProd/keyColorNorm > d) {
                            // maxdespill:
                            // if despill in [0,1]: only outside regions are despilled
                            // if despill in [1,2]: inside regions are despilled too
                            double maxdespill = Kbg*std::min(_despill,1.) + (1-Kbg)*std::max(0., _despill-1);
                            double colorshift = maxdespill*(scalarProd/keyColorNorm - d);
                            fgr -= colorshift * _keyColor.r / keyColorNorm;
                            fgg -= colorshift * _keyColor.g / keyColorNorm;
                            fgb -= colorshift * _keyColor.b / keyColorNorm;
                        }
                    }

                    // premultiply foreground
                    if (_outputMode != eOutputModeUnpremultiplied) {
                        fgr *= (1.-Kbg);
                        fgg *= (1.-Kbg);
                        fgb *= (1.-Kbg);
                    }

                    // clamp foreground color to [0,1]
                    fgr = std::max(0.,std::min(fgr,1.));
                    fgg = std::max(0.,std::min(fgg,1.));
                    fgb = std::max(0.,std::min(fgb,1.));
                }

                // At this point, we have Kbg,

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
                    case eOutputModeUnpremultiplied:
                        dstPix[0] = floatToSample<PIX,maxValue>(fgr);
                        dstPix[1] = floatToSample<PIX,maxValue>(fgg);
                        dstPix[2] = floatToSample<PIX,maxValue>(fgb);
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
class KeyerPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    KeyerPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , bgClip_(0)
    , inMaskClip_(0)
    , outMaskClip_(0)
    , _keyColor(0)
    , _keyerMode(0)
    , _softnessLower(0)
    , _toleranceLower(0)
    , _center(0)
    , _toleranceUpper(0)
    , _softnessUpper(0)
    , _despill(0)
    , _outputMode(0)
    , _sourceAlpha(0)
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
        _keyColor = fetchRGBParam(kParamKeyColor);
        _keyerMode = fetchChoiceParam(kParamKeyerMode);
        _softnessLower = fetchDoubleParam(kParamSoftnessLower);
        _toleranceLower = fetchDoubleParam(kParamToleranceLower);
        _center = fetchDoubleParam(kParamCenter);
        _toleranceUpper = fetchDoubleParam(kParamToleranceUpper);
        _softnessUpper = fetchDoubleParam(kParamSoftnessUpper);
        _despill = fetchDoubleParam(kParamDespill);
        assert(_keyColor && _keyerMode && _softnessLower && _toleranceLower && _center && _toleranceUpper && _softnessUpper && _despill);
        _outputMode = fetchChoiceParam(kParamOutputMode);
        _sourceAlpha = fetchChoiceParam(kParamSourceAlpha);
        assert(_outputMode && _sourceAlpha);
    }
 
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(KeyerProcessorBase &, const OFX::RenderArguments &args);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void setThresholdsFromKeyColor(double r, double g, double b, KeyerModeEnum keyerMode);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *bgClip_;
    OFX::Clip *inMaskClip_;
    OFX::Clip *outMaskClip_;
    
    OFX::RGBParam* _keyColor;
    OFX::ChoiceParam* _keyerMode;
    OFX::DoubleParam* _softnessLower;
    OFX::DoubleParam* _toleranceLower;
    OFX::DoubleParam* _center;
    OFX::DoubleParam* _toleranceUpper;
    OFX::DoubleParam* _softnessUpper;
    OFX::DoubleParam* _despill;

    OFX::ChoiceParam* _outputMode;
    OFX::ChoiceParam* _sourceAlpha;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
KeyerPlugin::setupAndProcess(KeyerProcessorBase &processor, const OFX::RenderArguments &args)
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
    int keyerModeI;
    double softnessLower;
    double toleranceLower;
    double center;
    double toleranceUpper;
    double softnessUpper;
    double despill;
    int outputModeI;
    int sourceAlphaI;
    _keyColor->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
    _keyerMode->getValueAtTime(args.time, keyerModeI);
    KeyerModeEnum keyerMode = (KeyerModeEnum)keyerModeI;
    _softnessLower->getValueAtTime(args.time, softnessLower);
    _toleranceLower->getValueAtTime(args.time, toleranceLower);
    _center->getValueAtTime(args.time, center);
    _toleranceUpper->getValueAtTime(args.time, toleranceUpper);
    _softnessUpper->getValueAtTime(args.time, softnessUpper);
    _despill->getValueAtTime(args.time, despill);
    _outputMode->getValueAtTime(args.time, outputModeI);
    OutputModeEnum outputMode = (OutputModeEnum)outputModeI;
    _sourceAlpha->getValueAtTime(args.time, sourceAlphaI);
    SourceAlphaEnum sourceAlpha = (SourceAlphaEnum)sourceAlphaI;
    processor.setValues(keyColor, keyerMode, softnessLower, toleranceLower, center, toleranceUpper, softnessUpper, despill, outputMode, sourceAlpha);
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(), bg.get(), inMask.get(), outMask.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
KeyerPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            //case OFX::eBitDepthUByte: {
            //    KeyerProcessor<unsigned char, 4, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort: {
                KeyerProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                KeyerProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            //case OFX::eBitDepthUByte: {
            //    KeyerProcessor<unsigned char, 3, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
            case OFX::eBitDepthUShort: {
                KeyerProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                KeyerProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


/* Override the clip preferences */
void
KeyerPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of dstClip_
    int outputModeI;
    OutputModeEnum outputMode;
    _outputMode->getValue(outputModeI);
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
    
    // Output is RGBA
    clipPreferences.setClipComponents(*dstClip_, ePixelComponentRGBA);
}

void
KeyerPlugin::setThresholdsFromKeyColor(double r, double g, double b, KeyerModeEnum keyerMode)
{
    switch (keyerMode) {
        case eKeyerModeLuminance: {
            double l = rgb2luminance(r, g, b);
            _softnessLower->setValue(-l);
            _toleranceLower->setValue(0.);
            _center->setValue(l);
            _toleranceUpper->setValue(0.);
            _softnessUpper->setValue(1. - l);
            break;
        }
        case eKeyerModeColor:
        case eKeyerModeScreen: {
            // for Color and Screen modes, how much the scalar product between RGB and the keyColor must be
            // multiplied by to get the foreground key value 1, which corresponds to the maximum
            // possible value, e.g. for (R,G,B)=(1,1,1)
            // Kfg = 1 = colorKeyFactor * (1,1,1)._keyColor (where "." is the scalar product)
            const double keyColor111 = r + g + b;
            const double keyColorNorm2 = (r*r) + (g*g) + (b*b);
            const double l = keyColor111 == 0. ? 0. : (keyColorNorm2/keyColor111);
            _softnessLower->setValue(-l);
            _toleranceLower->setValue(0.);
            _center->setValue(l);
            _toleranceUpper->setValue(0.);
            _softnessUpper->setValue(1. - l);
            break;
        }
        case eKeyerModeNone:
            break;
    }

}

void
KeyerPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamKeyColor && args.reason == eChangeUserEdit) {
        OfxRGBColourD keyColor;
        _keyColor->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
        int keyerModeI;
        _keyerMode->getValueAtTime(args.time, keyerModeI);
        KeyerModeEnum keyerMode = (KeyerModeEnum)keyerModeI;
        setThresholdsFromKeyColor(keyColor.r, keyColor.g, keyColor.b, keyerMode);
    }
    if (paramName == kParamKeyerMode && args.reason == eChangeUserEdit) {
        OfxRGBColourD keyColor;
        _keyColor->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
        int keyerModeI;
        _keyerMode->getValueAtTime(args.time, keyerModeI);
        KeyerModeEnum keyerMode = (KeyerModeEnum)keyerModeI;
        _softnessLower->setEnabled(keyerMode != eKeyerModeNone);
        _toleranceLower->setEnabled(keyerMode != eKeyerModeNone);
        _center->setEnabled(keyerMode != eKeyerModeNone);
        _toleranceUpper->setEnabled(keyerMode != eKeyerModeNone && keyerMode != eKeyerModeScreen);
        _softnessUpper->setEnabled(keyerMode != eKeyerModeNone && keyerMode != eKeyerModeScreen);
        _despill->setEnabled(keyerMode == eKeyerModeNone || keyerMode == eKeyerModeScreen);
        setThresholdsFromKeyColor(keyColor.r, keyColor.g, keyColor.b, keyerMode);
    }
}

mDeclarePluginFactory(KeyerPluginFactory, {}, {});

void KeyerPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}


void KeyerPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(false);
    
    // create the inside mask clip
    ClipDescriptor *inMaskClip =  desc.defineClip(kClipInsideMask);
    inMaskClip->addSupportedComponent(ePixelComponentAlpha);
    inMaskClip->setTemporalClipAccess(false);
    inMaskClip->setOptional(true);
    inMaskClip->setSupportsTiles(kSupportsTiles);
    inMaskClip->setIsMask(true);

    // outside mask clip (garbage matte)
    ClipDescriptor *outMaskClip =  desc.defineClip(kClipOutsidemask);
    outMaskClip->addSupportedComponent(ePixelComponentAlpha);
    outMaskClip->setTemporalClipAccess(false);
    outMaskClip->setOptional(true);
    outMaskClip->setSupportsTiles(kSupportsTiles);
    outMaskClip->setIsMask(true);

    ClipDescriptor* bgClip = desc.defineClip(kClipBg);
    bgClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    bgClip->addSupportedComponent( OFX::ePixelComponentRGB );
    bgClip->setTemporalClipAccess(false);
    bgClip->setSupportsTiles(kSupportsTiles);
    bgClip->setOptional(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);


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

    // keyer mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamKeyerMode);
        param->setLabels(kParamKeyerModeLabel, kParamKeyerModeLabel, kParamKeyerModeLabel);
        param->setHint(kParamKeyerModeHint);
        assert(param->getNOptions() == (int)eKeyerModeLuminance);
        param->appendOption(kParamKeyerModeOptionLuminance, kParamKeyerModeOptionLuminanceHint);
        assert(param->getNOptions() == (int)eKeyerModeColor);
        param->appendOption(kParamKeyerModeOptionColor, kParamKeyerModeOptionColorHint);
        assert(param->getNOptions() == (int)eKeyerModeScreen);
        param->appendOption(kParamKeyerModeOptionScreen, kParamKeyerModeOptionScreenHint);
        assert(param->getNOptions() == (int)eKeyerModeNone);
        param->appendOption(kParamKeyerModeOptionNone, kParamKeyerModeOptionNoneHint);
        param->setDefault((int)kParamKeyerModeDefault);
        page->addChild(*param);
    }

    // softness (lower)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftnessLower);
        param->setLabels(kParamSoftnessLowerLabel, kParamSoftnessLowerLabel, kParamSoftnessLowerLabel);
        param->setHint(kParamSoftnessLowerHint);
        param->setRange(-1., 0.);
        param->setDisplayRange(-1., 0.);
        param->setDigits(5);
        param->setDefault(-0.5);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // tolerance (lower)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamToleranceLower);
        param->setLabels(kParamToleranceLowerLabel, kParamToleranceLowerLabel, kParamToleranceLowerLabel);
        param->setHint(kParamToleranceLowerHint);
        param->setRange(-1., 0.);
        param->setDisplayRange(-1., 0.);
        param->setDigits(5);
        param->setDefault(0.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // center
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCenter);
        param->setLabels(kParamCenterLabel, kParamCenterLabel, kParamCenterLabel);
        param->setHint(kParamCenterHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        param->setDefault(1.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // tolerance (upper)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamToleranceUpper);
        param->setLabels(kParamToleranceUpperLabel, kParamToleranceUpperLabel, kParamToleranceUpperLabel);
        param->setHint(kParamToleranceUpperHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        param->setDefault(0.);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // softness (upper)
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftnessUpper);
        param->setLabels(kParamSoftnessUpperLabel, kParamSoftnessUpperLabel, kParamSoftnessUpperLabel);
        param->setHint(kParamSoftnessUpperHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDigits(5);
        param->setDefault(0.5);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // despill
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamDespill);
        param->setLabels(kParamDespillLabel, kParamDespillLabel, kParamDespillLabel);
        param->setHint(kParamDespillHint);
        param->setRange(0.,2.);
        param->setDisplayRange(0.,2.);
        param->setDefault(1.);
        param->setEnabled((kParamKeyerModeDefault == eKeyerModeScreen) || (kParamKeyerModeDefault == eKeyerModeNone));
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
        param->setDefault((int)eOutputModeIntermediate);
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

OFX::ImageEffect* KeyerPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new KeyerPlugin(handle);
}

void getKeyerPluginID(OFX::PluginFactoryArray &ids)
{
    static KeyerPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
