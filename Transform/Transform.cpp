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
 * OFX Transform & DirBlur plugins.
 */

#include "Transform.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "ofxsCoords.h"

#define kPluginName "TransformOFX"
#define kPluginMaskedName "TransformMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Translate / Rotate / Scale a 2D image.\n"\
"This plugin concatenates transforms."
#define kPluginMaskedDescription "Translate / Rotate / Scale a 2D image, with optional masking.\n"\
"This plugin concatenates transforms upstream."
#define kPluginIdentifier "net.sf.openfx.TransformPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.TransformMaskedPlugin"
#define kPluginDirBlurName "DirBlurOFX"
#define kPluginDirBlurGrouping "Filter"
#define kPluginDirBlurDescription "Apply directional blur to an image.\n"\
"This plugin concatenates transforms upstream."
#define kPluginDirBlurIdentifier "net.sf.openfx.DirBlur"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSrcClipChanged "srcClipChanged"

using namespace OFX;


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle, bool masked, bool isDirBlur)
    : Transform3x3Plugin(handle, masked, isDirBlur ? eTransform3x3ParamsTypeDirBlur : eTransform3x3ParamsTypeMotionBlur)
    , _translate(0)
    , _rotate(0)
    , _scale(0)
    , _scaleUniform(0)
    , _skewX(0)
    , _skewY(0)
    , _skewOrder(0)
    , _center(0)
    , _interactive(0)
    , _srcClipChanged(0)
    {
        // NON-GENERIC
        _translate = fetchDouble2DParam(kParamTransformTranslateOld);
        _rotate = fetchDoubleParam(kParamTransformRotateOld);
        _scale = fetchDouble2DParam(kParamTransformScaleOld);
        _scaleUniform = fetchBooleanParam(kParamTransformScaleUniformOld);
        _skewX = fetchDoubleParam(kParamTransformSkewXOld);
        _skewY = fetchDoubleParam(kParamTransformSkewYOld);
        _skewOrder = fetchChoiceParam(kParamTransformSkewOrderOld);
        _center = fetchDouble2DParam(kParamTransformCenterOld);
        _interactive = fetchBooleanParam(kParamTransformInteractiveOld);
        assert(_translate && _rotate && _scale && _scaleUniform && _skewX && _skewY && _skewOrder && _center && _interactive);
        _srcClipChanged = fetchBooleanParam(kSrcClipChanged);
        assert(_srcClipChanged);
        
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    void resetCenter(double time);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::DoubleParam* _skewX;
    OFX::DoubleParam* _skewY;
    OFX::ChoiceParam* _skewOrder;
    OFX::Double2DParam* _center;
    OFX::BooleanParam* _interactive;
    OFX::BooleanParam* _srcClipChanged; // set to true the first time the user connects src
};

// overridden is identity
bool
TransformPlugin::isIdentity(double time)
{
    // NON-GENERIC
    OfxPointD scaleParam = { 1., 1. };
    if (_scale) {
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    }
    bool scaleUniform = false;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }
    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);
    OfxPointD translate = { 0., 0. };
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    double rotate = 0.;
    if (_rotate) {
        _rotate->getValueAtTime(time, rotate);
    }
    double skewX = 0.;
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    double skewY = 0.;
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }
    
    if (scale.x == 1. && scale.y == 1. && translate.x == 0. && translate.y == 0. && rotate == 0. && skewX == 0. && skewY == 0.) {
        return true;
    }

    return false;
}

bool
TransformPlugin::getInverseTransformCanonical(double time, int /*view*/, double amount, bool invert, OFX::Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD center = { 0., 0. };
    if (_center) {
        _center->getValueAtTime(time, center.x, center.y);
    }
    OfxPointD translate = { 0., 0. };
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    OfxPointD scaleParam = { 1., 1. };
    if (_scale) {
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    }
    bool scaleUniform = false;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }
    double rotate = 0.;
    if (_rotate) {
        _rotate->getValueAtTime(time, rotate);
    }
    double skewX = 0.;
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    double skewY = 0.;
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }
    int skewOrder = 0;
    if (_skewOrder) {
        _skewOrder->getValueAtTime(time, skewOrder);
    }

    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    if (amount != 1.) {
        translate.x *= amount;
        translate.y *= amount;
        if (scale.x <= 0.) {
            // linear interpolation
            scale.x = 1. + (scale.x - 1.) * amount;
        } else {
            // geometric interpolation
            scale.x = std::pow(scale.x, amount);
        }
        if (scale.y <= 0) {
            // linear interpolation
            scale.y = 1. + (scale.y - 1.) * amount;
        } else {
            // geometric interpolation
            scale.y = std::pow(scale.y, amount);
        }
        rotate *= amount;
        skewX *= amount;
        skewY *= amount;
    }

    double rot = OFX::ofxsToRadians(rotate);
    if (!invert) {
        *invtransform = OFX::ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    } else {
        *invtransform = OFX::ofxsMatTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    }
    return true;
}

void
TransformPlugin::resetCenter(double time)
{
    if (!_srcClip) {
        return;
    }
    OfxRectD rod = _srcClip->getRegionOfDefinition(time);
    if (rod.x1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.x2 ||
        rod.y1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.y2) {
        return;
    }
    if (OFX::Coords::rectIsEmpty(rod)) {
        // default to project window
        OfxPointD offset = getProjectOffset();
        OfxPointD size = getProjectSize();
        rod.x1 = offset.x;
        rod.x2 = offset.x + size.x;
        rod.y1 = offset.y;
        rod.y2 = offset.y + size.y;
    }
    double currentRotation = 0.;
    if (_rotate) {
        _rotate->getValueAtTime(time, currentRotation);
    }
    double rot = OFX::ofxsToRadians(currentRotation);

    double skewX = 0.;
    double skewY = 0.;
    int skewOrder = 0;
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }
    if (_skewOrder) {
        _skewOrder->getValueAtTime(time, skewOrder);
    }

    OfxPointD scaleParam = { 1., 1. };
    if (_scale) {
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    }
    bool scaleUniform = true;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }

    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD translate = {0., 0. };
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    OfxPointD center = {0., 0. };
    if (_center) {
        _center->getValueAtTime(time, center.x, center.y);
    }

    OFX::Matrix3x3 Rinv = (ofxsMatRotation(-rot) *
                           ofxsMatSkewXY(skewX, skewY, skewOrder) *
                           ofxsMatScale(scale.x, scale.y));
    OfxPointD newCenter;
    newCenter.x = (rod.x1+rod.x2)/2;
    newCenter.y = (rod.y1+rod.y2)/2;
    beginEditBlock("resetCenter");
    if (_center) {
        _center->setValue(newCenter.x, newCenter.y);
    }
    if (_translate) {
        double dxrot = newCenter.x - center.x;
        double dyrot = newCenter.y - center.y;
        OFX::Point3D dRot;
        dRot.x = dxrot;
        dRot.y = dyrot;
        dRot.z = 1;
        dRot = Rinv * dRot;
        if (dRot.z != 0) {
            dRot.x /= dRot.z;
            dRot.y /= dRot.z;
        }
        double dx = dRot.x;
        double dy = dRot.y;
        OfxPointD newTranslate;
        newTranslate.x = translate.x + dx - dxrot;
        newTranslate.y = translate.y + dy - dyrot;
        _translate->setValue(newTranslate.x,newTranslate.y);
    }
    endEditBlock();
}

void
TransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamTransformResetCenterOld) {
        resetCenter(args.time);
    } else if (paramName == kParamTransformTranslateOld ||
        paramName == kParamTransformRotateOld ||
        paramName == kParamTransformScaleOld ||
        paramName == kParamTransformScaleUniformOld ||
        paramName == kParamTransformSkewXOld ||
        paramName == kParamTransformSkewYOld ||
        paramName == kParamTransformSkewOrderOld ||
        paramName == kParamTransformCenterOld) {
        changedTransform(args);
    } else if (paramName == kParamPremult && args.reason == OFX::eChangeUserEdit) {
        _srcClipChanged->setValue(true);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
TransformPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName &&
        _srcClip && _srcClip->isConnected() &&
        args.reason == OFX::eChangeUserEdit) {
        resetCenter(args.time);
    }
}


using namespace OFX;

mDeclarePluginFactory(TransformPluginFactory, {}, {});


static
void TransformPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    ofxsTransformDescribeParams(desc, page, NULL, /*isOpen=*/true, /*oldParams=*/true);
}

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptorOldParams);
}

void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false, OFX::Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur);
    
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kSrcClipChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

}

OFX::ImageEffect* TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, false, false);
}


mDeclarePluginFactory(TransformMaskedPluginFactory, {}, {});

void TransformMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMaskedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginMaskedDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptorOldParams);
}


void TransformMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, OFX::Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur);

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kSrcClipChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* TransformMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true, false);
}

mDeclarePluginFactory(DirBlurPluginFactory, {}, {});

void DirBlurPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginDirBlurName);
    desc.setPluginGrouping(kPluginDirBlurGrouping);
    desc.setPluginDescription(kPluginDirBlurDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptorOldParams);
}


void DirBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, OFX::Transform3x3Plugin::eTransform3x3ParamsTypeDirBlur);

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kSrcClipChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* DirBlurPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true, true);
}




void getTransformPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        static TransformPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static TransformMaskedPluginFactory p(kPluginMaskedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static DirBlurPluginFactory p(kPluginDirBlurIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
