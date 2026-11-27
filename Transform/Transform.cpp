/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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

#include <cmath>
#include <iostream>

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "TransformDefaults.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TransformOFX"
#define kPluginMaskedName "TransformMaskedOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Translate / Rotate / Scale a 2D image.\n" \
    "This plugin concatenates transforms.\n" \
    "See also https://web.archive.org/web/20220627030948/http://www.opticalenquiry.com/nuke/index.php?title=Transform"

#define kPluginMaskedDescription "Translate / Rotate / Scale a 2D image, with optional masking.\n" \
    "This plugin concatenates transforms upstream."
#define kPluginIdentifier "net.sf.openfx.TransformPlugin"
#define kPluginMaskedIdentifier "net.sf.openfx.TransformMaskedPlugin"
#define kPluginDirBlurName "DirBlurOFX"
#define kPluginDirBlurGrouping "Filter"
#define kPluginDirBlurDescription "Apply directional blur to an image.\n" \
    "The output is a weighted average of the image along the path from no transform to the transform given by the parameters, scaled by Amount (from its opposite to it when Centered is checked; Fading sets the weights), so the blur follows that transform:\n" \
    "- zoom blur: set Scale (e.g. 1.1) around Center;\n" \
    "- radial (spin) blur: set Rotate around Center;\n" \
    "- linear blur: set Translate.\n" \
    "This is not the DirBlur node of other compositing applications: it has no blur type, length or angle controls; use the transform parameters instead.\n" \
    "This plugin concatenates transforms upstream."
#define kPluginDirBlurIdentifier "net.sf.openfx.DirBlur"
// History (Transform, TransformMasked, DirBlur):
// version 1.0: initial version
// version 1.1: a transform matrix whose inverse overflows (huge entries, e.g. a concatenated source transform) is reported as not invertible (SupportExt #229)
// SupportExt#179, in the current minor versions: with the Box
//              filter, the thin band just outside the source image is black (Black Outside) or the
//              edge colour, not about 4x the edge value (garbage).
// SupportExt#183-#185, #205 and #207, also in the current minor versions:
//              - TransformMasked, and DirBlur at Motion Blur 0: with Mix < 1 and no mask, the region of definition
//                includes the source, which the output blends with (it was clipped to the
//                transformed region), and the source under the output is fetched (SupportExt#185);
//              - all three: directional blur with Motion Blur 0 renders the full transform, not
//                0.999 of it (SupportExt#184);
//              - TransformMasked, DirBlur: the Mask checkbox works on hosts where the mask always
//                looks connected (DaVinci Resolve, SupportExt#183);
//              - Transform, TransformMasked: the Shutter can be up to 1000 frames and the custom
//                shutter offset +-1000 (they were limited to 2 and +-1, SupportExt#205; part of
//                openfx-misc#273), and a longer, infinite or NaN shutter gives an infinite region
//                of definition (SupportExt#207).
// #66, also in the current minor versions: on hosts that ignore normalised defaults (OFX 1.1 hosts
//              such as VEGAS, and DaVinci Resolve), the default center is the project centre, not the
//              bottom left corner. In existing projects on those hosts, an untouched center moves to
//              the image centre, which changes the output.
// 1.1 (Transform, TransformMasked, DirBlur): the Lanczos4, Lanczos6 and Sinc4 filters (appended options) and the
//      Prefilter Downscaling checkbox (after Black Outside, off) (issue #270); the default output is unchanged.
// 2.0 (Transform, TransformMasked): Prefilter Downscaling is on by default (issue #270), so that scaling down
//      filters the image instead of aliasing; version 1 is obsolete, with the same output as before.
//      DirBlur stays at version 1 (Prefilter Downscaling off by default).
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMajorPrefilterOff 1 // the obsolete version, Prefilter Downscaling off by default
#define kPluginVersionMinorPrefilterOff 1
#define kPluginDirBlurVersionMajor 1
#define kPluginDirBlurVersionMinor 1

#define kParamSrcClipChanged "srcClipChanged"


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle,
                    bool masked,
                    bool isDirBlur)
        : Transform3x3Plugin(handle, masked, isDirBlur ? eTransform3x3ParamsTypeDirBlur : eTransform3x3ParamsTypeMotionBlur)
        , _translate(NULL)
        , _rotate(NULL)
        , _scale(NULL)
        , _scaleUniform(NULL)
        , _skewX(NULL)
        , _skewY(NULL)
        , _skewOrder(NULL)
        , _transformAmount(NULL)
        , _center(NULL)
        , _interactive(NULL)
        , _srcClipChanged(NULL)
    {
        // NON-GENERIC
        if (isDirBlur) {
            _dirBlurAmount = fetchDoubleParam(kParamTransform3x3DirBlurAmount);
            _dirBlurCentered = fetchBooleanParam(kParamTransform3x3DirBlurCentered);
            _dirBlurFading = fetchDoubleParam(kParamTransform3x3DirBlurFading);
        }

        _translate = fetchDouble2DParam(kParamTransformTranslateOld);
        _rotate = fetchDoubleParam(kParamTransformRotateOld);
        _scale = fetchDouble2DParam(kParamTransformScaleOld);
        _scaleUniform = fetchBooleanParam(kParamTransformScaleUniformOld);
        _skewX = fetchDoubleParam(kParamTransformSkewXOld);
        _skewY = fetchDoubleParam(kParamTransformSkewYOld);
        _skewOrder = fetchChoiceParam(kParamTransformSkewOrderOld);
        if (!isDirBlur) {
            _transformAmount = fetchDoubleParam(kParamTransformAmount);
        }
        _center = fetchDouble2DParam(kParamTransformCenterOld);
        _centerChanged = fetchBooleanParam(kParamTransformCenterChanged);
        _interactive = fetchBooleanParam(kParamTransformInteractiveOld);
        assert(_translate && _rotate && _scale && _scaleUniform && _skewX && _skewY && _skewOrder && _center && _interactive);
        _srcClipChanged = fetchBooleanParam(kParamSrcClipChanged);
        assert(_srcClipChanged);
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_scaleUniform->getValue() &&
             ( _scaleUniform->getNumKeys() == 0) ) {
            _scaleUniform->setIsSecretAndDisabled(true);
        }
        // last, since setValue() may call changedParam()
        transformDenormaliseDefaultsKeepCenterChanged(*this, *_centerChanged);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;

    void resetCenter(double time);

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // NON-GENERIC
    Double2DParam* _translate;
    DoubleParam* _rotate;
    Double2DParam* _scale;
    BooleanParam* _scaleUniform;
    DoubleParam* _skewX;
    DoubleParam* _skewY;
    ChoiceParam* _skewOrder;
    DoubleParam* _transformAmount;
    Double2DParam* _center;
    BooleanParam* _centerChanged;
    BooleanParam* _interactive;
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
};

// overridden is identity
bool
TransformPlugin::isIdentity(double time)
{
    // NON-GENERIC
    if (_paramsType != eTransform3x3ParamsTypeDirBlur) {
        double amount = _transformAmount->getValueAtTime(time);
        if (amount == 0.) {
            return true;
        }
    }

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

    if ( (scale.x == 1.) && (scale.y == 1.) && (translate.x == 0.) && (translate.y == 0.) && (rotate == 0.) && (skewX == 0.) && (skewY == 0.) ) {
        return true;
    }

    return false;
}

bool
TransformPlugin::getInverseTransformCanonical(double time,
                                              int /*view*/,
                                              double amount,
                                              bool invert,
                                              Matrix3x3* invtransform) const
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
        scaleUniform = _scaleUniform->getValueAtTime(time);
    }
    double rotate = 0.;
    if (_rotate) {
        rotate = _rotate->getValueAtTime(time);
    }
    double skewX = 0.;
    if (_skewX) {
        skewX = _skewX->getValueAtTime(time);
    }
    double skewY = 0.;
    if (_skewY) {
        skewY = _skewY->getValueAtTime(time);
    }
    int skewOrder = 0;
    if (_skewOrder) {
        skewOrder = _skewOrder->getValueAtTime(time);
    }
    if (_transformAmount) {
        amount *= _transformAmount->getValueAtTime(time);
    }

    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    if (amount != 1.) {
        translate.x *= amount;
        translate.y *= amount;
        if (scale.x <= 0. || amount <= 0.) {
            // linear interpolation
            scale.x = 1. + (scale.x - 1.) * amount;
        } else {
            // geometric interpolation
            scale.x = std::pow(scale.x, amount);
        }
        if (scale.y <= 0 || amount <= 0.) {
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

    double rot = ofxsToRadians(rotate);
    if (!invert) {
        *invtransform = ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    } else {
        *invtransform = ofxsMatTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    }

    return true;
} // TransformPlugin::getInverseTransformCanonical

void
TransformPlugin::resetCenter(double time)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    OfxRectD rod = _srcClip->getRegionOfDefinition(time);
    if ( (rod.x1 <= kOfxFlagInfiniteMin) || (kOfxFlagInfiniteMax <= rod.x2) ||
         ( rod.y1 <= kOfxFlagInfiniteMin) || ( kOfxFlagInfiniteMax <= rod.y2) ) {
        return;
    }
    if ( Coords::rectIsEmpty(rod) ) {
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
    double rot = ofxsToRadians(currentRotation);
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

    Matrix3x3 Rinv = ( ofxsMatRotation(-rot) *
                       ofxsMatSkewXY(skewX, skewY, skewOrder) *
                       ofxsMatScale(scale.x, scale.y) );
    OfxPointD newCenter;
    newCenter.x = (rod.x1 + rod.x2) / 2;
    newCenter.y = (rod.y1 + rod.y2) / 2;
    if (newCenter.x != center.x || newCenter.y != center.y) {
        bool editBlockNecessary = false;
        OfxPointD newTranslate = {0., 0.};
        if (_translate) {
            double dxrot = newCenter.x - center.x;
            double dyrot = newCenter.y - center.y;
            Point3D dRot;
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
            newTranslate.x = translate.x + dx - dxrot;
            newTranslate.y = translate.y + dy - dyrot;
            if (newTranslate.x != translate.x || newTranslate.y != translate.y) {
                editBlockNecessary = true;
            }
        }
        EditBlock eb(*this, "resetCenter", editBlockNecessary);
        if (_center) {
            _center->setValue(newCenter.x, newCenter.y);
        }
        if (_translate) {
            _translate->setValue(newTranslate.x, newTranslate.y);
        }
    }
} // TransformPlugin::resetCenter

void
TransformPlugin::changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName)
{
    if (paramName == kParamTransformResetCenterOld) {
        resetCenter(args.time);
        // Only set if necessary
        if (_centerChanged->getValue()) {
            _centerChanged->setValue(false);
        }
    } else if ( (paramName == kParamTransformTranslateOld) ||
                ( paramName == kParamTransformRotateOld) ||
                ( paramName == kParamTransformScaleOld) ||
                ( paramName == kParamTransformScaleUniformOld) ||
                ( paramName == kParamTransformSkewXOld) ||
                ( paramName == kParamTransformSkewYOld) ||
                ( paramName == kParamTransformSkewOrderOld) ||
                ( paramName == kParamTransformCenterOld) ) {
        if ( (paramName == kParamTransformCenterOld) &&
             ( (args.reason == eChangeUserEdit) || (args.reason == eChangePluginEdit) ) ) {
            // Only set if necessary
            if (!_centerChanged->getValue()) {
                _centerChanged->setValue(true);
            }
        }
        changedTransform(args);
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        // Only set if necessary
        if (!_srcClipChanged->getValue()) {
            _srcClipChanged->setValue(true);
        }
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
TransformPlugin::changedClip(const InstanceChangedArgs &args,
                             const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_centerChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        resetCenter(args.time);
    }
}

// Prefilter Downscaling (kParamFilterPrefilter, defined by Transform3x3DescribeInContextEnd) is on by
// default from version 2 of Transform and TransformMasked (issue #270).
static
void
TransformPluginDescribePrefilterDefault(ImageEffectDescriptor &desc,
                                        unsigned int majorVersion)
{
    BooleanParamDescriptor* param = dynamic_cast<BooleanParamDescriptor*>( desc.getParamDescriptor(kParamFilterPrefilter) );

    assert(param);
    if (param) {
        param->setDefault(majorVersion > kPluginVersionMajorPrefilterOff);
    }
}

mDeclarePluginFactoryVersioned(TransformPluginFactory, {ofxsThreadSuiteCheck();}, {});
static
void
TransformPluginDescribeInContext(ImageEffectDescriptor &desc,
                                 ContextEnum /*context*/,
                                 PageParamDescriptor *page)
{
    // NON-GENERIC PARAMETERS
    //
    ofxsTransformDescribeParams(desc, page, NULL, /*isOpen=*/ true, /*oldParams=*/ true, /*hasAmount=*/ true, /*noTranslate=*/ false);
}

template<unsigned int majorVersion>
void
TransformPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, false);

    ofxsSetOverlayInteractDescriptorV2(desc, new TransformOverlayDescriptorOldParamsV2);
}

template<unsigned int majorVersion>
void
TransformPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, false, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur, eTransform3x3FilteringSincPrefilter); // opt-in (issue #270)
    TransformPluginDescribePrefilterDefault(desc, majorVersion);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

template<unsigned int majorVersion>
ImageEffect*
TransformPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                     ContextEnum /*context*/)
{
    return new TransformPlugin(handle, false, false);
}

mDeclarePluginFactoryVersioned(TransformMaskedPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
TransformMaskedPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMaskedName);
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginMaskedDescription);

    Transform3x3Describe(desc, true);

    ofxsSetOverlayInteractDescriptorV2(desc, new TransformOverlayDescriptorOldParamsV2);
}

template<unsigned int majorVersion>
void
TransformMaskedPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                              ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, Transform3x3Plugin::eTransform3x3ParamsTypeMotionBlur, eTransform3x3FilteringSincPrefilter); // opt-in (issue #270)
    TransformPluginDescribePrefilterDefault(desc, majorVersion);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

template<unsigned int majorVersion>
ImageEffect*
TransformMaskedPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                           ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true, false);
}

mDeclarePluginFactory(DirBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DirBlurPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginDirBlurName);
    desc.setPluginGrouping(kPluginDirBlurGrouping);
    desc.setPluginDescription(kPluginDirBlurDescription);

    Transform3x3Describe(desc, true);

    ofxsSetOverlayInteractDescriptorV2(desc, new TransformOverlayDescriptorOldParamsV2);
}

void
DirBlurPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    TransformPluginDescribeInContext(desc, context, page);

    Transform3x3DescribeInContextEnd(desc, context, page, true, Transform3x3Plugin::eTransform3x3ParamsTypeDirBlur, eTransform3x3FilteringSincPrefilter); // opt-in (issue #270)

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect*
DirBlurPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new TransformPlugin(handle, true, true);
}

static TransformPluginFactory<kPluginVersionMajor> p1(kPluginIdentifier, kPluginVersionMinor);
static TransformMaskedPluginFactory<kPluginVersionMajor> p2(kPluginMaskedIdentifier, kPluginVersionMinor);
static DirBlurPluginFactory p3(kPluginDirBlurIdentifier, kPluginDirBlurVersionMajor, kPluginDirBlurVersionMinor);
static TransformPluginFactory<kPluginVersionMajorPrefilterOff> p1v1(kPluginIdentifier, kPluginVersionMinorPrefilterOff);
static TransformMaskedPluginFactory<kPluginVersionMajorPrefilterOff> p2v1(kPluginMaskedIdentifier, kPluginVersionMinorPrefilterOff);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p1v1)
mRegisterPluginFactoryInstance(p2v1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
