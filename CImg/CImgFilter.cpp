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

#include "CImgFilter.h"

using namespace OFX;

#if defined(HAVE_THREAD_LOCAL)
thread_local ImageEffect *tls::gImageEffect = 0;
#elif defined(HAVE_PTHREAD)
#include <assert.h>
#include <pthread.h>
pthread_key_t tls::gImageEffect_key;
pthread_once_t tls::gImageEffect_once = PTHREAD_ONCE_INIT;
#endif


#define kParamPremultChanged "premultChanged"

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

CImgFilterPluginHelperBase::CImgFilterPluginHelperBase(OfxImageEffectHandle handle,
                                                       bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                                                       bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                                                       bool supportsTiles,
                                                       bool supportsMultiResolution,
                                                       bool supportsRenderScale,
                                                       bool defaultUnpremult /* = true*/,
                                                       bool isFilter /* = true*/)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClip(NULL)
    , _maskClip(NULL)
    , _processR(NULL)
    , _processG(NULL)
    , _processB(NULL)
    , _processA(NULL)
    , _premult(NULL)
    , _premultChannel(NULL)
    , _mix(NULL)
    , _maskApply(NULL)
    , _maskInvert(NULL)
    , _usesMask(usesMask)
    , _supportsComponentRemapping(supportsComponentRemapping)
    , _supportsTiles(getImageEffectHostDescription()->supportsTiles && supportsTiles)
    , _supportsMultiResolution(getImageEffectHostDescription()->supportsMultiResolution && supportsMultiResolution)
    , _supportsRenderScale(supportsRenderScale)
    , _defaultUnpremult(defaultUnpremult)
    , _premultChanged(NULL)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                         _dstClip->getPixelComponents() == ePixelComponentRGBA) );
    if (isFilter) {
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
    }

    if ( paramExists(kParamProcessR) ) {
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }
    if ( paramExists(kParamPremult) ) {
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
    }
    _mix = fetchDoubleParam(kParamMix);
    _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);
    if ( paramExists(kParamPremultChanged) ) {
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
    }
}

void
CImgFilterPluginHelperBase::changedClip(const InstanceChangedArgs &args,
                                        const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         ( args.reason == eChangeUserEdit) ) {
        if ( _defaultUnpremult && _premult && _premultChanged && !_premultChanged->getValue() ) {
            if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
                _premult->setValue(false);
            } else {
                switch ( _srcClip->getPreMultiplication() ) {
                case eImageOpaque:
                    _premult->setValue(false);
                    break;
                case eImagePreMultiplied:
                    _premult->setValue(true);
                    break;
                case eImageUnPreMultiplied:
                    _premult->setValue(false);
                    break;
                }
            }
        }
    }
}

void
CImgFilterPluginHelperBase::changedParam(const InstanceChangedArgs &args,
                                         const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) && _premultChanged ) {
        _premultChanged->setValue(true);
    }
}

PageParamDescriptor*
CImgFilterPluginHelperBase::describeInContextBegin(bool sourceIsOptional,
                                                   ImageEffectDescriptor &desc,
                                                   ContextEnum context,
                                                   bool supportsRGBA,
                                                   bool supportsRGB,
                                                   bool supportsXY,
                                                   bool supportsAlpha,
                                                   bool supportsTiles,
                                                   bool processRGB,
                                                   bool processAlpha,
                                                   bool processIsSecret)
{
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif

    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    if (supportsRGBA) {
        srcClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcClip->addSupportedComponent(ePixelComponentRGB);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (supportsXY) {
        srcClip->addSupportedComponent(ePixelComponentXY);
    }
#endif
    if (supportsAlpha) {
        srcClip->addSupportedComponent(ePixelComponentAlpha);
    }
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(supportsTiles);
    srcClip->setIsMask(false);
    if ( (context == eContextGeneral) && sourceIsOptional ) {
        srcClip->setOptional(sourceIsOptional);
    }

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    if (supportsRGBA) {
        dstClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        dstClip->addSupportedComponent(ePixelComponentRGB);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (supportsXY) {
        dstClip->addSupportedComponent(ePixelComponentXY);
    }
#endif
    if (supportsAlpha) {
        dstClip->addSupportedComponent(ePixelComponentAlpha);
    }
    dstClip->setSupportsTiles(supportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(supportsTiles);
    maskClip->setIsMask(true);

    // create the params
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(processRGB);
        param->setIsSecretAndDisabled(processIsSecret);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(processRGB);
        param->setIsSecretAndDisabled(processIsSecret);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(processRGB);
        param->setIsSecretAndDisabled(processIsSecret);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(processAlpha);
        param->setIsSecretAndDisabled(processIsSecret);
        if (page) {
            page->addChild(*param);
        }
    }


    return page;
} // CImgFilterPluginHelperBase::describeInContextBegin

void
CImgFilterPluginHelperBase::describeInContextEnd(ImageEffectDescriptor &desc,
                                                 ContextEnum /*context*/,
                                                 PageParamDescriptor* page,
                                                 bool hasUnpremult)
{
    if (hasUnpremult) {
        ofxsPremultDescribeParams(desc, page);
    }
    ofxsMaskMixDescribeParams(desc, page);

    if (hasUnpremult) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

/* set up and run a copy processor */
void
CImgFilterPluginHelperBase::setupAndFill(PixelProcessorFilterBase & processor,
                                         const OfxRectI &renderWindow,
                                         const OfxPointD &renderScale,
                                         void *dstPixelData,
                                         const OfxRectI& dstBounds,
                                         PixelComponentEnum dstPixelComponents,
                                         int dstPixelComponentCount,
                                         BitDepthEnum dstPixelDepth,
                                         int dstRowBytes)
{
    assert(dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow, renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

/* set up and run a copy processor */
void
CImgFilterPluginHelperBase::setupAndCopy(PixelProcessorFilterBase & processor,
                                         double time,
                                         const OfxRectI &renderWindow,
                                         const OfxPointD &renderScale,
                                         const Image* orig,
                                         const Image* mask,
                                         const void *srcPixelData,
                                         const OfxRectI& srcBounds,
                                         PixelComponentEnum srcPixelComponents,
                                         int srcPixelComponentCount,
                                         BitDepthEnum srcBitDepth,
                                         int srcRowBytes,
                                         int srcBoundary,
                                         void *dstPixelData,
                                         const OfxRectI& dstBounds,
                                         PixelComponentEnum dstPixelComponents,
                                         int dstPixelComponentCount,
                                         BitDepthEnum dstPixelDepth,
                                         int dstRowBytes,
                                         bool premult,
                                         int premultChannel,
                                         double mix,
                                         bool maskInvert)
{
# ifndef NDEBUG
    // src may not be valid over the renderWindow
    //assert(srcPixelData &&
    //       srcBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= srcBounds.x2 &&
    //       srcBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= srcBounds.y2);
    // dst must be valid over the renderWindow
    assert(dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // make sure bit depths are sane
    if ( srcPixelData && (srcBitDepth != dstPixelDepth) ) {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
# endif

    if ( Coords::rectIsEmpty(renderWindow) ) {
        return;
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        processor.doMasking(true);
        processor.setMaskImg(mask, maskInvert);
    }

    // set the images
    assert(dstPixelData);
    processor.setOrigImg(orig);
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);
    assert(0 <= srcBoundary && srcBoundary <= 2);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, srcBoundary);

    // set the render window
    processor.setRenderWindow(renderWindow, renderScale);

    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// utility functions
bool
CImgFilterPluginHelperBase::maskLineIsZero(const Image* mask,
                                           int x1,
                                           int x2,
                                           int y,
                                           bool maskInvert)
{
    assert( !mask || (mask->getPixelComponents() == ePixelComponentAlpha && mask->getPixelDepth() == eBitDepthFloat) );

    if (maskInvert) {
        if (!mask) {
            return false;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the line is out of maskbounds, then mask is 1 at these places
        if ( (y < maskBounds.y1) || (maskBounds.y2 <= y) || (x1 < maskBounds.x1) || (maskBounds.x2 <= x2) ) {
            return false;
        }
        // the whole line is within the mask
        const float *p = reinterpret_cast<const float*>( mask->getPixelAddress(x1, y) );
        assert(p);
        for (int x = x1; x < x2; ++x, ++p) {
            if (*p != 1.) {
                return false;
            }
        }
    } else {
        if (!mask) {
            return true;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if the line is completely out of the mask, it is 0
        if ( (y < maskBounds.y1) || (maskBounds.y2 <= y) ) {
            return true;
        }
        // restrict the search to the part of the line which is within the mask
        x1 = (std::max)(x1, maskBounds.x1);
        x2 = (std::min)(x2, maskBounds.x2);
        if (x1 < x2) { // the line is not empty
            const float *p = reinterpret_cast<const float*>( mask->getPixelAddress(x1, y) );
            assert(p);

            for (int x = x1; x < x2; ++x, ++p) {
                if (*p != 0.) {
                    return false;
                }
            }
        }
    }

    return true;
}

bool
CImgFilterPluginHelperBase::maskColumnIsZero(const Image* mask,
                                             int x,
                                             int y1,
                                             int y2,
                                             bool maskInvert)
{
    if (!mask) {
        return (!maskInvert);
    }

    assert(mask->getPixelComponents() == ePixelComponentAlpha && mask->getPixelDepth() == eBitDepthFloat);
    const int rowElems = mask->getRowBytes() / sizeof(float); // may be negative, @see kOfxImagePropRowBytes

    if (maskInvert) {
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the column is out of maskbounds, then mask is 1 at these places
        if ( (x < maskBounds.x1) || (maskBounds.x2 <= x) || (y1 < maskBounds.y1) || (maskBounds.y2 <= y2) ) {
            return false;
        }
        // the whole column is within the mask
        const float *p = reinterpret_cast<const float*>( mask->getPixelAddress(x, y1) );
        assert(p);
        for (int y = y1; y < y2; ++y,  p += rowElems) {
            if (*p != 1.) {
                return false;
            }
        }
    } else {
        const OfxRectI& maskBounds = mask->getBounds();
        // if the column is completely out of the mask, it is 0
        if ( (x < maskBounds.x1) || (maskBounds.x2 <= x) ) {
            return true;
        }
        // restrict the search to the part of the column which is within the mask
        y1 = (std::max)(y1, maskBounds.y1);
        y2 = (std::min)(y2, maskBounds.y2);
        if (y1 < y2) { // the column is not empty
            const float *p = reinterpret_cast<const float*>( mask->getPixelAddress(x, y1) );
            assert(p);

            for (int y = y1; y < y2; ++y,  p += rowElems) {
                if (*p != 0.) {
                    return false;
                }
            }
        }
    }

    return true;
}
