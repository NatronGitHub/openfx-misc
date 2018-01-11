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

#include "CImgOperator.h"

using namespace OFX;

CImgOperatorPluginHelperBase::CImgOperatorPluginHelperBase(OfxImageEffectHandle handle,
                                                           const char* srcAClipName, //!< should be either kOfxImageEffectSimpleSourceClipName or "A" if you want this to be the default output when plugin is disabled
                                                           const char* srcBClipName,
                                                           bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                                                           bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                                                           bool supportsTiles,
                                                           bool supportsMultiResolution,
                                                           bool supportsRenderScale,
                                                           bool defaultUnpremult,
                                                           bool defaultProcessAlphaOnRGBA)
    : CImgFilterPluginHelperBase(handle, usesMask, supportsComponentRemapping, supportsTiles, supportsMultiResolution, supportsRenderScale, defaultUnpremult, defaultProcessAlphaOnRGBA)
    , _srcAClip(NULL)
    , _srcBClip(NULL)
    , _srcAClipName(srcAClipName)
    , _srcBClipName(srcBClipName)
{
    _srcAClip = fetchClip(_srcAClipName);
    assert( _srcAClip && (!_srcAClip->isConnected() || _srcAClip->getPixelComponents() == ePixelComponentRGB || _srcAClip->getPixelComponents() == ePixelComponentRGBA) );
    _srcBClip = fetchClip(_srcBClipName);
    assert( _srcBClip && (!_srcBClip->isConnected() || _srcAClip->getPixelComponents() == ePixelComponentRGB || _srcBClip->getPixelComponents() == ePixelComponentRGBA) );
}

void
CImgOperatorPluginHelperBase::changedClip(const InstanceChangedArgs &args,
                                          const std::string &clipName)
{
    if ( (clipName == _srcAClipName) &&
         _srcAClip && _srcAClip->isConnected() &&
         ( args.reason == eChangeUserEdit) ) {
        if ( _defaultUnpremult && _premultChanged && !_premultChanged->getValue() ) {
            switch ( _srcAClip->getPreMultiplication() ) {
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
    if ( (clipName == _srcBClipName) &&
         _srcBClip && _srcBClip->isConnected() &&
         _premultChanged && !_premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        if (_defaultUnpremult) {
            switch ( _srcBClip->getPreMultiplication() ) {
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

PageParamDescriptor*
CImgOperatorPluginHelperBase::describeInContextBegin(ImageEffectDescriptor &desc,
                                                     ContextEnum /*context*/,
                                                     const char* srcAClipName,
                                                     const char* srcAClipHint,
                                                     const char* srcBClipName,
                                                     const char* srcBClipHint,
                                                     bool supportsRGBA,
                                                     bool supportsRGB,
                                                     bool supportsXY,
                                                     bool supportsAlpha,
                                                     bool supportsTiles,
                                                     bool /*processRGB*/,
                                                     bool /*processAlpha*/,
                                                     bool /*processIsSecret*/)
{
    ClipDescriptor *srcBClip = desc.defineClip(srcBClipName);

    if (srcBClipHint) {
        srcBClip->setHint(srcBClipHint);
    }
    ClipDescriptor *srcAClip = desc.defineClip(srcAClipName);
    if (srcAClipHint) {
        srcAClip->setHint(srcAClipHint);
    }
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);

    if (supportsRGBA) {
        srcAClip->addSupportedComponent(ePixelComponentRGBA);
        srcBClip->addSupportedComponent(ePixelComponentRGBA);
        dstClip->addSupportedComponent(ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcAClip->addSupportedComponent(ePixelComponentRGB);
        srcBClip->addSupportedComponent(ePixelComponentRGB);
        dstClip->addSupportedComponent(ePixelComponentRGB);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (supportsXY) {
        srcAClip->addSupportedComponent(ePixelComponentXY);
        srcBClip->addSupportedComponent(ePixelComponentXY);
        dstClip->addSupportedComponent(ePixelComponentXY);
    }
#endif
    if (supportsAlpha) {
        srcAClip->addSupportedComponent(ePixelComponentAlpha);
        srcBClip->addSupportedComponent(ePixelComponentAlpha);
        dstClip->addSupportedComponent(ePixelComponentAlpha);
    }
    srcAClip->setTemporalClipAccess(false);
    srcBClip->setTemporalClipAccess(false);
    dstClip->setSupportsTiles(supportsTiles);
    srcAClip->setSupportsTiles(supportsTiles);
    srcBClip->setSupportsTiles(supportsTiles);
    srcAClip->setIsMask(false);
    srcBClip->setIsMask(false);

    // create the params
    PageParamDescriptor *page = desc.definePageParam("Controls");

    return page;
}
