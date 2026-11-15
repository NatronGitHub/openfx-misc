/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
#include <vector>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"
#include "ofxsLut.h"

#include "CImgFilter.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "HistEQCImg"
#define kPluginGrouping      "Color"
#define kPluginDescriptionHead \
    "Equalize histogram of brightness values.\n" \
    "Uses the 'equalize' function from the CImg library on the 'V' channel of the HSV decomposition of the image.\n"
#define kPluginDescriptionChannels \
    "The HSV decomposition always uses the R, G and B channels, and only the processed channels are modified. " \
    "The alpha channel is equalized on its own only if no color channel is processed (e.g. on Alpha images).\n"
#define kPluginDescriptionTail \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."
#define kPluginDescription kPluginDescriptionHead kPluginDescriptionChannels kPluginDescriptionTail
#define kPluginDescriptionLegacy kPluginDescriptionHead kPluginDescriptionTail

#define kPluginIdentifier    "net.sf.cimg.CImgHistEQ"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 3.0: when only some of the R, G, B channels are processed, compute the HSV decomposition
//              from R, G and B anyway, and only write the processed channels (version 2 used the
//              processed channels as if they were R, G, B)
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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


/// HistEQ plugin
struct CImgHistEQParams
{
    int nb_levels;
    bool processR, processG, processB, processA;
};

class CImgHistEQPlugin
    : public CImgFilterPluginHelper<CImgHistEQParams, false>
{
public:

    CImgHistEQPlugin(OfxImageEffectHandle handle,
                     int majorVersion)
        // version >= 3 gets all the channels in render() (no component remapping), in order to
        // compute the HSV decomposition from R, G and B even if some of them are not processed
        : CImgFilterPluginHelper<CImgHistEQParams, false>(handle, /*usesMask=*/false, /*supportsComponentRemapping=*/ (majorVersion < 3) && kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, /*defaultUnpremult=*/ true)
        , _legacyChannels(majorVersion < 3)
    {
        _nb_levels  = fetchIntParam(kParamNbLevels);
        assert(_nb_levels);
    }

    virtual void getValuesAtTime(double time,
                                 CImgHistEQParams& params) OVERRIDE FINAL
    {
        _nb_levels->getValueAtTime(time, params.nb_levels);
        params.processR = params.processG = params.processB = params.processA = true;
        if (_processR) {
            _processR->getValueAtTime(time, params.processR);
            _processG->getValueAtTime(time, params.processG);
            _processB->getValueAtTime(time, params.processB);
            _processA->getValueAtTime(time, params.processA);
        }
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& /*renderScale*/,
                        const CImgHistEQParams& /*params*/,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        // does not support tiles: the whole image is processed anyway
        *roi = rect;
    }

    virtual void render(const RenderArguments & /*args*/,
                        const CImgHistEQParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (!_legacyChannels) {
            renderAllChannels(params, cimg);

            return;
        }
        // legacy (version < 3): cimg only holds the processed channels, which are used as if
        // they were R, G, B (kept so old projects render the same)
        if (cimg.spectrum() < 3) {
            assert(cimg.spectrum() == 1); // Alpha image
            float vmin, vmax;
            vmin = cimg.min_max(vmax);
            cimg.equalize(params.nb_levels, vmin, vmax);
        } else {
            cimg_pragma_openmp(parallel for if (cimg.size()>=1048576))
            cimg_forXY(cimg, x, y) {
                float h, s, v;
                Color::rgb_to_hsv(cimg(x, y, 0, 0), cimg(x, y, 0, 1), cimg(x, y, 0, 2), &h, &s, &v);

                cimg(x, y, 0, 0) = h;
                cimg(x, y, 0, 1) = s;
                cimg(x, y, 0, 2) = v;
            }
            cimg_library::CImg<cimgpix_t> vchannel = cimg.get_shared_channel(2);
            float vmin, vmax;
            vmin = vchannel.min_max(vmax);
            vchannel.equalize(params.nb_levels, vmin, vmax);
            cimg_forXY(cimg, x, y) {
                float r, g, b;
                Color::hsv_to_rgb(cimg(x, y, 0, 0), cimg(x, y, 0, 1), cimg(x, y, 0, 2), &r, &g, &b);

                cimg(x, y, 0, 0) = r;
                cimg(x, y, 0, 1) = g;
                cimg(x, y, 0, 2) = b;
            }
        }
    }

    // version >= 3: cimg holds all the channels of the image (no component remapping)
    void renderAllChannels(const CImgHistEQParams& params,
                           cimg_library::CImg<cimgpix_t>& cimg)
    {
        const int nc = cimg.spectrum();
        const bool hasRGB = (nc >= 3);
        if ( hasRGB && (params.processR || params.processG || params.processB) ) {
            // HSV decomposition from R, G and B, whichever of them are processed
            cimg_library::CImg<cimgpix_t> h(cimg.width(), cimg.height(), 1, 1);
            cimg_library::CImg<cimgpix_t> s(cimg.width(), cimg.height(), 1, 1);
            cimg_library::CImg<cimgpix_t> v(cimg.width(), cimg.height(), 1, 1);
            cimg_pragma_openmp(parallel for if (cimg.size()>=1048576))
            cimg_forXY(cimg, x, y) {
                Color::rgb_to_hsv(cimg(x, y, 0, 0), cimg(x, y, 0, 1), cimg(x, y, 0, 2), &h(x, y), &s(x, y), &v(x, y));
            }
            float vmin, vmax;
            vmin = v.min_max(vmax);
            v.equalize(params.nb_levels, vmin, vmax);
            const bool process[3] = { params.processR, params.processG, params.processB };
            cimg_forXY(cimg, x, y) {
                float rgb[3];
                Color::hsv_to_rgb(h(x, y), s(x, y), v(x, y), &rgb[0], &rgb[1], &rgb[2]);
                // only write the processed channels, the others keep the source values
                for (int c = 0; c < 3; ++c) {
                    if (process[c]) {
                        cimg(x, y, 0, c) = rgb[c];
                    }
                }
            }
            // the alpha channel (if any) is kept, as in version 2

            return;
        }
        // no color channel processed (Alpha or XY images, or RGBA with only alpha processed):
        // equalize the processed channels together, as in version 2
        std::vector<int> channels;
        if (nc == 1) {
            if (params.processA) {
                channels.push_back(0);
            }
        } else if (nc == 2) {
            // XY images have no B channel
            if (params.processR) {
                channels.push_back(0);
            }
            if (params.processG) {
                channels.push_back(1);
            }
        } else if ( (nc == 4) && params.processA ) {
            channels.push_back(3);
        }
        if ( channels.empty() ) {
            return;
        }
        cimg_library::CImg<cimgpix_t> sub(cimg.width(), cimg.height(), 1, (int)channels.size());
        for (int i = 0; i < (int)channels.size(); ++i) {
            cimg_forXY(cimg, x, y) {
                sub(x, y, 0, i) = cimg(x, y, 0, channels[i]);
            }
        }
        float vmin, vmax;
        vmin = sub.min_max(vmax);
        sub.equalize(params.nb_levels, vmin, vmax);
        for (int i = 0; i < (int)channels.size(); ++i) {
            cimg_forXY(cimg, x, y) {
                cimg(x, y, 0, channels[i]) = sub(x, y, 0, i);
            }
        }
    }

    //virtual bool isIdentity(const IsIdentityArguments &args, const CImgHistEQParams& params) OVERRIDE FINAL
    //{
    //    return false;
    //};

private:

    // params
    IntParam *_nb_levels;
    bool _legacyChannels; // version < 3: the processed channels were used as if they were R, G, B
};


mDeclarePluginFactoryVersioned(CImgHistEQPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgHistEQPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsDeprecated(true);
    }
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    // version 2 processed channels as if they were R, G, B (kept so old projects render the same)
    desc.setPluginDescription(majorVersion < 3 ? kPluginDescriptionLegacy : kPluginDescription);

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

template<unsigned int majorVersion>
void
CImgHistEQPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                           ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgHistEQPlugin::describeInContextBegin(desc, context,
                                                                              kSupportsRGBA,
                                                                              kSupportsRGB,
                                                                              kSupportsXY,
                                                                              kSupportsAlpha,
                                                                              kSupportsTiles,
                                                                              /*processRGB=*/ true,
                                                                              /*processAlpha=*/ true,
                                                                              /*processIsSecret=*/ true);

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamNbLevels);
        param->setLabel(kParamNbLevelsLabel);
        param->setHint(kParamNbLevelsHint);
        param->setDefault(kParamNbLevelsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    CImgHistEQPlugin::describeInContextEnd(desc, context, page);
}

template<unsigned int majorVersion>
ImageEffect*
CImgHistEQPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new CImgHistEQPlugin(handle, majorVersion);
}

// version 2 is kept for backward compatibility
static CImgHistEQPluginFactory<2> p2(kPluginIdentifier, 0);
static CImgHistEQPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
