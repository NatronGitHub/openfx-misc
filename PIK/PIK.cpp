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
 * OFX PIK plugin.
 */
/*
 * TODO:
 * *Screen Matte
 * Clip Rollback: compute a mask of the non-clipped areas, dilate this mask and the values inside, then mask the result with this mask and values.
 * Screen Dilate: dilate (or erode) the matte
 * Screen Softness: blur the matte
 * Screen Despot Black: dilate followed by erode of the same amount (closing)
 * Screen Despot White: erode followed by dilate of the same amount (opening)
 */
/*
   IBK tutorials:


   Nuke doc tutorial:
   http://help.thefoundry.co.uk/nuke/content/getting_started/tutorial3/image_based_keying.html

   Video tutorial by Steve Wright:
   https://www.youtube.com/watch?v=-GmMC0AYXJ4

   Advanced tutorial:
   https://compositingmentor.com/2014/07/19/advanced-keying-breakdown-alpha-1-4-ibk-stacked-technique/

 */
/*
   http://tiraoka.blogspot.fr/2015/07/secret-of-ibk.html

   secret of IBK


   There are some nice tutorials about IBK in the following link,

   http://compositingmentor.com

   This is pretty much amazing! Thank you pretty much, Tony.

   IBK node which IBK Colour and IBK Gizmo have it inside themselves can be broken down into the following formula.

   alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))

   A is pfg and B is c. and this is the the case of "Green" keying, I mean we choose "Green" on IBK.
   rw is the value of "red weight" and gbw is the value of "green/blue weight".

   So, When preparing clean plate with IBK Colour, we need to tweak the value of the "darks" and the "lights" on itself. The "darks" is the "offset" of the Grade node which affects on input plate in IBK Colour. The "lights" is the "multiple" of the Grade node as well.

   Anyway, we compare green and red + blue. If the pixel goes "green > red + blue", the pixel would be remained. Or if the pixel goes to "green < red + blue", the pixel would be turned to black. I mean IBK Colour-wise.

   When the green in the green screen looks saturated, I usually take the red or the blue value of "lights" up.
   When the green in the green screen doesn't so saturated, I usually take the red or the blue value of "lights" down. This is the case of Green screen.


   the top node(IBK Colour) of IBK Stack, this is the case of "Saturated"
   the top node(IBK Colour) of IBK Stack, this is the case of "Less Saturated"
   the clean plate which is the resulted of IBK stack.

   checking for key extract

   And "use bkg luminance" on "IBK Gizmo" works like "Additive Keyer"
   See http://www.nukepedia.com/written-tutorials/additive-keyer/
   This is also awesome great tutorial.

 */

/*
   http://www.jahshaka.com/forums/archive/index.php/t-16044.html
   http://www.jahshaka.com/forums/showthread.php?16044-Bestest-Keyer-For-Detail

   Keylight is definitely not a chroma keyer. It's a color difference keyer that uses a mix (in Shake-speak) i.e. blend i.e. dissolve operation instead of a max operation to combine the non-backing screen channels. For a green screen the math would be g-(r*c+b*(1-c)) where c controls the mix between the red and the blue channel. This approach generally gives better results for transparent objects, hair, motion blur, defocus, etc. compared to keyers which use max, but it's biggest problem is that it produces a weaker core matte. It's especially sensitive to secondary colors which contain the backing screen color (i.e. yellow and cyan for a green screen).

   IBK is a color difference keyer with a very simple basic algorithm. In case of a green screen the math is g-(r*rw+b*bw), where rw is the red weight and bw is the blue weight with a default value of 0.5 for both. What makes it sophisticated (among other things) is the way it uses another image to scale the result of the above mentioned equation.

   Every keyer scales (normalizes) the result of it's basic algorithm so that, on one end, you get 1 for the pixels that match the chosen screen color, and 0, on the other end, for the pixels that contain no or little of the primary color of the backing screen (this is afterward inverted so you end up with black for the transparent parts of the image and white for the opaque parts).

   Keylight, for example, scales the result of it's basic algorithm (which is g-(r*0.5+b*0.5), the same as IBK by default) by dividing it with the the result of gc-(rc*0.5+bc*0.5), where rc,gc and gc are the red, green and blue values of the chosen screen color. IBK does the same if you set "pick" as the screen type and select the backing screen color. If you set screen type to "C-green" or "C-blue" instead of using a single value for normalizing the result of the basic equation (i.e. the unscaled matte image), it processes a "control" image with the gc-(rc*rw+bc*bw) formula pixel by pixel, and then divides the unscaled matte image with the processed control image.
 */
/*
   about keying in general:
   https://bradwoodgate.files.wordpress.com/2011/06/i7824248innovations.pdf

   how to shoot a good keyable greenscreen:
   http://vfxio.com/PDFs/Screaming_at_the_Greenscreen.pdf
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <limits>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsMaskMix.h"
#include "ofxsLut.h"
#include "ofxsCoords.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

#define DISABLE_LM // define to disable luminance match (not yet implemented)
#define DISABLE_AL // define to disable autolevels (not yet implemented)
#define DISABLE_RGBAL // define to disable RGBA legal (not yet implemented)


using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "PIK"
#define kPluginGrouping "Keyer"
#define kPluginDescription \
    "A keyer that works by generating a clean plate from the green/blue screen sequences. Inspired by Nuke's IBK by Paul Lambert and Fusion's KAK by Pieter Van Houte.\n" \
    "\n" \
    "There are 2 options to pull a key with PIK. One is to use PIKColor to automatically extract a clean plate from the foreground image and use it as the the C input, and the other is to pick a color which best represents the area you are trying to key.\n" \
    "\n" \
    "The blue- or greenscreen image should be used as the Fg input, which is used to compute the output color. If that image contains significant noise, a denoised version should be used as the PFg input, which is used to pull the key. The C input should either be a clean plate or the outupt of PIKColor, and is used as the screen color if the 'Screen Type' is not 'Pick'. The Bg image is used in calculating fine edge detail when either 'Use Bg Luminance' or 'Use Bg Chroma' is checked. Optionally, an inside mask (a.k.a. holdout matte or core matte) and an outside mask (a.k.a. garbage matte) can be connected to inputs InM and OutM. Note that the outside mask takes precedence over the inside mask.\n" \
    "\n" \
    "If PIKcolor is used to build the clean plate, the PIKColor Source input should be the same as the PFg input to PIK, e.g. the denoised footage, and the inside mask of PIK can also be fed into the InM input of PIKColor.\n" \
    "\n" \
    "The color weights deal with the hardness of the matte. When viewing the output (with screen subtraction checked), one may notice areas where edges have a slight discoloration due to the background not being fully removed from the original plate. This is not spill but a result of the matte being too strong. Lowering one of the weights will correct that particular edge. For example, if it is a red foreground image with an edge problem, lower the red weight. This may affect other edges so the use of multiple PIKs with different weights, split with KeyMixes, is recommended.\n" \
    "\n" \
    "The Alpha Bias setting may be used either if there is a strong global color cast on the scene (e.g. the green or blue screen color is not pure), or if parts of the foreground are transparent in the output. This color is considered by the algorithm as being a grey reference: all colors from the PFg input are first normalized by this color before computation.\n" \
    "If the Alpha Bias is set, but the screen subtraction has a strong color bias (e.g. the despilled areas show the screen color), uncheck 'Use Alpha for Despill' and set the Despill Bias to the color of the foreground elements that are most affected by the color bias.\n" \
/*"The 'Luminance Match' feature adds a luminance factor to the keying algorithm which helps to capture transparent areas of the foreground which are brighter than the backing screen. It will also allow you to lessen some of the garbage area noise by bringing down the screen range - pushing this control too far will also eat into some of your foreground blacks. 'Luminance Level' allows you to make the overall effect stronger or weaker.\n"*/ \
/*"\n"*/ \
/*"'Autolevels' will perform a color correction before the image is pulled so that hard edges from a foreground subject with saturated colors are reduced. The same can be achieved with the weights but here only those saturated colors are affected whereas the use of weights will affect the entire image. When using this feature it's best to have this as a separate node which you can then split with other PIKs as the weights will no longer work as expected. You can override some of the logic for when you actually have particular foreground colors you want to keep.\n"*/ \
/*"For example when you have a saturated red subject against bluescreen you'll get a magenta transition area. Autolevels will eliminate this but if you have a magenta foreground object then this control will make the magenta more red unless you check the magenta box to keep it.\n"*/ \
    "\n" \
    "'Screen Subtraction' (a.k.a. despill) removes the background color from the output via a subtraction process (1-alpha times the screen color is subtracted at each pixel). When unchecked, the output is simply the original Fg premultiplied with the generated matte.\n" \
    "\n" \
    "'Use Bkg Luminance' and 'Use Bkg Chroma' affect the output color by the new background. " /*These controls are best used with the 'Luminance Match' sliders above. */ "This feature can also sometimes really help with screens that exhibit some form of fringing artifact - usually a darkening or lightening of an edge on one of the color channels on the screen. The effect can be offset by grading the Bg input up or down with a grade node just before input. If it is just an area which needs help then just rotoscope that area and locally grade the Bg input up or down to remove the artifact.\n" \
    "\n" \
    "The output of PIK is controlled by the \"Output Mode\" option. For example, if the output is \"Premultiplied\", it should be composited with the background using a Merge-over operation.\n" \
    "\n" \
    "The basic equation used to extract the key in PIK is (in the case of \"green\" keying):\n" \
    "alpha = 0 if (Ag-Ar*rw-Ab*gbw) is negative, else 1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw)\n" \
    "A is input PFg and B is input C, rw is the value of \"Red Weight\" and gbw is the value of \"Green/Blue Weight\".\n" \
    "\n" \
    "See also:\n" \
    "- http://opticalenquiry.com/nuke/index.php?title=The_Keyer_Nodes#IBK\n" \
    "- https://compositingmentor.com/2014/07/19/advanced-keying-breakdown-alpha-1-4-ibk-stacked-technique/"

#define kPluginIdentifier "net.sf.openfx.PIK"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipFg "Fg"
#define kClipFgHint "The blue- or greenscreen image. Used to compute the output color."
#define kClipPFg "PFg"
#define kClipPFgHint "(optional) The preprocessed/denoised blue- or greenscreen image. Used to compute the output key (alpha). A denoised image usually gives a less noisy key. If not connected, the Fg input is used instead."
#define kClipC "C"
#define kClipCHint "(optional) A clean plate if available, or the output of PIKColor to generate the clean plate at each frame."
#define kClipBg "Bg"
#define kClipBgHint "(optional) The background image. This is used in calculating fine edge detail when the 'Use Bg Luminance' or 'Use Bg Chroma' options are checked."
#define kClipInsideMask "InM"
#define kClipInsideMaskHint "The Inside Mask, or holdout matte, or core matte, used to confirm areas that are definitely foreground."
#define kClipOutsidemask "OutM"
#define kClipOutsideMaskHint "The Outside Mask, or garbage matte, used to remove unwanted objects (lighting rigs, and so on) from the foreground. The Outside Mask has priority over the Inside Mask, so that areas where both are one are considered to be outside."

#define kParamScreenType "screenType"
#define kParamScreenTypeLabel "Screen Type"
#define kParamScreenTypeHint "The type of background screen used for the key."
#define kParamScreenTypeOptionGreen "C-Green", "Background screen with a green tint.", "green"
#define kParamScreenTypeOptionBlue "C-Blue", "Background screen with a blue tint.", "blue"
#define kParamScreenTypeOptionPick "Pick", "The background screen color is selected by the \"color\" parameter, and the type of screen (green or blue) is set automatically from this color.", "pick"
enum ScreenTypeEnum
{
    eScreenTypeGreen = 0,
    eScreenTypeBlue,
    eScreenTypePick,
};

#define kParamScreenTypeDefault eScreenTypeBlue

#define kParamColor "color"
#define kParamColorLabel "Color"
#define kParamColorHint "The screen color in case 'Pick' was chosen as the 'Screen Type'."

#define kParamRedWeight "redWeight"
#define kParamRedWeightLabel "Red Weight"
#define kParamRedWeightHint "Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation."
#define kParamRedWeightDefault 0.5 // 1 in IBK, 0.5 in IBKGizmo

#define kParamBlueGreenWeight "blueGreenWeight"
#define kParamBlueGreenWeightLabel "Blue/Green Weight"
#define kParamBlueGreenWeightHint "Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation."
#define kParamBlueGreenWeightDefault 0.5 // 0 in IBK, 0.5 in IBKGizmo

#define kParamAlphaBias "alphaBias"
#define kParamAlphaBiasLabel "Alpha Bias"
#define kParamAlphaBiasHint "Divide C and PFg colors by this color before computing alpha. This may be used when the whole scene, including the background, has a strong color cast."

#define kParamDespillBias "despillBias"
#define kParamDespillBiasLabel "Despill Bias"
#define kParamDespillBiasHint "Divide C color by this color before despill."

#define kParamDespillBiasIsAlphaBias "despillBiasIsAlphaBias"
#define kParamDespillBiasIsAlphaBiasLabel "Use Alpha Bias for Despill"
#define kParamDespillBiasIsAlphaBiasHint "Use alpha bias color for despill instead of despill bias color."

#define kParamLMEnable "lmEnable"
#define kParamLMEnableLabel "Luminance Match Enable"
#define kParamLMEnableHint "Adds a luminance factor to the color difference algorithm."
#define kParamLMEnableDefault false

#define kParamLevel "level"
#define kParamLevelLabel "Screen Range"
#define kParamLevelHint "Helps retain blacks and shadows."
#define kParamLevelDefault 1

#define kParamLuma "luma"
#define kParamLumaLabel "Luminance Level"
#define kParamLumaHint "Makes the matte more additive."
#define kParamLumaDefault 0 // 0.5 in IBK, 0 in IBKGizmo

#define kParamLLEnable "llEnable"
#define kParamLLEnableLabel "Enable"
#define kParamLLEnableHint "Disable the luminance level when us bg influence."
#define kParamLLEnableDefault false

#define kParamAutolevels "autolevels"
#define kParamAutolevelsLabel "Autolevels"
#define kParamAutolevelsHint "Removes hard edges from the matte."
#define kParamAutolevelsDefault false

#define kParamYellow "yellow"
#define kParamYellowLabel "Yellow"
#define kParamYellowHint "Override autolevel with yellow component."
#define kParamYellowDefault false

#define kParamCyan "cyan"
#define kParamCyanLabel "Cyan"
#define kParamCyanHint "Override autolevel with cyan component."
#define kParamCyanDefault false

#define kParamMagenta "magenta"
#define kParamMagentaLabel "Magenta"
#define kParamMagentaHint "Override autolevel with magenta component."
#define kParamMagentaDefault false

#define kGroupScreenMatte "screenMatte"
#define kGroupScreenMatteLabel "Screen Matte"

#define kParamScreenClipMin "screenClipMin"
#define kParamScreenClipMinLabel "Clip Black"
#define kParamScreenClipMinHint "Any alpha below this value is set to 0."

#define kParamScreenClipMax "screenClipMax"
#define kParamScreenClipMaxLabel "Clip White"
#define kParamScreenClipMaxHint "Any alpha above this value is set to 1."

#define kParamScreenReplace "screenReplace"
#define kParamScreenReplaceLabel "Screen Replace"
#define kParamScreenReplaceHint "What to do with the color of the pixels for which alpha was modified by the screen matte settings."

#define kParamScreenReplaceColor "screenReplaceColor"
#define kParamScreenReplaceColorLabel "Screen Replace Color"
#define kParamScreenReplaceColorHint "The color to use when the Screen Replace parameter is set to Soft or Hard Color."

#define kParamSS "ss"
#define kParamSSLabel "Screen Subtraction"
#define kParamSSHint "Have the keyer subtract the foreground or just premult."
#define kParamSSDefault true

#define kParamClampAlpha "clampAlpha"
#define kParamClampAlphaLabel "Clamp"
#define kParamClampAlphaHint "Clamp matte to 0-1."
#define kParamClampAlphaDefault true

#define kParamRGBAL "rgbal"
#define kParamRGBALLabel "RGBA Legal"
#define kParamRGBALHint "Legalize rgba relationship."
#define kParamRGBALDefault false

#define kGroupInsideMask "insideMask"
#define kGroupInsideMaskLabel "Inside Mask"

#define kParamSourceAlpha "sourceAlphaHandling"
#define kParamSourceAlphaLabel "Source Alpha"
#define kParamSourceAlphaHint \
    "How the alpha embedded in the Source input should be used"
#define kParamSourceAlphaOptionIgnore "Ignore", "Ignore the source alpha.", "ignore"
#define kParamSourceAlphaOptionAddToInsideMask "Add to Inside Mask", "Source alpha is added to the inside mask. Use for multi-pass keying.", "inside"
//#define kParamSourceAlphaOptionNormal "Normal", "Foreground key is multiplied by source alpha when compositing.", "normal"
enum SourceAlphaEnum
{
    eSourceAlphaIgnore,
    eSourceAlphaAddToInsideMask,
    //eSourceAlphaNormal,
};

#define kParamInsideReplace "insideReplace"
#define kParamInsideReplaceLabel "Inside Replace"
#define kParamInsideReplaceHint "What to do with the color of the pixels for which alpha was modified by the inside mask."
#define kParamReplaceOptionNone "None", "Subtracted image is not affected by alpha modifications.", "none"
#define kParamReplaceOptionSource "Source", "When alpha is modified, a corresponding amount of the Fg color is added.", "source"
#define kParamReplaceOptionHardColor "Hard Color", "When alpha is modified, a corresponding amount of the replace color is added.", "hardcolor"
#define kParamReplaceOptionSoftColor "Soft Color", "When alpha is modified, a corresponding amount of the replace color is added, but the resulting luminance is matched with Fg.", "softcolor"
enum ReplaceEnum
{
    eReplaceNone,
    eReplaceSource,
    eReplaceHardColor,
    eReplaceSoftColor,
};

#define kParamInsideReplaceColor "insideReplaceColor"
#define kParamInsideReplaceColorLabel "Inside Replace Color"
#define kParamInsideReplaceColorHint "The color to use when the Inside Replace parameter is set to Soft or Hard Color."

#define kParamNoKey "noKey"
#define kParamNoKeyLabel "No Key"
#define kParamNoKeyHint "Apply despill, background luminance and chroma to Fg rgba input using the Fg alpha channel as the key - no key is pulled, but Inside Mask and Outside Mask are applied if connected."
#define kParamNoKeyDefault false

#define kParamUBL "ubl"
#define kParamUBLLabel "Use Bg Luminance"
#define kParamUBLHint "Have the output RGB be biased by the difference between the Bg luminance and the C luminance). Luminance is computed using the given Colorspace." // only applied where the key is transparent
#define kParamUBLDefault false

#define kParamUBC "ubc"
#define kParamUBCLabel "Use Bg Chroma"
#define kParamUBCHint "Have the output RGB be biased by the Bg chroma. Chroma is computed using the given Colorspace"
#define kParamUBCDefault false

#define kParamColorspace "colorspace"
#define kParamColorspaceLabel "Colorspace"
#define kParamColorspaceHint "Formula used to compute luminance and chrominance from RGB values for the \"Use Bg Luminance\" and \"Use Bg Choma\" options."
#define kParamColorspaceOptionRec709 "Rec. 709", "Use Rec. 709 with D65 illuminant.", "rec709"
#define kParamColorspaceOptionRec2020 "Rec. 2020", "Use Rec. 2020 with D65 illuminant.", "rec2020"
#define kParamColorspaceOptionACESAP0 "ACES AP0", "Use ACES AP0 with ACES (approx. D60) illuminant.", "acesap0"
#define kParamColorspaceOptionACESAP1 "ACES AP1", "Use ACES AP1 with ACES (approx. D60) illuminant.", "acesap1"

enum ColorspaceEnum
{
    eColorspaceRec709,
    eColorspaceRec2020,
    eColorspaceACESAP0,
    eColorspaceACESAP1,
};

#define kParamOutputMode "show"
#define kParamOutputModeLabel "Output Mode"
#define kParamOutputModeHint \
    "What image to output."
#define kParamOutputModeOptionSource "Source", "The PFg input (or Fg input, if PFg is not connected).", "source"
#define kParamOutputModeOptionSourceAlpha "Source Alpha", "The Alpha channel from the PFg input (or Fg input, if PFg is not connected), displayed as luminance.", "sourcealpha"
#define kParamOutputModeOptionCleanPlate "Clean Plate", "The clean plate from the C input (or the screen color, if C is not connected).", "cleanplate"
#define kParamOutputModeOptionScreenMatte "Screen Matte", "The screen matte after keying and screen matte processing, but before applying the inside and outside mask, displayed as luminance.", "screenmatte"
#define kParamOutputModeOptionInsideMask "Inside Mask", "The inside mask, displayed as luminance.", "insidemask"
#define kParamOutputModeOptionOutsideMask "Outside Mask", "The outside mask, displayed as luminance.", "outsidemask"
#define kParamOutputModeOptionCombinedMatte "Combined Matte", "The final matte, after applying inside and outside mask, displayed as luminance.", "matte"
#define kParamOutputModeOptionStatus "Status", "An image showing which pixels are pure background (black), pure foreground (white), partially transparent (grey), affected by Screen Replace (green), affected by Inside Replace (blue), or affected by Outside Mask (red).", "status"
#define kParamOutputModeOptionIntermediate "Intermediate", "Color is the source color. Alpha is the foreground key. Use for multi-pass keying.", "intermediate"
#define kParamOutputModeOptionPremultiplied "Premultiplied", "Color is the Source color after key color suppression, multiplied by alpha. Alpha is the foreground key.", "premultiplied"
#define kParamOutputModeOptionUnpremultiplied "Unpremultiplied", "Color is the Source color after key color suppression. Alpha is the foreground key.", "unpremultiplied"
#define kParamOutputModeOptionComposite "Composite", "Color is the composite of Source and Bg. Alpha is the foreground key.", "composite"

enum OutputModeEnum
{
    eOutputModeSource,
    eOutputModeSourceAlpha,
    eOutputModeCleanPlate,
    eOutputModeScreenMatte,
    eOutputModeInsideMask,
    eOutputModeOutsideMask,
    eOutputModeCombinedMatte,
    eOutputModeStatus,
    eOutputModeIntermediate,
    eOutputModePremultiplied,
    eOutputModeUnpremultiplied,
    eOutputModeComposite,
};

static float
luminance(ColorspaceEnum colorspace,
          float const rgb[3])
{
    switch (colorspace) {
    case eColorspaceRec709:
    default:

        return Color::rgb709_to_y(rgb[0], rgb[1], rgb[2]);
        break;

    case eColorspaceRec2020:

        return Color::rgb2020_to_y(rgb[0], rgb[1], rgb[2]);
        break;

    case eColorspaceACESAP0:

        return Color::rgbACESAP0_to_y(rgb[0], rgb[1], rgb[2]);
        break;

    case eColorspaceACESAP1:

        return Color::rgbACESAP1_to_y(rgb[0], rgb[1], rgb[2]);
        break;
    }
}

class PIKProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_fgImg;
    const Image *_pfgImg;
    const Image *_cImg;
    const Image *_bgImg;
    const Image *_inMaskImg;
    const Image *_outMaskImg;
    ScreenTypeEnum _screenType; // Screen Type: The type of background screen used for the key.
    float _color[3];
    bool _useColor;
    double _redWeight; // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    double _blueGreenWeight; // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    float _alphaBias[3];
    float _despillBias[3];
    bool _lmEnable; // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
    double _level; // Screen Range: Helps retain blacks and shadows.
    double _luma; // Luminance Level: Makes the matte more additive.
    bool _llEnable; // Luminance Level Enable: Disable the luminance level when us bg influence.
    bool _autolevels; // Autolevels: Removes hard edges from the matte.
    bool _yellow; // Yellow: Override autolevel with yellow component.
    bool _cyan; // Cyan: Override autolevel with cyan component.
    bool _magenta; // Magenta: Override autolevel with magenta component.
    bool _ss; // Screen Subtraction: Have the keyer subtract the foreground or just premult.
    bool _clampAlpha; // Clamp: Clamp matte to 0-1.
    bool _rgbal; // Legalize rgba relationship.
    double _screenClipMin;
    double _screenClipMax;
    ReplaceEnum _screenReplace;
    float _screenReplaceColor[3];
    SourceAlphaEnum _sourceAlpha;
    ReplaceEnum _insideReplace;
    float _insideReplaceColor[3];
    bool _noKey; // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
    bool _ubl; // Use Bg Lum: Have the output rgb be biased by the bg luminance.
    bool _ubc; // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
    ColorspaceEnum _colorspace;
    OutputModeEnum _outputMode;

public:

    PIKProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _fgImg(NULL)
        , _pfgImg(NULL)
        , _cImg(NULL)
        , _bgImg(NULL)
        , _inMaskImg(NULL)
        , _outMaskImg(NULL)
        , _screenType(kParamScreenTypeDefault)
        , _useColor(false)
        , _redWeight(kParamRedWeightDefault)
        , _blueGreenWeight(kParamBlueGreenWeightDefault)
        , _lmEnable(kParamLMEnableDefault)
        , _level(kParamLevelDefault)
        , _luma(kParamLumaDefault)
        , _llEnable(kParamLLEnableDefault)
        , _autolevels(kParamAutolevelsDefault)
        , _yellow(kParamYellowDefault)
        , _cyan(kParamCyanDefault)
        , _magenta(kParamMagentaDefault)
        , _ss(kParamSSDefault)
        , _clampAlpha(kParamClampAlphaDefault)
        , _rgbal(kParamRGBALDefault)
        , _screenClipMin(0.)
        , _screenClipMax(1.)
        , _screenReplace(eReplaceSoftColor)
        , _sourceAlpha(eSourceAlphaIgnore)
        , _insideReplace(eReplaceSoftColor)
        , _noKey(kParamNoKeyDefault)
        , _ubl(kParamUBLDefault)
        , _ubc(kParamUBCDefault)
        , _colorspace(eColorspaceRec709)
        , _outputMode(eOutputModePremultiplied)
    {
        _color[0] = _color[1] = _color[2] = 0.;
        _alphaBias[0] = _alphaBias[1] = _alphaBias[2] = 0.;
        _despillBias[0] = _despillBias[1] = _despillBias[2] = 0.;
        _screenReplaceColor[0] = _screenReplaceColor[1] = _screenReplaceColor[2] = 0.;
        _insideReplaceColor[0] = _insideReplaceColor[1] = _insideReplaceColor[2] = 0.;
    }

    void setSrcImgs(const Image *fgImg,
                    const Image *pfgImg,
                    const Image *cImg,
                    const Image *bgImg,
                    const Image *inMaskImg,
                    const Image *outMaskImg)
    {
        _fgImg = fgImg;
        _pfgImg = pfgImg;
        _cImg = cImg;
        _bgImg = bgImg;
        _inMaskImg = inMaskImg;
        _outMaskImg = outMaskImg;
    }

    void setValues(ScreenTypeEnum screenType, // Screen Type: The type of background screen used for the key.
                   const OfxRGBColourD& color,
                   double redWeight, // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
                   double blueGreenWeight, // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
                   const OfxRGBColourD& alphaBias,
                   const OfxRGBColourD& despillBias,
                   bool lmEnable, // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
                   double level, // Screen Range: Helps retain blacks and shadows.
                   double luma, // Luminance Level: Makes the matte more additive.
                   bool llEnable, // Luminance Level Enable: Disable the luminance level when us bg influence.
                   bool autolevels, // Autolevels: Removes hard edges from the matte.
                   bool yellow, // Yellow: Override autolevel with yellow component.
                   bool cyan, // Cyan: Override autolevel with cyan component.
                   bool magenta, // Magenta: Override autolevel with magenta component.
                   bool ss, // Screen Subtraction: Have the keyer subtract the foreground or just premult.
                   bool clampAlpha, // Clamp: Clamp matte to 0-1.
                   bool rgbal, // Legalize rgba relationship.
                   double screenClipMin,
                   double screenClipMax,
                   ReplaceEnum screenReplace,
                   const OfxRGBColourD& screenReplaceColor,
                   SourceAlphaEnum sourceAlpha,
                   ReplaceEnum insideReplace,
                   const OfxRGBColourD& insideReplaceColor,
                   bool noKey, // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
                   bool ubl, // Use Bg Lum: Have the output rgb be biased by the bg luminance.
                   bool ubc, // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
                   ColorspaceEnum colorspace,
                   OutputModeEnum outputMode)
    {
        // all colors are normalized with unit luminance
        _alphaBias[0] = std::max(0.0001, alphaBias.r);
        _alphaBias[1] = std::max(0.0001, alphaBias.g);
        _alphaBias[2] = std::max(0.0001, alphaBias.b);
        float l = luminance(_colorspace, _alphaBias);
        _alphaBias[0] /= l;
        _alphaBias[1] /= l;
        _alphaBias[2] /= l;
        _despillBias[0] = std::max(0.0001, despillBias.r);
        _despillBias[1] = std::max(0.0001, despillBias.g);
        _despillBias[2] = std::max(0.0001, despillBias.b);
        _despillBias[0] /= l;
        _despillBias[1] /= l;
        _despillBias[2] /= l;
        if (screenType == eScreenTypePick) {
            _screenType = (color.g / _alphaBias[1] > color.b / _alphaBias[2]) ? eScreenTypeGreen : eScreenTypeBlue;
            _color[0] = color.r / _alphaBias[0];
            _color[1] = color.g / _alphaBias[1];
            _color[2] = color.b / _alphaBias[2];
            _useColor = true;
        } else {
            _screenType = screenType;
            _useColor = false;
        }
        _redWeight = redWeight;
        _blueGreenWeight = blueGreenWeight;
        _lmEnable = lmEnable;
        _level = level;
        _luma = luma;
        _llEnable = llEnable;
        _autolevels = autolevels;
        _yellow = yellow;
        _cyan = cyan;
        _magenta = magenta;
        _ss = ss;
        _clampAlpha = clampAlpha;
        _rgbal = rgbal;
        _screenClipMin = screenClipMin;
        _screenClipMax = std::max(screenClipMin + 0.0001, screenClipMax); // avoid divisions by zero
        _screenReplace = screenReplace;
        if ( (screenReplace == eReplaceHardColor) || (screenReplace == eReplaceSoftColor) ) {
            _screenReplaceColor[0] = screenReplaceColor.r;
            _screenReplaceColor[1] = screenReplaceColor.g;
            _screenReplaceColor[2] = screenReplaceColor.b;
            // only normalize if the replace method is soft color
            if (screenReplace == eReplaceSoftColor) {
                if ( (_screenReplaceColor[0] == 0) && (_screenReplaceColor[1] == 0) && (_screenReplaceColor[2] == 0) ) {
                    _screenReplaceColor[0] = _screenReplaceColor[1] = _screenReplaceColor[2] = 1.;
                } else {
                    l = luminance(_colorspace, _screenReplaceColor);
                    _screenReplaceColor[0] /= l;
                    _screenReplaceColor[1] /= l;
                    _screenReplaceColor[2] /= l;
                }
            }
        }
        _sourceAlpha = sourceAlpha;
        _insideReplace = insideReplace;
        if ( (insideReplace == eReplaceHardColor) || (insideReplace == eReplaceSoftColor) ) {
            _insideReplaceColor[0] = insideReplaceColor.r;
            _insideReplaceColor[1] = insideReplaceColor.g;
            _insideReplaceColor[2] = insideReplaceColor.b;
            // only normalize if the replace method is soft color
            if (insideReplace == eReplaceSoftColor) {
                if ( (_insideReplaceColor[0] == 0) && (_insideReplaceColor[1] == 0) && (_insideReplaceColor[2] == 0) ) {
                    _insideReplaceColor[0] = _insideReplaceColor[1] = _insideReplaceColor[2] = 1.;
                } else {
                    l = luminance(_colorspace, _insideReplaceColor);
                    _insideReplaceColor[0] /= l;
                    _insideReplaceColor[1] /= l;
                    _insideReplaceColor[2] /= l;
                }
            }
        }
        _noKey = noKey;
        _ubl = ubl;
        _ubc = ubc;
        _colorspace = colorspace;
        _outputMode = outputMode;
    } // setValues
};


template<class PIX, int maxValue>
static float
sampleToFloat(PIX value)
{
    return (maxValue == 1) ? value : (value / (float)maxValue);
}

template<class PIX, int maxValue>
static PIX
floatToSample(float value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5f);
}

template<class PIX, int maxValue>
static PIX
floatToSample(double value)
{
    if (maxValue == 1) {
        return PIX(value);
    }
    if (value <= 0) {
        return 0;
    } else if (value >= 1.) {
        return maxValue;
    }

    return PIX(value * maxValue + 0.5);
}

template <class PIX, int nComponents, int maxValue>
class PIKProcessor
    : public PIKProcessorBase
{
public:
    PIKProcessor(ImageEffect &instance)
        : PIKProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 4);
        assert(!_fgImg || _fgImg->getPixelComponents() == ePixelComponentRGBA || _fgImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_pfgImg || _pfgImg->getPixelComponents() == ePixelComponentRGBA || _pfgImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_cImg || _cImg->getPixelComponents() == ePixelComponentRGBA || _cImg->getPixelComponents() == ePixelComponentRGB);
        assert(!_bgImg || _bgImg->getPixelComponents() == ePixelComponentRGBA || _bgImg->getPixelComponents() == ePixelComponentRGB);
        //assert(_fgImg); // crashes with Nuke
        const int fgComponents = _fgImg ? (_fgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int pfgComponents = _pfgImg ? (_pfgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int cComponents = _cImg ? (_cImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        const int bgComponents = _bgImg ? (_bgImg->getPixelComponents() == ePixelComponentRGBA ? 4 : 3) : 0;
        float c[4] = {_color[0], _color[1], _color[2], 1.};

        // first, handle the simple output modes (Source, Source Alpha, Inside Mask, Outside Mask)
        if (_outputMode == eOutputModeSource) {
            const Image *img = _pfgImg ? _pfgImg : _fgImg;
            const int comps = _pfgImg ? pfgComponents : fgComponents;

            for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                if ( _effect.abort() ) {
                    break;
                }

                PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                assert(dstPix);

                for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                    const PIX *pix = (const PIX *)  (img ? img->getPixelAddress(x, y) : 0 );
                    if (!pix) {
                        for (int i = 0; i < nComponents; ++i) {
                            dstPix[i] = PIX();
                        }
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            dstPix[i] = pix[i];
                        }
                        if (nComponents == 4) {
                            if (comps == 4) {
                                dstPix[3] = pix[3];
                            } else {
                                dstPix[3] = 1.;
                            }
                        }
                    }
                }
            }

            return;
        } else if (_outputMode == eOutputModeSourceAlpha) {
            const Image *img = _pfgImg ? _pfgImg : _fgImg;
            const int comps = _pfgImg ? pfgComponents : fgComponents;

            if (comps == 3) {
                // no alpha
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        for (int i = 0; i < nComponents; ++i) {
                            dstPix[i] = maxValue;
                        }
                    }
                }
            } else {
                // src has alpha
                assert(comps == 4);
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        const PIX *pix = (const PIX *)  (img ? img->getPixelAddress(x, y) : 0 );
                        if (!pix) {
                            for (int i = 0; i < nComponents; ++i) {
                                dstPix[i] = PIX();
                            }
                        } else {
                            for (int i = 0; i < 3; ++i) {
                                dstPix[i] = pix[3];
                            }
                            if (nComponents == 4) {
                                dstPix[3] = 1.;
                            }
                        }
                    }
                }
            }

            return;
        } else if (_outputMode == eOutputModeCleanPlate) {
            if (!_cImg) {
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        for (int i = 0; i < nComponents; ++i) {
                            dstPix[i] = floatToSample<PIX, maxValue>(c[i]);
                        }
                    }
                }
            } else {
                const Image *img = _cImg;
                const int comps = cComponents;

                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        const PIX *pix = (const PIX *)  (img ? img->getPixelAddress(x, y) : 0 );
                        if (!pix) {
                            for (int i = 0; i < nComponents; ++i) {
                                dstPix[i] = PIX();
                            }
                        } else {
                            for (int i = 0; i < 3; ++i) {
                                dstPix[i] = pix[i];
                            }
                            if (nComponents == 4) {
                                if (comps == 4) {
                                    dstPix[3] = pix[3];
                                } else {
                                    dstPix[3] = 1.;
                                }
                            }
                        }
                    }
                }
            }

            return;
        } else if (_outputMode == eOutputModeInsideMask) {
            const Image *img = _inMaskImg;

            if (!img) {
                // no alpha
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        for (int i = 0; i < nComponents; ++i) {
                            dstPix[i] = PIX();
                        }
                    }
                }
            } else {
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        const PIX *pix = (const PIX *)  (img ? img->getPixelAddress(x, y) : 0 );
                        if (!pix) {
                            for (int i = 0; i < nComponents; ++i) {
                                dstPix[i] = PIX();
                            }
                        } else {
                            for (int i = 0; i < 3; ++i) {
                                dstPix[i] = pix[0];
                            }
                            if (nComponents == 4) {
                                dstPix[3] = 1.;
                            }
                        }
                    }
                }
            }

            return;
        } else if (_outputMode == eOutputModeOutsideMask) {
            const Image *img = _outMaskImg;

            if (!img) {
                // no alpha
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        for (int i = 0; i < nComponents; ++i) {
                            dstPix[i] = PIX();
                        }
                    }
                }
            } else {
                for (int y = procWindow.y1; y < procWindow.y2; ++y) {
                    if ( _effect.abort() ) {
                        break;
                    }

                    PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
                    assert(dstPix);

                    for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                        const PIX *pix = (const PIX *)  (img ? img->getPixelAddress(x, y) : 0 );
                        if (!pix) {
                            for (int i = 0; i < nComponents; ++i) {
                                dstPix[i] = PIX();
                            }
                        } else {
                            for (int i = 0; i < 3; ++i) {
                                dstPix[i] = pix[0];
                            }
                            if (nComponents == 4) {
                                dstPix[3] = 1.;
                            }
                        }
                    }
                }
            }

            return;
        }


        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            assert(dstPix);

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                const PIX *fgPix = (const PIX *)  ( (_fgImg) ? _fgImg->getPixelAddress(x, y) : 0 );
                const PIX *pfgPix = (const PIX *)  ( (!_noKey && _pfgImg) ? _pfgImg->getPixelAddress(x, y) : 0 );
                const PIX *cPix = (const PIX *)  ( (/*!_noKey &&*/ _cImg) ? _cImg->getPixelAddress(x, y) : 0 );
                const PIX *bgPix = (const PIX *)  ( ( ( _ubc || _ubl || (_outputMode == eOutputModeComposite) ) && _bgImg ) ? _bgImg->getPixelAddress(x, y) : 0 );
                const PIX *inMaskPix = (const PIX *)  (_inMaskImg ? _inMaskImg->getPixelAddress(x, y) : 0);
                const PIX *outMaskPix = (const PIX *)  (_outMaskImg ? _outMaskImg->getPixelAddress(x, y) : 0);
                float inMask = inMaskPix ? sampleToFloat<PIX, maxValue>(*inMaskPix) : 0.f;
                if ( (_sourceAlpha == eSourceAlphaAddToInsideMask) && (nComponents == 4) && fgPix ) {
                    // take the max of inMask and the source Alpha
                    inMask = std::max( inMask, sampleToFloat<PIX, maxValue>(fgPix[3]) );
                }
                float outMask = outMaskPix ? sampleToFloat<PIX, maxValue>(*outMaskPix) : 0.f;

                // clamp inMask and outMask in the [0,1] range
                inMask = std::max( 0.f, std::min(inMask, 1.f) );
                outMask = std::max( 0.f, std::min(outMask, 1.f) );

                float fg[4] = {0., 0., 0., 1.};
                float pfg[4] = {0., 0., 0., 1.};
                float bg[4] = {0., 0., 0., 1.};
                float out[4] = {0., 0., 0., 1.};

                if (fgPix) {
                    for (int i = 0; i < fgComponents; ++i) {
                        fg[i] = sampleToFloat<PIX, maxValue>(fgPix[i]);
                    }
                }
                if (pfgPix) {
                    for (int i = 0; i < pfgComponents; ++i) {
                        pfg[i] = sampleToFloat<PIX, maxValue>(pfgPix[i]);
                        if (i < 3) {
                            pfg[i] /= _alphaBias[i];
                        }
                    }
                }
                if (cPix && !_useColor) {
                    for (int i = 0; i < cComponents; ++i) {
                        c[i] = sampleToFloat<PIX, maxValue>(cPix[i]);
                        if (i < 3) {
                            c[i] /= _alphaBias[i];
                        }
                    }
                }

                if ( bgPix && (_ubc || _ubl) ) {
                    for (int i = 0; i < bgComponents; ++i) {
                        bg[i] = sampleToFloat<PIX, maxValue>(bgPix[i]);
                    }
                }

                float status[4] = {0., 0., 0., 1.}; // only used for status output

                float alpha = 0.;
                if (_noKey) {
                    alpha = fg[3];
                } else {
                    if (_screenType == eScreenTypeGreen) {
                        if (c[1] <= 0.) {
                            alpha = 1.;
                        } else {
                            //alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))
                            //A is pfg and B is c.
                            double pfgKey = pfg[1] - pfg[0] * _redWeight - pfg[2] * _blueGreenWeight;
                            if (pfgKey <= 0.) {
                                alpha = 1.;
                            } else {
                                double cKey = c[1] - c[0] * _redWeight - c[2] * _blueGreenWeight;
                                if (cKey <= 0) {
                                    alpha = 1.;
                                } else {
                                    alpha = 1. - pfgKey / cKey;
#ifndef DISABLE_RGBAL
#pragma message WARN("RGBAL is not yet properly implemented")
                                    // wrong
                                    if (_rgbal) {
                                        float k[3] = {0., 0., 0.};
                                        for (int i = 0; i < 3; ++i) {
                                            if (c[i] > 0) {
                                                k[i] = pfg[i] / c[i];
                                            }
                                        }
                                        double kmax = -DBL_MAX;
                                        for (int i = 0; i < 3; ++i) {
                                            if (k[i] > kmax) {
                                                kmax = k[i];
                                            }
                                        }
                                        float kKey = pfgKey / cKey;
                                        if ( (kKey > kmax) && (kKey > 1.) ) {
                                            alpha = 0.; // the "zero zone" is OK
                                        } else {
                                            // the second part ((kmax - kKey) / (50*kKey)) is wrong, but that's
                                            // the closest I could get to IBK
                                            alpha = std::max( (double)alpha, std::min( (kmax - kKey) / (50 * kKey), 1. ) );
                                        }
                                    }
#endif
                                }
                            }
                        }
                    } else if (_screenType == eScreenTypeBlue) {
                        if (c[2] <= 0.) {
                            alpha = 1.;
                        } else {
                            //alpha = (Ag-Ar*rw-Ab*gbw)<=0?1:clamp(1-(Ag-Ar*rw-Ab*gbw)/(Bg-Br*rw-Bb*gbw))
                            //A is pfg and B is c.
                            double pfgKey = pfg[2] - pfg[0] * _redWeight - pfg[1] * _blueGreenWeight;
                            if (pfgKey <= 0.) {
                                alpha = 1.;
                            } else {
                                double cKey = c[2] - c[0] * _redWeight - c[1] * _blueGreenWeight;
                                if (cKey <= 0) {
                                    alpha = 1.;
                                } else {
                                    alpha = 1. - pfgKey / cKey;
#ifndef DISABLE_RGBAL
                                    if (_rgbal) {
                                        float k[3] = {0., 0., 0.};
                                        for (int i = 0; i < 3; ++i) {
                                            if (c[i] > 0) {
                                                k[i] = pfg[i] / c[i];
                                            }
                                        }
                                        double kmax = -DBL_MAX;
                                        for (int i = 0; i < 3; ++i) {
                                            if (k[i] > kmax) {
                                                kmax = k[i];
                                            }
                                        }
                                        float kKey = pfgKey / cKey;
                                        if ( (kKey > kmax) && (kKey > 1.) ) {
                                            alpha = 0.; // the "zero zone" is OK
                                        } else {
                                            // the second part ((kmax - kKey) / (50*kKey)) is wrong, but that's
                                            // the closest I could get to IBK
                                            alpha = std::max( (double)alpha, std::min( (kmax - kKey) / (50 * kKey), 1. ) );
                                        }
                                    }
#endif
                                }
                            }
                        }
                    }
                } // if (!_noKey)
                if (_outputMode == eOutputModeScreenMatte) {
                    for (int i = 0; i < 3; ++i) {
                        out[i] = alpha;
                    }
                    if (nComponents == 4) {
                        out[3] = 1.;
                    }
                    goto FINISH;
                }

                if (alpha <= 0) {
                    status[0] = status[1] = status[2] = 0.;
                } else if (alpha >= 1.) {
                    status[0] = status[1] = status[2] = 1.;
                } else {
                    status[0] = status[1] = status[2] = 0.5;
                }

                if ( !_ss || (alpha >= 1) ) {
                    for (int i = 0; i < 3; ++i) {
                        out[i] = fg[i];
                    }
                } else {
                    // screen subtraction / despill
                    for (int i = 0; i < 3; ++i) {
                        float v = fg[i] + c[i] * _despillBias[i] * (alpha - 1.);
                        out[i] = v < 0. ? 0 : v;
                    }
                }
                    /*
                       } else if (_rgbal) {
                       double alphamin = DBL_MAX;
                       for (int i = 0; i < 3; ++i) {
                        if (c[i] > 0) {
                            double a = 1. - pfg[i] / c[i];
                            if (a < alphamin) {
                                alphamin = a;
                            }
                        }
                       }
                       // alphamin, which corresponds to black is mapped to alpha=0
                       // alpha = alphamin -> 0.
                       // alpha = 1 -> 1.
                       alpha = (alpha - alphamin) / (1. - alphamin);
                       if (alpha <= 0.) {
                        alpha = 0.;
                        dstPix[0] = dstPix[1] = dstPix[2] = 0;
                        dstPix[0] = dstPix[1] = dstPix[2] = 1;alpha=1;
                       } else {
                        for (int i = 0; i < 3; ++i) {
                            dstPix[i] = floatToSample<PIX, maxValue>(fg[i]*alpha);
                        }
                       }
                     */

                if (_clampAlpha) {
                    if (alpha < 0.) {
                        alpha = 0.;
                    } else if (alpha > 1.) {
                        alpha = 1.;
                    }
                }
                ////////////////////////////////////////
                // Screen Matte options

                // the clip function is piecewise linear and continuous:
                // 0. from 0 to screenClipMin
                // 0. to 1. from screenClipMin to screenClipMax
                // 1. from screenClipMax to 1.
                float alphaClipped;
                if (alpha <= _screenClipMin) {
                    alphaClipped = 0.;
                } else if (alpha >= _screenClipMax) {
                    alphaClipped = 1.;
                } else {
                    alphaClipped = (alpha - _screenClipMin) / (_screenClipMax - _screenClipMin);
                }

                if (alphaClipped > alpha) {
                    float diff = alphaClipped - alpha;
                    // method 1
                    status[1] += diff / 2.;
                    // method 2
                    //status[0] = diff;
                    //status[1] = 1;
                    //status[2] = diff;

                    if (_outputMode == eOutputModePremultiplied ||
                        _outputMode == eOutputModeUnpremultiplied ||
                        _outputMode == eOutputModeComposite) {
                        switch (_screenReplace) {
                            case eReplaceNone:
                                // do nothing
                                break;

                            case eReplaceSource:
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + fg[i] * diff;
                                }
                                break;

                            case eReplaceHardColor:
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + _screenReplaceColor[i] * diff;
                                }
                                break;

                            case eReplaceSoftColor: {
                                // match the luminance of fg
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + _screenReplaceColor[i] * diff * luminance(_colorspace, fg);
                                }
                                break;
                            }
                        }
                    }
                    alpha = alphaClipped;
                } else if (alphaClipped < alpha) {
                    assert(alpha > 0.);
                    if (alphaClipped == 0.) {
                        status[0] = 0.;
                        status[1] = (alpha - alphaClipped) / 2.;
                        status[2] = 0.;
                    } else {
                        status[0] = 0.5 - (alpha - alphaClipped) / 2.;
                        status[1] = 0.5;
                        status[2] = 0.5 - (alpha - alphaClipped) / 2.;
                    }
                    // re-premultiply output
                    for (int i = 0; i < 3; ++i) {
                        out[i] = out[i] * alphaClipped / alpha; // no division by zero: alpha > 0
                    }
                    alpha = alphaClipped;
                }

                // nonadditive mix between the key generator and the garbage matte (outMask)
                // outside mask has priority over inside mask, treat inside first
                if ( (inMask > 0.) && (alpha < inMask) ) {
                    float diff = inMask - alpha;
                    // method 1
                    status[2] += diff / 2.;
                    // method 2
                    //status[0] = diff;
                    //status[1] = diff;
                    //status[2] = 1;

                    if (_outputMode == eOutputModePremultiplied ||
                        _outputMode == eOutputModeUnpremultiplied ||
                        _outputMode == eOutputModeComposite) {
                        switch (_insideReplace) {
                            case eReplaceNone:
                                // do nothing
                                break;

                            case eReplaceSource:
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + fg[i] * diff;
                                }
                                break;

                            case eReplaceHardColor:
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + _insideReplaceColor[i] * diff;
                                }
                                break;

                            case eReplaceSoftColor: {
                                // match the luminance of fg
                                for (int i = 0; i < 3; ++i) {
                                    out[i] = out[i] + _insideReplaceColor[i] * diff * luminance(_colorspace, fg);
                                }
                                break;
                            }
                        }
                    }
                    alpha = inMask;
                }

                if ( (outMask > 0.) && (alpha > 1. - outMask) ) {
                    assert(alpha > 0.);
                    status[1] -= ( alpha - (1. - outMask) ) / 2.;
                    status[2] -= ( alpha - (1. - outMask) ) / 2.;
                    if (_outputMode == eOutputModePremultiplied ||
                        _outputMode == eOutputModeUnpremultiplied ||
                        _outputMode == eOutputModeComposite) {
                        // re-premultiply output
                        for (int i = 0; i < 3; ++i) {
                            out[i] = out[i] * (1. - outMask) / alpha; // no division by zero: alpha > 0
                        }
                    }
                    alpha = 1. - outMask;
                }

                if (_outputMode == eOutputModeStatus) {
                    for (int i = 0; i < 4; ++i) {
                        out[i] = status[i];
                    }
                    goto FINISH;
                }
                if (_outputMode == eOutputModeCombinedMatte) {
                    for (int i = 0; i < 3; ++i) {
                        out[i] = alpha;
                    }
                    if (nComponents == 4) {
                        out[3] = 1.;
                    }
                    goto FINISH;
                }
                if (_outputMode == eOutputModeIntermediate) {
                    for (int i = 0; i < 3; ++i) {
                        out[i] = fg[i];
                    }
                    if (nComponents == 4) {
                        out[3] = alpha;
                    }
                    goto FINISH;
                }

                if (!_ss) { // if no screen subtraction, just premult
                    for (int i = 0; i < 3; ++i) {
                        out[i] = out[i] * alpha;
                    }
                }
                out[3] = alpha;

                // ubl, ubc
                if (_ubl || _ubc) {
                    // we use the CIE xyZ colorspace to separate luminance from chrominance
                    float out_Y, out_x, out_y;
                    // Convert to XYZ
                    {
                        float X, Y, Z, x, y, XYZ, invXYZ;
                        switch (_colorspace) {
                        case eColorspaceRec709:
                        default:
                            Color::rgb709_to_xyz(out[0], out[1], out[2], &X, &Y, &Z);
                            break;

                        case eColorspaceRec2020:
                            Color::rgb2020_to_xyz(out[0], out[1], out[2], &X, &Y, &Z);
                            break;

                        case eColorspaceACESAP0:
                            Color::rgbACESAP0_to_xyz(out[0], out[1], out[2], &X, &Y, &Z);
                            break;

                        case eColorspaceACESAP1:
                            Color::rgbACESAP1_to_xyz(out[0], out[1], out[2], &X, &Y, &Z);
                            break;
                        }
                        XYZ = X + Y + Z;
                        invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        // convert to xyY
                        x = X * invXYZ;
                        y = Y * invXYZ;

                        //out_X = X;
                        out_Y = Y;
                        //out_Z = Z;
                        out_x = x;
                        out_y = y;
                    }
                    float bg_Y, bg_x, bg_y;
                    {
                        float X, Y, Z, x, y, XYZ, invXYZ;
                        Color::rgb709_to_xyz(bg[0], bg[1], bg[2], &X, &Y, &Z);
                        XYZ = X + Y + Z;
                        invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        // convert to xyY
                        x = X * invXYZ;
                        y = Y * invXYZ;

                        //bg_X = X;
                        bg_Y = Y;
                        //bg_Z = Z;
                        bg_x = x;
                        bg_y = y;
                    }

                    // mix
                    float a = std::max(0.f, out[3]);
                    if ( _ubc && (bg_Y > 0.) ) {
                        out_x = a * out_x + (1 - a) * bg_x;
                        out_y = a * out_y + (1 - a) * bg_y;
                        //out_X = a * out_X + (1 - a) * bg_X;
                        //out_Z = a * out_Z + (1 - a) * bg_Z;
                    }
                    if (_ubl) {
                        // magic number (to look like IBK, really)
                        out_Y = out_Y * (a * 1 + (1 - a) * 5.38845 * bg_Y);
                    }

                    // convert to RGB
                    {
                        float Y = out_Y;
                        //float X = out_X;
                        //float Z = out_Z;
                        float X = (out_y == 0.) ? 0. : out_x * Y / out_y;
                        float Z = (out_y == 0.) ? 0. : (1. - out_x - out_y) * Y / out_y;

                        switch (_colorspace) {
                        case eColorspaceRec709:
                        default:
                            Color::xyz_to_rgb709(X, Y, Z, &out[0], &out[1], &out[2]);
                            break;

                        case eColorspaceRec2020:
                            Color::xyz_to_rgb2020(X, Y, Z, &out[0], &out[1], &out[2]);
                            break;

                        case eColorspaceACESAP0:
                            Color::xyz_to_rgbACESAP0(X, Y, Z, &out[0], &out[1], &out[2]);
                            break;

                        case eColorspaceACESAP1:
                            Color::xyz_to_rgbACESAP1(X, Y, Z, &out[0], &out[1], &out[2]);
                            break;
                        }
                    }
                }

#ifndef DISABLE_LM
#pragma message WARN("luminance match not yet implemented")
#endif
#ifndef DISABLE_AL
#pragma message WARN("autolevels not yet implemented")
#endif

                if (_outputMode == eOutputModeUnpremultiplied) {
                    if (out[3] <= 0) {
                        for (int i = 0; i < 3; ++i) {
                            out[i] = 0;
                        }
                    } else {
                        for (int i = 0; i < 3; ++i) {
                            out[i] = out[i] / out[3];
                        }
                    }
                    goto FINISH;
                }

                if (_outputMode == eOutputModeComposite) {
                    if (out[3] <= 0) {
                        for (int i = 0; i < 4; ++i) {
                            out[i] = bg[i];
                        }
                    } else {
                        for (int i = 0; i < 4; ++i) {
                            out[i] = out[i] + bg[i] * (1 - out[3]);
                        }
                    }
                    goto FINISH;
                }
FINISH:
                for (int i = 0; i < nComponents; ++i) {
                    dstPix[i] = floatToSample<PIX, maxValue>(out[i]);
                }
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class PIKPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    PIKPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _fgClip(NULL)
        , _pfgClip(NULL)
        , _cClip(NULL)
        , _bgClip(NULL)
        , _inMaskClip(NULL)
        , _outMaskClip(NULL)
        , _screenType(NULL)
        , _color(NULL)
        , _redWeight(NULL)
        , _blueGreenWeight(NULL)
        , _alphaBias(NULL)
        , _despillBias(NULL)
        , _despillBiasIsAlphaBias(NULL)
        , _lmEnable(NULL)
        , _level(NULL)
        , _luma(NULL)
        , _llEnable(NULL)
        , _autolevels(NULL)
        , _yellow(NULL)
        , _cyan(NULL)
        , _magenta(NULL)
        , _ss(NULL)
        , _clampAlpha(NULL)
        , _rgbal(NULL)
        , _screenClipMin(NULL)
        , _screenClipMax(NULL)
        , _screenReplace(NULL)
        , _screenReplaceColor(NULL)
        , _sourceAlpha(NULL)
        , _insideReplace(NULL)
        , _insideReplaceColor(NULL)
        , _noKey(NULL)
        , _ubl(NULL)
        , _ubc(NULL)
        , _colorspace(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _fgClip = fetchClip(kClipFg);
        assert( ( _fgClip && (!_fgClip->isConnected() || _fgClip->getPixelComponents() ==  ePixelComponentRGB ||
                              _fgClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _pfgClip = fetchClip(kClipPFg);
        assert( ( _pfgClip && (!_pfgClip->isConnected() || _pfgClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _pfgClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _cClip = fetchClip(kClipC);
        assert( ( _cClip && (!_cClip->isConnected() || _cClip->getPixelComponents() ==  ePixelComponentRGB ||
                             _cClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _bgClip = fetchClip(kClipBg);
        assert( _bgClip && (!_bgClip->isConnected() || _bgClip->getPixelComponents() == ePixelComponentRGB || _bgClip->getPixelComponents() == ePixelComponentRGBA) );
        _inMaskClip = fetchClip(kClipInsideMask);;
        assert( _inMaskClip && (!_inMaskClip->isConnected() || _inMaskClip->getPixelComponents() == ePixelComponentAlpha) );
        _outMaskClip = fetchClip(kClipOutsidemask);;
        assert( _outMaskClip && (!_outMaskClip->isConnected() || _outMaskClip->getPixelComponents() == ePixelComponentAlpha) );

        _screenType = fetchChoiceParam(kParamScreenType); // Screen Type: The type of background screen used for the key.
        _color = fetchRGBParam(kParamColor); // Screen Type: The type of background screen used for the key.
        _redWeight = fetchDoubleParam(kParamRedWeight); // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
        _blueGreenWeight = fetchDoubleParam(kParamBlueGreenWeight); // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
        _alphaBias = fetchRGBParam(kParamAlphaBias);
        _despillBias = fetchRGBParam(kParamDespillBias);
        _despillBiasIsAlphaBias = fetchBooleanParam(kParamDespillBiasIsAlphaBias);
        _lmEnable = fetchBooleanParam(kParamLMEnable); // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
        _level = fetchDoubleParam(kParamLevel); // Screen Range: Helps retain blacks and shadows.
        _luma = fetchDoubleParam(kParamLuma); // Luminance Level: Makes the matte more additive.
        _llEnable = fetchBooleanParam(kParamLLEnable); // Luminance Level Enable: Disable the luminance level when us bg influence.
        _autolevels = fetchBooleanParam(kParamAutolevels); // Autolevels: Removes hard edges from the matte.
        _yellow = fetchBooleanParam(kParamYellow); // Yellow: Override autolevel with yellow component.
        _cyan = fetchBooleanParam(kParamCyan); // Cyan: Override autolevel with cyan component.
        _magenta = fetchBooleanParam(kParamMagenta); // Magenta: Override autolevel with magenta component.
        _ss = fetchBooleanParam(kParamSS); // Screen Subtraction: Have the keyer subtract the foreground or just premult.
        _clampAlpha = fetchBooleanParam(kParamClampAlpha); // Clamp: Clamp matte to 0-1.
        _rgbal = fetchBooleanParam(kParamRGBAL); // Legalize rgba relationship.
        _screenClipMin = fetchDoubleParam(kParamScreenClipMin);
        _screenClipMax = fetchDoubleParam(kParamScreenClipMax);
        _screenReplace = fetchChoiceParam(kParamScreenReplace);
        _screenReplaceColor = fetchRGBParam(kParamScreenReplaceColor);
        _sourceAlpha = fetchChoiceParam(kParamSourceAlpha);
        _insideReplace = fetchChoiceParam(kParamInsideReplace);
        _insideReplaceColor = fetchRGBParam(kParamInsideReplaceColor);
        _noKey = fetchBooleanParam(kParamNoKey); // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
        _ubl = fetchBooleanParam(kParamUBL); // Use Bg Lum: Have the output rgb be biased by the bg luminance.
        _ubc = fetchBooleanParam(kParamUBC); // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
        _colorspace = fetchChoiceParam(kParamColorspace);
        _outputMode = fetchChoiceParam(kParamOutputMode);

        updateEnabled();
    }

private:
    /** @brief the get RoI action */
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(PIKProcessorBase &, const RenderArguments &args);

    void updateEnabled()
    {
        ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValue();
        bool noKey = _noKey->getValue();
        bool lmEnable = _lmEnable->getValue();
        bool llEnable = _llEnable->getValue();
        bool autolevels = _autolevels->getValue();
        OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();
        bool doSomething = !(outputMode == eOutputModeSource ||
                             outputMode == eOutputModeSourceAlpha ||
                             outputMode == eOutputModeInsideMask ||
                             outputMode == eOutputModeOutsideMask);

        bool doScreenSubtraction =  (outputMode == eOutputModePremultiplied ||
                                     outputMode == eOutputModeUnpremultiplied ||
                                     outputMode == eOutputModeComposite);

        bool doScreenMatte = (outputMode == eOutputModeScreenMatte ||
                              outputMode == eOutputModeCombinedMatte ||
                              outputMode == eOutputModeStatus ||
                              outputMode == eOutputModeIntermediate ||
                              doScreenSubtraction);

        _screenType->setEnabled(doSomething);
        _color->setEnabled(doSomething && screenType == eScreenTypePick);
        _redWeight->setEnabled(doSomething && doScreenMatte && !noKey);
        _blueGreenWeight->setEnabled(doSomething && doScreenMatte && !noKey);
        _alphaBias->setEnabled( doSomething && (doScreenMatte || doScreenSubtraction) );
        _despillBias->setEnabled( doSomething && doScreenSubtraction && !_despillBiasIsAlphaBias->getValue() );
        _despillBiasIsAlphaBias->setEnabled(doSomething && doScreenSubtraction);
        _lmEnable->setEnabled(doSomething && doScreenSubtraction);
        _level->setEnabled(doSomething && lmEnable && doScreenSubtraction);
        _llEnable->setEnabled(doSomething && lmEnable && doScreenSubtraction);
        _luma->setEnabled(doSomething && llEnable && doScreenSubtraction);
        _autolevels->setEnabled(doSomething && doScreenSubtraction);
        _yellow->setEnabled(doSomething && autolevels && doScreenSubtraction);
        _cyan->setEnabled(doSomething && autolevels && doScreenSubtraction);
        _magenta->setEnabled(doSomething && autolevels && doScreenSubtraction);
        _ss->setEnabled(doSomething && doScreenSubtraction);
        _clampAlpha->setEnabled(doSomething);
        _rgbal->setEnabled(doSomething);

        _screenReplace->setEnabled(doSomething && doScreenSubtraction);
        ReplaceEnum screenReplace = (ReplaceEnum)_screenReplace->getValue();
        bool hasScreenReplaceColor = (screenReplace == eReplaceSoftColor || screenReplace == eReplaceHardColor);
        _screenReplaceColor->setEnabled(doSomething && doScreenSubtraction && hasScreenReplaceColor);

        _insideReplace->setEnabled(doSomething && doScreenSubtraction);
        ReplaceEnum insideReplace = (ReplaceEnum)_insideReplace->getValue();
        bool hasInsideReplaceColor = (insideReplace == eReplaceSoftColor || insideReplace == eReplaceHardColor);
        _insideReplaceColor->setEnabled(doSomething && doScreenSubtraction && hasInsideReplaceColor);

        _ubl->setEnabled(doSomething && doScreenSubtraction);
        _ubc->setEnabled(doSomething && doScreenSubtraction);
        _colorspace->setEnabled(doSomething && doScreenSubtraction);
    }

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_fgClip;
    Clip *_pfgClip;
    Clip *_cClip;
    Clip *_bgClip;
    Clip *_inMaskClip;
    Clip *_outMaskClip;
    ChoiceParam* _screenType; // Screen Type: The type of background screen used for the key.
    RGBParam* _color;
    DoubleParam* _redWeight; // Red Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    DoubleParam* _blueGreenWeight; // Blue/Green Weight: Determines how the red channel and complement channel (blue for a green screen, green for a blue screen) are weighted in the keying calculation.
    RGBParam* _alphaBias;
    RGBParam* _despillBias;
    BooleanParam* _despillBiasIsAlphaBias;
    BooleanParam* _lmEnable; // Luminane Match Enable: Luminance Match Enable: Adds a luminance factor to the color difference algorithm.
    DoubleParam* _level; // Screen Range: Helps retain blacks and shadows.
    DoubleParam* _luma; // Luminance Level: Makes the matte more additive.
    BooleanParam* _llEnable; // Luminance Level Enable: Disable the luminance level when us bg influence.
    BooleanParam* _autolevels; // Autolevels: Removes hard edges from the matte.
    BooleanParam* _yellow; // Yellow: Override autolevel with yellow component.
    BooleanParam* _cyan; // Cyan: Override autolevel with cyan component.
    BooleanParam* _magenta; // Magenta: Override autolevel with magenta component.
    BooleanParam* _ss; // Screen Subtraction: Have the keyer subtract the foreground or just premult.
    BooleanParam* _clampAlpha; // Clamp: Clamp matte to 0-1.
    BooleanParam* _rgbal; // Legalize rgba relationship.
    DoubleParam* _screenClipMin;
    DoubleParam* _screenClipMax;
    ChoiceParam* _screenReplace;
    RGBParam* _screenReplaceColor;
    ChoiceParam* _sourceAlpha;
    ChoiceParam* _insideReplace;
    RGBParam* _insideReplaceColor;
    BooleanParam* _noKey; // No Key: Apply background luminance and chroma to Fg rgba input - no key is pulled.
    BooleanParam* _ubl; // Use Bg Lum: Have the output rgb be biased by the bg luminance.
    BooleanParam* _ubc; // Use Bg Chroma: Have the output rgb be biased by the bg chroma.
    ChoiceParam* _colorspace;
    ChoiceParam* _outputMode;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
PIKPlugin::setupAndProcess(PIKProcessorBase &processor,
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
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValueAtTime(time);
    OfxRGBColourD color = {0., 0., 1.};
    _color->getValueAtTime(time, color.r, color.g, color.b);
    double redWeight = _redWeight->getValueAtTime(time);
    double blueGreenWeight = _blueGreenWeight->getValueAtTime(time);
    OfxRGBColourD alphaBias = {0.5, 0.5, 0.5};
    _alphaBias->getValueAtTime(time, alphaBias.r, alphaBias.g, alphaBias.b);
    OfxRGBColourD despillBias = {0.5, 0.5, 0.5};
    if ( _despillBiasIsAlphaBias->getValueAtTime(time) ) {
        despillBias = alphaBias;
    } else {
        _despillBias->getValueAtTime(time, despillBias.r, despillBias.g, despillBias.b);
    }
    bool lmEnable = _lmEnable->getValueAtTime(time);
    double level = _level->getValueAtTime(time);
    double luma = _luma->getValueAtTime(time);
    bool llEnable = _llEnable->getValueAtTime(time);
    bool autolevels = _autolevels->getValueAtTime(time);
    bool yellow = _yellow->getValueAtTime(time);
    bool cyan = _cyan->getValueAtTime(time);
    bool magenta = _magenta->getValueAtTime(time);
    bool ss = _ss->getValueAtTime(time);
    bool clampAlpha = _clampAlpha->getValueAtTime(time);
    bool rgbal = _rgbal->getValueAtTime(time);
    double screenClipMin = _screenClipMin->getValueAtTime(time);
    double screenClipMax = _screenClipMax->getValueAtTime(time);
    ReplaceEnum screenReplace = (ReplaceEnum)_screenReplace->getValueAtTime(time);
    OfxRGBColourD screenReplaceColor = {0.5, 0.5, 0.5};
    _screenReplaceColor->getValueAtTime(time, screenReplaceColor.r, screenReplaceColor.g, screenReplaceColor.b);
    SourceAlphaEnum sourceAlpha = (SourceAlphaEnum)_sourceAlpha->getValueAtTime(time);
    ReplaceEnum insideReplace = (ReplaceEnum)_insideReplace->getValueAtTime(time);
    OfxRGBColourD insideReplaceColor = {0.5, 0.5, 0.5};
    _insideReplaceColor->getValueAtTime(time, insideReplaceColor.r, insideReplaceColor.g, insideReplaceColor.b);
    bool noKey = _noKey->getValueAtTime(time);
    bool ubl = _ubl->getValueAtTime(time);
    bool ubc = _ubc->getValueAtTime(time);
    ColorspaceEnum colorspace = (ColorspaceEnum)_colorspace->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    bool getfg = false;
    bool getpfg = false; // gets pfg or fg if pfg is not connected
    bool getc = false;
    bool getbg = false;
    bool getinm = false;
    bool getoutm = false;

    switch (outputMode) {
    case eOutputModeSource:
        getpfg = true;
        break;

    case eOutputModeSourceAlpha:
        getpfg = true;
        break;

    case eOutputModeCleanPlate:
        getc = true;
        break;

    case eOutputModeScreenMatte:
        getpfg = true;
        getc = true;
        break;

    case eOutputModeInsideMask:
        getinm = true;
        break;

    case eOutputModeOutsideMask:
        getoutm = true;
        break;

    case eOutputModeCombinedMatte:
        getpfg = true;
        getc = true;
        getinm = true;
        getoutm = true;
        break;

    case eOutputModeStatus:
        getpfg = true;
        getc = true;
        getinm = true;
        getoutm = true;
        break;

    case eOutputModeIntermediate:
        getfg = true;
        getpfg = true;
        getc = true;
        // bg not needed
        getinm = true;
        getoutm = true;
        break;

    case eOutputModePremultiplied:
    case eOutputModeUnpremultiplied:
    case eOutputModeComposite:
        getfg = true;
        getpfg = true;
        getc = true;
        getbg = true;
        getinm = true;
        getoutm = true;
        break;
    } // switch

    bool doSomething = !(outputMode == eOutputModeSource ||
                         outputMode == eOutputModeSourceAlpha ||
                         outputMode == eOutputModeInsideMask ||
                         outputMode == eOutputModeOutsideMask);

    getc = getc && doSomething && (screenType != eScreenTypePick);
    getbg = getbg && ( ubl || ubc || (outputMode == eOutputModeComposite) );

    auto_ptr<const Image> pfg( ( getpfg && !noKey && ( _pfgClip && _pfgClip->isConnected() ) ) ?
                                    _pfgClip->fetchImage(time) : 0 );
    if ( getpfg && !pfg.get() ) {
        getfg = true;
    }
    auto_ptr<const Image> fg( ( getfg && ( _fgClip && _fgClip->isConnected() ) ) ?
                                   _fgClip->fetchImage(time) : 0 );
    auto_ptr<const Image> c( ( getc && ( _cClip && _cClip->isConnected() ) ) ?
                                  _cClip->fetchImage(time) : 0 );
    auto_ptr<const Image> bg( ( getbg && ( _bgClip && _bgClip->isConnected() ) ) ?
                                   _bgClip->fetchImage(time) : 0 );
    auto_ptr<const Image> inMask( ( getinm && ( _inMaskClip && _inMaskClip->isConnected() ) ) ?
                                       _inMaskClip->fetchImage(time) : 0 );
    auto_ptr<const Image> outMask( ( getoutm && ( _outMaskClip && _outMaskClip->isConnected() ) ) ?
                                        _outMaskClip->fetchImage(time) : 0 );
    if ( fg.get() ) {
        if ( (fg->getRenderScale().x != args.renderScale.x) ||
             ( fg->getRenderScale().y != args.renderScale.y) ||
             ( ( fg->getField() != eFieldNone) /* for DaVinci Resolve */ && ( fg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum fgBitDepth      = fg->getPixelDepth();
        //PixelComponentEnum fgComponents = fg->getPixelComponents();
        if (fgBitDepth != dstBitDepth /* || fgComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else if (getfg) {
        // Nuke sometimes returns NULL when render is interrupted
        throwSuiteStatusException(kOfxStatFailed);
    }

    if ( pfg.get() ) {
        if ( (pfg->getRenderScale().x != args.renderScale.x) ||
             ( pfg->getRenderScale().y != args.renderScale.y) ||
             ( ( pfg->getField() != eFieldNone) /* for DaVinci Resolve */ && ( pfg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum pfgBitDepth      = pfg->getPixelDepth();
        //PixelComponentEnum pfgComponents = pfg->getPixelComponents();
        if (pfgBitDepth != dstBitDepth /* || pfgComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    if ( c.get() ) {
        if ( (c->getRenderScale().x != args.renderScale.x) ||
             ( c->getRenderScale().y != args.renderScale.y) ||
             ( ( c->getField() != eFieldNone) /* for DaVinci Resolve */ && ( c->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum cBitDepth      = c->getPixelDepth();
        //PixelComponentEnum cComponents = c->getPixelComponents();
        if (cBitDepth != dstBitDepth /* || cComponents != dstComponents*/) { // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else if (getc) {
        setPersistentMessage(Message::eMessageError, "", "Clean plate (C input) is required but not available or not connected");
        throwSuiteStatusException(kOfxStatFailed);
    }

    if ( bg.get() ) {
        if ( (bg->getRenderScale().x != args.renderScale.x) ||
             ( bg->getRenderScale().y != args.renderScale.y) ||
             ( ( bg->getField() != eFieldNone) /* for DaVinci Resolve */ && ( bg->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = bg->getPixelDepth();
        //PixelComponentEnum srcComponents = bg->getPixelComponents();
        if (srcBitDepth != dstBitDepth /* || srcComponents != dstComponents*/) {  // Keyer outputs RGBA but may have RGB input
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    } else if (getbg) {
        setPersistentMessage(Message::eMessageError, "", "Backgroung (Bg input) is required but not available or not connected");
        throwSuiteStatusException(kOfxStatFailed);
    }

    if ( inMask.get() ) {
        if ( (inMask->getRenderScale().x != args.renderScale.x) ||
             ( inMask->getRenderScale().y != args.renderScale.y) ||
             ( ( inMask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( inMask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if ( outMask.get() ) {
        if ( (outMask->getRenderScale().x != args.renderScale.x) ||
             ( outMask->getRenderScale().y != args.renderScale.y) ||
             ( ( outMask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( outMask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    processor.setValues(screenType, color, redWeight, blueGreenWeight, alphaBias, despillBias, lmEnable, level, luma, llEnable, autolevels, yellow, cyan, magenta, ss, clampAlpha, rgbal,
                        screenClipMin, screenClipMax, screenReplace, screenReplaceColor,
                        sourceAlpha, insideReplace, insideReplaceColor, noKey, ubl, ubc, colorspace, outputMode);
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( fg.get(), ( /*!noKey &&*/ !( _pfgClip && _pfgClip->isConnected() ) ) ? fg.get() : pfg.get(), c.get(), bg.get(), inMask.get(), outMask.get() );
    processor.setRenderWindow(args.renderWindow);

    processor.process();
} // PIKPlugin::setupAndProcess

// the overridden render function
void
PIKPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_fgClip || !_fgClip->isConnected() || _fgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_fgClip || !_fgClip->isConnected() || _fgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_pfgClip || !_pfgClip->isConnected() || _pfgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_pfgClip || !_pfgClip->isConnected() || _pfgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_cClip || !_cClip->isConnected() || _cClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_cClip || !_cClip->isConnected() || _cClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || !_bgClip || !_bgClip->isConnected() || _bgClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_bgClip || !_bgClip->isConnected() || _bgClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    if (dstComponents != ePixelComponentRGBA) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host dit not take into account output components");
        throwSuiteStatusException(kOfxStatErrImageFormat);

        return;
    }

    clearPersistentMessage();
    
    switch (dstBitDepth) {
    //case eBitDepthUByte: {
    //    PIKProcessor<unsigned char, 4, 255> fred(*this);
    //    setupAndProcess(fred, args);
    //    break;
    //}
    case eBitDepthUShort: {
        PIKProcessor<unsigned short, 4, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        PIKProcessor<float, 4, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

/** @brief the get RoI action */
void
PIKPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                RegionOfInterestSetter &rois)
{
    const double time = args.time;

    // this action does nothing but intersecting the roi with the rod of each input clip,
    // because Nuke forgets to do this and issues an error if one of the input clips is smaller, saying that the input RoI has negative sizes.
    // Maybe this should always be done in libSupport's Private::regionsOfInterestAction before calling getRegionsOfInterest?
    if ( Coords::rectIsEmpty(args.regionOfInterest) ) {
        return;
    }
    const OfxRectD emptyRoD = {0, 0, 1, 1}; // Nuke's reader issues an "out of memory" error when asked for an empty RoD
    std::vector<Clip*> inputClips;
    inputClips.push_back(_fgClip);

    bool noKey = _noKey->getValueAtTime(time);
    if (noKey) {
        rois.setRegionOfInterest(*_pfgClip, emptyRoD);
    } else {
        inputClips.push_back(_pfgClip);
    }
    inputClips.push_back(_cClip);
    bool ubl = _ubl->getValueAtTime(time);
    bool ubc = _ubc->getValueAtTime(time);
    if (!ubl && !ubc) {
        rois.setRegionOfInterest(*_bgClip, emptyRoD);
    } else {
        inputClips.push_back(_bgClip);
    }

    for (std::vector<Clip*>::const_iterator it = inputClips.begin();
         it != inputClips.end();
         ++it) {
        OfxRectD rod = (*it)->getRegionOfDefinition(args.time);
        // intersect the rod with args.regionOfInterest
        if ( Coords::rectIntersection(rod, args.regionOfInterest, &rod) ) {
            rois.setRegionOfInterest(*(*it), rod);
        } else {
            rois.setRegionOfInterest(*(*it), emptyRoD);
        }
    }
}

/* Override the clip preferences */
void
PIKPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the premultiplication of _dstClip
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValue();
    clipPreferences.setOutputPremultiplication(outputMode == eOutputModeUnpremultiplied ? eImageUnPreMultiplied : eImagePreMultiplied);

    // Output is RGBA
    clipPreferences.setClipComponents(*_dstClip, ePixelComponentRGBA);
    // note: Keyer handles correctly inputs with different components: it only uses RGB components from both clips
}

bool
PIKPlugin::isIdentity(const IsIdentityArguments &args,
                      Clip * &identityClip,
                      double &identityTime
                      , int& /*view*/, std::string& /*plane*/)
{
    const double time = args.time;
    OutputModeEnum outputModeEnum = (OutputModeEnum)_outputMode->getValueAtTime(time);

    if (outputModeEnum == eOutputModeSource) {
        identityClip = ( _pfgClip && _pfgClip->isConnected() ) ? _pfgClip : _fgClip;
        identityTime = time;

        return true;
    }
    if (outputModeEnum == eOutputModeCleanPlate) {
        ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValueAtTime(time);
        if (screenType != eScreenTypePick) {
            identityClip = _cClip;
            identityTime = time;

            return true;
        }
    }

    return false;
}

void
PIKPlugin::changedParam(const InstanceChangedArgs & /*args*/,
                        const std::string &paramName)
{
    //const double time = args.time;

    if ( (paramName == kParamScreenType) ||
         ( paramName == kParamDespillBiasIsAlphaBias) ||
         ( paramName == kParamNoKey) ||
         ( paramName == kParamLMEnable) ||
         ( paramName == kParamLLEnable) ||
         ( paramName == kParamAutolevels) ||
         ( paramName == kParamScreenReplace) ||
         ( paramName == kParamInsideReplace) ||
         ( paramName == kParamOutputMode) ) {
        updateEnabled();
    }
}

mDeclarePluginFactory(PIKPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
PIKPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedBitDepth(eBitDepthUByte);
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
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
PIKPlugin::getFramesNeeded(const FramesNeededArguments &args,
                           FramesNeededSetter &frames)
{
    const double time = args.time;
    ScreenTypeEnum screenType = (ScreenTypeEnum)_screenType->getValueAtTime(time);
    bool noKey = _noKey->getValueAtTime(time);
    bool ubl = _ubl->getValueAtTime(time);
    bool ubc = _ubc->getValueAtTime(time);
    OutputModeEnum outputMode = (OutputModeEnum)_outputMode->getValueAtTime(time);
    bool getfg = false;
    bool getpfg = false; // gets pfg or fg if pfg is not connected
    bool getc = false;
    bool getbg = false;
    bool getinm = false;
    bool getoutm = false;

    switch (outputMode) {
    case eOutputModeSource:
        getpfg = true;
        break;

    case eOutputModeSourceAlpha:
        getpfg = true;
        break;

    case eOutputModeCleanPlate:
        getc = true;
        break;

    case eOutputModeScreenMatte:
        getpfg = true;
        getc = true;
        break;

    case eOutputModeInsideMask:
        getinm = true;
        break;

    case eOutputModeOutsideMask:
        getoutm = true;
        break;

    case eOutputModeCombinedMatte:
        getpfg = true;
        getc = true;
        getinm = true;
        getoutm = true;
        break;

    case eOutputModeStatus:
        getpfg = true;
        getc = true;
        getinm = true;
        getoutm = true;
        break;

    case eOutputModeIntermediate:
        getfg = true;
        getpfg = true;
        getc = true;
        // bg not needed
        getinm = true;
        getoutm = true;
        break;

    case eOutputModePremultiplied:
    case eOutputModeUnpremultiplied:
    case eOutputModeComposite:
        getfg = true;
        getpfg = true;
        getc = true;
        getbg = true;
        getinm = true;
        getoutm = true;
        break;
    } // switch
    if (noKey) {
        getpfg = false;
    }
    getc = getc && ( /*!noKey*/true || (outputMode == eOutputModeCleanPlate) ) && screenType != eScreenTypePick;
    getbg = getbg && ( ubl || ubc || (outputMode == eOutputModeComposite) );

    if ( getpfg && !( _pfgClip && _pfgClip->isConnected() ) ) {
        getpfg = false;
        getfg = true;
    }
    OfxRangeD range;
    range.min = range.max = time;
    if (getfg) {
        frames.setFramesNeeded(*_fgClip, range);
    }
    if (getpfg) {
        frames.setFramesNeeded(*_pfgClip, range);
    }
    if (getc) {
        frames.setFramesNeeded(*_cClip, range);
    }
    if (getbg) {
        frames.setFramesNeeded(*_bgClip, range);
    }
    if (getinm) {
        frames.setFramesNeeded(*_inMaskClip, range);
    }
    if (getoutm) {
        frames.setFramesNeeded(*_outMaskClip, range);
    }
} // PIKPlugin::getFramesNeeded

void
PIKPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                    ContextEnum /*context*/)
{
    {
        ClipDescriptor* clip = desc.defineClip(kClipFg);
        clip->setHint(kClipFgHint);
        clip->addSupportedComponent( ePixelComponentRGBA );
        clip->addSupportedComponent( ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(false);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipPFg);
        clip->setHint(kClipPFgHint);
        clip->addSupportedComponent( ePixelComponentRGBA );
        clip->addSupportedComponent( ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipC);
        clip->setHint(kClipCHint);
        clip->addSupportedComponent( ePixelComponentRGBA );
        clip->addSupportedComponent( ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }
    {
        ClipDescriptor* clip = desc.defineClip(kClipBg);
        clip->setHint(kClipBgHint);
        clip->addSupportedComponent( ePixelComponentRGBA );
        clip->addSupportedComponent( ePixelComponentRGB );
        clip->setTemporalClipAccess(false);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setOptional(true);
    }

    // create the inside mask clip
    {
        ClipDescriptor *clip =  desc.defineClip(kClipInsideMask);
        clip->setHint(kClipInsideMaskHint);
        clip->addSupportedComponent(ePixelComponentAlpha);
        clip->setTemporalClipAccess(false);
        clip->setOptional(true);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setIsMask(true);
    }
    {
        // outside mask clip (garbage matte)
        ClipDescriptor *clip =  desc.defineClip(kClipOutsidemask);
        clip->setHint(kClipOutsideMaskHint);
        clip->addSupportedComponent(ePixelComponentAlpha);
        clip->setTemporalClipAccess(false);
        clip->setOptional(true);
        clip->setSupportsTiles(kSupportsTiles);
        clip->setIsMask(true);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);

    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    GroupParamDescriptor *group = NULL;

    // screenType
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamScreenType);
        param->setLabel(kParamScreenTypeLabel);
        param->setHint(kParamScreenTypeHint);
        assert(param->getNOptions() == (int)eScreenTypeGreen);
        param->appendOption(kParamScreenTypeOptionGreen);
        assert(param->getNOptions() == (int)eScreenTypeBlue);
        param->appendOption(kParamScreenTypeOptionBlue);
        assert(param->getNOptions() == (int)eScreenTypePick);
        param->appendOption(kParamScreenTypeOptionPick);
        param->setDefault( (int)kParamScreenTypeDefault );
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamColor);
        param->setLabel(kParamColorLabel);
        param->setHint(kParamColorHint);
        param->setDefault(0., 0., 1.);
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintDivider);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamNoKey);
        param->setLabel(kParamNoKeyLabel);
        param->setHint(kParamNoKeyHint);
        param->setDefault(kParamNoKeyDefault);
        param->setAnimates(false);
        //param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRedWeight);
        param->setLabel(kParamRedWeightLabel);
        param->setHint(kParamRedWeightHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamRedWeightDefault);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBlueGreenWeight);
        param->setLabel(kParamBlueGreenWeightLabel);
        param->setHint(kParamBlueGreenWeightHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamBlueGreenWeightDefault);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    // alpha bias
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamAlphaBias);
        param->setLabel(kParamAlphaBiasLabel);
        param->setHint(kParamAlphaBiasHint);
        param->setDefault(0.5, 0.5, 0.5);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    // despill bias
    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamDespillBias);
        param->setLabel(kParamDespillBiasLabel);
        param->setHint(kParamDespillBiasHint);
        param->setDefault(0.5, 0.5, 0.5);
        param->setAnimates(true);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamDespillBiasIsAlphaBias);
        param->setLabel(kParamDespillBiasIsAlphaBiasLabel);
        param->setHint(kParamDespillBiasIsAlphaBiasHint);
        param->setDefault(true);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintDivider);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamLMEnable);
        param->setLabel(kParamLMEnableLabel);
        param->setHint(kParamLMEnableHint);
        param->setDefault(kParamLMEnableDefault);
        param->setAnimates(false);
#ifdef DISABLE_LM
        param->setIsSecretAndDisabled(true);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLevel);
        param->setLabel(kParamLevelLabel);
        param->setHint(kParamLevelHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamLevelDefault);
        param->setAnimates(true);
#ifdef DISABLE_LM
        param->setIsSecretAndDisabled(true);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamLuma);
        param->setLabel(kParamLumaLabel);
        param->setHint(kParamLumaHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 1.);
        param->setDefault(kParamLumaDefault);
        param->setAnimates(true);
#ifdef DISABLE_LM
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamLLEnable);
        param->setLabel(kParamLLEnableLabel);
        param->setHint(kParamLLEnableHint);
        param->setDefault(kParamLLEnableDefault);
        param->setAnimates(false);
#ifdef DISABLE_LM
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintDivider);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAutolevels);
        param->setLabel(kParamAutolevelsLabel);
        param->setHint(kParamAutolevelsHint);
        param->setDefault(kParamAutolevelsDefault);
        param->setAnimates(false);
#ifdef DISABLE_AL
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamYellow);
        param->setLabel(kParamYellowLabel);
        param->setHint(kParamYellowHint);
        param->setDefault(kParamYellowDefault);
        param->setAnimates(false);
#ifdef DISABLE_AL
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamCyan);
        param->setLabel(kParamCyanLabel);
        param->setHint(kParamCyanHint);
        param->setDefault(kParamCyanDefault);
        param->setAnimates(false);
#ifdef DISABLE_AL
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMagenta);
        param->setLabel(kParamMagentaLabel);
        param->setHint(kParamMagentaHint);
        param->setDefault(kParamMagentaDefault);
        param->setAnimates(false);
#ifdef DISABLE_AL
        param->setIsSecretAndDisabled(true);
#else
        param->setLayoutHint(eLayoutHintDivider);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamSS);
        param->setLabel(kParamSSLabel);
        param->setHint(kParamSSHint);
        param->setDefault(kParamSSDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamClampAlpha);
        param->setLabel(kParamClampAlphaLabel);
        param->setHint(kParamClampAlphaHint);
        param->setDefault(kParamClampAlphaDefault);
        param->setAnimates(false);
#ifndef DISABLE_RGBAL
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRGBAL);
        param->setLabel(kParamRGBALLabel);
        param->setHint(kParamRGBALHint);
        param->setDefault(kParamRGBALDefault);
        param->setAnimates(false);
#ifdef DISABLE_RGBAL
        param->setIsSecretAndDisabled(true);
#endif
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupScreenMatte);
        if (group) {
            group->setLabel(kGroupScreenMatteLabel);
            //group->setHint(kGroupScreenMatteHint);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
        }

        // clip min
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamScreenClipMin);
            param->setLabel(kParamScreenClipMinLabel);
            param->setHint(kParamScreenClipMinHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // clip max
        {
            DoubleParamDescriptor* param = desc.defineDoubleParam(kParamScreenClipMax);
            param->setLabel(kParamScreenClipMaxLabel);
            param->setHint(kParamScreenClipMaxHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 1.);
            param->setDefault(1.);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // screen replace
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamScreenReplace);
            param->setLabel(kParamScreenReplaceLabel);
            param->setHint(kParamScreenReplaceHint);
            assert(param->getNOptions() == (int)eReplaceNone);
            param->appendOption(kParamReplaceOptionNone);
            assert(param->getNOptions() == (int)eReplaceSource);
            param->appendOption(kParamReplaceOptionSource);
            assert(param->getNOptions() == (int)eReplaceHardColor);
            param->appendOption(kParamReplaceOptionHardColor);
            assert(param->getNOptions() == (int)eReplaceSoftColor);
            param->appendOption(kParamReplaceOptionSoftColor);
            param->setDefault( (int)eReplaceSoftColor );
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // screen replace color
        {
            RGBParamDescriptor* param = desc.defineRGBParam(kParamScreenReplaceColor);
            param->setLabel(kParamScreenReplaceColorLabel);
            param->setHint(kParamScreenReplaceColorHint);
            param->setDefault(0.5, 0.5, 0.5);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }

    {
        GroupParamDescriptor* group = desc.defineGroupParam(kGroupInsideMask);
        if (group) {
            group->setLabel(kGroupInsideMaskLabel);
            //group->setHint(kGroupInsideMaskHint);
            group->setOpen(false);
            if (page) {
                page->addChild(*group);
            }
        }

        // source alpha
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamSourceAlpha);
            param->setLabel(kParamSourceAlphaLabel);
            param->setHint(kParamSourceAlphaHint);
            assert(param->getNOptions() == (int)eSourceAlphaIgnore);
            param->appendOption(kParamSourceAlphaOptionIgnore);
            assert(param->getNOptions() == (int)eSourceAlphaAddToInsideMask);
            param->appendOption(kParamSourceAlphaOptionAddToInsideMask);
            //assert(param->getNOptions() == (int)eSourceAlphaNormal);
            //param->appendOption(kParamSourceAlphaOptionNormal, kParamSourceAlphaOptionNormalHint);
            param->setDefault( (int)eSourceAlphaIgnore );
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // inside replace
        {
            ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamInsideReplace);
            param->setLabel(kParamInsideReplaceLabel);
            param->setHint(kParamInsideReplaceHint);
            assert(param->getNOptions() == (int)eReplaceNone);
            param->appendOption(kParamReplaceOptionNone);
            assert(param->getNOptions() == (int)eReplaceSource);
            param->appendOption(kParamReplaceOptionSource);
            assert(param->getNOptions() == (int)eReplaceHardColor);
            param->appendOption(kParamReplaceOptionHardColor);
            assert(param->getNOptions() == (int)eReplaceSoftColor);
            param->appendOption(kParamReplaceOptionSoftColor);
            param->setDefault( (int)eReplaceSoftColor );
            param->setAnimates(false);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
        // inside replace color
        {
            RGBParamDescriptor* param = desc.defineRGBParam(kParamInsideReplaceColor);
            param->setLabel(kParamInsideReplaceColorLabel);
            param->setHint(kParamInsideReplaceColorHint);
            param->setDefault(0.5, 0.5, 0.5);
            param->setAnimates(true);
            if (group) {
                param->setParent(*group);
            }
            if (page) {
                page->addChild(*param);
            }
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUBL);
        param->setLabel(kParamUBLLabel);
        param->setHint(kParamUBLHint);
        param->setDefault(kParamUBLDefault);
        param->setAnimates(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUBC);
        param->setLabel(kParamUBCLabel);
        param->setHint(kParamUBCHint);
        param->setDefault(kParamUBCDefault);
        param->setAnimates(false);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamColorspace);
        param->setLabel(kParamColorspaceLabel);
        param->setHint(kParamColorspaceHint);
        assert(param->getNOptions() == eColorspaceRec709);
        param->appendOption(kParamColorspaceOptionRec709);
        assert(param->getNOptions() == eColorspaceRec2020);
        param->appendOption(kParamColorspaceOptionRec2020);
        assert(param->getNOptions() == eColorspaceACESAP0);
        param->appendOption(kParamColorspaceOptionACESAP0);
        assert(param->getNOptions() == eColorspaceACESAP1);
        param->appendOption(kParamColorspaceOptionACESAP1);
        param->setDefault( (int)eColorspaceRec709 );
        param->setLayoutHint(eLayoutHintDivider);
        param->setAnimates(false);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamOutputMode);
        param->setLabel(kParamOutputModeLabel);
        param->setHint(kParamOutputModeHint);
        assert(param->getNOptions() == eOutputModeSource);
        param->appendOption(kParamOutputModeOptionSource);
        assert(param->getNOptions() == eOutputModeSourceAlpha);
        param->appendOption(kParamOutputModeOptionSourceAlpha);
        assert(param->getNOptions() == eOutputModeCleanPlate);
        param->appendOption(kParamOutputModeOptionCleanPlate);
        assert(param->getNOptions() == eOutputModeScreenMatte);
        param->appendOption(kParamOutputModeOptionScreenMatte);
        assert(param->getNOptions() == eOutputModeInsideMask);
        param->appendOption(kParamOutputModeOptionInsideMask);
        assert(param->getNOptions() == eOutputModeOutsideMask);
        param->appendOption(kParamOutputModeOptionOutsideMask);
        assert(param->getNOptions() == eOutputModeCombinedMatte);
        param->appendOption(kParamOutputModeOptionCombinedMatte);
        assert(param->getNOptions() == eOutputModeStatus);
        param->appendOption(kParamOutputModeOptionStatus);
        assert(param->getNOptions() == eOutputModeIntermediate);
        param->appendOption(kParamOutputModeOptionIntermediate);
        assert(param->getNOptions() == eOutputModePremultiplied);
        param->appendOption(kParamOutputModeOptionPremultiplied);
        assert(param->getNOptions() == eOutputModeUnpremultiplied);
        param->appendOption(kParamOutputModeOptionUnpremultiplied);
        assert(param->getNOptions() == eOutputModeComposite);
        param->appendOption(kParamOutputModeOptionComposite);
        param->setDefault( (int)eOutputModePremultiplied );
        param->setLayoutHint(eLayoutHintDivider);
        param->setAnimates(false);
        desc.addClipPreferencesSlaveParam(*param);
        if (group) {
            // coverity[dead_error_line]
            param->setParent(*group);
        }
        if (page) {
            page->addChild(*param);
        }
    }
} // PIKPluginFactory::describeInContext

ImageEffect*
PIKPluginFactory::createInstance(OfxImageEffectHandle handle,
                                 ContextEnum /*context*/)
{
    return new PIKPlugin(handle);
}

static PIKPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
