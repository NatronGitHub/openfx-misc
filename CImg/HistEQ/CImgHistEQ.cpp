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
 * OFX CImgHistEQ plugin.
 */

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
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 0 // Histogram must be computed on the whole image
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
    : CImgFilterPluginHelper<CImgHistEQParams,false>(handle, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/true, /*defaultProcessAlphaOnRGBA=*/false)
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
                                                                              kSupportsXY,
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
        if (page) {
            page->addChild(*param);
        }
    }

    CImgHistEQPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgHistEQPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgHistEQPlugin(handle);
}


static CImgHistEQPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
