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
 * OFX CImgErodeSmooth plugin.
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

#if cimg_version < 161
#error "This plugin requires CImg 1.6.1 produces incorrect results, please upgrade CImg."
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ErodeSmoothCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Erode or dilate input stream using a normalized power-weighted filter.\n" \
    "This gives a smoother result than the Erode or Dilate node.\n" \
    "See \"Robust local max-min filters by normalized power-weighted filtering\" by L.J. van Vliet, " \
    "http://dx.doi.org/10.1109/ICPR.2004.1334273\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgErodeSmooth"
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

#define kParamRange "range"
#define kParamRangeLabel "Range"
#define kParamRangeHint "Expected range for input values."

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Size (diameter) of the filter kernel, in pixel units (>=0). The standard deviation of the corresponding Gaussian is size/2.4. No filter is applied if size < 1.2. Negative values correspond to dilation, positive valies to erosion. Both values should have the same sign."
#define kParamSizeDefault 0.

#define kParamUniform "uniform"
#define kParamUniformLabel "Uniform"
#define kParamUniformHint "Apply the same amount of blur on X and Y."

#define kParamExponent "exponent"
#define kParamExponentLabel "Exponent"
#define kParamExponentHint "Exponent of the normalized power-weighted filter. Lower values give a smoother result. Default is 5."
#define kParamExponentDefault 5

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black", "Dirichlet boundary condition: pixel values out of the image domain are zero.", "black"
#define kParamBoundaryOptionNeumann "Nearest", "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain.", "nearest"
#define kParamBoundaryOptionPeriodic "Periodic", "Image is considered to be periodic out of the image domain.", "periodic"
#define kParamBoundaryDefault eBoundaryNeumann
enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "Bluring filter. The quasi-Gaussian filter should be appropriate in most cases. The Gaussian filter is more isotropic (its impulse response has rotational symmetry), but slower."
#define kParamFilterOptionQuasiGaussian "Quasi-Gaussian", "Quasi-Gaussian filter (0-order recursive Deriche filter, faster).", "quasigaussian"
#define kParamFilterOptionGaussian "Gaussian", "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower).", "gaussian"
#define kParamFilterOptionBox "Box", "Box filter - FIR (finite support / impulsional response).", "box"
#define kParamFilterOptionTriangle "Triangle", "Triangle/tent filter - FIR (finite support / impulsional response).", "triangle"
#define kParamFilterOptionQuadratic "Quadratic", "Quadratic filter - FIR (finite support / impulsional response).", "quadratic"
#define kParamFilterDefault eFilterQuadratic
enum FilterEnum
{
    eFilterQuasiGaussian = 0,
    eFilterGaussian,
    eFilterBox,
    eFilterTriangle,
    eFilterQuadratic,
};

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by 1.5*size (3.6*sigma)."

typedef cimgpix_t T;
using namespace cimg_library;


static inline
T
get_data(T *data,
         const int N,
         const unsigned long off,
         const bool boundary_conditions,
         const int x)
{
    assert(N >= 1);
    if (x < 0) {
        return boundary_conditions ? data[0] : T();
    }
    if (x >= N) {
        return boundary_conditions ? data[(N - 1) * off] : T();
    }

    return data[x * off];
}

// [internal] Apply a box/triangle/quadratic filter (used by CImg<T>::box()).
/**
   \param ptr the pointer of the data
   \param N size of the data
   \param width width of the box filter
   \param off the offset between two data point
   \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
   \param order the order of the filter 0 (smoothing), 1st derivtive, 2nd derivative, 3rd derivative
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
 **/
static void
_cimg_box_apply(T *data,
                const double width,
                const int N,
                const unsigned long off,
                const int iter,
                const int order,
                const bool boundary_conditions)
{
    // smooth
    if ( (width > 1.) && (iter > 0) ) {
        int w2 = (int)(width - 1) / 2;
        double frac = ( width - (2 * w2 + 1) ) / 2.;
        int winsize = 2 * w2 + 1;
        std::vector<T> win(winsize);
        for (int i = 0; i < iter; ++i) {
            // prepare for first iteration
            double sum = 0; // window sum
            for (int x = -w2; x <= w2; ++x) {
                win[x + w2] = get_data(data, N, off, boundary_conditions, x);
                sum += win[x + w2];
            }
            int ifirst = 0;
            int ilast = 2 * w2;
            T prev = get_data(data, N, off, boundary_conditions, -w2 - 1);
            T next = get_data(data, N, off, boundary_conditions, +w2 + 1);
            // main loop
            for (int x = 0; x < N - 1; ++x) {
                // add partial pixels
                double sum2 = sum + frac * (prev + next);
                // fill result
                data[x * off] = sum2 / width;
                // advance for next iteration
                prev = win[ifirst];
                sum -= prev;
                ifirst = (ifirst + 1) % winsize;
                ilast = (ilast + 1) % winsize;
                assert( (ilast + 1) % winsize == ifirst ); // it is a circular buffer
                win[ilast] = next;
                sum += next;
                next = get_data(data, N, off, boundary_conditions, x + w2 + 2);
            }
            // last iteration
            // add partial pixels
            double sum2 = sum + frac * (prev + next);
            // fill result
            data[(N - 1) * off] = sum2 / width;
        }
    }
    // derive
    switch (order) {
    case 0:
        // nothing to do
        break;
    case 1: {
        T p = get_data(data, N, off, boundary_conditions, -1);
        T c = get_data(data, N, off, boundary_conditions, 0);
        T n = get_data(data, N, off, boundary_conditions, +1);
        for (int x = 0; x < N - 1; ++x) {
            data[x * off] = (n - p) / 2.;
            // advance
            p = c;
            c = n;
            n = get_data(data, N, off, boundary_conditions, x + 2);
        }
        // last pixel
        data[(N - 1) * off] = (n - p) / 2.;
    }
    break;
    case 2: {
        T p = get_data(data, N, off, boundary_conditions, -1);
        T c = get_data(data, N, off, boundary_conditions, 0);
        T n = get_data(data, N, off, boundary_conditions, +1);
        for (int x = 0; x < N - 1; ++x) {
            data[x * off] = n - 2 * c + p;
            // advance
            p = c;
            c = n;
            n = get_data(data, N, off, boundary_conditions, x + 2);
        }
        // last pixel
        data[(N - 1) * off] = n - 2 * c + p;
    }
    break;
    }
} // _cimg_box_apply

//! Box/Triangle/Quadratic filter.
/**
   \param width width of the box filter
   \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
   \param order the order of the filter 0,1,2,3
   \param axis  Axis along which the filter is computed. Can be <tt>{ 'x' | 'y' | 'z' | 'c' }</tt>.
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
 **/
static void
box(CImg<T>& img,
    const float width,
    const int iter,
    const int order,
    const char axis = 'x',
    const bool boundary_conditions = true)
{
    if ( img.is_empty() ) {
        return /* *this*/;
    }
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis =  cimg::lowercase(axis); // was cimg::uncase(axis) before CImg 1.7.2
    if ( img.is_empty() || ( (width <= 1.f) && !order ) ) {
        return /* *this*/;
    }
    switch (naxis) {
    case 'x': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forYZC(img, y, z, c)
        _cimg_box_apply(img.data(0, y, z, c), width, img._width, 1U, iter, order, boundary_conditions);
    }
    break;
    case 'y': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXZC(img, x, z, c)
        _cimg_box_apply(img.data(x, 0, z, c), width, _height, (unsigned long)_width, iter, order, boundary_conditions);
    }
    break;
    case 'z': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYC(img, x, y, c)
        _cimg_box_apply(img.data(x, y, 0, c), width, _depth, (unsigned long)(_width * _height),
                        iter, order, boundary_conditions);
    }
    break;
    default: {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYZ(img, x, y, z)
        _cimg_box_apply(img.data(x, y, z, 0), width, _spectrum, (unsigned long)(_width * _height * _depth),
                        iter, order, boundary_conditions);
    }
    }
    /* *this*/
}

#define ERODESMOOTH_MIN 1.e-8 // minimum value for the weight
#define ERODESMOOTH_OFFSET 0.1 // offset to the image values to avoid divisions by zero

/// ErodeSmooth plugin
struct CImgErodeSmoothParams
{
    double min;
    double max;
    double sizex, sizey; // sizex takes PixelAspectRatio intor account
    int exponent;
    int boundary_i;
    FilterEnum filter;
    bool expandRoD;
};

class CImgErodeSmoothPlugin
    : public CImgFilterPluginHelper<CImgErodeSmoothParams, false>
{
public:

    CImgErodeSmoothPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgErodeSmoothParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
        , _range(NULL)
        , _size(NULL)
        , _uniform(NULL)
        , _exponent(NULL)
        , _boundary(NULL)
        , _filter(NULL)
        , _expandRoD(NULL)
    {
        _range = fetchDouble2DParam(kParamRange);
        _size  = fetchDouble2DParam(kParamSize);
        _uniform = fetchBooleanParam(kParamUniform);
        _exponent  = fetchIntParam(kParamExponent);
        _boundary  = fetchChoiceParam(kParamBoundary);
        assert(_range && _size && _uniform && _boundary);
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_expandRoD);
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_uniform->getValue() &&
             ( _uniform->getNumKeys() == 0) ) {
            _uniform->setIsSecretAndDisabled(true);
        }
    }

    virtual void getValuesAtTime(double time,
                                 CImgErodeSmoothParams& params) OVERRIDE FINAL
    {
        _range->getValueAtTime(time, params.min, params.max);
        _size->getValueAtTime(time, params.sizex, params.sizey);
        bool uniform;
        _uniform->getValueAtTime(time, uniform);
        if (uniform) {
            params.sizey = params.sizex;
        } else if ( ( (params.sizex > 0) && (params.sizey < 0) ) ||
                    ( ( params.sizex < 0) && ( params.sizey > 0) ) ) {
            // both sizes should have the same sign
            params.sizey = 0.;
        }
        double par = (_srcClip && _srcClip->isConnected()) ? _srcClip->getPixelAspectRatio() : 0.;
        if (par != 0.) {
            params.sizex /= par;
        }
        _exponent->getValueAtTime(time, params.exponent);
        _boundary->getValueAtTime(time, params.boundary_i);
        params.filter = (FilterEnum)_filter->getValueAtTime(time);
        _expandRoD->getValueAtTime(time, params.expandRoD);
    }

    bool getRegionOfDefinition(const OfxRectI& srcRoD,
                               const OfxPointD& renderScale,
                               const CImgErodeSmoothParams& params,
                               OfxRectI* dstRoD) OVERRIDE FINAL
    {
        double sx = renderScale.x * std::abs(params.sizex);
        double sy = renderScale.y * std::abs(params.sizey);

        if ( params.expandRoD && !Coords::rectIsEmpty(srcRoD) ) {
            if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
                float sigmax = (float)(sx / 2.4);
                float sigmay = (float)(sy / 2.4);
                if ( (sigmax < 0.1) && (sigmay < 0.1) ) {
                    return false; // identity
                }
                int delta_pixX = (std::max)( 3, (int)std::ceil(sx * 1.5) );
                int delta_pixY = (std::max)( 3, (int)std::ceil(sy * 1.5) );
                dstRoD->x1 = srcRoD.x1 - delta_pixX;
                dstRoD->x2 = srcRoD.x2 + delta_pixX;
                dstRoD->y1 = srcRoD.y1 - delta_pixY;
                dstRoD->y2 = srcRoD.y2 + delta_pixY;
            } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
                if ( (sx <= 1) && (sy <= 1) ) {
                    return false; // identity
                }
                int iter = ( params.filter == eFilterBox ? 1 :
                             (params.filter == eFilterTriangle ? 2 : 3) );
                int delta_pixX = iter * std::ceil( (sx - 1) / 2 );
                int delta_pixY = iter * std::ceil( (sy - 1) / 2 );
                dstRoD->x1 = srcRoD.x1 - delta_pixX;
                dstRoD->x2 = srcRoD.x2 + delta_pixX;
                dstRoD->y1 = srcRoD.y1 - delta_pixY;
                dstRoD->y2 = srcRoD.y2 + delta_pixY;
            } else {
                assert(false);
            }

            return true;
        }

        return false;
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgErodeSmoothParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        // same as in CImgBlur
        double sx = renderScale.x * std::abs(params.sizex);
        double sy = renderScale.y * std::abs(params.sizey);

        if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
            float sigmax = (float)std::abs(sx / 2.4);
            float sigmay = (float)std::abs(sy / 2.4);
            if ( (sigmax < 0.1) && (sigmay < 0.1) ) {
                *roi = rect;

                return;
            }

            int delta_pixX = (std::max)( 3, (int)std::ceil(sx * 1.5) );
            int delta_pixY = (std::max)( 3, (int)std::ceil(sy * 1.5) );
            roi->x1 = rect.x1 - delta_pixX;
            roi->x2 = rect.x2 + delta_pixX;
            roi->y1 = rect.y1 - delta_pixY;
            roi->y2 = rect.y2 + delta_pixY;
        } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
            int iter = ( params.filter == eFilterBox ? 1 :
                         (params.filter == eFilterTriangle ? 2 : 3) );
            int delta_pixX = iter * (std::floor( (sx - 1) / 2 ) + 1);
            int delta_pixY = iter * (std::floor( (sy - 1) / 2 ) + 1);
            roi->x1 = rect.x1 - delta_pixX;
            roi->x2 = rect.x2 + delta_pixX;
            roi->y1 = rect.y1 - delta_pixY;
            roi->y2 = rect.y2 + delta_pixY;
        } else {
            assert(false);
        }
    }

    virtual void render(const RenderArguments &args,
                        const CImgErodeSmoothParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        bool isdilate = params.sizex < 0 || params.sizey < 0;
        const double rmin = isdilate ? params.min : params.max;
        const double rmax = isdilate ? params.max : params.min;
        double sx = args.renderScale.x * std::abs(params.sizex);
        double sy = args.renderScale.y * std::abs(params.sizey);

        if (rmax == rmin) {
            return;
        }
        // scale to [0,1]
        cimg_pragma_openmp(parallel for if (cimg.size()>=4096))
        cimg_rof(cimg, ptrd, cimgpix_t) * ptrd = (cimgpix_t)( (*ptrd - rmin) / (rmax - rmin) + ERODESMOOTH_OFFSET );

        // see "Robust local max-min filters by normalized power-weighted filtering" by L.J. van Vliet
        // http://dx.doi.org/10.1109/ICPR.2004.1334273
        // compute blur(x^(P+1))/blur(x^P)
        {
            cimg_library::CImg<cimgpix_t> denom(cimg, false);
            const double vmin = std::pow( (double)ERODESMOOTH_MIN, (double)1. / params.exponent );
            //printf("%g\n",vmin);
            cimg_pragma_openmp(parallel for if (denom.size()>=4096))
            cimg_rof(denom, ptrd, cimgpix_t) * ptrd = (cimgpix_t)std::pow( (double)( (*ptrd < 0. ? 0. : *ptrd) + vmin ), params.exponent ); // C++98 and C++11 both have std::pow(double,int)

            cimg.mul(denom);

            if ( abort() ) { return; }
            // almost the same code as in CImgBlur.cpp, except we smooth both cimg and denom
            if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
                float sigmax = (float)(sx / 2.4);
                float sigmay = (float)(sy / 2.4);
                if ( (sigmax < 0.1) && (sigmay < 0.1) ) {
                    return;
                }
                if (params.filter == eFilterGaussian) {
                    cimg.vanvliet(sigmax, 0, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    cimg.vanvliet(sigmay, 0, 'y', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    denom.vanvliet(sigmax, 0, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    denom.vanvliet(sigmay, 0, 'y', (bool)params.boundary_i);
                } else {
                    cimg.deriche(sigmax, 0, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    cimg.deriche(sigmay, 0, 'y', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    denom.deriche(sigmax, 0, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    denom.deriche(sigmay, 0, 'y', (bool)params.boundary_i);
                }
            } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
                int iter = ( params.filter == eFilterBox ? 1 :
                             (params.filter == eFilterTriangle ? 2 : 3) );
                box(cimg, sx, iter, 0, 'x', (bool)params.boundary_i);
                if ( abort() ) { return; }
                box(cimg, sy, iter, 0, 'y', (bool)params.boundary_i);
                if ( abort() ) { return; }
                box(denom, sx, iter, 0, 'x', (bool)params.boundary_i);
                if ( abort() ) { return; }
                box(denom, sy, iter, 0, 'y', (bool)params.boundary_i);
            } else {
                assert(false);
            }
            if ( abort() ) { return; }

            assert( cimg.width() == denom.width() && cimg.height() == denom.height() && cimg.depth() == denom.depth() && cimg.spectrum() == denom.spectrum() );
            cimg.div(denom);
            if ( abort() ) { return; }
        }

        // scale to [rmin,rmax]
        cimg_pragma_openmp(parallel for if (cimg.size()>=4096))
        cimg_rof(cimg, ptrd, cimgpix_t) * ptrd = (cimgpix_t)( (*ptrd - ERODESMOOTH_OFFSET) * (rmax - rmin) + rmin );
    } // render

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgErodeSmoothParams& params) OVERRIDE FINAL
    {
        return ( (params.sizex == 0. && params.sizey == 0) || params.exponent <= 0 );
    };
    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        if ( (paramName == kParamRange) && (args.reason == eChangeUserEdit) ) {
            double rmin, rmax;
            _range->getValueAtTime(args.time, rmin, rmax);
            if (rmax < rmin) {
                _range->setValue(rmax, rmin);
            }
        } else {
            CImgFilterPluginHelper<CImgErodeSmoothParams, false>::changedParam(args, paramName);
        }
    }

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const CImgErodeSmoothParams& params)  OVERRIDE FINAL { return params.boundary_i; }

private:

    // params
    Double2DParam *_range;
    Double2DParam *_size;
    BooleanParam *_uniform;
    IntParam *_exponent;
    ChoiceParam *_boundary;
    ChoiceParam *_filter;
    BooleanParam *_expandRoD;
};


mDeclarePluginFactory(CImgErodeSmoothPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgErodeSmoothPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgErodeSmoothPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                                ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgErodeSmoothPlugin::describeInContextBegin(desc, context,
                                                                                   kSupportsRGBA,
                                                                                   kSupportsRGB,
                                                                                   kSupportsXY,
                                                                                   kSupportsAlpha,
                                                                                   kSupportsTiles,
                                                                                   /*processRGB=*/ true,
                                                                                   /*processAlpha*/ false,
                                                                                   /*processIsSecret=*/ false);

    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamRange);
        param->setLabel(kParamRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setHint(kParamRangeHint);
        param->setDefault(0., 1.);
        param->setDoubleType(eDoubleTypePlain);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 1., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(-1000, -1000, 1000, 1000);
        param->setDisplayRange(-100, -100, 100, 100);
        param->setDefault(kParamSizeDefault, kParamSizeDefault);
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setDigits(1);
        param->setIncrement(0.1);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamUniform);
        param->setLabel(kParamUniformLabel);
        param->setHint(kParamUniformHint);
        // uniform parameter is false by default on Natron
        // https://github.com/MrKepzie/Natron/issues/1204
        param->setDefault(!getImageEffectHostDescription()->isNatron);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamExponent);
        param->setLabel(kParamExponentLabel);
        param->setHint(kParamExponentHint);
        param->setRange(1, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamExponentDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabel(kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic, kParamBoundaryOptionPeriodicHint);
        param->setDefault( (int)kParamBoundaryDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterQuasiGaussian && param->getNOptions() == 0);
        param->appendOption(kParamFilterOptionQuasiGaussian);
        assert(param->getNOptions() == eFilterGaussian && param->getNOptions() == 1);
        param->appendOption(kParamFilterOptionGaussian);
        assert(param->getNOptions() == eFilterBox && param->getNOptions() == 2);
        param->appendOption(kParamFilterOptionBox);
        assert(param->getNOptions() == eFilterTriangle && param->getNOptions() == 3);
        param->appendOption(kParamFilterOptionTriangle);
        assert(param->getNOptions() == eFilterQuadratic && param->getNOptions() == 4);
        param->appendOption(kParamFilterOptionQuadratic);
        param->setDefault( (int)kParamFilterDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgErodeSmoothPlugin::describeInContextEnd(desc, context, page);
} // CImgErodeSmoothPluginFactory::describeInContext

ImageEffect*
CImgErodeSmoothPluginFactory::createInstance(OfxImageEffectHandle handle,
                                             ContextEnum /*context*/)
{
    return new CImgErodeSmoothPlugin(handle);
}

static CImgErodeSmoothPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
