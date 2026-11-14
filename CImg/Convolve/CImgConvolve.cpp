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
#include <cstring>
#include <vector>
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

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ConvolveCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Convolve the Source input by the image in the Filter input (the convolution kernel), using the Fast Fourier Transform (FFT).\n" \
    "This can be used for example to simulate lens defocus (bokeh) with an arbitrary aperture shape, such as the one produced by the DiscBlades generator.\n" \
    "\n" \
    "Each channel of Source is convolved by the corresponding channel of Filter (or by the Filter luminance or alpha, see the 'Filter Channel' parameter). " \
    "The kernel is the Filter image inside its region of definition (or the bounding box of its non-zero pixels, if smaller). " \
    "If 'Normalize' is checked, the result is divided by the sum of the kernel values, so that the overall image brightness is preserved.\n" \
    "\n" \
    "By default, the kernel origin is the center of the Filter format (or of the project window if the host does not support formats). " \
    "The 'Filter Center' parameter can also center the kernel automatically on its centroid (the weighted mean position of its pixels, with weights equal to the absolute kernel values), or on the center of its region of definition.\n" \
    "\n" \
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
// version 1.0: initial version
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
#define kParamFilterChannelHint "Which channel(s) of the Filter input are used as the convolution kernel."
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

// kernel channel keys
enum KernelKeyEnum
{
    eKernelR = 0,
    eKernelG,
    eKernelB,
    eKernelA,
    eKernelLuminance,
    eKernelCount
};

static inline unsigned int
nextPowerOfTwo(unsigned int n)
{
    unsigned int p = 1;

    while (p < n) {
        p <<= 1;
    }

    return p;
}

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
            params.filterRoD = _filterClip->getRegionOfDefinition(time);
            params.par = _filterClip->getPixelAspectRatio();
            bool gotFormat = false;
#ifdef OFX_EXTENSIONS_NATRON
            OfxRectI format;
            _filterClip->getFormat(format);
            if ( !Coords::rectIsEmpty(format) ) {
                // the format is in pixel coordinates at render scale 1
                params.formatCenter.x = (format.x1 + format.x2) * 0.5 * params.par;
                params.formatCenter.y = (format.y1 + format.y2) * 0.5;
                gotFormat = true;
            }
#endif
            if (!gotFormat) {
                OfxPointD offset = getProjectOffset();
                OfxPointD size = getProjectSize();
                params.formatCenter.x = offset.x + size.x * 0.5;
                params.formatCenter.y = offset.y + size.y * 0.5;
            }
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

    // the Filter clip needs its whole region of definition
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                      RegionOfInterestSetter &rois) OVERRIDE FINAL
    {
        CImgFilterPluginHelper<CImgConvolveParams, false>::getRegionsOfInterest(args, rois);
        if ( _filterClip && _filterClip->isConnected() ) {
            OfxRectD filterRoD = _filterClip->getRegionOfDefinition(args.time);
            if ( !Coords::rectIsEmpty(filterRoD) ) {
                rois.setRegionOfInterest(*_filterClip, filterRoD);
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
        std::vector<int> channelKey(nc, eKernelLuminance);
        {
            const bool processR = _processR ? _processR->getValueAtTime(args.time) : true;
            const bool processG = _processG ? _processG->getValueAtTime(args.time) : true;
            const bool processB = _processB ? _processB->getValueAtTime(args.time) : true;
            const bool processA = _processA ? _processA->getValueAtTime(args.time) : true;
            if (nc == 1) {
                processChannel[0] = processA;
                channelKey[0] = eKernelA;
            } else {
                const bool p[4] = {processR, processG, processB, processA};
                for (int c = 0; c < nc && c < 4; ++c) {
                    processChannel[c] = p[c];
                    channelKey[c] = c; // eKernelR..eKernelA
                }
            }
        }

        OFX::auto_ptr<const Image> filter( _filterClip->fetchImage(args.time) );
        OfxRectI kernelRect, offsets;
        if ( !filter.get() || !getKernelOffsets(params, args.renderScale, &kernelRect, &offsets) ) {
            // empty kernel: the convolution is zero
            for (int c = 0; c < nc; ++c) {
                if (processChannel[c]) {
                    cimg.get_shared_channel(c).fill(0);
                }
            }

            return;
        }
        checkBadRenderScaleOrField(filter, args);
        if ( filter->getPixelDepth() != eBitDepthFloat ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        const int fnc = filter->getPixelComponentCount();
        // the kernel is the part of the filter image within its RoD
        OfxRectI filterBounds = filter->getBounds();
        if ( !Coords::rectIntersection(kernelRect, filterBounds, &kernelRect) ) {
            for (int c = 0; c < nc; ++c) {
                if (processChannel[c]) {
                    cimg.get_shared_channel(c).fill(0);
                }
            }

            return;
        }

        // map the source channel keys to the kernel channel keys, given the filter components
        for (int c = 0; c < nc; ++c) {
            int key = channelKey[c];
            switch ( (FilterChannelEnum)params.filterChannel ) {
            case eFilterChannelLuminance:
                key = eKernelLuminance;
                break;
            case eFilterChannelAlpha:
                key = eKernelA;
                break;
            case eFilterChannelRGBA:
            default:
                break;
            }
            if (fnc == 1) {
                key = eKernelA; // alpha-only filter: use it for all channels
            } else if ( (key == eKernelA) && (fnc != 4) ) {
                key = eKernelLuminance; // no alpha in filter
            } else if ( (key == eKernelB) && (fnc == 2) ) {
                key = eKernelLuminance; // no blue in XY filter
            }
            channelKey[c] = key;
        }
        bool keyUsed[eKernelCount] = {false, false, false, false, false};
        for (int c = 0; c < nc; ++c) {
            if (processChannel[c]) {
                keyUsed[channelKey[c]] = true;
            }
        }

        // extract the kernels
        int kw = kernelRect.x2 - kernelRect.x1;
        int kh = kernelRect.y2 - kernelRect.y1;
        std::vector<cimg_library::CImg<cimgpix_t> > kernels(eKernelCount);
        for (int k = 0; k < eKernelCount; ++k) {
            if (keyUsed[k]) {
                kernels[k].assign(kw, kh, 1, 1, 0);
            }
        }
        for (int y = kernelRect.y1; y < kernelRect.y2; ++y) {
            for (int x = kernelRect.x1; x < kernelRect.x2; ++x) {
                const float *pix = (const float*)filter->getPixelAddress(x, y);
                if (!pix) {
                    continue;
                }
                for (int k = 0; k < eKernelCount; ++k) {
                    if (keyUsed[k]) {
                        kernels[k](x - kernelRect.x1, y - kernelRect.y1) = kernelValue(pix, fnc, k);
                    }
                }
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
                for (int k = 0; k < eKernelCount; ++k) {
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
            for (int c = 0; c < nc; ++c) {
                if (processChannel[c]) {
                    cimg.get_shared_channel(c).fill(0);
                }
            }

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
            for (int k = 0; k < eKernelCount; ++k) {
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

        // FFT size: power of two, large enough to hold the linear convolution
        const int w = cimg.width();
        const int h = cimg.height();
        const unsigned int fw = nextPowerOfTwo(w + kw - 1);
        const unsigned int fh = nextPowerOfTwo(h + kh - 1);
        cimg_library::CImg<cimgpix_t> kre, kim, sre, sim;

        for (int k = 0; k < eKernelCount; ++k) {
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

            // FFT of the kernel, placed at the origin
            kre.assign(fw, fh, 1, 1, 0);
            kim.assign(fw, fh, 1, 1, 0);
            kre.draw_image(0, 0, kernels[k]);
            if (scale != 1.) {
                kre *= (cimgpix_t)scale;
            }
            cimg_library::CImg<cimgpix_t>::FFT(kre, kim, false);
            if ( abort() ) {
                return;
            }

            // process the channels using this kernel, two by two (as real and imaginary parts,
            // since the kernel is real, the convolution of a complex signal is the complex combination
            // of the convolutions of its real and imaginary parts)
            std::vector<int> chans;
            for (int c = 0; c < nc; ++c) {
                if (processChannel[c] && (channelKey[c] == k)) {
                    chans.push_back(c);
                }
            }
            for (std::size_t i = 0; i < chans.size(); i += 2) {
                const int c0 = chans[i];
                const int c1 = (i + 1 < chans.size()) ? chans[i + 1] : -1;
                sre.assign(fw, fh, 1, 1, 0);
                sim.assign(fw, fh, 1, 1, 0);
                sre.draw_image( 0, 0, cimg.get_shared_channel(c0) );
                if (c1 >= 0) {
                    sim.draw_image( 0, 0, cimg.get_shared_channel(c1) );
                }
                cimg_library::CImg<cimgpix_t>::FFT(sre, sim, false);
                if ( abort() ) {
                    return;
                }
                // complex multiplication
                {
                    cimgpix_t *pr = sre.data();
                    cimgpix_t *pi = sim.data();
                    const cimgpix_t *qr = kre.data();
                    const cimgpix_t *qi = kim.data();
                    for (std::size_t n = (std::size_t)fw * fh; n; --n, ++pr, ++pi, ++qr, ++qi) {
                        const cimgpix_t a = *pr, b = *pi;
                        *pr = a * *qr - b * *qi;
                        *pi = a * *qi + b * *qr;
                    }
                }
                cimg_library::CImg<cimgpix_t>::FFT(sre, sim, true);
                if ( abort() ) {
                    return;
                }
                // out(i) = L(i - m), where L is the linear convolution of the source with the
                // kernel placed at the origin.
                for (int y = 0; y < h; ++y) {
                    const int ly = y - my;
                    const bool yin = (0 <= ly && ly < (int)fh);
                    cimgpix_t *d0 = cimg.data(0, y, 0, c0);
                    cimgpix_t *d1 = (c1 >= 0) ? cimg.data(0, y, 0, c1) : NULL;
                    for (int x = 0; x < w; ++x) {
                        const int lx = x - mx;
                        if ( yin && (0 <= lx) && ( lx < (int)fw) ) {
                            d0[x] = sre(lx, ly);
                            if (d1) {
                                d1[x] = sim(lx, ly);
                            }
                        } else {
                            d0[x] = 0;
                            if (d1) {
                                d1[x] = 0;
                            }
                        }
                    }
                }
            }
        }
    } // render

    virtual bool isIdentity(const IsIdentityArguments & /*args*/,
                            const CImgConvolveParams& params) OVERRIDE FINAL
    {
        return !params.filterConnected;
    };

private:
    static inline float kernelValue(const float* pix,
                                    int fnc,
                                    int key)
    {
        if (fnc == 1) {
            return pix[0];
        }
        switch (key) {
        case eKernelR:
            return pix[0];
        case eKernelG:
            return pix[1];
        case eKernelB:
            return (fnc >= 3) ? pix[2] : 0.f;
        case eKernelA:
            return (fnc >= 4) ? pix[3] : 0.f;
        case eKernelLuminance:
        default:
            if (fnc >= 3) {
                return 0.2126f * pix[0] + 0.7152f * pix[1] + 0.0722f * pix[2];
            }

            return pix[0];
        }
    }

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_filterClip;
    // params
    ChoiceParam *_filterChannel;
    BooleanParam *_normalize;
    ChoiceParam *_center;
};


mDeclarePluginFactory(CImgConvolvePluginFactory, {ofxsThreadSuiteCheck();}, {});

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
