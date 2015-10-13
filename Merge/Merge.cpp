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
 * OFX Merge plugin.
 */

#include "Merge.h"

#include <cmath>
#include <algorithm>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"

#include "ofxNatron.h"
#include "ofxsMacros.h"

#define kPluginName "MergeOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
"Pixel-by-pixel merge operation between two or more inputs.\n" \
"Input A is first merged with B (B is non-optional), then A2, if connected, is merged with the intermediary result, then A3, etc.\n" \
"A complete explanation of the different operators can be found in \"Compositing Digital Images\", by T. Porter and T. Duff (Proc. SIGGRAPH 1984) http://keithp.com/~keithp/porterduff/p253-porter.pdf\n" \
"See also:\n" \
"- \"Digital Image Compositing\" by Marc Levoy https://graphics.stanford.edu/courses/cs248-06/comp/comp.html\n" \
"- \"Merge Blend Modes\" by Martin Constable http://opticalenquiry.com/nuke/index.php?title=Merge_Blend_Modes."

#define kPluginGroupingSub "Merge/Merges"

#define kPluginNamePlus "PlusOFX"
#define kPluginNameMatte "MatteOFX"
#define kPluginNameMultiply "MultiplyOFX"
#define kPluginNameIn "InOFX"
#define kPluginNameOut "OutOFX"
#define kPluginNameScreen "ScreenOFX"
#define kPluginNameMax "MaxOFX"
#define kPluginNameMin "MinOFX"
#define kPluginNameAbsminus "AbsminusOFX"

#define kPluginIdentifier "net.sf.openfx.MergePlugin"
#define kPluginIdentifierSub "net.sf.openfx.Merge"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint \
"The operation used to merge the input A and B images.\n" \
"The operator formula is applied to each component: A and B represent the input component (Red, Green, Blue, or Alpha) of each input, and a and b represent the Alpha component of each input.\n" \
"If Alpha masking is checked, the output alpha is computed using a different formula (a+b - a*b).\n" \
"Alpha masking is always enabled for HSL modes (hue, saturation, color, luminosity)."

#define kParamAlphaMasking "screenAlpha"
#define kParamAlphaMaskingLabel "Alpha masking"
#define kParamAlphaMaskingHint "When enabled, the input images are unchanged where the other image has 0 alpha, and" \
    " the output alpha is set to a+b - a*b. When disabled the alpha channel is processed as " \
    "any other channel. Option is disabled for operations where it does not apply or makes no difference."

#define kParamBbox "bbox"
#define kParamBboxLabel "Bounding Box"
#define kParamBboxHint "What to use to produce the output image's bounding box."

#define kClipA "A"
#define kClipB "B"

#define kMaximumAInputs 64

using namespace OFX;
using namespace MergeImages2D;

class MergeProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImgA;
    const OFX::Image *_srcImgB;
    const OFX::Image *_maskImg;
    std::vector<const OFX::Image*> _optionalAImages;
    bool  _doMasking;
    int _bbox;
    bool _alphaMasking;
    double _mix;
    bool _maskInvert;

public:
    
    MergeProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    , _maskImg(0)
    , _doMasking(false)
    , _bbox(0)
    , _alphaMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
        
    }
    
    void setSrcImg(const OFX::Image *A, const OFX::Image *B,
                   const std::vector<const OFX::Image*>& optionalAImages) {
        _srcImgA = A;
        _srcImgB = B;
        _optionalAImages = optionalAImages;
    }
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}

    void setValues(int bboxChoice,
                   bool alphaMasking,
                   double mix)
    {
        _bbox = bboxChoice;
        _alphaMasking = alphaMasking;
        _mix = mix;
    }
    
};



template <MergingFunctionEnum f, class PIX, int nComponents, int maxValue>
class MergeProcessor : public MergeProcessorBase
{
public:
    MergeProcessor(OFX::ImageEffect &instance)
    : MergeProcessorBase(instance)
    {
    }
    
private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float tmpPix[4];
        float tmpA[4];
        float tmpB[4];

        for (int c = 0; c < 4; ++c) {
            tmpA[c] = tmpB[c] = 0.;
        }
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                const PIX *srcPixA = (const PIX *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);
                
                assert(_optionalAImages.size() == 0 || _optionalAImages.size() == (kMaximumAInputs - 1));
                
                
                if (srcPixA || srcPixB) {

                    for (int c = 0; c < nComponents; ++c) {
#                     ifdef DEBUG
                        // check for NaN
                        assert(!srcPixA || srcPixA[c] == srcPixA[c]);
                        assert(!srcPixB || srcPixB[c] == srcPixB[c]);
#                     endif
                        // all images are supposed to be black and transparent outside o
                        tmpA[c] = srcPixA ? ((float)srcPixA[c] / maxValue) : 0.f;
                        tmpB[c] = srcPixB ? ((float)srcPixB[c] / maxValue) : 0.f;
#                     ifdef DEBUG
                        // check for NaN
                        assert(tmpA[c] == tmpA[c]);
                        assert(tmpB[c] == tmpB[c]);
#                     endif
                    }
                    if (nComponents != 4) {
                        // set alpha (1 inside, 0 outside)
                        tmpA[3] = srcPixA ? 1. : 0.;
                        tmpB[3] = srcPixB ? 1. : 0.;
                    }
                    // work in float: clamping is done when mixing
                    mergePixel<f, float, 4, 1>(_alphaMasking, tmpA, tmpB, tmpPix);
    
                    
                } else {
                    // everything is black and transparent
                    for (int c = 0; c < 4; ++c) {
                        tmpPix[c] = 0;
                    }
                }

#             ifdef DEBUG
                // check for NaN
                for (int c = 0; c < 4; ++c) {
                    assert(tmpPix[c] == tmpPix[c]);
                }
#             endif

                for (unsigned int i = 0; i < _optionalAImages.size(); ++i) {
                    srcPixA = (const PIX *)  (_optionalAImages[i] ? _optionalAImages[i]->getPixelAddress(x, y) : 0);
                    
                    if (srcPixA) {
                        
                        for (int c = 0; c < nComponents; ++c) {
#                     ifdef DEBUG
                            // check for NaN
                            assert(srcPixA[c] == srcPixA[c]);
#                     endif
                            // all images are supposed to be black and transparent outside o
                            tmpA[c] = (float)srcPixA[c] / maxValue;
#                         ifdef DEBUG
                            // check for NaN
                            assert(tmpA[c] == tmpA[c]);
#                         endif
                        }
                        if (nComponents != 4) {
                            // set alpha (1 inside, 0 outside)
                            assert(srcPixA);
                            tmpA[3] = 1.;
                        }
                        for (int c = 0; c < 4; ++c) {
                            tmpB[c] = tmpPix[c];
                        }

                        // work in float: clamping is done when mixing
                        mergePixel<f, float, nComponents, 1>(_alphaMasking, tmpA, tmpB, tmpPix);

#                     ifdef DEBUG
                        // check for NaN
                        for (int c = 0; c < 4; ++c) {
                            assert(tmpPix[c] == tmpPix[c]);
                        }
#                     endif
                    }
                }
                
                // tmpPix has 4 components, but we only need the first nComponents

                // denormalize
                for (int c = 0; c < nComponents; ++c) {
                    tmpPix[c] *= maxValue;
                }
                
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                
                dstPix += nComponents;
            }
        }
    }
    
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MergePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    MergePlugin(OfxImageEffectHandle handle, bool numerousInputs)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClipA(0)
    , _srcClipB(0)
    , _maskClip(0)
    , _optionalASrcClips(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha));
        _srcClipA = fetchClip(kClipA);
        assert(_srcClipA && (_srcClipA->getPixelComponents() == ePixelComponentRGB || _srcClipA->getPixelComponents() == ePixelComponentRGBA || _srcClipA->getPixelComponents() == ePixelComponentAlpha));
        
        if (numerousInputs) {
            _optionalASrcClips.resize(kMaximumAInputs - 1);
            for (int i = 2; i <= kMaximumAInputs; ++i) {
                std::stringstream ss;
                ss << kClipA << i;
                std::string clipName = ss.str();
                OFX::Clip* clip = fetchClip(clipName);
                assert(clip && (clip->getPixelComponents() == ePixelComponentRGB || clip->getPixelComponents() == ePixelComponentRGBA || clip->getPixelComponents() == ePixelComponentAlpha));
                _optionalASrcClips[i - 2] = clip;
            }
        }
        
        _srcClipB = fetchClip(kClipB);
        assert(_srcClipB && (_srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA || _srcClipB->getPixelComponents() == ePixelComponentAlpha));
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _operation = fetchChoiceParam(kParamOperation);
        _operationString = fetchStringParam(kNatronOfxParamStringSublabelName);
        _bbox = fetchChoiceParam(kParamBbox);
        _alphaMasking = fetchBooleanParam(kParamAlphaMasking);
        assert(_operation && _operationString && _bbox && _alphaMasking);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }
    
private:
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(MergeProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    
private:
    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClipA;
    OFX::Clip *_srcClipB;
    OFX::Clip *_maskClip;
    std::vector<OFX::Clip *> _optionalASrcClips;
    
    OFX::ChoiceParam *_operation;
    OFX::StringParam *_operationString;
    OFX::ChoiceParam *_bbox;
    OFX::BooleanParam *_alphaMasking;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
};


// override the rod call
bool
MergePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!_srcClipA->isConnected() && !_srcClipB->isConnected()) {
        return false;
    }
    
    OfxRectD rodA = _srcClipA->getRegionOfDefinition(args.time);
    OfxRectD rodB = _srcClipB->getRegionOfDefinition(args.time);
   
    
    int bboxChoice;
    double mix;
    _mix->getValueAtTime(args.time, mix);
    //Do the same as isIdentity otherwise the result of getRegionOfDefinition() might not be coherent with the RoD of the identity clip.
    if (mix == 0.) {
        rod = rodB;
        return true;
    }
    
    _bbox->getValueAtTime(args.time, bboxChoice);
    
    switch (bboxChoice) {
        case 0: { //union
            bool aConnected = _srcClipA->isConnected();
            bool bConnected = _srcClipB->isConnected();
            bool rodSet = false;
            if (aConnected) {
                rod = rodA;
                rodSet = true;
            }
            if (bConnected) {
                if (!rodSet) {
                    rod = rodB;
                    rodSet = true;
                } else {
                    OFX::Coords::rectBoundingBox(rod, rodB, &rod);
                }
            }
            
            for (unsigned int i = 0; i < _optionalASrcClips.size(); ++i) {
                OfxRectD rodOptionalA = _optionalASrcClips[i]->getRegionOfDefinition(args.time);
                if (!_optionalASrcClips[i]->isConnected()) {
                    continue;
                }
                if (rodSet) {
                    OFX::Coords::rectBoundingBox(rodOptionalA, rod, &rod);
                } else {
                    rod = rodOptionalA;
                    rodSet = true;
                }
            }
			return true;
		}
        case 1: { //intersection
            bool interesect = OFX::Coords::rectIntersection(rodA, rodB, &rod);
            if (!interesect) {
                setPersistentMessage(OFX::Message::eMessageError, "", "Input images intersection is empty.");
                return false;
            }
            for (unsigned int i = 0; i < _optionalASrcClips.size(); ++i) {
                OfxRectD rodOptionalA = _optionalASrcClips[i]->getRegionOfDefinition(args.time);
                interesect = OFX::Coords::rectIntersection(rodOptionalA, rod, &rod);
                if (!interesect) {
                    setPersistentMessage(OFX::Message::eMessageError, "",
                                         "Input images intersection is empty. ");
                    return false;
                }
            }
			return true;
		}
        case 2: { //A
			rod = rodA;
			return true;
		}
        case 3: { //B
			rod = rodB;
			return true;
		}
	}
	return false;
}

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

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
MergePlugin::setupAndProcess(MergeProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
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
    std::auto_ptr<const OFX::Image> srcA((_srcClipA && _srcClipA->isConnected()) ?
                                         _srcClipA->fetchImage(args.time) : 0);
    std::auto_ptr<const OFX::Image> srcB((_srcClipB && _srcClipB->isConnected()) ?
                                         _srcClipB->fetchImage(args.time) : 0);
    
    OptionalImagesHolder_RAII optionalImages;
    for (unsigned i = 0; i < _optionalASrcClips.size(); ++i) {
        optionalImages.images.push_back((_optionalASrcClips[i] && _optionalASrcClips[i]->isConnected()) ?
                                        _optionalASrcClips[i]->fetchImage(args.time) : 0);
        const OFX::Image* optImg = optionalImages.images.back();
        if (optImg) {
            if (optImg->getRenderScale().x != args.renderScale.x ||
                optImg->getRenderScale().y != args.renderScale.y ||
                (optImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && optImg->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
            OFX::BitDepthEnum    srcBitDepth      = optImg->getPixelDepth();
            OFX::PixelComponentEnum srcComponents = optImg->getPixelComponents();
            if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
                OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
    }

    if (srcA.get()) {
        if (srcA->getRenderScale().x != args.renderScale.x ||
            srcA->getRenderScale().y != args.renderScale.y ||
            (srcA->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcA->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    if (srcB.get()) {
        if (srcB->getRenderScale().x != args.renderScale.x ||
            srcB->getRenderScale().y != args.renderScale.y ||
            (srcB->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && srcB->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    // auto ptr for the mask.
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    
    // do we do masking
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    int bboxChoice;
    bool alphaMasking;
    _bbox->getValueAtTime(args.time, bboxChoice);
    _alphaMasking->getValueAtTime(args.time, alphaMasking);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(bboxChoice, alphaMasking, mix);
    processor.setDstImg(dst.get());
    processor.setSrcImg(srcA.get(), srcB.get(), optionalImages.images);
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

template<int nComponents>
void
MergePlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            renderForBitDepth<unsigned char, nComponents, 255>(args);
            break;
        }
        case OFX::eBitDepthUShort: {
            renderForBitDepth<unsigned short, nComponents, 65535>(args);
            break;
        }
        case OFX::eBitDepthFloat: {
            renderForBitDepth<float, nComponents, 1>(args);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
MergePlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    int operation_i;
    _operation->getValueAtTime(args.time, operation_i);
    MergingFunctionEnum operation = (MergingFunctionEnum)operation_i;
    std::auto_ptr<MergeProcessorBase> fred;
    switch (operation) {
        case eMergeATop:
            fred.reset(new MergeProcessor<eMergeATop, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeAverage:
            fred.reset(new MergeProcessor<eMergeAverage, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeColorBurn:
            fred.reset(new MergeProcessor<eMergeColorBurn, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeColorDodge:
            fred.reset(new MergeProcessor<eMergeColorDodge, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeConjointOver:
            fred.reset(new MergeProcessor<eMergeConjointOver, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeCopy:
            fred.reset(new MergeProcessor<eMergeCopy, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeDifference:
            fred.reset(new MergeProcessor<eMergeDifference, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeDisjointOver:
            fred.reset(new MergeProcessor<eMergeDisjointOver, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeDivide:
            fred.reset(new MergeProcessor<eMergeDivide, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeExclusion:
            fred.reset(new MergeProcessor<eMergeExclusion, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeFreeze:
            fred.reset(new MergeProcessor<eMergeFreeze, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeFrom:
            fred.reset(new MergeProcessor<eMergeFrom, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeGeometric:
            fred.reset(new MergeProcessor<eMergeGeometric, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeGrainExtract:
            fred.reset(new MergeProcessor<eMergeGrainExtract, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeGrainMerge:
            fred.reset(new MergeProcessor<eMergeGrainMerge, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeHardLight:
            fred.reset(new MergeProcessor<eMergeHardLight, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeHypot:
            fred.reset(new MergeProcessor<eMergeHypot, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeIn:
            fred.reset(new MergeProcessor<eMergeIn, PIX, nComponents, maxValue>(*this));
            break;
        //case eMergeInterpolated:
        //    fred.reset(new MergeProcessor<eMergeInterpolated, PIX, nComponents, maxValue>(*this));
        //    break;
        case eMergeMask:
            fred.reset(new MergeProcessor<eMergeMask, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeMatte:
            fred.reset(new MergeProcessor<eMergeMatte, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeMax:
            fred.reset(new MergeProcessor<eMergeMax, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeMin:
            fred.reset(new MergeProcessor<eMergeMin, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeMinus:
            fred.reset(new MergeProcessor<eMergeMinus, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeMultiply:
            fred.reset(new MergeProcessor<eMergeMultiply, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeOut:
            fred.reset(new MergeProcessor<eMergeOut, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeOver:
            fred.reset(new MergeProcessor<eMergeOver, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeOverlay:
            fred.reset(new MergeProcessor<eMergeOverlay, PIX, nComponents, maxValue>(*this));
            break;
        case eMergePinLight:
            fred.reset(new MergeProcessor<eMergePinLight, PIX, nComponents, maxValue>(*this));
            break;
        case eMergePlus:
            fred.reset(new MergeProcessor<eMergePlus, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeReflect:
            fred.reset(new MergeProcessor<eMergeReflect, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeScreen:
            fred.reset(new MergeProcessor<eMergeScreen, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeSoftLight:
            fred.reset(new MergeProcessor<eMergeSoftLight, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeStencil:
            fred.reset(new MergeProcessor<eMergeStencil, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeUnder:
            fred.reset(new MergeProcessor<eMergeUnder, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeXOR:
            fred.reset(new MergeProcessor<eMergeXOR, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeHue:
            fred.reset(new MergeProcessor<eMergeHue, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeSaturation:
            fred.reset(new MergeProcessor<eMergeSaturation, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeColor:
            fred.reset(new MergeProcessor<eMergeColor, PIX, nComponents, maxValue>(*this));
            break;
        case eMergeLuminosity:
            fred.reset(new MergeProcessor<eMergeLuminosity, PIX, nComponents, maxValue>(*this));
            break;
    } // switch
    assert(fred.get());
    if (fred.get()) {
        setupAndProcess(*fred, args);
    }
}

// the overridden render function
void
MergePlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth());
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args);
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args);
    }
}

void
MergePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    OFX::PixelComponentEnum outputComps = _dstClip->getPixelComponents();
    clipPreferences.setClipComponents(*_srcClipA, outputComps);
    clipPreferences.setClipComponents(*_srcClipB, outputComps);
    for (unsigned i = 0; i < _optionalASrcClips.size(); ++i) {
        clipPreferences.setClipComponents(*_optionalASrcClips[i], outputComps);
    }
}

void
MergePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamOperation) {
        int operation_i;
        _operation->getValueAtTime(args.time, operation_i);
        // depending on the operation, enable/disable alpha masking
        _alphaMasking->setEnabled(MergeImages2D::isMaskable((MergingFunctionEnum)operation_i));
        _operationString->setValue(MergeImages2D::getOperationString((MergingFunctionEnum)operation_i));
    }
}

bool
MergePlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClipB;
        return true;
    }
    

    OfxRectI maskRoD;
    bool maskRoDValid = false;
    
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(args.time)) && _maskClip && _maskClip->isConnected());
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            maskRoDValid = true;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if (!OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0)) {
                identityClip = _srcClipB;
                return true;
            }
        }
    }
    
    // The region of effect is only the set of the intersections between the A inputs and the mask.
    // If at least one of these regions intersects the renderwindow, the effect is not identity.

    std::vector<OFX::Clip *> aClips = _optionalASrcClips;
    aClips.push_back(_srcClipA);
    for (unsigned int i = 0; i < aClips.size(); ++i) {
        if (!aClips[i]->isConnected()) {
            continue;
        }
        OfxRectD srcARoD = aClips[i]->getRegionOfDefinition(args.time);
        if (srcARoD.x2 <= srcARoD.x1 || srcARoD.y2 <= srcARoD.y1) {
            // RoD is empty
            continue;
        }

        OfxRectI srcARoDPixel;
        OFX::Coords::toPixelEnclosing(srcARoD, args.renderScale, aClips[i]->getPixelAspectRatio(), &srcARoDPixel);
        bool srcARoDValid = true;
        if (maskRoDValid) {
            // mask the srcARoD with the mask RoD. The result may be empty
            srcARoDValid = OFX::Coords::rectIntersection<OfxRectI>(srcARoDPixel, maskRoD, &srcARoDPixel);
        }
        if (OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, srcARoDPixel, 0)) {
            // renderWindow intersects one of the effect areas
            return false;
        }
    }

    // renderWindow intersects no area where a "A" source is applied
    identityClip = _srcClipB;
    return true;
}


//mDeclarePluginFactory(MergePluginFactory, {}, {});
template<MergingFunctionEnum plugin>
class MergePluginFactory : public OFX::PluginFactoryHelper<MergePluginFactory<plugin> >
{
public:
    MergePluginFactory<plugin>(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<MergePluginFactory>(id, verMaj, verMin){}
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

template<MergingFunctionEnum plugin>
void MergePluginFactory<plugin>::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    switch (plugin) {
        case eMergeOver:
            desc.setLabel(kPluginName);
            desc.setPluginGrouping(kPluginGrouping);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergePlus:
            desc.setLabel(kPluginNamePlus);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeMatte:
            desc.setLabel(kPluginNameMatte);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeMultiply:
            desc.setLabel(kPluginNameMultiply);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeIn:
            desc.setLabel(kPluginNameIn);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeOut:
            desc.setLabel(kPluginNameOut);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeScreen:
            desc.setLabel(kPluginNameScreen);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeMax:
            desc.setLabel(kPluginNameMax);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeMin:
            desc.setLabel(kPluginNameMin);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
        case eMergeDifference:
            desc.setLabel(kPluginNameAbsminus);
            desc.setPluginGrouping(kPluginGroupingSub);
            desc.setPluginDescription(kPluginDescription);
            break;
    }

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
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
    
}

static void
addMergeOption(ChoiceParamDescriptor* param, MergingFunctionEnum e, const char* help, bool cascading)
{
    assert(param->getNOptions() == e);
    if (cascading) {
        param->appendOption(getOperationGroupString(e) + '/' + getOperationString(e), help);
    } else {
        param->appendOption(getOperationString(e), '(' + getOperationGroupString(e) + ") " + help);
    }
}

template<MergingFunctionEnum plugin>
void
MergePluginFactory<plugin>::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    OFX::ClipDescriptor* srcClipB = desc.defineClip(kClipB);
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClipB->addSupportedComponent( OFX::ePixelComponentXY );
    srcClipB->addSupportedComponent( OFX::ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);
    
    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    srcClipB->setOptional(false); // B clip is non-optional

    OFX::ClipDescriptor* srcClipA = desc.defineClip(kClipA);
    srcClipA->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClipA->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClipA->addSupportedComponent( OFX::ePixelComponentXY );
    srcClipA->addSupportedComponent( OFX::ePixelComponentAlpha );
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(kSupportsTiles);
    
    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    srcClipA->setOptional(true);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    if (numerousInputs) {
        for (int i = 2; i <= kMaximumAInputs; ++i) {
            assert(i < 100);
            char name[4] = { 'A', 0, 0, 0 }; // don't use std::stringstream (not thread-safe on OSX)
            assert(i < 100);
            name[1] = (i < 10) ? ('0' + i) : ('0' + i / 10);
            name[2] = (i < 10) ?         0 : ('0' + i % 10);
            OFX::ClipDescriptor* optionalSrcClip = desc.defineClip(name);
            optionalSrcClip->addSupportedComponent( OFX::ePixelComponentRGBA );
            optionalSrcClip->addSupportedComponent( OFX::ePixelComponentRGB );
            optionalSrcClip->addSupportedComponent( OFX::ePixelComponentXY );
            optionalSrcClip->addSupportedComponent( OFX::ePixelComponentAlpha );
            optionalSrcClip->setTemporalClipAccess(false);
            optionalSrcClip->setSupportsTiles(kSupportsTiles);
            
            //Optional: If we want a render to be triggered even if one of the inputs is not connected
            //they need to be optional.
            optionalSrcClip->setOptional(true);
        }
    }

    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // operationString
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecret(true); // always secret
        param->setEnabled(false);
        param->setIsPersistant(true);
        param->setEvaluateOnChange(false);
        param->setDefault(getOperationString(plugin));
        if (page) {
            page->addChild(*param);
        }
    }

    // operation
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOperation);
        param->setLabel(kParamOperationLabel);
        param->setHint(kParamOperationHint);
        bool cascading = false;// OFX::getImageEffectHostDescription()->supportsCascadingChoices;
        param->setCascading(cascading);
        addMergeOption(param, eMergeATop, "Ab + B(1 - a)", cascading);
        addMergeOption(param, eMergeAverage, "(A + B) / 2", cascading);
        addMergeOption(param, eMergeColor, "SetLum(A, Lum(B))", cascading);
        addMergeOption(param, eMergeColorBurn, "darken B towards A", cascading);
        addMergeOption(param, eMergeColorDodge, "brighten B towards A", cascading);
        addMergeOption(param, eMergeConjointOver, "A + B(1-a)/b, A if a > b", cascading);
        addMergeOption(param, eMergeCopy, "A", cascading);
        addMergeOption(param, eMergeDifference, "abs(A-B) (a.k.a. absminus)", cascading);
        addMergeOption(param, eMergeDisjointOver, "A+B(1-a)/b, A+B if a+b < 1", cascading);
        addMergeOption(param, eMergeDivide, "A/B, 0 if A < 0 and B < 0", cascading);
        addMergeOption(param, eMergeExclusion, "A+B-2AB", cascading);
        addMergeOption(param, eMergeFreeze, "1-sqrt(1-A)/B", cascading);
        addMergeOption(param, eMergeFrom, "B-A", cascading);
        addMergeOption(param, eMergeGeometric, "2AB/(A+B)", cascading);
        addMergeOption(param, eMergeGrainExtract, "B - A + 0.5", cascading);
        addMergeOption(param, eMergeGrainMerge, "B + A - 0.5", cascading);
        addMergeOption(param, eMergeHardLight, "multiply if A < 0.5, screen if A > 0.5", cascading);
        addMergeOption(param, eMergeHue, "SetLum(SetSat(A, Sat(B)), Lum(B))", cascading);
        addMergeOption(param, eMergeHypot, "sqrt(A*A+B*B)", cascading);
        addMergeOption(param, eMergeIn, "Ab", cascading);
        //addMergeOption(param, eMergeInterpolated, "(like average but better and slower)", cascading);
        addMergeOption(param, eMergeLuminosity, "SetLum(B, Lum(A))", cascading);
        addMergeOption(param, eMergeMask, "Ba", cascading);
        addMergeOption(param, eMergeMatte, "Aa + B(1-a) (unpremultiplied over)", cascading);
        addMergeOption(param, eMergeMax, "max(A, B) (a.k.a. lighten)", cascading);
        addMergeOption(param, eMergeMin, "min(A, B) (a.k.a. darken)", cascading);
        addMergeOption(param, eMergeMinus, "A-B", cascading);
        addMergeOption(param, eMergeMultiply, "AB, 0 if A < 0 and B < 0", cascading);
        addMergeOption(param, eMergeOut, "A(1-b)", cascading);
        addMergeOption(param, eMergeOver, "A+B(1-a)", cascading);
        addMergeOption(param, eMergeOverlay, "multiply if B<0.5, screen if B>0.5", cascading);
        addMergeOption(param, eMergePinLight, "if B >= 0.5 then max(A, 2*B - 1), min(A, B * 2.0 ) else", cascading);
        addMergeOption(param, eMergePlus, "A+B", cascading);
        addMergeOption(param, eMergeReflect, "A*A / (1 - B)", cascading);
        addMergeOption(param, eMergeSaturation, "SetLum(SetSat(B, Sat(A)), Lum(B))", cascading);
        addMergeOption(param, eMergeScreen, "A+B-AB", cascading);
        addMergeOption(param, eMergeSoftLight, "burn-in if A < 0.5, lighten if A > 0.5", cascading);
        addMergeOption(param, eMergeStencil, "B(1-a)", cascading);
        addMergeOption(param, eMergeUnder, "A(1-b)+B", cascading);
        addMergeOption(param, eMergeXOR, "A(1-b)+B(1-a)", cascading);
        param->setDefault(plugin);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }

    // boundingBox
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBbox);
        param->setLabel(kParamBboxLabel);
        param->setHint(kParamBboxHint);
        param->appendOption("Union");
        param->appendOption("Intersection");
        param->appendOption("A");
        param->appendOption("B");
        param->setAnimates(true);
        param->setDefault(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // alphaMasking
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAlphaMasking);
        param->setLabel(kParamAlphaMaskingLabel);
        param->setAnimates(true);
        param->setDefault(false);
        param->setEnabled(MergeImages2D::isMaskable(eMergeOver));
        param->setHint(kParamAlphaMaskingHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
}

template<MergingFunctionEnum plugin>
OFX::ImageEffect*
MergePluginFactory<plugin>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new MergePlugin(handle, numerousInputs);
}

void getMergePluginID(OFX::PluginFactoryArray &ids)
{
    {
        static MergePluginFactory<eMergeOver> p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergePlus> p(std::string(kPluginIdentifierSub) + "Plus", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeMatte> p(std::string(kPluginIdentifierSub) + "Matte", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeMultiply> p(std::string(kPluginIdentifierSub) + "Multiply", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeIn> p(std::string(kPluginIdentifierSub) + "In", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeOut> p(std::string(kPluginIdentifierSub) + "Out", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeScreen> p(std::string(kPluginIdentifierSub) + "Screen", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeMax> p(std::string(kPluginIdentifierSub) + "Max", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeMin> p(std::string(kPluginIdentifierSub) + "Min", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static MergePluginFactory<eMergeDifference> p(std::string(kPluginIdentifierSub) + "Difference", kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}

