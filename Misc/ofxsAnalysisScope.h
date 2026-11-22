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
 * Scope guards for the analyses of TrackerPM, ImageStatistics, HSVTool, ColorLookup and
 * DenoiseSharpen (issue #237): the "in analysis" property and the progress bar are restored when
 * the analysis ends, is cancelled, or throws (C++ Core Guidelines R.1, E.6).
 */

#ifndef openfx_misc_ofxsAnalysisScope_h
#define openfx_misc_ofxsAnalysisScope_h

#include <string>

#include "ofxOld.h" // kOfxImageEffectPropInAnalysis
#include "ofxsImageEffect.h"

namespace OFX {

// Sets kOfxImageEffectPropInAnalysis to 1 on the effect, and to 0 on exit. Scopes must not nest:
// the inner one would end the outer analysis. The property is in ofxOld.h (it was removed from
// OFX 1.4, and HostSupport doesn't have it), which the Support headers include: it is set with
// throwOnFailure=false, so hosts that don't have it ignore it.
class InAnalysisScope
{
public:
    explicit InAnalysisScope(ImageEffect& effect)
        : _effect(effect)
    {
        _effect.getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 1, false);
    }

    // a destructor must not throw (C.36, E.16)
    ~InAnalysisScope()
    {
        try {
            _effect.getPropertySet().propSetInt(kOfxImageEffectPropInAnalysis, 0, false);
        } catch (...) {
            // nothing sensible to do: the analysis is over either way
        }
    }

    InAnalysisScope(const InAnalysisScope&) = delete;
    InAnalysisScope& operator=(const InAnalysisScope&) = delete;

private:
    ImageEffect& _effect;
};

// Starts a progress bar, and ends it on exit, including when the analysis is cancelled or throws.
class ProgressScope
{
public:
    ProgressScope(ImageEffect& effect,
                  const std::string& message)
        : _effect(effect)
    {
        _effect.progressStart(message);
    }

    // a destructor must not throw (C.36, E.16)
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
