/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
 * OFX CImgMedian plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <cfloat> // DBL_MAX
#include <algorithm>
#include <vector>
#include <atomic>
#include <limits>
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

#define kPluginName          "MedianCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Apply a median filter to input images. Pixel values within a box of the given radius around the current pixel are sorted, and the median value is output (with a Threshold, only the values that do not differ from the current value by more than the Threshold are used). Median filtering is performed per-channel. From version 3, pixels outside the image get the value of the nearest pixel, and NaN values are left out.\n" \
    "Version 2 (obsolete) uses the 'blur_median' function from the CImg library, with a square box whose radius is the Size, cropped at the image borders.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgMedian"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: NaN/inf-safe premultiply (SupportExt #73, #79)
// version 3.0: the size is a fractional radius per axis, default 3 (a 7x7 box), rounded half up; pixels outside
//              the image get the value of the nearest pixel; NaNs are left out of the median (NaN if the window
//              holds only NaNs; infinite values are sorted as any other); with a Threshold, the centre value is
//              always used; no unpremultiplication by default; version 2 (one integer radius, default 1, a
//              smaller box at the image borders) is kept for backward compatibility
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMajorOld 2 // version 2, kept for backward compatibility
#define kPluginVersionMinorOld 1
// the first version with a radius per axis (kParamSize is a Double2DParam) and edge-repeat borders
constexpr unsigned int kPluginVersionRadius2D = 3;

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

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Radius of the box, in pixels, for x and y (>=0). Each radius r is rounded half up, and the box is 2*r+1 pixels wide: the default, 3, is a 7x7 box. The radius is rounded after scaling to the render scale, so a small size filters relatively more at a proxy scale (1 is a 3x3 box at scale 1, and at scale 0.5 too)."
#define kParamSizeHintOld "Width and height of the structuring element is 2*size+1, in pixel units (>=0)."
#define kParamSizeDefault 3.
#define kParamSizeDefaultOld 1

#define kParamThreshold "threshold"
#define kParamThresholdLabel "Threshold"
#define kParamThresholdHint "Threshold used to discard pixels too far from the current pixel value in the median computation. A threshold value of zero disables the threshold."
#define kParamThresholdDefault 0.


/// Median plugin
struct CImgMedianParams
{
    double sizex; // radius in pixels at render scale 1 (version 2: sizex == sizey, an integer)
    double sizey;
    double threshold;
};

// MedianCImg 3 renders in parallel (OpenMP, as CImg) when the image has at least this many pixels
[[maybe_unused]] constexpr int kMedianParallelPixels = 4096;

// The median of the values in [first, last): the NaNs are left out; the mean of the two middle values if there
// is an even number of them, as CImg's median(); NaN if there is no value but NaNs (or none). The values are
// reordered. (std::nth_element needs a strict weak order, which operator< is not with NaNs.)
static cimgpix_t
medianOf(std::vector<cimgpix_t>::iterator first,
         std::vector<cimgpix_t>::iterator last)
{
    last = std::partition(first, last, [](cimgpix_t v) { return !std::isnan(v); });
    const std::ptrdiff_t n = last - first;
    if (n == 0) {
        return std::numeric_limits<cimgpix_t>::quiet_NaN();
    }
    const std::vector<cimgpix_t>::iterator mid = first + n / 2;
    std::nth_element(first, mid, last);
    if (n % 2) {
        return *mid;
    }
    // the lower middle value is the largest of the values before mid
    const cimgpix_t lower = *std::max_element(first, mid);

    return (lower + *mid) / 2;
}

// Median filter of radius (rx, ry >= 0) of each channel of img; pixels outside img get the value of the nearest
// pixel, and NaNs are left out of the window (see medianOf()). With threshold > 0, only the centre value v0 and
// the values v with |v - v0| <= threshold are used. Returns false if the render was aborted (img is then partly
// filtered).
static bool
medianFilter(cimg_library::CImg<cimgpix_t>& img,
             int rx,
             int ry,
             double threshold)
{
    if ( img.is_empty() || ( (rx == 0) && (ry == 0) ) ) {
        return true;
    }
    assert(rx >= 0 && ry >= 0);
    const cimg_library::CImg<cimgpix_t> src(img, false);
    const int w = src.width();
    const int h = src.height();
    const std::size_t windowSize = (std::size_t)(2 * rx + 1) * (std::size_t)(2 * ry + 1);
    std::atomic<bool> aborted(false);
    cimg_pragma_openmp(parallel if (w * h >= kMedianParallelPixels))
    {
        std::vector<cimgpix_t> values(windowSize);
        cimg_pragma_openmp(for collapse(2))
        for (int c = 0; c < src.spectrum(); ++c) {
            for (int y = 0; y < h; ++y) {
                if ( aborted || cimgRenderAborted() ) {
                    aborted = true;
                    continue; // a loop of an OpenMP for cannot be left early
                }
                for (int x = 0; x < w; ++x) {
                    const cimgpix_t v0 = src(x, y, 0, c);
                    std::vector<cimgpix_t>::iterator end = values.begin();
                    for (int dy = -ry; dy <= ry; ++dy) {
                        const int yy = (std::min)( (std::max)(y + dy, 0), h - 1 );
                        for (int dx = -rx; dx <= rx; ++dx) {
                            const int xx = (std::min)( (std::max)(x + dx, 0), w - 1 );
                            const cimgpix_t v = src(xx, yy, 0, c);
                            // the centre value is always used (a NaN or infinite v0 is within no threshold)
                            if ( (threshold <= 0.) || ( (dx == 0) && (dy == 0) ) ||
                                 (std::abs( (double)v - (double)v0 ) <= threshold) ) {
                                *end++ = v;
                            }
                        }
                    }
                    img(x, y, 0, c) = medianOf(values.begin(), end);
                }
            }
        }
    }

    return !aborted;
}

class CImgMedianPlugin
    : public CImgFilterPluginHelper<CImgMedianParams, false>
{
public:

    CImgMedianPlugin(OfxImageEffectHandle handle,
                     unsigned int majorVersion)
        : CImgFilterPluginHelper<CImgMedianParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale,
                                                          /*defaultUnpremult=*/ majorVersion < kPluginVersionRadius2D)
        , _radius2D(majorVersion >= kPluginVersionRadius2D)
        , _size(NULL)
        , _sizeOld(NULL)
        , _threshold(NULL)
    {
        if (_radius2D) {
            _size = fetchDouble2DParam(kParamSize);
        } else {
            _sizeOld = fetchIntParam(kParamSize);
        }
        _threshold  = fetchDoubleParam(kParamThreshold);
        assert( (_size || _sizeOld) && _threshold );
    }

    virtual void getValuesAtTime(double time,
                                 CImgMedianParams& params) OVERRIDE FINAL
    {
        if (_radius2D) {
            _size->getValueAtTime(time, params.sizex, params.sizey);
        } else {
            int size;
            _sizeOld->getValueAtTime(time, size);
            params.sizex = params.sizey = size;
        }
        _threshold->getValueAtTime(time, params.threshold);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgMedianParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix_x, delta_pix_y;
        if (_radius2D) {
            delta_pix_x = cimgRadius(params.sizex, renderScale.x);
            delta_pix_y = cimgRadius(params.sizey, renderScale.y);
        } else {
            // version 2, as it was
            delta_pix_x = (int)std::ceil(std::abs(params.sizex) * renderScale.x);
            delta_pix_y = (int)std::ceil(std::abs(params.sizex) * renderScale.y);
        }

        cimgPadRect(rect, delta_pix_x, delta_pix_y, roi);
    }

    virtual void render(const RenderArguments &args,
                        const CImgMedianParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (_radius2D) {
            medianFilter(cimg, cimgRadius(params.sizex, args.renderScale.x), cimgRadius(params.sizey, args.renderScale.y), params.threshold);
        } else {
            // version 2, as it was
            cimg.blur_median( static_cast<unsigned int>( std::floor((std::max)(1., params.sizex) * args.renderScale.x) ) * 2 + 1, static_cast<float>(params.threshold) );
        }
    }

    virtual bool isIdentity(const IsIdentityArguments &args,
                            const CImgMedianParams& params) OVERRIDE FINAL
    {
        if (_radius2D) {
            return cimgRadius(params.sizex, args.renderScale.x) == 0 && cimgRadius(params.sizey, args.renderScale.y) == 0;
        }

        return (std::floor(params.sizex * args.renderScale.x) == 0);
    };

private:
    // version 3: pixels outside the source get the value of the nearest pixel, as inside the image
    virtual int getBoundary(const CImgMedianParams& /*params*/) OVERRIDE FINAL { return _radius2D ? 1 : 0; }

    const bool _radius2D; // version 3 and later
    // params
    Double2DParam *_size; // version 3 and later
    IntParam *_sizeOld; // version 2
    DoubleParam *_threshold;
};


mDeclarePluginFactoryVersioned(CImgMedianPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgMedianPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
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

template<unsigned int majorVersion>
void
CImgMedianPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                         ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgMedianPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    if (majorVersion >= kPluginVersionRadius2D) {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setDoubleType(eDoubleTypePlain);
        param->setRange(0, 0, 100, 100);
        param->setDisplayRange(0, 0, 10, 10);
        param->setDefault(kParamSizeDefault, kParamSizeDefault);
        param->setDigits(1);
        param->setIncrement(0.5);
        if (page) {
            page->addChild(*param);
        }
    } else {
        IntParamDescriptor *param = desc.defineIntParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHintOld);
        param->setRange(1, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamSizeDefaultOld);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamThreshold);
        param->setLabel(kParamThresholdLabel);
        param->setHint(kParamThresholdHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(0, 1);
        param->setDefault(kParamThresholdDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgMedianPlugin::describeInContextEnd(desc, context, page);
}

template<unsigned int majorVersion>
ImageEffect*
CImgMedianPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                      ContextEnum /*context*/)
{
    return new CImgMedianPlugin(handle, majorVersion);
}

static CImgMedianPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
// version 2 (one integer radius, default 1), kept for backward compatibility
static CImgMedianPluginFactory<kPluginVersionMajorOld> pOld(kPluginIdentifier, kPluginVersionMinorOld);
mRegisterPluginFactoryInstance(pOld)

OFXS_NAMESPACE_ANONYMOUS_EXIT
