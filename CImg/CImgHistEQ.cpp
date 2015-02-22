/*
 OFX CImgHistEQ plugin.

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

#include "CImgHistEQ.h"

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
#include "ofxsLut.h"

#include "CImgFilter.h"

#define kPluginName          "HistEQCImg"
#define kPluginGrouping      "Color"
#define kPluginDescription \
"Equalize histogram of brightness values.\n" \
"Uses the 'equalize' function from the CImg library on the 'V' channel of the HSV decomposition of the image.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgHistEQ"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0 // Histogram must be computed on the whole image
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe
#define kHostFrameThreading true
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsAlpha true

#define kParamNbLevels "nb_levels"
#define kParamNbLevelsLabel "NbLevels"
#define kParamNbLevelsHint "Number of histogram levels used for the equalization."
#define kParamNbLevelsDefault 4096

using namespace OFX;

/// HistEQ plugin
struct CImgHistEQParams
{
    int nb_levels;
};

class CImgHistEQPlugin : public CImgFilterPluginHelper<CImgHistEQParams,false>
{
public:

    CImgHistEQPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgHistEQParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _nb_levels  = fetchIntParam(kParamNbLevels);
        assert(_nb_levels);
    }

    virtual void getValuesAtTime(double time, CImgHistEQParams& params) OVERRIDE FINAL
    {
        _nb_levels->getValueAtTime(time, params.nb_levels);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& /*renderScale*/, const CImgHistEQParams& /*params*/, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 0;
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &/*args*/, const CImgHistEQParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (cimg.spectrum() < 3) {
            assert(cimg.spectrum() == 1); // Alpha image
            float vmin, vmax;
            vmin = cimg.min_max(vmax);
            cimg.equalize(params.nb_levels, vmin, vmax);
        } else {
#ifdef cimg_use_openmp
#pragma omp parallel for if (cimg.size()>=1048576)
#endif
            cimg_forXY(cimg, x, y) {
                OFX::Color::rgb_to_hsv(cimg(x,y,0,0), cimg(x,y,0,1), cimg(x,y,0,2), &cimg(x,y,0,0), &cimg(x,y,0,1), &cimg(x,y,0,2));
            }
            cimg_library::CImg<float> vchannel = cimg.get_shared_channel(2);
            float vmin, vmax;
            vmin = vchannel.min_max(vmax);
            vchannel.equalize(params.nb_levels, vmin, vmax);
            cimg_forXY(cimg, x, y) {
                OFX::Color::hsv_to_rgb(cimg(x,y,0,0), cimg(x,y,0,1), cimg(x,y,0,2), &cimg(x,y,0,0), &cimg(x,y,0,1), &cimg(x,y,0,2));
            }
        }
    }

    //virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgHistEQParams& params) OVERRIDE FINAL
    //{
    //    return false;
    //};

private:

    // params
    OFX::IntParam *_nb_levels;
};


mDeclarePluginFactory(CImgHistEQPluginFactory, {}, {});

void CImgHistEQPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgHistEQPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgHistEQPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles,
                                                                              /*processRGB=*/true,
                                                                              /*processAlpha=*/true,
                                                                              /*processIsSecret=*/true);

    {
        OFX::IntParamDescriptor *param = desc.defineIntParam(kParamNbLevels);
        param->setLabel(kParamNbLevelsLabel);
        param->setHint(kParamNbLevelsHint);
        param->setDefault(kParamNbLevelsDefault);
        page->addChild(*param);
    }

    CImgHistEQPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgHistEQPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgHistEQPlugin(handle);
}


void getCImgHistEQPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgHistEQPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
