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
 * OFX CImgBlur and CImgLaplacian plugins.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <climits>
#include <cfloat> // DBL_MAX
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif
//#include <iostream>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#if cimg_version < 161
#error "This plugin requires CImg 1.6.1, please upgrade CImg."
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "BlurCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Blur input stream or compute derivatives.\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameLaplacian          "LaplacianCImg"
#define kPluginDescriptionLaplacian \
    "Blur input stream, and subtract the result from the input image. This is not a mathematically correct Laplacian (which would be the sum of second derivatives over X and Y).\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameChromaBlur          "ChromaBlurCImg"
#define kPluginDescriptionChromaBlur \
    "Blur the (Rec.709) chrominance of an input stream. Used to prep strongly compressed and chroma subsampled footage for keying.\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameBloom          "BloomCImg"
#define kPluginGroupingBloom      "Filter"
#define kPluginDescriptionBloom \
    "Apply a Bloom filter (Kawase 2004) that sums multiple blur filters of different radii,\n" \
    "resulting in a larger but sharper glare than a simple blur.\n" \
    "The blur radii follow a geometric progression (of common ratio 2 in the original implementation, " \
    "bloomRatio in this implementation), and a total of bloomCount blur kernels are summed up (bloomCount=5 " \
    "in the original implementation, and the kernels are Gaussian).\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "Ref.: Masaki Kawase, \"Practical Implementation of High Dynamic Range Rendering\", GDC 2004.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgBlur"
#define kPluginIdentifierLaplacian    "net.sf.cimg.CImgLaplacian"
#define kPluginIdentifierChromaBlur    "net.sf.cimg.CImgChromaBlur"
#define kPluginIdentifierBloom    "net.sf.cimg.CImgBloom"
// History:
// version 1.0: initial version
// version 2.0: size now has two dimensions
// version 3.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1 // except for ChromaBlur
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
#define kSupportsXY true // except for ChromaBlue
#define kSupportsAlpha true // except for ChromaBlue

#define kDefaultUnpremult false // Blur works on premultiplied RGBA by default
#define kDefaultProcessAlphaOnRGBA true // Alpha is processed as other channels

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Size (diameter) of the filter kernel, in pixel units (>=0). The standard deviation of the corresponding Gaussian is size/2.4. No filter is applied if size < 1.2."
#define kParamSizeDefault 0.
#define kParamSizeDefaultLaplacian 3.

#define kParamUniform "uniform"
#define kParamUniformLabel "Uniform"
#define kParamUniformHint "Apply the same amount of blur on X and Y."

#define kParamOrderX "orderX"
#define kParamOrderXLabel "X derivation order"
#define kParamOrderXHint "Derivation order in the X direction. (orderX=0,orderY=0) does smoothing, (orderX=1,orderY=0) computes the X component of the image gradient."

#define kParamOrderY "orderY"
#define kParamOrderYLabel "Y derivation order"
#define kParamOrderYHint "Derivation order in the Y direction. (orderX=0,orderY=0) does smoothing, (orderX=0,orderY=1) computes the X component of the image gradient."

#define kParamBloomRatio "bloomRatio"
#define kParamBloomRatioLabel "Ratio"
#define kParamBloomRatioHint "Ratio between successive kernel sizes of the bloom filter. A ratio of 1 gives no Bloom effect, just the original blur. A higher ratio gives a blur kernel with a heavier tail. The original implementation uses a value of 2."
#define kParamBloomRatioDefault 2.

#define kParamBloomCount "bloomCount"
#define kParamBloomCountLabel "Count"
#define kParamBloomCountHint "Number of blur kernels of the bloom filter. The original implementation uses a value of 5. Higher values give a wider of heavier tail (the size of the largest blur kernel is 2**bloomCount * size). A count of 1 is just the original blur."
#define kParamBloomCountDefault 5

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black"
#define kParamBoundaryOptionDirichletHint "Dirichlet boundary condition: pixel values out of the image domain are zero."
#define kParamBoundaryOptionNeumann "Nearest"
#define kParamBoundaryOptionNeumannHint "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain."
#define kParamBoundaryOptionPeriodic "Periodic"
#define kParamBoundaryOptionPeriodicHint "Image is considered to be periodic out of the image domain."
#define kParamBoundaryDefault eBoundaryDirichlet
#define kParamBoundaryDefaultLaplacian eBoundaryNeumann
#define kParamBoundaryDefaultBloom eBoundaryNeumann

enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

#define kParamChrominanceMath "chrominanceMath"
#define kParamChrominanceMathLabel "Chrominance Math"
#define kParamChrominanceMathHint "Formula used to compute chrominance from RGB values."
#define kParamChrominanceMathOptionRec709 "Rec. 709"
#define kParamChrominanceMathOptionRec709Hint "Use Rec. 709."
#define kParamChrominanceMathOptionCcir601 "CCIR 601"
#define kParamChrominanceMathOptionCcir601Hint "Use CCIR 601."

enum ChrominanceMathEnum
{
    eChrominanceMathRec709,
    eChrominanceMathCcir601,
};

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "Bluring filter. The quasi-Gaussian filter should be appropriate in most cases. The Gaussian filter is more isotropic (its impulse response has rotational symmetry), but slower."
#define kParamFilterOptionQuasiGaussian "Quasi-Gaussian"
#define kParamFilterOptionQuasiGaussianHint "Quasi-Gaussian filter (0-order recursive Deriche filter, faster) - IIR (infinite support / impulsional response)."
#define kParamFilterOptionGaussian "Gaussian"
#define kParamFilterOptionGaussianHint "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower) - IIR (infinite support / impulsional response)."
#define kParamFilterOptionBox "Box"
#define kParamFilterOptionBoxHint "Box filter - FIR (finite support / impulsional response)."
#define kParamFilterOptionTriangle "Triangle"
#define kParamFilterOptionTriangleHint "Triangle/tent filter - FIR (finite support / impulsional response)."
#define kParamFilterOptionQuadratic "Quadratic"
#define kParamFilterOptionQuadraticHint "Quadratic filter - FIR (finite support / impulsional response)."
#define kParamFilterDefault eFilterGaussian
#define kParamFilterDefaultBloom eFilterQuasiGaussian
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

#if cimg_version < 160
#define cimgblur_internal_vanvliet
#endif

// Exponentiation by squaring
// works with positive or negative integer exponents
template<typename T>
T
ipow(T base,
     int exp)
{
    T result = T(1);

    if (exp >= 0) {
        while (exp) {
            if (exp & 1) {
                result *= base;
            }
            exp >>= 1;
            base *= base;
        }
    } else {
        exp = -exp;
        while (exp) {
            if (exp & 1) {
                result /= base;
            }
            exp >>= 1;
            base *= base;
        }
    }

    return result;
}

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
    const char naxis = cimg::lowercase(axis); // was cimg::uncase(axis) before CImg 1.7.2
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

#ifdef cimgblur_internal_vanvliet
// [internal] Apply a recursive filter (used by CImg<T>::vanvliet()).
/**
   \param ptr the pointer of the data
   \param filter the coefficient of the filter in the following order [n,n-1,n-2,n-3].
   \param N size of the data
   \param off the offset between two data point
   \param order the order of the filter 0 (smoothing), 1st derivtive, 2nd derivative, 3rd derivative
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
   \note dirichlet boundary conditions have a strange behavior. And
   boundary condition should be corrected using Bill Triggs method (IEEE trans on Sig Proc 2005).
 **/
template <int K>
static void
_cimg_recursive_apply(T *data,
                      const double filter[],
                      const int N,
                      const unsigned long off,
                      const int order,
                      const bool boundary_conditions)
{
    double val[K];  // res[n,n-1,n-2,n-3,..] or res[n,n+1,n+2,n+3,..]
    const double
        sumsq = filter[0],
        sum = sumsq * sumsq,
        b1 = filter[1], b2 = filter[2], b3 = filter[3],
        a3 = b3,
        a2 = b2,
        a1 = b1,
        scaleM = 1.0 / ( (1.0 + a1 - a2 + a3) * (1.0 - a1 - a2 - a3) * (1.0 + a2 + (a1 - a3) * a3) );
    double M[9]; // Triggs matrix (for K == 4)

    if (K == 4) {
        M[0] = scaleM * (-a3 * a1 + 1.0 - a3 * a3 - a2);
        M[1] = scaleM * (a3 + a1) * (a2 + a3 * a1);
        M[2] = scaleM * a3 * (a1 + a3 * a2);
        M[3] = scaleM * (a1 + a3 * a2);
        M[4] = -scaleM * (a2 - 1.0) * (a2 + a3 * a1);
        M[5] = -scaleM * a3 * (a3 * a1 + a3 * a3 + a2 - 1.0);
        M[6] = scaleM * (a3 * a1 + a2 + a1 * a1 - a2 * a2);
        M[7] = scaleM * (a1 * a2 + a3 * a2 * a2 - a1 * a3 * a3 - a3 * a3 * a3 - a3 * a2 + a3);
        M[8] = scaleM * a3 * (a1 + a3 * a2);
    }
    switch (order) {
    case 0: {
        const double iplus = (boundary_conditions ? data[(N - 1) * off] : 0);
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 1; k < K; ++k) {
                    val[k] = (boundary_conditions ? *data / sumsq : 0);
                }
            } else {
                /* apply Triggs border condition */
                const double
                    uplus = iplus / (1.0 - a1 - a2 - a3),
                    vplus = uplus / (1.0 - b1 - b2 - b3),
                    p1 = val[1],
                    p2 = val[2],
                    p3 = val[3],
                    unp = p1 - uplus,
                    unp1 = p2 - uplus,
                    unp2 = p3 - uplus;
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2 + vplus) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2 + vplus) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2 + vplus) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N; ++n) {
                val[0] = (*data);
                if (pass) {
                    val[0] *= sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            if (!pass) {
                data -= off;
            }
        }
    }
    break;
    case 1: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off);
                    val[0] = 0.5f * (x[0] - x[2]);
                } else {
                    val[0] = (*data) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                    for (int k = 2; k > 0; --k) {
                        x[k] = x[k - 1];
                    }
                } else {
                    data -= off;
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    case 2: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off); val[0] = (x[1] - x[2]);
                } else                                                            {
                    x[0] = *(data - off); val[0] = (x[2] - x[1]) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = 2; k > 0; --k) {
                    x[k] = x[k - 1];
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    case 3: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off); val[0] = (x[0] - 2 * x[1] + x[2]);
                } else                                                                     {
                    x[0] = *(data - off); val[0] = 0.5f * (x[2] - x[0]) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = 2; k > 0; --k) {
                    x[k] = x[k - 1];
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    } // switch
} // _cimg_recursive_apply

//! Van Vliet recursive Gaussian filter.
/**
   \param sigma standard deviation of the Gaussian filter
   \param order the order of the filter 0,1,2,3
   \param axis  Axis along which the filter is computed. Can be <tt>{ 'x' | 'y' | 'z' | 'c' }</tt>.
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
   \note dirichlet boundary condition has a strange behavior

   I.T. Young, L.J. van Vliet, M. van Ginkel, Recursive Gabor filtering.
   IEEE Trans. Sig. Proc., vol. 50, pp. 2799-2805, 2002.

   (this is an improvement over Young-Van Vliet, Sig. Proc. 44, 1995)

   Boundary conditions (only for order 0) using Triggs matrix, from
   B. Triggs and M. Sdika. Boundary conditions for Young-van Vliet
   recursive filtering. IEEE Trans. Signal Processing,
   vol. 54, pp. 2365-2367, 2006.
 **/
void
vanvliet(CImg<T>& img,
         const float sigma,
         const int order,
         const char axis = 'x',
         const bool boundary_conditions = true)
{
    if ( img.is_empty() ) {
        return /* *this*/;
    }
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis = cimg::uncase(axis);
    const float nsigma = sigma >= 0 ? sigma : -sigma * (naxis == 'x' ? _width : naxis == 'y' ? _height : naxis == 'z' ? _depth : _spectrum) / 100;
    if ( img.is_empty() || ( (nsigma < 0.1f) && !order ) ) {
        return /* *this*/;
    }
    const double
        nnsigma = nsigma < 0.1f ? 0.1f : nsigma,
        m0 = 1.16680, m1 = 1.10783, m2 = 1.40586,
        m1sq = m1 * m1, m2sq = m2 * m2,
        q = ( nnsigma < 3.556 ? -0.2568 + 0.5784 * nnsigma + 0.0561 * nnsigma * nnsigma : 2.5091 + 0.9804 * (nnsigma - 3.556) ),
        qsq = q * q,
        scale = (m0 + q) * (m1sq + m2sq + 2 * m1 * q + qsq),
        b1 = -q * (2 * m0 * m1 + m1sq + m2sq + (2 * m0 + 4 * m1) * q + 3 * qsq) / scale,
        b2 = qsq * (m0 + 2 * m1 + 3 * q) / scale,
        b3 = -qsq * q / scale,
        B = ( m0 * (m1sq + m2sq) ) / scale;
    double filter[4];
    filter[0] = B; filter[1] = -b1; filter[2] = -b2; filter[3] = -b3;
    switch (naxis) {
    case 'x': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forYZC(img, y, z, c)
        _cimg_recursive_apply<4>(img.data(0, y, z, c), filter, img._width, 1U, order, boundary_conditions);
    }
    break;
    case 'y': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXZC(img, x, z, c)
        _cimg_recursive_apply<4>(img.data(x, 0, z, c), filter, _height, (unsigned long)_width, order, boundary_conditions);
    }
    break;
    case 'z': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYC(img, x, y, c)
        _cimg_recursive_apply<4>(img.data(x, y, 0, c), filter, _depth, (unsigned long)(_width * _height),
                                 order, boundary_conditions);
    }
    break;
    default: {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYZ(img, x, y, z)
        _cimg_recursive_apply<4>(img.data(x, y, z, 0), filter, _spectrum, (unsigned long)(_width * _height * _depth),
                                 order, boundary_conditions);
    }
    }
    /* *this*/
} // vanvliet

#endif // cimg_version < 160

/// Blur plugin
struct CImgBlurParams
{
    double sizex, sizey; // sizex takes PixelAspectRatio intor account
    int orderX;
    int orderY;
    double bloomRatio;
    int bloomCount;
    ChrominanceMathEnum chrominanceMath;
    int boundary_i;
    FilterEnum filter;
    bool expandRoD;
};

enum BlurPluginEnum
{
    eBlurPluginBlur,
    eBlurPluginLaplacian,
    eBlurPluginChromaBlur,
    eBlurPluginBloom
};

class CImgBlurPlugin
    : public CImgFilterPluginHelper<CImgBlurParams, false>
{
public:

    CImgBlurPlugin(OfxImageEffectHandle handle,
                   BlurPluginEnum blurPlugin = eBlurPluginBlur)
        : CImgFilterPluginHelper<CImgBlurParams, false>(handle, blurPlugin == eBlurPluginChromaBlur ? false : kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, kDefaultUnpremult, kDefaultProcessAlphaOnRGBA)
        , _blurPlugin(blurPlugin)
        , _size(0)
        , _uniform(0)
        , _orderX(0)
        , _orderY(0)
        , _bloomRatio(0)
        , _bloomCount(0)
        , _chrominanceMath(0)
        , _boundary(0)
        , _filter(0)
        , _expandRoD(0)
    {
        _size  = fetchDouble2DParam(kParamSize);
        _uniform = fetchBooleanParam(kParamUniform);
        assert(_size && _uniform);
        if (blurPlugin == eBlurPluginBlur) {
            _orderX = fetchIntParam(kParamOrderX);
            _orderY = fetchIntParam(kParamOrderY);
            assert(_orderX && _orderY);
        }
        if (blurPlugin == eBlurPluginBloom) {
            _bloomRatio = fetchDoubleParam(kParamBloomRatio);
            _bloomCount = fetchIntParam(kParamBloomCount);
            assert(_bloomRatio && _bloomCount);
        }
        if (blurPlugin == eBlurPluginChromaBlur) {
            _chrominanceMath = fetchChoiceParam(kParamChrominanceMath);
            assert(_chrominanceMath);
        } else {
            _boundary  = fetchChoiceParam(kParamBoundary);
            assert(_boundary);
        }
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
        if (blurPlugin != eBlurPluginChromaBlur) {
            _expandRoD = fetchBooleanParam(kParamExpandRoD);
            assert(_expandRoD);
        }
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_uniform->getValue() &&
             ( _uniform->getNumKeys() == 0) ) {
            _uniform->setIsSecret(true);
        }
    }

    virtual void getValuesAtTime(double time,
                                 CImgBlurParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.sizex, params.sizey);
        bool uniform = _uniform->getValueAtTime(time);
        if (uniform) {
            params.sizey = params.sizex;
        }
        double par = (_srcClip && _srcClip->isConnected()) ? _srcClip->getPixelAspectRatio() : 0.;
        if (par != 0.) {
            params.sizex /= par;
        }
        if (_blurPlugin == eBlurPluginBlur) {
            params.orderX = std::max( 0, _orderX->getValueAtTime(time) );
            params.orderY = std::max( 0, _orderY->getValueAtTime(time) );
        } else {
            params.orderX = params.orderY = 0;
        }
        if (_blurPlugin == eBlurPluginBloom) {
            params.bloomRatio = _bloomRatio->getValueAtTime(time);
            params.bloomCount = std::max( 1, _bloomCount->getValueAtTime(time) );
            if (params.bloomRatio <= 1.) {
                params.bloomCount = 1;
            }
            if (params.bloomCount == 1) {
                params.bloomRatio = 1.;
            }
        } else {
            params.bloomRatio = 1.;
            params.bloomCount = 1;
        }
        if (_blurPlugin == eBlurPluginChromaBlur) {
            params.chrominanceMath = (ChrominanceMathEnum)_chrominanceMath->getValueAtTime(time);
            params.boundary_i = 1; // nearest
        } else {
            params.boundary_i = _boundary->getValueAtTime(time);
        }
        params.filter = (FilterEnum)_filter->getValueAtTime(time);
        params.expandRoD = (_blurPlugin == eBlurPluginChromaBlur) ? false : _expandRoD->getValueAtTime(time);
    }

    bool getRegionOfDefinition(const OfxRectI& srcRoD,
                               const OfxPointD& renderScale,
                               const CImgBlurParams& params,
                               OfxRectI* dstRoD) OVERRIDE FINAL
    {
        double sx = renderScale.x * params.sizex;
        double sy = renderScale.y * params.sizey;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            double scale = ipow( params.bloomRatio, (params.bloomCount - 1) );
            sx *= scale;
            sy *= scale;
        }
        if ( params.expandRoD && !isEmpty(srcRoD) ) {
            if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
                float sigmax = (float)(sx / 2.4);
                float sigmay = (float)(sy / 2.4);
                if ( (sigmax < 0.1) && (sigmay < 0.1) && (params.orderX == 0) && (params.orderY == 0) ) {
                    return false; // identity
                }
                int delta_pixX = std::max( 3, (int)std::ceil(sx * 1.5) );
                int delta_pixY = std::max( 3, (int)std::ceil(sy * 1.5) );
                dstRoD->x1 = srcRoD.x1 - delta_pixX - params.orderX;
                dstRoD->x2 = srcRoD.x2 + delta_pixX + params.orderX;
                dstRoD->y1 = srcRoD.y1 - delta_pixY - params.orderY;
                dstRoD->y2 = srcRoD.y2 + delta_pixY + params.orderY;
            } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
                if ( (sx <= 1) && (sy <= 1) && (params.orderX == 0) && (params.orderY == 0) ) {
                    return false; // identity
                }
                int iter = ( params.filter == eFilterBox ? 1 :
                             (params.filter == eFilterTriangle ? 2 : 3) );
                int delta_pixX = iter * std::ceil( (sx - 1) / 2 );
                int delta_pixY = iter * std::ceil( (sy - 1) / 2 );
                dstRoD->x1 = srcRoD.x1 - delta_pixX - (params.orderX > 0);
                dstRoD->x2 = srcRoD.x2 + delta_pixX + (params.orderX > 0);
                dstRoD->y1 = srcRoD.y1 - delta_pixY - (params.orderY > 0);
                dstRoD->y2 = srcRoD.y2 + delta_pixY + (params.orderY > 0);
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
                        const CImgBlurParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        double sx = renderScale.x * params.sizex;
        double sy = renderScale.y * params.sizey;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            double scale = ipow( params.bloomRatio, (params.bloomCount - 1) );
            sx *= scale;
            sy *= scale;
        }
        if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
            float sigmax = (float)(sx / 2.4);
            float sigmay = (float)(sy / 2.4);
            if ( (sigmax < 0.1) && (sigmay < 0.1) && (params.orderX == 0) && (params.orderY == 0) ) {
                *roi = rect;

                return;
            }

            int delta_pixX = std::max( 3, (int)std::ceil(sx * 1.5) );
            int delta_pixY = std::max( 3, (int)std::ceil(sy * 1.5) );
            roi->x1 = rect.x1 - delta_pixX - params.orderX;
            roi->x2 = rect.x2 + delta_pixX + params.orderX;
            roi->y1 = rect.y1 - delta_pixY - params.orderY;
            roi->y2 = rect.y2 + delta_pixY + params.orderY;
        } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
            int iter = ( params.filter == eFilterBox ? 1 :
                         (params.filter == eFilterTriangle ? 2 : 3) );
            int delta_pixX = iter * (std::floor( (sx - 1) / 2 ) + 1);
            int delta_pixY = iter * (std::floor( (sy - 1) / 2 ) + 1);
            roi->x1 = rect.x1 - delta_pixX - (params.orderX > 0);
            roi->x2 = rect.x2 + delta_pixX + (params.orderX > 0);
            roi->y1 = rect.y1 - delta_pixY - (params.orderY > 0);
            roi->y2 = rect.y2 + delta_pixY + (params.orderY > 0);
        } else {
            assert(false);
        }
    }

    virtual void render(const OFX::RenderArguments &args,
                        const CImgBlurParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        double sx = args.renderScale.x * params.sizex;
        double sy = args.renderScale.y * params.sizey;
        //std::cout << "renderScale=" << args.renderScale.x << ',' << args.renderScale.y << std::endl;
        //std::cout << "renderWindow=" << args.renderWindow.x1 << ',' << args.renderWindow.y1 << ',' << args.renderWindow.x2 << ',' << args.renderWindow.y2 << std::endl;
        //std::cout << "cimg=" << cimg.width() << ',' << cimg.height() << std::endl;
        CImg<cimgpix_t> cimg0;
        CImg<cimgpix_t> cimg1;
        if (_blurPlugin == eBlurPluginLaplacian) {
            cimg0 = cimg;
        } else if (_blurPlugin == eBlurPluginChromaBlur) {
            // ChromaBlur only supports RGBA and RGBA, and components cannot be remapped
            assert(cimg.spectrum() >= 3);
            // allocate chrominance image
            cimg0.resize(cimg.width(), cimg.height(), cimg.depth(), 2);
            // chrominance (U+V) goes into cimg0, luminance goes into first channel of cimg
            cimgpix_t *pr = &cimg(0, 0, 0, 0);
            const cimgpix_t *pg = &cimg(0, 0, 0, 1);
            const cimgpix_t *pb = &cimg(0, 0, 0, 2);
            cimgpix_t *pu = &cimg0(0, 0, 0, 0), *pv = &cimg0(0, 0, 0, 1);
#pragma message WARN("wrong math: work in XYZ instead of YUV, remove rec601, add rec2020")
            if (params.chrominanceMath == eChrominanceMathRec709) {
                for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                    const float R = *pr;
                    const float G = *pg;
                    const float B = *pb;
                    /// YUV (Rec.709)
                    /// ref: https://en.wikipedia.org/wiki/YUV#HDTV_with_BT.709
                    *pr =  0.2126f  * R + 0.7152f  * G + 0.0722f  * B; //Y
                    *pu = -0.09991f * R - 0.33609f * G + 0.436f   * B; //U
                    *pv =  0.615f   * R - 0.55861f * G - 0.05639f * B; //V
                    ++pr;
                    ++pg;
                    ++pb;
                    ++pu;
                    ++pv;
                }
            } else {
                for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                    const float R = *pr;
                    const float G = *pg;
                    const float B = *pb;
                    /// YUV (BT.601)
                    /// ref: https://en.wikipedia.org/wiki/YUV#SDTV_with_BT.601
                    *pr =  0.299f   * R + 0.587f   * G + 0.114f  * B;
                    *pu = -0.14713f * R - 0.28886f * G + 0.114f  * B;
                    *pv =  0.615f   * R - 0.51499f * G - 0.10001 * B;
                    ++pr;
                    ++pg;
                    ++pb;
                    ++pu;
                    ++pv;
                }
            }
        } else if (_blurPlugin == eBlurPluginBloom) {
            // allocate a zero-valued result image to store the sum
            cimg1.assign(cimg.width(), cimg.height(), cimg.depth(), cimg.spectrum(), 0.);
        }

        // the loop is used only for BloomCImg, other filters only do one iteration
        for (int i = 0; i < params.bloomCount; ++i) {
            if (_blurPlugin == eBlurPluginBloom) {
                // copy original image
                cimg0 = cimg;
            }
            cimg_library::CImg<cimgpix_t>& cimg_blur = (_blurPlugin == eBlurPluginChromaBlur ||
                                                        _blurPlugin == eBlurPluginBloom) ? cimg0 : cimg;
            double scale = ipow(params.bloomRatio, i);
            if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
                float sigmax = (float)(sx * scale / 2.4);
                float sigmay = (float)(sy * scale / 2.4);
                if ( (_blurPlugin != eBlurPluginBloom) && (sigmax < 0.1) && (sigmay < 0.1) && (params.orderX == 0) && (params.orderY == 0) ) {
                    return;
                }
                // VanVliet filter was inexistent before 1.53, and buggy before CImg.h from
                // 57ffb8393314e5102c00e5f9f8fa3dcace179608 Thu Dec 11 10:57:13 2014 +0100
                if (params.filter == eFilterGaussian) {
#ifdef cimgblur_internal_vanvliet
                    vanvliet(cimg_blur, /*cimg_blur.vanvliet(*/ sigmax, params.orderX, 'x', (bool)params.boundary_i);
                    vanvliet(cimg_blur, /*cimg_blur.vanvliet(*/ sigmay, params.orderY, 'y', (bool)params.boundary_i);
#else
                    cimg_blur.vanvliet(sigmax, params.orderX, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    cimg_blur.vanvliet(sigmay, params.orderY, 'y', (bool)params.boundary_i);
#endif
                } else {
                    cimg_blur.deriche(sigmax, params.orderX, 'x', (bool)params.boundary_i);
                    if ( abort() ) { return; }
                    cimg_blur.deriche(sigmay, params.orderY, 'y', (bool)params.boundary_i);
                }
            } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
                int iter = ( params.filter == eFilterBox ? 1 :
                             (params.filter == eFilterTriangle ? 2 : 3) );
                box(cimg_blur, sx * scale, iter, params.orderX, 'x', (bool)params.boundary_i);
                if ( abort() ) { return; }
                box(cimg_blur, sy * scale, iter, params.orderY, 'y', (bool)params.boundary_i);
            } else {
                assert(false);
            }
            if (_blurPlugin == eBlurPluginBloom) {
                // accumulate result
                cimg1 += cimg0;
            }
        }

        if (_blurPlugin == eBlurPluginLaplacian) {
            cimg *= -1;
            cimg += cimg0;
        } else if (_blurPlugin == eBlurPluginChromaBlur) {
            // recombine luminance in cimg0 & chrominance in cimg to cimg
            // chrominance (U+V) is in cimg0, luminance is in first channel of cimg
            cimgpix_t *pr = &cimg(0, 0, 0, 0);
            cimgpix_t *pg = &cimg(0, 0, 0, 1);
            cimgpix_t *pb = &cimg(0, 0, 0, 2);
            const cimgpix_t *pu = &cimg0(0, 0, 0, 0);
            const cimgpix_t *pv = &cimg0(0, 0, 0, 1);
            if (params.chrominanceMath == eChrominanceMathRec709) {
                for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                    const float Y = *pr;
                    const float U = *pu;
                    const float V = *pv;
                    /// YUV (Rec.709)
                    /// ref: https://en.wikipedia.org/wiki/YUV#HDTV_with_BT.709
                    *pr = Y               + 1.28033f * V,
                    *pg = Y - 0.21482f * U - 0.38059f * V;
                    *pb = Y + 2.12798f * U;
                    ++pr;
                    ++pg;
                    ++pb;
                    ++pu;
                    ++pv;
                }
            } else {
                for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                    const float Y = *pr;
                    const float U = *pu;
                    const float V = *pv;
                    /// YUV (BT.601)
                    /// ref: https://en.wikipedia.org/wiki/YUV#SDTV_with_BT.601
                    *pr = Y                + 1.13983f * V,
                    *pg = Y - 0.39465f * U - 0.58060f * V;
                    *pb = Y + 2.03211f * U;
                    ++pr;
                    ++pg;
                    ++pb;
                    ++pu;
                    ++pv;
                }
            }
        } else if (_blurPlugin == eBlurPluginBloom) {
            cimg = cimg1 / params.bloomCount;
        }
    } // render

    virtual bool isIdentity(const OFX::IsIdentityArguments &args,
                            const CImgBlurParams& params) OVERRIDE FINAL
    {
        double sx = args.renderScale.x * params.sizex;
        double sy = args.renderScale.y * params.sizey;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            double scale = ipow( params.bloomRatio, (params.bloomCount - 1) );
            sx *= scale;
            sy *= scale;
        }
        bool ret = false;
        if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
            float sigmax = (float)(sx / 2.4);
            float sigmay = (float)(sy / 2.4);
            ret = (sigmax < 0.1 && sigmay < 0.1 && params.orderX == 0 && params.orderY == 0);
        } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
            ret = (sx <= 1 && sy <= 1 && params.orderX == 0 && params.orderY == 0);
        } else {
            assert(false);
        }

        return ret;
    };

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const CImgBlurParams& params)  OVERRIDE FINAL { return params.boundary_i; }

    // describe function for plugin factories
    static void describe(OFX::ImageEffectDescriptor& desc, int majorVersion, int minorVersion, BlurPluginEnum blurPlugin = eBlurPluginBlur);

    // describeInContext function for plugin factories
    static void describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context, int majorVersion, int minorVersion, BlurPluginEnum blurPlugin = eBlurPluginBlur);

private:

    // params
    const BlurPluginEnum _blurPlugin;
    OFX::Double2DParam *_size;
    OFX::BooleanParam *_uniform;
    OFX::IntParam *_orderX;
    OFX::IntParam *_orderY;
    OFX::DoubleParam *_bloomRatio;
    OFX::IntParam *_bloomCount;
    OFX::ChoiceParam *_chrominanceMath;
    OFX::ChoiceParam *_boundary;
    OFX::ChoiceParam *_filter;
    OFX::BooleanParam *_expandRoD;
};


void
CImgBlurPlugin::describe(OFX::ImageEffectDescriptor& desc,
                         int /*majorVersion*/,
                         int /*minorVersion*/,
                         BlurPluginEnum blurPlugin)
{
    // basic labels
    switch (blurPlugin) {
    case eBlurPluginBlur:
        desc.setLabel(kPluginName);
        desc.setPluginDescription(kPluginDescription);
        break;
    case eBlurPluginLaplacian:
        desc.setLabel(kPluginNameLaplacian);
        desc.setPluginDescription(kPluginDescriptionLaplacian);
        break;
    case eBlurPluginChromaBlur:
        desc.setLabel(kPluginNameChromaBlur);
        desc.setPluginDescription(kPluginDescriptionChromaBlur);
        break;
    case eBlurPluginBloom:
        desc.setLabel(kPluginNameBloom);
        desc.setPluginDescription(kPluginDescriptionBloom);
        break;
    }
    desc.setPluginGrouping(kPluginGrouping);

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
CImgBlurPlugin::describeInContext(OFX::ImageEffectDescriptor& desc,
                                  OFX::ContextEnum context,
                                  int /*majorVersion*/,
                                  int /*minorVersion*/,
                                  BlurPluginEnum blurPlugin)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgBlurPlugin::describeInContextBegin(desc, context,
                                                                            kSupportsRGBA,
                                                                            kSupportsRGB,
                                                                            blurPlugin == eBlurPluginChromaBlur ? false : kSupportsXY,
                                                                            blurPlugin == eBlurPluginChromaBlur ? false : kSupportsAlpha,
                                                                            kSupportsTiles,
                                                                            /*processRGB=*/ true,
                                                                            /*processAlpha*/ false,
                                                                            /*processIsSecret=*/ false);

    {
        OFX::Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(0, 0, 1000, 1000);
        if (blurPlugin == eBlurPluginChromaBlur) {
            param->setDisplayRange(0, 0, 10, 10);
        } else {
            param->setDisplayRange(0, 0, 100, 100);
        }
        if (blurPlugin == eBlurPluginLaplacian) {
            param->setDefault(kParamSizeDefaultLaplacian, kParamSizeDefaultLaplacian);
        } else {
            param->setDefault(kParamSizeDefault, kParamSizeDefault);
        }
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
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamUniform);
        param->setLabel(kParamUniformLabel);
        param->setHint(kParamUniformHint);
        // uniform parameter is false by default on Natron
        // https://github.com/MrKepzie/Natron/issues/1204
        param->setDefault(!OFX::getImageEffectHostDescription()->isNatron);
        if (page) {
            page->addChild(*param);
        }
    }

    if (blurPlugin == eBlurPluginBlur) {
        {
            OFX::IntParamDescriptor *param = desc.defineIntParam(kParamOrderX);
            param->setLabel(kParamOrderXLabel);
            param->setHint(kParamOrderXHint);
            param->setRange(0, 2);
            param->setDisplayRange(0, 2);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::IntParamDescriptor *param = desc.defineIntParam(kParamOrderY);
            param->setLabel(kParamOrderYLabel);
            param->setHint(kParamOrderYHint);
            param->setRange(0, 2);
            param->setDisplayRange(0, 2);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginBloom) {
        {
            OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBloomRatio);
            param->setLabel(kParamBloomRatioLabel);
            param->setHint(kParamBloomRatioHint);
            param->setRange(1., DBL_MAX);
            param->setDisplayRange(1., 4.);
            param->setDefault(kParamBloomRatioDefault);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            OFX::IntParamDescriptor *param = desc.defineIntParam(kParamBloomCount);
            param->setLabel(kParamBloomCountLabel);
            param->setHint(kParamBloomCountHint);
            param->setRange(1, INT_MAX);
            param->setDisplayRange(1, 10);
            param->setDefault(kParamBloomCountDefault);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginChromaBlur) {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChrominanceMath);
        param->setLabel(kParamChrominanceMathLabel);
        param->setHint(kParamChrominanceMathHint);
        assert(param->getNOptions() == eChrominanceMathRec709 && param->getNOptions() == 0);
        param->appendOption(kParamChrominanceMathOptionRec709, kParamChrominanceMathOptionRec709Hint);
        assert(param->getNOptions() == eChrominanceMathCcir601 && param->getNOptions() == 1);
        param->appendOption(kParamChrominanceMathOptionCcir601, kParamChrominanceMathOptionCcir601Hint);
        param->setDefault( (int)eChrominanceMathRec709 );
        if (page) {
            page->addChild(*param);
        }
    } else {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabel(kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet, kParamBoundaryOptionDirichletHint);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann, kParamBoundaryOptionNeumannHint);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic, kParamBoundaryOptionPeriodicHint);
        if (blurPlugin == eBlurPluginLaplacian) {
            param->setDefault( (int)kParamBoundaryDefaultLaplacian );
        } else if (blurPlugin == eBlurPluginBloom) {
            param->setDefault( (int)kParamBoundaryDefaultBloom );
        } else {
            param->setDefault( (int)kParamBoundaryDefault );
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterQuasiGaussian && param->getNOptions() == 0);
        param->appendOption(kParamFilterOptionQuasiGaussian, kParamFilterOptionQuasiGaussianHint);
        assert(param->getNOptions() == eFilterGaussian && param->getNOptions() == 1);
        param->appendOption(kParamFilterOptionGaussian, kParamFilterOptionGaussianHint);
        assert(param->getNOptions() == eFilterBox && param->getNOptions() == 2);
        param->appendOption(kParamFilterOptionBox, kParamFilterOptionBoxHint);
        assert(param->getNOptions() == eFilterTriangle && param->getNOptions() == 3);
        param->appendOption(kParamFilterOptionTriangle, kParamFilterOptionTriangleHint);
        assert(param->getNOptions() == eFilterQuadratic && param->getNOptions() == 4);
        param->appendOption(kParamFilterOptionQuadratic, kParamFilterOptionQuadraticHint);
        if (blurPlugin == eBlurPluginBloom) {
            param->setDefault( (int)kParamFilterDefaultBloom );
        } else {
            param->setDefault( (int)kParamFilterDefault );
        }
        if (page) {
            page->addChild(*param);
        }
    }
    if (blurPlugin != eBlurPluginChromaBlur) {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(blurPlugin != eBlurPluginBloom); // the expanded RoD of Bloom may be very large
        if (page) {
            page->addChild(*param);
        }
    }

    CImgBlurPlugin::describeInContextEnd(desc, context, page);
} // CImgBlurPlugin::describeInContext

mDeclarePluginFactory(CImgBlurPluginFactory, {}, {});
void
CImgBlurPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe( desc, getMajorVersion(), getMinorVersion() );
}

void
CImgBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                         OFX::ContextEnum context)
{
    return CImgBlurPlugin::describeInContext( desc, context, getMajorVersion(), getMinorVersion() );
}

OFX::ImageEffect*
CImgBlurPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle);
}

mDeclarePluginFactory(CImgLaplacianPluginFactory, {}, {});
void
CImgLaplacianPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, getMajorVersion(), getMinorVersion(), eBlurPluginLaplacian);
}

void
CImgLaplacianPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                              OFX::ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, getMajorVersion(), getMinorVersion(), eBlurPluginLaplacian);
}

OFX::ImageEffect*
CImgLaplacianPluginFactory::createInstance(OfxImageEffectHandle handle,
                                           OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginLaplacian);
}

mDeclarePluginFactory(CImgChromaBlurPluginFactory, {}, {});
void
CImgChromaBlurPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, getMajorVersion(), getMinorVersion(), eBlurPluginChromaBlur);
}

void
CImgChromaBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                               OFX::ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, getMajorVersion(), getMinorVersion(), eBlurPluginChromaBlur);
}

OFX::ImageEffect*
CImgChromaBlurPluginFactory::createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginChromaBlur);
}

mDeclarePluginFactory(CImgBloomPluginFactory, {}, {});
void
CImgBloomPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, getMajorVersion(), getMinorVersion(), eBlurPluginBloom);
}

void
CImgBloomPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc,
                                          OFX::ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, getMajorVersion(), getMinorVersion(), eBlurPluginBloom);
}

OFX::ImageEffect*
CImgBloomPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginBloom);
}

static CImgBlurPluginFactory p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static CImgLaplacianPluginFactory p2(kPluginIdentifierLaplacian, kPluginVersionMajor, kPluginVersionMinor);
static CImgChromaBlurPluginFactory p3(kPluginIdentifierChromaBlur, kPluginVersionMajor, kPluginVersionMinor);
static CImgBloomPluginFactory p4(kPluginIdentifierBloom, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)

OFXS_NAMESPACE_ANONYMOUS_EXIT
