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
#include <vector>

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
    // Cells of a whole number of output pixels (ContactSheet 2, see setWholePixelCells()): the
    // width and height of a cell in pixels at render scale 1, or 0 for cells that divide the
    // RoD evenly (ContactSheet 1, LayerContactSheet)
    int cellWidth = 0;
    int cellHeight = 0;
    double par = 1.; // the pixel aspect ratio of the output, with whole-pixel cells
};

// The size of a cell of n cells over size pixels, with a gap on their left (or bottom) side and on
// the right (or top) side of the sheet: (size - gap) / n, rounded to a whole number of pixels
// (as in the tests/nuke reference, where the cells start at the bottom left of the sheet).
inline int
wholePixelCellSize(int size,
                   int gap,
                   int n)
{
    return (n <= 0) ? 0 : static_cast<int>( std::floor( static_cast<double>(size - gap) / n + 0.5 ) );
}

// Make the cells of grid a whole number of pixels, for an output of width x height pixels at
// render scale 1, with the given gap (in pixels) and pixel aspect ratio.
inline void
setWholePixelCells(Grid* grid,
                   int width,
                   int height,
                   int gap,
                   double par)
{
    grid->cellWidth = wholePixelCellSize(width, gap, grid->columns);
    grid->cellHeight = wholePixelCellSize(height, gap, grid->rows);
    grid->par = par;
}

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
    if (grid.cellWidth > 0) {
        // whole-pixel cells, from the bottom left corner of rod
        cellRect->x1 = rod.x1 + x * grid.cellWidth * grid.par;
        cellRect->y1 = rod.y1 + y * grid.cellHeight;
        cellRect->x2 = cellRect->x1 + grid.cellWidth * grid.par;
        cellRect->y2 = cellRect->y1 + grid.cellHeight;

        return true;
    }
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
    const double cellw = (grid.cellWidth > 0) ? (grid.cellWidth * grid.par) : ( (rod.x2 - rod.x1) / grid.columns );
    const double cellh = (grid.cellHeight > 0) ? grid.cellHeight : ( (rod.y2 - rod.y1) / grid.rows );
    if ( (cellw <= 0.) || (cellh <= 0.) ) {
        return false;
    }
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

// ContactSheet 2: where the image of an input goes in a whole-pixel cell (see setWholePixelCells()).
// The positions are in output pixels at render scale 1.
struct Placement
{
    OfxRectD cell = {0., 0., 0., 0.}; // the cell, which clips the image and its filtered edges
    double x0 = 0.; // the bottom left corner of the image (not a whole pixel with Center)
    double y0 = 0.;
    double scaleX = 1.; // output pixels per input pixel
    double scaleY = 1.;
};

// The placement of an input whose format is srcFormat (in its pixels, of aspect ratio srcPar) in
// the cell cellCanonical of a grid with whole-pixel cells, for an output of aspect ratio dstPar:
// the image is scaled to fit the cell minus the gap on its left and bottom sides, keeping its
// aspect ratio, and it is at the bottom left of that area, or centred in it.
// Return false if nothing is drawn (an empty format, or a gap that fills the cell).
inline bool
placeInCell(const OfxRectI& srcFormat,
            double srcPar,
            const OfxRectD& cellCanonical,
            int gap,
            bool center,
            double dstPar,
            Placement* p)
{
    if ( !(srcPar > 0.) || !(dstPar > 0.) ) {
        return false;
    }
    const double sw = srcFormat.x2 - srcFormat.x1; // in input pixels
    const double sh = srcFormat.y2 - srcFormat.y1;
    // the cell is a whole number of pixels: remove the rounding errors of the canonical coordinates
    p->cell.x1 = std::floor(cellCanonical.x1 / dstPar + 0.5);
    p->cell.x2 = std::floor(cellCanonical.x2 / dstPar + 0.5);
    p->cell.y1 = std::floor(cellCanonical.y1 + 0.5);
    p->cell.y2 = std::floor(cellCanonical.y2 + 0.5);
    const double aw = p->cell.x2 - p->cell.x1 - gap; // the area of the image, in output pixels
    const double ah = p->cell.y2 - p->cell.y1 - gap;
    if ( (sw <= 0.) || (sh <= 0.) || (aw <= 0.) || (ah <= 0.) ) {
        return false;
    }
    // the scale of the canonical coordinates
    const double f = (std::min)( (aw * dstPar) / (sw * srcPar), ah / sh );
    const double w = sw * srcPar * f / dstPar; // the size of the image, in output pixels
    const double h = sh * f;
    p->x0 = p->cell.x1 + gap + (center ? (aw - w) / 2 : 0.);
    p->y0 = p->cell.y1 + gap + (center ? (ah - h) / 2 : 0.);
    p->scaleX = w / sw;
    p->scaleY = h / sh;

    return true;
}

// The resampling filter of ContactSheet 2, as in the tests/nuke reference: the cubic BC-spline
// with B = 0 and C = 0 (2|x|^3 - 3|x|^2 + 1 on [-1, 1]), widened by the minification.
inline double
cubicFilter(double x)
{
    x = std::fabs(x);

    return (x < 1.) ? ( (2. * x - 3.) * x * x + 1. ) : 0.;
}

// The filter support, in input pixels on each side, for scale output pixels per input pixel.
inline double
filterRadius(double scale)
{
    return (std::max)(1., 1. / scale);
}

// The filter taps for the output pixel whose centre is at u, in input pixel coordinates (input
// pixel j covers [j, j + 1)), for scale output pixels per input pixel: *first is the first input
// pixel, in [lo, hi), and weights holds the weights of the input pixels from *first, up to hi.
// The weights are divided by the sum of the weights of all the taps, also those outside
// [lo, hi): the input is black outside.
inline void
filterTaps(double u,
           double scale,
           int lo,
           int hi,
           int* first,
           std::vector<float>* weights)
{
    weights->clear();
    *first = lo;
    const double r = filterRadius(scale);
    if ( !(std::fabs(u) < 1e9) ) {
        return; // too far (or NaN): no input pixel
    }
    const int j1 = static_cast<int>( std::floor(u - r - 0.5) );
    const int j2 = static_cast<int>( std::ceil(u + r - 0.5) );
    double total = 0.;
    for (int j = j1; j <= j2; ++j) {
        total += cubicFilter( (u - (j + 0.5)) / r );
    }
    if (total <= 0.) {
        return;
    }
    *first = (std::max)(j1, lo);
    for (int j = *first; j <= j2 && j < hi; ++j) {
        weights->push_back( static_cast<float>( cubicFilter( (u - (j + 0.5)) / r ) / total ) );
    }
}

// The region of the input that drawPlacedImage() reads for the output area outputCanonical (both
// in canonical coordinates), for an input of format srcFormat (in its pixels, of aspect ratio
// srcPar) placed by p in an output of aspect ratio dstPar. Return false if it reads nothing.
inline bool
placedImageRoI(const OfxRectD& outputCanonical,
               const Placement& p,
               const OfxRectI& srcFormat,
               double srcPar,
               double dstPar,
               OfxRectD* roi)
{
    // the output pixels of the cell in the output area
    OfxRectD area = { outputCanonical.x1 / dstPar, outputCanonical.y1, outputCanonical.x2 / dstPar, outputCanonical.y2 };
    if ( !Coords::rectIntersection(area, p.cell, &area) ) {
        return false;
    }
    // in input pixels, with the filter support
    const double rx = filterRadius(p.scaleX) + 1.;
    const double ry = filterRadius(p.scaleY) + 1.;
    const double u1 = srcFormat.x1 + (area.x1 - p.x0) / p.scaleX - rx;
    const double u2 = srcFormat.x1 + (area.x2 - p.x0) / p.scaleX + rx;
    const double v1 = srcFormat.y1 + (area.y1 - p.y0) / p.scaleY - ry;
    const double v2 = srcFormat.y1 + (area.y2 - p.y0) / p.scaleY + ry;
    roi->x1 = u1 * srcPar;
    roi->x2 = u2 * srcPar;
    roi->y1 = v1;
    roi->y2 = v2;

    return true;
}

// Draw the (float) src image, placed by p, in the (float) dst image, inside the cell of p and the
// renderWindow (in dst pixels). srcFormat is the format of the input, in its pixels at render
// scale 1. Both images are at renderScale; src is black outside its bounds.
// The caller must check both images with checkImageFormat().
inline void
drawPlacedImage(const Image* src,
                Image* dst,
                const Placement& p,
                const OfxRectI& srcFormat,
                const OfxRectI& renderWindow,
                const OfxPointD& renderScale)
{
    assert(dst->getPixelDepth() == eBitDepthFloat && src->getPixelDepth() == eBitDepthFloat);
    // the cell, in dst pixels at renderScale
    OfxRectI win;
    win.x1 = static_cast<int>( std::floor(p.cell.x1 * renderScale.x + 0.5) );
    win.x2 = static_cast<int>( std::floor(p.cell.x2 * renderScale.x + 0.5) );
    win.y1 = static_cast<int>( std::floor(p.cell.y1 * renderScale.y + 0.5) );
    win.y2 = static_cast<int>( std::floor(p.cell.y2 * renderScale.y + 0.5) );
    if ( !Coords::rectIntersection(win, renderWindow, &win) ||
         !Coords::rectIntersection(win, dst->getBounds(), &win) ) {
        return;
    }
    const OfxRectI& sb = src->getBounds();
    if ( (sb.x2 <= sb.x1) || (sb.y2 <= sb.y1) ) {
        return;
    }
    const int depth = (std::min)( src->getPixelComponentCount(), dst->getPixelComponentCount() );
    // the input pixel coordinates of the output pixel centres: the bottom left corner of the image
    // is at the bottom left corner of the input format
    const double x0 = p.x0 * renderScale.x;
    const double y0 = p.y0 * renderScale.y;
    const double sx1 = srcFormat.x1 * renderScale.x;
    const double sy1 = srcFormat.y1 * renderScale.y;

    // horizontal taps of each output column
    const int width = win.x2 - win.x1;
    std::vector<int> xFirst(width);
    std::vector<std::vector<float> > xWeights(width);
    for (int i = 0; i < width; ++i) {
        filterTaps(sx1 + (win.x1 + i + 0.5 - x0) / p.scaleX, p.scaleX, sb.x1, sb.x2, &xFirst[i], &xWeights[i]);
    }
    // vertical taps of each output row, and the input rows they read
    const int height = win.y2 - win.y1;
    std::vector<int> yFirst(height);
    std::vector<std::vector<float> > yWeights(height);
    int rowMin = sb.y2;
    int rowMax = sb.y1 - 1;
    for (int k = 0; k < height; ++k) {
        filterTaps(sy1 + (win.y1 + k + 0.5 - y0) / p.scaleY, p.scaleY, sb.y1, sb.y2, &yFirst[k], &yWeights[k]);
        if ( !yWeights[k].empty() ) {
            rowMin = (std::min)(rowMin, yFirst[k]);
            rowMax = (std::max)( rowMax, yFirst[k] + static_cast<int>( yWeights[k].size() ) - 1 );
        }
    }
    if (rowMax < rowMin) {
        return;
    }
    // first pass: each input row that is read, resampled horizontally
    const std::size_t rowSize = static_cast<std::size_t>(width) * depth;
    std::vector<float> rows(static_cast<std::size_t>(rowMax - rowMin + 1) * rowSize, 0.f);
    for (int j = rowMin; j <= rowMax; ++j) {
        float* out = &rows[static_cast<std::size_t>(j - rowMin) * rowSize];
        for (int i = 0; i < width; ++i, out += depth) {
            const std::vector<float>& w = xWeights[i];
            for (std::size_t t = 0; t < w.size(); ++t) {
                const float* pix = static_cast<const float*>( src->getPixelAddress(xFirst[i] + static_cast<int>(t), j) );
                if (!pix) {
                    continue;
                }
                for (int c = 0; c < depth; ++c) {
                    out[c] += w[t] * pix[c];
                }
            }
        }
    }
    // second pass: the vertical resampling, into dst
    const int dstComponents = dst->getPixelComponentCount();
    std::vector<float> acc(depth);
    for (int k = 0; k < height; ++k) {
        const std::vector<float>& w = yWeights[k];
        float* dpix = static_cast<float*>( dst->getPixelAddress(win.x1, win.y1 + k) );
        if (!dpix) {
            continue;
        }
        for (int i = 0; i < width; ++i, dpix += dstComponents) {
            std::fill(acc.begin(), acc.end(), 0.f);
            for (std::size_t t = 0; t < w.size(); ++t) {
                const float* in = &rows[static_cast<std::size_t>(yFirst[k] + static_cast<int>(t) - rowMin) * rowSize + static_cast<std::size_t>(i) * depth];
                for (int c = 0; c < depth; ++c) {
                    acc[c] += w[t] * in[c];
                }
            }
            for (int c = 0; c < depth; ++c) {
                dpix[c] = acc[c];
            }
        }
    }
} // drawPlacedImage

} // namespace ContactSheet
} // namespace OFX

#endif // openfx_misc_ContactSheetLayout_h
