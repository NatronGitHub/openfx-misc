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
 * OFX LayerContactSheet plugin.
 * LayerContactSheet between inputs.
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
#include <windows.h>
#endif

#include <GL/gl.h>
#endif


#include "ofxsMacros.h"
#include "ofxNatron.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsFilter.h"
#include "ofxsOGLTextRenderer.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;


OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "LayerContactSheetOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
    "Make a contact sheet from all layers."
#define kPluginIdentifier "net.sf.openfx.LayerContactSheetOFX"
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

#define kParamAutoDims "autoDims"
#define kParamAutoDimsLabel "Automatic Rows/Columns"
#define kParamAutoDimsHint \
    "Automatically  sets the number of rows/columns to display all layers."

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
#define kParamColumnOrderOptionLeftRight "LeftRight", "From left to right column.", "leftright"
#define kParamColumnOrderOptionRightLeft "RightLeft", "From right to left column.", "rightleft"
enum ColumnOrderEnum {
    eColumnOrderLeftRight = 0,
    eColumnOrderRightLeft,
};

#define kParamShowLayerNames "showLayerNames"
#define kParamShowLayerNamesLabel "Show Layer Names"
#define kParamShowLayerNamesHint \
"Display the layer name in the bottom left of each frame."

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class LayerContactSheetPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    LayerContactSheetPlugin(OfxImageEffectHandle handle);

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** Override the get frames needed action */
    //virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
#ifdef OFX_EXTENSIONS_NUKE
    virtual OfxStatus getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
#endif

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:

    void updateGUI();

    // do not need to delete these, the ImageEffect is managing them for us
    Clip* _dstClip;
    Clip* _srcClip;
    Int2DParam *_resolution;
    Int2DParam* _rowsColumns;
    BooleanParam* _autoDims;
    IntParam* _gap;
    BooleanParam* _center;
    ChoiceParam* _rowOrder;
    ChoiceParam* _colOrder;
    BooleanParam* _showLayerNames;
};

LayerContactSheetPlugin::LayerContactSheetPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(NULL)
    , _srcClip(NULL)
    , _resolution(NULL)
    , _rowsColumns(NULL)
    , _autoDims(NULL)
    , _gap(NULL)
    , _center(NULL)
    , _rowOrder(NULL)
    , _colOrder(NULL)
    , _showLayerNames(NULL)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
    _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
    assert(_srcClip);
    _resolution  = fetchInt2DParam(kParamResolution);
    _rowsColumns = fetchInt2DParam(kParamRowsColums);
    assert(_resolution && _rowsColumns);
    _autoDims = fetchBooleanParam(kParamAutoDims);
    _gap = fetchIntParam(kParamGap);
    _center = fetchBooleanParam(kParamCenter);
    _rowOrder = fetchChoiceParam(kParamRowOrder);
    _colOrder = fetchChoiceParam(kParamColumnOrder);
    _showLayerNames = fetchBooleanParam(kParamShowLayerNames);

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
LayerContactSheetPlugin::render(const OFX::RenderArguments &args)
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
    double dstPar = _dstClip->getPixelAspectRatio();
    OfxRectD renderWindowCanonical;
    Coords::toCanonical(args.renderWindow, args.renderScale, dstPar, &renderWindowCanonical);

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    OfxRectD srcFormatCanonical;
    {
        OfxRectI srcFormat;
        _srcClip->getFormat(srcFormat);
        double srcPar = _srcClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
            // no format is available, use the RoD instead
            srcFormatCanonical = _srcClip->getRegionOfDefinition(time);
        } else {
            const OfxPointD rs1 = {1., 1.};
            Coords::toCanonical(srcFormat, rs1, srcPar, &srcFormatCanonical);
        }
    }

    std::vector<std::string> planes;
    _srcClip->getPlanesPresent(&planes);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int rows, columns;
    if ( !_autoDims->getValueAtTime(time) ) {
        _rowsColumns->getValueAtTime(time, rows, columns);
    } else {
        std::size_t n = planes.size();
        double w = rod.x2 - rod.x1;
        double h = rod.y2 - rod.y1;
        double sw = srcFormatCanonical.x2 - srcFormatCanonical.x1;
        double sh = srcFormatCanonical.y2 - srcFormatCanonical.y1;
        columns = (int)std::max( 1., std::floor( std::sqrt( (n * w * sh) / (h * sw) ) ) );
        rows = (int)std::ceil( n / (double) columns);
    }

    for (std::size_t layer = 0; layer < planes.size(); ++layer) {
        int r = layer / columns;
        int c = layer % columns;
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
            auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                           _srcClip->fetchImagePlane(time, planes[layer].c_str()) : 0 );
            if ( !src.get() || !( _srcClip && _srcClip->isConnected() ) ) {
                // nothing to do
                return;
            } else {
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
}


// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
LayerContactSheetPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                   OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;

    // ask for the full format
    OfxRectD srcFormatCanonical;
    {
        OfxRectI srcFormat;
        _srcClip->getFormat(srcFormat);
        double srcPar = _srcClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
            // no format is available, use the RoD instead
            srcFormatCanonical = _srcClip->getRegionOfDefinition(time);
        } else {
            const OfxPointD rs1 = {1., 1.};
            Coords::toCanonical(srcFormat, rs1, srcPar, &srcFormatCanonical);
        }
    }
    rois.setRegionOfInterest(*_srcClip, srcFormatCanonical);
}

bool
LayerContactSheetPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
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



/* Override the clip preferences */
void
LayerContactSheetPlugin::getClipPreferences(OFX::ClipPreferencesSetter & clipPreferences)
{
    updateGUI();

    OfxRectI format = {0, 0, 0, 0};
    _resolution->getValue(format.x2, format.y2);
    clipPreferences.setOutputFormat(format);
    //clipPreferences.setPixelAspectRatio(*_dstClip, par);
}

#ifdef OFX_EXTENSIONS_NUKE
OfxStatus
LayerContactSheetPlugin::getClipComponents(const ClipComponentsArguments& args,
                                           ClipComponentsSetter& clipComponents)
{
    // no pass-through
    clipComponents.setPassThroughClip(NULL, args.time, args.view);

    // produces RGBA only
    clipComponents.addClipPlane(*_dstClip, kFnOfxImagePlaneColour);

    // ask for the first rows * cols first components from the input
    std::vector<std::string> planes;
    _srcClip->getPlanesPresent(&planes);
    for (size_t layer = 0; layer < planes.size(); ++layer) {
        clipComponents.addClipPlane(*_srcClip, planes[layer]);
    }

    return kOfxStatReplyDefault;
}
#endif

void
LayerContactSheetPlugin::changedClip(const InstanceChangedArgs & /*args*/,
                          const std::string & /*clipName*/)
{
}

void
LayerContactSheetPlugin::changedParam(const InstanceChangedArgs &/*args*/,
                           const std::string &paramName)
{
    if (paramName == kParamAutoDims) {
        updateGUI();
    }
}

void
LayerContactSheetPlugin::updateGUI()
{
    bool autoDims = _autoDims->getValue();
    _rowsColumns->setEnabled(!autoDims);
}

//////////// INTERACT

class LayerContactSheetInteract : public OverlayInteract {

public:
    LayerContactSheetInteract(OfxInteractHandle handle, ImageEffect* effect)
    : OverlayInteract(handle)
    , _srcClip(NULL)
    , _dstClip(NULL)
    , _resolution(NULL)
    , _rowsColumns(NULL)
    , _autoDims(NULL)
    , _gap(NULL)
    , _center(NULL)
    , _rowOrder(NULL)
    , _colOrder(NULL)
    , _showLayerNames(NULL)
    {
        _srcClip = effect->fetchClip(kOfxImageEffectSimpleSourceClipName);
        _dstClip = effect->fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
        _resolution  = effect->fetchInt2DParam(kParamResolution);
        _rowsColumns = effect->fetchInt2DParam(kParamRowsColums);
        _autoDims = effect->fetchBooleanParam(kParamAutoDims);
        _gap = effect->fetchIntParam(kParamGap);
        _center = effect->fetchBooleanParam(kParamCenter);
        _rowOrder = effect->fetchChoiceParam(kParamRowOrder);
        _colOrder = effect->fetchChoiceParam(kParamColumnOrder);
        _showLayerNames = effect->fetchBooleanParam(kParamShowLayerNames);
    }

private:
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args) OVERRIDE FINAL;

private:
    Clip* _srcClip;
    Clip* _dstClip;
    Int2DParam *_resolution;
    Int2DParam* _rowsColumns;
    BooleanParam* _autoDims;
    IntParam* _gap;
    BooleanParam* _center;
    ChoiceParam* _rowOrder;
    ChoiceParam* _colOrder;
    BooleanParam* _showLayerNames;

};

// draw the interact
bool
LayerContactSheetInteract::draw(const OFX::DrawArgs &args)
{
    const double time = args.time;

    if ( !_showLayerNames->getValueAtTime(time) ) {
        return false;
    }

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
    //double dstPar = _dstClip->getPixelAspectRatio();

    OfxRectD srcFormatCanonical;
    {
        OfxRectI srcFormat;
        _srcClip->getFormat(srcFormat);
        double srcPar = _srcClip->getPixelAspectRatio();
        if ( OFX::Coords::rectIsEmpty(srcFormat) ) {
            // no format is available, use the RoD instead
            srcFormatCanonical = _srcClip->getRegionOfDefinition(time);
        } else {
            const OfxPointD rs1 = {1., 1.};
            Coords::toCanonical(srcFormat, rs1, srcPar, &srcFormatCanonical);
        }
    }

    std::vector<std::string> planes;
    _srcClip->getPlanesPresent(&planes);

    // now, for each clip, compute the required region of interest, which is the union of the intersection of each cell with the renderWindow
    int rows, columns;
    if ( !_autoDims->getValueAtTime(time) ) {
        _rowsColumns->getValueAtTime(time, rows, columns);
    } else {
        std::size_t n = planes.size();
        double w = rod.x2 - rod.x1;
        double h = rod.y2 - rod.y1;
        double sw = srcFormatCanonical.x2 - srcFormatCanonical.x1;
        double sh = srcFormatCanonical.y2 - srcFormatCanonical.y1;
        columns = (int)std::max( 1., std::floor( std::sqrt( (n * w * sh) / (h * sw) ) ) );
        rows = (int)std::ceil( n / (double) columns);
    }

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

    for (std::size_t layer = 0; layer < planes.size(); ++layer) {
        std::string name = planes[layer];
        {
            std::size_t first_underline = name.find_first_of('_');
            std::size_t second_underline = std::string::npos;
            if (first_underline != std::string::npos) {
                second_underline = name.find_first_of('_', first_underline + 1);
                name = name.substr(first_underline + 1, second_underline - (first_underline + 1));
            }
        }
        std::string prefix("uk.co.thefoundry.OfxImagePlane");
        if (!name.compare(0, prefix.size(), prefix)) {
            name = name.substr(prefix.size());
            if (name == "Colour") {
                name = "Color"; // US English
            }
        }

        /** @brief string to indicate a 2D right stereo disparity image plane be fetched.
         
         Passed to FnOfxImageEffectPlaneSuiteV1::clipGetImagePlane \e plane argument.
         */
#define kFnOfxImagePlaneStereoDisparityRight "uk.co.thefoundry.OfxImagePlaneStereoDisparityRight"

        int r = layer / columns;
        int c = layer % columns;
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

            glColor3f(color.r * l, color.g * l, color.b * l);

            TextRenderer::bitmapString( imageRoD.x1, imageRoD.y1, name.c_str() );
        }
    }

    return true;
}



//////////// FACTORY

mDeclarePluginFactory(LayerContactSheetPluginFactory, {}, {});

class LayerContactSheetOverlayDescriptor : public DefaultEffectOverlayDescriptor<LayerContactSheetOverlayDescriptor, LayerContactSheetInteract> {};

void
LayerContactSheetPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

    desc.setIsMultiPlanar(true);
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelBlockAllNonRenderedPlanes);
#endif
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif

    desc.setOverlayInteractDescriptor( new LayerContactSheetOverlayDescriptor);
}

void
LayerContactSheetPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                                  OFX::ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    {
        ClipDescriptor *srcClip; 
        srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentXY);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // autoDims
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAutoDims);
        param->setLabel(kParamAutoDimsLabel);
        param->setHint(kParamAutoDimsHint);
        param->setAnimates(false);
        param->setDefault(true);
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

    // showLayerNames
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamShowLayerNames);
        param->setLabel(kParamShowLayerNamesLabel);
        param->setHint(kParamShowLayerNamesHint);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
    
} // LayerContactSheetPluginFactory::describeInContext

OFX::ImageEffect*
LayerContactSheetPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    OFX::ContextEnum /*context*/)
{
    return new LayerContactSheetPlugin(handle);
}

static LayerContactSheetPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
