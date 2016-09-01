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
 * OFX Posterize plugin.
 */

#include <cmath>
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

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

#define kPluginName "PosterizeOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Reduce the number of color levels per channel, creating abrupt changes."
#define kPluginIdentifier "net.sf.openfx.Posterize"
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

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamColors "colors"
#define kParamColorsLabel "Colors"
#define kParamColorsHint "Number of color levels to use per channel."
#define kParamColorsDefault 16
#define kParamColorsMin 2
#define kParamColorsMax 256

// a great resource about dithering: http://bisqwit.iki.fi/story/howto/dither/jy/
#define kParamDither "dither"
#define kParamDitherLabel "Dither"
#define kParamDitherHint "Dithering method to apply in order to avoid the banding effect."
#define kParamDitherOptionNone "None"
#define kParamDitherOptionNoneHint "No dithering."
#define kParamDitherOptionOrderedBayer2 "Ordered (Bayer 2x2)"
#define kParamDitherOptionOrderedBayer2Hint "Ordered dithering using a 2x2 Bayer matrix."
#define kParamDitherOptionOrderedBayer4 "Ordered (Bayer 4x4)"
#define kParamDitherOptionOrderedBayer4Hint "Ordered dithering using a 4x4 Bayer matrix."
#define kParamDitherOptionOrderedBayer8 "Ordered (Bayer 8x8)"
#define kParamDitherOptionOrderedBayer8Hint "Ordered dithering using a 8x8 Bayer matrix."
#define kParamDitherOptionOrderedVoidAndCluster "Ordered (void-and-cluster)"
#define kParamDitherOptionOrderedVoidAndClusterHint "Ordered dithering using a void-and-cluster matrix."
#define kParamDitherOptionRandom "Random"
#define kParamDitherOptionRandomHint "Random dithering."
enum DitherEnum {
    eDitherNone = 0,
    eDitherOrderedBayer2,
    eDitherOrderedBayer4,
    eDitherOrderedBayer8,
    eDitherRandom,
};

#define kParamSeed "seed"
#define kParamSeedLabel "Seed"
#define kParamSeedHint "Random seed: change this if you want different instances to have different dithering (only for random dithering)."


static const int bayer88[8][8] = {
    { 0, 32, 8, 40, 2, 34, 10, 42},
    {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38},
    {60, 28, 52, 20, 62, 30, 54, 22},
    { 3, 35, 11, 43, 1, 33, 9, 41},
    {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37},
    {63, 31, 55, 23, 61, 29, 53, 21} };

static const int bayer44[4][4] = {
    {5,   9,   6,   10},
    {13,   1,   14,   2},
    {7,   11,   4,   8},
    {15,   3,   12,   0} };

static const int bayer22[2][2] = {
    {1, 2},
    {3, 0} };

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

using namespace OFX;

class PosterizeProcessorBase
    : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB, _processA;
    double _colors;
    DitherEnum _dither;
    uint32_t _seed;       // base seed

public:
    PosterizeProcessorBase(OFX::ImageEffect &instance,
                                const OFX::RenderArguments & /*args*/)
        : OFX::ImageProcessor(instance)
        , _srcImg(0)
        , _maskImg(0)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        , _processA(false)
        , _colors(kParamColorsDefault)
        , _dither(eDitherNone)
        , _seed(0)
    {
    }

    void setSrcImg(const OFX::Image *v) {_srcImg = v; }

    void setMaskImg(const OFX::Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   double colors,
                   DitherEnum dither,
                   uint32_t seed)
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _colors = colors;
        _dither = dither;
        _seed = seed;
    }
};


template <class PIX, int nComponents, int maxValue>
class PosterizeProcessor
    : public PosterizeProcessorBase
{
public:
    PosterizeProcessor(OFX::ImageEffect &instance,
                            const OFX::RenderArguments &args)
        : PosterizeProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:


    template<bool processR, bool processG, bool processB, bool processA>
    void process(OfxRectI procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        // set up a random number generator and set the seed
#ifdef USE_RANDOMGENERATOR
        RandomGenerator randy;
#endif
        double randValue;
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                // for a given x,y position, the output should always be the same.
                if (_dither == eDitherRandom) {
#                 ifdef USE_RANDOMGENERATOR
                    randy.reseed(hash(x + 0x10000 * _seed) + y);
                    randValue = randy.random();
#                 else
                    randValue = hash(hash(hash(_seed ^ x) ^ y) ^ nComponents) / ( (double)0x100000000ULL );
#                 endif
                }

                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);

                switch (_dither) {
                    case eDitherNone: {
                        // process the pixel (the actual computation goes here)
                        for (int c = 0; c < 4; ++c) {

                            float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                            // no dithering (identical tu Nuke's Posterize)
                            //tmpPix[c] = std::floor(unpPix[c] * _colors) / (_colors - 1.); // ok except when unpPix[c] * _colors is a positive integer
                            tmpPix[c] = rounded / (_colors - 1.);
                        }
                        break;
                    }
                    case eDitherOrderedBayer2: {
                        // 2x2 Bayer
                        int subx = x % 2;
                        if (subx < 0) {
                            subx += 2;
                        }
                        int suby = y % 2;
                        if (suby < 0) {
                            suby += 2;
                        }
                        int dith = bayer22[subx][suby];
                        for (int c = 0; c < 4; ++c) {

                            float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                            float v = unpPix[c] * (_colors - 1.) + 1./8.; // ok for integer _colors
                            float fv = (rounded <= v) ? rounded : (rounded - 1.);
                            assert( (v - fv) >= 0 );
                            assert( (v - fv) < 1 );
                            if ( (v - fv) * 4. <= (dith + 1) ) {
                                tmpPix[c] = fv / (_colors - 1.);
                            } else {
                                tmpPix[c] = (fv + 1) / (_colors - 1.);
                            }
                        }
                        break;
                    }
                    case eDitherOrderedBayer4: {
                        // 4x4 Bayer
                        int subx = x % 4;
                        if (subx < 0) {
                            subx += 4;
                        }
                        int suby = y % 4;
                        if (suby < 0) {
                            suby += 4;
                        }
                        int dith = bayer44[subx][suby];
                        for (int c = 0; c < 4; ++c) {
                            float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                            float v = unpPix[c] * (_colors - 1.) + 1./32.; // ok for integer _colors
                            float fv = (rounded <= v) ? rounded : (rounded - 1.);
                            assert( (v - fv) >= 0 );
                            assert( (v - fv) < 1 );
                            if ( (v - fv) * 16. <= (dith + 1) ) {
                                tmpPix[c] = fv / (_colors - 1.);
                            } else {
                                tmpPix[c] = (fv + 1) / (_colors - 1.);
                            }
                        }
                        break;
                    }
                    case eDitherOrderedBayer8: {
                        // 8x8 Bayer
                        int subx = x % 8;
                        if (subx < 0) {
                            subx += 8;
                        }
                        int suby = y % 8;
                        if (suby < 0) {
                            suby += 8;
                        }
                        int dith = bayer88[subx][suby];
                        for (int c = 0; c < 4; ++c) {

                            float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                            float v = unpPix[c] * (_colors - 1.) + 1./128.; // ok for integer _colors
                            float fv = (rounded <= v) ? rounded : (rounded - 1.);
                            assert( (v - fv) >= 0 );
                            assert( (v - fv) < 1 );
                            if ( (v - fv) * 64. <= (dith + 1) ) {
                                tmpPix[c] = fv / (_colors - 1.);
                            } else {
                                tmpPix[c] = (fv + 1) / (_colors - 1.);
                            }
                        }
                        break;
                    }
                    case eDitherRandom: {
                        for (int c = 0; c < 4; ++c) {
#                         ifdef USE_RANDOMGENERATOR
                            randValue = randy.random();
#                         else
                            randValue = hash(hash(hash(_seed ^ x) ^ y) ^ c) / ( (double)0x100000000ULL );
#                         endif

                            float rounded = (unpPix[c] <= 0) ? std::floor(unpPix[c] * _colors) : std::ceil(unpPix[c] * _colors - 1.);
                            float v = unpPix[c] * (_colors - 1.); // ok for integer _colors
                            float fv = (rounded <= v) ? rounded : (rounded - 1.);
                            assert( (v - fv) >= 0 );
                            assert( (v - fv) < 1 );
                            if ( (v - fv) <= randValue ) {
                                tmpPix[c] = fv / (_colors - 1.);
                            } else {
                                tmpPix[c] = (fv + 1) / (_colors - 1.);
                            }
                        }
                        break;
                    }
                }
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class PosterizePlugin
    : public OFX::ImageEffect
{
public:

    /** @brief ctor */
    PosterizePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _maskClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _colors(0)
        , _premult(0)
        , _premultChannel(0)
        , _mix(0)
        , _maskApply(0)
        , _maskInvert(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == OFX::eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // TODO: fetch noise parameters

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = paramExists(kParamMaskApply) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);

        _colors = fetchDoubleParam(kParamColors);
        _dither = fetchChoiceParam(kParamDither);
        _seed   = fetchIntParam(kParamSeed);
        assert(_colors && _dither && _seed);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(PosterizeProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _colors;
    ChoiceParam* _dither;
    IntParam* _seed;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskApply;
    OFX::BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
PosterizePlugin::setupAndProcess(PosterizeProcessorBase &processor,
                                      const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );

    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                         _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    std::auto_ptr<const OFX::Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    double colors = _colors->getValueAtTime(time);
    DitherEnum dither = (DitherEnum)_dither->getValueAtTime(time);
    float time_f = args.time;
    uint32_t seed = *( (uint32_t*)&time_f );

    // set the seed based on the current time, and double it we get difference seeds on different fields
    seed = hash( seed ^ _seed->getValueAtTime(args.time) );

    processor.setValues(premult, premultChannel, mix,
                        processR, processG, processB, processA, colors, dither, seed);
    processor.process();
} // PosterizePlugin::setupAndProcess


// the overridden render function
void
PosterizePlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == OFX::ePixelComponentRGBA || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentXY || dstComponents == OFX::ePixelComponentAlpha);
    // do the rendering
    switch (dstComponents) {
    case OFX::ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case OFX::ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    case OFX::ePixelComponentXY:
        renderForComponents<2>(args);
        break;
    case OFX::ePixelComponentAlpha:
        renderForComponents<1>(args);
        break;
    default:
        //std::cout << "components usupported\n";
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
PosterizePlugin::renderForComponents(const OFX::RenderArguments &args)
{
    OFX::BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case OFX::eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case OFX::eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case OFX::eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        //std::cout << "depth usupported\n";
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
PosterizePlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    PosterizeProcessor<PIX, nComponents, maxValue> fred(*this, args);
    setupAndProcess(fred, args);
}

bool
PosterizePlugin::isIdentity(const IsIdentityArguments &args,
                                 Clip * &identityClip,
                                 double & /*identityTime*/)
{
    //std::cout << "isIdentity!\n";
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        _processA->getValueAtTime(time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    // TODO: which plugin parameter values give identity?
    //if (...) {
    //    identityClip = _srcClip;
    //    //std::cout << "isIdentity! true\n";
    //    return true;
    //}

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            OFX::Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !OFX::Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // PosterizePlugin::isIdentity

void
PosterizePlugin::changedClip(const InstanceChangedArgs &args,
                                  const std::string &clipName)
{
    //std::cout << "changedClip!\n";
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _srcClip && (args.reason == OFX::eChangeUserEdit) ) {
        switch ( _srcClip->getPreMultiplication() ) {
        case eImageOpaque:
            _premult->setValue(false);
            break;
        case eImagePreMultiplied:
            _premult->setValue(true);
            break;
        case eImageUnPreMultiplied:
            _premult->setValue(false);
            break;
        }
    }
    //std::cout << "changedClip OK!\n";
}

mDeclarePluginFactory(PosterizePluginFactory, {}, {});
void
PosterizePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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
    desc.setChannelSelector(OFX::ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
PosterizePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                               OFX::ContextEnum context)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }


    // describe plugin params
    {
        OFX::DoubleParamDescriptor* param = desc.defineDoubleParam(kParamColors);
        param->setLabel(kParamColorsLabel);
        param->setHint(kParamColorsHint);
        param->setRange(0, DBL_MAX);
        param->setDisplayRange(kParamColorsMin, kParamColorsMax);
        param->setDefault(kParamColorsDefault);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDither);
        param->setLabel(kParamDitherLabel);
        param->setHint(kParamDitherHint);
        assert(param->getNOptions() == eDitherNone);
        param->appendOption(kParamDitherOptionNone, kParamDitherOptionNoneHint);
        assert(param->getNOptions() == eDitherOrderedBayer2);
        param->appendOption(kParamDitherOptionOrderedBayer2, kParamDitherOptionOrderedBayer2Hint);
        assert(param->getNOptions() == eDitherOrderedBayer4);
        param->appendOption(kParamDitherOptionOrderedBayer4, kParamDitherOptionOrderedBayer4Hint);
        assert(param->getNOptions() == eDitherOrderedBayer8);
        param->appendOption(kParamDitherOptionOrderedBayer8, kParamDitherOptionOrderedBayer8Hint);
        assert(param->getNOptions() == eDitherRandom);
        param->appendOption(kParamDitherOptionRandom, kParamDitherOptionRandomHint);
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

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInContext! OK\n";
} // PosterizePluginFactory::describeInContext

OFX::ImageEffect*
PosterizePluginFactory::createInstance(OfxImageEffectHandle handle,
                                            OFX::ContextEnum /*context*/)
{
    return new PosterizePlugin(handle);
}

static PosterizePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
