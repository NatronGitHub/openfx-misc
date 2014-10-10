/*
 OFX CImgPlasma plugin.

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

#include "CImgPlasma.h"

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

#define kPluginName          "PlasmaCImg"
#define kPluginGrouping      "Draw"
#define kPluginDescription \
"Draw a random plasma texture (using the mid-point algorithm).\n" \
"Note that each render gives a different noise.\n" \
"Uses the 'draw_plasma' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgPlasma"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0 // Plasma effect can only be computed on the whole image
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamAlpha "alpha"
#define kParamAlphaLabel "Alpha"
#define kParamAlphaHint "Alpha-parameter, in intensity units (>=0)."
#define kParamAlphaDefault 0.002 // 0.5/255
#define kParamAlphaMin 0.
#define kParamAlphaMax 0.02 // 5./255
#define kParamAlphaIncrement 0.0005

#define kParamBeta "beta"
#define kParamBetaLabel "Beta"
#define kParamBetaHint "Beta-parameter, in intensity units (>=0)."
#define kParamBetaDefault 0.
#define kParamBetaMin 0.
#define kParamBetaMax 0.5 // 100./255
#define kParamBetaIncrement 0.01

#define kParamScale "scale"
#define kParamScaleLabel "Scale"
#define kParamScaleHint "Scale, in pixels (>=0)."
#define kParamScaleDefault 8
#define kParamScaleMin 2
#define kParamScaleMax 10


using namespace OFX;

/// Plasma plugin
struct CImgPlasmaParams
{
    double alpha;
    double beta;
    int scale;
};

class CImgPlasmaPlugin : public CImgFilterPluginHelper<CImgPlasmaParams>
{
public:

    CImgPlasmaPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgPlasmaParams>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _alpha  = fetchDoubleParam(kParamAlpha);
        _beta  = fetchDoubleParam(kParamBeta);
        _scale = fetchIntParam(kParamScale);
        assert(_alpha && _beta && _scale);
    }

    virtual void getValuesAtTime(double time, CImgPlasmaParams& params) OVERRIDE FINAL
    {
        _alpha->getValueAtTime(time, params.alpha);
        _beta->getValueAtTime(time, params.beta);
        _scale->getValueAtTime(time, params.scale);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI rect, const OfxPointD& renderScale, const CImgPlasmaParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = std::ceil((params.scale) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgPlasmaParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
      cimg.draw_plasma(params.alpha, params.beta, std::floor(params.scale * args.renderScale.x));
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgPlasmaParams& params) OVERRIDE FINAL
    {
        return (std::floor(params.scale * args.renderScale.x) == 0);
    };

private:

    // params
    OFX::DoubleParam *_alpha;
    OFX::DoubleParam *_beta;
    OFX::IntParam *_scale;
};


mDeclarePluginFactory(CImgPlasmaPluginFactory, {}, {});

void CImgPlasmaPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgPlasmaPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgPlasmaPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
        param->setLabels(kParamAlphaLabel, kParamAlphaLabel, kParamAlphaLabel);
        param->setHint(kParamAlphaHint);
        param->setRange(kParamAlphaMin, kParamAlphaMax);
        param->setDefault(kParamAlphaDefault);
        param->setIncrement(kParamAlphaIncrement);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBeta);
        param->setLabels(kParamBetaLabel, kParamBetaLabel, kParamBetaLabel);
        param->setHint(kParamBetaHint);
        param->setRange(kParamBetaMin, kParamBetaMax);
        param->setDefault(kParamBetaDefault);
        param->setIncrement(kParamBetaIncrement);
        page->addChild(*param);
    }
    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamScale);
        param->setLabels(kParamScaleLabel, kParamScaleLabel, kParamScaleLabel);
        param->setHint(kParamScaleHint);
        param->setRange(kParamScaleMin, kParamScaleMax);
        param->setDefault(kParamScaleDefault);
        page->addChild(*param);
    }

    CImgPlasmaPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgPlasmaPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CImgPlasmaPlugin(handle);
}


void getCImgPlasmaPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgPlasmaPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
