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

/*
 * OFX CImgErode plugin.
 */

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

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ErodeCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Erode (or dilate) input stream by a rectangular structuring element of specified size and Neumann boundary conditions (pixels out of the image get the value of the nearest pixel).\n" \
    "A negative size will perform a dilation instead of an erosion.\n" \
    "Different sizes can be given for the x and y axis.\n" \
    "Uses the 'erode' and 'dilate' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgErode"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: add expand rod parameter
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
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
#define kParamSizeHint "Width/height of the rectangular structuring element is 2*size+1, in pixel units (>=0)."
#define kParamSizeDefault 1

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by 2*size pixels if size is negative"


/// Erode plugin
struct CImgErodeParams
{
    int sx;
    int sy;
    bool expandRod;
};

class CImgErodePlugin
    : public CImgFilterPluginHelper<CImgErodeParams, false>
{
public:

    CImgErodePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgErodeParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _size  = fetchInt2DParam(kParamSize);
        _expandRod = fetchBooleanParam(kParamExpandRoD);
        assert(_size && _expandRod);
    }

    virtual void getValuesAtTime(double time,
                                 CImgErodeParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.sx, params.sy);
        _expandRod->getValueAtTime(time, params.expandRod);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgErodeParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix_x = (int)std::ceil(std::abs(params.sx) * renderScale.x);
        int delta_pix_y = (int)std::ceil(std::abs(params.sy) * renderScale.y);

        roi->x1 = rect.x1 - delta_pix_x;
        roi->x2 = rect.x2 + delta_pix_x;
        roi->y1 = rect.y1 - delta_pix_y;
        roi->y2 = rect.y2 + delta_pix_y;
    }

    bool getRegionOfDefinition(const OfxRectI& srcRoD,
                               const OfxPointD& renderScale,
                               const CImgErodeParams& params,
                               OfxRectI* dstRoD) OVERRIDE FINAL
    {
        if (params.expandRod) {
            int delta_pix_x = params.sx < 0 ? (int)std::ceil(std::abs(params.sx) * renderScale.x) : 0;
            int delta_pix_y = params.sy < 0 ? (int)std::ceil(std::abs(params.sy) * renderScale.y) : 0;
            *dstRoD = srcRoD;
            dstRoD->x1 = dstRoD->x1 - delta_pix_x;
            dstRoD->x2 = dstRoD->x2 + delta_pix_x;
            dstRoD->y1 = dstRoD->y1 - delta_pix_y;
            dstRoD->y2 = dstRoD->y2 + delta_pix_y;

            return true;
        } else {
            return false;
        }
    }

    virtual void render(const RenderArguments &args,
                        const CImgErodeParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.sx > 0) || (params.sy > 0) ) {
            cimg.erode( (unsigned int)std::floor((std::max)(0, params.sx) * args.renderScale.x) * 2 + 1,
                        (unsigned int)std::floor((std::max)(0, params.sy) * args.renderScale.y) * 2 + 1 );
        }
        if ( abort() ) { return; }
        if ( (params.sx < 0) || (params.sy < 0) ) {
            cimg.dilate( (unsigned int)std::floor((std::max)(0, -params.sx) * args.renderScale.x) * 2 + 1,
                         (unsigned int)std::floor((std::max)(0, -params.sy) * args.renderScale.y) * 2 + 1 );
        }
    }

    virtual bool isIdentity(const IsIdentityArguments &args,
                            const CImgErodeParams& params) OVERRIDE FINAL
    {
        return (std::floor(params.sx * args.renderScale.x) == 0 && std::floor(params.sy * args.renderScale.y) == 0);
    };

private:

    // params
    Int2DParam *_size;
    BooleanParam* _expandRod;
};


mDeclarePluginFactory(CImgErodePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgErodePluginFactory::describe(ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

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
    desc.setChannelSelector(ePixelComponentRGBA); // Enable alpha by default, so it works OK on masks
#endif
}

void
CImgErodePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                          ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgErodePlugin::describeInContextBegin(desc, context,
                                                                        kSupportsRGBA,
                                                                        kSupportsRGB,
                                                                        kSupportsXY,
                                                                        kSupportsAlpha,
                                                                        kSupportsTiles,
                                                                        /*processRGB=*/ true,
                                                                        /*processAlpha*/ true,      // Enable alpha by default, so it works OK on masks
                                                                        /*processIsSecret=*/ false);

    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(-1000, -1000, 1000, 1000);
        param->setDisplayRange(-100, -100, 100, 100);
        param->setDefault(kParamSizeDefault, kParamSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgErodePlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgErodePluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new CImgErodePlugin(handle);
}

static CImgErodePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
