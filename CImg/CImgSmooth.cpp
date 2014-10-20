/*
 OFX CImgSmooth plugin.

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

#include "CImgSmooth.h"

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

#define kPluginName          "SmoothCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Smooth/Denoise input stream using anisotropic PDE-based smoothing.\n" \
"Uses the 'blur_anisotropic' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgSmooth"
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

#define kParamAmplitude "amplitude"
#define kParamAmplitudeLabel "Amplitude"
#define kParamAmplitudeHint "Amplitude of the smoothing, in pixel units (>=0). This is the maximum length of streamlines used to smooth the data."
#define kParamAmplitudeDefault 60.0

#define kParamSharpness "sharpness"
#define kParamSharpnessLabel "Sharpness"
#define kParamSharpnessHint "Contour preservation (>=0)"
#define kParamSharpnessDefault 0.7

#define kParamAnisotropy "anisotropy"
#define kParamAnisotropyLabel "Anisotropy"
#define kParamAnisotropyHint "Smoothing anisotropy (0<=a<=1)"
#define kParamAnisotropyDefault 0.3

#define kParamAlpha "alpha"
#define kParamAlphaLabel "Alpha"
#define kParamAlphaHint "Noise scale, in pixels units (>=0)"
#define kParamAlphaDefault 0.6

#define kParamSigma "sigma"
#define kParamSigmaLabel "Sigma"
#define kParamSigmaHint "Geometry regularity, in pixels units (>=0)"
#define kParamSigmaDefault 1.1

#define kParamDl "dl"
#define kParamDlLabel "dl"
#define kParamDlHint "Spatial discretization, in pixel units (0<=dl<=1)"
#define kParamDlDefault 0.8

#define kParamDa "da"
#define kParamDaLabel "da"
#define kParamDaHint "Angular integration step, in degrees (0<=da<=90). If da=0, Iterated oriented Laplacians is used instead of LIC-based smoothing."
#define kParamDaDefault 30.0

#define kParamGaussPrec "prec"
#define kParamGaussPrecLabel "Precision"
#define kParamGaussPrecHint "Precision of the diffusion process (>0)."
#define kParamGaussPrecDefault 2.0

#define kParamInterp "interpolation"
#define kParamInterpLabel "Interpolation"
#define kParamInterpHint "Interpolation type"
#define kParamInterpOptionNearest "Nearest-neighbor"
#define kParamInterpOptionNearestHint "Nearest-neighbor."
#define kParamInterpOptionLinear "Linear"
#define kParamInterpOptionLinearHint "Linear interpolation."
#define kParamInterpOptionRungeKutta "Runge-Kutta"
#define kParamInterpOptionRungeKuttaHint "Runge-Kutta interpolation."
#define kParamInterpDefault eInterpNearest
enum InterpEnum
{
    eInterpNearest = 0,
    eInterpLinear,
    eInterpRungeKutta,
};

#define kParamFastApprox "is_fast_approximation"
#define kParamFastApproxLabel "fast Approximation"
#define kParamFastApproxHint "Tells if a fast approximation of the gaussian function is used or not"
#define kParamFastApproxDafault true

using namespace OFX;




/// Smooth plugin
struct CImgSmoothParams
{
    double amplitude;
    double sharpness;
    double anisotropy;
    double alpha;
    double sigma;
    double dl;
    double da;
    double gprec;
    int interp_i;
    //InterpEnum interp;
    bool fast_approx;
};

class CImgSmoothPlugin : public CImgFilterPluginHelper<CImgSmoothParams>
{
public:

    CImgSmoothPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgSmoothParams>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _amplitude  = fetchDoubleParam(kParamAmplitude);
        _sharpness  = fetchDoubleParam(kParamSharpness);
        _anisotropy = fetchDoubleParam(kParamAnisotropy);
        _alpha      = fetchDoubleParam(kParamAlpha);
        _sigma      = fetchDoubleParam(kParamSigma);
        _dl         = fetchDoubleParam(kParamDl);
        _da         = fetchDoubleParam(kParamDa);
        _gprec      = fetchDoubleParam(kParamGaussPrec);
        _interp     = fetchChoiceParam(kParamInterp);
        _fast_approx = fetchBooleanParam(kParamFastApprox);
        assert(_amplitude && _sharpness && _anisotropy && _alpha && _sigma && _dl && _da && _gprec && _interp && _fast_approx);
    }

    virtual void getValuesAtTime(double time, CImgSmoothParams& params) OVERRIDE FINAL
    {
        _amplitude->getValueAtTime(time, params.amplitude);
        _sharpness->getValueAtTime(time, params.sharpness);
        _anisotropy->getValueAtTime(time, params.anisotropy);
        _alpha->getValueAtTime(time, params.alpha);
        _sigma->getValueAtTime(time, params.sigma);
        _dl->getValueAtTime(time, params.dl);
        _da->getValueAtTime(time, params.da);
        _gprec->getValueAtTime(time, params.gprec);
        _interp->getValueAtTime(time, params.interp_i);
        _fast_approx->getValueAtTime(time, params.fast_approx);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgSmoothParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = std::ceil((params.amplitude + params.alpha + params.sigma) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgSmoothParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        cimg.blur_anisotropic(params.amplitude * args.renderScale.x, // in pixels
                              params.sharpness,
                              params.anisotropy,
                              params.alpha * args.renderScale.x, // in pixels
                              params.sigma * args.renderScale.x, // in pixels
                              params.dl, // in pixel, but we don't discretize more
                              params.da,
                              params.gprec,
                              params.interp_i,
                              params.fast_approx);

    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgSmoothParams& params) OVERRIDE FINAL
    {
        return (params.amplitude <= 0. || params.dl < 0.);
    };

private:

    // params
    OFX::DoubleParam *_amplitude;
    OFX::DoubleParam *_sharpness;
    OFX::DoubleParam *_anisotropy;
    OFX::DoubleParam *_alpha;
    OFX::DoubleParam *_sigma;
    OFX::DoubleParam *_dl;
    OFX::DoubleParam *_da;
    OFX::DoubleParam *_gprec;
    OFX::ChoiceParam *_interp;
    OFX::BooleanParam *_fast_approx;
};


mDeclarePluginFactory(CImgSmoothPluginFactory, {}, {});

void CImgSmoothPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgSmoothPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgSmoothPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmplitude);
        param->setLabels(kParamAmplitudeLabel, kParamAmplitudeLabel, kParamAmplitudeLabel);
        param->setHint(kParamAmplitudeHint);
        param->setRange(0., 1000.);
        param->setDisplayRange(0., 100.);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(1);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSharpness);
        param->setLabels(kParamSharpnessLabel, kParamSharpnessLabel, kParamSharpnessLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamSharpnessDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAnisotropy);
        param->setLabels(kParamAnisotropyLabel, kParamAnisotropyLabel, kParamAnisotropyLabel);
        param->setHint(kParamAnisotropyHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamAnisotropyDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
        param->setLabels(kParamAlphaLabel, kParamAlphaLabel, kParamAlphaLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamAlphaDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabels(kParamSigmaLabel, kParamSigmaLabel, kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0., 3.);
        param->setDisplayRange(0., 3.);
        param->setDefault(kParamSigmaDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDl);
        param->setLabels(kParamDlLabel, kParamDlLabel, kParamDlLabel);
        param->setHint(kParamDlHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamDlDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDa);
        param->setLabels(kParamDaLabel, kParamDaLabel, kParamDaLabel);
        param->setHint(kParamDaHint);
        param->setRange(0., 90.);
        param->setDisplayRange(0., 90.);
        param->setDefault(kParamDaDefault);
        param->setIncrement(0.5);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGaussPrec);
        param->setLabels(kParamGaussPrecLabel, kParamGaussPrecLabel, kParamGaussPrecLabel);
        param->setHint(kParamGaussPrecHint);
        param->setRange(0., 5.);
        param->setDisplayRange(0., 5.);
        param->setDefault(kParamGaussPrecDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInterp);
        param->setLabels(kParamInterpLabel, kParamInterpLabel, kParamInterpLabel);
        param->setHint(kParamInterpHint);
        assert(param->getNOptions() == eInterpNearest && param->getNOptions() == 0);
        param->appendOption(kParamInterpOptionNearest, kParamInterpOptionNearestHint);
        assert(param->getNOptions() == eInterpLinear && param->getNOptions() == 1);
        param->appendOption(kParamInterpOptionLinear, kParamInterpOptionLinearHint);
        assert(param->getNOptions() == eInterpRungeKutta && param->getNOptions() == 2);
        param->appendOption(kParamInterpOptionRungeKutta, kParamInterpOptionRungeKuttaHint);
        param->setDefault((int)kParamInterpDefault);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFastApprox);
        param->setLabels(kParamFastApproxLabel, kParamFastApproxLabel, kParamFastApproxLabel);
        param->setHint(kParamFastApproxHint);
        param->setDefault(kParamFastApproxDafault);
        page->addChild(*param);
    }

    CImgSmoothPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgSmoothPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CImgSmoothPlugin(handle);
}


void getCImgSmoothPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgSmoothPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
