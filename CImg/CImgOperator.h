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

//
//  CImgOperator.h
//
//  A base class to simplify the creation of CImg plugins that have two images as input (and no mask)
//

#ifndef Misc_CImgOperator_h
#define Misc_CImgOperator_h

#include "CImgFilter.h"

class CImgOperatorPluginHelperBase
    : public CImgFilterPluginHelperBase
{
public:

    CImgOperatorPluginHelperBase(OfxImageEffectHandle handle,
                                 const char* srcAClipName, //!< should be either kOfxImageEffectSimpleSourceClipName or "A" if you want this to be the default output when plugin is disabled
                                 const char* srcBClipName,
                                 bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                                 bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                                 bool supportsTiles,
                                 bool supportsMultiResolution,
                                 bool supportsRenderScale,
                                 bool defaultUnpremult /*= true*/,
                                 bool defaultProcessAlphaOnRGBA/* = false*/);

    virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    static OFX::PageParamDescriptor* describeInContextBegin(OFX::ImageEffectDescriptor &desc,
                                                            OFX::ContextEnum /*context*/,
                                                            const char* srcAClipName,
                                                            const char* srcAClipHint,
                                                            const char* srcBClipName,
                                                            const char* srcBClipHint,
                                                            bool supportsRGBA,
                                                            bool supportsRGB,
                                                            bool supportsXY,
                                                            bool supportsAlpha,
                                                            bool supportsTiles,
                                                            bool processRGB /*= true*/,
                                                            bool processAlpha /*= false*/,
                                                            bool processIsSecret /*= false*/);

protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_srcAClip;
    OFX::Clip *_srcBClip;

    // params
    std::string _srcAClipName;
    std::string _srcBClipName;
};

template <class Params>
class CImgOperatorPluginHelper
    : public CImgOperatorPluginHelperBase
{
public:

    CImgOperatorPluginHelper(OfxImageEffectHandle handle,
                             const char* srcAClipName, //!< should be either kOfxImageEffectSimpleSourceClipName or "A" if you want this to be the default output when plugin is disabled
                             const char* srcBClipName,
                             bool usesMask, // true if the mask parameter to render should be a single-channel image containing the mask
                             bool supportsComponentRemapping, // true if the number and order of components of the image passed to render() has no importance
                             bool supportsTiles,
                             bool supportsMultiResolution,
                             bool supportsRenderScale,
                             bool defaultUnpremult/* = true*/,
                             bool defaultProcessAlphaOnRGBA/* = false*/)
        : CImgOperatorPluginHelperBase(handle, srcAClipName, srcBClipName, usesMask, supportsComponentRemapping, supportsTiles, supportsMultiResolution, supportsRenderScale, defaultUnpremult, defaultProcessAlphaOnRGBA)
    {}

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip* &identityClip, double &identityTime, int& /*view*/, std::string& /*plane*/) OVERRIDE FINAL;

    // the following functions can be overridden/implemented by the plugin
    virtual void getValuesAtTime(double time, Params& params) = 0;

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const Params& params, OfxRectI* roi) = 0;
    virtual bool getRegionOfDefinition(const OfxRectI& /*srcARoD*/,
                                       const OfxRectI& /*srcBRoD*/,
                                       const OfxPointD& /*renderScale*/,
                                       const Params& /*params*/,
                                       OfxRectI* /*dstRoD*/) { return false; };
    virtual void render(const cimg_library::CImg<cimgpix_t>& srcA, const cimg_library::CImg<cimgpix_t>& srcB, const OFX::RenderArguments &args, const Params& params, int x1, int y1, cimg_library::CImg<cimgpix_t>& dst) = 0;

    // returns 0 (no identity), 1 (dst:=dstA) or 2 (dst:=srcB)
    virtual int isIdentity(const OFX::IsIdentityArguments & /*args*/,
                           const Params& /*params*/) { return false; };

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const Params& /*params*/) { return 0; }

    //static void describe(OFX::ImageEffectDescriptor &desc, bool supportsTiles);

    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
};

template <class Params>
void
CImgOperatorPluginHelper<Params>::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    //The render function expects all clips to have the same components, but they describe that they
    //can support everything, so guide the host into providing us something good for the render action
    OFX::PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    clipPreferences.setClipComponents(*_srcAClip, outputComps);
    clipPreferences.setClipComponents(*_srcBClip, outputComps);
}

template <class Params>
void
CImgOperatorPluginHelper<Params>::render(const OFX::RenderArguments &args)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif

    const double time = args.time;
    const OfxPointD& renderScale = args.renderScale;
    const OfxRectI& renderWindow = args.renderWindow;
    OFX::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    const OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstPixelComponents  = dst->getPixelComponents();
    const int dstPixelComponentCount = dst->getPixelComponentCount();
    assert(dstBitDepth == OFX::eBitDepthFloat); // only float is supported for now (others are untested)

    OFX::auto_ptr<const OFX::Image> srcA( ( _srcAClip && _srcAClip->isConnected() ) ?
                                          _srcAClip->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( srcA.get() ) {
        OFX::BitDepthEnum srcABitDepth      = srcA->getPixelDepth();
        OFX::PixelComponentEnum srcAPixelComponents = srcA->getPixelComponents();
        if ( (srcABitDepth != dstBitDepth) || (srcAPixelComponents != dstPixelComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        checkBadRenderScaleOrField(srcA, args);
    }
# endif

    const void *srcAPixelData;
    OfxRectI srcABounds;
    OfxRectI srcARoD;
    OFX::PixelComponentEnum srcAPixelComponents;
    int srcAPixelComponentCount;
    OFX::BitDepthEnum srcABitDepth;
    int srcARowBytes;
    if ( !srcA.get() ) {
        srcAPixelData = NULL;
        srcABounds.x1 = srcABounds.y1 = srcABounds.x2 = srcABounds.y2 = 0;
        srcARoD.x1 = srcARoD.y1 = srcARoD.x2 = srcARoD.y2 = 0;
        srcAPixelComponents = _srcAClip ? _srcAClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcAPixelComponentCount = 0;
        srcABitDepth = _srcAClip ? _srcAClip->getPixelDepth() : OFX::eBitDepthNone;
        srcARowBytes = 0;
    } else {
        srcAPixelData = srcA->getPixelData();
        srcABounds = srcA->getBounds();
        // = src->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        if (_supportsTiles && _srcAClip) {
            OFX::Coords::toPixelEnclosing(_srcAClip->getRegionOfDefinition(time), args.renderScale, _srcAClip->getPixelAspectRatio(), &srcARoD);
        } else {
            // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
            // in hosts that do not support tiles (such as Sony Catalyst Edit), the image RoD is the image Bounds anyway.
            srcARoD = srcABounds;
        }
        srcAPixelComponents = srcA->getPixelComponents();
        srcAPixelComponentCount = srcA->getPixelComponentCount();
        srcABitDepth = srcA->getPixelDepth();
        srcARowBytes = srcA->getRowBytes();
    }

    OFX::auto_ptr<const OFX::Image> srcB( ( _srcBClip && _srcBClip->isConnected() ) ?
                                          _srcBClip->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( srcB.get() ) {
        OFX::BitDepthEnum srcBBitDepth      = srcB->getPixelDepth();
        OFX::PixelComponentEnum srcBPixelComponents = srcB->getPixelComponents();
        if ( (srcBBitDepth != dstBitDepth) || (srcBPixelComponents != dstPixelComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        checkBadRenderScaleOrField(srcB, args);
    }
# endif

    const void *srcBPixelData;
    OfxRectI srcBBounds;
    OfxRectI srcBRoD;
    OFX::PixelComponentEnum srcBPixelComponents;
    int srcBPixelComponentCount;
    OFX::BitDepthEnum srcBBitDepth;
    int srcBRowBytes;
    if ( !srcB.get() ) {
        srcBPixelData = NULL;
        srcBBounds.x1 = srcBBounds.y1 = srcBBounds.x2 = srcBBounds.y2 = 0;
        srcBRoD.x1 = srcBRoD.y1 = srcBRoD.x2 = srcBRoD.y2 = 0;
        srcBPixelComponents = _srcBClip ? _srcBClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcBPixelComponentCount = 0;
        srcBBitDepth = _srcBClip ? _srcBClip->getPixelDepth() : OFX::eBitDepthNone;
        srcBRowBytes = 0;
    } else {
        srcBPixelData = srcB->getPixelData();
        srcBBounds = srcB->getBounds();
        // = srcB->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        if (_supportsTiles && _srcBClip) {
            OFX::Coords::toPixelEnclosing(_srcBClip->getRegionOfDefinition(time), args.renderScale, _srcBClip->getPixelAspectRatio(), &srcBRoD);
        } else {
            // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
            // in hosts that do not support tiles (such as Sony Catalyst Edit), the image RoD is the image Bounds anyway.
            srcBRoD = srcBBounds;
        }
        srcBPixelComponents = srcB->getPixelComponents();
        srcBPixelComponentCount = srcB->getPixelComponentCount();
        srcBBitDepth = srcB->getPixelDepth();
        srcBRowBytes = srcB->getRowBytes();
    }

    void *dstPixelData = dst->getPixelData();
    const OfxRectI& dstBounds = dst->getBounds();
    OfxRectI dstRoD; // = dst->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
    if (_supportsTiles) {
        OFX::Coords::toPixelEnclosing(_dstClip->getRegionOfDefinition(time), args.renderScale, _dstClip->getPixelAspectRatio(), &dstRoD);
    } else {
        // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
        // in hosts that do not support tiles (such as Sony Catalyst Edit), the image RoD is the image Bounds anyway.
        dstRoD = dstBounds;
    }
    //const OFX::PixelComponentEnum dstPixelComponents = dst->getPixelComponents();
    //const OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
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


    printRectI("srcARoD", srcARoD);
    printRectI("srcABounds", srcABounds);
    printRectI("srcBRoD", srcBRoD);
    printRectI("srcBBounds", srcBBounds);
    printRectI("dstRoD", dstRoD);
    printRectI("dstBounds", dstBounds);
    printRectI("renderWindow", renderWindow);

    // compute the src ROI (should be consistent with getRegionsOfInterest())
    OfxRectI srcRoI;
    getRoI(renderWindow, renderScale, params, &srcRoI);

    // intersect against the destination RoD
    bool intersect = OFX::Coords::rectIntersection(srcRoI, dstRoD, &srcRoI);
    if (!intersect) {
        srcA.reset(NULL);
        srcAPixelData = NULL;
        srcABounds.x1 = srcABounds.y1 = srcABounds.x2 = srcABounds.y2 = 0;
        srcARoD.x1 = srcARoD.y1 = srcARoD.x2 = srcARoD.y2 = 0;
        srcAPixelComponents = _srcAClip ? _srcAClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcAPixelComponentCount = 0;
        srcABitDepth = _srcAClip ? _srcAClip->getPixelDepth() : OFX::eBitDepthNone;
        srcARowBytes = 0;
        srcB.reset(NULL);
        srcBPixelData = NULL;
        srcBBounds.x1 = srcBBounds.y1 = srcBBounds.x2 = srcBBounds.y2 = 0;
        srcBRoD.x1 = srcBRoD.y1 = srcBRoD.x2 = srcBRoD.y2 = 0;
        srcBPixelComponents = _srcBClip ? _srcBClip->getPixelComponents() : OFX::ePixelComponentNone;
        srcBPixelComponentCount = 0;
        srcBBitDepth = _srcBClip ? _srcBClip->getPixelDepth() : OFX::eBitDepthNone;
        srcBRowBytes = 0;
    }

    // The following checks may be wrong, because the srcRoI may be outside of the region of definition of src.
    // It is not an error: areas outside of srcRoD should be considered black and transparent.
    // IF THE FOLLOWING CODE HAS TO BE DISACTIVATED, PLEASE COMMENT WHY.
    // This was disactivated by commit c47d07669b78a71960b204989d9c36f746d14a4c, then reactivated.
    // DISACTIVATED AGAIN by FD 9/12/2014: boundary conditions are now handled by pixelcopier, and interstection with dstRoD was added above
#if 0 //def CIMGFILTER_INSTERSECT_ROI
    OFX::Coords::rectIntersection(srcRoI, srcRoD, &srcRoI);
    // the resulting ROI should be within the src bounds, or it means that the host didn't take into account the region of interest (see getRegionsOfInterest() )
    assert(srcBounds.x1 <= srcRoI.x1 && srcRoI.x2 <= srcBounds.x2 &&
           srcBounds.y1 <= srcRoI.y1 && srcRoI.y2 <= srcBounds.y2);
    if ( (srcBounds.x1 > srcRoI.x1) || (srcRoI.x2 > srcBounds.x2) ||
         ( srcBounds.y1 > srcRoI.y1) || ( srcRoI.y2 > srcBounds.y2) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    int srcNComponents = ( (srcAPixelComponents == OFX::ePixelComponentAlpha) ? 1 :
                           ( (srcAPixelComponents == OFX::ePixelComponentRGB) ? 3 : 4 ) );

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
    const int tmpPixelComponentCount = dstPixelComponentCount;
    const OFX::BitDepthEnum tmpBitDepth = OFX::eBitDepthFloat;
    const int tmpWidth = tmpBounds.x2 - tmpBounds.x1;
    const int tmpHeight = tmpBounds.y2 - tmpBounds.y1;
    const size_t tmpRowBytes = tmpPixelComponentCount * getComponentBytes(tmpBitDepth) * tmpWidth;
    size_t tmpSize = tmpRowBytes * tmpHeight;

    assert(tmpSize > 0);
    OFX::auto_ptr<OFX::ImageMemory> tmpAData( new OFX::ImageMemory(tmpSize, this) );
    float *tmpAPixelData = (float*)tmpAData->lock();

    {
        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if ( !srcA.get() ) {
            // no src, fill with black & transparent
            fred.reset( new OFX::BlackFiller<float>(*this, dstPixelComponentCount) );
        } else {
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset( new OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1>(*this) );
            } else if (dstPixelComponentCount == 4) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 4>(*this) );
            } else if (dstPixelComponentCount == 3) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 3>(*this) );
            } else if (dstPixelComponentCount == 2) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 2>(*this) );
            }  else if (dstPixelComponentCount == 1) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 1>(*this) );
            }
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, srcRoI, renderScale,
                         NULL, NULL,
                         srcAPixelData, srcABounds, srcAPixelComponents, srcAPixelComponentCount, srcABitDepth, srcARowBytes, srcBoundary,
                         tmpAPixelData, tmpBounds, tmpPixelComponents, tmpPixelComponentCount, tmpBitDepth, tmpRowBytes,
                         premult, premultChannel,
                         1., false);
        }
    }
    OFX::auto_ptr<OFX::ImageMemory> tmpBData( new OFX::ImageMemory(tmpSize, this) );
    float *tmpBPixelData = (float*)tmpBData->lock();

    {
        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if ( !srcB.get() ) {
            // no src, fill with black & transparent
            if (dstPixelComponentCount == 4) {
                fred.reset( new OFX::BlackFiller<float>(*this, 4) );
            } else if (dstPixelComponentCount == 3) {
                fred.reset( new OFX::BlackFiller<float>(*this, 3) );
            } else if (dstPixelComponentCount == 2) {
                fred.reset( new OFX::BlackFiller<float>(*this, 2) );
            } else if (dstPixelComponentCount == 1) {
                fred.reset( new OFX::BlackFiller<float>(*this, 1) );
            }
        } else {
            if (dstPixelComponents == OFX::ePixelComponentRGBA) {
                fred.reset( new OFX::PixelCopierUnPremult<float, 4, 1, float, 4, 1>(*this) );
            } else if (dstPixelComponentCount == 4) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 4>(*this) );
            } else if (dstPixelComponentCount == 3) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 3>(*this) );
            } else if (dstPixelComponentCount == 2) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 2>(*this) );
            }  else if (dstPixelComponentCount == 1) {
                // just copy, no premult
                fred.reset( new OFX::PixelCopier<float, 1>(*this) );
            }
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, srcRoI, renderScale,
                         NULL, NULL,
                         srcBPixelData, srcBBounds, srcBPixelComponents, srcBPixelComponentCount, srcBBitDepth, srcBRowBytes, srcBoundary,
                         tmpBPixelData, tmpBounds, tmpPixelComponents, tmpPixelComponentCount, tmpBitDepth, tmpRowBytes,
                         premult, premultChannel,
                         1., false);
        }
    }
    OFX::auto_ptr<OFX::ImageMemory> tmpData( new OFX::ImageMemory(tmpSize, this) );
    float *tmpPixelData = (float*)tmpData->lock();

    //////////////////////////////////////////////////////////////////////////////////////////
    // 2- extract channels to be processed from tmp to a cimg of size srcRoI (and do the interleaved to coplanar conversion)

    // allocate the cimg data to hold the src ROI
    const int cimgSpectrum = srcNComponents;
    const int cimgWidth = srcRoI.x2 - srcRoI.x1;
    const int cimgHeight = srcRoI.y2 - srcRoI.y1;
    const size_t cimgSize = cimgWidth * cimgHeight * cimgSpectrum * sizeof(cimgpix_t);


    if (cimgSize) { // may be zero if no channel is processed
        OFX::auto_ptr<OFX::ImageMemory> cimgAData( new OFX::ImageMemory(cimgSize, this) );
        cimgpix_t *cimgAPixelData = (cimgpix_t*)cimgAData->lock();
        cimg_library::CImg<cimgpix_t> cimgA(cimgAPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);

        for (int c = 0; c < cimgSpectrum; ++c) {
            cimgpix_t *dst = cimgA.data(0, 0, 0, c);
            const float *src = tmpAPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += srcNComponents, ++dst) {
                *dst = *src;
            }
        }

        OFX::auto_ptr<OFX::ImageMemory> cimgBData( new OFX::ImageMemory(cimgSize, this) );
        cimgpix_t *cimgBPixelData = (cimgpix_t*)cimgBData->lock();
        cimg_library::CImg<cimgpix_t> cimgB(cimgBPixelData, cimgWidth, cimgHeight, 1, cimgSpectrum, true);

        for (int c = 0; c < cimgSpectrum; ++c) {
            cimgpix_t *dst = cimgB.data(0, 0, 0, c);
            const float *src = tmpBPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, src += srcNComponents, ++dst) {
                *dst = *src;
            }
        }

        //////////////////////////////////////////////////////////////////////////////////////////
        // 3- process the cimg
        printRectI("render srcRoI", srcRoI);
        cimg_library::CImg<cimgpix_t> cimg;
        render(cimgA, cimgB, args, params, srcRoI.x1, srcRoI.y1, cimg);
        // check that the dimensions didn't change
        assert(cimg.width() == cimgWidth && cimg.height() == cimgHeight && cimg.depth() == 1 && cimg.spectrum() == cimgSpectrum);

        //////////////////////////////////////////////////////////////////////////////////////////
        // 4- copy back the processed channels from the cImg to tmp. only processWindow has to be copied

        // We copy the whole srcRoI. This could be optimized to copy only renderWindow
        for (int c = 0; c < cimgSpectrum; ++c) {
            const cimgpix_t *src = cimg.data(0, 0, 0, c);
            float *dst = tmpPixelData + c;
            for (unsigned int siz = cimgWidth * cimgHeight; siz; --siz, ++src, dst += srcNComponents) {
                *dst = *src;
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // 5- copy+premult+max+mix tmp to dst (only processWindow)

    {
        OFX::auto_ptr<OFX::PixelProcessorFilterBase> fred;
        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            fred.reset( new OFX::PixelCopierPremult<float, 4, 1, float, 4, 1>(*this) );
        } else if (dstPixelComponentCount == 4) {
            // just copy, no premult
            fred.reset( new OFX::PixelCopier<float, 4>(*this) );
        } else if (dstPixelComponentCount == 3) {
            // just copy, no premult
            fred.reset( new OFX::PixelCopier<float, 3>(*this) );
        } else if (dstPixelComponentCount == 2) {
            // just copy, no premult
            fred.reset( new OFX::PixelCopier<float, 2>(*this) );
        } else if (dstPixelComponentCount == 1) {
            // just copy, no premult
            fred.reset( new OFX::PixelCopier<float, 1>(*this) );
        }
        assert( fred.get() );
        if ( fred.get() ) {
            setupAndCopy(*fred, time, renderWindow, renderScale,
                         NULL, NULL,
                         tmpPixelData, tmpBounds, tmpPixelComponents, tmpPixelComponentCount, tmpBitDepth, tmpRowBytes, 0,
                         dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                         premult, premultChannel,
                         1., false);
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////
    // done!
} // >::render

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
template <class Params>
void
CImgOperatorPluginHelper<Params>::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                                       OFX::RegionOfInterestSetter &rois)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    const double time = args.time;
    const OfxRectD& regionOfInterest = args.regionOfInterest;
    Params params;
    getValuesAtTime(time, params);

    {
        OfxRectD srcRoI;
        double pixelaspectratio = _srcAClip ? _srcAClip->getPixelAspectRatio() : 1.;
        OfxRectI rectPixel;
        OFX::Coords::toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
        OfxRectI srcRoIPixel;
        getRoI(rectPixel, args.renderScale, params, &srcRoIPixel);
        OFX::Coords::toCanonical(srcRoIPixel, args.renderScale, pixelaspectratio, &srcRoI);

        rois.setRegionOfInterest(*_srcAClip, srcRoI);
    }
    {
        OfxRectD srcRoI;
        double pixelaspectratio = _srcBClip ? _srcBClip->getPixelAspectRatio() : 1.;
        OfxRectI rectPixel;
        OFX::Coords::toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
        OfxRectI srcRoIPixel;
        getRoI(rectPixel, args.renderScale, params, &srcRoIPixel);
        OFX::Coords::toCanonical(srcRoIPixel, args.renderScale, pixelaspectratio, &srcRoI);

        rois.setRegionOfInterest(*_srcBClip, srcRoI);
    }
}

template <class Params>
bool
CImgOperatorPluginHelper<Params>::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                                        OfxRectD &rod)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    Params params;
    getValuesAtTime(args.time, params);

    double srcApixelaspectratio = _srcAClip ? _srcAClip->getPixelAspectRatio() : 1.;
    OfxRectI srcARoDPixel = {0, 0, 0, 0};
    if (_srcAClip) {
        OFX::Coords::toPixelEnclosing(_srcAClip->getRegionOfDefinition(args.time), args.renderScale, srcApixelaspectratio, &srcARoDPixel);
    }

    double srcBpixelaspectratio = _srcBClip ? _srcBClip->getPixelAspectRatio() : 1.;
    OfxRectI srcBRoDPixel = {0, 0, 0, 0};
    if (_srcBClip) {
        OFX::Coords::toPixelEnclosing(_srcBClip->getRegionOfDefinition(args.time), args.renderScale, srcBpixelaspectratio, &srcARoDPixel);
    }

    OfxRectI rodPixel;
    bool ret = getRegionOfDefinition(srcARoDPixel, srcBRoDPixel, args.renderScale, params, &rodPixel);
    if (ret) {
        double dstpixelaspectratio = _dstClip ? _dstClip->getPixelAspectRatio() : 1.;
        OFX::Coords::toCanonical(rodPixel, args.renderScale, dstpixelaspectratio, &rod);

        return true;
    }

    return false;
}

template <class Params>
bool
CImgOperatorPluginHelper<Params>::isIdentity(const OFX::IsIdentityArguments &args,
                                             OFX::Clip * &identityClip,
                                             double & /*identityTime*/
                                             , int& /*view*/, std::string& /*plane*/)
{
# ifndef NDEBUG
    if ( !_supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    const double time = args.time;
    Params params;
    getValuesAtTime(time, params);
    switch ( isIdentity(args, params) ) {
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

#endif // ifndef Misc_CImgOperator_h
