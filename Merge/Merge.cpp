/*
 OFX Merge plugin.
 
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

#include "Merge.h"

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsFilter.h"

#include "ofxNatron.h"

#define kPluginName "MergeOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Pixel-by-pixel merge operation between the two inputs."
#define kPluginIdentifier "net.sf.openfx:MergePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kOperationParamName "operation"
#define kOperationParamLabel "Operation"
#define kOperationParamHint "The operation used to merge the input A and B images."
#define kAlphaMaskingParamName "screenAlpha"
#define kAlphaMaskingParamLabel "Alpha masking"
#define kAlphaMaskingParamHint "When enabled, the input images are unchanged where the other image has 0 alpha, and" \
    " the output alpha is set to a+b - a*b. When disabled the alpha channel is processed as " \
    "any other channel. Option is disabled for operations where it does not apply or makes no difference."
#define kBboxParamName "bbox"
#define kBboxParamLabel "Bounding Box"
#define kBboxParamHint "What to use to produce the output image's bounding box."
#define kSourceClipAName "A"
#define kSourceClipBName "B"

using namespace OFX;
using namespace MergeImages2D;

class MergeProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImgA;
    OFX::Image *_srcImgB;
    OFX::Image *_maskImg;
    bool   _doMasking;
    MergingFunctionEnum _operation;
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
    , _operation(eMergePlus)
    , _bbox(0)
    , _alphaMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
        
    }
    
    void setSrcImg(OFX::Image *A, OFX::Image *B) {_srcImgA = A; _srcImgB = B;}
    
    void setMaskImg(OFX::Image *v) {_maskImg = v;}
    
    void doMasking(bool v) {_doMasking = v;}

    void setValues(MergingFunctionEnum operation, int bboxChoice, bool alphaMasking, double mix, bool maskInvert)
    {
        _operation = operation;
        _bbox = bboxChoice;
        _alphaMasking = MergeImages2D::isMaskable(operation) ? alphaMasking : false;
        _mix = mix;
        _maskInvert = maskInvert;
    }
    
};



template <class PIX, int nComponents, int maxValue>
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
        float tmpPix[nComponents];
        float tmpA[nComponents];
        float tmpB[nComponents];
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                PIX *srcPixA = (PIX *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                PIX *srcPixB = (PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);

                if (srcPixA || srcPixB) {

                    for (int c = 0; c < nComponents; ++c) {
                        // all images are supposed to be black and transparent outside o
                        tmpA[c] = srcPixA ? ((float)srcPixA[c] / (float)maxValue) : 0.;
                        tmpB[c] = srcPixB ? ((float)srcPixB[c] / (float)maxValue) : 0.;
                    }

                    mergePixel<float, nComponents, maxValue>(_operation,_alphaMasking, tmpA, tmpB, tmpPix);
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, _mix, _maskInvert, dstPix);
                } else {
                    // everything is black and transparent
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = 0;
                    }
                }
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
    MergePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClipA_(0)
    , srcClipB_(0)
    , maskClip_(0)
    
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha));
        srcClipA_ = fetchClip(kSourceClipAName);
        assert(srcClipA_ && (srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA || srcClipA_->getPixelComponents() == ePixelComponentAlpha));
        srcClipB_ = fetchClip(kSourceClipBName);
        assert(srcClipB_ && (srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA || srcClipB_->getPixelComponents() == ePixelComponentAlpha));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _operation = fetchChoiceParam(kOperationParamName);
        _operationString = fetchStringParam(kOfxParamStringSublabelName);
        _bbox = fetchChoiceParam(kBboxParamName);
        _alphaMasking = fetchBooleanParam(kAlphaMaskingParamName);
        _mix = fetchDoubleParam(kFilterMixParamName);
        _maskInvert = fetchBooleanParam(kFilterMaskInvertParamName);
        assert(_operation && _operationString && _bbox && _alphaMasking && _mix && _maskInvert);
    }
    
private:
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName);

    /* set up and run a processor */
    void setupAndProcess(MergeProcessorBase &, const OFX::RenderArguments &args);

    bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime);
    
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClipA_;
    OFX::Clip *srcClipB_;
    OFX::Clip *maskClip_;
    
    OFX::ChoiceParam *_operation;
    OFX::StringParam *_operationString;
    OFX::ChoiceParam *_bbox;
    OFX::BooleanParam *_alphaMasking;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


// override the rod call
bool
MergePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    //OfxRectD srcRodA = translateRegion( _clipSrcA->getCanonicalRod( args.time ), params._offsetA );
	//OfxRectD srcRodB = translateRegion( _clipSrcB->getCanonicalRod( args.time ), params._offsetB );
    if (!srcClipA_->isConnected() && !srcClipB_->isConnected()) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    
    OfxRectD rodA = srcClipA_->getRegionOfDefinition(args.time);
    OfxRectD rodB = srcClipB_->getRegionOfDefinition(args.time);
    
    int bboxChoice;
    _bbox->getValueAtTime(args.time, bboxChoice);
    
	switch (bboxChoice)
	{
		case 0: //union
		{
            rectanglesBoundingBox(rodA, rodB, &rod);
			return true;
		}
		case 1: //intersection
		{
            bool interesect = rectangleIntersect(rodA, rodB, &rod);
            if (!interesect) {
                setPersistentMessage(OFX::Message::eMessageError, "", "The bounding boxes of the 2 images don't intersect.");
                return false;
            }
			return true;
		}
		case 2: //A
		{
			rod = rodA;
			return true;
		}
		case 3: //B
		{
			rod = rodB;
			return true;
		}
	}
	return false;
}


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
MergePlugin::setupAndProcess(MergeProcessorBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> srcA(srcClipA_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> srcB(srcClipB_->fetchImage(args.time));
    if (srcA.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    if (srcB.get()) {
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    
    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((getContext() != OFX::eContextFilter) ? maskClip_->fetchImage(args.time) : 0);
    
    // do we do masking
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get());
    }

    int operation;
    int bboxChoice;
    bool alphaMasking;
    _operation->getValueAtTime(args.time, operation);
    _bbox->getValueAtTime(args.time, bboxChoice);
    _alphaMasking->getValueAtTime(args.time, alphaMasking);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(args.time, maskInvert);
    processor.setValues((MergingFunctionEnum)operation, bboxChoice, alphaMasking, mix, maskInvert);
    processor.setDstImg(dst.get());
    processor.setSrcImg(srcA.get(),srcB.get());
    processor.setRenderWindow(args.renderWindow);
   
    processor.process();
}

// the overridden render function
void
MergePlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();
    
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentAlpha);
    if (dstComponents == OFX::ePixelComponentRGBA)
    {
        switch (dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                MergeProcessor<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                MergeProcessor<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                MergeProcessor<float,4,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else if (dstComponents == OFX::ePixelComponentRGB)
    {
        switch (dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                MergeProcessor<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                MergeProcessor<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                MergeProcessor<float,3,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
    else
    {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth)
        {
            case OFX::eBitDepthUByte :
            {
                MergeProcessor<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort :
            {
                MergeProcessor<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat :
            {
                MergeProcessor<float,1,1> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


void
MergePlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kOperationParamName) {
        int operation_i;
        _operation->getValueAtTime(args.time, operation_i);
        // depending on the operation, enable/disable alpha masking
        _alphaMasking->setEnabled(MergeImages2D::isMaskable((MergingFunctionEnum)operation_i));
        _operationString->setValue(MergeImages2D::getOperationString((MergingFunctionEnum)operation_i));
    }
}

bool
MergePlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = srcClipB_;
        return true;
    } else {
        return false;
    }
}


mDeclarePluginFactory(MergePluginFactory, {}, {});

void MergePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
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


void MergePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    OFX::ClipDescriptor* srcClipB = desc.defineClip(kSourceClipBName);
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClipB->addSupportedComponent( OFX::ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(true);
    srcClipB->setOptional(false);

    OFX::ClipDescriptor* srcClipA = desc.defineClip(kSourceClipAName);
    srcClipA->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClipA->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClipA->addSupportedComponent( OFX::ePixelComponentAlpha );
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(true);
    srcClipA->setOptional(false);

    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);
    
    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    StringParamDescriptor* operationString = desc.defineStringParam(kOfxParamStringSublabelName);
    operationString->setIsSecret(true);
    operationString->setEnabled(false);
    operationString->setIsPersistant(true);
    operationString->setEvaluateOnChange(false);
    operationString->setDefault(getOperationString(eMergeOver));
    page->addChild(*operationString);
 
    ChoiceParamDescriptor* operation = desc.defineChoiceParam(kOperationParamName);
    operation->setLabels(kOperationParamLabel, kOperationParamLabel, kOperationParamLabel);
    operation->setHint(kOperationParamHint);
    assert(operation->getNOptions() == eMergeATop);
    operation->appendOption( "atop", "Ab + B(1 - a)" );
    assert(operation->getNOptions() == eMergeAverage);
    operation->appendOption( "average", "(A + B) / 2" );
    assert(operation->getNOptions() == eMergeColorBurn);
    operation->appendOption( "color-burn", "darken B towards A" );
    assert(operation->getNOptions() == eMergeColorDodge);
    operation->appendOption( "color-dodge", "brighten B towards A" );
    assert(operation->getNOptions() == eMergeConjointOver);
    operation->appendOption( "conjoint-over", "A + B(1-a)/b, A if a > b" );
    assert(operation->getNOptions() == eMergeCopy);
    operation->appendOption( "copy", "A" );
    assert(operation->getNOptions() == eMergeDifference);
    operation->appendOption( "difference", "abs(A-B)" );
    assert(operation->getNOptions() == eMergeDisjointOver);
    operation->appendOption( "disjoint-over", "A+B(1-a)/b, A+B if a+b < 1" );
    assert(operation->getNOptions() == eMergeDivide);
    operation->appendOption( "divide", "A/B, 0 if A < 0 and B < 0" );
    assert(operation->getNOptions() == eMergeExclusion);
    operation->appendOption( "exclusion", "A+B-2AB" );
    assert(operation->getNOptions() == eMergeFreeze);
    operation->appendOption( "freeze", "1-sqrt(1-A)/B" );
    assert(operation->getNOptions() == eMergeFrom);
    operation->appendOption( "from", "B-A" );
    assert(operation->getNOptions() == eMergeGeometric);
    operation->appendOption( "geometric", "2AB/(A+B)" );
    assert(operation->getNOptions() == eMergeHardLight);
    operation->appendOption( "hard-light", "multiply if A < 0.5, screen if A > 0.5" );
    assert(operation->getNOptions() == eMergeHypot);
    operation->appendOption( "hypot", "sqrt(A*A+B*B)" );
    assert(operation->getNOptions() == eMergeIn);
    operation->appendOption( "in", "Ab" );
    assert(operation->getNOptions() == eMergeInterpolated);
    operation->appendOption( "interpolated", "(like average but better and slower)" );
    assert(operation->getNOptions() == eMergeMask);
    operation->appendOption( "mask", "Ba" );
    assert(operation->getNOptions() == eMergeMatte);
    operation->appendOption( "matte", "Aa + B(1-a) (unpremultiplied over)" );
    assert(operation->getNOptions() == eMergeLighten);
    operation->appendOption( "max", "max(A, B)" );
    assert(operation->getNOptions() == eMergeDarken);
    operation->appendOption( "min", "min(A, B)" );
    assert(operation->getNOptions() == eMergeMinus);
    operation->appendOption( "minus", "A-B" );
    assert(operation->getNOptions() == eMergeMultiply);
    operation->appendOption( "multiply", "AB, 0 if A < 0 and B < 0" );
    assert(operation->getNOptions() == eMergeOut);
    operation->appendOption( "out", "A(1-b)" );
    assert(operation->getNOptions() == eMergeOver);
    operation->appendOption( "over", "A+B(1-a)" );
    assert(operation->getNOptions() == eMergeOverlay);
    operation->appendOption( "overlay", ": multiply if B<0.5, screen if B>0.5" );
    assert(operation->getNOptions() == eMergePinLight);
    operation->appendOption( "pinlight", "if B >= 0.5 then max(A, 2*B - 1), min(A, B * 2.0 ) else" );
    assert(operation->getNOptions() == eMergePlus);
    operation->appendOption( "plus", "A+B" );
    assert(operation->getNOptions() == eMergeReflect);
    operation->appendOption( "reflect", "A*A / (1 - B)" );
    assert(operation->getNOptions() == eMergeScreen);
    operation->appendOption( "screen", "A+B-AB" );
    assert(operation->getNOptions() == eMergeSoftLight);
    operation->appendOption( "soft-light", "burn-in if A < 0.5, lighten if A > 0.5" );
    assert(operation->getNOptions() == eMergeStencil);
    operation->appendOption( "stencil", "B(1-a)" );
    assert(operation->getNOptions() == eMergeUnder);
    operation->appendOption( "under", "A(1-b)+B" );
    assert(operation->getNOptions() == eMergeXOR);
    operation->appendOption( "xor", ": A(1-b)+B(1-a)" );
    operation->setDefault(eMergeOver);
    operation->setAnimates(true);
    operation->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*operation);
    
    
    ChoiceParamDescriptor* boundingBox = desc.defineChoiceParam(kBboxParamName);
    boundingBox->setLabels(kBboxParamLabel, kBboxParamLabel, kBboxParamLabel);
    boundingBox->setHint(kBboxParamHint);
    boundingBox->appendOption("Union");
    boundingBox->appendOption("Intersection");
    boundingBox->appendOption("A");
    boundingBox->appendOption("B");
    boundingBox->setAnimates(true);
    boundingBox->setDefault(0);
    page->addChild(*boundingBox);
    
    BooleanParamDescriptor* alphaMasking = desc.defineBooleanParam(kAlphaMaskingParamName);
    alphaMasking->setLabels(kAlphaMaskingParamLabel, kAlphaMaskingParamLabel, kAlphaMaskingParamLabel);
    alphaMasking->setAnimates(true);
    alphaMasking->setDefault(false);
    alphaMasking->setEnabled(MergeImages2D::isMaskable(eMergeOver));
    alphaMasking->setHint(kAlphaMaskingParamHint);
    page->addChild(*alphaMasking);
    
    ofxsFilterDescribeParamsMaskMix(desc, page);
 
}

OFX::ImageEffect* MergePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new MergePlugin(handle);
}

void getMergePluginID(OFX::PluginFactoryArray &ids)
{
    static MergePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

