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

#include "CImgOperator.h"

CImgOperatorPluginHelperBase::CImgOperatorPluginHelperBase(OfxImageEffectHandle handle,
                                                           const char* srcAClipName, //!< should be either kOfxImageEffectSimpleSourceClipName or "A" if you want this to be the default output when plugin is disabled
                                                           const char* srcBClipName,
                                                           bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                                                           bool supportsTiles,
                                                           bool supportsMultiResolution,
                                                           bool supportsRenderScale,
                                                           bool defaultUnpremult,
                                                           bool defaultProcessAlphaOnRGBA)
    : CImgFilterPluginHelperBase(handle, supportsComponentRemapping, supportsTiles, supportsMultiResolution, supportsRenderScale, defaultUnpremult, defaultProcessAlphaOnRGBA, false)
    , _srcAClip(0)
    , _srcBClip(0)
    , _srcAClipName(srcAClipName)
    , _srcBClipName(srcBClipName)
{
    _srcAClip = fetchClip(_srcAClipName);
    assert( _srcAClip && (_srcAClip->getPixelComponents() == OFX::ePixelComponentRGB || _srcAClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
    _srcBClip = fetchClip(_srcBClipName);
    assert( _srcBClip && (_srcBClip->getPixelComponents() == OFX::ePixelComponentRGB || _srcBClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
}

void
CImgOperatorPluginHelperBase::changedClip(const OFX::InstanceChangedArgs &args,
                                          const std::string &clipName)
{
    if ( (clipName == _srcAClipName) &&
         _srcAClip && _srcAClip->isConnected() &&
         ( args.reason == OFX::eChangeUserEdit) ) {
        if ( _defaultUnpremult && !_premultChanged->getValue() ) {
            switch ( _srcAClip->getPreMultiplication() ) {
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
    }
    if ( (clipName == _srcBClipName) &&
         _srcBClip && _srcBClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == OFX::eChangeUserEdit) ) {
        if (_defaultUnpremult) {
            switch ( _srcBClip->getPreMultiplication() ) {
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
    }
}

OFX::PageParamDescriptor*
CImgOperatorPluginHelperBase::describeInContextBegin(OFX::ImageEffectDescriptor &desc,
                                                     OFX::ContextEnum /*context*/,
                                                     const char* srcAClipName,
                                                     const char* srcBClipName,
                                                     bool supportsRGBA,
                                                     bool supportsRGB,
                                                     bool supportsXY,
                                                     bool supportsAlpha,
                                                     bool supportsTiles,
                                                     bool /*processRGB*/,
                                                     bool /*processAlpha*/,
                                                     bool /*processIsSecret*/)
{
    OFX::ClipDescriptor *srcBClip = desc.defineClip(srcBClipName);
    OFX::ClipDescriptor *srcAClip = desc.defineClip(srcAClipName);
    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);

    if (supportsRGBA) {
        srcAClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        srcBClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    }
    if (supportsRGB) {
        srcAClip->addSupportedComponent(OFX::ePixelComponentRGB);
        srcBClip->addSupportedComponent(OFX::ePixelComponentRGB);
        dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    }
    if (supportsXY) {
        srcAClip->addSupportedComponent(OFX::ePixelComponentXY);
        srcBClip->addSupportedComponent(OFX::ePixelComponentXY);
        dstClip->addSupportedComponent(OFX::ePixelComponentXY);
    }
    if (supportsAlpha) {
        srcAClip->addSupportedComponent(OFX::ePixelComponentAlpha);
        srcBClip->addSupportedComponent(OFX::ePixelComponentAlpha);
        dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    }
    srcAClip->setTemporalClipAccess(false);
    srcBClip->setTemporalClipAccess(false);
    dstClip->setSupportsTiles(supportsTiles);
    srcAClip->setSupportsTiles(supportsTiles);
    srcBClip->setSupportsTiles(supportsTiles);
    srcAClip->setIsMask(false);
    srcBClip->setIsMask(false);

    // create the params
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    return page;
}

