/* 
 Copyright (C) 2015 Nicholas Carroll
 http://casanico.com

=====================================================================================
 LICENSE
=====================================================================================
 INK is free software: you can redistribute it and/or modify it under the terms of the
 GNU General Public License as published by the Free Software Foundation; either 
 version 3 of the License, or (at your option) any later version.

 INK is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
 without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 PURPOSE.  See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with INK. If
 not, see <http://www.gnu.org/licenses/gpl-3.0.html>

=====================================================================================
 THIRD PARTY LICENSES
=====================================================================================
 INK IS BASED ON CHROMAKEYER BY F. DEVERNAY:
 https://github.com/devernay/openfx-misc/ChromaKeyer/ChromaKeyer.cpp
 Copyright (C) 2014 INRIA. Chromakeyer is GNU GPL 2.0.
 
 INK INCLUDES THE CIMG LIBRARY:
 CImg is a free, open-source library distributed under the CeCILL-C license.  
 http://www.cecill.info/licences/Licence_CeCILL-C_V1-en.html 
 http://cimg.sourceforge.net

=====================================================================================
VERSION HISTORY
=====================================================================================

Version   Date       Author       Description
-------   ---------  ----------   ---------------------------------------------------
    1.0   03-NOV-15  N. Carroll   Copied from Chromakeyer.cpp by Frédéric Devernay
    1.1   13-NOV-15  N. Carroll   Added keying equation and masks and source alpha
    1.2   13-NOV-15  N. Carroll   Commented out BG input (all comments marked '//bg')
    1.3   18-NOV-15  N. Carroll   Implemented Black Point, White Point, Invert
    1.4   18-NOV-15  N. Carroll   Implemented Despill Core

* TODO implement Spill Replacement
* TODO implement Tune Spill Replace
* TODO implement Key Amount
* TODO implement Tune Key Amount
* TODO implement Matte Balance
* TODO implement Despill Balance
* TODO implement Erode and Blur

*/
#include "INK.h"
#include <algorithm>
#include <cmath>
#include <limits>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
 
#define cimg_display 0
CLANG_DIAG_OFF(shorten-64-to-32)
#include "CImg.h"
CLANG_DIAG_ON(shorten-64-to-32)

#define kPluginName "INK"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
"INK proportionate colour difference keyer\n" \
"Copyleft 2015 Nicholas Carroll\n" \
"http://casanico.com\n\n" \

#define kPluginIdentifier "com.casanico.INK"
#define kPluginVersionMajor 1 // Increment this if you have broken backwards compatibility.
#define kPluginVersionMinor 4

#define kSupportsTiles 1 
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamKeyColor "keyColour"
#define kParamKeyColorLabel "Key Colour"
#define kParamKeyColorHint \
"Use the dropper to select the green/blue screen colour."

#define kParamKeyAmount "keyAmount"
#define kParamKeyAmountLabel "* Key Amount"
#define kParamKeyAmountHint \
"* NOT YET IMPLEMENTED\nHow much is keyed (both despill and matte generation)."

// tune key amount
#define kParamMidpoint "Midpoint"
#define kParamMidpointLabel "Midpoint"
#define kParamMidpointHint \
""

#define kParamShadows "Shadows"
#define kParamShadowsLabel "Shadows"
#define kParamShadowsHint \
""

#define kParamMidtones "Midtones"
#define kParamMidtonesLabel "Midtones"
#define kParamMidtonesHint \
""

#define kParamHighlights "Highlights"
#define kParamHighlightsLabel "Highlights"
#define kParamHighlightsHint \
""

#define kParamKeyBalance "keyBalance"
#define kParamKeyBalanceLabel "Key Balance"
#define kParamKeyBalanceHint \
"How much each of the two lesser channels of RGB should influence the key. Higher favours the least channel."

#define kParamMatteBalance "matteBalance"
#define kParamMatteBalanceLabel "* Matte Balance"
#define kParamMatteBalanceHint \
"* NOT YET IMPLEMENTED\nColour balances the key colour used to pull the matte."

#define kParamDespillBalance "despillBalance"
#define kParamDespillBalanceLabel "* Despill Balance"
#define kParamDespillBalanceHint \
"* NOT YET IMPLEMENTED\nColour balances the key colour used for despill."

#define kParamDespillCore "despillCore"
#define kParamDespillCoreLabel "Despill Core"
#define kParamDespillCoreHint "Enabled: the core matte has no effect on despill.\n\nDisabled: the core matte acts as a holdout against despill."

#define kParamReplacementColour "replacementColour"
#define kParamReplacementColourLabel "* Spill Replacement"
#define kParamReplacementColourHint "* NOT YET IMPLEMENTED\nAdd this colour to despilled areas of the core matte."

#define kParamReplacementAmount "replacementAmount"
#define kParamReplacementAmountLabel "Replacement Amount"
#define kParamReplacementAmountHint "Fade the replace amount"

#define kParamMatchLuminance "matchLuminance"
#define kParamMatchLuminanceLabel "Match Luminance"
#define kParamMatchLuminanceHint "Match the source pixel luminance where spill replacement is occurring"

#define kParamHueRange "hueRange"
#define kParamHueRangeLabel "Hue Range"
#define kParamHueRangeHint "Range of hues over which spill replace will be applied (degrees from spill replacement hue)"

#define kParamBlackPoint "blackPoint"
#define kParamBlackPointLabel "Black Point"
#define kParamBlackPointHint \
"Alpha below this value will be set to zero"

#define kParamWhitePoint "whitePoint"
#define kParamWhitePointLabel "White Point"
#define kParamWhitePointHint \
"Alpha above this value will be set to 1"

#define kParamBlur "blur"
#define kParamBlurLabel "* Blur"
#define kParamBlurHint \
"* NOT YET IMPLEMENTED\nBlur the matte"

#define kParamInvert "invert"
#define kParamInvertLabel "Invert"
#define kParamInvertHint \
"Use this to make a garbage matte"

#define kParamErode "erode"
#define kParamErodeLabel "* Erode"
#define kParamErodeHint \
"* NOT YET IMPLEMENTED\nErode (or dilate) the matte"

#define kParamOutputMode "outputMode"
#define kParamOutputModeLabel "Output Mode"
#define kParamOutputModeHint \
"What image to output."
#define kParamOutputModeOptionIntermediate "Source with Matte"
#define kParamOutputModeOptionIntermediateHint "RGB holds the untouched source. Alpha holds the combined matte. Use for multi-pass keying.\n"
#define kParamOutputModeOptionPremultiplied "Premultiplied"
#define kParamOutputModeOptionPremultipliedHint "Normal keyer output (keyed and despilled). Alpha holds the combined matte.\n"
#define kParamOutputModeOptionUnpremultiplied "Unpremultiplied"
#define kParamOutputModeOptionUnpremultipliedHint "Premultiplied RGB divided by Alpha. Alpha holds the combined matte.\n"
#define kParamOutputModeOptionComposite "Composite"
#define kParamOutputModeOptionCompositeHint "Keyer output is composited over Bg as A+B(1-a). Alpha holds the combined matte.\n"
#define kParamOutputModeOptionMatteMonitor "Matte Monitor"
#define kParamOutputModeOptionMatteMonitorHint "Mattes shown with all pixel values from 0.00001 to 0.99999 set to 0.5. Core is in the red channel, current matte (without source alpha) is in the green channel and garbage matte is in the blue channel. Alpha holds the combined matte. For when you need to see the full extent of each matte and where they overlap."

#define kParamSourceAlpha "sourceAlphaHandling"
#define kParamSourceAlphaLabel "Source Alpha"
#define kParamSourceAlphaHint \
"How the alpha embedded in the Source input should be used"
#define kParamSourceAlphaOptionIgnore "Discard"
#define kParamSourceAlphaOptionIgnoreHint "Ignore the source alpha.\n"
#define kParamSourceAlphaOptionAddToCore "Add to Core"
#define kParamSourceAlphaOptionAddToCoreHint "Source alpha is added to the core matte. Use for multi-pass keying.\n"
#define kSourceAlphaNormalOption "Multiply"
#define kParamSourceAlphaOptionNormalHint "Combined matte is multiplied by source alpha."

//bg#define kClipBg "Bg"
#define kClipCore "Core"
#define kClipGarbage "Garbage"

enum OutputModeEnum {
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    //bg   eOutputModeComposite,
    eOutputModeMatteMonitor,
};

enum SourceAlphaEnum {
    eSourceAlphaIgnore,
    eSourceAlphaAddToCore,
    eSourceAlphaNormal,
};

using namespace OFX;
using namespace std;
using namespace cimg_library;

class INKProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
  //bg    const OFX::Image *_bgImg;
    const OFX::Image *_coreImg;
    const OFX::Image *_garbageImg;
    OfxRGBColourD _keyColor;
    double _acceptanceAngle;
    double _tan__acceptanceAngle2;
    double _suppressionAngle;
    double _tan__suppressionAngle2;
    double _keyBalance;
    double _keyAmount;
    double _midpoint;
    double _shadows;
    double _midtones;
    double _highlights;
  OfxRGBColourD _replacementColour;
  OfxRGBColourD  _matteBalance;
  OfxRGBColourD  _despillBalance;
    double _replacementAmount;
    double _matchLuminance;
   double _hueRange;
    bool _despillCore;
    double _blackPoint;
    bool _invert;
    double _whitePoint;
    double _erode;
    OutputModeEnum _outputMode;
    SourceAlphaEnum _sourceAlpha;
    double _sinKey, _cosKey, _xKey, _ys;

public:
    
    INKProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
      //bg , _bgImg(0)
    , _coreImg(0)
    , _garbageImg(0)
    , _acceptanceAngle(0.)
    , _tan__acceptanceAngle2(0.)
    , _suppressionAngle(0.)
    , _tan__suppressionAngle2(0.)
    , _keyBalance(0.)
    , _keyAmount(1.)
    , _midpoint(0.)
    , _shadows(0.)
    , _midtones(0.)
    , _highlights(0.)
    , _replacementAmount(1.)
        , _matchLuminance(1.)
      , _hueRange(0.)
    , _despillCore(true)
    , _blackPoint(0.)
    , _invert(false)
    , _whitePoint(1.)
    , _erode(0.)
      //bg , _outputMode(eOutputModeComposite)
    , _sourceAlpha(eSourceAlphaIgnore)
    , _sinKey(0)
    , _cosKey(0)
    , _xKey(0)
    , _ys(0)
    {
      _keyColor.r = _keyColor.g = _keyColor.b = 0.;
      _replacementColour.r = _replacementColour.r = _replacementColour.r = 0;
      _matteBalance.r = _matteBalance.r = _matteBalance.r = 0;
      _despillBalance.r = _despillBalance.r = _despillBalance.r = 0;
    }

    
  void setSrcImgs(const OFX::Image *srcImg, /*//bg const OFX::Image *bgImg,*/ const OFX::Image *coreImg, const OFX::Image *garbageImg)
    {
        _srcImg = srcImg;
	//bg _bgImg = bgImg;
        _coreImg = coreImg;
        _garbageImg = garbageImg;
    }
    
  void setValues(const OfxRGBColourD& keyColor, double acceptanceAngle, double suppressionAngle, double keyBalance, double keyAmount,double midpoint, double shadows, double midtones, double highlights, const OfxRGBColourD& replacementColour, OfxRGBColourD& matteBalance, OfxRGBColourD& despillBalance, double replacementAmount, double matchLuminance, double hueRange, bool despillCore, double blackPoint, bool invert, double whitePoint, double erode, OutputModeEnum outputMode, SourceAlphaEnum sourceAlpha)
    {
        _keyColor = keyColor;
        _acceptanceAngle = acceptanceAngle;
        _suppressionAngle = suppressionAngle;
        _keyBalance = keyBalance;
        _keyAmount = keyAmount;
	_midpoint = midpoint;
       	_shadows = shadows;
	_midtones = midtones;
	_highlights = highlights;
	_replacementColour = replacementColour;
	_matteBalance =  matteBalance;
	_despillBalance =  despillBalance;
	_replacementAmount = replacementAmount;
	_matchLuminance = matchLuminance;
	_hueRange = hueRange;
	_despillCore = despillCore;
        _blackPoint = blackPoint;
        _invert = invert;
        _whitePoint = whitePoint;
        _erode = erode;
        _outputMode = outputMode;
        _sourceAlpha = sourceAlpha;
    }
};


// Matte  Monitor
double matteMonitor(double v) {
  if (v >= 0.99999) {
        return 1;
    } else if (v > 0.00001) {
        return .5;
    }
    return 0.;
}

template<class PIX, int maxValue>
static float sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX floatToSample(float value)
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
static PIX floatToSample(double value)
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
class INKProcessor : public INKProcessorBase
{
public:
    INKProcessor(OFX::ImageEffect &instance)
    : INKProcessorBase(instance)
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

	      // inputs
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
		//bg const PIX *bgPix = (const PIX *)  (_bgImg ? _bgImg->getPixelAddress(x, y) : 0);
                const PIX *corePix = (const PIX *)  (_coreImg ? _coreImg->getPixelAddress(x, y) : 0);
                const PIX *garbagePix = (const PIX *)  (_garbageImg ? _garbageImg->getPixelAddress(x, y) : 0);

		// masks
               float core = corePix ? *corePix : 0.f;
                if (_sourceAlpha == eSourceAlphaAddToCore && nComponents == 4 && srcPix) {
                    //  Add source Alpha to core
		    core = (core + sampleToFloat<PIX,maxValue>(srcPix[3])) - (core * sampleToFloat<PIX,maxValue>(srcPix[3])) ;
                }
                float garbage = garbagePix ? *garbagePix : 0.f;

                // clamp core and garbage in the [0,1] range
                core = max(0.f,min(core,1.f));
                garbage = max(0.f,min(garbage,1.f));

		// which channel of the key colour is max
		int minKey = 0; double minK = _keyColor.r;
		int midKey = 1; double midK = _keyColor.g;
		int maxKey = 2; double maxK = _keyColor.b;
		if(_keyColor.b <= _keyColor.r && _keyColor.r <= _keyColor.g ){
		  minKey = 2; minK = _keyColor.b;
		  midKey = 0; midK = _keyColor.r;
		  maxKey = 1; maxK = _keyColor.g;
		} else if (_keyColor.r <= _keyColor.b && _keyColor.b <= _keyColor.g ){
		  minKey = 0; minK = _keyColor.r;
		  midKey = 2; midK = _keyColor.b;
		  maxKey = 1; maxK = _keyColor.g;
		} else if (_keyColor.g <= _keyColor.b && _keyColor.b <= _keyColor.r ){
		  minKey = 1; minK = _keyColor.g;
		  midKey = 2; midK = _keyColor.b;
		  maxKey = 0; maxK = _keyColor.r;
		} else if (_keyColor.g <= _keyColor.r && _keyColor.r <= _keyColor.b ){
		  minKey = 1; minK = _keyColor.g;
		  midKey = 0; midK = _keyColor.r;
		  maxKey = 2; maxK = _keyColor.b;
		} else if (_keyColor.b <= _keyColor.g && _keyColor.g <= _keyColor.r ){
		  minKey = 2; minK = _keyColor.b;
		  midKey = 1; midK = _keyColor.g;
		  maxKey = 0; maxK = _keyColor.r;
		}
		// source pixel
                double minP = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[minKey]) : 0.;
		double midP = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[midKey]) : 0.;
                double maxP = srcPix ? sampleToFloat<PIX,maxValue>(srcPix[maxKey]) : 0.;

		// background
                //bg double minBg = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[minKey]) : 0.;
		//bg double midBg = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[midKey]) : 0.;
                //bg double maxBg = bgPix ? sampleToFloat<PIX,maxValue>(bgPix[maxKey]) : 0.;

		// source pixel channels
		double minChan = minP;
		double midChan = midP;
		double maxChan = maxP;
		double currMatte = 1.;
		// TUNE KEY AMOUNT TODO
		double keyAmountRGB = _keyAmount;
		// We will apply the core matte to RGB by reducing the key amount
		if (!_despillCore) {
		  keyAmountRGB *= (1-(double)core);
		}
		
		if (!(midK == 0. && maxK == 0. && minK == 0.) && !(midP == 0. && maxP == 0. && minP == 0.) && !(keyAmountRGB==0.)) {
		    // solve minChan		
		    double min1 = (minP/(maxP-_keyBalance*midP)-minK/(maxK-_keyBalance*midK))
		      / (1+minP/(maxP-_keyBalance*midP)-(2-_keyBalance)*minK/(maxK-_keyBalance*midK));
		    double min2 = min(minP,(maxP-_keyBalance*midP)*min1/(1-min1));
		    minChan = max(0.,min(min2,1.));
		    // solve midChan
		    double mid1 = (midP/(maxP-(1-_keyBalance)*minP)-midK/(maxK-(1-_keyBalance)*minK))
		      / (1+midP/(maxP-(1-_keyBalance)*minP)-(1+_keyBalance)*midK/(maxK-(1-_keyBalance)*minK));
		    double mid2 = min(midP,(maxP-(1-_keyBalance)*minP)*mid1/(1-mid1));
		    midChan = max(0.,min(mid2,1.));
	  	    // solve maxChan
		    double max1 = min(maxP,(_keyBalance*min(midP,(maxP-(1-_keyBalance)*minP)*mid1/(1-mid1))
		    			  + (1-_keyBalance)*min(minP,(maxP-_keyBalance*midP)*min1/(1-min1))));
		    maxChan = max(0.,min(max1,1.));
		    // solve alpha
		    double a1 = (1-maxK)+(_keyBalance*midK+(1-_keyBalance)*minK);
		    double a2 = (_keyAmount*_keyAmount)*(1+a1/abs(1-a1));
		    double a3 =  (1-maxP)-maxP*(a2-(1+((_keyBalance*midP+(1-_keyBalance)*minP))/maxP*(a2)));
		    double a4 = max(midChan,max(a3,minChan));
		    currMatte = max(0.,min(a4,1.)); //alpha
		}
                double sourceMatte = (_sourceAlpha == eSourceAlphaNormal &&
                                    srcPix) ? sampleToFloat<PIX,maxValue>(srcPix[3]) : 1.;
		// add core and garbage mattes and source alpha option 'Multiply'
		double combMatte = (currMatte+(double)core - currMatte*(double)core) * (1-garbage) * sourceMatte;

		// apply the garbage and source mattes to RGB
		minChan *= (1-garbage) * sourceMatte;
		midChan *= (1-garbage) * sourceMatte;
		maxChan *= (1-garbage) * sourceMatte;
		
		// MATTE POSTPROCESS
		//invert
		if (_invert) {
		combMatte = 1-combMatte;
		}
		//black point
		if (_blackPoint >=1.) {
		  combMatte = 0.;
		} else if (_blackPoint >0.) {
		  combMatte = (combMatte-_blackPoint)/(1-_blackPoint);
		  combMatte = max(0.,min(combMatte,1.));
		}
		// white point
		if (_whitePoint <=0.) {
		  combMatte = 0.;
		} else if (_whitePoint <1.) {
		  combMatte /= _whitePoint; // TODO maybe comp different
		  combMatte = max(0.,min(combMatte,1.));
		}

		// OUTPUT MODE
                switch (_outputMode) {
                    case eOutputModeIntermediate:
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = srcPix ? srcPix[c] : 0;
                        }
                        break;
                    case eOutputModePremultiplied:
                        dstPix[minKey] = floatToSample<PIX,maxValue>(minChan);
                        dstPix[midKey] = floatToSample<PIX,maxValue>(midChan);
                        dstPix[maxKey] = floatToSample<PIX,maxValue>(maxChan);
                        break;
                    case eOutputModeUnpremultiplied:
                        if (combMatte == 0.) {
                            dstPix[midKey] = dstPix[maxKey] = dstPix[minKey] = maxValue;
                        } else {
                            dstPix[minKey] = floatToSample<PIX,maxValue>(minChan / combMatte);
                            dstPix[midKey] = floatToSample<PIX,maxValue>(midChan / combMatte);
                            dstPix[maxKey] = floatToSample<PIX,maxValue>(maxChan / combMatte);
                        }
                        break;
                    //bg case eOutputModeComposite:
                    //bg     // traditional composite.
		    //bg     dstPix[minKey] = floatToSample<PIX,maxValue>(minChan + minBg*(1-combMatte));
		    //bg     dstPix[midKey] = floatToSample<PIX,maxValue>(midChan + midBg*(1-combMatte));
		    //bg     dstPix[maxKey] = floatToSample<PIX,maxValue>(maxChan + maxBg*(1-combMatte));
                    //bg     break;
		    case eOutputModeMatteMonitor:
		        dstPix[0] = floatToSample<PIX,maxValue>(matteMonitor(core));
		        dstPix[1] = floatToSample<PIX,maxValue>(matteMonitor(currMatte));
		        dstPix[2] = floatToSample<PIX,maxValue>(matteMonitor(garbage));
                        break;
                }
                if (nComponents == 4) {
                    dstPix[3] = floatToSample<PIX,maxValue>(combMatte);
                }

            }
        }
    }

};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class INKPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    INKPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
      //bg  , _bgClip(0)
    , _coreClip(0)
    , _garbageClip(0)
    , _keyColor(0)
    , _acceptanceAngle(0)
    , _suppressionAngle(0)
    , _keyBalance(0)
    , _keyAmount(0)
    , _midpoint(0)
    , _shadows(0)
    , _midtones(0)
    , _highlights(0)
     , _replacementColour(0)
    , _matteBalance(0)
    , _despillBalance(0)
    , _replacementAmount(0)
       , _matchLuminance(0)
         , _hueRange(0)
      , _despillCore(0)
    , _blackPoint(0)
    , _invert(0)
    , _whitePoint(0)
    , _erode(0)
    , _outputMode(0)
    , _sourceAlpha(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
	//bg  _bgClip = fetchClip(kClipBg);
	//bg  assert(_bgClip && (_bgClip->getPixelComponents() == ePixelComponentRGB || _bgClip->getPixelComponents() == ePixelComponentRGBA));
        _coreClip = fetchClip(kClipCore);;
        assert(_coreClip && _coreClip->getPixelComponents() == ePixelComponentAlpha);
        _garbageClip = fetchClip(kClipGarbage);;
        assert(_garbageClip && _garbageClip->getPixelComponents() == ePixelComponentAlpha);
        _keyColor = fetchRGBParam(kParamKeyColor);
        _acceptanceAngle = fetchDoubleParam(kParamKeyAmount);
        _suppressionAngle = fetchDoubleParam(kParamKeyBalance);
        _keyBalance = fetchDoubleParam(kParamKeyBalance);
        _keyAmount = fetchDoubleParam(kParamKeyAmount);
	_midpoint = fetchDoubleParam(kParamMidpoint);
	_shadows = fetchDoubleParam(kParamShadows);
	_midtones = fetchDoubleParam(kParamMidtones);
	_highlights = fetchDoubleParam(kParamHighlights);
	_replacementColour = fetchRGBParam(kParamReplacementColour);
	_matteBalance = fetchRGBParam(kParamMatteBalance);
	_despillBalance = fetchRGBParam(kParamDespillBalance);
	_replacementAmount = fetchDoubleParam(kParamReplacementAmount);
		_matchLuminance = fetchDoubleParam(kParamMatchLuminance);
	_hueRange = fetchDoubleParam(kParamHueRange);
	_despillCore = fetchBooleanParam(kParamDespillCore);
        _blackPoint = fetchDoubleParam(kParamBlackPoint);
        _invert = fetchBooleanParam(kParamInvert);
	_whitePoint = fetchDoubleParam(kParamWhitePoint);
	_erode = fetchDoubleParam(kParamErode);
        _outputMode = fetchChoiceParam(kParamOutputMode);
        _sourceAlpha = fetchChoiceParam(kParamSourceAlpha);
        assert(_keyColor && _acceptanceAngle && _suppressionAngle && _keyBalance && _keyAmount && _midpoint && _shadows && _midtones && _highlights && _replacementColour && _matteBalance && _despillBalance && _replacementAmount && _matchLuminance && _hueRange && _despillCore && _blackPoint && _invert && _whitePoint && _outputMode && _sourceAlpha);
    }
 
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(INKProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
  //bg OFX::Clip *_bgClip;
    OFX::Clip *_coreClip;
    OFX::Clip *_garbageClip;
    
    OFX::RGBParam* _keyColor;
    OFX::DoubleParam* _acceptanceAngle;
    OFX::DoubleParam* _suppressionAngle;
    OFX::DoubleParam* _keyBalance;
    OFX::DoubleParam* _keyAmount;
    OFX::DoubleParam*  _midpoint;
    OFX::DoubleParam*  _shadows;
    OFX::DoubleParam*  _midtones;
    OFX::DoubleParam*  _highlights;
  OFX::RGBParam*  _replacementColour;
  OFX::RGBParam*  _matteBalance;
  OFX::RGBParam*  _despillBalance;
    OFX::DoubleParam*  _replacementAmount;
      OFX::DoubleParam*  _matchLuminance;
  OFX::DoubleParam*  _hueRange;
    OFX::BooleanParam* _despillCore;
    OFX::DoubleParam* _blackPoint;
    OFX::BooleanParam* _invert;
    OFX::DoubleParam* _whitePoint;
    OFX::DoubleParam* _erode;
    OFX::ChoiceParam* _outputMode;
    OFX::ChoiceParam* _sourceAlpha;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
INKPlugin::setupAndProcess(INKProcessorBase &processor, const OFX::RenderArguments &args)
{
    auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    //bg auto_ptr<const OFX::Image> bg((_bgClip && _bgClip->isConnected()) ?
    //bg                                   _bgClip->fetchImage(args.time) : 0);
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        //OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth/* || srcComponents != dstComponents*/) { 
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
      
    //bg if (bg.get()) {
    //bg     OFX::BitDepthEnum    srcBitDepth      = bg->getPixelDepth();
    //bg     OFX::PixelComponentEnum srcComponents = bg->getPixelComponents();
    //bg     if (srcBitDepth != dstBitDepth/* || srcComponents != dstComponents*/) { // INK outputs RGBA but may have RGB input
    //bg         OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    //bg     }
    //bg     if (bg->getRenderScale().x != args.renderScale.x ||
    //bg         bg->getRenderScale().y != args.renderScale.y ||
    //bg         (bg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && bg->getField() != args.fieldToRender)) {
    //bg         setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
    //bg         OFX::throwSuiteStatusException(kOfxStatFailed);
    //bg     }
    //bg }
    
    // auto ptr for the masks.
    auto_ptr<const OFX::Image> core((_coreClip && _coreClip->isConnected()) ?
                                           _coreClip->fetchImage(args.time) : 0);
    if (core.get()) {
        if (core->getRenderScale().x != args.renderScale.x ||
            core->getRenderScale().y != args.renderScale.y ||
            (core->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && core->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    auto_ptr<const OFX::Image> garbage((_garbageClip && _garbageClip->isConnected()) ?
                                            _garbageClip->fetchImage(args.time) : 0);
    if (garbage.get()) {
        if (garbage->getRenderScale().x != args.renderScale.x ||
            garbage->getRenderScale().y != args.renderScale.y ||
            (garbage->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && garbage->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    OfxRGBColourD keyColor;
    double acceptanceAngle;
    double suppressionAngle;
    double keyBalance;
    double keyAmount;
    double midpoint;
    double shadows;
    double midtones;
    double highlights;
    OfxRGBColourD replacementColour;
    OfxRGBColourD matteBalance;
    OfxRGBColourD despillBalance;
    double replacementAmount;
        double matchLuminance;
    double hueRange;
    bool despillCore;
    double blackPoint;
    bool invert;
    double whitePoint;
    double erode;
    int outputModeI;
    int sourceAlphaI;
    _keyColor->getValueAtTime(args.time, keyColor.r, keyColor.g, keyColor.b);
    _acceptanceAngle->getValueAtTime(args.time, acceptanceAngle);
    _suppressionAngle->getValueAtTime(args.time, suppressionAngle);
    _keyBalance->getValueAtTime(args.time, keyBalance);
    _keyAmount->getValueAtTime(args.time, keyAmount);
    _midpoint->getValueAtTime(args.time, midpoint);
    _shadows->getValueAtTime(args.time, shadows);
    _midtones->getValueAtTime(args.time, midtones);
    _highlights->getValueAtTime(args.time, highlights);
    _replacementColour->getValueAtTime(args.time, replacementColour.r, replacementColour.g, replacementColour.b);
    _matteBalance->getValueAtTime(args.time, matteBalance.r, matteBalance.g, matteBalance.b);
    _despillBalance->getValueAtTime(args.time, despillBalance.r, despillBalance.g, despillBalance.b);
    _replacementAmount->getValueAtTime(args.time, replacementAmount);
       _matchLuminance->getValueAtTime(args.time, matchLuminance);
    _hueRange->getValueAtTime(args.time, hueRange);
    _despillCore->getValueAtTime(args.time, despillCore);
    _blackPoint->getValueAtTime(args.time, blackPoint);
    _invert->getValueAtTime(args.time, invert);
    _whitePoint->getValueAtTime(args.time, whitePoint);
    _erode->getValueAtTime(args.time, erode);
    _outputMode->getValueAtTime(args.time, outputModeI);
    OutputModeEnum outputMode = (OutputModeEnum)outputModeI;
    _sourceAlpha->getValueAtTime(args.time, sourceAlphaI);
    SourceAlphaEnum sourceAlpha = (SourceAlphaEnum)sourceAlphaI;
    processor.setValues(keyColor, acceptanceAngle, suppressionAngle, keyBalance, keyAmount, midpoint, shadows, midtones, highlights, replacementColour, matteBalance, despillBalance, replacementAmount, matchLuminance, hueRange, despillCore, blackPoint, invert, whitePoint, erode, outputMode, sourceAlpha);
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(),/*//bg bg.get(),*/ core.get(), garbage.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
INKPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    if (dstComponents != OFX::ePixelComponentRGBA) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host dit not take into account output components");
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        return;
    }

    switch (dstBitDepth) {
            //case OFX::eBitDepthUByte: {
            //    INKProcessor<unsigned char, 4, 255> fred(*this);
            //    setupAndProcess(fred, args);
            //    break;
            //}
        case OFX::eBitDepthUShort: {
            INKProcessor<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat: {
            INKProcessor<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}


/* Override the clip preferences */
void
INKPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    int outputModeI;
    OutputModeEnum outputMode;
    _outputMode->getValue(outputModeI);
    outputMode = (OutputModeEnum)outputModeI;

    switch(outputMode) {
        case eOutputModeIntermediate:
        case eOutputModeUnpremultiplied:
	case eOutputModeMatteMonitor:
	  //bg case eOutputModeComposite:
	  //bg    clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
	  //bg   break;
        case eOutputModePremultiplied:
            clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
	    break;
    }
    
    // Output is RGBA
    clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
}

mDeclarePluginFactory(INKPluginFactory, {}, {});

void INKPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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


void INKPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    ClipDescriptor* srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClip->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(false);
    
    // create the core mask clip
    ClipDescriptor *coreClip =  desc.defineClip(kClipCore);
    coreClip->addSupportedComponent(ePixelComponentAlpha);
    coreClip->setTemporalClipAccess(false);
    coreClip->setOptional(true);
    coreClip->setSupportsTiles(kSupportsTiles);
    coreClip->setIsMask(true);

    // garbage mask clip 
    ClipDescriptor *garbageClip =  desc.defineClip(kClipGarbage);
    garbageClip->addSupportedComponent(ePixelComponentAlpha);
    garbageClip->setTemporalClipAccess(false);
    garbageClip->setOptional(true);
    garbageClip->setSupportsTiles(kSupportsTiles);
    garbageClip->setIsMask(true);

    //bg ClipDescriptor* bgClip = desc.defineClip(kClipBg);
    //bg bgClip->addSupportedComponent( OFX::ePixelComponentRGBA );
    //bg bgClip->addSupportedComponent( OFX::ePixelComponentRGB );
    //bg bgClip->setTemporalClipAccess(false);
    //bg bgClip->setSupportsTiles(kSupportsTiles);
    //bg bgClip->setOptional(true);

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
        double kmin = -numeric_limits<double>::max();
        double kmax = numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // key amount
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyAmount);
        param->setLabel(kParamKeyAmountLabel);
        param->setHint(kParamKeyAmountHint);
        //param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 2);
        param->setDisplayRange(0.5, 1.5);  
        param->setDefault(1.);    
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    
      GroupParamDescriptor* tuneKey = desc.defineGroupParam("* Tune Key Amount");
      tuneKey->setOpen(false);
      tuneKey->setHint("* NOT YET IMPLEMENTED\nVary Key Amount by pixel luminance");
      if (page) {
            page->addChild(*tuneKey);
        }
     // midpoint
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamMidpoint);
        param->setLabel(kParamMidpointLabel);
        param->setHint(kParamMidpointHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);  
        param->setDefault(.5);    
        param->setAnimates(true);
	param->setParent(*tuneKey);

    }

    // shadows
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamShadows);
        param->setLabel(kParamShadowsLabel);
        param->setHint(kParamShadowsHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 2.);
        param->setDisplayRange(0., 2.);  
        param->setDefault(1.);    
        param->setAnimates(true);
	param->setParent(*tuneKey);
    }

    // midtones
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamMidtones);
        param->setLabel(kParamMidtonesLabel);
        param->setHint(kParamMidtonesHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 2.);
        param->setDisplayRange(0., 2.);  
        param->setDefault(1.);    
        param->setAnimates(true);
	param->setParent(*tuneKey);
    }
    // highlights
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamHighlights);
        param->setLabel(kParamHighlightsLabel);
        param->setHint(kParamHighlightsHint);
        param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 2.);
        param->setDisplayRange(0., 2.);  
        param->setDefault(1.);    
        param->setAnimates(true);
	param->setParent(*tuneKey);
    }
    

    // key balance
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamKeyBalance);
        param->setLabel(kParamKeyBalanceLabel);
        param->setHint(kParamKeyBalanceHint);
        //param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 1.); 
        param->setDisplayRange(0., 1.);
        param->setDefault(0.5);
	param->setDigits(3);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // matte balance
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamMatteBalance);
        param->setLabel(kParamMatteBalanceLabel);
        param->setHint(kParamMatteBalanceHint);
        param->setDefault(0.5, 0.5, 0.5);
        // the following should be the default
        double kmin = -numeric_limits<double>::max();
        double kmax = numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
	}
    }
    // despill balance
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamDespillBalance);
        param->setLabel(kParamDespillBalanceLabel);
        param->setHint(kParamDespillBalanceHint);
        param->setDefault(0.5, 0.5, 0.5);
        // the following should be the default
        double kmin = -numeric_limits<double>::max();
        double kmax = numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
	}
    }

    // despill core
   {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDespillCore);
        param->setLabel(kParamDespillCoreLabel);
        param->setHint(kParamDespillCoreHint);
	param->setDefault(true);
        param->setAnimates(true);
	if (page) {
            page->addChild(*param);
        }
    }

     // replacement colour
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamReplacementColour);
        param->setLabel(kParamReplacementColourLabel);
        param->setHint(kParamReplacementColourHint);
        param->setDefault(0., 0., 0.);
        // the following should be the default
        double kmin = -std::numeric_limits<double>::max();
        double kmax = std::numeric_limits<double>::max();
        param->setRange(kmin, kmin, kmin, kmax, kmax, kmax);
        param->setDisplayRange(0., 0., 0., 1., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

      GroupParamDescriptor* tuneDespill = desc.defineGroupParam("* Tune Spill Replace");
      tuneDespill->setOpen(false);
      tuneDespill->setHint("* NOT YET IMPLEMENTED\nControl Spill Replacement parameters");
      if (page) {
            page->addChild(*tuneDespill);
        }

    // replace amount
      // TODO make this proportionate to the density of the core matte
      // or ratio of core matte to generated matte
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamReplacementAmount);
        param->setLabel(kParamReplacementAmountLabel);
        param->setHint(kParamReplacementAmountHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);  
        param->setDefault(1.);    
        param->setAnimates(true);
	param->setParent(*tuneDespill);
    }

    // luminance
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamMatchLuminance);
        param->setLabel(kParamMatchLuminanceLabel);
        param->setHint(kParamMatchLuminanceHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);  
        param->setDefault(1.);    
        param->setAnimates(true);
	param->setParent(*tuneDespill);
    }
    // hue difference
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamHueRange);
        param->setLabel(kParamHueRangeLabel);
        param->setHint(kParamHueRangeHint);
	param->setDoubleType(eDoubleTypeAngle);;
        param->setRange(0., 180.);
        param->setDisplayRange(0., 180.);  
        param->setDefault(180.);    
        param->setAnimates(true);
	param->setParent(*tuneDespill);
    }

   
      GroupParamDescriptor* matte = desc.defineGroupParam("Matte Postprocess");
      matte->setOpen(false);
      matte->setHint("Conveniences for making a garbage or core matte");
      if (page) {
            page->addChild(*matte);
        }

      // invert
      {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInvert);
        param->setLabel(kParamInvertLabel);
        param->setHint(kParamInvertHint);
  	param->setDefault(false);
        param->setAnimates(true);
        param->setParent(*matte);
       }
      
      // black point
      {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBlackPoint);
        param->setLabel(kParamBlackPointLabel);
        param->setHint(kParamBlackPointHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        param->setDefault(0.);
        param->setDigits(3);
        param->setAnimates(true);
        param->setParent(*matte);
      }

    // white point
      {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamWhitePoint);
        param->setLabel(kParamWhitePointLabel);
        param->setHint(kParamWhitePointHint);
        param->setRange(0.,1.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.01);
        param->setDefault(1.);
        param->setDigits(3);
        param->setAnimates(true);
        param->setParent(*matte);
    }


    // erode
      {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamErode);
        param->setLabel(kParamErodeLabel);
        param->setHint(kParamErodeHint);
        param->setDisplayRange(-100., 100.);
        param->setIncrement(1.);
        param->setDefault(0.);
        param->setDigits(1);
        param->setAnimates(true);
        param->setParent(*matte);
      }    

    // blur
      {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBlur);
        param->setLabel(kParamBlurLabel);
        param->setHint(kParamBlurHint);
        param->setRange(0.,100.);
        param->setDisplayRange(0., 100.);
        param->setIncrement(1.);
        param->setDefault(0.);
        param->setDigits(1);
        param->setAnimates(true);
        param->setParent(*matte);
    }

    // output mode
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == (int)eOutputModeIntermediate);
        param->appendOption(kParamOutputModeOptionIntermediate, kParamOutputModeOptionIntermediateHint);
        assert(param->getNOptions() == (int)eOutputModePremultiplied);
        param->appendOption(kParamOutputModeOptionPremultiplied, kParamOutputModeOptionPremultipliedHint);
        assert(param->getNOptions() == (int)eOutputModeUnpremultiplied);
        param->appendOption(kParamOutputModeOptionUnpremultiplied, kParamOutputModeOptionUnpremultipliedHint);
        //bg assert(param->getNOptions() == (int)eOutputModeComposite);
	//bg param->appendOption(kParamOutputModeOptionComposite, kParamOutputModeOptionCompositeHint);
        assert(param->getNOptions() == (int)eOutputModeMatteMonitor);
        param->appendOption(kParamOutputModeOptionMatteMonitor, kParamOutputModeOptionMatteMonitorHint);
        param->setDefault((int)eOutputModePremultiplied); //bg eOutputModeComposite
        param->setAnimates(true);
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
        param->appendOption(kParamSourceAlphaOptionIgnore, kParamSourceAlphaOptionIgnoreHint);
        assert(param->getNOptions() == (int)eSourceAlphaAddToCore);
        param->appendOption(kParamSourceAlphaOptionAddToCore, kParamSourceAlphaOptionAddToCoreHint);
        assert(param->getNOptions() == (int)eSourceAlphaNormal);
        param->appendOption(kSourceAlphaNormalOption, kParamSourceAlphaOptionNormalHint);
        param->setDefault((int)eSourceAlphaIgnore);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* INKPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new INKPlugin(handle);
}

void getINKPluginID(OFX::PluginFactoryArray &ids)
{
    static INKPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
