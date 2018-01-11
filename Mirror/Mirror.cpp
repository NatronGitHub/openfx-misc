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
 * OFX Mirror plugin.
 */

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
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        if (flip) {
            assert(_srcBounds.y1 <= ( _yoff - (procWindow.y2 - 1) ) && (_yoff - procWindow.y1) < _srcBounds.y2);
        } else {
            assert(_srcBounds.y1 <= procWindow.y1 && procWindow.y2 <= _srcBounds.y2);
        }
        if (flop) {
            assert(_srcBounds.x1 <= ( _xoff - (procWindow.x2 - 1) ) && (_xoff - procWindow.x1) < _srcBounds.x2);
        } else {
            assert(_srcBounds.x1 <= procWindow.x1 && procWindow.x2 <= _srcBounds.x2);
        }

        int srcx1 = flop ? (_xoff - procWindow.x1) : procWindow.x1;

        for (int dsty = procWindow.y1; dsty < procWindow.y2; ++dsty) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, dsty);
            assert(dstPix);

            int srcy = flip ? (_yoff - dsty) : dsty;

            assert( !( (srcy < _srcBounds.y1) || (_srcBounds.y2 <= srcy) || (_srcBounds.y2 <= _srcBounds.y1) ) );

            const PIX *srcPix = (const PIX *) getSrcPixelAddress(srcx1, srcy);
            assert(srcPix);
            if (flop) {
                for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents, srcPix -= nComponents) {
                    std::copy(srcPix, srcPix + nComponents, dstPix);
                }
            } else {
                std::memcpy( dstPix, srcPix, sizeof(PIX) * nComponents * (procWindow.x2 - procWindow.x1) );
            }
        }
    }

    int _xoff, _yoff;
};


template<class PIX, int nComponents, bool flip, bool flop>
void
mirrorPixelsForDepthAndComponentsFlipFlop(ImageEffect &instance,
                                          const OfxRectI & renderWindow,
                                          const PIX *srcPixelData,
                                          const OfxRectI & srcBounds,
                                          PixelComponentEnum srcPixelComponents,
                                          int srcPixelComponentCount,
                                          BitDepthEnum srcBitDepth,
                                          int srcRowBytes,
                                          PIX *dstPixelData,
                                          const OfxRectI & dstBounds,
                                          PixelComponentEnum dstPixelComponents,
                                          int dstPixelComponentCount,
                                          BitDepthEnum dstBitDepth,
                                          int dstRowBytes,
                                          int xoff,
                                          int yoff)
{
    (void)srcPixelComponents;
    (void)srcBitDepth;
    (void)dstPixelComponents;
    (void)dstBitDepth;

    PixelMirrorer<PIX, nComponents, flip, flop> processor(instance, xoff, yoff);
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

template<class PIX, int nComponents>
void
mirrorPixelsForDepthAndComponents(ImageEffect &instance,
                                  const OfxRectI & renderWindow,
                                  const PIX *srcPixelData,
                                  const OfxRectI & srcBounds,
                                  PixelComponentEnum srcPixelComponents,
                                  int srcPixelComponentCount,
                                  BitDepthEnum srcBitDepth,
                                  int srcRowBytes,
                                  PIX *dstPixelData,
                                  const OfxRectI & dstBounds,
                                  PixelComponentEnum dstPixelComponents,
                                  int dstPixelComponentCount,
                                  BitDepthEnum dstBitDepth,
                                  int dstRowBytes,
                                  bool flip,
                                  bool flop,
                                  int xoff,
                                  int yoff)
{
    assert(srcPixelData && dstPixelData);
    if (flip) {
        assert(srcBounds.y1 <= (yoff + 1 - renderWindow.y2) && renderWindow.y1 <= renderWindow.y2 && (yoff + 1 - renderWindow.y1) <= srcBounds.y2);
    } else {
        assert(srcBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= srcBounds.y2);
    }
    if (flop) {
        assert(srcBounds.x1 <= (xoff + 1 - renderWindow.x2) && renderWindow.x1 <= renderWindow.x2 && (xoff + 1 - renderWindow.x1) <= srcBounds.x2);
    } else {
        assert(srcBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= srcBounds.x2);
    }
    assert(srcPixelComponents == dstPixelComponents && srcBitDepth == dstBitDepth);
    assert(srcPixelComponentCount == dstPixelComponentCount && srcPixelComponentCount == nComponents);
    (void)srcPixelComponents;
    (void)srcBitDepth;
    (void)dstPixelComponents;
    (void)dstBitDepth;
    (void)srcPixelComponentCount;
    (void)dstPixelComponentCount;

    if (flip) {
        if (flop) {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, true, true>(instance, renderWindow,
                                                                                    (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                    (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                    xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, true, false>(instance, renderWindow,
                                                                                     (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                     (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                     xoff, yoff);
        }
    } else {
        if (flop) {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, false, true>(instance, renderWindow,
                                                                                     (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                     (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                     xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX, nComponents, false, false>(instance, renderWindow,
                                                                                      (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                      (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                      xoff, yoff);
        }
    }
}

template<class PIX>
void
mirrorPixelsForDepth(ImageEffect &instance,
                     const OfxRectI & renderWindow,
                     const void *srcPixelData,
                     const OfxRectI & srcBounds,
                     PixelComponentEnum srcPixelComponents,
                     int srcPixelComponentCount,
                     BitDepthEnum srcBitDepth,
                     int srcRowBytes,
                     void *dstPixelData,
                     const OfxRectI & dstBounds,
                     PixelComponentEnum dstPixelComponents,
                     int dstPixelComponentCount,
                     BitDepthEnum dstBitDepth,
                     int dstRowBytes,
                     bool flip,
                     bool flop,
                     int xoff,
                     int yoff)
{
    assert(srcPixelData && dstPixelData);
    assert(srcPixelComponents == dstPixelComponents && srcBitDepth == dstBitDepth);
    assert(srcPixelComponentCount == dstPixelComponentCount);
    // do the rendering
    if ( (dstPixelComponentCount < 0) || (4 < dstPixelComponentCount) ) {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstPixelComponentCount == 4) {
        mirrorPixelsForDepthAndComponents<PIX, 4>(instance, renderWindow,
                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 3) {
        mirrorPixelsForDepthAndComponents<PIX, 3>(instance, renderWindow,
                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 2) {
        mirrorPixelsForDepthAndComponents<PIX, 2>(instance, renderWindow,
                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    }  else if (dstPixelComponentCount == 1) {
        mirrorPixelsForDepthAndComponents<PIX, 1>(instance, renderWindow,
                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } // switch
}

inline void
mirrorPixels(ImageEffect &instance,
             const OfxRectI & renderWindow,
             const void *srcPixelData,
             const OfxRectI & srcBounds,
             PixelComponentEnum srcPixelComponents,
             int srcPixelComponentCount,
             BitDepthEnum srcBitDepth,
             int srcRowBytes,
             void *dstPixelData,
             const OfxRectI & dstBounds,
             PixelComponentEnum dstPixelComponents,
             int dstPixelComponentCount,
             BitDepthEnum dstBitDepth,
             int dstRowBytes,
             bool flip,
             bool flop,
             int xoff,
             int yoff)
{
    assert(dstPixelData);
    assert(srcPixelData);
    assert(srcPixelComponents == dstPixelComponents && srcBitDepth == dstBitDepth);
    assert(srcPixelComponentCount == dstPixelComponentCount);
    // do the rendering
    if ( (dstBitDepth != eBitDepthUByte) && (dstBitDepth != eBitDepthUShort) && (dstBitDepth != eBitDepthHalf) && (dstBitDepth != eBitDepthFloat) ) {
        throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstBitDepth == eBitDepthUByte) {
        mirrorPixelsForDepth<unsigned char>(instance, renderWindow,
                                            srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                            dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if ( (dstBitDepth == eBitDepthUShort) || (dstBitDepth == eBitDepthHalf) ) {
        mirrorPixelsForDepth<unsigned short>(instance, renderWindow,
                                             srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                             dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstBitDepth == eBitDepthFloat) {
        mirrorPixelsForDepth<float>(instance, renderWindow,
                                    srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                    dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
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
        , _srcClipChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);

        _flip = fetchBooleanParam(kParamMirrorFlip);
        _flop = fetchBooleanParam(kParamMirrorFlop);
        assert(_flip && _flop);
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
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _flip;
    BooleanParam* _flop;
    BooleanParam* _srcClipChanged; // set to true the first time the user connects src
};

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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    void* dstPixelData;
    OfxRectI dstBounds;
    PixelComponentEnum dstComponents;
    BitDepthEnum dstBitDepth;
    int dstRowBytes;
    getImageData(dst.get(), &dstPixelData, &dstBounds, &dstComponents, &dstBitDepth, &dstRowBytes);
    int dstPixelComponentCount = dst->getPixelComponentCount();
    auto_ptr<const Image> src(_srcClip->isConnected() ?
                                   _srcClip->fetchImage(args.time) : 0);
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
    } else {
        setPersistentMessage(Message::eMessageError, "", "Failed to fetch source image");
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    const void* srcPixelData;
    OfxRectI srcBounds;
    PixelComponentEnum srcPixelComponents;
    BitDepthEnum srcBitDepth;
    int srcRowBytes;
    getImageData(src.get(), &srcPixelData, &srcBounds, &srcPixelComponents, &srcBitDepth, &srcRowBytes);
    int srcPixelComponentCount = src->getPixelComponentCount();
    const double time = args.time;
    bool flip;
    bool flop;
    _flip->getValueAtTime(time, flip);
    _flop->getValueAtTime(time, flop);

    int xoff = 0;
    int yoff = 0;
    OfxRectI srcRoD = {0, 0, 0, 0};
    OfxRectD srcRoDCanonical = _srcClip->getRegionOfDefinition(time);
    assert( !Coords::rectIsEmpty(srcRoDCanonical) );
    Coords::toPixelEnclosing(srcRoDCanonical, args.renderScale, _srcClip->getPixelAspectRatio(), &srcRoD);
    if ( !Coords::rectIsEmpty(srcRoD) ) {
        if (flop) {
            xoff = srcRoD.x1 + srcRoD.x2 - 1;
        }
        if (flip) {
            yoff = srcRoD.y1 + srcRoD.y2 - 1;
        }
    }

    const OfxRectI& renderWindow = args.renderWindow;
    // these things should never happens
    if ( !src.get() ||
         ( flip  &&
           !( ( srcBounds.y1 <= (yoff + 1 - renderWindow.y2) ) && ( renderWindow.y1 <= renderWindow.y2) && ( (yoff + 1 - renderWindow.y1) <= srcBounds.y2 ) ) ) ||
         ( !flip &&
           !( ( srcBounds.y1 <= renderWindow.y1) && ( renderWindow.y1 <= renderWindow.y2) && ( renderWindow.y2 <= srcBounds.y2) ) ) ||
         ( flop  &&
           !( ( srcBounds.x1 <= (xoff + 1 - renderWindow.x2) ) && ( renderWindow.x1 <= renderWindow.x2) && ( (xoff + 1 - renderWindow.x1) <= srcBounds.x2 ) ) ) ||
         ( !flop &&
           !( ( srcBounds.x1 <= renderWindow.x1) && ( renderWindow.x1 <= renderWindow.x2) && ( renderWindow.x2 <= srcBounds.x2) ) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave source image with wrong dimensions");
        throwSuiteStatusException(kOfxStatFailed);
    }
    mirrorPixels(*this, args.renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, dstPixelData, dstBounds, dstComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
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
    OfxRectD srcRod = _srcClip->getRegionOfDefinition(time);
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
