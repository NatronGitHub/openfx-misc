//
//  CImgFilter.h
//  Misc
//
//  Created by Frédéric Devernay on 09/10/2014.
//  Copyright (c) 2014 OpenFX. All rights reserved.
//

#ifndef Misc_CImgFilter_h
#define Misc_CImgFilter_h

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsPixelProcessor.h"
#include "ofxsCopier.h"
#include "ofxsMerging.h"

#include <cassert>
#include <memory>

#define cimg_display 0
#include <CImg.h>

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component"

template <class Params>
class CImgFilterPluginHelper : public OFX::ImageEffect
{
public:

    CImgFilterPluginHelper(OfxImageEffectHandle handle,
                           bool supportsTiles,
                           bool supportsMultiResolution,
                           bool supportsRenderScale)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    , _supportsTiles(supportsTiles)
    , _supportsMultiResolution(supportsMultiResolution)
    , _supportsRenderScale(supportsRenderScale)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == OFX::ePixelComponentRGB || dstClip_->getPixelComponents() == OFX::ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == OFX::ePixelComponentRGB || srcClip_->getPixelComponents() == OFX::ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == OFX::ePixelComponentAlpha);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip* &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL
    {
        if (clipName == kOfxImageEffectSimpleSourceClipName && srcClip_ && args.reason == OFX::eChangeUserEdit) {
            switch (srcClip_->getPreMultiplication()) {
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
            switch (srcClip_->getPixelComponents()) {
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
                    _processA->setValue(false);
                    break;
                default:
                    break;
            }
        }
    }

    // the following functions can be overridden/implemented by the plugin

    virtual void getValuesAtTime(double time, Params& params) = 0;

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    virtual void getRoI(const OfxRectI rect, const OfxPointD& renderScale, const Params& params, OfxRectI* roi) = 0;

    virtual void render(const OFX::RenderArguments &args, const Params& params, int x1, int y1,cimg_library::CImg<float>& cimg) = 0;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const Params& /*params*/) { return false; };


    //static void describe(OFX::ImageEffectDescriptor &desc, bool supportsTiles);

    static OFX::PageParamDescriptor*
    describeInContextBegin(OFX::ImageEffectDescriptor &desc,
                           OFX::ContextEnum context,
                           bool supportsRGBA,
                           bool supportsRGB,
                           bool supportsAlpha,
                           bool supportsTiles)
    {
        OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        if (supportsRGBA) {
            srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        }
        if (supportsRGB) {
            srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
        }
        if (supportsAlpha) {
            srcClip->addSupportedComponent(OFX::ePixelComponentAlpha);
        }
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(supportsTiles);
        srcClip->setIsMask(false);

        OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
        if (supportsRGBA) {
            dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
        }
        if (supportsRGB) {
            dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
        }
        if (supportsAlpha) {
            dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
        }
        dstClip->setSupportsTiles(supportsTiles);
        
        if (context == OFX::eContextGeneral || context == OFX::eContextPaint) {
            OFX::ClipDescriptor *maskClip = context == OFX::eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
            maskClip->addSupportedComponent(OFX::ePixelComponentAlpha);
            maskClip->setTemporalClipAccess(false);
            if (context == OFX::eContextGeneral) {
                maskClip->setOptional(true);
            }
            maskClip->setSupportsTiles(supportsTiles);
            maskClip->setIsMask(true);
        }

        // create the params
        OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
            param->setLabels(kParamProcessRLabel, kParamProcessRLabel, kParamProcessRLabel);
            param->setHint(kParamProcessRHint);
            param->setDefault(true);
            param->setLayoutHint(OFX::eLayoutHintNoNewLine);
            page->addChild(*param);
        }
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
            param->setLabels(kParamProcessGLabel, kParamProcessGLabel, kParamProcessGLabel);
            param->setHint(kParamProcessGHint);
            param->setDefault(true);
            param->setLayoutHint(OFX::eLayoutHintNoNewLine);
            page->addChild(*param);
        }
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam( kParamProcessB );
            param->setLabels(kParamProcessBLabel, kParamProcessBLabel, kParamProcessBLabel);
            param->setHint(kParamProcessBHint);
            param->setDefault(true);
            param->setLayoutHint(OFX::eLayoutHintNoNewLine);
            page->addChild(*param);
        }
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam( kParamProcessA );
            param->setLabels(kParamProcessALabel, kParamProcessALabel, kParamProcessALabel);
            param->setHint(kParamProcessAHint);
            param->setDefault(false);
            page->addChild(*param);
        }

        return page;
    }

    static void
    describeInContextEnd(OFX::ImageEffectDescriptor &desc,
                         OFX::ContextEnum /*context*/,
                         OFX::PageParamDescriptor* page)
    {
        ofxsPremultDescribeParams(desc, page);
        ofxsMaskMixDescribeParams(desc, page);
    }

private:
    void
    setupAndFill(OFX::PixelProcessorFilterBase & processor,
                 const OfxRectI &renderWindow,
                 void *dstPixelData,
                 const OfxRectI& dstBounds,
                 OFX::PixelComponentEnum dstPixelComponents,
                 OFX::BitDepthEnum dstPixelDepth,
                 int dstRowBytes);

    void
    setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                 double time,
                 const OfxRectI &renderWindow,
                 const void *srcPixelData,
                 const OfxRectI& srcBounds,
                 OFX::PixelComponentEnum srcPixelComponents,
                 OFX::BitDepthEnum srcBitDepth,
                 int srcRowBytes,
                 void *dstPixelData,
                 const OfxRectI& dstBounds,
                 OFX::PixelComponentEnum dstPixelComponents,
                 OFX::BitDepthEnum dstPixelDepth,
                 int dstRowBytes,
                 bool premult,
                 int premultChannel,
                 double mix,
                 bool maskInvert);


    // utility functions
    static bool
    isEmpty(const OfxRectI& r)
    {
        return r.x1 >= r.x2 || r.y1 >= r.y2;
    }

    static
    bool
    maskLineIsZero(const OFX::Image* mask, int x1, int x2, int y, bool maskInvert)
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

    static
    bool
    maskColumnIsZero(const OFX::Image* mask, int x, int y1, int y2, bool maskInvert)
    {
        assert(mask->getPixelComponents() == OFX::ePixelComponentAlpha && mask->getPixelDepth() == OFX::eBitDepthFloat);
        const int rowElems = mask->getRowBytes() / sizeof(float);

        if (maskInvert) {
            if (!mask) {
                return false;
            }
            const OfxRectI& maskBounds = mask->getBounds();
            // if part of the column is out of maskbounds, then mask is 1 at these places
            if (x < maskBounds.x1 || maskBounds.x2 <= x || y1 < maskBounds.y1 || maskBounds.y2 <= y2) {
                return false;
            }
            // the whole column is within the mask
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y1));
            assert(p);
            for (int y = y1; y < y2; ++y) {
                const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y));
                if (p && *p != 1.) {
                    return false;
                }
            }
        } else {
            if (!mask) {
                return true;
            }
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

    static void
    toPixelEnclosing(const OfxRectD& regionOfInterest,
                     const OfxPointD& renderScale,
                     double par,
                     OfxRectI *rect)
    {
        rect->x1 = std::floor(regionOfInterest.x1 * renderScale.x / par);
        rect->y1 = std::floor(regionOfInterest.y1 * renderScale.y);
        rect->x2 = std::ceil(regionOfInterest.x2 * renderScale.x / par);
        rect->y2 = std::ceil(regionOfInterest.y2 * renderScale.y);
    }

    static void
    toCanonical(const OfxRectI& rect,
                const OfxPointD& renderScale,
                double par,
                OfxRectD *regionOfInterest)
    {
        regionOfInterest->x1 = rect.x1 * par / renderScale.x;
        regionOfInterest->y1 = rect.y1 / renderScale.y;
        regionOfInterest->x2 = rect.x2 * par / renderScale.x;
        regionOfInterest->y2 = rect.y2 / renderScale.y;
    }

    static void
    enlargeRectI(const OfxRectI& rect,
                 int delta_pix,
                 const OfxRectI& bounds,
                 OfxRectI* rectOut)
    {
        rectOut->x1 = std::max(bounds.x1, rect.x1 - delta_pix);
        rectOut->x2 = std::min(bounds.x2, rect.x2 + delta_pix);
        rectOut->y1 = std::max(bounds.y1, rect.y1 - delta_pix);
        rectOut->y2 = std::min(bounds.y2, rect.y2 + delta_pix);
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    // params
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;

    bool _supportsTiles;
    bool _supportsMultiResolution;
    bool _supportsRenderScale;
};


/* set up and run a copy processor */
template <class Params>
void
CImgFilterPluginHelper<Params>::setupAndFill(OFX::PixelProcessorFilterBase & processor,
                                             const OfxRectI &renderWindow,
                                             void *dstPixelData,
                                             const OfxRectI& dstBounds,
                                             OFX::PixelComponentEnum dstPixelComponents,
                                             OFX::BitDepthEnum dstPixelDepth,
                                             int dstRowBytes)
{
    assert(dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


/* set up and run a copy processor */
template <class Params>
void
CImgFilterPluginHelper<Params>::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                                             double time,
                                             const OfxRectI &renderWindow,
                                             const void *srcPixelData,
                                             const OfxRectI& srcBounds,
                                             OFX::PixelComponentEnum srcPixelComponents,
                                             OFX::BitDepthEnum srcBitDepth,
                                             int srcRowBytes,
                                             void *dstPixelData,
                                             const OfxRectI& dstBounds,
                                             OFX::PixelComponentEnum dstPixelComponents,
                                             OFX::BitDepthEnum dstPixelDepth,
                                             int dstRowBytes,
                                             bool premult,
                                             int premultChannel,
                                             double mix,
                                             bool maskInvert)
{
    assert(srcPixelData &&
           srcBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= srcBounds.x2 &&
           srcBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= srcBounds.y2 &&
           dstPixelData &&
           dstBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= dstBounds.x2 &&
           dstBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= dstBounds.y2);
    // make sure bit depths are sane
    if(srcBitDepth != dstPixelDepth/* || srcPixelComponents != dstPixelComponents*/) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    if (isEmpty(renderWindow)) {
        return;
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(time) : 0);
    std::auto_ptr<OFX::Image> orig(srcClip_->fetchImage(time));
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


template <class Params>
void
CImgFilterPluginHelper<Params>::render(const OFX::RenderArguments &args)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    const OfxPointD& renderScale = args.renderScale;
    const OfxRectI& renderWindow = args.renderWindow;
    const OFX::FieldEnum fieldToRender = args.fieldToRender;

    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != renderScale.x ||
        dst->getRenderScale().y != renderScale.y ||
        dst->getField() != fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstPixelComponents  = dst->getPixelComponents();
    assert(dstBitDepth == OFX::eBitDepthFloat); // only float is supported for now (others are untested)

    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcPixelComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcPixelComponents != dstPixelComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (src->getRenderScale().x != renderScale.x ||
            src->getRenderScale().y != renderScale.y ||
            src->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        // src is considered black and transparent, just fill black to dst and return
        void* dstPixelData = NULL;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dst.get(), &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);

        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::BlackFiller<float, 4> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::BlackFiller<float, 3> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::BlackFiller<float, 1> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch

        return;
    }

    const void *srcPixelData = src->getPixelData();
    const OfxRectI srcBounds = src->getBounds();
    const OfxRectI srcRod = src->getRegionOfDefinition();
    const OFX::PixelComponentEnum srcPixelComponents = src->getPixelComponents();
    const OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
    //srcPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
    const int srcRowBytes = src->getRowBytes();

    void *dstPixelData = dst->getPixelData();
    const OfxRectI dstBounds = dst->getBounds();
    const OfxRectI dstRod = dst->getRegionOfDefinition();
    //const OFX::PixelComponentEnum dstPixelComponents = dst->getPixelComponents();
    //const OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    //dstPixelBytes = getPixelBytes(dstPixelComponents, dstBitDepth);
    const int dstRowBytes = dst->getRowBytes();

    if (!_supportsTiles) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
        //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
        assert(srcRod.x1 == srcBounds.x1);
        assert(srcRod.x2 == srcBounds.x2);
        assert(srcRod.y1 == srcBounds.y1);
        assert(srcRod.y2 == srcBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        assert(dstRod.x1 == dstBounds.x1);
        assert(dstRod.x2 == dstBounds.x2);
        assert(dstRod.y1 == dstBounds.y1);
        assert(dstRod.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
    }
    if (!_supportsMultiResolution) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
        //   Multiple resolution images mean...
        //    input and output images can be of any size
        //    input and output images can be offset from the origin
        assert(srcRod.x1 == 0);
        assert(srcRod.y1 == 0);
        assert(srcRod.x1 == dstRod.x1);
        assert(srcRod.x2 == dstRod.x2);
        assert(srcRod.y1 == dstRod.y1);
        assert(srcRod.y2 == dstRod.y2); // crashes on Natron if kSupportsMultiResolution=0
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(time, maskInvert);
    if (!processR && !processG && !processB) {
        // no need to (un)premult if we don't change colors
        premult = false;
    }

    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(time) : 0);
    OfxRectI processWindow = renderWindow; //!< the window where pixels have to be computed (may be smaller than renderWindow if mask is zero on the borders)

    if (mix == 0.) {
        // no processing at all
        processWindow.x2 = processWindow.x1;
        processWindow.y2 = processWindow.y1;
    }
    if (mask.get()) {
        if (mask->getRenderScale().x != renderScale.x ||
            mask->getRenderScale().y != renderScale.y ||
            mask->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }


        // shrink the processWindow at much as possible
        // top
        while (processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y2-1, maskInvert)) {
            --processWindow.y2;
        }
        // bottom
        while (processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y1, maskInvert)) {
            ++processWindow.y1;
        }
        // left
        while (processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x1, processWindow.y1, processWindow.y2, maskInvert)) {
            ++processWindow.x1;
        }
        // right
        while (processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x2-1, processWindow.y1, processWindow.y2, maskInvert)) {
            --processWindow.x2;
        }
    }

    // copy areas of renderWindow that are not within processWindow to dst

    OfxRectI copyWindowN, copyWindowS, copyWindowE, copyWindowW;
    // top
    copyWindowN.x1 = renderWindow.x1;
    copyWindowN.x2 = renderWindow.x2;
    copyWindowN.y1 = processWindow.y2;
    copyWindowN.y2 = renderWindow.y2;
    // bottom
    copyWindowS.x1 = renderWindow.x1;
    copyWindowS.x2 = renderWindow.x2;
    copyWindowS.y1 = renderWindow.y1;
    copyWindowS.y2 = processWindow.y1;
    // left
    copyWindowW.x1 = renderWindow.x1;
    copyWindowW.x2 = processWindow.x1;
    copyWindowW.y1 = processWindow.y1;
    copyWindowW.y2 = processWindow.y2;
    // right
    copyWindowE.x1 = processWindow.x2;
    copyWindowE.x2 = renderWindow.x2;
    copyWindowE.y1 = processWindow.y1;
    copyWindowE.y2 = processWindow.y2;
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopier<float, 4, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } // switch

    if (isEmpty(processWindow)) {
        // the area that actually has to be processed is empty, the job is finished!
        return;
    }
    assert(mix != 0.); // mix == 0. should give an empty processWindow

    const bool doMasking = getContext() != OFX::eContextFilter && maskClip_->isConnected();

    Params params;
    getValuesAtTime(time, params);

    // compute the src ROI (must be consistent with getRegionsOfInterest())
    OfxRectI srcRoI;
    getRoI(processWindow, renderScale, params, &srcRoI);
    OfxRectI srcRoD = src->getRegionOfDefinition();
    OFX::MergeImages2D::rectIntersection(srcRoI, srcRoD, &srcRoI);
    // the resulting ROI should be within the src bounds, or it means that the host didn't take into account the region of interest (see getRegionsOfInterest() )
    assert(srcBounds.x1 <= srcRoI.x1 && srcRoI.x2 <= srcBounds.x2 &&
           srcBounds.y1 <= srcRoI.y1 && srcRoI.y2 <= srcBounds.y2);
    if (srcBounds.x1 > srcRoI.x1 || srcRoI.x2 > srcBounds.x2 ||
        srcBounds.y1 > srcRoI.y1 || srcRoI.y2 > srcBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    if (doMasking && mix != 1.) {
        // the renderWindow should also be contained within srcBounds, since we are mixing
        assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x2 <= srcBounds.x2 &&
               srcBounds.y1 <= renderWindow.y1 && renderWindow.y2 <= srcBounds.y2);
        if (srcBounds.x1 > renderWindow.x1 || renderWindow.x2 > srcBounds.x2 ||
            srcBounds.y1 > renderWindow.y1 || renderWindow.y2 > srcBounds.y2) {
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    int srcNComponents = ((srcPixelComponents == OFX::ePixelComponentAlpha) ? 1 :
                          ((srcPixelComponents == OFX::ePixelComponentRGB) ? 3 : 4));

    // from here on, we do the following steps:
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)
    // 3- process the cimg
    // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    //////////////////////////////////////////////////////////////////////////////////////////
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI

    const OfxRectI tmpBounds = srcRoI;
    const OFX::PixelComponentEnum tmpPixelComponents = srcPixelComponents;
    const OFX::BitDepthEnum tmpBitDepth = OFX::eBitDepthFloat;
    const int tmpWidth = tmpBounds.x2 - tmpBounds.x1;
    const int tmpHeight = tmpBounds.y2 - tmpBounds.y1;
    const int tmpRowBytes = getPixelBytes(tmpPixelComponents, tmpBitDepth) * tmpWidth;
    size_t tmpSize = tmpRowBytes * tmpHeight;

    assert(tmpSize > 0);
    std::auto_ptr<OFX::ImageMemory> tmpData(new OFX::ImageMemory(tmpSize, this));
    float *tmpPixelData = (float*)tmpData->lock();

    if (srcPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else if (srcPixelComponents == OFX::ePixelComponentRGB) {
        // just copy, no premult
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else {
        // just copy, no premult
        assert(srcPixelComponents == OFX::ePixelComponentAlpha);
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     premult, premultChannel, mix, maskInvert);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)

    // allocate the cimg data to hold the src ROI
    const int cimgSpectrum = ((srcPixelComponents == OFX::ePixelComponentAlpha) ? (int)processA :
                              ((srcPixelComponents == OFX::ePixelComponentRGB) ? ((int)processR + (int)processG + (int) processB) :
                               ((int)processR + (int)processG + (int) processB + (int)processA)));
    const int cimgWidth = srcRoI.x2 - srcRoI.x1;
    const int cimgHeight = srcRoI.y2 - srcRoI.y1;
    const size_t cimgSize = cimgWidth * cimgHeight * cimgSpectrum * sizeof(float);
    std::vector<int> srcChannel(cimgSpectrum, -1);
    std::vector<int> cimgChannel(srcNComponents, -1);

    if (srcNComponents == 1) {
        if (processA) {
            assert(cimgSpectrum == 1);
            srcChannel[0] = 0;
            cimgChannel[0] = 0;
        } else {
            assert(cimgSpectrum == 0);
        }
    } else {
        int c = 0;
        if (processR) {
            srcChannel[c] = 0;
            ++c;
        }
        if (processG) {
            srcChannel[c] = 1;
            ++c;
        }
        if (processB) {
            srcChannel[c] = 2;
            ++c;
        }
        if (processA && srcNComponents >= 4) {
            srcChannel[c] = 3;
            ++c;
        }
        assert(c == cimgSpectrum);
    }

    if (cimgSize) { // may be zero if no channel is processed
        std::auto_ptr<OFX::ImageMemory> cimgData(new OFX::ImageMemory(cimgSize, this));
        float *cimgPixelData = (float*)cimgData->lock();
        cimg_library::CImg<float> cimg(cimgPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);


        for (int c=0; c < cimgSpectrum; ++c) {
            float *dst = cimg.data(0,0,0,c);
            const float *src = tmpPixelData + srcChannel[c];
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += srcNComponents, ++dst) {
                *dst = *src;
            }
        }

        //////////////////////////////////////////////////////////////////////////////////////////
        // 3- process the cimg

        render(args, params, srcRoI.x1, srcRoI.y1, cimg);

        //////////////////////////////////////////////////////////////////////////////////////////
        // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied

        // We copy the whole srcRoI. This could be optimized to copy only renderWindow
        for (int c=0; c < cimgSpectrum; ++c) {
            const float *src = cimg.data(0,0,0,c);
            float *dst = tmpPixelData + srcChannel[c];
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, ++src, dst += srcNComponents) {
                *dst = *src;
            }
        }

    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    if (srcPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopierPremultMaskMix<float, 4, 1, float, 4, 1> fred(*this);
        setupAndCopy(fred, time, processWindow,
                     tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else if (srcPixelComponents == OFX::ePixelComponentRGB) {
        // just copy, no premult
        if (doMasking) {
            OFX::PixelCopierMaskMix<float, 3, 1, true> fred(*this);
            setupAndCopy(fred, time, processWindow,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        } else {
            OFX::PixelCopierMaskMix<float, 3, 1, false> fred(*this);
            setupAndCopy(fred, time, processWindow,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        }
    } else {
        // just copy, no premult
        assert(srcPixelComponents == OFX::ePixelComponentAlpha);
        if (doMasking) {
            OFX::PixelCopierMaskMix<float, 1, 1, true> fred(*this);
            setupAndCopy(fred, time, processWindow,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        } else {
            OFX::PixelCopierMaskMix<float, 1, 1, false> fred(*this);
            setupAndCopy(fred, time, processWindow,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                         dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                         premult, premultChannel, mix, maskInvert);
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // done!
}



// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
template <class Params>
void
CImgFilterPluginHelper<Params>::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    const OfxRectD& regionOfInterest = args.regionOfInterest;
    OfxRectD srcRoI;

    double mix = 1.;
    const bool doMasking = getContext() != OFX::eContextFilter && maskClip_->isConnected();
    if (doMasking) {
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            // identity transform
            //srcRoI = regionOfInterest;
            //rois.setRegionOfInterest(*srcClip_, srcRoI);
            return;
        }
    }

    Params params;
    getValuesAtTime(args.time, params);

    double pixelaspectratio = srcClip_->getPixelAspectRatio();

    OfxRectI rectPixel;
    toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
    getRoI(rectPixel, args.renderScale, params, &rectPixel);
    toCanonical(rectPixel, args.renderScale, pixelaspectratio, &srcRoI);

    if (doMasking && mix != 1.) {
        // for masking or mixing, we also need the source image.
        // compute the bounding box with the default ROI
        OFX::MergeImages2D::rectBoundingBox(srcRoI, regionOfInterest, &srcRoI);
    }

    // no need to set it on mask (the default ROI is OK)
    rois.setRegionOfInterest(*srcClip_, srcRoI);
}

template <class Params>
bool
CImgFilterPluginHelper<Params>::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                                      OfxRectD &/*rod*/)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

template <class Params>
bool
CImgFilterPluginHelper<Params>::isIdentity(const OFX::IsIdentityArguments &args,
                                           OFX::Clip * &identityClip,
                                           double &/*identityTime*/)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    
    double mix;
    _mix->getValueAtTime(time, mix);
    if (mix == 0.) {
        identityClip = srcClip_;
        return true;
    }

    bool red, green, blue, alpha;
    _processR->getValueAtTime(args.time, red);
    _processG->getValueAtTime(args.time, green);
    _processB->getValueAtTime(args.time, blue);
    _processA->getValueAtTime(args.time, alpha);
    if (!red && !green && !blue && !alpha) {
        identityClip = srcClip_;
        return true;
    }

    Params params;
    getValuesAtTime(time, params);
    if (isIdentity(args, params)) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}

#endif
