/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * OFX Transform & DirBlur plugins.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <iostream>
#include <limits>
#include <algorithm>

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "ofxsFormatResolution.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReformatOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Convert the image to another format or size.\n" \
    "An image transform is computed that goes from the input region of definition (RoD) to the selected format. The Resize Type parameter adjust the way the transform is computed.\n" \
    "This plugin concatenates transforms.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Reformat"

#define kPluginDescriptionNatron "Convert the image to another format or size.\n" \
    "An image transform is computed that goes from the input format, regardless of the region of definition (RoD), to the selected format. The Resize Type parameter adjust the way the transform is computed.\n" \
    "The output format is set by this effect.\n" \
    "In order to set the output format without transforming the image content, use the NoOp effect.\n" \
    "This plugin concatenates transforms.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Reformat"

#define kPluginIdentifier "net.sf.openfx.Reformat"
// History:
// version 1.0: initial version
// version 1.1: fix https://github.com/MrKepzie/Natron/issues/1397
// version 1.2: add useRoD parameter for Natron
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kParamUseRoD "useRoD"
#define kParamUseRoDLabel "Use Source RoD"
#define kParamUseRoDHint "Use the region of definition of the source as the source format."

#define kParamType "reformatType"
#define kParamTypeLabel "Type"
#define kParamTypeHint "To Format: Converts between formats, the image is resized to fit in the target format. " \
    "To Box: Scales to fit into a box of a given width and height. " \
    "Scale: Scales the image (rounding to integer pixel sizes)."
#define kParamTypeOptionToFormat "To Format", "Resize to predefined format.", "format"
#define kParamTypeOptionToBox "To Box", "Resize to given bounding box.", "box"
#define kParamTypeOptionScale "Scale", "Apply scale.", "scale"

enum ReformatTypeEnum
{
    eReformatTypeToFormat = 0,
    eReformatTypeToBox,
    eReformatTypeScale,
};

#define kParamFormat kNatronParamFormatChoice
#define kParamFormatLabel "Format"
#define kParamFormatHint "The output format"
#define kParamFormatDefault eParamFormatPCVideo


#define kParamFormatBoxSize kNatronParamFormatSize
#define kParamFormatBoxSizeLabel "Size"
#define kParamFormatBoxSizeHint "The output dimensions of the image in pixels."

#define kParamFormatBoxPAR kNatronParamFormatPar
#define kParamFormatBoxPARLabel "Pixel Aspect Ratio"
#define kParamFormatBoxPARHint "Output pixel aspect ratio."

#define kParamBoxSize "boxSize"
#define kParamBoxSizeLabel "Size"
#define kParamBoxSizeHint "The output dimensions of the image in pixels."

#define kParamBoxFixed "boxFixed"
#define kParamBoxFixedLabel "Force This Shape"
#define kParamBoxFixedHint "If checked, the output image is cropped to this size. Else, image is resized according to the resize type but the whole image is kept."

#define kParamBoxPAR "boxPar"
#define kParamBoxPARLabel "Pixel Aspect Ratio"
#define kParamBoxPARHint "Output pixel aspect ratio."

#define kParamScale "reformatScale"
#define kParamScaleLabel "Scale"
#define kParamScaleHint "The scale factor to apply to the image. The scale factor is rounded slightly, so that the output image is an integer number of pixels in the direction chosen under resize type."

#define kParamScaleUniform "reformatScaleUniform"
#define kParamScaleUniformLabel "Uniform"
#define kParamScaleUniformHint "Use the X scale for both directions"

#define kParamResize "resize"
#define kParamResizeLabel "Resize Type"
#define kParamResizeHint "Format: Converts between formats, the image is resized to fit in the target format. " \
    "Size: Scales to fit into a box of a given width and height. " \
    "Scale: Scales the image."
#define kParamResizeOptionNone "None", "Do not resize the original.", "none"
#define kParamResizeOptionWidth "Width", "Scale the original so that its width fits the output width, while preserving the aspect ratio.", "width"
#define kParamResizeOptionHeight "Height", "Scale the original so that its height fits the output height, while preserving the aspect ratio.", "height"
#define kParamResizeOptionFit "Fit", "Scale the original so that its smallest size fits the output width or height, while preserving the aspect ratio.", "fit"
#define kParamResizeOptionFill "Fill", "Scale the original so that its longest size fits the output width or height, while preserving the aspect ratio.", "fill"
#define kParamResizeOptionDistort "Distort", "Scale the original so that both sides fit the output dimensions. This does not preserve the aspect ratio.", "distort"

enum ResizeEnum
{
    eResizeNone = 0,
    eResizeWidth,
    eResizeHeight,
    eResizeFit,
    eResizeFill,
    eResizeDistort,
};


#define kParamReformatCenter "reformatCentered"
#define kParamReformatCenterLabel "Center"
#define kParamReformatCenterHint "Translate the center of the image to the center of the output. Otherwise, the lower left corner is left untouched."

#define kParamFlip "flip"
#define kParamFlipLabel "Flip"
#define kParamFlipHint "Mirror the image vertically."

#define kParamFlop "flop"
#define kParamFlopLabel "Flop"
#define kParamFlopHint "Mirror the image horizontally."

#define kParamTurn "turn"
#define kParamTurnLabel "Turn"
#define kParamTurnHint "Rotate the image by 90 degrees counter-clockwise."

#define kParamPreserveBoundingBox "preserveBB"
#define kParamPreserveBoundingBoxLabel "Preserve BBox" // and Concat"
#define kParamPreserveBoundingBoxHint \
    "If checked, preserve the whole image bounding box and concatenate transforms downstream.\n" \
    "Normally, all pixels outside of the outside format are clipped off. If this is checked, the whole image RoD is kept.\n" \
    "By default, transforms are only concatenated upstream, i.e. the image is rendered by this effect by concatenating upstream transforms (e.g. CornerPin, Transform...), and the original image is resampled only once. If checked, and there are concatenating transform effects downstream, the image is rendered by the last consecutive concatenating effect."


static bool gHostCanTransform;
static bool gHostIsNatron = false;
static bool gHostSupportsFormat = false;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReformatPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    ReformatPlugin(OfxImageEffectHandle handle)
        : Transform3x3Plugin(handle, false, eTransform3x3ParamsTypeNone)
        , _type(NULL)
        , _format(NULL)
        , _boxSize(NULL)
        , _boxFixed(NULL)
        , _boxPAR(NULL)
        , _scale(NULL)
        , _scaleUniform(NULL)
        , _preserveBB(NULL)
        , _resize(NULL)
        , _center(NULL)
        , _flip(NULL)
        , _flop(NULL)
        , _turn(NULL)
    {
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);

        // NON-GENERIC
        _useRoD = fetchBooleanParam(kParamUseRoD);
        _type = fetchChoiceParam(kParamType);
        _format = fetchChoiceParam(kParamFormat);
        _formatBoxSize = fetchInt2DParam(kParamFormatBoxSize);
        _formatBoxPAR = fetchDoubleParam(kParamFormatBoxPAR);
        _boxSize = fetchInt2DParam(kParamBoxSize);
        _boxFixed = fetchBooleanParam(kParamBoxFixed);
        _boxPAR = fetchDoubleParam(kParamBoxPAR);
        _scale = fetchDouble2DParam(kParamScale);
        _scaleUniform = fetchBooleanParam(kParamScaleUniform);
        _preserveBB = fetchBooleanParam(kParamPreserveBoundingBox);
        _resize = fetchChoiceParam(kParamResize);
        _center = fetchBooleanParam(kParamReformatCenter);
        _flip = fetchBooleanParam(kParamFlip);
        _flop = fetchBooleanParam(kParamFlop);
        _turn = fetchBooleanParam(kParamTurn);
        assert(_useRoD && _type && _format && _boxSize && _boxFixed && _boxPAR && _scale && _scaleUniform && _preserveBB && _resize && _center && _flip && _flop && _turn);


        if (!gHostIsNatron) {
            // try to guess the output format from the project size
            // do it only if not Natron otherwise this will override what the host has set in the format when loading
            double projectPAR = getProjectPixelAspectRatio();
            OfxPointD projectSize = getProjectSize();

            ///Try to find a format matching the project format in which case we switch to format mode otherwise
            ///switch to size mode and set the size accordingly
            bool foundFormat = false;
            for (int i = 0; i < (int)eParamFormatCount; ++i) {
                int width = 0, height = 0;
                double par = -1.;
                getFormatResolution( (EParamFormat)i, &width, &height, &par );
                assert(par != -1);
                if ( (width * par == projectSize.x) && (height == projectSize.y) && (std::abs(par - projectPAR) < 0.01) ) {
                    _type->setValue( (int)eReformatTypeToFormat );
                    _format->setValue( (EParamFormat)i );
                    _boxSize->setValue(width, height);
                    _formatBoxSize->setValue(width, height);
                    _boxPAR->setValue(par);
                    _formatBoxPAR->setValue(par);
                    foundFormat = true;
                    break;
                }
            }
            if (!foundFormat) {
                _type->setValue( (int)eReformatTypeToBox );
                _boxSize->setValue(projectSize.x / projectPAR, projectSize.y);
                _formatBoxSize->setValue(projectSize.x / projectPAR, projectSize.y);
                _boxPAR->setValue(projectPAR);
                _formatBoxPAR->setValue(projectPAR);
                _boxFixed->setValue(true);
            }
        }
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_scaleUniform->getValue() &&
             ( _scaleUniform->getNumKeys() == 0) ) {
            _scaleUniform->setIsSecretAndDisabled(true);
        }

        refreshVisibility();
        refreshDynamicProps();
    }

private:
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    void refreshVisibility();

    void refreshDynamicProps();

    void setBoxValues(double time);

    void getInputFormat(const double time,
                        double* par,
                        OfxRectD* rect) const;

    void getOutputFormat(const double time,
                         double* par,
                         OfxRectD* rect, // the rect to which the input format is mapped
                         OfxRectI* format) const; // the full format (only useful if host supports format, really)

    // NON-GENERIC
    BooleanParam* _useRoD;
    ChoiceParam *_type;
    ChoiceParam *_format;
    Int2DParam *_formatBoxSize;
    DoubleParam* _formatBoxPAR;
    Int2DParam *_boxSize;
    BooleanParam* _boxFixed;
    DoubleParam* _boxPAR;
    Double2DParam *_scale;
    BooleanParam* _scaleUniform;
    BooleanParam* _preserveBB;
    ChoiceParam *_resize;
    BooleanParam* _center;
    BooleanParam* _flip;
    BooleanParam* _flop;
    BooleanParam* _turn;
};

// override the rod call
bool
ReformatPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    if ( !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }

    bool ret = Transform3x3Plugin::getRegionOfDefinition(args, rod);
    if ( !ret ||
         _preserveBB->getValue() ) {
        return ret;
    }

    const double time = args.time;
    // intersect with format RoD
    OfxRectD rect;
    OfxRectI format;
    double par;
    getOutputFormat(time, &par, &rect, &format);
    const OfxPointD rsOne = {1., 1.}; // format is with respect to unit renderscale
    OfxRectD formatrod;
    Coords::toCanonical(format, rsOne, par, &formatrod);

    Coords::rectIntersection(rod, formatrod, &rod);

    return true;
} // ReformatPlugin::getRegionOfDefinition

// overridden is identity
bool
ReformatPlugin::isIdentity(const double time)
{
    if ( _center->getValueAtTime(time) || _flip->getValueAtTime(time) ||
         _flop->getValueAtTime(time)   || _turn->getValueAtTime(time) ) {
        return false;
    }

    if ( (ResizeEnum)_resize->getValueAtTime(time) == eResizeNone ) {
        return true;
    }

    return false;
}

// recturn the input format in pixel units (we use a RectD in case the input format is the RoD)
void
ReformatPlugin::getInputFormat(const double time,
                               double* par,
                               OfxRectD* rect) const
{
    *par = _srcClip->getPixelAspectRatio();
#ifdef OFX_EXTENSIONS_NATRON
    if (gHostSupportsFormat && !_useRoD->getValueAtTime(time)) {
        OfxRectI format;
        _srcClip->getFormat(format);
        if ( !Coords::rectIsEmpty(format) ) {
            // host returned a non-empty format
            rect->x1 = format.x1;
            rect->y1 = format.y1;
            rect->x2 = format.x2;
            rect->y2 = format.y2;
            return;
        }
    }
    // host does not support format
#endif
    OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
    const OfxPointD rsOne = {1., 1.}; // format is with respect to unit renderscale
    Coords::toPixelSub(srcRod, rsOne, *par, rect);
}

void
ReformatPlugin::getOutputFormat(const double time,
                                double* par,
                                OfxRectD* rect,
                                OfxRectI* format) const
{
    int type_i;

    _type->getValue(type_i);
    ReformatTypeEnum typeVal = (ReformatTypeEnum)type_i;
    OfxPointI boxSize;
    double boxPAR;
    bool boxFixed;
    switch (typeVal) {
    case eReformatTypeToFormat:
        boxFixed = true;
        boxSize = _formatBoxSize->getValueAtTime(time);
        boxPAR = _formatBoxPAR->getValueAtTime(time);
        break;
    case eReformatTypeScale:
        boxFixed = true;
        boxSize = _boxSize->getValueAtTime(time);
        boxPAR = _boxPAR->getValueAtTime(time);
        break;
    case eReformatTypeToBox:
        boxFixed = _boxFixed->getValue();
        boxSize = _boxSize->getValueAtTime(time);
        boxPAR = _boxPAR->getValueAtTime(time);
        break;
    }

    ResizeEnum resize = (ResizeEnum)_resize->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);
    //bool flip = _flip->getValueAtTime(time);
    //bool flop = _flop->getValueAtTime(time);
    bool turn = _turn->getValueAtTime(time);
    // same as getRegionOfDefinition, but without rounding, and without conversion to pixels


    if (format && boxFixed) { // the non-boxFixed case is treated at the end of the function
        format->x1 = format->y1 = 0;
        format->x2 = boxSize.x;
        format->y2 = boxSize.y;
    }

    if ( (boxSize.x == 0) && (boxSize.y == 0) ) {
        //probably scale is 0
        rect->x1 = rect->y1 = rect->x2 = rect->y2 = 0;
        *par = 1.;

        return;
    }
    OfxRectD boxRod = { 0., 0., (double)boxSize.x * boxPAR, (double)boxSize.y};
#ifdef OFX_EXTENSIONS_NATRON
    OfxRectD srcRod;
    OfxRectI srcFormat;
    _srcClip->getFormat(srcFormat);
    if ( Coords::rectIsEmpty(srcFormat) ) {
        srcRod = _srcClip->getRegionOfDefinition(time);
    } else {
        // host returned a non-empty format, use it as the src rod to compute the transform
        double srcPar = _srcClip->getPixelAspectRatio();
        const OfxPointD rsOne = {1., 1.}; // format is always with respect to unit renderscale
        Coords::toCanonical(srcFormat, rsOne, srcPar, &srcRod);
    }
#else
    OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
#endif
    if ( Coords::rectIsEmpty(srcRod) ) {
        // degenerate case
        rect->x1 = rect->y1 = rect->x2 = rect->y2 = 0;
        *par = 1.;

        return;
    }
    double srcw = srcRod.x2 - srcRod.x1;
    double srch = srcRod.y2 - srcRod.y1;
    // if turn, inverse both dimensions
    if (turn) {
        std::swap(srcw, srch);
    }
    // if fit or fill, determine if it should be fit to width or height
    if (resize == eResizeFit) {
        if (boxRod.x2 * srch > boxRod.y2 * srcw) {
            resize = eResizeHeight;
        } else {
            resize = eResizeWidth;
        }
    } else if (resize == eResizeFill) {
        if (boxRod.x2 * srch > boxRod.y2 * srcw) {
            resize = eResizeWidth;
        } else {
            resize = eResizeHeight;
        }
    }

    OfxRectD dstRod;
    dstRod.x1 = dstRod.y1 = dstRod.x2 = dstRod.y2 = 0.;
    if (resize == eResizeNone) {
        if (center && boxFixed) {
            // translate the source
            double xoff = ( (boxRod.x1 + boxRod.x2) - (srcRod.x1 + srcRod.x2) ) / 2;
            double yoff = ( (boxRod.y1 + boxRod.y2) - (srcRod.y1 + srcRod.y2) ) / 2;
            dstRod.x1 = srcRod.x1 + xoff;
            dstRod.x2 = srcRod.x2 + xoff;
            dstRod.y1 = srcRod.y1 + yoff;
            dstRod.y2 = srcRod.y2 + yoff;
        } else {
            // identity, with RoD = dstRod for flip/flop/turn
            srcRod = dstRod = boxRod;
        }
    } else if (resize == eResizeDistort) {
        // easy case
        dstRod.x2 = boxRod.x2;
        dstRod.y2 = boxRod.y2;
    } else if (resize == eResizeWidth) {
        double scale = boxRod.x2 / srcw;
        dstRod.x2 = boxRod.x2;
        double dsth = srch * scale;
        double offset = (center && boxFixed) ? (boxRod.y2 - dsth) / 2 : 0;
        dstRod.y1 = offset;
        dstRod.y2 = offset + dsth;
    } else if (resize == eResizeHeight) {
        double scale = boxRod.y2 / srch;
        double dstw = srcw * scale;
        double offset = (center && boxFixed) ? (boxRod.x2 - dstw) / 2 : 0;
        dstRod.x1 = offset;
        dstRod.x2 = offset + dstw;
        dstRod.y2 = boxRod.y2;
    }
    assert( !Coords::rectIsEmpty(dstRod) );
    *par = boxPAR;
    const OfxPointD rsOne = {1., 1.}; // format is with respect to unit renderscale
    Coords::toPixelSub(dstRod, rsOne, *par, rect);
    if (format && !boxFixed) {
        Coords::toPixelNearest(dstRod, rsOne, *par, format);
    }
} // ReformatPlugin::getOutputFormat

bool
ReformatPlugin::getInverseTransformCanonical(const double time,
                                             const int /*view*/,
                                             const double /*amount*/,
                                             const bool invert,
                                             Matrix3x3* invtransform) const
{
    if ( !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }

    OfxRectD srcRod, dstRod;
    {
        double par;
        OfxRectD format;
        const OfxPointD rsOne = {1., 1.}; // format is with respect to unit renderscale
        getInputFormat(time, &par, &format);
        Coords::toCanonical(format, rsOne, par, &srcRod);
        getOutputFormat(time, &par, &format, NULL);
        Coords::toCanonical(format, rsOne, par, &dstRod);
    }
    bool flip = _flip->getValueAtTime(time);
    bool flop = _flop->getValueAtTime(time);
    bool turn = _turn->getValueAtTime(time);

    // flip/flop.
    // be careful, srcRod may be empty after this, because bounds are swapped,
    // but this is used for transform computation
    if (flip) {
        std::swap(srcRod.y1, srcRod.y2);
    }
    if (flop) {
        std::swap(srcRod.x1, srcRod.x2);
    }
    if (!invert) {
        if ( (dstRod.x1 == dstRod.x2) ||
             ( dstRod.y1 == dstRod.y2) ) {
            return false;
        }
        // now, compute the transform from dstRod to srcRod
        if (!turn) {
            // simple case: no rotation
            // x <- srcRod.x1 + (x - dstRod.x1) * (srcRod.x2 - srcRod.x1) / (dstRod.x2 - dstRod.x1)
            // y <- srcRod.y1 + (y - dstRod.y1) * (srcRod.y2 - srcRod.y1) / (dstRod.y2 - dstRod.y1)
            double ax = (srcRod.x2 - srcRod.x1) / (dstRod.x2 - dstRod.x1);
            double ay = (srcRod.y2 - srcRod.y1) / (dstRod.y2 - dstRod.y1);
            assert(ax == ax && ay == ay);
            (*invtransform)(0,0) = ax; (*invtransform)(0,1) =  0; (*invtransform)(0,2) = srcRod.x1 - dstRod.x1 * ax;
            (*invtransform)(1,0) =  0; (*invtransform)(1,1) = ay; (*invtransform)(1,2) = srcRod.y1 - dstRod.y1 * ay;
            (*invtransform)(2,0) =  0; (*invtransform)(2,1) =  0; (*invtransform)(2,2) = 1.;
        } else {
            // rotation 90 degrees counterclockwise
            // x <- srcRod.x1 + (y - dstRod.y1) * (srcRod.x2 - srcRod.x1) / (dstRod.y2 - dstRod.y1)
            // y <- srcRod.y1 + (dstRod.x2 - x) * (srcRod.y2 - srcRod.y1) / (dstRod.x2 - dstRod.x1)
            double ax = (srcRod.x2 - srcRod.x1) / (dstRod.y2 - dstRod.y1);
            double ay = (srcRod.y2 - srcRod.y1) / (dstRod.x2 - dstRod.x1);
            assert(ax == ax && ay == ay);
            (*invtransform)(0,0) =  0; (*invtransform)(0,1) = ax; (*invtransform)(0,2) = srcRod.x1 - dstRod.y1 * ax;
            (*invtransform)(1,0) = -ay; (*invtransform)(1,1) =  0; (*invtransform)(1,2) = srcRod.y1 + dstRod.x2 * ay;
            (*invtransform)(2,0) =  0; (*invtransform)(2,1) =  0; (*invtransform)(2,2) = 1.;
        }
    } else { // invert
        if ( (srcRod.x1 == srcRod.x2) ||
             ( srcRod.y1 == srcRod.y2) ) {
            return false;
        }
        // now, compute the transform from srcRod to dstRod
        if (!turn) {
            // simple case: no rotation
            // x <- dstRod.x1 + (x - srcRod.x1) * (dstRod.x2 - dstRod.x1) / (srcRod.x2 - srcRod.x1)
            // y <- dstRod.y1 + (y - srcRod.y1) * (dstRod.y2 - dstRod.y1) / (srcRod.y2 - srcRod.y1)
            double ax = (dstRod.x2 - dstRod.x1) / (srcRod.x2 - srcRod.x1);
            double ay = (dstRod.y2 - dstRod.y1) / (srcRod.y2 - srcRod.y1);
            assert(ax == ax && ay == ay);
            (*invtransform)(0,0) = ax; (*invtransform)(0,1) =  0; (*invtransform)(0,2) = dstRod.x1 - srcRod.x1 * ax;
            (*invtransform)(1,0) =  0; (*invtransform)(1,1) = ay; (*invtransform)(1,2) = dstRod.y1 - srcRod.y1 * ay;
            (*invtransform)(2,0) =  0; (*invtransform)(2,1) =  0; (*invtransform)(2,2) = 1.;
        } else {
            // rotation 90 degrees counterclockwise
            // x <- dstRod.x1 + (srcRod.y2 - y) * (dstRod.x2 - dstRod.x1) / (srcRod.y2 - srcRod.y1)
            // y <- dstRod.y1 + (x - srcRod.x1) * (dstRod.y2 - dstRod.y1) / (srcRod.x2 - srcRod.x1)
            double ax = (dstRod.x2 - dstRod.x1) / (srcRod.y2 - srcRod.y1);
            double ay = (dstRod.y2 - dstRod.y1) / (srcRod.x2 - srcRod.x1);
            assert(ax == ax && ay == ay);
            (*invtransform)(0,0) =  0; (*invtransform)(0,1) = -ax; (*invtransform)(0,2) = dstRod.x1 + srcRod.y2 * ax;
            (*invtransform)(1,0) = ay; (*invtransform)(1,1) =  0; (*invtransform)(1,2) = dstRod.y1 - srcRod.x1 * ay;
            (*invtransform)(2,0) =  0; (*invtransform)(2,1) =  0; (*invtransform)(2,2) = 1.;
        }
    }
    assert((*invtransform)(0,0) == (*invtransform)(0,0) && (*invtransform)(0,1) == (*invtransform)(0,1) && (*invtransform)(0,2) == (*invtransform)(0,2) &&
           (*invtransform)(1,0) == (*invtransform)(1,0) && (*invtransform)(1,1) == (*invtransform)(1,1) && (*invtransform)(1,2) == (*invtransform)(1,2) &&
           (*invtransform)(2,0) == (*invtransform)(2,0) && (*invtransform)(2,1) == (*invtransform)(2,1) && (*invtransform)(2,2) == (*invtransform)(2,2));

    return true;
} // ReformatPlugin::getInverseTransformCanonical

void
ReformatPlugin::setBoxValues(const double time)
{
    ReformatTypeEnum type = (ReformatTypeEnum)_type->getValue();

    switch (type) {
    case eReformatTypeToFormat: {
        //size & par have been set by natron with the Format choice extension
        if (!gHostIsNatron) {
            EParamFormat format = (EParamFormat)_format->getValue();
            assert(0 <= (int)format && (int)format < eParamFormatCount);
            int w = 0, h = 0;
            double par = -1;
            getFormatResolution(format, &w, &h, &par);
            assert(par != -1);
            _formatBoxSize->setValue(w, h);
            _formatBoxPAR->setValue(par);
        }


        _boxFixed->setValue(true);
        break;
    }
    case eReformatTypeToBox:
        // nothing to do, the user sets the box
        break;
    case eReformatTypeScale: {
        OfxPointD scale = _scale->getValue();
        if ( _scaleUniform->getValue() ) {
            scale.y = scale.x;
        }
        double srcPar = _srcClip->getPixelAspectRatio();
        OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
        // scale the RoD
        srcRod.x1 *= scale.x;
        srcRod.x2 *= scale.x;
        srcRod.y1 *= scale.y;
        srcRod.y2 *= scale.y;
        // round to the nearest pixel size
        OfxRectI srcRodPixel;
        OfxPointD rs = {1., 1.};
        Coords::toPixelNearest(srcRod, rs, srcPar, &srcRodPixel);
        int w = srcRodPixel.x2 - srcRodPixel.x1;
        int h = srcRodPixel.y2 - srcRodPixel.y1;
        _boxSize->setValue(w, h);
        _boxPAR->setValue(srcPar);
        _boxFixed->setValue(true);
    }
    }
} // ReformatPlugin::setBoxValues

void
ReformatPlugin::changedParam(const InstanceChangedArgs &args,
                             const std::string &paramName)
{
    // must clear persistent message, or render() is not called by Nuke
    clearPersistentMessage();

    if (paramName == kParamType) {
        refreshVisibility();
    }
    if ( (paramName == kParamType) || (paramName == kParamFormat) || (paramName == kParamScale) || (paramName == kParamScaleUniform) ) {
        setBoxValues(args.time);

        return;
    }

    if (paramName == kParamPreserveBoundingBox) {
        refreshDynamicProps();

        return;
    }

    return Transform3x3Plugin::changedParam(args, paramName);
}

void
ReformatPlugin::refreshDynamicProps()
{
    setCanTransform( _preserveBB->getValue() );
}

void
ReformatPlugin::refreshVisibility()
{
    ReformatTypeEnum type = (ReformatTypeEnum)_type->getValue();

    switch (type) {
    case eReformatTypeToFormat:
        _format->setIsSecretAndDisabled(false);
        _boxSize->setIsSecretAndDisabled(true);
        _boxPAR->setIsSecretAndDisabled(true);
        _boxFixed->setIsSecretAndDisabled(true);
        _scale->setIsSecretAndDisabled(true);
        _scaleUniform->setIsSecretAndDisabled(true);
        break;

    case eReformatTypeToBox:
        _format->setIsSecretAndDisabled(true);
        _boxSize->setIsSecretAndDisabled(false);
        _boxPAR->setIsSecretAndDisabled(false);
        _boxFixed->setIsSecretAndDisabled(false);
        _scale->setIsSecretAndDisabled(true);
        _scaleUniform->setIsSecretAndDisabled(true);
        break;

    case eReformatTypeScale:
        _format->setIsSecretAndDisabled(true);
        _boxSize->setIsSecretAndDisabled(true);
        _boxPAR->setIsSecretAndDisabled(true);
        _boxFixed->setIsSecretAndDisabled(true);
        _scale->setIsSecretAndDisabled(false);
        _scaleUniform->setIsSecretAndDisabled(false);
        break;
    }
}

void
ReformatPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    ReformatTypeEnum type = (ReformatTypeEnum)_type->getValue();
    double par;
    OfxRectD rect;
    OfxRectI format;

    getOutputFormat(0., &par, &rect, &format);

    switch (type) {
    case eReformatTypeToFormat:
    case eReformatTypeToBox: {
        //specific output PAR
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
        break;
    }

    case eReformatTypeScale:
        // don't change the pixel aspect ratio
        break;
    }
#ifdef OFX_EXTENSIONS_NATRON
    clipPreferences.setOutputFormat(format);
#endif
}

mDeclarePluginFactory(ReformatPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ReformatPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(getImageEffectHostDescription()->isNatron ? kPluginDescriptionNatron : kPluginDescription);
    Transform3x3Describe(desc, false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsMultipleClipPARs(true);
    gHostCanTransform = false;

#ifdef OFX_EXTENSIONS_NUKE
    if (getImageEffectHostDescription()->canTransform) {
        gHostCanTransform = true;
        // say the effect implements getTransform(), even though transform concatenation
        // may be disabled (see ReformatPlugin::refreshDynamicProps())
        desc.setCanTransform(true);
    }
#endif
#ifdef OFX_EXTENSIONS_NATRON
    if (getImageEffectHostDescription()->isNatron) {
        gHostIsNatron = true;
        gHostSupportsFormat = true;
    }
#endif
}

void
ReformatPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseRoD);
        param->setLabel(kParamUseRoDLabel);
        param->setHint(kParamUseRoDHint);
        // for now, only Natron supports OFX format extension
        param->setEnabled(gHostSupportsFormat);
        param->setDefault(!gHostSupportsFormat);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // type
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamType);
        param->setLabel(kParamTypeLabel);
        param->setHint(kParamTypeHint);
        assert(param->getNOptions() == eReformatTypeToFormat);
        param->appendOption(kParamTypeOptionToFormat);
        assert(param->getNOptions() == eReformatTypeToBox);
        param->appendOption(kParamTypeOptionToBox);
        assert(param->getNOptions() == eReformatTypeScale);
        param->appendOption(kParamTypeOptionScale);
        param->setDefault(0);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // format
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamFormat);
        param->setLabel(kParamFormatLabel);
        param->setHint(kParamFormatHint);
        assert(param->getNOptions() == eParamFormatPCVideo);
        param->appendOption(kParamFormatPCVideoLabel, "", kParamFormatPCVideo);
        assert(param->getNOptions() == eParamFormatNTSC);
        param->appendOption(kParamFormatNTSCLabel, "", kParamFormatNTSC);
        assert(param->getNOptions() == eParamFormatPAL);
        param->appendOption(kParamFormatPALLabel, "", kParamFormatPAL);
        assert(param->getNOptions() == eParamFormatNTSC169);
        param->appendOption(kParamFormatNTSC169Label, "", kParamFormatNTSC169);
        assert(param->getNOptions() == eParamFormatPAL169);
        param->appendOption(kParamFormatPAL169Label, "", kParamFormatPAL169);
        assert(param->getNOptions() == eParamFormatHD720);
        param->appendOption(kParamFormatHD720Label, "", kParamFormatHD720);
        assert(param->getNOptions() == eParamFormatHD);
        param->appendOption(kParamFormatHDLabel, "", kParamFormatHD);
        assert(param->getNOptions() == eParamFormatUHD4K);
        param->appendOption(kParamFormatUHD4KLabel, "", kParamFormatUHD4K);
        assert(param->getNOptions() == eParamFormat1kSuper35);
        param->appendOption(kParamFormat1kSuper35Label, "", kParamFormat1kSuper35);
        assert(param->getNOptions() == eParamFormat1kCinemascope);
        param->appendOption(kParamFormat1kCinemascopeLabel, "", kParamFormat1kCinemascope);
        assert(param->getNOptions() == eParamFormat2kSuper35);
        param->appendOption(kParamFormat2kSuper35Label, "", kParamFormat2kSuper35);
        assert(param->getNOptions() == eParamFormat2kCinemascope);
        param->appendOption(kParamFormat2kCinemascopeLabel, "", kParamFormat2kCinemascope);
        assert(param->getNOptions() == eParamFormat2kDCP);
        param->appendOption(kParamFormat2kDCPLabel, "", kParamFormat2kDCP);
        assert(param->getNOptions() == eParamFormat4kSuper35);
        param->appendOption(kParamFormat4kSuper35Label, "", kParamFormat4kSuper35);
        assert(param->getNOptions() == eParamFormat4kCinemascope);
        param->appendOption(kParamFormat4kCinemascopeLabel, "", kParamFormat4kCinemascope);
        assert(param->getNOptions() == eParamFormat4kDCP);
        param->appendOption(kParamFormat4kDCPLabel, "", kParamFormat4kDCP);
        assert(param->getNOptions() == eParamFormatSquare256);
        param->appendOption(kParamFormatSquare256Label, "", kParamFormatSquare256);
        assert(param->getNOptions() == eParamFormatSquare512);
        param->appendOption(kParamFormatSquare512Label, "", kParamFormatSquare512);
        assert(param->getNOptions() == eParamFormatSquare1k);
        param->appendOption(kParamFormatSquare1kLabel, "", kParamFormatSquare1k);
        assert(param->getNOptions() == eParamFormatSquare2k);
        param->appendOption(kParamFormatSquare2kLabel, "", kParamFormatSquare2k);
        param->setDefault(kParamFormatDefault);
        param->setHint(kParamFormatHint);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        Int2DParamDescriptor* param = desc.defineInt2DParam(kParamFormatBoxSize);
        param->setLabel(kParamFormatBoxSizeLabel);
        param->setHint(kParamFormatBoxSizeHint);
        param->setDefault(200, 200);
        param->setIsSecretAndDisabled(true); // secret Natron-specific param
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFormatBoxPAR);
        param->setLabel(kParamFormatBoxPARLabel);
        param->setHint(kParamFormatBoxPARHint);
        param->setRange(0., 10);
        param->setDisplayRange(0.5, 2.);
        param->setDefault(1.);
        param->setIsSecretAndDisabled(true); // secret Natron-specific param
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }


    {
        Int2DParamDescriptor* param = desc.defineInt2DParam(kParamBoxSize);
        param->setLabel(kParamBoxSizeLabel);
        param->setHint(kParamBoxSizeHint);
        param->setDefault(200, 200);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBoxFixed);
        param->setLabel(kParamBoxFixedLabel);
        param->setHint(kParamBoxFixedHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBoxPAR);
        param->setLabel(kParamBoxPARLabel);
        param->setHint(kParamBoxPARHint);
        param->setRange(0., 10);
        param->setDisplayRange(0.5, 2.);
        param->setDefault(1.);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // scale
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamScale);
        param->setLabel(kParamScaleLabel);
        param->setHint(kParamScaleHint);
        param->setDoubleType(eDoubleTypeScale);
        //param->setDimensionLabels("w","h");
        param->setDefault(1, 1);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(0.1, 0.1, 10, 10);
        param->setIncrement(0.01);
        param->setUseHostNativeOverlayHandle(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // scaleUniform
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamScaleUniform);
        param->setLabel(kParamScaleUniformLabel);
        param->setHint(kParamScaleUniformHint);
        // uniform parameter is false by default on Natron
        // https://github.com/MrKepzie/Natron/issues/1204
        param->setDefault(!getImageEffectHostDescription()->isNatron);
        param->setLayoutHint(eLayoutHintDivider);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // resize
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamResize);
        param->setLabel(kParamResizeLabel);
        param->setHint(kParamResizeHint);
        assert(param->getNOptions() == eResizeNone);
        param->appendOption(kParamResizeOptionNone);
        assert(param->getNOptions() == eResizeWidth);
        param->appendOption(kParamResizeOptionWidth);
        assert(param->getNOptions() == eResizeHeight);
        param->appendOption(kParamResizeOptionHeight);
        assert(param->getNOptions() == eResizeFit);
        param->appendOption(kParamResizeOptionFit);
        assert(param->getNOptions() == eResizeFill);
        param->appendOption(kParamResizeOptionFill);
        assert(param->getNOptions() == eResizeDistort);
        param->appendOption(kParamResizeOptionDistort);
        param->setDefault( (int)eResizeWidth );
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // center
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamReformatCenter);
        param->setLabel(kParamReformatCenterLabel);
        param->setHint(kParamReformatCenterHint);
        param->setDefault(true);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // flip
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFlip);
        param->setLabel(kParamFlipLabel);
        param->setHint(kParamFlipHint);
        param->setDefault(false);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // flop
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamFlop);
        param->setLabel(kParamFlopLabel);
        param->setHint(kParamFlopHint);
        param->setDefault(false);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // turn
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTurn);
        param->setLabel(kParamTurnLabel);
        param->setHint(kParamTurnHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        param->getPropertySet().propSetInt(kOfxParamPropLayoutPadWidth, 1, false);
        if (page) {
            page->addChild(*param);
        }
    }

    // Preserve bounding box
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPreserveBoundingBox);
        param->setLabel(kParamPreserveBoundingBoxLabel);
        param->setHint(kParamPreserveBoundingBoxHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    // clamp, filter, black outside
    ofxsFilterDescribeParamsInterpolate2D(desc, page, /*blackOutsideDefault*/ false);
} // ReformatPluginFactory::describeInContext

ImageEffect*
ReformatPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new ReformatPlugin(handle);
}

static ReformatPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
