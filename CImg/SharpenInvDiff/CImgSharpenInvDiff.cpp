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
 * OFX CImgSharpenInvDiff plugin.
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

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "SharpenInvDiffCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Sharpen selected images by inverse diffusion.\n" \
    "Uses 'sharpen' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgSharpenInvDiff"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // a maximum computation is done in sharpen, tiling is theoretically not possible (although gmicol uses a 24 pixel overlap)
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

#define kParamAmplitude "amplitude"
#define kParamAmplitudeLabel "Amplitude"
#define kParamAmplitudeHint "Standard deviation of the spatial kernel, in pixel units (>=0). Details smaller than this size are filtered out."
#define kParamAmplitudeDefault 0.2 // 50.0/255

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations. A reasonable value is 2."
#define kParamIterationsDefault 2

using namespace cimg_library;

/// SharpenInvDiff plugin
struct CImgSharpenInvDiffParams
{
    double amplitude;
    int iterations;
};

class CImgSharpenInvDiffPlugin
    : public CImgFilterPluginHelper<CImgSharpenInvDiffParams, false>
{
public:

    CImgSharpenInvDiffPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgSharpenInvDiffParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _amplitude  = fetchDoubleParam(kParamAmplitude);
        _iterations = fetchIntParam(kParamIterations);
        assert(_amplitude && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgSharpenInvDiffParams& params) OVERRIDE FINAL
    {
        _amplitude->getValueAtTime(time, params.amplitude);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgSharpenInvDiffParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 24 * params.iterations; // overlap is 24 in gmicol

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments & /*args*/,
                        const CImgSharpenInvDiffParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.iterations <= 0) || (params.amplitude == 0.) || cimg.is_empty() ) {
            return;
        }
        for (int i = 0; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }
#ifdef CIMG_ABORTABLE
            // args
            const float amplitude = static_cast<float>(params.amplitude);

#define Tfloat float
#define T float

            T val_min, val_max = cimg.max_min(val_min);
            CImg<Tfloat> velocity(cimg._width, cimg._height, cimg._depth, cimg._spectrum), _veloc_max(cimg._spectrum);

            cimg_forC(cimg, c) {
                Tfloat *ptrd = velocity.data(0, 0, 0, c), veloc_max = 0;

                CImg_3x3(I, Tfloat);
                cimg_for3(cimg._height, y) {
                    if ( abort() ) {
                        return;
                    }
                    for (int x = 0,
                         _p1x = 0,
                         _n1x = (int)(
                             ( I[0] = I[1] = (T)cimg(_p1x, _p1y, 0, c) ),
                             ( I[3] = I[4] = (T)cimg(0, y, 0, c) ),
                             ( I[6] = I[7] = (T)cimg(0, _n1y, 0, c) ),
                             1 >= cimg._width ? cimg.width() - 1 : 1);
                         ( _n1x < cimg.width() && (
                               ( I[2] = (T)cimg(_n1x, _p1y, 0, c) ),
                               ( I[5] = (T)cimg(_n1x, y, 0, c) ),
                               ( I[8] = (T)cimg(_n1x, _n1y, 0, c) ), 1) ) ||
                         x == --_n1x;
                         I[0] = I[1], I[1] = I[2],
                         I[3] = I[4], I[4] = I[5],
                         I[6] = I[7], I[7] = I[8],
                         _p1x = x++, ++_n1x) {
                        const Tfloat veloc = -Ipc - Inc - Icp - Icn + 4 * Icc;
                        *(ptrd++) = veloc;
                        if (veloc > veloc_max) {
                            veloc_max = veloc;
                        } else if (-veloc > veloc_max) {
                            veloc_max = -veloc;
                        }
                    }
                }
                _veloc_max[c] = veloc_max;
            }

            const Tfloat veloc_max = _veloc_max.max();
            if (veloc_max > 0) {
                ( (velocity *= amplitude / veloc_max) += cimg ).cut(val_min, val_max).move_to(cimg);
            }

#undef Tfloat
#undef T

#else // ifdef CIMG_ABORTABLE
            cimg.sharpen( (float)params.amplitude );
#endif // ifdef CIMG_ABORTABLE
        }
    } // render

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgSharpenInvDiffParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0 || params.amplitude == 0.);
    };

private:

    // params
    DoubleParam *_amplitude;
    IntParam *_iterations;
};


mDeclarePluginFactory(CImgSharpenInvDiffPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgSharpenInvDiffPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgSharpenInvDiffPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                                   ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgSharpenInvDiffPlugin::describeInContextBegin(desc, context,
                                                                                 kSupportsRGBA,
                                                                                 kSupportsRGB,
                                                                                 kSupportsXY,
                                                                                 kSupportsAlpha,
                                                                                 kSupportsTiles,
                                                                                 /*processRGB=*/ true,
                                                                                 /*processAlpha*/ false,
                                                                                 /*processIsSecret=*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmplitude);
        param->setLabel(kParamAmplitudeLabel);
        param->setHint(kParamAmplitudeHint);
        param->setRange(0, 4. /*1000/256*/);
        param->setDisplayRange(0, 1.2 /*300/255*/);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(0.01);
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

    CImgSharpenInvDiffPlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgSharpenInvDiffPluginFactory::createInstance(OfxImageEffectHandle handle,
                                                ContextEnum /*context*/)
{
    return new CImgSharpenInvDiffPlugin(handle);
}

static CImgSharpenInvDiffPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
