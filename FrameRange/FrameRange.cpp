/*
 OFX FrameRange plugin.

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

#include "FrameRange.h"

#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#define kPluginName "FrameRangeOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Set the frame range for a clip. Useful in conjunction with AppendClipOFX."
#define kPluginIdentifier "net.sf.openfx.FrameRange"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamFrameRange "frameRange"
#define kParamFrameRangeLabel "Frame Range"
#define kParamFrameRangeHint "Output frame range."

#define kParamReset "reset"
#define kParamResetLabel "Reset"
#define kParamResetHint "Resets the frame range to its initial value."

using namespace OFX;

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class FrameRangePlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    FrameRangePlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _frameRange(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentAlpha || _srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA));
        _frameRange = fetchInt2DParam(kParamFrameRange);
        assert(_frameRange);
    }

private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const OFX::TransformArguments &args, OFX::Clip * &transformClip, double transformMatrix[9]);
#endif

    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

      /* override the time domain action, only for the general context */
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;

    OFX::Int2DParam *_frameRange;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


// the overridden render function
void
FrameRangePlugin::render(const OFX::RenderArguments &/*args*/)
{
    assert(false);
}

bool
FrameRangePlugin::isIdentity(const IsIdentityArguments &/*args*/, Clip * &identityClip, double &/*identityTime*/)
{
    identityClip = _srcClip;
    return true;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool
FrameRangePlugin::getTransform(const OFX::TransformArguments &/*args*/, OFX::Clip * &transformClip, double transformMatrix[9])
{
    transformClip = _srcClip;
    transformMatrix[0] = 1.;
    transformMatrix[1] = 0.;
    transformMatrix[2] = 0.;
    transformMatrix[3] = 0.;
    transformMatrix[4] = 1.;
    transformMatrix[5] = 0.;
    transformMatrix[6] = 0.;
    transformMatrix[7] = 0.;
    transformMatrix[8] = 1.;

    return true;
}
#endif


void
FrameRangePlugin::changedParam(const OFX::InstanceChangedArgs &/*args*/, const std::string &paramName)
{
    if (paramName == kParamReset) {
        OfxRangeD range = _srcClip->getFrameRange();
        _frameRange->setValue((int)range.min, (int)range.max);
    }
}

/* override the time domain action, only for the general context */
bool
FrameRangePlugin::getTimeDomain(OfxRangeD &range)
{
    // this should only be called in the general context, ever!
    assert (getContext() == OFX::eContextGeneral);
    int min, max;
    _frameRange->getValue(min, max);
    range.min = min;
    range.max = max;
    return true;
}

using namespace OFX;

mDeclarePluginFactory(FrameRangePluginFactory, {}, {});

void FrameRangePluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts, only general, because the only useful action is getTimeDomain
    desc.addSupportedContext(eContextGeneral);

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
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
#endif
}

void FrameRangePluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum /*context*/)
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
    srcClip->setCanTransform(true);
#endif
    srcClip->addSupportedComponent(ePixelComponentCustom);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
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
    dstClip->setSupportsTiles(kSupportsTiles);
    
    // make some pages and to things in 
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // frameRange
    {
        Int2DParamDescriptor *param = desc.defineInt2DParam(kParamFrameRange);
        param->setLabel(kParamFrameRangeLabel);
        param->setHint(kParamFrameRangeHint);
        param->setDefault(1,1);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    // reset
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamReset);
        param->setLabel(kParamResetLabel);
        param->setHint(kParamResetHint);
        if (page) {
            page->addChild(*param);
        }
    }
}

OFX::ImageEffect* FrameRangePluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new FrameRangePlugin(handle);
}

void getFrameRangePluginID(OFX::PluginFactoryArray &ids)
{
    static FrameRangePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
