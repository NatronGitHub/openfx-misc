/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
 * OFX CImgPlasma plugin.
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

#define kPluginName          "PlasmaCImg"
#define kPluginGrouping      "Draw"
#define kPluginDescription \
    "Draw a random plasma texture (using the mid-point algorithm).\n" \
    "Note that each render scale gives a different noise, but the image rendered at full scale always has the same noise at a given time. Noise can be modulated using the 'seed' parameter.\n" \
    "Uses the 'draw_plasma' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgPlasma"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // Plasma effect can only be computed on the whole image
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
#define kSupportsXY true
#define kSupportsAlpha true

#define kParamAlpha "alpha"
#define kParamAlphaLabel "Alpha"
#define kParamAlphaHint "Alpha-parameter, in intensity units (>=0)."
#define kParamAlphaDefault 0.002 // 0.5/255
#define kParamAlphaMin 0.
#define kParamAlphaMax 0.02 // 5./255
#define kParamAlphaIncrement 0.0005

#define kParamBeta "beta"
#define kParamBetaLabel "Beta"
#define kParamBetaHint "Beta-parameter, in intensity units (>=0)."
#define kParamBetaDefault 0.
#define kParamBetaMin 0.
#define kParamBetaMax 0.5 // 100./255
#define kParamBetaIncrement 0.01

#define kParamScale "scale"
#define kParamScaleLabel "Scale"
#define kParamScaleHint "Noise scale, as a power of two (>=0)."
#define kParamScaleDefault 8
#define kParamScaleMin 2
#define kParamScaleMax 10

#define kParamSeed "seed"
#define kParamSeedLabel "Random Seed"
#define kParamSeedHint "Random seed used to generate the image. Time value is added to this seed, to get a time-varying effect."


/// Plasma plugin
struct CImgPlasmaParams
{
    double alpha;
    double beta;
    int scale;
    int seed;
};

class CImgPlasmaPlugin
    : public CImgFilterPluginHelper<CImgPlasmaParams, true>
{
public:

    CImgPlasmaPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgPlasmaParams, true>(handle, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true, /*defaultProcessAlphaOnRGBA=*/ false)
    {
        _alpha  = fetchDoubleParam(kParamAlpha);
        _beta  = fetchDoubleParam(kParamBeta);
        _scale = fetchIntParam(kParamScale);
        _seed = fetchIntParam(kParamSeed);
        assert(_alpha && _beta && _scale);
    }

    virtual void getValuesAtTime(double time,
                                 CImgPlasmaParams& params) OVERRIDE FINAL
    {
        _alpha->getValueAtTime(time, params.alpha);
        _beta->getValueAtTime(time, params.beta);
        _scale->getValueAtTime(time, params.scale);
        _seed->getValueAtTime(time, params.seed);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgPlasmaParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 1 << std::max( 0, params.scale - (int)OFX::Coords::mipmapLevelFromScale(renderScale.x) );

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args,
                        const CImgPlasmaParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        cimg_library::cimg::srand( (unsigned int)args.time + (unsigned int)params.seed );

        cimg.draw_plasma( (float)params.alpha / args.renderScale.x, (float)params.beta / args.renderScale.x, std::max( 0, params.scale - (int)OFX::Coords::mipmapLevelFromScale(args.renderScale.x) ) );
    }

    //virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgPlasmaParams& params) OVERRIDE FINAL
    //{
    //    return (params.scale - (int)OFX::Coords::mipmapLevelFromScale(args.renderScale.x) <= 0);
    //};

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        clipPreferences.setOutputFrameVarying(true);
        clipPreferences.setOutputHasContinousSamples(true);
    }

private:

    // params
    OFX::DoubleParam *_alpha;
    OFX::DoubleParam *_beta;
    OFX::IntParam *_scale;
    OFX::IntParam *_seed;
};


mDeclarePluginFactory(CImgPlasmaPluginFactory, {}, {});

void
CImgPlasmaPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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
CImgPlasmaPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                           OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgPlasmaPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsXY,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles,
                                                                              /*processRGB=*/ true,
                                                                              /*processAlpha*/ false,
                                                                              /*processIsSecret=*/ false);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
        param->setLabel(kParamAlphaLabel);
        param->setHint(kParamAlphaHint);
        param->setRange(kParamAlphaMin, kParamAlphaMax);
        param->setDisplayRange(kParamAlphaMin, kParamAlphaMax);
        param->setDefault(kParamAlphaDefault);
        param->setIncrement(kParamAlphaIncrement);
        param->setDigits(4);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBeta);
        param->setLabel(kParamBetaLabel);
        param->setHint(kParamBetaHint);
        param->setRange(kParamBetaMin, kParamBetaMax);
        param->setDisplayRange(kParamBetaMin, kParamBetaMax);
        param->setDefault(kParamBetaDefault);
        param->setIncrement(kParamBetaIncrement);
        param->setDigits(2);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamScale);
        param->setLabel(kParamScaleLabel);
        param->setHint(kParamScaleHint);
        param->setRange(kParamScaleMin, kParamScaleMax);
        param->setDisplayRange(kParamScaleMin, kParamScaleMax);
        param->setDefault(kParamScaleDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamSeed);
        param->setLabel(kParamSeedLabel);
        param->setHint(kParamSeedHint);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgPlasmaPlugin::describeInContextEnd(desc, context, page);
} // CImgPlasmaPluginFactory::describeInContext

OFX::ImageEffect*
CImgPlasmaPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        OFX::ContextEnum /*context*/)
{
    return new CImgPlasmaPlugin(handle);
}

static CImgPlasmaPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
