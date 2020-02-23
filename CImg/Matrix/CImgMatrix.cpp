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
 * OFX CImgMatrix plugin.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <cfloat> // DBL_MAX
#include <algorithm>
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
using namespace cimg_library;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "Matrix0x0CImg"
#define kPluginGrouping      "Filter/Matrix"
#define kPluginDescription \
"Compute the convolution of the input image with the specified matrix.\n" \
"This works by multiplying each surrounding pixel of the input image with the corresponding matrix coefficient (the current pixel is at the center of the matrix), and summing up the results.\n" \
"For example [-1 -1 -1] [-1 8 -1] [-1 -1 -1] produces an edge detection filter (which is an approximation of the Laplacian filter) by multiplying the center pixel by 8 and the surrounding pixels by -1, and then adding the nine values together to calculate the new value of the center pixel.\n" \
"Uses the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "eu.cimg.CImgMatrix"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1
#define kSupportsTiles 1
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

#define kParamMatrix "matrix"
#define kParamMatrixLabel "Matrix", "The coefficients of the matrix filter."
#define kParamMatrixCoeffLabel "", "Matrix coefficient."

#define kParamNormalize "normalize"
#define kParamNormalizeLabel "Normalize", "Normalize the matrix coefficients so that their sum is 1."


/// Matrix plugin
template<int dim>
struct CImgMatrixParams
{
    double coeff[dim*dim];
    bool normalize;
};

template<int dim>
class CImgMatrixPlugin
: public CImgFilterPluginHelper<CImgMatrixParams<dim>, false>
{
public:

    CImgMatrixPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgMatrixParams<dim>, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ false)
    {
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                _coeff[i*dim + j] = ImageEffect::fetchDoubleParam(std::string(kParamMatrix) + (char)('1' + i) + (char)('1' + j));
                assert(_coeff[i*dim + j]);
            }
        }
        _normalize  = ImageEffect::fetchBooleanParam(kParamNormalize);
        assert(_normalize);
    }

    virtual void getValuesAtTime(double time,
                                 CImgMatrixParams<dim>& params) OVERRIDE FINAL
    {
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                _coeff[i*dim + j]->getValueAtTime(time, params.coeff[i*dim + j]);
            }
        }
        _normalize->getValueAtTime(time, params.normalize);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgMatrixParams<dim>& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        int delta_pix_x = (int)std::ceil((dim * renderScale.x - 1)/2.);
        int delta_pix_y = (int)std::ceil((dim * renderScale.x - 1)/2.); // note: we only use renderScale.x

        roi->x1 = rect.x1 - delta_pix_x;
        roi->x2 = rect.x2 + delta_pix_x;
        roi->y1 = rect.y1 - delta_pix_y;
        roi->y2 = rect.y2 + delta_pix_y;
    }

    virtual void render(const RenderArguments &args,
                        const CImgMatrixParams<dim>& params,
                        int /*x1*/,
                        int /*y1*/,
                        CImg<cimgpix_t>& /*mask*/,
                        CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        // compute the filter size from renderscale.x:
        // - 1x1: identity if normalize is checked, else mult by scalar
        // - 3x3 if dim=3 and renderScale.x > 0.33 or dim=5 and renderScale.x > 0.4
        // - 5x5 if dim=5 and renderScale.x > 0.6
        const OfxPointD& renderScale = args.renderScale;
        double s = 1;
        if (params.normalize) {
            s = 0.;
            for (int i=0; i < dim * dim; ++i) {
                s += params.coeff[i];
            }
            if (s == 0.) {
                ImageEffect::setPersistentMessage(OFX::Message::eMessageError, "", "Matrix sums to zero, cannot normalize");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        if (dim == 5 && renderScale.x > 0.6) {
            CImg<cimgpix_t> res(cimg.width(), cimg.height(), cimg.depth(), cimg.spectrum());
            double *M = (double*)params.coeff;
            double Mbb = M[0]; double Mpb = M[1]; double Mcb = M[2]; double Mnb = M[3]; double Mab = M[4];
            double Mbp = M[5]; double Mpp = M[6]; double Mcp = M[7]; double Mnp = M[8]; double Map = M[9];
            double Mbc = M[10]; double Mpc = M[11]; double Mcc = M[12]; double Mnc = M[13]; double Mac = M[14];
            double Mbn = M[15]; double Mpn = M[16]; double Mcn = M[17]; double Mnn = M[18]; double Man = M[19];
            double Mba = M[20]; double Mpa = M[21]; double Mca = M[22]; double Mna = M[23]; double Maa = M[24];

            cimg_pragma_openmp(parallel for cimg_openmp_if(cimg.width() * cimg.height() >= 262144 && cimg.spectrum() >= 2))
            cimg_forC(cimg,c) {
                cimgpix_t *ptrd = res.data() + cimg.width() * cimg.height() * c;
                CImg_5x5(I,cimgpix_t);
                cimg_for5x5(cimg,x,y,0,c,I,cimgpix_t) {
                    *(ptrd++) = static_cast<cimgpix_t>(Mbb * Ibb + Mpb * Ipb + Mcb * Icb + Mnb * Inb + Mab * Iab +
                                                       Mbp * Ibp + Mpp * Ipp + Mcp * Icp + Mnp * Inp + Map * Iap +
                                                       Mbc * Ibc + Mpc * Ipc + Mcc * Icc + Mnc * Inc + Mac * Iac +
                                                       Mbn * Ibn + Mpn * Ipn + Mcn * Icn + Mnn * Inn + Man * Ian +
                                                       Mba * Iba + Mpa * Ipa + Mca * Ica + Mna * Ina + Maa * Iaa);
                }
            }
            cimg = res;
            if (params.normalize) {
                cimg /= s;
            }

        } else if ( (dim == 3 && renderScale.x > 1./3.) ||
                    (dim == 5 && renderScale.x > 0.4) ) {
            CImg<cimgpix_t> res(cimg.width(), cimg.height(), cimg.depth(), cimg.spectrum());
            double *M = (double*)params.coeff;
            double Mpp = M[0]; double Mcp = M[1]; double Mnp = M[2];
            double Mpc = M[3]; double Mcc = M[4]; double Mnc = M[5];
            double Mpn = M[6]; double Mcn = M[7]; double Mnn = M[8];

            cimg_pragma_openmp(parallel for cimg_openmp_if(cimg.width() * cimg.height() >= 262144 && cimg.spectrum() >= 2))
            cimg_forC(cimg,c) {
                cimgpix_t *ptrd = res.data() + cimg.width() * cimg.height() * c;
                CImg_3x3(I,cimgpix_t);
                cimg_for3x3(cimg,x,y,0,c,I,cimgpix_t) {
                    *(ptrd++) = static_cast<cimgpix_t>(Mpp * Ipp + Mcp * Icp + Mnp * Inp +
                                                       Mpc * Ipc + Mcc * Icc + Mnc * Inc +
                                                       Mpn * Ipn + Mcn * Icn + Mnn * Inn);
                }
            }
            cimg = res;
            if (params.normalize) {
                cimg /= s;
            }

        } else {
            if (params.normalize) {
                return;
            }
            cimg *= s;
        }
    }

    virtual bool isIdentity(const IsIdentityArguments &/*args*/,
                            const CImgMatrixParams<dim>& params) OVERRIDE FINAL
    {
        for (int i = 0; i < dim; ++i) {
            for (int j = 0; j < dim; ++j) {
                bool isIdentityCoeff = false;
                if (i == (dim-1)/2 && j == (dim-1)/2 ) {
                    if ( (params.coeff[i*dim + j] == 1.) ||
                        (params.coeff[i*dim + j] != 0. && params.normalize) ) {
                        isIdentityCoeff = true;
                    }
                } else if (params.coeff[i*dim + j] == 0.) {
                    isIdentityCoeff = true;
                }
                if (!isIdentityCoeff) {
                    return false;
                }
            }
        }
        return false;
    };

    virtual void changedParam(const InstanceChangedArgs &args,
                              const std::string &paramName) OVERRIDE FINAL
    {
        // must clear persistent message, or render() is not called by Nuke
        ImageEffect::clearPersistentMessage();

        CImgFilterPluginHelper<CImgMatrixParams<dim>, false>::changedParam(args, paramName);
    }
private:

    // params
    DoubleParam *_coeff[dim*dim];
    BooleanParam *_normalize;
};


//mDeclarePluginFactory(CImgMatrixPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<int dim>
class CImgMatrixPluginFactory
: public PluginFactoryHelper<CImgMatrixPluginFactory<dim> >
{
public:
    CImgMatrixPluginFactory<dim>(const std::string & id, unsigned int verMaj, unsigned int verMin)
    : PluginFactoryHelper<CImgMatrixPluginFactory>(id, verMaj, verMin)
    {
    }

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();}
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};

template<int dim>
void
CImgMatrixPluginFactory<dim>::describe(ImageEffectDescriptor& desc)
{
    // basic labels
    std::string pluginName(kPluginName);
    std::replace( pluginName.begin(), pluginName.end(), '0', (char)('0' + dim));
    desc.setLabel(pluginName);
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

template<int dim>
void
CImgMatrixPluginFactory<dim>::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgMatrixPlugin<dim>::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsXY,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles,
                                                                              /*processRGB=*/ true,
                                                                              /*processAlpha*/ true,
                                                                              /*processIsSecret=*/ false);
    
    
    {
        GroupParamDescriptor* group = desc.defineGroupParam(kParamMatrix);
        if (group) {
            group->setLabelAndHint(kParamMatrixLabel);
            group->setOpen(true);
        }
        for (int i = dim-1; i >=0; --i) {
            for (int j = 0; j < dim; ++j) {
                DoubleParamDescriptor* param = desc.defineDoubleParam(std::string(kParamMatrix) + (char)('1' + i) + (char)('1' + j));
                param->setLabelAndHint(kParamMatrixCoeffLabel);
                param->setRange(-DBL_MAX, DBL_MAX);
                param->setDisplayRange(-1., 1.);
                param->setDefault(0.);
                if (j < dim - 1) {
                    param->setLayoutHint(eLayoutHintNoNewLine, 1);
                }
                if (group) {
                    param->setParent(*group);
                }
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamNormalize);
        param->setLabelAndHint(kParamNormalizeLabel);
        param->setDefault(false);
        param->setAnimates(false);
        //if (group) {
        //    param->setParent(*group);
        //}
        if (page) {
            page->addChild(*param);
        }
    }

    CImgMatrixPlugin<dim>::describeInContextEnd(desc, context, page);
}

template<int dim>
ImageEffect*
CImgMatrixPluginFactory<dim>::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgMatrixPlugin<dim>(handle);
}

static CImgMatrixPluginFactory<3> p3(kPluginIdentifier "3x3", kPluginVersionMajor, kPluginVersionMinor);
static CImgMatrixPluginFactory<5> p5(kPluginIdentifier "5x5", kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p5)

OFXS_NAMESPACE_ANONYMOUS_EXIT

