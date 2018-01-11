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
 * OFX ColorSuppress plugin.
 */

#include <cmath>
#include <cstring>
#include <cfloat> // DBL_MAX

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsLut.h"
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
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
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


#define kParamLuminanceMath "luminanceMath"
#define kParamLuminanceMathLabel "Luminance Math"
#define kParamLuminanceMathHint "Formula used to compute luminance from RGB values."
#define kParamLuminanceMathOptionRec709 "Rec. 709", "Use Rec. 709 (0.2126r + 0.7152g + 0.0722b).", "rec709"
#define kParamLuminanceMathOptionRec2020 "Rec. 2020", "Use Rec. 2020 (0.2627r + 0.6780g + 0.0593b).", "rec2020"
#define kParamLuminanceMathOptionACESAP0 "ACES AP0", "Use ACES AP0 (0.3439664498r + 0.7281660966g + -0.0721325464b).", "acesap0"
#define kParamLuminanceMathOptionACESAP1 "ACES AP1", "Use ACES AP1 (0.2722287168r +  0.6740817658g +  0.0536895174b).", "acesap1"
#define kParamLuminanceMathOptionCcir601 "CCIR 601", "Use CCIR 601 (0.2989r + 0.5866g + 0.1145b).", "ccir601"
#define kParamLuminanceMathOptionAverage "Average", "Use average of r, g, b.", "average"
#define kParamLuminanceMathOptionMaximum "Max", "Use max or r, g, b.", "max"

enum LuminanceMathEnum
{
    eLuminanceMathRec709,
    eLuminanceMathRec2020,
    eLuminanceMathACESAP0,
    eLuminanceMathACESAP1,
    eLuminanceMathCcir601,
    eLuminanceMathAverage,
    eLuminanceMathMaximum,
};

#define kParamPremultChanged "premultChanged"


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
                   double mix)
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
    }

protected:
    double luminance (double r,
                      double g,
                      double b)
    {
        switch (_luminanceMath) {
        case eLuminanceMathRec709:
        default:

            return Color::rgb709_to_y(r, g, b);

        case eLuminanceMathRec2020: // https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2087-0-201510-I!!PDF-E.pdf

            return Color::rgb2020_to_y(r, g, b);
        case eLuminanceMathACESAP0: // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

            return Color::rgbACESAP0_to_y(r, g, b);
        case eLuminanceMathACESAP1: // https://en.wikipedia.org/wiki/Academy_Color_Encoding_System#Converting_ACES_RGB_values_to_CIE_XYZ_values

            return Color::rgbACESAP1_to_y(r, g, b);
        case eLuminanceMathCcir601:

            return 0.2989 * r + 0.5866 * g + 0.1145 * b;
        case eLuminanceMathAverage:

            return (r + g + b) / 3;
        case eLuminanceMathMaximum:

            return std::max(std::max(r, g), b);
        }
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

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];
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
                        double diff1 = ( output.r - std::max( output.g, output.b ) ) * _redSuppress;
                        output.r -= diff1;
                        pixelModified += std::abs(diff1);
                    }
                }

                // Green
                if (_greenSuppress != 0.0) {
                    if ( (output.g > output.b) && (output.g > output.r) ) {
                        double diff1 = ( output.g - std::max( output.b, output.r ) ) * _greenSuppress;
                        output.g -= diff1;
                        pixelModified += std::abs(diff1);
                    }
                }

                // Blue
                if (_blueSuppress != 0.0) {
                    if ( (output.b > output.g) && (output.b > output.r) ) {
                        double diff1 = ( output.b - std::max( output.g, output.r ) ) * _blueSuppress;
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
                // fill output Alpha
                if ( (_outputMode == eOutputModeAlpha) || (_outputMode == eOutputModeAlphaImage) ) {
                    tmpPix[3] = pixelModified;
                } else {
                    tmpPix[3] = unpPix[3];
                }

                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
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
    ColorSuppressPlugin(OfxImageEffectHandle handle)
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
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         (dst->getRenderScale().y != args.renderScale.y) ||
         ( (dst->getField() != eFieldNone) /* for DaVinci Resolve */ && (dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             (src->getRenderScale().y != args.renderScale.y) ||
             ( (src->getField() != eFieldNone) /* for DaVinci Resolve */ && (src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 (mask->getRenderScale().y != args.renderScale.y) ||
                 ( (mask->getField() != eFieldNone) /* for DaVinci Resolve */ && (mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert = _maskInvert->getValueAtTime(time);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    double redSuppress = _redSuppress->getValueAtTime(time);
    double blueSuppress = _blueSuppress->getValueAtTime(time);
    double greenSuppress = _greenSuppress->getValueAtTime(time);
    double cyanSuppress = _cyanSuppress->getValueAtTime(time);
    double magentaSuppress = _magentaSuppress->getValueAtTime(time);
    double yellowSuppress = _yellowSuppress->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    bool preserveLuma = (outputMode != eOutputModeAlpha) && _preserveLuma->getValueAtTime(time);
    LuminanceMathEnum luminanceMath = (LuminanceMathEnum)_luminanceMath->getValueAtTime(time);
    bool premult = _premult->getValueAtTime(time);
    int premultChannel = _premultChannel->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    processor.setValues(redSuppress, blueSuppress, greenSuppress,
                        cyanSuppress, magentaSuppress, yellowSuppress,
                        outputMode,
                        preserveLuma,
                        luminanceMath,
                        premult, premultChannel, mix);

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

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);
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

mDeclarePluginFactory(ColorSuppressPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ColorSuppressPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

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

void
ColorSuppressPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                              ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
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

ImageEffect*
ColorSuppressPluginFactory::createInstance(OfxImageEffectHandle handle,
                                           ContextEnum /*context*/)
{
    return new ColorSuppressPlugin(handle);
}

static ColorSuppressPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
