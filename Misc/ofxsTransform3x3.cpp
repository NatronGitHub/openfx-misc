/*
 OFX Transform3x3 plugin: a base plugin for 2D homographic transform,
 represented by a 3x3 matrix.

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
 */

#include "ofxsTransform3x3.h"
#include "ofxsTransform3x3Processor.h"

using namespace OFX;


Transform3x3Plugin::Transform3x3Plugin(OfxImageEffectHandle handle, bool masked)
: ImageEffect(handle)
, dstClip_(0)
, srcClip_(0)
, maskClip_(0)
, _invert(0)
, _filter(0)
, _clamp(0)
, _blackOutside(0)
, _masked(masked)
, _mix(0)
{
    dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
    assert(dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA);
    srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA);
    // name of mask clip depends on the context
    if (masked) {
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);
    }
    // Transform3x3-GENERIC
    _invert = fetchBooleanParam(kTransform3x3InvertParamName);
    // GENERIC
    _filter = fetchChoiceParam(kFilterTypeParamName);
    _clamp = fetchBooleanParam(kFilterClampParamName);
    _blackOutside = fetchBooleanParam(kFilterBlackOutsideParamName);
    if (masked) {
        _mix = fetchDoubleParam(kFilterMixParamName);
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
Transform3x3Plugin::setupAndProcess(Transform3x3ProcessorBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);

        // Transform3x3-GENERIC
        bool invert;
        // GENERIC
        bool blackOutside;
        double mix = 1.;

        // Transform3x3-GENERIC
        _invert->getValue(invert);

        // GENERIC
        _blackOutside->getValue(blackOutside);
        if (_masked) {
            _mix->getValueAtTime(args.time, mix);
        }

        OFX::Matrix3x3 invtransform;
        bool success = getInverseTransformCanonical(args.time, invert, eGetTransformRender ,&invtransform); // virtual function
        if (!success) {
            invtransform.a = 0.;
            invtransform.b = 0.;
            invtransform.c = 0.;
            invtransform.d = 0.;
            invtransform.e = 0.;
            invtransform.f = 0.;
            invtransform.g = 0.;
            invtransform.h = 0.;
            invtransform.i = 1.;
        }
        processor.setValues(invtransform,
                            srcClip_->getPixelAspectRatio(),
                            args.renderScale, //!< 0.5 for a half-resolution image
                            args.fieldToRender,
                            //(FilterEnum)filter, clamp,
                            blackOutside, mix);
    }

    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((_masked && (getContext() != OFX::eContextFilter)) ? maskClip_->fetchImage(args.time) : 0);

    // do we do masking
    if (_masked && getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get());
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// override the rod call
// Transform3x3-GENERIC
// the RoD should at least contain the region of definition of the source clip,
// which will be filled with black or by continuity.
bool
Transform3x3Plugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);

    ///if is identity return the input rod instead of transforming
    // Transform3x3-GENERIC
    if (isIdentity(args.time)) {
        rod = srcRoD;
        return true;
    }

    // GENERIC
    if (_masked) {
        double mix;
        _mix->getValueAtTime(args.time, mix);
        if (mix == 0.) {
            rod = srcRoD;
            return true;
        }
    }

    bool invert;

    // Transform3x3-GENERIC
    _invert->getValue(invert);
    
    OFX::Matrix3x3 transform;
    bool success = getInverseTransformCanonical(args.time, !invert,eGetTransformRoD, &transform); // RoD is computed using the *DIRECT* transform
    if (!success) {
        return false; // can't compute - use default values
    }

    /// now transform the 4 corners of the source clip to the output image
    OFX::Point3D topLeft = transform * OFX::Point3D(srcRoD.x1,srcRoD.y2,1);
    OFX::Point3D topRight = transform * OFX::Point3D(srcRoD.x2,srcRoD.y2,1);
    OFX::Point3D bottomLeft = transform * OFX::Point3D(srcRoD.x1,srcRoD.y1,1);
    OFX::Point3D bottomRight = transform * OFX::Point3D(srcRoD.x2,srcRoD.y1,1);

    if (topLeft.z == 0. || topRight.z == 0. || bottomLeft.z == 0. || bottomRight.z == 0.) {
        // at least on of the corners is at infinity
        return false;
    }
    topLeft.x /= topLeft.z;
    topLeft.y /= topLeft.z;
    topRight.x /= topRight.z;
    topRight.y /= topRight.z;
    bottomLeft.x /= bottomLeft.z;
    bottomLeft.y /= bottomLeft.z;
    bottomRight.x /= bottomRight.z;
    bottomRight.y /= bottomRight.z;

    double l = std::min(std::min(topLeft.x, bottomLeft.x),std::min(topRight.x,bottomRight.x));
    double b = std::min(std::min(topLeft.y, bottomLeft.y),std::min(topRight.y,bottomRight.y));
    double r = std::max(std::max(topLeft.x, bottomLeft.x),std::max(topRight.x,bottomRight.x));
    double t = std::max(std::max(topLeft.y, bottomLeft.y),std::max(topRight.y,bottomRight.y));

    // GENERIC
    rod.x1 = l;
    rod.x2 = r;
    rod.y1 = b;
    rod.y2 = t;
    assert(rod.x1 < rod.x2 && rod.y1 < rod.y2);

    bool blackOutside;
    _blackOutside->getValue(blackOutside);

    ofxsFilterExpandRoD(this, dstClip_->getPixelAspectRatio(), args.renderScale, blackOutside, &rod);

    // say we set it
    return true;
}


// override the roi call
// Transform3x3-GENERIC
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
Transform3x3Plugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    const OfxRectD roi = args.regionOfInterest;
    bool invert;

    // Transform3x3-GENERIC
    _invert->getValue(invert);

    OFX::Matrix3x3 invtransform;
    bool success = getInverseTransformCanonical(args.time, invert,eGetTransformRoD, &invtransform);
    if (!success) {
        // can't compute inverse transform, use default value (render will probably fail anyway)
        return;
    }

    /// now find the positions in the src clip of the 4 corners of the roi
    OFX::Point3D topLeft = invtransform * OFX::Point3D(roi.x1,roi.y2,1);
    OFX::Point3D topRight = invtransform * OFX::Point3D(roi.x2,roi.y2,1);
    OFX::Point3D bottomLeft = invtransform * OFX::Point3D(roi.x1,roi.y1,1);
    OFX::Point3D bottomRight = invtransform * OFX::Point3D(roi.x2,roi.y1,1);

    if (topLeft.z == 0. || topRight.z == 0. || bottomLeft.z == 0. || bottomRight.z == 0.) {
        // at least on of the corners is at infinity
        return;
    }
    topLeft.x /= topLeft.z;
    topLeft.y /= topLeft.z;
    topRight.x /= topRight.z;
    topRight.y /= topRight.z;
    bottomLeft.x /= bottomLeft.z;
    bottomLeft.y /= bottomLeft.z;
    bottomRight.x /= bottomRight.z;
    bottomRight.y /= bottomRight.z;

    double l = std::min(std::min(topLeft.x, bottomLeft.x),std::min(topRight.x,bottomRight.x));
    double b = std::min(std::min(topLeft.y, bottomLeft.y),std::min(topRight.y,bottomRight.y));
    double r = std::max(std::max(topLeft.x, bottomLeft.x),std::max(topRight.x,bottomRight.x));
    double t = std::max(std::max(topLeft.y, bottomLeft.y),std::max(topRight.y,bottomRight.y));

    // GENERIC
    int filter;
    _filter->getValue(filter);
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    const bool doMasking = _masked && getContext() != OFX::eContextFilter && maskClip_->isConnected();
    double mix = 1.;
    if (_masked) {
        _mix->getValueAtTime(args.time, mix);
    }


    OfxRectD srcRoI;
    srcRoI.x1 = l;
    srcRoI.x2 = r;
    srcRoI.y1 = b;
    srcRoI.y2 = t;
    assert(srcRoI.x1 < srcRoI.x2 && srcRoI.y1 < srcRoI.y2);

    ofxsFilterExpandRoI(roi, srcClip_->getPixelAspectRatio(), args.renderScale, (FilterEnum)filter, doMasking, mix, &srcRoI);


    // set it on the mask only if we are in an interesting context
    // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
    if (doMasking) {
        rois.setRegionOfInterest(*maskClip_, roi);
    }
    rois.setRegionOfInterest(*srcClip_, srcRoI);
}


template <class PIX, int nComponents, int maxValue, bool masked>
void
Transform3x3Plugin::renderInternalForBitDepth(const OFX::RenderArguments &args)
{
    int filter;
    _filter->getValue(filter);
    bool clamp;
    _clamp->getValue(clamp);

    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch ((FilterEnum)filter) {
        case eFilterImpulse:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterImpulse, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterBilinear:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterBilinear, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterCubic:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterCubic, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterKeys:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterKeys, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterKeys, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterSimon:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterSimon, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterSimon, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterRifman:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterRifman, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterRifman, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterMitchell:
            if (clamp) {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterMitchell, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterMitchell, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterParzen:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterParzen, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterNotch:
        {
            Transform3x3Processor<PIX, nComponents, maxValue, masked, eFilterNotch, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
    }
}

// the internal render function
template <int nComponents, bool masked>
void
Transform3x3Plugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
            renderInternalForBitDepth<unsigned char, nComponents, 255, masked>(args);
            break;
        case OFX::eBitDepthUShort :
            renderInternalForBitDepth<unsigned short, nComponents, 65535, masked>(args);
            break;
        case OFX::eBitDepthFloat :
            renderInternalForBitDepth<float, nComponents, 1, masked>(args);
            break;
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
Transform3x3Plugin::render(const OFX::RenderArguments &args)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        if (_masked) {
            renderInternal<4,true>(args, dstBitDepth);
        } else {
            renderInternal<4,false>(args, dstBitDepth);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        if (_masked) {
            renderInternal<3,true>(args, dstBitDepth);
        } else {
            renderInternal<3,false>(args, dstBitDepth);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        if (_masked) {
            renderInternal<1,true>(args, dstBitDepth);
        } else {
            renderInternal<1,false>(args, dstBitDepth);
        }
    }
}

bool Transform3x3Plugin::isIdentity(const RenderArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    // Transform3x3-GENERIC
    if (isIdentity(args.time)) { // let's call the Transform-specific one first
        identityClip = srcClip_;
        identityTime = args.time;
        return true;
    }

    // GENERIC
    if (_masked) {
        double mix;
        _mix->getValueAtTime(args.time, mix);
        if (mix == 0.) {
            identityClip = srcClip_;
            identityTime = args.time;
            return true;
        }
    }
    
    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool Transform3x3Plugin::getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9])
{
    bool invert;

    //std::cout << "getTransform called!" << std::endl;
    // Transform3x3-GENERIC
    _invert->getValueAtTime(args.time, invert);

    OFX::Matrix3x3 invtransform;
    bool success = getInverseTransformCanonical(args.time, invert,eGetTransformRoD, &invtransform);
    if (!success) {
        return false;
    }
    double pixelaspectratio = srcClip_->getPixelAspectRatio();
    bool fielded = args.fieldToRender == eFieldLower || args.fieldToRender == eFieldUpper;
    OFX::Matrix3x3 invtransformpixel = (OFX::ofxsMatCanonicalToPixel(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded) *
                                        invtransform *
                                        OFX::ofxsMatPixelToCanonical(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded));
    transformClip = srcClip_;
    transformMatrix[0] = invtransformpixel.a;
    transformMatrix[1] = invtransformpixel.b;
    transformMatrix[2] = invtransformpixel.c;
    transformMatrix[3] = invtransformpixel.d;
    transformMatrix[4] = invtransformpixel.e;
    transformMatrix[5] = invtransformpixel.f;
    transformMatrix[6] = invtransformpixel.g;
    transformMatrix[7] = invtransformpixel.h;
    transformMatrix[8] = invtransformpixel.i;
    return true;
}
#endif

void OFX::Transform3x3Describe(OFX::ImageEffectDescriptor &desc, bool masked)
{
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    if (masked) {
        desc.addSupportedContext(eContextPaint);
    }
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);

    // Transform3x3-GENERIC

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

#ifdef OFX_EXTENSIONS_NUKE
    if (!masked) {
        // Enable transform by the host.
        // It is only possible for transforms which can be represented as a 3x3 matrix.
        desc.setCanTransform(true);
        if (getImageEffectHostDescription()->canTransform) {
            //std::cout << "kFnOfxImageEffectCanTransform (describe) =" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
        }
    }
#endif
}

OFX::PageParamDescriptor * OFX::Transform3x3DescribeInContextBegin(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, bool masked)
{
    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    if (masked && (context == eContextGeneral || context == eContextPaint)) {
        // GENERIC (MASKED)
        //
        // if general or paint context, define the mask clip
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    return page;
}

void OFX::Transform3x3DescribeInContextEnd(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context, OFX::PageParamDescriptor* page, bool masked)
{

    BooleanParamDescriptor* invert = desc.defineBooleanParam(kTransform3x3InvertParamName);
    invert->setLabels(kTransform3x3InvertParamName, kTransform3x3InvertParamName, kTransform3x3InvertParamName);
    invert->setDefault(false);
    invert->setAnimates(false);
    page->addChild(*invert);

    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page);

    if (masked) {
        // GENERIC (MASKED)
        //
        ofxsFilterDescribeParamsMaskMix(desc, page);
#ifdef OFX_EXTENSIONS_NUKE
    } else if (getImageEffectHostDescription()->canTransform) {
        // Transform3x3-GENERIC (NON-MASKED)
        //
        //std::cout << "kFnOfxImageEffectCanTransform in describeincontext(" << context << ")=" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
#endif
    }

}
