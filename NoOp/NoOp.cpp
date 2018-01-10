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
 * OFX NoOp plugin.
 * Does nothing.
 */

#include <sstream> // stringstream

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsGenerator.h" // for format-related parameters
#include "ofxsFormatResolution.h" // for format-related enums

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "NoOpOFX"
#define kPluginGrouping "Other"
#define kPluginDescription "Copies the input to the ouput.\n" \
    "This effect does not modify the actual content of the image, but can be used to modify the metadata associated with the clip (premultiplication, field order, format, pixel aspect ratio, frame rate).\n" \
    "This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.NoOpPlugin"

// History:
// Version 2.0: introduce setFormat, deprecate setPixelAspectRatio on Natron
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs true
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

#define kParamForceCopy "forceCopy"
#define kParamForceCopyLabel "Force Copy"
#define kParamForceCopyHint "Force copy from input to output"

#define kParamSetPremult "setPremult"
#define kParamSetPremultLabel "Set Premultiplication"
#define kParamSetPremultHint "Set the premultiplication state of the output clip, without modifying the raw content. Use the Premult or UnPremult plu-gins to affect the content."

#define kParamOutputPremult "outputPremult"
#define kParamOutputPremultLabel "Output Premultiplication"
#define kParamOutputPremultHint "Premultiplication state of the output clip."

#define kParamSetFieldOrder "setFieldOrder"
#define kParamSetFieldOrderLabel "Set Field Order"
#define kParamSetFieldOrderHint "Set the field order state of the output clip, without modifying the raw content."

#define kParamOutputFieldOrder "outputFieldOrder"
#define kParamOutputFieldOrderLabel "Output Field Order"
#define kParamOutputFieldOrderHint "Field order state of the output clip."

#ifdef OFX_EXTENSIONS_NATRON
#define kParamSetFormat "setFormat"
#define kParamSetFormatLabel "Set Format"
#define kParamSetFormatHint "Set the format of the output clip, without modifying the raw content."
#endif

#define kParamSetPixelAspectRatio "setPixelAspectRatio"
#define kParamSetPixelAspectRatioLabel "Set Pixel Aspect Ratio"
#define kParamSetPixelAspectRatioHint "Set the pixel aspect ratio of the output clip, without modifying the raw content."

#define kParamOutputPixelAspectRatio "outputPixelAspectRatio"
#define kParamOutputPixelAspectRatioLabel "Output Pixel Aspect Ratio"
#define kParamOutputPixelAspectRatioHint "Pixel aspect ratio of the output clip."

#define kParamSetFrameRate "setFrameRate"
#define kParamSetFrameRateLabel "Set Frame Rate"
#define kParamSetFrameRateHint "Set the frame rate state of the output clip, without modifying the raw content."

#define kParamOutputFrameRate "outputFrameRate"
#define kParamOutputFrameRateLabel "Output Frame Rate"
#define kParamOutputFrameRateHint "Frame rate of the output clip."

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class NoOpPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    NoOpPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _forceCopy(NULL)
        , _setPremult(NULL)
        , _premult(NULL)
        , _setFieldOrder(NULL)
        , _fieldOrder(NULL)
#ifdef OFX_EXTENSIONS_NATRON
        , _setFormat(NULL)
        , _extent(NULL)
        , _format(NULL)
        , _formatSize(NULL)
        , _formatPar(NULL)
        , _btmLeft(NULL)
        , _size(NULL)
        , _recenter(NULL)
#endif
        , _setPixelAspectRatio(NULL)
        , _pixelAspectRatio(NULL)
        , _setFrameRate(NULL)
        , _frameRate(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _forceCopy = fetchBooleanParam(kParamForceCopy);
        _setPremult = fetchBooleanParam(kParamSetPremult);
        _premult = fetchChoiceParam(kParamOutputPremult);
        assert(_forceCopy && _setPremult && _premult);

        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();
        if (gHostDescription.supportsSetableFielding) {
            _setFieldOrder = fetchBooleanParam(kParamSetFieldOrder);
            _fieldOrder = fetchChoiceParam(kParamOutputFieldOrder);
            assert(_setFieldOrder && _fieldOrder);
        }
#ifdef OFX_EXTENSIONS_NATRON
        if (gHostDescription.isNatron) {
            _setFormat = fetchBooleanParam(kParamSetFormat);
            _extent = fetchChoiceParam(kParamGeneratorExtent);
            _format = fetchChoiceParam(kParamGeneratorFormat);
            _formatSize = fetchInt2DParam(kParamGeneratorSize);
            _formatPar = fetchDoubleParam(kParamGeneratorPAR);
            _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
            _size = fetchDouble2DParam(kParamRectangleInteractSize);
            _recenter = fetchPushButtonParam(kParamGeneratorCenter);
        }
#endif
        if (gHostDescription.supportsMultipleClipPARs) {
            _setPixelAspectRatio = fetchBooleanParam(kParamSetPixelAspectRatio);
            _pixelAspectRatio = fetchDoubleParam(kParamOutputPixelAspectRatio);
            assert(_setPixelAspectRatio && _pixelAspectRatio);
        }
        if (gHostDescription.supportsSetableFrameRate) {
            _setFrameRate = fetchBooleanParam(kParamSetFrameRate);
            _frameRate = fetchDoubleParam(kParamOutputFrameRate);
            assert(_setFrameRate && _frameRate);
        }

        updateVisibility();

#ifdef OFX_EXTENSIONS_NATRON
        // honor kParamDefaultsNormalised
        if ( paramExists(kParamDefaultsNormalised) ) {
            // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
            // handle these ourselves!
            BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
            assert(param);
            bool normalised = param->getValue();
            if (normalised) {
                OfxPointD size = getProjectExtent();
                OfxPointD origin = getProjectOffset();
                OfxPointD p;
                // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
            }
        }
#endif
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const TransformArguments & args, Clip * &transformClip, double transformMatrix[9]) OVERRIDE FINAL;
#endif

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateVisibility()
    {
        const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();

        _premult->setEnabled( _setPremult->getValue() );
        if (gHostDescription.supportsSetableFielding) {
            _fieldOrder->setEnabled( _setFieldOrder->getValue() );
        }
#ifdef OFX_EXTENSIONS_NATRON
        if (gHostDescription.isNatron) {
            GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();
            bool hasFormat = (extent == eGeneratorExtentFormat);
            bool hasSize = (extent == eGeneratorExtentSize);

            _format->setIsSecret(!hasFormat);
            _size->setIsSecret(!hasSize);
            _recenter->setIsSecret(!hasSize);
            _btmLeft->setIsSecret(!hasSize);

            bool setFormat = _setFormat->getValue();
            _extent->setEnabled(setFormat);
            _format->setEnabled(setFormat);
            _size->setEnabled(setFormat);
            _recenter->setEnabled(setFormat);
            _btmLeft->setEnabled(setFormat);
        }
#endif
        if (gHostDescription.supportsMultipleClipPARs) {
            _pixelAspectRatio->setEnabled( _setPixelAspectRatio->getValue() );
        }
        if (gHostDescription.supportsSetableFrameRate) {
            _frameRate->setEnabled( _setFrameRate->getValue() );
        }
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam *_forceCopy;
    BooleanParam *_setPremult;
    ChoiceParam *_premult;
    BooleanParam *_setFieldOrder;
    ChoiceParam *_fieldOrder;
#ifdef OFX_EXTENSIONS_NATRON
    BooleanParam *_setFormat;
    ChoiceParam* _extent;
    ChoiceParam* _format;
    Int2DParam* _formatSize;
    DoubleParam* _formatPar;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    PushButtonParam *_recenter;
#endif
    BooleanParam *_setPixelAspectRatio;
    DoubleParam *_pixelAspectRatio;
    BooleanParam *_setFrameRate;
    DoubleParam *_frameRate;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
NoOpPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                 RegionOfInterestSetter &rois)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    if (!_setPixelAspectRatio) {
        return;
    }
    bool setPixelAspectRatio;
    _setPixelAspectRatio->getValueAtTime(args.time, setPixelAspectRatio);
    if (!setPixelAspectRatio) {
        return;
    }
    double srcPAR = _srcClip->getPixelAspectRatio();
    double pixelAspectRatio = 1.;
    _pixelAspectRatio->getValueAtTime(args.time, pixelAspectRatio);
    if ( (srcPAR <= 0) || (pixelAspectRatio <= 0) ) {
        return;
    }

    OfxRectD srcRoI = args.regionOfInterest;
    srcRoI.x1 *= srcPAR / pixelAspectRatio;
    srcRoI.x2 *= srcPAR / pixelAspectRatio;

    rois.setRegionOfInterest(*_srcClip, srcRoI);
}

bool
NoOpPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                  OfxRectD &rod)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    if (!_setPixelAspectRatio) {
        return false;
    }
    bool setPixelAspectRatio;
    _setPixelAspectRatio->getValueAtTime(args.time, setPixelAspectRatio);
    if (!setPixelAspectRatio) {
        return false;
    }
    double srcPAR = _srcClip->getPixelAspectRatio();
    double pixelAspectRatio = 1.;
    if (_pixelAspectRatio) {
        _pixelAspectRatio->getValueAtTime(args.time, pixelAspectRatio);
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (_formatPar) {
        _formatPar->getValueAtTime(args.time, pixelAspectRatio);
    }
#endif
    if ( (srcPAR <= 0) || (pixelAspectRatio <= 0) ) {
        return false;
    }

    const OfxRectD srcRoD = _srcClip->getRegionOfDefinition(args.time);
    if ( Coords::rectIsEmpty(srcRoD) ) {
        return false;
    }
    rod = srcRoD;
    rod.x1 *= pixelAspectRatio / srcPAR;
    rod.x2 *= pixelAspectRatio / srcPAR;

    return true;
}

// the overridden render function
void
NoOpPlugin::render(const RenderArguments &args)
{
    const double time = args.time;
    bool forceCopy;

    _forceCopy->getValueAtTime(time, forceCopy);

#ifdef DEBUG
    if (!forceCopy) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host should not render");
        throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
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
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    copyPixels( *this, args.renderWindow, src.get(), dst.get() );
}

bool
NoOpPlugin::isIdentity(const IsIdentityArguments &args,
                       Clip * &identityClip,
                       double & /*identityTime*/
                       , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    bool forceCopy;

    _forceCopy->getValueAtTime(time, forceCopy);

    if (!forceCopy) {
        identityClip = _srcClip;

        return true;
    } else {
        return false;
    }
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool
NoOpPlugin::getTransform(const TransformArguments &args,
                         Clip * &transformClip,
                         double transformMatrix[9])
{
    const double time = args.time;
    bool forceCopy;

    _forceCopy->getValueAtTime(time, forceCopy);
    if (forceCopy) {
        return false;
    }
    transformClip = _srcClip;
    transformMatrix[0] = 1.;
    transformMatrix[1] = 0.;
    transformMatrix[2] = 0.;
    transformMatrix[3] = 0.;
    transformMatrix[4] = 1.;
    transformMatrix[5] = 0.;
    transformMatrix[6] = 0.;
    transformMatrix[7] = 0.;
    transformMatrix[8] = 1.;

    return true;
}

#endif


static const char*
bitDepthString(BitDepthEnum bitDepth)
{
    switch (bitDepth) {
    case eBitDepthUByte:

        return "8u";
    case eBitDepthUShort:

        return "16u";
    case eBitDepthHalf:

        return "16f";
    case eBitDepthFloat:

        return "32f";
    case eBitDepthCustom:

        return "x";
    case eBitDepthNone:

        return "0";
#ifdef OFX_EXTENSIONS_VEGAS
    case eBitDepthUByteBGRA:

        return "8uBGRA";
    case eBitDepthUShortBGRA:

        return "16uBGRA";
    case eBitDepthFloatBGRA:

        return "32fBGRA";
#endif
    default:

        return "[unknown bit depth]";
    }
}

static std::string
pixelComponentString(const std::string& p)
{
    const std::string prefix = "OfxImageComponent";

    std::string s = p;

    return s.replace(s.find(prefix), prefix.length(), "");
}

static const char*
premultString(PreMultiplicationEnum e)
{
    switch (e) {
    case eImageOpaque:

        return "Opaque";
    case eImagePreMultiplied:

        return "PreMultiplied";
    case eImageUnPreMultiplied:

        return "UnPreMultiplied";
    default:

        return "[unknown premult]";
    }
}

#ifdef OFX_EXTENSIONS_VEGAS
static const char*
pixelOrderString(PixelOrderEnum e)
{
    switch (e) {
    case ePixelOrderRGBA:

        return "RGBA";
    case ePixelOrderBGRA:

        return "BGRA";
    default:

        return "[unknown pixel order]";
    }
}

#endif

static const char*
fieldOrderString(FieldEnum e)
{
    switch (e) {
    case eFieldNone:

        return "None";
    case eFieldBoth:

        return "Both";
    case eFieldLower:

        return "Lower";
    case eFieldUpper:

        return "Upper";
    case eFieldSingle:

        return "Single";
    case eFieldDoubled:

        return "Doubled";
    default:

        return "[unknown field order]";
    }
}

void
NoOpPlugin::changedParam(const InstanceChangedArgs &args,
                         const std::string &paramName)
{
    if (paramName == kParamSetPremult) {
        updateVisibility();
    } else if (paramName == kParamSetFieldOrder) {
        updateVisibility();
    } else if (paramName == kParamSetFieldOrder) {
        updateVisibility();
#ifdef OFX_EXTENSIONS_NATRON
    } else if (paramName == kParamSetFormat) {
        updateVisibility();
    } else if (paramName == kParamGeneratorExtent) {
        updateVisibility();
    } else if (paramName == kParamGeneratorFormat) {
        //the host does not handle the format itself, do it ourselves
        EParamFormat format = (EParamFormat)_format->getValue();
        int w = 0, h = 0;
        double par = -1;
        getFormatResolution(format, &w, &h, &par);
        assert(par != -1);
        _formatPar->setValue(par);
        _formatSize->setValue(w, h);
    } else if (paramName == kParamGeneratorCenter) {
        Clip* srcClip = _srcClip;
        OfxRectD srcRoD;
        if ( srcClip && srcClip->isConnected() ) {
            srcRoD = srcClip->getRegionOfDefinition(args.time);
        } else {
            OfxPointD siz = getProjectSize();
            OfxPointD off = getProjectOffset();
            srcRoD.x1 = off.x;
            srcRoD.x2 = off.x + siz.x;
            srcRoD.y1 = off.y;
            srcRoD.y2 = off.y + siz.y;
        }
        OfxPointD center;
        center.x = (srcRoD.x2 + srcRoD.x1) / 2.;
        center.y = (srcRoD.y2 + srcRoD.y1) / 2.;

        OfxRectD rectangle;
        _size->getValue(rectangle.x2, rectangle.y2);
        _btmLeft->getValue(rectangle.x1, rectangle.y1);
        rectangle.x2 += rectangle.x1;
        rectangle.y2 += rectangle.y1;

        OfxRectD newRectangle;
        newRectangle.x1 = center.x - (rectangle.x2 - rectangle.x1) / 2.;
        newRectangle.y1 = center.y - (rectangle.y2 - rectangle.y1) / 2.;
        newRectangle.x2 = newRectangle.x1 + (rectangle.x2 - rectangle.x1);
        newRectangle.y2 = newRectangle.y1 + (rectangle.y2 - rectangle.y1);

        _size->setValue(newRectangle.x2 - newRectangle.x1, newRectangle.y2 - newRectangle.y1);
        _btmLeft->setValue(newRectangle.x1, newRectangle.y1);
#endif
    } else if (paramName == kParamSetPixelAspectRatio) {
        updateVisibility();
    } else if (paramName == kParamSetFrameRate) {
        updateVisibility();
    } else if (paramName == kParamClipInfo) {
        std::ostringstream oss;
        oss << "Clip Info:\n\n";
        oss << "Input: ";
        if (!_srcClip) {
            oss << "N/A";
        } else {
            Clip &c = *_srcClip;
            oss << pixelComponentString( c.getPixelComponentsProperty() );
            oss << bitDepthString( c.getPixelDepth() );
            oss << " (unmapped: ";
            oss << pixelComponentString( c.getUnmappedPixelComponentsProperty() );
            oss << bitDepthString( c.getUnmappedPixelDepth() );
            oss << ")\npremultiplication: ";
            oss << premultString( c.getPreMultiplication() );
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString( c.getPixelOrder() );
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString( c.getFieldOrder() );
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
#ifdef OFX_EXTENSIONS_NATRON
            oss << "\nformat: ";
            OfxRectI format;
            c.getFormat(format);
            oss << format.x2 - format.x1 << 'x' << format.y2 - format.y1;
            if ( (format.x1 != 0) && (format.y1 != 0) ) {
                if (format.x1 < 0) {
                    oss << format.x1;
                } else {
                    oss << '+' << format.x1;
                }
                if (format.y1 < 0) {
                    oss << format.y1;
                } else {
                    oss << '+' << format.y1;
                }
            }
#endif
            oss << "\npixel aspect ratio: ";
            oss << c.getPixelAspectRatio();
            oss << "\nframe rate: ";
            oss << c.getFrameRate();
            oss << " (unmapped: ";
            oss << c.getUnmappedFrameRate();
            oss << ")";
            OfxRangeD range = c.getFrameRange();
            oss << "\nframe range: ";
            oss << range.min << "..." << range.max;
            oss << " (unmapped: ";
            range = c.getUnmappedFrameRange();
            oss << range.min << "..." << range.max;
            oss << ")";
            oss << "\nregion of definition: ";
            OfxRectD rod = c.getRegionOfDefinition(args.time);
            oss << rod.x1 << ' ' << rod.y1 << ' ' << rod.x2 << ' ' << rod.y2;
        }
        oss << "\n\n";
        oss << "Output: ";
        if (!_dstClip) {
            oss << "N/A";
        } else {
            Clip &c = *_dstClip;
            oss << pixelComponentString( c.getPixelComponentsProperty() );
            oss << bitDepthString( c.getPixelDepth() );
            oss << " (unmapped: ";
            oss << pixelComponentString( c.getUnmappedPixelComponentsProperty() );
            oss << bitDepthString( c.getUnmappedPixelDepth() );
            oss << ")\npremultiplication: ";
            oss << premultString( c.getPreMultiplication() );
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString( c.getPixelOrder() );
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString( c.getFieldOrder() );
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
#ifdef OFX_EXTENSIONS_NATRON
            oss << "\nformat: ";
            OfxRectI format;
            c.getFormat(format);
            oss << format.x2 - format.x1 << 'x' << format.y2 - format.y1;
            if ( (format.x1 != 0) && (format.y1 != 0) ) {
                if (format.x1 < 0) {
                    oss << format.x1;
                } else {
                    oss << '+' << format.x1;
                }
                if (format.y1 < 0) {
                    oss << format.y1;
                } else {
                    oss << '+' << format.y1;
                }
            }
#endif
            oss << "\npixel aspect ratio: ";
            oss << c.getPixelAspectRatio();
            oss << "\nframe rate: ";
            oss << c.getFrameRate();
            oss << " (unmapped: ";
            oss << c.getUnmappedFrameRate();
            oss << ")";
            OfxRangeD range = c.getFrameRange();
            oss << "\nframe range: ";
            oss << range.min << "..." << range.max;
            oss << " (unmapped: ";
            range = c.getUnmappedFrameRange();
            oss << range.min << "..." << range.max;
            oss << ")";
            oss << "\nregion of definition: ";
            OfxRectD rod = c.getRegionOfDefinition(args.time);
            oss << rod.x1 << ' ' << rod.y1 << ' ' << rod.x2 << ' ' << rod.y2;
        }
        oss << "\n\n";
        oss << "time: " << args.time << ", renderscale: " << args.renderScale.x << 'x' << args.renderScale.y << '\n';

        sendMessage( Message::eMessageMessage, "", oss.str() );
    }
} // NoOpPlugin::changedParam

/* Override the clip preferences */
void
NoOpPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    bool setPremult = _setPremult->getValue();

    if (setPremult) {
        PreMultiplicationEnum premult = (PreMultiplicationEnum)_premult->getValue();

        clipPreferences.setOutputPremultiplication(premult);
    }
    if (_setFieldOrder) {
        // set the field order of _dstClip
        bool setFieldOrder = _setFieldOrder->getValue();
        if (setFieldOrder) {
            FieldEnum fieldOrder = (FieldEnum)_fieldOrder->getValue();

            clipPreferences.setOutputFielding(fieldOrder);
        }
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (_setFormat) {
        bool setFormat = _setFormat->getValue();
        if (setFormat) {
            GeneratorExtentEnum extent = (GeneratorExtentEnum)_extent->getValue();

            switch (extent) {
            case eGeneratorExtentFormat: {
                int w, h;
                _formatSize->getValue(w, h);
                double par = _formatPar->getValue();
                OfxRectI pixelFormat;
                pixelFormat.x1 = pixelFormat.y1 = 0;
                pixelFormat.x2 = w;
                pixelFormat.y2 = h;
                clipPreferences.setOutputFormat(pixelFormat);
                clipPreferences.setPixelAspectRatio(*_dstClip, par);
                break;
            }
            case eGeneratorExtentSize: {
                OfxRectD rod;
                _size->getValue(rod.x2, rod.y2);
                _btmLeft->getValue(rod.x1, rod.y1);
                rod.x2 += rod.x1;
                rod.y2 += rod.y1;
                double par = _srcClip->getPixelAspectRatio();
                OfxPointD renderScale = {1., 1.};
                OfxRectI pixelFormat;
                Coords::toPixelNearest(rod, renderScale, par, &pixelFormat);
                clipPreferences.setOutputFormat(pixelFormat);
                //clipPreferences.setPixelAspectRatio(*_dstClip, par); // should be the default
                break;
            }
            case eGeneratorExtentProject: {
                OfxRectD rod;
                OfxPointD siz = getProjectSize();
                OfxPointD off = getProjectOffset();
                rod.x1 = off.x;
                rod.x2 = off.x + siz.x;
                rod.y1 = off.y;
                rod.y2 = off.y + siz.y;
                double par = getProjectPixelAspectRatio();
                OfxPointD renderScale = {1., 1.};
                OfxRectI pixelFormat;
                Coords::toPixelNearest(rod, renderScale, par, &pixelFormat);
                clipPreferences.setOutputFormat(pixelFormat);
                clipPreferences.setPixelAspectRatio(*_dstClip, par);
                break;
            }
            case eGeneratorExtentDefault:
                break;
            }
        }
    }
#endif // ifdef OFX_EXTENSIONS_NATRON
    if (_setPixelAspectRatio) {
        bool setPixelAspectRatio = _setPixelAspectRatio->getValue();
        if (setPixelAspectRatio) {
            double pixelAspectRatio = _pixelAspectRatio->getValue();
            clipPreferences.setPixelAspectRatio(*_dstClip, pixelAspectRatio);
        }
    }
    if (_setFrameRate) {
        bool setFrameRate = _setFrameRate->getValue();
        if (setFrameRate) {
            double frameRate = _frameRate->getValue();
            clipPreferences.setOutputFrameRate(frameRate);
        }
    }
} // NoOpPlugin::getClipPreferences

mDeclarePluginFactory(NoOpPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
NoOpPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only filter at the moment
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthNone);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthCustom);
#ifdef OFX_EXTENSIONS_VEGAS
    desc.addSupportedBitDepth(eBitDepthUByteBGRA);
    desc.addSupportedBitDepth(eBitDepthUShortBGRA);
    desc.addSupportedBitDepth(eBitDepthFloatBGRA);
#endif

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
    desc.setOverlayInteractDescriptor(new GeneratorOverlayDescriptor);
}

void
NoOpPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                     ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentNone);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
#ifdef OFX_EXTENSIONS_NUKE
    srcClip->setCanTransform(true);
#endif
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // forceCopy
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamForceCopy);
        param->setLabel(kParamForceCopyLabel);
        param->setHint(kParamForceCopyHint);
        param->setDefault(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    //// setPremult
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetPremult);
        param->setLabel(kParamSetPremultLabel);
        param->setHint(kParamSetPremultHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    //// premult
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputPremult);
        param->setLabel(kParamOutputPremultLabel);
        param->setHint(kParamOutputPremultHint);
        assert(param->getNOptions() == eImageOpaque);
        param->appendOption( premultString(eImageOpaque) );
        assert(param->getNOptions() == eImagePreMultiplied);
        param->appendOption( premultString(eImagePreMultiplied) );
        assert(param->getNOptions() == eImageUnPreMultiplied);
        param->appendOption( premultString(eImageUnPreMultiplied) );
        param->setDefault(eImagePreMultiplied); // images should be premultiplied in a compositing context
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    const ImageEffectHostDescription &gHostDescription = *getImageEffectHostDescription();

    if (gHostDescription.supportsSetableFielding) {
        //// setFieldOrder
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetFieldOrder);
            param->setLabel(kParamSetFieldOrderLabel);
            param->setHint(kParamSetFieldOrderHint);
            param->setDefault(false);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }

        //// fieldOrder
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputFieldOrder);
            param->setLabel(kParamOutputFieldOrderLabel);
            param->setHint(kParamOutputFieldOrderHint);
            assert(param->getNOptions() == eFieldNone);
            param->appendOption( fieldOrderString(eFieldNone) );
            assert(param->getNOptions() == eFieldBoth);
            param->appendOption( fieldOrderString(eFieldBoth) );
            assert(param->getNOptions() == eFieldLower);
            param->appendOption( fieldOrderString(eFieldLower) );
            assert(param->getNOptions() == eFieldUpper);
            param->appendOption( fieldOrderString(eFieldUpper) );
            assert(param->getNOptions() == eFieldSingle);
            param->appendOption( fieldOrderString(eFieldSingle) );
            assert(param->getNOptions() == eFieldDoubled);
            param->appendOption( fieldOrderString(eFieldDoubled) );
            param->setDefault(eFieldNone);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
    }

#ifdef OFX_EXTENSIONS_NATRON
    if (gHostDescription.isNatron) {
        //// setFormat
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetFormat);
            param->setLabel(kParamSetFormatLabel);
            param->setHint(kParamSetFormatHint);
            param->setDefault(false);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
        // extent
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamGeneratorExtent);
            param->setLabel(kParamGeneratorExtentLabel);
            param->setHint(kParamGeneratorExtentHint);
            assert(param->getNOptions() == eGeneratorExtentFormat);
            param->appendOption(kParamGeneratorExtentOptionFormat);
            assert(param->getNOptions() == eGeneratorExtentSize);
            param->appendOption(kParamGeneratorExtentOptionSize);
            assert(param->getNOptions() == eGeneratorExtentProject);
            param->appendOption(kParamGeneratorExtentOptionProject);
            //assert(param->getNOptions() == eGeneratorExtentDefault);
            //param->appendOption(kParamGeneratorExtentOptionDefault);
            param->setDefault(eGeneratorExtentFormat);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }

        // recenter
        {
            PushButtonParamDescriptor* param = desc.definePushButtonParam(kParamGeneratorCenter);
            param->setLabel(kParamGeneratorCenterLabel);
            param->setHint(kParamGeneratorCenterHint);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (page) {
                page->addChild(*param);
            }
        }

        // format
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamGeneratorFormat);
            param->setLabel(kParamGeneratorFormatLabel);
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
            param->setDefault(eParamFormatPCVideo);
            param->setHint(kParamGeneratorFormatHint);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }

        {
            int w = 0, h = 0;
            double par = -1.;
            getFormatResolution(eParamFormatPCVideo, &w, &h, &par);
            assert(par != -1);
            {
                Int2DParamDescriptor* param = desc.defineInt2DParam(kParamGeneratorSize);
                param->setLabel(kParamGeneratorSizeLabel);
                param->setHint(kParamGeneratorSizeHint);
                param->setIsSecretAndDisabled(true);
                param->setDefault(w, h);
                if (page) {
                    page->addChild(*param);
                }
            }

            {
                DoubleParamDescriptor* param = desc.defineDoubleParam(kParamGeneratorPAR);
                param->setLabel(kParamGeneratorPARLabel);
                param->setHint(kParamGeneratorPARHint);
                param->setIsSecretAndDisabled(true);
                param->setRange(0., DBL_MAX);
                param->setDisplayRange(0.5, 2.);
                param->setDefault(par);
                if (page) {
                    page->addChild(*param);
                }
            }
        }

        // btmLeft
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
            param->setLabel(kParamRectangleInteractBtmLeftLabel);
            param->setDoubleType(eDoubleTypeXYAbsolute);
            if ( param->supportsDefaultCoordinateSystem() ) {
                param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
            } else {
                gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
            }
            param->setDefault(0., 0.);
            param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            param->setHint("Coordinates of the bottom left corner of the size rectangle.");
            param->setDigits(0);
            if (page) {
                page->addChild(*param);
            }
        }

        // size
        {
            Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
            param->setLabel(kParamRectangleInteractSizeLabel);
            param->setDoubleType(eDoubleTypeXY);
            if ( param->supportsDefaultCoordinateSystem() ) {
                param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
            } else {
                gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
            }
            param->setDefault(1., 1.);
            param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
            param->setDisplayRange(0, 0, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
            param->setIncrement(1.);
            param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
            param->setHint("Width and height of the size rectangle.");
            param->setIncrement(1.);
            param->setDigits(0);
            if (page) {
                page->addChild(*param);
            }
        }
    }
#endif // OFX_EXTENSIONS_NATRON

    if (gHostDescription.supportsMultipleClipPARs) {
        //// setPixelAspectRatio
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetPixelAspectRatio);
            param->setLabel(kParamSetPixelAspectRatioLabel);
            param->setHint(kParamSetPixelAspectRatioHint);
            param->setDefault(false);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
        //// pixelAspectRatio
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOutputPixelAspectRatio);
            param->setLabel(kParamOutputPixelAspectRatioLabel);
            param->setHint(kParamOutputPixelAspectRatioHint);
            param->setDefault(1.);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    if (gHostDescription.supportsSetableFrameRate) {
        //// setFrameRate
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetFrameRate);
            param->setLabel(kParamSetFrameRateLabel);
            param->setHint(kParamSetFrameRateHint);
            param->setDefault(false);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }

        //// frameRate
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOutputFrameRate);
            param->setLabel(kParamOutputFrameRateLabel);
            param->setHint(kParamOutputFrameRateHint);
            param->setDefault(24.);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    // clipInfo
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }
} // NoOpPluginFactory::describeInContext

ImageEffect*
NoOpPluginFactory::createInstance(OfxImageEffectHandle handle,
                                  ContextEnum /*context*/)
{
    return new NoOpPlugin(handle);
}

static NoOpPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
