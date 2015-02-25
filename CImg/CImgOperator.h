//
//  CImgOperator.h
//
//  A base class to simplify the creation of CImg plugins that have two images as input (and no mask)
//
//  Created by Frédéric Devernay on 09/10/2014.
//  Copyright (c) 2014 OpenFX. All rights reserved.
//

#ifndef Misc_CImgOperator_h
#define Misc_CImgOperator_h

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsPixelProcessor.h"
#include "ofxsCopier.h"
#include "ofxsMerging.h"

#include <cassert>
#include <memory>

//#define CIMG_DEBUG

// use the locally-downloaded CImg.h
//
// To download the latest CImg.h, use:
// git archive --remote=git://git.code.sf.net/p/gmic/source HEAD:src CImg.h |tar xf -
//
// CImg.h must at least be the version from Oct 17 2014, commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3
// To download, use:
// git archive --remote=git://git.code.sf.net/p/gmic/source 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3:src CImg.h |tar xf -
#define cimg_display 0
CLANG_DIAG_OFF(shorten-64-to-32)
#include "CImg.h"
CLANG_DIAG_ON(shorten-64-to-32)

template <class Params>
class CImgOperatorPluginHelper : public OFX::ImageEffect
{
public:

    CImgOperatorPluginHelper(OfxImageEffectHandle handle,
                             const char* srcAClipName, //!< should be either kOfxImageEffectSimpleSourceClipName or "A" if you want this to be the default output when plugin is disabled
                             const char* srcBClipName,
                             bool supportsTiles,
                             bool supportsMultiResolution,
                             bool supportsRenderScale,
                             bool defaultUnpremult = true,
                             bool defaultProcessAlphaOnRGBA = false)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcAClip(0)
    , _srcBClip(0)
    , _premult(0)
    , _premultChannel(0)
    , _srcAClipName(srcAClipName)
    , _srcBClipName(srcBClipName)
    , _supportsTiles(supportsTiles)
    , _supportsMultiResolution(supportsMultiResolution)
    , _supportsRenderScale(supportsRenderScale)
    , _defaultUnpremult(defaultUnpremult)
    , _defaultProcessAlphaOnRGBA(defaultProcessAlphaOnRGBA)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA));
        _srcAClip = fetchClip(_srcAClipName);
        assert(_srcAClip && (_srcAClip->getPixelComponents() == OFX::ePixelComponentRGB || _srcAClip->getPixelComponents() == OFX::ePixelComponentRGBA));
        _srcBClip = fetchClip(_srcBClipName);
        assert(_srcBClip && (_srcBClip->getPixelComponents() == OFX::ePixelComponentRGB || _srcBClip->getPixelComponents() == OFX::ePixelComponentRGBA));

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
    }

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip* &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL
    {
        if (clipName == _srcAClipName && _srcAClip && args.reason == OFX::eChangeUserEdit) {
            if (_defaultUnpremult) {
                switch (_srcAClip->getPreMultiplication()) {
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
        if (clipName == _srcBClipName && _srcBClip && args.reason == OFX::eChangeUserEdit) {
            if (_defaultUnpremult) {
                switch (_srcBClip->getPreMultiplication()) {
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

    // the following functions can be overridden/implemented by the plugin

    virtual void getValuesAtTime(double time, Params& params) = 0;

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const Params& params, OfxRectI* roi) = 0;

    virtual bool getRoD(const OfxRectI& /*srcARoD*/, const OfxRectI& /*srcBRoD*/, const OfxPointD& /*renderScale*/, const Params& /*params*/, OfxRectI* /*dstRoD*/) { return false; };

    virtual void render(const cimg_library::CImg<float>& srcA, const cimg_library::CImg<float>& srcB, const OFX::RenderArguments &args, const Params& params, int x1, int y1, cimg_library::CImg<float>& dst) = 0;

    // returns 0 (no identity), 1 (dst:=dstA) or 2 (dst:=srcB)
    virtual int isIdentity(const OFX::IsIdentityArguments &/*args*/, const Params& /*params*/) { return false; };

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const Params& /*params*/) { return 0; }

    //static void describe(OFX::ImageEffectDescriptor &desc, bool supportsTiles);

    static OFX::PageParamDescriptor*
    describeInContextBegin(OFX::ImageEffectDescriptor &desc,
                           OFX::ContextEnum /*context*/,
                           const char* srcAClipName,
                           const char* srcBClipName,
                           bool supportsRGBA,
                           bool supportsRGB,
                           bool supportsAlpha,
                           bool supportsTiles,
                           bool /*processRGB*/ = true,
                           bool /*processAlpha*/ = false,
                           bool /*processIsSecret*/ = false)
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

    static void
    describeInContextEnd(OFX::ImageEffectDescriptor &desc,
                         OFX::ContextEnum /*context*/,
                         OFX::PageParamDescriptor* page)
    {
        ofxsPremultDescribeParams(desc, page);
    }

    // utility functions
    static bool
    isEmpty(const OfxRectI& r)
    {
        return r.x1 >= r.x2 || r.y1 >= r.y2;
    }

private:
#ifdef CIMG_DEBUG
    static void
    printRectI(const char*name, const OfxRectI& rect) {
        printf("%s= (%d, %d)-(%d, %d)\n", name, rect.x1, rect.y1, rect.x2, rect.y2);
    }
#else
    static void printRectI(const char*, const OfxRectI&) {}
#endif


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
                 int srcBoundary,
                 void *dstPixelData,
                 const OfxRectI& dstBounds,
                 OFX::PixelComponentEnum dstPixelComponents,
                 OFX::BitDepthEnum dstPixelDepth,
                 int dstRowBytes,
                 bool premult,
                 int premultChannel);


    // utility functions


private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcAClip;
    OFX::Clip *_srcBClip;

    // params
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;

    std::string _srcAClipName;
    std::string _srcBClipName;
    bool _supportsTiles;
    bool _supportsMultiResolution;
    bool _supportsRenderScale;
    bool _defaultUnpremult; //!< unpremult by default
    bool _defaultProcessAlphaOnRGBA; //!< process alpha by default on RGBA images
};


/* set up and run a copy processor */
template <class Params>
void
CImgOperatorPluginHelper<Params>::setupAndFill(OFX::PixelProcessorFilterBase & processor,
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
CImgOperatorPluginHelper<Params>::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                                                              double /*time*/,
                                                              const OfxRectI &renderWindow,
                                                              const void *srcPixelData,
                                                              const OfxRectI& srcBounds,
                                                              OFX::PixelComponentEnum srcPixelComponents,
                                                              OFX::BitDepthEnum srcBitDepth,
                                                              int srcRowBytes,
                                                              int srcBoundary,
                                                              void *dstPixelData,
                                                              const OfxRectI& dstBounds,
                                                              OFX::PixelComponentEnum dstPixelComponents,
                                                              OFX::BitDepthEnum dstPixelDepth,
                                                              int dstRowBytes,
                                                              bool premult,
                                                              int premultChannel)
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

    // set the images
    assert(dstPixelData);
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    assert(0 <= srcBoundary && srcBoundary <= 2);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes, srcBoundary);

    // set the render window
    processor.setRenderWindow(renderWindow);

    processor.setPremultMaskMix(premult, premultChannel, 1.);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


template <class Params>
void
CImgOperatorPluginHelper<Params>::render(const OFX::RenderArguments &args)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    const OfxPointD& renderScale = args.renderScale;
    const OfxRectI& renderWindow = args.renderWindow;
    const OFX::FieldEnum fieldToRender = args.fieldToRender;

    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(time));
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

    std::auto_ptr<const OFX::Image> srcA((_srcAClip && _srcAClip->isConnected()) ?
                                         _srcAClip->fetchImage(args.time) : 0);
    if (srcA.get()) {
        OFX::BitDepthEnum    srcABitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcAPixelComponents = srcA->getPixelComponents();
        if (srcABitDepth != dstBitDepth || srcAPixelComponents != dstPixelComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (srcA->getRenderScale().x != renderScale.x ||
            srcA->getRenderScale().y != renderScale.y ||
            srcA->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    const void *srcAPixelData;
    OfxRectI srcABounds;
    OfxRectI srcARoD;
    OFX::PixelComponentEnum srcAPixelComponents;
    OFX::BitDepthEnum srcABitDepth;
    //srcAPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
    int srcARowBytes;
    if (!srcA.get()) {
        srcAPixelData = NULL;
        srcABounds.x1 = srcABounds.y1 = srcABounds.x2 = srcABounds.y2 = 0;
        srcARoD.x1 = srcARoD.y1 = srcARoD.x2 = srcARoD.y2 = 0;
        srcAPixelComponents = _srcAClip ? _srcAClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcABitDepth = _srcAClip ? _srcAClip->getPixelDepth() : OFX::eBitDepthNone;
        //srcAPixelBytes = getPixelBytes(srcAPixelComponents, srcABitDepth);
        srcARowBytes = 0;
    } else {
        srcAPixelData = srcA->getPixelData();
        srcABounds = srcA->getBounds();
        // = src->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        OFX::MergeImages2D::toPixelEnclosing(_srcAClip->getRegionOfDefinition(time), args.renderScale, _srcAClip->getPixelAspectRatio(), &srcARoD);
        srcAPixelComponents = srcA->getPixelComponents();
        srcABitDepth = srcA->getPixelDepth();
        //srcAPixelBytes = getPixelBytes(srcAPixelComponents, srcABitDepth);
        srcARowBytes = srcA->getRowBytes();
    }

    std::auto_ptr<const OFX::Image> srcB((_srcBClip && _srcBClip->isConnected()) ?
                                         _srcBClip->fetchImage(args.time) : 0);
    if (srcB.get()) {
        OFX::BitDepthEnum    srcBBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcBPixelComponents = srcB->getPixelComponents();
        if (srcBBitDepth != dstBitDepth || srcBPixelComponents != dstPixelComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (srcB->getRenderScale().x != renderScale.x ||
            srcB->getRenderScale().y != renderScale.y ||
            srcB->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }

    const void *srcBPixelData;
    OfxRectI srcBBounds;
    OfxRectI srcBRoD;
    OFX::PixelComponentEnum srcBPixelComponents;
    OFX::BitDepthEnum srcBBitDepth;
    //srcBPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
    int srcBRowBytes;
    if (!srcB.get()) {
        srcBPixelData = NULL;
        srcBBounds.x1 = srcBBounds.y1 = srcBBounds.x2 = srcBBounds.y2 = 0;
        srcBRoD.x1 = srcBRoD.y1 = srcBRoD.x2 = srcBRoD.y2 = 0;
        srcBPixelComponents = _srcBClip ? _srcBClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcBBitDepth = _srcBClip ? _srcBClip->getPixelDepth() : OFX::eBitDepthNone;
        //srcPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
        srcBRowBytes = 0;
    } else {
        srcBPixelData = srcB->getPixelData();
        srcBBounds = srcB->getBounds();
        // = srcB->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        OFX::MergeImages2D::toPixelEnclosing(_srcBClip->getRegionOfDefinition(time), args.renderScale, _srcBClip->getPixelAspectRatio(), &srcBRoD);
        srcBPixelComponents = srcB->getPixelComponents();
        srcBBitDepth = srcB->getPixelDepth();
        //srcPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
        srcBRowBytes = srcB->getRowBytes();
    }

    void *dstPixelData = dst->getPixelData();
    const OfxRectI& dstBounds = dst->getBounds();
    OfxRectI dstRoD; // = dst->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
    OFX::MergeImages2D::toPixelEnclosing(_dstClip->getRegionOfDefinition(time), args.renderScale, _dstClip->getPixelAspectRatio(), &dstRoD);
    //const OFX::PixelComponentEnum dstPixelComponents = dst->getPixelComponents();
    //const OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    //dstPixelBytes = getPixelBytes(dstPixelComponents, dstBitDepth);
    const int dstRowBytes = dst->getRowBytes();

    if (!_supportsTiles) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
        //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
        assert(srcARoD.x1 == srcABounds.x1);
        assert(srcARoD.x2 == srcABounds.x2);
        assert(srcARoD.y1 == srcABounds.y1);
        assert(srcARoD.y2 == srcABounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        assert(srcBRoD.x1 == srcBBounds.x1);
        assert(srcBRoD.x2 == srcBBounds.x2);
        assert(srcBRoD.y1 == srcBBounds.y1);
        assert(srcBRoD.y2 == srcBBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        assert(dstRoD.x1 == dstBounds.x1);
        assert(dstRoD.x2 == dstBounds.x2);
        assert(dstRoD.y1 == dstBounds.y1);
        assert(dstRoD.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
    }
    if (!_supportsMultiResolution) {
        // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
        //   Multiple resolution images mean...
        //    input and output images can be of any size
        //    input and output images can be offset from the origin
        assert(srcARoD.x1 == 0);
        assert(srcARoD.y1 == 0);
        assert(srcARoD.x1 == dstRoD.x1);
        assert(srcARoD.x2 == dstRoD.x2);
        assert(srcARoD.y1 == dstRoD.y1);
        assert(srcARoD.y2 == dstRoD.y2); // crashes on Natron if kSupportsMultiResolution=0
        assert(srcBRoD.x1 == 0);
        assert(srcBRoD.y1 == 0);
        assert(srcBRoD.x1 == dstRoD.x1);
        assert(srcBRoD.x2 == dstRoD.x2);
        assert(srcBRoD.y1 == dstRoD.y1);
        assert(srcBRoD.y2 == dstRoD.y2); // crashes on Natron if kSupportsMultiResolution=0
    }

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);

    Params params;
    getValuesAtTime(time, params);
    int srcBoundary = getBoundary(params);
    assert(0 <= srcBoundary && srcBoundary <= 2);


    printRectI("srcARoD",srcARoD);
    printRectI("srcABounds",srcABounds);
    printRectI("srcBRoD",srcBRoD);
    printRectI("srcBBounds",srcBBounds);
    printRectI("dstRoD",dstRoD);
    printRectI("dstBounds",dstBounds);
    printRectI("renderWindow",renderWindow);

    // compute the src ROI (should be consistent with getRegionsOfInterest())
    OfxRectI srcRoI;
    getRoI(renderWindow, renderScale, params, &srcRoI);

    // intersect against the destination RoD
    OFX::MergeImages2D::rectIntersection(srcRoI, dstRoD, &srcRoI);

    // The following checks may be wrong, because the srcRoI may be outside of the region of definition of src.
    // It is not an error: areas outside of srcRoD should be considered black and transparent.
    // IF THE FOLLOWING CODE HAS TO BE DISACTIVATED, PLEASE COMMENT WHY.
    // This was disactivated by commit c47d07669b78a71960b204989d9c36f746d14a4c, then reactivated.
    // DISACTIVATED AGAIN by FD 9/12/2014: boundary conditions are now handled by pixelcopier, and interstection with dstRoD was added above
#if 0 //def CIMGFILTER_INSTERSECT_ROI
    OFX::MergeImages2D::rectIntersection(srcRoI, srcRoD, &srcRoI);
    // the resulting ROI should be within the src bounds, or it means that the host didn't take into account the region of interest (see getRegionsOfInterest() )
    assert(srcBounds.x1 <= srcRoI.x1 && srcRoI.x2 <= srcBounds.x2 &&
           srcBounds.y1 <= srcRoI.y1 && srcRoI.y2 <= srcBounds.y2);
    if (srcBounds.x1 > srcRoI.x1 || srcRoI.x2 > srcBounds.x2 ||
        srcBounds.y1 > srcRoI.y1 || srcRoI.y2 > srcBounds.y2) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    int srcNComponents = ((srcAPixelComponents == OFX::ePixelComponentAlpha) ? 1 :
                          ((srcAPixelComponents == OFX::ePixelComponentRGB) ? 3 : 4));

    // from here on, we do the following steps:
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)
    // 3- process the cimg
    // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    //////////////////////////////////////////////////////////////////////////////////////////
    // 1- copy & unpremult all channels from srcRoI, from src to a tmp image of size srcRoI

    const OfxRectI tmpBounds = srcRoI;
    const OFX::PixelComponentEnum tmpPixelComponents = dstPixelComponents;
    const OFX::BitDepthEnum tmpBitDepth = OFX::eBitDepthFloat;
    const int tmpWidth = tmpBounds.x2 - tmpBounds.x1;
    const int tmpHeight = tmpBounds.y2 - tmpBounds.y1;
    const int tmpRowBytes = getPixelBytes(tmpPixelComponents, tmpBitDepth) * tmpWidth;
    size_t tmpSize = tmpRowBytes * tmpHeight;

    assert(tmpSize > 0);
    std::auto_ptr<OFX::ImageMemory> tmpAData(new OFX::ImageMemory(tmpSize, this));
    float *tmpAPixelData = (float*)tmpAData->lock();

    {
        std::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (!srcA.get()) {
            // no src, fill with black & transparent
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset(new OFX::BlackFiller<float, 4>(*this));
            } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
                fred.reset(new OFX::BlackFiller<float, 3>(*this));
            }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
                fred.reset(new OFX::BlackFiller<float, 1>(*this));
            }
        } else {
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset(new OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1>(*this));
            } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
                // just copy, no premult
                fred.reset(new OFX::PixelCopier<float, 3>(*this));
            }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
                // just copy, no premult
                fred.reset(new OFX::PixelCopier<float, 1>(*this));
            }
        }
        setupAndCopy(*fred, time, srcRoI,
                     srcAPixelData, srcABounds, srcAPixelComponents, srcABitDepth, srcARowBytes, srcBoundary,
                     tmpAPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     premult, premultChannel);
    }
    
    std::auto_ptr<OFX::ImageMemory> tmpBData(new OFX::ImageMemory(tmpSize, this));
    float *tmpBPixelData = (float*)tmpBData->lock();

    {
        std::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (!srcB.get()) {
            // no src, fill with black & transparent
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset(new OFX::BlackFiller<float, 4>(*this));
            } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
                fred.reset(new OFX::BlackFiller<float, 3>(*this));
            }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
                fred.reset(new OFX::BlackFiller<float, 1>(*this));
            }
        } else {
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset(new OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1>(*this));
            } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
                // just copy, no premult
                fred.reset(new OFX::PixelCopier<float, 3>(*this));
            }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
                // just copy, no premult
                fred.reset(new OFX::PixelCopier<float, 1>(*this));
            }
        }
        setupAndCopy(*fred, time, srcRoI,
                     srcBPixelData, srcBBounds, srcBPixelComponents, srcBBitDepth, srcBRowBytes, srcBoundary,
                     tmpBPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes,
                     premult, premultChannel);
    }

    std::auto_ptr<OFX::ImageMemory> tmpData(new OFX::ImageMemory(tmpSize, this));
    float *tmpPixelData = (float*)tmpData->lock();

    //////////////////////////////////////////////////////////////////////////////////////////
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)

    // allocate the cimg data to hold the src ROI
    const int cimgSpectrum = srcNComponents;
    const int cimgWidth = srcRoI.x2 - srcRoI.x1;
    const int cimgHeight = srcRoI.y2 - srcRoI.y1;
    const size_t cimgSize = cimgWidth * cimgHeight * cimgSpectrum * sizeof(float);


    if (cimgSize) { // may be zero if no channel is processed
        std::auto_ptr<OFX::ImageMemory> cimgAData(new OFX::ImageMemory(cimgSize, this));
        float *cimgAPixelData = (float*)cimgAData->lock();
        cimg_library::CImg<float> cimgA(cimgAPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);

        for (int c=0; c < cimgSpectrum; ++c) {
            float *dst = cimgA.data(0,0,0,c);
            const float *src = tmpAPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += srcNComponents, ++dst) {
                *dst = *src;
            }
        }

        std::auto_ptr<OFX::ImageMemory> cimgBData(new OFX::ImageMemory(cimgSize, this));
        float *cimgBPixelData = (float*)cimgBData->lock();
        cimg_library::CImg<float> cimgB(cimgBPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);

        for (int c=0; c < cimgSpectrum; ++c) {
            float *dst = cimgB.data(0,0,0,c);
            const float *src = tmpBPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += srcNComponents, ++dst) {
                *dst = *src;
            }
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////
        // 3- process the cimg
        printRectI("render srcRoI", srcRoI);
        cimg_library::CImg<float> cimg;
        render(cimgA, cimgB, args, params, srcRoI.x1, srcRoI.y1, cimg);
        // check that the dimensions didn't change
        assert(cimg.width() == cimgWidth && cimg.height() == cimgHeight && cimg.depth() == 1 && cimg.spectrum() == cimgSpectrum);

        //////////////////////////////////////////////////////////////////////////////////////////
        // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied

        // We copy the whole srcRoI. This could be optimized to copy only renderWindow
        for (int c=0; c < cimgSpectrum; ++c) {
            const float *src = cimg.data(0,0,0,c);
            float *dst = tmpPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, ++src, dst += srcNComponents) {
                *dst = *src;
            }
        }

    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    {
        std::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            fred.reset(new OFX::PixelCopierPremult<float, 4, 1, float, 4, 1>(*this));
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            // just copy, no premult
            fred.reset(new OFX::PixelCopier<float, 3>(*this));
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            // just copy, no premult
            assert(srcAPixelComponents == OFX::ePixelComponentAlpha);
            fred.reset(new OFX::PixelCopier<float, 1>(*this));
        }
        setupAndCopy(*fred, time, renderWindow,
                     tmpPixelData, tmpBounds, tmpPixelComponents, tmpBitDepth, tmpRowBytes, 0,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel);
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // done!
}



// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
template <class Params>
void
CImgOperatorPluginHelper<Params>::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
    const OfxRectD& regionOfInterest = args.regionOfInterest;

    Params params;
    getValuesAtTime(time, params);

    {
        OfxRectD srcRoI;
        double pixelaspectratio = _srcAClip ? _srcAClip->getPixelAspectRatio() : 1.;

        OfxRectI rectPixel;
        OFX::MergeImages2D::toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
        OfxRectI srcRoIPixel;
        getRoI(rectPixel, args.renderScale, params, &srcRoIPixel);
        OFX::MergeImages2D::toCanonical(srcRoIPixel, args.renderScale, pixelaspectratio, &srcRoI);

        rois.setRegionOfInterest(*_srcAClip, srcRoI);
    }
    {
        OfxRectD srcRoI;
        double pixelaspectratio = _srcBClip ? _srcBClip->getPixelAspectRatio() : 1.;

        OfxRectI rectPixel;
        OFX::MergeImages2D::toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
        OfxRectI srcRoIPixel;
        getRoI(rectPixel, args.renderScale, params, &srcRoIPixel);
        OFX::MergeImages2D::toCanonical(srcRoIPixel, args.renderScale, pixelaspectratio, &srcRoI);

        rois.setRegionOfInterest(*_srcBClip, srcRoI);
    }
}

template <class Params>
bool
CImgOperatorPluginHelper<Params>::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                                                       OfxRectD &rod)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    Params params;
    getValuesAtTime(args.time, params);

    double srcApixelaspectratio = _srcAClip ? _srcAClip->getPixelAspectRatio() : 1.;

    OfxRectI srcARoDPixel = {0, 0, 0, 0};
    if (_srcAClip) {
        OFX::MergeImages2D::toPixelEnclosing(_srcAClip->getRegionOfDefinition(args.time), args.renderScale, srcApixelaspectratio, &srcARoDPixel);
    }

    double srcBpixelaspectratio = _srcBClip ? _srcBClip->getPixelAspectRatio() : 1.;

    OfxRectI srcBRoDPixel = {0, 0, 0, 0};
    if (_srcBClip) {
        OFX::MergeImages2D::toPixelEnclosing(_srcBClip->getRegionOfDefinition(args.time), args.renderScale, srcBpixelaspectratio, &srcARoDPixel);
    }

    OfxRectI rodPixel;

    bool ret = getRoD(srcARoDPixel, srcBRoDPixel, args.renderScale, params, &rodPixel);
    if (ret) {
        double dstpixelaspectratio = _dstClip ? _dstClip->getPixelAspectRatio() : 1.;
        OFX::MergeImages2D::toCanonical(rodPixel, args.renderScale, dstpixelaspectratio, &rod);
        return true;
    }

    return false;
}

template <class Params>
bool
CImgOperatorPluginHelper<Params>::isIdentity(const OFX::IsIdentityArguments &args,
                                                            OFX::Clip * &identityClip,
                                                            double &/*identityTime*/)
{
    if (!_supportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;

    Params params;
    getValuesAtTime(time, params);
    switch (isIdentity(args, params)) {
        case 0:
            return false;
        case 1:
            identityClip = _srcAClip;
            return true;
        case 2:
            identityClip = _srcBClip;
            return true;
    }
    return false;
}

#endif
