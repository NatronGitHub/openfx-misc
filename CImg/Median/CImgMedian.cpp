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
 * OFX CImgMedian plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <cfloat> // DBL_MAX
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

#define kPluginName          "MedianCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Apply a median filter to input images. Pixel values within a square box of the given size around the current pixel are sorted, and the median value is output if it does not differ from the current value by more than the given. Median filtering is performed per-channel.\n" \
    "Uses the 'blur_median' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgMedian"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
#define kParamSizeHint "Width and height of the structuring element is 2*size+1, in pixel units (>=0)."
#define kParamSizeDefault 1

#define kParamThreshold "threshold"
#define kParamThresholdLabel "Threshold"
#define kParamThresholdHint "Threshold used to discard pixels too far from the current pixel value in the median computation. A threshold value of zero disables the threshold."
#define kParamThresholdDefault 1


/// Median plugin
struct CImgMedianParams
{
    int size;
    double threshold;
};

class CImgMedianPlugin
    : public CImgFilterPluginHelper<CImgMedianParams, false>
{
public:

    CImgMedianPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgMedianParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _size  = fetchIntParam(kParamSize);
        _threshold  = fetchDoubleParam(kParamThreshold);
        assert(_size);
    }

    virtual void getValuesAtTime(double time,
                                 CImgMedianParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.size);
        _threshold->getValueAtTime(time, params.threshold);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgMedianParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix_x = (int)std::ceil(std::abs(params.size) * renderScale.x);
        int delta_pix_y = (int)std::ceil(std::abs(params.size) * renderScale.y);

        roi->x1 = rect.x1 - delta_pix_x;
        roi->x2 = rect.x2 + delta_pix_x;
        roi->y1 = rect.y1 - delta_pix_y;
        roi->y2 = rect.y2 + delta_pix_y;
    }

    virtual void render(const RenderArguments &args,
                        const CImgMedianParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        cimg.blur_median( static_cast<unsigned int>( std::floor((std::max)(1, params.size) * args.renderScale.x) ) * 2 + 1, static_cast<float>(params.threshold) );
    }

    virtual bool isIdentity(const IsIdentityArguments &args,
                            const CImgMedianParams& params) OVERRIDE FINAL
    {
        return (std::floor(params.size * args.renderScale.x) == 0);
    };

private:

    // params
    IntParam *_size;
    DoubleParam *_threshold;
};


mDeclarePluginFactory(CImgMedianPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgMedianPluginFactory::describe(ImageEffectDescriptor& desc)
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
}

void
CImgMedianPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgMedianPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(1, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamThreshold);
        param->setLabel(kParamThresholdLabel);
        param->setHint(kParamThresholdHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(0, 1);
        param->setDefault(0.);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgMedianPlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgMedianPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgMedianPlugin(handle);
}

static CImgMedianPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
