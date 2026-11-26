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
 * OFX NoOp plugin.
 * Does nothing.
 */

#include <sstream> // stringstream
#include <string>
#include <vector>
#include <cstdlib> // getenv
#include <cstdio> // rename
#include <cerrno>
#include <atomic>
#ifdef _WIN32
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility> // move
#include <process.h> // _getpid
#else
#include <fcntl.h> // open
#include <unistd.h> // write, close, unlink, getpid
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"
#include "ofxsNormalisedDefaults.h"
#include "ofxsGenerator.h" // for format-related parameters
#include "ofxsFormatResolution.h" // for format-related enums
#include "ofxsMultiThread.h" // getNumCPUs
#include "ofxColour.h" // kOfxImageEffectColourManagement*
#include "ofxDialog.h" // kOfxDialogSuite
#include "ofxDrawSuite.h" // kOfxDrawSuite
#include "ofxGPURender.h" // kOfxOpenGLRenderSuite, kOfxOpenCLProgramSuite
#include "ClipInfo.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;
using namespace OFX::ClipInfo;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "NoOpOFX"
#define kPluginGrouping "Other"
#define kPluginDescription "Copies the input to the output.\n" \
    "This effect does not modify the actual content of the image, but can be used to modify the metadata associated with the clip (premultiplication, field order, format, pixel aspect ratio, frame rate).\n" \
    "This plugin concatenates transforms."
#define kPluginIdentifier "net.sf.openfx.NoOpPlugin"

// History:
// Version 2.0: introduce setFormat, deprecate setPixelAspectRatio on Natron
// version 2.1: a NULL Source image (disconnected input; render is called although isIdentity returned true,
//              e.g. by hostDemo) renders black instead of crashing (SupportExt copyPixels);
//              Clip Info shows the display time offset and the displayed time (display only);
//              new "Host Info..." button shows the host description (display only, see issue #66)
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs true
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

#define kParamHostInfo "hostInfo"
#define kParamHostInfoLabel "Host Info..."
#define kParamHostInfoHint "Display what the OpenFX host supports (host name and version, properties, suites), " \
    "in the format of openfx-misc's README-hosts.txt. The same text is also saved to OFXHostInfo-<hostName>.txt " \
    "in the temporary directory (TMPDIR or /tmp, or the TEMP folder on Windows)."

#define kParamForceCopy "forceCopy"
#define kParamForceCopyLabel "Force Copy"
#define kParamForceCopyHint "Force copy from input to output"

#define kParamSupportsTiles "supportsTiles"
#define kParamSupportsTilesLabel "Supports Tiles"
#define kParamSupportsTilesHint "Does the plugin support image tiling, i.e. rendering only a subset of the full region of definition? Only supported on OpenFX 1.4 hosts."

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
        , _supportsTiles(NULL)
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
        const ImageEffectHostDescription &hostDescription = *getImageEffectHostDescription();
        if ( hostDescription.APIVersionMajor > 1 || (hostDescription.APIVersionMajor == 1 && hostDescription.APIVersionMinor >= 4) ) {
            _supportsTiles = fetchBooleanParam(kParamSupportsTiles);
        }
        _setPremult = fetchBooleanParam(kParamSetPremult);
        _premult = fetchChoiceParam(kParamOutputPremult);
        assert(_forceCopy && _setPremult && _premult);

        if (hostDescription.supportsSetableFielding) {
            _setFieldOrder = fetchBooleanParam(kParamSetFieldOrder);
            _fieldOrder = fetchChoiceParam(kParamOutputFieldOrder);
            assert(_setFieldOrder && _fieldOrder);
        }
#ifdef OFX_EXTENSIONS_NATRON
        if (hostDescription.isNatron) {
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
        if (hostDescription.supportsMultipleClipPARs) {
            _setPixelAspectRatio = fetchBooleanParam(kParamSetPixelAspectRatio);
            _pixelAspectRatio = fetchDoubleParam(kParamOutputPixelAspectRatio);
            assert(_setPixelAspectRatio && _pixelAspectRatio);
        }
        if (hostDescription.supportsSetableFrameRate) {
            _setFrameRate = fetchBooleanParam(kParamSetFrameRate);
            _frameRate = fetchDoubleParam(kParamOutputFrameRate);
            assert(_setFrameRate && _frameRate);
        }


        // finally
        syncPrivateData();
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

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        updateVisibility();
    }

    void updateVisibility()
    {
        const ImageEffectHostDescription &hostDescription = *getImageEffectHostDescription();

        _premult->setEnabled( _setPremult->getValue() );
        if (hostDescription.supportsSetableFielding) {
            _fieldOrder->setEnabled( _setFieldOrder->getValue() );
        }
#ifdef OFX_EXTENSIONS_NATRON
        if (hostDescription.isNatron) {
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
        if (hostDescription.supportsMultipleClipPARs) {
            _pixelAspectRatio->setEnabled( _setPixelAspectRatio->getValue() );
        }
        if (hostDescription.supportsSetableFrameRate) {
            _frameRate->setEnabled( _setFrameRate->getValue() );
        }
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam *_forceCopy;
    BooleanParam *_supportsTiles;
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
    // Do not use the format PAR here: when Set Pixel Aspect Ratio is checked, the output PAR is
    // _pixelAspectRatio (it overrides the format PAR in getClipPreferences), as in getRegionsOfInterest.
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

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( src.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
# endif
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );
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


// Support's enum-to-string functions throw std::invalid_argument on values they don't know (e.g. a
// custom component that the host lists): return the placeholder instead.
template <typename Enum>
const char*
nameOr(const char* (*toStr)(Enum),
       Enum e,
       const char* placeholder)
{
    try {
        return toStr(e);
    } catch (...) {
        return placeholder;
    }
}

// Append "key=item0,item1,...\n", with format(item) printed for each item.
template <typename Container, typename Format>
void
joinTo(std::ostringstream& oss,
       const char* key,
       const Container& items,
       Format format)
{
    oss << key << '=';
    for (std::size_t i = 0; i < items.size(); ++i) {
        oss << (i ? "," : "") << format(items[i]);
    }
    oss << '\n';
}

static const char*
nativeOriginName(NativeOriginEnum e)
{
    switch (e) {
    case eNativeOriginBottomLeft:
        return kOfxHostNativeOriginBottomLeft;
    case eNativeOriginTopLeft:
        return kOfxHostNativeOriginTopLeft;
    case eNativeOriginCenter:
        return kOfxHostNativeOriginCenter;
    }

    return "[unknown origin]";
}

static const char*
colourManagementStyleName(ColourManagementStyleEnum e)
{
    switch (e) {
    case eColourManagementStyleNone:
        return kOfxImageEffectColourManagementNone;
    case eColourManagementStyleBasic:
        return kOfxImageEffectColourManagementBasic;
    case eColourManagementStyleCore:
        return kOfxImageEffectColourManagementCore;
    case eColourManagementStyleFull:
        return kOfxImageEffectColourManagementFull;
    case eColourManagementStyleOCIO:
        return kOfxImageEffectColourManagementOCIO;
    }

    return "[unknown style]";
}

// Append the suite name to the comma-separated list if the host provides it.
// "Repeated calls to fetchSuite with the same parameters will return the same pointer" (ofxCore.h),
// so fetching again after kOfxActionLoad is harmless; optional=true never throws.
static void
appendSuite(std::ostringstream& oss,
            bool& first,
            const char* suiteName,
            int suiteVersion,
            const char* printedName)
{
    if ( fetchSuite(suiteName, suiteVersion, true) ) {
        if (!first) {
            oss << ',';
        }
        first = false;
        oss << printedName;
    }
}

// Append the host description, in the format of README-hosts.txt (one "key=value" per line, 0/1 for
// booleans). Everything comes from the host description that Support read at kOfxActionLoad with the
// non-throwing getters (a property that an OFX 1.4 host lacks gets its default), plus fetchSuite.
static void
appendHostInfo(std::ostringstream& oss)
{
    const ImageEffectHostDescription& h = *getImageEffectHostDescription();

    // (Support sets 1.0 when the host does not set kOfxPropAPIVersion, OFX 1.2)
    oss << "OFX API version " << h.APIVersionMajor << '.' << h.APIVersionMinor << '\n';
    oss << "hostName=" << h.hostName << '\n';
    oss << "hostLabel=" << h.hostLabel << '\n';
    oss << "hostVersion=" << h.versionMajor << '.' << h.versionMinor << '.' << h.versionMicro;
    oss << " (" << h.versionLabel << ")\n";
    oss << "hostIsBackground=" << h.hostIsBackground << '\n';
    oss << "supportsOverlays=" << h.supportsOverlays << '\n';
    oss << "supportsDrawSuite=" << h.supportsDrawSuite << '\n';
    oss << "supportsMultiResolution=" << h.supportsMultiResolution << '\n';
    oss << "supportsTiles=" << h.supportsTiles << '\n';
    oss << "temporalClipAccess=" << h.temporalClipAccess << '\n';
    const auto asIs = [](const std::string& item) -> const std::string& { return item; };
    joinTo(oss, "supportedComponents", h._supportedComponents,
           [](PixelComponentEnum e) { return nameOr(mapPixelComponentEnumToStr, e, "[custom components]"); });
    joinTo(oss, "supportedContexts", h._supportedContexts,
           [](ContextEnum e) { return nameOr(mapContextEnumToStr, e, "[unknown context]"); });
    joinTo(oss, "supportedPixelDepths", h._supportedPixelDepths,
           [](BitDepthEnum e) { return nameOr(mapBitDepthEnumToStr, e, "[unknown depth]"); });
    oss << "supportsMultipleClipDepths=" << h.supportsMultipleClipDepths << '\n';
    oss << "supportsMultipleClipPARs=" << h.supportsMultipleClipPARs << '\n';
    oss << "supportsSetableFrameRate=" << h.supportsSetableFrameRate << '\n';
    oss << "supportsSetableFielding=" << h.supportsSetableFielding << '\n';
    // render threads: kOfxImageEffectInstancePropSequentialRender on the host (0: never sequential,
    // 1: always, 2: if the plug-in asks; OFX 1.2), and the multithread suite's number of CPUs
    oss << "sequentialRender=" << h.sequentialRender << '\n';
    oss << "numCPUs=" << MultiThread::getNumCPUs() << '\n';
    oss << "supportsRenderQualityDraft=" << h.supportsRenderQualityDraft << '\n';
    oss << "supportsStringAnimation=" << h.supportsStringAnimation << '\n';
    oss << "supportsCustomInteract=" << h.supportsCustomInteract << '\n';
    oss << "supportsChoiceAnimation=" << h.supportsChoiceAnimation << '\n';
    oss << "supportsStrChoice=" << h.supportsStrChoice << '\n';
    oss << "supportsStrChoiceAnimation=" << h.supportsStrChoiceAnimation << '\n';
    oss << "supportsBooleanAnimation=" << h.supportsBooleanAnimation << '\n';
    oss << "supportsCustomAnimation=" << h.supportsCustomAnimation << '\n';
    oss << "supportsParametricParameter=" << h.supportsParametricParameter << '\n';
    oss << "supportsParametricAnimation=" << h.supportsParametricAnimation << '\n';
#ifdef OFX_EXTENSIONS_NUKE
    oss << "canTransform=" << h.canTransform << '\n';
#endif
    oss << "nativeOrigin=" << nativeOriginName(h.nativeOrigin) << '\n';
    oss << "osHandle=" << (h.osHandle ? 1 : 0) << '\n';
    oss << "supportsCPURender=" << h.supportsCPURender << '\n';
#ifdef OFX_SUPPORTS_OPENGLRENDER
    // kOfxImageEffectPropOpenGLRenderSupported is "true" and the host provides the OpenGL render suite
    oss << "supportsOpenGLRender=" << h.supportsOpenGLRender << '\n';
#endif
    oss << "supportsOpenCLRender=" << h.supportsOpenCLRender << '\n';
    oss << "supportsCudaRender=" << h.supportsCudaRender << '\n';
    oss << "supportsCudaStream=" << h.supportsCudaStream << '\n';
    oss << "supportsMetalRender=" << h.supportsMetalRender << '\n';
    oss << "supportsMetalTexture=" << h.supportsMetalTexture << '\n';
    oss << "colourManagementStyle=" << colourManagementStyleName(h.colourManagementStyle) << '\n';
    joinTo(oss, "colourManagementAvailableConfigs", h.colourManagementAvailableConfigs, asIs);
#ifdef OFX_EXTENSIONS_NUKE
    oss << "supportsCamera=" << h.supportsCamera << '\n';
    oss << "isMultiPlanar=" << h.isMultiPlanar << '\n';
#endif
    oss << "maxParameters=" << h.maxParameters << '\n';
    oss << "maxPages=" << h.maxPages << '\n';
    oss << "pageRowCount=" << h.pageRowCount << '\n';
    oss << "pageColumnCount=" << h.pageColumnCount << '\n';
    oss << "supportsProgressSuite=" << h.supportsProgressSuite << '\n';
    oss << "supportsTimeLineSuite=" << h.supportsTimeLineSuite << '\n';
    oss << "supportsMessageSuiteV2=" << h.supportsMessageSuiteV2 << '\n';
#ifdef OFX_EXTENSIONS_NATRON
    oss << "isNatron=" << h.isNatron << '\n';
    oss << "supportsDynamicChoices=" << h.supportsDynamicChoices << '\n';
    oss << "supportsCascadingChoices=" << h.supportsCascadingChoices << '\n';
    oss << "supportsChannelSelector=" << h.supportsChannelSelector << '\n';
    oss << "canDistort=" << h.canDistort << '\n';
    oss << "supportsInteractKeyModifiers=" << h.supportsInteractKeyModifiers << '\n';
    joinTo(oss, "nativeOverlays", h.nativeInteracts,
           [](const ImageEffectHostDescription::NativeOverlayHandle& n) -> const std::string& { return n.identifier; });
    joinTo(oss, "supportedCursors", h.supportedCursors, asIs);
#endif
    oss << "suites=";
    bool first = true;
    appendSuite(oss, first, kOfxImageEffectSuite, 1, kOfxImageEffectSuite);
    appendSuite(oss, first, kOfxPropertySuite, 1, kOfxPropertySuite);
    appendSuite(oss, first, kOfxParameterSuite, 1, kOfxParameterSuite);
    appendSuite(oss, first, kOfxMemorySuite, 1, kOfxMemorySuite);
    appendSuite(oss, first, kOfxMultiThreadSuite, 1, kOfxMultiThreadSuite);
    appendSuite(oss, first, kOfxMessageSuite, 1, kOfxMessageSuite);
    appendSuite(oss, first, kOfxMessageSuite, 2, kOfxMessageSuite "V2");
    appendSuite(oss, first, kOfxProgressSuite, 1, kOfxProgressSuite);
    appendSuite(oss, first, kOfxProgressSuite, 2, kOfxProgressSuite "V2");
    appendSuite(oss, first, kOfxTimeLineSuite, 1, kOfxTimeLineSuite);
    appendSuite(oss, first, kOfxParametricParameterSuite, 1, kOfxParametricParameterSuite);
    appendSuite(oss, first, kOfxInteractSuite, 1, kOfxInteractSuite);
    appendSuite(oss, first, kOfxDrawSuite, 1, kOfxDrawSuite);
    appendSuite(oss, first, kOfxOpenGLRenderSuite, 1, kOfxOpenGLRenderSuite);
    appendSuite(oss, first, kOfxOpenCLProgramSuite, 1, kOfxOpenCLProgramSuite);
    appendSuite(oss, first, kOfxDialogSuite, 1, kOfxDialogSuite);
#ifdef OFX_EXTENSIONS_NUKE
    appendSuite(oss, first, kNukeOfxCameraSuite, 1, kNukeOfxCameraSuite);
    appendSuite(oss, first, kFnOfxImageEffectPlaneSuite, 1, kFnOfxImageEffectPlaneSuite "V1");
    appendSuite(oss, first, kFnOfxImageEffectPlaneSuite, 2, kFnOfxImageEffectPlaneSuite "V2");
#endif
#ifdef OFX_EXTENSIONS_VEGAS
    appendSuite(oss, first, kOfxVegasProgressSuite, 1, kOfxVegasProgressSuite);
    appendSuite(oss, first, kOfxVegasStereoscopicImageEffectSuite, 1, kOfxVegasStereoscopicImageEffectSuite);
    appendSuite(oss, first, kOfxVegasKeyframeSuite, 1, kOfxVegasKeyframeSuite);
    appendSuite(oss, first, kOfxHWndInteractSuite, 1, kOfxHWndInteractSuite);
#endif
    oss << '\n';
} // appendHostInfo

// Save the host info text to OFXHostInfo-<hostName>.txt in the user's temporary directory, because
// some hosts truncate messages or show them in a small dialog. Best effort, never throws: returns
// the path to show the user, or an empty string if the file could not be written (then no
// file, not even a partial one, is left behind).
static std::string
saveHostInfo(const std::string& hostName,
             const std::string& text)
{
    // keep only portable file name characters of the host name, and at most 64 of them
    std::string fileName = "OFXHostInfo-";
    for (std::size_t i = 0; i < hostName.size() && i < 64; ++i) {
        const char c = hostName[i];
        const bool ok = ( (c >= 'A') && (c <= 'Z') ) || ( (c >= 'a') && (c <= 'z') ) || ( (c >= '0') && (c <= '9') ) ||
                        (c == '.') || (c == '-') || (c == '_');
        fileName += ok ? c : '_';
    }
    fileName += ".txt";

    // The text is written to a new file next to the final name, which then replaces the final name
    // (rename), so that a failure leaves no partial file. A unique suffix for the new file: several
    // instances, or hosts, may save at the same time.
    static std::atomic<unsigned> counter(0);
    std::ostringstream suffix;
#ifdef _WIN32
    suffix << ".tmp" << _getpid() << '.' << counter++;
#else
    suffix << ".tmp" << getpid() << '.' << counter++;
#endif

#ifdef _WIN32
    // temp_directory_path() is %TMP%, %TEMP% or %USERPROFILE% (GetTempPath2W), per-user: no other user can
    // plant a file there. Text mode writes CRLF (Notepad).
    namespace fs = std::filesystem;
    fs::path tmpPath; // empty until the new file is named
    try {
        std::error_code ec;
        const fs::path path = fs::temp_directory_path(ec) / fileName;
        if (ec) {
            return std::string();
        }
        fs::path newPath = path;
        newPath += suffix.str();
        tmpPath = std::move(newPath); // named only once complete, so the catch never removes the old file
        std::ofstream out(tmpPath);
        out << text;
        out.close(); // sets failbit if a write or the close failed
        if (out) {
            fs::rename(tmpPath, path, ec); // replaces an existing file
        }
        if (!out || ec) {
            std::error_code ignored;
            fs::remove(tmpPath, ignored);

            return std::string();
        }
        const auto u8 = path.u8string(); // std::string in C++17, std::u8string in C++20

        return std::string( u8.begin(), u8.end() );
    } catch (...) {
        if ( !tmpPath.empty() ) {
            std::error_code ec;
            fs::remove(tmpPath, ec); // no partial file (a no-op after the rename)
        }

        return std::string();
    }
#else
    // The temporary directory may be shared (/tmp, with TMPDIR unset on Linux), and another user may
    // have planted a FIFO, a symbolic or hard link, or a file of their own at the final name: the
    // text is written to a new file next to it, created with O_EXCL (never an existing file, FIFO
    // or link) and mode 0600, which then replaces the final name with rename(). If the final name
    // belongs to another user in a sticky directory, rename() fails, and only the message is shown.
    const char* dir = std::getenv("TMPDIR");
    std::string path = (dir && *dir) ? dir : "/tmp";
    if (path[path.size() - 1] != '/') {
        path += '/';
    }
    path += fileName;
    const std::string tmpPath = path + suffix.str();
    const int fd = open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        return std::string();
    }
    std::size_t written = 0;
    while ( written < text.size() ) {
        const ssize_t n = write(fd, text.data() + written, text.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (n == 0) {
            break;
        }
        written += (std::size_t)n;
    }
    const bool closed = close(fd) == 0;
    // rename() replaces the directory entry atomically, whatever it was (it never follows a link)
    if ( (written != text.size()) || !closed || (std::rename( tmpPath.c_str(), path.c_str() ) != 0) ) {
        unlink( tmpPath.c_str() );

        return std::string();
    }

    return path;
#endif
} // saveHostInfo

void
NoOpPlugin::changedParam(const InstanceChangedArgs &args,
                         const std::string &paramName)
{
    if (paramName == kParamSupportsTiles) {
        bool supportsTiles = _supportsTiles->getValueAtTime(args.time);
        setSupportsTiles(supportsTiles);
    } else if (paramName == kParamSetPremult) {
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
            appendClipInfo(oss, *_srcClip, args.time);
        }
        oss << "\n\n";
        oss << "Output: ";
        if (!_dstClip) {
            oss << "N/A";
        } else {
            appendClipInfo(oss, *_dstClip, args.time);
        }
        oss << "\n\n";
        oss << "time: " << args.time;
        if (_dstClip) {
            // the frame the host shows the user for args.time (0 offset if the host does not set
            // kOfxImageClipPropDisplayTimeOffset; getDisplayTimeOffset does not throw)
            oss << " (displayed as " << args.time + _dstClip->getDisplayTimeOffset() << ')';
        }
        oss << ", renderscale: " << args.renderScale.x << 'x' << args.renderScale.y << '\n';

        sendMessage( Message::eMessageMessage, "", oss.str() );
    } else if (paramName == kParamHostInfo) {
        std::ostringstream oss;
        appendHostInfo(oss);
        const std::string text = oss.str();
        const std::string saved = saveHostInfo(getImageEffectHostDescription()->hostName, text);
        if ( saved.empty() ) {
            sendMessage(Message::eMessageMessage, "", text);
        } else {
            sendMessage(Message::eMessageMessage, "", text + "\n(also saved to " + saved + ")\n");
        }
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
    ofxsSetOverlayInteractDescriptorV2(desc, new GeneratorOverlayDescriptorV2);
}

void
NoOpPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                     ContextEnum /*context*/)
{
    const ImageEffectHostDescription &hostDescription = *getImageEffectHostDescription();

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

    // supportsTiles
    if ( hostDescription.APIVersionMajor > 1 || (hostDescription.APIVersionMajor == 1 && hostDescription.APIVersionMinor >= 4) ) {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamSupportsTiles);
        param->setLabel(kParamSupportsTilesLabel);
        param->setHint(kParamSupportsTilesHint);
        param->setDefault(true);
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

    if (hostDescription.supportsSetableFielding) {
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
    if (hostDescription.isNatron) {
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
            ofxsSetDefaultCoordinateSystemNormalised(*param); // see ofxsNormalisedDefaults.h (#236)
            param->setDefault(0., 0.);
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
            ofxsSetDefaultCoordinateSystemNormalised(*param); // see ofxsNormalisedDefaults.h (#236)
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

    if (hostDescription.supportsMultipleClipPARs) {
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

    if (hostDescription.supportsSetableFrameRate) {
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

    // hostInfo
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamHostInfo);
        param->setLabel(kParamHostInfoLabel);
        param->setHint(kParamHostInfoHint);
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
