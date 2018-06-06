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
 * OFX CImgPlasma plugin.
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

#define kPluginName          "PlasmaCImg"
#define kPluginGrouping      "Draw"
#define kPluginDescription \
    "Draw a random plasma texture (using the mid-point algorithm).\n" \
    "\n" \
    "Uses the 'draw_plasma' function from the CImg library, modified so that noise is reproductible at each render..\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

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

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "Offset to add to the plasma noise."
#define kParamOffsetGeneratorDefault 0.5

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different noise."

#define kParamStaticSeed "staticSeed"
#define kParamStaticSeedLabel "Static Seed"
#define kParamStaticSeedHint "When enabled, the dither pattern remains the same for every frame producing a constant noise effect."

#define cimg_noise_internal

#ifdef cimg_noise_internal

// the following is cimg_library::CImg::draw_plasma(), but with a fixed seed and pseudo-random function
//! Add random noise to pixel values.

#define T cimgpix_t
#define Tfloat cimgpixfloat_t
using namespace cimg_library;

// DIfferences with the original cimg_library::CImg::noise:
// - static function
// - replaced *this with img
// - replaced cimg::rand with cimg_rand, etc.

//! Draw a random plasma texture.
/**
 \param alpha Alpha-parameter.
 \param beta Beta-parameter.
 \param scale Scale-parameter.
 \note Use the mid-point algorithm to render.
 **/
CImg<T>&
draw_plasma(CImg<T>&img, const float alpha/*=1*/, const float beta/*=0*/, const unsigned int scale/*=8*/, unsigned int seed, int x_1, int y_1)
{
    if (img.is_empty()) {
        return img;
    }
    const int w = img.width();
    const int h = img.height();
    const Tfloat m = (Tfloat)cimg::type<T>::min();
    const Tfloat M = (Tfloat)cimg::type<T>::max();
    cimg_forZC(img,z,c) {
        CImg<T> ref = img.get_shared_slice(z,c);
        for (int delta = 1<<(std::min)(scale,31U); delta>1; delta>>=1) {
            const int delta2 = delta>>1;
            const float r = alpha*delta + beta;

            // Square step.
            for (int y0 = 0; y0<h; y0+=delta)
                for (int x0 = 0; x0<w; x0+=delta) {
                    const int x1 = (x0 + delta)%w;
                    const int y1 = (y0 + delta)%h;
                    const int xc = (x0 + delta2)%w;
                    const int yc = (y0 + delta2)%h;
                    const Tfloat val = (Tfloat)(0.25f*(ref(x0,y0) + ref(x0,y1) + ref(x0,y1) + ref(x1,y1)) +
                                                r*cimg_rand(seed, xc + x_1, yc + y_1, c,-1,1));
                    ref(xc,yc) = (T)(val<m?m:val>M?M:val);
                }

            // Diamond steps.
            for (int y = -delta2; y<h; y+=delta)
                for (int x0=0; x0<w; x0+=delta) {
                    const int y0 = cimg::mod(y,h);
                    const int x1 = (x0 + delta)%w;
                    const int y1 = (y + delta)%h;
                    const int xc = (x0 + delta2)%w;
                    const int yc = (y + delta2)%h;
                    const Tfloat val = (Tfloat)(0.25f*(ref(xc,y0) + ref(x0,yc) + ref(xc,y1) + ref(x1,yc)) +
                                                r*cimg_rand(seed, xc + x_1, yc + y_1, c,-1,1));
                    ref(xc,yc) = (T)(val<m?m:val>M?M:val);
                }
            for (int y0 = 0; y0<h; y0+=delta)
                for (int x = -delta2; x<w; x+=delta) {
                    const int x0 = cimg::mod(x,w);
                    const int x1 = (x + delta)%w;
                    const int y1 = (y0 + delta)%h;
                    const int xc = (x + delta2)%w;
                    const int yc = (y0 + delta2)%h;
                    const Tfloat val = (Tfloat)(0.25f*(ref(xc,y0) + ref(x0,yc) + ref(xc,y1) + ref(x1,yc)) +
                                                r*cimg_rand(seed, xc + x_1, yc + y_1, c,-1,1));
                    ref(xc,yc) = (T)(val<m?m:val>M?M:val);
                }
            for (int y = -delta2; y<h; y+=delta)
                for (int x = -delta2; x<w; x+=delta) {
                    const int x0 = cimg::mod(x,w);
                    const int y0 = cimg::mod(y,h);
                    const int x1 = (x + delta)%w;
                    const int y1 = (y + delta)%h;
                    const int xc = (x + delta2)%w;
                    const int yc = (y + delta2)%h;
                    const Tfloat val = (Tfloat)(0.25f*(ref(xc,y0) + ref(x0,yc) + ref(xc,y1) + ref(x1,yc)) +
                                                r*cimg_rand(seed, xc + x_1, yc + y_1, c,-1,1));
                    ref(xc,yc) = (T)(val<m?m:val>M?M:val);
                }
        }
    }
    return img;
}
#endif

/// Plasma plugin
struct CImgPlasmaParams
{
    double alpha;
    double beta;
    int scale;
    double offset;
    int seed;
    bool staticSeed;
};

class CImgPlasmaPlugin
    : public CImgFilterPluginHelper<CImgPlasmaParams, true>
{
public:

    CImgPlasmaPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgPlasmaParams, true>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _alpha  = fetchDoubleParam(kParamAlpha);
        _beta  = fetchDoubleParam(kParamBeta);
        _scale = fetchIntParam(kParamScale);
        _offset = fetchDoubleParam(kParamOffset);
        _seed   = fetchIntParam(kParamSeed);
        _staticSeed = fetchBooleanParam(kParamStaticSeed);
        assert(_seed && _staticSeed);
        assert(_alpha && _beta && _scale);
    }

    virtual void getValuesAtTime(double time,
                                 CImgPlasmaParams& params) OVERRIDE FINAL
    {
        _alpha->getValueAtTime(time, params.alpha);
        _beta->getValueAtTime(time, params.beta);
        _scale->getValueAtTime(time, params.scale);
        _offset->getValueAtTime(time, params.offset);
        _seed->getValueAtTime(time, params.seed);
        _staticSeed->getValueAtTime(time, params.staticSeed);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgPlasmaParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 1 << (std::max)( 0, params.scale - (int)Coords::mipmapLevelFromScale(renderScale.x) );

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgPlasmaParams& params,
                        int x1,
                        int y1,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
#ifdef cimg_noise_internal
        unsigned int seed = cimg_hash(params.seed);
        if (!params.staticSeed) {
            float time_f = args.time;

            // set the seed based on the current time, and double it we get difference seeds on different fields
            seed = cimg_hash( *( (unsigned int*)&time_f ) ^ seed );
        }
        draw_plasma(cimg, (float)params.alpha / args.renderScale.x, (float)params.beta / args.renderScale.x, (std::max)( 0, params.scale - (int)Coords::mipmapLevelFromScale(args.renderScale.x) ), seed, x1, y1);
#else
        cimg_library::cimg::srand( (unsigned int)args.time + (unsigned int)params.seed );

        cimg.draw_plasma( (float)params.alpha / args.renderScale.x, (float)params.beta / args.renderScale.x, (std::max)( 0, params.scale - (int)Coords::mipmapLevelFromScale(args.renderScale.x) ) );
#endif
        if (params.offset != 0.) {
            cimg += params.offset;
        }
    }

    //virtual bool isIdentity(const IsIdentityArguments &args, const CImgPlasmaParams& params) OVERRIDE FINAL
    //{
    //    return (params.scale - (int)Coords::mipmapLevelFromScale(args.renderScale.x) <= 0);
    //};

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        bool staticSeed = _staticSeed->getValue();
        if (!staticSeed) {
            clipPreferences.setOutputFrameVarying(true);
            clipPreferences.setOutputHasContinuousSamples(true);
        }
    }

private:

    // params
    DoubleParam *_alpha;
    DoubleParam *_beta;
    IntParam *_scale;
    DoubleParam *_offset;
    IntParam *_seed;
    BooleanParam* _staticSeed;
};


mDeclarePluginFactory(CImgPlasmaPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgPlasmaPluginFactory::describe(ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGenerator);
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
CImgPlasmaPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgPlasmaPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
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
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBeta);
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
        IntParamDescriptor *param = desc.defineIntParam(kParamScale);
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
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        if (context == eContextGenerator) {
            param->setDefault(kParamOffsetGeneratorDefault);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    // seed
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSeed);
        param->setLabel(kParamSeedLabel);
        param->setHint(kParamSeedHint);
        param->setDefault(2000);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamStaticSeed);
        param->setLabel(kParamStaticSeedLabel);
        param->setHint(kParamStaticSeedHint);
        param->setDefault(true);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgPlasmaPlugin::describeInContextEnd(desc, context, page);
} // CImgPlasmaPluginFactory::describeInContext

ImageEffect*
CImgPlasmaPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgPlasmaPlugin(handle);
}

static CImgPlasmaPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
