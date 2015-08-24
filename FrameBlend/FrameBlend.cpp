/*
 OFX FrameBlend plugin.

 Copyright (C) 2014 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

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


 This plugin is based on:

 OFX retimer example plugin, a plugin that illustrates the use of the OFX Support library.

 This will not work very well on fielded imagery.

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

// TODO:
// - show progress

#include "FrameBlend.h"

#include <cmath> // for floor
#include <climits> // for kOfxFlagInfiniteMax
#include <cassert>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsPixelProcessor.h"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

#define kPluginName "FrameBlendOFX"
#define kPluginGrouping "Time"
#define kPluginDescription \
"Blend frames of the input clip.\n" \
"If a foreground matte is connected, only pixels with a negative or zero (<= 0) foreground value are taken into account.\n" \
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

#define kParamFrameRangeName  "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint  "Range of frames which are to be blended together. Frame range is absolute if \"absolute\" is checked, else relative."

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
#define kParamOperationOptionAverage "Average"
#define kParamOperationOptionAverageHint "Output is the average of selected frames."
#define kParamOperationOptionMin "Min"
#define kParamOperationOptionMinHint "Output is the minimum of selected frames."
#define kParamOperationOptionMax "Max"
#define kParamOperationOptionMaxHint "Output is the maximum of selected frames."
#define kParamOperationOptionSum "Sum"
#define kParamOperationOptionSumHint "Output is the sum/addition of selected frames."
#define kParamOperationOptionProduct "Product"
#define kParamOperationOptionProductHint "Output is the product/multiplication of selected frames."
#define kParamOperationDefault eOperationAverage
enum OperationEnum {
    eOperationAverage,
    eOperationMin,
    eOperationMax,
    eOperationSum,
    eOperationProduct,
};


#define kParamOutputCountName  "outputCount"
#define kParamOutputCountLabel "Output Count to Alpha"
#define kParamOutputCountHint  "Output image count at each pixel to alpha (input must have an alpha channel)."

#define kClipFgMName "FgM"

#define kFrameChunk 4 // how many frames to process simultaneously

using namespace OFX;

class FrameBlendProcessorBase : public OFX::PixelProcessor
{
protected:
    const OFX::Image *_srcImg;
    std::vector<const OFX::Image*> _srcImgs;
    std::vector<const OFX::Image*> _fgMImgs;
    float *_accumulatorData;
    unsigned short *_countData;
    const OFX::Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    bool _lastPass;
    bool _outputCount;
    bool  _doMasking;
    double _mix;
    bool _maskInvert;

public:

    FrameBlendProcessorBase(OFX::ImageEffect &instance)
    : OFX::PixelProcessor(instance)
    , _srcImg(0)
    , _srcImgs(0)
    , _fgMImgs(0)
    , _accumulatorData(0)
    , _countData(0)
    , _maskImg(0)
    , _processR(true)
    , _processG(true)
    , _processB(true)
    , _processA(false)
    , _lastPass(false)
    , _outputCount(false)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    void setSrcImgs(const OFX::Image *src, const std::vector<const OFX::Image*> &v) {_srcImg = src; _srcImgs = v;}
    void setFgMImgs(const std::vector<const OFX::Image*> &v) {_fgMImgs = v;}
    void setAccumulators(float *accumulatorData, unsigned short *countData)
    {_accumulatorData = accumulatorData; _countData = countData;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   bool lastPass,
                   bool outputCount,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _lastPass = lastPass;
        _outputCount = outputCount;
        _mix = mix;
    }

    virtual OperationEnum getOperation() = 0;
private:
};



template <class PIX, int nComponents, int maxValue, OperationEnum operation>
class FrameBlendProcessor : public FrameBlendProcessorBase
{
public:
    FrameBlendProcessor(OFX::ImageEffect &instance)
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
                        return process<true ,true ,true ,true >(procWindow); // RGBA
                    } else {
                        return process<true ,true ,true ,false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true ,true ,false,true >(procWindow); // RGbA
                    } else {
                        return process<true ,true ,false,false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true ,false,true ,true >(procWindow); // RgBA
                    } else {
                        return process<true ,false,true ,false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true ,false,false,true >(procWindow); // RgbA
                    } else {
                        return process<true ,false,false,false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false,true ,true ,true >(procWindow); // rGBA
                    } else {
                        return process<false,true ,true ,false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false,true ,false,true >(procWindow); // rGbA
                    } else {
                        return process<false,true ,false,false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false,false,true ,true >(procWindow); // rgBA
                    } else {
                        return process<false,false,true ,false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false,false,false,true >(procWindow); // rgbA
                    } else {
                        return process<false,false,false,false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif
    }

    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        assert(1 <= nComponents && nComponents <= 4);
        assert(!_lastPass || _dstPixelData);
        assert(_srcImgs.size() == _fgMImgs.size());
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
            }
        }

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
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
                size_t renderPix = ((_renderWindow.x2 - _renderWindow.x1) * (y - _renderWindow.y1) +
                                    (x - _renderWindow.x1));
                int count = _countData ? _countData[renderPix] : 0;
                if (_accumulatorData) {
                    std::copy(&_accumulatorData[renderPix * nComponents], &_accumulatorData[renderPix * nComponents + nComponents], tmpPix);
                } else {
                    std::fill(tmpPix, tmpPix + nComponents, initVal);
                }
                // accumulate
                for (unsigned i = 0; i < _srcImgs.size(); ++i) {
                    const PIX *fgMPix = (const PIX *)  (_fgMImgs[i] ? _fgMImgs[i]->getPixelAddress(x, y) : 0);
                    if (!fgMPix || *fgMPix <= 0) {
                        const PIX *srcPixi = (const PIX *)  (_srcImgs[i] ? _srcImgs[i]->getPixelAddress(x, y) : 0);
                        if (srcPixi) {
                            for (int c = 0; c < nComponents; ++c) {
                                switch (operation) {
                                    case eOperationAverage:
                                        tmpPix[c] += srcPixi[c];
                                        break;
                                    case eOperationMin:
                                        tmpPix[c] = std::min(tmpPix[c], (float)srcPixi[c]);
                                        break;
                                    case eOperationMax:
                                        tmpPix[c] = std::max(tmpPix[c], (float)srcPixi[c]);
                                        break;
                                    case eOperationSum:
                                        tmpPix[c] += srcPixi[c];
                                        break;
                                    case eOperationProduct:
                                        tmpPix[c] *= srcPixi[c];
                                        break;
                                }
                            }
                        }
                        ++count;
                    }
                }
                if (!_lastPass) {
                    if (_countData) {
                        _countData[renderPix] = count;
                    }
                    if (_accumulatorData) {
                        std::copy(tmpPix, tmpPix + nComponents , &_accumulatorData[renderPix * nComponents]);
                    }
                } else {
                    // copy back original values from unprocessed channels
                    if (nComponents == 1) {
                        int c = 0;
                        if (_outputCount) {
                            tmpPix[c] = count;
                        } else if (operation == eOperationAverage) {
                            tmpPix[c] =  (count ? (tmpPix[c] / count) : 0);
                        }
                    } else if (3 <= nComponents && nComponents <= 4) {
                        if (operation == eOperationAverage) {
                            for (int c = 0; c < 3; ++c) {
                                tmpPix[c] = (count ? (tmpPix[c] / count) : 0);
                            }
                        }
                        if (nComponents >= 4) {
                            int c = nComponents - 1;
                            if (_outputCount) {
                                tmpPix[c] = count;
                            } else if (operation == eOperationAverage) {
                                tmpPix[c] =  (count ? (tmpPix[c] / count) : 0);
                            }
                        }
                    }
                    // tmpPix is not normalized, it is within [0,maxValue]
                    ofxsMaskMixPix<PIX,nComponents,maxValue,true>(tmpPix, x, y, srcPix, _doMasking,
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
                        if (nComponents >= 2 && !processG) {
                            dstPix[1] = srcPix ? srcPix[1] : PIX();
                        }
                        if (nComponents >= 3 && !processB) {
                            dstPix[2] = srcPix ? srcPix[2] : PIX();
                        }
                        if (nComponents >= 4 && !processA) {
                            dstPix[3] = srcPix ? srcPix[3] : PIX();
                        }
                    }
                    // increment the dst pixel
                    dstPix += nComponents;
                }
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameBlendPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    FrameBlendPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _maskClip(0)
    , _fgMClip(0)
    , _processR(0)
    , _processG(0)
    , _processB(0)
    , _processA(0)
    , _frameRange(0)
    , _absolute(0)
    , _inputRange(0)
    , _frameInterval(0)
    , _operation(0)
    , _outputCount(0)
    , _mix(0)
    , _maskApply(0)
    , _maskInvert(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha ||
                            _dstClip->getPixelComponents() == ePixelComponentXY ||
                            _dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha ||
                             _srcClip->getPixelComponents() == ePixelComponentXY ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _fgMClip = fetchClip(kClipFgMName);
        assert(!_fgMClip || _fgMClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _frameRange = fetchInt2DParam(kParamFrameRangeName);
        _absolute = fetchBooleanParam(kParamAbsoluteName);
        _inputRange = fetchPushButtonParam(kParamInputRangeName);
        _frameInterval = fetchIntParam(kParamFrameIntervalName);
        _operation = fetchChoiceParam(kParamOperation);
        _outputCount = fetchBooleanParam(kParamOutputCountName);
        assert(_frameRange && _absolute && _inputRange && _operation && _outputCount);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(FrameBlendProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue, OperationEnum operation>
    void renderForOperation(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    OFX::Clip *_fgMClip;
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    Int2DParam* _frameRange;
    BooleanParam* _absolute;
    PushButtonParam* _inputRange;
    IntParam* _frameInterval;
    ChoiceParam* _operation;
    BooleanParam* _outputCount;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

namespace {
// Since we cannot hold a std::auto_ptr in the vector we must hold a raw pointer.
// To ensure that images are always freed even in case of exceptions, use a RAII class.
struct OptionalImagesHolder_RAII
{
    std::vector<const OFX::Image*> images;
    
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
}

/* set up and run a processor */
void
FrameBlendPlugin::setupAndProcess(FrameBlendProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(time));
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

    // fetch the mask
    // auto ptr for the mask.
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    // do we do masking
    if (doMasking) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
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
    bool outputCount = false;
    if (dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentAlpha) {
        _outputCount->getValue(outputCount);
    }
    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    // If masking or mixing, fetch the original image
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected() && (doMasking || mix != 1.)) ?
                                        _srcClip->fetchImage(args.time) :
                                        0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // accumulator image
    std::auto_ptr<OFX::ImageMemory> accumulator;
    float *accumulatorData = NULL;
    std::auto_ptr<OFX::ImageMemory> count;
    unsigned short *countData = NULL;

    // compute range
    bool absolute;
    _absolute->getValueAtTime(time, absolute);
    int min, max;
    _frameRange->getValueAtTime(time, min, max);
    if (min > max) {
        std::swap(min, max);
    }
    int interval;
    _frameInterval->getValueAtTime(time, interval);
    interval = std::max(1,interval);

    int n = (max + 1 - min) / interval;
    if (!absolute) {
        min += time;
        //max += time; // max is not used anymore
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
        imax = std::min(imin + kFrameChunk, n);
        bool lastPass = (imax == n);

        if (!lastPass) {
            // Initialize accumulator image (always use float)
            if (!accumulatorData) {
                int dstNComponents = _dstClip->getPixelComponentCount();
                accumulator.reset(new OFX::ImageMemory(nPixels * dstNComponents * sizeof(float), this));
                accumulatorData = (float*)accumulator->lock();
                switch (operation) {
                    case eOperationAverage:
                    case eOperationSum:
                        std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, 0.);
                        break;
                    case eOperationMin:
                        std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, std::numeric_limits<float>::infinity());
                        break;
                    case eOperationMax:
                        std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, -std::numeric_limits<float>::infinity());
                        break;
                    case eOperationProduct:
                        std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, 1.);
                        break;
                }

            }
            // Initialize count image if operator is average or outputCount is true and output has alpha (use short)
            if (!countData && (operation == eOperationAverage || outputCount)) {
                count.reset(new OFX::ImageMemory(nPixels * sizeof(unsigned short), this));
                countData = (unsigned short*)count->lock();
                std::fill(countData, countData + nPixels, 0);
            }
        }

        // fetch the source images
        OptionalImagesHolder_RAII srcImgs;
        for (int i = imin; i < imax; ++i) {
            if (abort()) {
                return;
            }
            const OFX::Image* src = _srcClip ? _srcClip->fetchImage(min + i*interval) : 0;
            if (src) {
                if (src->getRenderScale().x != args.renderScale.x ||
                    src->getRenderScale().y != args.renderScale.y ||
                    (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
                    setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                }
                OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
                OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
                if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
                    OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
            srcImgs.images.push_back(src);
        }
        // fetch the foreground mattes
        OptionalImagesHolder_RAII fgMImgs;
        for (int i = imin; i < imax; ++i) {
            if (abort()) {
                return;
            }
            const OFX::Image* mask = (_fgMClip && _fgMClip->isConnected()) ? _fgMClip->fetchImage(min + i*interval) : 0;
            if (mask) {
                assert(_fgMClip->isConnected());
                if (mask->getRenderScale().x != args.renderScale.x ||
                    mask->getRenderScale().y != args.renderScale.y ||
                    (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                    setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                    OFX::throwSuiteStatusException(kOfxStatFailed);
                }
            }
            fgMImgs.images.push_back(mask);
        }

        // set the images
        if (lastPass) {
            processor.setDstImg(dst.get());
        }
        processor.setSrcImgs(lastPass ? src.get() : 0, srcImgs.images);
        processor.setFgMImgs(fgMImgs.images);
        // set the render window
        processor.setRenderWindow(renderWindow);
        processor.setAccumulators(accumulatorData, countData);

        processor.setValues(processR, processG, processB, processA,
                            lastPass, outputCount, mix);
        
        // Call the base class process member, this will call the derived templated process code
        processor.process();
    }
}

// the overridden render function
void
FrameBlendPlugin::render(const OFX::RenderArguments &args)
{
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentAlpha) {
        renderForComponents<1>(args);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args);
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        renderForComponents<3>(args);
    }
}

template<int nComponents>
void
FrameBlendPlugin::renderForComponents(const OFX::RenderArguments &args)
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
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
FrameBlendPlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    int operation_i;
    _operation->getValueAtTime(args.time, operation_i);
    OperationEnum operation = (OperationEnum)operation_i;

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

    }
}

template <class PIX, int nComponents, int maxValue, OperationEnum operation>
void
FrameBlendPlugin::renderForOperation(const OFX::RenderArguments &args)
{
    FrameBlendProcessor<PIX, nComponents, maxValue, eOperationAverage> fred(*this);
    setupAndProcess(fred, args);
}

bool
FrameBlendPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime)
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
    
    if (_fgMClip && _fgMClip->isConnected()) {
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
        identityTime = time;
        return true;
    }

    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClip;
                return true;
            }
        }
    }

    return false;
}

void
FrameBlendPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                                   OFX::FramesNeededSetter &frames)
{
    const double time = args.time;
    bool absolute;
    _absolute->getValueAtTime(time, absolute);
    OfxRangeD range;
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
    if (interval <= 1) {
        range.min = min;
        range.max = max;
        frames.setFramesNeeded(*_srcClip, range);
    } else {
        for (int i = min; i <= max; i += interval) {
            range.min = range.max = i;
            frames.setFramesNeeded(*_srcClip, range);
        }
    }
}

bool
FrameBlendPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
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
        OFX::Coords::rectBoundingBox(srcRoD, rod, &rod);
    }
    return true;
}

/** @brief called when a param has just had its value changed */
void
FrameBlendPlugin::changedParam(const InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamInputRangeName && args.reason == eChangeUserEdit) {
        OfxRangeD range;
        if ( _srcClip && _srcClip->isConnected() ) {
            range = _srcClip->getFrameRange();
        } else {
            timeLineGetBounds(range.min, range.max);
        }
        _frameRange->setValue((int)range.min, (int)range.max);
        _absolute->setValue(true);
    }
}


mDeclarePluginFactory(FrameBlendPluginFactory, {}, {});

void FrameBlendPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif

}

void FrameBlendPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kNatronOfxParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kNatronOfxParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kNatronOfxParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRangeName);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDimensionLabels("min", "max");
        param->setDefault(-5, 0);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine);
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
        param->setLayoutHint(eLayoutHintNoNewLine);
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
        param->setRange(1, kOfxFlagInfiniteMax);
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
        param->appendOption(kParamOperationOptionAverage, kParamOperationOptionAverageHint);
        assert(param->getNOptions() == (int)eOperationMin);
        param->appendOption(kParamOperationOptionMin, kParamOperationOptionMinHint);
        assert(param->getNOptions() == (int)eOperationMax);
        param->appendOption(kParamOperationOptionMax, kParamOperationOptionMaxHint);
        assert(param->getNOptions() == (int)eOperationSum);
        param->appendOption(kParamOperationOptionSum, kParamOperationOptionSumHint);
        assert(param->getNOptions() == (int)eOperationProduct);
        param->appendOption(kParamOperationOptionProduct, kParamOperationOptionProductHint);
        param->setDefault((int)kParamOperationDefault);
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
}

OFX::ImageEffect* FrameBlendPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new FrameBlendPlugin(handle);
}

void getFrameBlendPluginID(OFX::PluginFactoryArray &ids)
{
    static FrameBlendPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

