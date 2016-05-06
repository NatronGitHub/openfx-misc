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
 * OFX TimeDissolve plugin.
 */

#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsImageBlender.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxNatron.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TimeDissolveOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Dissolves between two inputs, starting the dissolve at the in frame and ending at the out frame. You can specify the dissolve curve over time, if the OFX host supports it (else it is a traditional smoothstep)."
#define kPluginIdentifier "net.sf.openfx.TimeDissolvePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamIn "dissolveIn"
#define kParamInLabel "In"
#define kParamInHint "Start dissolve at this frame number."

#define kParamOut "dissolveOut"
#define kParamOutLabel "Out"
#define kParamOutHint "End dissolve at this frame number."

#define kParamCurve "dissolveCurve"
#define kParamCurveLabel "Curve"
#define kParamCurveHint "Shape of the dissolve. Horizontal value is from 0 to 1: 0 is the frame before the In frame and should have a value of 0; 1 is the frame after the Out frame and should have a value of 1."

#define kClipA "A"
#define kClipB "B"

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeDissolvePlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    TimeDissolvePlugin(OfxImageEffectHandle handle,
                       bool supportsParametricParameter)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClipA(0)
        , _srcClipB(0)
        , _dissolveIn(0)
        , _dissolveOut(0)
        , _dissolveCurve(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == OFX::ePixelComponentRGBA || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentXY || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha) );

        _srcClipA = fetchClip(kClipA);
        assert( _srcClipA && (_srcClipA->getPixelComponents() == OFX::ePixelComponentRGBA || _srcClipA->getPixelComponents() == OFX::ePixelComponentRGB || _srcClipA->getPixelComponents() == OFX::ePixelComponentXY || _srcClipA->getPixelComponents() == OFX::ePixelComponentAlpha) );

        _srcClipB = fetchClip(getContext() == OFX::eContextFilter ? kOfxImageEffectSimpleSourceClipName : kClipB);
        assert( _srcClipB && (_srcClipB->getPixelComponents() == OFX::ePixelComponentRGBA || _srcClipB->getPixelComponents() == OFX::ePixelComponentRGB || _srcClipB->getPixelComponents() == OFX::ePixelComponentXY || _srcClipB->getPixelComponents() == OFX::ePixelComponentAlpha) );

        _dissolveIn = fetchIntParam(kParamIn);
        _dissolveOut = fetchIntParam(kParamOut);
        assert(_dissolveIn && _dissolveOut);
        if (supportsParametricParameter) {
            _dissolveCurve = fetchParametricParam(kParamCurve);
        }
    }

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const OFX::IsIdentityArguments &args, OFX::Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    //virtual void changedClip(const OFX::InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(OFX::ImageBlenderBase &, const OFX::RenderArguments &args);

private:

    template<int nComponents>
    void renderForComponents(const OFX::RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const OFX::RenderArguments &args);

    double getTransition(double time);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClipA;
    OFX::Clip *_srcClipB;
    OFX::IntParam* _dissolveIn;
    OFX::IntParam* _dissolveOut;
    OFX::ParametricParam* _dissolveCurve;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// make sure components are sane
static void
checkComponents(const OFX::Image &src,
                OFX::BitDepthEnum dstBitDepth,
                OFX::PixelComponentEnum dstComponents)
{
    OFX::BitDepthEnum srcBitDepth     = src.getPixelDepth();
    OFX::PixelComponentEnum srcComponents  = src.getPixelComponents();

    // see if they have the same depths and bytes and all
    if ( ( srcBitDepth != dstBitDepth) || ( srcComponents != dstComponents) ) {
        OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
}

double
TimeDissolvePlugin::getTransition(double time)
{
    int dissolveIn, dissolveOut;

    _dissolveIn->getValueAtTime(time, dissolveIn);
    --dissolveIn;
    _dissolveOut->getValueAtTime(time, dissolveOut);
    ++dissolveOut;
    if (dissolveOut < dissolveIn) {
        dissolveOut = dissolveIn;
    }
    double which;
    if (time <= dissolveIn) {
        which = 0.;
    } else if (time >= dissolveOut) {
        which = 1.;
    } else {
        which = std::max( 0., std::min( (time - dissolveIn) / (dissolveOut - dissolveIn), 1. ) );
        if (_dissolveCurve) {
            which = std::max( 0., std::min(_dissolveCurve->getValue(0, time, which), 1.) );
        } else {
            // no curve (OFX host does not support it), default to traditional smoothstep
            which = which * which * (3 - 2 * which);
        }
    }

    return which;
}

/* set up and run a processor */
void
TimeDissolvePlugin::setupAndProcess(OFX::ImageBlenderBase &processor,
                                    const OFX::RenderArguments &args)
{
    const double time = args.time;
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( _dstClip->fetchImage(time) );

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

    // get the transition value
    double which = getTransition(time);

    if ( (which == 0.) || (which == 1.) ) {
        std::auto_ptr<const OFX::Image> src( ( which == 0. && _srcClipA && _srcClipA->isConnected() ) ?
                                             _srcClipA->fetchImage(time) :
                                             ( which == 1. && _srcClipB && _srcClipB->isConnected() ) ?
                                             _srcClipB->fetchImage(time) : 0 );
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
        copyPixels( *this, args.renderWindow, src.get(), dst.get() );

        return;
    }

    // fetch the two source images
    std::auto_ptr<const OFX::Image> fromImg( ( _srcClipA && _srcClipA->isConnected() ) ?
                                             _srcClipA->fetchImage(time) : 0 );
    std::auto_ptr<const OFX::Image> toImg( ( _srcClipB && _srcClipB->isConnected() ) ?
                                           _srcClipB->fetchImage(time) : 0 );

    // make sure bit depths are sane
    if ( fromImg.get() ) {
        if ( (fromImg->getRenderScale().x != args.renderScale.x) ||
             ( fromImg->getRenderScale().y != args.renderScale.y) ||
             ( ( fromImg->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( fromImg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*fromImg, dstBitDepth, dstComponents);
    }
    if ( toImg.get() ) {
        if ( (toImg->getRenderScale().x != args.renderScale.x) ||
             ( toImg->getRenderScale().y != args.renderScale.y) ||
             ( ( toImg->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( toImg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        checkComponents(*toImg, dstBitDepth, dstComponents);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setFromImg( fromImg.get() );
    processor.setToImg( toImg.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the scales
    processor.setBlend(which);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // TimeDissolvePlugin::setupAndProcess

// the overridden render function
void
TimeDissolvePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || ( _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() && _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() ) );
    assert( kSupportsMultipleClipDepths || ( _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth()       && _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() ) );

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        renderForComponents<4>(args);
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        renderForComponents<3>(args);
    } else if (dstComponents == OFX::ePixelComponentXY) {
        renderForComponents<2>(args);
    }  else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        renderForComponents<1>(args);
    } // switch
} // render

template<int nComponents>
void
TimeDissolvePlugin::renderForComponents(const OFX::RenderArguments &args)
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
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
TimeDissolvePlugin::renderForBitDepth(const OFX::RenderArguments &args)
{
    OFX::ImageBlender<PIX, nComponents> fred(*this);

    setupAndProcess(fred, args);
}

// overridden is identity
bool
TimeDissolvePlugin::isIdentity(const OFX::IsIdentityArguments &args,
                               OFX::Clip * &identityClip,
                               double &identityTime)
{
    const double time = args.time;
    // get the transition value
    double which = getTransition(time);

    identityTime = time;

    // at the start?
    if (which <= 0.) {
        identityClip = _srcClipA;
        identityTime = time;

        return true;
    }

    if (which >= 1.) {
        identityClip = _srcClipB;
        identityTime = time;

        return true;
    }

    // nope, identity we isnt
    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
TimeDissolvePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                         OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    double which = getTransition(time);
    const OfxRectD emptyRoI = {0., 0., 0., 0.};

    if (which <= 0.) {
        rois.setRegionOfInterest(*_srcClipB, emptyRoI);
    } else if (which >= 1.) {
        rois.setRegionOfInterest(*_srcClipA, emptyRoI);
    }
}

bool
TimeDissolvePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                          OfxRectD &rod)
{
    const double time = args.time;
    // get the transition value
    double which = getTransition(time);

    // at the start?
    if ( (which <= 0.0) && _srcClipA ) {
        rod = _srcClipA->getRegionOfDefinition(time);

        return true;
    }

    // at the end?
    if ( (which >= 1.0) && _srcClipB ) {
        rod = _srcClipB->getRegionOfDefinition(time);

        return true;
    }

    if (_srcClipA && _srcClipB) {
        OfxRectD fromRoD = _srcClipA->getRegionOfDefinition(time);
        OfxRectD toRoD = _srcClipB->getRegionOfDefinition(time);
        OFX::Coords::rectBoundingBox(fromRoD, toRoD, &rod);

        return true;
    }

    return false;
}

/* Override the clip preferences, we need to say we are setting the frame varying flag */
void
TimeDissolvePlugin::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    clipPreferences.setOutputFrameVarying(true);
    clipPreferences.setOutputHasContinousSamples(true);
}

mDeclarePluginFactory(TimeDissolvePluginFactory, {}, {}
                      );
void
TimeDissolvePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
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
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
TimeDissolvePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                             ContextEnum context)
{
    {
        ClipDescriptor *srcClip;
        srcClip = desc.defineClip(context == eContextFilter ? kOfxImageEffectSimpleSourceClipName : kClipB);
        srcClip->setOptional(true);
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentXY);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        srcClip = desc.defineClip(kClipA);
        srcClip->setOptional(true);
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentXY);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);

    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        IntParamDescriptor *param = desc.defineIntParam(kParamIn);
        param->setLabel(kParamInLabel);
        param->setHint(kParamInHint);
        param->setDefault(1);
        param->setRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamOut);
        param->setLabel(kParamOutLabel);
        param->setHint(kParamOutHint);
        param->setDefault(10);
        param->setRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(INT_MIN, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        if (page) {
            page->addChild(*param);
        }
    }
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !( gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                  (gHostDescription.versionMajor == 8 || gHostDescription.versionMajor == 9) ) ); // Nuke 8 and 9 are known to *not* support Parametric
    if (supportsParametricParameter) {
        OFX::ParametricParamDescriptor* param = desc.defineParametricParam(kParamCurve);
        assert(param);
        param->setLabel(kParamCurveLabel);
        param->setHint(kParamCurveHint);

        // define it as two dimensional
        param->setDimension(1);
        param->setDimensionLabel(kParamCurveLabel, 0);

        // set the UI colour for each dimension
        const OfxRGBColourD shadow   = {0.93, 0.24, 0.71};
        param->setUIColour( 0, shadow );

        // set the min/max parametric range to 0..1
        param->setRange(0.0, 1.0);

        param->addControlPoint(0, // curve to set
                               0.0,   // time, ignored in this case, as we are not adding a key
                               0.0,   // parametric position, zero
                               0.0,   // value to be, 0
                               false);   // don't add a key
        param->addControlPoint(0, 0.0, 1.0, 1.0, false);
        if (page) {
            page->addChild(*param);
        }
    }
} // TimeDissolvePluginFactory::describeInContext

ImageEffect*
TimeDissolvePluginFactory::createInstance(OfxImageEffectHandle handle,
                                          ContextEnum /*context*/)
{
    const ImageEffectHostDescription &gHostDescription = *OFX::getImageEffectHostDescription();
    const bool supportsParametricParameter = ( gHostDescription.supportsParametricParameter &&
                                               !( gHostDescription.hostName == "uk.co.thefoundry.nuke" &&
                                                  (gHostDescription.versionMajor == 8 || gHostDescription.versionMajor == 9) ) );

    return new TimeDissolvePlugin(handle, supportsParametricParameter);
}

static TimeDissolvePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
