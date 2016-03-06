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
 * OFX TimeBuffer plugin.
 */

#include <cmath>
#include <algorithm>
//#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsCopier.h"
//#include "ofxsCoords.h"
#include "ofxsMacros.h"

#define kPluginReadName "TimeBufferRead"
#define kPluginReadDescription \
"Read an time buffer at current time.\n" \
"A time buffer may be used to get the output of any plugin at a previous time, captured using TimeBufferWrite.\n" \
"This can typically be used to accumulate several render passes on the same image.\n"
#define kPluginReadIdentifier "net.sf.openfx.TimeBufferRead"
#define kPluginWriteName "TimeBufferWrite"
#define kPluginWriteDescription "Write an time buffer at currect time. Only one instance may exist with a given accumulation buffer name."
#define kPluginWriteIdentifier "net.sf.openfx.TimeBufferWrite"
#define kPluginGrouping "Time"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe


using namespace OFX;


/*
 We maintain a global map from the buffer name to the buffer data.
 
 The buffer data contains:
 - an image buffers stored with its valid read time (which is the write time +1), or an invalid date
 - the pointer to the read and the write instances, which should be unique, or NULL if it is not yet created.


 When TimeBufferReadPlugin::render(t) is called:
 * if the write instance does not exist, an error is displayed and render fails
 * if t <= startTime:
   - a black image is rendered
   - if t == startTime, the buffer is locked and marked as dirty, with date t+1, then unlocked
 * if t > startTime:
   - the buffer is locked, and if it doesn't have date t, then either the render fails, a black image is rendered, or the buffer is used anyway, depending on the user-chosen strategy
   - if it is marked as dirty, it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
   - when the buffer is locked and clean, it is copied to output and unlocked
   - the buffer is re-locked for writing, and marked as dirty, with date t+1, then unlocked

 When TimeBufferReadPlugin::getRegionOfDefinition(t) is called:
 * if the write instance does not exist, an error is displayed and render fails
 * if t <= startTime:
 - the RoD is empty
 * if t > startTime:
 - the buffer is locked, and if it doesn't have date t, then either getRoD fails, a black image with an empty RoD is rendered, or the RoD from buffer is used anyway, depending on the user-chosen strategy
 - if it is marked as dirty ,it is unlocked, then locked and read again after a delay (there are no condition variables in the multithread suite, polling is the only solution). The delay starts at 10ms, and is multiplied by two at each unsuccessful lock. abort() is checked at each iteration.
 - when the buffer is locked and clean, the buffer's RoD is returned and it is unlocked


 When TimeBufferWritePlugin::render(t) is called:
 - if the read instance does not exist, an error is displayed and render fails
 - the buffer is locked for writing, and if it doesn't have date t+1 or is not dirty, then it is unlocked, render fails and a message is posted. It may be because the TimeBufferRead plugin is not upstream - in this case a solution is to connect TimeBufferRead output to TimeBufferWrite input for syncing.
 - src is copied to the buffer, and it is marked as not dirty, then unlocked
 - src is also copied to output.


 There is a "Reset" button both in TimeBufferRead and TimeBufferWrite, which resets the lock and the buffer.

If we ever need it, a read-write lock can be implemented using two mutexes, as described in
 https://en.wikipedia.org/wiki/Readers%E2%80%93writer_lock#Using_two_mutexes
 */

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeBufferReadPlugin : public OFX::ImageEffect
{
public:

    /** @brief ctor */
    TimeBufferReadPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGBA)));

    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

// the overridden render function
void
TimeBufferReadPlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";
    const double time = args.time;

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

    assert(dstBitDepth == OFX::eBitDepthFloat);
    assert(dstComponents == OFX::ePixelComponentRGBA);

    // TODO: do the rendering
    fillBlack(*this, args.renderWindow, dst.get());
    //std::cout << "render! OK\n";
}


mDeclarePluginFactory(TimeBufferReadPluginFactory, {}, {});

void
TimeBufferReadPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginReadName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginReadDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextGenerator);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
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
    
    //std::cout << "describe! OK\n";
}


void
TimeBufferReadPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);
    

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    // TODO: describe plugin params

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
//std::cout << "describeInContext! OK\n";
}

OFX::ImageEffect*
TimeBufferReadPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TimeBufferReadPlugin(handle);
}


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TimeBufferWritePlugin : public OFX::ImageEffect
{
public:

    /** @brief ctor */
    TimeBufferWritePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
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

    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

// the overridden render function
void
TimeBufferWritePlugin::render(const OFX::RenderArguments &args)
{
    //std::cout << "render!\n";

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());

    // do the rendering
    // get a dst image
    std::auto_ptr<OFX::Image>  dst( _dstClip->fetchImage(args.time) );
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    assert(dstComponents == OFX::ePixelComponentRGBA);
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

    const double time = args.time;

    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // TODO: do the stuff
    copyPixels(*this, args.renderWindow, src.get(), dst.get());
    //std::cout << "render! OK\n";
}


mDeclarePluginFactory(TimeBufferWritePluginFactory, {}, {});

void TimeBufferWritePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginWriteName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginWriteDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
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
    
    //std::cout << "describe! OK\n";
}


void
TimeBufferWritePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);
    

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    // TODO: describe plugin params

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kNatronOfxParamProcessA);
        param->setLabel(kNatronOfxParamProcessALabel);
        param->setHint(kNatronOfxParamProcessAHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
//std::cout << "describeInContext! OK\n";
}

OFX::ImageEffect*
TimeBufferWritePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new TimeBufferWritePlugin(handle);
}

static TimeBufferReadPluginFactory p1(kPluginReadIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static TimeBufferWritePluginFactory p2(kPluginWriteIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
