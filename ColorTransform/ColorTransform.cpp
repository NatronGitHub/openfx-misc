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
 * OFX ColorTransform plugin.
 */

#include <cmath>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsLut.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginRGBToHSVName "RGBToHSV"
#define kPluginRGBToHSVDescription "Convert from linear RGB to HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978). H is in degrees, S and V are in the same units as RGB. RGB is gamma-compressed using the sRGB transfer function before conversion."
#define kPluginRGBToHSVIdentifier "net.sf.openfx.RGBToHSV"

#define kPluginHSVToRGBName "HSVToRGB"
#define kPluginHSVToRGBDescription "Convert from HSV color model (hue, saturation, value, as defined by A. R. Smith in 1978) to linear RGB. H is in degrees, S and V are in the same units as RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion."
#define kPluginHSVToRGBIdentifier "net.sf.openfx.HSVToRGB"

#define kPluginRGBToHSLName "RGBToHSL"
#define kPluginRGBToHSLDescription "Convert from RGB to HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978). H is in degrees, S and L are in the same units as RGB. RGB is gamma-compressed using the sRGB transfer function before conversion."
#define kPluginRGBToHSLIdentifier "net.sf.openfx.RGBToHSL"

#define kPluginHSLToRGBName "HSLToRGB"
#define kPluginHSLToRGBDescription "Convert from HSL color model (hue, saturation, lightness, as defined by Joblove and Greenberg in 1978) to linear RGB. H is in degrees, S and L are in the same units as RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion."
#define kPluginHSLToRGBIdentifier "net.sf.openfx.HSLToRGB"

#define kPluginRGBToHSIName "RGBToHSI"
#define kPluginRGBToHSIDescription "Convert from linear RGB to HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992). H is in degrees, S and I are in the same units as RGB. RGB is gamma-compressed using the sRGB transfer function before conversion.\n" \
"The HSI colour space (hue, saturation and intensity) attempts to produce a more intuitive representation of colour. The I axis represents the luminance information. The H and S axes are polar coordinates on the plane orthogonal to I. H is the angle, specified such that red is at zero, green at 120 degrees, and blue at 240 degrees. Hue thus represents what humans implicitly understand as colour. S is the magnitude of the colour vector projected in the plane orthogonal to I, and so represents the difference between pastel colours (low saturation) and vibrant colours (high saturation). The main drawback of this colour space is that hue is undefined if saturation is zero, making error propagation in transformations from the RGB colour space more complicated.\n" \
"It should also be noted that, although the HSI colour space may be more intuitive, is not \"perceptual\", in the sense that small displacements of equal size in different parts of the colour space will be perceived by human observers as changes of different magnitude. Attempts have been made to define such colour spaces: CIE-LAB and CIE-LUV are two examples."
#define kPluginRGBToHSIIdentifier "net.sf.openfx.RGBToHSI"

#define kPluginHSIToRGBName "HSIToRGB"
#define kPluginHSIToRGBDescription "Convert from HSI color model (hue, saturation, intensity, as defined by Gonzalez and Woods in 1992) to linear RGB. H is in degrees, S and I are in the same units as RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion.\n" \
"The HSI colour space (hue, saturation and intensity) attempts to produce a more intuitive representation of colour. The I axis represents the luminance information. The H and S axes are polar coordinates on the plane orthogonal to I. H is the angle, specified such that red is at zero, green at 120 degrees, and blue at 240 degrees. Hue thus represents what humans implicitly understand as colour. S is the magnitude of the colour vector projected in the plane orthogonal to I, and so represents the difference between pastel colours (low saturation) and vibrant colours (high saturation). The main drawback of this colour space is that hue is undefined if saturation is zero, making error propagation in transformations from the RGB colour space more complicated.\n" \
"It should also be noted that, although the HSI colour space may be more intuitive, is not \"perceptual\", in the sense that small displacements of equal size in different parts of the colour space will be perceived by human observers as changes of different magnitude. Attempts have been made to define such colour spaces: CIE-LAB and CIE-LUV are two examples."
#define kPluginHSIToRGBIdentifier "net.sf.openfx.HSIToRGB"

#define kPluginRGBToYCbCr601Name "RGBToYCbCr601"
#define kPluginRGBToYCbCr601Description "Convert from linear RGB to YCbCr color model (ITU.BT-601). RGB is gamma-compressed using the sRGB transfer function before conversion."
#define kPluginRGBToYCbCr601Identifier "net.sf.openfx.RGBToYCbCr601"

#define kPluginYCbCr601ToRGBName "YCbCr601ToRGB"
#define kPluginYCbCr601ToRGBDescription "Convert from YCbCr color model (ITU.BT-601) to linear RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion."
#define kPluginYCbCr601ToRGBIdentifier "net.sf.openfx.YCbCr601ToRGB"

#define kPluginRGBToYCbCr709Name "RGBToYCbCr709"
#define kPluginRGBToYCbCr709Description "Convert from linear RGB to YCbCr color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 transfer function before conversion."
#define kPluginRGBToYCbCr709Identifier "net.sf.openfx.RGBToYCbCr709"

#define kPluginYCbCr709ToRGBName "YCbCr709ToRGB"
#define kPluginYCbCr709ToRGBDescription "Convert from YCbCr color model (ITU.BT-709) to linear RGB. RGB is gamma-decompressed using the Rec.709 transfer function after conversion."
#define kPluginYCbCr709ToRGBIdentifier "net.sf.openfx.YCbCr709ToRGB"

#define kPluginRGBToYPbPr601Name "RGBToYPbPr601"
#define kPluginRGBToYPbPr601Description "Convert from RGB to YPbPr color model (ITU.BT-601). RGB is gamma-compressed using the sRGB transfer function before conversion."
#define kPluginRGBToYPbPr601Identifier "net.sf.openfx.RGBToYPbPr601"

#define kPluginYPbPr601ToRGBName "YPbPr601ToRGB"
#define kPluginYPbPr601ToRGBDescription "Convert from YPbPr color model (ITU.BT-601) to RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion."
#define kPluginYPbPr601ToRGBIdentifier "net.sf.openfx.YPbPr601ToRGB"

#define kPluginRGBToYPbPr709Name "RGBToYPbPr709"
#define kPluginRGBToYPbPr709Description "Convert from RGB to YPbPr color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 transfer function before conversion."
#define kPluginRGBToYPbPr709Identifier "net.sf.openfx.RGBToYPbPr709"

#define kPluginYPbPr709ToRGBName "YPbPr709ToRGB"
#define kPluginYPbPr709ToRGBDescription "Convert from YPbPr color model (ITU.BT-709) to RGB. RGB is gamma-decompressed using the Rec.709 transfer function after conversion."
#define kPluginYPbPr709ToRGBIdentifier "net.sf.openfx.YPbPr709ToRGB"

#define kPluginRGBToYUV601Name "RGBToYUV601"
#define kPluginRGBToYUV601Description "Convert from RGB to YUV color model (ITU.BT-601). RGB is gamma-compressed using the sRGB transfer function before conversion."
#define kPluginRGBToYUV601Identifier "net.sf.openfx.RGBToYUV601"

#define kPluginYUV601ToRGBName "YUV601ToRGB"
#define kPluginYUV601ToRGBDescription "Convert from YUV color model (ITU.BT-601) to RGB. RGB is gamma-decompressed using the sRGB transfer function after conversion."
#define kPluginYUV601ToRGBIdentifier "net.sf.openfx.YUV601ToRGB"

#define kPluginRGBToYUV709Name "RGBToYUV709"
#define kPluginRGBToYUV709Description "Convert from RGB to YUV color model (ITU.BT-709). RGB is gamma-compressed using the Rec.709 transfer function before conversion."
#define kPluginRGBToYUV709Identifier "net.sf.openfx.RGBToYUV709"

#define kPluginYUV709ToRGBName "YUV709ToRGB"
#define kPluginYUV709ToRGBDescription "Convert from YUV color model (ITU.BT-709) to RGB. RGB is gamma-decompressed using the Rec.709 transfer function after conversion."
#define kPluginYUV709ToRGBIdentifier "net.sf.openfx.YUV709ToRGB"


#define kPluginRGBToXYZName "RGBToXYZ"
#define kPluginRGBToXYZDescription "Convert from RGB to XYZ color model (Rec.709 with D65 illuminant). X, Y and Z are in the same units as RGB."
#define kPluginRGBToXYZIdentifier "net.sf.openfx.RGBToXYZPlugin"

#define kPluginXYZToRGBName "XYZToRGB"
#define kPluginXYZToRGBDescription "Convert from XYZ color model (Rec.709 with D65 illuminant) to RGB. X, Y and Z are in the same units as RGB."
#define kPluginXYZToRGBIdentifier "net.sf.openfx.XYZToRGBPlugin"

#define kPluginRGBToLabName "RGBToLab"
#define kPluginRGBToLabDescription "Convert from RGB to L*a*b color model (Rec.709 with D65 illuminant). L*a*b coordinates are divided by 100 for better visualization."
#define kPluginRGBToLabIdentifier "net.sf.openfx.RGBToLabPlugin"

#define kPluginLabToRGBName "LabToRGB"
#define kPluginLabToRGBDescription "Convert from L*a*b color model (Rec.709 with D65 illuminant) to RGB. L*a*b coordinates are divided by 100 for better visualization."
#define kPluginLabToRGBIdentifier "net.sf.openfx.LabToRGBPlugin"

#define kPluginGrouping "Color/Transform"

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


enum ColorTransformEnum {
    eColorTransformRGBToHSV,
    eColorTransformHSVToRGB,
    eColorTransformRGBToHSL,
    eColorTransformHSLToRGB,
    eColorTransformRGBToHSI,
    eColorTransformHSIToRGB,
    eColorTransformRGBToYCbCr601,
    eColorTransformYCbCr601ToRGB,
    eColorTransformRGBToYCbCr709,
    eColorTransformYCbCr709ToRGB,
    eColorTransformRGBToYPbPr601,
    eColorTransformYPbPr601ToRGB,
    eColorTransformRGBToYPbPr709,
    eColorTransformYPbPr709ToRGB,
    eColorTransformRGBToYUV601,
    eColorTransformYUV601ToRGB,
    eColorTransformRGBToYUV709,
    eColorTransformYUV709ToRGB,
    eColorTransformRGBToXYZ,
    eColorTransformXYZToRGB,
    eColorTransformRGBToLab,
    eColorTransformLabToRGB
};

#define toRGB(e)   ((e) == eColorTransformHSVToRGB || \
                    (e) == eColorTransformHSLToRGB || \
                    (e) == eColorTransformHSIToRGB || \
                    (e) == eColorTransformYCbCr601ToRGB || \
                    (e) == eColorTransformYCbCr709ToRGB || \
                    (e) == eColorTransformYPbPr601ToRGB || \
                    (e) == eColorTransformYPbPr709ToRGB || \
                    (e) == eColorTransformYUV601ToRGB || \
                    (e) == eColorTransformYUV709ToRGB || \
                    (e) == eColorTransformXYZToRGB || \
                    (e) == eColorTransformLabToRGB)

#define fromRGB(e) (!toRGB(e))

class ColorTransformProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    bool _premult;
    int _premultChannel;
    double _mix;

public:
    
    ColorTransformProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _premult(false)
    , _premultChannel(3)
    , _mix(1.)
    {
    }
    
    void setSrcImg(const OFX::Image *v) {_srcImg = v;}
        
    void setValues(bool premult,
                   int premultChannel)
    {
        _premult = premult;
        _premultChannel = premultChannel;
    }


private:
};



template <class PIX, int nComponents, int maxValue, ColorTransformEnum transform>
class ColorTransformProcessor : public ColorTransformProcessorBase
{
public:
    ColorTransformProcessor(OFX::ImageEffect &instance)
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
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, dounpremult, _premultChannel);
                switch (transform) {
                    case eColorTransformRGBToHSV:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_hsv(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformHSVToRGB:
                        OFX::Color::hsv_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;

                    case eColorTransformRGBToHSL:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_hsl(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformHSLToRGB:
                        OFX::Color::hsl_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;

                    case eColorTransformRGBToHSI:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_hsi(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformHSIToRGB:
                        OFX::Color::hsi_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;


                    case eColorTransformRGBToYCbCr601:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_ycbcr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYCbCr601ToRGB:
                        OFX::Color::ycbcr601_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;
                        
                    case eColorTransformRGBToYCbCr709:
                        unpPix[0] = OFX::Color::to_func_Rec709(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_Rec709(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_Rec709(unpPix[2]);
                        OFX::Color::rgb_to_ycbcr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYCbCr709ToRGB:
                        OFX::Color::ycbcr709_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_Rec709(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_Rec709(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_Rec709(tmpPix[2]);
                        break;

                    case eColorTransformRGBToYPbPr601:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_ypbpr601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYPbPr601ToRGB:
                        OFX::Color::ypbpr601_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;

                    case eColorTransformRGBToYPbPr709:
                        unpPix[0] = OFX::Color::to_func_Rec709(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_Rec709(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_Rec709(unpPix[2]);
                        OFX::Color::rgb_to_ypbpr709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYPbPr709ToRGB:
                        OFX::Color::ypbpr709_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_Rec709(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_Rec709(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_Rec709(tmpPix[2]);
                        break;

                    case eColorTransformRGBToYUV601:
                        unpPix[0] = OFX::Color::to_func_srgb(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_srgb(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_srgb(unpPix[2]);
                        OFX::Color::rgb_to_yuv601(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYUV601ToRGB:
                        OFX::Color::yuv601_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_srgb(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_srgb(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_srgb(tmpPix[2]);
                        break;

                    case eColorTransformRGBToYUV709:
                        unpPix[0] = OFX::Color::to_func_Rec709(unpPix[0]);
                        unpPix[1] = OFX::Color::to_func_Rec709(unpPix[1]);
                        unpPix[2] = OFX::Color::to_func_Rec709(unpPix[2]);
                        OFX::Color::rgb_to_yuv709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformYUV709ToRGB:
                        OFX::Color::yuv709_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] = OFX::Color::from_func_Rec709(tmpPix[0]);
                        tmpPix[1] = OFX::Color::from_func_Rec709(tmpPix[1]);
                        tmpPix[2] = OFX::Color::from_func_Rec709(tmpPix[2]);
                        break;

                    case eColorTransformRGBToXYZ:
                        OFX::Color::rgb_to_xyz_rec709(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformXYZToRGB:
                        OFX::Color::xyz_rec709_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                    case eColorTransformRGBToLab:
                        OFX::Color::rgb_to_lab(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        tmpPix[0] /= 100;
                        tmpPix[1] /= 100;
                        tmpPix[2] /= 100;
                        break;

                    case eColorTransformLabToRGB:
                        unpPix[0] *= 100;
                        unpPix[1] *= 100;
                        unpPix[2] *= 100;
                        OFX::Color::lab_to_rgb(unpPix[0], unpPix[1], unpPix[2], &tmpPix[0], &tmpPix[1], &tmpPix[2]);
                        break;

                }
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, dopremult, _premultChannel, x, y, srcPix, /*doMasking=*/false, /*maskImg=*/NULL, /*mix=*/1.f, /*maskInvert=*/false, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }

   }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template <ColorTransformEnum transform>
class ColorTransformPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    ColorTransformPlugin(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _premultChanged(0)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                            _dstClip->getPixelComponents() == ePixelComponentRGBA));
        _srcClip = getContext() == OFX::eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert((!_srcClip && getContext() == OFX::eContextGenerator) ||
               (_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentRGBA)));
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
    }
    
private:
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;
    
    /* set up and run a processor */
    void setupAndProcess(ColorTransformProcessorBase &, const OFX::RenderArguments &args);

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;
    
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::setupAndProcess(ColorTransformProcessorBase &processor, const OFX::RenderArguments &args)
{
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
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(args.time) : 0);
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
    
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    processor.setRenderWindow(args.renderWindow);
    
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    
    processor.setValues(premult, premultChannel);
    processor.process();
}

// the overridden render function
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::render(const OFX::RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    
    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ColorTransformProcessor<unsigned char, 4, 255, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorTransformProcessor<unsigned short, 4, 65535, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorTransformProcessor<float, 4, 1, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentRGB);
        switch (dstBitDepth) {
            case OFX::eBitDepthUByte: {
                ColorTransformProcessor<unsigned char, 3, 255, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthUShort: {
                ColorTransformProcessor<unsigned short, 3, 65535, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            case OFX::eBitDepthFloat: {
                ColorTransformProcessor<float, 3, 1, transform> fred(*this);
                setupAndProcess(fred, args);
                break;
            }
            default:
                OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
}

/* Override the clip preferences */
template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::getClipPreferences(OFX::ClipPreferencesSetter &clipPreferences)
{
    if (_srcClip && _srcClip->getPixelComponents() == ePixelComponentRGBA) {
        bool premult;
        _premult->getValue(premult);
        // set the premultiplication of _dstClip
        if (fromRGB(transform)) {
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
ColorTransformPlugin<transform>::changedClip(const InstanceChangedArgs &args, const std::string &clipName)
{
    if (clipName == kOfxImageEffectSimpleSourceClipName &&
        _srcClip && _srcClip->isConnected() &&
        !_premultChanged->getValue() &&
        args.reason == OFX::eChangeUserEdit) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _premult->setValue(false);
        } else switch (_srcClip->getPreMultiplication()) {
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


template <ColorTransformEnum transform>
void
ColorTransformPlugin<transform>::changedParam(const OFX::InstanceChangedArgs &args, const std::string &paramName)
{
    if (paramName == kParamPremult && args.reason == OFX::eChangeUserEdit) {
        _premultChanged->setValue(true);
    }
}

template <ColorTransformEnum transform>
class ColorTransformPluginFactory : public OFX::PluginFactoryHelper<ColorTransformPluginFactory<transform> >
{
public:
    ColorTransformPluginFactory(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<ColorTransformPluginFactory<transform> >(id, verMaj, verMin){}

    //virtual void load() OVERRIDE FINAL {};
    //virtual void unload() OVERRIDE FINAL {};
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describe(OFX::ImageEffectDescriptor &desc)
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

        case eColorTransformYCbCr601ToRGB:
            desc.setLabel(kPluginYCbCr601ToRGBName);
            desc.setPluginDescription(kPluginYCbCr601ToRGBDescription);
            break;
            
        case eColorTransformRGBToYCbCr709:
            desc.setLabel(kPluginRGBToYCbCr709Name);
            desc.setPluginDescription(kPluginRGBToYCbCr709Description);
            break;

        case eColorTransformYCbCr709ToRGB:
            desc.setLabel(kPluginYCbCr709ToRGBName);
            desc.setPluginDescription(kPluginYCbCr709ToRGBDescription);
            break;
            
        case eColorTransformRGBToYPbPr601:
            desc.setLabel(kPluginRGBToYPbPr601Name);
            desc.setPluginDescription(kPluginRGBToYPbPr601Description);
            break;

        case eColorTransformYPbPr601ToRGB:
            desc.setLabel(kPluginYPbPr601ToRGBName);
            desc.setPluginDescription(kPluginYPbPr601ToRGBDescription);
            break;

        case eColorTransformRGBToYPbPr709:
            desc.setLabel(kPluginRGBToYPbPr709Name);
            desc.setPluginDescription(kPluginRGBToYPbPr709Description);
            break;

        case eColorTransformYPbPr709ToRGB:
            desc.setLabel(kPluginYPbPr709ToRGBName);
            desc.setPluginDescription(kPluginYPbPr709ToRGBDescription);
            break;

        case eColorTransformRGBToYUV601:
            desc.setLabel(kPluginRGBToYUV601Name);
            desc.setPluginDescription(kPluginRGBToYUV601Description);
            break;

        case eColorTransformYUV601ToRGB:
            desc.setLabel(kPluginYUV601ToRGBName);
            desc.setPluginDescription(kPluginYUV601ToRGBDescription);
            break;

        case eColorTransformRGBToYUV709:
            desc.setLabel(kPluginRGBToYUV709Name);
            desc.setPluginDescription(kPluginRGBToYUV709Description);
            break;

        case eColorTransformYUV709ToRGB:
            desc.setLabel(kPluginYUV709ToRGBName);
            desc.setPluginDescription(kPluginYUV709ToRGBDescription);
            break;

        case eColorTransformRGBToXYZ:
            desc.setLabel(kPluginRGBToXYZName);
            desc.setPluginDescription(kPluginRGBToXYZDescription);
            break;

        case eColorTransformXYZToRGB:
            desc.setLabel(kPluginXYZToRGBName);
            desc.setPluginDescription(kPluginXYZToRGBDescription);
            break;

        case eColorTransformRGBToLab:
            desc.setLabel(kPluginRGBToLabName);
            desc.setPluginDescription(kPluginRGBToLabDescription);
            break;

        case eColorTransformLabToRGB:
            desc.setLabel(kPluginLabToRGBName);
            desc.setPluginDescription(kPluginLabToRGBDescription);
            break;
    }
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
}

template <ColorTransformEnum transform>
void
ColorTransformPluginFactory<transform>::describeInContext(OFX::ImageEffectDescriptor &desc,
                                           OFX::ContextEnum /*context*/)
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
    PageParamDescriptor *page = desc.definePageParam("Controls");
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremult);
        if (fromRGB(transform)) {
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
        OFX::ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamPremultChannel);
        param->setLabel(kParamPremultChannelLabel);
        param->setHint(kParamPremultChannelHint);
        param->appendOption(kParamPremultChannelR, kParamPremultChannelRHint);
        param->appendOption(kParamPremultChannelG, kParamPremultChannelGHint);
        param->appendOption(kParamPremultChannelB, kParamPremultChannelBHint);
        param->appendOption(kParamPremultChannelA, kParamPremultChannelAHint);
        param->setDefault(3); // alpha
        param->setIsSecret(true); // not yet implemented
        if (page) {
            page->addChild(*param);
        }
    }

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecret(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

template <ColorTransformEnum transform>
OFX::ImageEffect*
ColorTransformPluginFactory<transform>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
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
// RGBtoYCbCr601
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr601> p7(kPluginRGBToYCbCr601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YCbCr601toRGB
static ColorTransformPluginFactory<eColorTransformYCbCr601ToRGB> p8(kPluginYCbCr601ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoYCbCr709
static ColorTransformPluginFactory<eColorTransformRGBToYCbCr709> p17(kPluginRGBToYCbCr709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YCbCr709toRGB
static ColorTransformPluginFactory<eColorTransformYCbCr709ToRGB> p18(kPluginYCbCr709ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoYPbPr601
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr601> p9(kPluginRGBToYPbPr601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YPbPr601toRGB
static ColorTransformPluginFactory<eColorTransformYPbPr601ToRGB> p10(kPluginYPbPr601ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoYPbPr709
static ColorTransformPluginFactory<eColorTransformRGBToYPbPr709> p15(kPluginRGBToYPbPr709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YPbPr709toRGB
static ColorTransformPluginFactory<eColorTransformYPbPr709ToRGB> p16(kPluginYPbPr709ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoYUV601
static ColorTransformPluginFactory<eColorTransformRGBToYUV601> p19(kPluginRGBToYUV601Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YUV601toRGB
static ColorTransformPluginFactory<eColorTransformYUV601ToRGB> p20(kPluginYUV601ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoYUV709
static ColorTransformPluginFactory<eColorTransformRGBToYUV709> p21(kPluginRGBToYUV709Identifier, kPluginVersionMajor, kPluginVersionMinor);
// YUV709toRGB
static ColorTransformPluginFactory<eColorTransformYUV709ToRGB> p22(kPluginYUV709ToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoXYZ
static ColorTransformPluginFactory<eColorTransformRGBToXYZ> p11(kPluginRGBToXYZIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// XYZtoRGB
static ColorTransformPluginFactory<eColorTransformXYZToRGB> p12(kPluginXYZToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// RGBtoLab
static ColorTransformPluginFactory<eColorTransformRGBToLab> p13(kPluginRGBToLabIdentifier, kPluginVersionMajor, kPluginVersionMinor);
// LabtoRGB
static ColorTransformPluginFactory<eColorTransformLabToRGB> p14(kPluginLabToRGBIdentifier, kPluginVersionMajor, kPluginVersionMinor);

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

OFXS_NAMESPACE_ANONYMOUS_EXIT
