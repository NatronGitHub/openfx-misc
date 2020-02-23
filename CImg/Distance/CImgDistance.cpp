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
 * OFX CImgDistance plugin.
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
using namespace cimg_library;

using std::min; using std::max; using std::floor; using std::ceil; using std::sqrt;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#ifdef DEBUG
#define EXPERIMENTAL // turn on experimental not-for-everyone code
#endif

#define kPluginName          "DistanceCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Compute at each pixel the distance to pixels that have a value of zero.\n" \
"The distance is normalized with respect to the largest image dimension, so that it is between 0 and 1.\n" \
"Optionally, a signed distance to the frontier between zero and nonzero values can be computed.\n" \
"The distance transform can then be thresholded using the Threshold effect, or transformed using the ColorLookup effect, in order to generate a mask for another effect.\n" \
"See alse https://en.wikipedia.org/wiki/Distance_transform\n" \
"Uses the 'distance' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "eu.cimg.Distance"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // requires the whole image to compute distance
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

#define kParamMetric "metric"
#define kParamMetricLabel "Metric"
#define kParamMetricHint "Type of metric."
#define kParamMetricOptionChebyshev "Chebyshev", "max(abs(x-xborder),abs(y-yborder))", "chebyshev"
#define kParamMetricOptionManhattan "Manhattan", "abs(x-xborder) + abs(y-yborder)", "manhattan"
#define kParamMetricOptionEuclidean "Euclidean", "sqrt(sqr(x-xborder) + sqr(y-yborder))", "euclidean"
//#define kParamMetricOptionSquaredEuclidean "Squared Euclidean", "sqr(x-xborder) + sqr(y-yborder)", "squaredeuclidean"
#ifdef EXPERIMENTAL
#define kParamMetricOptionSpherical "Spherical", "Compute the Euclidean distance, and draw a 2.5D sphere at each point with the distance as radius. Gives a round shape rather than a conical shape to the distance function.", "spherical"
#endif
enum MetricEnum {
    eMetricChebyshev = 0,
    eMetricManhattan,
    eMetricEuclidean,
//    eMetricSquaredEuclidean,
#ifdef EXPERIMENTAL
    eMetricSpherical,
#endif
};
#define kParamMetricDefault eMetricEuclidean

#define kParamSigned "signed"
#define kParamSignedLabel "Signed Distance"
#define kParamSignedHint "Instead of computing the distance to pixels with a value of zero, compute the signed distance to the contour between zero and non-zero pixels. On output, non-zero-valued pixels have a positive signed distance, zero-valued pixels have a negative signed distance."

/// Distance plugin
struct CImgDistanceParams
{
    MetricEnum metric;
    bool signedDistance;
};

class CImgDistancePlugin
: public CImgFilterPluginHelper<CImgDistanceParams, false>
{
public:

    CImgDistancePlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgDistanceParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ false)
    {
        _metric  = fetchChoiceParam(kParamMetric);
        _signed  = fetchBooleanParam(kParamSigned);
        assert(_metric && _signed);
    }

    virtual void getValuesAtTime(double time,
                                 CImgDistanceParams& params) OVERRIDE FINAL
    {
        params.metric = (MetricEnum)_metric->getValueAtTime(time);
        params.signedDistance = _signed->getValueAtTime(time);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgDistanceParams& /*params*/,
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
                        const CImgDistanceParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        CImg<cimgpix_t>& /*mask*/,
                        CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place

        // first compute the maximum dimension, which is used to normalize the distance so that it is between 0 and 1
        // - of the format, if it is defined
        // - else use the cimg dimension, since this plugin does not support ties anyway

        double maxdim = max( cimg.width(), cimg.height() );
#ifdef OFX_EXTENSIONS_NATRON
        OfxRectI srcFormat;
        _srcClip->getFormat(srcFormat);
        OfxRectD srcFormatD;
        srcFormatD.x1 = srcFormat.x1 * args.renderScale.x;
        srcFormatD.x2 = srcFormat.x2 * args.renderScale.x;
        srcFormatD.y1 = srcFormat.y1 * args.renderScale.y;
        srcFormatD.y2 = srcFormat.y2 * args.renderScale.y;
        if ( !Coords::rectIsEmpty(srcFormatD) ) {
            maxdim = max( srcFormatD.x2 - srcFormatD.x1, srcFormatD.y2 - srcFormatD.y1 );
        }
#endif
#ifdef EXPERIMENTAL
        int m = (params.metric == eMetricSpherical) ? /*(int)eMetricSquaredEuclidean*/3 : (int)params.metric;
#else
        int m = (int)params.metric;
#endif
        int niter = 1;
        CImg<cimgpix_t> cimg_save;

        if (params.signedDistance) {
            // to compute the signed distance, first compute the distance A to zero-valued pixels, then B to non-zero
            // valued pixels, then the result is A - 0.5 - (B - 0.5)
            niter = 2;
            // copy image to compute the non-zero part afterwards part
            cimg_save.assign(cimg, /*is_shared=*/false);
            cimg.swap(cimg_save);
            cimg_pragma_openmp(parallel for cimg_openmp_if(cimg.size()>=8192))
            // compute the negative part first
            cimg_rof(cimg,ptrd,cimgpix_t) *ptrd = (*ptrd == 0);
        }

        for (int i = 0; i < niter; ++i) {
            cimg.distance(0, m);

#ifdef EXPERIMENTAL
            if (params.metric == eMetricSpherical) {
                bool finished = false;
                CImg<cimgpix_t> distance(cimg, /*is_shared=*/false);

                // TODO: perform a MAT (medial axis transform) first to reduce the number of points.
                //  E. Remy and E. Thiel. Exact Medial Axis with Euclidean Distance. Image and Vision Computing, 23(2):167-175, 2005.
                // see http://pageperso.lif.univ-mrs.fr/~edouard.thiel/IVC2004/

                while (!finished) {
                    cimg_abort_test;
                    int max_x = 0, max_y = 0, max_z = 0, max_c = 0;
                    cimgpix_t dmax = distance(0,0,0,0);
                    // TODO: if we start from the MAT, we can process each sphere center sequentially: no need to extract the maximum to get the next center (this is only useful to prune points that are within the Z-cone). The main loop would thus be on the non-zero MAT pixels.
                    cimg_forXYZC(cimg, x, y, z, c) {
                        if (distance(x,y,z,c) > dmax) {
                            dmax = distance(x,y,z,c);
                            max_x = x;
                            max_y = y;
                            max_z = z;
                            max_c = c;
                        }
                    }//image loop
                    //printf("dmax=%g\n", dmax);
                    if (dmax <= 0) {
                        // no more sphere to draw
                        finished = true;
                    } else {
                        distance(max_x, max_y, max_z, max_c) = 0;
                        // draw a Z-sphere in the zmap and prune points in
                        // the cimg corresponding to occluded spheres
                        cimgpix_t r2 = dmax;
                        cimgpix_t r = sqrt(r2);
                        int xmin = (int)floor(max((cimgpix_t)0, max_x - r));
                        int xmax = (int)ceil(min((cimgpix_t)cimg.width(), max_x + r));
                        int ymin = (int)floor(max((cimgpix_t)0, max_y - r));
                        int ymax = (int)ceil(min((cimgpix_t)cimg.height(), max_y + r));
                        // loop on all pixels in the bounding box
                        cimg_for_inXY(cimg, xmin, ymin, xmax, ymax, x, y) {
                            cimgpix_t pr2 = static_cast<cimgpix_t>((x - max_x)*(x - max_x) + (y - max_y)*(y - max_y));
                            if (pr2 < r2) {
                                // draw the Z-sphere point
                                cimgpix_t z = r2 - pr2;
                                if (cimg(x,y, max_z, max_c) < z) {
                                    cimg(x,y, max_z, max_c) = z;
                                }
                                // prune points below the Z-cone (should not be necessary if we do the MAT first)
                                if (distance(x, y, max_z, max_c) > 0 && distance(x, y, max_z, max_c) < /*(r - sqrt(pr2))^2=*/(r2 + pr2 - 2 * r * sqrt(pr2)) ) {
                                    distance(x, y, max_z, max_c) = 0;
                                }
                            }
                        }
                    }
                }
                // now compute the square root
                cimg.sqrt();
            }
#endif
            if (params.signedDistance) {
                if (i == 0) {
                    cimg.swap(cimg_save);
                    // now cimg_save contains the negative part
                }
                if (i == 1) {
                    // now cimg_save contains the negative part
                    // now cimg contains the positive part
                    cimgpix_t *ptrd = cimg.data();
                    for (const cimgpix_t *ptrs = cimg_save.data(), *ptrs_end = ptrs + cimg_save.size();
                         ptrs < ptrs_end;
                         ++ptrd, ++ptrs) {
                        *ptrd = static_cast<cimgpix_t>(*ptrd > 0 ? (*ptrd - 0.5) : (0.5 - *ptrs));

                    }

                }
            }
        }
        //if (params.metric == eMetricSquaredEuclidean) {
        //    cimg /= maxdim * maxdim/* * args.renderScale.x*/;
        //} else {
        cimg /= maxdim/* * args.renderScale.x*/;
        //}
    }

private:

    // params
    ChoiceParam *_metric;
    BooleanParam *_signed;
};


mDeclarePluginFactory(CImgDistancePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgDistancePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgDistancePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgDistancePlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ false,
                                                                         /*processAlpha*/ true,      // Enable alpha by default, so it works OK on masks
                                                                         /*processIsSecret=*/ false);

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamMetric);
        param->setLabel(kParamMetricLabel);
        param->setHint(kParamMetricHint);
        assert(param->getNOptions() == eMetricChebyshev);
        param->appendOption(kParamMetricOptionChebyshev);
        assert(param->getNOptions() == eMetricManhattan);
        param->appendOption(kParamMetricOptionManhattan);
        assert(param->getNOptions() == eMetricEuclidean);
        param->appendOption(kParamMetricOptionEuclidean);
        //assert(param->getNOptions() == eMetricSquaredEuclidean);
        //param->appendOption(kParamMetricOptionSquaredEuclidean);
#ifdef EXPERIMENTAL
        assert(param->getNOptions() == eMetricSpherical);
        param->appendOption(kParamMetricOptionSpherical);
#endif
        param->setDefault(kParamMetricDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamSigned);
        param->setLabel(kParamSignedLabel);
        param->setHint(kParamSignedHint);
    }

    CImgDistancePlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgDistancePluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgDistancePlugin(handle);
}

static CImgDistancePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
