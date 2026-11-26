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
 * Shared implementation of the OFX CImgErode and CImgDilate plugins.
 *
 * Both plugins are the same filter with the sign of the size swapped:
 * a positive size erodes in CImgErode and dilates in CImgDilate.
 * This header must be included by exactly one .cpp per plugin, which
 * instantiates CImgErodeDilatePluginFactory<dilate> and registers it.
 */

#ifndef Misc_CImgErodeDilate_h
#define Misc_CImgErodeDilate_h

#include <memory>
#include <cmath>
#include <cstring>
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginGrouping      "Filter"
#define kPluginDescriptionCommon \
    "Different sizes can be given for the x and y axis.\n"
#define kPluginDescriptionCImg \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kErodePluginName          "ErodeCImg"
#define kErodePluginDescription \
    "Erode (or dilate) input stream by a rectangular structuring element of specified size and Neumann boundary conditions (pixels out of the image get the value of the nearest pixel).\n" \
    "A negative size will perform a dilation instead of an erosion.\n" \
    kPluginDescriptionCommon \
    "Uses the 'erode' and 'dilate' functions from the CImg library.\n" \
    kPluginDescriptionCImg
#define kErodePluginIdentifier    "net.sf.cimg.CImgErode"

#define kDilatePluginName          "DilateCImg"
#define kDilatePluginDescription \
    "Dilate (or erode) input stream by a rectangular structuring element of specified size and Neumann boundary conditions (pixels out of the image get the value of the nearest pixel).\n" \
    "A negative size will perform an erosion instead of a dilation.\n" \
    kPluginDescriptionCommon \
    "Uses the 'dilate' and 'erode' functions from the CImg library.\n" \
    kPluginDescriptionCImg
#define kDilatePluginIdentifier    "net.sf.cimg.CImgDilate"

// History (both plugins):
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: add expand rod parameter
// version 2.2: NaN/inf-safe premultiply (SupportExt #73, #79)
// version 3.0: the default size is 0 (no change), as in the reference; version 2 (default size 1) is kept for
//              backward compatibility
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMajorOld 2 // version 2, kept for backward compatibility
#define kPluginVersionMinorOld 2

#define kSupportsComponentRemapping 1
#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety OFX::eRenderFullySafe
#if cimg_use_openmp!=0
#define kHostFrameThreading false
#else
#define kHostFrameThreading true
#endif
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY true
#define kSupportsAlpha true

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Width/height of the rectangular structuring element is 2*size+1, in pixel units (>=0). " \
    "For a single fractional size s in canonical units (canonical x is pixel x times the pixel aspect ratio PAR), use " \
    "round(s/PAR) for x and round(s) for y: e.g. s = 1.5 with PAR = 1.0667 is (1, 2)."
#define kParamSizeDefault 0
#define kParamSizeDefaultOld 1 // version 2

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kErodeParamExpandRoDHint "Expand the source region of definition by 2*size pixels if size is negative"
#define kDilateParamExpandRoDHint "Expand the source region of definition by 2*size pixels if size is positive"


// parameter values at a given time
struct CImgErodeDilateParams
{
    int sx;
    int sy;
    bool expandRod;
};

// dilate=false: CImgErode (positive size erodes), dilate=true: CImgDilate (positive size dilates)
template <bool dilate>
class CImgErodeDilatePlugin
    : public CImgFilterPluginHelper<CImgErodeDilateParams, false>
{
public:

    CImgErodeDilatePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgErodeDilateParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _size  = fetchInt2DParam(kParamSize);
        _expandRod = fetchBooleanParam(kParamExpandRoD);
        assert(_size && _expandRod);
    }

    virtual void getValuesAtTime(double time,
                                 CImgErodeDilateParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.sx, params.sy);
        _expandRod->getValueAtTime(time, params.expandRod);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgErodeDilateParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix_x = (int)std::ceil(std::abs(params.sx) * renderScale.x);
        int delta_pix_y = (int)std::ceil(std::abs(params.sy) * renderScale.y);

        cimgPadRect(rect, delta_pix_x, delta_pix_y, roi);
    }

    bool getRegionOfDefinition(const OfxRectI& srcRoD,
                               const OfxPointD& renderScale,
                               const CImgErodeDilateParams& params,
                               OfxRectI* dstRoD) OVERRIDE FINAL
    {
        if (params.expandRod) {
            // the RoD grows only where the filter dilates:
            // for a negative size in Erode, for a positive size in Dilate
            const bool dilateX = dilate ? (params.sx > 0) : (params.sx < 0);
            const bool dilateY = dilate ? (params.sy > 0) : (params.sy < 0);
            int delta_pix_x = dilateX ? (int)std::ceil(std::abs(params.sx) * renderScale.x) : 0;
            int delta_pix_y = dilateY ? (int)std::ceil(std::abs(params.sy) * renderScale.y) : 0;
            cimgPadRect(srcRoD, delta_pix_x, delta_pix_y, dstRoD);

            return true;
        } else {
            return false;
        }
    }

    virtual void render(const OFX::RenderArguments &args,
                        const CImgErodeDilateParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        // positive sizes first (erode in Erode, dilate in Dilate), then negative sizes (the opposite operation)
        if ( (params.sx > 0) || (params.sy > 0) ) {
            morpho(cimg, !dilate,
                   (unsigned int)std::floor((std::max)(0, params.sx) * args.renderScale.x) * 2 + 1,
                   (unsigned int)std::floor((std::max)(0, params.sy) * args.renderScale.y) * 2 + 1 );
        }
        if ( abort() ) { return; }
        if ( (params.sx < 0) || (params.sy < 0) ) {
            morpho(cimg, dilate,
                   (unsigned int)std::floor((std::max)(0, -params.sx) * args.renderScale.x) * 2 + 1,
                   (unsigned int)std::floor((std::max)(0, -params.sy) * args.renderScale.y) * 2 + 1 );
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                            const CImgErodeDilateParams& params) OVERRIDE FINAL
    {
        return (std::floor(params.sx * args.renderScale.x) == 0 && std::floor(params.sy * args.renderScale.y) == 0);
    };

private:
    static void morpho(cimg_library::CImg<cimgpix_t>& cimg,
                       bool erode,
                       unsigned int sx,
                       unsigned int sy)
    {
        if (erode) {
            cimg.erode(sx, sy);
        } else {
            cimg.dilate(sx, sy);
        }
    }

    // params
    OFX::Int2DParam *_size;
    OFX::BooleanParam* _expandRod;
};


// one factory class per version: the Support library allows one instance of each factory class
template <bool dilate, unsigned int majorVersion>
class CImgErodeDilatePluginFactory
    : public OFX::PluginFactoryHelper<CImgErodeDilatePluginFactory<dilate, majorVersion> >
{
public:
    CImgErodeDilatePluginFactory(const std::string & id,
                                 unsigned int verMin)
        : OFX::PluginFactoryHelper<CImgErodeDilatePluginFactory<dilate, majorVersion> >(id, majorVersion, verMin)
    {
    }

    virtual void load() OVERRIDE FINAL {OFX::ofxsThreadSuiteCheck();}
    virtual void unload() OVERRIDE FINAL {}
    virtual void describe(OFX::ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context) OVERRIDE FINAL;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) OVERRIDE FINAL;
};

template <bool dilate, unsigned int majorVersion>
void
CImgErodeDilatePluginFactory<dilate, majorVersion>::describe(OFX::ImageEffectDescriptor& desc)
{
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }
    // basic labels
    desc.setLabel(dilate ? kDilatePluginName : kErodePluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(dilate ? kDilatePluginDescription : kErodePluginDescription);

    // add supported context
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(kHostFrameThreading);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NATRON
    if (!dilate) {
        // historically, only Erode sets the channel selector
        desc.setChannelSelector(OFX::ePixelComponentRGBA); // Enable alpha by default, so it works OK on masks
    }
#endif
}

template <bool dilate, unsigned int majorVersion>
void
CImgErodeDilatePluginFactory<dilate, majorVersion>::describeInContext(OFX::ImageEffectDescriptor& desc,
                                                        OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgErodeDilatePlugin<dilate>::describeInContextBegin(desc, context,
                                                                                           kSupportsRGBA,
                                                                                           kSupportsRGB,
                                                                                           kSupportsXY,
                                                                                           kSupportsAlpha,
                                                                                           kSupportsTiles,
                                                                                           /*processRGB=*/ true,
                                                                                           /*processAlpha*/ true,      // Enable alpha by default, so it works OK on masks
                                                                                           /*processIsSecret=*/ false);

    {
        OFX::Int2DParamDescriptor *param = desc.defineInt2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(-1000, -1000, 1000, 1000);
        param->setDisplayRange(-100, -100, 100, 100);
        const int sizeDefault = (majorVersion < kPluginVersionMajor) ? kParamSizeDefaultOld : kParamSizeDefault;
        param->setDefault(sizeDefault, sizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(dilate ? kDilateParamExpandRoDHint : kErodeParamExpandRoDHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgErodeDilatePlugin<dilate>::describeInContextEnd(desc, context, page);
}

template <bool dilate, unsigned int majorVersion>
OFX::ImageEffect*
CImgErodeDilatePluginFactory<dilate, majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                     OFX::ContextEnum /*context*/)
{
    return new CImgErodeDilatePlugin<dilate>(handle);
}

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // ifndef Misc_CImgErodeDilate_h
