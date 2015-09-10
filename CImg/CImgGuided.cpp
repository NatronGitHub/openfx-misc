/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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

#include "CImgGuided.h"

#include <memory>
#include <cmath>
#include <cstring>
#ifdef _WINDOWS
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

#define kPluginName          "GuidedCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Blur image, with the Guided Image filter.\n" \
"The algorithm is described in: " \
"He et al., \"Guided Image Filtering,\" " \
"http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf\n" \
"Uses the 'blur_guided' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgGuided"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#ifdef cimg_use_openmp
#define kHostFrameThreading false
#else
#define kHostFrameThreading true
#endif
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamRadius "radius"
#define kParamRadiusLabel "Radius"
#define kParamRadiusHint "Radius of the spatial kernel (positional sigma), in pixel units (>=0)."
#define kParamRadiusDefault 5

#define kParamEpsilon "epsilon"
#define kParamEpsilonLabel "Epsilon"
#define kParamEpsilonHint "Regularization parameter. The actual guided filter parameter is epsilon^2)."
#define kParamEpsilonDefault 0.2

using namespace OFX;

/// Guided plugin
struct CImgGuidedParams
{
    int radius;
    double epsilon;
};

class CImgGuidedPlugin : public CImgFilterPluginHelper<CImgGuidedParams,false>
{
public:

    CImgGuidedPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgGuidedParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _radius  = fetchIntParam(kParamRadius);
        _epsilon  = fetchDoubleParam(kParamEpsilon);
        assert(_radius && _epsilon);
    }

    virtual void getValuesAtTime(double time, CImgGuidedParams& params) OVERRIDE FINAL
    {
        _radius->getValueAtTime(time, params.radius);
        _epsilon->getValueAtTime(time, params.epsilon);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgGuidedParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil(params.radius * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgGuidedParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.radius == 0) {
            return;
        }
        // blur_guided was introduced in CImg 1.6.0 on Thu Oct 30 11:47:06 2014 +0100
        cimg.blur_guided(cimg, (float)(params.radius * args.renderScale.x), (float)(params.epsilon*params.epsilon));
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgGuidedParams& params) OVERRIDE FINAL
    {
        return (params.radius == 0);
    };

private:

    // params
    OFX::IntParam *_radius;
    OFX::DoubleParam *_epsilon;
};


mDeclarePluginFactory(CImgGuidedPluginFactory, {}, {});

void CImgGuidedPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgGuidedPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgGuidedPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamRadius);
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
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEpsilon);
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

    CImgGuidedPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgGuidedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgGuidedPlugin(handle);
}


void getCImgGuidedPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgGuidedPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
