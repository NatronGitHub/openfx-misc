/*
 OFX CImgSharpenShock plugin.

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


#include "CImgSharpenShock.h"

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

#define kPluginName          "SharpenShockCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Sharpen selected images by shock filters.\n" \
"Uses 'sharpen' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgSharpenShock"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0 // a maximum computation is done in sharpen, tiling is theoretically not possible (although gmicol uses a 24 pixel overlap)
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamAmplitude "amplitude"
#define kParamAmplitudeLabel "Amplitude"
#define kParamAmplitudeHint "Standard deviation of the spatial kernel, in pixel units (>=0). Details smaller than this size are filtered out."
#define kParamAmplitudeDefault 0.6 // 150.0/255

#define kParamEdgeThreshold "edgeThreshold"
#define kParamEdgeThresholdLabel "Edge Threshold"
#define kParamEdgeThresholdHint "Edge threshold."
#define kParamEdgeThresholdDefault 0.1

#define kParamGradientSmoothness "alpha"
#define kParamGradientSmoothnessLabel "Gradient Smoothness"
#define kParamGradientSmoothnessHint "Gradient smoothness (in pixels)."
#define kParamGradientSmoothnessDefault 0.8

#define kParamTensorSmoothness "alpha"
#define kParamTensorSmoothnessLabel "Tensor Smoothness"
#define kParamTensorSmoothnessHint "Tensor smoothness (in pixels)."
#define kParamTensorSmoothnessDefault 1.1

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations. A reasonable value is 1."
#define kParamIterationsDefault 1

using namespace OFX;

/// SharpenShock plugin
struct CImgSharpenShockParams
{
    double amplitude;
    double edge;
    double alpha;
    double sigma;
    int iterations;
};

class CImgSharpenShockPlugin : public CImgFilterPluginHelper<CImgSharpenShockParams,false>
{
public:

    CImgSharpenShockPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgSharpenShockParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _amplitude  = fetchDoubleParam(kParamAmplitude);
        _edge  = fetchDoubleParam(kParamEdgeThreshold);
        _alpha  = fetchDoubleParam(kParamGradientSmoothness);
        _sigma  = fetchDoubleParam(kParamTensorSmoothness);
        _iterations = fetchIntParam(kParamIterations);
        assert(_amplitude && _edge && _alpha && _sigma && _iterations);
    }

    virtual void getValuesAtTime(double time, CImgSharpenShockParams& params) OVERRIDE FINAL
    {
        _amplitude->getValueAtTime(time, params.amplitude);
        _edge->getValueAtTime(time, params.edge);
        _alpha->getValueAtTime(time, params.alpha);
        _sigma->getValueAtTime(time, params.sigma);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgSharpenShockParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 24; // overlap is 24 in gmicol
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgSharpenShockParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.iterations <= 0 || params.amplitude == 0.) {
            return;
        }
        float alpha = args.renderScale.x * params.alpha;
        float sigma = args.renderScale.x * params.sigma;
        for (int i = 1; i < params.iterations; ++i) {
            if (abort()) {
                return;
            }
            cimg.sharpen(params.amplitude, true, params.edge, alpha, sigma);
        }
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgSharpenShockParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0 || params.amplitude == 0.);
    };

private:

    // params
    OFX::DoubleParam *_amplitude;
    OFX::DoubleParam *_edge;
    OFX::DoubleParam *_alpha;
    OFX::DoubleParam *_sigma;
    OFX::IntParam *_iterations;
};


mDeclarePluginFactory(CImgSharpenShockPluginFactory, {}, {});

void CImgSharpenShockPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgSharpenShockPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgSharpenShockPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmplitude);
        param->setLabels(kParamAmplitudeLabel, kParamAmplitudeLabel, kParamAmplitudeLabel);
        param->setHint(kParamAmplitudeHint);
        param->setDisplayRange(0, 1.5 /*400/255*/);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(0.01);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEdgeThreshold);
        param->setLabels(kParamEdgeThresholdLabel, kParamEdgeThresholdLabel, kParamEdgeThresholdLabel);
        param->setHint(kParamEdgeThresholdHint);
        param->setDisplayRange(0, 0.7);
        param->setDefault(kParamEdgeThresholdDefault);
        param->setIncrement(0.01);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGradientSmoothness);
        param->setLabels(kParamGradientSmoothnessLabel, kParamGradientSmoothnessLabel, kParamGradientSmoothnessLabel);
        param->setHint(kParamGradientSmoothnessHint);
        param->setDisplayRange(0, 10.);
        param->setDefault(kParamGradientSmoothnessDefault);
        param->setIncrement(0.01);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamTensorSmoothness);
        param->setLabels(kParamTensorSmoothnessLabel, kParamTensorSmoothnessLabel, kParamTensorSmoothnessLabel);
        param->setHint(kParamTensorSmoothnessHint);
        param->setDisplayRange(0, 10.);
        param->setDefault(kParamTensorSmoothnessDefault);
        param->setIncrement(0.01);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamIterations);
        param->setLabels(kParamIterationsLabel, kParamIterationsLabel, kParamIterationsLabel);
        param->setHint(kParamIterationsHint);
        param->setRange(0, 10);
        param->setDisplayRange(0, 10);
        param->setDefault(kParamIterationsDefault);
        page->addChild(*param);
    }

    CImgSharpenShockPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgSharpenShockPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgSharpenShockPlugin(handle);
}


void getCImgSharpenShockPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgSharpenShockPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

