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
 * "Before"/"After" behaviour outside a frame range (Original, Hold, Black, Loop, Bounce),
 * shared by FrameRange and Retime.
 */

#ifndef openfx_misc_FrameRangeBeforeAfter_h
#define openfx_misc_FrameRangeBeforeAfter_h

#include <cassert>
#include <string>

#include "ofxsImageEffect.h"

#define kParamBefore "before"
#define kParamBeforeLabel "Before"
#define kParamAfter "after"
#define kParamAfterLabel "After"
#define kParamBeforeAfterOptionOriginal "Original", "Return the original frame from the source, even if it is out of the frame range.", "original"
#define kParamBeforeAfterOptionHold "Hold", "Return the nearest frame within the frame range.", "hold"
#define kParamBeforeAfterOptionBlack "Black", "Return an empty frame.", "black"
#define kParamBeforeAfterOptionLoop "Loop", "Substitutes an equal number of frames, effectively creating a clip loop.", "loop"
#define kParamBeforeAfterOptionBounce "Bounce", "Substitutes a reversed equal number of frames, creating a clip bounce.", "bounce"
// FrameRange shipped Bounce with the duplicate id "loop". Natron restores a choice by index if the id
// at that index still matches, else by id, so changing it would reload old Bounce projects as Loop.
#define kParamBeforeAfterOptionBounceLegacy "Bounce", "Substitutes a reversed equal number of frames, creating a clip bounce.", "loop"

namespace OFX {
namespace FrameRangeBeforeAfter {

// the option order is saved in projects: only append new options
enum BeforeAfterEnum
{
    eBeforeAfterOriginal = 0,
    eBeforeAfterHold,
    eBeforeAfterBlack,
    eBeforeAfterLoop,
    eBeforeAfterBounce,
};

// returns a frame between x and y for frame f
inline int
frameBounce(int x,
            int y,
            int f)
{
    if (y <= x) {
        // empty or single-frame range: avoid a division by zero below
        return x;
    }
    if (f > y) {
        f = x + (f - x) % ((y-x)*2);
        if (f > y) {
            f = y - (f - y);
        }
    } else if (f < x) {
        f = y - (y - f) % ((y-x)*2);
        if (f < x) {
            f = x + (x - f);
        }
    }
    assert(x <= f && f <= y);

    return f;
}

// returns a frame between x and y for frame f
inline int
frameLoop(int x,
          int y,
          int f)
{
    if (y <= x) {
        // empty or single-frame range: avoid a division by zero below
        return x;
    }
    if (f > y) {
        f = x + (f - x) % (y+1-x);
    } else if (f < x) {
        f = y - (y - f) % (y+1-x);
    }
    assert(x <= f && f <= y);

    return f;
}

// Resolve the Before/After behaviour for time in the frame range [first, last].
// Returns the behaviour that applies (eBeforeAfterOriginal within the frame range)
// and sets *srcTime to the source frame to use (meaningless for eBeforeAfterBlack).
// Loop and Bounce work on whole frames (time is truncated to an int, as FrameRange always did).
inline BeforeAfterEnum
getBeforeAfterTime(int first,
                   int last,
                   BeforeAfterEnum before,
                   BeforeAfterEnum after,
                   double time,
                   double *srcTime)
{
    BeforeAfterEnum mode = eBeforeAfterOriginal;

    if (time < first) {
        mode = before;
    } else if (time > last) {
        mode = after;
    }
    switch (mode) {
    case eBeforeAfterHold:
        *srcTime = (time < first) ? first : last;
        break;
    case eBeforeAfterLoop:
        *srcTime = frameLoop(first, last, (int)time);
        break;
    case eBeforeAfterBounce:
        *srcTime = frameBounce(first, last, (int)time);
        break;
    case eBeforeAfterOriginal:
    case eBeforeAfterBlack:
    default:
        *srcTime = time;
        break;
    }

    return mode;
}

// Define a non-animated Before or After choice param.
// It is a clip preferences slave param because it may change kOfxImageEffectFrameVarying.
inline void
defineBeforeAfterParam(ImageEffectDescriptor &desc,
                       PageParamDescriptor *page,
                       const std::string &name,
                       const std::string &label,
                       const std::string &hint,
                       BeforeAfterEnum defaultValue,
                       bool legacyBounceID = false)
{
    ChoiceParamDescriptor *param = desc.defineChoiceParam(name);

    param->setLabel(label);
    param->setHint(hint);
    assert(param->getNOptions() == (int)eBeforeAfterOriginal);
    param->appendOption(kParamBeforeAfterOptionOriginal);
    assert(param->getNOptions() == (int)eBeforeAfterHold);
    param->appendOption(kParamBeforeAfterOptionHold);
    assert(param->getNOptions() == (int)eBeforeAfterBlack);
    param->appendOption(kParamBeforeAfterOptionBlack);
    assert(param->getNOptions() == (int)eBeforeAfterLoop);
    param->appendOption(kParamBeforeAfterOptionLoop);
    assert(param->getNOptions() == (int)eBeforeAfterBounce);
    if (legacyBounceID) {
        param->appendOption(kParamBeforeAfterOptionBounceLegacy);
    } else {
        param->appendOption(kParamBeforeAfterOptionBounce);
    }
    param->setDefault( (int)defaultValue );
    param->setAnimates(false);
    desc.addClipPreferencesSlaveParam(*param);
    if (page) {
        page->addChild(*param);
    }
}

} // namespace FrameRangeBeforeAfter
} // namespace OFX

#endif // openfx_misc_FrameRangeBeforeAfter_h
