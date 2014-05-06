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

#include "../include/ofxsProcessing.H"
#include "../Misc/ofxsMerging.h"
#include "../Misc/ofxsFilter.h"

#define kOperationParamName "Operation"
#define kAlphaMaskingParamName "Alpha masking"
#define kBboxParamName "Bounding Box"
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
    MergingFunction _operation;
    int _bbox;
    double _mix;
    bool _alphaMasking;

public:
    
    MergeProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImgA(0)
    , _srcImgB(0)
    , _maskImg(0)
    , _doMasking(false)
    , _operation(Merge_Plus)
    , _bbox(0)
    , _mix(0)
    , _alphaMasking(false)
    {
        
    }
    
    void setSrcImg(OFX::Image *A,OFX::Image *B) {_srcImgA = A; _srcImgB = B;}
    
    void setMaskImg(OFX::Image *v) {_maskImg = v;}
    
    void doMasking(bool v) {_doMasking = v;}

    void setValues(MergingFunction operation,int bboxChoice,double mix,bool alphaMasking)
    {
        _operation = operation;
        _bbox = bboxChoice;
        _mix = mix;
        _alphaMasking = alphaMasking;
    }
    
};



template <class PIX, int nComponents, int maxValue>
class MergeProcessor : public MergeProcessorBase
{
public :
    MergeProcessor(OFX::ImageEffect &instance)
    : MergeProcessorBase(instance)
    {
        
    }
    
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
                
                if (srcPixA && srcPixB) {
                    for (int c = 0; c < nComponents; ++c) {
                        tmpA[c] = (float)srcPixA[c] / (float)maxValue;
                        tmpB[c] = (float)srcPixB[c] / (float)maxValue;
                    }
                    mergePixel<float, nComponents, maxValue>(_operation,_alphaMasking, tmpA, tmpB, tmpPix);
                    ofxsMaskMix<PIX, nComponents, maxValue, true>(tmpPix, x, y, _srcImgB, _doMasking, _maskImg, _mix, dstPix);
                } else if (srcPixA && !srcPixB) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = srcPixA[c];
                    }
                } else if (srcPixB && !srcPixA) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = srcPixB[c];
                    }
                } else {
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
public :
    /** @brief ctor */
    MergePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClipA_(0)
    , srcClipB_(0)
    , maskClip_(0)
    
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA || dstClip_->getPixelComponents() == ePixelComponentAlpha);
        srcClipA_ = fetchClip(kSourceClipAName);
        assert(srcClipA_->getPixelComponents() == ePixelComponentRGB || srcClipA_->getPixelComponents() == ePixelComponentRGBA || srcClipA_->getPixelComponents() == ePixelComponentAlpha);
        srcClipB_ = fetchClip(kSourceClipBName);
        assert(srcClipB_->getPixelComponents() == ePixelComponentRGB || srcClipB_->getPixelComponents() == ePixelComponentRGBA || srcClipB_->getPixelComponents() == ePixelComponentAlpha);
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
        _operation = fetchChoiceParam(kOperationParamName);
        _bbox = fetchChoiceParam(kBboxParamName);
        _doMask = fetchBooleanParam(kFilterMaskParamName);
        _mix = fetchDoubleParam(kFilterMixParamName);
        _alphaMasking = fetchBooleanParam(kAlphaMaskingParamName);
    }
    
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod);

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    /* set up and run a processor */
    void setupAndProcess(MergeProcessorBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClipA_;
    OFX::Clip *srcClipB_;
    OFX::Clip *maskClip_;
    
    OFX::ChoiceParam *_operation;
    OFX::ChoiceParam *_bbox;
    OFX::BooleanParam *_doMask;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam *_alphaMasking;
};


// override the rod call
bool
MergePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    //OfxRectD srcRodA = translateRegion( _clipSrcA->getCanonicalRod( args.time ), params._offsetA );
	//OfxRectD srcRodB = translateRegion( _clipSrcB->getCanonicalRod( args.time ), params._offsetB );
    OfxRectD rodA = srcClipA_->getRegionOfDefinition(args.time);
    OfxRectD rodB = srcClipB_->getRegionOfDefinition(args.time);
    
    int bboxChoice;
    _bbox->getValue(bboxChoice);
    
	switch(bboxChoice)
	{
		case 0: //union
		{
            rod = rectanglesBoundingBox(rodA,rodB);
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
    if(srcA.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    if(srcB.get())
    {
        OFX::BitDepthEnum    srcBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if(srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            throw int(1);
    }
    
    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((getContext() != OFX::eContextFilter) ? maskClip_->fetchImage(args.time) : 0);
    
    // do we do masking
    if (getContext() != OFX::eContextFilter) {
        bool doMasking;
        _doMask->getValue(doMasking);
        if (doMasking) {
            // say we are masking
            processor.doMasking(true);
            
            // Set it in the processor
            processor.setMaskImg(mask.get());
        }
    }
    
    int operation;
    int bboxChoice;
    double mix;
    bool alphaMasking;
    _alphaMasking->getValue(alphaMasking);
    _operation->getValue(operation);
    _bbox->getValue(bboxChoice);
    _mix->getValueAtTime(args.time, mix);
    processor.setValues((MergingFunction)operation, bboxChoice, mix,alphaMasking);
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
    if(dstComponents == OFX::ePixelComponentRGBA)
    {
        switch(dstBitDepth)
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
        switch(dstBitDepth)
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
        switch(dstBitDepth)
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


void MergePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("MergeOFX", "MergeOFX", "MergeOFX");
    desc.setPluginGrouping("Merge");
    desc.setPluginDescription("Merges 2 images together");
    
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
        if(context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true);
    }
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
 
    ChoiceParamDescriptor* operation = desc.defineChoiceParam(kOperationParamName);
    operation->setLabels(kOperationParamName, kOperationParamName, kOperationParamName);
    operation->setScriptName(kOperationParamName);
    operation->setHint("The operation used to merge the input A and B images.");
    operation->appendOption( "atop", "atop: Ab + B(1 - a)" );
	operation->appendOption( "average", "average: (A + B) / 2" );
	operation->appendOption( "color-burn", "color-burn: darken B towards A" );
	operation->appendOption( "color-dodge", "color-dodge: brighten B towards A" );
	operation->appendOption( "conjoint-over", "conjoint-over: A + B(1-a)/b, A if a > b" );
	operation->appendOption( "copy", "copy: A" );
	operation->appendOption( "difference", "difference: abs(A-B)" );
	operation->appendOption( "disjoint-over", "disjoint-over: A+B(1-a)/b, A+B if a+b < 1" );
	operation->appendOption( "divide", "divide: A/B, 0 if A < 0 and B < 0" );
	operation->appendOption( "exclusion", "exclusion: A+B-2AB" );
	operation->appendOption( "freeze", "freeze: 1-sqrt(1-A)/B" );
	operation->appendOption( "from", "from: B-A" );
	operation->appendOption( "geometric", "geometric: 2AB/(A+B)" );
	operation->appendOption( "hard-light", "hard-light: multiply if A < 0.5, screen if A > 0.5" );
	operation->appendOption( "hypot", "hypot: sqrt(A*A+B*B)" );
	operation->appendOption( "in", "in: Ab" );
	operation->appendOption( "interpolated", "interpolated: (like average but better and slower)" );
	operation->appendOption( "mask", "mask: Ba" );
	operation->appendOption( "matte", "matte: Aa + B(1-a) (unpremultiplied over)" );
	operation->appendOption( "lighten", "lighten: max(A, B)" );
	operation->appendOption( "darken", "darken: min(A, B)" );
	operation->appendOption( "minus", "minus: A-B" );
	operation->appendOption( "multiply", "multiply: AB, 0 if A < 0 and B < 0" );
	operation->appendOption( "out", "out: A(1-b)" );
	operation->appendOption( "over", "over: A+B(1-a)" );
	operation->appendOption( "overlay", "overlay: multiply if B<0.5, screen if B>0.5" );
	operation->appendOption( "pinlight", "pinlight: if B >= 0.5 then max(A, 2*B - 1), min(A, B * 2.0 ) else" );
	operation->appendOption( "plus", "plus: A+B" );
	operation->appendOption( "reflect", "reflect: a² / (1 - b)" );
	operation->appendOption( "screen", "screen: A+B-AB" );
	operation->appendOption( "stencil", "stencil: B(1-a)" );
	operation->appendOption( "under", "under: A(1-b)+B" );
	operation->appendOption( "xor", "xor: A(1-b)+B(1-a)" );
    operation->setDefault(Merge_Plus);
    operation->setAnimates(false);
    page->addChild(*operation);
    
    BooleanParamDescriptor* alphaMasking = desc.defineBooleanParam(kAlphaMaskingParamName);
    alphaMasking->setLabels(kAlphaMaskingParamName, kAlphaMaskingParamName, kAlphaMaskingParamName);
    alphaMasking->setAnimates(false);
    alphaMasking->setDefault(false);
    alphaMasking->setHint("When enabled, the input images are unchanged where the other image has 0 alpha and"
                          " the output alpha is set to a+b - a*b. When disabled the alpha channel is processed as "
                          "any other channel.");
    
    ChoiceParamDescriptor* boundingBox = desc.defineChoiceParam(kBboxParamName);
    boundingBox->setLabels(kBboxParamName, kBboxParamName, kBboxParamName);
    boundingBox->setHint("What to use to produce the output image's bounding box.");
    boundingBox->appendOption("Union");
    boundingBox->appendOption("Intersection");
    boundingBox->appendOption("A");
    boundingBox->appendOption("B");
    boundingBox->setAnimates(false);
    boundingBox->setDefault(0);
    page->addChild(*boundingBox);
    
    ofxsFilterDescribeParamsMaskMix(desc, page);
 
}

OFX::ImageEffect* MergePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new MergePlugin(handle);
}

