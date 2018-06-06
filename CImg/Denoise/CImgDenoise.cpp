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
 * OFX CImgDenoise plugin.
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

#define kPluginName          "SmoothPatchBasedCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Denoise selected images by non-local patch averaging.\n" \
    "This uses the method described in:  " \
    "Non-Local Image Smoothing by Applying Anisotropic Diffusion PDE's in the Space of Patches " \
    "(D. Tschumperlé, L. Brun), ICIP'09 " \
    "(https://tschumperle.users.greyc.fr/publications/tschumperle_icip09.pdf).\n" \
    "Uses the 'blur_patch' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgDenoise"
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
#ifdef cimg_use_openmp
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
#define kParamSigmaSHint "Standard deviation of the spatial kernel, in pixel units (>=0)."
#define kParamSigmaSDefault 10.0

#define kParamSigmaR "sigma_r"
#define kParamSigmaRLabel "Value Std Dev"
#define kParamSigmaRHint "Standard deviation of the range kernel, in intensity units (>=0). In the context of denoising, Liu et al. (\"Noise estimation from a single image\", CVPR2006) recommend a value of 1.95*sigma_n, where sigma_n is the local image noise."
#define kParamSigmaRDefault 0.05

#define kParamPatchSize "psize"
#define kParamPatchSizeLabel "Patch Size"
#define kParamPatchSizeHint "Size of the patchs, in pixels (>=0)."
#define kParamPatchSizeDefault 5

#define kParamLookupSize "lsize"
#define kParamLookupSizeLabel "Lookup Size"
#define kParamLookupSizeHint "Size of the window to search similar patchs, in pixels (>=0)."
#define kParamLookupSizeDefault 6

#define kParamSmoothness "smoothness"
#define kParamSmoothnessLabel "Smoothness"
#define kParamSmoothnessHint "Smoothness for the patch comparison, in pixels (>=0)."
#define kParamSmoothnessDefault 1.

#define kParamFastApprox "is_fast_approximation"
#define kParamFastApproxLabel "fast Approximation"
#define kParamFastApproxHint "Tells if a fast approximation of the gaussian function is used or not"
#define kParamFastApproxDefault true

using namespace cimg_library;

/// Denoise plugin
struct CImgDenoiseParams
{
    double sigma_s;
    double sigma_r;
    int psize;
    int lsize;
    double smoothness;
    bool fast_approx;
};

class CImgDenoisePlugin
    : public CImgFilterPluginHelper<CImgDenoiseParams, false>
{
public:

    CImgDenoisePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgDenoiseParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _sigma_s  = fetchDoubleParam(kParamSigmaS);
        _sigma_r  = fetchDoubleParam(kParamSigmaR);
        _psize = fetchIntParam(kParamPatchSize);
        _lsize = fetchIntParam(kParamLookupSize);
        _smoothness = fetchDoubleParam(kParamSmoothness);
        _fast_approx = fetchBooleanParam(kParamFastApprox);
        assert(_sigma_s && _sigma_r);
    }

    virtual void getValuesAtTime(double time,
                                 CImgDenoiseParams& params) OVERRIDE FINAL
    {
        _sigma_s->getValueAtTime(time, params.sigma_s);
        _sigma_r->getValueAtTime(time, params.sigma_r);
        _psize->getValueAtTime(time, params.psize);
        _lsize->getValueAtTime(time, params.lsize);
        _smoothness->getValueAtTime(time, params.smoothness);
        _fast_approx->getValueAtTime(time, params.fast_approx);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgDenoiseParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil( (params.sigma_s * 3.6) * renderScale.x ) + std::ceil(params.psize * renderScale.x) + std::ceil(params.lsize * renderScale.x);

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgDenoiseParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
#ifdef CIMG_ABORTABLE
        // code copied from CImg.h's get_blur_patch, with calls to abort() added here and there

        // args
        const float sigma_s = (float)(params.sigma_s * args.renderScale.x);
        const float sigma_p = (float)params.sigma_r;
        const unsigned int patch_size = (unsigned int)std::ceil((std::max)(0, params.psize) * args.renderScale.x);
        const unsigned int lookup_size = (unsigned int)std::ceil((std::max)(0, params.lsize) * args.renderScale.x);
        const float smoothness = (float)(params.smoothness * args.renderScale.x);
        const bool is_fast_approx = params.fast_approx;

#define Tfloat float
#define T float
#undef _cimg_blur_patch2d_fast
#undef _cimg_blur_patch2d

#undef cimg_abort_test
#ifdef cimg_use_openmp
#define cimg_abort_test if (!omp_get_thread_num() && abort()) throw CImgAbortException("")
#else
#define cimg_abort_test if (abort()) throw CImgAbortException("")
#endif

        // the macros are taken from CImg.h, with (*this) replaced by (cimg)
#define _cimg_blur_patch2d_fast(N) \
        cimg_for##N##Y(res,y) { cimg_abort_test; \
        cimg_for##N##X(res,x) { \
          T *pP = P._data; cimg_forC(res,c) { cimg_get##N##x##N(img,x,y,0,c,pP,T); pP+=N2; } \
          const int x0 = x - rsize1, y0 = y - rsize1, x1 = x + rsize2, y1 = y + rsize2; \
          float sum_weights = 0; \
          cimg_for_in##N##XY(res,x0,y0,x1,y1,p,q) if (cimg::abs(img(x,y,0,0) - img(p,q,0,0))<sigma_p3) { \
            T *pQ = Q._data; cimg_forC(res,c) { cimg_get##N##x##N(img,p,q,0,c,pQ,T); pQ+=N2; } \
            float distance2 = 0; \
            pQ = Q._data; cimg_for(P,pP,T) { const float dI = (float)*pP - (float)*(pQ++); distance2+=dI*dI; } \
            distance2/=Pnorm; \
            const float dx = (float)p - x, dy = (float)q - y, \
              alldist = distance2 + (dx*dx+dy*dy)/sigma_s2, weight = alldist>3?0.0f:1.0f; \
            sum_weights+=weight; \
            cimg_forC(res,c) res(x,y,c)+=weight*(cimg)(p,q,c); \
          } \
          if (sum_weights>0) cimg_forC(res,c) res(x,y,c)/=sum_weights; \
          else cimg_forC(res,c) res(x,y,c) = (Tfloat)((cimg)(x,y,c)); \
        } }

#define _cimg_blur_patch2d(N) \
        cimg_for##N##Y(res,y) { cimg_abort_test; \
        cimg_for##N##X(res,x) { \
          T *pP = P._data; cimg_forC(res,c) { cimg_get##N##x##N(img,x,y,0,c,pP,T); pP+=N2; } \
          const int x0 = x - rsize1, y0 = y - rsize1, x1 = x + rsize2, y1 = y + rsize2; \
          float sum_weights = 0, weight_max = 0; \
          cimg_for_in##N##XY(res,x0,y0,x1,y1,p,q) if (p!=x || q!=y) { \
            T *pQ = Q._data; cimg_forC(res,c) { cimg_get##N##x##N(img,p,q,0,c,pQ,T); pQ+=N2; } \
            float distance2 = 0; \
            pQ = Q._data; cimg_for(P,pP,T) { const float dI = (float)*pP - (float)*(pQ++); distance2+=dI*dI; } \
            distance2/=Pnorm; \
            const float dx = (float)p - x, dy = (float)q - y, \
              alldist = distance2 + (dx*dx+dy*dy)/sigma_s2, weight = (float)std::exp(-alldist); \
            if (weight>weight_max) weight_max = weight; \
            sum_weights+=weight; \
            cimg_forC(res,c) res(x,y,c)+=weight*(cimg)(p,q,c); \
          } \
          sum_weights+=weight_max; cimg_forC(res,c) res(x,y,c)+=weight_max*(cimg)(x,y,c); \
          if (sum_weights>0) cimg_forC(res,c) res(x,y,c)/=sum_weights; \
          else cimg_forC(res,c) res(x,y,c) = (Tfloat)((cimg)(x,y,c)); \
    } }

        if (cimg.is_empty() || !patch_size || !lookup_size) return;
        CImg<Tfloat> res(cimg._width,cimg._height,cimg._depth,cimg._spectrum,0);
        const CImg<T> _img = smoothness>0?cimg.get_blur(smoothness):CImg<Tfloat>(),&img = smoothness>0?_img:cimg;
        CImg<T> P(patch_size*patch_size*cimg._spectrum), Q(P);
        const float
        nsigma_s = sigma_s>=0?sigma_s:-sigma_s*cimg::max(cimg._width,cimg._height,cimg._depth)/100,
        sigma_s2 = nsigma_s*nsigma_s, sigma_p2 = sigma_p*sigma_p, sigma_p3 = 3*sigma_p,
        Pnorm = P.size()*sigma_p2;
        const int rsize2 = (int)lookup_size/2, rsize1 = (int)lookup_size - rsize2 - 1;
        const unsigned int N2 = patch_size*patch_size, N3 = N2*patch_size;
        cimg::unused(N2,N3);
        switch (patch_size) { // 2d
            case 2 : if (is_fast_approx) _cimg_blur_patch2d_fast(2) else _cimg_blur_patch2d(2) break;
            case 3 : if (is_fast_approx) _cimg_blur_patch2d_fast(3) else _cimg_blur_patch2d(3) break;
            case 4 : if (is_fast_approx) _cimg_blur_patch2d_fast(4) else _cimg_blur_patch2d(4) break;
            case 5 : if (is_fast_approx) _cimg_blur_patch2d_fast(5) else _cimg_blur_patch2d(5) break;
                // use OpenMP when patch_size > 5
            //case 6 : if (is_fast_approx) _cimg_blur_patch2d_fast(6) else _cimg_blur_patch2d(6) break;
            //case 7 : if (is_fast_approx) _cimg_blur_patch2d_fast(7) else _cimg_blur_patch2d(7) break;
            //case 8 : if (is_fast_approx) _cimg_blur_patch2d_fast(8) else _cimg_blur_patch2d(8) break;
            //case 9 : if (is_fast_approx) _cimg_blur_patch2d_fast(9) else _cimg_blur_patch2d(9) break;
            default : { // Fast
                const int psize2 = (int)patch_size/2, psize1 = (int)patch_size - psize2 - 1;
                _cimg_abort_init_omp;
                cimg_abort_init;
                if (is_fast_approx) {
                    cimg_pragma_openmp(parallel for cimg_openmp_if(res._width>=32 && res._height>=4) firstprivate(P,Q))
                    cimg_forY(res,y) {
                        _cimg_abort_try_omp {
                            cimg_abort_test;
                            cimg_forX(res,x) { // 2d fast approximation.
                                P = img.get_crop(x - psize1,y - psize1,x + psize2,y + psize2,true);
                                const int x0 = x - rsize1, y0 = y - rsize1, x1 = x + rsize2, y1 = y + rsize2;
                                float sum_weights = 0;
                                cimg_for_inXY(res,x0,y0,x1,y1,p,q) if (cimg::abs(img(x,y,0)-img(p,q,0))<sigma_p3) {
                                    (Q = img.get_crop(p - psize1,q - psize1,p + psize2,q + psize2,true))-=P;
                                    const float
                                        dx = (float)x - p, dy = (float)y - q,
                                        distance2 = (float)(Q.pow(2).sum()/Pnorm + (dx*dx + dy*dy)/sigma_s2),
                                        weight = distance2>3?0.0f:1.0f;
                                    sum_weights+=weight;
                                    cimg_forC(res,c) res(x,y,c)+=weight*cimg(p,q,c);
                                }
                                if (sum_weights>0) cimg_forC(res,c) res(x,y,c)/=sum_weights;
                                else cimg_forC(res,c) res(x,y,c) = (Tfloat)(cimg(x,y,c));
                            }
                        } _cimg_abort_catch_omp
                    }
                } else {
                    cimg_pragma_openmp(parallel for cimg_openmp_if(res._width>=32 && res._height>=4) firstprivate(P,Q))
                    cimg_forY(res,y) {
                        _cimg_abort_try_omp {
                            cimg_abort_test;
                            cimg_forX(res,x) { // 2d exact algorithm.
                                P = img.get_crop(x - psize1,y - psize1,x + psize2,y + psize2,true);
                                const int x0 = x - rsize1, y0 = y - rsize1, x1 = x + rsize2, y1 = y + rsize2;
                                float sum_weights = 0, weight_max = 0;
                                cimg_for_inXY(res,x0,y0,x1,y1,p,q) if (p!=x || q!=y) {
                                    (Q = img.get_crop(p - psize1,q - psize1,p + psize2,q + psize2,true))-=P;
                                    const float
                                            dx = (float)x - p, dy = (float)y - q,
                                            distance2 = (float)(Q.pow(2).sum()/Pnorm + (dx*dx + dy*dy)/sigma_s2),
                                            weight = (float)std::exp(-distance2);
                                    if (weight>weight_max) weight_max = weight;
                                    sum_weights+=weight;
                                    cimg_forC(res,c) res(x,y,c)+=weight*cimg(p,q,c);
                                }
                                sum_weights+=weight_max; cimg_forC(res,c) res(x,y,c)+=weight_max*cimg(x,y,c);
                                if (sum_weights>0) cimg_forC(res,c) res(x,y,c)/=sum_weights;
                                else cimg_forC(res,c) res(x,y,0,c) = (Tfloat)(cimg(x,y,c));
                            }
                        } _cimg_abort_catch_omp
                    }
                }
                cimg_abort_test;
            }
        }
        cimg.assign(res);
#undef Tfloat
#undef T

#else // ifdef CIMG_ABORTABLE
        cimg.blur_patch( (float)(params.sigma_s * args.renderScale.x),
                         (float)params.sigma_r,
                         (unsigned int)std::ceil((std::max)(0, params.psize) * args.renderScale.x),
                         (unsigned int)std::ceil((std::max)(0, params.lsize) * args.renderScale.x),
                         (float)(params.smoothness * args.renderScale.x),
                         params.fast_approx );
#endif // ifdef CIMG_ABORTABLE
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgDenoiseParams& params) OVERRIDE FINAL
    {
        return (params.sigma_s == 0. && params.sigma_r == 0.);
    };

private:

    // params
    DoubleParam *_sigma_s;
    DoubleParam *_sigma_r;
    IntParam *_psize;
    IntParam *_lsize;
    DoubleParam *_smoothness;
    BooleanParam *_fast_approx;
};


mDeclarePluginFactory(CImgDenoisePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgDenoisePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgDenoisePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                            ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgDenoisePlugin::describeInContextBegin(desc, context,
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
        IntParamDescriptor *param = desc.defineIntParam(kParamPatchSize);
        param->setLabel(kParamPatchSizeLabel);
        param->setHint(kParamPatchSizeHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 25);
        param->setDefault(kParamPatchSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamLookupSize);
        param->setLabel(kParamLookupSizeLabel);
        param->setHint(kParamLookupSizeHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 25);
        param->setDefault(kParamLookupSizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSmoothness);
        param->setLabel(kParamSmoothnessLabel);
        param->setHint(kParamSmoothnessHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 25);
        param->setDefault(kParamSmoothnessDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFastApprox);
        param->setLabel(kParamFastApproxLabel);
        param->setHint(kParamFastApproxHint);
        param->setDefault(kParamFastApproxDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgDenoisePlugin::describeInContextEnd(desc, context, page);
} // CImgDenoisePluginFactory::describeInContext

ImageEffect*
CImgDenoisePluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new CImgDenoisePlugin(handle);
}

static CImgDenoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
