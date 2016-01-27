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

#include "CImgFilter.h"

#ifdef HAVE_THREAD_LOCAL
thread_local OFX::ImageEffect *tls::gImageEffect = 0;

#endif

#define kParamPremultChanged "premultChanged"


CImgFilterPluginHelperBase::CImgFilterPluginHelperBase(OfxImageEffectHandle handle,
                                                       bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                                                       bool supportsTiles,
                                                       bool supportsMultiResolution,
                                                       bool supportsRenderScale,
                                                       bool defaultUnpremult/* = true*/,
                                                       bool defaultProcessAlphaOnRGBA/* = false*/,
                                                       bool isFilter/* = true*/)
: ImageEffect(handle)
, _dstClip(0)
, _srcClip(0)
, _maskClip(0)
, _processR(0)
, _processG(0)
, _processB(0)
, _processA(0)
, _premult(0)
, _premultChannel(0)
, _mix(0)
, _maskApply(0)
, _maskInvert(0)
, _supportsComponentRemapping(supportsComponentRemapping)
, _supportsTiles(supportsTiles)
, _supportsMultiResolution(supportsMultiResolution)
, _supportsRenderScale(supportsRenderScale)
, _defaultUnpremult(defaultUnpremult)
, _defaultProcessAlphaOnRGBA(defaultProcessAlphaOnRGBA)
, _premultChanged(0)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                        _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA));
    if (isFilter) {
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == OFX::ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == OFX::ePixelComponentRGBA)));
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == OFX::ePixelComponentAlpha);
    }

    if (paramExists(kNatronOfxParamProcessR)) {
        _processR = fetchBooleanParam(kNatronOfxParamProcessR);
        _processG = fetchBooleanParam(kNatronOfxParamProcessG);
        _processB = fetchBooleanParam(kNatronOfxParamProcessB);
        _processA = fetchBooleanParam(kNatronOfxParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }
    _premult = fetchBooleanParam(kParamPremult);
    _premultChannel = fetchChoiceParam(kParamPremultChannel);
    assert(_premult && _premultChannel);
    _mix = fetchDoubleParam(kParamMix);
    _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
    _maskInvert = fetchBooleanParam(kParamMaskInvert);
    assert(_mix && _maskInvert);
    _premultChanged = fetchBooleanParam(kParamPremultChanged);
    assert(_premultChanged);
}


void
CImgFilterPluginHelperBase::changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName &&
        _srcClip && _srcClip->isConnected() &&
        args.reason == OFX::eChangeUserEdit) {
        beginEditBlock("changedClip");
        if (_defaultUnpremult && !_premultChanged->getValue()) {
            switch (_srcClip->getPreMultiplication()) {
                case OFX::eImageOpaque:
                    _premult->setValue(false);
                    break;
                case OFX::eImagePreMultiplied:
                    _premult->setValue(true);
                    break;
                case OFX::eImageUnPreMultiplied:
                    _premult->setValue(false);
                    break;
            }
        }
        if (_processR) {
            switch (_srcClip->getPixelComponents()) {
                case OFX::ePixelComponentAlpha:
                    _processR->setValue(false);
                    _processG->setValue(false);
                    _processB->setValue(false);
                    _processA->setValue(true);
                    break;
                case OFX::ePixelComponentRGBA:
                case OFX::ePixelComponentRGB:
                    // Alpha is not processed by default on RGBA images
                    _processR->setValue(true);
                    _processG->setValue(true);
                    _processB->setValue(true);
                    _processA->setValue(_defaultProcessAlphaOnRGBA);
                    break;
                default:
                    break;
            }
        }
        endEditBlock();
    }
}

void
CImgFilterPluginHelperBase::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamPremult && args.reason == OFX::eChangeUserEdit) {
        _premultChanged->setValue(true);
    }
}

OFX::PageParamDescriptor*
CImgFilterPluginHelperBase::describeInContextBegin(bool sourceIsOptional,
                                                   OFX::ImageEffectDescriptor &desc,
                                                   OFX::ContextEnum context,
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
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif

    OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    if (supportsRGBA) {
        srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
    }
    if (supportsXY) {
        srcClip->addSupportedComponent(OFX::ePixelComponentXY);
    }
    if (supportsAlpha) {
        srcClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    }
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(supportsTiles);
    srcClip->setIsMask(false);
    if (context == OFX::eContextGeneral && sourceIsOptional) {
        srcClip->setOptional(sourceIsOptional);
    }

    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    if (supportsRGBA) {
        dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    }
    if (supportsRGB) {
        dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    }
    if (supportsXY) {
        dstClip->addSupportedComponent(OFX::ePixelComponentXY);
    }
    if (supportsAlpha) {
        dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    }
    dstClip->setSupportsTiles(supportsTiles);

    OFX::ClipDescriptor *maskClip = (context == OFX::eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != OFX::eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(supportsTiles);
    maskClip->setIsMask(true);

    // create the params
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessR);
        param->setLabel(kNatronOfxParamProcessRLabel);
        param->setHint(kNatronOfxParamProcessRHint);
        param->setDefault(processRGB);
        param->setIsSecret(processIsSecret);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessG);
        param->setLabel(kNatronOfxParamProcessGLabel);
        param->setHint(kNatronOfxParamProcessGHint);
        param->setDefault(processRGB);
        param->setIsSecret(processIsSecret);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessB);
        param->setLabel(kNatronOfxParamProcessBLabel);
        param->setHint(kNatronOfxParamProcessBHint);
        param->setDefault(processRGB);
        param->setIsSecret(processIsSecret);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setDefault(processAlpha);
        param->setIsSecret(processIsSecret);
        if (page) {
            page->addChild(*param);
        }
    }


    return page;
}

void
CImgFilterPluginHelperBase::describeInContextEnd(OFX::ImageEffectDescriptor &desc,
                                                 OFX::ContextEnum /*context*/,
                                                 OFX::PageParamDescriptor* page)
{
    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}




/* set up and run a copy processor */
void
CImgFilterPluginHelperBase::setupAndFill(OFX::PixelProcessorFilterBase & processor,
                                         const OfxRectI &renderWindow,
                                         void *dstPixelData,
                                         const OfxRectI& dstBounds,
                                         OFX::PixelComponentEnum dstPixelComponents,
                                         int dstPixelComponentCount,
                                         OFX::BitDepthEnum dstPixelDepth,
                                         int dstRowBytes)
{
    assert(dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstPixelDepth, dstRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


/* set up and run a copy processor */
void
CImgFilterPluginHelperBase::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                                         double time,
                                         const OfxRectI &renderWindow,
                                         const OFX::Image* orig,
                                         const OFX::Image* mask,
                                         const void *srcPixelData,
                                         const OfxRectI& srcBounds,
                                         OFX::PixelComponentEnum srcPixelComponents,
                                         int srcPixelComponentCount,
                                         OFX::BitDepthEnum srcBitDepth,
                                         int srcRowBytes,
                                         int srcBoundary,
                                         void *dstPixelData,
                                         const OfxRectI& dstBounds,
                                         OFX::PixelComponentEnum dstPixelComponents,
                                         int dstPixelComponentCount,
                                         OFX::BitDepthEnum dstPixelDepth,
                                         int dstRowBytes,
                                         bool premult,
                                         int premultChannel,
                                         double mix,
                                         bool maskInvert)
{
    // src may not be valid over the renderWindow
    //assert(srcPixelData &&
    //       srcBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= srcBounds.x2 &&
    //       srcBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= srcBounds.y2);
    // dst must be valid over the renderWindow
    assert(dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // make sure bit depths are sane
    if(srcBitDepth != dstPixelDepth/* || srcPixelComponents != dstPixelComponents*/) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    if (isEmpty(renderWindow)) {
        return;
    }
    bool doMasking = ((!_maskApply || _maskApply->getValueAtTime(time)) && _maskClip && _maskClip->isConnected());
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
    processor.setRenderWindow(renderWindow);

    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


// utility functions
bool
CImgFilterPluginHelperBase::maskLineIsZero(const OFX::Image* mask, int x1, int x2, int y, bool maskInvert)
{
    assert(!mask || (mask->getPixelComponents() == OFX::ePixelComponentAlpha && mask->getPixelDepth() == OFX::eBitDepthFloat));

    if (maskInvert) {
        if (!mask) {
            return false;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the line is out of maskbounds, then mask is 1 at these places
        if (y < maskBounds.y1 || maskBounds.y2 <= y || x1 < maskBounds.x1 || maskBounds.x2 <= x2) {
            return false;
        }
        // the whole line is within the mask
        const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x1, y));
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
        if (y < maskBounds.y1 || maskBounds.y2 <= y) {
            return true;
        }
        // restrict the search to the part of the line which is within the mask
        x1 = std::max(x1, maskBounds.x1);
        x2 = std::min(x2, maskBounds.x2);
        if (x1 < x2) { // the line is not empty
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x1, y));
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
CImgFilterPluginHelperBase::maskColumnIsZero(const OFX::Image* mask, int x, int y1, int y2, bool maskInvert)
{
    if (!mask) {
        return (!maskInvert);
    }

    assert(mask->getPixelComponents() == OFX::ePixelComponentAlpha && mask->getPixelDepth() == OFX::eBitDepthFloat);
    const int rowElems = mask->getRowBytes() / sizeof(float);

    if (maskInvert) {
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the column is out of maskbounds, then mask is 1 at these places
        if (x < maskBounds.x1 || maskBounds.x2 <= x || y1 < maskBounds.y1 || maskBounds.y2 <= y2) {
            return false;
        }
        // the whole column is within the mask
        const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y1));
        assert(p);
        for (int y = y1; y < y2; ++y,  p += rowElems) {
            if (*p != 1.) {
                return false;
            }
        }
    } else {
        const OfxRectI& maskBounds = mask->getBounds();
        // if the column is completely out of the mask, it is 0
        if (x < maskBounds.x1 || maskBounds.x2 <= x) {
            return true;
        }
        // restrict the search to the part of the column which is within the mask
        y1 = std::max(y1, maskBounds.y1);
        y2 = std::min(y2, maskBounds.y2);
        if (y1 < y2) { // the column is not empty
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y1));
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

