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
 * OFX CImgSmooth plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
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

#define kPluginName          "SmoothAnisotropicCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Smooth/Denoise input stream using anisotropic PDE-based smoothing.\n" \
    "Uses the 'blur_anisotropic' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgSmooth"
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
#if cimg_use_openmp!=0
#define kHostFrameThreading false
#else
#define kHostFrameThreading true
#endif
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY true
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
#define kParamAlphaLabel "Gradient Smoothness"
#define kParamAlphaHint "Noise scale, in pixels units (>=0)"
#define kParamAlphaDefault 0.6

#define kParamSigma "sigma"
#define kParamSigmaLabel "Tensor Smoothness"
#define kParamSigmaHint "Geometry regularity, in pixels units (>=0)"
#define kParamSigmaDefault 1.1

#define kParamDl "dl"
#define kParamDlLabel "Spatial Precision"
#define kParamDlHint "Spatial discretization, in pixel units (0<=dl<=1)"
#define kParamDlDefault 0.8

#define kParamDa "da"
#define kParamDaLabel "Angular Precision"
#define kParamDaHint "Angular integration step, in degrees (0<=da<=90). If da=0, Iterated oriented Laplacians is used instead of LIC-based smoothing."
#define kParamDaDefault 30.0

#define kParamGaussPrec "prec"
#define kParamGaussPrecLabel "Value Precision"
#define kParamGaussPrecHint "Precision of the diffusion process (>0)."
#define kParamGaussPrecDefault 2.0

#define kParamInterp "interpolation"
#define kParamInterpLabel "Interpolation"
#define kParamInterpHint "Interpolation type"
#define kParamInterpOptionNearest "Nearest-neighbor", "Nearest-neighbor.", "nearest"
#define kParamInterpOptionLinear "Linear", "Linear interpolation.", "linear"
#define kParamInterpOptionRungeKutta "Runge-Kutta", "Runge-Kutta interpolation.", "rungekutta"
#define kParamInterpDefault eInterpNearest
enum InterpEnum
{
    eInterpNearest = 0,
    eInterpLinear,
    eInterpRungeKutta,
};

#define kParamFastApprox "is_fast_approximation"
#define kParamFastApproxLabel "Fast Approximation"
#define kParamFastApproxHint "Tells if a fast approximation of the gaussian function is used or not"
#define kParamFastApproxDefault true

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations."
#define kParamIterationsDefault 1

#define kParamThinBrush "thinBrush"
#define kParamThinBrushLabel "Set Thin Brush Defaults"
#define kParamThinBrushHint "Set the defaults to the value of the Thin Brush filter by PhotoComiX, as featured in the G'MIC Gimp plugin."

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
    int iterations;
};

class CImgSmoothPlugin
    : public CImgFilterPluginHelper<CImgSmoothParams, false>
{
public:

    CImgSmoothPlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgSmoothParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
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
        _iterations = fetchIntParam(kParamIterations);
        assert(_amplitude && _sharpness && _anisotropy && _alpha && _sigma && _dl && _da && _gprec && _interp && _fast_approx && _iterations);
    }

    virtual void getValuesAtTime(double time,
                                 CImgSmoothParams& params) OVERRIDE FINAL
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
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgSmoothParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil( (params.amplitude + params.alpha + params.sigma) * renderScale.x * params.iterations );

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments &args,
                        const CImgSmoothParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if ( (params.iterations <= 0) || (params.amplitude <= 0.) || (params.dl < 0.) || cimg.is_empty() ) {
            return;
        }
        for (int i = 0; i < params.iterations; ++i) {
            if ( abort() ) {
                return;
            }
            cimg.blur_anisotropic( (float)(params.amplitude * args.renderScale.x), // in pixels
                                   (float)params.sharpness,
                                   (float)params.anisotropy,
                                   (float)(params.alpha * args.renderScale.x), // in pixels
                                   (float)(params.sigma * args.renderScale.x), // in pixels
                                   (float)params.dl, // in pixel, but we don't discretize more
                                   (float)params.da,
                                   (float)params.gprec,
                                   params.interp_i,
                                   params.fast_approx );
        }
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgSmoothParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0) || (params.amplitude <= 0.) || (params.dl < 0.);
    };
    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        if ( (paramName == kParamThinBrush) ) {
            _amplitude->resetToDefault();
            _sharpness->setValue(0.9);
            _anisotropy->setValue(0.64);
            _alpha->setValue(3.1);
            _sigma->resetToDefault();
            _dl->resetToDefault();
            _da->resetToDefault();
            _gprec->resetToDefault();
            _interp->resetToDefault();
            _fast_approx->resetToDefault();
            _iterations->resetToDefault();
        } else {
            CImgFilterPluginHelper<CImgSmoothParams, false>::changedParam(args, paramName);
        }
    }

private:

    // params
    DoubleParam *_amplitude;
    DoubleParam *_sharpness;
    DoubleParam *_anisotropy;
    DoubleParam *_alpha;
    DoubleParam *_sigma;
    DoubleParam *_dl;
    DoubleParam *_da;
    DoubleParam *_gprec;
    ChoiceParam *_interp;
    BooleanParam *_fast_approx;
    IntParam *_iterations;
};


mDeclarePluginFactory(CImgSmoothPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgSmoothPluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgSmoothPluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgSmoothPlugin::describeInContextBegin(desc, context,
                                                                         kSupportsRGBA,
                                                                         kSupportsRGB,
                                                                         kSupportsXY,
                                                                         kSupportsAlpha,
                                                                         kSupportsTiles,
                                                                         /*processRGB=*/ true,
                                                                         /*processAlpha*/ false,
                                                                         /*processIsSecret=*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmplitude);
        param->setLabel(kParamAmplitudeLabel);
        param->setHint(kParamAmplitudeHint);
        param->setRange(0., 1000.);
        param->setDisplayRange(0., 100.);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSharpness);
        param->setLabel(kParamSharpnessLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamSharpnessDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAnisotropy);
        param->setLabel(kParamAnisotropyLabel);
        param->setHint(kParamAnisotropyHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamAnisotropyDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
        param->setLabel(kParamAlphaLabel);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamAlphaDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabel(kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0., 3.);
        param->setDisplayRange(0., 3.);
        param->setDefault(kParamSigmaDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDl);
        param->setLabel(kParamDlLabel);
        param->setHint(kParamDlHint);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamDlDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDa);
        param->setLabel(kParamDaLabel);
        param->setHint(kParamDaHint);
        param->setRange(0., 90.);
        param->setDisplayRange(0., 90.);
        param->setDefault(kParamDaDefault);
        param->setIncrement(0.5);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGaussPrec);
        param->setLabel(kParamGaussPrecLabel);
        param->setHint(kParamGaussPrecHint);
        param->setRange(0., 5.);
        param->setDisplayRange(0., 5.);
        param->setDefault(kParamGaussPrecDefault);
        param->setIncrement(0.05);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInterp);
        param->setLabel(kParamInterpLabel);
        param->setHint(kParamInterpHint);
        assert(param->getNOptions() == eInterpNearest && param->getNOptions() == 0);
        param->appendOption(kParamInterpOptionNearest);
        assert(param->getNOptions() == eInterpLinear && param->getNOptions() == 1);
        param->appendOption(kParamInterpOptionLinear);
        assert(param->getNOptions() == eInterpRungeKutta && param->getNOptions() == 2);
        param->appendOption(kParamInterpOptionRungeKutta);
        param->setDefault( (int)kParamInterpDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFastApprox);
        param->setLabel(kParamFastApproxLabel);
        param->setHint(kParamFastApproxHint);
        param->setDefault(kParamFastApproxDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIterations);
        param->setLabel(kParamIterationsLabel);
        param->setHint(kParamIterationsHint);
        param->setRange(0, 10);
        param->setDisplayRange(0, 10);
        param->setDefault(kParamIterationsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamThinBrush);
        param->setLabel(kParamThinBrushLabel);
        param->setHint(kParamThinBrushHint);
    }

    CImgSmoothPlugin::describeInContextEnd(desc, context, page);
} // CImgSmoothPluginFactory::describeInContext

ImageEffect*
CImgSmoothPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgSmoothPlugin(handle);
}

static CImgSmoothPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
