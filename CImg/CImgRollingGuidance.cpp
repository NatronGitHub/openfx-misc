/*
 OFX CImgRollingGuidance plugin.

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


#include "CImgRollingGuidance.h"

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

#if cimg_version < 161
#error "This plugin requires CImg 1.6.1, please upgrade CImg."
#endif

#define kPluginName          "RollingGuidanceCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Filter out details under a given scale using the Rolling Guidance filter.\n" \
"Rolling Guidance is described fully in http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/\n" \
"Iterates the 'blur_bilateral' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgRollingGuidance"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0 // The Rolling Guidance filter gives a global result, tiling is impossible
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamSigmaS "sigma_s"
#define kParamSigmaSLabel "Sigma_s"
#define kParamSigmaSHint "Standard deviation of the spatial kernel, in pixel units (>=0). Details smaller than this size are filtered out."
#define kParamSigmaSDefault 10.0

#define kParamSigmaR "sigma_r"
#define kParamSigmaRLabel "Sigma_r"
#define kParamSigmaRHint "Standard deviation of the range kernel, in intensity units (>=0). A reasonable value is 1/10 of the intensity range."
#define kParamSigmaRDefault 0.1

#define kParamIterations "iterations"
#define kParamIterationsLabel "Iterations"
#define kParamIterationsHint "Number of iterations of the rolling guidance filter. 1 corresponds to Gaussian smoothing. A reasonable value is 4."
#define kParamIterationsDefault 4

using namespace OFX;

/// RollingGuidance plugin
struct CImgRollingGuidanceParams
{
    double sigma_s;
    double sigma_r;
    int iterations;
};

class CImgRollingGuidancePlugin : public CImgFilterPluginHelper<CImgRollingGuidanceParams,false>
{
public:

    CImgRollingGuidancePlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgRollingGuidanceParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _sigma_s  = fetchDoubleParam(kParamSigmaS);
        _sigma_r  = fetchDoubleParam(kParamSigmaR);
        _iterations = fetchIntParam(kParamIterations);
        assert(_sigma_s && _sigma_r && _iterations);
    }

    virtual void getValuesAtTime(double time, CImgRollingGuidanceParams& params) OVERRIDE FINAL
    {
        _sigma_s->getValueAtTime(time, params.sigma_s);
        _sigma_r->getValueAtTime(time, params.sigma_r);
        _iterations->getValueAtTime(time, params.iterations);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgRollingGuidanceParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil((params.sigma_s * 4.) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgRollingGuidanceParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.iterations <= 0 || params.sigma_s == 0.) {
            return;
        }
        // for a full description of the Rolling Guidance filter, see
        // http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/paper/%5BECCV2014%5DRollingGuidanceFilter_5M.pdf
        // http://www.cse.cuhk.edu.hk/~leojia/projects/rollguidance/
        if (params.iterations == 1) {
            // Gaussian filter
            cimg.blur((float)(params.sigma_s * args.renderScale.x), true, true);
            return;
        }
        // first iteration is Gaussian blur (equivalent to a bilateral filter with a constant image as the guide)
        cimg_library::CImg<float> guide = cimg.get_blur((float)(params.sigma_s * args.renderScale.x), true, true);
        // next iterations use the bilateral filter
        for (int i = 1; i < params.iterations; ++i) {
            if (abort()) {
                return;
            }
            // filter the original image using the updated guide
            guide = cimg.get_blur_bilateral(guide, (float)(params.sigma_s * args.renderScale.x), (float)params.sigma_r);
        }
        cimg = guide;
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgRollingGuidanceParams& params) OVERRIDE FINAL
    {
        return (params.iterations <= 0 || params.sigma_s == 0.);
    };

private:

    // params
    OFX::DoubleParam *_sigma_s;
    OFX::DoubleParam *_sigma_r;
    OFX::IntParam *_iterations;
};


mDeclarePluginFactory(CImgRollingGuidancePluginFactory, {}, {});

void CImgRollingGuidancePluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgRollingGuidancePluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgRollingGuidancePlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigmaS);
        param->setLabel(kParamSigmaSLabel);
        param->setHint(kParamSigmaSHint);
        param->setRange(0, 1000);
        param->setDisplayRange(0, 25);
        param->setDefault(kParamSigmaSDefault);
        param->setIncrement(0.1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigmaR);
        param->setLabel(kParamSigmaRLabel);
        param->setHint(kParamSigmaRHint);
        param->setRange(0, 10.0);
        param->setDisplayRange(0, 0.5);
        param->setDefault(kParamSigmaRDefault);
        param->setIncrement(0.005);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamIterations);
        param->setLabel(kParamIterationsLabel);
        param->setHint(kParamIterationsHint);
        param->setRange(0, 10);
        param->setDisplayRange(0, 10);
        param->setDefault(kParamIterationsDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgRollingGuidancePlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgRollingGuidancePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgRollingGuidancePlugin(handle);
}


void getCImgRollingGuidancePluginID(OFX::PluginFactoryArray &ids)
{
    static CImgRollingGuidancePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
