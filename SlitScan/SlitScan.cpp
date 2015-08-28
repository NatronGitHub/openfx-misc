/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2015 INRIA
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
 * OFX SlitScan plugin.
 */

#include "SlitScan.h"

#include <cmath> // for floor
#include <climits> // for INT_MAX
#include <cassert>

#include "ofxsImageEffect.h"
#include "ofxsMultiThread.h"

#include "ofxsPixelProcessor.h"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

#define kPluginName "SlitScan"
#define kPluginGrouping "Time"
#define kPluginDescription \
"Apply per-pixel retiming: the time is computed for each pixel from the retime map (by default, it is a vertical ramp, to get the SlitScan effect, originally by Douglas Trumbull)."

#define kPluginIdentifier "net.sf.openfx.SlitScan"
// History:
// version 1.0: initial version
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

void getSlitScanPluginID(OFX::PluginFactoryArray &ids)
{
#pragma message WARN("TODO")
    //static SlitScanPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    //ids.push_back(&p);
}

