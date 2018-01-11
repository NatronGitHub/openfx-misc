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
 * OFX JoinViews plugin.
 * JoinView inputs to make a stereo output.
 */

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "JoinViewsOFX"
#define kPluginGrouping "Views"
#define kPluginDescription "JoinView inputs to make a stereo output. " \
    "The first view from each input is copied to the left and right views of the output."
#define kPluginIdentifier "net.sf.openfx.joinViewsPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipLeft "Left"
#define kClipRight "Right"


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class JoinViewsPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    JoinViewsPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcLeftClip(NULL)
        , _srcRightClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcLeftClip = fetchClip(kClipLeft);
        assert( _srcLeftClip && (_srcLeftClip->getPixelComponents() == ePixelComponentAlpha || _srcLeftClip->getPixelComponents() == ePixelComponentRGB || _srcLeftClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcRightClip = fetchClip(kClipRight);
        assert( _srcRightClip && (_srcRightClip->getPixelComponents() == ePixelComponentAlpha || _srcRightClip->getPixelComponents() == ePixelComponentRGB || _srcRightClip->getPixelComponents() == ePixelComponentRGBA) );
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** @brief get the frame/views needed for input clips*/
    virtual void getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(PixelProcessorFilterBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcLeftClip;
    Clip *_srcRightClip;
};

bool
JoinViewsPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                       OfxRectD &rod)
{
    /*
       The RoD has to be the union of all views. Imagine a graph example in Natron as such:

       Reader1—>Crop1 \ (right)
                            JoinViews1—> SideBySide1 —> Viewer
       Reader2—>Crop2 / (left)

       In OpenFX-HostSupport, the RoI returned by getRegionsOfInterest is clipped against the RoD.
       That would mean that the RoIS returned by SideBySide1 in our example would get clipped against the
       RoD of Crop2, which is wrong obviously for the RoI of the right view.
       The solution is to return the union of the RoDs of the views for JoinViews so that the clip does not harm
       the RoIs of the grpah downstream.
     */

    OfxRectD leftRoD = _srcLeftClip->getRegionOfDefinition(args.time, 0);
    OfxRectD rightRoD = _srcRightClip->getRegionOfDefinition(args.time, 0);

    Coords::rectBoundingBox(leftRoD, rightRoD, &rod);

    return true;
}

void
JoinViewsPlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args,
                                     FrameViewsNeededSetter& frameViews)
{
    OfxRangeD range;

    range.min = range.max = args.time;

    //Always fetch the view 0 on source clips
    frameViews.addFrameViewsNeeded(*_srcLeftClip, range, 0);
    frameViews.addFrameViewsNeeded(*_srcRightClip, range, 0);
}

void
JoinViewsPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    clipPreferences.setClipComponents(*_srcLeftClip, outputComps);
    clipPreferences.setClipComponents(*_srcRightClip, outputComps);
}

bool
JoinViewsPlugin::isIdentity(const IsIdentityArguments &args,
                            Clip * &identityClip,
                            double & /*identityTime*/
                            , int& /*view*/, std::string& /*plane*/)
{
    if (args.view == 0) {
        identityClip = _srcLeftClip;
    } else {
        identityClip = _srcRightClip;
    }

    return true;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
JoinViewsPlugin::setupAndProcess(PixelProcessorFilterBase &processor,
                                 const RenderArguments &args)
{
    // get a dst image
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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    auto_ptr<const Image> src( args.renderView == 0 ?
                                    ( ( _srcLeftClip && _srcLeftClip->isConnected() ) ?
                                      _srcLeftClip->fetchStereoscopicImage(args.time, 0) : 0 ) :
                                    ( ( _srcRightClip && _srcRightClip->isConnected() ) ?
                                      _srcRightClip->fetchStereoscopicImage(args.time, 0) : 0 ) );

    if ( !src.get() ) {
        if ( !abort() ) {
            throwSuiteStatusException(kOfxStatFailed);
        } else {
            return;
        }
    } else {
        // make sure bit depths are sane
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // JoinViewsPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
JoinViewsPlugin::renderInternal(const RenderArguments &args,
                                BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        PixelCopier<unsigned char, nComponents> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        PixelCopier<unsigned short, nComponents> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        PixelCopier<float, nComponents> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
JoinViewsPlugin::render(const RenderArguments &args)
{
    if ( !fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) ) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcLeftClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcLeftClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcRightClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcRightClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

mDeclarePluginFactory(JoinViewsPluginFactory,{ofxsThreadSuiteCheck();}, {});
#if 0
void
JoinViewsPluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}
#endif

void
JoinViewsPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only general at the moment, because there are several inputs
    // (see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectContextFilter)
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
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

    //We only render color plane
    desc.setIsMultiPlanar(false);

    //We're using the view calls (i.e: getFrameViewsNeeded)
    desc.setIsViewAware(true);

    //We do not render the same thing on all views
    desc.setIsViewInvariant(eViewInvarianceAllViewsVariant);


    // returning an error here crashes Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
    if (getImageEffectHostDescription()->isNatron) {
        desc.setIsDeprecated(true); // prefer Natron's internal JoinViews
    }
#endif
}

void
JoinViewsPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                          ContextEnum /*context*/)
{
    if ( !fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) &&
         !fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }

    // create the source clips from the rightmost one (in Nuke's GUI) to the leftmost
    ClipDescriptor *srcRightClip = desc.defineClip(kClipRight);
    srcRightClip->addSupportedComponent(ePixelComponentRGBA);
    srcRightClip->addSupportedComponent(ePixelComponentRGB);
    srcRightClip->addSupportedComponent(ePixelComponentXY);
    srcRightClip->addSupportedComponent(ePixelComponentAlpha);
    srcRightClip->setTemporalClipAccess(false);
    srcRightClip->setSupportsTiles(kSupportsTiles);
    srcRightClip->setIsMask(false);

    ClipDescriptor *srcLeftClip = desc.defineClip(kClipLeft);
    srcLeftClip->addSupportedComponent(ePixelComponentRGBA);
    srcLeftClip->addSupportedComponent(ePixelComponentRGB);
    srcLeftClip->addSupportedComponent(ePixelComponentXY);
    srcLeftClip->addSupportedComponent(ePixelComponentAlpha);
    srcLeftClip->setTemporalClipAccess(false);
    srcLeftClip->setSupportsTiles(kSupportsTiles);
    srcLeftClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
}

ImageEffect*
JoinViewsPluginFactory::createInstance(OfxImageEffectHandle handle,
                                       ContextEnum /*context*/)
{
    return new JoinViewsPlugin(handle);
}

static JoinViewsPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
