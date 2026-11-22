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
 * Cell layout and drawing shared by ContactSheet and LayerContactSheet.
 */

#ifndef openfx_misc_ContactSheetLayout_h
#define openfx_misc_ContactSheetLayout_h

#include <cassert>
#include <cstddef>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsCoords.h"
#include "ofxsFilter.h"

namespace OFX {
namespace ContactSheet {

// Compute the RoD of cell number "cell" in the rows x columns grid that covers rod.
// Return false if the cell is beyond the last row.
template<class Index>
inline bool
cellRoD(Index cell,
        int rows,
        int columns,
        bool topbottom,
        bool leftright,
        const OfxRectD& rod,
        OfxRectD* cellRect)
{
    int r = cell / columns;
    int c = cell % columns;
    if (r >= rows) {
        return false;
    }
    if (topbottom) {
        r = rows - 1 - r;
    }
    if (!leftright) {
        c = columns - 1 - c;
    }
    // now compute the four corners of the cell in the rod
    cellRect->x1 = rod.x1 + c * (rod.x2 - rod.x1) / columns;
    cellRect->y1 = rod.y1 + r * (rod.y2 - rod.y1) / rows;
    cellRect->x2 = rod.x1 + (c + 1) * (rod.x2 - rod.x1) / columns;
    cellRect->y2 = rod.y1 + (r + 1) * (rod.y2 - rod.y1) / rows;

    return true;
}

// Fit the source format in the cell, minus the gap.
// Return the scale factor in *f and the image area in *imageRoD.
inline void
fitRod(const OfxRectD& srcFormatCanonical,
       const OfxRectD& cellRoD,
       int gap,
       bool center,
       double* f,
       OfxRectD* imageRoD)
{
    double sw = srcFormatCanonical.x2 - srcFormatCanonical.x1;
    double sh = srcFormatCanonical.y2 - srcFormatCanonical.y1;
    OfxRectD cRoD = { cellRoD.x1 + gap / 2, cellRoD.y1 + gap/2, cellRoD.x2 - (gap + 1) / 2, cellRoD.y2 - (gap + 1) / 2};
    double cw = (std::max)(1., cRoD.x2 - cRoD.x1);
    double ch = (std::max)(1., cRoD.y2 - cRoD.y1);
    if ( (sw <= 0.) || (sh <= 0.) ) {
        // empty source format (e.g. a disconnected input): an empty image area, instead of
        // *f = ch / 0 and f * sw = inf * 0 = NaN
        *f = 1.;
        imageRoD->x1 = imageRoD->x2 = cRoD.x1;
        imageRoD->y1 = imageRoD->y2 = cRoD.y1;

        return;
    }
    bool fitwidth = sw * ch > sh * cw;
    *f = fitwidth ? (cw/sw) : (ch / sh);
    if (center) {
        imageRoD->x1 = cRoD.x1 + (cw - *f * sw) / 2;
        imageRoD->x2 = cRoD.x2 - (cw - *f * sw) / 2;
        imageRoD->y1 = cRoD.y1 + (ch - *f * sh) / 2;
        imageRoD->y2 = cRoD.y2 - (ch - *f * sh) / 2;
    } else {
        imageRoD->x1 = cRoD.x1;
        imageRoD->x2 = cRoD.x1 + *f * sw;
        imageRoD->y1 = cRoD.y1;
        imageRoD->y2 = cRoD.y1 + *f * sh;
    }
}

#define kContactSheetBitDepthUnsupportedMessage \
    "This plug-in only supports 32-bit floating-point images, but the host gave it images of another bit depth. " \
    "Please set the pixel format (bit depth) of the host project to 32-bit floating point."

// The plugins only declare eBitDepthFloat, and the pixels are accessed as float: check it in
// release builds too, since a host that ignores kOfxImageEffectPropSupportedPixelDepths
// (e.g. giving 8-bit images) would otherwise make drawImage() write past the end of the buffers.
// If img is not float, post an error message that explains it and throw kOfxStatFailed (the
// render action doc: "kOfxStatFailed, something wrong, but no error code appropriate, plugin to
// post message"). render() clears the message once the output image is float.
inline void
checkBitDepth(ImageEffect& effect,
              const Image& img)
{
    if (img.getPixelDepth() != eBitDepthFloat) {
        effect.setPersistentMessage(Message::eMessageError, "", kContactSheetBitDepthUnsupportedMessage);
        throwSuiteStatusException(kOfxStatFailed);
    }
}

// Resize the whole (float) src image into the imageRoD area of the (float) dst image.
// The caller must check both depths with checkBitDepth().
inline void
drawImage(const Image* src,
          Image* dst,
          const OfxRectD& imageRoD,
          const OfxPointD& renderScale,
          double dstPar)
{
    assert(dst->getPixelDepth() == eBitDepthFloat && src->getPixelDepth() == eBitDepthFloat);
    // the strides below are in floats: row bytes that are not a whole number of floats can't be addressed
    if ( (dst->getRowBytes() % (int)sizeof(float) != 0) || (src->getRowBytes() % (int)sizeof(float) != 0) ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    const OfxRectI& dstBounds = dst->getBounds();
    float* b = (float*)dst->getPixelData();
    const size_t bwidth = dstBounds.x2 - dstBounds.x1;
    const size_t bheight = dstBounds.y2 - dstBounds.y1;
    const size_t bxstride = dst->getPixelComponentCount();
    // rows may be padded, and row bytes may be negative (kOfxImagePropRowBytes): use them, not
    // width * components. getPixelData() is the first row in memory order (bounds.y1).
    const std::ptrdiff_t bystride = dst->getRowBytes() / (std::ptrdiff_t)sizeof(float);

    const OfxRectI& srcBounds = src->getBounds();
    const float* a = (const float*)src->getPixelData();
    const size_t awidth = srcBounds.x2 - srcBounds.x1;
    const size_t aheight = srcBounds.y2 - srcBounds.y1;
    const size_t axstride = src->getPixelComponentCount();
    const std::ptrdiff_t aystride = src->getRowBytes() / (std::ptrdiff_t)sizeof(float);
    const size_t depth = (std::min)(axstride, bxstride);
    const OfxRectD from = {0., 0., (double)awidth, (double)aheight};
    OfxRectI to;
    Coords::toPixelEnclosing(imageRoD, renderScale, dstPar, &to);
    to.x1 -= dstBounds.x1;
    to.y1 -= dstBounds.y1;
    to.x2 -= dstBounds.x1;
    to.y2 -= dstBounds.y1;

    ofxsFilterResize2d(a, awidth, aheight, axstride, aystride, depth,
                       from, /*zeroOutside=*/false,
                       b, bwidth, bheight, bxstride, bystride,
                       to);
}

} // namespace ContactSheet
} // namespace OFX

#endif // openfx_misc_ContactSheetLayout_h
