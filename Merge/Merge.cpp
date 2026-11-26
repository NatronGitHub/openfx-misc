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
 * OFX Merge plugin.
 */

#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <bitset>
#ifdef DEBUG
#include <iostream>
#endif

#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMultiPlane.h"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "ofxsNonFinite.h" // finiteSample

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

// A helper class that adds version numbers to the ImageEffect class.
// Why wasn't it there in the first place?
class ImageEffectVersioned
    : public OFX::ImageEffect
{
public:
      /** @brief ctor */
    ImageEffectVersioned(OfxImageEffectHandle handle,
                         unsigned int majorVersion,
                         unsigned int minorVersion)
    : OFX::ImageEffect(handle)
    , _maj(majorVersion)
    , _min(minorVersion)
    {}

    /** @brief dtor */
    virtual ~ImageEffectVersioned() {}

    unsigned int getMajorVersion() { return _maj; }
    unsigned int getMinorVersion() { return _min; }

private:
    unsigned int _maj;
    unsigned int _min;
};

#define kPluginName "MergeOFX"
#define kPluginGrouping "Merge"
#define kPluginDescriptionStart \
    "Pixel-by-pixel merge operation between two or more inputs.\n" \
    "Input A is first merged with B (or with a black and transparent background if B is not connected), then A2, if connected, is merged with the intermediary result, then A3, etc.\n\n" \
    "A description of most operators is available in the W3C Compositing and Blending Level 1 Recommendation https://www.w3.org/TR/compositing-1/ and a complete explanation of the Porter-Duff compositing operators can be found in \"Compositing Digital Images\", by T. Porter and T. Duff (Proc. SIGGRAPH 1984) http://keithp.com/~keithp/porterduff/p253-porter.pdf\n" \
    "\n"
#define kPluginDescriptionStartRoto \
    "Pixel-by-pixel merge operation between two inputs using and external alpha channel for input A.\n" \
    "All channels from input A are merged with those from B, using RotoMask as the alpha channel for input A: the alpha channel from A is thus merged onto the alpha channel from B using the RotoMask as the alpha value (\"a\" in the formulas).\n" \
    "This may be useful, for example, to \"paint\" alpha values from A onto the alpha channel of B using a given operation with an external alpha mask (which may be opaque even where the alpha channel of A is zero).\n\n" \
    "A description of most operators is available in the W3C Compositing and Blending Level 1 Recommendation https://www.w3.org/TR/compositing-1/ and a complete explanation of the Porter-Duff compositing operators can be found in \"Compositing Digital Images\", by T. Porter and T. Duff (Proc. SIGGRAPH 1984) http://keithp.com/~keithp/porterduff/p253-porter.pdf\n" \
    "\n"
#define kPluginDescriptionMidRGB \
    "Note that if an input with only RGB components is connected to A or B, its alpha channel " \
    "is considered to be opaque (one) by default, thus the output will be completely opaque if " \
    "the checkbox for channel A of input B is checked. One reason for this behaviour is that " \
    "non-zero RGB values with a zero A value are not valid alpha-premultiplied RGBA values. " \
    "If the user wishes to keep the background fully transparent, it can only be black, which " \
    "is equivalent to not using the merge operator. Non-black fully transparent pixels should " \
    "never appear anywhere in a proper compositing graph.\n" \
    "\n"
#define kPluginDescriptionEnd \
    "See also:\n" \
    "\n" \
    "- \"Digital Image Compositing\" by Marc Levoy https://graphics.stanford.edu/courses/cs248-06/comp/comp.html\n" \
    "- \"Compositing and Blending Level 1\" https://www.w3.org/TR/compositing-1/\n" \
    "- \"SVG Compositing Specification\" https://www.w3.org/TR/SVGCompositing/\n" \
    "- \"ISO 32000-1:2008: Portable Document Format (July 2008)\", Sec. 11.3 \"Basic Compositing Operations\"  http://www.adobe.com/devnet/pdf/pdf_reference.html\n" \
    "- \"Merge\" by Martin Constable https://web.archive.org/web/20220627015903/http://www.opticalenquiry.com/nuke/index.php?title=Merge" "\n" \
    "- \"Merge Blend Modes\" by Martin Constable https://web.archive.org/web/20220807185657/http://www.opticalenquiry.com/nuke/index.php?title=Merge_Blend_Modes" "\n" \
    "- \"Primacy of the B Feed\" by Martin Constable https://web.archive.org/web/20220807185551/http://www.opticalenquiry.com/nuke/index.php?title=Primacy_of_the_B_Feed" "\n" \
    "- grain-extract and grain-merge are described in http://docs.gimp.org/en/gimp-concepts-layer-modes.html"

// merge plugin // default merge function
enum MergePluginEnum
{
    eMergePluginMerge, // eMergeOver
    eMergePluginPlus, // eMergePlus
    eMergePluginMatte, // eMergeMatte
    eMergePluginMultiply, // eMergeMultiply
    eMergePluginIn, // eMergeIn
    eMergePluginOut, // eMergeOut
    eMergePluginScreen, // eMergeScreen
    eMergePluginMax, // eMergeMax
    eMergePluginMin, // eMergeMin
    eMergePluginAbsMinus, // eMergeDifference
    eMergePluginRoto, // eMergeOver
};

#define kPluginGroupingSub "Merge/Merges"

#define kPluginNamePlus "PlusOFX"
#define kPluginNameMatte "MatteOFX"
#define kPluginNameMultiply "MultiplyOFX"
#define kPluginNameIn "InOFX"
#define kPluginNameOut "OutOFX"
#define kPluginNameScreen "ScreenOFX"
#define kPluginNameMax "MaxOFX"
#define kPluginNameMin "MinOFX"
#define kPluginNameAbsminus "AbsminusOFX"
#define kPluginNameRoto "RotoMerge"

#define kPluginIdentifier "net.sf.openfx.MergePlugin"
#define kPluginIdentifierSub "net.sf.openfx.Merge"
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
// History:
// 1.0: Original version
// 2.0: After fixing https://github.com/NatronGitHub/Natron/issues/647
// 1.1 and 2.1 (every Merge plug-in, kept for backward compatibility): a merge result beyond the float range
//      gives +-FLT_MAX, not +-inf, and a huge masked alpha (a + b - ab) no longer gives NaN (SupportExt #80);
//      infinite A and B values are made finite (+-FLT_MAX) before the operator, instead of giving NaN
//      (inf - inf, inf * 0) (SupportExt #82);
//      MergeRoto also renders on hosts without the plane suite v2 (e.g. OFX 1.4 hosts), with no roto mask and
//      the Mask clip image as the mask (render failed before);
//      MergeRoto: a NaN or infinite RotoMask sample gives a finite result (NaN is 0, +-inf is +-FLT_MAX, as in
//      the mask mix), instead of NaN (issue #220).
// 3.0: With Alpha-only images, the A checkboxes (instead of the R checkboxes) of A and B control the alpha channel.
//      A inputs after the first one are merged as transparent black outside of their image (instead of being skipped).
//      As in 1.1 and 2.1, the SupportExt #80 and #82 fixes, MergeRoto renders on hosts without the plane suite v2,
//      and the issue #220 fix (non-finite RotoMask samples).
// 1.1, 2.1 and 3.0 (unreleased, fixed in place): in the filter context, B is the mandated Source clip
//      (RGBA or Alpha), and Source and Output are no longer remapped there (issue #217). The general
//      context, used by Natron, is unchanged.
// 1.1 and 2.1 (unreleased, fixed in place): with Alpha-only images, B is not passed through when B's R checkbox,
//      which controls the alpha in these versions, is unchecked (issue #328).
// 1.1, 2.1 and 3.0 (unreleased, fixed in place, issue #328): with A disconnected (and an operator that gives B
//      with B alone, or any operator in version 1), the output channels are B's channels whose B checkbox is on,
//      else 0, and the unchecked output channels are B's (B was copied whole, and the unchecked output channels
//      were 0); Mask and Mix apply. Natron 2.5 shipped Merge 1.0 and 2.0: its projects change only with A
//      disconnected and a B or output channel checkbox off (with all the B checkboxes on, the result is still B)
//      (bug fix, minor bump).
// 3.0 (unreleased, fixed in place): in the general context, the time domain is the frame range of B, or of
//      A with the new "Range From" parameter (default B), instead of the host default (issue #277).
//      Rendered images are unchanged. Versions 1 and 2 keep the host default, with no parameter.
// 3.0 (unreleased, fixed in place, issue #277): the operators match the tests/nuke reference (MergeV3):
//      Alpha masking leaves each input unchanged where the other one has zero alpha, with a blend term on
//      premultiplied colors (it only set the output alpha), and its alpha is a+b-ab in [0, 1], else max(a, b);
//      in the separable operators (not the HSL modes, which are unchanged), alpha values outside [0, 1] are
//      clamped where they weigh the inputs (e.g. over), and the alpha of atop is b; divide is A where B <= 0 (it
//      was 0); soft-light is 2AB+BB(1-AB) (it was the W3C formula; two opaque inputs give an alpha of 2);
//      screen, exclusion, hard-light, overlay, geometric, color-dodge and color-burn change outside [0, 1].
//      Versions 1 and 2 render as before.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint \
    "The operation used to merge the input A and B images.\n" \
    "The operator formula is applied to each component: A and B represent the input component (Red, Green, Blue, or Alpha) of each input, and a and b represent the alpha channel of each input.\n" \
    "If Alpha masking is checked, the output alpha is computed using a different formula (a+b - a*b).\n" \
    "Alpha masking is always enabled for HSL modes (hue, saturation, color, luminosity)."

#define kParamAlphaMasking "screenAlpha"
#define kParamAlphaMaskingLabel "Alpha masking"
#define kParamAlphaMaskingHint "When enabled, the input images are unchanged where the other image has 0 alpha, and" \
    " the output alpha is set to a+b - a*b. When disabled the alpha channel is processed as " \
    "any other channel. Option is disabled for operations where it does not apply or makes no difference."
#define kParamAlphaMaskingHintV3 "When enabled, the input images are unchanged where the other image has 0 alpha, and" \
    " the output alpha is set to a+b - a*b (or max(a, b) if a or b is outside [0, 1]). Where both alphas are not 0, " \
    "the color is A(1-b) + B(1-a) plus a blend term on the premultiplied colors, which is the operator where both inputs " \
    "are opaque. When disabled the alpha channel is processed as " \
    "any other channel. Option is disabled for operations where it does not apply or makes no difference."

#define kParamBBox "bbox"
#define kParamBBoxLabel "Bounding Box"
#define kParamBBoxHint "What to use to produce the output image's bounding box."
#define kParamBBoxOptionUnion "Union", "Union of all connected inputs.", "union"
#define kParamBBoxOptionIntersection "Intersection", "Intersection of all connected inputs.", "intersection"
#define kParamBBoxOptionA "A", "Bounding box of input A.", "a"
#define kParamBBoxOptionB "B", "Bounding box of input B.", "b"

enum BBoxEnum
{
    eBBoxUnion = 0,
    eBBoxIntersection,
    eBBoxA,
    eBBoxB
};

#define kParamAChannels       "AChannels"
#define kParamAChannelsLabel  "A Channels"
#define kParamAChannelsHint   "Channels to use from A input(s) (other channels are set to zero)."
#define kParamAChannelsR      "AChannelsR"
#define kParamAChannelsRLabel "R"
#define kParamAChannelsRHint  "Use red channel from A input(s)."
#define kParamAChannelsG      "AChannelsG"
#define kParamAChannelsGLabel "G"
#define kParamAChannelsGHint  "Use green channel from A input(s)."
#define kParamAChannelsB      "AChannelsB"
#define kParamAChannelsBLabel "B"
#define kParamAChannelsBHint  "Use blue channel from A input(s)."
#define kParamAChannelsA      "AChannelsA"
#define kParamAChannelsALabel "A"
#define kParamAChannelsAHint  "Use alpha channel from A input(s)."

#define kParamBChannels       "BChannels"
#define kParamBChannelsLabel  "B Channels"
#define kParamBChannelsHint   "Channels to use from B input (other channels are set to zero)."
#define kParamBChannelsR      "BChannelsR"
#define kParamBChannelsRLabel "R"
#define kParamBChannelsRHint  "Use red channel from B input."
#define kParamBChannelsG      "BChannelsG"
#define kParamBChannelsGLabel "G"
#define kParamBChannelsGHint  "Use green channel from B input."
#define kParamBChannelsB      "BChannelsB"
#define kParamBChannelsBLabel "B"
#define kParamBChannelsBHint  "Use blue channel from B input."
#define kParamBChannelsA      "BChannelsA"
#define kParamBChannelsALabel "A"
#define kParamBChannelsAHint  "Use alpha channel from B input."

#define kParamOutputChannels       "OutputChannels"
#define kParamOutputChannelsLabel  "Output"
#define kParamOutputChannelsHint   "Channels from result to write to output (other channels are taken from B input)."
#define kParamOutputChannelsR      "OutputChannelsR"
#define kParamOutputChannelsRLabel "R"
#define kParamOutputChannelsRHint  "Write red channel to output."
#define kParamOutputChannelsG      "OutputChannelsG"
#define kParamOutputChannelsGLabel "G"
#define kParamOutputChannelsGHint  "Write green channel to output."
#define kParamOutputChannelsB      "OutputChannelsB"
#define kParamOutputChannelsBLabel "B"
#define kParamOutputChannelsBHint  "Write blue channel to output."
#define kParamOutputChannelsA      "OutputChannelsA"
#define kParamOutputChannelsALabel "A"
#define kParamOutputChannelsAHint  "Write alpha channel to output."

#define kParamRangeFrom "rangeFrom"
#define kParamRangeFromLabel "Range From"
#define kParamRangeFromHint "Which input's frame range is passed to the output (the frame range of the effect). If that input is not connected, the host decides."
#define kParamRangeFromOptionB "B", "Frame range of input B.", "b"
#define kParamRangeFromOptionA "A", "Frame range of input A.", "a"
#define kRangeFromMinMajor 3 // Range From is in versions 3 and later (versions 1 and 2: the host default)

enum RangeFromEnum
{
    eRangeFromB = 0,
    eRangeFromA,
};

#define kParamAChannelsAChanged "aChannelsChanged" // did the user explicitly change the "A" checkbox for A input?
#define kParamBChannelsAChanged "bChannelsChanged" // did the user explicitly change the "A" checkbox for B input?

#define kRotoMaskPlaneID "RotoMask"

#define kClipA "A"
#define kClipAHint "The image sequence to merge with input B."
#define kClipB "B"
#define kClipBHint "The main input. This input is passed through when the merge node is disabled."
// in the filter context, B is the mandated Source clip (ofxImageEffectContexts.rst, The Filter Context)
#define kClipBName(context) ( (context) == eContextFilter ? kOfxImageEffectSimpleSourceClipName : kClipB )
#define kClipRotoMask "RotoMask"
#define kClipRotoMaskHint "A roto mask, which is used as the alpha channel in the merge operations."

#define kMaximumAInputs 64

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

using namespace MergeImages2D;

static const char* alphaChannels[1] = {"A"};

static const MultiPlane::ImagePlaneDesc& getRotoMaskPlane()
{
    static const MultiPlane::ImagePlaneDesc plane(kRotoMaskPlaneID, "", "Alpha", alphaChannels, 1);
    return plane;
}

static std::string getRotoMaskPlaneOFXString()
{
    const MultiPlane::ImagePlaneDesc &plane = getRotoMaskPlane();
    return MultiPlane::ImagePlaneDesc::mapPlaneToOFXPlaneString(plane);
}

static
MergingFunctionEnum
getDefaultOperation(MergePluginEnum e)
{
    switch(e) {
        case eMergePluginMerge:
            return eMergeOver;
        case eMergePluginPlus:
            return eMergePlus;
        case eMergePluginMatte:
            return eMergeMatte;
        case eMergePluginMultiply:
            return eMergeMultiply;
        case eMergePluginIn:
            return eMergeIn;
        case eMergePluginOut:
            return eMergeOut;
        case eMergePluginScreen:
            return eMergeScreen;
        case eMergePluginMax:
            return eMergeMax;
        case eMergePluginMin:
            return eMergeMin;
        case eMergePluginAbsMinus:
            return eMergeDifference;
        case eMergePluginRoto:
            return eMergeOver;
    }
    return eMergeOver;
}

static
std::string
unsignedToString(unsigned i)
{
    if (i == 0) {
        return "0";
    }
    std::string nb;
    for (unsigned j = i; j != 0; j /= 10) {
        nb = (char)( '0' + (j % 10) ) + nb;
    }

    return nb;
}

/*
   For explanations on why we use bitset instead of vector<bool>, see:

   D. Kalev. What You Should Know about vector<bool>.
   http://www.informit.com/guides/content.aspx?g=cplusplus&seqNum=98

   S. D. Meyers. Effective STL: 50 Specific Ways to Improve Your Use of the Standard Template Library.
   Item 18: "Avoid using vector<bool>".
   Professional Computing Series. Addison-Wesley, Boston, 4 edition, 2004

   V. Pieterse et al. Performance of C++ Bit-vector Implementations
   http://www.cs.up.ac.za/cs/vpieterse/pub/PieterseEtAl_SAICSIT2010.pdf
 */

// With Alpha-only images (nComponents == 1), the only component is alpha, so it is controlled by the
// A checkboxes. Versions 1 and 2 used the R checkboxes of A and B for it, which is kept so that old
// projects render the same. Used by the processor and isIdentity().
constexpr bool
alphaUsesAChannel(int nComponents,
                  unsigned int majorVersion)
{
    return (nComponents == 1) && (majorVersion >= 3);
}

// Merge 3 (issue #277): the operators that differ from SupportExt's (ofxsMerging.h) outside [0, 1], alpha
// values clamped to [0, 1] where they weigh the inputs, and Alpha masking that leaves each input unchanged where
// the other one is transparent. Merge 1 and 2 keep MergeImages2D::mergePixel() unchanged.
// The results match the tests/nuke reference (tests/nuke/cases.py: the Merge cases, with values from -1 to 3 and
// alphas from -0.5 to 1.44). Foundry's documentation of the Merge node
// (https://learn.foundry.com/nuke/content/reference_guide/merge_nodes/merge.html) gives the formulas in [0, 1].
// Floating point only (maxValue 1): the processor works in float.
namespace MergeV3 {

// The formula of the operators that differ from SupportExt's in Merge 3, or an empty string.
inline const char*
formula(MergingFunctionEnum f)
{
    switch (f) {
    case eMergeColorBurn:

        return "1-(1-B)/max(A,1-B), with B>=0; 0 if A<=0";
    case eMergeColorDodge:

        return "B/max(1-A,B), with A,B>=0; 1 if A>=1";
    case eMergeDivide:

        return "A/B if B>0, otherwise A";
    case eMergeExclusion:

        return "A+B-2AB if A,B>=0, otherwise A+B";
    case eMergeGeometric:

        return "2AB/(A+B) if A,B>0, otherwise 0";
    case eMergeHardLight:

        return "2AB if 2A<1, otherwise 1-2(1-A)(1-B), with A,B>=0, and A,B<=1 in 1-2(1-A)(1-B)";
    case eMergeOverlay:

        return "2AB if 2B<1, otherwise 1-2(1-A)(1-B), with A,B>=0, and A,B<=1 in 1-2(1-A)(1-B)";
    case eMergeScreen:

        return "A+B-AB if 0<A<1 and 0<B<1, otherwise max(A,B)";
    case eMergeSoftLight:

        return "2AB+BB(1-AB) if AB<1, otherwise 2AB, with A,B>=0";
    default:

        return "";
    }
}

inline double
positive(double v)
{
    return (v > 0.) ? v : 0.; // NaN gives 0
}

inline double
clamp01(double v)
{
    return (v > 1.) ? 1. : positive(v);
}

// "A+B-AB if 0<A<1 and 0<B<1, otherwise max(A,B)"
inline double
screen(double A,
       double B)
{
    return ( (0. < A) && (A < 1.) && (0. < B) && (B < 1.) ) ? (A + B - A * B) : (std::max)(A, B);
}

// The operator f on one channel (Alpha masking off): A and B are the channel values, a and b the alphas
// (in [0, 1]). f is separable (not an HSL mode).
template <MergingFunctionEnum f>
double
channel(double A,
        double B,
        double a,
        double b)
{
    switch (f) {
    case eMergeColorBurn: {
        if (A <= 0.) {
            return 0.;
        }
        const double Bp = positive(B);

        return 1. - (1. - Bp) / (std::max)(A, 1. - Bp); // the denominator is >= A > 0
    }
    case eMergeColorDodge: {
        if (A >= 1.) {
            return 1.;
        }
        const double Bp = positive(B);

        return Bp / (std::max)(1. - positive(A), Bp); // the denominator is >= 1 - A > 0
    }
    case eMergeDivide:

        return (B > 0.) ? (A / B) : A;
    case eMergeExclusion:

        return ( (A >= 0.) && (B >= 0.) ) ? (A + B - 2. * A * B) : (A + B);
    case eMergeGeometric:

        return ( (A > 0.) && (B > 0.) ) ? (2. * A * B / (A + B)) : 0.;
    case eMergeHardLight:

        return (2. * A < 1.) ? (2. * positive(A) * positive(B)) : ( 1. - 2. * ( 1. - (std::min)(A, 1.) ) * ( 1. - clamp01(B) ) );
    case eMergeOverlay:

        return (2. * B < 1.) ? (2. * positive(A) * positive(B)) : ( 1. - 2. * ( 1. - (std::min)(B, 1.) ) * ( 1. - clamp01(A) ) );
    case eMergeScreen:

        return screen(A, B);
    case eMergeSoftLight: {
        A = positive(A);
        B = positive(B);
        const double AB = A * B;

        return (AB < 1.) ? (2. * AB + B * B * (1. - AB) ) : (2. * AB);
    }
    default: {
        // the other separable operators are those of SupportExt, on one channel
        const float Af = static_cast<float>(A);
        const float Bf = static_cast<float>(B);
        float r = 0.f;
        MergeImages2D::mergePixel<f, float, 1, 1>(false, &Af, static_cast<float>(a), &Bf, static_cast<float>(b), &r);

        return r;
    }
    }
} // channel

// The operators whose A and B are made positive in Alpha masking too, as without it.
constexpr bool
masksPositiveInputs(MergingFunctionEnum f)
{
    return (f == eMergeColorBurn) || (f == eMergeColorDodge) || (f == eMergeHardLight) || (f == eMergeOverlay) ||
           (f == eMergeSoftLight);
}

// The blend term T of Alpha masking, on premultiplied colors: the color is A(1-b) + B(1-a) + T, which is A where
// b = 0 and B where a = 0 if T is 0 there, and channel(A, B) where both inputs are opaque. Most terms are
// ab channel(A/a, B/b), written without the division (as in the premultiplied formulas of the SVG Compositing
// Specification). The exceptions, which match the tests/nuke reference, whose masking cases have alphas from 0
// to 1 in both inputs: geometric (unweighted 2AB/(A+B)), from, minus and hypot (ab times the operator on A and B),
// divide (A/B where B > 0, also where b = 0, instead of ab (A/a)/(B/b) = A b^2 / B; Ab elsewhere) and soft-light
// (2AB + (a/b)B^2(1 - AB/a) where AB < a, instead of the AB/(ab) and AB < ab of ab f(A/a, B/b)). a and b are in [0, 1].
template <MergingFunctionEnum f>
double
maskedTerm(double A,
           double B,
           double a,
           double b)
{
    const double Ab = A * b;
    const double Ba = B * a;
    switch (f) {
    case eMergeAverage:

        return (Ab + Ba) / 2.;
    case eMergeColorBurn:

        return ( (A <= 0.) || (Ab + Ba <= a * b) ) ? 0. : ( a * (Ab + Ba - a * b) / A );
    case eMergeColorDodge:

        return ( (a > 0.) && (b > 0.) ) ? ( a * b * channel<f>(A / a, B / b, 1., 1.) ) : 0.;
    case eMergeDifference:

        return std::abs(Ab - Ba);
    case eMergeDivide:

        return (B > 0.) ? (A / B) : Ab;
    case eMergeExclusion:

        return ( (A >= 0.) && (B >= 0.) ) ? (Ab + Ba - 2. * A * B) : (Ab + Ba);
    case eMergeFrom:

        return a * b * (B - A);
    case eMergeGeometric:

        return channel<f>(A, B, a, b);
    case eMergeHardLight:

        return (2. * A < a) ? (2. * A * B) : ( a * b - 2. * ( b - (std::min)(B, b) ) * ( a - (std::min)(A, a) ) );
    case eMergeHypot:

        return a * b * std::sqrt(A * A + B * B);
    case eMergeMax:

        return (std::max)(Ab, Ba);
    case eMergeMin:

        return (std::min)(Ab, Ba);
    case eMergeMinus:

        return a * b * (A - B);
    case eMergeMultiply:

        return ( (A < 0.) && (B < 0.) ) ? Ab : (A * B);
    case eMergeOverlay:

        return (2. * B < b) ? (2. * A * B) : ( a * b - 2. * ( a - (std::min)(A, a) ) * ( b - (std::min)(B, b) ) );
    case eMergePlus:

        return Ab + Ba;
    case eMergeSoftLight:

        return ( (a > 0.) && (b > 0.) && (A * B < a) ) ? ( 2. * A * B + (a / b) * B * B * (1. - A * B / a) ) : (2. * A * B);
    default:

        // the operators that are not in Foundry's list (freeze, grain-extract, grain-merge, pinlight,
        // reflect): the general formula of W3C Compositing and Blending Level 1, 0 where an alpha is 0
        return ( (a > 0.) && (b > 0.) ) ? ( a * b * channel<f>(A / a, B / b, 1., 1.) ) : 0.;
    }
} // maskedTerm

inline float
clampAlpha(float a)
{
    // NaN stays NaN
    return (a < 0.f) ? 0.f : ( (a > 1.f) ? 1.f : a );
}

// Merge 3's mergePixel(): A and B have nComponents (at most 4) components, a and b are their alphas.
// - The HSL modes are those of Merge 2 (MergeImages2D::mergePixel()).
// - In the other (separable) operators, the alphas a and b are clamped to [0, 1] where they weigh the inputs
//   (over: "A+B(1-a)" with a = 1.44 gave a negative contribution of B).
// - Alpha masking (RGBA, with a maskable operator): "the input images are unchanged where the other image
//   has zero alpha, and the output alpha is set to a+b-a*b" (Foundry): the color is A(1-b) + B(1-a) + T
//   (maskedTerm()), and the alpha is a+b-ab in [0, 1], else max(a, b) (as screen), as in matte. Merge 2 only
//   replaced the output alpha.
template <MergingFunctionEnum f, int nComponents>
void
mergePixel(bool doAlphaMasking,
           const float *A,
           float a,
           const float *B,
           float b,
           float* dst)
{
    if ( !isSeparable(f) ) {
        MergeImages2D::mergePixel<f, float, nComponents, 1>(doAlphaMasking, A, a, B, b, dst);

        return;
    }
    float Af[4] = {};
    float Bf[4] = {};
    for (int i = 0; i < nComponents; ++i) {
        Af[i] = mergeFiniteInput<float>(A[i]);
        Bf[i] = mergeFiniteInput<float>(B[i]);
    }
    const float aRaw = mergeFiniteInput<float>(a);
    const float bRaw = mergeFiniteInput<float>(b);
    a = clampAlpha(aRaw);
    b = clampAlpha(bRaw);
    const bool masked = (nComponents == 4) && doAlphaMasking && isMaskable(f);
    if ( (nComponents == 4) && ( masked || (f == eMergeMatte) ) ) {
        // the alpha of Alpha masking and of matte ("Aa+B(1-a)"): a+b-ab in [0, 1], else max(a, b)
        dst[3] = mergeDoubleToPIX<float>( screen(aRaw, bRaw) );
    } else if ( (nComponents == 4) && (f == eMergeATop) ) {
        // atop, "Ab+B(1-a). Where a,b>=0", on the alpha: the alpha of A is clamped to [0, 1] like a, and b is not,
        // so that the output alpha is b
        dst[3] = mergeDoubleToPIX<float>( clamp01(Af[3]) * static_cast<double>(bRaw) + static_cast<double>(Bf[3]) * (1. - a) );
    }
    const int maxComp = ( (nComponents == 4) && ( masked || (f == eMergeMatte) || (f == eMergeATop) ) ) ? 3 : nComponents;
    for (int i = 0; i < maxComp; ++i) {
        if (!masked) {
            dst[i] = mergeDoubleToPIX<float>( channel<f>(Af[i], Bf[i], a, b) );
        } else {
            double Ai = Af[i];
            double Bi = Bf[i];
            if ( masksPositiveInputs(f) ) {
                Ai = positive(Ai);
                Bi = positive(Bi);
            }
            dst[i] = mergeDoubleToPIX<float>( Ai * (1. - b) + Bi * (1. - a) + maskedTerm<f>(Ai, Bi, a, b) );
        }
    }
} // mergePixel
} // namespace MergeV3

// The description of operator e in Merge majorVersion: the formula of the operators that Merge 3 changed
// (MergeV3::formula()), else SupportExt's.
std::string
operationDescription(MergingFunctionEnum e,
                     unsigned int majorVersion)
{
    const char* formula = (majorVersion >= 3) ? MergeV3::formula(e) : "";
    if (*formula == '\0') {
        return getOperationDescription(e);
    }
    std::string description(formula);
    if (e == eMergeSoftLight) {
        // the alpha channel follows the same formula: two opaque inputs give an alpha of 2
        description += " (on the alpha too: two opaque inputs give 2, unless Alpha masking is checked)";
    }

    return description;
}

// getOperationHelp(e, true) (markdown) with the description of Merge majorVersion
std::string
operationHelpMarkdown(MergingFunctionEnum e,
                      unsigned int majorVersion)
{
    const char* formula = (majorVersion >= 3) ? MergeV3::formula(e) : "";
    if (*formula == '\0') {
        return getOperationHelp(e, true);
    }
    std::string help = getOperationString(e) + ": ";
    for (const char* c = formula; *c != '\0'; ++c) {
        if ( std::strchr("\\`*_{}[]()#+-.!", *c) ) {
            help += '\\'; // a markdown special character
        }
        help += *c;
    }

    return help;
}

// isIdentityForBOnly() in Merge majorVersion: in Merge 3, screen with A = 0 is "max(A,B)", which is 0, not B,
// where B is negative.
inline bool
isIdentityForBOnlyVersioned(MergingFunctionEnum f,
                            unsigned int majorVersion)
{
    return isIdentityForBOnly(f) && !( (majorVersion >= 3) && (f == eMergeScreen) );
}

// The merge of the processor: Merge 3's (MergeV3::mergePixel()), or that of Merge 1 and 2, unchanged.
template <MergingFunctionEnum f, int nComponents>
void
mergePixelVersioned(unsigned int majorVersion,
                    bool doAlphaMasking,
                    const float *A,
                    float a,
                    const float *B,
                    float b,
                    float* dst)
{
    if (majorVersion >= 3) {
        MergeV3::mergePixel<f, nComponents>(doAlphaMasking, A, a, B, b, dst);
    } else {
        MergeImages2D::mergePixel<f, float, nComponents, 1>(doAlphaMasking, A, a, B, b, dst);
    }
}

class MergeProcessorBase
    : public ImageProcessor
{
protected:
    unsigned int _majorVersion;
    std::vector<const Image*> _srcImgAs;
    const Image *_srcImgB;
    const Image* _rotoMaskImgB;
    const Image *_maskImg;
    std::vector<const Image*> _rotoMaskImgAs;
    bool _doMasking;
    bool _alphaMasking;
    double _mix;
    bool _maskInvert;
    std::bitset<4> _aChannels;
    std::bitset<4> _bChannels;
    std::bitset<4> _outputChannels;

public:

    MergeProcessorBase(ImageEffectVersioned &instance)
        : ImageProcessor(instance)
        , _majorVersion( instance.getMajorVersion() )
        , _srcImgAs()
        , _srcImgB(NULL)
        , _rotoMaskImgB(NULL)
        , _maskImg(NULL)
        , _rotoMaskImgAs()
        , _doMasking(false)
        , _alphaMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _aChannels()
        , _bChannels()
        , _outputChannels()
    {
    }

    void setSrcImg(const std::vector<const Image*>& As,
                   const Image *B)
    {
        _srcImgAs = As;
        _srcImgB = B;
    }

    void setMaskInvert(bool invert)
    {
         _maskInvert = invert;
    }

    void setMaskImg(const Image *v) { _maskImg = v; }

    void setRotoMaskImg(const std::vector<const Image*>& rotoMaskAs, const Image *rotoMaskB) {
        _rotoMaskImgAs = rotoMaskAs;
        _rotoMaskImgB = rotoMaskB;
    }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool alphaMasking,
                   double mix,
                   std::bitset<4> aChannels,
                   std::bitset<4> bChannels,
                   std::bitset<4> outputChannels)
    {
        _alphaMasking = alphaMasking;
        _mix = mix;
        assert(aChannels.size() == 4 && bChannels.size() == 4 && outputChannels.size() == 4);
        _aChannels = aChannels;
        _bChannels = bChannels;
        _outputChannels = outputChannels;
    }
};


template <MergingFunctionEnum f, class PIX, int nComponents, int maxValue>
class MergeProcessor
    : public MergeProcessorBase
{
public:
    MergeProcessor(ImageEffectVersioned &instance)
        : MergeProcessorBase(instance)
    {
    }

private:
    // Return the alpha of the A input i at (x, y), and premultiply tmpA by the roto mask of that input, if any.
    float getAlphaA(std::size_t i,
                    const PIX *srcPixA,
                    float tmpA[nComponents],
                    int x,
                    int y) const
    {
        float a;
        if (i >= _rotoMaskImgAs.size() || !_rotoMaskImgAs[i]) {
            if (nComponents == 4) {
                a = tmpA[nComponents - 1];
            } else if (nComponents == 1) {
                a = tmpA[0];
            } else {
                a = (_aChannels[3] && srcPixA) ? 1. : 0.;
            }
        } else {
            const PIX *rotoMaskPix = (const PIX *)_rotoMaskImgAs[i]->getPixelAddress(x, y);
            if (rotoMaskPix) {
                // a non-finite float roto mask sample is made finite (NaN gives 0, +-inf gives +-FLT_MAX,
                // as in ofxsMaskMixPix); mergePixel keeps the overflowing products finite (issue #220)
                a = finiteSample<PIX, maxValue>(*rotoMaskPix) / (float)maxValue;
            } else {
                a = 0.;
            }
            if (_maskInvert) {
                a = 1. - a;
            }
            // When rendering the RotoMask plane, srcImg and rotoMask image point to the same image
            if (_rotoMaskImgAs[i] != _srcImgAs[i]) {
                // Premult all A pixels by the roto mask
                for (int c = 0; c < nComponents; ++c) {
                    tmpA[c] *= a;
                }
            }
        }

        return a;
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        float tmpPix[nComponents];
        float tmpA[nComponents];
        float tmpB[nComponents];
        const bool alphaFromA = alphaUsesAChannel(nComponents, _majorVersion);

        for (int c = 0; c < nComponents; ++c) {
            tmpA[c] = tmpB[c] = 0.;
        }
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                // If the operator is not identity when B only is connected, still process
                // one A input, even if none is connected.
                // This behavior was introduced in version 2.0.
                if ( _srcImgAs.size() == 0 && ( isIdentityForBOnlyVersioned(f, _majorVersion) || _majorVersion == 1 ) ) {
                    // B alone: the checked output channels are B's channels whose B checkbox is on, else 0,
                    // and the unchecked output channels are B's (issue #328; matches tests/nuke). Before,
                    // B was copied whole, and the unchecked output channels were 0.
                    const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);
                    for (int c = 0; c < nComponents; ++c) {
                        const bool bChannel = _bChannels[alphaFromA ? 3 : c];
                        tmpPix[c] = (bChannel && srcPixB) ? static_cast<float>(srcPixB[c]) : 0.f;
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                    for (int c = 0; c < nComponents; ++c) {
                        if (!_outputChannels[nComponents > 1 ? c : 3]) {
                            dstPix[c] = srcPixB ? srcPixB[c] : 0;
                        }
                    }
                } else {
                    // process the first connected A input first

                    std::size_t i = 0;
                    const PIX *srcPixA = (const PIX *)  ((_srcImgAs.size() && _srcImgAs[i]) ? _srcImgAs[i]->getPixelAddress(x, y) : 0);
                    const PIX *srcPixB = (const PIX *)  (_srcImgB ? _srcImgB->getPixelAddress(x, y) : 0);


                    float b = 0.;
                    if (_rotoMaskImgB) {
                        const PIX *rotoMaskPix = (const PIX *)_rotoMaskImgB->getPixelAddress(x, y);
                        if (rotoMaskPix) {
                            b = finiteSample<PIX, maxValue>(*rotoMaskPix) / (float)maxValue; // see getAlphaA
                            if (_maskInvert) {
                                b = 1. - b;
                            }
                        }
                    }

                    if (srcPixA || srcPixB) {
                        for (std::size_t c = 0; c < nComponents; ++c) {
                            // all images are supposed to be black and transparent outside o
                            tmpA[c] = (_aChannels[alphaFromA ? 3 : c] && srcPixA) ? ( (float)srcPixA[c] / maxValue ) : 0.f;
                            tmpB[c] = (_bChannels[alphaFromA ? 3 : c] && srcPixB) ? ( (float)srcPixB[c] / maxValue ) : 0.f;
                        }

                        // work in float: clamping is done when mixing
                        float a = getAlphaA(i, srcPixA, tmpA, x, y);
                        if (!_rotoMaskImgB) {
                            if (nComponents == 4) {
                                b = tmpB[nComponents - 1];
                            } else if (nComponents == 1) {
                                b = tmpB[0];
                            } else {
                                b = (_bChannels[3] && srcPixB) ? 1. : 0.;
                            }
                        }

                        mergePixelVersioned<f, nComponents>(_majorVersion, _alphaMasking, tmpA, a, tmpB, b, tmpPix);
                    } else {
                        // everything is black and transparent
                        for (int c = 0; c < nComponents; ++c) {
                            tmpPix[c] = 0;
                        }
                    }

                    for (std::size_t i = 1; i < _srcImgAs.size(); ++i) {
                        // process the other connected A inputs

                        srcPixA = (const PIX *)  (_srcImgAs[i] ? _srcImgAs[i]->getPixelAddress(x, y) : 0);

                        // Since version 3, like the first A input, the other A inputs are
                        // merged as transparent black outside of their image.
                        // Before version 3, they were skipped there.
                        if ( srcPixA || (_majorVersion >= 3) ) {
                            for (std::size_t c = 0; c < nComponents; ++c) {
                                // all images are supposed to be black and transparent outside o
                                tmpA[c] = (_aChannels[alphaFromA ? 3 : c] && srcPixA) ? ( (float)srcPixA[c] / maxValue ) : 0.f;
                            }

                            // work in float: clamping is done when mixing
                            float a = getAlphaA(i, srcPixA, tmpA, x, y);

                            // Update b from the previously computed value.
                            // see https://github.com/MrKepzie/Natron/issues/1648
                            if (nComponents == 4) {
                                b = tmpPix[nComponents - 1];
                            } else if (nComponents == 1) {
                                b = tmpPix[0];
                            } else {
                                b = 1.;
                            }

                            mergePixelVersioned<f, nComponents>(_majorVersion, _alphaMasking, tmpA, a, tmpPix, b, tmpPix);
                        }
                    }

                    // tmpPix has 4 components, but we only need the first nComponents

                    // denormalize
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] *= maxValue;
                    }

                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                    for (int c = 0; c < nComponents; ++c) {
                        if (!_outputChannels[nComponents > 1 ? c : 3]) {
                            dstPix[c] = srcPixB ? srcPixB[c] : 0;
                        }
                    }
                }

                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class MergePlugin
    : public ImageEffectVersioned
{
public:
    /** @brief ctor */
    MergePlugin(OfxImageEffectHandle handle,
                int majorVersion,
                int minorVersion,
                MergePluginEnum plugin,
                bool numerousInputs)
    : ImageEffectVersioned(handle, majorVersion, minorVersion)
    , _pluginType(plugin)
    , _dstClip(NULL)
    , _srcClipAs()
    , _srcClipB(NULL)
    , _maskClip(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || OFX_COMPONENTS_OK(_dstClip->getPixelComponents())) );
        _srcClipAs.push_back( fetchClip(kClipA) );
        assert( _srcClipAs[0] && (!_srcClipAs[0]->isConnected() || OFX_COMPONENTS_OK(_srcClipAs[0]->getPixelComponents())) );

        if (numerousInputs) {
            _srcClipAs.resize(kMaximumAInputs);
            for (int i = 2; i <= kMaximumAInputs; ++i) {
                Clip* clip = fetchClip( std::string(kClipA) + unsignedToString(i) );
                assert( clip && (!clip->isConnected() || clip->getPixelComponents() == ePixelComponentRGB || clip->getPixelComponents() == ePixelComponentRGBA || clip->getPixelComponents() == ePixelComponentAlpha) );
                _srcClipAs[i - 1] = clip;
            }
        }

        _srcClipB = fetchClip( kClipBName( getContext() ) );
        assert( _srcClipB && (!_srcClipB->isConnected() || _srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA || _srcClipB->getPixelComponents() == ePixelComponentAlpha) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);


        _operation = fetchChoiceParam(kParamOperation);
        _operationString = fetchStringParam(kNatronOfxParamStringSublabelName);

        _bbox = fetchChoiceParam(kParamBBox);
        _alphaMasking = fetchBooleanParam(kParamAlphaMasking);
        // only in the general context, in versions >= kRangeFromMinMajor
        _rangeFrom = paramExists(kParamRangeFrom) ? fetchChoiceParam(kParamRangeFrom) : NULL;
        assert(_operation && _operationString && _bbox && _alphaMasking);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _aChannels[0] = fetchBooleanParam(kParamAChannelsR);
        _aChannels[1] = fetchBooleanParam(kParamAChannelsG);
        _aChannels[2] = fetchBooleanParam(kParamAChannelsB);
        _aChannels[3] = fetchBooleanParam(kParamAChannelsA);
        assert(_aChannels[0] && _aChannels[1] && _aChannels[2] && _aChannels[3]);

        _bChannels[0] = fetchBooleanParam(kParamBChannelsR);
        _bChannels[1] = fetchBooleanParam(kParamBChannelsG);
        _bChannels[2] = fetchBooleanParam(kParamBChannelsB);
        _bChannels[3] = fetchBooleanParam(kParamBChannelsA);
        assert(_bChannels[0] && _bChannels[1] && _bChannels[2] && _bChannels[3]);

        _outputChannels[0] = fetchBooleanParam(kParamOutputChannelsR);
        _outputChannels[1] = fetchBooleanParam(kParamOutputChannelsG);
        _outputChannels[2] = fetchBooleanParam(kParamOutputChannelsB);
        _outputChannels[3] = fetchBooleanParam(kParamOutputChannelsA);
        assert(_outputChannels[0] && _outputChannels[1] && _outputChannels[2] && _outputChannels[3]);

        {
            MergingFunctionEnum operation = (MergingFunctionEnum)_operation->getValue();
            // depending on the operation, enable/disable alpha masking
            _alphaMasking->setEnabled( MergeImages2D::isMaskable(operation) );
            _operationString->setValue( MergeImages2D::getOperationString(operation) );
        }
    }

private:
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    // override the time domain action, only called in the general context
    virtual bool getTimeDomain(OfxRangeD &range) OVERRIDE FINAL;

#ifdef OFX_EXTENSIONS_NUKE
    virtual OfxStatus getClipComponents(const ClipComponentsArguments& args, ClipComponentsSetter& clipComponents) OVERRIDE FINAL;
#endif

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(MergeProcessorBase &, const RenderArguments &args, bool renderRotoMask);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    template<int nComponents>
    void renderForComponents(const RenderArguments &args, bool renderRotoMask);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args, bool renderRotoMask);

    MergePluginEnum _pluginType;
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    std::vector<Clip *> _srcClipAs;
    Clip *_srcClipB;
    Clip *_maskClip;
    ChoiceParam *_operation;
    StringParam *_operationString;
    ChoiceParam *_bbox;
    BooleanParam *_alphaMasking;
    ChoiceParam *_rangeFrom;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _aChannels[4];
    BooleanParam* _bChannels[4];
    BooleanParam* _outputChannels[4];
};


// override the rod call
bool
MergePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                   OfxRectD &rod)
{
    const double time = args.time;
    double mix = _mix->getValueAtTime(time);

    //Do the same as isIdentity otherwise the result of getRegionOfDefinition() might not be coherent with the RoD of the identity clip.
    if (mix == 0.) {
        if ( _srcClipB->isConnected() ) {
            OfxRectD rodB = _srcClipB->getRegionOfDefinition(time);
            rod = rodB;

            return true;
        }

        return false;
    }

    std::vector<OfxRectD> rods;
    BBoxEnum bboxChoice = (BBoxEnum)_bbox->getValueAtTime(time);
    if ( (bboxChoice == eBBoxUnion) || (bboxChoice == eBBoxIntersection) ) {
        if ( _srcClipB->isConnected() ) {
            rods.push_back( _srcClipB->getRegionOfDefinition(time) );
        }
        for (std::size_t i = 0; i < _srcClipAs.size(); ++i) {
            if ( _srcClipAs[i]->isConnected() ) {
                rods.push_back( _srcClipAs[i]->getRegionOfDefinition(time) );
            }
        }
        if ( !rods.size() ) {
            return false;
        }
        rod = rods[0];
    }
    switch (bboxChoice) {
    case eBBoxUnion: {     //union
        for (unsigned i = 1; i < rods.size(); ++i) {
            Coords::rectBoundingBox(rod, rods[i], &rod);
        }

        return true;
    }
    case eBBoxIntersection: {     //intersection
        for (unsigned i = 1; i < rods.size(); ++i) {
            Coords::rectIntersection(rod, rods[i], &rod);
        }

        // may return an empty RoD if intersection is empty
        return true;
    }
    case eBBoxA: {     //A
        if ( _srcClipAs.size() > 0 && _srcClipAs[0] && _srcClipAs[0]->isConnected() ) {
            rod = _srcClipAs[0]->getRegionOfDefinition(time);

            return true;
        }

        return false;
    }
    case eBBoxB: {     //B
        if ( _srcClipB->isConnected() ) {
            rod = _srcClipB->getRegionOfDefinition(time);

            return true;
        }

        return false;
    }
    }

    return false;
} // MergePlugin::getRegionOfDefinition

#ifdef OFX_EXTENSIONS_NUKE

static void addRotoMaskIfAvailableOnClip(Clip* clip, ClipComponentsSetter& clipComponents)
{
    std::vector<std::string> availablePlanes;
    clip->getPlanesPresent(&availablePlanes);
    for (std::size_t i = 0; i < availablePlanes.size(); ++i) {
        MultiPlane::ImagePlaneDesc plane;
        if (availablePlanes[i] == kOfxMultiplaneColorPlaneID) {
            plane = MultiPlane::ImagePlaneDesc::mapNCompsToColorPlane(clip->getPixelComponentCount());
        } else {
            plane = MultiPlane::ImagePlaneDesc::mapOFXPlaneStringToPlane(availablePlanes[i]);
        }
        if (plane.getPlaneID() == kRotoMaskPlaneID) {
            clipComponents.addClipPlane(*clip, availablePlanes[i]);
            return;
        }
    }
}

OfxStatus
MergePlugin::getClipComponents(const ClipComponentsArguments& args,
                                    ClipComponentsSetter& clipComponents)
{
    assert(_pluginType == eMergePluginRoto);


    bool isColorPlaneRendered = true;
    bool outputChannels[4];
    for (int c = 0; c < 4; ++c) {
        _outputChannels[c]->getValueAtTime(args.time, outputChannels[c]);
    }
    if (!outputChannels[0] && !outputChannels[1] && !outputChannels[2] && !outputChannels[3]) {
        isColorPlaneRendered = false;
    }

    std::string ofxPlaneStr = getRotoMaskPlaneOFXString();

    // For each clip we need color and RotoMask

    clipComponents.addClipPlane(*_dstClip, ofxPlaneStr);
    if (isColorPlaneRendered) {
        clipComponents.addClipPlane(*_dstClip, kOfxMultiplaneColorPlaneID);
    }

    addRotoMaskIfAvailableOnClip(_srcClipB, clipComponents);
    if (isColorPlaneRendered) {
        clipComponents.addClipPlane(*_srcClipB, kOfxMultiplaneColorPlaneID);
    }

    for (std::size_t i = 0; i < _srcClipAs.size(); ++i) {
        if (_srcClipAs[i]->isConnected()) {
            addRotoMaskIfAvailableOnClip(_srcClipAs[i], clipComponents);
            if (isColorPlaneRendered) {
                clipComponents.addClipPlane(*_srcClipAs[i], kOfxMultiplaneColorPlaneID);
            }
        }
    }

    if (_maskClip && _maskClip->isConnected()) {
        addRotoMaskIfAvailableOnClip(_maskClip, clipComponents);
    }
    return kOfxStatOK;

} // getClipComponents

#endif

// Since we cannot hold a auto_ptr in the vector we must hold a raw pointer.
// To ensure that images are always freed even in case of exceptions, use a RAII class.
struct OptionalImagesHolder_RAII
{
    std::vector<const Image*> images;
    std::vector<const Image*> rotoMaskImg;

    OptionalImagesHolder_RAII()
        : images()
        , rotoMaskImg()
    {
    }

    ~OptionalImagesHolder_RAII()
    {
        for (std::size_t i = 0; i < images.size(); ++i) {
            delete images[i];
        }
        for (std::size_t i = 0; i < rotoMaskImg.size(); ++i) {
            delete rotoMaskImg[i];
        }
    }

    // owns the images: not copyable (C.21)
    OptionalImagesHolder_RAII(const OptionalImagesHolder_RAII&) = delete;
    OptionalImagesHolder_RAII& operator=(const OptionalImagesHolder_RAII&) = delete;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

// MergeRoto: fetch the RotoMask plane of a clip. Hosts without the plane suite v2 (e.g. OFX 1.4
// hosts) have no RotoMask plane: there is then no roto mask (NULL), as for an input without it.
static Image*
fetchRotoMaskPlane(Clip* clip,
                   double time,
                   int view,
                   const std::string& plane)
{
#ifdef OFX_EXTENSIONS_NUKE
    if ( fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
        return clip->fetchImagePlane( time, view, plane.c_str() );
    }
#else
    (void)clip; (void)time; (void)view; (void)plane;
#endif

    return NULL;
}

/* set up and run a processor */
void
MergePlugin::setupAndProcess(MergeProcessorBase &processor,
                             const RenderArguments &args,
                             bool renderRotoMask)
{
    const double time = args.time;


    std::string rotoMaskOfxPlaneStr = getRotoMaskPlaneOFXString();

    Image* dstImage = 0;
    if (_pluginType != eMergePluginRoto || !renderRotoMask) {
        dstImage = _dstClip->fetchImage(time);
    } else {
        dstImage = _dstClip->fetchImagePlane(args.time, args.renderView, rotoMaskOfxPlaneStr.c_str());
    }
    auto_ptr<Image> dst(dstImage);

    // If the plug-in is eMergePluginRoto, we can produce a RotoMask but don't necessarily produce a Color plane.
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();
    if (dst.get() && _pluginType != eMergePluginRoto) {
        // Only check if the plug-in is not multi-planar, because the RotoMask plane may not necessariy have the same properties
        // as the color plane.
        if ( ( dstBitDepth != dst->getPixelDepth() ) ||
            ( dstComponents !=  dst->getPixelComponents()) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
            throwSuiteStatusException(kOfxStatFailed);
        }
# ifndef NDEBUG
        checkBadRenderScaleOrField(dst, args);
# endif
    }
    auto_ptr<const Image> srcB( ( !renderRotoMask && _srcClipB && _srcClipB->isConnected() ) ?
                                    _srcClipB->fetchImage(time) : 0 );
    OptionalImagesHolder_RAII srcAs;
    for (std::size_t i = 0; i < _srcClipAs.size(); ++i) {
        if ( _srcClipAs[i] && _srcClipAs[i]->isConnected() ) {
            if (renderRotoMask) {
#ifdef OFX_EXTENSIONS_NUKE
                const Image* rotoMaskA = fetchRotoMaskPlane(_srcClipAs[i], time, args.renderView, rotoMaskOfxPlaneStr);
                // Since version 3, keep NULL images, so that a connected A input is merged
                // as transparent black even if it has no image here.
                if ( rotoMaskA || (getMajorVersion() >= 3) ) {
                    srcAs.rotoMaskImg.push_back(rotoMaskA);
                }
#endif
            } else {
                const Image* srcA = _srcClipAs[i]->fetchImage(time);
                // Since version 3, keep NULL images, so that a connected A input is merged
                // as transparent black even if it has no image here.
                if ( srcA || (getMajorVersion() >= 3) ) {
                    srcAs.images.push_back(srcA);

                    if (srcA) {
#                 ifndef NDEBUG
                        checkBadRenderScaleOrField(srcA, args);
#                 endif
                        BitDepthEnum srcBitDepth      = srcA->getPixelDepth();
                        PixelComponentEnum srcComponents = srcA->getPixelComponents();
                        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
                            throwSuiteStatusException(kOfxStatErrImageFormat);
                        }
                    }
#ifdef OFX_EXTENSIONS_NUKE
                    if (_pluginType == eMergePluginRoto) {
                        const Image* rotoMaskA = fetchRotoMaskPlane(_srcClipAs[i], time, args.renderView, rotoMaskOfxPlaneStr);
                        // always push the rotoMask, even if NULL, so that indexes in srcAs.images and srcAs.rotoMaskImg correspond
                        srcAs.rotoMaskImg.push_back(rotoMaskA);

                    }
#endif
                }
            }
        }
    }

    if ( srcB.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(srcB, args);
# endif
        BitDepthEnum srcBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );

    const Image* maskImage = 0;
    if (doMasking) {
        if (_pluginType == eMergePluginRoto) {
#ifdef OFX_EXTENSIONS_NUKE
            if ( fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ) {
                maskImage = _maskClip->fetchImagePlane( time, args.renderView, rotoMaskOfxPlaneStr.c_str() );
            } else
#endif
            {
                // no plane suite (e.g. OFX 1.4 hosts): the mask is the image of the Mask clip
                maskImage = _maskClip->fetchImage(time);
            }
        } else {
            maskImage = _maskClip->fetchImage(time);
        }
    }

    auto_ptr<const Image> mask(maskImage);

    bool maskInvert = _maskInvert->getValueAtTime(time);
    processor.setMaskInvert(maskInvert);

    // do we do masking
    if (doMasking) {

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get());
    }

    const Image* rotoMaskImgB = 0;
    if (_pluginType == eMergePluginRoto && _srcClipB) {
#ifdef OFX_EXTENSIONS_NUKE
        rotoMaskImgB = fetchRotoMaskPlane(_srcClipB, time, args.renderView, rotoMaskOfxPlaneStr);
#endif
    }

    processor.setDstImg( dst.get() );


    auto_ptr<const Image> rotoMaskB(rotoMaskImgB);
    if (!renderRotoMask) {
        processor.setRotoMaskImg( srcAs.rotoMaskImg, rotoMaskB.get() );
        processor.setSrcImg( srcAs.images, srcB.get() );
    } else {
        // We render the RotoMask
        processor.setRotoMaskImg( srcAs.rotoMaskImg, rotoMaskB.get() );
        processor.setSrcImg( srcAs.rotoMaskImg, rotoMaskB.get() );
    }

    bool alphaMasking = _alphaMasking->getValueAtTime(time);
    double mix = _mix->getValueAtTime(time);
    std::bitset<4> aChannels;
    std::bitset<4> bChannels;
    std::bitset<4> outputChannels;
    for (std::size_t c = 0; c < 4; ++c) {
        aChannels[c] = _aChannels[c]->getValueAtTime(time);
        bChannels[c] = _bChannels[c]->getValueAtTime(time);
        outputChannels[c] = _outputChannels[c]->getValueAtTime(time);
    }
    processor.setValues(alphaMasking, mix, aChannels, bChannels, outputChannels);
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    processor.process();
} // MergePlugin::setupAndProcess

template<int nComponents>
void
MergePlugin::renderForComponents(const RenderArguments &args, bool renderRotoMask)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte: {
        renderForBitDepth<unsigned char, nComponents, 255>(args, renderRotoMask);
        break;
    }
    case eBitDepthUShort: {
        renderForBitDepth<unsigned short, nComponents, 65535>(args, renderRotoMask);
        break;
    }
    case eBitDepthFloat: {
        renderForBitDepth<float, nComponents, 1>(args, renderRotoMask);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
MergePlugin::renderForBitDepth(const RenderArguments &args, bool renderRotoMask)
{
    const double time = args.time;
    MergingFunctionEnum operation = (MergingFunctionEnum)_operation->getValueAtTime(time);

    auto_ptr<MergeProcessorBase> fred;

    switch (operation) {
    case eMergeATop:
        fred.reset( new MergeProcessor<eMergeATop, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeAverage:
        fred.reset( new MergeProcessor<eMergeAverage, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeColorBurn:
        fred.reset( new MergeProcessor<eMergeColorBurn, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeColorDodge:
        fred.reset( new MergeProcessor<eMergeColorDodge, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeConjointOver:
        fred.reset( new MergeProcessor<eMergeConjointOver, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeCopy:
        fred.reset( new MergeProcessor<eMergeCopy, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeDifference:
        fred.reset( new MergeProcessor<eMergeDifference, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeDisjointOver:
        fred.reset( new MergeProcessor<eMergeDisjointOver, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeDivide:
        fred.reset( new MergeProcessor<eMergeDivide, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeExclusion:
        fred.reset( new MergeProcessor<eMergeExclusion, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeFreeze:
        fred.reset( new MergeProcessor<eMergeFreeze, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeFrom:
        fred.reset( new MergeProcessor<eMergeFrom, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeGeometric:
        fred.reset( new MergeProcessor<eMergeGeometric, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeGrainExtract:
        fred.reset( new MergeProcessor<eMergeGrainExtract, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeGrainMerge:
        fred.reset( new MergeProcessor<eMergeGrainMerge, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeHardLight:
        fred.reset( new MergeProcessor<eMergeHardLight, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeHypot:
        fred.reset( new MergeProcessor<eMergeHypot, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeIn:
        fred.reset( new MergeProcessor<eMergeIn, PIX, nComponents, maxValue>(*this) );
        break;
    //case eMergeInterpolated:
    //    fred.reset(new MergeProcessor<eMergeInterpolated, PIX, nComponents, maxValue>(*this));
    //    break;
    case eMergeMask:
        fred.reset( new MergeProcessor<eMergeMask, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeMatte:
        fred.reset( new MergeProcessor<eMergeMatte, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeMax:
        fred.reset( new MergeProcessor<eMergeMax, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeMin:
        fred.reset( new MergeProcessor<eMergeMin, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeMinus:
        fred.reset( new MergeProcessor<eMergeMinus, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeMultiply:
        fred.reset( new MergeProcessor<eMergeMultiply, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeOut:
        fred.reset( new MergeProcessor<eMergeOut, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeOver:
        fred.reset( new MergeProcessor<eMergeOver, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeOverlay:
        fred.reset( new MergeProcessor<eMergeOverlay, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergePinLight:
        fred.reset( new MergeProcessor<eMergePinLight, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergePlus:
        fred.reset( new MergeProcessor<eMergePlus, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeReflect:
        fred.reset( new MergeProcessor<eMergeReflect, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeScreen:
        fred.reset( new MergeProcessor<eMergeScreen, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeSoftLight:
        fred.reset( new MergeProcessor<eMergeSoftLight, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeStencil:
        fred.reset( new MergeProcessor<eMergeStencil, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeUnder:
        fred.reset( new MergeProcessor<eMergeUnder, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeXOR:
        fred.reset( new MergeProcessor<eMergeXOR, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeHue:
        fred.reset( new MergeProcessor<eMergeHue, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeSaturation:
        fred.reset( new MergeProcessor<eMergeSaturation, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeColor:
        fred.reset( new MergeProcessor<eMergeColor, PIX, nComponents, maxValue>(*this) );
        break;
    case eMergeLuminosity:
        fred.reset( new MergeProcessor<eMergeLuminosity, PIX, nComponents, maxValue>(*this) );
        break;
    } // switch
    assert( fred.get() );
    if ( fred.get() ) {
        setupAndProcess(*fred, args, renderRotoMask);
    }
} // MergePlugin::renderForBitDepth

// the overridden render function
void
MergePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClipAs[0]->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipAs[0]->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );


    bool renderRotoMask = false;
    if (_pluginType == eMergePluginRoto) {
        std::string planeToRender;
        if (args.planes.empty()) {
            planeToRender = kFnOfxImagePlaneColour;
        } else {
            planeToRender = args.planes.front();
            renderRotoMask = planeToRender == getRotoMaskPlaneOFXString();
        }
    }

    if (renderRotoMask) {
        dstComponents = ePixelComponentAlpha;
    }

    if (dstComponents == ePixelComponentRGBA) {
        renderForComponents<4>(args, renderRotoMask);
    } else if (dstComponents == ePixelComponentRGB) {
        renderForComponents<3>(args, renderRotoMask);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderForComponents<2>(args, renderRotoMask);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderForComponents<1>(args, renderRotoMask);
    }
}

void
MergePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    // The output of Merge should always have an alpha channel.
    // For example, if B is RGB, B is considered transparent, so
    // that outside of A the output should be transparent.
    // Thus, if A and B are RGB, output should be RGBA.
    if (outputComps == ePixelComponentRGB) {
        outputComps = ePixelComponentRGBA;
    }

    if (getContext() == eContextFilter) {
        // "In all contexts, except for the general context, mandated input clips cannot have their
        // component types remapped, nor can the output" (ofxClipPreferences.rst), and in the filter
        // context the Output "will always be the same as the Source clip" (ofxImageEffectContexts.rst).
        // Source (B) is RGBA or Alpha there (an RGB Source is mapped to RGBA by the host): remap only A.
        outputComps = (_srcClipB->getUnmappedPixelComponents() == ePixelComponentAlpha) ? ePixelComponentAlpha : ePixelComponentRGBA;
    } else {
        clipPreferences.setClipComponents(*_dstClip, outputComps);
        clipPreferences.setClipComponents(*_srcClipB, outputComps);
    }
    for (std::size_t i = 0; i < _srcClipAs.size(); ++i) {
        clipPreferences.setClipComponents(*_srcClipAs[i], outputComps);
    }
}

void
MergePlugin::changedParam(const InstanceChangedArgs &args,
                          const std::string &paramName)
{
    if (paramName == kParamOperation) {
        MergingFunctionEnum operation = (MergingFunctionEnum)_operation->getValueAtTime(args.time);
        // depending on the operation, enable/disable alpha masking
        _alphaMasking->setEnabled( MergeImages2D::isMaskable(operation) );
        _operationString->setValue( MergeImages2D::getOperationString(operation) );
    }
}


// The OFX spec (kOfxImageEffectActionGetTimeDomain) only calls this in the general context; the
// default is the union of the frame ranges of the non-optional clips (all Merge clips are optional
// there, so the default is infinite). Versions 1 and 2 (no Range From) keep the host default.
bool
MergePlugin::getTimeDomain(OfxRangeD &range)
{
    if ( !_rangeFrom || (getContext() != eContextGeneral) ) {
        return false;
    }
    const Clip* clip = ( static_cast<RangeFromEnum>( _rangeFrom->getValue() ) == eRangeFromA ) ? _srcClipAs[0] : _srcClipB;
    if ( !clip || !clip->isConnected() ) {
        return false;
    }
    range = clip->getFrameRange();

    return true;
}

bool
MergePlugin::isIdentity(const IsIdentityArguments &args,
                        Clip * &identityClip,
                        double & /*identityTime*/
                        , int& /*view*/, std::string& plane)
{
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0.) {
        identityClip = _srcClipB;

        return true;
    }

    bool outputChannels[4];
    bool srcBChannels[4];
    for (int c = 0; c < 4; ++c) {
        _outputChannels[c]->getValueAtTime(time, outputChannels[c]);
        _bChannels[c]->getValueAtTime(time, srcBChannels[c]);
    }
    const int nComponents = _dstClip->getPixelComponentCount();
    // versions 1 and 2, Alpha-only images: B's R checkbox controls the alpha channel, as in the
    // processor (issue #328)
    if ( (nComponents == 1) && !alphaUsesAChannel( nComponents, getMajorVersion() ) ) {
        srcBChannels[3] = srcBChannels[0];
    }
    if (!outputChannels[0] && !outputChannels[1] && !outputChannels[2] && !outputChannels[3] && plane == kFnOfxImagePlaneColour) {
        identityClip = _srcClipB;

        return true;
    }

    // For each checked output channels, if srcB channel is unchecked, we are not identity
    // (Alpha-only images only have the alpha channel)
    const int firstChannel = (nComponents == 1) ? 3 : 0;
    for (int i = firstChannel; i < 4; ++i) {
        if (!outputChannels[i]) {
            continue;
        }
        if (!srcBChannels[i]) {
            return false;
        }
    }


    OfxRectI maskRoD;
    bool maskRoDValid = ofxsMaskGetRoDPixel(_maskClip, _maskApply, _maskInvert, time, args.renderScale, &maskRoD);
    // effect is identity if the renderWindow doesn't intersect the mask RoD
    if ( maskRoDValid && !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
        identityClip = _srcClipB;

        return true;
    }

    MergingFunctionEnum blendingOperator = (MergingFunctionEnum)_operation->getValueAtTime(args.time);
    if ( !isIdentityForBOnlyVersioned( blendingOperator, getMajorVersion() ) ) {
        // For most operators, we cannot be identity on regions where the A RoD does not intersect the render window.
        // E.g: multiply should produce black and transparent in B regions that do not intersect A
        return false;
    }

    // Even with an infinite renderwindow, we may have an identity, depending on the mask and the A inputs RoD (see below)
    //if (Coords::rectIsInfinite(args.renderWindow)) {
    //    return false;
    //}

    // The region of effect is only the set of the intersections between the A inputs and the mask.
    // If at least one of these regions intersects the renderwindow, the effect is not identity.

    //if (maskRoDValid) {
    //    printf("maskRoD:%d %d %d %d\n", maskRoD.x1, maskRoD.y1, maskRoD.x2, maskRoD.y2);
    //}
    //printf("renderWindow:%d %d %d %d\n", args.renderWindow.x1, args.renderWindow.y1, args.renderWindow.x2, args.renderWindow.y2);

    for (std::size_t i = 0; i < _srcClipAs.size(); ++i) {
        if ( !_srcClipAs[i]->isConnected() ) {
            continue;
        }
        OfxRectD srcARoD = _srcClipAs[i]->getRegionOfDefinition(time);
        if ( Coords::rectIsEmpty(srcARoD) ) {
            // RoD is empty
            continue;
        }

        OfxRectI srcARoDPixel;
        Coords::toPixelEnclosing(srcARoD, args.renderScale, _srcClipAs[i]->getPixelAspectRatio(), &srcARoDPixel);
        bool srcARoDValid = true;
        //printf("srcARoD[%d]:%d %d %d %d\n", (int)i, srcARoDPixel.x1, srcARoDPixel.y1, srcARoDPixel.x2, srcARoDPixel.y2);
        if (maskRoDValid) {
            // mask the srcARoD with the mask RoD. The result may be empty
            srcARoDValid = Coords::rectIntersection<OfxRectI>(srcARoDPixel, maskRoD, &srcARoDPixel);
        }
        if ( srcARoDValid && Coords::rectIntersection<OfxRectI>(args.renderWindow, srcARoDPixel, 0) ) {
            // renderWindow intersects one of the effect areas
            return false;
        }
    }

    // renderWindow intersects no area where a "A" source is applied
    identityClip = _srcClipB;

    return true;
} // MergePlugin::isIdentity

//mDeclarePluginFactory(MergePluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion, MergePluginEnum plugin>
class MergePluginFactory
: public PluginFactoryHelper<MergePluginFactory<majorVersion,plugin> >
{
public:
    MergePluginFactory(const std::string & id, unsigned int verMaj, unsigned int verMin)
    : PluginFactoryHelper<MergePluginFactory<majorVersion,plugin> >(id, verMaj, verMin)
    {
    }

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();}
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};

template<unsigned int majorVersion, MergePluginEnum plugin>
void
MergePluginFactory<majorVersion,plugin>::describe(ImageEffectDescriptor &desc)
{
    // basic labels

    switch (plugin) {
    case eMergePluginMerge:
        desc.setLabel(kPluginName);
        desc.setPluginGrouping(kPluginGrouping);
        break;
    case eMergePluginPlus:
        desc.setLabel(kPluginNamePlus);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginMatte:
        desc.setLabel(kPluginNameMatte);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginMultiply:
        desc.setLabel(kPluginNameMultiply);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginIn:
        desc.setLabel(kPluginNameIn);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginOut:
        desc.setLabel(kPluginNameOut);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginScreen:
        desc.setLabel(kPluginNameScreen);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginMax:
        desc.setLabel(kPluginNameMax);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginMin:
        desc.setLabel(kPluginNameMin);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginAbsMinus:
        desc.setLabel(kPluginNameAbsminus);
        desc.setPluginGrouping(kPluginGroupingSub);
        break;
    case eMergePluginRoto:
        desc.setLabel(kPluginNameRoto);
        desc.setPluginGrouping(kPluginGrouping);
        break;
    }
    std::string help = (plugin == eMergePluginRoto ? kPluginDescriptionStartRoto : kPluginDescriptionStart);
    if ( getImageEffectHostDescription()->supportsPixelComponent(ePixelComponentRGB) ) {
        // Merge has a special way of handling RGB inputs, which are transparent by default
        help += kPluginDescriptionMidRGB;
    }
    // only Natron benefits from the long description, because '<' characters may break the OFX
    // plugins cache in hosts using the older HostSupport library.
    if (getImageEffectHostDescription()->isNatron) {
        help += "### Operators\n";
        help += "The following operators are available.\n";
        help += "\n#### Porter-Duff compositing operators\n";
        // missing: clear
        help += "\n- " + operationHelpMarkdown(eMergeCopy, majorVersion) + '\n'; // src
        // missing: dst
        help += "\n- " + operationHelpMarkdown(eMergeOver, majorVersion) + '\n'; // src-over
        help += "\n- " + operationHelpMarkdown(eMergeUnder, majorVersion) + '\n'; // dst-over
        help += "\n- " + operationHelpMarkdown(eMergeIn, majorVersion) + '\n'; // src-in
        help += "\n- " + operationHelpMarkdown(eMergeMask, majorVersion) + '\n'; // dst-in
        help += "\n- " + operationHelpMarkdown(eMergeOut, majorVersion) + '\n'; // src-out
        help += "\n- " + operationHelpMarkdown(eMergeStencil, majorVersion) + '\n'; // dst-out
        help += "\n- " + operationHelpMarkdown(eMergeATop, majorVersion) + '\n'; // src-atop
        // missing: dst-atop
        help += "\n- " + operationHelpMarkdown(eMergeXOR, majorVersion) + '\n'; // xor

        help += "\n#### Blend modes, see https://en.wikipedia.org/wiki/Blend_modes\n";
        help += "\n##### Multiply and Screen\n";
        help += "\n- " + operationHelpMarkdown(eMergeMultiply, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeScreen, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeOverlay, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeHardLight, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeSoftLight, majorVersion) + '\n';
        help += "\n##### Dodge and burn\n";
        help += "\n- " + operationHelpMarkdown(eMergeColorDodge, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeColorBurn, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergePinLight, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeDifference, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeExclusion, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeDivide, majorVersion) + '\n';
        help += "\n##### Simple arithmetic blend modes\n";
        help += "\n- " + operationHelpMarkdown(eMergeDivide, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergePlus, majorVersion) + '\n';// add (see <http://keithp.com/~keithp/render/protocol.html>)
        help += "\n- " + operationHelpMarkdown(eMergeFrom, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeMinus, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeDifference, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeMin, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeMax, majorVersion) + '\n';
        help += "\n##### Hue, saturation and luminosity\n";
        help += "\n- " + operationHelpMarkdown(eMergeHue, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeSaturation, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeColor, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeLuminosity, majorVersion) + '\n';
        help += "\n#### Other\n";
        help += "\n- " + operationHelpMarkdown(eMergeAverage, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeConjointOver, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeDisjointOver, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeFreeze, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeGeometric, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeGrainExtract, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeGrainMerge, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeHypot, majorVersion) + '\n';
        //help += "\n- " + operationHelpMarkdown(eMergeInterpolated, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeMatte, majorVersion) + '\n';
        help += "\n- " + operationHelpMarkdown(eMergeReflect, majorVersion) + '\n';
        help += '\n';
    }
    help += kPluginDescriptionEnd;
    if (getImageEffectHostDescription()->isNatron) {
        desc.setDescriptionIsMarkdown(true);
        desc.setPluginDescription(help, /*validate=*/ false);
    } else {
        desc.setPluginDescription(help);
    }

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
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

    // Old versions of this plugin are obsolete.
    if (this->getMajorVersion() < kPluginVersionMajor) {
        desc.setIsObsolete(true);
    }
} // >::describe

static void
addMergeOption(ChoiceParamDescriptor* param,
               MergingFunctionEnum e,
               bool cascading,
               unsigned int majorVersion)
{
    assert(param->getNOptions() == e);
    if (cascading) {
        param->appendOption( getOperationGroupString(e) + '/' + getOperationString(e), operationDescription(e, majorVersion) );
    } else {
        param->appendOption( getOperationString(e), /*'(' + getOperationGroupString(e) + ") " +*/ operationDescription(e, majorVersion) );
    }
}

template<unsigned int majorVersion, MergePluginEnum plugin>
void
MergePluginFactory<majorVersion,plugin>::describeInContext(ImageEffectDescriptor &desc,
                                                           ContextEnum context)
{
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (/*plugin != eMergePluginRoto &&*/
                            getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);
    // the filter context mandates an input clip named Source: it is B there
    ClipDescriptor* srcClipB = desc.defineClip( kClipBName(context) );

    srcClipB->setHint(kClipBHint);
    srcClipB->addSupportedComponent( ePixelComponentRGBA );
    if (context != eContextFilter) {
        // filter context: Output has the components of Source, and the output of Merge has an
        // alpha channel (see getClipPreferences), so Source is RGBA or Alpha there
        srcClipB->addSupportedComponent( ePixelComponentRGB );
        srcClipB->addSupportedComponent( ePixelComponentXY );
    }
    srcClipB->addSupportedComponent( ePixelComponentAlpha );
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);

    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    // (the mandated Source clip of the filter context is not optional)
    srcClipB->setOptional(context != eContextFilter); // B clip is optional

    ClipDescriptor* srcClipA = desc.defineClip(kClipA);
    srcClipA->setHint(kClipAHint);
    srcClipA->addSupportedComponent( ePixelComponentRGBA );
    srcClipA->addSupportedComponent( ePixelComponentRGB );
    srcClipA->addSupportedComponent( ePixelComponentXY );
    srcClipA->addSupportedComponent( ePixelComponentAlpha );
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(kSupportsTiles);

    //Optional: If we want a render to be triggered even if one of the inputs is not connected
    //they need to be optional.
    srcClipA->setOptional(true);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    if (numerousInputs) {
        for (int i = 2; i <= kMaximumAInputs; ++i) {
            ClipDescriptor* optionalSrcClip = desc.defineClip( std::string(kClipA) + unsignedToString(i) );
            optionalSrcClip->addSupportedComponent( ePixelComponentRGBA );
            optionalSrcClip->addSupportedComponent( ePixelComponentRGB );
            optionalSrcClip->addSupportedComponent( ePixelComponentXY );
            optionalSrcClip->addSupportedComponent( ePixelComponentAlpha );
            optionalSrcClip->setTemporalClipAccess(false);
            optionalSrcClip->setSupportsTiles(kSupportsTiles);

            //Optional: If we want a render to be triggered even if one of the inputs is not connected
            //they need to be optional.
            optionalSrcClip->setOptional(true);
        }
    }


    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // operationString
    {
        StringParamDescriptor* param = desc.defineStringParam(kNatronOfxParamStringSublabelName);
        param->setIsSecretAndDisabled(true); // always secret
        param->setIsPersistent(false);
        param->setEvaluateOnChange(false);
        param->setDefault( getOperationString( getDefaultOperation(plugin) ) );
        if (page) {
            page->addChild(*param);
        }
    }

    // operation
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOperation);
        param->setLabel(kParamOperationLabel);
        param->setHint(kParamOperationHint);
        bool cascading = false;// getImageEffectHostDescription()->supportsCascadingChoices;
        param->setCascading(cascading);
        addMergeOption(param, eMergeATop, cascading, majorVersion);
        addMergeOption(param, eMergeAverage, cascading, majorVersion);
        addMergeOption(param, eMergeColor, cascading, majorVersion);
        addMergeOption(param, eMergeColorBurn, cascading, majorVersion);
        addMergeOption(param, eMergeColorDodge, cascading, majorVersion);
        addMergeOption(param, eMergeConjointOver, cascading, majorVersion);
        addMergeOption(param, eMergeCopy, cascading, majorVersion);
        addMergeOption(param, eMergeDifference, cascading, majorVersion);
        addMergeOption(param, eMergeDisjointOver, cascading, majorVersion);
        addMergeOption(param, eMergeDivide, cascading, majorVersion);
        addMergeOption(param, eMergeExclusion, cascading, majorVersion);
        addMergeOption(param, eMergeFreeze, cascading, majorVersion);
        addMergeOption(param, eMergeFrom, cascading, majorVersion);
        addMergeOption(param, eMergeGeometric, cascading, majorVersion);
        addMergeOption(param, eMergeGrainExtract, cascading, majorVersion);
        addMergeOption(param, eMergeGrainMerge, cascading, majorVersion);
        addMergeOption(param, eMergeHardLight, cascading, majorVersion);
        addMergeOption(param, eMergeHue, cascading, majorVersion);
        addMergeOption(param, eMergeHypot, cascading, majorVersion);
        addMergeOption(param, eMergeIn, cascading, majorVersion);
        //addMergeOption(param, eMergeInterpolated, cascading, majorVersion);
        addMergeOption(param, eMergeLuminosity, cascading, majorVersion);
        addMergeOption(param, eMergeMask, cascading, majorVersion);
        addMergeOption(param, eMergeMatte, cascading, majorVersion);
        addMergeOption(param, eMergeMax, cascading, majorVersion);
        addMergeOption(param, eMergeMin, cascading, majorVersion);
        addMergeOption(param, eMergeMinus, cascading, majorVersion);
        addMergeOption(param, eMergeMultiply, cascading, majorVersion);
        addMergeOption(param, eMergeOut, cascading, majorVersion);
        addMergeOption(param, eMergeOver, cascading, majorVersion);
        addMergeOption(param, eMergeOverlay, cascading, majorVersion);
        addMergeOption(param, eMergePinLight, cascading, majorVersion);
        addMergeOption(param, eMergePlus, cascading, majorVersion);
        addMergeOption(param, eMergeReflect, cascading, majorVersion);
        addMergeOption(param, eMergeSaturation, cascading, majorVersion);
        addMergeOption(param, eMergeScreen, cascading, majorVersion);
        addMergeOption(param, eMergeSoftLight, cascading, majorVersion);
        addMergeOption(param, eMergeStencil, cascading, majorVersion);
        addMergeOption(param, eMergeUnder, cascading, majorVersion);
        addMergeOption(param, eMergeXOR, cascading, majorVersion);
        param->setDefault( getDefaultOperation(plugin) );
        param->setAnimates(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    // alphaMasking, on the same row as the operation, before the bounding box (#378)
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAlphaMasking);
        param->setLabel(kParamAlphaMaskingLabel);
        param->setAnimates(true);
        param->setDefault(false);
        param->setEnabled( MergeImages2D::isMaskable(eMergeOver) );
        param->setHint( (majorVersion >= 3) ? kParamAlphaMaskingHintV3 : kParamAlphaMaskingHint );
        if (page) {
            page->addChild(*param);
        }
    }

    // the last param before the channels has a divider
    const bool hasRangeFrom = (context == eContextGeneral) && (majorVersion >= kRangeFromMinMajor);

    // boundingBox
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamBBox);
        param->setLabel(kParamBBoxLabel);
        param->setHint(kParamBBoxHint);
        assert(param->getNOptions() == (int)eBBoxUnion);
        param->appendOption(kParamBBoxOptionUnion);
        assert(param->getNOptions() == (int)eBBoxIntersection);
        param->appendOption(kParamBBoxOptionIntersection);
        assert(param->getNOptions() == (int)eBBoxA);
        param->appendOption(kParamBBoxOptionA);
        assert(param->getNOptions() == (int)eBBoxB);
        param->appendOption(kParamBBoxOptionB);
        param->setAnimates(true);
        param->setDefault( (int)eBBoxUnion );
        if (!hasRangeFrom) {
            param->setLayoutHint(eLayoutHintDivider);
        }
        if (page) {
            page->addChild(*param);
        }
    }

    // rangeFrom (issue #277): the time domain, only called in the general context
    if (hasRangeFrom) {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamRangeFrom);
        param->setLabel(kParamRangeFromLabel);
        param->setHint(kParamRangeFromHint);
        assert(param->getNOptions() == static_cast<int>(eRangeFromB));
        param->appendOption(kParamRangeFromOptionB);
        assert(param->getNOptions() == static_cast<int>(eRangeFromA));
        param->appendOption(kParamRangeFromOptionA);
        param->setDefault( static_cast<int>(eRangeFromB) );
        param->setAnimates(false); // used in getTimeDomain()
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
    desc.setIsMultiPlanar(plugin == eMergePluginRoto);
#endif
    {
        StringParamDescriptor* param = desc.defineStringParam(kParamAChannels);
        param->setLabel("");
        param->setHint(kParamAChannelsHint);
        param->setDefault(kParamAChannelsLabel);
        param->setStringType(eStringTypeLabel);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAChannelsR);
        param->setLabel(kParamAChannelsRLabel);
        param->setHint(kParamAChannelsRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAChannelsG);
        param->setLabel(kParamAChannelsGLabel);
        param->setHint(kParamAChannelsGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAChannelsB);
        param->setLabel(kParamAChannelsBLabel);
        param->setHint(kParamAChannelsBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAChannelsA);
        param->setLabel(kParamAChannelsALabel);
        param->setHint(kParamAChannelsAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamBChannels);
        param->setLabel("");
        param->setHint(kParamBChannelsHint);
        param->setDefault(kParamBChannelsLabel);
        param->setStringType(eStringTypeLabel);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBChannelsR);
        param->setLabel(kParamBChannelsRLabel);
        param->setHint(kParamBChannelsRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBChannelsG);
        param->setLabel(kParamBChannelsGLabel);
        param->setHint(kParamBChannelsGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBChannelsB);
        param->setLabel(kParamBChannelsBLabel);
        param->setHint(kParamBChannelsBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBChannelsA);
        param->setLabel(kParamBChannelsALabel);
        param->setHint(kParamBChannelsAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        StringParamDescriptor* param = desc.defineStringParam(kParamOutputChannels);
        param->setLabel("");
        param->setHint(kParamOutputChannelsHint);
        param->setDefault(kParamOutputChannelsLabel);
        param->setStringType(eStringTypeLabel);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputChannelsR);
        param->setLabel(kParamOutputChannelsRLabel);
        param->setHint(kParamOutputChannelsRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputChannelsG);
        param->setLabel(kParamOutputChannelsGLabel);
        param->setHint(kParamOutputChannelsGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputChannelsB);
        param->setLabel(kParamOutputChannelsBLabel);
        param->setHint(kParamOutputChannelsBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputChannelsA);
        param->setLabel(kParamOutputChannelsALabel);
        param->setHint(kParamOutputChannelsAHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintDivider);
        if (page) {
            page->addChild(*param);
        }
    }


    if ( getImageEffectHostDescription()->supportsPixelComponent(ePixelComponentRGB) ) {
        // two hidden parameters to keep track of the fact that the user explicitly checked or unchecked tha "A" checkbox
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamAChannelsAChanged);
            param->setDefault(false);
            param->setIsSecretAndDisabled(true);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            BooleanParamDescriptor* param = desc.defineBooleanParam(kParamBChannelsAChanged);
            param->setDefault(false);
            param->setIsSecretAndDisabled(true);
            param->setAnimates(false);
            param->setEvaluateOnChange(false);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // >::describeInContext

template<unsigned int majorVersion, MergePluginEnum plugin>
ImageEffect*
MergePluginFactory<majorVersion,plugin>::createInstance(OfxImageEffectHandle handle,
                                                        ContextEnum /*context*/)
{
    assert(unsignedToString(12345) == "12345");
    //Natron >= 2.0 allows multiple inputs to be folded like the viewer node, so use this to merge
    //more than 2 images
    bool numerousInputs =  (/*plugin != eMergePluginRoto &&*/
                            getImageEffectHostDescription()->isNatron &&
                            getImageEffectHostDescription()->versionMajor >= 2);

    return new MergePlugin(handle, this->getMajorVersion(), this->getMinorVersion(), plugin, numerousInputs);
}


static MergePluginFactory<kPluginVersionMajor,eMergePluginMerge>        p1(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginPlus>        p2(kPluginIdentifierSub "Plus", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginMatte>       p3(kPluginIdentifierSub "Matte", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginMultiply>    p4(kPluginIdentifierSub "Multiply", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginIn>          p5(kPluginIdentifierSub "In", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginOut>         p6(kPluginIdentifierSub "Out", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginScreen>      p7(kPluginIdentifierSub "Screen", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginMax>         p8(kPluginIdentifierSub "Max", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginMin>         p9(kPluginIdentifierSub "Min", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginAbsMinus>   p10(kPluginIdentifierSub "Difference", kPluginVersionMajor, kPluginVersionMinor);
static MergePluginFactory<kPluginVersionMajor,eMergePluginRoto>       p11(kPluginIdentifierSub "Roto", kPluginVersionMajor, kPluginVersionMinor);
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

// Version 2: with Alpha-only images, the R checkboxes control the alpha channel,
// and the A inputs after the first one are skipped outside of their image
// (minor 1: SupportExt #80 and #82, see the history at the top; MergeRoto: also the plane suite v2 fallback)
static MergePluginFactory<2,eMergePluginMerge>       r1(kPluginIdentifier, 2, 1);
static MergePluginFactory<2,eMergePluginPlus>        r2(kPluginIdentifierSub "Plus", 2, 1);
static MergePluginFactory<2,eMergePluginMatte>       r3(kPluginIdentifierSub "Matte", 2, 1);
static MergePluginFactory<2,eMergePluginMultiply>    r4(kPluginIdentifierSub "Multiply", 2, 1);
static MergePluginFactory<2,eMergePluginIn>          r5(kPluginIdentifierSub "In", 2, 1);
static MergePluginFactory<2,eMergePluginOut>         r6(kPluginIdentifierSub "Out", 2, 1);
static MergePluginFactory<2,eMergePluginScreen>      r7(kPluginIdentifierSub "Screen", 2, 1);
static MergePluginFactory<2,eMergePluginMax>         r8(kPluginIdentifierSub "Max", 2, 1);
static MergePluginFactory<2,eMergePluginMin>         r9(kPluginIdentifierSub "Min", 2, 1);
static MergePluginFactory<2,eMergePluginAbsMinus>   r10(kPluginIdentifierSub "Difference", 2, 1);
static MergePluginFactory<2,eMergePluginRoto>       r11(kPluginIdentifierSub "Roto", 2, 1); // minor 1: renders on hosts without the plane suite v2
mRegisterPluginFactoryInstance(r1)
mRegisterPluginFactoryInstance(r2)
mRegisterPluginFactoryInstance(r3)
mRegisterPluginFactoryInstance(r4)
mRegisterPluginFactoryInstance(r5)
mRegisterPluginFactoryInstance(r6)
mRegisterPluginFactoryInstance(r7)
mRegisterPluginFactoryInstance(r8)
mRegisterPluginFactoryInstance(r9)
mRegisterPluginFactoryInstance(r10)
mRegisterPluginFactoryInstance(r11)

// Keep backward compatibility following the change introduced in
// https://github.com/NatronGitHub/Natron/issues/647
// (minor 1: SupportExt #80 and #82, see the history at the top; MergeRoto: also the plane suite v2 fallback)
static MergePluginFactory<1,eMergePluginMerge>       q1(kPluginIdentifier, 1, 1);
static MergePluginFactory<1,eMergePluginPlus>        q2(kPluginIdentifierSub "Plus", 1, 1);
static MergePluginFactory<1,eMergePluginMatte>       q3(kPluginIdentifierSub "Matte", 1, 1);
static MergePluginFactory<1,eMergePluginMultiply>    q4(kPluginIdentifierSub "Multiply", 1, 1);
static MergePluginFactory<1,eMergePluginIn>          q5(kPluginIdentifierSub "In", 1, 1);
static MergePluginFactory<1,eMergePluginOut>         q6(kPluginIdentifierSub "Out", 1, 1);
static MergePluginFactory<1,eMergePluginScreen>      q7(kPluginIdentifierSub "Screen", 1, 1);
static MergePluginFactory<1,eMergePluginMax>         q8(kPluginIdentifierSub "Max", 1, 1);
static MergePluginFactory<1,eMergePluginMin>         q9(kPluginIdentifierSub "Min", 1, 1);
static MergePluginFactory<1,eMergePluginAbsMinus>   q10(kPluginIdentifierSub "Difference", 1, 1);
static MergePluginFactory<1,eMergePluginRoto>       q11(kPluginIdentifierSub "Roto", 1, 1); // minor 1: renders on hosts without the plane suite v2
mRegisterPluginFactoryInstance(q1)
mRegisterPluginFactoryInstance(q2)
mRegisterPluginFactoryInstance(q3)
mRegisterPluginFactoryInstance(q4)
mRegisterPluginFactoryInstance(q5)
mRegisterPluginFactoryInstance(q6)
mRegisterPluginFactoryInstance(q7)
mRegisterPluginFactoryInstance(q8)
mRegisterPluginFactoryInstance(q9)
mRegisterPluginFactoryInstance(q10)
mRegisterPluginFactoryInstance(q11)

OFXS_NAMESPACE_ANONYMOUS_EXIT
