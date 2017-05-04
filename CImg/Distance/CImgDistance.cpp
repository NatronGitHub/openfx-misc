/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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

using std::min; using std::max; using std::floor; using std::ceil; using std::sqrt;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "DistanceCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Compute Euclidean distance function to pixels that have a value of zero. The distance is normalized with respect to the largest image dimension, so that it is always between 0 and 1.\n" \
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
#ifdef cimg_use_openmp
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
#define kParamMetricOptionChebyshev "Chebyshev", "max(abs(x-xborder),abs(y-yborder))"
#define kParamMetricOptionManhattan "Manhattan", "abs(x-xborder) + abs(y-yborder)"
#define kParamMetricOptionEuclidean "Euclidean", "sqrt(sqr(x-xborder) + sqr(y-yborder))"
#define kParamMetricOptionSquaredEuclidean "Squared Euclidean", "sqr(x-xborder) + sqr(y-yborder)"
#define kParamMetricOptionSpherical "Spherical", "Compute the Euclidean distance, and draw a sphere at each point. Gives a round shape rather than a conical shape to the distance function."
enum MetricEnum {
    eMetricChebyshev = 0,
    eMetricManhattan,
    eMetricEuclidean,
    eMetricSquaredEuclidean,
    eMetricSpherical,
};
#define kParamMetricDefault eMetricEuclidean

/// Distance plugin
struct CImgDistanceParams
{
    MetricEnum metric;
};

class CImgDistancePlugin
: public CImgFilterPluginHelper<CImgDistanceParams, false>
{
public:

    CImgDistancePlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgDistanceParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ false)
    {
        _metric  = fetchChoiceParam(kParamMetric);
        assert(_metric);
    }

    virtual void getValuesAtTime(double time,
                                 CImgDistanceParams& params) OVERRIDE FINAL
    {
        params.metric = (MetricEnum)_metric->getValueAtTime(time);
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
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
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
        if ( !isEmpty(srcFormatD) ) {
            maxdim = max( srcFormatD.x2 - srcFormatD.x1, srcFormatD.y2 - srcFormatD.y1 );
        }
#endif
        int m = (params.metric == eMetricSpherical) ? (int)eMetricEuclidean : (int)params.metric;
        cimg.distance(0, m);

        if (params.metric == eMetricSpherical) {
            bool finished = false;
            cimg_library::CImg<cimgpix_t> distance(cimg, /*is_shared=*/false);

            while (!finished) {
                cimg_abort_test();
                int max_x = 0, max_y = 0, max_z = 0, max_c = 0;
                cimgpix_t dmax = distance(0,0,0,0);
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
                    cimgpix_t r = dmax;
                    int xmin = (int)floor(max((cimgpix_t)0, max_x - dmax));
                    int xmax = (int)ceil(min((cimgpix_t)cimg.width(), max_x + dmax));
                    int ymin = (int)floor(max((cimgpix_t)0, max_y - dmax));
                    int ymax = (int)ceil(min((cimgpix_t)cimg.height(), max_y + dmax));
                    // loop on all pixels in the bounding box
                    cimg_for_inXY(cimg, xmin, ymin, xmax, ymax, x, y) {
                        cimgpix_t r2 = (x - max_x)*(x - max_x) + (y - max_y)*(y - max_y);
                        if (r2 < r * r) {
                            // draw the Z-sphere point
                            cimgpix_t z = sqrt(r*r - r2);
                            if (cimg(x,y, max_z, max_c) < z) {
                                cimg(x,y, max_z, max_c) = z;
                            }
                            // prune points below the Z-cone
                            if (distance(x, y, max_z, max_c) > 0 && distance(x, y, max_z, max_c) < (r - sqrt(r2))) {
                                distance(x, y, max_z, max_c) = 0;
                            }
                        }
                    }
                }
            }
        }
        if (params.metric == eMetricSquaredEuclidean) {
            cimg /= maxdim * maxdim/* * args.renderScale.x*/;
        } else {
            cimg /= maxdim/* * args.renderScale.x*/;
        }
    }

private:

    // params
    ChoiceParam *_metric;
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
                                                                         /*processRGB=*/ true,
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
        assert(param->getNOptions() == eMetricSquaredEuclidean);
        param->appendOption(kParamMetricOptionSquaredEuclidean);
        assert(param->getNOptions() == eMetricSpherical);
        param->appendOption(kParamMetricOptionSpherical);
        param->setDefault(kParamMetricDefault);
        if (page) {
            page->addChild(*param);
        }
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
