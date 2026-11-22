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
 * Scope guards for the analysis loops of TrackerPM and ImageStatistics (issue #237): the
 * "in analysis" property and the progress bar are restored when the loop ends, is cancelled, or
 * throws (C++ Core Guidelines R.1, E.6).
 */

#ifndef openfx_misc_ofxsAnalysisScope_h
#define openfx_misc_ofxsAnalysisScope_h

#include <string>

#include "ofxsImageEffect.h"

namespace OFX {

// Sets kOfxImageEffectPropInAnalysis to 1 on the effect, and back to 0 on exit. The property was
// removed from OFX 1.4 (ofxOld.h): hosts that don't have it ignore it (propSetInt does not throw).
class InAnalysisScope
{
public:
    explicit InAnalysisScope(ImageEffect& effect)
        : _effect(effect)
    {
        set(1);
    }

    ~InAnalysisScope()
    {
        set(0);
    }

    InAnalysisScope(const InAnalysisScope&) = delete;
    InAnalysisScope& operator=(const InAnalysisScope&) = delete;

private:
    void set(int value)
    {
#ifdef kOfxImageEffectPropInAnalysis // removed from OFX 1.4
        // Although the property has been there since OFX 1.0, it is not in the HostSupport library:
        // don't throw if the host doesn't have it.
        _effect.getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, value, false);
#else
        (void)value;
#endif
    }

    ImageEffect& _effect;
};

// Starts a progress bar, and ends it on exit, including when the analysis is cancelled or throws.
// progressEnd() is a host call that must not throw out of the destructor (C.36, E.16).
class ProgressScope
{
public:
    ProgressScope(ImageEffect& effect,
                  const std::string& message)
        : _effect(effect)
    {
        _effect.progressStart(message);
    }

    ~ProgressScope()
    {
        try {
            _effect.progressEnd();
        } catch (...) {
            // nothing sensible to do: the analysis is over either way
        }
    }

    ProgressScope(const ProgressScope&) = delete;
    ProgressScope& operator=(const ProgressScope&) = delete;

private:
    ImageEffect& _effect;
};

} // namespace OFX

#endif // openfx_misc_ofxsAnalysisScope_h
