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
 * OFX TimeBlur plugin.
 */

#include <cmath> // for floor
#include <climits> // for INT_MAX
#include <cassert>
#include <algorithm>
#ifdef DEBUG
#include <cstdio>
#endif

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsPixelProcessor.h"
#include "ofxsCoords.h"
#include "ofxsShutter.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TimeBlurOFX"
#define kPluginGrouping "Time"
#define kPluginDescription \
    "Blend frames of the input clip over the shutter range."

#define kPluginDescriptionNuke \
" Note that this effect does not work correctly in Nuke, because frames cannot be fetched at fractional times."

#define kPluginIdentifier "net.sf.openfx.TimeBlur"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamDivisions     "division"
#define kParamDivisionsLabel "Divisions"
#define kParamDivisionsHint  "Number of time samples along the shutter time. The first frame is always at the start of the shutter range, and the shutter range is divided by divisions. The frame corresponding to the end of the shutter range is not included. If divisions=4, Shutter=1, Shutter Offset=Centered, this leads to blending the frames at t-0.5, t-0.25, t, t+0.25."

#define kFrameChunk 4 // how many frames to process simultaneously

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

class TimeBlurProcessorBase
    : public PixelProcessor
{
protected:
    std::vector<const Image*> _srcImgs;
    float *_accumulatorData;
    int _divisions; // 0 for all passes except the last one

public:

    TimeBlurProcessorBase(ImageEffect &instance)
        : PixelProcessor(instance)
        , _srcImgs()
        , _accumulatorData(NULL)
        , _divisions(0)
    {
    }

    void setSrcImgs(const std::vector<const Image*> &v) {_srcImgs = v; }

    void setAccumulator(float *accumulatorData) {_accumulatorData = accumulatorData; }

    void setValues(int divisions)
    {
        _divisions = divisions;
    }

private:
};


template <class PIX, int nComponents, int maxValue>
class TimeBlurProcessor
    : public TimeBlurProcessorBase
{
public:
    TimeBlurProcessor(ImageEffect &instance)
        : TimeBlurProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        assert(1 <= nComponents && nComponents <= 4);
        assert(!_divisions || _dstPixelData);
        float tmpPix[nComponents];
        const float initVal = 0.;
        const bool lastPass = (_divisions != 0);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = lastPass ? (PIX *) getDstPixelAddress(procWindow.x1, y) : 0;
            assert(!lastPass || dstPix);
            if (lastPass && !dstPix) {
                // coverity[dead_error_line]
                continue;
            }

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                size_t renderPix = ( (size_t)(_renderWindow.x2 - _renderWindow.x1) * (y - _renderWindow.y1) +
                                     (x - _renderWindow.x1) );
                if (_accumulatorData) {
                    std::copy(&_accumulatorData[renderPix * nComponents], &_accumulatorData[renderPix * nComponents + nComponents], tmpPix);
                } else {
                    std::fill(tmpPix, tmpPix + nComponents, initVal);
                }
                // accumulate
                for (unsigned i = 0; i < _srcImgs.size(); ++i) {
                    const PIX *srcPixi = (const PIX *)  (_srcImgs[i] ? _srcImgs[i]->getPixelAddress(x, y) : 0);
                    if (srcPixi) {
                        for (int c = 0; c < nComponents; ++c) {
                            tmpPix[c] += srcPixi[c];
                        }
                    }
                }
                if (!lastPass) {
                    assert(_accumulatorData);
                    if (_accumulatorData) {
                        std::copy(tmpPix, tmpPix + nComponents, &_accumulatorData[renderPix * nComponents]);
                    }
                } else {
                    for (int c = 0; c < nComponents; ++c) {
                        float v = tmpPix[c] / _divisions;
                        dstPix[c] = ofxsClampIfInt<PIX, maxValue>(v, 0, maxValue);
                    }
                    // increment the dst pixel
                    dstPix += nComponents;
                }
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeBlurPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    TimeBlurPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _divisions(NULL)
        , _shutter(NULL)
        , _shutteroffset(NULL)
        , _shuttercustomoffset(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents()) ) ) );
        _divisions = fetchIntParam(kParamDivisions);
        _shutter = fetchDoubleParam(kParamShutter);
        _shutteroffset = fetchChoiceParam(kParamShutterOffset);
        _shuttercustomoffset = fetchDoubleParam(kParamShutterCustomOffset);
        assert(_divisions && _shutter && _shutteroffset && _shuttercustomoffset);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(TimeBlurProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

private:

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    IntParam* _divisions;
    DoubleParam* _shutter;
    ChoiceParam* _shutteroffset;
    DoubleParam* _shuttercustomoffset;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// Since we cannot hold a auto_ptr in the vector we must hold a raw pointer.
// To ensure that images are always freed even in case of exceptions, use a RAII class.
struct OptionalImagesHolder_RAII
{
    std::vector<const Image*> images;

    OptionalImagesHolder_RAII()
        : images()
    {
    }

    ~OptionalImagesHolder_RAII()
    {
        for (unsigned int i = 0; i < images.size(); ++i) {
            delete images[i];
        }
    }
};

/* set up and run a processor */
void
TimeBlurPlugin::setupAndProcess(TimeBlurProcessorBase &processor,
                                const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif

    // accumulator image
    auto_ptr<ImageMemory> accumulator;
    float *accumulatorData = NULL;

    // compute range
    double shutter = _shutter->getValueAtTime(time);
    ShutterOffsetEnum shutteroffset = (ShutterOffsetEnum)_shutteroffset->getValueAtTime(time);
    double shuttercustomoffset = _shuttercustomoffset->getValueAtTime(time);
    OfxRangeD range;
    shutterRange(time, shutter, shutteroffset, shuttercustomoffset, &range);
    int divisions = _divisions->getValueAtTime(time);
    double interval = divisions >= 1 ? (range.max - range.min) / divisions : 1.;
    const OfxRectI& renderWindow = args.renderWindow;
    size_t nPixels = (size_t)(renderWindow.y2 - renderWindow.y1) * (renderWindow.x2 - renderWindow.x1);

    // Main processing loop.
    // We process the frame range by chunks, to avoid using too much memory.
    //
    // Note that Nuke has a bug in TimeBlur when divisions=1:
    // -the RoD is the expected RoD from the beginning of the shutter time
    // - the image is always identity
    // We chose not to reproduce this bug: when divisions = 1 both the RoD
    // and the image correspond to the start of shutter time.

    int imin;
    int imax = 0;
    const int n = divisions;
    while (imax < n) {
        imin = imax;
        imax = (std::min)(imin + kFrameChunk, n);
        bool lastPass = (imax == n);

        if (!lastPass) {
            // Initialize accumulator image (always use float)
            if (!accumulatorData) {
                int dstNComponents = _dstClip->getPixelComponentCount();
                accumulator.reset( new ImageMemory(nPixels * dstNComponents * sizeof(float), this) );
                accumulatorData = (float*)accumulator->lock();
                std::fill(accumulatorData, accumulatorData + nPixels * dstNComponents, 0.);
            }
        }

        // fetch the source images
        OptionalImagesHolder_RAII srcImgs;
        for (int i = imin; i < imax; ++i) {
            if ( abort() ) {
                return;
            }
            const Image* src = _srcClip ? _srcClip->fetchImage(range.min + i * interval) : 0;
            //std::printf("TimeBlur: fetchimage(%g)\n", range.min + i * interval);
#         ifndef NDEBUG
            if (src) {
                checkBadRenderScaleOrField(src, args);
                BitDepthEnum srcBitDepth      = src->getPixelDepth();
                PixelComponentEnum srcComponents = src->getPixelComponents();
                if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
                    throwSuiteStatusException(kOfxStatErrImageFormat);
                }
            }
#         endif
            srcImgs.images.push_back(src);
        }

        // set the images
        if (lastPass) {
            processor.setDstImg( dst.get() );
        }
        processor.setSrcImgs(srcImgs.images);
        // set the render window
        processor.setRenderWindow(renderWindow, args.renderScale);
        processor.setAccumulator(accumulatorData);

        processor.setValues(lastPass ? divisions : 0);

        // Call the base class process member, this will call the derived templated process code
        processor.process();
    }
} // TimeBlurPlugin::setupAndProcess

// the overridden render function
void
TimeBlurPlugin::render(const RenderArguments &args)
{
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentAlpha) {
        renderForComponents<1>(args);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
#endif
    } else {
        assert(dstComponents == ePixelComponentRGB);
        renderForComponents<3>(args);
    }
}

template<int nComponents>
void
TimeBlurPlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
TimeBlurPlugin::renderForBitDepth(const RenderArguments &args)
{
    TimeBlurProcessor<PIX, nComponents, maxValue> fred(*this);
    setupAndProcess(fred, args);
}

bool
TimeBlurPlugin::isIdentity(const IsIdentityArguments &args,
                           Clip * &identityClip,
                           double &identityTime
                           , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;

    // compute range
    double shutter = 0.;

    _shutter->getValueAtTime(time, shutter);
    if (shutter != 0) {
        int divisions;
        _divisions->getValueAtTime(time, divisions);
        if (divisions > 1) {
            return false;
        }
    }
    ShutterOffsetEnum shutteroffset_i = (ShutterOffsetEnum)_shutteroffset->getValueAtTime(time);
    double shuttercustomoffset = _shuttercustomoffset->getValueAtTime(time);
    OfxRangeD range;
    shutterRange(time, shutter, (ShutterOffsetEnum)shutteroffset_i, shuttercustomoffset, &range);

    // Note that Nuke has a bug in TimeBlur when divisions=1:
    // -the RoD is the expected RoD from the beginning of the shutter time
    // - the image is always identity
    // We chose not to reproduce this bug: when divisions = 1 both the RoD
    // and the image correspond to the start of shutter time.
    //
    identityClip = _srcClip;
    identityTime = range.min;

    return true;
}

void
TimeBlurPlugin::getFramesNeeded(const FramesNeededArguments &args,
                                FramesNeededSetter &frames)
{
    const double time = args.time;
    // compute range
    double shutter = _shutter->getValueAtTime(time);
    ShutterOffsetEnum shutteroffset_i = (ShutterOffsetEnum)_shutteroffset->getValueAtTime(time);
    double shuttercustomoffset = _shuttercustomoffset->getValueAtTime(time);
    OfxRangeD range;

    // Note that Nuke has a bug in TimeBlur when divisions=1:
    // -the RoD is the expected RoD from the beginning of the shutter time
    // - the image is always identity
    // We chose not to reproduce this bug: when divisions = 1 both the RoD
    // and the image correspond to the start of shutter time.

    shutterRange(time, shutter, (ShutterOffsetEnum)shutteroffset_i, shuttercustomoffset, &range);
    int divisions = _divisions->getValueAtTime(time);

    if ( (shutter == 0) || (divisions <= 1) ) {
        range.max = range.min;
        frames.setFramesNeeded(*_srcClip, range);

        return;
    }

    //#define OFX_HOST_ACCEPTS_FRACTIONAL_FRAME_RANGES // works with Natron, but this is perhaps borderline with respect to OFX spec
    // Edit: Natron works better if you input the same range that what is going to be done in render.
#ifdef OFX_HOST_ACCEPTS_FRACTIONAL_FRAME_RANGES
    //std::printf("TimeBlur: range(%g,%g)\n", range.min, range.max);
    frames.setFramesNeeded(*_srcClip, range);
#else
    // return the exact list of frames rather than a frame range , so that they can be pre-rendered by the host.
    double interval = (range.max - range.min) / divisions;
    for (int i = 0; i < divisions; ++i) {
        double t = range.min + i * interval;
        OfxRangeD r = {t, t};
        //std::printf("TimeBlur: frames for t=%g range(%g,%g) %lx\n", args.time, r.min, r.max, *((unsigned long*)&t));
        frames.setFramesNeeded(*_srcClip, r);
    }
#endif
}

bool
TimeBlurPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    const double time = args.time;
    // compute range
    double shutter = _shutter->getValueAtTime(time);
    ShutterOffsetEnum shutteroffset = (ShutterOffsetEnum)_shutteroffset->getValueAtTime(time);
    double shuttercustomoffset = _shuttercustomoffset->getValueAtTime(time);
    OfxRangeD range;

    // Compute the RoD as the union of all fetched input's RoDs
    //
    // Note that Nuke has a bug in TimeBlur when divisions=1:
    // -the RoD is the expected RoD from the beginning of the shutter time
    // - the image is always identity
    // We chose not to reproduce this bug: when divisions = 1 both the RoD
    // and the image correspond to the start of shutter time.

    shutterRange(time, shutter, shutteroffset, shuttercustomoffset, &range);
    int divisions = _divisions->getValueAtTime(time);
    double interval = divisions > 1 ? (range.max - range.min) / divisions : 1.;

    rod = _srcClip->getRegionOfDefinition(range.min);

    for (int i = 1; i < divisions; ++i) {
        OfxRectD srcRoD = _srcClip->getRegionOfDefinition(range.min + i * interval);
        Coords::rectBoundingBox(srcRoD, rod, &rod);
    }

    return true;
}

mDeclarePluginFactory(TimeBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
TimeBlurPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    std::string description = kPluginDescription;
    if (getImageEffectHostDescription()->hostName == "uk.co.thefoundry.nuke") {
        description += kPluginDescriptionNuke;
    }
    desc.setPluginDescription(description);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

#ifdef OFX_EXTENSIONS_NATRON
    //desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

void
TimeBlurPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamDivisions);
        param->setLabel(kParamDivisionsLabel);
        param->setHint(kParamDivisionsHint);
        param->setDefault(10);
        param->setRange(1, INT_MAX);
        param->setDisplayRange(1, 10);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
    shutterDescribeInContext(desc, context, page);
}

ImageEffect*
TimeBlurPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new TimeBlurPlugin(handle);
}

static TimeBlurPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
