/*
 OFX Mirror plugin.
 
 Copyright (C) 2015 INRIA
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.
 
 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.
 
 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France
 
 
 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.
 
 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England
 
 */

#include "Mirror.h"

#include <cstring> // for memcpy
#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsPixelProcessor.h"
#include "ofxsMerging.h"
#include "ofxsMacros.h"

#define kPluginMirrorName "MirrorOFX"
#define kPluginMirrorGrouping "Transform"
#define kPluginMirrorDescription "Flip (vertical mirror) or flop (horizontal mirror) an image. Interlaced video can not be flipped."
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

using namespace OFX;


template <class PIX, int nComponents, bool flip, bool flop>
class PixelMirrorer
    : public OFX::PixelProcessorFilterBase
{
public:
    // ctor
    PixelMirrorer(OFX::ImageEffect &instance, int xoff, int yoff)
    : OFX::PixelProcessorFilterBase(instance)
    , _xoff(xoff)
    , _yoff(yoff)
    {
    }

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_srcBounds.x1 < _srcBounds.x2 && _srcBounds.y1 < _srcBounds.y2); // image should be non-empty

        if (flip) {
            assert(_srcBounds.y1 <= (_yoff - procWindow.y2) && (_yoff - procWindow.y1) <= _srcBounds.y2);
        } else {
            assert(_srcBounds.y1 <= procWindow.y1 && procWindow.y2 <= _srcBounds.y2);
        }
        if (flop) {
            assert(_srcBounds.x1 <= (_xoff - procWindow.x2) && (_xoff - procWindow.x1) <= _srcBounds.x2);
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

            assert(!((srcy < _srcBounds.y1) || (_srcBounds.y2 <= srcy) || (_srcBounds.y2 <= _srcBounds.y1)));

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


template<class PIX,int nComponents,bool flip, bool flop>
void
mirrorPixelsForDepthAndComponentsFlipFlop(OFX::ImageEffect &instance,
                                          const OfxRectI & renderWindow,
                                          const PIX *srcPixelData,
                                          const OfxRectI & srcBounds,
                                          OFX::PixelComponentEnum srcPixelComponents,
                                          int srcPixelComponentCount,
                                          OFX::BitDepthEnum srcBitDepth,
                                          int srcRowBytes,
                                          PIX *dstPixelData,
                                          const OfxRectI & dstBounds,
                                          OFX::PixelComponentEnum dstPixelComponents,
                                          int dstPixelComponentCount,
                                          OFX::BitDepthEnum dstBitDepth,
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
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents,dstPixelComponentCount, dstBitDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, 0);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

template<class PIX,int nComponents>
void
mirrorPixelsForDepthAndComponents(OFX::ImageEffect &instance,
                                  const OfxRectI & renderWindow,
                                  const PIX *srcPixelData,
                                  const OfxRectI & srcBounds,
                                  OFX::PixelComponentEnum srcPixelComponents,
                                  int srcPixelComponentCount,
                                  OFX::BitDepthEnum srcBitDepth,
                                  int srcRowBytes,
                                  PIX *dstPixelData,
                                  const OfxRectI & dstBounds,
                                  OFX::PixelComponentEnum dstPixelComponents,
                                  int dstPixelComponentCount,
                                  OFX::BitDepthEnum dstBitDepth,
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
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX,nComponents,true,true>(instance, renderWindow,
                                                                                 (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                 (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                 xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX,nComponents,true,false>(instance, renderWindow,
                                                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                  xoff, yoff);
        }
    } else {
        if (flop) {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX,nComponents,false,true>(instance, renderWindow,
                                                                                  (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                  (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                  xoff, yoff);
        } else {
            mirrorPixelsForDepthAndComponentsFlipFlop<PIX,nComponents,false,false>(instance, renderWindow,
                                                                                   (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                                                   (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes,
                                                                                   xoff, yoff);
        }
    }
}

template<class PIX>
void
mirrorPixelsForDepth(OFX::ImageEffect &instance,
                     const OfxRectI & renderWindow,
                     const void *srcPixelData,
                     const OfxRectI & srcBounds,
                     OFX::PixelComponentEnum srcPixelComponents,
                     int srcPixelComponentCount,
                     OFX::BitDepthEnum srcBitDepth,
                     int srcRowBytes,
                     void *dstPixelData,
                     const OfxRectI & dstBounds,
                     OFX::PixelComponentEnum dstPixelComponents,
                     int dstPixelComponentCount,
                     OFX::BitDepthEnum dstBitDepth,
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
    if (dstPixelComponentCount < 0 || 4 < dstPixelComponentCount) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstPixelComponentCount == 4) {
        mirrorPixelsForDepthAndComponents<PIX,4>(instance, renderWindow,
                                                 (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                 (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 3) {
        mirrorPixelsForDepthAndComponents<PIX,3>(instance, renderWindow,
                                                 (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                 (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstPixelComponentCount == 2) {
        mirrorPixelsForDepthAndComponents<PIX,2>(instance, renderWindow,
                                                 (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                 (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    }  else if (dstPixelComponentCount == 1) {
        mirrorPixelsForDepthAndComponents<PIX,1>(instance, renderWindow,
                                                 (const PIX*)srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                                 (PIX *)dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } // switch
}

inline void
mirrorPixels(OFX::ImageEffect &instance,
             const OfxRectI & renderWindow,
             const void *srcPixelData,
             const OfxRectI & srcBounds,
             OFX::PixelComponentEnum srcPixelComponents,
             int srcPixelComponentCount,
             OFX::BitDepthEnum srcBitDepth,
             int srcRowBytes,
             void *dstPixelData,
             const OfxRectI & dstBounds,
             OFX::PixelComponentEnum dstPixelComponents,
             int dstPixelComponentCount,
             OFX::BitDepthEnum dstBitDepth,
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
    if (dstBitDepth != OFX::eBitDepthUByte && dstBitDepth != OFX::eBitDepthUShort && dstBitDepth != OFX::eBitDepthHalf && dstBitDepth != OFX::eBitDepthFloat) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }
    if (dstBitDepth == OFX::eBitDepthUByte) {
        mirrorPixelsForDepth<unsigned char>(instance, renderWindow,
                                          srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                          dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstBitDepth == OFX::eBitDepthUShort || dstBitDepth == OFX::eBitDepthHalf) {
        mirrorPixelsForDepth<unsigned short>(instance, renderWindow,
                                           srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                           dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } else if (dstBitDepth == OFX::eBitDepthFloat) {
        mirrorPixelsForDepth<float>(instance, renderWindow,
                                  srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes,
                                  dstPixelData, dstBounds, dstPixelComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
    } // switch
}


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MirrorPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    MirrorPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _flip(0)
    , _flop(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);

        _flip = fetchBooleanParam(kParamMirrorFlip);
        _flop = fetchBooleanParam(kParamMirrorFlop);
        assert(_flip && _flop);
        _flip->setEnabled(_srcClip->getFieldOrder() == eFieldNone);
}

private:
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    BooleanParam* _flip;
    BooleanParam* _flop;
};

// the overridden render function
void
MirrorPlugin::render(const OFX::RenderArguments &args)
{
    assert (_srcClip && _dstClip);
    if (!_srcClip || !_dstClip) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    assert(kSupportsMultipleClipPARs   || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

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
    void* dstPixelData;
    OfxRectI dstBounds;
    OFX::PixelComponentEnum dstComponents;
    OFX::BitDepthEnum dstBitDepth;
    int dstRowBytes;
    getImageData(dst.get(), &dstPixelData, &dstBounds, &dstComponents, &dstBitDepth, &dstRowBytes);
    int dstPixelComponentCount = dst->getPixelComponentCount();

    std::auto_ptr<const OFX::Image> src(_srcClip->isConnected() ?
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
    const void* srcPixelData;
    OfxRectI srcBounds;
    OFX::PixelComponentEnum srcPixelComponents;
    OFX::BitDepthEnum srcBitDepth;
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
    OfxRectI srcRoD;
    OFX::MergeImages2D::toPixelEnclosing(_srcClip->getRegionOfDefinition(time), args.renderScale, _srcClip->getPixelAspectRatio(), &srcRoD);
    if (flop) {
        xoff = srcRoD.x1 + srcRoD.x2 - 1;
    }
    if (flip) {
        yoff = srcRoD.y1 + srcRoD.y2 - 1;
    }

    const OfxRectI& renderWindow = args.renderWindow;
    // these things should never happens
    if (!src.get() ||
        (flip  &&
         !(srcBounds.y1 <= (yoff + 1 - renderWindow.y2) && renderWindow.y1 <= renderWindow.y2 && (yoff + 1 - renderWindow.y1) <= srcBounds.y2)) ||
        (!flip &&
         !(srcBounds.y1 <= renderWindow.y1 && renderWindow.y1 <= renderWindow.y2 && renderWindow.y2 <= srcBounds.y2)) ||
        (flop  &&
         !(srcBounds.x1 <= (xoff + 1 - renderWindow.x2) && renderWindow.x1 <= renderWindow.x2 && (xoff + 1 - renderWindow.x1) <= srcBounds.x2)) ||
        (!flop &&
         !(srcBounds.x1 <= renderWindow.x1 && renderWindow.x1 <= renderWindow.x2 && renderWindow.x2 <= srcBounds.x2))) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave source image with wrong dimensions");
        throwSuiteStatusException(kOfxStatFailed);
    }
    mirrorPixels(*this, args.renderWindow, srcPixelData, srcBounds, srcPixelComponents, srcPixelComponentCount, srcBitDepth, srcRowBytes, dstPixelData, dstBounds, dstComponents, dstPixelComponentCount, dstBitDepth, dstRowBytes, flip, flop, xoff, yoff);
}

// override the roi call
void
MirrorPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
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
MirrorPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
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
MirrorPlugin::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName && _srcClip && args.reason == OFX::eChangeUserEdit) {
        _flip->setEnabled(_srcClip->getFieldOrder() == eFieldNone);
    }
}


mDeclarePluginFactory(MirrorPluginFactory, {}, {});

void MirrorPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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

void MirrorPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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
        if (page) {
            page->addChild(*param);
        }
    }
    // flop
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMirrorFlop);
        param->setLabel(kParamMirrorFlopLabel);
        param->setHint(kParamMirrorFlopHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect*
MirrorPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new MirrorPlugin(handle);
}

void getMirrorPluginID(OFX::PluginFactoryArray &ids)
{
    {
        static MirrorPluginFactory p(kPluginMirrorIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
