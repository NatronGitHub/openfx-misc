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
 * OFX SideBySide plugin.
 * Put the left and right view of the input next to each other.
 */

#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "SideBySideOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription "Put the left and right view of the input next to each other."
#define kPluginIdentifier "net.sf.openfx.sideBySidePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamVertical "vertical"
#define kParamVerticalLabel "Vertical"
#define kParamVerticalHint "Stack views vertically instead of horizontally"

#define kParamView1 "view1"
#define kParamView1Label "View 1"
#define kParamView1Hint "First view"
#define kParamView2 "view2"
#define kParamView2Label "View 2"
#define kParamView2Hint "Second view"
#define kParamViewOptionLeft "Left", "Left view.", "left"
#define kParamViewOptionRight "Right", "Right view.", "right"

// Base class for the RGBA and the Alpha processor
class SideBySideBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg1;
    const Image *_srcImg2;
    bool _vertical;

    //Contains the (x1,x2) or (y1,y2) (depending on _vertical) bounds of the first image
    OfxRangeI _srcOffset;

public:
    /** @brief no arg ctor */
    SideBySideBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg1(NULL)
        , _srcImg2(NULL)
        , _vertical(false)
        , _srcOffset()
    {
    }

    /** @brief set the left src image */
    void setSrcImg1(const Image *v) {_srcImg1 = v; }

    /** @brief set the right src image */
    void setSrcImg2(const Image *v) {_srcImg2 = v; }

    /** @brief set vertical stacking and offset oin the vertical or horizontal direction */
    void setVerticalAndOffset(bool v,
                              const OfxRangeI& offset) {_vertical = v; _srcOffset = offset; }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ImageSideBySide
    : public SideBySideBase
{
public:
    // ctor
    ImageSideBySide(ImageEffect &instance)
        : SideBySideBase(instance)
    {}

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_srcOffset.max != 0);
        double offset = _srcOffset.max - _srcOffset.min;
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix;
                if ( ( _vertical && (y >= _srcOffset.max) ) || ( !_vertical && (x < _srcOffset.max) ) ) {
                    srcPix = (const PIX *)(_srcImg1 ? _srcImg1->getPixelAddress(x, _vertical ? y - offset : y) : 0);
                } else {
                    srcPix = (const PIX *)(_srcImg2 ? _srcImg2->getPixelAddress(_vertical ? x : x - offset, y ) : 0);
                }

                if (srcPix) {
                    for (int c = 0; c < nComponents; c++) {
                        dstPix[c] = srcPix[c];
                    }
                } else {
                    // no data here, be black and transparent
                    for (int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SideBySidePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    SideBySidePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , vertical_(NULL)
        , view1_(NULL)
        , view2_(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentAlpha ||
                               _srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        vertical_ = fetchBooleanParam(kParamVertical);
        view1_ = fetchChoiceParam(kParamView1);
        view2_ = fetchChoiceParam(kParamView2);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /** @brief get the frame/views needed for input clips*/
    virtual void getFrameViewsNeeded(const FrameViewsNeededArguments& args, FrameViewsNeededSetter& frameViews) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(SideBySideBase &, const RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam *vertical_;
    ChoiceParam *view1_;
    ChoiceParam *view2_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
SideBySidePlugin::setupAndProcess(SideBySideBase &processor,
                                  const RenderArguments &args)
{
    // get a dst image
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
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
    int view1;
    view1_->getValueAtTime(args.time, view1);
    int view2;
    view2_->getValueAtTime(args.time, view2);
    if (!_srcClip || !_srcClip->isConnected()) {
        throwSuiteStatusException(kOfxStatFailed);

        return;
    }
    auto_ptr<const Image> src1( ( _srcClip && _srcClip->isConnected() ) ?
                                     _srcClip->fetchImagePlane(args.time, view1, kFnOfxImagePlaneColour) : 0 );
    auto_ptr<const Image> src2( ( _srcClip && _srcClip->isConnected() ) ?
                                     _srcClip->fetchImagePlane(args.time, view2, kFnOfxImagePlaneColour) : 0 );

    // make sure bit depths are sane
    if ( src1.get() ) {
        if ( (src1->getRenderScale().x != args.renderScale.x) ||
             ( src1->getRenderScale().y != args.renderScale.y) ||
             ( ( src1->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src1->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src1->getPixelDepth();
        PixelComponentEnum srcComponents = src1->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    if ( src2.get() ) {
        if ( (src2->getRenderScale().x != args.renderScale.x) ||
             ( src2->getRenderScale().y != args.renderScale.y) ||
             ( ( src2->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src2->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src2->getPixelDepth();
        PixelComponentEnum srcComponents = src2->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    bool vertical = vertical_->getValueAtTime(args.time);

    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    OfxRectD leftRod = _srcClip->getRegionOfDefinition(args.time, view1);
    OfxRangeI offset;
    offset.min = vertical ? int(leftRod.y1 * args.renderScale.y) : int(leftRod.x1 * args.renderScale.x);
    offset.max = vertical ? int(leftRod.y2 * args.renderScale.y) : int(leftRod.x2 * args.renderScale.x);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg1( src1.get() );
    processor.setSrcImg2( src2.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the parameters
    processor.setVerticalAndOffset(vertical, offset);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // SideBySidePlugin::setupAndProcess

// override the rod call
bool
SideBySidePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return false;
    }
    bool vertical = vertical_->getValueAtTime(args.time);
    int view1;
    view1_->getValueAtTime(args.time, view1);
    int view2;
    view2_->getValueAtTime(args.time, view2);
    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    rod = _srcClip->getRegionOfDefinition(args.time, view1);
    OfxRectD rightRod = _srcClip->getRegionOfDefinition(args.time, view2);
    if ( Coords::rectIsEmpty(rightRod) ) {
        return false;
    }
    if (vertical) {
        rod.y2 = rod.y2 + (rightRod.y2 - rightRod.y1);
    } else {
        rod.x2 = rod.x2 + (rightRod.x2 - rightRod.x1);
    }

    // say we set it
    return true;
}

// override the roi call
void
SideBySidePlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                       RegionOfInterestSetter &rois)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    bool vertical = vertical_->getValueAtTime(args.time);

    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    OfxRectD roi = _srcClip->getRegionOfDefinition(args.time);
    if ( Coords::rectIsEmpty(roi) ) {
        return;
    }

    // since getRegionsOfInterest is not view-specific, return a full horizontal or vertical band
    if (vertical) {
        roi.x1 = args.regionOfInterest.x1;
        roi.x2 = args.regionOfInterest.x2;
    } else {
        roi.y1 = args.regionOfInterest.y1;
        roi.y2 = args.regionOfInterest.y2;
    }
    rois.setRegionOfInterest(*_srcClip, roi);


    // set it on the mask only if we are in an interesting context
    //if (getContext() != eContextFilter)
    //  rois.setRegionOfInterest(*_maskClip, roi);
}

void
SideBySidePlugin::getFrameViewsNeeded(const FrameViewsNeededArguments& args,
                                      FrameViewsNeededSetter& frameViews)
{
    int view1;

    view1_->getValueAtTime(args.time, view1);
    int view2;
    view2_->getValueAtTime(args.time, view2);
    OfxRangeD range;
    range.min = range.max = args.time;
    frameViews.addFrameViewsNeeded(*_srcClip, range, view1);
    frameViews.addFrameViewsNeeded(*_srcClip, range, view2);
}

// the internal render function
template <int nComponents>
void
SideBySidePlugin::renderInternal(const RenderArguments &args,
                                 BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ImageSideBySide<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ImageSideBySide<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ImageSideBySide<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
SideBySidePlugin::render(const RenderArguments &args)
{
    if ( !fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) ) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

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

mDeclarePluginFactory(SideBySidePluginFactory, {ofxsThreadSuiteCheck();}, {});
#if 0
void
SideBySidePluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}
#endif

void
SideBySidePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only filter at the moment
    desc.addSupportedContext(eContextFilter);

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

    //We're using the view calls (i.e: getFrameViewsNeeded)
    desc.setIsViewAware(true);

    //We render the same thing whatever the requested view
    desc.setIsViewInvariant(eViewInvarianceAllViewsInvariant);


    // returning an error here crashes Nuke
    //if (!fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
SideBySidePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
{
    if ( !fetchSuite(kFnOfxImageEffectPlaneSuite, 1, true) ) {
        throwHostMissingSuiteException(kFnOfxImageEffectPlaneSuite);
    }

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

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // vertical
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamVertical);
        param->setDefault(false);
        param->setHint(kParamVerticalHint);
        param->setLabel(kParamVerticalLabel);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // view1
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamView1);
        param->setHint(kParamView1Hint);
        param->setLabel(kParamView1Label);
        param->appendOption(kParamViewOptionLeft);
        param->appendOption(kParamViewOptionRight);
        param->setDefault(0);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // view2
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamView2);
        param->setHint(kParamView2Hint);
        param->setLabel(kParamView2Label);
        param->appendOption(kParamViewOptionLeft);
        param->appendOption(kParamViewOptionRight);
        param->setDefault(1);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // SideBySidePluginFactory::describeInContext

ImageEffect*
SideBySidePluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new SideBySidePlugin(handle);
}

static SideBySidePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
