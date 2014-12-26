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

// TODO: fix Gaussian filter in CImg when Border= nearest and no expand RoD
// TODO: pass the border conditions to the copy processors in CImgFilter

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
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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
#define kParamFilterOptionQuasiGaussianHint "Quasi-Gaussian filter (0-order recursive Deriche filter, faster)."
#define kParamFilterOptionGaussian "Gaussian"
#define kParamFilterOptionGaussianHint "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower)."
#define kParamFilterDefault eFilterGaussian
enum FilterEnum
{
    eFilterQuasiGaussian = 0,
    eFilterGaussian,
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

using namespace OFX;

/// Blur plugin
struct CImgBlurParams
{
    double size;
    int orderX;
    int orderY;
    int boundary_i;
    int filter_i;
    bool expandRoD;
};

class CImgBlurPlugin : public CImgFilterPluginHelper<CImgBlurParams,false>
{
public:

    CImgBlurPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgBlurParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, kDefaultUnpremult, kDefaultProcessAlphaOnRGBA)
    {
        _size  = fetchDoubleParam(kParamSize);
        _orderX = fetchIntParam(kParamOrderX);
        _orderY = fetchIntParam(kParamOrderY);
        _boundary  = fetchChoiceParam(kParamBoundary);
        assert(_size && _orderX && _orderY && _boundary);
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_expandRoD);
    }

    virtual void getValuesAtTime(double time, CImgBlurParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.size);
        _orderX->getValueAtTime(time, params.orderX);
        _orderY->getValueAtTime(time, params.orderY);
        _boundary->getValueAtTime(time, params.boundary_i);
        _filter->getValueAtTime(time, params.filter_i);
        _expandRoD->getValueAtTime(time, params.expandRoD);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = std::ceil((params.size * 1.5) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgBlurParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        float sigma = args.renderScale.x * params.size / 2.4;
        if (sigma <= 0.5 && params.orderX == 0 && params.orderY == 0) {
            return;
        }
#if cimg_version >= 160
        if (params.orderX == 0 && params.orderY == 0) {
            cimg.blur(sigma, (bool)params.boundary_i, (bool)params.filter_i);
        } else {
            if ((bool)params.filter_i) {
                cimg.vanvliet(sigma, params.orderX, 'x', (bool)params.boundary_i);
                cimg.vanvliet(sigma, params.orderY, 'y', (bool)params.boundary_i);
            } else {
                cimg.deriche(sigma, params.orderX, 'x', (bool)params.boundary_i);
                cimg.deriche(sigma, params.orderY, 'y', (bool)params.boundary_i);
            }
        }
#else
        // VanVliet filter was inexistent before 1.53, and buggy before CImg.h from
        // 57ffb8393314e5102c00e5f9f8fa3dcace179608 Thu Dec 11 10:57:13 2014 +0100
        if ((bool)params.filter_i) {
            vanvliet(cimg,/*cimg.vanvliet(*/sigma, params.orderX, 'x', (bool)params.boundary_i);
            vanvliet(cimg,/*cimg.vanvliet(*/sigma, params.orderY, 'y', (bool)params.boundary_i);
        } else {
            cimg.deriche(sigma, params.orderX, 'x', (bool)params.boundary_i);
            cimg.deriche(sigma, params.orderY, 'y', (bool)params.boundary_i);
        }
#endif
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgBlurParams& params) OVERRIDE FINAL
    {
        float sigma = args.renderScale.x * params.size / 2.4;
        return (sigma < 0.1 && params.orderX == 0 && params.orderY == 0);
    };

    virtual bool getRoD(const OfxRectI& srcRoD, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* dstRoD) OVERRIDE FINAL;

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const CImgBlurParams& params)  OVERRIDE FINAL { return params.boundary_i; }

private:

    // params
    OFX::DoubleParam *_size;
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
        int delta_pix = std::ceil((params.size * 1.5) * renderScale.x);
        dstRoD->x1 = srcRoD.x1 - delta_pix;
        dstRoD->x2 = srcRoD.x2 + delta_pix;
        dstRoD->y1 = srcRoD.y1 - delta_pix;
        dstRoD->y2 = srcRoD.y2 + delta_pix;

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

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSize);
        param->setLabels(kParamSizeLabel, kParamSizeLabel, kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, 100);
        param->setDefault(kParamSizeDefault);
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
    static CImgBlurPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
