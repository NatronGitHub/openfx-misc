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
 * OFX Merge plugin.
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include <bitset>
#ifdef DEBUG
#include <iostream>
#endif

#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "KeyMixOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
    "KeyMix takes two images and layers them together according to a third input. It can be used to lay a foreground over a background using the output of a keyer. The only disadvantage to this method is that it outputs an image with no alpha.\n" \
    "\n" \
    "It copies the pixel from A to B only where the Mask is non-zero. It is the same as the Matte operation, but alpha for input A is taken from an external mask, and the output alpha is mixed between A and B. The output bounding box is the union of A and B.\n" \
    "\n" \
    "As well as functioning as a layering node, it can also be used to integrate two color operations with one mask. This guards against 'recycled masks', where two consecutive color filters are masked using the same mask, which may generate strange artifacts.\n" \
    "\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=KeyMix"

#define kPluginIdentifier "net.sf.openfx.KeyMix"
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

#define kClipA "A"
#define kClipAHint "The image sequence to mix with input B."
#define kClipB "B"
#define kClipBHint "The main input. This input is passed through when the KeyMix node is disabled."


/*
   For explanations on why we use bitset instead of vector<bool>, see:

   D. Kalev. What You Should Know about vector<bool>.
   http://www.informit.com/guides/content.aspx?g=cplusplus&seqNum=98

   S. D. Meyers. Effective STL: 50 Specific Ways to Improve Your Use of the Standard Template Library.
   Item 18: "Avoid using vector<bool>".
   Professional Computing Series. Addison-Wesley, Boston, 4 edition, 2004

   V. Pieterse et al. Performance of C++ Bit-vector Implementations
   http://www.cs.up.ac.za/cs/vpieterse/pub/PieterseEtAl_SAICSIT2010.pdf
 */

class KeyMixProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImgA;
    const Image *_srcImgB;
    const Image *_maskImg;
    double _mix;
    bool _maskInvert;
    std::bitset<4> _aChannels;

public:

    KeyMixProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _maskImg(NULL)
        , _mix(1.)
        , _maskInvert(false)
        , _aChannels()
    {
    }

    void setSrcImg(const Image *A,
                   const Image *B)
    {
        _srcImgA = A;
        _srcImgB = B;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void setValues(double mix,
                   std::bitset<4> aChannels)
    {
        _mix = mix;
        assert(aChannels.size() == 4);
        _aChannels = aChannels;
    }
};


template <class PIX, int nComponents, int maxValue>
class KeyMixProcessor
    : public KeyMixProcessorBase
{
public:
    KeyMixProcessor(ImageEffect &instance)
        : KeyMixProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float tmpPix[4];
        for (int c = 0; c < 4; ++c) {
            tmpPix[c] = 0;
        }

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                const PIX *srcPixA = (const PIX *)  (_srcImgA ? _srcImgA->getPixelAddress(x, y) : 0);
                const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);


                if (srcPixA) {
                    for (std::size_t c = 0; c < nComponents; ++c) {
#                     ifdef DEBUG
                        // check for NaN
                        assert( !srcPixA || !OFX::IsNaN(srcPixA[c]) );
                        assert( !srcPixB || !OFX::IsNaN(srcPixB[c]) );
#                     endif
                        // all images are supposed to be black and transparent outside o
                        tmpPix[c] = (_aChannels[c] && srcPixA) ? srcPixA[c] : 0.f;
                    }
                    if (nComponents != 4) {
                        // set alpha (1 inside, 0 outside)
                        tmpPix[3] = (_aChannels[3] && srcPixA) ? 1. : 0.;
                    }
                } else {
                    // everything is black and transparent
                    for (int c = 0; c < 4; ++c) {
                        tmpPix[c] = 0;
                    }
                }

#             ifdef DEBUG
                // check for NaN
                for (int c = 0; c < 4; ++c) {
                    assert( !OFX::IsNaN(tmpPix[c]) );
                }
#             endif

                // tmpPix has 4 components, but we only need the first nComponents

                // ofxsMaskMixPix takes denormalized input
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, true, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy unprocessed channels from B
                for (int c = 0; c < nComponents; ++c) {
                    if (!_aChannels[c]) {
                        dstPix[c] = srcPixB ? srcPixB[c] : 0;
                    }
                }

                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class KeyMixPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    KeyMixPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
        , _maskClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClipA = fetchClip(kClipA);
        assert( _srcClipA && (!_srcClipA->isConnected() || _srcClipA->getPixelComponents() == ePixelComponentRGB || _srcClipA->getPixelComponents() == ePixelComponentRGBA || _srcClipA->getPixelComponents() == ePixelComponentAlpha) );


        _srcClipB = fetchClip(kClipB);
        assert( _srcClipB && (!_srcClipB->isConnected() || _srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA || _srcClipB->getPixelComponents() == ePixelComponentAlpha) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _aChannels[0] = fetchBooleanParam(kParamProcessR);
        _aChannels[1] = fetchBooleanParam(kParamProcessG);
        _aChannels[2] = fetchBooleanParam(kParamProcessB);
        _aChannels[3] = fetchBooleanParam(kParamProcessA);
        assert(_aChannels[0] && _aChannels[1] && _aChannels[2] && _aChannels[3]);
    }

private:
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(KeyMixProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClipA;
    Clip *_srcClipB;
    Clip *_maskClip;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _aChannels[4];
};


// override the rod call
bool
KeyMixPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    //Do the same as isIdentity otherwise the result of getRegionOfDefinition() might not be coherent with the RoD of the identity clip.
    if ( (mix == 0.) || !( _maskClip && _maskClip->isConnected() ) ) {
        if ( _srcClipB->isConnected() ) {
            OfxRectD rodB = _srcClipB->getRegionOfDefinition(time);
            rod = rodB;

            return true;
        }

        return false;
    }

    if ( _srcClipB->isConnected() ) {
        rod = _srcClipB->getRegionOfDefinition(time);
    } else {
        rod.x1 = rod.y1 = rod.x2 = rod.y2 = 0.;
    }
    if ( _srcClipA->isConnected() ) {
        Coords::rectBoundingBox(rod, _srcClipA->getRegionOfDefinition(time), &rod);
    }

    return true;
} // KeyMixPlugin::getRegionOfDefinition

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
KeyMixPlugin::setupAndProcess(KeyMixProcessorBase &processor,
                              const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(time) : 0 );
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(time) : 0 );

    if ( srcA.get() ) {
        if ( (srcA->getRenderScale().x != args.renderScale.x) ||
             ( srcA->getRenderScale().y != args.renderScale.y) ||
             ( ( srcA->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcA->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = srcA->getPixelDepth();
        PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( srcB.get() ) {
        if ( (srcB->getRenderScale().x != args.renderScale.x) ||
             ( srcB->getRenderScale().y != args.renderScale.y) ||
             ( ( srcB->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcB->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);

    // do we do masking
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    double mix = _mix->getValueAtTime(time);
    std::bitset<4> aChannels;
    for (std::size_t c = 0; c < 4; ++c) {
        aChannels[c] = _aChannels[c]->getValueAtTime(time);
    }
    processor.setValues(mix, aChannels);
    processor.setDstImg( dst.get() );
    processor.setSrcImg( srcA.get(), srcB.get() );
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // KeyMixPlugin::setupAndProcess

template<int nComponents>
void
KeyMixPlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte: {
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;
    }
    case eBitDepthUShort: {
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;
    }
    case eBitDepthFloat: {
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
KeyMixPlugin::renderForBitDepth(const RenderArguments &args)
{
    //const double time = args.time;
    auto_ptr<KeyMixProcessorBase> fred;

    fred.reset( new KeyMixProcessor<PIX, nComponents, maxValue>(*this) );

    assert( fred.get() );
    if ( fred.get() ) {
        setupAndProcess(*fred, args);
    }
} // KeyMixPlugin::renderForBitDepth

// the overridden render function
void
KeyMixPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args);
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args);
    }
}

void
KeyMixPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    clipPreferences.setClipComponents(*_srcClipA, outputComps);
    clipPreferences.setClipComponents(*_srcClipB, outputComps);
#ifdef OFX_EXTENSIONS_NATRON
    // the output format is the format of the B clip if it is connected
    if ( _srcClipB && _srcClipB->isConnected() ) {
        OfxRectI format;
        _srcClipB->getFormat(format);
        clipPreferences.setOutputFormat(format);
    }
#endif
}

bool
KeyMixPlugin::isIdentity(const IsIdentityArguments &args,
                         Clip * &identityClip,
                         double & /*identityTime*/
                         , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0.) {
        identityClip = _srcClipB;

        return true;
    }

    bool aChannels[4];
    for (int c = 0; c < 4; ++c) {
        aChannels[c] = _aChannels[c]->getValueAtTime(time);
    }
    if (!aChannels[0] && !aChannels[1] && !aChannels[2] && !aChannels[3]) {
        identityClip = _srcClipB;

        return true;
    }

    OfxRectI maskRoD;
    bool maskRoDValid = false;
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            maskRoDValid = true;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClipB;

                return true;
            }
        }
    }

    // The region of effect is only the set of the intersections between the A inputs and the mask.
    // If at least one of these regions intersects the renderwindow, the effect is not identity.

    if ( _srcClipA->isConnected() ) {
        OfxRectD srcARoD = _srcClipA->getRegionOfDefinition(time);
        if ( !Coords::rectIsEmpty(srcARoD) ) {
            OfxRectI srcARoDPixel;
            Coords::toPixelEnclosing(srcARoD, args.renderScale, _srcClipA->getPixelAspectRatio(), &srcARoDPixel);
            bool srcARoDValid = true;
            if (maskRoDValid) {
                // mask the srcARoD with the mask RoD. The result may be empty
                srcARoDValid = Coords::rectIntersection<OfxRectI>(srcARoDPixel, maskRoD, &srcARoDPixel);
            }
            if ( srcARoDValid && Coords::rectIntersection<OfxRectI>(args.renderWindow, srcARoDPixel, 0) ) {
                // renderWindow intersects one of the effect areas
                return false;
            }
        }
    }

    // renderWindow intersects no area where a "A" source is applied
    identityClip = _srcClipB;

    return true;
} // KeyMixPlugin::isIdentity

mDeclarePluginFactory(KeyMixPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
KeyMixPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
} // >::describe

void
KeyMixPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum context)
{
    ClipDescriptor* srcClipB = desc.defineClip(kClipB);

    srcClipB->setHint(kClipBHint);
    srcClipB->addSupportedComponent( ePixelComponentRGBA );
    srcClipB->addSupportedComponent( ePixelComponentRGB );
    srcClipB->addSupportedComponent( ePixelComponentXY );
    srcClipB->addSupportedComponent( ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);

    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    srcClipB->setOptional(true); // B clip is optional

    ClipDescriptor* srcClipA = desc.defineClip(kClipA);
    srcClipA->setHint(kClipAHint);
    srcClipA->addSupportedComponent( ePixelComponentRGBA );
    srcClipA->addSupportedComponent( ePixelComponentRGB );
    srcClipA->addSupportedComponent( ePixelComponentXY );
    srcClipA->addSupportedComponent( ePixelComponentAlpha );
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(kSupportsTiles);
    srcClipA->setOptional(true);

    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    srcClipA->setOptional(true);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

ImageEffect*
KeyMixPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    return new KeyMixPlugin(handle);
}

static KeyMixPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
