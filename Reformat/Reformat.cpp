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
 * OFX Transform & DirBlur plugins.
 */

#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "ofxsFormatResolution.h"
#include "ofxsCoords.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ReformatOFX"
#define kPluginGrouping "Transform"
#define kPluginDescription "Convert the image to another format or size\n" \
    "This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.Reformat"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kParamType "reformatType"
#define kParamTypeLabel "Type"
#define kParamTypeHint "To Format: Converts between formats, the image is resized to fit in the target format. " \
    "To Box: Scales to fit into a box of a given width and height. " \
    "Scale: Scales the image (rounding to integer pixel sizes)."
#define kParamTypeOptionToFormat "To Format"
#define kParamTypeOptionToBox "To Box"
#define kParamTypeOptionScale "Scale"

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

#define kParamBoxSize kNatronParamFormatSize
#define kParamBoxSizeLabel "Size"
#define kParamBoxSizeHint "The output dimensions of the image in pixels."

#define kParamBoxFixed "boxFixed"
#define kParamBoxFixedLabel "Force This Shape"
#define kParamBoxFixedHint "If checked, the output image is cropped to this size. Else, image is resized according to the resize type but the whole image is kept."

#define kParamBoxPAR kNatronParamFormatPar
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
#define kParamResizeOptionNone "None"
#define kParamResizeOptionNoneHint "Do not resize the original."
#define kParamResizeOptionWidth "Width"
#define kParamResizeOptionWidthHint "Scale the original so that its width fits the output width, while preserving the aspect ratio."
#define kParamResizeOptionHeight "Height"
#define kParamResizeOptionHeightHint "Scale the original so that its height fits the output height, while preserving the aspect ratio."
#define kParamResizeOptionFit "Fit"
#define kParamResizeOptionFitHint "Scale the original so that its smallest size fits the output width or height, while preserving the aspect ratio."
#define kParamResizeOptionFill "Fill"
#define kParamResizeOptionFillHint "Scale the original so that its longest size fits the output width or height, while preserving the aspect ratio."
#define kParamResizeOptionDistort "Distort"
#define kParamResizeOptionDistortHint "Scale the original so that both sides fit the output dimensions. This does not preserve the aspect ratio."

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
#define kParamPreserveBoundingBoxLabel "Preserve BBox & Concat"
#define kParamPreserveBoundingBoxHint \
    "If checked, preserve the whole image bounding box and concatenate transforms downstream.\n" \
    "Normally, all pixels outside of the outside format are clipped off. If this is checked, the whole image RoD is kept.\n" \
    "By default, transforms are only concatenated upstream, i.e. the image is rendered by this effect by concatenating upstream transforms (e.g. CornerPin, Transform...), and the original image is resampled only once. If checked, and there are concatenating transform effects downstream, the image is rendered by the last consecutive concatenating effect."


static bool gHostCanTransform;
static bool gHostIsNatron = false;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ReformatPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    ReformatPlugin(OfxImageEffectHandle handle)
        : Transform3x3Plugin(handle, false, eTransform3x3ParamsTypeNone)
        , _type(0)
        , _format(0)
        , _boxSize(0)
        , _boxFixed(0)
        , _boxPAR(0)
        , _scale(0)
        , _scaleUniform(0)
        , _preserveBB(0)
        , _resize(0)
        , _center(0)
        , _flip(0)
        , _flop(0)
        , _turn(0)
    {
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);

        // NON-GENERIC
        _type = fetchChoiceParam(kParamType);
        _format = fetchChoiceParam(kParamFormat);
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
        assert(_type && _format && _boxSize && _boxFixed && _boxPAR && _scale && _scaleUniform && _preserveBB && _resize && _center && _flip && _flop && _turn);

        _boxSize_saved = _boxSize->getValue();
        _boxPAR_saved = _boxPAR->getValue();
        _boxFixed_saved = _boxFixed->getValue();

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
                getFormatResolution( (OFX::EParamFormat)i, &width, &height, &par );
                assert(par != -1);
                if ( (width * par == projectSize.x) && (height == projectSize.y) && (std::abs(par - projectPAR) < 0.01) ) {
                    _type->setValue( (int)eReformatTypeToFormat );
                    _format->setValue( (OFX::EParamFormat)i );
                    _boxSize->setValue(width, height);
                    _boxPAR->setValue(par);
                    foundFormat = true;
                    break;
                }
            }
            if (!foundFormat) {
                _type->setValue( (int)eReformatTypeToBox );
                _boxSize->setValue(projectSize.x / projectPAR, projectSize.y);
                _boxPAR->setValue(projectPAR);
                _boxFixed->setValue(true);
                _boxSize_saved.x = projectSize.x / projectPAR;
                _boxSize_saved.y = projectSize.y;
                _boxPAR_saved = projectPAR;
                _boxFixed_saved = true;
            }
        }
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_scaleUniform->getValue() &&
             ( _scaleUniform->getNumKeys() == 0) ) {
            _scaleUniform->setIsSecret(true);
        }

        refreshVisibility();
        refreshDynamicProps();
    }

private:
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, OFX::Matrix3x3* invtransform) const OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    void refreshVisibility();

    void refreshDynamicProps();

    void setBoxValues(double time);


    // NON-GENERIC
    OFX::ChoiceParam *_type;
    OFX::ChoiceParam *_format;
    OFX::Int2DParam *_boxSize;
    OFX::BooleanParam* _boxFixed;
    OFX::DoubleParam* _boxPAR;
    OFX::Double2DParam *_scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::BooleanParam* _preserveBB;
    OFX::ChoiceParam *_resize;
    OFX::BooleanParam* _center;
    OFX::BooleanParam* _flip;
    OFX::BooleanParam* _flop;
    OFX::BooleanParam* _turn;

    // saved values for user-specified box
    OfxPointI _boxSize_saved;
    double _boxPAR_saved;
    bool _boxFixed_saved;
};

// override the rod call
bool
ReformatPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    if ( !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }

    const double time = args.time;
    bool ret = Transform3x3Plugin::getRegionOfDefinition(args, rod);
    if ( !ret ||
         _preserveBB->getValue() ) {
        return ret;
    }
    // intersect with format RoD
    OfxRectD formatrod;
    formatrod.x1 = formatrod.y1 = 0.;
    OfxPointI boxSize = _boxSize->getValueAtTime(time);
    double boxPAR = _boxPAR->getValueAtTime(time);

    // convert the source RoD to pixels
    OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
    if ( Coords::rectIsEmpty(srcRod) ) {
        return false;
    }
    double srcPar = _srcClip->getPixelAspectRatio();
    OfxRectI srcRodPixel;
    OfxPointD rs = {1., 1.};
    Coords::toPixelNearest(srcRod, rs, srcPar, &srcRodPixel);
    int srcw = srcRodPixel.x2 - srcRodPixel.x1;
    int srch = srcRodPixel.y2 - srcRodPixel.y1;
    // if turn, inverse both dimensions
    if ( _turn->getValueAtTime(time) ) {
        std::swap(srcw, srch);
    }
    // if fit or fill, determine if it should be fit to width or height
    ResizeEnum resize = (ResizeEnum)_resize->getValueAtTime(time);
    if (resize == eResizeFit) {
        if (boxSize.x * srch > boxSize.y * srcw) {
            resize = eResizeHeight;
        } else {
            resize = eResizeWidth;
        }
    } else if (resize == eResizeFill) {
        if (boxSize.x * srch > boxSize.y * srcw) {
            resize = eResizeWidth;
        } else {
            resize = eResizeHeight;
        }
    }

    bool boxFixed = _boxFixed->getValue();
    if ( (resize == eResizeNone) ||
         ( resize == eResizeDistort) ||
         boxFixed ) {
        // easy case
        formatrod.x2 = boxSize.x * boxPAR;
        formatrod.y2 = boxSize.y;
    } else if (resize == eResizeWidth) {
        double scale = boxSize.x / (double)srcw;
        formatrod.x2 = boxSize.x * boxPAR;
        int dsth = std::floor(srch * scale + 0.5);
        int offset = 0;//_center->getValueAtTime(time) ? (boxSize.y - dsth) / 2 : 0;
        formatrod.y1 = offset;
        formatrod.y2 = offset + dsth;
    } else if (resize == eResizeHeight) {
        double scale = boxSize.y / (double)srch;
        int dstw = std::floor(srcw * scale + 0.5);
        int offset = 0;//_center->getValueAtTime(time) ? (boxSize.x - dstw) / 2 : 0;
        formatrod.x1 = offset * boxPAR;
        formatrod.x2 = (offset + dstw) * boxPAR;
        formatrod.y2 = boxSize.y;
    }
    Coords::rectIntersection(rod, formatrod, &rod);

    return true;
} // ReformatPlugin::getRegionOfDefinition

// overridden is identity
bool
ReformatPlugin::isIdentity(const double time)
{
    // must clear persistent message in isIdentity, or render() is not called by Nuke after an error
    clearPersistentMessage();

    if ( _center->getValueAtTime(time) || _flip->getValueAtTime(time) ||
         _flop->getValueAtTime(time)   || _turn->getValueAtTime(time) ) {
        return false;
    }

    if ( (ResizeEnum)_resize->getValueAtTime(time) == eResizeNone ) {
        return true;
    }

    return false;
}

bool
ReformatPlugin::getInverseTransformCanonical(const double time,
                                             const int /*view*/,
                                             const double /*amount*/,
                                             const bool invert,
                                             OFX::Matrix3x3* invtransform) const
{
    if ( !_srcClip || !_srcClip->isConnected() ) {
        return false;
    }

    ResizeEnum resize = (ResizeEnum)_resize->getValueAtTime(time);
    bool center = _center->getValueAtTime(time);
    bool flip = _flip->getValueAtTime(time);
    bool flop = _flop->getValueAtTime(time);
    bool turn = _turn->getValueAtTime(time);
    bool boxFixed = _boxFixed->getValueAtTime(time);
    if ( (resize == eResizeNone) &&
         !( (center && boxFixed) || flip || flop || turn ) ) {
        invtransform->setIdentity();

        return true;
    }
    // same as getRegionOfDefinition, but without rounding, and without conversion to pixels

    OfxPointI boxSize = _boxSize->getValueAtTime(time);
    if ( (boxSize.x == 0) && (boxSize.y == 0) ) {
        //probably scale is 0
        return false;
    }
    double boxPAR = _boxPAR->getValueAtTime(time);
    OfxRectD boxRod = { 0., 0., boxSize.x * boxPAR, boxSize.y};
    OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
    if ( Coords::rectIsEmpty(srcRod) ) {
        return false;
    }
    double srcw = srcRod.x2 - srcRod.x1;
    double srch = srcRod.y2 - srcRod.y1;
    // if turn, inverse both dimensions
    if ( _turn->getValueAtTime(time) ) {
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
    dstRod.x1 = dstRod.y1 = 0.;
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
        if ( !_turn->getValueAtTime(time) ) {
            // simple case: no rotation
            // x <- srcRod.x1 + (x - dstRod.x1) * (srcRod.x2 - srcRod.x1) / (dstRod.x2 - dstRod.x1)
            // y <- srcRod.y1 + (y - dstRod.y1) * (srcRod.y2 - srcRod.y1) / (dstRod.y2 - dstRod.y1)
            double ax = (srcRod.x2 - srcRod.x1) / (dstRod.x2 - dstRod.x1);
            double ay = (srcRod.y2 - srcRod.y1) / (dstRod.y2 - dstRod.y1);
            assert(ax == ax && ay == ay);
            invtransform->a = ax; invtransform->b =  0; invtransform->c = srcRod.x1 - dstRod.x1 * ax;
            invtransform->d =  0; invtransform->e = ay; invtransform->f = srcRod.y1 - dstRod.y1 * ay;
            invtransform->g =  0; invtransform->h =  0; invtransform->i = 1.;
        } else {
            // rotation 90 degrees counterclockwise
            // x <- srcRod.x1 + (y - dstRod.y1) * (srcRod.x2 - srcRod.x1) / (dstRod.y2 - dstRod.y1)
            // y <- srcRod.y1 + (dstRod.x2 - x) * (srcRod.y2 - srcRod.y1) / (dstRod.x2 - dstRod.x1)
            double ax = (srcRod.x2 - srcRod.x1) / (dstRod.y2 - dstRod.y1);
            double ay = (srcRod.y2 - srcRod.y1) / (dstRod.x2 - dstRod.x1);
            assert(ax == ax && ay == ay);
            invtransform->a =  0; invtransform->b = ax; invtransform->c = srcRod.x1 - dstRod.y1 * ax;
            invtransform->d = -ay; invtransform->e =  0; invtransform->f = srcRod.y1 + dstRod.x2 * ay;
            invtransform->g =  0; invtransform->h =  0; invtransform->i = 1.;
        }
    } else { // invert
        if ( (srcRod.x1 == srcRod.x2) ||
             ( srcRod.y1 == srcRod.y2) ) {
            return false;
        }
        // now, compute the transform from srcRod to dstRod
        if ( !_turn->getValueAtTime(time) ) {
            // simple case: no rotation
            // x <- dstRod.x1 + (x - srcRod.x1) * (dstRod.x2 - dstRod.x1) / (srcRod.x2 - srcRod.x1)
            // y <- dstRod.y1 + (y - srcRod.y1) * (dstRod.y2 - dstRod.y1) / (srcRod.y2 - srcRod.y1)
            double ax = (dstRod.x2 - dstRod.x1) / (srcRod.x2 - srcRod.x1);
            double ay = (dstRod.y2 - dstRod.y1) / (srcRod.y2 - srcRod.y1);
            assert(ax == ax && ay == ay);
            invtransform->a = ax; invtransform->b =  0; invtransform->c = dstRod.x1 - srcRod.x1 * ax;
            invtransform->d =  0; invtransform->e = ay; invtransform->f = dstRod.y1 - srcRod.y1 * ay;
            invtransform->g =  0; invtransform->h =  0; invtransform->i = 1.;
        } else {
            // rotation 90 degrees counterclockwise
            // x <- dstRod.x1 + (srcRod.y2 - y) * (dstRod.x2 - dstRod.x1) / (srcRod.y2 - srcRod.y1)
            // y <- dstRod.y1 + (x - srcRod.x1) * (dstRod.y2 - dstRod.y1) / (srcRod.x2 - srcRod.x1)
            double ax = (dstRod.x2 - dstRod.x1) / (srcRod.y2 - srcRod.y1);
            double ay = (dstRod.y2 - dstRod.y1) / (srcRod.x2 - srcRod.x1);
            assert(ax == ax && ay == ay);
            invtransform->a =  0; invtransform->b = -ax; invtransform->c = dstRod.x1 + srcRod.y2 * ax;
            invtransform->d = ay; invtransform->e =  0; invtransform->f = dstRod.y1 - srcRod.x1 * ay;
            invtransform->g =  0; invtransform->h =  0; invtransform->i = 1.;
        }
    }
    assert(invtransform->a == invtransform->a && invtransform->b == invtransform->b && invtransform->c == invtransform->c &&
           invtransform->d == invtransform->d && invtransform->e == invtransform->e && invtransform->f == invtransform->f &&
           invtransform->g == invtransform->g && invtransform->h == invtransform->h && invtransform->i == invtransform->i);

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
            _boxSize->setValue(w, h);
            _boxPAR->setValue(par);
        }


        _boxFixed->setValue(true);
        break;
    }
    case eReformatTypeToBox:
        _boxSize->setValue(_boxSize_saved);
        _boxPAR->setValue(_boxPAR_saved);
        _boxFixed->setValue(_boxFixed_saved);
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
ReformatPlugin::changedParam(const OFX::InstanceChangedArgs &args,
                             const std::string &paramName)
{
    if (paramName == kParamType) {
        refreshVisibility();
        ReformatTypeEnum type = (ReformatTypeEnum)_type->getValue();
        if (type == eReformatTypeToBox) {
            // restore saved values
            _boxSize->setValue(_boxSize_saved);
            _boxPAR->setValue(_boxPAR_saved);
            _boxFixed->setValue(_boxFixed_saved);
        }
    }
    if ( (paramName == kParamType) || (paramName == kParamFormat) || (paramName == kParamScale) || (paramName == kParamScaleUniform) ) {
        setBoxValues(args.time);

        return;
    }
    if ( (paramName == kParamBoxSize) && (args.reason == eChangeUserEdit) ) {
        _boxSize_saved = _boxSize->getValue();

        return;
    }
    if ( (paramName == kParamBoxPAR) && (args.reason == eChangeUserEdit) ) {
        _boxPAR_saved = _boxPAR->getValue();
    }
    if ( (paramName == kParamBoxFixed) && (args.reason == eChangeUserEdit) ) {
        _boxFixed_saved = _boxFixed->getValue();
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
        _format->setIsSecret(false);
        _boxSize->setIsSecret(true);
        _boxPAR->setIsSecret(true);
        _boxFixed->setIsSecret(true);
        _scale->setIsSecret(true);
        _scaleUniform->setIsSecret(true);
        break;

    case eReformatTypeToBox:
        _format->setIsSecret(true);
        _boxSize->setIsSecret(false);
        _boxPAR->setIsSecret(false);
        _boxFixed->setIsSecret(false);
        _scale->setIsSecret(true);
        _scaleUniform->setIsSecret(true);
        break;

    case eReformatTypeScale:
        _format->setIsSecret(true);
        _boxSize->setIsSecret(true);
        _boxPAR->setIsSecret(true);
        _boxFixed->setIsSecret(true);
        _scale->setIsSecret(false);
        _scaleUniform->setIsSecret(false);
        break;
    }
}

void
ReformatPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    ReformatTypeEnum type = (ReformatTypeEnum)_type->getValue();

    switch (type) {
    case eReformatTypeToFormat:
    case eReformatTypeToBox: {
        //specific output PAR
        double par;
        _boxPAR->getValue(par);
        clipPreferences.setPixelAspectRatio(*_dstClip, par);
        break;
    }

    case eReformatTypeScale:
        // don't change the pixel aspect ratio
        break;
    }
}

mDeclarePluginFactory(ReformatPluginFactory, {}, {});
void
ReformatPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    Transform3x3Describe(desc, false);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsMultipleClipPARs(true);
    gHostCanTransform = false;

#ifdef OFX_EXTENSIONS_NUKE
    if (OFX::getImageEffectHostDescription()->canTransform) {
        gHostCanTransform = true;
    }
#endif
#ifdef OFX_EXTENSIONS_NATRON
    if (OFX::getImageEffectHostDescription()->isNatron) {
        gHostIsNatron = true;
    }
#endif
}

void
ReformatPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                         OFX::ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, false);

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
        param->appendOption(kParamFormatPCVideoLabel);
        assert(param->getNOptions() == eParamFormatNTSC);
        param->appendOption(kParamFormatNTSCLabel);
        assert(param->getNOptions() == eParamFormatPAL);
        param->appendOption(kParamFormatPALLabel);
        assert(param->getNOptions() == eParamFormatHD);
        param->appendOption(kParamFormatHDLabel);
        assert(param->getNOptions() == eParamFormatNTSC169);
        param->appendOption(kParamFormatNTSC169Label);
        assert(param->getNOptions() == eParamFormatPAL169);
        param->appendOption(kParamFormatPAL169Label);
        assert(param->getNOptions() == eParamFormat1kSuper35);
        param->appendOption(kParamFormat1kSuper35Label);
        assert(param->getNOptions() == eParamFormat1kCinemascope);
        param->appendOption(kParamFormat1kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat2kSuper35);
        param->appendOption(kParamFormat2kSuper35Label);
        assert(param->getNOptions() == eParamFormat2kCinemascope);
        param->appendOption(kParamFormat2kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormat4kSuper35);
        param->appendOption(kParamFormat4kSuper35Label);
        assert(param->getNOptions() == eParamFormat4kCinemascope);
        param->appendOption(kParamFormat4kCinemascopeLabel);
        assert(param->getNOptions() == eParamFormatSquare256);
        param->appendOption(kParamFormatSquare256Label);
        assert(param->getNOptions() == eParamFormatSquare512);
        param->appendOption(kParamFormatSquare512Label);
        assert(param->getNOptions() == eParamFormatSquare1k);
        param->appendOption(kParamFormatSquare1kLabel);
        assert(param->getNOptions() == eParamFormatSquare2k);
        param->appendOption(kParamFormatSquare2kLabel);
        assert(param->getNOptions() == eParamFormatCount);
        param->setDefault(kParamFormatDefault);
        param->setHint(kParamFormatHint);
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
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBoxFixed);
        param->setLabel(kParamBoxFixedLabel);
        param->setHint(kParamBoxFixedHint);
        param->setDefault(false);
        param->setAnimates(true);
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
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
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
        param->setDefault(!OFX::getImageEffectHostDescription()->isNatron);
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintDivider);
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
        param->appendOption(kParamResizeOptionNone, kParamResizeOptionNoneHint);
        assert(param->getNOptions() == eResizeWidth);
        param->appendOption(kParamResizeOptionWidth, kParamResizeOptionWidthHint);
        assert(param->getNOptions() == eResizeHeight);
        param->appendOption(kParamResizeOptionHeight, kParamResizeOptionHeightHint);
        assert(param->getNOptions() == eResizeFit);
        param->appendOption(kParamResizeOptionFit, kParamResizeOptionFitHint);
        assert(param->getNOptions() == eResizeFill);
        param->appendOption(kParamResizeOptionFill, kParamResizeOptionFillHint);
        assert(param->getNOptions() == eResizeDistort);
        param->appendOption(kParamResizeOptionDistort, kParamResizeOptionDistortHint);
        param->setDefault( (int)eResizeWidth );
        param->setAnimates(true);
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
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
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
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
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
        param->setAnimates(true);
        param->setLayoutHint(OFX::eLayoutHintNoNewLine, 1);
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
        param->setAnimates(true);
        param->getPropertySet().propSetInt(kOfxParamPropLayoutPadWidth, 1, false);
        if (page) {
            page->addChild(*param);
        }
    }

    // Preserve bounding box
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPreserveBoundingBox);
        param->setLabel(kParamPreserveBoundingBoxLabel);
        param->setHint(kParamPreserveBoundingBoxHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // clamp, filter, black outside
    ofxsFilterDescribeParamsInterpolate2D(desc, page, /*blackOutsideDefault*/ false);
} // ReformatPluginFactory::describeInContext

OFX::ImageEffect*
ReformatPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      OFX::ContextEnum /*context*/)
{
    return new ReformatPlugin(handle);
}

static ReformatPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
