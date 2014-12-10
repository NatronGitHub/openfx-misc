/*
 OFX CImgErodeSmooth plugin.

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

#include "CImgErodeSmooth.h"

#include <memory>
#include <cmath>
#include <cstring>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsMerging.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#define kPluginName          "ErodeSmoothCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Erode or dilate input stream using a normalize power-weighted filter.\n" \
"This gives a smoother result than the Erode or Dilate node.\n" \
"See \"Robust local max-min filters by normalized power-weighted filtering\" by L.J. van Vliet, " \
"http://dx.doi.org/10.1109/ICPR.2004.1334273\n" \
"Smoothing is done using a quasi-gaussian or gaussian filter (recursive implementation).\n" \
"Uses the 'blur' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgErodeSmooth"
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

#define kParamRange "range"
#define kParamRangeLabel "Range"
#define kParamRangeHint "Expected range for input values."

#define kParamSigma "sigma"
#define kParamSigmaLabel "Sigma"
#define kParamSigmaHint "Standard deviation of the smoothing filter, in pixel units. Negative values correspond to dilation, positive valies to erosion."
#define kParamSigmaDefault 1.0

#define kParamExponent "exponent"
#define kParamExponentLabel "Exponent"
#define kParamExponentHint "Exponent of the normalized power-weighted filter. Lower values give a smoother result. Default is 5."
#define kParamExponentDefault 5

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black"
#define kParamBoundaryOptionDirichletHint "Dirichlet boundary condition: pixel values out of the image domain are zero."
#define kParamBoundaryOptionNeumann "Nearest"
#define kParamBoundaryOptionNeumannHint "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain."
#define kParamBoundaryOptionPeriodic "Periodic"
#define kParamBoundaryOptionPeriodicHint "Image is considered to be periodic out of the image domain."
#define kParamBoundaryDefault eBoundaryNeumann
enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

#if cimg_version >= 153
#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "Bluring filter. The quasi-Gaussian filter should be appropriate in most cases. The Gaussian filter is more isotropic (its impulse response has rotational symmetry), but slower."
#define kParamFilterOptionQuasiGaussian "Quasi-Gaussian"
#define kParamFilterOptionQuasiGaussianHint "Quasi-Gaussian filter (0-order recursive Deriche filter, faster)."
#define kParamFilterOptionGaussian "Gaussian"
#define kParamFilterOptionGaussianHint "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower)."
#define kParamFilterDefault eFilterQuasiGaussian
enum FilterEnum
{
    eFilterQuasiGaussian = 0,
    eFilterGaussian,
};
#endif

using namespace OFX;

#define ERODESMOOTH_MIN 1.e-8 // minimum value for the weight
#define ERODESMOOTH_OFFSET 0.1 // offset to the image values to avoid divisions by zero

/// ErodeSmooth plugin
struct CImgErodeSmoothParams
{
    double min;
    double max;
    double sigma;
    int exponent;
    int boundary_i;
#if cimg_version >= 153
    int filter_i;
#endif
};

class CImgErodeSmoothPlugin : public CImgFilterPluginHelper<CImgErodeSmoothParams,false>
{
public:

    CImgErodeSmoothPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgErodeSmoothParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _range = fetchDouble2DParam(kParamRange);
        _sigma  = fetchDoubleParam(kParamSigma);
        _exponent  = fetchIntParam(kParamExponent);
        _boundary  = fetchChoiceParam(kParamBoundary);
#if cimg_version >= 153
        _filter = fetchChoiceParam(kParamFilter);
        assert(_range && _sigma && _boundary && _filter);
#else
        assert(_range && _sigma && _boundary);
#endif
    }

    virtual void getValuesAtTime(double time, CImgErodeSmoothParams& params) OVERRIDE FINAL
    {
        _range->getValueAtTime(time, params.min, params.max);
        _sigma->getValueAtTime(time, params.sigma);
        _exponent->getValueAtTime(time, params.exponent);
        _boundary->getValueAtTime(time, params.boundary_i);
#if cimg_version >= 153
        _filter->getValueAtTime(time, params.filter_i);
#endif
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgErodeSmoothParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = std::ceil((std::abs(params.sigma) * 6.) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgErodeSmoothParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        const double rmin = params.sigma < 0 ? params.min : params.max;
        const double rmax = params.sigma < 0 ? params.max : params.min;
        double s = std::abs(params.sigma) * args.renderScale.x;

        if (rmax == rmin) {
            return;
        }
        // scale to [0,1]
#ifdef cimg_use_openmp
#pragma omp parallel for if (denom.size()>=4096)
#endif
        cimg_rof(cimg,ptrd,float) *ptrd = (*ptrd-rmin)/(rmax-rmin) + ERODESMOOTH_OFFSET;

        // see "Robust local max-min filters by normalized power-weighted filtering" by L.J. van Vliet
        // http://dx.doi.org/10.1109/ICPR.2004.1334273
        // compute blur(x^(P+1))/blur(x^P)
        {
            cimg_library::CImg<float> denom(cimg, false);
            const double vmin = std::pow((double)ERODESMOOTH_MIN, (double)1./params.exponent);
            //printf("%g\n",vmin);
#ifdef cimg_use_openmp
#pragma omp parallel for if (denom.size()>=4096)
#endif
            cimg_rof(denom,ptrd,float) *ptrd = std::pow((double)((*ptrd<0.?0.:*ptrd)+vmin), params.exponent); // C++98 and C++11 both have std::pow(double,int)

            cimg.mul(denom);

#if cimg_version >= 153
            cimg.blur(s, (bool)params.boundary_i, (bool)params.filter_i);
            denom.blur(s, (bool)params.boundary_i, (bool)params.filter_i);
#else
            cimg.blur(s, (bool)params.boundary_i);
            denom.blur(s, (bool)params.boundary_i);
#endif
            assert(cimg.width() == denom.width() && cimg.height() == denom.height() && cimg.depth() == denom.depth() && cimg.spectrum() == denom.spectrum());
            cimg.div(denom);
        }

        // scale to [rmin,rmax]
#ifdef cimg_use_openmp
#pragma omp parallel for if (denom.size()>=4096)
#endif
        cimg_rof(cimg,ptrd,float) *ptrd = (*ptrd-ERODESMOOTH_OFFSET)*(rmax-rmin)+rmin;
}

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgErodeSmoothParams& params) OVERRIDE FINAL
    {
        return (params.sigma == 0. || params.exponent <= 0);
    };

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL
    {
        if (paramName == kParamRange && args.reason == eChangeUserEdit) {
            double rmin, rmax;
            _range->getValueAtTime(args.time, rmin, rmax);
            if (rmax < rmin) {
                _range->setValue(rmax, rmin);
            }
        } else {
            CImgFilterPluginHelper<CImgErodeSmoothParams,false>::changedParam(args, paramName);
        }
    }
private:

    // params
    OFX::Double2DParam *_range;
    OFX::DoubleParam *_sigma;
    OFX::IntParam *_exponent;
    OFX::ChoiceParam *_boundary;
#if cimg_version >= 153
    OFX::ChoiceParam *_filter;
#endif
};


mDeclarePluginFactory(CImgErodeSmoothPluginFactory, {}, {});

void CImgErodeSmoothPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgErodeSmoothPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgErodeSmoothPlugin::describeInContextBegin(desc, context,
                                                                                 kSupportsRGBA,
                                                                                 kSupportsRGB,
                                                                                 kSupportsAlpha,
                                                                                 kSupportsTiles);

    {
        Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamRange);
        param->setLabels(kParamRangeLabel, kParamRangeLabel, kParamRangeLabel);
        param->setDimensionLabels("min", "max");
        param->setHint(kParamRangeHint);
        param->setDefault(0., 1.);
        param->setAnimates(true);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabels(kParamSigmaLabel, kParamSigmaLabel, kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(-1000, 1000);
        param->setDisplayRange(-1, 1);
        param->setDefault(kParamSigmaDefault);
        param->setIncrement(0.005);
        param->setDigits(3);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamExponent);
        param->setLabels(kParamExponentLabel, kParamExponentLabel, kParamExponentLabel);
        param->setHint(kParamExponentHint);
        param->setRange(1, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamExponentDefault);
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
#if cimg_version >= 153
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
#endif

    CImgErodeSmoothPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgErodeSmoothPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgErodeSmoothPlugin(handle);
}


void getCImgErodeSmoothPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgErodeSmoothPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
