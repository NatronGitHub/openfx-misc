/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2016 INRIA
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
 * OFX ContactSheet plugin.
 * ContactSheet between inputs.
 */

#include <string>
#include <algorithm>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsMacros.h"
#include "ofxNatron.h"
#include "ofxsCopier.h"
#include "ofxsCoords.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ContactSheetOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription \
    "Make a contact sheet from inputs."
#define kPluginIdentifier "net.sf.openfx.ContactSheetOFX"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamResolution "resolution"
#define kParamResolutionLabel "Resolution"
#define kParamResolutionHint \
    "Resolution of the output image, in pixels."

#define kParamRowsColums "rowsColumns"
#define kParamRowsColumsLabel "Rows/Columns"
#define kParamRowsColumsHint \
    "How many rows and columns in the grid where the input images or frames are arranged."

#define kClipSourceCount 16
#define kClipSourceCountNumerous 128

static
std::string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    std::string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb += ( '0' + (j % 10) );
    }

    return nb;
}

#if 0
// code grabbed from http://stackoverflow.com/questions/9570895/image-downscaling-algorithm

int thumbwidth = 15;
int thumbheight = 15;
double xscale = (thumbwidth+0.0) / width;
double yscale = (thumbheight+0.0) / height;
double threshold = 0.5 / (xscale * yscale);
double yend = 0.0;
for (int f = 0; f < thumbheight; f++) // y on output
{
    double ystart = yend;
    yend = (f + 1) / yscale;
    if (yend >= height) yend = height - 0.000001;
    double xend = 0.0;
    for (int g = 0; g < thumbwidth; g++) // x on output
    {
        double xstart = xend;
        xend = (g + 1) / xscale;
        if (xend >= width) xend = width - 0.000001;
        double sum = 0.0;
        for (int y = (int)ystart; y <= (int)yend; ++y)
        {
            double yportion = 1.0;
            if (y == (int)ystart) yportion -= ystart - y;
            if (y == (int)yend) yportion -= y+1 - yend;
            for (int x = (int)xstart; x <= (int)xend; ++x)
            {
                double xportion = 1.0;
                if (x == (int)xstart) xportion -= xstart - x;
                if (x == (int)xend) xportion -= x+1 - xend;
                sum += picture4[y][x] * yportion * xportion;
            }
        }
        picture3[f][g] = (sum > threshold) ? 1 : 0;
    }
}
#endif

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ContactSheetPlugin
    : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ContactSheetPlugin(OfxImageEffectHandle handle, bool numerousInputs);

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const OFX::FramesNeededArguments &args, OFX::FramesNeededSetter &frames) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip* _dstClip;
    std::vector<OFX::Clip *> _srcClip;
    OFX::Int2DParam *_resolution;
    OFX::Int2DParam* _rowsColumns;
};

ContactSheetPlugin::ContactSheetPlugin(OfxImageEffectHandle handle,
                           bool numerousInputs)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(numerousInputs ? kClipSourceCountNumerous : kClipSourceCount)
    , _resolution(0)
    , _rowsColumns(0)
{
    _dstClip = fetchClip(kOfxImageEffectOutputClipName);
    assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == OFX::ePixelComponentAlpha || _dstClip->getPixelComponents() == OFX::ePixelComponentRGB || _dstClip->getPixelComponents() == OFX::ePixelComponentRGBA) );
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        if ( (getContext() == OFX::eContextFilter) && (i == 0) ) {
            _srcClip[i] = fetchClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            _srcClip[i] = fetchClip( unsignedToString(i) );
        }
        assert(_srcClip[i]);
    }
    _resolution  = fetchInt2DParam(kParamResolution);
    _rowsColumns = fetchInt2DParam(kParamRowsColums);
    assert(_resolution && _rowsColumns);
}

void
ContactSheetPlugin::render(const OFX::RenderArguments &args)
{
    // do nothing as this should never be called as isIdentity should always be trapped
    assert(false);

    const double time = args.time;

    // copy input to output
    int input = 0;
    if ( _automatic->getValueAtTime(time) ) {
        input = getInputAutomatic(time);
    } else {
        input = _which->getValueAtTime(time);
        input = std::max( 0, std::min(input, (int)_srcClip.size() - 1) );
    }
    OFX::Clip *srcClip = _srcClip[input];
    assert( kSupportsMultipleClipPARs   || !srcClip || srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !srcClip || srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    std::auto_ptr<OFX::Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != OFX::eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
    std::auto_ptr<const OFX::Image> src( ( srcClip && srcClip->isConnected() ) ?
                                         srcClip->fetchImage(time) : 0 );
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
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
ContactSheetPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                   OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    // ask for all the inputs that are in the output image
    // TODO1: optimize so that only the inputs visible in args.roi are asked for.
    // TODO2: optimize even more to ask for partial inputs
    int rows, columns;
    _rowsColums->getValueAtTime(time, rows, columns);
    for (unsigned i = 0; i < std::min(_srcClip.size(), rows * columns); ++i) {
        if (i != (unsigned)input) {
            rois.setRegionOfInterest(*_srcClip[i], _srcClip[i]->getRegionOfDefinition() );
        }
    }
}

bool
ContactSheetPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                    OfxRectD &rod)
{
    const double time = args.time;
    int input;

    if ( _automatic->getValueAtTime(time) ) {
        input = getInputAutomatic(time);
    } else {
        input = _which->getValueAtTime(time);
        input = std::max( 0, std::min(input, (int)_srcClip.size() - 1) );
    }
    if ( _srcClip[input] && _srcClip[input]->isConnected() ) {
        rod = _srcClip[input]->getRegionOfDefinition(args.time);

        return true;
    }

    return false;
}


void
ContactSheetPlugin::getFramesNeeded(const OFX::FramesNeededArguments &args,
                                  OFX::FramesNeededSetter &frames)
{
    const double time = args.time;
    int firstFrame;

    _firstFrame->getValueAtTime(time, firstFrame);
    int fadeIn;
    _fadeIn->getValueAtTime(time, fadeIn);
    int fadeOut;
    _fadeOut->getValueAtTime(time, fadeOut);
    int crossDissolve;
    _crossDissolve->getValueAtTime(time, crossDissolve);
    int clip0, clip1;
    double t0, t1;
    getSources(firstFrame, fadeIn, fadeOut, crossDissolve, time, &clip0, &t0, NULL, &clip1, &t1, NULL, NULL);
    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        OfxRangeD range;
        if (i == (unsigned)clip0) {
            range.min = t0;
            range.max = t0;
        } else if (i == (unsigned)clip1) {
            range.min = t1;
            range.max = t1;
        } else {
            // empty range
            range.min = firstFrame;
            range.max = firstFrame - 1;
        }
        frames.setFramesNeeded(*_srcClip[i], range);
    }
}

/* Override the clip preferences */
void
ContactSheetPlugin::getClipPreferences(OFX::ClipPreferencesSetter & /*clipPreferences*/)
{
    // all inputs and outputs should have the same components
    OFX::PixelComponentEnum outputComps = _dstClip->getPixelComponents();

    for (unsigned i = 0; i < _srcClip.size(); ++i) {
        clipPreferences.setClipComponents(*_srcClip[i], outputComps);
    }
}

mDeclarePluginFactory(ContactSheetPluginFactory, {}, {});
void
ContactSheetPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextFilter);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthNone);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthHalf);
    desc.addSupportedBitDepth(eBitDepthFloat);
    desc.addSupportedBitDepth(eBitDepthCustom);
#ifdef OFX_EXTENSIONS_VEGAS
    desc.addSupportedBitDepth(eBitDepthUByteBGRA);
    desc.addSupportedBitDepth(eBitDepthUShortBGRA);
    desc.addSupportedBitDepth(eBitDepthFloatBGRA);
#endif

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
#endif
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
ContactSheetPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc,
                                       OFX::ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);
    unsigned clipSourceCount = numerousInputs ? kClipSourceCountNumerous : kClipSourceCount;

    // Source clip only in the filter context
    // create the mandated source clip
    {
        ClipDescriptor *srcClip;
        if (context == eContextFilter) {
            srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
        } else {
            srcClip = desc.defineClip("0");
            srcClip->setOptional(true);
        }
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }
    {
        ClipDescriptor *srcClip;
        srcClip = desc.defineClip("1");
        srcClip->setOptional(true);
        srcClip->addSupportedComponent(ePixelComponentNone);
        srcClip->addSupportedComponent(ePixelComponentRGB);
        srcClip->addSupportedComponent(ePixelComponentRGBA);
        srcClip->addSupportedComponent(ePixelComponentAlpha);
        srcClip->setTemporalClipAccess(false);
        srcClip->setSupportsTiles(kSupportsTiles);
        srcClip->setIsMask(false);
    }

    if (numerousInputs) {
        for (unsigned i = 2; i < clipSourceCount; ++i) {
            ClipDescriptor *srcClip = desc.defineClip( unsignedToString(i) );
            srcClip->setOptional(true);
            srcClip->addSupportedComponent(ePixelComponentNone);
            srcClip->addSupportedComponent(ePixelComponentRGB);
            srcClip->addSupportedComponent(ePixelComponentRGBA);
            srcClip->addSupportedComponent(ePixelComponentAlpha);
            srcClip->setTemporalClipAccess(false);
            srcClip->setSupportsTiles(kSupportsTiles);
            srcClip->setIsMask(false);
        }
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // which
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamWhich);
        param->setLabel(kParamWhichLabel);
        param->setHint(kParamWhichHint);
        param->setDefault(0);
        param->setRange(0, clipSourceCount - 1);
        param->setDisplayRange(0, clipSourceCount - 1);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    // automatic
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamAutomatic);
        param->setLabel(kParamAutomaticLabel);
        param->setHint(kParamAutomaticHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
#endif
} // ContactSheetPluginFactory::describeInContext

OFX::ImageEffect*
ContactSheetPluginFactory::createInstance(OfxImageEffectHandle handle,
                                    OFX::ContextEnum /*context*/)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (OFX::getImageEffectHostDescription()->isNatron &&
                            OFX::getImageEffectHostDescription()->versionMajor >= 2);

    return new ContactSheetPlugin(handle, numerousInputs);
}

static ContactSheetPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
