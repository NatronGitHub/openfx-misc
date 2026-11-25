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
 * OFX Mirror plugin.
 */

#include <algorithm>
#include <cstring> // for memcpy
#include <cmath>
#include <iostream>

#include "ofxsPixelProcessor.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginMirrorName "MirrorOFX"
#define kPluginMirrorGrouping "Transform"
#define kPluginMirrorDescription "Flip (vertical mirror) or flop (horizontal mirror) an image. Interlaced video can not be flipped.\n" \
    "This plugin does not concatenate transforms."
#define kPluginMirrorIdentifier "net.sf.openfx.Mirror"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.
// 1.1: (unreleased) the Mirror Around parameter: mirror around the input format (issue #301), or around the input
//      region of definition (the default, as in 1.0)

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamMirrorFlip "flip"
#define kParamMirrorFlipLabel "Vertical (flip)"
#define kParamMirrorFlipHint "Upside-down (swap top and bottom). Only possible if input is not interlaced."

#define kParamMirrorFlop "flop"
#define kParamMirrorFlopLabel "Horizontal (flop)"
#define kParamMirrorFlopHint "Mirror image (swap left and right)"

#define kParamMirrorAround "mirrorAround"
#define kParamMirrorAroundLabel "Mirror Around"
#define kParamMirrorAroundHint "The rectangle whose center the image is mirrored around."
#define kParamMirrorAroundOptionFormat "Format", "The format of the input: an element at the left of the frame goes to its right. The region of definition of the input if the host gives no format.", "format"
#define kParamMirrorAroundOptionRoD "Region of Definition", "The region of definition of the input (Mirror 1.0): an element alone in the input stays in place.", "rod"
enum MirrorAroundEnum
{
    eMirrorAroundFormat = 0,
    eMirrorAroundRoD,
};

#define kParamSrcClipChanged "sourceChanged"


template <class PIX, int nComponents, bool flip, bool flop>
class PixelMirrorer
    : public PixelProcessorFilterBase
{
public:
    // ctor
    PixelMirrorer(ImageEffect &instance,
                  int xoff,
                  int yoff)
        : PixelProcessorFilterBase(instance)
        , _xoff(xoff)
        , _yoff(yoff)
    {
    }

    // and do some processing
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        // the destination columns whose source pixel is in the source image: [vx1, vx2)
        // (the others, and the rows whose source row is outside it, are black and transparent:
        // with Mirror Around Format, the render window may reach one pixel beyond the mirrored
        // source bounds at a render scale where the format does not fall on whole pixels)
        int vx1 = flop ? (_xoff - _srcBounds.x2 + 1) : _srcBounds.x1;
        int vx2 = flop ? (_xoff - _srcBounds.x1 + 1) : _srcBounds.x2;
        vx1 = (std::max)(vx1, procWindow.x1);
        vx2 = (std::min)(vx2, procWindow.x2);
        const int rowLength = procWindow.x2 - procWindow.x1;

        for (int dsty = procWindow.y1; dsty < procWindow.y2; ++dsty) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, dsty);
            assert(dstPix);
            if (!dstPix) {
                continue;
            }

            const int srcy = flip ? (_yoff - dsty) : dsty;
            const PIX *srcPix = ( (srcy < _srcBounds.y1) || (_srcBounds.y2 <= srcy) || (vx2 <= vx1) ) ? NULL :
                                (const PIX *) getSrcPixelAddress(flop ? (_xoff - vx1) : vx1, srcy);
            if (!srcPix) {
                std::fill( dstPix, dstPix + nComponents * rowLength, PIX() );
                continue;
            }
            std::fill( dstPix, dstPix + nComponents * (vx1 - procWindow.x1), PIX() );
            dstPix += nComponents * (vx1 - procWindow.x1);
            if (flop) {
                for (int x = vx1; x < vx2; ++x, dstPix += nComponents, srcPix -= nComponents) {
                    std::copy(srcPix, srcPix + nComponents, dstPix);
                }
            } else {
                std::copy(srcPix, srcPix + nComponents * (vx2 - vx1), dstPix);
                dstPix += nComponents * (vx2 - vx1);
            }
            std::fill( dstPix, dstPix + nComponents * (procWindow.x2 - vx2), PIX() );
        }
    }

private:
    int _xoff, _yoff;
};


template<class PIX, int nComponents, bool flip, bool flop>
void
mirrorPixelsForDepthAndComponentsFlipFlop(ImageEffect &instance,
                                          const OfxRectI & renderWindow,
                                          const OfxPointD& renderScale,
                                          const Image* src,
                                          Image* dst,
                                          int xoff,
                                          int yoff)
{
    PixelMirrorer<PIX, nComponents, flip, flop> processor(instance, xoff, yoff);
    // set the images
    processor.setDstImg(dst);
    processor.setSrcImg(src, 0);

    // set the render window
    processor.setRenderWindow(renderWindow, renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

template<class PIX, int nComponents>
void
mirrorPixelsForDepthAndComponents(ImageEffect &instance,
                                  const OfxRectI & renderWindow,
                                  const OfxPointD& renderScale,
                                  const Image* src,
                                  Image* dst,
                                  bool flip,
                                  bool flop,
                                  int xoff,
                                  int yoff)
{
    assert(src->getPixelComponentCount() == dst->getPixelComponentCount() && src->getPixelComponentCount() == nComponents);

    if (flip) {
        if (flop) {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, true, true>(instance, renderWindow, renderScale, src, dst, xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, true, false>(instance, renderWindow, renderScale, src, dst, xoff, yoff);
        }
    } else {
        if (flop) {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, false, true>(instance, renderWindow, renderScale, src, dst, xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, false, false>(instance, renderWindow, renderScale, src, dst, xoff, yoff);
        }
    }
}

template<class PIX>
void
mirrorPixelsForDepth(ImageEffect &instance,
                     const OfxRectI & renderWindow,
                     const OfxPointD& renderScale,
                     const Image* src,
                     Image* dst,
                     bool flip,
                     bool flop,
                     int xoff,
                     int yoff)
{
    int dstPixelComponentCount = dst->getPixelComponentCount();

    // do the rendering
    if ( (dstPixelComponentCount < 0) || (4 < dstPixelComponentCount) ) {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstPixelComponentCount == 4) {
        mirrorPixelsForDepthAndComponents<PIX, 4>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 3) {
        mirrorPixelsForDepthAndComponents<PIX, 3>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 2) {
        mirrorPixelsForDepthAndComponents<PIX, 2>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    }  else if (dstPixelComponentCount == 1) {
        mirrorPixelsForDepthAndComponents<PIX, 1>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } // switch
}

inline void
mirrorPixels(ImageEffect &instance,
             const OfxRectI & renderWindow,
             const OfxPointD& renderScale,
             const Image* src,
             Image* dst,
             bool flip,
             bool flop,
             int xoff,
             int yoff)
{
    assert(src && dst);
    assert(src->getPixelComponents() == dst->getPixelComponents() && src->getPixelDepth() == dst->getPixelDepth());
    assert(src->getPixelComponentCount() == dst->getPixelComponentCount());
    assert(dst->getPixelData() && src->getPixelData());
    BitDepthEnum dstBitDepth = dst->getPixelDepth();

    // do the rendering
    if ( (dstBitDepth != eBitDepthUByte) && (dstBitDepth != eBitDepthUShort) && (dstBitDepth != eBitDepthHalf) && (dstBitDepth != eBitDepthFloat) ) {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstBitDepth == eBitDepthUByte) {
        mirrorPixelsForDepth<unsigned char>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } else if ( (dstBitDepth == eBitDepthUShort) || (dstBitDepth == eBitDepthHalf) ) {
        mirrorPixelsForDepth<unsigned short>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } else if (dstBitDepth == eBitDepthFloat) {
        mirrorPixelsForDepth<float>(instance, renderWindow, renderScale, src, dst, flip, flop, xoff, yoff);
    } // switch
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MirrorPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    MirrorPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _flip(NULL)
        , _flop(NULL)
        , _mirrorAround(NULL)
        , _srcClipChanged(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);

        _flip = fetchBooleanParam(kParamMirrorFlip);
        _flop = fetchBooleanParam(kParamMirrorFlop);
        _mirrorAround = fetchChoiceParam(kParamMirrorAround);
        assert(_flip && _flop && _mirrorAround);
        if (_srcClip) {
            _flip->setEnabled(_srcClip->getFieldOrder() == eFieldNone);
        }
        _srcClipChanged = fetchBooleanParam(kParamSrcClipChanged);
        assert(_srcClipChanged);
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    // The rectangle the image is mirrored in (around its center), in canonical coordinates: the input
    // format with Mirror Around Format (if the host gives one), else the input RoD.
    OfxRectD getMirrorRect(double time) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _flip;
    BooleanParam* _flop;
    ChoiceParam* _mirrorAround;
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
};

OfxRectD
MirrorPlugin::getMirrorRect(double time) const
{
    assert(_srcClip);
#ifdef OFX_EXTENSIONS_NATRON
    if (_mirrorAround->getValueAtTime(time) == eMirrorAroundFormat) {
        OfxRectI format = {0, 0, 0, 0};
        _srcClip->getFormat(format); // in pixels at render scale 1
        if ( !Coords::rectIsEmpty(format) ) {
            const double par = _srcClip->getPixelAspectRatio();
            const OfxRectD rect = {format.x1 * par, (double)format.y1, format.x2 * par, (double)format.y2};

            return rect;
        }
    }
#endif

    return _srcClip->getRegionOfDefinition(time);
}

// the overridden render function
void
MirrorPlugin::render(const RenderArguments &args)
{
    assert (_srcClip && _dstClip);
    if (!_srcClip || !_dstClip) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    assert( kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );

    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src(_srcClip->isConnected() ?
                                   _srcClip->fetchImage(args.time) : 0);
    if ( src.get() ) {
#     ifndef NDEBUG
        checkBadRenderScaleOrField(src, args);
#     endif
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( ( srcBitDepth != dst->getPixelDepth() ) || ( srcComponents != dst->getPixelComponents() ) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else {
        setPersistentMessage(Message::eMessageError, "", "Failed to fetch source image");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    const double time = args.time;
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);

    // source pixel x maps to xoff - x, y to yoff - y
    int xoff = 0;
    int yoff = 0;
    OfxRectI mirrorRect = {0, 0, 0, 0};
    const OfxRectD mirrorRectCanonical = getMirrorRect(time);
    assert( !Coords::rectIsEmpty(mirrorRectCanonical) );
    Coords::toPixelEnclosing(mirrorRectCanonical, args.renderScale, _srcClip->getPixelAspectRatio(), &mirrorRect);
    if ( !Coords::rectIsEmpty(mirrorRect) ) {
        if (flop) {
            xoff = mirrorRect.x1 + mirrorRect.x2 - 1;
        }
        if (flip) {
            yoff = mirrorRect.y1 + mirrorRect.y2 - 1;
        }
    }

    // the processor fills with black the pixels whose source is outside the source image
    mirrorPixels(*this, args.renderWindow, args.renderScale, src.get(), dst.get(), flip, flop, xoff, yoff);
} // MirrorPlugin::render

// override the roi call
void
MirrorPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                   RegionOfInterestSetter &rois)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    const double time = args.time;
    const OfxRectD srcRod = getMirrorRect(time);
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);
    OfxRectD roi;
    if (flop) {
        roi.x1 = srcRod.x1 + srcRod.x2 - args.regionOfInterest.x2;
        roi.x2 = srcRod.x1 + srcRod.x2 - args.regionOfInterest.x1;
    } else {
        roi.x1 = args.regionOfInterest.x1;
        roi.x2 = args.regionOfInterest.x2;
    }
    if (flip) {
        roi.y1 = srcRod.y1 + srcRod.y2 - args.regionOfInterest.y2;
        roi.y2 = srcRod.y1 + srcRod.y2 - args.regionOfInterest.y1;
    } else {
        roi.y1 = args.regionOfInterest.y1;
        roi.y2 = args.regionOfInterest.y2;
    }
    rois.setRegionOfInterest(*_srcClip, roi);
}

bool
MirrorPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    if ( !_srcClip || !_srcClip->isConnected() ||
         (_mirrorAround->getValueAtTime(args.time) == eMirrorAroundRoD) ) {
        return false; // the host default: the Source RoD, which mirrors into itself
    }
    const double time = args.time;
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);
    const OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
    if ( Coords::rectIsEmpty(srcRod) || (!flip && !flop) ) {
        return false;
    }
    // the Source RoD, mirrored in the rectangle
    const OfxRectD rect = getMirrorRect(time);
    rod = srcRod;
    if (flop) {
        rod.x1 = rect.x1 + rect.x2 - srcRod.x2;
        rod.x2 = rect.x1 + rect.x2 - srcRod.x1;
    }
    if (flip) {
        rod.y1 = rect.y1 + rect.y2 - srcRod.y2;
        rod.y2 = rect.y1 + rect.y2 - srcRod.y1;
    }

    return true;
}

// overridden is identity
bool
MirrorPlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double & /*identityTime*/
                         , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    bool flip;
    bool flop;

    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);

    if (!flip && !flop) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

void
MirrorPlugin::changedClip(const InstanceChangedArgs &args,
                          const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_srcClipChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        _flip->setEnabled(_srcClip->getFieldOrder() == eFieldNone);
        _srcClipChanged->setValue(true);
    }
}

mDeclarePluginFactory(MirrorPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
MirrorPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginMirrorName);
    desc.setPluginGrouping(kPluginMirrorGrouping);
    desc.setPluginDescription(kPluginMirrorDescription);

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
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
MirrorPluginFactory::describeInContext(ImageEffectDescriptor &desc,
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

    // flip
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMirrorFlip);
        param->setLabel(kParamMirrorFlipLabel);
        param->setHint(kParamMirrorFlipHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    // flop
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMirrorFlop);
        param->setLabel(kParamMirrorFlopLabel);
        param->setHint(kParamMirrorFlopHint);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // mirrorAround
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamMirrorAround);
        param->setLabel(kParamMirrorAroundLabel);
        param->setHint(kParamMirrorAroundHint);
        assert(param->getNOptions() == eMirrorAroundFormat);
        param->appendOption(kParamMirrorAroundOptionFormat);
        assert(param->getNOptions() == eMirrorAroundRoD);
        param->appendOption(kParamMirrorAroundOptionRoD);
        param->setDefault( (int)eMirrorAroundRoD );
        param->setAnimates(false);
#ifndef OFX_EXTENSIONS_NATRON
        param->setEnabled(false); // no input format: always the RoD
#endif
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSrcClipChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // MirrorPluginFactory::describeInContext

ImageEffect*
MirrorPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    return new MirrorPlugin(handle);
}

static MirrorPluginFactory p(kPluginMirrorIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
