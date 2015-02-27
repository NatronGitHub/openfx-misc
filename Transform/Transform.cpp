/*
 OFX Transform & DirBlur plugin.
 
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

#include "Transform.h"
#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#define kPluginName "TransformOFX"
#define kPluginMaskedName "TransformMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Translate / Rotate / Scale a 2D image."
#define kPluginIdentifier "net.sf.openfx.TransformPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.TransformMaskedPlugin"
#define kPluginDirBlurName "DirBlurOFX"
#define kPluginDirBlurGrouping "Filter"
#define kPluginDirBlurDescription "Apply directional blur to an image."
#define kPluginDirBlurIdentifier "net.sf.openfx.DirBlur"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle, bool masked, bool isDirBlur)
    : Transform3x3Plugin(handle, masked, isDirBlur)
    , _translate(0)
    , _rotate(0)
    , _scale(0)
    , _scaleUniform(0)
    , _skewX(0)
    , _skewY(0)
    , _skewOrder(0)
    , _center(0)
    , _interactive(0)
    {
        // NON-GENERIC
        _translate = fetchDouble2DParam(kParamTransformTranslate);
        _rotate = fetchDoubleParam(kParamTransformRotate);
        _scale = fetchDouble2DParam(kParamTransformScale);
        _scaleUniform = fetchBooleanParam(kParamTransformScaleUniform);
        _skewX = fetchDoubleParam(kParamTransformSkewX);
        _skewY = fetchDoubleParam(kParamTransformSkewY);
        _skewOrder = fetchChoiceParam(kParamTransformSkewOrder);
        _center = fetchDouble2DParam(kParamTransformCenter);
        _interactive = fetchBooleanParam(kParamTransformInteractive);
        assert(_translate && _rotate && _scale && _scaleUniform && _skewX && _skewY && _skewOrder && _center && _interactive);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

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
    BooleanParam* _interactive;
};

// overridden is identity
bool
TransformPlugin::isIdentity(double time)
{
    // NON-GENERIC
    OfxPointD scale;
    OfxPointD translate;
    double rotate;
    double skewX, skewY;
    _scale->getValueAtTime(time, scale.x, scale.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    if (scaleUniform) {
        scale.y = scale.x;
    }
    _translate->getValueAtTime(time, translate.x, translate.y);
    _rotate->getValueAtTime(time, rotate);
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    
    if (scale.x == 1. && scale.y == 1. && translate.x == 0. && translate.y == 0. && rotate == 0. && skewX == 0. && skewY == 0.) {
        return true;
    }

    return false;
}

bool
TransformPlugin::getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);
    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);
    double rotate;
    _rotate->getValueAtTime(time, rotate);
    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD scale;
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
    OfxRectD rod = _srcClip->getRegionOfDefinition(time);
    if (rod.x1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.x2 ||
        rod.y1 <= kOfxFlagInfiniteMin || kOfxFlagInfiniteMax <= rod.y2) {
        return;
    }
    if (rod.x1 == 0. && rod.x2 == 0. && rod.y1 == 0. && rod.y2 == 0.) {
        // default to project window
        OfxPointD offset = getProjectOffset();
        OfxPointD size = getProjectSize();
        rod.x1 = offset.x;
        rod.x2 = offset.x + size.x;
        rod.y1 = offset.y;
        rod.y2 = offset.y + size.y;
    }
    double currentRotation;
    _rotate->getValueAtTime(time, currentRotation);
    double rot = OFX::ofxsToRadians(currentRotation);

    double skewX, skewY;
    int skewOrder;
    _skewX->getValueAtTime(time, skewX);
    _skewY->getValueAtTime(time, skewY);
    _skewOrder->getValueAtTime(time, skewOrder);

    OfxPointD scaleParam;
    _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    bool scaleUniform;
    _scaleUniform->getValueAtTime(time, scaleUniform);

    OfxPointD scale;
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD translate;
    _translate->getValueAtTime(time, translate.x, translate.y);
    OfxPointD center;
    _center->getValueAtTime(time, center.x, center.y);

    OFX::Matrix3x3 Rinv = (ofxsMatRotation(-rot) *
                           ofxsMatSkewXY(skewX, skewY, skewOrder) *
                           ofxsMatScale(scale.x, scale.y));
    OfxPointD newCenter;
    newCenter.x = (rod.x1+rod.x2)/2;
    newCenter.y = (rod.y1+rod.y2)/2;
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

    beginEditBlock("resetCenter");
    _center->setValue(newCenter.x, newCenter.y);
    _translate->setValue(newTranslate.x,newTranslate.y);
    endEditBlock();
}

void
TransformPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamTransformResetCenter) {
        resetCenter(args.time);
    } else if (paramName == kParamTransformTranslate ||
        paramName == kParamTransformRotate ||
        paramName == kParamTransformScale ||
        paramName == kParamTransformScaleUniform ||
        paramName == kParamTransformSkewX ||
        paramName == kParamTransformSkewY ||
        paramName == kParamTransformSkewOrder ||
        paramName == kParamTransformCenter) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
TransformPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        resetCenter(args.time);
    }
}


#define kPluginMirrorName "MirrorOFX"
#define kPluginMirrorGrouping "Transform"
#define kPluginMirrorDescription "Flip (vertical mirror) or flop (horizontal mirror) an image."
#define kPluginMirrorIdentifier "net.sf.openfx.Mirror"

#define kParamMirrorFlip "flip"
#define kParamMirrorFlipLabel "Vertical (flip)"
#define kParamMirrorFlipHint "Upside-down (swap top and bottom)."

#define kParamMirrorFlop "flop"
#define kParamMirrorFlopLabel "Horizontal (flop)"
#define kParamMirrorFlopHint "Mirror image (swap left and right)"

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MirrorPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    MirrorPlugin(OfxImageEffectHandle handle)
    : Transform3x3Plugin(handle, /*masked=*/false, false)
    , _flip(0)
    , _flop(0)
    {
        // NON-GENERIC
        _flip = fetchBooleanParam(kParamMirrorFlip);
        _flop = fetchBooleanParam(kParamMirrorFlop);
        assert(_flip && _flop);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;


    // NON-GENERIC
    BooleanParam* _flip;
    BooleanParam* _flop;
};

// overridden is identity
bool
MirrorPlugin::isIdentity(double time)
{
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);

    if (!flip && !flop) {
        return true;
    }

    return false;
}

bool
MirrorPlugin::getInverseTransformCanonical(double time, double /*amount*/, bool /*invert*/, OFX::Matrix3x3* invtransform) const
{
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);
    if (!flip && !flop) {
        // identity
        invtransform->a = 1.;
        invtransform->b = 0.;
        invtransform->c = 0.;
        invtransform->d = 0.;
        invtransform->e = 1.;
        invtransform->f = 0.;
        invtransform->g = 0.;
        invtransform->h = 0.;
        invtransform->i = 1.;
        return true;
    }
    OfxRectD rod = _srcClip->getRegionOfDefinition(time);
    if (flop) {
        invtransform->a = -1.;
        invtransform->b = 0.;
        invtransform->c = rod.x1 + rod.x2;
    } else {
        invtransform->a = 1.;
        invtransform->b = 0.;
        invtransform->c = 0.;
    }
    if (flip) {
        invtransform->d = 0.;
        invtransform->e = -1.;
        invtransform->f = rod.y1 + rod.y2;
    } else {
        invtransform->d = 0.;
        invtransform->e = 1.;
        invtransform->f = 0.;
    }
    invtransform->g = 0.;
    invtransform->h = 0.;
    invtransform->i = 1.;
    
    return true;
}

void
MirrorPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamMirrorFlip ||
        paramName == kParamMirrorFlop) {
        changedTransform(args);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}



using namespace OFX;

mDeclarePluginFactory(TransformPluginFactory, {}, {});


static
void TransformPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    // translate
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTransformTranslate);
        param->setLabel(kParamTransformTranslateLabel);
        //param->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
        param->setDoubleType(eDoubleTypeXYAbsolute);
        //param->setDimensionLabels("x","y");
        param->setDefault(0, 0);
        param->setIncrement(10.);
        if (page) {
            page->addChild(*param);
        }
    }

    // rotate
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformRotate);
        param->setLabel(kParamTransformRotateLabel);
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(0);
        //param->setRange(-180, 180); // the angle may be -infinity..+infinity
        param->setDisplayRange(-180, 180);
        param->setIncrement(0.1);
        if (page) {
            page->addChild(*param);
        }
    }

    // scale
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTransformScale);
        param->setLabel(kParamTransformScaleLabel);
        param->setDoubleType(eDoubleTypeScale);
        //param->setDimensionLabels("w","h");
        param->setDefault(1,1);
        //param->setRange(0.1,0.1,10,10);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setIncrement(0.01);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }

    // scaleUniform
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformScaleUniform);
        param->setLabel(kParamTransformScaleUniformLabel);
        param->setHint(kParamTransformScaleUniformHint);
        // don't check it by default: it is easy to obtain Uniform scaling using the slider or the interact
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // skewX
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformSkewX);
        param->setLabel(kParamTransformSkewXLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }

    // skewY
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformSkewY);
        param->setLabel(kParamTransformSkewYLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }

    // skewOrder
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamTransformSkewOrder);
        param->setLabel(kParamTransformSkewOrderLabel);
        param->setDefault(0);
        param->appendOption("XY");
        param->appendOption("YX");
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // center
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamTransformCenter);
        param->setLabel(kParamTransformCenterLabel);
        //param->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
        param->setDoubleType(eDoubleTypeXYAbsolute);
        //param->setDimensionLabels("x","y");
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0.5, 0.5);
        param->setIncrement(1.);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }

    // resetcenter
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamTransformResetCenter);
        param->setLabel(kParamTransformResetCenterLabel);
        param->setHint(kParamTransformResetCenterHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformInteractive);
        param->setLabel(kParamTransformInteractiveLabel);
        param->setHint(kParamTransformInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}

void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false);
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
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}


void TransformMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true);
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

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}


void DirBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, true);
}

OFX::ImageEffect* DirBlurPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true, true);
}



mDeclarePluginFactory(MirrorPluginFactory, {}, {});

void MirrorPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMirrorName);
    desc.setPluginGrouping(kPluginMirrorGrouping);
    desc.setPluginDescription(kPluginMirrorDescription);

    Transform3x3Describe(desc, false);
}

void MirrorPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    // flip
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMirrorFlip);
        param->setLabel(kParamMirrorFlipLabel);
        param->setHint(kParamMirrorFlipHint);
        if (page) {
            page->addChild(*param);
        }
    }
    // flop
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMirrorFlop);
        param->setLabel(kParamMirrorFlopLabel);
        param->setHint(kParamMirrorFlopHint);
        if (page) {
            page->addChild(*param);
        }
    }

    //Transform3x3DescribeInContextEnd(desc, context, page, false);
}

OFX::ImageEffect*
MirrorPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new MirrorPlugin(handle);
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
    {
        static MirrorPluginFactory p(kPluginMirrorIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
