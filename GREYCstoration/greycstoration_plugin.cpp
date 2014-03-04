/*
 #
 #  Licenses        : This file is 'dual-licensed', you have to choose one
 #                    of the two licenses below to apply.
 #
 #                    CeCILL-C
 #                    The CeCILL-C license is close to the GNU LGPL.
 #                    ( http://www.cecill.info/licences/Licence_CeCILL-C_V1-en.html )
 #
 #                or  CeCILL v2.0
 #                    The CeCILL license is compatible with the GNU GPL.
 #                    ( http://www.cecill.info/licences/Licence_CeCILL_V2-en.html )
 #
 #  This software is governed either by the CeCILL or the CeCILL-C license
 #  under French law and abiding by the rules of distribution of free software.
 #  You can  use, modify and or redistribute the software under the terms of
 #  the CeCILL or CeCILL-C licenses as circulated by CEA, CNRS and INRIA
 #  at the following URL : "http://www.cecill.info".
 #
 #  As a counterpart to the access to the source code and  rights to copy,
 #  modify and redistribute granted by the license, users are provided only
 #  with a limited warranty  and the software's author,  the holder of the
 #  economic rights,  and the successive licensors  have only  limited
 #  liability.
 #
 #  In this respect, the user's attention is drawn to the risks associated
 #  with loading,  using,  modifying and/or developing or reproducing the
 #  software by the user in light of its specific status of free software,
 #  that may mean  that it is complicated to manipulate,  and  that  also
 #  therefore means  that it is reserved for developers  and  experienced
 #  professionals having in-depth computer knowledge. Users are therefore
 #  encouraged to load and test the software's suitability as regards their
 #  requirements in conditions enabling the security of their systems and/or
 #  data to be ensured and,  more generally, to use and operate it in the
 #  same conditions as regards security.
 #
 #  The fact that you are presently reading this means that you have had
 #  knowledge of the CeCILL and CeCILL-C licenses and that you accept its terms.
 #
 */
// Written by Esteban Tovagliari.

#include "greycstoration_plugin.hpp"

#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#define cimg_display 0
#include <CImg.h>

// some utility functions
void copy_ofx_image_to_rgb_cimg(OFX::Image& src, cimg_library::CImg<float>& dst);
void copy_rgb_cimg_to_ofx_image(const cimg_library::CImg<float>& src, OFX::Image& dst);
void copy_alpha_channel(OFX::Image& src, OFX::Image& dst);

class greycstoration_plugin : public OFX::ImageEffect
{
public:

    greycstoration_plugin(OfxImageEffectHandle handle) : ImageEffect(handle)
    {
        src_clip_ = fetchClip("Source");
        dst_clip_ = fetchClip("Output");

        iters_      = fetchIntParam("iters");
        amplitude_  = fetchDoubleParam("amplitude");
        sharpness_  = fetchDoubleParam("sharpness");
        anisotropy_ = fetchDoubleParam("anisotropy");
        alpha_      = fetchDoubleParam("alpha");
        sigma_      = fetchDoubleParam("sigma");
        dl_         = fetchDoubleParam("dl");
        da_         = fetchDoubleParam("da");
        gprec_      = fetchDoubleParam("gprec");
    }

    virtual void render(const OFX::RenderArguments &args)
    {
        std::auto_ptr<OFX::Image> src(src_clip_->fetchImage(args.time));
        std::auto_ptr<OFX::Image> dst(dst_clip_->fetchImage(args.time));

        assert(src->getBounds().x1 == dst->getBounds().x1);
        assert(src->getBounds().x2 == dst->getBounds().x2);
        assert(src->getBounds().y1 == dst->getBounds().y1);
        assert(src->getBounds().y2 == dst->getBounds().y2);

        cimg_library::CImg<float> src_img;

        int iters;
        double amplitude;
        double sharpness;
        double anisotropy;
        double alpha;
        double sigma;
        double dl;
        double da;
        double gprec;

        iters_->getValue(iters);
        amplitude_->getValue(amplitude);
        sharpness_->getValue(sharpness);
        anisotropy_->getValue(anisotropy);
        alpha_->getValue(alpha);
        sigma_->getValue(sigma);
        dl_->getValue(dl);
        da_->getValue(da);
        gprec_->getValue(gprec);

        copy_ofx_image_to_rgb_cimg(*src, src_img);

        src_img *= 255.0f;

        for (int i = 0; i < iters; ++i) {
            src_img = src_img.get_blur_anisotropic(amplitude / args.renderScale.x, sharpness, anisotropy, alpha, sigma, dl, da, gprec *args.renderScale.x);
        }

        src_img *= (1.0f / 255.0f);

        copy_rgb_cimg_to_ofx_image(src_img, *dst);
        copy_alpha_channel(*src, *dst);
    }

private:

    // clips
    OFX::Clip *src_clip_;
    OFX::Clip *dst_clip_;

    // params
    OFX::IntParam *iters_;
    OFX::DoubleParam *amplitude_;
    OFX::DoubleParam *sharpness_;
    OFX::DoubleParam *anisotropy_;
    OFX::DoubleParam *alpha_;
    OFX::DoubleParam *sigma_;
    OFX::DoubleParam *dl_;
    OFX::DoubleParam *da_;
    OFX::DoubleParam *gprec_;
};

void greycstoration_plugin_factory::describe(OFX::ImageEffectDescriptor& desc)
{
    desc.setLabels("GREYCstoration", "GREYCstoration", "GREYCstoration");
    desc.setPluginGrouping("Filter");
    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);
    desc.setSupportsTiles(false);
    desc.setSupportsMultiResolution(true);
    desc.addSupportedBitDepth(OFX::eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
}

void greycstoration_plugin_factory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    OFX::ClipDescriptor *srcClip = desc.defineClip("Source");
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(false);
    srcClip->setIsMask(false);

    OFX::ClipDescriptor *dstClip = desc.defineClip("Output");
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->setSupportsTiles(false);

    // create the params
    OFX::PageParamDescriptor *page = desc.definePageParam("controls");

    OFX::IntParamDescriptor *iters = desc.defineIntParam("iters");
    iters->setLabels("Iters", "Iters", "Iters");
    iters->setScriptName("iters");
    iters->setRange(1, 5);
    iters->setDefault(1);
    page->addChild(*iters);

    OFX::DoubleParamDescriptor *amplitude = desc.defineDoubleParam("amplitude");
    amplitude->setLabels("Amplitude", "Amplitude", "Amplitude");
    amplitude->setScriptName("amplitude");
    amplitude->setRange(0, 1000);
    amplitude->setDefault(100);
    amplitude->setIncrement(1);
    page->addChild(*amplitude);

    OFX::DoubleParamDescriptor *sharpness = desc.defineDoubleParam("sharpness");
    sharpness->setLabels("Sharpness", "Sharpness", "Sharpness");
    sharpness->setScriptName("sharpness");
    sharpness->setRange(0, 1);
    sharpness->setDefault(0.7);
    sharpness->setIncrement(0.05);
    page->addChild(*sharpness);

    OFX::DoubleParamDescriptor *anisotropy = desc.defineDoubleParam("anisotropy");
    anisotropy->setLabels("Anisotropy", "Anisotropy", "Anisotropy");
    anisotropy->setScriptName("anisotropy");
    anisotropy->setRange(0, 1);
    anisotropy->setDefault(0.6);
    anisotropy->setIncrement(0.05);
    page->addChild(*anisotropy);

    OFX::DoubleParamDescriptor *alpha = desc.defineDoubleParam("alpha");
    alpha->setLabels("Alpha", "Alpha", "Alpha");
    alpha->setScriptName("alpha");
    alpha->setRange(0, 1);
    alpha->setDefault(0.6);
    alpha->setIncrement(0.05);
    page->addChild(*alpha);

    OFX::DoubleParamDescriptor *sigma = desc.defineDoubleParam("sigma");
    sigma->setLabels("Sigma", "Sigma", "Sigma");
    sigma->setScriptName("sigma");
    sigma->setRange(0, 3);
    sigma->setDefault(1.1);
    sigma->setIncrement(0.05);
    page->addChild(*sigma);

    OFX::DoubleParamDescriptor *dl = desc.defineDoubleParam("dl");
    dl->setLabels("Dl", "Dl", "Dl");
    dl->setScriptName("dl");
    dl->setRange(0, 1);
    dl->setDefault(0.8);
    dl->setIncrement(0.05);
    page->addChild(*dl);

    OFX::DoubleParamDescriptor *da = desc.defineDoubleParam("da");
    da->setLabels("Da", "Da", "Da");
    da->setScriptName("da");
    da->setRange(0, 180);
    da->setDefault(30);
    da->setIncrement(0.5);
    page->addChild(*da);

    OFX::DoubleParamDescriptor *gprec = desc.defineDoubleParam("gprec");
    gprec->setLabels("Gprec", "Gprec", "Gprec");
    gprec->setScriptName("gprec");
    gprec->setRange(0, 5);
    gprec->setDefault(2);
    gprec->setIncrement(0.05);
    page->addChild(*gprec);
}

OFX::ImageEffect* greycstoration_plugin_factory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new greycstoration_plugin(handle);
}

// utils
void copy_ofx_image_to_rgb_cimg(OFX::Image& src, cimg_library::CImg<float>& dst)
{
    int width  = src.getBounds().x2 - src.getBounds().x1;
    int height = src.getBounds().y2 - src.getBounds().y1;

    if ((dst.width() != width) || (dst.height() != height)) {
	    dst.assign(width, height, 1, 3);
    }

    for (int j = 0; j < height; ++j) {
        float *psrc = reinterpret_cast<float*>(src.getPixelAddress(src.getBounds().x1, src.getBounds().y1 + j));

        for (int i = 0; i < width; ++i) {
            dst(i, j, 0, 0) = *psrc++;
            dst(i, j, 0, 1) = *psrc++;
            dst(i, j, 0, 2) = *psrc++;
            ++psrc; // skip alpha
        }
    }
}

void copy_rgb_cimg_to_ofx_image(const cimg_library::CImg<float>& src, OFX::Image& dst)
{
    int width  = dst.getBounds().x2 - dst.getBounds().x1;
    int height = dst.getBounds().y2 - dst.getBounds().y1;

    for (int j = 0; j < height; ++j) {
        float *pdst = reinterpret_cast<float*>(dst.getPixelAddress(dst.getBounds().x1, dst.getBounds().y1 + j));

        for (int i = 0; i < width; ++i) {
            *pdst++ = src(i, j, 0, 0);
            *pdst++ = src(i, j, 0, 1);
            *pdst++ = src(i, j, 0, 2);
            ++pdst; // skip alpha
        }
    }
}

void copy_alpha_channel(OFX::Image& src, OFX::Image& dst)
{
    int width  = dst.getBounds().x2 - dst.getBounds().x1;
    int height = dst.getBounds().y2 - dst.getBounds().y1;

    for (int j = 0; j < height; ++j) {
        float *psrc = reinterpret_cast<float*>(src.getPixelAddress(src.getBounds().x1, src.getBounds().y1 + j));
        float *pdst = reinterpret_cast<float*>(dst.getPixelAddress(dst.getBounds().x1, dst.getBounds().y1 + j));
        
        psrc += 3;
        pdst += 3;
        
        for (int i = 0; i < width; ++i) {
            *psrc = *pdst;
            psrc += 4;
            pdst += 4;
        }
    }
}
