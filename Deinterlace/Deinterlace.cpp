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
 * OFX Deinterlace plugin.
 */

#include <cstring> // for memcpy
#include <cmath>
#include <algorithm>

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#include "ofxsProcessing.H"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "DeinterlaceOFX"
#define kPluginGrouping      "Time"
#define kPluginDescription \
    "Deinterlace input stream.\n" \
    "The following deinterlacing algorithms are supported:\n" \
    "- Weave: This is what 100fps.com calls \"do nothing\". Other names: \"disabled\" or \"no deinterlacing\". Should be used for PsF content.\n" \
    "- Blend: Blender (full resolution). Each line of the picture is created as the average of a line from the odd and a line from the even half-pictures. This ignores the fact that they are supposed to be displayed at different times.\n" \
    "- Bob: Doubler. Display each half-picture like a full picture, by simply displaying each line twice. Preserves temporal resolution of interlaced video.\n" \
    "- Discard: Only display one of the half-pictures, discard the other. Other name: \"single field\". Both temporal and vertical spatial resolutions are halved. Can be used for slower computers or to give interlaced video movie-like look with characteristic judder.\n" \
    "- Linear: Doubler. Bob with linear interpolation: instead of displaying each line twice, line 2 is created as the average of line 1 and 3, etc.\n" \
    "- Mean: Blender (half resolution). Display a half-picture that is created as the average of the two original half-pictures.\n" \
    "- Yadif: Interpolator (Yet Another DeInterlacing Filter) from MPlayer by Michael Niedermayer (http://www.mplayerhq.hu). It checks pixels of previous, current and next frames to re-create the missed field by some local adaptive method (edge-directed interpolation) and uses spatial check to prevent most artifacts." \

#define kPluginIdentifier    "net.sf.openfx.Deinterlace"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 0
#define kSupportsMultiResolution 0
#define kSupportsRenderScale 1 // are images still fielded at any renderscale?
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamMode "mode"
#define kParamModeLabel "Deinterlacing Mode"
#define kParamModeHint "Choice of the deinterlacing mode/algorithm"
#define kParamModeOptionWeave "Weave", "This is what 100fps.com calls \"do nothing\". Other names: \"disabled\" or \"no deinterlacing\". Should be used for PsF content.", "weave"
#define kParamModeOptionBlend "Blend", "Blender (full resolution). Each line of the picture is created as the average of a line from the odd and a line from the even half-pictures. This ignores the fact that they are supposed to be displayed at different times.", "blend"
#define kParamModeOptionBob "Bob", "Doubler. Display each half-picture like a full picture, by simply displaying each line twice. Preserves temporal resolution of interlaced video.", "bob"
#define kParamModeOptionDiscard "Discard", "Only display one of the half-pictures, discard the other. Other name: \"single field\". Both temporal and vertical spatial resolutions are halved. Can be used for slower computers or to give interlaced video movie-like look with characteristic judder.", "discard"
#define kParamModeOptionLinear "Linear", "Doubler. Bob with linear interpolation: instead of displaying each line twice, line 2 is created as the average of line 1 and 3, etc.", "linear"
#define kParamModeOptionMean "Mean", "Blender (half resolution). Display a half-picture that is created as the average of the two original half-pictures.", "mean"
#define kParamModeOptionYadif "Yadif", "Interpolator (Yet Another DeInterlacing Filter) from MPlayer by Michael Niedermayer (http://www.mplayerhq.hu). It checks pixels of previous, current and next frames to re-create the missed field by some local adaptive method (edge-directed interpolation) and uses spatial check to prevent most artifacts.", "yadif"

enum DeinterlaceModeEnum
{
    eDeinterlaceModeWeave,
    eDeinterlaceModeBlend,
    eDeinterlaceModeBob,
    eDeinterlaceModeDiscard,
    eDeinterlaceModeLinear,
    eDeinterlaceModeMean,
    eDeinterlaceModeYadif,
};

#define kParamFieldOrder "fieldOrder"
#define kParamFieldOrderLabel "Field Order"
#define kParamFieldOrderHint "Interlaced field order"
#define kParamFieldOrderOptionLower "Lower field first", "Lower field first.", "lower"
#define kParamFieldOrderOptionUpper "Upper field first", "Upper field first", "upper"
#define kParamFieldOrderOptionAuto "HD=upper,SD=lower", "Automatic.", "auto"

enum FieldOrderEnum
{
    eFieldOrderLower,
    eFieldOrderUpper,
    eFieldOrderAuto,
};

#define kParamParity "parity"
#define kParamParityLabel "Parity"
#define kParamParityHint "Field to interpolate."
#define kParamParityOptionLower "Lower", "Interpolate lower field.", "lower"
#define kParamParityOptionUpper "Upper", "Interpolate upper field.", "upper"

enum ParityEnum
{
    eParityLower,
    eParityUpper,
};

#define kParamDoubleFramerate "doubleFramerate"
#define kParamDoubleFramerateLabel "Double Framerate"
#define kParamDoubleFramerateHint "Each input frame produces two output frames, and the framerate is doubled."

#define kParamYadifMode "yadifMode"
#define kParamYadifModeLabel "Yadif Processing Mode"
#define kParamYadifModeHint "Mode of checking fields"
#define kParamYadifModeOptionTemporalSpatial "Temporal & spatial", "Temporal and spatial interlacing check (default).", "temporalspatial"
#define kParamYadifModeOptionTemporal "Temporal only", "Skips spatial interlacing check.", "temporal"

enum YadifModeEnum
{
    eYadifModeTemporalSpatial,
    eYadifModeTemporal,
};

class DeinterlacePlugin
    : public ImageEffect
{
public:
    DeinterlacePlugin(OfxImageEffectHandle handle) : ImageEffect(handle), _dstClip(NULL), _srcClip(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);

        mode = fetchChoiceParam("mode");
        fieldOrder = fetchChoiceParam("fieldOrder");
        parity = fetchChoiceParam("parity");
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /** @brief get the clip preferences */
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* override is identity */
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** Override the get frames needed action */
    virtual void getFramesNeeded(const FramesNeededArguments &args, FramesNeededSetter &frames) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    ChoiceParam *fieldOrder, *mode, *parity;
};


// =========== GNU Lesser General Public License code start =================

// Yadif (yet another deinterlacing filter)
// http://avisynth.org.ru/yadif/yadif.html
// http://mplayerhq.hu

// Original port to OFX/Vegas by George Yohng http://yohng.com
// Rewritten after yadif relicensing to LGPL:
// http://git.videolan.org/?p=ffmpeg.git;a=commit;h=194ef56ba7e659196fe554782d797b1b45c3915f


// Copyright notice and licence from the original libavfilter/vf_yadif.c file:
/*
 * Copyright (C) 2006-2011 Michael Niedermayer <michaelni@gmx.at>
 *               2010      James Darnley <james.darnley@gmail.com>
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

//#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
//#define FFMAX(a,b) ((a) < (b) ? (b) : (a))
//#define FFABS(a) ((a) > 0 ? (a) : (-(a)))
#define FFMIN(a, b) (std::min)(a, b)
#define FFMAX(a, b) (std::max)(a, b)
#define FFABS(a) std::abs(a)

#define FFMIN3(a, b, c) FFMIN(FFMIN(a, b), c)
#define FFMAX3(a, b, c) FFMAX(FFMAX(a, b), c)

inline float
halven(float f) { return f * 0.5f; }

inline int
halven(int i) { return i >> 1; }

inline int
one1(unsigned char *) { return 1; }

inline int
one1(unsigned short *) { return 1; }

inline float
one1(float *) { return 0.f; }

#define CHECK(j) \
    {   Diff score = FFABS(cur[mrefs + ch * ( -1 + (j) )] - cur[prefs + ch * ( -1 - (j) )]) \
                     + FFABS(cur[mrefs   + ch * (j)] - cur[prefs   - ch * (j)]) \
                     + FFABS(cur[mrefs + ch * ( 1 + (j) )] - cur[prefs + ch * ( 1 - (j) )]); \
        if (score < spatial_score) { \
            spatial_score = score; \
            spatial_pred = halven(cur[mrefs  + ch * (j)] + cur[prefs  - ch * (j)]); \

/* The is_not_edge argument here controls when the code will enter a branch
 * which reads up to and including x-3 and x+3. */

#define FILTER(start, end, is_not_edge) \
    for (x = start; x < end; x++) { \
        Diff c = cur[mrefs]; \
        Diff d = halven(prev2[0] + next2[0]); \
        Diff e = cur[prefs]; \
        Diff temporal_diff0 = FFABS(prev2[0] - next2[0]); \
        Diff temporal_diff1 = halven( FFABS(prev[mrefs] - c) + FFABS(prev[prefs] - e) ); \
        Diff temporal_diff2 = halven( FFABS(next[mrefs] - c) + FFABS(next[prefs] - e) ); \
        Diff diff = FFMAX3(halven(temporal_diff0), temporal_diff1, temporal_diff2); \
        Diff spatial_pred = halven(c + e); \
 \
        if (is_not_edge) { \
            Diff spatial_score = FFABS(cur[mrefs - ch] - cur[prefs - ch]) + FFABS(c - e) \
                                 + FFABS(cur[mrefs + ch] - cur[prefs + ch]) - one1( (Comp*)0 ); \
            CHECK(-1) CHECK(-2) } \
    } \
    } \
    } \
    CHECK( 1) CHECK( 2) } \
    } \
    } \
    } \
    } \
 \
    if ( !(mode & 2) ) { \
        Diff b = halven(prev2[2 * mrefs] + next2[2 * mrefs]); \
        Diff f = halven(prev2[2 * prefs] + next2[2 * prefs]); \
        Diff max = FFMAX3( d - e, d - c, FFMIN(b - c, f - e) ); \
        Diff min = FFMIN3( d - e, d - c, FFMAX(b - c, f - e) ); \
 \
        diff = FFMAX3(diff, min, -max); \
    } \
 \
    if (spatial_pred > d + diff) { \
        spatial_pred = d + diff; } \
    else if (spatial_pred < d - diff) { \
        spatial_pred = d - diff; } \
 \
    dst[0] = (Comp)spatial_pred; \
 \
    dst += ch; \
    cur += ch; \
    prev += ch; \
    next += ch; \
    prev2 += ch; \
    next2 += ch; \
    }

template<int ch, typename Comp, typename Diff>
inline void
filter_line_c(Comp *dst1,
              const Comp *prev1,
              const Comp *cur1,
              const Comp *next1,
              int w,
              int prefs,
              int mrefs,
              int parity,
              int mode)
{
    Comp *dst  = dst1;
    const Comp *prev = prev1;
    const Comp *cur  = cur1;
    const Comp *next = next1;
    int x;
    const Comp *prev2 = parity ? prev : cur;
    const Comp *next2 = parity ? cur  : next;

    /* The function is called with the pointers already pointing to data[3] and
     * with 6 subtracted from the width.  This allows the FILTER macro to be
     * called so that it processes all the pixels normally.  A constant value of
     * true for is_not_edge lets the compiler ignore the if statement. */
    FILTER(0, w, 1)
}

#define MAX_ALIGN 8
template<int ch, typename Comp, typename Diff>
inline void
filter_edges(Comp *dst1,
             const Comp *prev1,
             const Comp *cur1,
             const Comp *next1,
             int w,
             int prefs,
             int mrefs,
             int parity,
             int mode)
{
    Comp *dst  = dst1;
    const Comp *prev = prev1;
    const Comp *cur  = cur1;
    const Comp *next = next1;
    int x;
    const Comp *prev2 = parity ? prev : cur;
    const Comp *next2 = parity ? cur  : next;

    /* Only edge pixels need to be processed here.  A constant value of false
     * for is_not_edge should let the compiler ignore the whole branch. */
    FILTER(0, 3, 0)

    dst  = dst1  + (w - 3) * ch;
    prev = prev1 + (w - 3) * ch;
    cur  = cur1  + (w - 3) * ch;
    next = next1 + (w - 3) * ch;
    prev2 = parity ? prev : cur;
    next2 = parity ? cur  : next;

    FILTER(w - 3, w, 0)
}

#if 0
inline void
interpolate(unsigned char *dst,
            const unsigned char *cur0,
            const unsigned char *cur2,
            int w)
{
    int x;

    for (x = 0; x < w; x++) {
        dst[x] = (cur0[x] + cur2[x] + 1) >> 1; // simple average
    }
}

inline void
interpolate(float *dst,
            const float *cur0,
            const float *cur2,
            int w)
{
    int x;

    for (x = 0; x < w; x++) {
        dst[x] = (cur0[x] + cur2[x] ) * 0.5f; // simple average
    }
}

#endif

template<int ch, typename Comp, typename Diff>
static void
filter_plane(int mode,
             Comp *dst,
             int dst_stride,
             const Comp *prev0,
             const Comp *cur0,
             const Comp *next0,
             int refs,
             int w,
             int h,
             int parity,
             int tff)
{
    int pix_3 = 3 * ch;

    for (int y = 0; y < h; ++y) {
        if ( ( (y ^ parity) & 1 ) ) {
            const Comp *prev = prev0 + y * refs;
            const Comp *cur = cur0 + y * refs;
            const Comp *next = next0 + y * refs;
            Comp *dst2 = dst + y * dst_stride;
            int mode2 = y == 1 || y + 2 == h ? 2 : mode;

            for (int c = 0; c < ch; ++c) {
                filter_line_c<ch, Comp, Diff>(dst2 + c + pix_3, prev + c + pix_3, cur + c + pix_3, next + c + pix_3, w - 6,
                                              y + 1 < h ? refs : -refs,
                                              y ? -refs : refs,
                                              parity ^ tff, mode2);
                filter_edges<ch, Comp, Diff>(dst2 + c, prev + c, cur + c, next + c, w,
                                             y + 1 < h ? refs : -refs,
                                             y ? -refs : refs,
                                             parity ^ tff, mode2);
            }
        } else {
            std::memcpy( &dst[y * dst_stride],
                         &cur0[y * refs], w * ch * sizeof(Comp) ); // copy original
        }
    }
}

template<int ch, typename Comp, typename Diff>
static void
filter_plane_ofx(int mode,
                 Image *dst_,
                 const Image *srcp,
                 const Image *src,
                 const Image *srcn,
                 int parity,
                 int tff)
{
    Comp *dst = (Comp*)dst_->getPixelData(); // change this when we support renderWindow
    int dst_stride = dst_->getRowBytes() / sizeof(Comp); // may be negative, @see kOfxImagePropRowBytes
    const Comp *prev0 = (const Comp*)( srcp ? srcp->getPixelData() : src->getPixelData() );
    const Comp *cur0 = (const Comp*)src->getPixelData();
    const Comp *next0 = (const Comp*)( srcn ? srcn->getPixelData() : src->getPixelData() );
    int refs = src->getRowBytes() / sizeof(Comp); // may be negative, @see kOfxImagePropRowBytes
    const OfxRectI bounds = dst_->getBounds();

    filter_plane<ch, Comp, Diff>(mode, dst, dst_stride,
                                 prev0, cur0, next0,
                                 refs,
                                 bounds.x2 - bounds.x1, bounds.y2 - bounds.y1,
                                 parity, tff);
}

// =========== GNU Lesser General Public License code end =================

void
DeinterlacePlugin::render(const RenderArguments &args)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
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
    auto_ptr<const Image> srcp( ( _srcClip && _srcClip->isConnected() ) ?
                                     _srcClip->fetchImage(args.time - 1) : 0 );
    auto_ptr<const Image> srcn( ( _srcClip && _srcClip->isConnected() ) ?
                                     _srcClip->fetchImage(args.time + 1) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        //All the code below expects src to be valid
        setPersistentMessage(Message::eMessageError, "", "Failed to fetch input image");
        throwSuiteStatusException(kOfxStatFailed);
    }
    if ( srcp.get() ) {
        if ( (srcp->getRenderScale().x != args.renderScale.x) ||
             ( srcp->getRenderScale().y != args.renderScale.y) ||
             ( ( srcp->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcp->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if ( srcn.get() ) {
        if ( (srcn->getRenderScale().x != args.renderScale.x) ||
             ( srcn->getRenderScale().y != args.renderScale.y) ||
             ( ( srcn->getField() != eFieldNone) /* for DaVinci Resolve */ && ( srcn->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }

    const OfxRectI rect = dst->getBounds();
    int width = rect.x2 - rect.x1;
    int height = rect.y2 - rect.y1;
    int imode       = 0;
    int ifieldOrder = 2;
    int iparity     = 0;

    mode->getValueAtTime(args.time, imode);
    fieldOrder->getValueAtTime(args.time, ifieldOrder);
    parity->getValueAtTime(args.time, iparity);

    imode *= 2;

    if (ifieldOrder == 2) {
        if (width > 1024) {
            ifieldOrder = 1;
        } else {
            ifieldOrder = 0;
        }
    }


    if ( (width < 3) || (height < 3) ) {
        // Video of less than 3 columns or lines is not supported
        // just copy src to dst
        for (int y = 0; y < height; y++) {
            unsigned char* lineStart = (unsigned char*)dst->getPixelAddress(0, y);
            unsigned int lineLen = std::abs( dst->getRowBytes() );
            if ( src.get() ) {
                std::memcpy( lineStart, src->getPixelAddress(0, y), lineLen );
            } else {
                std::fill( lineStart, lineStart + lineLen, 0 );
            }
        }
    } else {
        if (dstComponents == ePixelComponentRGBA) {
            switch (dstBitDepth) {
            case eBitDepthUByte:
                filter_plane_ofx<4, unsigned char, int>(imode, // mode
                                                        dst.get(),
                                                        srcp.get(), src.get(), srcn.get(),
                                                        iparity, ifieldOrder); // parity, tff
                break;

            case eBitDepthUShort:
                filter_plane_ofx<4, unsigned short, int>(imode, // mode
                                                         dst.get(),
                                                         srcp.get(), src.get(), srcn.get(),
                                                         iparity, ifieldOrder); // parity, tff
                break;

            case eBitDepthFloat:
                filter_plane_ofx<4, float, float>(imode,   // mode
                                                  dst.get(),
                                                  srcp.get(), src.get(), srcn.get(),
                                                  iparity, ifieldOrder);  // parity, tff
                break;

            default:
                break;
            }
        } else if (dstComponents == ePixelComponentRGB) {
            switch (dstBitDepth) {
            case eBitDepthUByte:
                filter_plane_ofx<3, unsigned char, int>(imode,   // mode
                                                        dst.get(),
                                                        srcp.get(), src.get(), srcn.get(),
                                                        iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthUShort:
                filter_plane_ofx<3, unsigned short, int>(imode,   // mode
                                                         dst.get(),
                                                         srcp.get(), src.get(), srcn.get(),
                                                         iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthFloat:
                filter_plane_ofx<3, float, float>(imode,   // mode
                                                  dst.get(),
                                                  srcp.get(), src.get(), srcn.get(),
                                                  iparity, ifieldOrder);  // parity, tff
                break;

            default:
                break;
            }
#ifdef OFX_EXTENSIONS_NATRON
        } else if (dstComponents == ePixelComponentXY) {
            switch (dstBitDepth) {
            case eBitDepthUByte:
                filter_plane_ofx<2, unsigned char, int>(imode,   // mode
                                                        dst.get(),
                                                        srcp.get(), src.get(), srcn.get(),
                                                        iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthUShort:
                filter_plane_ofx<2, unsigned short, int>(imode,   // mode
                                                         dst.get(),
                                                         srcp.get(), src.get(), srcn.get(),
                                                         iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthFloat:
                filter_plane_ofx<2, float, float>(imode,   // mode
                                                  dst.get(),
                                                  srcp.get(), src.get(), srcn.get(),
                                                  iparity, ifieldOrder);  // parity, tff
                break;

            default:
                break;
            }
#endif
        } else if (dstComponents == ePixelComponentAlpha) {
            switch (dstBitDepth) {
            case eBitDepthUByte:
                filter_plane_ofx<1, unsigned char, int>(imode,   // mode
                                                        dst.get(),
                                                        srcp.get(), src.get(), srcn.get(),
                                                        iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthUShort:
                filter_plane_ofx<1, unsigned short, int>(imode,   // mode
                                                         dst.get(),
                                                         srcp.get(), src.get(), srcn.get(),
                                                         iparity, ifieldOrder);  // parity, tff
                break;

            case eBitDepthFloat:
                filter_plane_ofx<1, float, float>(imode,   // mode
                                                  dst.get(),
                                                  srcp.get(), src.get(), srcn.get(),
                                                  iparity, ifieldOrder);  // parity, tff
                break;

            default:
                break;
            }
        }
    }
} // DeinterlacePlugin::render

/* Override the clip preferences */
void
DeinterlacePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // set the fielding of _dstClip
    clipPreferences.setOutputFielding(eFieldNone);
}

bool
DeinterlacePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                         OfxRectD & /*rod*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

bool
DeinterlacePlugin::isIdentity(const IsIdentityArguments &args,
                              Clip * & /*identityClip*/,
                              double & /*identityTime*/
, int& /*view*/, std::string& /*plane*/)
{
    if ( !kSupportsRenderScale && ( (args.renderScale.x != 1.) || (args.renderScale.y != 1.) ) ) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

void
DeinterlacePlugin::getFramesNeeded(const FramesNeededArguments &args,
                                   FramesNeededSetter &frames)
{
    OfxRangeD range;

    range.min = args.time - 1;
    range.max = args.time + 1;
    frames.setFramesNeeded(*_srcClip, range);
}

mDeclarePluginFactory(DeinterlacePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DeinterlacePluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
DeinterlacePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                            ContextEnum /*context*/)
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
    srcClip->setTemporalClipAccess(true);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);
    srcClip->setFieldExtraction(eFieldExtractBoth);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);
    dstClip->setFieldExtraction(eFieldExtractBoth);


    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamMode);
        param->setLabel(kParamModeLabel);
        param->setHint(kParamModeHint);
        assert(param->getNOptions() == eDeinterlaceModeWeave);
        param->appendOption(kParamModeOptionWeave);
        assert(param->getNOptions() == eDeinterlaceModeBlend);
        param->appendOption(kParamModeOptionBlend);
        assert(param->getNOptions() == eDeinterlaceModeBob);
        param->appendOption(kParamModeOptionBob);
        assert(param->getNOptions() == eDeinterlaceModeDiscard);
        param->appendOption(kParamModeOptionDiscard);
        assert(param->getNOptions() == eDeinterlaceModeLinear);
        param->appendOption(kParamModeOptionLinear);
        assert(param->getNOptions() == eDeinterlaceModeMean);
        param->appendOption(kParamModeOptionMean);
        assert(param->getNOptions() == eDeinterlaceModeYadif);
        param->appendOption(kParamModeOptionYadif);
        param->setDefault( int(eDeinterlaceModeYadif) );
        param->setAnimates(true); // can animate
        param->setIsSecretAndDisabled(true); // Not yet implemented!
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFieldOrder);
        param->setLabel(kParamFieldOrderLabel);
        param->setHint(kParamFieldOrderHint);
        assert(param->getNOptions() == eFieldOrderLower);
        param->appendOption(kParamFieldOrderOptionLower);
        assert(param->getNOptions() == eFieldOrderUpper);
        param->appendOption(kParamFieldOrderOptionUpper);
        assert(param->getNOptions() == eFieldOrderAuto);
        param->appendOption(kParamFieldOrderOptionAuto);
        param->setDefault( int(eFieldOrderAuto) );
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamParity);
        param->setLabel(kParamParityLabel);
        param->setHint(kParamParityHint);
        assert(param->getNOptions() == eParityLower);
        param->appendOption(kParamParityOptionLower);
        assert(param->getNOptions() == eParityUpper);
        param->appendOption(kParamParityOptionUpper);
        param->setDefault( int(eParityLower) );
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamYadifMode);
        param->setLabel(kParamYadifModeLabel);
        param->setHint(kParamYadifModeHint);
        assert(param->getNOptions() == eYadifModeTemporalSpatial);
        param->appendOption(kParamYadifModeOptionTemporalSpatial);
        assert(param->getNOptions() == eYadifModeTemporal);
        param->appendOption(kParamYadifModeOptionTemporal);
        param->setDefault(eYadifModeTemporalSpatial);
        param->setAnimates(true); // can animate
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamDoubleFramerate);
        param->setLabel(kParamDoubleFramerateLabel);
        param->setHint(kParamDoubleFramerateHint);
        param->setIsSecretAndDisabled(true); // Not yet implemented!
        if (page) {
            page->addChild(*param);
        }
    }
} // DeinterlacePluginFactory::describeInContext

ImageEffect*
DeinterlacePluginFactory::createInstance(OfxImageEffectHandle handle,
                                         ContextEnum /*context*/)
{
    return new DeinterlacePlugin(handle);
}

static DeinterlacePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
