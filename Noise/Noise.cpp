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
 * OFX Noise plugin.
 */

#include "Noise.h"

#include <limits>
#include <cmath>
#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

//#define USE_RANDOMGENERATOR // randomGenerator is more than 10 times slower than our pseudo-random hash
#ifdef USE_RANDOMGENERATOR
#include "randomGenerator.H"
#else
#ifdef _WINDOWS
#define uint32_t unsigned int
#else
#include <stdint.h> // for uint32_t
#endif
#endif

#define kPluginName "NoiseOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription "Generate noise."
#define kPluginIdentifier "net.sf.openfx.Noise"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamNoiseLevel "noise"
#define kParamNoiseLevelLabel "Noise"
#define kParamNoiseLevelHint "How much noise to make."

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different noise."

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
// base class for the noise

/** @brief  Base class used to blend two images together */
class NoiseGeneratorBase : public OFX::ImageProcessor
{
protected:
    float       _noiseLevel;   // noise amplitude
    float       _mean;   // mean value
    uint32_t    _seed;    // base seed
public:
    /** @brief no arg ctor */
    NoiseGeneratorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _noiseLevel(0.5f)
    , _mean(0.5f)
    , _seed(0)
    {
    }

    /** @brief set the scale */
    void setNoiseLevel(float v) {_noiseLevel = v;}

    /** @brief set the offset */
    void setNoiseMean(float v) {_mean = v;}

    /** @brief the seed to use */
    void setSeed(uint32_t v) {_seed = v;}
};

static unsigned int hash(unsigned int a)
{
    a = (a ^ 61) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2d;
    a = a ^ (a >> 15);
    return a;
}

/** @brief templated class to blend between two images */
template <class PIX, int nComponents, int max>
class NoiseGenerator : public NoiseGeneratorBase
{
public:
    // ctor
    NoiseGenerator(OFX::ImageEffect &instance)
    : NoiseGeneratorBase(instance)
    {}

    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float noiseLevel = _noiseLevel;

        // set up a random number generator and set the seed
#ifdef USE_RANDOMGENERATOR
        RandomGenerator randy;
#endif

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // for a given x,y position, the output should always be the same.
#ifdef USE_RANDOMGENERATOR
                randy.reseed(hash(x + 0x10000*_seed) + y);
#endif
                for (int c = 0; c < nComponents; c++) {
                    // get the random value out of it, scale up by the pixel max level and the noise level
#ifdef USE_RANDOMGENERATOR
                    double randValue = _mean + max * noiseLevel * (randy.random()-0.5);
#else
                    double randValue = _mean + max * noiseLevel * (hash(hash(hash(_seed^x)^y)^c)/((double)0x100000000ULL) - 0.5);
#endif

                    if (max == 1) // implies floating point, so don't clamp
                        dstPix[c] = PIX(randValue);
                    else {  // integer base one, clamp it
                        dstPix[c] = randValue < 0 ? 0 : (randValue > max ? max : PIX(randValue));
                    }
                }
                dstPix += nComponents;
            }
        }
    }

};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class NoisePlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_srcClip;
    OFX::Clip *_dstClip;

    OFX::DoubleParam  *_noise;
    OFX::IntParam  *_seed;

public:
    /** @brief ctor */
    NoisePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _srcClip(0)
    , _dstClip(0)
    , _noise(0)
    , _seed(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                            _dstClip->getPixelComponents() == ePixelComponentAlpha));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha)));
        _noise   = fetchDoubleParam(kParamNoiseLevel);
        _seed   = fetchIntParam(kParamSeed);
        assert(_noise && _seed);
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(NoiseGeneratorBase &, const OFX::RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
NoisePlugin::setupAndProcess(NoiseGeneratorBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image>  dst(_dstClip->fetchImage(args.time));
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

    // set the images
    processor.setDstImg(dst.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the scales
    // noise level depends on the render scale
    // (the following formula is for Gaussian noise only, but we use it as an approximation)
    double noise = _noise->getValueAtTime(args.time);
    processor.setNoiseLevel((float)(noise * std::sqrt(args.renderScale.x)));
    processor.setNoiseMean((float)(noise / 2.));

    // set the seed based on the current time, and double it we get difference seeds on different fields
    processor.setSeed(hash((unsigned)(args.time)^_seed->getValueAtTime(args.time)));

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

/* Override the clip preferences, we need to say we are setting the frame varying flag */
void
NoisePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    clipPreferences.setOutputFrameVarying(true);
}

// the overridden render function
void
NoisePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                NoiseGenerator<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort: {
                NoiseGenerator<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat: {
                NoiseGenerator<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                NoiseGenerator<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort: {
                NoiseGenerator<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat: {
                NoiseGenerator<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

mDeclarePluginFactory(NoisePluginFactory, {}, {});

using namespace OFX;

void NoisePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void NoisePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, ContextEnum /*context*/)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    // noise
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamNoiseLevel);
        param->setLabel(kParamNoiseLevelLabel);
        param->setHint(kParamNoiseLevelHint);
        param->setDefault(0.2);
        param->setRange(0, 10);
        param->setIncrement(0.1);
        param->setDisplayRange(0, 1);
        param->setAnimates(true); // can animate
        param->setDoubleType(eDoubleTypeScale);
        if (page) {
            page->addChild(*param);
        }
    }

    // seed
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamSeed);
        param->setLabel(kParamSeed);
        param->setHint(kParamSeedHint);
        param->setDefault(2000);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }
}

ImageEffect* NoisePluginFactory::createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/)
{
    return new NoisePlugin(handle);
}

void getNoisePluginID(OFX::PluginFactoryArray &ids)
{
    static NoisePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
