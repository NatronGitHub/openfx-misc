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
 * OFX Rand plugin (previously named NoiseOFX).
 */

#include <limits>
#include <cmath>
#include <cfloat> // DBL_MAX

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsGenerator.h"
#include "ofxsMacros.h"
#include "ofxsMaskMix.h"

//#define USE_RANDOMGENERATOR // randomGenerator is more than 10 times slower than our pseudo-random hash
#ifdef USE_RANDOMGENERATOR
#include "randomGenerator.H"
#else
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#define uint32_t unsigned int
#else
#include <stdint.h> // for uint32_t
#endif
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

// Note: this plugin was initially named NoiseOFX, but was renamed to Rand (like the Shake node)
#define kPluginName "Rand"
#define kPluginGrouping "Draw"
#define kPluginDescription "Generate a random field of noise. The field does not resample if you change the resolution or density (you can animate the density without pixels randomly changing)."
#define kPluginIdentifier "net.sf.openfx.Noise" // don't ever change the plugin ID
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamNoiseLevel "noise"
#define kParamNoiseLevelLabel "Noise"
#define kParamNoiseLevelHint "How much noise to make."

#define kParamNoiseDensity "density"
#define kParamNoiseDensityLabel "Density"
#define kParamNoiseDensityHint "The density from 0 to 1 of the pixels. A lower density mean fewer random pixels."

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different noise."

#define kParamStaticSeed "staticSeed"
#define kParamStaticSeedLabel "Static Seed"
#define kParamStaticSeedHint "When enabled, the seed is not combined with the frame number, and thus the effect is the same for all frames for a given seed number."


////////////////////////////////////////////////////////////////////////////////
// base class for the noise

/** @brief  Base class used to blend two images together */
class RandGeneratorBase
    : public ImageProcessor
{
protected:
    float _noiseLevel;         // noise amplitude
    double _density;
    float _mean;         // mean value
    uint32_t _seed;       // base seed

public:
    /** @brief no arg ctor */
    RandGeneratorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _noiseLevel(0.5f)
        , _density(1.)
        , _mean(0.5f)
        , _seed(0)
    {
    }

    /** @brief set the values */
    void setValues(float noiseLevel,
                   double density,
                   float mean,
                   uint32_t seed)
    {
        _noiseLevel = noiseLevel;
        _density = density;
        _mean = mean;
        _seed = seed;
    }
};

static unsigned int
hash(unsigned int a)
{
    a = (a ^ 61) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2d;
    a = a ^ (a >> 15);

    return a;
}

/** @brief templated class to blend between two images */
template <class PIX, int nComponents, int maxValue>
class RandGenerator
    : public RandGeneratorBase
{
public:
    // ctor
    RandGenerator(ImageEffect &instance)
        : RandGeneratorBase(instance)
    {}

    // and do some processing
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        float noiseLevel = _noiseLevel;

        // set up a random number generator and set the seed
#ifdef USE_RANDOMGENERATOR
        RandomGenerator randy;
#endif

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // for a given x,y position, the output should always be the same.
#ifdef USE_RANDOMGENERATOR
                randy.reseed(hash(x + 0x10000 * _seed) + y);
#endif
#ifdef USE_RANDOMGENERATOR
                double randValue = randy.random();
#else
                double randValue = hash(hash(hash(_seed ^ x) ^ y) ^ nComponents) / ( (double)0x100000000ULL );
#endif
                if (randValue <= _density) {
                    for (int c = 0; c < nComponents; c++) {
                        // get the random value out of it, scale up by the pixel max level and the noise level
#ifdef USE_RANDOMGENERATOR
                        randValue = randy.random() - 0.5;
#else
                        randValue = hash(hash(hash(_seed ^ x) ^ y) ^ c) / ( (double)0x100000000ULL ) - 0.5;
#endif
                        randValue = _mean + noiseLevel * randValue;
                        dstPix[c] = ofxsClampIfInt<PIX, maxValue>(randValue * maxValue, 0, maxValue);
                    }
                } else {
                    std::fill(dstPix, dstPix + nComponents, 0);
                }
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class RandPlugin
    : public GeneratorPlugin
{
private:
    // do not need to delete these, the ImageEffect is managing them for us
    DoubleParam  *_noise;
    DoubleParam  *_density;
    IntParam  *_seed;
    BooleanParam* _staticSeed;

public:
    /** @brief ctor */
    RandPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _noise(NULL)
        , _density(NULL)
        , _seed(NULL)
        , _staticSeed(NULL)
    {

        _noise   = fetchDoubleParam(kParamNoiseLevel);
        _density = fetchDoubleParam(kParamNoiseDensity);
        _seed   = fetchIntParam(kParamSeed);
        _staticSeed = fetchBooleanParam(kParamStaticSeed);
        assert(_noise && _density && _seed && _staticSeed);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* Override the clip preferences, we need to say we are setting the frame varying flag */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(RandGeneratorBase &, const RenderArguments &args);
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
RandPlugin::setupAndProcess(RandGeneratorBase &processor,
                            const RenderArguments &args)
{
    const double time = args.time;

    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(args.time) );

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

    // set the images
    processor.setDstImg( dst.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    double noise;
    _noise->getValueAtTime(time, noise);
    double density;
    _density->getValueAtTime(time, density);

    bool staticSeed = _staticSeed->getValueAtTime(time);
    uint32_t seed = hash( (unsigned int)_seed->getValueAtTime(time) );
    if (!staticSeed) {
        float time_f = static_cast<float>(args.time);

        // set the seed based on the current time, and double it we get difference seeds on different fields
        seed = hash( *( (uint32_t*)&time_f ) ^ seed );
    }
    // set the scales
    // noise level depends on the render scale
    // (the following formula is for Gaussian noise only, but we use it as an approximation)
    double densityRS = (std::min)( 1., density / (args.renderScale.x * args.renderScale.y) );
    float noiseLevel = (float)( noise * (density / densityRS) * std::sqrt(args.renderScale.x) );
    float mean = (float)(noise * (density / densityRS) / 2.);

    processor.setValues(noiseLevel, densityRS, mean, seed);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // RandPlugin::setupAndProcess

/* Override the clip preferences, we need to say we are setting the frame varying flag */
void
RandPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    GeneratorPlugin::getClipPreferences(clipPreferences);
    bool staticSeed = _staticSeed->getValue();
    if (!staticSeed) {
        clipPreferences.setOutputFrameVarying(true);
        clipPreferences.setOutputHasContinuousSamples(true);
    }
    clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
}

// the overridden render function
void
RandPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            RandGenerator<unsigned char, 4, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthUShort: {
            RandGenerator<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthFloat: {
            RandGenerator<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == ePixelComponentRGB) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            RandGenerator<unsigned char, 3, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthUShort: {
            RandGenerator<unsigned short, 3, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthFloat: {
            RandGenerator<float, 3, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            RandGenerator<unsigned char, 1, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthUShort: {
            RandGenerator<unsigned short, 1, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthFloat: {
            RandGenerator<float, 1, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // RandPlugin::render

mDeclarePluginFactory(RandPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
RandPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }
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

    generatorDescribe(desc);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
RandPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                     ContextEnum context)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGB, true, context);

    // noise
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamNoiseLevel);
        param->setLabel(kParamNoiseLevelLabel);
        param->setHint(kParamNoiseLevelHint);
        param->setDefault(1.);
        param->setIncrement(0.1);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(0, 1);
        param->setAnimates(true); // can animate
        param->setDoubleType(eDoubleTypeScale);
        if (page) {
            page->addChild(*param);
        }
    }

    // density
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamNoiseDensity);
        param->setLabel(kParamNoiseDensityLabel);
        param->setHint(kParamNoiseDensityHint);
        param->setDefault(1.);
        param->setIncrement(0.01);
        param->setRange(0., 1.);
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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamStaticSeed);
        param->setLabel(kParamStaticSeedLabel);
        param->setHint(kParamStaticSeedHint);
        param->setDefault(false);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
} // RandPluginFactory::describeInContext

ImageEffect*
RandPluginFactory::createInstance(OfxImageEffectHandle handle,
                                  ContextEnum /*context*/)
{
    return new RandPlugin(handle);
}

static RandPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
