/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
#include <OpenGL/gl.h>
#else
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include <GL/gl.h>
#endif


#include "ofxsMacros.h"
#include "ofxNatron.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsFilter.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define SELECTION

using namespace OFX;


OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ContactSheetOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
    "Make a contact sheet from several inputs or frames."
#define kPluginIdentifier "net.sf.openfx.ContactSheetOFX"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamResolution "resolution"
#define kParamResolutionLabel "Resolution"
#define kParamResolutionHint \
    "Resolution of the output image, in pixels."

#define kParamRowsColums "rowsColumns"
#define kParamRowsColumsLabel "Rows/Columns"
#define kParamRowsColumsHint \
    "How many rows and columns in the grid where the input images or frames are arranged."

#define kParamGap "gap"
#define kParamGapLabel "Gap"
#define kParamGapHint \
"Gap in pixels around each input or frame."

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint \
"Center each input/frame within its cell."

#define kParamRowOrder "rowOrder"
#define kParamRowOrderLabel "Row Order"
#define kParamRowOrderHint \
"How image rows are populated."
#define kParamRowOrderOptionTopBottom "TopBottom", "From top to bottom row.", "topbottom"
#define kParamRowOrderOptionBottomTop "BottomTop", "From bottom to top row.", "bottomtop"
enum RowOrderEnum {
    eRowOrderTopBottom = 0,
    eRowOrderBottomTop,
};

#define kParamColumnOrder "colOrder"
#define kParamColumnOrderLabel "Column Order"
#define kParamColumnOrderHint \
"How image columns are populated."
#define kParamColumnOrderOptionLeftRight "LeftRight", "From left to right column."
#define kParamColumnOrderOptionRightLeft "RightLeft", "From right to left column."
enum ColumnOrderEnum {
    eColumnOrderLeftRight = 0,
    eColumnOrderRightLeft,
};

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


static
std::string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    std::string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb += ( '0' + (j % 10) );
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
    ContactSheetPlugin(OfxImageEffectHandle handle, bool numerousInputs);

private:
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

private:

    void updateGUI();

    // do not need to delete these, the ImageEffect is managing them for us
    Clip* _dstClip;
    std::vector<Clip *> _srcClip;
    Int2DParam *_resolution;
    Int2DParam* _rowsColumns;
    IntParam* _gap;
    BooleanParam* _center;
    ChoiceParam* _rowOrder;
    ChoiceParam* _colOrder;
    Int2DParam* _frameRange;
    BooleanParam* _frameRangeAbsolute;
    BooleanParam* _selection;
    IntParam* _selectionInput;
    IntParam* _selectionFrame;

};

ContactSheetPlugin::ContactSheetPlugin(OfxImageEffectHandle handle,
                           bool numerousInputs)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClip(numerousInputs ? kClipSourceCountNumerous : kClipSourceCount)
    , _resolution(NULL)
    , _rowsColumns(NULL)
    , _gap(NULL)
    , _center(NULL)
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
            _srcClip[i] = fetchClip( unsignedToString(i) );
        }
        assert(_srcClip[i]);
    }
    _resolution  = fetchInt2DParam(kParamResolution);
    _rowsColumns = fetchInt2DParam(kParamRowsColums);
    assert(_resolution && _rowsColumns);
    _gap = fetchIntParam(kParamGap);
    _center = fetchBooleanParam(kParamCenter);
    _rowOrder = fetchChoiceParam(kParamRowOrder);
    _colOrder = fetchChoiceParam(kParamColumnOrder);
    _frameRange = fetchInt2DParam(kParamFrameRange);
    _frameRangeAbsolute = fetchBooleanParam(kParamFrameRangeAbsolute);
#ifdef SELECTION
    _selection = fetchBooleanParam(kParamSelection);
    _selectionInput = fetchIntParam(kParamSelectionInput);
    _selectionFrame = fetchIntParam(kParamSelectionFrame);
#endif

    updateGUI();
}

static void
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
    double cw = std::max(1., cRoD.x2 - cRoD.x1);
    double ch = std::max(1., cRoD.y2 - cRoD.y1);
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

void
ContactSheetPlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;

    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
        ( dst->getRenderScale().y != args.renderScale.y) ||
        ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    //PixelComponentEnum dstComponents  = dst->getPixelComponents();
    const OfxRectI& dstBounds = dst->getBounds();
    assert(dst->getPixelDepth() == eBitDepthFloat);
    float* b = (float*)dst->getPixelData();
    const size_t bwidth = dstBounds.x2 - dstBounds.x1;
    const size_t bheight = dstBounds.y2 - dstBounds.y1;
    const size_t bxstride = dst->getPixelComponentCount();
    const size_t bystride = bwidth * bxstride;
    // clear the renderWindow
    fillBlack( *this, args.renderWindow, dst.get() );

    int first, last;
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;
    OfxRectD rod;
    {
        int w, h;
        _resolution->getValue(w, h);
        double par = _dstClip->getPixelAspectRatio();
        OfxPointD rs1 = {1., 1.};
        OfxRectI rodpixel = {0, 0, w, h};
        Coords::toCanonical(rodpixel, rs1, par, &rod);
    }
    bool topbottom = (_rowOrder->getValueAtTime(time) == eRowOrderTopBottom);
    bool leftright = (_colOrder->getValueAtTime(time) == eColumnOrderLeftRight);
    int gap = _gap->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);
    bool absolute = _frameRangeAbsolute->getValueAtTime(time); // render only
    double dstPar = _dstClip->getPixelAspectRatio();
    OfxRectD renderWindowCanonical;
    Coords::toCanonical(args.renderWindow, args.renderScale, dstPar, &renderWindowCanonical);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int rows, columns;
    _rowsColumns->getValueAtTime(time, rows, columns);
    int framesLeft = rows * columns;
    int i = 0;
    while (framesLeft > 0 && i < (int)_srcClip.size()) {
        Clip *srcClip = _srcClip[i];
        assert( kSupportsMultipleClipPARs   || !srcClip || srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
        assert( kSupportsMultipleClipDepths || !srcClip || srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

        int clipCount = std::min(framesLeft, count); // number of frames from this clip
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
            int r = cell / columns;
            int c = cell % columns;
            if (r >= rows) {
                continue;
            }
            if (topbottom) {
                r = rows - 1 - r;
            }
            if (!leftright) {
                c = columns - 1 - c;
            }
            // now compute the four corners of the cell in the rod
            OfxRectD cellRoD = {
                rod.x1 + c * (rod.x2 - rod.x1) / columns,
                rod.y1 + r * (rod.y2 - rod.y1) / rows,
                rod.x1 + (c + 1) * (rod.x2 - rod.x1) / columns,
                rod.y1 + (r + 1) * (rod.y2 - rod.y1) / rows
            };

            // and the four corners of the image area in the dest rod
            double f = 1;
            OfxRectD imageRoD = {0., 0., 0., 0.};
            fitRod(srcFormatCanonical, cellRoD, gap, center, &f, &imageRoD);

            //- intersect with the render window
            OfxRectD imageRoDClipped;
            if (Coords::rectIntersection(renderWindowCanonical, imageRoD, &imageRoDClipped)) {

                //render:
                //- get the the src Image
                double srcTime = absolute ? (first + frame) : (time + first + frame);
                auto_ptr<const Image> src( ( srcClip && srcClip->isConnected() ) ?
                                               srcClip->fetchImage(srcTime) : 0 );
                if ( src.get() ) {
                    if ( (src->getRenderScale().x != args.renderScale.x) ||
                        ( src->getRenderScale().y != args.renderScale.y) ||
                        ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
                        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                        throwSuiteStatusException(kOfxStatFailed);
                    }
                    BitDepthEnum srcBitDepth      = src->getPixelDepth();
                    //PixelComponentEnum srcComponents = src->getPixelComponents();
                    if ( (srcBitDepth != dstBitDepth) /*|| (srcComponents != dstComponents)*/ ) {
                        throwSuiteStatusException(kOfxStatErrImageFormat);
                    }

                    //- draw it at the right place
                    const OfxRectI& srcBounds = src->getBounds();
                    assert(src->getPixelDepth() == eBitDepthFloat);
                    const float* a = (const float*)src->getPixelData();
                    const size_t awidth = srcBounds.x2 - srcBounds.x1;
                    const size_t aheight = srcBounds.y2 - srcBounds.y1;
                    const size_t axstride = src->getPixelComponentCount();
                    const size_t aystride = awidth * axstride;
                    const size_t depth = std::min(axstride, bxstride);
                    const OfxRectD from = {0., 0., (double)awidth, (double)aheight};
                    OfxRectI to;
                    Coords::toPixelEnclosing(imageRoD, args.renderScale, dstPar, &to);
                    to.x1 -= dstBounds.x1;
                    to.y1 -= dstBounds.y1;
                    to.x2 -= dstBounds.x1;
                    to.y2 -= dstBounds.y1;

                    ofxsFilterResize2d(a, awidth, aheight, axstride, aystride, depth,
                                       from, /*zeroOutside=*/false,
                                       b, bwidth, bheight, bxstride, bystride,
                                       to);
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
        _resolution->getValue(w, h);
        double par = _dstClip->getPixelAspectRatio();
        OfxPointD rs1 = {1., 1.};
        OfxRectI rodpixel = {0, 0, w, h};
        Coords::toCanonical(rodpixel, rs1, par, &rod);
    }
    bool topbottom = (_rowOrder->getValueAtTime(time) == eRowOrderTopBottom);
    bool leftright = (_colOrder->getValueAtTime(time) == eColumnOrderLeftRight);
    int gap = _gap->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int rows, columns;
    _rowsColumns->getValueAtTime(time, rows, columns);
    int framesLeft = rows * columns;
    int i = 0;
    while (framesLeft > 0 && i < (int)_srcClip.size()) {
        int clipCount = std::min(framesLeft, count); // number of frames from this clip
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
            int r = cell / columns;
            int c = cell % columns;
            if (r > rows) {
                continue;
            }
            if (topbottom) {
                r = rows - 1 - r;
            }
            if (!leftright) {
                c = columns - 1 - c;
            }
            // now compute the four corners of the cell in the rod
            OfxRectD cellRoD = {
                rod.x1 + c * (rod.x2 - rod.x1) / columns,
                rod.y1 + r * (rod.y2 - rod.y1) / rows,
                rod.x1 + (c + 1) * (rod.x2 - rod.x1) / columns,
                rod.y1 + (r + 1) * (rod.y2 - rod.y1) / rows
            };

            // and the four corners of the image area in the dest rod
            double f = 1;
            OfxRectD imageRoD = {0., 0., 0., 0.};
            fitRod(srcFormatCanonical, cellRoD, gap, center, &f, &imageRoD);

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
        int clipCount = std::min(framesLeft, count); // number of frames from this clip
        OfxRangeD range;
        range.min = first;
        range.max = std::min(last, first + clipCount);
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
    //clipPreferences.setPixelAspectRatio(*_dstClip, par);
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
    , _dstClip(NULL)
    , _resolution(NULL)
    , _rowsColumns(NULL)
    , _gap(NULL)
    , _center(NULL)
    , _rowOrder(NULL)
    , _colOrder(NULL)
    , _frameRange(NULL)
    , _frameRangeAbsolute(NULL)
    , _selection(NULL)
    , _selectionInput(NULL)
    , _selectionFrame(NULL)
    {
        _dstClip = effect->fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
        _resolution  = effect->fetchInt2DParam(kParamResolution);
        _rowsColumns = effect->fetchInt2DParam(kParamRowsColums);
        assert(_resolution && _rowsColumns);
        _gap = effect->fetchIntParam(kParamGap);
        _center = effect->fetchBooleanParam(kParamCenter);
        _rowOrder = effect->fetchChoiceParam(kParamRowOrder);
        _colOrder = effect->fetchChoiceParam(kParamColumnOrder);
        _frameRange = effect->fetchInt2DParam(kParamFrameRange);
        _frameRangeAbsolute = effect->fetchBooleanParam(kParamFrameRangeAbsolute);
        _selection = effect->fetchBooleanParam(kParamSelection);
        _selectionInput = effect->fetchIntParam(kParamSelectionInput);
        _selectionFrame = effect->fetchIntParam(kParamSelectionFrame);
    }

private:
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL;
    virtual bool penDown(const OFX::PenArgs &args) OVERRIDE FINAL;

private:
    Clip* _dstClip;
    Int2DParam *_resolution;
    Int2DParam* _rowsColumns;
    IntParam* _gap;
    BooleanParam* _center;
    ChoiceParam* _rowOrder;
    ChoiceParam* _colOrder;
    Int2DParam* _frameRange;
    BooleanParam* _frameRangeAbsolute;
    BooleanParam* _selection;
    IntParam* _selectionInput;
    IntParam* _selectionFrame;

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
    bool topbottom = (_rowOrder->getValueAtTime(time) == eRowOrderTopBottom);
    bool leftright = (_colOrder->getValueAtTime(time) == eColumnOrderLeftRight);

    int rows, columns;
    _rowsColumns->getValueAtTime(time, rows, columns);

    int selectionInput = _selectionInput->getValueAtTime(time);
    int selectionFrame = _selectionFrame->getValueAtTime(time);

    int c = selectionInput * count + (selectionFrame - first);
    int r = c / columns;
    if (r >= rows) {
        return false;
    }
    c = c % columns;
    if (topbottom) {
        r = rows - 1 - r;
    }
    if (!leftright) {
        c = columns - 1 - c;
    }
    double cellw = (rod.x2 - rod.x1) / columns;
    double cellh = (rod.y2 - rod.y1) / rows;

    OfxRGBColourD color = { 0.8, 0.8, 0.8 };
    getSuggestedColour(color);
    //const OfxPointD& pscale = args.pixelScale;
    GLdouble projection[16];
    glGetDoublev( GL_PROJECTION_MATRIX, projection);
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    OfxPointD shadow; // how much to translate GL_PROJECTION to get exactly one pixel on screen
    shadow.x = 2. / (projection[0] * viewport[2]);
    shadow.y = 2. / (projection[5] * viewport[3]);

    //glPushAttrib(GL_ALL_ATTRIB_BITS); // caller is responsible for protecting attribs
    glDisable(GL_LINE_STIPPLE);
    glEnable(GL_LINE_SMOOTH);
    glDisable(GL_POINT_SMOOTH);
    glEnable(GL_BLEND);
    glHint(GL_LINE_SMOOTH_HINT, GL_DONT_CARE);
    glLineWidth(3.f);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    double x1 = rod.x1 + cellw * c;
    double x2 = rod.x1 + cellw * (c + 1);
    double y1 = rod.y1 + cellh * r;
    double y2 = rod.y1 + cellh * (r + 1);

    // Draw everything twice
    // l = 0: shadow
    // l = 1: drawing
    for (int l = 0; l < 2; ++l) {
        // shadow (uses GL_PROJECTION)
        glMatrixMode(GL_PROJECTION);
        int direction = (l == 0) ? 1 : -1;
        // translate (1,-1) pixels
        glTranslated(direction * shadow.x, -direction * shadow.y, 0);
        glMatrixMode(GL_MODELVIEW); // Modelview should be used on Nuke

        glColor3f( (float)color.r * l, (float)color.g * l, (float)color.b * l );

        glBegin(GL_LINE_LOOP);
        glVertex2d(x1, y1);
        glVertex2d(x1, y2);
        glVertex2d(x2, y2);
        glVertex2d(x2, y1);
        glEnd();
    }
    //glPopAttrib();
    
    return true;
}


bool 
ContactSheetInteract::penDown(const OFX::PenArgs &args)
{
    const double time = args.time;

    if ( !_selection->getValueAtTime(time) ) {
        return false;
    }

    OfxRectD rod = _dstClip->getRegionOfDefinition(time);

    double x = args.penPosition.x;
    double y = args.penPosition.y;

    if (x < rod.x1 || x >= rod.x2 || y < rod.y1 || y >= rod.y2) {
        return false;
    }
    int first, last;

    int rows, columns;
    _rowsColumns->getValueAtTime(time, rows, columns);

    double cellw = (rod.x2 - rod.x1) / columns;
    double cellh = (rod.y2 - rod.y1) / rows;

    int c = std::floor((x - rod.x1) / cellw);
    if (c < 0 || columns <= c) {
        return false;
    }
    int r = std::floor((y - rod.y1) / cellh);
    if (r < 0 || rows <= r) {
        return false;
    }
    bool topbottom = (_rowOrder->getValueAtTime(time) == eRowOrderTopBottom);
    bool leftright = (_colOrder->getValueAtTime(time) == eColumnOrderLeftRight);
    if (topbottom) {
        r = rows - 1 - r;
    }
    if (!leftright) {
        c = columns - 1 - c;
    }
    _frameRange->getValueAtTime(time, first, last);
    if (first > last) {
        std::swap(first, last);
    }
    int count = last - first + 1;

    c = c + r * columns;

    int selectionFrame = c % count;
    int selectionInput = c / count;

    _selectionFrame->setValue(selectionFrame);
    _selectionInput->setValue(selectionInput);

    return true;
}

#endif // SELECTION

//////////// FACTORY

mDeclarePluginFactory(ContactSheetPluginFactory, {}, {});

#ifdef SELECTION
class ContactSheetOverlayDescriptor : public DefaultEffectOverlayDescriptor<ContactSheetOverlayDescriptor, ContactSheetInteract> {};
#endif

void
ContactSheetPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
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
    desc.setOverlayInteractDescriptor( new ContactSheetOverlayDescriptor);
#endif
}

void
ContactSheetPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
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
            ClipDescriptor *srcClip = desc.defineClip( unsignedToString(i) );
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
        if (page) {
            page->addChild(*param);
        }
    }

    // rowsColumns
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamRowsColums);
        param->setLabel(kParamRowsColumsLabel);
        param->setHint(kParamRowsColumsHint);
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
        param->setRange(INT_MIN, INT_MIN, INT_MAX, INT_MAX);
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
        param->setRange(INT_MIN, INT_MAX);
        param->setDisplayRange(-10, 10);
        param->setAnimates(true);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
#endif
} // ContactSheetPluginFactory::describeInContext

OFX::ImageEffect*
ContactSheetPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    OFX::ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new ContactSheetPlugin(handle, numerousInputs);
}

static ContactSheetPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
