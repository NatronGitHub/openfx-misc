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
 * Shared implementation of the FilterErodeCImg filter: an erosion or dilation by a weighted structuring
 * element, with fractional sizes.
 *
 * This header defines the plug-in and a factory class template (one instance per major version); it must be
 * included by exactly one .cpp, which registers the factory with the plug-in identifier.
 */

#ifndef CImg_CImgFilterErode_h
#define CImg_CImgFilterErode_h

#include <cmath>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <limits>

#include <atomic>

#include "ofxsMacros.h"
#include "ofxsCoords.h"

#include "CImgFilter.h"

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kFilterErodePluginName          "FilterErodeCImg"
#define kFilterErodePluginGrouping      "Filter"
#define kFilterErodePluginDescription \
    "Erode or dilate the input by a weighted structuring element, which gives fractional sizes and soft " \
    "edges.\n" \
    "A positive size erodes, a negative size dilates, on each axis. The structuring element is the Filter, " \
    "of width 2*|size|+1 pixels (x: the size divided by the pixel aspect ratio), sampled at the pixels around " \
    "the current pixel: each pixel within it has a weight w between 0 and 1 (1 at the centre). Eroding gives " \
    "the minimum of 1 - w*(1-v) over these pixels (v: the pixel value, so a weight of 0 gives 1), and " \
    "dilating the maximum of w*v: on a 0-1 mask, the edge moves by the size, with a soft transition of about " \
    "one pixel. A size of 0 erodes with a width of 1 pixel: no change with the Box and Triangle filters, a slight " \
    "erosion with the others. When the two sizes have different signs, y is processed first. Pixels outside the " \
    "image get the value of the nearest pixel.\n" \
    kFilterErodeDescriptionCImg
#define kFilterErodeDescriptionCImg \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

// History: see the .cpp that registers the plug-in.

#define kFilterErodeSupportsComponentRemapping 1
#define kFilterErodeSupportsTiles 1
#define kFilterErodeSupportsMultiResolution 1
#define kFilterErodeSupportsRenderScale 1
#define kFilterErodeSupportsMultipleClipPARs false
#define kFilterErodeSupportsMultipleClipDepths false
#define kFilterErodeRenderThreadSafety OFX::eRenderFullySafe
#if cimg_use_openmp!=0
#define kFilterErodeHostFrameThreading false
#else
#define kFilterErodeHostFrameThreading true
#endif

#define kParamFilterErodeSize "size"
#define kParamFilterErodeSizeLabel "Size"
#define kParamFilterErodeSizeHint "How far to erode (positive) or dilate (negative), in canonical units (x is in pixels " \
    "times the pixel aspect ratio), for x and y. The structuring element is 2*|size|+1 pixels wide."
#define kParamFilterErodeSizeDefault 1.

#define kParamFilterErodeFilter "filter"
#define kParamFilterErodeFilterLabel "Filter"
#define kParamFilterErodeFilterHint "The shape of the weights of the structuring element, of width D = 2*|size|+1 pixels."
#define kParamFilterErodeFilterOptionBox "Box", "The fraction of each pixel that is within D/2 of the centre: a size of 1 is a 3x3 box, 0.5 a 3x3 box with weights of 0.5 on the sides.", "box"
#define kParamFilterErodeFilterOptionTriangle "Triangle", "1 - |d|/D (d: the distance to the centre, in pixels).", "triangle"
#define kParamFilterErodeFilterOptionQuadratic "Quadratic", "The quadratic B-spline of d/D, normalized to 1 at the centre (radius 1.5*D).", "quadratic"
#define kParamFilterErodeFilterOptionGaussian "Gaussian", "2^(-(2d/D)^2), a Gaussian of full width at half maximum D, cropped at 1.5*D.", "gaussian"

enum FilterErodeFilterEnum
{
    eFilterErodeFilterBox = 0,
    eFilterErodeFilterTriangle,
    eFilterErodeFilterQuadratic,
    eFilterErodeFilterGaussian,
};

#define kParamFilterErodeFilterDefault eFilterErodeFilterBox

// the support of the Quadratic (the quadratic B-spline) and Gaussian weights, in units of the width D
constexpr double kFilterErodeSupportQuadraticGaussian = 1.5;
// the value at 0 of the quadratic B-spline (its weights are divided by it, to be 1 at the centre)
constexpr double kFilterErodeQuadraticCentre = 0.75;
// filterErodePass() runs in parallel (OpenMP, as CImg) when the image has at least this many pixels
[[maybe_unused]] constexpr int kFilterErodeParallelPixels = 4096;

enum class FilterErodeAxis { eX, eY };
enum class FilterErodeOperation { eErode, eDilate };

// The weights of the structuring element along one axis, for a width D >= 1 in pixels: w[i] is the weight of
// the pixel at offset i - radius (the result has 2*radius+1 elements, the outer ones not 0, w[radius] = 1).
inline std::vector<double>
filterErodeWeights(FilterErodeFilterEnum filter,
                   double D)
{
    // the support of the weights: |d| < D/2 + 1/2 (box), D (triangle), 1.5*D (quadratic, gaussian)
    const double support = (filter == eFilterErodeFilterBox) ? (D / 2 + 0.5) :
                           (filter == eFilterErodeFilterTriangle ? D : kFilterErodeSupportQuadraticGaussian * D);
    // the largest offset strictly inside the support
    const int radius = (std::max)(0, (int)std::ceil(support) - 1);
    std::vector<double> w(2 * radius + 1);
    for (int i = -radius; i <= radius; ++i) {
        const double d = std::abs( (double)i );
        double v = 0.;
        switch (filter) {
        case eFilterErodeFilterBox:
            v = (std::min)( 1., (std::max)(0., D / 2 + 0.5 - d) );
            break;
        case eFilterErodeFilterTriangle:
            v = (std::max)(0., 1. - d / D);
            break;
        case eFilterErodeFilterQuadratic: {
            const double t = d / D;
            v = ( (t < 0.5) ? (kFilterErodeQuadraticCentre - t * t) : ( (t < 1.5) ? 0.5 * (1.5 - t) * (1.5 - t) : 0. ) ) /
                kFilterErodeQuadraticCentre;
            break;
        }
        case eFilterErodeFilterGaussian:
            v = (d < kFilterErodeSupportQuadraticGaussian * D) ? std::pow(2., -(2 * d / D) * (2 * d / D)) : 0.;
            break;
        }
        w[i + radius] = v;
    }

    return w;
}

// One pass of the filter along an axis of img, with Nearest boundary conditions: for each pixel, the minimum
// (erode) of 1 - w*(1-v), or the maximum (dilate) of w*v, over the pixels of nonzero weight w (v at w = 1, so
// that infinite values stay infinite). NaN values are left out (the result is NaN only if all are NaN).
// Returns false if the render was aborted (img is then partly filtered).
inline bool
filterErodePass(cimg_library::CImg<cimgpix_t>& img,
                FilterErodeAxis axis,
                FilterErodeOperation operation,
                const std::vector<double>& w)
{
    const int radius = (int)w.size() / 2;
    if ( img.is_empty() || (radius == 0) ) {
        return true;
    }
    const bool alongX = (axis == FilterErodeAxis::eX);
    const bool erode = (operation == FilterErodeOperation::eErode);
    const int n = alongX ? img.width() : img.height(); // the length of a line
    const int linesPerChannel = alongX ? img.height() : img.width();
    const int lines = linesPerChannel * img.spectrum();
    const std::ptrdiff_t step = alongX ? 1 : img.width(); // between two pixels of a line
    const std::ptrdiff_t lineStep = alongX ? img.width() : 1; // between two lines of a channel
    std::atomic<bool> aborted(false);
    cimg_pragma_openmp(parallel if ( (int)img.size() >= kFilterErodeParallelPixels ))
    {
        std::vector<cimgpix_t> line(n); // a copy of the line being filtered, one per thread
        cimg_pragma_openmp(for)
        for (int l = 0; l < lines; ++l) {
            if ( aborted || cimgRenderAborted() ) {
                aborted = true;
                continue; // a loop of an OpenMP for cannot be left early
            }
            cimgpix_t* const p = img.data(0, 0, 0, l / linesPerChannel) + (l % linesPerChannel) * lineStep;
            for (int i = 0; i < n; ++i) {
                line[i] = p[i * step];
            }
            for (int i = 0; i < n; ++i) {
                double best = 0.;
                bool found = false;
                for (int k = -radius; k <= radius; ++k) {
                    const double wk = w[k + radius];
                    if (wk <= 0.) {
                        continue;
                    }
                    const double v = line[(std::min)( (std::max)(i + k, 0), n - 1 )];
                    const double t = (wk >= 1.) ? v : (erode ? 1. - wk * (1. - v) : wk * v);
                    if ( std::isnan(t) ) {
                        continue;
                    }
                    if ( !found || (erode ? (t < best) : (t > best)) ) {
                        best = t;
                        found = true;
                    }
                }
                p[i * step] = found ? (cimgpix_t)best : std::numeric_limits<cimgpix_t>::quiet_NaN();
            }
        }
    }

    return !aborted;
}

struct CImgFilterErodeParams
{
    double sx; // signed size in pixels at render scale 1 (x: divided by the PAR)
    double sy;
    FilterErodeFilterEnum filter;
};

class CImgFilterErodePlugin
    : public CImgFilterPluginHelper<CImgFilterErodeParams, false>
{
public:
    explicit CImgFilterErodePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgFilterErodeParams, false>(handle, /*usesMask=*/false, kFilterErodeSupportsComponentRemapping, kFilterErodeSupportsTiles, kFilterErodeSupportsMultiResolution, kFilterErodeSupportsRenderScale, /*defaultUnpremult=*/ false)
        , _size( fetchDouble2DParam(kParamFilterErodeSize) )
        , _filter( fetchChoiceParam(kParamFilterErodeFilter) )
    {
        assert(_size && _filter);
    }

    virtual void getValuesAtTime(double time,
                                 CImgFilterErodeParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.sx, params.sy);
        const double par = (_srcClip && _srcClip->isConnected()) ? _srcClip->getPixelAspectRatio() : 0.;
        if (par > 0.) {
            params.sx /= par;
        }
        params.filter = (FilterErodeFilterEnum)_filter->getValueAtTime(time);
    }

    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgFilterErodeParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        cimgPadRect(rect, radius(params.filter, params.sx, renderScale.x), radius(params.filter, params.sy, renderScale.y), roi);
    }

    virtual void render(const OFX::RenderArguments &args,
                        const CImgFilterErodeParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // the 2D weight is the product of the x and y weights, so the filter is separable (each pass keeps
        // the order of the values); each axis erodes (size >= 0) or dilates by the sign of its own size, y first
        // (this matters only when the signs differ)
        if ( !filterErodePass(cimg, FilterErodeAxis::eY, operation(params.sy), weights(params.filter, params.sy, args.renderScale.y) ) ) {
            return;
        }
        filterErodePass(cimg, FilterErodeAxis::eX, operation(params.sx), weights(params.filter, params.sx, args.renderScale.x) );
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                            const CImgFilterErodeParams& params) OVERRIDE FINAL
    {
        return radius(params.filter, params.sx, args.renderScale.x) == 0 &&
               radius(params.filter, params.sy, args.renderScale.y) == 0;
    }

    // pixels outside the source get the value of the nearest pixel, as inside the image
    virtual int getBoundary(const CImgFilterErodeParams& /*params*/) OVERRIDE FINAL { return 1; }

private:
    // a size of 0 or more erodes
    static FilterErodeOperation operation(double size)
    {
        return (size >= 0.) ? FilterErodeOperation::eErode : FilterErodeOperation::eDilate;
    }

    // the weights along one axis (with a size of 0, the Quadratic and Gaussian weights of width 1 still reach
    // the next pixels)
    static std::vector<double> weights(FilterErodeFilterEnum filter,
                                       double size,
                                       double renderScale)
    {
        return filterErodeWeights(filter, 2 * std::abs(size) * renderScale + 1);
    }

    // the radius of the structuring element, in pixels
    static int radius(FilterErodeFilterEnum filter,
                      double size,
                      double renderScale)
    {
        return (int)weights(filter, size, renderScale).size() / 2;
    }

    OFX::Double2DParam* const _size;
    OFX::ChoiceParam* const _filter;
};


// one factory class per major version (the Support library allows one instance of each factory class)
template <unsigned int majorVersion>
class CImgFilterErodePluginFactory
    : public OFX::PluginFactoryHelper<CImgFilterErodePluginFactory<majorVersion> >
{
public:
    CImgFilterErodePluginFactory(const std::string& id,
                                 unsigned int verMin)
        : OFX::PluginFactoryHelper<CImgFilterErodePluginFactory<majorVersion> >(id, majorVersion, verMin)
    {
    }

    virtual void load() OVERRIDE FINAL {OFX::ofxsThreadSuiteCheck();}
    virtual void unload() OVERRIDE FINAL {}
    virtual void describe(OFX::ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context) OVERRIDE FINAL;
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context) OVERRIDE FINAL;
};

template <unsigned int majorVersion>
void
CImgFilterErodePluginFactory<majorVersion>::describe(OFX::ImageEffectDescriptor& desc)
{
    desc.setLabel(kFilterErodePluginName);
    desc.setPluginGrouping(kFilterErodePluginGrouping);
    desc.setPluginDescription(kFilterErodePluginDescription);

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(kFilterErodeHostFrameThreading);
    desc.setSupportsMultiResolution(kFilterErodeSupportsMultiResolution);
    desc.setSupportsTiles(kFilterErodeSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kFilterErodeSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kFilterErodeSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kFilterErodeRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(OFX::ePixelComponentRGBA);
#endif
}

template <unsigned int majorVersion>
void
CImgFilterErodePluginFactory<majorVersion>::describeInContext(OFX::ImageEffectDescriptor& desc,
                                                             OFX::ContextEnum context)
{
    // the alpha channel only, by default
    OFX::PageParamDescriptor *page = CImgFilterErodePlugin::describeInContextBegin(desc, context,
                                                                                  /*supportsRGBA=*/ true,
                                                                                  /*supportsRGB=*/ true,
                                                                                  /*supportsXY=*/ true,
                                                                                  /*supportsAlpha=*/ true,
                                                                                  kFilterErodeSupportsTiles,
                                                                                  /*processRGB=*/ false,
                                                                                  /*processAlpha=*/ true,
                                                                                  /*processIsSecret=*/ false);
    {
        OFX::Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamFilterErodeSize);
        param->setLabel(kParamFilterErodeSizeLabel);
        param->setHint(kParamFilterErodeSizeHint);
        param->setDoubleType(OFX::eDoubleTypeXY);
        param->setRange(-1000, -1000, 1000, 1000);
        param->setDisplayRange(-100, -100, 100, 100);
        param->setDefault(kParamFilterErodeSizeDefault, kParamFilterErodeSizeDefault);
        param->setDigits(1);
        param->setIncrement(0.1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilterErodeFilter);
        param->setLabel(kParamFilterErodeFilterLabel);
        param->setHint(kParamFilterErodeFilterHint);
        assert(param->getNOptions() == eFilterErodeFilterBox);
        param->appendOption(kParamFilterErodeFilterOptionBox);
        assert(param->getNOptions() == eFilterErodeFilterTriangle);
        param->appendOption(kParamFilterErodeFilterOptionTriangle);
        assert(param->getNOptions() == eFilterErodeFilterQuadratic);
        param->appendOption(kParamFilterErodeFilterOptionQuadratic);
        assert(param->getNOptions() == eFilterErodeFilterGaussian);
        param->appendOption(kParamFilterErodeFilterOptionGaussian);
        param->setDefault( (int)kParamFilterErodeFilterDefault );
        if (page) {
            page->addChild(*param);
        }
    }

    CImgFilterErodePlugin::describeInContextEnd(desc, context, page);
}

template <unsigned int majorVersion>
OFX::ImageEffect*
CImgFilterErodePluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                          OFX::ContextEnum /*context*/)
{
    return new CImgFilterErodePlugin(handle);
}

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // ifndef CImg_CImgFilterErode_h
