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
#include "ofxsCopier.h"
#include "ofxsNonFinite.h" // finiteSample

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
// version 3.0: the frames blended are the lower bound of the range, then one frame out of frameInterval up to the upper bound
//              (version 2 aligned them on the last frame and dropped the lower bound, e.g. only frame 2 for range 0..2 with interval 2)
// version 3.1: Min and Max give 0 where no frame has a source pixel (they gave an infinite value, and NaN
//              with a mask or Mix), and Decay is clamped to [0,1], its range, on hosts that do not enforce it
//              (NaN audit); a merge result (Product, Over...) beyond the float range gives +-FLT_MAX, not +-inf (SupportExt #80),
//              and infinite inputs of the merge operators are made finite (+-FLT_MAX) instead of giving NaN (SupportExt #82);
//              a NaN Foreground Matte sample is 0, as in the mask mix (the frame was skipped, as under a matte) (issue #220)
//              Not an identity (pass-through of the only frame blended) where Mix, the mask, unprocessed channels or
//              the operator change it, and the unprocessed channels are the source ones with Mix 1 and no mask (they were 0);
//              the current frame is in the frames needed when the render reads it (issue #325);
//              Product is correct on byte and short images (it started at 1 instead of the maximum value,
//              so it was almost black, and where no frame is blended it is now white, as on float images),
//              and one frame with Decay 0 is passed through on byte and short images (issue #335)
// version 2.1 (kept for backward compatibility, frames aligned on the last frame of the range): the fixes of 3.1
//              (the NaN audit ones also applied to version 2, which was still registered as 2.0, SupportExt #80 and #82,
//              and issue #220), and the issue #325 fixes (identity, frames needed, and render: Natron 2.5.0 projects
//              with an unchecked channel, Mix 1 and no mask now get the source channel instead of 0, to be noted in the
//              Natron changelog); a one-frame range with a Frame Interval larger than 1 blends that frame (it was
//              passed through by isIdentity, and the render was black), and the issue #335 fix (Product on byte
//              and short images)
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

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
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha channel."
#endif

#define kParamFrameRangeName  "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHintBase "Range of frames which are to be blended together. Frame range is absolute if \"absolute\" is checked, else relative. "
#define kParamFrameRangeHint  kParamFrameRangeHintBase "The lower bound of the range is always included, and then one frame out of frameInterval within this interval."
#define kParamFrameRangeHintV2  kParamFrameRangeHintBase "The last frame is always included, and then one frame out of frameInterval within this interval." // version 2

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
    unsigned char *_touchedData; // Min/Max: whether a source pixel was already accumulated
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
        , _touchedData(NULL)
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
                         float* sumWeightsData,
                         unsigned char* touchedData)
    {_accumulatorData = accumulatorData; _countData = countData; _sumWeightsData = sumWeightsData; _touchedData = touchedData; }

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

    virtual OperationEnum getOperation() const = 0;

    // the initial value of the accumulator, in [0, maxValue] (the output where no frame has a source pixel)
    virtual float getInitialValue() const = 0;

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

    virtual OperationEnum getOperation() const OVERRIDE FINAL { return operation; }

    // Min and Max take the first source pixel as is (see touched), and multiplyFunc divides the
    // product by maxValue, so Product starts at maxValue, not 1 (issue #335)
    virtual float getInitialValue() const OVERRIDE FINAL
    {
        return (operation == eOperationProduct) ? static_cast<float>(maxValue) : 0.f;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
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
                        return process<true, true, true, true >(procWindow, rs); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow, rs); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow, rs); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow, rs); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow, rs); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow, rs); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow, rs); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow, rs); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow, rs); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow, rs); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow, rs); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow, rs); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow, rs); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow, rs); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow, rs); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow, rs); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow, const OfxPointD& rs)
    {
        unused(rs);
        assert(1 <= nComponents && nComponents <= 4);
        assert(!_lastPass || _dstPixelData);
        assert( _srcImgs.size() == _fgMImgs.size() );
        float tmpPix[nComponents];
        // Min and Max take the first source pixel as is (no infinite initial value, which the decay would
        // multiply and which would be output where no frame has a pixel): touched tells if there was one.
        const bool minMax = (operation == eOperationMin) || (operation == eOperationMax);
        const float initVal = getInitialValue();

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
                size_t renderPix = ( (size_t)(_renderWindow.x2 - _renderWindow.x1) * (y - _renderWindow.y1) +
                                     (x - _renderWindow.x1) );
                int count = _countData ? _countData[renderPix] : 0;
                float sumWeights = _sumWeightsData ? _sumWeightsData[renderPix] : 0;
                bool touched = _touchedData ? (_touchedData[renderPix] != 0) : false;
                if (_accumulatorData) {
                    std::copy(&_accumulatorData[renderPix * nComponents], &_accumulatorData[renderPix * nComponents + nComponents], tmpPix);
                } else {
                    std::fill(tmpPix, tmpPix + nComponents, initVal);
                }
                // accumulate
                for (unsigned i = 0; i < _srcImgs.size(); ++i) {
                    const PIX *fgMPix = (const PIX *)  (_fgMImgs[i] ? _fgMImgs[i]->getPixelAddress(x, y) : 0);
                    // a non-finite float matte sample is made finite (NaN gives 0, so the frame is blended
                    // as where the matte is 0, +-inf gives +-FLT_MAX, as in ofxsMaskMixPix) (issue #220)
                    if ( !fgMPix || (finiteSample<PIX, maxValue>(*fgMPix) <= 0) ) {
                        if ( (_decay > 0.) && (!minMax || touched) ) {
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
                                    tmpPix[c] = MergeImages2D::plusFunc((float)srcPixi[c], tmpPix[c]); // compute average in the end
                                    break;
                                case eOperationMin:
                                    tmpPix[c] = touched ? MergeImages2D::darkenFunc((float)srcPixi[c], tmpPix[c]) : (float)srcPixi[c];
                                    break;
                                case eOperationMax:
                                    tmpPix[c] = touched ? MergeImages2D::lightenFunc((float)srcPixi[c], tmpPix[c]) : (float)srcPixi[c];
                                    break;
                                case eOperationSum:
                                    tmpPix[c] = MergeImages2D::plusFunc((float)srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationProduct:
                                    tmpPix[c] = MergeImages2D::multiplyFunc<float,maxValue>(srcPixi[c], tmpPix[c]);
                                    break;
                                case eOperationOver:
                                    tmpPix[c] = MergeImages2D::overFunc<PIX, maxValue>(srcPixi[c], tmpPix[c], a, b);
                                    break;
                                }
                            }
                            touched = true;
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
                    if (_touchedData) {
                        _touchedData[renderPix] = touched;
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
                    } else if (nComponents == 2) {
                        // XY images have no alpha: normalize both channels like RGB
                        if (operation == eOperationAverage) {
                            for (int c = 0; c < 2; ++c) {
                                tmpPix[c] = (count ? (tmpPix[c] / sumWeights) : 0);
                            }
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
    FrameBlendPlugin(OfxImageEffectHandle handle,
                     unsigned int majorVersion)
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
        , _alignFramesOnLast(majorVersion < 3)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _fgMClip = fetchClip(kClipFgMName);
        assert(!_fgMClip || !_fgMClip->isConnected() || _fgMClip->getPixelComponents() == ePixelComponentAlpha);
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

    /** @brief compute the frames blended at time: first + i * interval, for 0 <= i < n */
    void getFrames(double time, int *first, int *interval, int *n);

    /** @brief the Process R, G, B and A checkboxes */
    struct ProcessChannels
    {
        bool r, g, b, a;
        bool all() const { return r && g && b && a; }
        bool none() const { return !r && !g && !b && !a; }
    };

    ProcessChannels getProcessChannels(double time) const;

    /** @brief true if render() applies the mask */
    bool getDoMasking(double time) const;

    /** @brief the Decay used by render(), clamped to its range [0,1] (NaN gives 0) */
    double getDecay(double time) const;

    /** @brief true if render() mixes the result with Source at the current time (mask or Mix) */
    static bool mixesWithSource(bool doMasking, double mix)
    {
        return doMasking || (mix != 1.);
    }

    /** @brief true if render() fetches Source at the current time: to mix the result with it, or to
        copy the unprocessed channels from it. Used by render(), isIdentity() and getFramesNeeded(). */
    static bool fetchesSourceAtTime(bool doMasking, double mix, const ProcessChannels& channels)
    {
        return mixesWithSource(doMasking, mix) || !channels.all();
    }

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
    bool _alignFramesOnLast; // version 2 and older: the blended frames are aligned on the last frame of the range
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

    // owns the images: not copyable (C.21)
    OptionalImagesHolder_RAII(const OptionalImagesHolder_RAII&) = delete;
    OptionalImagesHolder_RAII& operator=(const OptionalImagesHolder_RAII&) = delete;
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
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif

    // fetch the mask
    // auto ptr for the mask.
    bool doMasking = getDoMasking(time);
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            checkBadRenderScaleOrField(mask, args);
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        doMasking = true;
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    double mix = 1.;
    _mix->getValueAtTime(time, mix);
    double decay = getDecay(time);
    bool outputCount = false;
    if ( (dstComponents == ePixelComponentRGBA) || (dstComponents == ePixelComponentAlpha) ) {
        _outputCount->getValueAtTime(time, outputCount);
    }
    const ProcessChannels channels = getProcessChannels(time);

    // If masking, mixing, or copying unprocessed channels, fetch the original image
    // (the unprocessed channels were 0 with Mix 1 and no mask, issue #325)
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() && fetchesSourceAtTime(doMasking, mix, channels) ) ?
                                    _srcClip->fetchImage(args.time) :
                                    0 );
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
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
    auto_ptr<ImageMemory> touched;
    unsigned char *touchedData = NULL;

    // compute range
    int first, interval, n;
    getFrames(time, &first, &interval, &n);
    decay = 1. - ipow(1. - decay, std::abs(interval)); // adjust decay so that the final aspect is similar for different values of the interval
    if (n <= 0) {
        // no frame is blended (frame interval larger than the frame range)
        fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

        return;
    }

    const OfxRectI& renderWindow = args.renderWindow;
    size_t nPixels = (size_t)(renderWindow.y2 - renderWindow.y1) * (renderWindow.x2 - renderWindow.x1);
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
                std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, processor.getInitialValue());
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
            // Initialize touched image if operator is min or max
            if ( !touchedData && ( (operation == eOperationMin) || (operation == eOperationMax) ) ) {
                touched.reset( new ImageMemory(nPixels * sizeof(unsigned char), this) );
                touchedData = (unsigned char*)touched->lock();
                std::fill(touchedData, touchedData + nPixels, 0);
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
#         ifndef NDEBUG
                checkBadRenderScaleOrField(src, args);
#         endif
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
                checkBadRenderScaleOrField(mask, args);
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
        processor.setRenderWindow(renderWindow, args.renderScale);
        processor.setAccumulators(accumulatorData, countData, sumWeightsData, touchedData);

        processor.setValues(channels.r, channels.g, channels.b, channels.a,
                            lastPass, decay, outputCount, mix);

        // Call the base class process member, this will call the derived templated process code
        processor.process();
    }
} // FrameBlendPlugin::setupAndProcess

void
FrameBlendPlugin::getFrames(double time,
                            int *firstp,
                            int *intervalp,
                            int *np)
{
    bool absolute;
    _absolute->getValueAtTime(time, absolute);
    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    int interval;
    _frameInterval->getValueAtTime(time, interval);
    interval = (std::max)(1, interval);

    int n;
    if (_alignFramesOnLast) {
        // version 2 (kept so that old projects render the same): the last frame is always in the image set, so first frame must be adjusted if abs(interval) > 1,
        // and the lower bound of the range is dropped when the range length is not a multiple of the interval
        n = (std::abs(last - first) + 1) / interval;
        if ( (n == 0) && (first == last) ) {
            // a one-frame range with an interval larger than 1: blend that frame (versions 2.0 and
            // older passed it through with isIdentity; the range gave no frame, and the render black)
            n = 1;
        }
        if (first > last) {
            interval = -interval;
        }
        first = last - (n - 1) * interval;
    } else {
        // the frames are lower, lower + interval, ..., up to the upper bound (inclusive),
        // and they are processed from the first frame of the range towards the last one
        n = std::abs(last - first) / interval + 1;
        if (first > last) {
            // start from the highest frame of the set and step towards the lower bound
            first = last + (n - 1) * interval;
            interval = -interval;
        }
    }
    if (!absolute) {
        first += time;
    }
    *firstp = first;
    *intervalp = interval;
    *np = n;
}

FrameBlendPlugin::ProcessChannels
FrameBlendPlugin::getProcessChannels(double time) const
{
    ProcessChannels channels;
    _processR->getValueAtTime(time, channels.r);
    _processG->getValueAtTime(time, channels.g);
    _processB->getValueAtTime(time, channels.b);
    _processA->getValueAtTime(time, channels.a);

    return channels;
}

bool
FrameBlendPlugin::getDoMasking(double time) const
{
    return ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected();
}

double
FrameBlendPlugin::getDecay(double time) const
{
    // the range of Decay is [0,1], which hosts may not enforce: outside, sumWeights could be 0 with count > 0
    return (std::max)( 0., (std::min)(_decay->getValueAtTime(time), 1.) );
}

// the overridden render function
void
FrameBlendPlugin::render(const RenderArguments &args)
{
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
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

    const ProcessChannels channels = getProcessChannels(time);
    if ( channels.none() ) {
        identityClip = _srcClip;

        return true;
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

    const bool doMasking = getDoMasking(time);

    // one frame is blended (the same frames as in render()): the render copies it only if nothing
    // else changes it (issue #325)
    int first, interval, n;
    getFrames(time, &first, &interval, &n);
    // the mask and Mix mix with the source at the current time, which is not an exact copy (even
    // at the same frame); the unprocessed channels are copied from it
    if ( (n == 1) && !mixesWithSource(doMasking, mix) && ( channels.all() || (first == time) ) ) {
        bool copiesFrame;
        switch ( static_cast<OperationEnum>( _operation->getValueAtTime(time) ) ) {
        case eOperationMin:
        case eOperationMax:
            // the first source pixel is taken as is
            copiesFrame = true;
            break;
        case eOperationAverage:
        case eOperationSum:
        case eOperationOver:
            // on float images, the merge operators make the infinite values finite (+-FLT_MAX,
            // SupportExt #82), and -0 gives +0 (0 + -0 = +0); on byte and short images, they copy
            // the pixel (0 + A, A / 1, A over 0)
            copiesFrame = ( _srcClip->getPixelDepth() != eBitDepthFloat );
            break;
        case eOperationProduct:
            // the initial value maxValue is multiplied by 1 - Decay, then by the pixel divided by
            // maxValue (issue #335): with Decay 0, a copy on byte and short images (maxValue * A /
            // maxValue is exact); on float images, the merge operators make the infinite values finite
            copiesFrame = ( _srcClip->getPixelDepth() != eBitDepthFloat ) && (getDecay(time) <= 0.);
            break;
        default:
            copiesFrame = false;
            break;
        }
        if (copiesFrame) {
            identityClip = _srcClip;
            identityTime = first;

            return true;
        }
    }

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
    // must be the same frames as in render()
    int first, interval, n;
    getFrames(args.time, &first, &interval, &n);
    if (n <= 0) {
        // no frame is blended and render outputs black: the OFX spec has no empty frame range,
        // so set nothing and let the host use its default (the current frame)
        return;
    }
    const double time = args.time;
    const int last = first + (n - 1) * interval;
    OfxRangeD range;
    bool hasTime; // the blended frames include the current time
    if (std::abs(interval) == 1) {
        range.min = (std::min)(first, last);
        range.max = (std::max)(first, last);
        frames.setFramesNeeded(*_srcClip, range);
        hasTime = (range.min <= time) && (time <= range.max);
    } else {
        hasTime = false;
        for (int i = 0; i < n; ++i) {
            range.min = range.max = first + i * interval;
            frames.setFramesNeeded(*_srcClip, range);
            hasTime = hasTime || (range.min == time);
        }
    }
    // render() also fetches Source at the current time to mask, mix or copy unprocessed channels
    if ( !hasTime && fetchesSourceAtTime( getDoMasking(time), _mix->getValueAtTime(time), getProcessChannels(time) ) ) {
        range.min = range.max = time;
        frames.setFramesNeeded(*_srcClip, range);
    }
}

bool
FrameBlendPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    // must be the union of the RoDs of the frames used in render()
    int first, interval, n;
    getFrames(args.time, &first, &interval, &n);
    if (n <= 0) {
        return false; // use default behavior
    }

    rod = _srcClip->getRegionOfDefinition(first);

    for (int i = 1; i < n; ++i) {
        OfxRectD srcRoD = _srcClip->getRegionOfDefinition(first + i * interval);
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

mDeclarePluginFactoryVersioned(FrameBlendPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
FrameBlendPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

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

template<unsigned int majorVersion>
void
FrameBlendPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
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
        param->setHint(majorVersion < 3 ? kParamFrameRangeHintV2 : kParamFrameRangeHint);
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

template<unsigned int majorVersion>
ImageEffect*
FrameBlendPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new FrameBlendPlugin(handle, majorVersion);
}

static FrameBlendPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
// version 2: the blended frames are aligned on the last frame of the range
static FrameBlendPluginFactory<2> pv2(kPluginIdentifier, 1); // 2.1: see the history at the top
mRegisterPluginFactoryInstance(p)
mRegisterPluginFactoryInstance(pv2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
