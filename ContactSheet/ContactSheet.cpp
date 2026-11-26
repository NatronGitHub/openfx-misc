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
 * OFX ContactSheet plugin.
 * ContactSheet between inputs.
 */

#include <string>
#include <algorithm>
#include <cmath>

#ifdef __APPLE__
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION // Yes, we are still doing OpenGL 2.1
#endif
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <GL/gl.h>
#endif


#include "ofxsMacros.h"
#include "ofxNatron.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsFilter.h"
#include "ContactSheetLayout.h"
#include "ofxsOGLHiDPI.h"
#include "ofxsOverlayDraw.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define SELECTION

using namespace OFX;


OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ContactSheetOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
    "Make a contact sheet from several inputs or frames.\n" \
    "The output is a grid of Rows x Columns cells that covers the Resolution. " \
    "Each input takes as many consecutive cells as there are frames in Frame Range (one by default), in the order of the inputs; " \
    "a disconnected input keeps its cells, which stay black. Images beyond the last cell of the grid are not drawn.\n" \
    "The cells are populated row by row, in the order given by Row Order (the first row is the bottom one by default) " \
    "and Column Order. A sheet with fewer images than cells keeps the size and place of every cell: " \
    "its images fill the first rows, and the other rows are empty, unless Center Rows is checked.\n" \
    "Each cell is a whole number of pixels: (Resolution - Gap) / Columns wide and (Resolution - Gap) / Rows high, rounded, " \
    "and the cells start at the bottom left corner of the output, so that the Gap is on the left and bottom sides of each cell, " \
    "and on the right and top sides of the sheet.\n" \
    "Each image is scaled to fit its cell minus the gap, keeping its aspect ratio, with a smooth (cubic) filter, " \
    "and it is black outside its input. The output has square pixels (on hosts that support clips of different pixel " \
    "aspect ratios): Resolution is in square pixels, and the inputs may have any pixel aspect ratio."
#define kPluginIdentifier "net.sf.openfx.ContactSheetOFX"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
// 2.0: (unreleased) the clips of inputs 10 to 15 (and 10 to 127 in Natron) are named "10", "11", ...:
//      ContactSheet 1 wrote the digits in reverse order ("01", "11", "21", ...; issue #401). Version 1 is kept,
//      obsolete, with the old clip names and the old layout.
//      (unreleased, fixed in place, issue #305) new layout and resampling: the cells are a whole number of
//      pixels, (Resolution - Gap) / Columns (or Rows), rounded, from the bottom left corner, with the gap on their
//      left and bottom sides (it was half the gap on each side, in cells that divide the output evenly); the
//      image is at its exact (sub-pixel) place, resampled with the cubic filter (it was box-filtered into whole
//      pixels); the output pixel aspect ratio is 1 on hosts that support multiple clip PARs (it was the one of
//      the inputs), and the inputs may have different PARs. The tests/nuke cases of ContactSheet match.
// 1.1: (unreleased) the Snake column order, the Center Rows option, and the hints that say how the cells are placed;
//      a click with Enable Selection sets Selection Frame to first + the frame index, as the overlay reads it
//      (it was the frame index alone, so the highlighted cell was not the clicked one unless Frame Range starts at 0);
//      an empty source format (e.g. a disconnected input) no longer gives a NaN image area (NaN audit);
//      in the filter context, Source is RGBA, so that the RGBA Output has the components of Source, as the
//      OpenFX spec mandates (issue #190); images with padded rows or negative row bytes
//      (kOfxImagePropRowBytes) are drawn correctly instead of skewed or out of bounds (issue #232)

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamResolution "resolution"
#define kParamResolutionLabel "Resolution"
#define kParamResolutionHint \
    "Resolution of the output image, in pixels. They are square pixels on hosts that support clips of different pixel aspect ratios; " \
    "on other hosts, they have the pixel aspect ratio that the host gives the output (usually the one of the project)."

#define kParamRowsColumns "rowsColumns"
#define kParamRowsColumnsLabel "Rows/Columns"
#define kParamRowsColumnsHint \
    "How many rows and columns in the grid where the input images or frames are arranged."

#define kParamGap "gap"
#define kParamGapLabel "Gap"
#define kParamGapHint \
"Gap in pixels between the images, and around them: on the left and bottom sides of each cell, and on the right and top sides of the sheet. " \
"(ContactSheet 1: half the gap on each side of each cell.)"

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint \
"Center the image of each input or frame in its cell, minus the gap. The image is scaled to fit there, keeping its aspect ratio: " \
"if its aspect ratio is not the one of the cell, it is at the bottom left of the cell (after the gap) when this is not checked. " \
"This does not move the cells: see Center Rows."

#define kParamCenterRows "centerRows"
#define kParamCenterRowsLabel "Center Rows"
#define kParamCenterRowsHint \
"Center the rows that hold images vertically within the Resolution bounds, when the images do not fill all the rows: " \
"the rows after the cells of the last connected input are split evenly above and below them. " \
"For example, with 2 inputs in a 2x2 grid, the row of the two images is in the middle of the output."

#define kParamRowOrder "rowOrder"
#define kParamRowOrderLabel "Row Order"
#define kParamRowOrderHint \
"The order in which the rows are populated (the first input is in the first row)."
#define kParamRowOrderOptionTopBottom "TopBottom", "From top to bottom row.", "topbottom"
#define kParamRowOrderOptionBottomTop "BottomTop", "From bottom to top row.", "bottomtop"
enum RowOrderEnum {
    eRowOrderTopBottom = 0,
    eRowOrderBottomTop,
};

#define kParamColumnOrder "colOrder"
#define kParamColumnOrderLabel "Column Order"
#define kParamColumnOrderHint \
"The order in which the cells of each row are populated."
#define kParamColumnOrderOptionLeftRight "LeftRight", "From left to right column."
#define kParamColumnOrderOptionRightLeft "RightLeft", "From right to left column."
#define kParamColumnOrderOptionSnake "Snake", "In alternate directions, starting from left to right: the first row from left to right, the second one from right to left, and so on.", "snake"
enum ColumnOrderEnum {
    eColumnOrderLeftRight = 0,
    eColumnOrderRightLeft,
    eColumnOrderSnake, // appended in version 1.1
};

ContactSheet::ColumnOrder
toColumnOrder(int option)
{
    switch (option) {
    case eColumnOrderRightLeft:
        return ContactSheet::ColumnOrder::eRightLeft;
    case eColumnOrderSnake:
        return ContactSheet::ColumnOrder::eSnake;
    case eColumnOrderLeftRight:
    default:
        return ContactSheet::ColumnOrder::eLeftRight;
    }
}

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint \
"Frames that are taken from each input. For example, if there are 4 inputs, 'frameRange' is 0-1, and 'absolute' is not checked, the current frame and the next frame is taken from each input, and the contact sheet will contain 8 frames in total."

#define kParamFrameRangeAbsolute "frameRangeAbsolute"
#define kParamFrameRangeAbsoluteLabel "Absolute"
#define kParamFrameRangeAbsoluteHint \
"If checked, the 'frameRange' parameter contains absolute frame numbers."

#ifdef SELECTION
#define kParamSelection "selection"
#define kParamSelectionLabel "Enable Selection"
#define kParamSelectionHint \
"If checked, the mouse can be used to select an input or frame, and 'selectionInput' and 'selectionFrame' are set to the selected frame. At at least one keyframe to 'selectionInput' and 'selectionFrame' to enable time-varying selection."

#define kParamSelectionInput "selectionInput"
#define kParamSelectionInputLabel "Selection Input"
#define kParamSelectionInputHint \
"The selected input. Can be used as the 'which' parameter of a Switch effect. At at least one keyframe to this parameter to enable time-varying selection."

#define kParamSelectionFrame "selectionFrame"
#define kParamSelectionFrameLabel "Selection Frame"
#define kParamSelectionFrameHint \
"The selected frame (if frameRangeAbsolute is checked, this is an absolute frame number). Can be used as the 'firstFrame' parameter of a FrameHold effect. At at least one keyframe to this parameter to enable time-varying selection."
#endif

#define kClipSourceCount 16
#define kClipSourceCountNumerous 128


// The name of the clip of input i. ContactSheet 1 wrote the digits in reverse order (inputs 10 to
// 15 are "01", "11", ..., "51"; issue #401): its clips keep these names, so that the connections of
// saved projects and scripts still match.
std::string
clipName(unsigned i,
         unsigned majorVersion)
{
    std::string nb = std::to_string(i);
    if (majorVersion < 2) {
        std::reverse( nb.begin(), nb.end() );
    }

    return nb;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ContactSheetPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ContactSheetPlugin(OfxImageEffectHandle handle, bool numerousInputs, unsigned majorVersion);

    // The grid of cells at time "time", with "count" frames per input (also used by the overlay).
    ContactSheet::Grid getGrid(double time, int count) const;

private:
    // The format of an input in its pixels at render scale 1 (its RoD if it has no format), and
    // its pixel aspect ratio.
    struct SourceFormat
    {
        OfxRectI format;
        double par;
    };
    SourceFormat getSourceFormat(int i, double time) const;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL;

private:

    void updateGUI();

    const unsigned _majorVersion;
    // do not need to delete these, the ImageEffect is managing them for us
    Clip* _dstClip;
    std::vector<Clip *> _srcClip;
    Int2DParam *_resolution;
    Int2DParam* _rowsColumns;
    IntParam* _gap;
    BooleanParam* _center;
    BooleanParam* _centerRows;
    ChoiceParam* _rowOrder;
    ChoiceParam* _colOrder;
    Int2DParam* _frameRange;
    BooleanParam* _frameRangeAbsolute;
    BooleanParam* _selection;
    IntParam* _selectionInput;
    IntParam* _selectionFrame;
};

ContactSheetPlugin::ContactSheetPlugin(OfxImageEffectHandle handle,
                                       bool numerousInputs,
                                       unsigned majorVersion)
    : ImageEffect(handle)
    , _majorVersion(majorVersion)
    , _dstClip(NULL)
    , _srcClip(numerousInputs ? kClipSourceCountNumerous : kClipSourceCount)
    , _resolution(NULL)
    , _rowsColumns(NULL)
    , _gap(NULL)
    , _center(NULL)
    , _centerRows(NULL)
    , _rowOrder(NULL)
    , _colOrder(NULL)
    , _frameRange(NULL)
    , _frameRangeAbsolute(NULL)
    , _selection(NULL)
    , _selectionInput(NULL)
    , _selectionFrame(NULL)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if ( (getContext() == OFX::eContextFilter) && (i == 0) ) {
            _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            _srcClip[i] = fetchClip( clipName(i, majorVersion) );
        }
        assert(_srcClip[i]);
    }
    _resolution  = fetchInt2DParam(kParamResolution);
    _rowsColumns = fetchInt2DParam(kParamRowsColumns);
    assert(_resolution && _rowsColumns);
    _gap = fetchIntParam(kParamGap);
    _center = fetchBooleanParam(kParamCenter);
    _centerRows = fetchBooleanParam(kParamCenterRows);
    _rowOrder = fetchChoiceParam(kParamRowOrder);
    _colOrder = fetchChoiceParam(kParamColumnOrder);
    _frameRange = fetchInt2DParam(kParamFrameRange);
    _frameRangeAbsolute = fetchBooleanParam(kParamFrameRangeAbsolute);
#ifdef SELECTION
    _selection = fetchBooleanParam(kParamSelection);
    _selectionInput = fetchIntParam(kParamSelectionInput);
    _selectionFrame = fetchIntParam(kParamSelectionFrame);
#endif

    // finally
    syncPrivateData();
}

ContactSheet::Grid
ContactSheetPlugin::getGrid(double time,
                            int count) const
{
    ContactSheet::Grid grid;
    _rowsColumns->getValueAtTime(time, grid.rows, grid.columns);
    grid.topBottom = (_rowOrder->getValueAtTime(time) == eRowOrderTopBottom);
    grid.columnOrder = toColumnOrder( _colOrder->getValueAtTime(time) );
    if (_majorVersion >= 2) {
        int w, h;
        _resolution->getValueAtTime(time, w, h);
        ContactSheet::setWholePixelCells( &grid, w, h, _gap->getValueAtTime(time), _dstClip->getPixelAspectRatio() );
    }
    if ( _centerRows->getValueAtTime(time) ) {
        // the images fill the cells up to the last one of the last connected input
        int lastConnected = -1;
        for (int i = 0; i < (int)_srcClip.size(); ++i) {
            if ( _srcClip[i] && _srcClip[i]->isConnected() ) {
                lastConnected = i;
            }
        }
        grid.rowShift = ContactSheet::rowShiftToCenter(grid, static_cast<long long>(lastConnected + 1) * count);
    }

    return grid;
}

ContactSheetPlugin::SourceFormat
ContactSheetPlugin::getSourceFormat(int i,
                                    double time) const
{
    SourceFormat f;
    _srcClip[i]->getFormat(f.format);
    f.par = _srcClip[i]->getPixelAspectRatio();
    if ( OFX::Coords::rectIsEmpty(f.format) ) {
        // no format is available, use the RoD instead
        const OfxRectD rod = _srcClip[i]->getRegionOfDefinition(time);
        const OfxPointD rs1 = {1., 1.};
        if ( OFX::Coords::rectIsEmpty(rod) || !(f.par > 0.) ) {
            f.format = OfxRectI{0, 0, 0, 0};
        } else {
            Coords::toPixelEnclosing(rod, rs1, f.par, &f.format);
        }
    }

    return f;
}

void
ContactSheetPlugin::syncPrivateData()
{
    updateGUI();
}

void
ContactSheetPlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;

    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    // the output depth is the same for all the cells: check it once
    ContactSheet::checkImageFormat(*this, *dst);
    // clear the message of checkImageFormat() if a previous render posted it
    clearPersistentMessage();
    // clear the renderWindow
    fillBlack( *this, args.renderWindow, args.renderScale, dst.get() );

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;
    OfxRectD rod;
    {
        int w, h;
        _resolution->getValueAtTime(time, w, h);
        double par = _dstClip->getPixelAspectRatio();
        OfxPointD rs1 = {1., 1.};
        OfxRectI rodpixel = {0, 0, w, h};
        Coords::toCanonical(rodpixel, rs1, par, &rod);
    }
    const ContactSheet::Grid grid = getGrid(time, count);
    int gap = _gap->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);
    bool absolute = _frameRangeAbsolute->getValueAtTime(time); // render only
    double dstPar = _dstClip->getPixelAspectRatio();
    OfxRectD renderWindowCanonical;
    Coords::toCanonical(args.renderWindow, args.renderScale, dstPar, &renderWindowCanonical);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int framesLeft = grid.rows * grid.columns;
    int i = 0;
    while (framesLeft > 0 && i < (int)_srcClip.size()) {
        Clip *srcClip = _srcClip[i];
        // version 2 supports multiple clip PARs (see describe())
        assert( _majorVersion >= 2 || !srcClip || srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
        assert( kSupportsMultipleClipDepths || !srcClip || srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

        int clipCount = (std::min)(framesLeft, count); // number of frames from this clip
        if (_majorVersion >= 2) {
            const SourceFormat sf = getSourceFormat(i, time);
            for (int frame = 0; frame < clipCount; ++frame) {
                OfxRectD cellRoD;
                if ( !ContactSheet::cellRoD(count * i + frame, grid, rod, &cellRoD) ) {
                    continue;
                }
                const std::optional<ContactSheet::Placement> placement =
                    ContactSheet::placeInCell(sf.format, sf.par, cellRoD, gap, center, dstPar);
                if (!placement) {
                    continue;
                }
                OfxRectD cellClipped;
                if ( !Coords::rectIntersection(renderWindowCanonical, cellRoD, &cellClipped) ||
                     !srcClip || !srcClip->isConnected() ) {
                    continue;
                }
                const double srcTime = absolute ? (first + frame) : (time + first + frame);
                auto_ptr<const Image> src( srcClip->fetchImage(srcTime) );
                if ( src.get() ) {
#                 ifndef NDEBUG
                    checkBadRenderScaleOrField(src, args);
#                 endif
                    ContactSheet::checkImageFormat(*this, *src);
                    ContactSheet::drawPlacedImage(src.get(), dst.get(), *placement, sf.format, args.renderWindow, args.renderScale);
                }
            }
            framesLeft -= clipCount;
            ++i;
            continue;
        }
        OfxRectD srcFormatCanonical;
        {
            OfxRectI srcFormat;
            _srcClip[i]->getFormat(srcFormat);
            double srcPar = _srcClip[i]->getPixelAspectRatio();
            if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
                // no format is available, use the RoD instead
                srcFormatCanonical = _srcClip[i]->getRegionOfDefinition(time);
            } else {
                const OfxPointD rs1 = {1., 1.};
                Coords::toCanonical(srcFormat, rs1, srcPar, &srcFormatCanonical);
            }
        }

        for (int frame = 0; frame < clipCount; ++frame) {
            int cell = count * i + frame; // cell number
            OfxRectD cellRoD;
            if ( !ContactSheet::cellRoD(cell, grid, rod, &cellRoD) ) {
                continue;
            }

            // and the four corners of the image area in the dest rod
            double f = 1;
            OfxRectD imageRoD = {0., 0., 0., 0.};
            ContactSheet::fitRod(srcFormatCanonical, cellRoD, gap, center, &f, &imageRoD);

            //- intersect with the render window
            OfxRectD imageRoDClipped;
            if (Coords::rectIntersection(renderWindowCanonical, imageRoD, &imageRoDClipped)) {

                //render:
                //- get the the src Image
                double srcTime = absolute ? (first + frame) : (time + first + frame);
                auto_ptr<const Image> src( ( srcClip && srcClip->isConnected() ) ?
                                               srcClip->fetchImage(srcTime) : 0 );
                if ( src.get() ) {
#                 ifndef NDEBUG
                    checkBadRenderScaleOrField(src, args);
#                 endif
                    // dst is float (checked above), so this also checks that the depths match
                    ContactSheet::checkImageFormat(*this, *src);
                    //- draw it at the right place
                    ContactSheet::drawImage(src.get(), dst.get(), imageRoD, args.renderScale, dstPar);
                }
            }
        }

        framesLeft -= clipCount;
        ++i;
    }
}


// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
ContactSheetPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                   OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;
    OfxRectD rod;
    {
        int w, h;
        _resolution->getValueAtTime(time, w, h);
        double par = _dstClip->getPixelAspectRatio();
        OfxPointD rs1 = {1., 1.};
        OfxRectI rodpixel = {0, 0, w, h};
        Coords::toCanonical(rodpixel, rs1, par, &rod);
    }
    const ContactSheet::Grid grid = getGrid(time, count);
    int gap = _gap->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int framesLeft = grid.rows * grid.columns;
    int i = 0;
    while (framesLeft > 0 && i < (int)_srcClip.size()) {
        int clipCount = (std::min)(framesLeft, count); // number of frames from this clip
        if (_majorVersion >= 2) {
            const SourceFormat sf = getSourceFormat(i, time);
            const double dstPar = _dstClip->getPixelAspectRatio();
            OfxRectD srcRoI = {0., 0., 0., 0.};
            for (int frame = 0; frame < clipCount; ++frame) {
                OfxRectD cellRoD;
                if ( !ContactSheet::cellRoD(count * i + frame, grid, rod, &cellRoD) ) {
                    continue;
                }
                const std::optional<ContactSheet::Placement> placement =
                    ContactSheet::placeInCell(sf.format, sf.par, cellRoD, gap, center, dstPar);
                const std::optional<OfxRectD> frameRoI = placement ?
                    ContactSheet::placedImageRoI(args.regionOfInterest, *placement, sf.format, sf.par, dstPar, args.renderScale) :
                    std::nullopt;
                if (frameRoI) {
                    Coords::rectBoundingBox(srcRoI, *frameRoI, &srcRoI);
                }
            }
            rois.setRegionOfInterest(*_srcClip[i], srcRoI);
            framesLeft -= clipCount;
            ++i;
            continue;
        }
        OfxRectD srcFormatCanonical;
        {
            OfxRectI srcFormat;
            _srcClip[i]->getFormat(srcFormat);
            double srcPar = _srcClip[i]->getPixelAspectRatio();
            if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
                // no format is available, use the RoD instead
                srcFormatCanonical = _srcClip[i]->getRegionOfDefinition(time);
            } else {
                const OfxPointD rs1 = {1., 1.};
                Coords::toCanonical(srcFormat, rs1, srcPar, &srcFormatCanonical);
            }
        }

        OfxRectD srcRoI = {0., 0., 0., 0.}; // getRegionsOfInterest only
        for (int frame = 0; frame < clipCount; ++frame) {
            int cell = count * i + frame; // cell number
            OfxRectD cellRoD;
            if ( !ContactSheet::cellRoD(cell, grid, rod, &cellRoD) ) {
                continue;
            }

            // and the four corners of the image area in the dest rod
            double f = 1;
            OfxRectD imageRoD = {0., 0., 0., 0.};
            ContactSheet::fitRod(srcFormatCanonical, cellRoD, gap, center, &f, &imageRoD);

            //- intersect with the render window
            OfxRectD imageRoDClipped;
            if (Coords::rectIntersection(args.regionOfInterest, imageRoD, &imageRoDClipped)) {
                //getRegionsOfInterest:

                //- transform back to the srcClip canonical coordinates
                OfxRectD frameRoi = {
                    srcFormatCanonical.x1 + (imageRoDClipped.x1 - imageRoD.x1) / f,
                    srcFormatCanonical.y1 + (imageRoDClipped.y1 - imageRoD.y1) / f,
                    srcFormatCanonical.x2 + (imageRoDClipped.x2 - imageRoD.x2) / f,
                    srcFormatCanonical.y2 + (imageRoDClipped.y2 - imageRoD.y2) / f
                };

                //- expand the srcClip rod accordingly
                Coords::rectBoundingBox(srcRoI, frameRoi, &srcRoI);
                
                //render:
                //- get the the src Image
                //- draw it at the right place
           }
        }
        rois.setRegionOfInterest(*_srcClip[i], srcRoI);

        framesLeft -= clipCount;
        ++i;
    }
}

bool
ContactSheetPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;
    int w, h;
    _resolution->getValueAtTime(time, w, h);
    double par = _dstClip->getPixelAspectRatio();
    OfxPointD rs1 = {1., 1.};
    OfxRectI rodpixel = {0, 0, w, h};
    Coords::toCanonical(rodpixel, rs1, par, &rod);

    return true;
}


void
ContactSheetPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                                  OFX::FramesNeededSetter &frames)
{
    const double time = args.time;

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;
    bool absolute = _frameRangeAbsolute->getValueAtTime(time);
    if (!absolute) {
        first += time;
        last += time;
    }

    // now, for each clip, compute the required frame range
    int rows, columns;
    _rowsColumns->getValueAtTime(time, rows, columns);
    int framesLeft = rows * columns;
    int i = 0;
    while (framesLeft > 0 && i < (int)_srcClip.size()) {
        int clipCount = (std::min)(framesLeft, count); // number of frames from this clip
        OfxRangeD range;
        range.min = first;
        range.max = (std::min)(last, first + clipCount - 1); // the range is inclusive
        frames.setFramesNeeded(*_srcClip[i], range);

        framesLeft -= clipCount;
        ++i;
    }
}


/* Override the clip preferences */
void
ContactSheetPlugin::getClipPreferences(OFX::ClipPreferencesSetter & clipPreferences)
{
    updateGUI();

    OfxRectI format = {0, 0, 0, 0};
    _resolution->getValue(format.x2, format.y2);
    clipPreferences.setOutputFormat(format);
    if ( (_majorVersion >= 2) && getImageEffectHostDescription()->supportsMultipleClipPARs ) {
        // Resolution is in square pixels; other hosts give the output the pixel aspect ratio of the
        // inputs, and the images are fitted in canonical coordinates
        clipPreferences.setPixelAspectRatio(*_dstClip, 1.);
    }
}

void
ContactSheetPlugin::changedClip(const InstanceChangedArgs & /*args*/,
                          const std::string & /*clipName*/)
{
    updateGUI();
}

void
ContactSheetPlugin::changedParam(const InstanceChangedArgs &/*args*/,
                           const std::string &paramName)
{
    if (paramName == kParamFrameRange) {
        updateGUI();
    }
#ifdef SELECTION
    if (paramName == kParamSelection) {
        updateGUI();
    }
#endif
}

void
ContactSheetPlugin::updateGUI()
{
    int maxconnected = 1;

    for (unsigned i = 2; i < _srcClip.size(); ++i) {
        if ( _srcClip[i] && _srcClip[i]->isConnected() ) {
            maxconnected = i;
        }
    }
    _selectionInput->setDisplayRange(0, maxconnected);

    int min = 0, max = 0;
    _frameRange->getValue(min, max);
    _selectionFrame->setDisplayRange(min, max);

    bool selectionEnabled = false;
    _selection->getValue(selectionEnabled);
    _selectionFrame->setEnabled(selectionEnabled);
    _selectionInput->setEnabled(selectionEnabled);
}

//////////// INTERACT

#ifdef SELECTION

class ContactSheetInteract : public OverlayInteract {

public:
    ContactSheetInteract(OfxInteractHandle handle, ImageEffect* effect)
    : OverlayInteract(handle)
    , _plugin( dynamic_cast<const ContactSheetPlugin*>(effect) )
    , _dstClip(NULL)
    , _resolution(NULL)
    , _gap(NULL)
    , _center(NULL)
    , _frameRange(NULL)
    , _frameRangeAbsolute(NULL)
    , _selection(NULL)
    , _selectionInput(NULL)
    , _selectionFrame(NULL)
    , _hiDPI(NULL)
    , _drawMode( OFX::ofxsOverlayDrawModeForNewInteract() )
    {
        assert(_plugin);
        _dstClip = effect->fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
        _resolution  = effect->fetchInt2DParam(kParamResolution);
        assert(_resolution);
        _gap = effect->fetchIntParam(kParamGap);
        _center = effect->fetchBooleanParam(kParamCenter);
        _frameRange = effect->fetchInt2DParam(kParamFrameRange);
        _frameRangeAbsolute = effect->fetchBooleanParam(kParamFrameRangeAbsolute);
        _selection = effect->fetchBooleanParam(kParamSelection);
        _selectionInput = effect->fetchIntParam(kParamSelectionInput);
        _selectionFrame = effect->fetchIntParam(kParamSelectionFrame);
        if ( effect->paramExists(kParamHiDPI) ) {
            _hiDPI = effect->fetchBooleanParam(kParamHiDPI);
        }
    }

private:
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const OFX::PenArgs &args) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NATRON
    virtual bool penMotion(const OFX::PenArgs &args) OVERRIDE FINAL;
#endif

    // the input and frame of the cell under the pen; false if selection is off or not over a cell
    bool cellAt(const OFX::PenArgs &args, int* selectionInput, int* selectionFrame) const;

private:
    const ContactSheetPlugin* _plugin; // the effect of this overlay, which owns it
    Clip* _dstClip;
    Int2DParam *_resolution;
    IntParam* _gap;
    BooleanParam* _center;
    Int2DParam* _frameRange;
    BooleanParam* _frameRangeAbsolute;
    BooleanParam* _selection;
    IntParam* _selectionInput;
    IntParam* _selectionFrame;
    BooleanParam* _hiDPI;
    OFX::OverlayDrawModeEnum _drawMode; // set by the descriptor that created the interact, see ofxsOverlayDraw.h
};

// draw the interact
bool
ContactSheetInteract::draw(const OFX::DrawArgs &args)
{
    const double time = args.time;

    if ( !_selection->getValueAtTime(time) ) {
        return false;
    }

    OfxRectD rod = _dstClip->getRegionOfDefinition(time);

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;
    if (!_plugin) {
        return false;
    }
    const ContactSheet::Grid grid = _plugin->getGrid(time, count);

    int selectionInput = _selectionInput->getValueAtTime(time);
    int selectionFrame = _selectionFrame->getValueAtTime(time);

    const long long cell = static_cast<long long>(selectionInput) * count + (selectionFrame - first);
    OfxRectD cellRect;
    if ( (cell < 0) || !ContactSheet::cellRoD(cell, grid, rod, &cellRect) ) {
        return false;
    }

    // all drawing goes through d: OpenGL in V1 draws, the DrawSuite in V2 draws
    OverlayDraw d(args, _drawMode);
    if ( !d.canDraw() ) {
        return false; // V2 draw without a draw context, or V1 draw without OpenGL
    }

    bool hiDPI = _hiDPI ? _hiDPI->getValue() : false;
    double screenPixelRatio = hiDPI ? 2 : 1;
#ifdef OFX_EXTENSIONS_NATRON
    screenPixelRatio *= OverlayDraw::hostScreenPixelRatio(args); // 1 if the host does not set it
    //hiDPI |= args.screenPixelRatio > 1;
#endif
    screenPixelRatio = d.overlayScreenPixelRatio(screenPixelRatio); // DrawSuite: 1
    //TextRenderer::Font font = hiDPI ? TextRenderer::FONT_TIMES_ROMAN_24 : TextRenderer::FONT_HELVETICA_12;

    OfxRGBColourD color = { 0.8, 0.8, 0.8 };
    getSuggestedColour(color);
    //const OfxPointD& pscale = args.pixelScale;
    GLdouble projection[16];
    d.glGetDoublev( GL_PROJECTION_MATRIX, projection);
    GLint viewport[4];
    d.glGetIntegerv(GL_VIEWPORT, viewport);
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    // 0 for a degenerate viewport or projection (inf, then inf - inf = NaN)
    {
        const double denx = projection[0] * viewport[2];
        const double deny = projection[5] * viewport[3];
        shadow.x = (denx != 0.) ? 2. / denx : 0.;
        shadow.y = (deny != 0.) ? 2. / deny : 0.;
    }

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs
    d.glDisable(GL_LINE_STIPPLE);
    d.glEnable(GL_LINE_SMOOTH);
    d.glDisable(GL_POINT_SMOOTH);
    d.glEnable(GL_BLEND);
    d.glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    d.glLineWidth(3.f * (float)screenPixelRatio);
    d.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const double x1 = cellRect.x1;
    const double x2 = cellRect.x2;
    const double y1 = cellRect.y1;
    const double y2 = cellRect.y2;

    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    // with the DrawSuite, no shadow (the host's job): only the drawing pass
    for (int l = d.usesDrawSuite() ? 1 : 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        if ( !d.usesDrawSuite() ) {
            d.glMatrixMode(GL_PROJECTION);
            int direction = (l == 0) ? 1 : -1;
            // translate (1,-1) pixels
            d.glTranslated(direction * shadow.x, -direction * shadow.y, 0);
            d.glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke
        }

        d.glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );

        d.glBegin(GL_LINE_LOOP);
        d.glVertex2d(x1, y1);
        d.glVertex2d(x1, y2);
        d.glVertex2d(x2, y2);
        d.glVertex2d(x2, y1);
        d.glEnd();
    }
    //glPopAttrib();
    
    return true;
}


bool
ContactSheetInteract::cellAt(const OFX::PenArgs &args, int* selectionInput, int* selectionFrame) const
{
    const double time = args.time;

    if ( !_selection->getValueAtTime(time) ) {
        return false;
    }

    OfxRectD rod = _dstClip->getRegionOfDefinition(time);

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    const int count = last - first + 1;
    if (!_plugin) {
        return false;
    }
    long long c = 0;
    if ( !ContactSheet::cellAt(_plugin->getGrid(time, count), rod, args.penPosition, &c) ) {
        return false;
    }

    *selectionFrame = first + static_cast<int>(c % count); // the frame that draw() reads
    *selectionInput = static_cast<int>(c / count);

    return true;
}

bool
ContactSheetInteract::penDown(const OFX::PenArgs &args)
{
    int selectionInput, selectionFrame;

    if ( !cellAt(args, &selectionInput, &selectionFrame) ) {
        return false;
    }
    _selectionFrame->setValue(selectionFrame);
    _selectionInput->setValue(selectionInput);

    return true;
}

#ifdef OFX_EXTENSIONS_NATRON
bool
ContactSheetInteract::penMotion(const OFX::PenArgs &args)
{
    // cells are clickable when selection is on: show a pointer over them. The host reads the
    // cursor whatever penMotion returns, so don't trap the motion (other overlays still get it).
    int selectionInput, selectionFrame;
    const bool overCell = cellAt(args, &selectionInput, &selectionFrame);

    setCursor(overCell ? kOfxInteractCursorPointer : kOfxInteractCursorDefault);

    return false;
}
#endif

#endif // SELECTION

//////////// FACTORY

mDeclarePluginFactoryVersioned(ContactSheetPluginFactory, {}, {});

#ifdef SELECTION
// draws through OverlayDraw: the DrawSuite in V2 draws, OpenGL (with shadows) in V1 draws
class ContactSheetOverlayDescriptor : public DefaultEffectOverlayDescriptorV2<ContactSheetOverlayDescriptor, ContactSheetInteract> {};
#endif

template<unsigned int majorVersion>
void
ContactSheetPluginFactory<majorVersion>::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthNone);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    //desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    //desc.addSupportedBitDepth(eBitDepthCustom);
#ifdef OFX_EXTENSIONS_VEGAS
    //desc.addSupportedBitDepth(eBitDepthUByteBGRA);
    //desc.addSupportedBitDepth(eBitDepthUShortBGRA);
    //desc.addSupportedBitDepth(eBitDepthFloatBGRA);
#endif

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    // version 2 makes square output pixels and fits inputs of any pixel aspect ratio
    desc.setSupportsMultipleClipPARs(majorVersion >= 2);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(false);
#endif
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif

#ifdef SELECTION
    ofxsSetOverlayInteractDescriptorV2(desc, new ContactSheetOverlayDescriptor); // V2, and V1 with OpenGL (hasOpenGLDraw)
#endif
}

template<unsigned int majorVersion>
void
ContactSheetPluginFactory<majorVersion>::describeInContext(OFX::ImageEffectDescriptor &desc,
                                             OFX::ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);
    unsigned clipSourceCount = numerousInputs ? kClipSourceCountNumerous : kClipSourceCount;

    // Source clip only in the filter context
    // create the mandated source clip
    {
        ClipDescriptor *srcClip;
        if (context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            srcClip = desc.defineClip("0");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        if (context != eContextFilter) {
            // The Output is RGBA. In the filter context, "a plugin cannot change the component
            // type of the Output clip, it will always be the same as the Source clip"
            // (ofxImageEffectContexts.rst, The Filter Context): Source is RGBA there, and the host
            // maps the other components to RGBA ("it must map the clip to RGBA in a manner that is
            // transparent to the plugin", ofxClipPreferences.rst).
#ifdef OFX_EXTENSIONS_NATRON
            srcClip->addSupportedComponent(ePixelComponentXY);
#endif
            srcClip->addSupportedComponent(ePixelComponentRGB);
            srcClip->addSupportedComponent(ePixelComponentAlpha);
        }
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        srcClip = desc.defineClip("1");
        srcClip->setOptional(true);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    //if (numerousInputs) {
        for (unsigned i = 2; i < clipSourceCount; ++i) {
            ClipDescriptor *srcClip = desc.defineClip( clipName(i, majorVersion) );
            srcClip->setOptional(true);
#ifdef OFX_EXTENSIONS_NATRON
            srcClip->addSupportedComponent(ePixelComponentXY);
#endif
            srcClip->addSupportedComponent(ePixelComponentRGB);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentAlpha);
            srcClip->setTemporalClipAccess(false);
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    //}

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // resolution
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamResolution);
        param->setLabel(kParamResolutionLabel);
        param->setHint(kParamResolutionHint);
        param->setDefault(3072, 2048);
        param->setRange(1, 1, INT_MAX, INT_MAX);
        param->setDisplayRange(256, 256, 4096, 4096);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // rowsColumns
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamRowsColumns);
        param->setLabel(kParamRowsColumnsLabel);
        param->setHint(kParamRowsColumnsHint);
        param->setDefault(3, 4);
        param->setRange(1, 1, INT_MAX, INT_MAX);
        param->setDisplayRange(1, 1, 32, 32);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // gap
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamGap);
        param->setLabel(kParamGapLabel);
        param->setHint(kParamGapHint);
        param->setDefault(0);
        param->setRange(0, INT_MAX);
        param->setDisplayRange(0, 100);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // center
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamCenter);
        param->setLabel(kParamCenterLabel);
        param->setHint(kParamCenterHint);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // centerRows
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamCenterRows);
        param->setLabel(kParamCenterRowsLabel);
        param->setHint(kParamCenterRowsHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // rowOrder
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRowOrder);
        param->setLabel(kParamRowOrderLabel);
        param->setHint(kParamRowOrderHint);
        param->setAnimates(false);
        assert(param->getNOptions() == (int)eRowOrderTopBottom);
        param->appendOption(kParamRowOrderOptionTopBottom);
        assert(param->getNOptions() == (int)eRowOrderBottomTop);
        param->appendOption(kParamRowOrderOptionBottomTop);
        param->setDefault((int)eRowOrderBottomTop);
        if (page) {
            page->addChild(*param);
        }
    }

    // colOrder
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamColumnOrder);
        param->setLabel(kParamColumnOrderLabel);
        param->setHint(kParamColumnOrderHint);
        param->setAnimates(false);
        assert(param->getNOptions() == (int)eColumnOrderLeftRight);
        param->appendOption(kParamColumnOrderOptionLeftRight);
        assert(param->getNOptions() == (int)eColumnOrderRightLeft);
        param->appendOption(kParamColumnOrderOptionRightLeft);
        assert(param->getNOptions() == (int)eColumnOrderSnake);
        param->appendOption(kParamColumnOrderOptionSnake);
        if (page) {
            page->addChild(*param);
        }
    }

    // frameRange
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDefault(0, 0);
        param->setDisplayRange(-10, -10, 10, 10);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // frameRangeAbsolute
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFrameRangeAbsolute);
        param->setLabel(kParamFrameRangeAbsoluteLabel);
        param->setHint(kParamFrameRangeAbsoluteHint);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef SELECTION
    // selection
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamSelection);
        param->setLabel(kParamSelectionLabel);
        param->setHint(kParamSelectionHint);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // selectionInput
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSelectionInput);
        param->setLabel(kParamSelectionInputLabel);
        param->setHint(kParamSelectionInputHint);
        param->setDefault(0);
        param->setRange(0, clipSourceCount - 1);
        param->setDisplayRange(0, clipSourceCount - 1);
        param->setAnimates(true);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // selectionFrame
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSelectionFrame);
        param->setLabel(kParamSelectionFrameLabel);
        param->setHint(kParamSelectionFrameHint);
        param->setDefault(0);
        param->setDisplayRange(-10, 10);
        param->setAnimates(true);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    hiDPIDescribeParams(desc, NULL, page);
#endif
} // ContactSheetPluginFactory::describeInContext

template<unsigned int majorVersion>
OFX::ImageEffect*
ContactSheetPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                    OFX::ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new ContactSheetPlugin(handle, numerousInputs, majorVersion);
}

static ContactSheetPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
// 1.1: the inputs 10 to 15 are named "01", "11", ..., "51" (see clipName)
static ContactSheetPluginFactory<1> p1(kPluginIdentifier, 1);
mRegisterPluginFactoryInstance(p1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
