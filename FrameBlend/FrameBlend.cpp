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
 * OFX FrameBlend plugin.
 */

// TODO:
// - show progress

#include <cmath> // for floor
#include <climits> // for INT_MAX
#include <cfloat>
#include <cassert>
#include <algorithm>
#include <limits>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsPixelProcessor.h"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsMerging.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "FrameBlendOFX"
#define kPluginGrouping "Time"
#define kPluginDescription \
    "Blend frames of the input clip.\n" \
    "If a foreground matte is connected, only pixels with a negative or zero foreground value are taken into account, so that the foreground is not mixed with the background.\n" \
    "The number of values used to compute each pixel can be output to the alpha channel."

#define kPluginIdentifier "net.sf.openfx.FrameBlend"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
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

#define kParamFrameRangeName  "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint  "Range of frames which are to be blended together. Frame range is absolute if \"absolute\" is checked, else relative. The last frame is always included, and then one frame out of frameInterval within this interval."

#define kParamAbsoluteName  "absolute"
#define kParamAbsoluteLabel "Absolute"
#define kParamAbsoluteHint  "Use an absolute frame range. If the frame range is not animated or is not an expression, then all output images will be the same."

#define kParamInputRangeName  "inputRange"
#define kParamInputRangeLabel "Input Range"
#define kParamInputRangeHint  "Set the frame range to the input range. This can be used, combined with a foreground matte, to produce a clean background plate."

#define kParamFrameIntervalName  "frameInterval"
#define kParamFrameIntervalLabel "Frame Interval"
#define kParamFrameIntervalHint  "Interval (in frames) between frames to process. 1 means to process every frame in the range. The first frame processed is the lower bound of the range. Can be used to reduce processing time or memory usage."

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint \
    "The operation used to compute the output image."
#define kParamOperationOptionAverage "Average", "Output is the average of selected frames.", "average"
#define kParamOperationOptionMin "Min", "Output is the minimum of selected frames.", "min"
#define kParamOperationOptionMax "Max", "Output is the maximum of selected frames.", "max"
#define kParamOperationOptionSum "Sum", "Output is the sum/addition of selected frames.", "sum"
#define kParamOperationOptionProduct "Product", "Output is the product/multiplication of selected frames.", "product"
#define kParamOperationOptionOver "Over", "Output is the 'over' composition of selected frames.", "over"
#define kParamOperationDefault eOperationAverage
enum OperationEnum
{
    eOperationAverage,
    eOperationMin,
    eOperationMax,
    eOperationSum,
    eOperationProduct,
    eOperationOver,
};

#define kParamDecayName  "decay"
#define kParamDecayLabel "Decay"
#define kParamDecayHint  "Before applying the blending operation, frame t is multiplied by (1-decay)^(last-t)."

#define kParamOutputCountName  "outputCount"
#define kParamOutputCountLabel "Output Count to Alpha"
#define kParamOutputCountHint  "Output image count at each pixel to alpha (input must have an alpha channel)."

#define kClipFgMName "FgM"
#define kClipFgMHint "The foreground matte. If it is connected, only pixels with a negative or zero foreground value are taken into account."

#define kFrameChunk 4 // how many frames to process simultaneously

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

class FrameBlendProcessorBase
    : public PixelProcessor
{
protected:
    const Image *_srcImg;
    std::vector<const Image*> _srcImgs;
    std::vector<const Image*> _fgMImgs;
    float *_accumulatorData;
    unsigned short *_countData;
    float *_sumWeightsData;
    const Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    bool _lastPass;
    double _decay;
    bool _outputCount;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:

    FrameBlendProcessorBase(ImageEffect &instance)
        : PixelProcessor(instance)
        , _srcImg(NULL)
        , _srcImgs()
        , _fgMImgs()
        , _accumulatorData(NULL)
        , _countData(NULL)
        , _sumWeightsData(NULL)
        , _maskImg(NULL)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _lastPass(false)
        , _decay(0.)
        , _outputCount(false)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    void setSrcImgs(const Image *src,
                    const std::vector<const Image*> &v) {_srcImg = src; _srcImgs = v; }

    void setFgMImgs(const std::vector<const Image*> &v) {_fgMImgs = v; }

    void setAccumulators(float *accumulatorData,
                         unsigned short *countData,
                         float* sumWeightsData)
    {_accumulatorData = accumulatorData; _countData = countData; _sumWeightsData = sumWeightsData; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   bool lastPass,
                   double decay,
                   bool outputCount,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _lastPass = lastPass;
        _decay = decay;
        _outputCount = outputCount;
        _mix = mix;
    }

    virtual OperationEnum getOperation() = 0;

private:
};


template <class PIX, int nComponents, int maxValue, OperationEnum operation>
class FrameBlendProcessor
    : public FrameBlendProcessorBase
{
public:
    FrameBlendProcessor(ImageEffect &instance)
        : FrameBlendProcessorBase(instance)
    {
    }

private:

    virtual OperationEnum getOperation() OVERRIDE FINAL { return operation; };

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        assert(1 <= nComponents && nComponents <= 4);
        assert(!_lastPass || _dstPixelData);
        assert( _srcImgs.size() == _fgMImgs.size() );
        float tmpPix[nComponents];
        float initVal = 0.;
        if (!_accumulatorData) {
            switch (operation) {
            case eOperationAverage:
                initVal = 0.;
                break;
            case eOperationMin:
                initVal = std::numeric_limits<float>::infinity();
                break;
            case eOperationMax:
                initVal = -std::numeric_limits<float>::infinity();
                break;
            case eOperationSum:
                initVal = 0.;
                break;
            case eOperationProduct:
                initVal = 1.;
                break;
            case eOperationOver:
                initVal = 0.;
                break;
            }
        }

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = _lastPass ? (PIX *) getDstPixelAddress(procWindow.x1, y) : 0;
            assert(!_lastPass || dstPix);
            if (_lastPass && !dstPix) {
                // coverity[dead_error_line]
                continue;
            }

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                size_t renderPix = ( (_renderWindow.x2 - _renderWindow.x1) * (y - _renderWindow.y1) +
                                     (x - _renderWindow.x1) );
                int count = _countData ? _countData[renderPix] : 0;
                float sumWeights = _sumWeightsData ? _sumWeightsData[renderPix] : 0;
                if (_accumulatorData) {
                    std::copy(&_accumulatorData[renderPix * nComponents], &_accumulatorData[renderPix * nComponents + nComponents], tmpPix);
                } else {
                    std::fill(tmpPix, tmpPix + nComponents, initVal);
                }
                // accumulate
                for (unsigned i = 0; i < _srcImgs.size(); ++i) {
                    const PIX *fgMPix = (const PIX *)  (_fgMImgs[i] ? _fgMImgs[i]->getPixelAddress(x, y) : 0);
                    if ( !fgMPix || (*fgMPix <= 0) ) {
                        if (_decay > 0.) {
                            for (int c = 0; c < nComponents; ++c) {
                                tmpPix[c] *= (1. - _decay);
                            }
                            sumWeights *= (1. - _decay);
                        }
                        const PIX *srcPixi = (const PIX *)  (_srcImgs[i] ? _srcImgs[i]->getPixelAddress(x, y) : 0);
                        if (srcPixi) {
                            PIX a = PIX();
                            float b = 0.;
                            if (operation == eOperationOver) {
                                // over requires alpha
                                if (nComponents == 4) {
                                    a = srcPixi[3];
                                    b = tmpPix[3];
                                } else if (nComponents == 1) {
                                    a = srcPixi[0];
                                    b = tmpPix[0];
                                } else {
                                    a = (PIX)maxValue;
                                    b = 1.;
                                }
                            }
                            for (int c = 0; c < nComponents; ++c) {
                                switch (operation) {
                                case eOperationAverage:
                                    tmpPix[c] = MergeImages2D::plusFunctor((float)srcPixi[c], tmpPix[c]); // compute average in the end
                                    break;
                                case eOperationMin:
                                    tmpPix[c] = MergeImages2D::darkenFunctor((float)srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationMax:
                                    tmpPix[c] = MergeImages2D::lightenFunctor((float)srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationSum:
                                    tmpPix[c] = MergeImages2D::plusFunctor((float)srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationProduct:
                                    tmpPix[c] = MergeImages2D::multiplyFunctor<float,maxValue>(srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationOver:
                                    tmpPix[c] = MergeImages2D::overFunctor<PIX, maxValue>(srcPixi[c], tmpPix[c], a, b);
                                    break;
                                }
                            }
                        }
                        ++count;
                        sumWeights += 1;
                    }
                }
                if (!_lastPass) {
                    if (_countData) {
                        _countData[renderPix] = count;
                    }
                    if (_sumWeightsData) {
                        _sumWeightsData[renderPix] = sumWeights;
                    }
                    if (_accumulatorData) {
                        std::copy(tmpPix, tmpPix + nComponents, &_accumulatorData[renderPix * nComponents]);
                    }
                } else {
                    if (nComponents == 1) {
                        int c = 0;
                        if (_outputCount) {
                            tmpPix[c] = count;
                        } else if (operation == eOperationAverage) {
                            tmpPix[c] =  (count ? (tmpPix[c] / sumWeights) : 0);
                        }
                    } else if ( (3 <= nComponents) && (nComponents <= 4) ) {
                        if (operation == eOperationAverage) {
                            for (int c = 0; c < 3; ++c) {
                                tmpPix[c] = (count ? (tmpPix[c] / sumWeights) : 0);
                            }
                        }
                        if (nComponents >= 4) {
                            int c = nComponents - 1;
                            if (_outputCount) {
                                tmpPix[c] = count;
                            } else if (operation == eOperationAverage) {
                                tmpPix[c] =  (count ? (tmpPix[c] / sumWeights) : 0);
                            }
                        }
                    }
                    // tmpPix is not normalized, it is within [0,maxValue]
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking,
                                                                     _maskImg, _mix, _maskInvert,
                                                                     dstPix);
                    // copy back original values from unprocessed channels
                    if (nComponents == 1) {
                        if (!processA) {
                            dstPix[0] = srcPix ? srcPix[0] : PIX();
                        }
                    } else {
                        if (!processR) {
                            dstPix[0] = srcPix ? srcPix[0] : PIX();
                        }
                        if ( (nComponents >= 2) && !processG ) {
                            dstPix[1] = srcPix ? srcPix[1] : PIX();
                        }
                        if ( (nComponents >= 3) && !processB ) {
                            dstPix[2] = srcPix ? srcPix[2] : PIX();
                        }
                        if ( (nComponents >= 4) && !processA ) {
                            dstPix[3] = srcPix ? srcPix[3] : PIX();
                        }
                    }
                    // increment the dst pixel
                    dstPix += nComponents;
                }
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameBlendPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    FrameBlendPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _fgMClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _frameRange(NULL)
        , _absolute(NULL)
        , _inputRange(NULL)
        , _frameInterval(NULL)
        , _operation(NULL)
        , _decay(NULL)
        , _outputCount(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _fgMClip = fetchClip(kClipFgMName);
        assert(!_fgMClip || _fgMClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _frameRange = fetchInt2DParam(kParamFrameRangeName);
        _absolute = fetchBooleanParam(kParamAbsoluteName);
        _inputRange = fetchPushButtonParam(kParamInputRangeName);
        _frameInterval = fetchIntParam(kParamFrameIntervalName);
        _operation = fetchChoiceParam(kParamOperation);
        _decay = fetchDoubleParam(kParamDecayName);
        _outputCount = fetchBooleanParam(kParamOutputCountName);
        assert(_frameRange && _absolute && _inputRange && _operation && _decay && _outputCount);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(FrameBlendProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue, OperationEnum operation>
    void renderForOperation(const RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    Clip *_fgMClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    Int2DParam* _frameRange;
    BooleanParam* _absolute;
    PushButtonParam* _inputRange;
    IntParam* _frameInterval;
    ChoiceParam* _operation;
    DoubleParam* _decay;
    BooleanParam* _outputCount;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// Since we cannot hold a auto_ptr in the vector we must hold a raw pointer.
// To ensure that images are always freed even in case of exceptions, use a RAII class.
struct OptionalImagesHolder_RAII
{
    std::vector<const Image*> images;

    OptionalImagesHolder_RAII()
        : images()
    {
    }

    ~OptionalImagesHolder_RAII()
    {
        for (unsigned int i = 0; i < images.size(); ++i) {
            delete images[i];
        }
    }
};

// Exponentiation by squaring
// works with positive or negative integer exponents
template<typename T>
T
ipow(T base,
     int exp)
{
    T result = T(1);

    if (exp >= 0) {
        while (exp) {
            if (exp & 1) {
                result *= base;
            }
            exp >>= 1;
            base *= base;
        }
    } else {
        exp = -exp;
        while (exp) {
            if (exp & 1) {
                result /= base;
            }
            exp >>= 1;
            base *= base;
        }
    }

    return result;
}

/* set up and run a processor */
void
FrameBlendPlugin::setupAndProcess(FrameBlendProcessorBase &processor,
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

    // fetch the mask
    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        doMasking = true;
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    double mix = 1.;
    _mix->getValueAtTime(time, mix);
    double decay = _decay->getValueAtTime(time);
    bool outputCount = false;
    if ( (dstComponents == ePixelComponentRGBA) || (dstComponents == ePixelComponentAlpha) ) {
        _outputCount->getValueAtTime(time, outputCount);
    }
    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    // If masking or mixing, fetch the original image
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() && (doMasking || mix != 1.) ) ?
                                    _srcClip->fetchImage(args.time) :
                                    0 );
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

    // accumulator image
    auto_ptr<ImageMemory> accumulator;
    float *accumulatorData = NULL;
    auto_ptr<ImageMemory> count;
    unsigned short *countData = NULL;
    auto_ptr<ImageMemory> sumWeights;
    float *sumWeightsData = NULL;

    // compute range
    bool absolute;
    _absolute->getValueAtTime(time, absolute);
    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    int interval;
    _frameInterval->getValueAtTime(time, interval);
    interval = (std::max)(1, interval);
    decay = 1. - ipow(1. - decay, interval); // adjust decay so that the final aspect is similar for different values of the interval

    int n = (std::abs(last - first) + 1) / interval;
    if (first > last) {
        interval = -interval;
    }
    // last frame should always be in the image set, so first frame must be adjusted if abs(interval) > 1
    first = last - (n - 1) * interval;
    if (!absolute) {
        first += time;
        //last += time; // last is not used anymore
    }

    const OfxRectI& renderWindow = args.renderWindow;
    size_t nPixels = (renderWindow.y2 - renderWindow.y1) * (renderWindow.x2 - renderWindow.x1);
    OperationEnum operation = processor.getOperation();

    // Main processing loop.
    // We process the frame range by chunks, to avoid using too much memory.
    int imin;
    int imax = 0;
    while (imax < n) {
        imin = imax;
        imax = (std::min)(imin + kFrameChunk, n);
        bool lastPass = (imax == n);

        if (!lastPass) {
            // Initialize accumulator image (always use float)
            if (!accumulatorData) {
                int dstNComponents = _dstClip->getPixelComponentCount();
                accumulator.reset( new ImageMemory(nPixels * dstNComponents * sizeof(float), this) );
                accumulatorData = (float*)accumulator->lock();
                switch (operation) {
                case eOperationAverage:
                case eOperationSum:
                case eOperationOver:
                    std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, 0.);
                    break;
                case eOperationMin:
                    std::fill( accumulatorData, accumulatorData + nPixels * dstNComponents, std::numeric_limits<float>::infinity() );
                    break;
                case eOperationMax:
                    std::fill( accumulatorData, accumulatorData + nPixels * dstNComponents, -std::numeric_limits<float>::infinity() );
                    break;
                case eOperationProduct:
                    std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, 1.);
                    break;
                }
            }
            // Initialize count image if operator is average or outputCount is true and output has alpha (use short)
            if ( !countData && ( (operation == eOperationAverage) || outputCount ) ) {
                count.reset( new ImageMemory(nPixels * sizeof(unsigned short), this) );
                countData = (unsigned short*)count->lock();
                std::fill(countData, countData + nPixels, 0);
            }
            // Initialize sumWeights image if operator is average
            if ( !sumWeightsData && (operation == eOperationAverage) ) {
                sumWeights.reset( new ImageMemory(nPixels * sizeof(float), this) );
                sumWeightsData = (float*)sumWeights->lock();
                std::fill(sumWeightsData, sumWeightsData + nPixels, 0);
            }
        }

        // fetch the source images
        OptionalImagesHolder_RAII srcImgs;
        for (int i = imin; i < imax; ++i) {
            if ( abort() ) {
                return;
            }
            const Image* src = _srcClip ? _srcClip->fetchImage(first + i * interval) : 0;
            if (src) {
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
            srcImgs.images.push_back(src);
        }
        // fetch the foreground mattes
        OptionalImagesHolder_RAII fgMImgs;
        for (int i = imin; i < imax; ++i) {
            if ( abort() ) {
                return;
            }
            const Image* mask = ( _fgMClip && _fgMClip->isConnected() ) ? _fgMClip->fetchImage(first + i * interval) : 0;
            if (mask) {
                assert( _fgMClip->isConnected() );
                if ( (mask->getRenderScale().x != args.renderScale.x) ||
                     ( mask->getRenderScale().y != args.renderScale.y) ||
                     ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                    setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    throwSuiteStatusException(kOfxStatFailed);
                }
            }
            fgMImgs.images.push_back(mask);
        }

        // set the images
        if (lastPass) {
            processor.setDstImg( dst.get() );
        }
        processor.setSrcImgs(lastPass ? src.get() : 0, srcImgs.images);
        processor.setFgMImgs(fgMImgs.images);
        // set the render window
        processor.setRenderWindow(renderWindow);
        processor.setAccumulators(accumulatorData, countData, sumWeightsData);

        processor.setValues(processR, processG, processB, processA,
                            lastPass, decay, outputCount, mix);

        // Call the base class process member, this will call the derived templated process code
        processor.process();
    }
} // FrameBlendPlugin::setupAndProcess

// the overridden render function
void
FrameBlendPlugin::render(const RenderArguments &args)
{
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentAlpha) {
        renderForComponents<1>(args);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
#endif
    } else {
        assert(dstComponents == ePixelComponentRGB);
        renderForComponents<3>(args);
    }
}

template<int nComponents>
void
FrameBlendPlugin::renderForComponents(const RenderArguments &args)
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
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
FrameBlendPlugin::renderForBitDepth(const RenderArguments &args)
{
    OperationEnum operation = (OperationEnum)_operation->getValueAtTime(args.time);

    switch (operation) {
    case eOperationAverage:
        renderForOperation<PIX, nComponents, maxValue, eOperationAverage>(args);
        break;

    case eOperationMin:
        renderForOperation<PIX, nComponents, maxValue, eOperationMin>(args);
        break;

    case eOperationMax:
        renderForOperation<PIX, nComponents, maxValue, eOperationMax>(args);
        break;

    case eOperationSum:
        renderForOperation<PIX, nComponents, maxValue, eOperationSum>(args);
        break;

    case eOperationProduct:
        renderForOperation<PIX, nComponents, maxValue, eOperationProduct>(args);
        break;

    case eOperationOver:
        renderForOperation<PIX, nComponents, maxValue, eOperationOver>(args);
        break;
    }
}

template <class PIX, int nComponents, int maxValue, OperationEnum operation>
void
FrameBlendPlugin::renderForOperation(const RenderArguments &args)
{
    FrameBlendProcessor<PIX, nComponents, maxValue, operation> fred(*this);
    setupAndProcess(fred, args);
}

bool
FrameBlendPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR, processG, processB, processA;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    if ( _fgMClip && _fgMClip->isConnected() ) {
        // FgM may contain anything
        return false;
    }

    bool outputCount;
    _outputCount->getValueAtTime(time, outputCount);
    if (outputCount) {
        return false;
    }

    bool absolute;
    _absolute->getValueAtTime(time, absolute);
    OfxRangeD range;
    int min, max;
    _frameRange->getValueAtTime(time, min, max);
    if (!absolute) {
        min += time;
        max += time;
    }
    range.min = min;
    range.max = max;

    if (range.min == range.max) {
        identityClip = _srcClip;
        identityTime = range.min;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // FrameBlendPlugin::isIdentity

void
FrameBlendPlugin::getFramesNeeded(const FramesNeededArguments &args,
                                  FramesNeededSetter &frames)
{
    const double time = args.time;
    bool absolute;

    _absolute->getValueAtTime(time, absolute);
    OfxRangeD range;
    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (!absolute) {
        first += time;
        last += time;
    }
    int interval;
    _frameInterval->getValueAtTime(time, interval);
    if (interval <= 1) {
        if (first > last) {
            std::swap(first, last);
        }
        range.min = first;
        range.max = last;
        frames.setFramesNeeded(*_srcClip, range);
    } else {
        if (first > last) {
            interval = -interval;
        }
        for (int i = first; i <= last; i += interval) {
            range.min = range.max = i;
            frames.setFramesNeeded(*_srcClip, range);
        }
    }
}

bool
FrameBlendPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double time = args.time;
    bool absolute;

    _absolute->getValueAtTime(time, absolute);
    int min, max;
    _frameRange->getValueAtTime(time, min, max);
    if (min > max) {
        std::swap(min, max);
    }
    if (!absolute) {
        min += time;
        max += time;
    }
    int interval;
    _frameInterval->getValueAtTime(time, interval);

    rod = _srcClip->getRegionOfDefinition(min);

    for (int i = min + interval; i <= max; i += interval) {
        OfxRectD srcRoD = _srcClip->getRegionOfDefinition(i);
        Coords::rectBoundingBox(srcRoD, rod, &rod);
    }

    return true;
}

/** @brief called when a param has just had its value changed */
void
FrameBlendPlugin::changedParam(const InstanceChangedArgs &args,
                               const std::string &paramName)
{
    if ( (paramName == kParamInputRangeName) && (args.reason == eChangeUserEdit) ) {
        OfxRangeD range;
        if ( _srcClip && _srcClip->isConnected() ) {
            range = _srcClip->getFrameRange();
        } else {
            timeLineGetBounds(range.min, range.max);
        }
        _frameRange->setValue( (int)range.min, (int)range.max );
        _absolute->setValue(true);
    }
}

mDeclarePluginFactory(FrameBlendPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
FrameBlendPluginFactory::describe(ImageEffectDescriptor &desc)
{
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
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

void
FrameBlendPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    if (context != eContextGenerator) {
        ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(true);
        if (context != eContextPaint) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }

    ClipDescriptor *fgM = desc.defineClip(kClipFgMName);
    fgM->addSupportedComponent(ePixelComponentAlpha);
    fgM->setTemporalClipAccess(true);
    fgM->setOptional(true);
    fgM->setSupportsTiles(kSupportsTiles);
    fgM->setIsMask(true);

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
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRangeName);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDimensionLabels("first", "last");
        param->setDefault(-5, 0);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAbsoluteName);
        param->setLabel(kParamAbsoluteLabel);
        param->setHint(kParamAbsoluteHint);
        param->setDefault(false);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamInputRangeName);
        param->setLabel(kParamInputRangeLabel);
        param->setHint(kParamInputRangeHint);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamFrameIntervalName);
        param->setLabel(kParamFrameIntervalLabel);
        param->setHint(kParamFrameIntervalHint);
        param->setRange(1, INT_MAX);
        param->setDisplayRange(1, 10);
        param->setDefault(1);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOperation);
        param->setLabel(kParamOperationLabel);
        param->setHint(kParamOperationHint);
        assert(param->getNOptions() == (int)eOperationAverage);
        param->appendOption(kParamOperationOptionAverage);
        assert(param->getNOptions() == (int)eOperationMin);
        param->appendOption(kParamOperationOptionMin);
        assert(param->getNOptions() == (int)eOperationMax);
        param->appendOption(kParamOperationOptionMax);
        assert(param->getNOptions() == (int)eOperationSum);
        param->appendOption(kParamOperationOptionSum);
        assert(param->getNOptions() == (int)eOperationProduct);
        param->appendOption(kParamOperationOptionProduct);
        assert(param->getNOptions() == (int)eOperationOver);
        param->appendOption(kParamOperationOptionOver);
        param->setDefault( (int)kParamOperationDefault );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDecayName);
        param->setLabel(kParamDecayLabel);
        param->setHint(kParamDecayHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamOutputCountName);
        param->setLabel(kParamOutputCountLabel);
        param->setHint(kParamOutputCountHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // FrameBlendPluginFactory::describeInContext

ImageEffect*
FrameBlendPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new FrameBlendPlugin(handle);
}

static FrameBlendPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
