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
#include <cmath>

#include "ofxsImageEffect.h"
#include "ofxsCoords.h"
#include "ofxsFilter.h"

namespace OFX {
namespace ContactSheet {

// The order in which the cells of the grid are populated.
enum class ColumnOrder
{
    eLeftRight, // every row from left to right
    eRightLeft, // every row from right to left
    eSnake,     // alternate directions, starting from left to right
};

// The grid of cells that covers the output RoD, and the order of its cells. Cell number n is in
// the populated row n / columns (row 0 is populated first: the bottom row, or the top row if
// topBottom), at column n % columns of that row in the order given by columnOrder.
struct Grid
{
    int rows = 1;
    int columns = 1;
    bool topBottom = false; // rows are populated from the top downward (else from the bottom upward)
    ColumnOrder columnOrder = ColumnOrder::eLeftRight;
    // vertical shift of all the cells, in rows (up if positive): non-zero to center the populated
    // rows vertically (see rowShiftToCenter())
    double rowShift = 0.;
};

// The shift that centers the rows that hold the first filledCells cells vertically in the grid:
// half the number of empty rows, towards the empty rows.
inline double
rowShiftToCenter(const Grid& grid,
                 long long filledCells)
{
    if ( (grid.rows <= 0) || (grid.columns <= 0) || (filledCells <= 0) ) {
        return 0.;
    }
    const long long filledRows = (std::min)( static_cast<long long>(grid.rows), (filledCells + grid.columns - 1) / grid.columns );
    const double shift = (grid.rows - filledRows) / 2.;

    // the rows populated first are at the top with topBottom: move them down
    return grid.topBottom ? -shift : shift;
}

// The column of the cell at index c (in population order) in populated row r, from the left.
inline int
displayColumn(const Grid& grid,
              int r,
              int c)
{
    const bool rightLeft = (grid.columnOrder == ColumnOrder::eRightLeft) ||
                           ( (grid.columnOrder == ColumnOrder::eSnake) && (r % 2 == 1) );

    return rightLeft ? (grid.columns - 1 - c) : c;
}

// The row of populated row r, from the bottom (without the row shift).
inline int
displayRow(const Grid& grid,
           int r)
{
    return grid.topBottom ? (grid.rows - 1 - r) : r;
}

// Compute the RoD of cell number "cell" in the grid that covers rod.
// Return false if the cell is beyond the last row.
template<class Index>
inline bool
cellRoD(Index cell,
        const Grid& grid,
        const OfxRectD& rod,
        OfxRectD* cellRect)
{
    const int r = static_cast<int>(cell / grid.columns);
    const int c = static_cast<int>(cell % grid.columns);
    if (r >= grid.rows) {
        return false;
    }
    const double y = displayRow(grid, r) + grid.rowShift;
    const int x = displayColumn(grid, r, c);
    // now compute the four corners of the cell in the rod
    cellRect->x1 = rod.x1 + x * (rod.x2 - rod.x1) / grid.columns;
    cellRect->y1 = rod.y1 + y * (rod.y2 - rod.y1) / grid.rows;
    cellRect->x2 = rod.x1 + (x + 1) * (rod.x2 - rod.x1) / grid.columns;
    cellRect->y2 = rod.y1 + (y + 1) * (rod.y2 - rod.y1) / grid.rows;

    return true;
}

// The same, for a grid with no row shift and with the rows populated from left to right
// (leftright) or from right to left.
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
    Grid grid;
    grid.rows = rows;
    grid.columns = columns;
    grid.topBottom = topbottom;
    grid.columnOrder = leftright ? ColumnOrder::eLeftRight : ColumnOrder::eRightLeft;

    return cellRoD(cell, grid, rod, cellRect);
}

// The number of the cell at point p of the grid that covers rod (the inverse of cellRoD()).
// Return false if p is outside rod or outside the cells.
inline bool
cellAt(const Grid& grid,
       const OfxRectD& rod,
       const OfxPointD& p,
       long long* cell)
{
    if ( (p.x < rod.x1) || (p.x >= rod.x2) || (p.y < rod.y1) || (p.y >= rod.y2) ) {
        return false;
    }
    const double cellw = (rod.x2 - rod.x1) / grid.columns;
    const double cellh = (rod.y2 - rod.y1) / grid.rows;
    const int x = static_cast<int>( std::floor( (p.x - rod.x1) / cellw ) );
    const int y = static_cast<int>( std::floor( (p.y - rod.y1) / cellh - grid.rowShift ) );
    if ( (x < 0) || (grid.columns <= x) || (y < 0) || (grid.rows <= y) ) {
        return false;
    }
    const int r = displayRow(grid, y); // displayRow() is its own inverse
    const int c = displayColumn(grid, r, x); // so is displayColumn(), for a given row
    *cell = static_cast<long long>(r) * grid.columns + c;

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
#define kContactSheetRowBytesUnsupportedMessage \
    "The host gave this plug-in an image whose row bytes are not a multiple of 4 bytes (one float). " \
    "Please report this to the host developers."

// The plugins only declare eBitDepthFloat, and the pixels are accessed as float: check it in
// release builds too, since a host that ignores kOfxImageEffectPropSupportedPixelDepths
// (e.g. giving 8-bit images) would otherwise make drawImage() write past the end of the buffers.
// If img is not float, post an error message that explains it and throw kOfxStatFailed (the
// render action doc: "kOfxStatFailed, something wrong, but no error code appropriate, plugin to
// post message"). render() clears the message once the output image is float.
// drawImage() uses the row bytes as a stride in floats: they must be a whole number of floats
// (kOfxImagePropRowBytes: "at least sizeof(PIXEL) * (bounds.x2-bounds.x1)", and may be negative).
inline void
checkImageFormat(ImageEffect& effect,
                 const Image& img)
{
    if (img.getPixelDepth() != eBitDepthFloat) {
        effect.setPersistentMessage(Message::eMessageError, "", kContactSheetBitDepthUnsupportedMessage);
        throwSuiteStatusException(kOfxStatFailed);
    }
    if (img.getRowBytes() % static_cast<int>( sizeof(float) ) != 0) {
        effect.setPersistentMessage(Message::eMessageError, "", kContactSheetRowBytesUnsupportedMessage);
        throwSuiteStatusException(kOfxStatFailed);
    }
}

// Resize the whole (float) src image into the imageRoD area of the (float) dst image.
// The caller must check both images with checkImageFormat().
inline void
drawImage(const Image* src,
          Image* dst,
          const OfxRectD& imageRoD,
          const OfxPointD& renderScale,
          double dstPar)
{
    assert(dst->getPixelDepth() == eBitDepthFloat && src->getPixelDepth() == eBitDepthFloat);
    assert(dst->getRowBytes() % static_cast<int>( sizeof(float) ) == 0 &&
           src->getRowBytes() % static_cast<int>( sizeof(float) ) == 0);
    const OfxRectI& dstBounds = dst->getBounds();
    float* b = (float*)dst->getPixelData();
    const size_t bwidth = dstBounds.x2 - dstBounds.x1;
    const size_t bheight = dstBounds.y2 - dstBounds.y1;
    const size_t bxstride = dst->getPixelComponentCount();
    // Rows may be padded, and row bytes may be negative (kOfxImagePropRowBytes): use them, not
    // width * components. getPixelData() is row bounds.y1, the lower left corner
    // (kOfxImagePropData); with negative row bytes, it is not the lowest address.
    const std::ptrdiff_t bystride = dst->getRowBytes() / static_cast<std::ptrdiff_t>( sizeof(float) );

    const OfxRectI& srcBounds = src->getBounds();
    const float* a = (const float*)src->getPixelData();
    const size_t awidth = srcBounds.x2 - srcBounds.x1;
    const size_t aheight = srcBounds.y2 - srcBounds.y1;
    const size_t axstride = src->getPixelComponentCount();
    const std::ptrdiff_t aystride = src->getRowBytes() / static_cast<std::ptrdiff_t>( sizeof(float) );
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
