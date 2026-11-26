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
 * The "Clip Info..." text, shared by NoOp and the Test plug-ins.
 */

#ifndef openfx_misc_ClipInfo_h
#define openfx_misc_ClipInfo_h

#include <sstream>
#include <string>

#include "ofxsImageEffect.h"

namespace OFX {
namespace ClipInfo {

inline const char*
bitDepthString(BitDepthEnum bitDepth)
{
    switch (bitDepth) {
    case eBitDepthUByte:

        return "8u";
    case eBitDepthUShort:

        return "16u";
    case eBitDepthHalf:

        return "16f";
    case eBitDepthFloat:

        return "32f";
    case eBitDepthCustom:

        return "x";
    case eBitDepthNone:

        return "0";
#ifdef OFX_EXTENSIONS_VEGAS
    case eBitDepthUByteBGRA:

        return "8uBGRA";
    case eBitDepthUShortBGRA:

        return "16uBGRA";
    case eBitDepthFloatBGRA:

        return "32fBGRA";
#endif
    default:

        return "[unknown bit depth]";
    }
}

inline std::string
pixelComponentString(const std::string& p)
{
    const std::string prefix = "OfxImageComponent";

    std::string s = p;
    const std::string::size_type pos = s.find(prefix);

    if (pos != std::string::npos) {
        s.erase(pos, prefix.length());
    }

    return s;
}

inline const char*
premultString(PreMultiplicationEnum e)
{
    switch (e) {
    case eImageOpaque:

        return "Opaque";
    case eImagePreMultiplied:

        return "PreMultiplied";
    case eImageUnPreMultiplied:

        return "UnPreMultiplied";
    default:

        return "[unknown premult]";
    }
}

#ifdef OFX_EXTENSIONS_VEGAS
inline const char*
pixelOrderString(PixelOrderEnum e)
{
    switch (e) {
    case ePixelOrderRGBA:

        return "RGBA";
    case ePixelOrderBGRA:

        return "BGRA";
    default:

        return "[unknown pixel order]";
    }
}

#endif

inline const char*
fieldOrderString(FieldEnum e)
{
    switch (e) {
    case eFieldNone:

        return "None";
    case eFieldBoth:

        return "Both";
    case eFieldLower:

        return "Lower";
    case eFieldUpper:

        return "Upper";
    case eFieldSingle:

        return "Single";
    case eFieldDoubled:

        return "Doubled";
    default:

        return "[unknown field order]";
    }
}

// Append the properties of clip c to oss. A property or suite call that fails (e.g. a property
// that the host does not support) stops the listing of this clip and is reported in the message,
// instead of aborting the whole action (see https://github.com/NatronGitHub/openfx-misc/issues/66).
inline void
appendClipInfo(std::ostringstream& oss,
               Clip &c,
               double time)
{
    try {
        oss << pixelComponentString( c.getPixelComponentsProperty() );
        oss << bitDepthString( c.getPixelDepth() );
        oss << " (unmapped: ";
        oss << pixelComponentString( c.getUnmappedPixelComponentsProperty() );
        oss << bitDepthString( c.getUnmappedPixelDepth() );
        oss << ")\npremultiplication: ";
        oss << premultString( c.getPreMultiplication() );
#ifdef OFX_EXTENSIONS_VEGAS
        oss << "\npixel order: ";
        oss << pixelOrderString( c.getPixelOrder() );
#endif
        oss << "\nfield order: ";
        oss << fieldOrderString( c.getFieldOrder() );
        oss << "\n";
        oss << (c.isConnected() ? "connected" : "not connected");
        oss << "\n";
        oss << (c.hasContinuousSamples() ? "continuous samples" : "discontinuous samples");
#ifdef OFX_EXTENSIONS_NATRON
        oss << "\nformat: ";
        OfxRectI format;
        c.getFormat(format);
        oss << format.x2 - format.x1 << 'x' << format.y2 - format.y1;
        if ( (format.x1 != 0) || (format.y1 != 0) ) {
            if (format.x1 < 0) {
                oss << format.x1;
            } else {
                oss << '+' << format.x1;
            }
            if (format.y1 < 0) {
                oss << format.y1;
            } else {
                oss << '+' << format.y1;
            }
        }
#endif
        oss << "\npixel aspect ratio: ";
        oss << c.getPixelAspectRatio();
        oss << "\nframe rate: ";
        oss << c.getFrameRate();
        oss << " (unmapped: ";
        oss << c.getUnmappedFrameRate();
        oss << ")";
        OfxRangeD range = c.getFrameRange();
        oss << "\nframe range: ";
        oss << range.min << "..." << range.max;
        oss << " (unmapped: ";
        range = c.getUnmappedFrameRange();
        oss << range.min << "..." << range.max;
        oss << ")";
        // the values above are in the OFX time domain; the host shows time t as t + this offset
        oss << "\ndisplay time offset: ";
        oss << c.getDisplayTimeOffset();
        oss << "\nregion of definition: ";
        OfxRectD rod = c.getRegionOfDefinition(time);
        oss << rod.x1 << ' ' << rod.y1 << ' ' << rod.x2 << ' ' << rod.y2;
    } catch (const OFX::Exception::PropertyUnknownToHost& e) {
        oss << "\n[error: the host does not support the property " << e.what() << "]";
    } catch (const OFX::Exception::PropertyValueIllegalToHost& e) {
        oss << "\n[error: the value of the property " << e.what() << " is illegal to the host]";
    } catch (const OFX::Exception::Suite& e) {
        oss << "\n[error: a host function returned " << e.what() << "]";
    } catch (const std::exception& e) {
        oss << "\n[error: " << e.what() << "]";
    }
}

} // namespace ClipInfo
} // namespace OFX

#endif // openfx_misc_ClipInfo_h
