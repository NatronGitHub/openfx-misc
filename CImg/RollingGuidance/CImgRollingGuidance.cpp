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
 * OFX CImgRollingGuidance plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#if cimg_version < 161
#error "This plugin requires CImg 1.6.1, please upgrade CImg."
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "SmoothRollingGuidanceCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Filter out details under a given scale using the Rolling Guidance filter.\n" \
    "Rolling Guidance is described fully in http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/\n" \
    "Iterates the 'blur_bilateral' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgRollingGuidance"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // The Rolling Guidance filter gives a global result, tiling is impossible
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

#define kParamSigmaS "sigma_s"
#define kParamSigmaSLabel "Spatial Std Dev"
#define kParamSigmaSHint "Standard deviation of the spatial kernel, in pixel units (>=0). Details smaller than this size are filtered out."
#define kParamSigmaSDefault 10.0

#define kParamSigmaR "sigma_r"
#define kParamSigmaRLabel "Value Std Dev"
#define kParamSigmaRHint "Standard deviation of the range kernel, in intensity units (>=0). A reasonable value is 1/10 of the intensity range. In the context of denoising, Liu et al. (\"Noise estimation from a single image\", CVPR2006) recommend a value of 1.95*sigma_n, where sigma_n is the local image noise."
#define kParamSigmaRDefault 0.1

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations of the rolling guidance filter. 1 corresponds to Gaussian smoothing. A reasonable value is 4."
#define kParamIterationsDefault 4


/// RollingGuidance plugin
struct CImgRollingGuidanceParams
{
    double sigma_s;
    double sigma_r;
    int iterations;
};

class CImgRollingGuidancePlugin
    : public CImgFilterPluginHelper<CImgRollingGuidanceParams, false>
{
public:

    CImgRollingGuidancePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgRollingGuidanceParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _sigma_s  = fetchDoubleParam(kParamSigmaS);
        _sigma_r  = fetchDoubleParam(kParamSigmaR);
        _iterations = fetchIntParam(kParamIterations);
        assert(_sigma_s && _sigma_r && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgRollingGuidanceParams& params) OVERRIDE FINAL
    {
        _sigma_s->getValueAtTime(time, params.sigma_s);
        _sigma_r->getValueAtTime(time, params.sigma_r);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgRollingGuidanceParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil( (params.sigma_s * 3.6) * renderScale.x * params.iterations);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgRollingGuidanceParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.iterations <= 0) || (params.sigma_s == 0.) ) {
            return;
        }
        // for a full description of the Rolling Guidance filter, see
        // http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/paper/%5BECCV2014%5DRollingGuidanceFilter_5M.pdf
        // http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/
        if (params.iterations == 1) {
            // Gaussian filter
            cimg.blur( (float)(params.sigma_s * args.renderScale.x), true, true );

            return;
        }
        // first iteration is Gaussian blur (equivalent to a bilateral filter with a constant image as the guide)
        cimg_library::CImg<cimgpix_t> guide = cimg.get_blur( (float)(params.sigma_s * args.renderScale.x), true, true );
        // next iterations use the bilateral filter
        for (int i = 1; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }
            // filter the original image using the updated guide
            guide = cimg.get_blur_bilateral(guide, (float)(params.sigma_s * args.renderScale.x), (float)params.sigma_r);
        }
        cimg = guide;
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgRollingGuidanceParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0 || params.sigma_s == 0.);
    };

private:

    // params
    DoubleParam *_sigma_s;
    DoubleParam *_sigma_r;
    IntParam *_iterations;
};


mDeclarePluginFactory(CImgRollingGuidancePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgRollingGuidancePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgRollingGuidancePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                                    ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgRollingGuidancePlugin::describeInContextBegin(desc, context,
                                                                                  kSupportsRGBA,
                                                                                  kSupportsRGB,
                                                                                  kSupportsXY,
                                                                                  kSupportsAlpha,
                                                                                  kSupportsTiles,
                                                                                  /*processRGB=*/ true,
                                                                                  /*processAlpha*/ false,
                                                                                  /*processIsSecret=*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigmaS);
        param->setLabel(kParamSigmaSLabel);
        param->setHint(kParamSigmaSHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 25);
        param->setDefault(kParamSigmaSDefault);
        param->setIncrement(0.1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigmaR);
        param->setLabel(kParamSigmaRLabel);
        param->setHint(kParamSigmaRHint);
        param->setRange(0, 10.0);
        param->setDisplayRange(0, 0.5);
        param->setDefault(kParamSigmaRDefault);
        param->setIncrement(0.005);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIterations);
        param->setLabel(kParamIterationsLabel);
        param->setHint(kParamIterationsHint);
        param->setRange(0, 10);
        param->setDisplayRange(0, 10);
        param->setDefault(kParamIterationsDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgRollingGuidancePlugin::describeInContextEnd(desc, context, page);
} // CImgRollingGuidancePluginFactory::describeInContext

ImageEffect*
CImgRollingGuidancePluginFactory::createInstance(OfxImageEffectHandle handle,
                                                 ContextEnum /*context*/)
{
    return new CImgRollingGuidancePlugin(handle);
}

static CImgRollingGuidancePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
