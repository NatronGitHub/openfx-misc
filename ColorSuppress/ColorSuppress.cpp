/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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
 * OFX ColorSuppress plugin.
 */

#include <cmath>
#include <cstring>
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
#include "ofxsLuminanceMath.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorSuppress"
#define kPluginGrouping "Color"
#define kPluginDescription \
    "Remove a color or tint from an image.\n"                              \
    "The effect can either modify the color and/or extract the amount of color " \
    "and store it in the alpha channel. " \
    "It can be used to fix the despill or extract a mask from a color."
#define kPluginIdentifier "net.sf.openfx.ColorSuppress"

// History:
// version 1.0: initial version
// version 1.1 (kept for backward compatibility): premultiplying an infinite or NaN colour by alpha 0 gives 0, not NaN (SupportExt #73)
// version 2.0: in "Alpha" and "Image and Alpha" output modes, premultiply RGB by the source alpha
//              (version 1 premultiplied RGB by the suppress mask);
//              premultiplying an infinite or NaN colour by alpha 0 gives 0, not NaN (SupportExt #73)
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamRedSuppress "redSuppress"
#define kParamRedSuppressLabel "Red"
#define kParamRedSuppressHint "Fraction of red to suppress."

#define kParamGreenSuppress "greenSuppress"
#define kParamGreenSuppressLabel "Green"
#define kParamGreenSuppressHint "Fraction of green to suppress."

#define kParamBlueSuppress "blueSuppress"
#define kParamBlueSuppressLabel "Blue"
#define kParamBlueSuppressHint "Fraction of blue to suppress."

#define kParamCyanSuppress "cyanSuppress"
#define kParamCyanSuppressLabel "Cyan"
#define kParamCyanSuppressHint "Fraction of cyan to suppress."

#define kParamMagentaSuppress "magentaSuppress"
#define kParamMagentaSuppressLabel "Magenta"
#define kParamMagentaSuppressHint "Fraction of magenta to suppress."

#define kParamYellowSuppress "yellowSuppress"
#define kParamYellowSuppressLabel "Yellow"
#define kParamYellowSuppressHint "Fraction of yellow to suppress."

#define kParamOutputMode "outputMode"
#define kParamOutputModeLabel "Output"
#define kParamOutputModeHint "Suppress mode."
#define kParamOutputModeOptionImage "Image", "Suppress color from the image.", "image"
#define kParamOutputModeOptionAlpha "Alpha", "Only store the suppress mask in the Alpha channel.", "alpha"
#define kParamOutputModeOptionImageAndAlpha "Image and Alpha", "Suppress the color from the image and store the suppress mask in the Alpha channel.", "both"

enum OutputModeEnum
{
    eOutputModeImage = 0,
    eOutputModeAlpha,
    eOutputModeAlphaImage
};

#define kParamPreserveLuma "preserveLuma"
#define kParamPreserveLumaLabel "Preserve Luminance"
#define kParamPreserveLumaHint "Preserve image luminosity."


#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values."

#define kParamPremultChanged "premultChanged"

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


class ColorSuppressProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    double _redSuppress;
    double _blueSuppress;
    double _greenSuppress;
    double _cyanSuppress;
    double _magentaSuppress;
    double _yellowSuppress;
    OutputModeEnum _outputMode;
    bool _preserveLuma;
    LuminanceMathEnum _luminanceMath;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _premultByMask; // version 1: in Alpha modes, premultiply RGB by the suppress mask instead of the source alpha

public:

    ColorSuppressProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _redSuppress(0.)
        , _blueSuppress(0.)
        , _greenSuppress(0.)
        , _cyanSuppress(0.)
        , _magentaSuppress(0.)
        , _yellowSuppress(0.)
        , _outputMode(eOutputModeImage)
        , _preserveLuma(false)
        , _luminanceMath(eLuminanceMathRec709)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _premultByMask(false)
    {
    }

    void setSrcImg(const Image *v)
    {
        _srcImg = v;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert)
    {
        _maskImg = v; _maskInvert = maskInvert;
    }

    void doMasking(bool v)
    {
        _doMasking = v;
    }

    void setValues(double redSuppress,
                   double blueSuppress,
                   double greenSuppress,
                   double cyanSuppress,
                   double magentaSuppress,
                   double yellowSuppress,
                   OutputModeEnum outputMode,
                   bool preserveLuma,
                   LuminanceMathEnum luminanceMath,
                   bool premult,
                   int premultChannel,
                   double mix,
                   bool premultByMask)
    {
        _redSuppress = redSuppress;
        _blueSuppress = blueSuppress;
        _greenSuppress = greenSuppress;
        _cyanSuppress = cyanSuppress;
        _magentaSuppress = magentaSuppress;
        _yellowSuppress = yellowSuppress;
        _outputMode = outputMode;
        _preserveLuma = preserveLuma;
        _luminanceMath = luminanceMath;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _premultByMask = premultByMask;
    }

protected:
    template<typename T>
    T
    luminance(T r,
              T g,
              T b)
    {
        return OFX::luminance(r, g, b, _luminanceMath);
    }

    double luminance(const OfxRGBAColourD &c)
    {
        return luminance(c.r, c.g, c.b);
    }
};


template <class PIX, int nComponents, int maxValue>
class ColorSuppressProcessor
    : public ColorSuppressProcessorBase
{
public:
    ColorSuppressProcessor(ImageEffect &instance)
        : ColorSuppressProcessorBase(instance)
    {
    }

private:

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4] = {0.f, 0.f, 0.f, 0.f};
        float tmpPix[4] = {0.f, 0.f, 0.f, 0.f};
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);

                // Processing:
                // process unpPix, put output in tmpPix
                double pixelModified = 0;
                OfxRGBAColourD input, output;
                input.r = unpPix[0];
                input.g = unpPix[1];
                input.b = unpPix[2];
                input.a = unpPix[3];
                output = input;
                double luma1 = .0;
                if (_preserveLuma == true) {
                    luma1 = luminance( input );
                }

                // Yellow
                if (_yellowSuppress != 0.0) {
                    if ( (output.b < output.g) && (output.b < output.r) ) {
                        double diff1 = (output.g - output.b) * _yellowSuppress;
                        double diff2 = (output.r - output.b) * _yellowSuppress;
                        if (diff1 > diff2) {
                            output.g -= diff2;
                            output.r -= diff2;
                            pixelModified += std::abs(diff2);
                        } else {
                            output.g -= diff1;
                            output.r -= diff1;
                            pixelModified += std::abs(diff1);
                        }
                    }
                }

                // Magenta
                if (_magentaSuppress != 0.0) {
                    if ( (output.g < output.b) && (output.g < output.r) ) {
                        double diff1 = (output.b - output.g) * _magentaSuppress;
                        double diff2 = (output.r - output.g) * _magentaSuppress;
                        if (diff1 > diff2) {
                            output.b -= diff2;
                            output.r -= diff2;
                            pixelModified += std::abs(diff2);
                        } else {
                            output.b -= diff1;
                            output.r -= diff1;
                            pixelModified += std::abs(diff1);
                        }
                    }
                }

                // Cyan
                if (_cyanSuppress != 0.0) {
                    if ( (output.r < output.g) && (output.r < output.b) ) {
                        double diff1 = (output.g - output.r) * _cyanSuppress;
                        double diff2 = (output.b - output.r) * _cyanSuppress;
                        if (diff1 > diff2) {
                            output.g -= diff2;
                            output.b -= diff2;
                            pixelModified += std::abs(diff2);
                        } else {
                            output.g -= diff1;
                            output.b -= diff1;
                            pixelModified += std::abs(diff1);
                        }
                    }
                }

                // Red
                if (_redSuppress != 0.0) {
                    if ( (output.r > output.g) && (output.r > output.b) ) {
                        double diff1 = ( output.r - (std::max)( output.g, output.b ) ) * _redSuppress;
                        output.r -= diff1;
                        pixelModified += std::abs(diff1);
                    }
                }

                // Green
                if (_greenSuppress != 0.0) {
                    if ( (output.g > output.b) && (output.g > output.r) ) {
                        double diff1 = ( output.g - (std::max)( output.b, output.r ) ) * _greenSuppress;
                        output.g -= diff1;
                        pixelModified += std::abs(diff1);
                    }
                }

                // Blue
                if (_blueSuppress != 0.0) {
                    if ( (output.b > output.g) && (output.b > output.r) ) {
                        double diff1 = ( output.b - (std::max)( output.g, output.r ) ) * _blueSuppress;
                        output.b -= diff1;
                        pixelModified += std::abs(diff1);
                    }
                }
                // fill output RGB
                if (_outputMode == eOutputModeAlpha) {
                    tmpPix[0] = unpPix[0];
                    tmpPix[1] = unpPix[1];
                    tmpPix[2] = unpPix[2];
                } else {
                    if (_preserveLuma == true) {
                        double luma2 = luminance(output);
                        luma2 = luma1 - luma2;
                        output.r += luma2;
                        output.g += luma2;
                        output.b += luma2;
                    }
                    tmpPix[0] = output.r;
                    tmpPix[1] = output.g;
                    tmpPix[2] = output.b;
                }
                if ( (_outputMode == eOutputModeImage) || _premultByMask ) {
                    // RGB is premultiplied by the output alpha: the source alpha in "Image" mode,
                    // the suppress mask in the Alpha modes of version 1 (kept so that old projects
                    // render the same)
                    tmpPix[3] = (_outputMode == eOutputModeImage) ? unpPix[3] : pixelModified;
                    ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                } else {
                    // "Alpha" or "Image and Alpha": RGB is premultiplied by the source alpha
                    // ("Alpha" leaves it unchanged), alpha is the suppress mask
                    float outPix[4];
                    if (nComponents == 1) {
                        outPix[0] = (float)(pixelModified * maxValue);
                    } else {
                        for (int c = 0; c < 3 && c < nComponents; ++c) {
                            if (_outputMode == eOutputModeAlpha) {
                                outPix[c] = srcPix ? (float)srcPix[c] : 0.f;
                            } else {
                                // premult by alpha <= 0 gives a signed zero, as in ofxsPremult():
                                // an infinite colour (HDR overflow upstream) gave inf * 0 = NaN
                                const float a = _premult ? (std::max)(0.f, unpPix[3]) : 1.f;
                                outPix[c] = (a == 0.f) ? std::copysign(0.f, tmpPix[c]) : (tmpPix[c] * a * maxValue);
                            }
                        }
                        if (nComponents == 4) {
                            outPix[3] = (float)(pixelModified * maxValue);
                        }
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(outPix, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorSuppressPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ColorSuppressPlugin(OfxImageEffectHandle handle,
                        unsigned int majorVersion)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _redSuppress(NULL)
        , _greenSuppress(NULL)
        , _blueSuppress(NULL)
        , _cyanSuppress(NULL)
        , _magentaSuppress(NULL)
        , _yellowSuppress(NULL)
        , _outputMode(NULL)
        , _preserveLuma(NULL)
        , _luminanceMath(NULL)
        , _premultChanged(NULL)
        , _premultByMask(majorVersion < 2)
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

        _outputMode = fetchChoiceParam(kParamOutputMode);
        _redSuppress = fetchDoubleParam(kParamRedSuppress);
        _greenSuppress = fetchDoubleParam(kParamGreenSuppress);
        _blueSuppress = fetchDoubleParam(kParamBlueSuppress);
        _cyanSuppress = fetchDoubleParam(kParamCyanSuppress);
        _magentaSuppress = fetchDoubleParam(kParamMagentaSuppress);
        _yellowSuppress = fetchDoubleParam(kParamYellowSuppress);
        _preserveLuma = fetchBooleanParam(kParamPreserveLuma);
        _luminanceMath = fetchChoiceParam(kParamLuminanceMath);
        assert(_outputMode &&
               _redSuppress && _greenSuppress && _blueSuppress &&
               _cyanSuppress && _magentaSuppress && _yellowSuppress &&
               _preserveLuma && _luminanceMath);

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        // finally
        syncPrivateData();
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ColorSuppressProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief The sync private data action, called when the effect needs to sync any private data to persistent parameters */
    virtual void syncPrivateData(void) OVERRIDE FINAL
    {
        // set visibility
        OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();
        switch (outputMode) {
        case eOutputModeImage:
        case eOutputModeAlphaImage: {
            _preserveLuma->setIsSecretAndDisabled(false);
            bool hasLuma = _preserveLuma->getValue();
            _luminanceMath->setIsSecretAndDisabled(!hasLuma);
            break;
        }
        case eOutputModeAlpha: {
            _preserveLuma->setIsSecretAndDisabled(true);
            _luminanceMath->setIsSecretAndDisabled(true);
            break;
        }
        }
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    DoubleParam* _redSuppress;
    DoubleParam* _greenSuppress;
    DoubleParam* _blueSuppress;
    DoubleParam* _cyanSuppress;
    DoubleParam* _magentaSuppress;
    DoubleParam* _yellowSuppress;
    ChoiceParam* _outputMode;
    BooleanParam* _preserveLuma;
    ChoiceParam* _luminanceMath;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
    bool _premultByMask; // version 1 premultiplied RGB by the suppress mask in Alpha modes, keep it so that old projects render the same
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
ColorSuppressPlugin::setupAndProcess(ColorSuppressProcessorBase &processor,
                                     const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
# ifndef NDEBUG
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
# endif
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            checkBadRenderScaleOrField(mask, args);
        }
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    double redSuppress = _redSuppress->getValueAtTime(time);
    double blueSuppress = _blueSuppress->getValueAtTime(time);
    double greenSuppress = _greenSuppress->getValueAtTime(time);
    double cyanSuppress = _cyanSuppress->getValueAtTime(time);
    double magentaSuppress = _magentaSuppress->getValueAtTime(time);
    double yellowSuppress = _yellowSuppress->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    bool preserveLuma = (outputMode != eOutputModeAlpha) && _preserveLuma->getValueAtTime(time);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    bool premult = _premult ? _premult->getValueAtTime(time) : false;
    int premultChannel = (premult && _premultChannel) ? _premultChannel->getValueAtTime(time) : 3;
    double mix = _mix->getValueAtTime(time);
    processor.setValues(redSuppress, blueSuppress, greenSuppress,
                        cyanSuppress, magentaSuppress, yellowSuppress,
                        outputMode,
                        preserveLuma,
                        luminanceMath,
                        premult, premultChannel, mix,
                        _premultByMask);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ColorSuppressPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorSuppressPlugin::renderInternal(const RenderArguments &args,
                                    BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ColorSuppressProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ColorSuppressProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ColorSuppressProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ColorSuppressPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || !_srcClip->isConnected() || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(OFX_COMPONENTS_OK(dstComponents));
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

bool
ColorSuppressPlugin::isIdentity(const IsIdentityArguments &args,
                                Clip * &identityClip,
                                double & /*identityTime*/
                                , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }

    double redSuppress = _redSuppress->getValueAtTime(time);
    double blueSuppress = _blueSuppress->getValueAtTime(time);
    double greenSuppress = _greenSuppress->getValueAtTime(time);
    double cyanSuppress = _cyanSuppress->getValueAtTime(time);
    double magentaSuppress = _magentaSuppress->getValueAtTime(time);
    double yellowSuppress = _yellowSuppress->getValueAtTime(time);
    if ( (redSuppress != 0.) ||
         (blueSuppress != 0.) ||
         (greenSuppress != 0.) ||
         (cyanSuppress != 0) ||
         (magentaSuppress != 0.) ||
         (yellowSuppress != 0.) ) {
        return false;
    }

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    return false;
} // ColorSuppressPlugin::isIdentity

void
ColorSuppressPlugin::changedClip(const InstanceChangedArgs &args,
                                 const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         (args.reason == eChangeUserEdit) ) {
        switch ( _srcClip->getPreMultiplication() ) {
        case eImageOpaque:
            _premult->setValue(false);
            break;
        case eImagePreMultiplied:
            _premult->setValue(true);
            break;
        case eImageUnPreMultiplied:
            _premult->setValue(false);
            break;
        }
    }
}

void
ColorSuppressPlugin::changedParam(const InstanceChangedArgs &args,
                                  const std::string &paramName)
{
    const double time = args.time;

    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);

        return;
    }
    if ( ( (paramName == kParamOutputMode) || (paramName == kParamPreserveLuma) ) && (args.reason == eChangeUserEdit) ) {
        OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
        switch (outputMode) {
        case eOutputModeImage:
        case eOutputModeAlphaImage: {
            _preserveLuma->setIsSecretAndDisabled(false);
            bool hasLuma = _preserveLuma->getValueAtTime(time);
            _luminanceMath->setIsSecretAndDisabled(!hasLuma);
            break;
        }
        case eOutputModeAlpha: {
            _preserveLuma->setIsSecretAndDisabled(true);
            _luminanceMath->setIsSecretAndDisabled(true);
            break;
        }
        }

        return;
    }
}

/* Override the clip preferences */
void
ColorSuppressPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if ( !_srcClip || !_srcClip->isConnected() || !_dstClip || !_dstClip->isConnected() ) {
        return;
    }

    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();
    switch (outputMode) {
    case eOutputModeAlpha:
    case eOutputModeAlphaImage:
        // Input and Output are RGBA
        clipPreferences.setClipComponents(*_srcClip, ePixelComponentRGBA);
        clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
        break;
    case eOutputModeImage:
        // Output has same components as input
        clipPreferences.setClipComponents( *_dstClip, _srcClip->getPixelComponents() );
        break;
    }
}

mDeclarePluginFactoryVersioned(ColorSuppressPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
ColorSuppressPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsDeprecated(true);
    }

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
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

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA); // we have our own channel selector
#endif
}

template<unsigned int majorVersion>
void
ColorSuppressPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                              ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    //srcClip->addSupportedComponent(ePixelComponentXY);
#endif
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    //dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamRedSuppress);
        param->setLabel(kParamRedSuppressLabel);
        param->setHint(kParamRedSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGreenSuppress);
        param->setLabel(kParamGreenSuppressLabel);
        param->setHint(kParamGreenSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBlueSuppress);
        param->setLabel(kParamBlueSuppressLabel);
        param->setHint(kParamBlueSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamCyanSuppress);
        param->setLabel(kParamCyanSuppressLabel);
        param->setHint(kParamCyanSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamMagentaSuppress);
        param->setLabel(kParamMagentaSuppressLabel);
        param->setHint(kParamMagentaSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor *param = desc.defineDoubleParam(kParamYellowSuppress);
        param->setLabel(kParamYellowSuppressLabel);
        param->setHint(kParamYellowSuppressHint);
        param->setDefault(0.0);
        param->setRange(0, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.0, 1.0);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == eOutputModeImage);
        param->appendOption(kParamOutputModeOptionImage);
        assert(param->getNOptions() == eOutputModeAlpha);
        param->appendOption(kParamOutputModeOptionAlpha);
        assert(param->getNOptions() == eOutputModeAlphaImage);
        param->appendOption(kParamOutputModeOptionImageAndAlpha);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamPreserveLuma);
        param->setLabel(kParamPreserveLumaLabel);
        param->setHint(kParamPreserveLumaHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamLuminanceMath);
        param->setLabel(kParamLuminanceMathLabel);
        param->setHint(kParamLuminanceMathHint);
        assert(param->getNOptions() == eLuminanceMathRec709);
        param->appendOption(kParamLuminanceMathOptionRec709);
        assert(param->getNOptions() == eLuminanceMathRec2020);
        param->appendOption(kParamLuminanceMathOptionRec2020);
        assert(param->getNOptions() == eLuminanceMathACESAP0);
        param->appendOption(kParamLuminanceMathOptionACESAP0);
        assert(param->getNOptions() == eLuminanceMathACESAP1);
        param->appendOption(kParamLuminanceMathOptionACESAP1);
        assert(param->getNOptions() == eLuminanceMathCcir601);
        param->appendOption(kParamLuminanceMathOptionCcir601);
        assert(param->getNOptions() == eLuminanceMathAverage);
        param->appendOption(kParamLuminanceMathOptionAverage);
        assert(param->getNOptions() == eLuminanceMathMaximum);
        param->appendOption(kParamLuminanceMathOptionMaximum);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // ColorSuppressPluginFactory::describeInContext

template<unsigned int majorVersion>
ImageEffect*
ColorSuppressPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                           ContextEnum /*context*/)
{
    return new ColorSuppressPlugin(handle, majorVersion);
}

static ColorSuppressPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
// version 1: in "Alpha" and "Image and Alpha" modes, RGB is premultiplied by the suppress mask
static ColorSuppressPluginFactory<1> p1(kPluginIdentifier, 1);
mRegisterPluginFactoryInstance(p)
mRegisterPluginFactoryInstance(p1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
