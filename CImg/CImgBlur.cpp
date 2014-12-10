/*
 OFX CImgBlur plugin.

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

// TODO: fix Gaussian filter in CImg when Border= nearest and no expand RoD
// TODO: pass the border conditions to the copy processors in CImgFilter

#include "CImgBlur.h"

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

#define kPluginName          "BlurCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Blur input stream by a quasi-gaussian or gaussian filter (recursive implementation).\n" \
"Uses the 'blur' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgBlur"
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

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Size (diameter) of the filter kernel, in pixel units (>=0). The standard deviation of the corresponding Gaussian is size/2.4."
#define kParamSizeDefault 0.

#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black"
#define kParamBoundaryOptionDirichletHint "Dirichlet boundary condition: pixel values out of the image domain are zero."
#define kParamBoundaryOptionNeumann "Nearest"
#define kParamBoundaryOptionNeumannHint "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain."
#define kParamBoundaryOptionPeriodic "Periodic"
#define kParamBoundaryOptionPeriodicHint "Image is considered to be periodic out of the image domain."
#define kParamBoundaryDefault eBoundaryDirichlet
enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

#if cimg_version >= 153
#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "Bluring filter. The quasi-Gaussian filter should be appropriate in most cases. The Gaussian filter is more isotropic (its impulse response has rotational symmetry), but slower."
#define kParamFilterOptionQuasiGaussian "Quasi-Gaussian"
#define kParamFilterOptionQuasiGaussianHint "Quasi-Gaussian filter (0-order recursive Deriche filter, faster)."
#define kParamFilterOptionGaussian "Gaussian"
#define kParamFilterOptionGaussianHint "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower)."
#define kParamFilterDefault eFilterQuasiGaussian
enum FilterEnum
{
    eFilterQuasiGaussian = 0,
    eFilterGaussian,
};
#endif

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by 1.5*size (3.6*sigma)."

using namespace OFX;

/// Blur plugin
struct CImgBlurParams
{
    double size;
    int boundary_i;
#if cimg_version >= 153
    int filter_i;
#endif
    bool expandRoD;
};

class CImgBlurPlugin : public CImgFilterPluginHelper<CImgBlurParams,false>
{
public:

    CImgBlurPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgBlurParams,false>(handle, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale)
    {
        _size  = fetchDoubleParam(kParamSize);
        _boundary  = fetchChoiceParam(kParamBoundary);
        assert(_size && _boundary);
#if cimg_version >= 153
        _filter = fetchChoiceParam(kParamFilter);
        assert(_filter);
#endif
        _expandRoD = fetchBooleanParam(kParamExpandRoD);
        assert(_expandRoD);
    }

    virtual void getValuesAtTime(double time, CImgBlurParams& params) OVERRIDE FINAL
    {
        _size->getValueAtTime(time, params.size);
        _boundary->getValueAtTime(time, params.boundary_i);
#if cimg_version >= 153
        _filter->getValueAtTime(time, params.filter_i);
#endif
        _expandRoD->getValueAtTime(time, params.expandRoD);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix = std::ceil((params.size * 1.5) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OFX::RenderArguments &args, const CImgBlurParams& params, int /*x1*/, int /*y1*/, cimg_library::CImg<float>& cimg) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
#if cimg_version >= 153
        cimg.blur(args.renderScale.x * params.size / 2.4, (bool)params.boundary_i, (bool)params.filter_i);
#else
        cimg.blur(args.renderScale.x * params.size / 2.4, (bool)params.boundary_i);
#endif
    }

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, const CImgBlurParams& params) OVERRIDE FINAL
    {
        return (args.renderScale.x * params.size / 2.4 < 0.1);
    };

    virtual bool getRoD(const OfxRectI& srcRoD, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* dstRoD) OVERRIDE FINAL;

private:

    // params
    OFX::DoubleParam *_size;
    OFX::ChoiceParam *_boundary;
#if cimg_version >= 153
    OFX::ChoiceParam *_filter;
#endif
    OFX::BooleanParam *_expandRoD;
};

bool
CImgBlurPlugin::getRoD(const OfxRectI& srcRoD, const OfxPointD& renderScale, const CImgBlurParams& params, OfxRectI* dstRoD)
{
    if (params.expandRoD && !isEmpty(srcRoD)) {
        int delta_pix = std::ceil((params.size * 1.5) * renderScale.x);
        dstRoD->x1 = srcRoD.x1 - delta_pix;
        dstRoD->x2 = srcRoD.x2 + delta_pix;
        dstRoD->y1 = srcRoD.y1 - delta_pix;
        dstRoD->y2 = srcRoD.y2 + delta_pix;

        return true;
    }

    return false;
}


mDeclarePluginFactory(CImgBlurPluginFactory, {}, {});

void CImgBlurPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
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

void CImgBlurPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    // create the clips and params
    OFX::PageParamDescriptor *page = CImgBlurPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles);

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSize);
        param->setLabels(kParamSizeLabel, kParamSizeLabel, kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setDisplayRange(0, 100);
        param->setDefault(kParamSizeDefault);
        param->setIncrement(0.1);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabels(kParamBoundaryLabel, kParamBoundaryLabel, kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet, kParamBoundaryOptionDirichletHint);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann, kParamBoundaryOptionNeumannHint);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic, kParamBoundaryOptionPeriodicHint);
        param->setDefault((int)kParamBoundaryDefault);
        page->addChild(*param);
    }
#if cimg_version >= 153
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabels(kParamFilterLabel, kParamFilterLabel, kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterQuasiGaussian && param->getNOptions() == 0);
        param->appendOption(kParamFilterOptionQuasiGaussian, kParamFilterOptionQuasiGaussianHint);
        assert(param->getNOptions() == eFilterGaussian && param->getNOptions() == 1);
        param->appendOption(kParamFilterOptionGaussian, kParamFilterOptionGaussianHint);
        param->setDefault((int)kParamFilterDefault);
        page->addChild(*param);
    }
#endif
    {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabels(kParamExpandRoDLabel, kParamExpandRoDLabel, kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(true);
        page->addChild(*param);
    }

    CImgBlurPlugin::describeInContextEnd(desc, context, page);
}

OFX::ImageEffect* CImgBlurPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle);
}


void getCImgBlurPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgBlurPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
