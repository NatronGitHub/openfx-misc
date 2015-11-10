/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX CImgNoise plugin.
 */

#include "CImgNoise.h"

#include <memory>
#include <cmath>
#include <cstring>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"

#include "CImgFilter.h"

#define kPluginName          "NoiseCImg"
#define kPluginGrouping      "Draw"
#define kPluginDescription \
"Add random noise to input stream.\n" \
"Note that each render gives a different noise.\n" \
"Uses the 'noise' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgNoise"
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

#define kParamSigma "sigma"
#define kParamSigmaLabel "Sigma"
#define kParamSigmaHint "Amplitude of the random additive noise."
#define kParamSigmaDefault 0.01

#define kParamType "type"
#define kParamTypeLabel "Type"
#define kParamTypeHint "Type of additive noise."
#define kParamTypeOptionGaussian "Gaussian"
#define kParamTypeOptionGaussianHint "Gaussian noise."
#define kParamTypeOptionUniform "Uniform"
#define kParamTypeOptionUniformHint "Uniform noise."
#define kParamTypeOptionSaltPepper "Salt & Pepper"
#define kParamTypeOptionSaltPepperHint "Salt & pepper noise."
#define kParamTypeOptionPoisson "Poisson"
#define kParamTypeOptionPoissonHint "Poisson noise. Image is divided by Sigma before computing noise, then remultiplied by Sigma."
#define kParamTypeOptionRice "Rice"
#define kParamTypeOptionRiceHint "Rician noise."
#define kParamTypeDefault eTypeGaussian
enum TypeEnum
{
    eTypeGaussian = 0,
    eTypeUniform,
    eTypeSaltPepper,
    eTypePoisson,
    eTypeRice,
};


using namespace OFX;

/// Noise plugin
struct CImgNoiseParams
{
    double sigma;
    int type_i;
};

class CImgNoisePlugin : public CImgFilterPluginHelper<CImgNoiseParams,true>
{
public:

    CImgNoisePlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgNoiseParams,true>(handle, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/false, /*defaultProcessAlphaOnRGBA=*/false)
    {
        _sigma  = fetchDoubleParam(kParamSigma);
        _type = fetchChoiceParam(kParamType);
        assert(_sigma && _type);
    }

    virtual void getValuesAtTime(double time, CImgNoiseParams& params) OVERRIDE FINAL
    {
        _sigma->getValueAtTime(time, params.sigma);
        _type->getValueAtTime(time, params.type_i);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& /*renderScale*/, const CImgNoiseParams& /*params*/, OfxRectI* roi) OVERRIDE FINAL
    {
        roi->x1 = rect.x1;
        roi->x2 = rect.x2;
        roi->y1 = rect.y1;
        roi->y2 = rect.y2;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgNoiseParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        // the noise vs. scale dependency formula is only valid for Gaussian noise
        if (params.type_i == eTypePoisson) {
            cimg /= params.sigma;
        }
        cimg.noise(params.sigma * std::sqrt(args.renderScale.x), params.type_i);
        if (params.type_i == eTypePoisson) {
            cimg *= params.sigma;
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgNoiseParams& params) OVERRIDE FINAL
    {
        return (params.sigma == 0.);
    };

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        clipPreferences.setOutputFrameVarying(true);
    }

private:

    // params
    OFX::DoubleParam *_sigma;
    OFX::ChoiceParam *_type;
};


mDeclarePluginFactory(CImgNoisePluginFactory, {}, {});

void CImgNoisePluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgNoisePluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgNoisePlugin::describeInContextBegin(desc, context,
                                                                             kSupportsRGBA,
                                                                             kSupportsRGB,
                                                                             kSupportsXY,
                                                                             kSupportsAlpha,
                                                                             kSupportsTiles,
                                                                             /*processRGB=*/true,
                                                                             /*processAlpha*/false,
                                                                             /*processIsSecret=*/false);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabel(kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0., 10.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.005);
        param->setDefault(kParamSigmaDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamType);
        param->setLabel(kParamTypeLabel);
        param->setHint(kParamTypeHint);
        assert(param->getNOptions() == eTypeGaussian && param->getNOptions() == 0);
        param->appendOption(kParamTypeOptionGaussian, kParamTypeOptionGaussianHint);
        assert(param->getNOptions() == eTypeUniform && param->getNOptions() == 1);
        param->appendOption(kParamTypeOptionUniform, kParamTypeOptionUniformHint);
        assert(param->getNOptions() == eTypeSaltPepper && param->getNOptions() == 2);
        param->appendOption(kParamTypeOptionSaltPepper, kParamTypeOptionSaltPepperHint);
        assert(param->getNOptions() == eTypePoisson && param->getNOptions() == 3);
        param->appendOption(kParamTypeOptionPoisson, kParamTypeOptionPoissonHint);
        assert(param->getNOptions() == eTypeRice && param->getNOptions() == 4);
        param->appendOption(kParamTypeOptionRice, kParamTypeOptionRiceHint);
        param->setDefault((int)kParamTypeDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgNoisePlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgNoisePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgNoisePlugin(handle);
}


void getCImgNoisePluginID(OFX::PluginFactoryArray &ids)
{
    static CImgNoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
