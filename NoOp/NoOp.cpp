/*
 OFX NoOp plugin.
 Does nothing.

 Copyright (C) 2014 INRIA
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

 */

#include "NoOp.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"

#define kPluginName "NoOpOFX"
#define kPluginGrouping "Other"
#define kPluginDescription "Copies the input to the ouput."
#define kPluginIdentifier "net.sf.openfx:NoOpPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kClipInfoParamName "clipInfo"
#define kClipInfoParamLabel "Clip Info..."
#define kClipInfoParamHint "Display information about the inputs"

#define kForceCopyParamName "forceCopy"
#define kForceCopyParamLabel "Force Copy"
#define kForceCopyParamHint "Force copy from input to output"

// Base class for the RGBA and the Alpha processor
class CopierBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;

public:
    /** @brief no arg ctor */
    CopierBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
};

// template to do the RGBA processing
template <class PIX, int nComponents>
class ImageCopier : public CopierBase
{
public:
    // ctor
    ImageCopier(OFX::ImageEffect &instance)
    : CopierBase(instance)
    {}

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {

                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                if (srcPix) {
                    for (int c = 0; c < nComponents; c++) {
                        dstPix[c] = srcPix[c];
                    }
                }
                else {
                    // no src pixel here, be black and transparent
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

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class NoOpPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    NoOpPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , forceCopy_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentAlpha || dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentAlpha || srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        forceCopy_ = fetchBooleanParam(kForceCopyParamName);
        assert(forceCopy_);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(CopierBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;

    OFX::BooleanParam *forceCopy_;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
NoOpPlugin::setupAndProcess(CopierBase &processor, const OFX::RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        dst->getField() != args.fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();

    // fetch main input image
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));

    // make sure bit depths are sane
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// the overridden render function
void
NoOpPlugin::render(const OFX::RenderArguments &args)
{
    bool forceCopy;
    forceCopy_->getValue(forceCopy);

    if (!forceCopy) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host should not render");
        throwSuiteStatusException(kOfxStatFailed);
    }

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch(dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageCopier<unsigned char, 4> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageCopier<unsigned short, 4> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageCopier<float, 4> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch(dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageCopier<unsigned char, 3> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageCopier<unsigned short, 3> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageCopier<float, 3> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentAlpha) {
        switch(dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageCopier<unsigned char, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageCopier<unsigned short, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageCopier<float, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

bool
NoOpPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    bool forceCopy;
    forceCopy_->getValue(forceCopy);

    if (!forceCopy) {
        identityClip = srcClip_;
        return true;
    } else {
        return false;
    }
}


static const char*
bitDepthString(BitDepthEnum bitDepth)
{
    switch (bitDepth) {
        case OFX::eBitDepthUByte:
            return "8u";
        case OFX::eBitDepthUShort:
            return "16u";
        case OFX::eBitDepthHalf:
            return "16f";
        case OFX::eBitDepthFloat:
            return "32f";
        case OFX::eBitDepthCustom:
            return "x";
        case OFX::eBitDepthNone:
            return "0";
#ifdef OFX_EXTENSIONS_VEGAS
        case eBitDepthUByteBGRA:
            return "8uBGRA";
        case eBitDepthUShortBGRA:
            return "16uBGRA";
        case eBitDepthFloatBGRA:
            return "32fBGRA";
#endif
        default:
            return "[unknown bit depth]";
    }
}

static std::string
pixelComponentString(const std::string& p)
{
    const std::string prefix = "OfxImageComponent";
    std::string s = p;
    return s.replace(s.find(prefix),prefix.length(),"");
}

static const char*
premultString(PreMultiplicationEnum e)
{
    switch (e) {
        case eImageOpaque:
            return "Opaque";
        case eImagePreMultiplied:
            return "PreMultiplied";
        case eImageUnPreMultiplied:
            return "UnPreMultiplied";
        default:
            return "[unknown premult]";
    }
}

#ifdef OFX_EXTENSIONS_VEGAS
static const char*
pixelOrderString(PixelOrderEnum e)
{
    switch (e) {
        case ePixelOrderRGBA:
            return "RGBA";
        case ePixelOrderBGRA:
            return "BGRA";
        default:
            return "[unknown pixel order]";
    }
}
#endif

static const char*
fieldOrderString(FieldEnum e)
{
    switch (e) {
        case eFieldNone:
            return "None";
        case eFieldBoth:
            return "Both";
        case eFieldLower:
            return "Lower";
        case eFieldUpper:
            return "Upper";
        case eFieldSingle:
            return "Single";
        case eFieldDoubled:
            return "Doubled";
        default:
            return "[unknown field order]";
    }
}

void
NoOpPlugin::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kClipInfoParamName) {
        std::ostringstream oss;
        oss << "Clip Info:\n\n";
        oss << "Input: ";
        if (!srcClip_) {
            oss << "N/A";
        } else {
            OFX::Clip &c = *srcClip_;
            oss << pixelComponentString(c.getPixelComponentsProperty());
            oss << bitDepthString(c.getPixelDepth());
            oss << " (unmapped: ";
            oss << pixelComponentString(c.getUnmappedPixelComponentsProperty());
            oss << bitDepthString(c.getUnmappedPixelDepth());
            oss << ")\npremultiplication: ";
            oss << premultString(c.getPreMultiplication());
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString(c.getPixelOrder());
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString(c.getFieldOrder());
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
            oss << "\npixel aspect ratio: ";
            oss << c.getPixelAspectRatio();
            oss << "\nframe rate: ";
            oss << c.getFrameRate();
            oss << " (unmapped: ";
            oss << c.getUnmappedFrameRate();
            oss << ")";
            OfxRangeD range = c.getFrameRange();
            oss << "\nframe range: ";
            oss << range.min << "..." << range.max;
            oss << " (unmapped: ";
            range = c.getUnmappedFrameRange();
            oss << range.min << "..." << range.max;
            oss << ")";
            oss << "\nregion of definition: ";
            OfxRectD rod = c.getRegionOfDefinition(args.time);
            oss << rod.x1 << ' ' << rod.y1 << ' ' << rod.x2 << ' ' << rod.y2;
        }
        oss << "\n\n";
        oss << "Output: ";
        if (!dstClip_) {
            oss << "N/A";
        } else {
            OFX::Clip &c = *dstClip_;
            oss << pixelComponentString(c.getPixelComponentsProperty());
            oss << bitDepthString(c.getPixelDepth());
            oss << " (unmapped: ";
            oss << pixelComponentString(c.getUnmappedPixelComponentsProperty());
            oss << bitDepthString(c.getUnmappedPixelDepth());
            oss << ")\npremultiplication: ";
            oss << premultString(c.getPreMultiplication());
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString(c.getPixelOrder());
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString(c.getFieldOrder());
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
            oss << "\npixel aspect ratio: ";
            oss << c.getPixelAspectRatio();
            oss << "\nframe rate: ";
            oss << c.getFrameRate();
            oss << " (unmapped: ";
            oss << c.getUnmappedFrameRate();
            oss << ")";
            OfxRangeD range = c.getFrameRange();
            oss << "\nframe range: ";
            oss << range.min << "..." << range.max;
            oss << " (unmapped: ";
            range = c.getUnmappedFrameRange();
            oss << range.min << "..." << range.max;
            oss << ")";
            oss << "\nregion of definition: ";
            OfxRectD rod = c.getRegionOfDefinition(args.time);
            oss << rod.x1 << ' ' << rod.y1 << ' ' << rod.x2 << ' ' << rod.y2;
        }
        oss << "\n\n";
        oss << "time: " << args.time << ", renderscale: " << args.renderScale.x << 'x' << args.renderScale.y << '\n';
        
        sendMessage(OFX::Message::eMessageMessage, "", oss.str());
    }
}

using namespace OFX;

mDeclarePluginFactory(NoOpPluginFactory, {}, {});

void NoOpPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only filter at the moment
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
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipDepths(true);
    desc.setSupportsMultipleClipPARs(true);
}

void NoOpPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentNone);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NUKE
    //srcClip->addSupportedComponent(ePixelComponentMotionVectors);
    //srcClip->addSupportedComponent(ePixelComponentStereoDisparity);
#endif
    srcClip->addSupportedComponent(ePixelComponentCustom);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);
    
    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NUKE
    //dstClip->addSupportedComponent(ePixelComponentMotionVectors); // crashes Nuke
    //dstClip->addSupportedComponent(ePixelComponentStereoDisparity);
#endif
    dstClip->addSupportedComponent(ePixelComponentCustom);
    dstClip->setSupportsTiles(true);
    
    // make some pages and to things in 
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    BooleanParamDescriptor *forceCopy = desc.defineBooleanParam(kForceCopyParamName);
    forceCopy->setLabels(kForceCopyParamLabel, kForceCopyParamLabel, kForceCopyParamLabel);
    forceCopy->setHint(kForceCopyParamHint);
    forceCopy->setDefault(false);
    forceCopy->setAnimates(false);
    page->addChild(*forceCopy);

    PushButtonParamDescriptor *clipInfo = desc.definePushButtonParam(kClipInfoParamName);
    clipInfo->setLabels(kClipInfoParamLabel, kClipInfoParamLabel, kClipInfoParamLabel);
    clipInfo->setHint(kClipInfoParamHint);
    page->addChild(*clipInfo);
}

OFX::ImageEffect* NoOpPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new NoOpPlugin(handle);
}

void getNoOpPluginID(OFX::PluginFactoryArray &ids)
{
    static NoOpPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
