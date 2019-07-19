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
 * OFX Dissolve plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlenderMasked.h"
#include "ofxsMaskMix.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DissolveOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Weighted average of two inputs."
#define kPluginIdentifier "net.sf.openfx.DissolvePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamWhich "which"
#define kParamWhichLabel "Which"
#define kParamWhichHint "Mix factor between the inputs."

#define kClipSourceCount 64

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

static
std::string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    std::string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb = (char)( '0' + (j % 10) ) + nb;
    }

    return nb;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DissolvePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    DissolvePlugin(OfxImageEffectHandle handle,
                   bool numerousInputs)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(numerousInputs ? kClipSourceCount : 2)
        , _which(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        for (unsigned i = 0; i < _srcClip.size(); ++i) {
            if ( (getContext() == eContextTransition) && (i < 2) ) {
                _srcClip[i] = fetchClip(i == 0 ? kOfxImageEffectTransitionSourceFromClipName : kOfxImageEffectTransitionSourceToClipName);
            } else {
                _srcClip[i] = fetchClip( unsignedToString(i) );
            }
            assert( _srcClip[i] && (!_srcClip[i]->isConnected() || OFX_COMPONENTS_OK(_srcClip[i]->getPixelComponents())) );
        }

        _maskClip = fetchClip("Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _which = fetchDoubleParam(getContext() == eContextTransition ? kOfxImageEffectTransitionParamName : kParamWhich);
        assert(_which);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_maskInvert);

        // finally
        syncPrivateData();
    }

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        updateRange();
    }

    /* set up and run a processor */
    void setupAndProcess(ImageBlenderMaskedBase &, const RenderArguments &args);

private:

    void updateRange()
    {
        int maxconnected = 1;

        for (unsigned i = 2; i < _srcClip.size(); ++i) {
            if ( _srcClip[i]->isConnected() ) {
                maxconnected = i;
            }
        }
        _which->setDisplayRange(0, maxconnected);
    }

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    std::vector<Clip *> _srcClip;
    Clip *_maskClip;
    DoubleParam* _which;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

#ifndef NDEBUG
// make sure components are sane
static void
checkComponents(const Image &src,
                BitDepthEnum dstBitDepth,
                PixelComponentEnum dstComponents)
{
    BitDepthEnum srcBitDepth     = src.getPixelDepth();
    PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( ( srcBitDepth != dstBitDepth) || ( srcComponents != dstComponents) ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}
#endif

/* set up and run a processor */
void
DissolvePlugin::setupAndProcess(ImageBlenderMaskedBase &processor,
                                const RenderArguments &args)
{
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
    
    // get the transition value
    double which = (std::max)( 0., (std::min)(_which->getValueAtTime(args.time), (double)_srcClip.size() - 1) );
    int prev = std::floor(which);
    int next = std::ceil(which);

    if (prev == next) {
        auto_ptr<const Image> src( ( _srcClip[prev] && _srcClip[prev]->isConnected() ) ?
                                        _srcClip[prev]->fetchImage(args.time) : 0 );
#     ifndef NDEBUG
        if ( src.get() ) {
            checkBadRenderScaleOrField(src, args);
            BitDepthEnum srcBitDepth      = src->getPixelDepth();
            PixelComponentEnum srcComponents = src->getPixelComponents();
            if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
                throwSuiteStatusException(kOfxStatErrImageFormat);
            }
        }
#     endif
        copyPixels( *this, args.renderWindow, args.renderScale, src.get(), dst.get() );

        return;
    }

    // fetch the two source images
    auto_ptr<const Image> fromImg( ( _srcClip[prev] && _srcClip[prev]->isConnected() ) ?
                                        _srcClip[prev]->fetchImage(args.time) : 0 );
    auto_ptr<const Image> toImg( ( _srcClip[next] && _srcClip[next]->isConnected() ) ?
                                      _srcClip[next]->fetchImage(args.time) : 0 );

# ifndef NDEBUG
    // make sure bit depths are sane
    if ( fromImg.get() ) {
        checkBadRenderScaleOrField(fromImg, args);
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if ( toImg.get() ) {
        checkBadRenderScaleOrField(toImg, args);
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }
# endif

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if ( mask.get() ) {
        checkBadRenderScaleOrField(mask, args);
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }


    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // set the scales
    processor.setBlend(which - prev);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DissolvePlugin::setupAndProcess

// the overridden render function
void
DissolvePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        assert( kSupportsMultipleClipPARs   || _srcClip[i]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
        assert( kSupportsMultipleClipDepths || _srcClip[i]->getPixelDepth()       == _dstClip->getPixelDepth() );
    }
    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args);
#endif
    }  else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
DissolvePlugin::renderForComponents(const RenderArguments &args)
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
DissolvePlugin::renderForBitDepth(const RenderArguments &args)
{
    if ( (getContext() != eContextFilter) &&
         ( getContext() != eContextTransition) &&
         _maskClip && _maskClip->isConnected() ) {
        ImageBlenderMasked<PIX, nComponents, maxValue, true> fred(*this);
        setupAndProcess(fred, args);
    } else {
        ImageBlenderMasked<PIX, nComponents, maxValue, false> fred(*this);
        setupAndProcess(fred, args);
    }
}

// overridden is identity
bool
DissolvePlugin::isIdentity(const IsIdentityArguments &args,
                           Clip * &identityClip,
                           double &identityTime
                           , int& /*view*/, std::string& /*plane*/)
{
    // get the transition value
    double which = (std::max)( 0., (std::min)(_which->getValueAtTime(args.time), (double)_srcClip.size() - 1) );
    int prev = (int)which;

    //int next = (std::min)((int)which+1,(int)_srcClip.size()-1);

    identityTime = args.time;

    // at the start?
    if (which <= 0.0) {
        identityClip = _srcClip[0];
        identityTime = args.time;

        return true;
    }

    if ( ( ( which >= _srcClip.size() ) || (prev == which) ) &&
         ( !_maskClip || !_maskClip->isConnected() ) ) {
        identityClip = _srcClip[prev];
        identityTime = args.time;

        return true;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip[0];

                    return true;
                }
            }
        }
    }

    // nope, identity we isnt
    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DissolvePlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                     RegionOfInterestSetter &rois)
{
    double which = (std::max)( 0., (std::min)(_which->getValueAtTime(args.time), (double)_srcClip.size() - 1) );
    unsigned prev = (unsigned)std::floor(which);
    unsigned next = (unsigned)std::ceil(which);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if ( (i != prev) && (i != next) ) {
            rois.setRegionOfInterest(*_srcClip[i], emptyRoI);
        }
    }
}

bool
DissolvePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    // get the transition value
    double which = (std::max)( 0., (std::min)(_which->getValueAtTime(args.time), (double)_srcClip.size() - 1) );
    int prev = (int)which;
    int next = (std::min)( (int)which + 1, (int)_srcClip.size() - 1 );

    // at the start?
    if ( (which <= 0.0) && _srcClip[0] && _srcClip[0]->isConnected() ) {
        rod = _srcClip[0]->getRegionOfDefinition(args.time);

        return true;
    }

    // at the end?
    if ( ( ( which >= _srcClip.size() ) || (which == prev) ) &&
         _srcClip[prev] && _srcClip[prev]->isConnected() &&
         ( !_maskClip || !_maskClip->isConnected() ) ) {
        rod = _srcClip[prev]->getRegionOfDefinition(args.time);

        return true;
    }

    if ( _srcClip[prev] && _srcClip[prev]->isConnected() && _srcClip[next] && _srcClip[next]->isConnected() ) {
        OfxRectD fromRoD = _srcClip[prev]->getRegionOfDefinition(args.time);
        OfxRectD toRoD = _srcClip[next]->getRegionOfDefinition(args.time);
        Coords::rectBoundingBox(fromRoD, toRoD, &rod);

        return true;
    }

    return false;
}

/* Override the clip preferences */
void
DissolvePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    updateRange();
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        clipPreferences.setClipComponents(*_srcClip[i], outputComps);
    }
}

void
DissolvePlugin::changedClip(const InstanceChangedArgs & /*args*/,
                            const std::string & /*clipName*/)
{
    updateRange();
}

mDeclarePluginFactory(DissolvePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DissolvePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // Say we are a transition context
    desc.addSupportedContext(eContextTransition);
    desc.addSupportedContext(eContextGeneral);

    // Add supported pixel depths
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
    desc.setChannelSelector(ePixelComponentRGBA);
#endif
}

void
DissolvePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);
    unsigned clipSourceCount = numerousInputs ? kClipSourceCount : 2;

    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceFromClipName);
        } else {
            srcClip = desc.defineClip("0");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        if (context == eContextTransition) {
            // we are a transition, so define the sourceFrom/sourceTo input clip
            srcClip = desc.defineClip(kOfxImageEffectTransitionSourceToClipName);
        } else {
            srcClip = desc.defineClip("1");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
        srcClip->addSupportedComponent(ePixelComponentXY);
#endif
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    ClipDescriptor *maskClip = desc.defineClip("Mask");

    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    if (numerousInputs) {
        for (unsigned i = 2; i < clipSourceCount; ++i) {
            ClipDescriptor *srcClip = desc.defineClip( unsignedToString(i) );
            srcClip->setOptional(true);
            srcClip->addSupportedComponent(ePixelComponentNone);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
            srcClip->addSupportedComponent(ePixelComponentXY);
#endif
            srcClip->addSupportedComponent(ePixelComponentAlpha);
            srcClip->setTemporalClipAccess(false);
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    }

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

    // Define the mandated "Transition" param, note that we don't do anything with this other than.
    // describe it. It is not a true param but how the host indicates to the plug-in how far through
    // the transition it is. It appears on no plug-in side UI, it is purely the hosts to manage.
    if (context == eContextTransition) {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kOfxImageEffectTransitionParamName);
        // The host should have its own interface to the Transition param.
        // (range is 0-1)
        (void)param;
    } else {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamWhich);
        param->setLabel(kParamWhichLabel);
        param->setHint(kParamWhichHint);
        param->setRange(0, clipSourceCount - 1);
        param->setDisplayRange(0, clipSourceCount - 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // don't define the mix param
    ofxsMaskDescribeParams(desc, page);
} // DissolvePluginFactory::describeInContext

ImageEffect*
DissolvePluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);

    return new DissolvePlugin(handle, numerousInputs);
}

static DissolvePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
