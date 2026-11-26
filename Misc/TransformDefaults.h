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
 * The center default of the plug-ins with the old Transform parameters and a Center Changed flag
 * (Transform, GodRays).
 * TODO: move to SupportExt, as an option of ofxsTransformDenormaliseDefaults().
 */

#ifndef openfx_misc_TransformDefaults_h
#define openfx_misc_TransformDefaults_h

#include "ofxsImageEffect.h"
#include "ofxsTransformInteract.h"

namespace OFX {

/// Call it last in the constructor. Hosts that ignore normalised defaults (OFX 1.1 hosts such as
/// VEGAS, and DaVinci Resolve) give the default center (0.5, 0.5) as is: move it to the project
/// centre (#66). The host may send instanceChanged for that setValue(), which changedParam() would
/// take as a center edit and stop the re-centring on a new source: keep Center Changed as it was
/// (this works only if the host sends instanceChanged synchronously, from setValue()).
inline void
transformDenormaliseDefaultsKeepCenterChanged(ImageEffect &effect,
                                              BooleanParam &centerChanged)
{
    const bool changed = centerChanged.getValue();

    ofxsTransformDenormaliseDefaults(effect, /*oldParams=*/true);
    if (centerChanged.getValue() != changed) {
        centerChanged.setValue(changed);
    }
}
} // namespace OFX

#endif // openfx_misc_TransformDefaults_h
