/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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

#include "NoOp.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "NoOpOFX"
#define kPluginGrouping "Other"
#define kPluginDescription "Copies the input to the ouput.\n"\
"This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.NoOpPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class NoOpPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    NoOpPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _forceCopy(0)
    , _setPremult(0)
    , _premult(0)
    , _setFieldOrder(0)
    , _fieldOrder(0)
    , _setPixelAspectRatio(0)
    , _pixelAspectRatio(0)
    , _setFrameRate(0)
    , _frameRate(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _forceCopy = fetchBooleanParam(kParamForceCopy);
        _setPremult = fetchBooleanParam(kParamSetPremult);
        _premult = fetchChoiceParam(kParamOutputPremult);
        assert(_forceCopy && _setPremult && _premult);
        _premult->setEnabled(_setPremult->getValue());

        const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
        if (gHostDescription.supportsSetableFielding) {
            _setFieldOrder = fetchBooleanParam(kParamSetFieldOrder);
            _fieldOrder = fetchChoiceParam(kParamOutputFieldOrder);
            assert(_setFieldOrder && _fieldOrder);
            _fieldOrder->setEnabled(_setFieldOrder->getValue());
        }
        if (gHostDescription.supportsMultipleClipPARs) {
            _setPixelAspectRatio = fetchBooleanParam(kParamSetPixelAspectRatio);
            _pixelAspectRatio = fetchDoubleParam(kParamOutputPixelAspectRatio);
            assert(_setPixelAspectRatio && _pixelAspectRatio);
            _pixelAspectRatio->setEnabled(_setPixelAspectRatio->getValue());
        }
        if (gHostDescription.supportsSetableFrameRate) {
            _setFrameRate = fetchBooleanParam(kParamSetFrameRate);
            _frameRate = fetchDoubleParam(kParamOutputFrameRate);
            assert(_setFrameRate && _frameRate);
            _frameRate->setEnabled(_setFrameRate->getValue());
        }
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9]) OVERRIDE FINAL;
#endif

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::BooleanParam *_forceCopy;
    OFX::BooleanParam *_setPremult;
    OFX::ChoiceParam *_premult;
    OFX::BooleanParam *_setFieldOrder;
    OFX::ChoiceParam *_fieldOrder;
    OFX::BooleanParam *_setPixelAspectRatio;
    OFX::DoubleParam *_pixelAspectRatio;
    OFX::BooleanParam *_setFrameRate;
    OFX::DoubleParam *_frameRate;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
NoOpPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    if (!_srcClip) {
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
    if (srcPAR <= 0 || pixelAspectRatio <= 0) {
        return;
    }

    OfxRectD srcRoI = args.regionOfInterest;
    srcRoI.x1 *= srcPAR / pixelAspectRatio;
    srcRoI.x2 *= srcPAR / pixelAspectRatio;

    rois.setRegionOfInterest(*_srcClip, srcRoI);
}


bool
NoOpPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!_srcClip) {
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
    _pixelAspectRatio->getValueAtTime(args.time, pixelAspectRatio);
    if (srcPAR <= 0 || pixelAspectRatio <= 0) {
        return false;
    }

    const OfxRectD srcRoD = _srcClip->getRegionOfDefinition(args.time);
    if (OFX::Coords::rectIsEmpty(srcRoD)) {
        return false;
    }
    rod = srcRoD;
    rod.x1 *= pixelAspectRatio / srcPAR;
    rod.x2 *= pixelAspectRatio / srcPAR;

    return true;
}

// the overridden render function
void
NoOpPlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;
    bool forceCopy;
    _forceCopy->getValueAtTime(time, forceCopy);

#ifdef DEBUG
    if (!forceCopy) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host should not render");
        throwSuiteStatusException(kOfxStatFailed);
    }
#endif

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // do the rendering
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    copyPixels(*this, args.renderWindow, src.get(), dst.get());
}

bool
NoOpPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
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
NoOpPlugin::getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9])
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
        case OFX::eBitDepthUByte:
            return "8u";
        case OFX::eBitDepthUShort:
            return "16u";
        case OFX::eBitDepthHalf:
            return "16f";
        case OFX::eBitDepthFloat:
            return "32f";
        case OFX::eBitDepthCustom:
            return "x";
        case OFX::eBitDepthNone:
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
    return s.replace(s.find(prefix),prefix.length(),"");
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
NoOpPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamSetPremult) {
        _premult->setEnabled(_setPremult->getValue());
    } else if (paramName == kParamSetFieldOrder) {
        _fieldOrder->setEnabled(_setFieldOrder->getValue());
    } else if (paramName == kParamSetFieldOrder) {
        _fieldOrder->setEnabled(_setFieldOrder->getValue());
    } else if (paramName == kParamSetPixelAspectRatio) {
        _pixelAspectRatio->setEnabled(_setPixelAspectRatio->getValue());
    } else if (paramName == kParamSetFrameRate) {
        _frameRate->setEnabled(_setFrameRate->getValue());
    } else if (paramName == kParamClipInfo) {
        std::ostringstream oss;
        oss << "Clip Info:\n\n";
        oss << "Input: ";
        if (!_srcClip) {
            oss << "N/A";
        } else {
            OFX::Clip &c = *_srcClip;
            oss << pixelComponentString(c.getPixelComponentsProperty());
            oss << bitDepthString(c.getPixelDepth());
            oss << " (unmapped: ";
            oss << pixelComponentString(c.getUnmappedPixelComponentsProperty());
            oss << bitDepthString(c.getUnmappedPixelDepth());
            oss << ")\npremultiplication: ";
            oss << premultString(c.getPreMultiplication());
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString(c.getPixelOrder());
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString(c.getFieldOrder());
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
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
            OFX::Clip &c = *_dstClip;
            oss << pixelComponentString(c.getPixelComponentsProperty());
            oss << bitDepthString(c.getPixelDepth());
            oss << " (unmapped: ";
            oss << pixelComponentString(c.getUnmappedPixelComponentsProperty());
            oss << bitDepthString(c.getUnmappedPixelDepth());
            oss << ")\npremultiplication: ";
            oss << premultString(c.getPreMultiplication());
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString(c.getPixelOrder());
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString(c.getFieldOrder());
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
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
        
        sendMessage(OFX::Message::eMessageMessage, "", oss.str());
    }
}

/* Override the clip preferences */
void
NoOpPlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    bool setPremult;
    _setPremult->getValue(setPremult);
    if (setPremult) {
        int premult_i;
        _premult->getValue(premult_i);
        PreMultiplicationEnum premult = (PreMultiplicationEnum)premult_i;

        clipPreferences.setOutputPremultiplication(premult);
    }
    if (_setFieldOrder) {
        // set the field order of _dstClip
        bool setFieldOrder;
        _setFieldOrder->getValue(setFieldOrder);
        if (setFieldOrder) {
            int fieldOrder_i;
            _fieldOrder->getValue(fieldOrder_i);
            FieldEnum fieldOrder = (FieldEnum)fieldOrder_i;

            clipPreferences.setOutputFielding(fieldOrder);
        }
    }
    if (_setPixelAspectRatio) {
        bool setPixelAspectRatio;
        _setPixelAspectRatio->getValue(setPixelAspectRatio);
        if (setPixelAspectRatio) {
            double pixelAspectRatio;
            _pixelAspectRatio->getValue(pixelAspectRatio);
            clipPreferences.setPixelAspectRatio(*_dstClip, pixelAspectRatio);
        }
    }
    if (_setFrameRate) {
        bool setFrameRate;
        _setFrameRate->getValue(setFrameRate);
        if (setFrameRate) {
            double frameRate;
            _frameRate->getValue(frameRate);
            clipPreferences.setOutputFrameRate(frameRate);
        }
    }
}

using namespace OFX;

mDeclarePluginFactory(NoOpPluginFactory, {}, {});

void NoOpPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
}

void NoOpPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetPremult);
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
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputPremult);
        param->setLabel(kParamOutputPremultLabel);
        param->setHint(kParamOutputPremultHint);
        assert(param->getNOptions() == eImageOpaque);
        param->appendOption(premultString(eImageOpaque));
        assert(param->getNOptions() == eImagePreMultiplied);
        param->appendOption(premultString(eImagePreMultiplied));
        assert(param->getNOptions() == eImageUnPreMultiplied);
        param->appendOption(premultString(eImageUnPreMultiplied));
        param->setDefault(eImagePreMultiplied); // images should be premultiplied in a compositing context
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }

    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();

    if (gHostDescription.supportsSetableFielding) {
        //// setFieldOrder
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetFieldOrder);
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
            OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOutputFieldOrder);
            param->setLabel(kParamOutputFieldOrderLabel);
            param->setHint(kParamOutputFieldOrderHint);
            assert(param->getNOptions() == eFieldNone);
            param->appendOption(fieldOrderString(eFieldNone));
            assert(param->getNOptions() == eFieldBoth);
            param->appendOption(fieldOrderString(eFieldBoth));
            assert(param->getNOptions() == eFieldLower);
            param->appendOption(fieldOrderString(eFieldLower));
            assert(param->getNOptions() == eFieldUpper);
            param->appendOption(fieldOrderString(eFieldUpper));
            assert(param->getNOptions() == eFieldSingle);
            param->appendOption(fieldOrderString(eFieldSingle));
            assert(param->getNOptions() == eFieldDoubled);
            param->appendOption(fieldOrderString(eFieldDoubled));
            param->setDefault(eFieldNone);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    if (gHostDescription.supportsMultipleClipPARs) {
        //// setPixelAspectRatio
        {
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetPixelAspectRatio);
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
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOutputPixelAspectRatio);
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
            OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSetFrameRate);
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
            OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamOutputFrameRate);
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
}

OFX::ImageEffect* NoOpPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new NoOpPlugin(handle);
}

void getNoOpPluginID(OFX::PluginFactoryArray &ids)
{
    static NoOpPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
