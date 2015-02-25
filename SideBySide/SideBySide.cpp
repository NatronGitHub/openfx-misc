/*
 OFX SideBySide plugin.
 Put the left and right view of the input next to each other.

 Copyright (C) 2013 INRIA
 Author: Frederic Devernay <frederic.devernay@inria.fr>

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England

 */

#include "SideBySide.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

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
#define kParamViewOptionLeft "Left"
#define kParamViewOptionRight "Right"

// Base class for the RGBA and the Alpha processor
class SideBySideBase : public OFX::ImageProcessor {
protected:
    const OFX::Image *_srcImg1;
    const OFX::Image *_srcImg2;
    bool _vertical;
    int _offset;
public:
    /** @brief no arg ctor */
    SideBySideBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg1(0)
    , _srcImg2(0)
    , _vertical(false)
    , _offset(0)
    {
    }

    /** @brief set the left src image */
    void setSrcImg1(const OFX::Image *v) {_srcImg1 = v;}

    /** @brief set the right src image */
    void setSrcImg2(const OFX::Image *v) {_srcImg2 = v;}

    /** @brief set vertical stacking and offset oin the vertical or horizontal direction */
    void setVerticalAndOffset(bool v, int offset) {_vertical = v; _offset = offset;}
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ImageSideBySide : public SideBySideBase
{
public:
    // ctor
    ImageSideBySide(OFX::ImageEffect &instance)
    : SideBySideBase(instance)
    {}

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(_offset != 0);
        for(int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for(int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix;
                if ((_vertical && y >= _offset) || (!_vertical && x < _offset)) {
                    srcPix = (const PIX *)(_srcImg1 ? _srcImg1->getPixelAddress(x, _vertical ? y - _offset : y) : 0);
                } else {
                    srcPix = (const PIX *)(_srcImg2 ? _srcImg2->getPixelAddress(_vertical ? x : x - _offset, y ) : 0);
                }

                if (srcPix) {
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = srcPix[c];
                    }
                } else {
                    // no data here, be black and transparent
                    for(int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class SideBySidePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    SideBySidePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , vertical_(0)
    , view1_(0)
    , view2_(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        vertical_ = fetchBooleanParam(kParamVertical);
        view1_ = fetchChoiceParam(kParamView1);
        view2_ = fetchChoiceParam(kParamView2);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(SideBySideBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::BooleanParam *vertical_;
    OFX::ChoiceParam *view1_;
    OFX::ChoiceParam *view2_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
SideBySidePlugin::setupAndProcess(SideBySideBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    int view1;
    view1_->getValueAtTime(args.time, view1);
    int view2;
    view2_->getValueAtTime(args.time, view2);
    std::auto_ptr<const OFX::Image> src1((_srcClip && _srcClip->isConnected()) ?
                                         _srcClip->fetchStereoscopicImage(args.time, view1) : 0);
    std::auto_ptr<const OFX::Image> src2((_srcClip && _srcClip->isConnected()) ?
                                         _srcClip->fetchStereoscopicImage(args.time, view2) : 0);

    // make sure bit depths are sane
    if (src1.get()) {
        if (src1->getRenderScale().x != args.renderScale.x ||
            src1->getRenderScale().y != args.renderScale.y ||
            src1->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src1->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src1->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }
    if (src2.get()) {
        if (src2->getRenderScale().x != args.renderScale.x ||
            src2->getRenderScale().y != args.renderScale.y ||
            src2->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src2->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src2->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    bool vertical = vertical_->getValueAtTime(args.time);
    OfxPointD offset = getProjectOffset();
    OfxPointD size = getProjectSize();

    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    OfxRectD rod = {0., 0., 0., 0.};
    if (_srcClip) {
        rod = _srcClip->getRegionOfDefinition(args.time);
    }

    // clip to the project rect
    //rod.x1 = std::max(rod.x1,offset.x);
    rod.x2 = std::min(rod.x2,offset.x+size.x);
    //rod.y1 = std::max(rod.y1,offset.y);
    rod.y2 = std::min(rod.y2,offset.y+size.y);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg1(src1.get());
    processor.setSrcImg2(src2.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // set the parameters
    processor.setVerticalAndOffset(vertical, vertical?(int)rod.y2:(int)rod.x2);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// override the rod call
bool
SideBySidePlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    if (!_srcClip) {
        return false;
    }
    bool vertical = vertical_->getValueAtTime(args.time);
    OfxPointD offset = getProjectOffset();
    OfxPointD size = getProjectSize();

    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    rod = _srcClip->getRegionOfDefinition(args.time);

    // clip to the project rect
    rod.x1 = std::max(rod.x1,offset.x);
    rod.x2 = std::min(rod.x2,offset.x+size.x);
    rod.y1 = std::max(rod.y1,offset.y);
    rod.y2 = std::min(rod.y2,offset.y+size.y);

    // the RoD is twice the size of the original ROD in one direction
    if (vertical) {
        rod.y2 = rod.y1 + 2*(rod.y2-rod.y1);
    } else {
        rod.x2 = rod.x1 + 2*(rod.x2-rod.x1);
    }
    // say we set it
    return true;
}

// override the roi call
void
SideBySidePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    if (!_srcClip) {
        return;
    }
    bool vertical = vertical_->getValueAtTime(args.time);

    // our RoD is defined with respect to the 'Source' clip's, we are not interested in the mask
    OfxRectD roi = _srcClip->getRegionOfDefinition(args.time);

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
    //if (getContext() != OFX::eContextFilter)
    //  rois.setRegionOfInterest(*_maskClip, roi);
}

// the overridden render function
void
SideBySidePlugin::render(const OFX::RenderArguments &args)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        OFX::throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageSideBySide<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageSideBySide<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageSideBySide<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageSideBySide<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageSideBySide<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageSideBySide<float, 3, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageSideBySide<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageSideBySide<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageSideBySide<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}


using namespace OFX;

mDeclarePluginFactory(SideBySidePluginFactory, ;, {});

void SideBySidePluginFactory::load()
{
    // we can't be used on hosts that don't support the stereoscopic suite
    // returning an error here causes a blank menu entry in Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //    throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void SideBySidePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
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
    // returning an error here crashes Nuke
    //if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
    //  throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    //}
}

void SideBySidePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
{
    if (!OFX::fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true)) {
        throwHostMissingSuiteException(kOfxVegasStereoscopicImageEffectSuite);
    }

    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
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
        page->addChild(*param);
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
        page->addChild(*param);
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
        page->addChild(*param);
    }
}

OFX::ImageEffect* SideBySidePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new SideBySidePlugin(handle);
}

void getSideBySidePluginID(OFX::PluginFactoryArray &ids)
{
    static SideBySidePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
