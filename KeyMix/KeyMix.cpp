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
    "KeyMix combines two images, A and B, using a third input, the Mask, to choose between them. A typical use is to put a keyed foreground over a background, with the keyer's output as the Mask. Note that the output alpha is a mix of the A and B alphas, not the Mask itself.\n" \
    "\n" \
    "It copies the pixel from A to B only where the Mask is non-zero. It is the same as the Matte operation, but alpha for input A is taken from an external mask, and the output alpha is mixed between A and B. The output bounding box is the union of A and B.\n" \
    "\n" \
    "KeyMix can also apply two successive color corrections through a single mask: correct the image twice, then use KeyMix with the uncorrected image as B and the corrected one as A. Masking each color correction separately with the same mask would apply the mask twice at soft edges, which gives artifacts.\n" \
    "\n" \
    "See also: https://web.archive.org/web/20220120144948/opticalenquiry.com/nuke/index.php?title=KeyMix"

#define kPluginIdentifier "net.sf.openfx.KeyMix"
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
// History:
// 1.0: Original version
// 2.0: With Alpha-only images, the A checkbox (instead of the R checkbox) controls the alpha channel
// 1.1 and 2.0 (2.0 is unreleased, fixed in place): in the filter context, the B input is the mandated Source
//      clip (it had no Source clip), and Source is not remapped there (Output has the components of Source);
//      the general context is unchanged (issue #217)

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
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha channel."
#endif

#define kClipA "A"
#define kClipAHint "The image sequence to mix with input B."
#define kClipB "B"
#define kClipBHint "The main input. This input is passed through when the KeyMix node is disabled."
// in the filter context, B is the mandated Source clip (ofxImageEffectContexts.rst, The Filter Context)
#define kClipBName(context) ( (context) == eContextFilter ? kOfxImageEffectSimpleSourceClipName : kClipB )


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
    bool _alphaOnlyUsesR; // version 1: with Alpha-only images, the R checkbox controls alpha

public:

    KeyMixProcessorBase(ImageEffect &instance,
                        bool alphaOnlyUsesR)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _maskImg(NULL)
        , _mix(1.)
        , _maskInvert(false)
        , _aChannels()
        , _alphaOnlyUsesR(alphaOnlyUsesR)
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
    KeyMixProcessor(ImageEffect &instance,
                    bool alphaOnlyUsesR)
        : KeyMixProcessorBase(instance, alphaOnlyUsesR)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        // With Alpha-only images, the only component is alpha, so it is controlled by the A checkbox.
        // Version 1 used the R checkbox for it, which is kept so that old projects render the same.
        const bool alphaFromA = (nComponents == 1) && !_alphaOnlyUsesR;
        float tmpPix[4] = {0.f, 0.f, 0.f, 0.f};

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
                        // all images are supposed to be black and transparent outside o
                        tmpPix[c] = (_aChannels[alphaFromA ? 3 : c] && srcPixA) ? srcPixA[c] : 0.f;
                    }
                } else {
                    // everything is black and transparent
                    for (int c = 0; c < 4; ++c) {
                        tmpPix[c] = 0;
                    }
                }

                // tmpPix has 4 components, but we only need the first nComponents

                // ofxsMaskMixPix takes denormalized input
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, true, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy unprocessed channels from B
                for (int c = 0; c < nComponents; ++c) {
                    if (!_aChannels[alphaFromA ? 3 : c]) {
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
    KeyMixPlugin(OfxImageEffectHandle handle,
                 unsigned int majorVersion)
        : ImageEffect(handle)
        , _alphaOnlyUsesR(majorVersion < 2)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
        , _maskClip(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClipA = fetchClip(kClipA);
        assert( _srcClipA && (!_srcClipA->isConnected() || _srcClipA->getPixelComponents() == ePixelComponentRGB || _srcClipA->getPixelComponents() == ePixelComponentRGBA || _srcClipA->getPixelComponents() == ePixelComponentAlpha) );


        _srcClipB = fetchClip( kClipBName( getContext() ) );
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

    bool _alphaOnlyUsesR; // version 1: with Alpha-only images, the R checkbox controls alpha
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
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(time) : 0 );
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(time) : 0 );

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
    processor.setRenderWindow(args.renderWindow, args.renderScale);

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
    KeyMixProcessor<PIX, nComponents, maxValue> fred(*this, _alphaOnlyUsesR);

    setupAndProcess(fred, args);
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
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args);
    }
}

void
KeyMixPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    if (getContext() == eContextFilter) {
        // "In all contexts, except for the general context, mandated input clips cannot have their
        // component types remapped, nor can the output" (ofxClipPreferences.rst), and in the filter
        // context the Output "will always be the same as the Source clip" (ofxImageEffectContexts.rst):
        // remap only A, to the components of Source (B)
        const PixelComponentEnum srcComps = _srcClipB->getUnmappedPixelComponents();
        if ( (srcComps == ePixelComponentRGBA) || (srcComps == ePixelComponentRGB) ||
             (srcComps == ePixelComponentXY) || (srcComps == ePixelComponentAlpha) ) {
            outputComps = srcComps;
        } else {
            outputComps = ePixelComponentRGBA;
        }
        clipPreferences.setClipComponents(*_srcClipA, outputComps);
    } else {
        clipPreferences.setClipComponents(*_srcClipA, outputComps);
        clipPreferences.setClipComponents(*_srcClipB, outputComps);
    }
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
    bool maskRoDValid = ofxsMaskGetRoDPixel(_maskClip, _maskApply, _maskInvert, time, args.renderScale, &maskRoD);
    // effect is identity if the renderWindow doesn't intersect the mask RoD
    if ( maskRoDValid && !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
        identityClip = _srcClipB;

        return true;
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

mDeclarePluginFactoryVersioned(KeyMixPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
KeyMixPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }

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

template<unsigned int majorVersion>
void
KeyMixPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                       ContextEnum context)
{
    // the filter context mandates an input clip named Source: it is B there
    ClipDescriptor* srcClipB = desc.defineClip( kClipBName(context) );

    srcClipB->setHint(kClipBHint);
    srcClipB->addSupportedComponent( ePixelComponentRGBA );
    srcClipB->addSupportedComponent( ePixelComponentRGB );
    srcClipB->addSupportedComponent( ePixelComponentXY );
    srcClipB->addSupportedComponent( ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);

    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    // (the mandated Source clip of the filter context is not optional)
    srcClipB->setOptional(context != eContextFilter); // B clip is optional

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
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
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

template<unsigned int majorVersion>
ImageEffect*
KeyMixPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                    ContextEnum /*context*/)
{
    return new KeyMixPlugin(handle, majorVersion);
}

static KeyMixPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

// Version 1: with Alpha-only images, the R checkbox controls the alpha channel
static KeyMixPluginFactory<1> pv1(kPluginIdentifier, 1); // 1.1: filter context fix (see History)
mRegisterPluginFactoryInstance(pv1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
