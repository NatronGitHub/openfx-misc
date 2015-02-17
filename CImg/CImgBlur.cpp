/*
 OFX CImgBlur plugin.

 Copyright (C) 2014 INRIA

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England

 */

#include "CImgBlur.h"

#include <memory>
#include <cmath>
#include <cstring>
#include <climits>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsMerging.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#define kPluginName          "BlurCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Blur input stream by a quasi-Gaussian or Gaussian filter (recursive implementation), or compute derivatives.\n" \
"Uses the 'blur', 'vanvliet' and 'deriche' functions from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgBlur"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kDefaultUnpremult false // Blur works on premultiplied RGBA by default
#define kDefaultProcessAlphaOnRGBA true // Alpha is processed as other channels

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Size (diameter) of the filter kernel, in pixel units (>=0). The standard deviation of the corresponding Gaussian is size/2.4. No filter is applied if size < 1.2."
#define kParamSizeDefault 0.

#define kParamOrderX "orderX"
#define kParamOrderXLabel "X derivation order"
#define kParamOrderXHint "Derivation order in the X direction. (orderX=0,orderY=0) does smoothing, (orderX=1,orderY=0) computes the X component of the image gradient."

#define kParamOrderY "orderY"
#define kParamOrderYLabel "Y derivation order"
#define kParamOrderYHint "Derivation order in the Y direction. (orderX=0,orderY=0) does smoothing, (orderX=0,orderY=1) computes the X component of the image gradient."

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
enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
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

typedef float T;
using namespace cimg_library;

#if cimg_version < 160
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
static void _cimg_recursive_apply(T *data, const double filter[], const int N, const unsigned long off,
                                  const int order, const bool boundary_conditions) {
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
        case 0 : {
            const double iplus = (boundary_conditions?data[(N-1)*off]:0);
            for (int pass = 0; pass<2; ++pass) {
                if (!pass || K != 4) {
                    for (int k = 1; k<K; ++k) val[k] = (boundary_conditions?*data/sumsq:0);
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
                    data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                for (int n = (pass && K == 4); n<N; ++n) {
                    val[0] = (*data);
                    if (pass) val[0] *= sum;
                    for (int k = 1; k<K; ++k) val[0]+=val[k]*filter[k];
                    *data = (T)val[0];
                    if (!pass) data+=off; else data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                if (!pass) data-=off;
            }
        } break;
        case 1 : {
            double x[3]; // [front,center,back]
            for (int pass = 0; pass<2; ++pass) {
                if (!pass || K != 4) {
                    for (int k = 0; k<3; ++k) x[k] = (boundary_conditions?*data:0);
                    for (int k = 0; k<K; ++k) val[k] = 0;
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
                    data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                for (int n = (pass && K == 4); n<N-1; ++n) {
                    if (!pass) {
                        x[0] = *(data+off);
                        val[0] = 0.5f * (x[0] - x[2]);
                    } else val[0] = (*data)*sum;
                    for (int k = 1; k<K; ++k) val[0]+=val[k]*filter[k];
                    *data = (T)val[0];
                    if (!pass) {
                        data+=off;
                        for (int k = 2; k>0; --k) x[k] = x[k-1];
                    } else data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                *data = (T)0;
            }
        } break;
        case 2: {
            double x[3]; // [front,center,back]
            for (int pass = 0; pass<2; ++pass) {
                if (!pass || K != 4) {
                    for (int k = 0; k<3; ++k) x[k] = (boundary_conditions?*data:0);
                    for (int k = 0; k<K; ++k) val[k] = 0;
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
                    data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                for (int n = (pass && K == 4); n<N-1; ++n) {
                    if (!pass) { x[0] = *(data+off); val[0] = (x[1] - x[2]); }
                    else { x[0] = *(data-off); val[0] = (x[2] - x[1])*sum; }
                    for (int k = 1; k<K; ++k) val[0]+=val[k]*filter[k];
                    *data = (T)val[0];
                    if (!pass) data+=off; else data-=off;
                    for (int k = 2; k>0; --k) x[k] = x[k-1];
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                *data = (T)0;
            }
        } break;
        case 3: {
            double x[3]; // [front,center,back]
            for (int pass = 0; pass<2; ++pass) {
                if (!pass || K != 4) {
                    for (int k = 0; k<3; ++k) x[k] = (boundary_conditions?*data:0);
                    for (int k = 0; k<K; ++k) val[k] = 0;
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
                    data-=off;
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                for (int n = (pass && K == 4); n<N-1; ++n) {
                    if (!pass) { x[0] = *(data+off); val[0] = (x[0] - 2*x[1] + x[2]); }
                    else { x[0] = *(data-off); val[0] = 0.5f*(x[2] - x[0])*sum; }
                    for (int k = 1; k<K; ++k) val[0]+=val[k]*filter[k];
                    *data = (T)val[0];
                    if (!pass) data+=off; else data-=off;
                    for (int k = 2; k>0; --k) x[k] = x[k-1];
                    for (int k = K-1; k>0; --k) val[k] = val[k-1];
                }
                *data = (T)0;
            }
        } break;
    }
}

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
vanvliet(CImg<T>& img, const float sigma, const int order, const char axis='x', const bool boundary_conditions=true)
{
    if (img.is_empty()) return/* *this*/;
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis = cimg::uncase(axis);
    const float nsigma = sigma>=0?sigma:-sigma*(naxis=='x'?_width:naxis=='y'?_height:naxis=='z'?_depth:_spectrum)/100;
    if (img.is_empty() || (nsigma<0.1f && !order)) return/* *this*/;
    const double
    nnsigma = nsigma<0.1f?0.1f:nsigma,
    m0 = 1.16680, m1 = 1.10783, m2 = 1.40586,
    m1sq = m1 * m1, m2sq = m2 * m2,
    q = (nnsigma<3.556?-0.2568+0.5784*nnsigma+0.0561*nnsigma*nnsigma:2.5091+0.9804*(nnsigma-3.556)),
    qsq = q * q,
    scale = (m0 + q) * (m1sq + m2sq + 2 * m1 * q + qsq),
    b1 = -q * (2 * m0 * m1 + m1sq + m2sq + (2 * m0 + 4 * m1) * q + 3 * qsq) / scale,
    b2 = qsq * (m0 + 2 * m1 + 3 * q) / scale,
    b3 = -qsq * q / scale,
    B = ( m0 * (m1sq + m2sq) ) / scale;
    double filter[4];
    filter[0] = B; filter[1] = -b1; filter[2] = -b2; filter[3] = -b3;
    switch (naxis) {
        case 'x' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forYZC(img,y,z,c)
            _cimg_recursive_apply<4>(img.data(0,y,z,c),filter,img._width,1U,order,boundary_conditions);
        } break;
        case 'y' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXZC(img,x,z,c)
            _cimg_recursive_apply<4>(img.data(x,0,z,c),filter,_height,(unsigned long)_width,order,boundary_conditions);
        } break;
        case 'z' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXYC(img,x,y,c)
            _cimg_recursive_apply<4>(img.data(x,y,0,c),filter,_depth,(unsigned long)(_width*_height),
                                     order,boundary_conditions);
        } break;
        default : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXYZ(img,x,y,z)
            _cimg_recursive_apply<4>(img.data(x,y,z,0),filter,_spectrum,(unsigned long)(_width*_height*_depth),
                                     order,boundary_conditions);
        }
    }
    return/* *this*/;
}
#endif // cimg_version < 160

static inline
T get_data(T *data, const int N, const unsigned long off, const bool boundary_conditions, const int x)
{
    assert(N >= 1);
    if (x < 0) {
        return boundary_conditions ? data[0] : T();
    }
    if (x >= N) {
        return boundary_conditions ? data[(N-1)*off] : T();
    }
    return data[x*off];
}

// [internal] Apply a recursive filter (used by CImg<T>::vanvliet()).
/**
 \param ptr the pointer of the data
 \param N size of the data
 \param width width of the box filter
 \param off the offset between two data point
 \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
 \param order the order of the filter 0 (smoothing), 1st derivtive, 2nd derivative, 3rd derivative
 \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
 **/
static void _cimg_box_apply(T *data, const double width, const int N, const unsigned long off, const int iter,
                                  const int order, const bool boundary_conditions)
{
    // smooth
    if (width > 1. && iter > 0) {
        int w2 = (int)(width - 1)/2;
        double frac = (width - (2*w2+1)) / 2.;
        int winsize = 2*w2+1;
        std::vector<T> win(winsize);
        for (int i = 0; i < iter; ++i) {
            // prepare for first iteration
            double sum = 0; // window sum
            for (int x = -w2; x <= w2; ++x) {
                win[x+w2] = get_data(data, N, off, boundary_conditions, x);
                sum += win[x+w2];
            }
            int ifirst = 0;
            int ilast = 2*w2;
            T prev = get_data(data, N, off, boundary_conditions, - w2 - 1);
            T next = get_data(data, N, off, boundary_conditions, + w2 + 1);
            // main loop
            for (int x = 0; x < N-1; ++x) {
                // add partial pixels
                double sum2 = sum + frac * (prev + next);
                // fill result
                data[x*off] = sum2 / width;
                // advance for next iteration
                prev = win[ifirst];
                sum -= prev;
                ifirst = (ifirst + 1) % winsize;
                ilast = (ilast + 1) % winsize;
                assert((ilast + 1) % winsize == ifirst); // it is a circular buffer
                win[ilast] = next;
                sum += next;
                next = get_data(data, N, off, boundary_conditions, x + w2 + 2);
            }
            // last iteration
            // add partial pixels
            double sum2 = sum + frac * (prev + next);
            // fill result
            data[(N-1)*off] = sum2 / width;
        }
    }
    // derive
    switch (order) {
        case 0 :
            // nothing to do
            break;
        case 1 : {
            T p = get_data(data, N, off, boundary_conditions, -1);
            T c = get_data(data, N, off, boundary_conditions, 0);
            T n = get_data(data, N, off, boundary_conditions, +1);
            for (int x = 0; x < N-1; ++x) {
                data[x*off] = (n-p)/2.;
                // advance
                p = c;
                c = n;
                n = get_data(data, N, off, boundary_conditions, x+2);
            }
            // last pixel
            data[(N-1)*off] = (n-p)/2.;
        } break;
        case 2: {
            T p = get_data(data, N, off, boundary_conditions, -1);
            T c = get_data(data, N, off, boundary_conditions, 0);
            T n = get_data(data, N, off, boundary_conditions, +1);
            for (int x = 0; x < N-1; ++x) {
                data[x*off] = n-2*c+p;
                // advance
                p = c;
                c = n;
                n = get_data(data, N, off, boundary_conditions, x+2);
            }
            // last pixel
            data[(N-1)*off] = n-2*c+p;
        } break;
    }
}

//! Van Vliet recursive Gaussian filter.
/**
 \param width width of the box filter
 \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
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
static void
box(CImg<T>& img, const float width, const int iter, const int order, const char axis='x', const bool boundary_conditions=true)
{
    if (img.is_empty()) return/* *this*/;
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis = cimg::uncase(axis);
    if (img.is_empty() || (width <= 1.f && !order)) return/* *this*/;
    switch (naxis) {
        case 'x' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forYZC(img,y,z,c)
            _cimg_box_apply(img.data(0,y,z,c),width,img._width,1U,iter,order,boundary_conditions);
        } break;
        case 'y' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXZC(img,x,z,c)
            _cimg_box_apply(img.data(x,0,z,c),width,_height,(unsigned long)_width,iter,order,boundary_conditions);
        } break;
        case 'z' : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXYC(img,x,y,c)
            _cimg_box_apply(img.data(x,y,0,c),width,_depth,(unsigned long)(_width*_height),
                                     iter,order,boundary_conditions);
        } break;
        default : {
#ifdef cimg_use_openmp
#pragma omp parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16)
#endif
            cimg_forXYZ(img,x,y,z)
            _cimg_box_apply(img.data(x,y,z,0),width,_spectrum,(unsigned long)(_width*_height*_depth),
                                     iter,order,boundary_conditions);
        }
    }
    return/* *this*/;
}

using namespace OFX;

/// Blur plugin
struct CImgBlurParams
{
    double sizex, sizey;
    int orderX;
    int orderY;
    int boundary_i;
    FilterEnum filter;
    bool expandRoD;
};

class CImgBlurPlugin : public CImgFilterPluginHelper<CImgBlurParams,false>
{
public:

    CImgBlurPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgBlurParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, kDefaultUnpremult, kDefaultProcessAlphaOnRGBA)
    , _size(0)
    , _size2D(0)
    , _orderX(0)
    , _orderY(0)
    , _boundary(0)
    , _filter(0)
    , _expandRoD(0)
    {
        try {
            _size  = fetchDoubleParam(kParamSize);
        } catch (OFX::Exception::TypeRequest) {
            _size2D  = fetchDouble2DParam(kParamSize);
        }
        _orderX = fetchIntParam(kParamOrderX);
        _orderY = fetchIntParam(kParamOrderY);
        _boundary  = fetchChoiceParam(kParamBoundary);
        assert((_size || _size2D) && _orderX && _orderY && _boundary);
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_expandRoD);
    }

    virtual void getValuesAtTime(double time, CImgBlurParams& params) OVERRIDE FINAL
    {
        if (_size) {
            _size->getValueAtTime(time, params.sizex);
            params.sizey = params.sizex;
        } else {
            // major version > 1
            assert(_size2D);
            _size2D->getValueAtTime(time, params.sizex, params.sizey);
        }
        _orderX->getValueAtTime(time, params.orderX);
        _orderY->getValueAtTime(time, params.orderY);
        _boundary->getValueAtTime(time, params.boundary_i);
        int filter_i;
        _filter->getValueAtTime(time, filter_i);
        params.filter = (FilterEnum)filter_i;
        _expandRoD->getValueAtTime(time, params.expandRoD);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        if (params.filter == eFilterQuasiGaussian || params.filter == eFilterGaussian) {
            float sigmax = (float)(renderScale.x * params.sizex / 2.4);
            float sigmay = (float)(renderScale.y * params.sizey / 2.4);
            if (sigmax < 0.1 && sigmay < 0.1 && params.orderX == 0 && params.orderY == 0) {
                *roi = rect;
                return;
            }

            int delta_pixX = std::max(3, (int)std::ceil((params.sizex * 1.5) * renderScale.x));
            int delta_pixY = std::max(3, (int)std::ceil((params.sizey * 1.5) * renderScale.y));
            roi->x1 = rect.x1 - delta_pixX - params.orderX;
            roi->x2 = rect.x2 + delta_pixX + params.orderX;
            roi->y1 = rect.y1 - delta_pixY - params.orderY;
            roi->y2 = rect.y2 + delta_pixY + params.orderY;
        } else if (params.filter == eFilterBox || params.filter == eFilterTriangle || params.filter == eFilterQuadratic) {
            int iter = (params.filter == eFilterBox ? 1 :
                        (params.filter == eFilterTriangle ? 2 : 3));
            int delta_pixX = iter * (std::floor((renderScale.x * params.sizex-1)/ 2) + 1);
            int delta_pixY = iter * (std::floor((renderScale.y * params.sizey-1)/ 2) + 1);
            roi->x1 = rect.x1 - delta_pixX - (params.orderX > 0);
            roi->x2 = rect.x2 + delta_pixX + (params.orderX > 0);
            roi->y1 = rect.y1 - delta_pixY - (params.orderY > 0);
            roi->y2 = rect.y2 + delta_pixY + (params.orderY > 0);
        } else {
            assert(false);
        }
    }

    virtual void render(const OFX::RenderArguments &args, const CImgBlurParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.filter == eFilterQuasiGaussian || params.filter == eFilterGaussian) {
            float sigmax = (float)(args.renderScale.x * params.sizex / 2.4);
            float sigmay = (float)(args.renderScale.y * params.sizey / 2.4);
            if (sigmax < 0.1 && sigmay < 0.1 && params.orderX == 0 && params.orderY == 0) {
                return;
            }
#if       cimg_version >= 160
            //if (params.orderX == 0 && params.orderY == 0) {
            //    cimg.blur(sigma, (bool)params.boundary_i, params.filter == eFilterGaussian);
            //} else {
            if (params.filter == eFilterGaussian) {
                cimg.vanvliet(sigmax, params.orderX, 'x', (bool)params.boundary_i);
                cimg.vanvliet(sigmay, params.orderY, 'y', (bool)params.boundary_i);
            } else {
                cimg.deriche(sigmax, params.orderX, 'x', (bool)params.boundary_i);
                cimg.deriche(sigmay, params.orderY, 'y', (bool)params.boundary_i);
            }
            //}
#         else
            // VanVliet filter was inexistent before 1.53, and buggy before CImg.h from
            // 57ffb8393314e5102c00e5f9f8fa3dcace179608 Thu Dec 11 10:57:13 2014 +0100
            if (params.filter == eFilterGaussian) {
                vanvliet(cimg,/*cimg.vanvliet(*/sigmax, params.orderX, 'x', (bool)params.boundary_i);
                vanvliet(cimg,/*cimg.vanvliet(*/sigmay, params.orderY, 'y', (bool)params.boundary_i);
            } else {
                cimg.deriche(sigmax, params.orderX, 'x', (bool)params.boundary_i);
                cimg.deriche(sigmay, params.orderY, 'y', (bool)params.boundary_i);
            }
#         endif
        } else if (params.filter == eFilterBox || params.filter == eFilterTriangle || params.filter == eFilterQuadratic) {
            int iter = (params.filter == eFilterBox ? 1 :
                        (params.filter == eFilterTriangle ? 2 : 3));
            box(cimg, params.sizex, iter, params.orderX, 'x', (bool)params.boundary_i);
            box(cimg, params.sizey, iter, params.orderY, 'y', (bool)params.boundary_i);
        } else {
            assert(false);
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgBlurParams& params) OVERRIDE FINAL
    {
        if (params.filter == eFilterQuasiGaussian || params.filter == eFilterGaussian) {
            float sigmax = (float)(args.renderScale.x * params.sizex / 2.4);
            float sigmay = (float)(args.renderScale.y * params.sizey / 2.4);
            return (sigmax < 0.1 && sigmay < 0.1 && params.orderX == 0 && params.orderY == 0);
        } else if (params.filter == eFilterBox || params.filter == eFilterTriangle || params.filter == eFilterQuadratic) {
            return (args.renderScale.x * params.sizex <= 1 && args.renderScale.y * params.sizey <= 1 && params.orderX == 0 && params.orderY == 0);
        } else {
            assert(false);
        }
    };

    virtual bool getRoD(const OfxRectI& srcRoD, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* dstRoD) OVERRIDE FINAL;

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const CImgBlurParams& params)  OVERRIDE FINAL { return params.boundary_i; }

private:

    // params
    OFX::DoubleParam *_size;
    OFX::Double2DParam *_size2D;
    OFX::IntParam *_orderX;
    OFX::IntParam *_orderY;
    OFX::ChoiceParam *_boundary;
    OFX::ChoiceParam *_filter;
    OFX::BooleanParam *_expandRoD;
};

bool
CImgBlurPlugin::getRoD(const OfxRectI& srcRoD, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* dstRoD)
{
    if (params.expandRoD && !isEmpty(srcRoD)) {
        if (params.filter == eFilterQuasiGaussian || params.filter == eFilterGaussian) {
            int delta_pixX = std::max(3, (int)std::ceil((params.sizex * 1.5) * renderScale.x));
            int delta_pixY = std::max(3, (int)std::ceil((params.sizey * 1.5) * renderScale.y));
            dstRoD->x1 = srcRoD.x1 - delta_pixX - params.orderX;
            dstRoD->x2 = srcRoD.x2 + delta_pixX + params.orderX;
            dstRoD->y1 = srcRoD.y1 - delta_pixY - params.orderY;
            dstRoD->y2 = srcRoD.y2 + delta_pixY + params.orderY;
        } else if (params.filter == eFilterBox || params.filter == eFilterTriangle || params.filter == eFilterQuadratic) {
            int iter = (params.filter == eFilterBox ? 1 :
                        (params.filter == eFilterTriangle ? 2 : 3));
            int delta_pixX = iter * (std::floor((renderScale.x * params.sizex-1)/ 2) + 1);
            int delta_pixY = iter * (std::floor((renderScale.y * params.sizey-1)/ 2) + 1);
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


mDeclarePluginFactory(CImgBlurPluginFactory, {}, {});

void CImgBlurPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
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
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void CImgBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgBlurPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    if (getMajorVersion() <= 1) {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSize);
        param->setLabels(kParamSizeLabel, kParamSizeLabel, kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, 100);
        param->setDefault(kParamSizeDefault);
        param->setDigits(1);
        param->setIncrement(0.1);
        page->addChild(*param);
    } else {
        OFX::Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSize);
        param->setLabels(kParamSizeLabel, kParamSizeLabel, kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(0, 0, INT_MAX, INT_MAX);
        param->setDisplayRange(0, 0, 100, 100);
        param->setDefault(kParamSizeDefault, kParamSizeDefault);
        param->setDigits(1);
        param->setIncrement(0.1);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamOrderX);
        param->setLabels(kParamOrderXLabel, kParamOrderXLabel, kParamOrderXLabel);
        param->setHint(kParamOrderXHint);
        param->setRange(0, 2);
        param->setDisplayRange(0, 2);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamOrderY);
        param->setLabels(kParamOrderYLabel, kParamOrderYLabel, kParamOrderYLabel);
        param->setHint(kParamOrderYHint);
        param->setRange(0, 2);
        param->setDisplayRange(0, 2);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabels(kParamBoundaryLabel, kParamBoundaryLabel, kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet, kParamBoundaryOptionDirichletHint);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann, kParamBoundaryOptionNeumannHint);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic, kParamBoundaryOptionPeriodicHint);
        param->setDefault((int)kParamBoundaryDefault);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabels(kParamFilterLabel, kParamFilterLabel, kParamFilterLabel);
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
        param->setDefault((int)kParamFilterDefault);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabels(kParamExpandRoDLabel, kParamExpandRoDLabel, kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        page->addChild(*param);
    }

    CImgBlurPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgBlurPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle);
}


void getCImgBlurPluginID(OFX::PluginFactoryArray &ids)
{
    {
        // version 1
        static CImgBlurPluginFactory p(kPluginIdentifier, 1, 0);
        ids.push_back(&p);
    }
    {
        static CImgBlurPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
