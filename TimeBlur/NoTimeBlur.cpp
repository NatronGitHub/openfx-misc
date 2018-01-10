/*
 * OFX NoTimeBlur plugin.
 */

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsCopier.h"

#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "NoTimeBlurOFX"
#define kPluginGrouping "Time"
#define kPluginDescription "Rounds fractional frame numbers to integers. This can be used to avoid computing non-integer frame numbers, and to discretize motion (useful for animated objects). This plug-in is usually inserted upstream from TimeBlur."
#define kPluginIdentifier "net.sf.openfx.NoTimeBlurPlugin"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamRounding "rounding"
#define kParamRoundingLabel "Rounding"
#define kParamRoundingHint "Rounding type/operation to use when blocking fractional frames."
#define kParamRoundingOptionRint "rint", "Round to the nearest integer value.", "rint"
#define kParamRoundingOptionFloor "floor", "Round dound to the nearest integer value.", "floor"
#define kParamRoundingOptionCeil "ceil", "Round up to the nearest integer value.", "ceil"
#define kParamRoundingOptionNone "none", "Do not round.", "none"
#define kParamRoundingDefault eRoundingRint

enum RoundingEnum
{
    eRoundingRint = 0,
    eRoundingFloor,
    eRoundingCeil,
    eRoundingNone,
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class NoTimeBlurPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    NoTimeBlurPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _rounding(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        _rounding = fetchChoiceParam(kParamRounding);
        assert(_rounding);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    ChoiceParam *_rounding;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


// the overridden render function
void
NoTimeBlurPlugin::render(const RenderArguments &args)
{
#ifdef DEBUG
    setPersistentMessage(Message::eMessageError, "", "OFX Host should not render");
    throwSuiteStatusException(kOfxStatFailed);
#endif

    const double time = args.time;

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    RoundingEnum rounding = (RoundingEnum)_rounding->getValueAtTime(time);
    double srcTime = time;
    switch (rounding) {
    case eRoundingRint:
        srcTime = std::floor(time + 0.5);
        break;
    case eRoundingFloor:
        srcTime = std::floor(time);
        break;
    case eRoundingCeil:
        srcTime = std::ceil(time);
        break;
    case eRoundingNone:
        break;
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(srcTime) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    copyPixels( *this, args.renderWindow, src.get(), dst.get() );
} // NoTimeBlurPlugin::render

bool
NoTimeBlurPlugin::isIdentity(const IsIdentityArguments &args,
                             Clip * &identityClip,
                             double &identityTime
                             , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    RoundingEnum rounding = (RoundingEnum)_rounding->getValueAtTime(time);
    double srcTime = time;

    switch (rounding) {
    case eRoundingRint:
        srcTime = std::floor(time + 0.5);
        break;
    case eRoundingFloor:
        srcTime = std::floor(time);
        break;
    case eRoundingCeil:
        srcTime = std::ceil(time);
        break;
    case eRoundingNone:
        break;
    }

    identityClip = _srcClip;
    identityTime = srcTime;

    return true;
}

bool
NoTimeBlurPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                        OfxRectD &rod)
{
    const double time = args.time;
    RoundingEnum rounding = (RoundingEnum)_rounding->getValueAtTime(time);
    double srcTime = time;

    switch (rounding) {
    case eRoundingRint:
        srcTime = std::floor(time + 0.5);
        break;
    case eRoundingFloor:
        srcTime = std::floor(time);
        break;
    case eRoundingCeil:
        srcTime = std::ceil(time);
        break;
    case eRoundingNone:
        break;
    }

    rod = _srcClip->getRegionOfDefinition(srcTime);

    return true;
}

mDeclarePluginFactory(NoTimeBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
NoTimeBlurPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
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
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NUKE
    // ask the host to render all planes
    desc.setPassThroughForNotProcessedPlanes(ePassThroughLevelRenderAllRequestedPlanes);
#endif
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
NoTimeBlurPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentNone);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentNone);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // rounding
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamRounding);
        param->setLabel(kParamRoundingLabel);
        param->setHint(kParamRoundingHint);
        assert(param->getNOptions() == eRoundingRint);
        param->appendOption(kParamRoundingOptionRint);
        assert(param->getNOptions() == eRoundingFloor);
        param->appendOption(kParamRoundingOptionFloor);
        assert(param->getNOptions() == eRoundingCeil);
        param->appendOption(kParamRoundingOptionCeil);
        assert(param->getNOptions() == eRoundingNone);
        param->appendOption(kParamRoundingOptionNone);
        param->setDefault(kParamRoundingDefault);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // NoTimeBlurPluginFactory::describeInContext

ImageEffect*
NoTimeBlurPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new NoTimeBlurPlugin(handle);
}

static NoTimeBlurPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
