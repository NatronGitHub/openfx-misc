/*
 OFX ReConverge plugin.
 Shift convergence so that tracked point appears at screen-depth.
 The ReConverge node only shifts views horizontally, not vertically.

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

#ifdef DEBUG

#include "ReConverge.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsPositionInteract.h"
#include "ofxsMacros.h"

#define kPluginName "ReConvergeOFX"
#define kPluginGrouping "Views/Stereo"
#define kPluginDescription "Shift convergence so that a tracked point appears at screen-depth. " \
"Horizontal disparity may be provided in the red channel of the " \
"disparity input if it has RGBA components, or the Alpha channel " \
"if it only has Alpha. " \
"If no disparity is given, only the offset is taken into account. " \
"The amount of shift in pixels is rounded to the closest integer. " \
"The ReConverge node only shifts views horizontally, not vertically."
#define kPluginIdentifier "net.sf.openfx.reConvergePlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamConvergePoint "convergePoint"
#define kParamConvergePointLabel "Converge Upon"
#define kParamConvergePointHint "Position of the tracked point when the convergence is set"

#define kParamInteractive "interactive"
#define kParamInteractiveLabel "Interactive"
#define kParamInteractiveHint \
"When checked the image will be rendered whenever moving the overlay interact instead of when releasing the mouse button."

#define kParamOffset "offset"
#define kParamOffsetLabel "Convergence Offset"
#define kParamOffsetHint "The disparity of the tracked point will be set to this"

#define kParamConvergeMode "convergeMode"
#define kParamConvergeModeLabel "Mode"
#define kParamConvergeModeHint "Select to view to be shifted in order to set convergence"
#define kParamConvergeModeOptionShiftRight "Shift Right"
#define kParamConvergeModeOptionShiftLeft "Shift Left"
#define kParamConvergeModeOptionShiftBoth "Shift Both"

#define kClipDisparity "Disparity"




// Base class for the RGBA and the Alpha processor
// This class performs a translation by an integer number of pixels (x,y)
class TranslateBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    int _translateX;
    int _translateY;
    
public:
    /** @brief no arg ctor */
    TranslateBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _translateX(0)
    , _translateY(0)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}

    /** @brief set the translation vector */
    void setTranslate(int x, int y) {_translateX = x; _translateY = y;}
};

// template to do the RGBA processing
template <class PIX, int nComponents, int max>
class ImageTranslator : public TranslateBase
{
public:
    // ctor
    ImageTranslator(OFX::ImageEffect &instance)
    : TranslateBase(instance)
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
#pragma message ("TODO")
                        dstPix[c] = max - srcPix[c];
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
class ReConvergePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ReConvergePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _dispClip(0)
    , _convergepoint(0)
    , _offset(0)
    , _convergemode(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        _dispClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(kClipDisparity);
        assert(!_dispClip || (_dispClip->getPixelComponents() == ePixelComponentAlpha || _dispClip->getPixelComponents() == ePixelComponentRGB || _dispClip->getPixelComponents() == ePixelComponentRGBA));

        if (getContext() == OFX::eContextGeneral) {
            _convergepoint = fetchDouble2DParam(kParamConvergePoint);
            assert(_convergepoint);
        }
        _offset = fetchIntParam(kParamOffset);
        _convergemode = fetchChoiceParam(kParamConvergeMode);
        assert(_offset && _convergemode);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(TranslateBase &, const OFX::RenderArguments &args);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_dispClip;

    OFX::Double2DParam *_convergepoint;
    OFX::IntParam     *_offset;
    OFX::ChoiceParam  *_convergemode;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ReConvergePlugin::setupAndProcess(TranslateBase &processor, const OFX::RenderArguments &args)
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
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);

    // make sure bit depths are sane
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            src->getField() != args.fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    int offset = _offset->getValueAtTime(args.time);
    int convergemode;
    _convergemode->getValueAtTime(args.time, convergemode);

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());

    // set the render window
    processor.setRenderWindow(args.renderWindow);

#pragma message ("TODO")
    (void)offset;
    // set the parameters
    if (getContext() == OFX::eContextGeneral && _convergepoint && _dispClip) {
        // fetch the disparity of the tracked point
    }
    //
    switch (convergemode) {
        case 0: // shift left
            break;
        case 1: // shift right
            break;
        case 2: // shift both
            break;
    }

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// override the roi call
void
ReConvergePlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    // set the ROI of the disp clip to the tracked point position (rounded to the nearest pixel)
    if (getContext() == OFX::eContextGeneral && _convergepoint && _srcClip && _dispClip) {
        OfxRectD roi;
        // since getRegionsOfInterest is not view-specific, return a full horizontal band
        roi = _srcClip->getRegionOfDefinition(args.time);
        roi.y1 = args.regionOfInterest.y1;
        roi.y2 = args.regionOfInterest.y2;

        // TODO: we could compute a smaller area, depending on the convergence
        rois.setRegionOfInterest(*_dispClip, roi);
    }
}

// the overridden render function
void
ReConvergePlugin::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    // do the rendering
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageTranslator<unsigned char, 4, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageTranslator<unsigned short, 4, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageTranslator<float, 4, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte : {
                ImageTranslator<unsigned char, 3, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageTranslator<unsigned short, 3, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageTranslator<float, 3, 1> fred(*this);
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
                ImageTranslator<unsigned char, 1, 255> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthUShort : {
                ImageTranslator<unsigned short, 1, 65535> fred(*this);
                setupAndProcess(fred, args);
            }
                break;

            case OFX::eBitDepthFloat : {
                ImageTranslator<float, 1, 1> fred(*this);
                setupAndProcess(fred, args);
            }
                break;
            default :
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}



mDeclarePluginFactory(ReConvergePluginFactory, {}, {});

struct ConvergePointParam {
    static const char* name() { return kParamConvergePoint; }
    static const char* interactiveName() { return kParamInteractive; }
};


using namespace OFX;
void ReConvergePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter); // parameters are offset and convergemode
    desc.addSupportedContext(eContextGeneral); // adds second input for disparity (in the red channel), and convergepoint (with interact)

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

    desc.setOverlayInteractDescriptor(new PositionOverlayDescriptor<ConvergePointParam>);
}

void ReConvergePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    if (context == eContextGeneral) {
        // Optional disparity clip
        ClipDescriptor *dispClip = desc.defineClip(kClipDisparity);
        dispClip->addSupportedComponent(ePixelComponentRGB);
        dispClip->addSupportedComponent(ePixelComponentRGBA);
        dispClip->addSupportedComponent(ePixelComponentAlpha);
        dispClip->setTemporalClipAccess(false);
        dispClip->setOptional(true);
        dispClip->setSupportsTiles(kSupportsTiles);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // convergepoint
    if (context == eContextGeneral) {
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamConvergePoint);
            param->setLabel(kParamConvergePointLabel);
            param->setHint(kParamConvergePointHint);
            param->setDoubleType(eDoubleTypeXYAbsolute);
            param->setDefaultCoordinateSystem(eCoordinatesNormalised);
            param->setDefault(0.5, 0.5);
            param->setIncrement(1.);
            param->setAnimates(true);
            page->addChild(*param);
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamInteractive);
            param->setLabel(kParamInteractiveLabel);
            param->setHint(kParamInteractiveHint);
            param->setAnimates(false);
            page->addChild(*param);
        }
    }

    // offset
    {
        IntParamDescriptor *param = desc.defineIntParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDefault(0);
        param->setRange(-1000, 1000);
        param->setDisplayRange(-100, 100);
        param->setAnimates(true);
        page->addChild(*param);
    }

    // convergemode
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamConvergeMode);
        param->setLabel(kParamConvergeModeLabel);
        param->setHint(kParamConvergeModeHint);
        param->appendOption(kParamConvergeModeOptionShiftRight);
        param->appendOption(kParamConvergeModeOptionShiftLeft);
        param->appendOption(kParamConvergeModeOptionShiftBoth);
        param->setAnimates(true);
        page->addChild(*param);
    }
}

OFX::ImageEffect* ReConvergePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new ReConvergePlugin(handle);
}

void getReConvergePluginID(OFX::PluginFactoryArray &ids)
{
    static ReConvergePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}

#endif