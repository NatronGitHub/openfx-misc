/*
 OFX GodRays plugin.
 
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

#include "GodRays.h"
#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#define kPluginName "GodRaysOFX"
#define kPluginGrouping "Filter"
#define kPluginDescription "God rays."
#define kPluginIdentifier "net.sf.openfx.GodRays"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamFromColor "fromColor"
#define kParamFromColorLabel "From Color"
#define kParamFromColorHint "Color by which the initial image is multiplied."

#define kParamToColor "toColor"
#define kParamToColorLabel "To Color"
#define kParamToColorHint "Color by which the final image is multiplied."

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Gamma space in which the colors are interpolated. Higher values yield brighter intermediate images"

#define kParamSteps "steps"
#define kParamStepsLabel "Steps"
#define kParamStepsHint "The number of intermediate images is 2^steps, i.e. 32 for steps=5."

#define kParamMax "max"
#define kParamMaxLabel "Max"
#define kParamMaxHint "Output the brightest value at each pixel rather than the average."


#define kTransform3x3MotionBlurCount 1000 // number of transforms used in the motion

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class GodRaysPlugin : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    GodRaysPlugin(OfxImageEffectHandle handle)
    : Transform3x3Plugin(handle, true, false, true)
    , _translate(0)
    , _rotate(0)
    , _scale(0)
    , _scaleUniform(0)
    , _skewX(0)
    , _skewY(0)
    , _skewOrder(0)
    , _center(0)
    , _interactive(0)
    , _fromColor(0)
    , _toColor(0)
    , _gamma(0)
    , _steps(0)
    , _max(0)
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
        _fromColor = fetchRGBAParam(kParamFromColor);
        _toColor = fetchRGBAParam(kParamToColor);
        _gamma = fetchRGBAParam(kParamGamma);
        _steps = fetchIntParam(kParamSteps);
        _max = fetchBooleanParam(kParamMax);

        assert(_fromColor && _toColor && _gamma && _steps && _max);
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;

    virtual bool getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;

    void resetCenter(double time);

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    /* internal render function */
    template <class PIX, int nComponents, int maxValue, bool masked>
    void renderInternalForBitDepth(const OFX::RenderArguments &args);

    template <int nComponents, bool masked>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);
    
    /* set up and run a processor (overridden in GodRays) */
    virtual void setupAndProcess(Transform3x3ProcessorBase &, const OFX::RenderArguments &args) OVERRIDE;

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
    RGBAParam* _fromColor;
    RGBAParam* _toColor;
    RGBAParam* _gamma;
    IntParam* _steps;
    BooleanParam* _max;
};

// overridden is identity
bool
GodRaysPlugin::isIdentity(double time)
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
GodRaysPlugin::getInverseTransformCanonical(double time, double amount, bool invert, OFX::Matrix3x3* invtransform) const
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
        scale.x = 1. + (scale.x - 1.) * amount;
        scale.y = 1. + (scale.y - 1.) * amount;
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
GodRaysPlugin::resetCenter(double time)
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
GodRaysPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
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
GodRaysPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        resetCenter(args.time);
    }
}

/* set up and run a processor */
void
GodRaysPlugin::setupAndProcess(Transform3x3ProcessorBase &processor,
                               const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
        ( dst->getRenderScale().y != args.renderScale.y) ||
        ( dst->getField() != args.fieldToRender) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src( _srcClip->fetchImage(time) );
    size_t invtransformsizealloc = 0;
    size_t invtransformsize = 0;
    std::vector<OFX::Matrix3x3> invtransform;
    bool blackOutside = true;
    double mix = 1.;

    if ( !src.get() ) {
        // no source image, use a dummy transform
        invtransformsizealloc = 1;
        invtransform.resize(invtransformsizealloc);
        invtransformsize = 1;
        invtransform[0].a = 0.;
        invtransform[0].b = 0.;
        invtransform[0].c = 0.;
        invtransform[0].d = 0.;
        invtransform[0].e = 0.;
        invtransform[0].f = 0.;
        invtransform[0].g = 0.;
        invtransform[0].h = 0.;
        invtransform[0].i = 1.;
    } else {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }

        bool invert;
        _invert->getValueAtTime(time, invert);

        _blackOutside->getValueAtTime(time, blackOutside);
        if (_masked) {
            _mix->getValueAtTime(time, mix);
        }
        const bool fielded = args.fieldToRender == OFX::eFieldLower || args.fieldToRender == OFX::eFieldUpper;
        const double pixelAspectRatio = src->getPixelAspectRatio();

#pragma message WARN("depend on steps")
        invtransformsizealloc = kTransform3x3MotionBlurCount;
        invtransform.resize(invtransformsizealloc);
        invtransformsize = getInverseTransformsBlur(time, args.renderScale, fielded, pixelAspectRatio, invert, &invtransform.front(), invtransformsizealloc);

        // compose with the input transform
        if ( !src->getTransformIsIdentity() ) {
            double srcTransform[9]; // transform to apply to the source image, in pixel coordinates, from source to destination
            src->getTransform(srcTransform);
            OFX::Matrix3x3 srcTransformMat;
            srcTransformMat.a = srcTransform[0];
            srcTransformMat.b = srcTransform[1];
            srcTransformMat.c = srcTransform[2];
            srcTransformMat.d = srcTransform[3];
            srcTransformMat.e = srcTransform[4];
            srcTransformMat.f = srcTransform[5];
            srcTransformMat.g = srcTransform[6];
            srcTransformMat.h = srcTransform[7];
            srcTransformMat.i = srcTransform[8];
            // invert it
            double det = ofxsMatDeterminant(srcTransformMat);
            if (det != 0.) {
                OFX::Matrix3x3 srcTransformInverse = ofxsMatInverse(srcTransformMat, det);

                for (size_t i = 0; i < invtransformsize; ++i) {
                    invtransform[i] = srcTransformInverse * invtransform[i];
                }
            }
        }
    }

    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask( ( _masked && (getContext() != OFX::eContextFilter) ) ? _maskClip->fetchImage(time) : 0 );

    // do we do masking
    if ( _masked && (getContext() != OFX::eContextFilter) && _maskClip->isConnected() ) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);
    assert(invtransform.size() && invtransformsize);
#pragma message WARN("dynamic cast to a specific processor to set GodRays-specific values")
    processor.setValues(&invtransform.front(),
                        invtransformsize,
                        blackOutside,
                        1.,
                        mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // setupAndProcess


using namespace OFX;

mDeclarePluginFactory(GodRaysPluginFactory, {}, {});


static
void GodRaysPluginDescribeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/, PageParamDescriptor *page)
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
        page->addChild(*param);
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
        page->addChild(*param);
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
        page->addChild(*param);
    }

    // scaleUniform
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformScaleUniform);
        param->setLabel(kParamTransformScaleUniformLabel);
        param->setHint(kParamTransformScaleUniformHint);
        // don't check it by default: it is easy to obtain Uniform scaling using the slider or the interact
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // skewX
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformSkewX);
        param->setLabel(kParamTransformSkewXLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        page->addChild(*param);
    }

    // skewY
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransformSkewY);
        param->setLabel(kParamTransformSkewYLabel);
        param->setDefault(0);
        param->setDisplayRange(-1,1);
        param->setIncrement(0.01);
        page->addChild(*param);
    }

    // skewOrder
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamTransformSkewOrder);
        param->setLabel(kParamTransformSkewOrderLabel);
        param->setDefault(0);
        param->appendOption("XY");
        param->appendOption("YX");
        param->setAnimates(true);
        page->addChild(*param);
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
        page->addChild(*param);
    }

    // resetcenter
    {
        PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamTransformResetCenter);
        param->setLabel(kParamTransformResetCenterLabel);
        param->setHint(kParamTransformResetCenterHint);
        page->addChild(*param);
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransformInteractive);
        param->setLabel(kParamTransformInteractiveLabel);
        param->setHint(kParamTransformInteractiveHint);
        param->setEvaluateOnChange(false);
        page->addChild(*param);
    }
}

void GodRaysPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}

void GodRaysPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    GodRaysPluginDescribeInContext(desc, context, page);

    // invert
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransform3x3Invert);
        param->setLabel(kParamTransform3x3InvertLabel);
        param->setHint(kParamTransform3x3InvertHint);
        param->setDefault(false);
        param->setAnimates(true);
        page->addChild(*param);
    }
    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page, false);

    // fromColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamFromColor);
        param->setLabel(kParamFromColorLabel);
        param->setHint(kParamFromColorHint);
        param->setDefault(1.,1.,1.,1.);
        page->addChild(*param);
    }

    // toColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamToColor);
        param->setLabel(kParamToColorLabel);
        param->setHint(kParamToColorHint);
        param->setDefault(1.,1.,1.,1.);
        page->addChild(*param);
    }

    // gamma
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamGamma);
        param->setLabel(kParamGammaLabel);
        param->setHint(kParamGammaHint);
        param->setDefault(1.,1.,1.,1.);
        param->setDisplayRange(0.2, 0.2, 0.2, 0.2, 5., 5., 5., 5.);
        page->addChild(*param);
    }

    // steps
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamSteps);
        param->setLabel(kParamStepsLabel);
        param->setHint(kParamStepsHint);
        param->setDefault(5);
        param->setDisplayRange(0, 10);
        page->addChild(*param);
    }

    // max
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMax);
        param->setLabel(kParamMaxLabel);
        param->setHint(kParamMaxHint);
        param->setDefault(false);
        page->addChild(*param);
    }

    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* GodRaysPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new GodRaysPlugin(handle);
}


void getGodRaysPluginID(OFX::PluginFactoryArray &ids)
{
    {
        static GodRaysPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
