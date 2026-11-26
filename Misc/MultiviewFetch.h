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
 * The view image fetch of the multiview plug-ins (Anaglyph, JoinViews, MixViews, OneView, SideBySide).
 */

#ifndef openfx_misc_MultiviewFetch_h
#define openfx_misc_MultiviewFetch_h

#include "ofxsImageEffect.h"

namespace OFX {

/// The image of a view of clip at time. OFX 1.4 hosts have neither the multiview plane suite
/// (kFnOfxImageEffectPlaneSuite v2) nor the Vegas stereoscopic suite: they have a single view,
/// which is then every view.
inline Image*
fetchViewImage(Clip* clip,
               double time,
               int view)
{
    if ( fetchSuite(kFnOfxImageEffectPlaneSuite, 2, true) ||
         fetchSuite(kOfxVegasStereoscopicImageEffectSuite, 1, true) ) {
        return clip->fetchStereoscopicImage(time, view);
    }

    return clip->fetchImage(time);
}
} // namespace OFX

#endif // openfx_misc_MultiviewFetch_h
