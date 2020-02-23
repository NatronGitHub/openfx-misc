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
 * OFX CImgInpaint plugin.
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

#ifdef PLUGIN_PACK_GPL2

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "InpaintCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Inpaint (a.k.a. content-aware fill) the areas indicated by the Mask input using patch-based inpainting.\n" \
    "Be aware that this filter may produce different results on each frame of a video, even if there is little change in the video content. To inpaint areas with lots of details, it may be better to inpaint on a single frame and paste the inpainted area on other frames (if a transform is also required to match the other frames, it may be computed by tracking).\n" \
    "\n" \
    "A tutorial on using this filter can be found at http://blog.patdavid.net/2014/02/getting-around-in-gimp-gmic-inpainting.html\n" \
    "The algorithm is described in the two following publications:\n" \
    "\"A Smarter Examplar-based Inpainting Algorithm using Local and Global Heuristics " \
    "for more Geometric Coherence.\" " \
    "(M. Daisy, P. Buyssens, D. Tschumperlé, O. Lezoray). " \
    "IEEE International Conference on Image Processing (ICIP'14), Paris/France, Oct. 2014\n" \
    "and\n" \
    "\"A Fast Spatial Patch Blending Algorithm for Artefact Reduction in Pattern-based " \
    "Image Inpainting.\" " \
    "(M. Daisy, D. Tschumperlé, O. Lezoray). " \
    "SIGGRAPH Asia 2013 Technical Briefs, Hong-Kong, November 2013.\n" \
    "\n" \
    "Uses the 'inpaint' plugin from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu). " \
    "The 'inpaint' CImg plugin is distributed under the CeCILL (compatible with the GNU GPL) license."

#define kPluginIdentifier    "eu.cimg.Inpaint"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // requires the whole image to search for patches, which may be far away (how far exactly?)
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

#define kParamPatchSize "patchSize"
#define kParamPatchSizeLabel "Patch Size" //, "Patch size in pixels."
#define kParamPatchSizeDefault 7 // 1-64

#define kParamLookupSize "lookupSize"
#define kParamLookupSizeLabel "Lookup Size" //, "Lookup region size in pixels."
#define kParamLookupSizeDefault 16 // 1-32

#define kParamLookupFactor "lookupFactor"
#define kParamLookupFactorLabel "Lookup Factor"
#define kParamLookupFactorDefault 0.1 // 0-1

#define kParamBlendSize "blendSize"
#define kParamBlendSizeLabel "Blend Size"
#define kParamBlendSizeDefault 1.2 // 0-5

#define kParamBlendThreshold "blendThreshold"
#define kParamBlendThresholdLabel "Blend Threshold"
#define kParamBlendThresholdDefault 0. // 0-1

#define kParamBlendDecay "blendDecay"
#define kParamBlendDecayLabel "Blend Decay"
#define kParamBlendDecayDefault 0.05 // 0-0.5

#define kParamBlendScales "blendScales"
#define kParamBlendScalesLabel "Blend Scales"
#define kParamBlendScalesDefault 10 // 1-20 (int)

#define kParamIsBlendOuter "isBlendOuter"
#define kParamIsBlendOuterLabel "Allow Outer Blending"
#define kParamIsBlendOuterDefault true


/// Inpaint plugin
struct CImgInpaintParams
{
    int patch_size;
    double lookup_size;
    double lookup_factor;
    //int lookup_increment=1;
    double blend_size;
    double blend_threshold;
    double blend_decay;
    int blend_scales;
    bool is_blend_outer;
};

class CImgInpaintPlugin
    : public CImgFilterPluginHelper<CImgInpaintParams, false>
{
public:

    CImgInpaintPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgInpaintParams, false>(handle, /*usesMask=*/true, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _patch_size      = fetchIntParam(kParamPatchSize);
        _lookup_size     = fetchDoubleParam(kParamLookupSize);
        _lookup_factor   = fetchDoubleParam(kParamLookupFactor);
        _blend_size      = fetchDoubleParam(kParamBlendSize);
        _blend_threshold = fetchDoubleParam(kParamBlendThreshold);
        _blend_decay     = fetchDoubleParam(kParamBlendDecay);
        _blend_scales    = fetchIntParam(kParamBlendScales);
        _is_blend_outer  = fetchBooleanParam(kParamIsBlendOuter);
        assert(_patch_size && _lookup_size && _lookup_factor && _blend_size && _blend_threshold && _blend_decay && _blend_scales && _is_blend_outer);
    }

    virtual void getValuesAtTime(double time,
                                 CImgInpaintParams& params) OVERRIDE FINAL
    {
        _patch_size->getValueAtTime(time, params.patch_size);
        _lookup_size->getValueAtTime(time, params.lookup_size);
        _lookup_factor->getValueAtTime(time, params.lookup_factor);
        _blend_size->getValueAtTime(time, params.blend_size);
        _blend_threshold->getValueAtTime(time, params.blend_threshold);
        _blend_decay->getValueAtTime(time, params.blend_decay);
        _blend_scales->getValueAtTime(time, params.blend_scales);
        _is_blend_outer->getValueAtTime(time, params.is_blend_outer);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgInpaintParams& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 0; // does not support tiles anyway
        assert(!kSupportsTiles);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgInpaintParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& mask,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        printf("%dx%d\n",cimg.width(), cimg.height());
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.patch_size <= 0) || (params.lookup_size <= 0.) || cimg.is_empty() ) {
            return;
        }
        // binarize the mask (because inpaint casts it to an int)
        cimg_for(mask, ptrd, cimgpix_t) {
            *ptrd = (*ptrd > 0);
        }
        cimg.inpaint_patch(mask,
                           static_cast<int>( std::ceil(params.patch_size * args.renderScale.x) ),
                           static_cast<int>( std::ceil(params.patch_size * params.lookup_size * args.renderScale.x) ),
                           static_cast<float>(params.lookup_factor),
                           /*params.lookup_increment=*/1,
                           static_cast<int>(params.blend_size * params.patch_size * args.renderScale.x),
                           static_cast<float>(params.blend_threshold),
                           static_cast<float>(params.blend_decay),
                           static_cast<unsigned int>(params.blend_scales),
                           params.is_blend_outer);
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgInpaintParams& params) OVERRIDE FINAL
    {
        return (params.patch_size <= 0) || (params.lookup_size <= 0.);
    };

    /*
    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        CImgFilterPluginHelper<CImgInpaintParams, false>::changedParam(args, paramName);
    }
     */

private:

    // params
    IntParam *_patch_size;
    DoubleParam *_lookup_size;
    DoubleParam *_lookup_factor;
    DoubleParam *_blend_size;
    DoubleParam *_blend_threshold;
    DoubleParam *_blend_decay;
    IntParam *_blend_scales;
    BooleanParam *_is_blend_outer;
};


mDeclarePluginFactory(CImgInpaintPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgInpaintPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgInpaintPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgInpaintPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamPatchSize);
        param->setLabel(kParamPatchSizeLabel);
        param->setRange(1, 64);
        param->setDisplayRange(1, 64);
        param->setDefault(kParamPatchSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamLookupSize);
        param->setLabel(kParamLookupSizeLabel);
        param->setRange(1., 32.);
        param->setDisplayRange(1., 32.);
        param->setDefault(kParamLookupSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamLookupFactor);
        param->setLabel(kParamLookupFactorLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamLookupFactorDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBlendSize);
        param->setLabel(kParamBlendSizeLabel);
        param->setRange(0., 5.);
        param->setDisplayRange(0., 5.);
        param->setDefault(kParamBlendSizeDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBlendThreshold);
        param->setLabel(kParamBlendThresholdLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamBlendThresholdDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBlendDecay);
        param->setLabel(kParamBlendDecayLabel);
        param->setRange(0., 0.5);
        param->setDisplayRange(0., 0.5);
        param->setDefault(kParamBlendDecayDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamBlendScales);
        param->setLabel(kParamBlendScalesLabel);
        param->setRange(1, 20);
        param->setDisplayRange(1, 20);
        param->setDefault(kParamBlendScalesDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamIsBlendOuter);
        param->setLabel(kParamIsBlendOuterLabel);
        param->setDefault(kParamIsBlendOuterDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgInpaintPlugin::describeInContextEnd(desc, context, page);
} // CImgInpaintPluginFactory::describeInContext

ImageEffect*
CImgInpaintPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgInpaintPlugin(handle);
}

static CImgInpaintPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // PLUGIN_PACK_GPL2
