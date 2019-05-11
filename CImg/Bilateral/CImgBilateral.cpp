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
 * OFX CImgBilateral plugin.
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
#include "CImgOperator.h"

#if cimg_version < 160
#error "The bilateral filter before CImg 1.6.0 produces incorrect results, please upgrade CImg."
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "SmoothBilateralCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Blur input stream by bilateral filtering.\n" \
    "Uses the 'blur_bilateral' function from the CImg library.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Bilateral\n" \
    "\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgBilateral"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kPluginGuidedName          "SmoothBilateralGuidedCImg"
#define kPluginGuidedIdentifier    "net.sf.cimg.CImgBilateralGuided"
#define kPluginGuidedDescription \
    "Apply joint/cross bilateral filtering on image A, guided by the intensity differences of image B. " \
    "Uses the 'blur_bilateral' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

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

#define kParamSigmaS "sigma_s"
#define kParamSigmaSLabel "Spatial Std Dev"
#define kParamSigmaSHint "Standard deviation of the spatial kernel (positional sigma), in pixel units (>=0). A reasonable value is 1/16 of the image dimension. Small values (1 pixel and below) will slow down filtering."
#define kParamSigmaSDefault 10.

#define kParamSigmaR "sigma_r"
#define kParamSigmaRLabel "Value Std Dev"
#define kParamSigmaRHint "Standard deviation of the range kernel (color sigma), in intensity units (>=0). A reasonable value is 1/10 of the intensity range. In the context of denoising, Liu et al. (\"Noise estimation from a single image\", CVPR2006) recommend a value of 1.95*sigma_n, where sigma_n is the local image noise. Small values (1/256 of the intensity range and below) will slow down filtering."
#define kParamSigmaRDefault 0.3

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations."
#define kParamIterationsDefault 2

#define kClipImage kOfxImageEffectSimpleSourceClipName
#define kClipGuide "Guide"
#define kClipGuideHint "The guide image indicates where similar pixels are located in each neighborhood. The neighborhood of a pixel consists of pixels that are within a neighborhood of side sigma_s, which have an intensity/value in the Guide image that is within a range of size sigma_r around the intensity of the considered pixel."


/// Bilateral plugin
struct CImgBilateralParams
{
    double sigma_s;
    double sigma_r;
    int iterations;
};

class CImgBilateralPlugin
    : public CImgFilterPluginHelper<CImgBilateralParams, false>
{
public:

    CImgBilateralPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgBilateralParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _sigma_s  = fetchDoubleParam(kParamSigmaS);
        _sigma_r  = fetchDoubleParam(kParamSigmaR);
        _iterations = fetchIntParam(kParamIterations);
        assert(_sigma_s && _sigma_r && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgBilateralParams& params) OVERRIDE FINAL
    {
        _sigma_s->getValueAtTime(time, params.sigma_s);
        _sigma_r->getValueAtTime(time, params.sigma_r);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgBilateralParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil( (params.sigma_s * 3.6) * renderScale.x * params.iterations);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgBilateralParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.sigma_s <= 0. || params.sigma_r <= 0.) {
            return;
        }
        for (int i = 0; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }
            cimg.blur_bilateral(cimg, (float)(params.sigma_s * args.renderScale.x), (float)params.sigma_r);
        }
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgBilateralParams& params) OVERRIDE FINAL
    {
        return (params.sigma_s <= 0. || params.sigma_r <= 0.);
    };

private:

    // params
    DoubleParam *_sigma_s;
    DoubleParam *_sigma_r;
    IntParam *_iterations;
};

class CImgBilateralGuidedPlugin
    : public CImgOperatorPluginHelper<CImgBilateralParams>
{
public:

    CImgBilateralGuidedPlugin(OfxImageEffectHandle handle)
        : CImgOperatorPluginHelper<CImgBilateralParams>(handle, kClipImage, kClipGuide, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true, /*defaultProcessAlphaOnRGBA=*/ false)
    {
        _sigma_s  = fetchDoubleParam(kParamSigmaS);
        _sigma_r  = fetchDoubleParam(kParamSigmaR);
        _iterations = fetchIntParam(kParamIterations);
        assert(_sigma_s && _sigma_r && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgBilateralParams& params) OVERRIDE FINAL
    {
        _sigma_s->getValueAtTime(time, params.sigma_s);
        _sigma_r->getValueAtTime(time, params.sigma_r);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgBilateralParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil( (params.sigma_s * 3.6) * renderScale.x * params.iterations);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const cimg_library::CImg<cimgpix_t>& srcA,
                        const cimg_library::CImg<cimgpix_t>& srcB,
                        const RenderArguments &args,
                        const CImgBilateralParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& dst) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.iterations <= 0) || (params.sigma_s <= 0.) || (params.sigma_r <= 0.)) {
            return;
        }

        for (int i = 0; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }

            if (i == 0) {
                dst = srcA.get_blur_bilateral(srcB, (float)(params.sigma_s * args.renderScale.x), (float)params.sigma_r);
            } else {
                dst.blur_bilateral(srcB, (float)(params.sigma_s * args.renderScale.x), (float)params.sigma_r);
            }
        }
    }

    virtual int isIdentity(const IsIdentityArguments & /*args*/,
                           const CImgBilateralParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0) || (params.sigma_s <= 0.) || (params.sigma_r <= 0.);
    };

private:

    // params
    DoubleParam *_sigma_s;
    DoubleParam *_sigma_r;
    IntParam *_iterations;
};

mDeclarePluginFactory(CImgBilateralPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgBilateralPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgBilateralPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                              ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgBilateralPlugin::describeInContextBegin(desc, context,
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
        param->setRange(0, 1000.);
        param->setDisplayRange(0.0, 10.);
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
        param->setRange(0, 100000.);
        param->setDisplayRange(0., 1.);
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

    CImgBilateralPlugin::describeInContextEnd(desc, context, page);
} // CImgBilateralPluginFactory::describeInContext

ImageEffect*
CImgBilateralPluginFactory::createInstance(OfxImageEffectHandle handle,
                                           ContextEnum /*context*/)
{
    return new CImgBilateralPlugin(handle);
}

mDeclarePluginFactory(CImgBilateralGuidedPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
CImgBilateralGuidedPluginFactory::describe(ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginGuidedName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginGuidedDescription);

    // add supported context
    //desc.addSupportedContext(eContextFilter);
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
CImgBilateralGuidedPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                                    ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgBilateralGuidedPlugin::describeInContextBegin(desc, context,
                                                                                  kClipImage,
                                                                                  NULL /*kClipImageHint*/,
                                                                                  kClipGuide,
                                                                                  kClipGuideHint,
                                                                                  kSupportsRGBA,
                                                                                  kSupportsRGB,
                                                                                  kSupportsXY,
                                                                                  kSupportsAlpha,
                                                                                  kSupportsTiles,
                                                                                  /*processRGB =*/ true,
                                                                                  /*processAlpha =*/ false,
                                                                                  /*processIsSecret =*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigmaS);
        param->setLabel(kParamSigmaSLabel);
        param->setHint(kParamSigmaSHint);
        param->setRange(0, 1000.);
        param->setDisplayRange(0.0, 10.);
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
        param->setRange(0, 100000.);
        param->setDisplayRange(0., 1.);
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

    CImgBilateralGuidedPlugin::describeInContextEnd(desc, context, page);
} // CImgBilateralGuidedPluginFactory::describeInContext

ImageEffect*
CImgBilateralGuidedPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                 ContextEnum /*context*/)
{
    return new CImgBilateralGuidedPlugin(handle);
}

static CImgBilateralPluginFactory p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static CImgBilateralGuidedPluginFactory p2(kPluginGuidedIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
