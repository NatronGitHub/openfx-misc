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
 * OFX SlitScan plugin.
 */

/*
 [JCL]
 • J'aimerai bien que l'on aie dans les nodes de retiming un Slitscan (qui manque à Nuke) :
 entrée 1 : source
 entrée 2 : retime Map

 Pour chaque pixel l'offset de timing est déterminé par la valeur de la retime Map
 On peut avoir un mode de retime map absolu en virgule flottante et un autre mappé de 0 à 1 avec un Range à définir (pas de retime pour 0.5).

 Fred tu nous fais ça ? :)

 [CoyHot]
 Une question à se posait aussi : à quoi ressemblerait la retime map :

 -  Min/max entre -1 et 1 avec un facteur multiplicateur ?
 - Greyscale visible pour que ça soit plus compréhensible avec mid-grey à 0.5 qui ne bouge pas et les valeur plus sombre en négatif et plus clair en positif ?

 [JCL]
 C'était un peu ce que je proposait :
 On peut avoir un mode de retime map absolu en virgule flottante (la valeur vaut le décalage d'image) et un autre mappé de 0 à 1 avec un Range à définir (pas de retime pour 0.5).

 [Fred]
 pour les valeurs qui tombent entre deux images, vous espérez quoi?

 - image la plus proche?
 - blend linéaire entre les deux images les plus proches? (risque d'être moche)
 - retiming? (ce sera pas pour tout de suite... il y a masse de boulot pour faire un bon retiming)

 Il y aura un paramètre un paramètre d'offset (valeur par défaut -0.5), et un paramètre de gain, évidemment, parce que ce qui est rigolo c'est de jouer avec ce paramètre.

 valeurs par défaut: offset à -0.5, multiplicateur à 10, frame range -5..5

 pour passer en mode "retime map en nombre de frames", il suffirait de mettre l'offset à 0 et le multiplicateur à 1. Ou bien est-ce qu'il faut une case à cocher pour forcer ce mode (qui désactiverait les deux autres paramètres?)

 il faudrait aussi une case à cocher "absolute". Dans ce cas le frame range et les valeurs données dans la retime map sont absolues.

 Et il y aura aussi un paramètre pourri qui s'appellera "maximum input frame range". Ce paramètre permettra de préfetcher toutes les images nécessaires à l'effet avant son exécution. On pourra dépasser ce range sous Natron, mais pas sous Nuke (Natron est plus tolérant).
*/

#include "SlitScan.h"

#include <cmath> // for floor
#include <climits> // for INT_MAX
#include <cassert>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsPixelProcessor.h"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

#define kPluginName "SlitScan"
#define kPluginGrouping "Time"
#define kPluginDescription \
"Apply per-pixel retiming: the time is computed for each pixel from the retime function (by default, it is a vertical ramp).\n" \
"\n" \
"The default retime function corresponds to a horizontal slit: it is a vertical ramp, which is a linear function of y, which is 0 at the center of the bottom image line, and 1 at the center of the tom image line. Optionally, a vertical slit may be used (0 at the center of the leftmost image column, 1 at the center of the rightmost image column), or the optional single-channel \"Retime Map\" input may also be used.\n" \
"\n" \
"This plugin requires to render many frames on input, which may fill up the host cache, but if more than 4 frames required on input, Natron renders them on-demand, rather than pre-caching them.\n" \
"Note that the results may be on higher quality if the video is slowed fown (e.g. using slowmoVideo)\n" \
"\n" \
"The parameters are:\n" \
"- retime function (default = horizontal slit)\n" \
"- offset for the retime function (default = 0)\n" \
"- gain for the retime function (default = -10)\n" \
"- absolute, a boolean indicating that the time map gives absolute frames rather than relative frames\n" \
"- frame range, only if RetimeMap is connected, because the frame range depends on the images (default = -10..0). If \"absolute\" is checked, this frame range is absolute, else it is relative to the current frame\n" \
"- filter to handle time offsets that \"fall between\" frames. They can be mapped to the nearest frame, or interpolated between the nearest frames.\n" \


#define kPluginIdentifier "net.sf.openfx.SlitScan"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipRetimeMap "Retime Map"

#define kParamRetimeFunction "retimeFunction"
#define kParamRetimeFunctionLabel "Retime Function"
#define kParamRetimeFunctionHint "The function that gives, for each pixel in the image, its time. The default retime function corresponds to a horizontal slit: it is a vertical ramp (a linear function of y) which is 0 at the center of the bottom image line, and 1 at the center of the top image line. Optionally, a vertical slit may be used (0 at the center of the leftmost image column, 1 at the center of the rightmost image column), or the optional single-channel \"Retime Map\" input may also be used."
#define kParamRetimeFunctionOptionHorizontalSlit "Horizontal Slit"
#define kParamRetimeFunctionOptionHorizontalSlitHint "A vertical ramp (a linear function of y) which is 0 at the center of the bottom image line, and 1 at the center of the tom image line."
#define kParamRetimeFunctionOptionVerticalSlit "Vertical Slit"
#define kParamRetimeFunctionOptionVerticalSlitHint "A horizontal ramp (alinear function of x) which is 0 at the center of the leftmost image line, and 1 at the center of the rightmost image line."
#define kParamRetimeFunctionOptionRetimeMap "Retime Map"
#define kParamRetimeFunctionOptionRetimeMapHint "The single-channel image from the \"Retime Map\" input (zero if not connected)."
#define kParamRetimeFunctionDefault eRetimeFunctionHorizontalSlit
enum RetimeFunctionEnum {
    eRetimeFunctionHorizontalSlit,
    eRetimeFunctionVerticalSlit,
    eRetimeFunctionRetimeMap,
};

#define kParamRetimeFunctionOptionVertical
#define kParamRetimeOffset "retimeOffset"
#define kParamRetimeOffsetLabel "Retime Offset"
#define kParamRetimeOffsetHint "Offset to the retime map."
#define kParamRetimeOffsetDefault 0.

#define kParamRetimeGain "retimeGain"
#define kParamRetimeGainLabel "Retime Offset"
#define kParamRetimeGainHint "Gain applied to the retime map (after offset). With the horizontal or vertical slits, to get one line or column per frame you should use respectively (height-1) or (width-1)."
#define kParamRetimeGainDefault -10

#define kParamRetimeAbsolute "retimeAbsolute"
#define kParamRetimeAbsoluteLabel "Absolute"
#define kParamRetimeAbsoluteHint "If checked, the retime map contains absolute time, if not it is relative to the current frame."
#define kParamRetimeAbsoluteDefault false

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Max. Frame Range"
#define kParamFrameRangeHint "Maximum input frame range to fetch images from (may be relative or absolute, depending on the \"absolute\" parameter). Only used if the Retime Map is connected."
#define kParamFrameRangeDefault -10,0

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "How input images are combined to compute the output image."

#define kParamFilterOptionNearest "Nearest"
#define kParamFilterOptionNearestHint "Pick input image with nearest integer time."
#define kParamFilterOptionLinear "Linear"
#define kParamFilterOptionLinearHint "Blend the two nearest images with linear interpolation."
// TODO:
#define kParamFilterOptionBox "Box"
#define kParamFilterOptionBoxHint "Weighted average of images over the shutter time (shutter time is defined in the output sequence)." // requires shutter parameter

enum FilterEnum {
    //eFilterNone, // nonsensical with SlitScan
    eFilterNearest,
    eFilterLinear,
    //eFilterBox,
};
#define kParamFilterDefault eFilterNearest


namespace OFX {
    extern ImageEffectHostDescription gHostDescription;
}
////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SlitScanPlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;            /**< @brief Mandated output clips */
    OFX::Clip *_srcClip;            /**< @brief Mandated input clips */
    OFX::Clip *_retimeMapClip;      /**< @brief Optional retime map */

    OFX::ChoiceParam  *_retimeFunction;
    OFX::DoubleParam  *_retimeOffset;
    OFX::DoubleParam  *_retimeGain;
    OFX::BooleanParam *_retimeAbsolute;
    OFX::Int2DParam *_frameRange;
    OFX::ChoiceParam *_filter;   /**< @brief how images are interpolated (or not). */

public:
    /** @brief ctor */
    SlitScanPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _retimeMapClip(0)
    , _retimeFunction(0)
    , _retimeOffset(0)
    , _retimeGain(0)
    , _retimeAbsolute(0)
    , _frameRange(0)
    , _filter(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        _retimeMapClip = fetchClip(kClipRetimeMap);

        _retimeFunction = fetchChoiceParam(kParamRetimeFunction);
        _retimeOffset = fetchDoubleParam(kParamRetimeOffset);
        _retimeGain = fetchDoubleParam(kParamRetimeGain);
        _retimeAbsolute = fetchBooleanParam(kParamRetimeAbsolute);
        _frameRange = fetchInt2DParam(kParamFrameRange);
        _filter = fetchChoiceParam(kParamFilter);
        assert(_retimeFunction && _retimeOffset && _retimeGain && _retimeAbsolute && _frameRange && _filter);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    //template <int nComponents>
    //void renderInternal(const OFX::RenderArguments &args, double sourceTime, FilterEnum filter, OFX::BitDepthEnum dstBitDepth);

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /* set up and run a processor */
    //void setupAndProcess(OFX::SlitScanerBase &, const OFX::RenderArguments &args, double sourceTime, FilterEnum filter);

};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum       srcBitDepth    = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}


void
SlitScanPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                              OFX::FramesNeededSetter &frames)
{
    if (!_srcClip) {
        return;
    }
    const double time = args.time;
    double tmin, tmax;
    bool retimeAbsolute;
    _retimeAbsolute->getValueAtTime(time, retimeAbsolute);
    int retimeFunction_i;
    _retimeFunction->getValueAtTime(time, retimeFunction_i);
    RetimeFunctionEnum retimeFunction = (RetimeFunctionEnum)retimeFunction_i;
    if (retimeFunction == eRetimeFunctionRetimeMap) {
        int t1, t2;
        _frameRange->getValueAtTime(time, t1, t2);
        if (retimeAbsolute) {
            tmin = std::min(t1, t2);
            tmax = std::max(t1, t2);
        } else {
            tmin = time + std::min(t1, t2);
            tmax = time + std::max(t1, t2);
        }
    } else {
        double retimeOffset, retimeGain;
        _retimeOffset->getValueAtTime(time, retimeOffset);
        _retimeGain->getValueAtTime(time, retimeGain);
        tmin = (retimeGain >  0) ? retimeOffset : retimeOffset + retimeGain;
        tmax = (retimeGain <= 0) ? retimeOffset : retimeOffset + retimeGain;
        if (!retimeAbsolute) {
            tmin += time;
            tmax += time;
        }
        int filter_i;
        _filter->getValueAtTime(time, filter_i);
        FilterEnum filter = (FilterEnum)filter_i;
        if (filter == eFilterNearest) {
            tmin = std::floor(tmin + 0.5);
            tmax = std::floor(tmax + 0.5);
        } else if (filter == eFilterLinear) {
            tmin = std::floor(tmin);
            tmax = std::ceil(tmax);
        }

    }

    OfxRangeD range;
    range.min = tmin;
    range.max = tmax;
    frames.setFramesNeeded(*_srcClip, range);
}

bool
SlitScanPlugin::isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime)
{
    const double time = args.time;
    double retimeGain;
    _retimeGain->getValueAtTime(time, retimeGain);

    int retimeFunction_i;
    _retimeFunction->getValueAtTime(time, retimeFunction_i);
    RetimeFunctionEnum retimeFunction = (RetimeFunctionEnum)retimeFunction_i;
    if (retimeFunction == eRetimeFunctionRetimeMap && !(_retimeMapClip && _retimeMapClip->isConnected())) {
        // no retime map, equivalent to value = 0 everywhere
        retimeGain = 0.;
    }

    if (retimeGain == 0.) {
        double retimeOffset;
        _retimeOffset->getValueAtTime(time, retimeOffset);
        bool retimeAbsolute;
        _retimeAbsolute->getValueAtTime(time, retimeAbsolute);
        identityTime = retimeAbsolute ? retimeOffset : (time + retimeOffset);
        int filter_i;
        if (identityTime != (int)identityTime) {
            _filter->getValueAtTime(time, filter_i);
            FilterEnum filter = (FilterEnum)filter_i;
            if (filter == eFilterNearest) {
                identityTime = std::floor(identityTime + 0.5);
            } else {
                return false; // result is blended
            }
        }
        identityClip = _srcClip;
        return true;
    }
    return false;
}

#if 0
/* set up and run a processor for a standard horizontal slit scan with linear time interpolation */
void
SlitScanPlugin::setupAndProcessSlitScanLinear(OFX::ImageBlenderBase &processor,
                                              const OFX::RenderArguments &args,
                                              double sourceTime,
                                              FilterEnum filter)
{
    const double time = args.time;
    // get a dst image
    std::auto_ptr<OFX::Image>  dst(_dstClip->fetchImage(time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // TODO: loop over lines
    if (sourceTime == (int)sourceTime || filter == eFilterNone || filter == eFilterNearest) {
        // should have been caught by isIdentity...
        std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                            _srcClip->fetchImage(sourceTime) : 0);
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
        return;
    }

    // figure the two images we are blending between
    double fromTime, toTime;
    double blend;
    framesNeeded(sourceTime, args.fieldToRender, &fromTime, &toTime, &blend);

    // fetch the two source images
    std::auto_ptr<OFX::Image> fromImg((_srcClip && _srcClip->isConnected()) ?
                                      _srcClip->fetchImage(fromTime) : 0);
    std::auto_ptr<OFX::Image> toImg((_srcClip && _srcClip->isConnected()) ?
                                    _srcClip->fetchImage(toTime) : 0);

    // make sure bit depths are sane
    if (fromImg.get()) {
        if (fromImg->getRenderScale().x != args.renderScale.x ||
            fromImg->getRenderScale().y != args.renderScale.y ||
            (fromImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && fromImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if (toImg.get()) {
        if (toImg->getRenderScale().x != args.renderScale.x ||
            toImg->getRenderScale().y != args.renderScale.y ||
            (toImg->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && toImg->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setFromImg(fromImg.get());
    processor.setToImg(toImg.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the blend between
    processor.setBlend((float)blend);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the internal render function for a standard horizontal slit scan with linear time interpolation

template <int nComponents>
void
SlitScanPlugin::renderInternalSlitScanLinear(const OFX::RenderArguments &args,
                                             double sourceTime,
                                             FilterEnum filter,
                                             OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte: {
            OFX::ImageBlender<unsigned char, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        case OFX::eBitDepthUShort: {
            OFX::ImageBlender<unsigned short, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        case OFX::eBitDepthFloat: {
            OFX::ImageBlender<float, nComponents> fred(*this);
            setupAndProcess(fred, args, sourceTime, filter);
            break;
        }
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}
#endif

// the overridden render function
void
SlitScanPlugin::render(const OFX::RenderArguments &args)
{
    const double time = args.time;
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    int retimeFunction_i;
    _retimeFunction->getValueAtTime(time, retimeFunction_i);
    
#if 0

    if (_retimeMapClip && _retimeMapClip->isConnected()) {
#pragma message WARN("TODO")
        OFX::throwSuiteStatusException(kOfxStatFailed);
        return;
    } else {
        int filter_i;
        _filter->getValueAtTime(time, filter_i);
        FilterEnum filter = (FilterEnum)filter_i;

        switch (filter) {
            case eFilterNearest:
#pragma message WARN("TODO")
                OFX::throwSuiteStatusException(kOfxStatFailed);
                break;
            case eFilterLinear:
                // do the rendering
                if (dstComponents == OFX::ePixelComponentRGBA) {
                    renderInternalSlitScanHorizontal<4>(args, dstBitDepth);
                } else if (dstComponents == OFX::ePixelComponentRGB) {
                    renderInternalSlitScanLinear<3>(args, dstBitDepth);
                } else if (dstComponents == OFX::ePixelComponentXY) {
                    renderInternalSlitScanLinear<2>(args, dstBitDepth);
                } else {
                    assert(dstComponents == OFX::ePixelComponentAlpha);
                    renderInternalSlitScanLinear<1>(args, dstBitDepth);
                }
                break;
        }
    }
#else
    OFX::throwSuiteStatusException(kOfxStatFailed);
#endif
}

using namespace OFX;
mDeclarePluginFactory(SlitScanPluginFactory, ;, {});

void SlitScanPluginFactory::load()
{
    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
}

/** @brief The basic describe function, passed a plugin descriptor */
void SlitScanPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(OFX::eContextFilter);
    desc.addSupportedContext(OFX::eContextGeneral);

    // Add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true); // say we will be doing random time access on clips
    desc.setRenderTwiceAlways(true); // each field has to be rendered separately, since it may come from a different time
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    // we can't be used on hosts that don't perfrom temporal clip access
    if (!gHostDescription.temporalClipAccess) {
        throw OFX::Exception::HostInadequate("Need random temporal image access to work");
    }
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

/** @brief The describe in context function, passed a plugin descriptor and a context */
void SlitScanPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true); // say we will be doing random time access on this clip
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway

    ClipDescriptor *retimeMapClip = desc.defineClip(kClipRetimeMap);
    retimeMapClip->addSupportedComponent(ePixelComponentAlpha);
    retimeMapClip->setSupportsTiles(kSupportsTiles);
    retimeMapClip->setOptional(true);
    retimeMapClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setFieldExtraction(eFieldExtractDoubled); // which is the default anyway
    dstClip->setSupportsTiles(kSupportsTiles);

    // make a page to put it in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRetimeFunction);
        param->setLabel(kParamRetimeFunctionLabel);
        param->setHint(kParamRetimeFunctionHint);
        assert(param->getNOptions() == eRetimeFunctionHorizontalSlit);
        param->appendOption(kParamRetimeFunctionOptionHorizontalSlit, kParamRetimeFunctionOptionHorizontalSlitHint);
        assert(param->getNOptions() == eRetimeFunctionVerticalSlit);
        param->appendOption(kParamRetimeFunctionOptionVerticalSlit, kParamRetimeFunctionOptionVerticalSlitHint);
        assert(param->getNOptions() == eRetimeFunctionRetimeMap);
        param->appendOption(kParamRetimeFunctionOptionRetimeMap, kParamRetimeFunctionOptionRetimeMapHint);
        param->setDefault((int)kParamRetimeFunctionDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamRetimeOffset);
        param->setDefault(kParamRetimeOffsetDefault);
        param->setHint(kParamRetimeOffsetHint);
        param->setLabel(kParamRetimeOffsetLabel);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamRetimeGain);
        param->setDefault(kParamRetimeGainDefault);
        param->setHint(kParamRetimeGainHint);
        param->setLabel(kParamRetimeGainLabel);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamRetimeAbsolute);
        param->setDefault(kParamRetimeAbsoluteDefault);
        param->setHint(kParamRetimeAbsoluteHint);
        param->setLabel(kParamRetimeAbsoluteLabel);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setDefault(kParamFrameRangeDefault);
        param->setHint(kParamFrameRangeHint);
        param->setLabel(kParamFrameRangeLabel);
        param->setDimensionLabels("min", "max");
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterNearest);
        param->appendOption(kParamFilterOptionNearest, kParamFilterOptionNearestHint);
        assert(param->getNOptions() == eFilterLinear);
        param->appendOption(kParamFilterOptionLinear, kParamFilterOptionLinearHint);
        //assert(param->getNOptions() == eFilterBox);
        //param->appendOption(kParamFilterOptionBox, kParamFilterOptionBoxHint);
        param->setDefault((int)kParamFilterDefault);
        if (page) {
            page->addChild(*param);
        }
    }
}

/** @brief The create instance function, the plugin must return an object derived from the \ref OFX::ImageEffect class */
ImageEffect* SlitScanPluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new SlitScanPlugin(handle);
}

void getSlitScanPluginID(OFX::PluginFactoryArray &ids)
{
#ifdef DEBUG
    static SlitScanPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
#endif
}

