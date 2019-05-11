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
 * OFX CImgNoise plugin.
 */

#include <memory>
#include <cmath>
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

#define kPluginName          "NoiseCImg"
#define kPluginGrouping      "Draw"
#define kPluginDescription \
    "Add random noise to input stream.\n" \
    "\n" \
    "Uses the 'noise' function from the CImg library, modified so that noise is reproductible at each render.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgNoise"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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

#define kParamSigma "sigma"
#define kParamSigmaLabel "Sigma"
#define kParamSigmaHint "Amplitude of the random additive noise."
#define kParamSigmaDefault 0.01

#define kParamType "type"
#define kParamTypeLabel "Type"
#define kParamTypeHint "Type of additive noise."
#define kParamTypeOptionGaussian "Gaussian", "Gaussian noise.", "gaussian"
#define kParamTypeOptionUniform "Uniform", "Uniform noise.", "uniform"
#define kParamTypeOptionSaltPepper "Salt & Pepper", "Salt & pepper noise.", "saltnpepper"
#define kParamTypeOptionPoisson "Poisson", "Poisson noise. Image is divided by Sigma before computing noise, then remultiplied by Sigma.", "poisson"
#define kParamTypeOptionRice "Rice", "Rician noise.", "rice"
#define kParamTypeDefault eTypeGaussian

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different noise."

#define kParamStaticSeed "staticSeed"
#define kParamStaticSeedLabel "Static Seed"
#define kParamStaticSeedHint "When enabled, the dither pattern remains the same for every frame producing a constant noise effect."

enum TypeEnum
{
    eTypeGaussian = 0,
    eTypeUniform,
    eTypeSaltPepper,
    eTypePoisson,
    eTypeRice,
};

#define cimg_noise_internal

#ifdef cimg_noise_internal

// the following is cimg_library::CImg::noise(), but with a fixed seed and pseudo-random function
//! Add random noise to pixel values.

#define T cimgpix_t
#define Tfloat cimgpixfloat_t
using namespace cimg_library;

//! Add random noise to pixel values.
// Differences with the original cimg_library::CImg::noise:
// - static function
// - replaced *this with img
// - replaced all cimg_rof loops with cimg_forXYC, in order to get reproductible results
// - replaced cimg::grand with cimg_grand, etc.
// - add cimg_pragma_openmp(...)
/**
 \param sigma Amplitude of the random additive noise. If \p sigma<0, it stands for a percentage of the
 global value range.
 \param noise_type Type of additive noise (can be \p 0=gaussian, \p 1=uniform, \p 2=Salt and Pepper,
 \p 3=Poisson or \p 4=Rician).
 \return A reference to the modified image instance.
 \note
 - For Poisson noise (\p noise_type=3), parameter \p sigma is ignored, as Poisson noise only depends on
 the image value itself.
 - Function \p CImg<T>::get_noise() is also defined. It returns a non-shared modified copy of the image instance.
 \par Example
 \code
 const CImg<float> img("reference.jpg"), res = img.get_noise(40);
 (img,res.normalize(0,255)).display();
 \endcode
 \image html ref_noise.jpg
 **/
static
CImg<T>&
noise(CImg<T>&img, const double sigma, const unsigned int noise_type, unsigned int seed, int x1, int y1)
{
    if (img.is_empty()) {
        return img;
    }
    const Tfloat vmin = (Tfloat)cimg::type<T>::min();
    const Tfloat vmax = (Tfloat)cimg::type<T>::max();
    Tfloat nsigma = (Tfloat)sigma;
    Tfloat m = 0;
    Tfloat M = 0;
    if (nsigma==0 && noise_type!=3) {
        return img;
    }
    if (nsigma<0 || noise_type==2) {
        m = (Tfloat)img.min_max(M);
    }
    if (nsigma<0) {
        nsigma = (Tfloat)(-nsigma*(M-m)/100.0);
    }
    switch (noise_type) {
        case 0 : { // Gaussian noise
            cimg_pragma_openmp(parallel for collapse(3) cimg_openmp_if(img.size()>=2048))
            cimg_forXYC(img, x, y, c) {
                Tfloat val = (Tfloat)(img(x,y,0,c) + nsigma * cimg_grand(seed, x + x1, y + y1, c));
                if (val > vmax) {
                    val = vmax;
                } else if (val < vmin) {
                    val = vmin;
                }
                img(x,y,0,c) = (T)val;
            }
        } break;
        case 1 : { // Uniform noise
            cimg_pragma_openmp(parallel for collapse(3) cimg_openmp_if(img.size()>=2048))
            cimg_forXYC(img, x, y, c) {
                Tfloat val = (Tfloat)(img(x,y,0,c) + nsigma * cimg_rand(seed, x + x1, y + y1, c, -1,1));
                if (val > vmax) {
                    val = vmax;
                } else if (val < vmin) {
                    val = vmin;
                }
                img(x,y,0,c) = (T)val;
            }
        } break;
        case 2 : { // Salt & Pepper noise
            if (nsigma<0) {
                nsigma = -nsigma;
            }
            if (M==m) {
                m = 0;
                M = cimg::type<T>::is_float()?(Tfloat)1:(Tfloat)cimg::type<T>::max();
            }
            cimg_pragma_openmp(parallel for collapse(3) cimg_openmp_if(img.size()>=2048))
            cimg_forXYC(img, x, y, c) {
                if (cimg_rand(seed, x + x1, y1 + img.height() - y, c, 100) < nsigma) {
                    img(x,y,0,c) = (T)(cimg_rand(seed, x + x1, y + y1, c)<0.5?M:m);
                }
            }
        } break;
        case 3 : { // Poisson Noise
            cimg_pragma_openmp(parallel for collapse(3) cimg_openmp_if(img.size()>=2048))
            cimg_forXYC(img,x,y,c) {
                img(x,y,0,c) = (T)cimg_prand(seed, x + x1, y + y1, c, img(x,y,0,c));
            }
        } break;
        case 4 : { // Rice noise
            const Tfloat sqrt2 = (Tfloat)std::sqrt(2.0);
            cimg_pragma_openmp(parallel for collapse(3) cimg_openmp_if(img.size()>=2048))
            cimg_forXYC(img, x, y, c) {
                const Tfloat
                val0 = (Tfloat)img(x,y)/sqrt2,
                re = (Tfloat)(val0 + nsigma * cimg_grand(seed, x + x1, y + y1, c)),
                im = (Tfloat)(val0 + nsigma * cimg_grand(seed, x + x1, y + y1, c));
                Tfloat val = cimg::hypot(re,im);
                if (val > vmax) {
                    val = vmax;
                } else if (val < vmin) {
                    val = vmin;
                }
                img(x,y,0,c) = (T)val;
            }
        } break;
        default :
            /*
             throw CImgArgumentException(_cimg_instance
             "noise(): Invalid specified noise type %d "
             "(should be { 0=gaussian | 1=uniform | 2=salt&Pepper | 3=poisson }).",
             cimg_instance,
             noise_type);
             */
            break;
    }
    return img;
}
#endif // cimg_noise_internal

/// Noise plugin
struct CImgNoiseParams
{
    double sigma;
    int type_i;
    int seed;
    bool staticSeed;
};

class CImgNoisePlugin
    : public CImgFilterPluginHelper<CImgNoiseParams, true>
{
public:

    CImgNoisePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgNoiseParams, true>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ false)
    {
        _sigma  = fetchDoubleParam(kParamSigma);
        _type = fetchChoiceParam(kParamType);
        assert(_sigma && _type);
        _seed   = fetchIntParam(kParamSeed);
        _staticSeed = fetchBooleanParam(kParamStaticSeed);
        assert(_seed && _staticSeed);
    }

    virtual void getValuesAtTime(double time,
                                 CImgNoiseParams& params) OVERRIDE FINAL
    {
        _sigma->getValueAtTime(time, params.sigma);
        _type->getValueAtTime(time, params.type_i);
        _seed->getValueAtTime(time, params.seed);
        _staticSeed->getValueAtTime(time, params.staticSeed);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgNoiseParams& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        roi->x1 = rect.x1;
        roi->x2 = rect.x2;
        roi->y1 = rect.y1;
        roi->y2 = rect.y2;
    }

    virtual void render(const RenderArguments &args,
                        const CImgNoiseParams& params,
                        int x1,
                        int y1,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        // the noise vs. scale dependency formula is only valid for Gaussian noise
        if (params.type_i == eTypePoisson) {
            cimg /= params.sigma;
        }
#ifdef cimg_noise_internal
        unsigned int seed = cimg_hash(params.seed);
        if (!params.staticSeed) {
            float time_f = args.time;

            // set the seed based on the current time, and double it we get difference seeds on different fields
            seed = cimg_hash( *( (unsigned int*)&time_f ) ^ seed );
        }
        noise(cimg, params.sigma * std::sqrt(args.renderScale.x), params.type_i, seed, x1, y1);
#else
        cimg.noise(params.sigma * std::sqrt(args.renderScale.x), params.type_i);
#endif
        if (params.type_i == eTypePoisson) {
            cimg *= params.sigma;
        }
    }

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgNoiseParams& params) OVERRIDE FINAL
    {
        return (params.sigma == 0.);
    };

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL
    {
        bool staticSeed = _staticSeed->getValue();
        if (!staticSeed) {
            clipPreferences.setOutputFrameVarying(true);
            clipPreferences.setOutputHasContinuousSamples(true);
        }
    }

private:

    // params
    DoubleParam *_sigma;
    ChoiceParam *_type;
    IntParam* _seed;
    BooleanParam* _staticSeed;
};


mDeclarePluginFactory(CImgNoisePluginFactory, {ofxsThreadSuiteCheck();}, {});

void
CImgNoisePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgNoisePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                          ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgNoisePlugin::describeInContextBegin(desc, context,
                                                                        kSupportsRGBA,
                                                                        kSupportsRGB,
                                                                        kSupportsXY,
                                                                        kSupportsAlpha,
                                                                        kSupportsTiles,
                                                                        /*processRGB=*/ true,
                                                                        /*processAlpha*/ false,
                                                                        /*processIsSecret=*/ false);

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabel(kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0., 10.);
        param->setDisplayRange(0., 1.);
        param->setIncrement(0.005);
        param->setDefault(kParamSigmaDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamType);
        param->setLabel(kParamTypeLabel);
        param->setHint(kParamTypeHint);
        assert(param->getNOptions() == eTypeGaussian && param->getNOptions() == 0);
        param->appendOption(kParamTypeOptionGaussian);
        assert(param->getNOptions() == eTypeUniform && param->getNOptions() == 1);
        param->appendOption(kParamTypeOptionUniform);
        assert(param->getNOptions() == eTypeSaltPepper && param->getNOptions() == 2);
        param->appendOption(kParamTypeOptionSaltPepper);
        assert(param->getNOptions() == eTypePoisson && param->getNOptions() == 3);
        param->appendOption(kParamTypeOptionPoisson);
        assert(param->getNOptions() == eTypeRice && param->getNOptions() == 4);
        param->appendOption(kParamTypeOptionRice);
        param->setDefault( (int)kParamTypeDefault );
        if (page) {
            page->addChild(*param);
        }
    }

    // seed
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSeed);
        param->setLabel(kParamSeedLabel);
        param->setHint(kParamSeedHint);
        param->setDefault(2000);
        param->setAnimates(true); // can animate
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamStaticSeed);
        param->setLabel(kParamStaticSeedLabel);
        param->setHint(kParamStaticSeedHint);
        param->setDefault(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    CImgNoisePlugin::describeInContextEnd(desc, context, page);
}

ImageEffect*
CImgNoisePluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new CImgNoisePlugin(handle);
}

static CImgNoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
