/*
 OFX CImgGuided plugin.

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

#include "CImgGuided.h"

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

#define kPluginName          "GuidedCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Blur image, with the Guided Image filter.\n" \
"The algorithm is described in: " \
"He et al., \"Guided Image Filtering,\" " \
"http://research.microsoft.com/en-us/um/people/kahe/publications/pami12guidedfilter.pdf\n" \
"Uses the 'blur_guided' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgGuided"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamRadius "radius"
#define kParamRadiusLabel "Radius"
#define kParamRadiusHint "Radius of the spatial kernel (positional sigma), in pixel units (>=0)."
#define kParamRadiusDefault 5

#define kParamEpsilon "epsilon"
#define kParamEpsilonLabel "Epsilon"
#define kParamEpsilonHint "Regularization parameter. The actual guided filter parameter is epsilon^2)."
#define kParamEpsilonDefault 0.2

using namespace OFX;

/// Guided plugin
struct CImgGuidedParams
{
    int radius;
    double epsilon;
};

class CImgGuidedPlugin : public CImgFilterPluginHelper<CImgGuidedParams,false>
{
public:

    CImgGuidedPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgGuidedParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _radius  = fetchIntParam(kParamRadius);
        _epsilon  = fetchDoubleParam(kParamEpsilon);
        assert(_radius && _epsilon);
    }

    virtual void getValuesAtTime(double time, CImgGuidedParams& params) OVERRIDE FINAL
    {
        _radius->getValueAtTime(time, params.radius);
        _epsilon->getValueAtTime(time, params.epsilon);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgGuidedParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = (int)std::ceil(params.radius * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgGuidedParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (params.radius == 0) {
            return;
        }
        // blur_guided was introduced in CImg 1.6.0 on Thu Oct 30 11:47:06 2014 +0100
        cimg.blur_guided(cimg, (float)(params.radius * args.renderScale.x), (float)(params.epsilon*params.epsilon));
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &/*args*/, const CImgGuidedParams& params) OVERRIDE FINAL
    {
        return (params.radius == 0);
    };

private:

    // params
    OFX::IntParam *_radius;
    OFX::DoubleParam *_epsilon;
};


mDeclarePluginFactory(CImgGuidedPluginFactory, {}, {});

void CImgGuidedPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgGuidedPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgGuidedPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamRadius);
        param->setLabel(kParamRadiusLabel);
        param->setHint(kParamRadiusHint);
        param->setRange(0, 100);
        param->setDisplayRange(1, 10);
        param->setDefault(kParamRadiusDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEpsilon);
        param->setLabel(kParamEpsilonLabel);
        param->setHint(kParamEpsilonHint);
        param->setRange(0, 1.);
        param->setDisplayRange(0., 0.4);
        param->setDefault(kParamEpsilonDefault);
        param->setIncrement(0.005);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgGuidedPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgGuidedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgGuidedPlugin(handle);
}


void getCImgGuidedPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgGuidedPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
