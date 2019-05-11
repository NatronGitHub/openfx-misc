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
 * OFX CImgGuided plugin.
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

#define kPluginName          "SmoothGuidedCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Blur image, with the Guided Image filter.\n" \
    "The algorithm is described in: " \
    "He et al., \"Guided Image Filtering,\" " \
    "http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf\n" \
    "Uses the 'blur_guided' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgGuided"
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

#define kParamRadius "radius"
#define kParamRadiusLabel "Radius"
#define kParamRadiusHint "Radius of the spatial kernel (positional sigma), in pixel units (>=0)."
#define kParamRadiusDefault 5

#define kParamEpsilon "epsilon"
#define kParamEpsilonLabel "Smoothness"
#define kParamEpsilonHint "Regularization parameter. The actual guided filter parameter is epsilon^2)."
#define kParamEpsilonDefault 0.2

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations."
#define kParamIterationsDefault 1


/// Guided plugin
struct CImgGuidedParams
{
    int radius;
    double epsilon;
    int iterations;
};

class CImgGuidedPlugin
    : public CImgFilterPluginHelper<CImgGuidedParams, false>
{
public:

    CImgGuidedPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgGuidedParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _radius  = fetchIntParam(kParamRadius);
        _epsilon  = fetchDoubleParam(kParamEpsilon);
        _iterations = fetchIntParam(kParamIterations);
        assert(_radius && _epsilon && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgGuidedParams& params) OVERRIDE FINAL
    {
        _radius->getValueAtTime(time, params.radius);
        _epsilon->getValueAtTime(time, params.epsilon);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgGuidedParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil(params.radius * renderScale.x * params.iterations);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgGuidedParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.iterations <= 0) || (params.radius <= 0) ) {
            return;
        }
        for (int i = 0; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }

            // blur_guided was introduced in CImg 1.6.0 on Thu Oct 30 11:47:06 2014 +0100
            cimg.blur_guided( cimg, (float)(params.radius * args.renderScale.x), (float)(params.epsilon * params.epsilon) );
        }
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgGuidedParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0) || (params.radius == 0);
    };

private:

    // params
    IntParam *_radius;
    DoubleParam *_epsilon;
    IntParam *_iterations;
};


mDeclarePluginFactory(CImgGuidedPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgGuidedPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgGuidedPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgGuidedPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamRadius);
        param->setLabel(kParamRadiusLabel);
        param->setHint(kParamRadiusHint);
        param->setRange(0, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamRadiusDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEpsilon);
        param->setLabel(kParamEpsilonLabel);
        param->setHint(kParamEpsilonHint);
        param->setRange(0, 1.);
        param->setDisplayRange(0., 0.4);
        param->setDefault(kParamEpsilonDefault);
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

    CImgGuidedPlugin::describeInContextEnd(desc, context, page);
} // CImgGuidedPluginFactory::describeInContext

ImageEffect*
CImgGuidedPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgGuidedPlugin(handle);
}

static CImgGuidedPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
