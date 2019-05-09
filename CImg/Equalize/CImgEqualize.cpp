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
 * OFX CImgEqualize plugin.
 */

#include <memory>
#include <cmath>
#include <cfloat> // DBL_MAX
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

#define kPluginName          "EqualizeCImg"
#define kPluginGrouping      "Color"
#define kPluginDescription \
    "Equalize histogram of pixel values.\n" \
    "To equalize image brightness only, use the HistEQCImg plugin.\n" \
    "Uses the 'equalize' function from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgEqualize"
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
#if cimg_use_openmp!=0
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

#define kParamMin "min_value"
#define kParamMinLabel "Min Value"
#define kParamMinHint "Minimum pixel value considered for the histogram computation. All pixel values lower than min_value will not be counted."
#define kParamMinDefault 0.0

#define kParamMax "max_value"
#define kParamMaxLabel "Max Value"
#define kParamMaxHint "Maximum pixel value considered for the histogram computation. All pixel values higher than max_value will not be counted."
#define kParamMaxDefault 1.0


/// Equalize plugin
struct CImgEqualizeParams
{
    int nb_levels;
    double min_value;
    double max_value;
};

class CImgEqualizePlugin
    : public CImgFilterPluginHelper<CImgEqualizeParams, false>
{
public:

    CImgEqualizePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgEqualizeParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
    {
        _nb_levels  = fetchIntParam(kParamNbLevels);
        _min_value  = fetchDoubleParam(kParamMin);
        _max_value  = fetchDoubleParam(kParamMax);
        assert(_nb_levels && _min_value && _max_value);
    }

    virtual void getValuesAtTime(double time,
                                 CImgEqualizeParams& params) OVERRIDE FINAL
    {
        _nb_levels->getValueAtTime(time, params.nb_levels);
        _min_value->getValueAtTime(time, params.min_value);
        _max_value->getValueAtTime(time, params.max_value);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgEqualizeParams& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = 0;

        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const RenderArguments & /*args*/,
                        const CImgEqualizeParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        cimg.equalize(params.nb_levels, (float)params.min_value, (float)params.max_value);
    }

    //virtual bool isIdentity(const IsIdentityArguments &/*args*/, const CImgEqualizeParams& /*params*/) OVERRIDE FINAL
    //{
    //    return false;
    //};

private:

    // params
    IntParam *_nb_levels;
    DoubleParam *_min_value;
    DoubleParam *_max_value;
};


mDeclarePluginFactory(CImgEqualizePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgEqualizePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgEqualizePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                             ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgEqualizePlugin::describeInContextBegin(desc, context,
                                                                           kSupportsRGBA,
                                                                           kSupportsRGB,
                                                                           kSupportsXY,
                                                                           kSupportsAlpha,
                                                                           kSupportsTiles,
                                                                           /*processRGB=*/ true,
                                                                           /*processAlpha*/ false,
                                                                           /*processIsSecret=*/ false);

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamNbLevels);
        param->setLabel(kParamNbLevelsLabel);
        param->setHint(kParamNbLevelsHint);
        param->setDefault(kParamNbLevelsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamMin);
        param->setLabel(kParamMinLabel);
        param->setHint(kParamMinHint);
        param->setDefault(kParamMinDefault);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.001);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamMax);
        param->setLabel(kParamMaxLabel);
        param->setHint(kParamMaxHint);
        param->setDefault(kParamMaxDefault);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.001);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgEqualizePlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgEqualizePluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    return new CImgEqualizePlugin(handle);
}

static CImgEqualizePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
