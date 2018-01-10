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
 * OFX ColorTransform plugin.
 */

#include <cmath>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginRGBToHSVName "RGBToHSV"
#define kPluginRGBToHSVDescription "Convert from linear RGB to HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978). H is in degrees, S and V are in the same units as RGB. RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToHSVIdentifier "net.sf.openfx.RGBToHSV"

#define kPluginHSVToRGBName "HSVToRGB"
#define kPluginHSVToRGBDescription "Convert from HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978) to linear RGB. H is in degrees, S and V are in the same units as RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginHSVToRGBIdentifier "net.sf.openfx.HSVToRGB"

#define kPluginRGBToHSLName "RGBToHSL"
#define kPluginRGBToHSLDescription "Convert from RGB to HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978). H is in degrees, S and L are in the same units as RGB. RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToHSLIdentifier "net.sf.openfx.RGBToHSL"

#define kPluginHSLToRGBName "HSLToRGB"
#define kPluginHSLToRGBDescription "Convert from HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978) to linear RGB. H is in degrees, S and L are in the same units as RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginHSLToRGBIdentifier "net.sf.openfx.HSLToRGB"

#define kPluginRGBToHSIName "RGBToHSI"
#define kPluginRGBToHSIDescription "Convert from linear RGB to HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992). H is in degrees, S and I are in the same units as RGB. RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion.\n" \
    "The HSI colour space (hue, saturation and intensity) attempts to produce a more intuitive representation of colour. The I axis represents the luminance information. The H and S axes are polar coordinates on the plane orthogonal to I. H is the angle, specified such that red is at zero, green at 120 degrees, and blue at 240 degrees. Hue thus represents what humans implicitly understand as colour. S is the magnitude of the colour vector projected in the plane orthogonal to I, and so represents the difference between pastel colours (low saturation) and vibrant colours (high saturation). The main drawback of this colour space is that hue is undefined if saturation is zero, making error propagation in transformations from the RGB colour space more complicated.\n" \
    "It should also be noted that, although the HSI colour space may be more intuitive, is not \"perceptual\", in the sense that small displacements of equal size in different parts of the colour space will be perceived by human observers as changes of different magnitude. Attempts have been made to define such colour spaces: CIE-LAB and CIE-LUV are two examples."
#define kPluginRGBToHSIIdentifier "net.sf.openfx.RGBToHSI"

#define kPluginHSIToRGBName "HSIToRGB"
#define kPluginHSIToRGBDescription "Convert from HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992) to linear RGB. H is in degrees, S and I are in the same units as RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion.\n" \
    "The HSI colour space (hue, saturation and intensity) attempts to produce a more intuitive representation of colour. The I axis represents the luminance information. The H and S axes are polar coordinates on the plane orthogonal to I. H is the angle, specified such that red is at zero, green at 120 degrees, and blue at 240 degrees. Hue thus represents what humans implicitly understand as colour. S is the magnitude of the colour vector projected in the plane orthogonal to I, and so represents the difference between pastel colours (low saturation) and vibrant colours (high saturation). The main drawback of this colour space is that hue is undefined if saturation is zero, making error propagation in transformations from the RGB colour space more complicated.\n" \
    "It should also be noted that, although the HSI colour space may be more intuitive, is not \"perceptual\", in the sense that small displacements of equal size in different parts of the colour space will be perceived by human observers as changes of different magnitude. Attempts have been made to define such colour spaces: CIE-LAB and CIE-LUV are two examples."
#define kPluginHSIToRGBIdentifier "net.sf.openfx.HSIToRGB"

#define kPluginRGBToYCbCr601Name "RGBToYCbCr601"
#define kPluginRGBToYCbCr601Description "Convert from linear RGB to YCbCr color model (ITU.BT-601). RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYCbCr601Identifier "net.sf.openfx.RGBToYCbCr601"

#define kPluginYCbCrToRGB601Name "YCbCrToRGB601"
#define kPluginYCbCrToRGB601Description "Convert from YCbCr color model (ITU.BT-601) to linear RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYCbCrToRGB601Identifier "net.sf.openfx.YCbCrToRGB601"

#define kPluginRGBToYCbCr709Name "RGBToYCbCr709"
#define kPluginRGBToYCbCr709Description "Convert from linear RGB to YCbCr color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYCbCr709Identifier "net.sf.openfx.RGBToYCbCr709"

#define kPluginYCbCrToRGB709Name "YCbCrToRGB709"
#define kPluginYCbCrToRGB709Description "Convert from YCbCr color model (ITU.BT-709) to linear RGB. RGB is gamma-decompressed using the Rec.709 Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYCbCrToRGB709Identifier "net.sf.openfx.YCbCrToRGB709"

#define kPluginRGBToYPbPr601Name "RGBToYPbPr601"
#define kPluginRGBToYPbPr601Description "Convert from RGB to YPbPr color model (ITU.BT-601). RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYPbPr601Identifier "net.sf.openfx.RGBToYPbPr601"

#define kPluginYPbPrToRGB601Name "YPbPrToRGB601"
#define kPluginYPbPrToRGB601Description "Convert from YPbPr color model (ITU.BT-601) to RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYPbPrToRGB601Identifier "net.sf.openfx.YPbPrToRGB601"

#define kPluginRGBToYPbPr709Name "RGBToYPbPr709"
#define kPluginRGBToYPbPr709Description "Convert from RGB to YPbPr color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYPbPr709Identifier "net.sf.openfx.RGBToYPbPr709"

#define kPluginYPbPrToRGB709Name "YPbPrToRGB709"
#define kPluginYPbPrToRGB709Description "Convert from YPbPr color model (ITU.BT-709) to RGB. RGB is gamma-decompressed using the Rec.709 Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYPbPrToRGB709Identifier "net.sf.openfx.YPbPrToRGB709"

#define kPluginRGBToYUV601Name "RGBToYUV601"
#define kPluginRGBToYUV601Description "Convert from RGB to YUV color model (ITU.BT-601). RGB is gamma-compressed using the sRGB Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYUV601Identifier "net.sf.openfx.RGBToYUV601"

#define kPluginYUVToRGB601Name "YUVToRGB601"
#define kPluginYUVToRGB601Description "Convert from YUV color model (ITU.BT-601) to RGB. RGB is gamma-decompressed using the sRGB Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYUVToRGB601Identifier "net.sf.openfx.YUVToRGB601"

#define kPluginRGBToYUV709Name "RGBToYUV709"
#define kPluginRGBToYUV709Description "Convert from RGB to YUV color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 Opto-Electronic Transfer Function (OETF) before conversion."
#define kPluginRGBToYUV709Identifier "net.sf.openfx.RGBToYUV709"

#define kPluginYUVToRGB709Name "YUVToRGB709"
#define kPluginYUVToRGB709Description "Convert from YUV color model (ITU.BT-709) to RGB. RGB is gamma-decompressed using the Rec.709 Electro-Optical Transfer Function (EOTF) after conversion."
#define kPluginYUVToRGB709Identifier "net.sf.openfx.YUVToRGB709"


#define kPluginRGB709ToXYZName "RGB709ToXYZ"
#define kPluginRGB709ToXYZDescription "Convert from RGB (Rec.709 with D65 illuminant) to XYZ color model. X, Y and Z are in the same units as RGB."
#define kPluginRGB709ToXYZIdentifier "net.sf.openfx.RGB709ToXYZ"

#define kPluginXYZToRGB709Name "XYZToRGB709"
#define kPluginXYZToRGB709Description "Convert from XYZ color model to RGB (Rec.709 with D65 illuminant). X, Y and Z are in the same units as RGB."
#define kPluginXYZToRGB709Identifier "net.sf.openfx.XYZToRGB709"

#define kPluginRGB709ToLabName "RGB709ToLab"
#define kPluginRGB709ToLabDescription "Convert from RGB (Rec.709 with D65 illuminant) to L*a*b color model. L*a*b coordinates are divided by 100 for better visualization."
#define kPluginRGB709ToLabIdentifier "net.sf.openfx.RGB709ToLab"

#define kPluginLabToRGB709Name "LabToRGB709"
#define kPluginLabToRGB709Description "Convert from L*a*b color model to RGB (Rec.709 with D65 illuminant). L*a*b coordinates are divided by 100 for better visualization."
#define kPluginLabToRGB709Identifier "net.sf.openfx.LabToRGB709"

#define kPluginXYZToLabName "XYZToLab"
#define kPluginXYZToLabDescription "Convert from CIE XYZ color space to CIE L*a*b color space. L*a*b coordinates are divided by 100 for better visualization."
#define kPluginXYZToLabIdentifier "net.sf.openfx.XYZToLab"

#define kPluginLabToXYZName "LabToXYZ"
#define kPluginLabToXYZDescription "Convert from CIE L*a*b color space to CIE XYZ color space. L*a*b coordinates are divided by 100 for better visualization."
#define kPluginLabToXYZIdentifier "net.sf.openfx.LabToXYZ"

#define kPluginXYZToxyYName "XYZToxyY"
#define kPluginXYZToxyYDescription "Convert from CIE XYZ color space to CIE xyY color space."
#define kPluginXYZToxyYIdentifier "net.sf.openfx.XYZToxyY"

#define kPluginxyYToXYZName "xyYToXYZ"
#define kPluginxyYToXYZDescription "Convert from CIE xyY color space to CIE XYZ color space."
#define kPluginxyYToXYZIdentifier "net.sf.openfx.xyYToXYZ"

#define kPluginGrouping "Color/Transform"

// history:
// 1.0 initial version
// 2.0 named plugins more consistently, add a few conversions
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamPremultRGBToXXXLabel "Unpremult"
#define kParamPremultRGBToXXXHint \
    "Divide the image by the alpha channel before processing. " \
    "Use if the input images are premultiplied."

#define kParamPremultXXXToRGBLabel "Premult"
#define kParamPremultXXXToRGBHint \
    "Multiply the image by the alpha channel after processing. " \
    "Use to get premultiplied output images."

#define kParamPremultChanged "premultChanged"


enum ColorTransformEnum
{
    eColorTransformRGBToHSV,
    eColorTransformHSVToRGB,
    eColorTransformRGBToHSL,
    eColorTransformHSLToRGB,
    eColorTransformRGBToHSI,
    eColorTransformHSIToRGB,
    eColorTransformRGBToYCbCr601,
    eColorTransformYCbCrToRGB601,
    eColorTransformRGBToYCbCr709,
    eColorTransformYCbCrToRGB709,
    eColorTransformRGBToYPbPr601,
    eColorTransformYPbPrToRGB601,
    eColorTransformRGBToYPbPr709,
    eColorTransformYPbPrToRGB709,
    eColorTransformRGBToYUV601,
    eColorTransformYUVToRGB601,
    eColorTransformRGBToYUV709,
    eColorTransformYUVToRGB709,
    eColorTransformRGB709ToXYZ,
    eColorTransformXYZToRGB709,
    eColorTransformRGB709ToLab,
    eColorTransformLabToRGB709,
    eColorTransformXYZToLab,
    eColorTransformLabToXYZ,
    eColorTransformXYZToxyY,
    eColorTransformxyYToXYZ,
};

#define toRGB(e)   ( (e) == eColorTransformHSVToRGB || \
                     (e) == eColorTransformHSLToRGB || \
                     (e) == eColorTransformHSIToRGB || \
                     (e) == eColorTransformYCbCrToRGB601 || \
                     (e) == eColorTransformYCbCrToRGB709 || \
                     (e) == eColorTransformYPbPrToRGB601 || \
                     (e) == eColorTransformYPbPrToRGB709 || \
                     (e) == eColorTransformYUVToRGB601 || \
                     (e) == eColorTransformYUVToRGB709 || \
                     (e) == eColorTransformXYZToRGB709 || \
                     (e) == eColorTransformLabToRGB709 )

#define fromRGB(e) ( !toRGB(e) && ( (e) != eColorTransformXYZToLab ) && ( (e) != eColorTransformLabToXYZ ) && ( (e) != eColorTransformXYZToxyY ) && ( (e) != eColorTransformxyYToXYZ ) )

class ColorTransformProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    bool _premult;
    int _premultChannel;
    double _mix;

public:

    ColorTransformProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _mix(1.)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(bool premult,
                   int premultChannel)
    {
        _premult = premult;
        _premultChannel = premultChannel;
    }

private:
};


template <class PIX, int nComponents, int maxValue, ColorTransformEnum transform>
class ColorTransformProcessor
    : public ColorTransformProcessorBase
{
public:
    ColorTransformProcessor(ImageEffect &instance)
        : ColorTransformProcessorBase(instance)
    {
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        float unpPix[4];
        float tmpPix[4];
        const bool dounpremult = _premult && fromRGB(transform);
        const bool dopremult = _premult && toRGB(transform);

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, dounpremult, _premultChannel);
                switch (transform) {
                case eColorTransformRGBToHSV:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_hsv(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSVToRGB:
                    Color::hsv_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToHSL:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_hsl(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSLToRGB:
                    Color::hsl_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToHSI:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_hsi(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformHSIToRGB:
                    Color::hsi_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;


                case eColorTransformRGBToYCbCr601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_ycbcr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYCbCrToRGB601:
                    Color::ycbcr_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYCbCr709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_ycbcr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYCbCrToRGB709:
                    Color::ycbcr_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGBToYPbPr601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_ypbpr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYPbPrToRGB601:
                    Color::ypbpr_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYPbPr709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYPbPrToRGB709:
                    Color::ypbpr_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGBToYUV601:
                    unpPix[0] = Color::to_func_srgb(unpPix[0]);
                    unpPix[1] = Color::to_func_srgb(unpPix[1]);
                    unpPix[2] = Color::to_func_srgb(unpPix[2]);
                    Color::rgb_to_yuv601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYUVToRGB601:
                    Color::yuv_to_rgb601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_srgb(tmpPix[0]);
                    tmpPix[1] = Color::from_func_srgb(tmpPix[1]);
                    tmpPix[2] = Color::from_func_srgb(tmpPix[2]);
                    break;

                case eColorTransformRGBToYUV709:
                    unpPix[0] = Color::to_func_Rec709(unpPix[0]);
                    unpPix[1] = Color::to_func_Rec709(unpPix[1]);
                    unpPix[2] = Color::to_func_Rec709(unpPix[2]);
                    Color::rgb_to_yuv709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformYUVToRGB709:
                    Color::yuv_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] = Color::from_func_Rec709(tmpPix[0]);
                    tmpPix[1] = Color::from_func_Rec709(tmpPix[1]);
                    tmpPix[2] = Color::from_func_Rec709(tmpPix[2]);
                    break;

                case eColorTransformRGB709ToXYZ:
                    Color::rgb709_to_xyz(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToRGB709:
                    Color::xyz_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformRGB709ToLab:
                    Color::rgb709_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] /= 100;
                    tmpPix[1] /= 100;
                    tmpPix[2] /= 100;
                    break;

                case eColorTransformLabToRGB709:
                    unpPix[0] *= 100;
                    unpPix[1] *= 100;
                    unpPix[2] *= 100;
                    Color::lab_to_rgb709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToLab:
                    Color::xyz_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    tmpPix[0] /= 100;
                    tmpPix[1] /= 100;
                    tmpPix[2] /= 100;
                    break;

                case eColorTransformLabToXYZ:
                    unpPix[0] *= 100;
                    unpPix[1] *= 100;
                    unpPix[2] *= 100;
                    Color::lab_to_xyz(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                    break;

                case eColorTransformXYZToxyY: {
                    float X = unpPix[0];
                    float Y = unpPix[1];
                    float Z = unpPix[2];
                    float XYZ = X + Y + Z;
                    float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                    tmpPix[0] = X * invXYZ;
                    tmpPix[1] = Y * invXYZ;
                    tmpPix[2] = Y;
                    break;
                }
                case eColorTransformxyYToXYZ: {
                    float x = unpPix[0];
                    float y = unpPix[1];
                    float Y = unpPix[2];
                    float invy = (y <= 0) ? 0. : (1 / y);
                    tmpPix[0] = x * Y * invy;
                    tmpPix[1] = Y;
                    tmpPix[2] = (1 - x - y) * Y * invy;
                    break;
                }
                } // switch
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, dopremult, _premultChannel, x, y, srcPix, /*doMasking=*/ false, /*maskImg=*/ NULL, /*mix=*/ 1.f, /*maskInvert=*/ false, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template <ColorTransformEnum transform>
class ColorTransformPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ColorTransformPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _premultChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        if ( fromRGB(transform) || toRGB(transform) ) {
            _premult = fetchBooleanParam(kParamPremult);
            _premultChannel = fetchChoiceParam(kParamPremultChannel);
            assert(_premult && _premultChannel);
            _premultChanged = fetchBooleanParam(kParamPremultChanged);
            assert(_premultChanged);
        }
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(ColorTransformProcessorBase &, const RenderArguments &args);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::setupAndProcess(ColorTransformProcessorBase &processor,
                                                 const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

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
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
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

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    bool premult = false;
    if (_premult) {
        _premult->getValueAtTime(args.time, premult);
    }
    int premultChannel = 3;
    if (_premultChannel) {
        _premultChannel->getValueAtTime(args.time, premultChannel);
    }

    processor.setValues(premult, premultChannel);
    processor.process();
} // >::setupAndProcess

// the overridden render function
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ColorTransformProcessor<unsigned char, 4, 255, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorTransformProcessor<unsigned short, 4, 65535, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorTransformProcessor<float, 4, 1, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == ePixelComponentRGB);
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ColorTransformProcessor<unsigned char, 3, 255, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            ColorTransformProcessor<unsigned short, 3, 65535, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            ColorTransformProcessor<float, 3, 1, transform> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // >::render

/* Override the clip preferences */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) &&
         _srcClip &&
         ( !_srcClip->isConnected() || (_srcClip->getPixelComponents() ==  ePixelComponentRGBA) ) ) {
        bool premult;
        _premult->getValue(premult);
        // set the premultiplication of _dstClip
        if ( fromRGB(transform) ) {
            // HSV is always unpremultiplied
            clipPreferences.setOutputPremultiplication(eImageUnPreMultiplied);
        } else {
            // RGB
            clipPreferences.setOutputPremultiplication(premult ? eImagePreMultiplied : eImageUnPreMultiplied);
        }
    }
}

template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedClip(const InstanceChangedArgs &args,
                                             const std::string &clipName)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) &&
         (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _premult->setValue(false);
        } else {
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
}

template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedParam(const InstanceChangedArgs &args,
                                              const std::string &paramName)
{
    if ( ( fromRGB(transform) || toRGB(transform) ) && (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

//mDeclarePluginFactory(ColorTransformPluginFactory, {ofxsThreadSuiteCheck();}, {});
template <ColorTransformEnum transform>
class ColorTransformPluginFactory
    : public PluginFactoryHelper<ColorTransformPluginFactory<transform> >
{
public:
    ColorTransformPluginFactory(const std::string& id,
                                unsigned int verMaj,
                                unsigned int verMin) : PluginFactoryHelper<ColorTransformPluginFactory<transform> >(id, verMaj, verMin) {}

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();};
    //virtual void unload() OVERRIDE FINAL {};
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    switch (transform) {
    case eColorTransformRGBToHSV:
        desc.setLabel(kPluginRGBToHSVName);
        desc.setPluginDescription(kPluginRGBToHSVDescription);
        break;

    case eColorTransformHSVToRGB:
        desc.setLabel(kPluginHSVToRGBName);
        desc.setPluginDescription(kPluginHSVToRGBDescription);
        break;

    case eColorTransformRGBToHSL:
        desc.setLabel(kPluginRGBToHSLName);
        desc.setPluginDescription(kPluginRGBToHSLDescription);
        break;

    case eColorTransformHSLToRGB:
        desc.setLabel(kPluginHSLToRGBName);
        desc.setPluginDescription(kPluginHSLToRGBDescription);
        break;

    case eColorTransformRGBToHSI:
        desc.setLabel(kPluginRGBToHSIName);
        desc.setPluginDescription(kPluginRGBToHSIDescription);
        break;

    case eColorTransformHSIToRGB:
        desc.setLabel(kPluginHSIToRGBName);
        desc.setPluginDescription(kPluginHSIToRGBDescription);
        break;

    case eColorTransformRGBToYCbCr601:
        desc.setLabel(kPluginRGBToYCbCr601Name);
        desc.setPluginDescription(kPluginRGBToYCbCr601Description);
        break;

    case eColorTransformYCbCrToRGB601:
        desc.setLabel(kPluginYCbCrToRGB601Name);
        desc.setPluginDescription(kPluginYCbCrToRGB601Description);
        break;

    case eColorTransformRGBToYCbCr709:
        desc.setLabel(kPluginRGBToYCbCr709Name);
        desc.setPluginDescription(kPluginRGBToYCbCr709Description);
        break;

    case eColorTransformYCbCrToRGB709:
        desc.setLabel(kPluginYCbCrToRGB709Name);
        desc.setPluginDescription(kPluginYCbCrToRGB709Description);
        break;

    case eColorTransformRGBToYPbPr601:
        desc.setLabel(kPluginRGBToYPbPr601Name);
        desc.setPluginDescription(kPluginRGBToYPbPr601Description);
        break;

    case eColorTransformYPbPrToRGB601:
        desc.setLabel(kPluginYPbPrToRGB601Name);
        desc.setPluginDescription(kPluginYPbPrToRGB601Description);
        break;

    case eColorTransformRGBToYPbPr709:
        desc.setLabel(kPluginRGBToYPbPr709Name);
        desc.setPluginDescription(kPluginRGBToYPbPr709Description);
        break;

    case eColorTransformYPbPrToRGB709:
        desc.setLabel(kPluginYPbPrToRGB709Name);
        desc.setPluginDescription(kPluginYPbPrToRGB709Description);
        break;

    case eColorTransformRGBToYUV601:
        desc.setLabel(kPluginRGBToYUV601Name);
        desc.setPluginDescription(kPluginRGBToYUV601Description);
        break;

    case eColorTransformYUVToRGB601:
        desc.setLabel(kPluginYUVToRGB601Name);
        desc.setPluginDescription(kPluginYUVToRGB601Description);
        break;

    case eColorTransformRGBToYUV709:
        desc.setLabel(kPluginRGBToYUV709Name);
        desc.setPluginDescription(kPluginRGBToYUV709Description);
        break;

    case eColorTransformYUVToRGB709:
        desc.setLabel(kPluginYUVToRGB709Name);
        desc.setPluginDescription(kPluginYUVToRGB709Description);
        break;

    case eColorTransformRGB709ToXYZ:
        desc.setLabel(kPluginRGB709ToXYZName);
        desc.setPluginDescription(kPluginRGB709ToXYZDescription);
        break;

    case eColorTransformXYZToRGB709:
        desc.setLabel(kPluginXYZToRGB709Name);
        desc.setPluginDescription(kPluginXYZToRGB709Description);
        break;

    case eColorTransformRGB709ToLab:
        desc.setLabel(kPluginRGB709ToLabName);
        desc.setPluginDescription(kPluginRGB709ToLabDescription);
        break;

    case eColorTransformLabToRGB709:
        desc.setLabel(kPluginLabToRGB709Name);
        desc.setPluginDescription(kPluginLabToRGB709Description);
        break;

    case eColorTransformXYZToLab:
        desc.setLabel(kPluginXYZToLabName);
        desc.setPluginDescription(kPluginXYZToLabDescription);
        break;

    case eColorTransformLabToXYZ:
        desc.setLabel(kPluginLabToXYZName);
        desc.setPluginDescription(kPluginLabToXYZDescription);
        break;

    case eColorTransformXYZToxyY:
        desc.setLabel(kPluginXYZToxyYName);
        desc.setPluginDescription(kPluginXYZToxyYDescription);
        break;

    case eColorTransformxyYToXYZ:
        desc.setLabel(kPluginxyYToXYZName);
        desc.setPluginDescription(kPluginxyYToXYZDescription);
        break;
    } // switch
    desc.setPluginGrouping(kPluginGrouping);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
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
    desc.setChannelSelector(ePixelComponentRGB);
#endif
} // >::describe

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describeInContext(ImageEffectDescriptor &desc,
                                                          ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    if ( fromRGB(transform) || toRGB(transform) ) {
        PageParamDescriptor *page = desc.definePageParam("Controls");
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremult);
            if ( fromRGB(transform) ) {
                param->setLabel(kParamPremultRGBToXXXLabel);
                param->setHint(kParamPremultRGBToXXXHint);
            } else {
                param->setLabel(kParamPremultXXXToRGBLabel);
                param->setHint(kParamPremultXXXToRGBHint);
            }
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            param->setAnimates(false);
            desc.addClipPreferencesSlaveParam(*param);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            // not yet implemented, for future use (whenever deep compositing is supported)
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPremultChannel);
            param->setLabel(kParamPremultChannelLabel);
            param->setHint(kParamPremultChannelHint);
            param->appendOption(kParamPremultChannelR);
            param->appendOption(kParamPremultChannelG);
            param->appendOption(kParamPremultChannelB);
            param->appendOption(kParamPremultChannelA);
            param->setDefault(3); // alpha
            param->setIsSecretAndDisabled(true); // not yet implemented
            if (page) {
                page->addChild(*param);
            }
        }

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
    }
} // >::describeInContext

template <ColorTransformEnum transform>
ImageEffect*
ColorTransformPluginFactory<transform>::createInstance(OfxImageEffectHandle handle,
                                                       ContextEnum /*context*/)
{
    return new ColorTransformPlugin<transform>(handle);
}

// RGBtoHSV
static ColorTransformPluginFactory<eColorTransformRGBToHSV> p1(kPluginRGBToHSVIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// HSVtoRGB
static ColorTransformPluginFactory<eColorTransformHSVToRGB> p2(kPluginHSVToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoHSL
static ColorTransformPluginFactory<eColorTransformRGBToHSL> p3(kPluginRGBToHSLIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// HSLtoRGB
static ColorTransformPluginFactory<eColorTransformHSLToRGB> p4(kPluginHSLToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoHSI
static ColorTransformPluginFactory<eColorTransformRGBToHSI> p5(kPluginRGBToHSIIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// HSItoRGB
static ColorTransformPluginFactory<eColorTransformHSIToRGB> p6(kPluginHSIToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYCbCr601
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr601> p7(kPluginRGBToYCbCr601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YCbCrToRGB601
static ColorTransformPluginFactory<eColorTransformYCbCrToRGB601> p8(kPluginYCbCrToRGB601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYCbCr709
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr709> p17(kPluginRGBToYCbCr709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YCbCrToRGB709
static ColorTransformPluginFactory<eColorTransformYCbCrToRGB709> p18(kPluginYCbCrToRGB709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYPbPr601
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr601> p9(kPluginRGBToYPbPr601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YPbPrToRGB601
static ColorTransformPluginFactory<eColorTransformYPbPrToRGB601> p10(kPluginYPbPrToRGB601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYPbPr709
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr709> p15(kPluginRGBToYPbPr709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YPbPrToRGB709
static ColorTransformPluginFactory<eColorTransformYPbPrToRGB709> p16(kPluginYPbPrToRGB709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYUV601
static ColorTransformPluginFactory<eColorTransformRGBToYUV601> p19(kPluginRGBToYUV601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YUVToRGB601
static ColorTransformPluginFactory<eColorTransformYUVToRGB601> p20(kPluginYUVToRGB601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBToYUV709
static ColorTransformPluginFactory<eColorTransformRGBToYUV709> p21(kPluginRGBToYUV709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YUVToRGB709
static ColorTransformPluginFactory<eColorTransformYUVToRGB709> p22(kPluginYUVToRGB709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGB709ToXYZ
static ColorTransformPluginFactory<eColorTransformRGB709ToXYZ> p11(kPluginRGB709ToXYZIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// XYZToRGB709
static ColorTransformPluginFactory<eColorTransformXYZToRGB709> p12(kPluginXYZToRGB709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// RGB709ToLab
static ColorTransformPluginFactory<eColorTransformRGB709ToLab> p13(kPluginRGB709ToLabIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// LabToRGB709
static ColorTransformPluginFactory<eColorTransformLabToRGB709> p14(kPluginLabToRGB709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// XYZToLab
static ColorTransformPluginFactory<eColorTransformXYZToLab> p23(kPluginXYZToLabIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// LabToXYZ
static ColorTransformPluginFactory<eColorTransformLabToXYZ> p24(kPluginLabToXYZIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// XYZToxyY
static ColorTransformPluginFactory<eColorTransformXYZToxyY> p25(kPluginXYZToxyYIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// xyYToXYZ
static ColorTransformPluginFactory<eColorTransformxyYToXYZ> p26(kPluginxyYToXYZIdentifier, kPluginVersionMajor, kPluginVersionMinor);

mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)
mRegisterPluginFactoryInstance(p5)
mRegisterPluginFactoryInstance(p6)
mRegisterPluginFactoryInstance(p7)
mRegisterPluginFactoryInstance(p8)
mRegisterPluginFactoryInstance(p9)
mRegisterPluginFactoryInstance(p10)
mRegisterPluginFactoryInstance(p11)
mRegisterPluginFactoryInstance(p12)
mRegisterPluginFactoryInstance(p13)
mRegisterPluginFactoryInstance(p14)
mRegisterPluginFactoryInstance(p15)
mRegisterPluginFactoryInstance(p16)
mRegisterPluginFactoryInstance(p17)
mRegisterPluginFactoryInstance(p18)
mRegisterPluginFactoryInstance(p19)
mRegisterPluginFactoryInstance(p20)
mRegisterPluginFactoryInstance(p21)
mRegisterPluginFactoryInstance(p22)
mRegisterPluginFactoryInstance(p23)
mRegisterPluginFactoryInstance(p24)
mRegisterPluginFactoryInstance(p25)
mRegisterPluginFactoryInstance(p26)

OFXS_NAMESPACE_ANONYMOUS_EXIT
