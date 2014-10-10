/*
 OFX CImgNoise plugin.

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
#include "ofxsMerging.h"
#include "ofxsCopier.h"

#define cimg_display 0
#include <CImg.h>

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

class CImgNoisePlugin : public CImgFilterPluginHelper<CImgNoiseParams>
{
public:

    CImgNoisePlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgNoiseParams>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
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
    virtual void getRoI(const OfxRectI rect, const OfxPointD& renderScale, const CImgNoiseParams& params, OfxRectI* roi) OVERRIDE FINAL
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

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgNoiseParams& params) OVERRIDE FINAL
    {
        return (params.sigma == 0.);
    };

private:

    // params
    OFX::DoubleParam *_sigma;
    OFX::ChoiceParam *_type;
};


mDeclarePluginFactory(CImgNoisePluginFactory, {}, {});

void CImgNoisePluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgNoisePluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgNoisePlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabels(kParamSigmaLabel, kParamSigmaLabel, kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0., 10.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.005);
        param->setDefault(kParamSigmaDefault);
        page->addChild(*param);
    }

    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamType);
        param->setLabels(kParamTypeLabel, kParamTypeLabel, kParamTypeLabel);
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
        page->addChild(*param);
    }

    CImgNoisePlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgNoisePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CImgNoisePlugin(handle);
}


void getCImgNoisePluginID(OFX::PluginFactoryArray &ids)
{
    static CImgNoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
