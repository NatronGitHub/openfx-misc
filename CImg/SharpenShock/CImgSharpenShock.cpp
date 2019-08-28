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
 * OFX CImgSharpenShock plugin.
 */

#include <memory>
#include <cmath>
#include <cfloat> // DBL_MAX
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

#define kPluginName          "SharpenShockCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Sharpen selected images by shock filters.\n" \
    "Uses 'sharpen' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgSharpenShock"
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
#define kParamAmplitudeDefault 0.6 // 150.0/255

#define kParamEdgeThreshold "edgeThreshold"
#define kParamEdgeThresholdLabel "Edge Threshold"
#define kParamEdgeThresholdHint "Edge threshold."
#define kParamEdgeThresholdDefault 0.1

#define kParamGradientSmoothness "alpha"
#define kParamGradientSmoothnessLabel "Gradient Smoothness"
#define kParamGradientSmoothnessHint "Gradient smoothness (in pixels)."
#define kParamGradientSmoothnessDefault 0.8

#define kParamTensorSmoothness "sigma"
#define kParamTensorSmoothnessLabel "Tensor Smoothness"
#define kParamTensorSmoothnessHint "Tensor smoothness (in pixels)."
#define kParamTensorSmoothnessDefault 1.1

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations. A reasonable value is 1."
#define kParamIterationsDefault 1

#undef cimg_abort_test
#if cimg_use_openmp!=0
#define cimg_abort_test if (!omp_get_thread_num() && abort()) throw CImgAbortException("")
#else
#define cimg_abort_test if (abort()) throw CImgAbortException("")
#endif

using namespace cimg_library;

/// SharpenShock plugin
struct CImgSharpenShockParams
{
    double amplitude;
    double edge;
    double alpha;
    double sigma;
    int iterations;
};

class CImgSharpenShockPlugin
    : public CImgFilterPluginHelper<CImgSharpenShockParams, false>
{
public:

    CImgSharpenShockPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgSharpenShockParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _amplitude  = fetchDoubleParam(kParamAmplitude);
        _edge  = fetchDoubleParam(kParamEdgeThreshold);
        _alpha  = fetchDoubleParam(kParamGradientSmoothness);
        _sigma  = fetchDoubleParam(kParamTensorSmoothness);
        _iterations = fetchIntParam(kParamIterations);
        assert(_amplitude && _edge && _alpha && _sigma && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgSharpenShockParams& params) OVERRIDE FINAL
    {
        _amplitude->getValueAtTime(time, params.amplitude);
        _edge->getValueAtTime(time, params.edge);
        _alpha->getValueAtTime(time, params.alpha);
        _sigma->getValueAtTime(time, params.sigma);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgSharpenShockParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 24 * params.iterations; // overlap is 24 in gmicol

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgSharpenShockParams& params,
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
        double alpha = args.renderScale.x * params.alpha;
        double sigma = args.renderScale.x * params.sigma;
        for (int i = 0; i < params.iterations; ++i) {
            cimg_abort_test;
#ifdef CIMG_ABORTABLE
            // args
            const float amplitude = (float)params.amplitude;
            const float edge = (float)params.edge;


#define Tfloat float
#define T float
            T val_min, val_max = cimg.max_min(val_min);
            const float nedge = edge / 2;
            CImg<Tfloat> velocity(cimg._width, cimg._height, cimg._depth, cimg._spectrum), _veloc_max(cimg._spectrum);

            // 2d.
            // Shock filters.
            CImg<Tfloat> G = ( alpha > 0 ? cimg.get_blur(alpha).get_structure_tensors() : cimg.get_structure_tensors() );
            if (sigma > 0) {
                G.blur(sigma);
            }
            _cimg_abort_init_openmp;
            cimg_abort_init;
            cimg_pragma_openmp(parallel for if (G.width()>=32 && G.height()>=16))
            cimg_forY(G, y) {
                _cimg_abort_try_openmp {
                    CImg<Tfloat> val, vec;
                    Tfloat *ptrG0 = G.data(0, y, 0, 0), *ptrG1 = G.data(0, y, 0, 1), *ptrG2 = G.data(0, y, 0, 2);
                    cimg_abort_test;
                    cimg_forX(G, x) {
                        G.get_tensor_at(x, y).symmetric_eigen(val, vec);
                        if (val[0] < 0) {val[0] = 0; }
                        if (val[1] < 0) {val[1] = 0; }
                        *(ptrG0++) = vec(0, 0);
                        *(ptrG1++) = vec(0, 1);
                        *(ptrG2++) = 1 - (Tfloat)std::pow(1 + val[0] + val[1], -(Tfloat)nedge);
                    }
                } _cimg_abort_catch_openmp
            }
            cimg_abort_test;
            cimg_pragma_openmp(parallel for if (cimg.width()*cimg.height()>=512 && cimg.spectrum()>=2))
            cimg_forC(cimg, c) {
                _cimg_abort_try_openmp {
                    Tfloat *ptrd = velocity.data(0, 0, 0, c), veloc_max = 0;

                    CImg_3x3(I, Tfloat);
                    cimg_for3(cimg._height, y) {
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
                                    const Tfloat u = G(x, y, 0),
                                    v = G(x, y, 1),
                                    amp = G(x, y, 2),
                                    ixx = Inc + Ipc - 2 * Icc,
                                    ixy = (Inn + Ipp - Inp - Ipn) / 4,
                                    iyy = Icn + Icp - 2 * Icc,
                                    ixf = Inc - Icc,
                                    ixb = Icc - Ipc,
                                    iyf = Icn - Icc,
                                    iyb = Icc - Icp,
                                    itt = u * u * ixx + v * v * iyy + 2 * u * v * ixy,
                                    it = u * cimg::minmod(ixf, ixb) + v * cimg::minmod(iyf, iyb),
                                    veloc = -amp*cimg::sign(itt) * cimg::abs(it);
                                    *(ptrd++) = veloc;
                                    if (veloc > veloc_max) {
                                        veloc_max = veloc;
                                    } else if (-veloc > veloc_max) {
                                        veloc_max = -veloc;
                                    }
                                }
                            }
                    _veloc_max[c] = veloc_max;
                } _cimg_abort_catch_openmp
            }
            cimg_abort_test;

            const Tfloat veloc_max = _veloc_max.max();
            if (veloc_max > 0) {
                ( (velocity *= amplitude / veloc_max) += cimg ).cut(val_min, val_max).move_to(cimg);
            }
#else // ifdef CIMG_ABORTABLE
            cimg.sharpen( (float)params.amplitude, true, (float)params.edge, (float)alpha, (float)sigma );
#endif // ifdef CIMG_ABORTABLE
        }
    } // render

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgSharpenShockParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0 || params.amplitude == 0.);
    };

private:

    // params
    DoubleParam *_amplitude;
    DoubleParam *_edge;
    DoubleParam *_alpha;
    DoubleParam *_sigma;
    IntParam *_iterations;
};


mDeclarePluginFactory(CImgSharpenShockPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgSharpenShockPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgSharpenShockPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                                 ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgSharpenShockPlugin::describeInContextBegin(desc, context,
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
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 1.5 /*400/255*/);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEdgeThreshold);
        param->setLabel(kParamEdgeThresholdLabel);
        param->setHint(kParamEdgeThresholdHint);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 0.7);
        param->setDefault(kParamEdgeThresholdDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGradientSmoothness);
        param->setLabel(kParamGradientSmoothnessLabel);
        param->setHint(kParamGradientSmoothnessHint);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 10.);
        param->setDefault(kParamGradientSmoothnessDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamTensorSmoothness);
        param->setLabel(kParamTensorSmoothnessLabel);
        param->setHint(kParamTensorSmoothnessHint);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 10.);
        param->setDefault(kParamTensorSmoothnessDefault);
        param->setIncrement(0.01);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIterations);
        param->setLabel(kParamIterationsLabel);
        param->setHint(kParamIterationsHint);
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, 10);
        param->setDefault(kParamIterationsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgSharpenShockPlugin::describeInContextEnd(desc, context, page);
} // CImgSharpenShockPluginFactory::describeInContext

ImageEffect*
CImgSharpenShockPluginFactory::createInstance(OfxImageEffectHandle handle,
                                              ContextEnum /*context*/)
{
    return new CImgSharpenShockPlugin(handle);
}

static CImgSharpenShockPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
