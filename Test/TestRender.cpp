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
 * OFX TestRender plugin.
 */

#ifdef DEBUG

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include <sstream> // stringstream

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "TestRenderOFX"
#define kPluginGrouping "Other/Test"
#define kPluginDescription \
    "Test rendering by the host.\n" \
    "This plugin paints pixel dots on the upper left and lower right parts of the input image. " \
    "The dots are spaced by 1 pixel at each render scale. " \
    "White dots are painted at coordinates which are multiples of 10. " \
    "Color dots are painted are coordinates which are multiples of 2, " \
    "and the color depends on the render scale " \
    "(respectively cyan, magenta, yellow, red, green, blue for levels 0, 1, 2, 3, 4, 5)." \
    "Several versions of this plugin are available, with support for tiling enabled/disabled (TiOK/TiNo), " \
    "multiresolution enabled/disabled (MrOK/MrNo), render scale support enabled/disabled (RsOK/RsNo)." \
    "The effect returns a region-dependent value for isIdentity."

#define kPluginIdentifier "net.sf.openfx.TestRender"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false

#define kParamColor0      "color0"
#define kParamColor0Label "Color 0"
#define kParamColor0Hint  "Color for render scale level 0"
#define kParamColor1      "color1"
#define kParamColor1Label "Color 1"
#define kParamColor1Hint  "Color for render scale level 1"
#define kParamColor2      "color2"
#define kParamColor2Label "Color 2"
#define kParamColor2Hint  "Color for render scale level 2"
#define kParamColor3      "color3"
#define kParamColor3Label "Color 3"
#define kParamColor3Hint  "Color for render scale level 3"
#define kParamColor4      "color4"
#define kParamColor4Label "Color 4"
#define kParamColor4Hint  "Color for render scale level 4"
#define kParamColor5      "color5"
#define kParamColor5Label "Color 5"
#define kParamColor5Hint  "Color for render scale level 5"

#define kParamClipInfo      "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint  "Display information about the inputs"

#define kParamIdentityEven "identityEven"
#define kParamIdentityEvenLabel "Identity for even levels"
#define kParamIdentityEvenHint "Even levels of the render scale (including full resolution) return the input image (isIdentity is true for these levels)"

#define kParamIdentityOdd "identityOdd"
#define kParamIdentityOddLabel "Identity for odd levels"
#define kParamIdentityOddHint "Odd levels of the render scale return the input image (isIdentity is true for these levels"

#define kParamForceCopy "forceCopy"
#define kParamForceCopyLabel "Force Copy"
#define kParamForceCopyHint "Force copy from input to output (isIdentity always returns false)"


// Base class for the RGBA and the Alpha processor
class TestRenderBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:
    /** @brief no arg ctor */
    TestRenderBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(double mix)
    {
        _mix = mix;
    }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class ImageTestRenderer
    : public TestRenderBase
{
public:
    // ctor
    ImageTestRenderer(ImageEffect &instance)
        : TestRenderBase(instance)
    {
    }

private:
    // and do some processing
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
#pragma message WARN("TODO: write the render function as described in the plugin help")
        float tmpPix[nComponents];
        //const OfxRectI srcRoD = _srcImg->getRegionOfDefinition();
        //int xmid = srcRoD.x1 + (srcRoD.x2-srcRoD.x1)/2;
        //int ymid = srcRoD.y1 + (srcRoD.y2-srcRoD.y1)/2;

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                //if ((x < xmid && y < ymid) || (x >= xmid && y >= ymid))
                // do we have a source image to scale up
                if (srcPix) {
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = (maxValue - srcPix[c]);
                    }
                } else {
                    // no src pixel here, be black and transparent
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = (maxValue - 0.f);
                    }
                }
                ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
class TestRenderPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    TestRenderPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _color[0] = fetchRGBAParam(kParamColor0);
        _color[1] = fetchRGBAParam(kParamColor1);
        _color[2] = fetchRGBAParam(kParamColor2);
        _color[3] = fetchRGBAParam(kParamColor3);
        _color[4] = fetchRGBAParam(kParamColor4);
        _color[5] = fetchRGBAParam(kParamColor5);
        assert(_color[0] && _color[1] && _color[2] && _color[3] && _color[4] && _color[5]);

        _identityEven = fetchBooleanParam(kParamIdentityEven);
        _identityOdd = fetchBooleanParam(kParamIdentityOdd);
        _forceCopy = fetchBooleanParam(kParamForceCopy);
        assert(_identityEven && _identityOdd && _forceCopy);

        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(TestRenderBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime
#ifdef OFX_EXTENSIONS_NUKE
                            , int& view, std::string& plane
#endif
                            ) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    RGBAParam* _color[6];
    BooleanParam* _identityEven;
    BooleanParam* _identityOdd;
    BooleanParam* _forceCopy;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
void
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::setupAndProcess(TestRenderBase &processor,
                                                                                               const RenderArguments &args)
{
    // get a dst image
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif

    // fetch main input image
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    // make sure bit depths are sane
    if ( src.get() ) {
        assert(_srcClip);
        checkBadRenderScaleOrField(src, args);
#     ifndef NDEBUG
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
#     endif
        OfxRectI srcRod; // = src->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        Coords::toPixelEnclosing(_srcClip->getRegionOfDefinition(args.time), args.renderScale, _srcClip->getPixelAspectRatio(), &srcRod);
        const OfxRectI& srcBounds = src->getBounds();
        OfxRectI dstRod; // = dst->getRegionOfDefinition(); //  Nuke's image RoDs are wrong
        Coords::toPixelEnclosing(_dstClip->getRegionOfDefinition(args.time), args.renderScale, _dstClip->getPixelAspectRatio(), &dstRod);
        const OfxRectI& dstBounds = dst->getBounds();

        if (!supportsTiles) {
            // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsTiles
            //  If a clip or plugin does not support tiled images, then the host should supply full RoD images to the effect whenever it fetches one.
            if ( src.get() ) {
                assert(srcRod.x1 == srcBounds.x1);
                assert(srcRod.x2 == srcBounds.x2);
                assert(srcRod.y1 == srcBounds.y1);
                assert(srcRod.y2 == srcBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
            }
            assert(dstRod.x1 == dstBounds.x1);
            assert(dstRod.x2 == dstBounds.x2);
            assert(dstRod.y1 == dstBounds.y1);
            assert(dstRod.y2 == dstBounds.y2); // crashes on Natron if kSupportsTiles=0 & kSupportsMultiResolution=1
        }
        if (!supportsMultiResolution) {
            // http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#kOfxImageEffectPropSupportsMultiResolution
            //   Multiple resolution images mean...
            //    input and output images can be of any size
            //    input and output images can be offset from the origin
            if ( src.get() ) {
                assert(srcRod.x1 == 0);
                assert(srcRod.y1 == 0);
                assert(srcRod.x1 == dstRod.x1);
                assert(srcRod.x2 == dstRod.x2);
                assert(srcRod.y1 == dstRod.y1);
                assert(srcRod.y2 == dstRod.y2); // crashes on Natron if kSupportsMultiResolution=0
            }
        }
    }

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);

    // do we do masking
    if (doMasking) {
        checkBadRenderScaleOrField(mask, args);
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(mix);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // >::setupAndProcess

// the internal render function
template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
template<int nComponents>
void
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::renderInternal(const RenderArguments &args,
                                                                                              BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ImageTestRenderer<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ImageTestRenderer<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ImageTestRenderer<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
void
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::render(const RenderArguments &args)
{
# ifndef NDEBUG
    if ( !supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# endif

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
bool
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::isIdentity(const IsIdentityArguments &args,
                                                                                          Clip * &identityClip,
                                                                                          double & /*identityTime*/
#ifdef OFX_EXTENSIONS_NUKE
                                                                                          , int& /*view*/, std::string& /*plane*/
#endif
                                                                                          )
{
# ifndef NDEBUG
    if ( !supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# endif

    bool forceCopy;
    _forceCopy->getValueAtTime(args.time, forceCopy);
    if (forceCopy) {
        return false;
    }

    double mix;
    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }
#if 0
    bool identityEven, identityOdd;
    _identityEven->getValueAtTime(args.time, identityEven);
    _identityOdd->getValueAtTime(args.time, identityOdd);
    unsigned int mipMapLevel = Coords::mipmapLevelFromScale(args.renderScale.x);
    bool isOdd = bool(mipMapLevel & 1);
    if ( (identityEven && !isOdd) || (identityOdd && isOdd) ) {
        identityClip = _srcClip;

        return true;
    }

    // if renderWindow is fully in the lower left or upper right corner, return true
    const OfxRectD rod = _dstClip->getRegionOfDefinition(args.time);
    OfxRectI roi = args.renderWindow;
    OfxRectD roiCanonical;
    roiCanonical.x1 = roi.x1 / args.renderScale.x;
    roiCanonical.x2 = roi.x2 / args.renderScale.x;
    roiCanonical.y1 = roi.y1 / args.renderScale.y;
    roiCanonical.y2 = roi.y2 / args.renderScale.y;
    double xmid = rod.x1 + (rod.x2 - rod.x1) / 2;
    double ymid = rod.y1 + (rod.y2 - rod.y1) / 2;
    if ( ( (roiCanonical.x2 < xmid) && (roiCanonical.y2 < ymid) ) ||
         ( ( roiCanonical.x2 >= xmid) && ( roiCanonical.y2 >= ymid) ) ) {
        identityClip = _srcClip;

        return true;
    }
#endif

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // >::isIdentity

static const char*
bitDepthString(BitDepthEnum bitDepth)
{
    switch (bitDepth) {
    case eBitDepthUByte:

        return "8u";
    case eBitDepthUShort:

        return "16u";
    case eBitDepthHalf:

        return "16f";
    case eBitDepthFloat:

        return "32f";
    case eBitDepthCustom:

        return "x";
    case eBitDepthNone:

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

    return s.replace(s.find(prefix), prefix.length(), "");
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

template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
void
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::changedParam(const InstanceChangedArgs &args,
                                                                                            const std::string &paramName)
{
# ifndef NDEBUG
    if ( !supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# endif

    if (paramName == kParamClipInfo) {
        std::ostringstream oss;
        oss << "Clip Info:\n\n";
        oss << "Input: ";
        if (!_srcClip) {
            oss << "N/A";
        } else {
            Clip &c = *_srcClip;
            oss << pixelComponentString( c.getPixelComponentsProperty() );
            oss << bitDepthString( c.getPixelDepth() );
            oss << " (unmapped: ";
            oss << pixelComponentString( c.getUnmappedPixelComponentsProperty() );
            oss << bitDepthString( c.getUnmappedPixelDepth() );
            oss << ")\npremultiplication: ";
            oss << premultString( c.getPreMultiplication() );
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString( c.getPixelOrder() );
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString( c.getFieldOrder() );
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
#ifdef OFX_EXTENSIONS_NATRON
            oss << "\nformat: ";
            OfxRectI format;
            c.getFormat(format);
            oss << format.x2 - format.x1 << 'x' << format.y2 - format.y1;
            if ( (format.x1 != 0) && (format.y1 != 0) ) {
                if (format.x1 < 0) {
                    oss << format.x1;
                } else {
                    oss << '+' << format.x1;
                }
                if (format.y1 < 0) {
                    oss << format.y1;
                } else {
                    oss << '+' << format.y1;
                }
            }
#endif
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
        if (!_dstClip) {
            oss << "N/A";
        } else {
            Clip &c = *_dstClip;
            oss << pixelComponentString( c.getPixelComponentsProperty() );
            oss << bitDepthString( c.getPixelDepth() );
            oss << " (unmapped: ";
            oss << pixelComponentString( c.getUnmappedPixelComponentsProperty() );
            oss << bitDepthString( c.getUnmappedPixelDepth() );
            oss << ")\npremultiplication: ";
            oss << premultString( c.getPreMultiplication() );
#ifdef OFX_EXTENSIONS_VEGAS
            oss << "\npixel order: ";
            oss << pixelOrderString( c.getPixelOrder() );
#endif
            oss << "\nfield order: ";
            oss << fieldOrderString( c.getFieldOrder() );
            oss << "\n";
            oss << (c.isConnected() ? "connected" : "not connected");
            oss << "\n";
            oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
#ifdef OFX_EXTENSIONS_NATRON
            oss << "\nformat: ";
            OfxRectI format;
            c.getFormat(format);
            oss << format.x2 - format.x1 << 'x' << format.y2 - format.y1;
            if ( (format.x1 != 0) && (format.y1 != 0) ) {
                if (format.x1 < 0) {
                    oss << format.x1;
                } else {
                    oss << '+' << format.x1;
                }
                if (format.y1 < 0) {
                    oss << format.y1;
                } else {
                    oss << '+' << format.y1;
                }
            }
#endif
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

        sendMessage( Message::eMessageMessage, "", oss.str() );
    }
} // >::changedParam

template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
bool
TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                                                                                     OfxRectD & /*rod*/)
{
# ifndef NDEBUG
    if ( !supportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
# endif
    
    // use the default RoD
    return false;
}

//mDeclarePluginFactory(TestRenderPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
class TestRenderPluginFactory
    : public PluginFactoryHelper<TestRenderPluginFactory<supportsTiles, supportsMultiResolution, supportsRenderScale> >
{
public:
    TestRenderPluginFactory(const std::string& id,
                            unsigned int verMaj,
                            unsigned int verMin) : PluginFactoryHelper<TestRenderPluginFactory<supportsTiles, supportsMultiResolution, supportsRenderScale> >(id, verMaj, verMin) {}

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();}
    //virtual void unload() {};
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};


template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
void
TestRenderPluginFactory<supportsTiles, supportsMultiResolution, supportsRenderScale>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    std::string name = ( std::string(kPluginName) + "_Ti" + (supportsTiles ? "OK" : "No")
                         +                          "_Mr" + (supportsMultiResolution ? "OK" : "No")
                         +                          "_Rs" + (supportsRenderScale ? "OK" : "No") );

    desc.setLabel(name);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedContext(eContextGenerator);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(supportsMultiResolution);
    desc.setSupportsTiles(supportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);

    desc.setRenderThreadSafety(eRenderFullySafe);
}

template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
void
TestRenderPluginFactory<supportsTiles, supportsMultiResolution, supportsRenderScale>::describeInContext(ImageEffectDescriptor &desc,
                                                                                                        ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(supportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(supportsTiles);

    if (context != eContextGenerator) {
        ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context != eContextPaint) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(supportsTiles);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // color0
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor0);
        param->setLabel(kParamColor0Label);
        param->setHint(kParamColor0Hint);
        param->setDefault(0.0, 1.0, 1.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color1
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor1);
        param->setLabel(kParamColor1Label);
        param->setHint(kParamColor1Hint);
        param->setDefault(1.0, 0.0, 1.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color2
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor2);
        param->setLabel(kParamColor2Label);
        param->setHint(kParamColor2Hint);
        param->setDefault(1.0, 1.0, 0.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color3
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor3);
        param->setLabel(kParamColor3Label);
        param->setHint(kParamColor3Hint);
        param->setDefault(1.0, 0.0, 0.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color4
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor4);
        param->setLabel(kParamColor4Label);
        param->setHint(kParamColor4Hint);
        param->setDefault(0.0, 1.0, 0.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // color5
    {
        RGBAParamDescriptor *param = desc.defineRGBAParam(kParamColor5);
        param->setLabel(kParamColor5Label);
        param->setHint(kParamColor5Hint);
        param->setDefault(0.0, 0.0, 1.0, 1.0);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    // identityEven
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamIdentityEven);
        param->setLabel(kParamIdentityEvenLabel);
        param->setHint(kParamIdentityEvenHint);
        param->setDefault(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // identityOdd
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamIdentityOdd);
        param->setLabel(kParamIdentityOddLabel);
        param->setHint(kParamIdentityOddHint);
        param->setDefault(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // forceCopy
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamForceCopy);
        param->setLabel(kParamForceCopyLabel);
        param->setHint(kParamForceCopyHint);
        param->setDefault(false);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }

    // clipInfo
    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

template<bool supportsTiles, bool supportsMultiResolution, bool supportsRenderScale>
ImageEffect*
TestRenderPluginFactory<supportsTiles, supportsMultiResolution, supportsRenderScale>::createInstance(OfxImageEffectHandle handle,
                                                                                                     ContextEnum /*context*/)
{
    return new TestRenderPlugin<supportsTiles, supportsMultiResolution, supportsRenderScale>(handle);
}

static TestRenderPluginFactory<true, true, true> p1(kPluginIdentifier "_TiOK_MrOK_RsOK", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<true, true, false> p2(kPluginIdentifier "_TiOK_MrOK_RsNo", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<true, false, true> p3(kPluginIdentifier "_TiOK_MrNo_RsOK", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<true, false, false> p4(kPluginIdentifier "_TiOK_MrNo_RsNo", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<false, true, true> p5(kPluginIdentifier "_TiNo_MrOK_RsOK", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<false, true, false> p6(kPluginIdentifier "_TiNo_MrOK_RsNo", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<false, false, true> p7(kPluginIdentifier "_TiNo_MrNo_RsOK", kPluginVersionMajor, kPluginVersionMinor);
static TestRenderPluginFactory<false, false, false> p8(kPluginIdentifier "_TiNo_MrNo_RsNo", kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)
mRegisterPluginFactoryInstance(p5)
mRegisterPluginFactoryInstance(p6)
mRegisterPluginFactoryInstance(p7)
mRegisterPluginFactoryInstance(p8)

OFXS_NAMESPACE_ANONYMOUS_EXIT

#endif // DEBUG
