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
 * OFX Difference plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsNonFinite.h" // finiteSample
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DifferenceOFX"
#define kPluginGrouping "Keyer"
#define kPluginDescription "Produce a rough matte from the difference of two input images.\n" \
    "A is the background without the subject (clean plate). B is the subject with the background. The output is RGBA: RGB is copied from B, and the difference of the RGB channels of A and B is output to alpha, after applying offset and gain.\n" \
    "See also: https://web.archive.org/web/20220524000628/http://www.opticalenquiry.com/nuke/index.php?title=The_Keyer_Nodes#Difference and https://web.archive.org/web/20211023071843/http://opticalenquiry.com/nuke/index.php?title=Keying_Tips"

#define kPluginIdentifier "net.sf.openfx.DifferencePlugin"
// history:
// 1.0 initial version
// 1.1 non-finite input samples give a finite output and a matte in [0,1] (issue #213): a NaN
//     sample is taken as 0, +-inf as +-FLT_MAX (inf - inf and 0 * inf gave NaN before the clamp
//     to [0,1], and the RGB copied from B was not finite). The output for finite inputs is unchanged.
//     The output is RGBA whatever the inputs (issue #275): with RGB inputs, the output was RGB and the
//     matte, computed from R and G only, overwrote blue (Y for XY inputs).
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 1 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "Value subtracted to each pixel of the output"
#define kParamGain "gain"
#define kParamGainLabel "Gain"
#define kParamGainHint "Multiply each pixel of the output by this value"

#define kClipA "A"
#define kClipAHint "The background without the subject (a clean plate)."
#define kClipB "B"
#define kClipBHint "The subject with the background."


template <class T>
inline T
Clamp(T v,
      int min,
      int max)
{
    if ( v < T(min) ) {
        return T(min);
    }
    if ( v > T(max) ) {
        return T(max);
    }

    return v;
}

class DifferencerBase
    : public ImageProcessor
{
protected:
    const Image *_srcImgA;
    const Image *_srcImgB;
    double _offset;
    double _gain;

public:
    DifferencerBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _offset(0.)
        , _gain(1.)
    {
    }

    void setSrcImg(const Image *A,
                   const Image *B) {_srcImgA = A; _srcImgB = B; }

    void setValues(double offset,
                   double gain)
    {
        _offset = offset;
        _gain = gain;
    }
};


// The images are RGBA (see getClipPreferences): RGB is copied from B, the matte goes to alpha.
template <class PIX, int maxValue>
class Differencer
    : public DifferencerBase
{
public:
    Differencer(ImageEffect &instance)
        : DifferencerBase(instance)
    {
    }

private:
    static const int nComponents = 4;

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPixA = (const PIX *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);

                // NaN rule (issue #213, see ofxsNonFinite.h): non-finite float samples are made finite
                // (NaN gives 0, +-inf gives +-FLT_MAX), so that diff is finite (in double), and
                // _gain * diff - _offset is finite or +-inf (an overflow, never NaN)
                if (srcPixA && srcPixB) {
                    double diff = 0.;
                    for (int c = 0; c < 3; ++c) {
                        const PIX b = finiteSample<PIX, maxValue>(srcPixB[c]);
                        dstPix[c] = b;
                        // normalize, so that offset and gain have the same meaning for all bit depths
                        double d = ( (double)b - (double)finiteSample<PIX, maxValue>(srcPixA[c]) ) / maxValue;
                        diff += d * d;
                    }
                    diff = _gain * diff - _offset; // this seems to be the formula used in Nuke
                    dstPix[3] = (PIX)( (std::max)( 0., (std::min)(diff, 1.) ) * maxValue );
                } else if (srcPixB && !srcPixA) {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = finiteSample<PIX, maxValue>(srcPixB[c]);
                    }
                } else {
                    for (int c = 0; c < nComponents; ++c) {
                        dstPix[c] = 0;
                    }
                }
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DifferencePlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    DifferencePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClipA = fetchClip(kClipA);
        assert( _srcClipA && (!_srcClipA->isConnected() || _srcClipA->getPixelComponents() == ePixelComponentRGB || _srcClipA->getPixelComponents() == ePixelComponentRGBA || _srcClipA->getPixelComponents() == ePixelComponentAlpha) );
        _srcClipB = fetchClip(kClipB);
        assert( _srcClipB && (!_srcClipB->isConnected() || _srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA || _srcClipB->getPixelComponents() == ePixelComponentAlpha) );
        _offset = fetchDoubleParam(kParamOffset);
        assert(_offset);
        _gain = fetchDoubleParam(kParamGain);
        assert(_gain);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(DifferencerBase &, const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClipA;
    Clip *_srcClipB;
    DoubleParam *_offset;
    DoubleParam *_gain;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

/* set up and run a processor */
void
DifferencePlugin::setupAndProcess(DifferencerBase &processor,
                                  const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(args.time) : 0 );
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(args.time) : 0 );
    if ( srcA.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(srcA, args);
# endif
        BitDepthEnum srcBitDepth      = srcA->getPixelDepth();
        PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( srcB.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(srcB, args);
# endif
        BitDepthEnum srcBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    double offset;
    double gain;
    _offset->getValueAtTime(args.time, offset);
    _gain->getValueAtTime(args.time, gain);
    processor.setValues(offset, gain);
    processor.setDstImg( dst.get() );
    processor.setSrcImg( srcA.get(), srcB.get() );
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    processor.process();
} // DifferencePlugin::setupAndProcess

// the internal render function
void
DifferencePlugin::renderInternal(const RenderArguments &args,
                                 BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        Differencer<unsigned char, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        Differencer<unsigned short, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        Differencer<float, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
DifferencePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering. The output is RGBA (see getClipPreferences).
    if (dstComponents != ePixelComponentRGBA) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave an output that is not RGBA");
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    renderInternal(args, dstBitDepth);
}

void
DifferencePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // The matte goes to alpha, so the output is RGBA whatever the inputs, and the inputs are mapped to
    // RGBA (an RGB input is opaque). Difference only has the general context, where "all input clips
    // may be remapped, as can the output clip" (ofxClipPreferences.rst, Specifying Pixel Components).
    clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
    clipPreferences.setClipComponents(*_srcClipA, ePixelComponentRGBA);
    clipPreferences.setClipComponents(*_srcClipB, ePixelComponentRGBA);
}

mDeclarePluginFactory(DifferencePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DifferencePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    //desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

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
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentAlpha);
#endif
}

void
DifferencePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
{
    ClipDescriptor* srcClipB = desc.defineClip(kClipB);

    srcClipB->setHint(kClipBHint);
    srcClipB->addSupportedComponent( ePixelComponentRGBA );
    srcClipB->addSupportedComponent( ePixelComponentRGB );
    srcClipB->addSupportedComponent( ePixelComponentXY );
    srcClipB->addSupportedComponent( ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);
    srcClipB->setOptional(false);

    ClipDescriptor* srcClipA = desc.defineClip(kClipA);
    srcClipA->setHint(kClipAHint);
    srcClipA->addSupportedComponent( ePixelComponentRGBA );
    srcClipA->addSupportedComponent( ePixelComponentRGB );
    srcClipA->addSupportedComponent( ePixelComponentXY );
    srcClipA->addSupportedComponent( ePixelComponentAlpha );
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(kSupportsTiles);
    srcClipA->setOptional(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // offset
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDefault(0.);
        param->setIncrement(0.005);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    // gain
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGain);
        param->setLabel(kParamGainLabel);
        param->setHint(kParamGainHint);
        param->setDefault(1.);
        param->setIncrement(0.005);
        param->setRange(-DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 1.);
        param->setDoubleType(eDoubleTypeScale);
        if (page) {
            page->addChild(*param);
        }
    }
} // DifferencePluginFactory::describeInContext

ImageEffect*
DifferencePluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new DifferencePlugin(handle);
}

static DifferencePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
