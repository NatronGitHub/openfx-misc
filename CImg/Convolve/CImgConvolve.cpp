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
 * OFX CImgConvolve plugin.
 * FFT-based convolution of the Source input by the image in the Filter input.
 */

#include <memory>
#include <cmath>
#include <vector>
#include <algorithm>

#include "ofxsMacros.h"
#include "ofxsCoords.h"

#include "CImgFilter.h"
#include "CImgFFTConvolve.h" // shared with ZDefocusCImg

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ConvolveCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Convolve the Source input by the image in the Filter input (the convolution kernel), using the Fast Fourier Transform (FFT).\n" \
    "This can be used for example to simulate lens defocus (bokeh) with an arbitrary aperture shape, such as the one produced by the DiscBlades generator.\n" \
    "\n" \
    "Each channel of Source is convolved by the corresponding channel of Filter (or by the Filter luminance or alpha, see the 'Filter Channel' parameter). " \
    "With the default Filter Channel (RGBA), a colored Filter thus gives a different kernel for each channel; to use a single kernel for all channels, set Filter Channel to Alpha. " \
    "The kernel is the Filter image inside its region of definition (or the bounding box of its non-zero pixels, if smaller). " \
    "If 'Normalize' is checked, the result is divided by the sum of the kernel values, so that the overall image brightness is preserved.\n" \
    "\n" \
    "By default, the kernel origin is the center of the Filter format (or of the project window if the host does not support formats). " \
    "The 'Filter Center' parameter can also center the kernel automatically on its centroid (the weighted mean position of its pixels, with weights equal to the absolute kernel values), or on the center of its region of definition.\n" \
    "\n" \
    "An infinite Filter region of definition (e.g. from a Constant generator) is clamped to the Filter format (or project window).\n" \
    "The output region of definition is the Source region of definition, enlarged by the kernel extent. Pixels outside of the Source region of definition are considered black and transparent.\n" \
    "At a render scale lower than 1 (e.g. proxy mode), the Filter image is fetched at the same render scale as Source.\n" \
    "If the Filter input is not connected, Source is passed through.\n" \
    "\n" \
    "Uses the 'FFT' function from the CImg library (power-of-two sizes, the image is zero-padded).\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgConvolve"
// History:
// version 1.0: initial version; in the general context, Output has the components of Source (with an RGB
//              Source and an RGBA Filter, Natron gave an RGBA Output, and the render failed) (issue #251)
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 0 // the cimg channels are the source channels, in the same order
// Tiles are supported: the source RoI is the render window enlarged by the kernel extent, and
// the FFT is computed on the zero-padded RoI, which gives the exact linear convolution on the render window.
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

#define kDefaultUnpremult false // convolution is linear: work on premultiplied RGBA by default
#define kDefaultProcessAlpha true

#define kClipFilter "Filter"
#define kClipFilterHint "The convolution kernel image."

#define kParamFilterChannel "filterChannel"
#define kParamFilterChannelLabel "Filter Channel"
#define kParamFilterChannelHint "Which channel(s) of the Filter input are used as the convolution kernel. With RGBA (the default), a colored Filter gives a different kernel for each channel; Alpha uses a single kernel for all channels."
#define kParamFilterChannelOptionRGBA "RGBA", "Each Source channel is convolved by the same channel of Filter. If Filter has a single (alpha) channel, it is used for all Source channels. If Filter has no alpha channel, the Source alpha is convolved by the Filter luminance.", "rgba"
#define kParamFilterChannelOptionLuminance "Luminance", "All Source channels are convolved by the Filter luminance (Rec.709 luminance of RGB, or alpha if Filter has a single channel).", "luminance"
#define kParamFilterChannelOptionAlpha "Alpha", "All Source channels are convolved by the Filter alpha (or by its luminance if Filter has no alpha channel).", "alpha"
enum FilterChannelEnum
{
    eFilterChannelRGBA = 0,
    eFilterChannelLuminance,
    eFilterChannelAlpha,
};
#define kParamFilterChannelDefault eFilterChannelRGBA

#define kParamNormalize "normalize"
#define kParamNormalizeLabel "Normalize"
#define kParamNormalizeHint "Divide the result by the sum of the kernel values (computed separately for each kernel channel), so that the overall brightness is preserved. Normalization is not applied to a kernel channel whose sum is zero (e.g. an edge-detection kernel). If unchecked, the kernel values are used as-is (and are compensated for the render scale)."
#define kParamNormalizeDefault true

#define kParamCenter "filterCenter"
#define kParamCenterLabel "Filter Center"
#define kParamCenterHint "Position in the Filter image that corresponds to the kernel origin (i.e. which does not shift the Source image)."
#define kParamCenterOptionFormat "Format", "Center of the Filter format (or of the project window if the host does not support formats).", "format"
#define kParamCenterOptionCentroid "Auto (Centroid)", "Center the kernel automatically on its centroid (weighted mean position of the kernel pixels, with weights equal to the absolute kernel values). Since the centroid is only known at render time, the output region of definition is enlarged by the full kernel size in each direction.", "centroid"
#define kParamCenterOptionRoD "Auto (RoD Center)", "Center the kernel automatically on the center of the Filter region of definition.", "rod"
enum CenterEnum
{
    eCenterFormat = 0,
    eCenterCentroid,
    eCenterRoD,
};
#define kParamCenterDefault eCenterFormat

/// Convolve plugin
struct CImgConvolveParams
{
    bool filterConnected;
    OfxRectD filterRoD; // canonical coordinates
    OfxPointD formatCenter; // canonical coordinates
    double par;
    int filterChannel;
    int center;
    bool normalize;
};

class CImgConvolvePlugin
    : public CImgFilterPluginHelper<CImgConvolveParams, false>
{
public:

    CImgConvolvePlugin(OfxImageEffectHandle handle)
        : CImgFilterPluginHelper<CImgConvolveParams, false>(handle, /*usesMask=*/false, kSupportsComponentRemapping, kSupportsTiles, kSupportsMultiResolution, kSupportsRenderScale, kDefaultUnpremult)
    {
        _filterClip = fetchClip(kClipFilter);
        _filterChannel = fetchChoiceParam(kParamFilterChannel);
        _normalize = fetchBooleanParam(kParamNormalize);
        _center = fetchChoiceParam(kParamCenter);
        assert(_filterClip && _filterChannel && _normalize && _center);
    }

    virtual void getValuesAtTime(double time,
                                 CImgConvolveParams& params) OVERRIDE FINAL
    {
        params.filterConnected = _filterClip && _filterClip->isConnected();
        params.filterRoD.x1 = params.filterRoD.y1 = params.filterRoD.x2 = params.filterRoD.y2 = 0.;
        params.par = 1.;
        params.formatCenter.x = params.formatCenter.y = 0.;
        if (params.filterConnected) {
            params.par = _filterClip->getPixelAspectRatio();
            // Filter RoD (clamped to the format if infinite, e.g. from a Constant generator) and format center
            cimgGetFilterRoD(*this, _filterClip, time, &params.filterRoD, &params.formatCenter);
        }
        params.filterChannel = _filterChannel->getValueAtTime(time);
        params.center = _center->getValueAtTime(time);
        params.normalize = _normalize->getValueAtTime(time);
    }

    // Compute the kernel rectangle (in pixel coordinates at the given render scale) and the
    // range of offsets (inclusive) between an output pixel and the source pixels it depends on.
    // Returns false if the kernel is empty.
    static bool getKernelOffsets(const CImgConvolveParams& params,
                                 const OfxPointD& renderScale,
                                 OfxRectI* kernelRect,
                                 OfxRectI* offsets)
    {
        if ( !params.filterConnected || Coords::rectIsEmpty(params.filterRoD) ) {
            return false;
        }
        Coords::toPixelEnclosing(params.filterRoD, renderScale, params.par, kernelRect);
        if ( Coords::rectIsEmpty(*kernelRect) ) {
            return false;
        }
        if (params.center == eCenterCentroid) {
            // the centroid is only known at render time, but it is within the kernel rectangle
            // (the weights are non-negative): be conservative.
            const int kw = kernelRect->x2 - kernelRect->x1;
            const int kh = kernelRect->y2 - kernelRect->y1;
            offsets->x1 = -(kw - 1);
            offsets->x2 = kw - 1;
            offsets->y1 = -(kh - 1);
            offsets->y2 = kh - 1;
        } else {
            int cx, cy;
            getKernelCenter(params, renderScale, *kernelRect, &cx, &cy);
            offsets->x1 = kernelRect->x1 - cx;
            offsets->x2 = kernelRect->x2 - 1 - cx;
            offsets->y1 = kernelRect->y1 - cy;
            offsets->y2 = kernelRect->y2 - 1 - cy;
        }

        return true;
    }

    // kernel center pixel, for the format and RoD center modes
    static void getKernelCenter(const CImgConvolveParams& params,
                                const OfxPointD& renderScale,
                                const OfxRectI& kernelRect,
                                int* cx,
                                int* cy)
    {
        if (params.center == eCenterRoD) {
            *cx = (int)std::floor( (kernelRect.x1 + kernelRect.x2) * 0.5 );
            *cy = (int)std::floor( (kernelRect.y1 + kernelRect.y2) * 0.5 );
        } else {
            // the pixel which contains the format center
            *cx = (int)std::floor(params.formatCenter.x * renderScale.x / params.par);
            *cy = (int)std::floor(params.formatCenter.y * renderScale.y);
        }
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgConvolveParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        OfxRectI kernelRect, offsets;

        if ( !getKernelOffsets(params, renderScale, &kernelRect, &offsets) ) {
            *roi = rect;

            return;
        }
        // output pixel p depends on source pixels p - d, for d in offsets
        roi->x1 = rect.x1 - offsets.x2;
        roi->x2 = rect.x2 - offsets.x1;
        roi->y1 = rect.y1 - offsets.y2;
        roi->y2 = rect.y2 - offsets.y1;
    }

    virtual bool getRegionOfDefinition(const OfxRectI& srcRoD,
                                       const OfxPointD& renderScale,
                                       const CImgConvolveParams& params,
                                       OfxRectI* dstRoD) OVERRIDE FINAL
    {
        OfxRectI kernelRect, offsets;

        if ( Coords::rectIsEmpty(srcRoD) || !getKernelOffsets(params, renderScale, &kernelRect, &offsets) ) {
            return false; // use default (source RoD)
        }
        if ( Coords::rectIsInfinite(srcRoD) ) {
            return false;
        }
        dstRoD->x1 = srcRoD.x1 + offsets.x1;
        dstRoD->x2 = srcRoD.x2 + offsets.x2;
        dstRoD->y1 = srcRoD.y1 + offsets.y1;
        dstRoD->y2 = srcRoD.y2 + offsets.y2;

        return true;
    }

    // the Filter clip needs its whole region of definition (clamped to its format if infinite)
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                      RegionOfInterestSetter &rois) OVERRIDE FINAL
    {
        CImgFilterPluginHelper<CImgConvolveParams, false>::getRegionsOfInterest(args, rois);
        if ( _filterClip && _filterClip->isConnected() ) {
            // use the same (possibly clamped) Filter RoD as getKernelOffsets()
            CImgConvolveParams params{};
            getValuesAtTime(args.time, params);
            if ( !Coords::rectIsEmpty(params.filterRoD) ) {
                rois.setRegionOfInterest(*_filterClip, params.filterRoD);
            }
        }
    }

    virtual void render(const RenderArguments &args,
                        const CImgConvolveParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int /*alphaChannel*/) OVERRIDE FINAL
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        if (!params.filterConnected) {
            return; // identity
        }
        const int nc = cimg.spectrum();
        if ( (nc == 0) || cimg.is_empty() ) {
            return;
        }

        // the channels to process (the cimg channels are the source components, see kSupportsComponentRemapping)
        std::vector<bool> processChannel(nc, true);
        std::vector<int> channelKey(nc, eCImgKernelLuminance);
        {
            const bool processR = _processR ? _processR->getValueAtTime(args.time) : true;
            const bool processG = _processG ? _processG->getValueAtTime(args.time) : true;
            const bool processB = _processB ? _processB->getValueAtTime(args.time) : true;
            const bool processA = _processA ? _processA->getValueAtTime(args.time) : true;
            if (nc == 1) {
                processChannel[0] = processA;
                channelKey[0] = eCImgKernelA;
            } else {
                const bool p[4] = {processR, processG, processB, processA};
                for (int c = 0; c < nc && c < 4; ++c) {
                    processChannel[c] = p[c];
                    channelKey[c] = c; // eCImgKernelR..eCImgKernelA
                }
            }
        }

        OFX::auto_ptr<const Image> filter( _filterClip->fetchImage(args.time) );
        OfxRectI kernelRect, offsets;
        if ( !filter.get() || !getKernelOffsets(params, args.renderScale, &kernelRect, &offsets) ) {
            // empty kernel: the convolution is zero
            zeroChannels(processChannel, cimg);

            return;
        }
        checkBadRenderScaleOrField(filter, args);
        if ( filter->getPixelDepth() != eBitDepthFloat ) {
            throwBitDepthUnsupported();
        }
        const int fnc = filter->getPixelComponentCount();
        // the kernel is the part of the filter image within its RoD
        OfxRectI filterBounds = filter->getBounds();
        if ( !Coords::rectIntersection(kernelRect, filterBounds, &kernelRect) ) {
            zeroChannels(processChannel, cimg);

            return;
        }

        // map the source channel keys to the kernel channel keys, given the filter components
        for (int c = 0; c < nc; ++c) {
            int key = channelKey[c];
            switch ( (FilterChannelEnum)params.filterChannel ) {
            case eFilterChannelLuminance:
                key = eCImgKernelLuminance;
                break;
            case eFilterChannelAlpha:
                key = eCImgKernelA;
                break;
            case eFilterChannelRGBA:
            default:
                break;
            }
            if (fnc == 1) {
                key = eCImgKernelA; // alpha-only filter: use it for all channels
            } else if ( (key == eCImgKernelA) && (fnc != 4) ) {
                key = eCImgKernelLuminance; // no alpha in filter
            } else if ( (key == eCImgKernelB) && (fnc == 2) ) {
                key = eCImgKernelLuminance; // no blue in XY filter
            }
            channelKey[c] = key;
        }
        bool keyUsed[eCImgKernelCount] = {false, false, false, false, false};
        for (int c = 0; c < nc; ++c) {
            if (processChannel[c]) {
                keyUsed[channelKey[c]] = true;
            }
        }

        // extract the kernels
        int kw = kernelRect.x2 - kernelRect.x1;
        int kh = kernelRect.y2 - kernelRect.y1;
        std::vector<cimg_library::CImg<cimgpix_t> > kernels(eCImgKernelCount);
        for (int k = 0; k < eCImgKernelCount; ++k) {
            if (keyUsed[k]) {
                cimgExtractKernel(*filter, kernelRect, k, &kernels[k]);
            }
        }
        filter.reset(NULL);
        if ( abort() ) {
            return;
        }

        // shrink the kernel to the bounding box of its non-zero pixels, and compute its centroid
        int bx1 = kw, bx2 = -1, by1 = kh, by2 = -1;
        double sumw = 0., sumwx = 0., sumwy = 0.;
        for (int y = 0; y < kh; ++y) {
            for (int x = 0; x < kw; ++x) {
                double w = 0.;
                for (int k = 0; k < eCImgKernelCount; ++k) {
                    if (keyUsed[k]) {
                        w += std::abs( (double)kernels[k](x, y) );
                    }
                }
                if (w > 0.) {
                    bx1 = (std::min)(bx1, x);
                    bx2 = (std::max)(bx2, x);
                    by1 = (std::min)(by1, y);
                    by2 = (std::max)(by2, y);
                    sumw += w;
                    sumwx += w * (x + 0.5);
                    sumwy += w * (y + 0.5);
                }
            }
        }
        if (bx2 < bx1) {
            // kernel is zero
            zeroChannels(processChannel, cimg);

            return;
        }

        // kernel center pixel
        int cx, cy;
        if (params.center == eCenterCentroid) {
            cx = kernelRect.x1 + (int)std::floor(sumwx / sumw);
            cy = kernelRect.y1 + (int)std::floor(sumwy / sumw);
        } else {
            getKernelCenter(params, args.renderScale, kernelRect, &cx, &cy);
        }

        // crop the kernels
        if ( (bx1 > 0) || (by1 > 0) || (bx2 < kw - 1) || (by2 < kh - 1) ) {
            for (int k = 0; k < eCImgKernelCount; ++k) {
                if (keyUsed[k]) {
                    kernels[k].crop(bx1, by1, bx2, by2);
                }
            }
            kernelRect.x1 += bx1;
            kernelRect.y1 += by1;
            kernelRect.x2 = kernelRect.x1 + (bx2 - bx1 + 1);
            kernelRect.y2 = kernelRect.y1 + (by2 - by1 + 1);
            kw = kernelRect.x2 - kernelRect.x1;
            kh = kernelRect.y2 - kernelRect.y1;
        }
        // offset of the first kernel pixel w.r.t. the kernel center
        const int mx = kernelRect.x1 - cx;
        const int my = kernelRect.y1 - cy;

        for (int k = 0; k < eCImgKernelCount; ++k) {
            if (!keyUsed[k]) {
                continue;
            }
            // normalization
            double scale = 1.;
            if (params.normalize) {
                double sum = kernels[k].sum();
                if (sum != 0.) {
                    scale = 1. / sum;
                }
            } else {
                // a filter image fetched at a lower render scale has fewer pixels: preserve the kernel sum
                scale = 1. / (args.renderScale.x * args.renderScale.y);
            }
            // the channels using this kernel
            std::vector<int> chans;
            for (int c = 0; c < nc; ++c) {
                if (processChannel[c] && (channelKey[c] == k)) {
                    chans.push_back(c);
                }
            }
            if ( !cimgConvolveFFT(cimg, chans, kernels[k], scale, mx, my, *this) ) {
                return; // aborted
            }
        }
    } // render

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgConvolveParams& params) OVERRIDE FINAL
    {
        return !params.filterConnected;
    }

private:
    // The result of a convolution by an empty or all-zero kernel is zero: clear the processed channels.
    static void zeroChannels(const std::vector<bool>& processChannel,
                             cimg_library::CImg<cimgpix_t>& cimg)
    {
        for (int c = 0; c < cimg.spectrum(); ++c) {
            if (processChannel[c]) {
                cimg.get_shared_channel(c).fill(0);
            }
        }
    }

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_filterClip;
    // params
    ChoiceParam *_filterChannel;
    BooleanParam *_normalize;
    ChoiceParam *_center;
};


mDeclarePluginFactory(CImgConvolvePluginFactory, {ofxsThreadSuiteCheck(); cimgFFTLoad();}, {});

void
CImgConvolvePluginFactory::describe(ImageEffectDescriptor& desc)
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
CImgConvolvePluginFactory::describeInContext(ImageEffectDescriptor& desc,
                                             ContextEnum context)
{
    // create the clips and params
    PageParamDescriptor *page = CImgConvolvePlugin::describeInContextBegin(desc, context,
                                                                           kSupportsRGBA,
                                                                           kSupportsRGB,
                                                                           kSupportsXY,
                                                                           kSupportsAlpha,
                                                                           kSupportsTiles,
                                                                           /*processRGB=*/ true,
                                                                           /*processAlpha*/ kDefaultProcessAlpha,
                                                                           /*processIsSecret=*/ false);

    {
        ClipDescriptor *filterClip = desc.defineClip(kClipFilter);
        filterClip->setHint(kClipFilterHint);
        filterClip->addSupportedComponent(ePixelComponentRGBA);
        filterClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        filterClip->addSupportedComponent(ePixelComponentXY);
#endif
        filterClip->addSupportedComponent(ePixelComponentAlpha);
        filterClip->setTemporalClipAccess(false);
        filterClip->setOptional(true);
        filterClip->setSupportsTiles(kSupportsTiles);
        filterClip->setIsMask(false);
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilterChannel);
        param->setLabel(kParamFilterChannelLabel);
        param->setHint(kParamFilterChannelHint);
        assert(param->getNOptions() == eFilterChannelRGBA);
        param->appendOption(kParamFilterChannelOptionRGBA);
        assert(param->getNOptions() == eFilterChannelLuminance);
        param->appendOption(kParamFilterChannelOptionLuminance);
        assert(param->getNOptions() == eFilterChannelAlpha);
        param->appendOption(kParamFilterChannelOptionAlpha);
        param->setDefault( (int)kParamFilterChannelDefault );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamNormalize);
        param->setLabel(kParamNormalizeLabel);
        param->setHint(kParamNormalizeHint);
        param->setDefault(kParamNormalizeDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamCenter);
        param->setLabel(kParamCenterLabel);
        param->setHint(kParamCenterHint);
        assert(param->getNOptions() == eCenterFormat);
        param->appendOption(kParamCenterOptionFormat);
        assert(param->getNOptions() == eCenterCentroid);
        param->appendOption(kParamCenterOptionCentroid);
        assert(param->getNOptions() == eCenterRoD);
        param->appendOption(kParamCenterOptionRoD);
        param->setDefault( (int)kParamCenterDefault );
        if (page) {
            page->addChild(*param);
        }
    }

    CImgConvolvePlugin::describeInContextEnd(desc, context, page);
} // CImgConvolvePluginFactory::describeInContext

ImageEffect*
CImgConvolvePluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    return new CImgConvolvePlugin(handle);
}

static CImgConvolvePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
