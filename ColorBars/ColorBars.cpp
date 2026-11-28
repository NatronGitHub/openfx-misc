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
 * OFX ColorBars plugin.
 */

#include <cmath>
#include <climits>
#include <cfloat>
#include <algorithm> // std::min, std::max
#include <array>
#include <string>

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ColorBarsOFX"
#define kPluginGrouping "Image"
#define kPluginDescription \
    "Generate an image with SMPTE color bars, in linear RGB (no alpha: 0 if the output is RGBA).\n" \
    "The top two thirds have seven bars at Bar Intensity (0 to 1 on the slider, 0.5 by default): gray, yellow, cyan, green, magenta, red and blue. Below them, a strip of blue, magenta, cyan and gray patches, alternating with black. The bottom quarter has the -I, white, +Q and black patches and the black-level steps (PLUGE); in 8-bit output, the below-black step clips to black.\n" \
    "PAL removes the NTSC-specific patches: the seven bars fill the image.\n" \
    "See also: https://learn.foundry.com/nuke/content/reference_guide/image_nodes/colorbars.html"
#define kPluginDescriptionV1 \
    "Generate an image with SMPTE RP 219:2002 color bars (version 1, obsolete: version 2 draws the SMPTE bars of the ColorBars node of the same name, in linear RGB, and has a PAL option).\n" \
    "Bar Intensity is in IRE (75 by default, i.e. 75 percent bars), and alpha is opaque (1).\n" \
    "The output of this plugin is broadcast-safe if \"Output IRE\" is unchecked. Be careful that colorbars are defined in a nonlinear colorspace. In order to get linear RGB, this plug-in should be combined with a transformation from the video space to linear.\n" \
    "See also: https://web.archive.org/web/20220807183203/http://www.opticalenquiry.com/nuke/index.php?title=Constant,_CheckerBoard,_ColorBars,_ColorWheel"

#define kPluginIdentifier "net.sf.openfx.ColorBars"
// history:
// 1.0 initial version
// 2.0 the bars of the ColorBars node of the same name: SMPTE bars in linear RGB, Bar Intensity from
//     0 to 1 (0.5 by default), no alpha (RGB output by default), and PAL, which removes the
//     NTSC-specific patches (#286). Version 1 is kept, obsolete.
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV1 0 // the minor version of the obsolete version 1

// version 2: the SMPTE bars in linear RGB, PAL, and an RGB output by default
static constexpr unsigned kVersionLinearBars = 2;

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
// true: GeneratorPlugin sets the output depth from "Output Bit Depth", which the OFX spec
// allows only if host and plugin both declare 1; the Source clip's images are never fetched
#define kSupportsMultipleClipDepths true
#define kRenderThreadSafety eRenderFullySafe

#define kParamBarIntensity "barIntensity"
#define kParamBarIntensityLabel "Bar Intensity"
#define kParamBarIntensityHint "Bar Intensity, in IRE unit."
#define kParamBarIntensityHintV2 "Intensity of the bars and of the strip below them (linear, 0 to 1 on the slider). The bottom patches do not change."
#define kParamBarIntensityDefault 75
#define kParamBarIntensityDefaultV2 0.5

#define kParamPAL "PAL"
#define kParamPALLabel "PAL"
#define kParamPALHint "When checked, the NTSC-specific patches are removed: the seven bars fill the image."

#define kParamOutputIRE "outputIRE"
#define kParamOutputIRELabel "Output IRE"
#define kParamOutputIREHint "When checked, the output is scaled so that 0 is black, the max value is white, and the superblack (under the middle of the magenta bar) has a negative value."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif

namespace ColorBars2 {
enum class PatternEnum
{
    eNTSC, // the bars, the strip and the bottom patches
    ePAL,  // the NTSC-specific patches removed: the bars fill the image
};
} // namespace ColorBars2

class ColorBarsProcessorBase
    : public ImageProcessor
{
protected:
    double _barIntensity;
    ColorBars2::PatternEnum _pattern; // version 2
    bool _outputIRE; // version 1
    OfxRectI _rod; // in pixel coordinates

public:
    /** @brief no arg ctor */
    ColorBarsProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _barIntensity(kParamBarIntensityDefault)
        , _pattern(ColorBars2::PatternEnum::eNTSC)
        , _outputIRE(false)
        , _rod()
    {
        _rod.x1 = _rod.y1 = _rod.x2 = _rod.y2 = 0;
    }

    void setValues(const double barIntensity,
                   const ColorBars2::PatternEnum pattern,
                   const bool outputIRE,
                   const OfxRectI& rod)
    {
        _barIntensity = barIntensity;
        _pattern = pattern;
        _outputIRE = outputIRE;
        _rod = rod;
    }
};

template <class PIX, int nComponents, int max>
class ColorBarsProcessor
    : public ColorBarsProcessorBase
{
public:
    // ctor
    ColorBarsProcessor(ImageEffect &instance)
        : ColorBarsProcessorBase(instance)
    {
        assert(nComponents >= 3);
    }

private:
    // and do some processing
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        float ire[3];

        // push pixels
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            int yhd = (y - _rod.y1) * 1080 / (_rod.y2 - _rod.y1);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                int xhd = (x - _rod.x1) * 1920 / (_rod.x2 - _rod.x1);
                if (yhd < 270) { // bottom row (pluge)
                    if (xhd < 240) {            //  15 IRE
                        ire[0] = ire[1] = ire[2] =  15;
                    } else if (xhd < 548) {     //   0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 960) {     // 100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else if (xhd < 1130) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1198) { //     -2 IRE (superblack)
                        ire[0] = ire[1] = ire[2] =  -2;
                    } else if (xhd < 1268) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1336) {    //   2 IRE
                        ire[0] = ire[1] = ire[2] =   2;
                    } else if (xhd < 1406) { //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1474) { //      4 IRE
                        ire[0] = ire[1] = ire[2] =   4;
                    } else if (xhd < 1680) {    //   0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else {                    //  15 IRE
                        ire[0] = ire[1] = ire[2] =  15;
                    }
                } else if (yhd < 360) { // row that starts with yellow, including the gradient ramp
                    if (xhd < 240) {            // 100,100,0 IRE (yellow)
                        ire[0] = ire[1] = 100; ire[2] = 0;
                    } else if (xhd < 446) {  //      0 IRE (black)
                        ire[0] = ire[1] = ire[2] =   0;
                    } else if (xhd < 1474) { //      gradient from 0 to 100 IRE
                        ire[0] = ire[1] = ire[2] = 100.f * (xhd - 446) / float(1474 - 446);
                    } else if (xhd < 1680) {    // 100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else {                    // 100,0,0 IRE (red)
                        ire[0] = 100; ire[1] = ire[2] = 0;
                    }
                } else if (yhd < 450) { // row that starts with cyan
                    if (xhd < 240) {            // 0,100,100 IRE (cyan)
                        ire[0] = 0; ire[1] = ire[2] = 100;
                    } else if (xhd < 446) {  //    100 IRE (white)
                        ire[0] = ire[1] = ire[2] = 100;
                    } else if (xhd < 1680) { //     75 IRE
                        ire[0] = ire[1] = ire[2] = 75;
                    } else {                    // 0,0,100 IRE (blue)
                        ire[0] = ire[1] = 0; ire[2] = 100;
                    }
                } else { // colorbars
                    if (xhd < 240) {            //  40 IRE
                        ire[0] = ire[1] = ire[2] =  40;
                    } else if (xhd < 446) {     //  75 IRE
                        ire[0] = ire[1] = ire[2] =  75;
                    } else if (xhd < 652) {     // yellow
                        ire[0] = ire[1] = 75; ire[2] = 0;
                    } else if (xhd < 858) {     // cyan
                        ire[0] = 0; ire[1] = ire[2] = 75;
                    } else if (xhd < 1062) {    // green
                        ire[0] = 0; ire[1] = 75; ire[2] = 0;
                    } else if (xhd < 1268) {    // magenta
                        ire[0] = 75; ire[1] = 0; ire[2] = 75;
                    } else if (xhd < 1474) {    // red
                        ire[0] = 75; ire[1] = ire[2] = 0;
                    } else if (xhd < 1680) {    // blue
                        ire[0] = ire[1] = 0; ire[2] = 75;
                    } else {                    //  40 IRE
                        ire[0] = ire[1] = ire[2] =  40;
                    }
                    if (_barIntensity != 75) {
                        ire[0] *= static_cast<float>(_barIntensity / 75.);
                        ire[1] *= static_cast<float>(_barIntensity / 75.);
                        ire[2] *= static_cast<float>(_barIntensity / 75.);
                    }
                }
                if (_outputIRE) {
                    if (max > 1) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = static_cast<PIX>( Color::floatToInt<max + 1>(ire[c] / 100.f) );
                        }
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = static_cast<PIX>(ire[c] / 100.f);
                        }
                    }
                } else {
                    if (max == 65535) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = static_cast<PIX>( 4096 + Color::floatToInt<60160 - 4096>(ire[c] / 100.f) );
                        }
                    } else if (max == 255) {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = static_cast<PIX>( 16 + Color::floatToInt<235 - 16>(ire[c] / 100.f) );
                        }
                    } else {
                        for (int c = 0; c < 3; ++c) {
                            dstPix[c] = static_cast<PIX>( 0.0625 + (0.91796875 - 0.0625) * (ire[c] / 100.f) );
                        }
                    }
                }
                if (nComponents == 4) {
                    // set alpha
                    dstPix[nComponents - 1] = max;
                }
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

// Version 2: the SMPTE bars of the ColorBars node of the same name, in linear RGB.
// The image is cut in proportion to its size, in whole pixels from its bottom-left corner:
// - the top 95/144 of the height: seven bars of equal width, at the bar intensity: gray, yellow,
//   cyan, green, magenta, red, blue;
// - from 49/192 to 49/144 of the height: blue, black, magenta, black, cyan, black, gray, at the
//   bar intensity, under the bars;
// - the bottom 49/192: -I, white, +Q and black, each 5/4 of a bar wide, then the black-level steps
//   (PLUGE: below black, black and above black), each 1/3 of a bar wide, then black.
// With PAL, the seven bars fill the image.
namespace ColorBars2 {
using RGB = std::array<float, 3>;

// the colors of the bars, in units of the bar intensity
constexpr int kNBars = 7;
constexpr RGB kBars[kNBars] = {
    {{1, 1, 1}}, {{1, 1, 0}}, {{0, 1, 1}}, {{0, 1, 0}}, {{1, 0, 1}}, {{1, 0, 0}}, {{0, 0, 1}}
};
// the strip under the bars, in units of the bar intensity
constexpr RGB kStrip[kNBars] = {
    {{0, 0, 1}}, {{0, 0, 0}}, {{1, 0, 1}}, {{0, 0, 0}}, {{0, 1, 1}}, {{0, 0, 0}}, {{1, 1, 1}}
};
// the bottom patches (linear RGB, whatever the bar intensity)
constexpr RGB kMinusI = {{0.f, 0.0048f, 0.048f}};
constexpr float kWhite = 0.9911f;
constexpr RGB kPlusQ = {{0.0168f, 0.f, 0.1144f}};
constexpr float kPluge = 0.003035f;

// The heights, as fractions of the image height: the bottom patches are below
// kBottomNum / kBottomDen, the strip below kStripNum / kStripDen.
constexpr long long kBottomNum = 49;
constexpr long long kBottomDen = 192;
constexpr long long kStripNum = 49;
constexpr long long kStripDen = 144;
// The bottom patches, in units of 1/kBottomUnits of the width (1/12 of a bar): each limit is the
// end of a patch.
constexpr long long kBottomUnits = 12 * kNBars;
constexpr long long kMinusIEnd = 15;
constexpr long long kWhiteEnd = 30;
constexpr long long kPlusQEnd = 45;
constexpr long long kBlackEnd = 60;
constexpr long long kBelowBlackEnd = 64;
constexpr long long kPlugeBlackEnd = 68;
constexpr long long kAboveBlackEnd = 72;

// The color of pixel (x, y) of a w x h image (x, y from its bottom-left corner).
inline RGB
color(long long x,
      long long y,
      long long w,
      long long h,
      float intensity,
      PatternEnum pattern)
{
    const bool ntsc = (pattern == PatternEnum::eNTSC);
    if ( ntsc && (y * kBottomDen < kBottomNum * h) ) {
        const long long u = x * kBottomUnits;
        if (u < kMinusIEnd * w) {
            return kMinusI;
        } else if (u < kWhiteEnd * w) {
            return RGB{{kWhite, kWhite, kWhite}};
        } else if (u < kPlusQEnd * w) {
            return kPlusQ;
        } else if ( (kBlackEnd * w <= u) && (u < kBelowBlackEnd * w) ) {
            return RGB{{-kPluge, -kPluge, -kPluge}};
        } else if ( (kPlugeBlackEnd * w <= u) && (u < kAboveBlackEnd * w) ) {
            return RGB{{kPluge, kPluge, kPluge}};
        }

        return RGB{{0.f, 0.f, 0.f}};
    }
    const int bar = static_cast<int>( (std::min)( static_cast<long long>(kNBars - 1), (std::max)(0LL, x * kNBars / w) ) );
    const RGB& k = ( ntsc && (y * kStripDen < kStripNum * h) ) ? kStrip[bar] : kBars[bar];

    return RGB{{k[0] * intensity, k[1] * intensity, k[2] * intensity}};
}
} // namespace ColorBars2

template <class PIX, int nComponents, int max>
class ColorBars2Processor
    : public ColorBarsProcessorBase
{
public:
    explicit ColorBars2Processor(ImageEffect &instance)
        : ColorBarsProcessorBase(instance)
    {
        static_assert(nComponents >= 3, "ColorBars outputs RGB or RGBA");
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        const long long w = _rod.x2 - _rod.x1;
        const long long h = _rod.y2 - _rod.y1;
        if ( (w <= 0) || (h <= 0) ) {
            return;
        }
        const float intensity = static_cast<float>(_barIntensity);
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }
            PIX *dstPix = static_cast<PIX *>( _dstImg->getPixelAddress(procWindow.x1, y) );
            if (!dstPix) {
                continue;
            }
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const ColorBars2::RGB rgb = ColorBars2::color(x - _rod.x1, y - _rod.y1, w, h, intensity, _pattern);
                for (int c = 0; c < 3; ++c) {
                    if (max == 1) { // float: linear, unclamped
                        dstPix[c] = static_cast<PIX>(rgb[c]);
                    } else if (max == 255) {
                        dstPix[c] = static_cast<PIX>( Color::floatToInt<256>( Color::to_func_srgb(rgb[c]) ) );
                    } else {
                        dstPix[c] = static_cast<PIX>( Color::floatToInt<65536>( Color::to_func_Rec709(rgb[c]) ) );
                    }
                }
                if (nComponents == 4) {
                    dstPix[3] = 0; // no alpha
                }
                dstPix += nComponents;
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ColorBarsPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    ColorBarsPlugin(OfxImageEffectHandle handle,
                    unsigned int majorVersion)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _majorVersion(majorVersion)
        , _barIntensity(NULL)
        , _pal(NULL)
        , _outputIRE(NULL)
        , _srcClip(NULL)
    {
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) );
        _barIntensity = fetchDoubleParam(kParamBarIntensity);
        if (_majorVersion >= kVersionLinearBars) {
            _pal = fetchBooleanParam(kParamPAL);
        } else {
            _outputIRE = fetchBooleanParam(kParamOutputIRE);
        }
        assert( _barIntensity && (_majorVersion >= kVersionLinearBars ? _pal != NULL : _outputIRE != NULL) );
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    template <class PIX, int nComponents, int max>
    void renderDepth(const RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(ColorBarsProcessorBase &, const RenderArguments &args);

    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    const unsigned int _majorVersion;
    DoubleParam* _barIntensity;
    BooleanParam* _pal; // version 2
    BooleanParam* _outputIRE; // version 1
    Clip* _srcClip;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ColorBarsPlugin::setupAndProcess(ColorBarsProcessorBase &processor,
                                 const RenderArguments &args)
{
    const double time = args.time;

    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(time) );

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
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    
    // set the images
    processor.setDstImg( dst.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    OfxRectD rod = {0., 0., 0., 0.};
    if ( !getRegionOfDefinition(time, rod) ) {
        if ( _srcClip && _srcClip->isConnected() ) {
            rod = _srcClip->getRegionOfDefinition(time);
        } else {
            OfxPointD siz = getProjectSize();
            OfxPointD off = getProjectOffset();
            rod.x1 = off.x;
            rod.x2 = off.x + siz.x;
            rod.y1 = off.y;
            rod.y2 = off.y + siz.y;
        }
    }
    OfxRectI rod_pixel;
    Coords::toPixelEnclosing(rod, args.renderScale, dst->getPixelAspectRatio(), &rod_pixel);
    double barIntensity = _barIntensity->getValueAtTime(time);
    const ColorBars2::PatternEnum pattern = (_pal && _pal->getValueAtTime(time)) ? ColorBars2::PatternEnum::ePAL : ColorBars2::PatternEnum::eNTSC;
    const bool outputIRE = _outputIRE && _outputIRE->getValueAtTime(time);
    processor.setValues(barIntensity, pattern, outputIRE, rod_pixel);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ColorBarsPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ColorBarsPlugin::renderInternal(const RenderArguments &args,
                                BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderDepth<unsigned char, nComponents, 255>(args);
        break;
    case eBitDepthUShort:
        renderDepth<unsigned short, nComponents, 65535>(args);
        break;
    case eBitDepthFloat:
        renderDepth<float, nComponents, 1>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int max>
void
ColorBarsPlugin::renderDepth(const RenderArguments &args)
{
    if (_majorVersion >= kVersionLinearBars) {
        ColorBars2Processor<PIX, nComponents, max> processor(*this);
        setupAndProcess(processor, args);
    } else {
        ColorBarsProcessor<PIX, nComponents, max> processor(*this);
        setupAndProcess(processor, args);
    }
}

// the overridden render function
void
ColorBarsPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(OFX_COMPONENTS_OK(dstComponents));

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    }
}


void
ColorBarsPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // output is continuous
    clipPreferences.setOutputHasContinuousSamples(true);

    GeneratorPlugin::getClipPreferences(clipPreferences);
    if (_majorVersion >= kVersionLinearBars) {
        // version 2 has no alpha: an RGB output is opaque ("the image is opaque and so has no
        // premultiplication state", ofxImageEffect.h); in an RGBA output alpha is 0, and the colors
        // are not premultiplied by it. Output Components sets the output components only in the
        // general context (setsOutputComponents()); elsewhere the host chooses them, and the param
        // is a hidden default.
        bool rgba = false;
        if ( setsOutputComponents() ) {
            std::string components;
            if (_outputComponents) {
                _outputComponents->getOption(_outputComponents->getValue(), components);
            }
            rgba = (components == kParamGeneratorOutputComponentsOptionRGBA);
        } else {
            rgba = (_dstClip->getPixelComponents() == ePixelComponentRGBA);
        }
        clipPreferences.setOutputPremultiplication(rgba ? eImageUnPreMultiplied : eImageOpaque);
    }
}


mDeclarePluginFactoryVersioned(ColorBarsPluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
ColorBarsPluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginDescription(majorVersion >= kVersionLinearBars ? kPluginDescription : kPluginDescriptionV1);
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    desc.setPluginGrouping(kPluginGrouping);
    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderTwiceAlways(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentRGBA);
#endif

    generatorDescribe(desc, eGeneratorOverlayDrawSuite); // V2 (DrawSuite) and V1 (OpenGL)
}

template<unsigned int majorVersion>
void
ColorBarsPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                        ContextEnum context)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    // version 2: RGB by default (the bars have no alpha)
    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault,
                               majorVersion >= kVersionLinearBars ? ePixelComponentRGB : ePixelComponentRGBA, true, context);

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamBarIntensity);
        param->setLabel(kParamBarIntensityLabel);
        if (majorVersion >= kVersionLinearBars) {
            param->setHint(kParamBarIntensityHintV2);
            param->setDefault(kParamBarIntensityDefaultV2);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 1.);
        } else {
            param->setHint(kParamBarIntensityHint);
            param->setDefault(kParamBarIntensityDefault);
            param->setRange(0., 100.);
            param->setDisplayRange(0., 100.);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    if (majorVersion >= kVersionLinearBars) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPAL);
        param->setLabel(kParamPALLabel);
        param->setHint(kParamPALHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    if (majorVersion < kVersionLinearBars) {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamOutputIRE);
        param->setLabel(kParamOutputIRELabel);
        param->setHint(kParamOutputIREHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
}

template<unsigned int majorVersion>
ImageEffect*
ColorBarsPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                     ContextEnum /*context*/)
{
    return new ColorBarsPlugin(handle, majorVersion);
}

// Each major version has its own factory class (FactoryMainEntryHelper stores the plugin UID in a
// static shared by all the objects of a class).
static ColorBarsPluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
static ColorBarsPluginFactory<1> p_v1(kPluginIdentifier, kPluginVersionMinorV1); // obsolete (Natron 2.5.0)
mRegisterPluginFactoryInstance(p)
mRegisterPluginFactoryInstance(p_v1)

OFXS_NAMESPACE_ANONYMOUS_EXIT
