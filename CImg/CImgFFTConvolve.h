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

//
//  CImgFFTConvolve.h
//
//  Helpers for the plugins that convolve an image by a kernel given by a "Filter" input image,
//  using the FFT: ConvolveCImg and ZDefocusCImg (custom aperture).
//

#ifndef Misc_CImgFFTConvolve_h
#define Misc_CImgFFTConvolve_h

#include <vector>

#include "CImgFilter.h" // CImg.h, cimgpix_t

// kernel channels that can be extracted from a Filter pixel
enum CImgKernelChannelEnum
{
    eCImgKernelR = 0,
    eCImgKernelG,
    eCImgKernelB,
    eCImgKernelA,
    eCImgKernelLuminance,
    eCImgKernelCount
};

// Value of the given kernel channel in a Filter pixel with fnc components.
// A single-channel (alpha) Filter gives the same value for all channels.
static inline float
cimgKernelValue(const float* pix,
                int fnc,
                int key)
{
    if (fnc == 1) {
        return pix[0];
    }
    switch (key) {
    case eCImgKernelR:
        return pix[0];
    case eCImgKernelG:
        return pix[1];
    case eCImgKernelB:
        return (fnc >= 3) ? pix[2] : 0.f;
    case eCImgKernelA:
        return (fnc >= 4) ? pix[3] : 0.f;
    case eCImgKernelLuminance:
    default:
        if (fnc >= 3) {
            return 0.2126f * pix[0] + 0.7152f * pix[1] + 0.0722f * pix[2];
        }

        return pix[0];
    }
}

// Extract the given kernel channel of the Filter image within rect (pixel coordinates).
// Pixels outside of the Filter image bounds are zero.
static inline void
cimgExtractKernel(const OFX::Image& filter,
                  const OfxRectI& rect,
                  int key,
                  cimg_library::CImg<cimgpix_t>* kernel)
{
    const int fnc = filter.getPixelComponentCount();

    kernel->assign(rect.x2 - rect.x1, rect.y2 - rect.y1, 1, 1, 0);
    for (int y = rect.y1; y < rect.y2; ++y) {
        for (int x = rect.x1; x < rect.x2; ++x) {
            const float *pix = (const float*)filter.getPixelAddress(x, y);
            if (pix) {
                (*kernel)(x - rect.x1, y - rect.y1) = cimgKernelValue(pix, fnc, key);
            }
        }
    }
}

// Region of definition of the Filter clip (canonical coordinates), and the center of its format.
// The format is the project window if the host does not support formats.
// An infinite RoD (e.g. from a Constant generator) is clamped to the format, so that the kernel is finite.
static inline void
cimgGetFilterRoD(const OFX::ImageEffect& effect,
                 OFX::Clip* filterClip,
                 double time,
                 OfxRectD* rod,
                 OfxPointD* formatCenter)
{
    *rod = filterClip->getRegionOfDefinition(time);
    const double par = filterClip->getPixelAspectRatio();
    OfxRectD formatRect;
    bool gotFormat = false;
#ifdef OFX_EXTENSIONS_NATRON
    OfxRectI format;
    filterClip->getFormat(format);
    if ( !OFX::Coords::rectIsEmpty(format) ) {
        // the format is in pixel coordinates at render scale 1
        const OfxPointD rs1 = {1., 1.};
        OFX::Coords::toCanonical(format, rs1, par, &formatRect);
        gotFormat = true;
    }
#endif
    if (!gotFormat) {
        OfxPointD offset = effect.getProjectOffset();
        OfxPointD size = effect.getProjectSize();
        formatRect.x1 = offset.x;
        formatRect.x2 = offset.x + size.x;
        formatRect.y1 = offset.y;
        formatRect.y2 = offset.y + size.y;
    }
    formatCenter->x = (formatRect.x1 + formatRect.x2) * 0.5;
    formatCenter->y = (formatRect.y1 + formatRect.y2) * 0.5;
    if ( OFX::Coords::rectIsInfinite(*rod) ) {
        if ( !OFX::Coords::rectIntersection(*rod, formatRect, rod) ) {
            rod->x1 = rod->y1 = rod->x2 = rod->y2 = 0.;
        }
    }
}

// Convolve the channels chans of img (in place) by kernel * scale, using the FFT.
// (mx, my) is the offset of the first kernel pixel w.r.t. the kernel origin (the pixel that does
// not shift the image). Pixels outside of img are zero: the FFT is computed on the zero-padded
// image, which gives the exact linear convolution.
// Returns false if the effect was aborted (img is then partially processed).
static inline bool
cimgConvolveFFT(cimg_library::CImg<cimgpix_t>& img,
                const std::vector<int>& chans,
                const cimg_library::CImg<cimgpix_t>& kernel,
                double scale,
                int mx,
                int my,
                const OFX::ImageEffect& effect)
{
    const int w = img.width();
    const int h = img.height();
    const int kw = kernel.width();
    const int kh = kernel.height();
    // FFT size: power of two, large enough to hold the linear convolution
    const unsigned int fw = (unsigned int)cimg_library::cimg::nearest_pow2( (unsigned int)(w + kw - 1) );
    const unsigned int fh = (unsigned int)cimg_library::cimg::nearest_pow2( (unsigned int)(h + kh - 1) );

    // FFT of the kernel, placed at the origin
    cimg_library::CImg<cimgpix_t> kre(fw, fh, 1, 1, 0), kim(fw, fh, 1, 1, 0);
    kre.draw_image(0, 0, kernel);
    if (scale != 1.) {
        kre *= (cimgpix_t)scale;
    }
    cimg_library::CImg<cimgpix_t>::FFT(kre, kim, false);
    if ( effect.abort() ) {
        return false;
    }

    // process the channels two by two (as real and imaginary parts:
    // since the kernel is real, the convolution of a complex signal is the complex combination
    // of the convolutions of its real and imaginary parts)
    cimg_library::CImg<cimgpix_t> sre, sim;
    for (std::size_t i = 0; i < chans.size(); i += 2) {
        const int c0 = chans[i];
        const int c1 = (i + 1 < chans.size()) ? chans[i + 1] : -1;
        sre.assign(fw, fh, 1, 1, 0);
        sim.assign(fw, fh, 1, 1, 0);
        sre.draw_image( 0, 0, img.get_shared_channel(c0) );
        if (c1 >= 0) {
            sim.draw_image( 0, 0, img.get_shared_channel(c1) );
        }
        cimg_library::CImg<cimgpix_t>::FFT(sre, sim, false);
        if ( effect.abort() ) {
            return false;
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
        if ( effect.abort() ) {
            return false;
        }
        // out(i) = L(i - m), where L is the linear convolution of the image with the
        // kernel placed at the origin.
        for (int y = 0; y < h; ++y) {
            const int ly = y - my;
            const bool yin = (0 <= ly && ly < (int)fh);
            cimgpix_t *d0 = img.data(0, y, 0, c0);
            cimgpix_t *d1 = (c1 >= 0) ? img.data(0, y, 0, c1) : NULL;
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

    return true;
} // cimgConvolveFFT

#endif // Misc_CImgFFTConvolve_h
