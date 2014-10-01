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
#include "ofxsMaskMix.h"

#include "ofxNatron.h"
#include "ofxsMacros.h"

#define kPluginName "MergeOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Pixel-by-pixel merge operation between the two inputs."
#define kPluginIdentifier "net.sf.openfx.MergePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint \
"The operation used to merge the input A and B images.\n" \
"The operator formula is applied to each component: A and B represent the input component (Red, Green, Blue, or Alpha) of each input, and a and b represent the Alpha component of each input.\n" \
"If Alpha masking is checked, the output alpha is computed using a different formula (a+b - a*b)"

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

using namespace OFX;
using namespace MergeImages2D;

class MergeProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImgA;
    const OFX::Image *_srcImgB;
    const OFX::Image *_maskImg;
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
    
    void setSrcImg(const OFX::Image *A, const OFX::Image *B) {_srcImgA = A; _srcImgB = B;}
    
    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }
    
    void doMasking(bool v) {_doMasking = v;}

    void setValues(MergingFunctionEnum operation,
                   int bboxChoice,
                   bool alphaMasking,
                   double mix)
    {
        _operation = operation;
        _bbox = bboxChoice;
        _alphaMasking = MergeImages2D::isMaskable(operation) ? alphaMasking : false;
        _mix = mix;
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
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                
                const PIX *srcPixA = (const PIX *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);

                if (srcPixA || srcPixB) {

                    for (int c = 0; c < nComponents; ++c) {
                        // all images are supposed to be black and transparent outside o
                        tmpA[c] = srcPixA ? ((float)srcPixA[c] / (float)maxValue) : 0.;
                        tmpB[c] = srcPixB ? ((float)srcPixB[c] / (float)maxValue) : 0.;
                    }
                    // work in float: clamping is done when mixing
                    mergePixel<float, nComponents, 1>(_operation, _alphaMasking, tmpA, tmpB, tmpPix);
                    // denormalize
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] *= maxValue;
                    }
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
        srcClipA_ = fetchClip(kClipA);
        assert(srcClipA_ && (srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA || srcClipA_->getPixelComponents() == ePixelComponentAlpha));
        srcClipB_ = fetchClip(kClipB);
        assert(srcClipB_ && (srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA || srcClipB_->getPixelComponents() == ePixelComponentAlpha));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _operation = fetchChoiceParam(kParamOperation);
        _operationString = fetchStringParam(kOfxParamStringSublabelName);
        _bbox = fetchChoiceParam(kParamBbox);
        _alphaMasking = fetchBooleanParam(kParamAlphaMasking);
        assert(_operation && _operationString && _bbox && _alphaMasking);
        _mix = fetchDoubleParam(kParamMix);
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
            rectBoundingBox(rodA, rodB, &rod);
			return true;
		}
		case 1: //intersection
		{
            bool interesect = rectIntersection(rodA, rodB, &rod);
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
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    int operation;
    int bboxChoice;
    bool alphaMasking;
    _operation->getValueAtTime(args.time, operation);
    _bbox->getValueAtTime(args.time, bboxChoice);
    _alphaMasking->getValueAtTime(args.time, alphaMasking);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues((MergingFunctionEnum)operation, bboxChoice, alphaMasking, mix);
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
    desc.setRenderThreadSafety(OFX::eRenderFullySafe);
    
}


void MergePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    OFX::ClipDescriptor* srcClipB = desc.defineClip(kClipB);
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGBA );
    srcClipB->addSupportedComponent( OFX::ePixelComponentRGB );
    srcClipB->addSupportedComponent( OFX::ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(true);
    srcClipB->setOptional(false);

    OFX::ClipDescriptor* srcClipA = desc.defineClip(kClipA);
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

    // operationString
    {
        StringParamDescriptor* param = desc.defineStringParam(kOfxParamStringSublabelName);
        param->setIsSecret(true);
        param->setEnabled(false);
        param->setIsPersistant(true);
        param->setEvaluateOnChange(false);
        param->setDefault(getOperationString(eMergeOver));
        page->addChild(*param);
    }

    // operation
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOperation);
        param->setLabels(kParamOperationLabel, kParamOperationLabel, kParamOperationLabel);
        param->setHint(kParamOperationHint);
        assert(param->getNOptions() == eMergeATop);
        param->appendOption( "atop", "Ab + B(1 - a)" );
        assert(param->getNOptions() == eMergeAverage);
        param->appendOption( "average", "(A + B) / 2" );
        assert(param->getNOptions() == eMergeColorBurn);
        param->appendOption( "color-burn", "darken B towards A" );
        assert(param->getNOptions() == eMergeColorDodge);
        param->appendOption( "color-dodge", "brighten B towards A" );
        assert(param->getNOptions() == eMergeConjointOver);
        param->appendOption( "conjoint-over", "A + B(1-a)/b, A if a > b" );
        assert(param->getNOptions() == eMergeCopy);
        param->appendOption( "copy", "A" );
        assert(param->getNOptions() == eMergeDifference);
        param->appendOption( "difference", "abs(A-B)" );
        assert(param->getNOptions() == eMergeDisjointOver);
        param->appendOption( "disjoint-over", "A+B(1-a)/b, A+B if a+b < 1" );
        assert(param->getNOptions() == eMergeDivide);
        param->appendOption( "divide", "A/B, 0 if A < 0 and B < 0" );
        assert(param->getNOptions() == eMergeExclusion);
        param->appendOption( "exclusion", "A+B-2AB" );
        assert(param->getNOptions() == eMergeFreeze);
        param->appendOption( "freeze", "1-sqrt(1-A)/B" );
        assert(param->getNOptions() == eMergeFrom);
        param->appendOption( "from", "B-A" );
        assert(param->getNOptions() == eMergeGeometric);
        param->appendOption( "geometric", "2AB/(A+B)" );
        assert(param->getNOptions() == eMergeHardLight);
        param->appendOption( "hard-light", "multiply if A < 0.5, screen if A > 0.5" );
        assert(param->getNOptions() == eMergeHypot);
        param->appendOption( "hypot", "sqrt(A*A+B*B)" );
        assert(param->getNOptions() == eMergeIn);
        param->appendOption( "in", "Ab" );
        assert(param->getNOptions() == eMergeInterpolated);
        param->appendOption( "interpolated", "(like average but better and slower)" );
        assert(param->getNOptions() == eMergeMask);
        param->appendOption( "mask", "Ba" );
        assert(param->getNOptions() == eMergeMatte);
        param->appendOption( "matte", "Aa + B(1-a) (unpremultiplied over)" );
        assert(param->getNOptions() == eMergeLighten);
        param->appendOption( "max", "max(A, B)" );
        assert(param->getNOptions() == eMergeDarken);
        param->appendOption( "min", "min(A, B)" );
        assert(param->getNOptions() == eMergeMinus);
        param->appendOption( "minus", "A-B" );
        assert(param->getNOptions() == eMergeMultiply);
        param->appendOption( "multiply", "AB, 0 if A < 0 and B < 0" );
        assert(param->getNOptions() == eMergeOut);
        param->appendOption( "out", "A(1-b)" );
        assert(param->getNOptions() == eMergeOver);
        param->appendOption( "over", "A+B(1-a)" );
        assert(param->getNOptions() == eMergeOverlay);
        param->appendOption( "overlay", "multiply if B<0.5, screen if B>0.5" );
        assert(param->getNOptions() == eMergePinLight);
        param->appendOption( "pinlight", "if B >= 0.5 then max(A, 2*B - 1), min(A, B * 2.0 ) else" );
        assert(param->getNOptions() == eMergePlus);
        param->appendOption( "plus", "A+B" );
        assert(param->getNOptions() == eMergeReflect);
        param->appendOption( "reflect", "A*A / (1 - B)" );
        assert(param->getNOptions() == eMergeScreen);
        param->appendOption( "screen", "A+B-AB" );
        assert(param->getNOptions() == eMergeSoftLight);
        param->appendOption( "soft-light", "burn-in if A < 0.5, lighten if A > 0.5" );
        assert(param->getNOptions() == eMergeStencil);
        param->appendOption( "stencil", "B(1-a)" );
        assert(param->getNOptions() == eMergeUnder);
        param->appendOption( "under", "A(1-b)+B" );
        assert(param->getNOptions() == eMergeXOR);
        param->appendOption( "xor", "A(1-b)+B(1-a)" );
        param->setDefault(eMergeOver);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        page->addChild(*param);
    }

    // boundingBox
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBbox);
        param->setLabels(kParamBboxLabel, kParamBboxLabel, kParamBboxLabel);
        param->setHint(kParamBboxHint);
        param->appendOption("Union");
        param->appendOption("Intersection");
        param->appendOption("A");
        param->appendOption("B");
        param->setAnimates(true);
        param->setDefault(0);
        page->addChild(*param);
    }

    // alphaMasking
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAlphaMasking);
        param->setLabels(kParamAlphaMaskingLabel, kParamAlphaMaskingLabel, kParamAlphaMaskingLabel);
        param->setAnimates(true);
        param->setDefault(false);
        param->setEnabled(MergeImages2D::isMaskable(eMergeOver));
        param->setHint(kParamAlphaMaskingHint);
        page->addChild(*param);
    }

    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* MergePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new MergePlugin(handle);
}

void getMergePluginID(OFX::PluginFactoryArray &ids)
{
    static MergePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

