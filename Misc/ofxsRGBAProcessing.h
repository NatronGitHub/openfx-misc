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
 * Helpers shared by the openfx-misc color plugins that process selected
 * R/G/B/A channels with a mask/mix (Add, Multiply, Gamma, Clamp, Grade, ...).
 */

#ifndef openfx_misc_ofxsRGBAProcessing_h
#define openfx_misc_ofxsRGBAProcessing_h

#include <string>

#include "ofxsImageEffect.h"
#include "ofxsCoords.h"
#include "ofxsMaskMix.h"

namespace OFX {
// Implementation of ofxsProcessRGBA() and ofxsProcessRGB(): each function turns one
// runtime bool into a template argument, then calls the function for the next channel.
namespace RGBAProcessingDetail {
template <class PROC, bool processR, bool processG, bool processB>
inline void
dispatchA(PROC &proc, bool a, const OfxRectI& procWindow, const OfxPointD& rs)
{
    if (a) {
        proc.template process<processR, processG, processB, true >(procWindow, rs);
    } else {
        proc.template process<processR, processG, processB, false>(procWindow, rs);
    }
}

template <class PROC, bool processR, bool processG>
inline void
dispatchB(PROC &proc, bool b, bool a, const OfxRectI& procWindow, const OfxPointD& rs)
{
    if (b) {
        dispatchA<PROC, processR, processG, true >(proc, a, procWindow, rs);
    } else {
        dispatchA<PROC, processR, processG, false>(proc, a, procWindow, rs);
    }
}

template <class PROC, bool processR>
inline void
dispatchG(PROC &proc, bool g, bool b, bool a, const OfxRectI& procWindow, const OfxPointD& rs)
{
    if (g) {
        dispatchB<PROC, processR, true >(proc, b, a, procWindow, rs);
    } else {
        dispatchB<PROC, processR, false>(proc, b, a, procWindow, rs);
    }
}

template <class PROC, bool processR, bool processG>
inline void
dispatchRGB_B(PROC &proc, bool b, const OfxRectI& procWindow, const OfxPointD& rs)
{
    if (b) {
        proc.template process<processR, processG, true >(procWindow, rs);
    } else {
        proc.template process<processR, processG, false>(procWindow, rs);
    }
}

template <class PROC, bool processR>
inline void
dispatchRGB_G(PROC &proc, bool g, bool b, const OfxRectI& procWindow, const OfxPointD& rs)
{
    if (g) {
        dispatchRGB_B<PROC, processR, true >(proc, b, procWindow, rs);
    } else {
        dispatchRGB_B<PROC, processR, false>(proc, b, procWindow, rs);
    }
}
} // namespace RGBAProcessingDetail

/** @brief Call proc.process<processR, processG, processB, processA>(procWindow, rs),
   where the template arguments are the selected channels that exist in an image
   with nComponents components. */
template <int nComponents, class PROC>
inline void
ofxsProcessRGBA(PROC &proc,
                bool processR,
                bool processG,
                bool processB,
                bool processA,
                const OfxRectI& procWindow,
                const OfxPointD& rs)
{
#ifndef __COVERITY__ // too many coverity[dead_error_line] errors
    const bool r = processR && (nComponents != 1);
    const bool g = processG && (nComponents >= 2);
    const bool b = processB && (nComponents >= 3);
    const bool a = processA && (nComponents == 1 || nComponents == 4);
    if (r) {
        RGBAProcessingDetail::dispatchG<PROC, true >(proc, g, b, a, procWindow, rs);
    } else {
        RGBAProcessingDetail::dispatchG<PROC, false>(proc, g, b, a, procWindow, rs);
    }
#else
    (void)proc; (void)processR; (void)processG; (void)processB; (void)processA; (void)procWindow; (void)rs;
#endif
}

/** @brief Same as ofxsProcessRGBA, for processors that only select R, G and B:
   calls proc.process<processR, processG, processB>(procWindow, rs). */
template <int nComponents, class PROC>
inline void
ofxsProcessRGB(PROC &proc,
               bool processR,
               bool processG,
               bool processB,
               const OfxRectI& procWindow,
               const OfxPointD& rs)
{
#ifndef __COVERITY__ // too many coverity[dead_error_line] errors
    const bool r = processR && (nComponents != 1);
    const bool g = processG && (nComponents >= 2);
    const bool b = processB && (nComponents >= 3);
    if (r) {
        RGBAProcessingDetail::dispatchRGB_G<PROC, true >(proc, g, b, procWindow, rs);
    } else {
        RGBAProcessingDetail::dispatchRGB_G<PROC, false>(proc, g, b, procWindow, rs);
    }
#else
    (void)proc; (void)processR; (void)processG; (void)processB; (void)procWindow; (void)rs;
#endif
}

/** @brief Copy back the original values of the channels that were not processed
   (or zero if there is no source pixel). */
template <class PIX, int nComponents, bool processR, bool processG, bool processB, bool processA>
inline void
ofxsCopyBackUnprocessedChannels(const PIX *srcPix,
                                PIX *dstPix)
{
    if (nComponents == 1) {
        if (!processA) {
            dstPix[0] = srcPix ? srcPix[0] : PIX();
        }
    } else if ( (nComponents == 3) || (nComponents == 4) ) {
        if (!processR) {
            dstPix[0] = srcPix ? srcPix[0] : PIX();
        }
        if (!processG) {
            dstPix[1] = srcPix ? srcPix[1] : PIX();
        }
        if (!processB) {
            dstPix[2] = srcPix ? srcPix[2] : PIX();
        }
        if ( !processA && (nComponents == 4) ) {
            dstPix[3] = srcPix ? srcPix[3] : PIX();
        }
    }
}

/** @brief When the user connects the source clip, and the premult parameter was never
   changed by the user, set premult according to the source premultiplication state.
   To be called from ImageEffect::changedClip(). */
inline void
ofxsPremultUpdateFromSourceClip(const InstanceChangedArgs &args,
                                const std::string &clipName,
                                Clip *srcClip,
                                BooleanParam *premult,
                                BooleanParam *premultChanged)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         srcClip && srcClip->isConnected() &&
         !premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        if (srcClip->getPixelComponents() != ePixelComponentRGBA) {
            premult->setValue(false);
        } else {
            switch ( srcClip->getPreMultiplication() ) {
            case eImageOpaque:
                premult->setValue(false);
                break;
            case eImagePreMultiplied:
                premult->setValue(true);
                break;
            case eImageUnPreMultiplied:
                premult->setValue(false);
                break;
            }
        }
    }
}

/** @brief Returns true if masking is enabled with a non-inverted mask whose RoD does
   not intersect the render window, in which case the effect is identity.
   To be called from ImageEffect::isIdentity(). */
inline bool
ofxsMaskRoDExcludesRenderWindow(const IsIdentityArguments &args,
                                Clip *maskClip,
                                BooleanParam *maskApply,
                                BooleanParam *maskInvert)
{
    // Skipped on hosts without multi-resolution: there, all inputs have the same RoD anyway,
    // and some (e.g. Sony Catalyst Edit) return clip RoDs in pixels instead of canonical coordinates.
    OfxRectI maskRoD;

    return ( getImageEffectHostDescription()->supportsMultiResolution &&
             ofxsMaskGetRoDPixel(maskClip, maskApply, maskInvert, args.time, args.renderScale, &maskRoD, args.fieldToRender) &&
             !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) );
}
} // namespace OFX

#endif // openfx_misc_ofxsRGBAProcessing_h
