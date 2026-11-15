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
 * OFX ZDefocusCImg plugin.
 *
 * Depth-dependent defocus: the image is split into focus slices according
 * to a depth (or disparity) map, each slice is blurred with its own blur size,
 * and the blurred slices are merged "over" each other from far to near so that
 * occlusions are handled correctly.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>
#include <vector>

#include "ofxsImageEffect.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#include "CImgFilter.h" // CImg.h, with the abort mechanism used by all CImg plugins

using namespace OFX;
using namespace cimg_library;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ZDefocusCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Depth-dependent defocus (depth of field) effect.\n" \
"The Source image is split into focus slices according to the Depth input (which may contain either a depth map or a disparity map). " \
"Each slice is blurred with a blur size that depends on its distance to the focal plane, and the blurred slices are merged " \
"(using the 'over' operator) from the farthest to the nearest slice, so that blurred foreground objects correctly occlude the background. " \
"The result is normalized by the merged slice coverage, so that the parts of the background hidden by in-focus foreground objects do not produce dark halos.\n" \
"The blur size of a pixel is 'Size' times its normalized defocus, where the normalized defocus is |focus/depth - 1| " \
"for a depth map (or |disparity/focus - 1| for a disparity map), so that 'Size' is the blur size of objects at infinity. " \
"It is then clamped to 'Maximum Size'. Pixels within the depth of focus are not blurred.\n" \
"The Source image is processed as premultiplied.\n" \
"In draft render mode or when rendering at a reduced scale (proxy), a Gaussian aperture is always used.\n" \
"Uses the 'vanvliet' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgZDefocus"
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
#if cimg_use_openmp!=0
#define kHostFrameThreading false
#else
#define kHostFrameThreading true
#endif

#define kClipDepth "Depth"
#define kClipDepthHint "Depth or disparity map. The channel used is given by the 'Depth Channel' parameter. If not connected, the effect does nothing."

#define kParamDepthChannel "depthChannel"
#define kParamDepthChannelLabel "Depth Channel"
#define kParamDepthChannelHint "Channel of the Depth input that contains the depth or disparity. If the Depth input has a single channel, it is always used. If it has two channels, only R and G can be used."
#define kParamDepthChannelOptionR "R", "Red channel.", "r"
#define kParamDepthChannelOptionG "G", "Green channel.", "g"
#define kParamDepthChannelOptionB "B", "Blue channel.", "b"
#define kParamDepthChannelOptionA "A", "Alpha channel.", "a"
#define kParamDepthChannelOptionLuminance "Luminance", "Rec.709 luminance of the RGB channels.", "luminance"
enum DepthChannelEnum
{
    eDepthChannelR = 0,
    eDepthChannelG,
    eDepthChannelB,
    eDepthChannelA,
    eDepthChannelLuminance,
};

#define kParamDepthType "depthType"
#define kParamDepthTypeLabel "Depth Type"
#define kParamDepthTypeHint "Whether the Depth input contains a depth map (larger values are farther) or a disparity map (proportional to the inverse of depth: larger values are nearer)."
#define kParamDepthTypeOptionDepth "Depth", "The input is a depth map. Values less than or equal to zero are considered to be at infinity.", "depth"
#define kParamDepthTypeOptionDisparity "Disparity", "The input is a disparity map (inverse depth, up to a scale factor). A zero disparity is at infinity.", "disparity"
enum DepthTypeEnum
{
    eDepthTypeDepth = 0,
    eDepthTypeDisparity,
};

#define kParamUseFocusPoint "useFocusPoint"
#define kParamUseFocusPointLabel "Use Focus Point"
#define kParamUseFocusPointHint "If checked, the focus distance is the value of the Depth input at the Focus Point. Else, the Focus value is used."

#define kParamFocusPoint "focusPoint"
#define kParamFocusPointLabel "Focus Point"
#define kParamFocusPointHint "Point in the image where the focus is set. The depth (or disparity) is read from the Depth input at this point, at render time."

#define kParamFocus "focus"
#define kParamFocusLabel "Focus"
#define kParamFocusHint "Focus distance (or focus disparity), in the same units as the Depth input. Used if 'Use Focus Point' is not checked, or if the depth at the focus point is not available."

#define kParamDepthOfFocus "depthOfFocus"
#define kParamDepthOfFocusLabel "Depth of Focus"
#define kParamDepthOfFocusHint "Width of the region around the focus distance that is in focus (not blurred), in the same units as the Depth input."

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Blur size (in pixels at full resolution) for a normalized defocus of 1, i.e. for objects at infinity. The blur size is proportional to the normalized defocus."

#define kParamMaxSize "maxSize"
#define kParamMaxSizeLabel "Maximum Size"
#define kParamMaxSizeHint "Maximum blur size (in pixels at full resolution). The focus slices are spread evenly between 0 and this size. Larger values are slower to render, because more of the input around each rendered tile is needed."

#define kParamSlices "slices"
#define kParamSlicesLabel "Focus Slices"
#define kParamSlicesHint "Number of focus slices on each side of the focal plane. The blur size is quantized in steps of 'Maximum Size'/'Focus Slices'. More slices give a smoother result but are slower to render."

#define kParamAperture "aperture"
#define kParamApertureLabel "Aperture"
#define kParamApertureHint "Shape of the aperture (the blur kernel). In draft render mode or when rendering at a reduced scale, Gaussian is always used."
#define kParamApertureOptionGaussian "Gaussian", "Gaussian aperture, computed using a fast recursive filter. The blur size is the same as in the Blur plugin.", "gaussian"
#define kParamApertureOptionDisc "Disc", "Disc (circular) aperture, giving a bokeh effect. The blur size is the disc diameter. Slower than Gaussian.", "disc"
enum ApertureEnum
{
    eApertureGaussian = 0,
    eApertureDisc,
};

// Rec.709 luminance coefficients
#define kLumR 0.2126f
#define kLumG 0.7152f
#define kLumB 0.0722f

// ratio between the size and the Gaussian standard deviation (same as CImgBlur)
#define kSizeToSigma (1. / 2.4)
// extra margin for the Gaussian, relative to size (same as CImgBlur)
#define kGaussianMargin 1.5

// Number of extra input pixels needed on each side of the render window to blur by
// sizePixels. render() and getRegionsOfInterest() must use the same value.
// The disc only needs sizePixels/2, so the Gaussian margin covers both apertures.
static inline int
blurPadding(double sizePixels)
{
    return (int)std::ceil(sizePixels * kGaussianMargin) + 3;
}

// Pixel aspect ratio of a clip, falling back to 1 if the host returns an invalid value.
static inline double
clipPAR(const Clip* clip)
{
    const double par = clip ? clip->getPixelAspectRatio() : 1.;

    return (par > 0.) ? par : 1.;
}


// Read the depth value from a (non-NULL) pixel with nComps components.
// Channels that the pixel does not have read as 0 (or 1 for a missing alpha).
static inline float
getDepthValue(const float* p,
              int nComps,
              DepthChannelEnum channel)
{
    switch (nComps) {
    case 1:

        return p[0];
    case 2:

        return (channel == eDepthChannelR) ? p[0] : ( (channel == eDepthChannelG) ? p[1] : 0.f );
    case 3:
    case 4:
        switch (channel) {
        case eDepthChannelR:

            return p[0];
        case eDepthChannelG:

            return p[1];
        case eDepthChannelB:

            return p[2];
        case eDepthChannelA:

            return (nComps == 4) ? p[3] : 1.f;
        case eDepthChannelLuminance:

            return kLumR * p[0] + kLumG * p[1] + kLumB * p[2];
        }
        break;
    default:
        break;
    }

    return 0.f;
}

// Convert a depth or disparity value to a signed "normalized defocus":
// 0 at the focal plane, -1 at infinity, positive for objects nearer than the focal plane.
// focus must be > 0 for depth maps.
static inline double
normalizedDefocus(double v,
                  double focus,
                  DepthTypeEnum depthType)
{
    if (depthType == eDepthTypeDepth) {
        if (v <= 0.) {
            return -1.; // infinity
        }

        return focus / v - 1.;
    }
    // disparity
    if (focus > 0.) {
        return v / focus - 1.;
    }

    return v - focus; // focus at infinity (or beyond): no normalization possible
}

// In-place disc blur of all channels of a planar image, with elliptic radii rx, ry (in pixels).
// Each disc row is a box filter, computed using row prefix sums: O(width*height*(2*ry+1)) per channel.
// Pixels outside of the image are ignored (normalized convolution).
static void
discBlur(CImg<cimgpix_t>& img,
         double rx,
         double ry,
         ImageEffect* effect)
{
    const int W = img.width();
    const int H = img.height();
    const int C = img.spectrum();
    if ( (W <= 0) || (H <= 0) || ( (rx < 0.5) && (ry < 0.5) ) ) {
        return;
    }
    const int Ry = (int)std::floor(ry);
    std::vector<int> hw(2 * Ry + 1); // half-width of each kernel row
    for (int dy = -Ry; dy <= Ry; ++dy) {
        double t = (ry > 0.) ? (dy / ry) : 0.;
        double s = 1. - t * t;
        hw[dy + Ry] = (int)std::floor( rx * std::sqrt( (std::max)(0., s) ) + 0.5 );
    }
    // normalization: number of kernel pixels inside the image
    CImg<float> norm(W, H, 1, 1, 0.f);
    cimg_forXY(norm, x, y) {
        int n = 0;
        for (int dy = -Ry; dy <= Ry; ++dy) {
            int yy = y + dy;
            if ( (yy < 0) || (yy >= H) ) {
                continue;
            }
            int w = hw[dy + Ry];
            n += (std::min)(x + w, W - 1) - (std::max)(x - w, 0) + 1;
        }
        norm(x, y) = n > 0 ? 1.f / n : 0.f;
    }
    CImg<double> prefix(W + 1, H);
    CImg<cimgpix_t> res(W, H);
    for (int c = 0; c < C; ++c) {
        if ( effect->abort() ) {
            return;
        }
        cimg_forY(img, y) {
            double sum = 0.;
            prefix(0, y) = 0.;
            for (int x = 0; x < W; ++x) {
                sum += img(x, y, 0, c);
                prefix(x + 1, y) = sum;
            }
        }
        cimg_forXY(res, x, y) {
            double sum = 0.;
            for (int dy = -Ry; dy <= Ry; ++dy) {
                int yy = y + dy;
                if ( (yy < 0) || (yy >= H) ) {
                    continue;
                }
                int w = hw[dy + Ry];
                sum += prefix( (std::min)(x + w, W - 1) + 1, yy ) - prefix( (std::max)(x - w, 0), yy );
            }
            res(x, y) = (cimgpix_t)(sum * norm(x, y));
        }
        cimg_forXY(res, x, y) {
            img(x, y, 0, c) = res(x, y);
        }
    }
} // discBlur

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ZDefocusPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ZDefocusPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _depthClip(NULL)
        , _depthChannel(NULL)
        , _depthType(NULL)
        , _useFocusPoint(NULL)
        , _focusPoint(NULL)
        , _focus(NULL)
        , _depthOfFocus(NULL)
        , _size(NULL)
        , _maxSize(NULL)
        , _slices(NULL)
        , _aperture(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha) );
        _depthClip = fetchClip(kClipDepth);
        assert(_depthClip);

        _depthChannel = fetchChoiceParam(kParamDepthChannel);
        _depthType = fetchChoiceParam(kParamDepthType);
        _useFocusPoint = fetchBooleanParam(kParamUseFocusPoint);
        _focusPoint = fetchDouble2DParam(kParamFocusPoint);
        _focus = fetchDoubleParam(kParamFocus);
        _depthOfFocus = fetchDoubleParam(kParamDepthOfFocus);
        _size = fetchDoubleParam(kParamSize);
        _maxSize = fetchDoubleParam(kParamMaxSize);
        _slices = fetchIntParam(kParamSlices);
        _aperture = fetchChoiceParam(kParamAperture);
        assert(_depthChannel && _depthType && _useFocusPoint && _focusPoint && _focus && _depthOfFocus &&
               _size && _maxSize && _slices && _aperture);

        updateGUI();
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateGUI();

    // get the focus value at the given time, reading the depth at the focus point if necessary
    double getFocusValue(double time, const OfxPointD& renderScale, const Image* depth, DepthChannelEnum depthChannel);

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_depthClip;
    ChoiceParam* _depthChannel;
    ChoiceParam* _depthType;
    BooleanParam* _useFocusPoint;
    Double2DParam* _focusPoint;
    DoubleParam* _focus;
    DoubleParam* _depthOfFocus;
    DoubleParam* _size;
    DoubleParam* _maxSize;
    IntParam* _slices;
    ChoiceParam* _aperture;
};


////////////////////////////////////////////////////////////////////////////////
double
ZDefocusPlugin::getFocusValue(double time,
                              const OfxPointD& renderScale,
                              const Image* depth,
                              DepthChannelEnum depthChannel)
{
    double focus = _focus->getValueAtTime(time);

    if ( !_useFocusPoint->getValueAtTime(time) ) {
        return focus;
    }
    OfxPointD p = _focusPoint->getValueAtTime(time);
    OfxPointI pix;
    Coords::toPixel(p, renderScale, clipPAR(_depthClip), &pix);
    if (depth) {
        const float* pixel = (const float*)depth->getPixelAddress(pix.x, pix.y);
        if (pixel) {
            return getDepthValue(pixel, depth->getPixelComponentCount(), depthChannel);
        }
    }
    // depth not available at the focus point (e.g. outside of the Depth RoD): use the focus value
    return focus;
}

void
ZDefocusPlugin::render(const RenderArguments &args)
{
    const double time = args.time;
    const OfxRectI& renderWindow = args.renderWindow;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    const BitDepthEnum dstBitDepth = dst->getPixelDepth();
    const int nComps = dst->getPixelComponentCount();
    if (dstBitDepth != eBitDepthFloat) {
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }

    auto_ptr<const Image> src( _srcClip->isConnected() ?
                               _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getPixelDepth() != dstBitDepth) || (src->getPixelComponentCount() != nComps) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        checkBadRenderScaleOrField(src, args);
    }
    auto_ptr<const Image> depth( ( _depthClip && _depthClip->isConnected() ) ?
                                 _depthClip->fetchImage(time) : 0 );
    if ( depth.get() && (depth->getPixelDepth() != eBitDepthFloat) ) {
        throwSuiteStatusException(kOfxStatErrImageFormat);
    }

    // fill the render window with black first
    for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
        float* d = (float*)dst->getPixelAddress(renderWindow.x1, y);
        if (!d) {
            continue;
        }
        std::fill(d, d + (size_t)(renderWindow.x2 - renderWindow.x1) * nComps, 0.f);
    }
    if ( !src.get() ) {
        return;
    }

    // get the parameters
    const DepthChannelEnum depthChannel = (DepthChannelEnum)_depthChannel->getValueAtTime(time);
    const DepthTypeEnum depthType = (DepthTypeEnum)_depthType->getValueAtTime(time);
    const double depthOfFocus = (std::max)(0., _depthOfFocus->getValueAtTime(time));
    const double size = _size->getValueAtTime(time);
    const double maxSize = _maxSize->getValueAtTime(time);
    const int slices = (std::max)(1, _slices->getValueAtTime(time));
    ApertureEnum aperture = (ApertureEnum)_aperture->getValueAtTime(time);
    // in draft mode or proxy mode, always use the fast Gaussian blur
    if ( args.renderQualityDraft || (args.renderScale.x < 1.) || (args.renderScale.y < 1.) ) {
        aperture = eApertureGaussian;
    }

    double focus = getFocusValue(time, args.renderScale, depth.get(), depthChannel);
    if ( (depthType == eDepthTypeDepth) && !(focus > 0.) ) {
        focus = 1e-6; // focus on the nearest possible distance
    }
    // bounds of the in-focus region, in normalized defocus
    double focusLo, focusHi;
    {
        double vNear, vFar; // values of the near and far limit of the depth of focus
        if (depthType == eDepthTypeDepth) {
            vNear = focus - depthOfFocus / 2;
            vFar = focus + depthOfFocus / 2;
            focusHi = (vNear > 0.) ? normalizedDefocus(vNear, focus, depthType) : DBL_MAX;
        } else {
            vNear = focus + depthOfFocus / 2;
            vFar = focus - depthOfFocus / 2;
            focusHi = normalizedDefocus(vNear, focus, depthType);
        }
        focusLo = normalizedDefocus(vFar, focus, depthType);
    }

    // compute the region to process: the render window, padded by the maximum blur size
    const int padX = blurPadding(maxSize * args.renderScale.x);
    const int padY = blurPadding(maxSize * args.renderScale.y);
    OfxRectI procRect = { renderWindow.x1 - padX, renderWindow.y1 - padY, renderWindow.x2 + padX, renderWindow.y2 + padY };
    OfxRectI srcBounds = src->getBounds();
    if ( !Coords::rectIntersection(procRect, srcBounds, &procRect) ) {
        return;
    }
    const int W = procRect.x2 - procRect.x1;
    const int H = procRect.y2 - procRect.y1;
    const bool unpremultiplied = ( nComps == 4 && src->getPreMultiplication() == eImageUnPreMultiplied );

    // load the source image (premultiplied) and compute the slice index of each pixel
    CImg<cimgpix_t> img(W, H, 1, nComps, 0.f);
    CImg<int> sliceIndex(W, H, 1, 1, 0);
    const int depthComps = depth.get() ? depth->getPixelComponentCount() : 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const float* s = (const float*)src->getPixelAddress(procRect.x1 + x, procRect.y1 + y);
            if (s) {
                const float a = unpremultiplied ? s[3] : 1.f;
                for (int c = 0; c < nComps; ++c) {
                    img(x, y, 0, c) = (unpremultiplied && c < 3) ? s[c] * a : s[c];
                }
            }
            const float* dp = depth.get() ? (const float*)depth->getPixelAddress(procRect.x1 + x, procRect.y1 + y) : 0;
            // no depth: consider that the pixel is at infinity (zero depth or disparity)
            const double u = normalizedDefocus(dp ? getDepthValue(dp, depthComps, depthChannel) : 0., focus, depthType);
            const double defocus = (u < focusLo) ? (u - focusLo) : ( (u > focusHi) ? (u - focusHi) : 0. );
            double blurSize = (std::max)( -maxSize, (std::min)(maxSize, size * defocus) );
            int k = (maxSize > 0.) ? (int)std::floor(blurSize * slices / maxSize + 0.5) : 0;
            sliceIndex(x, y) = (std::max)( -slices, (std::min)(slices, k) );
        }
    }

    // count the pixels in each slice
    std::vector<int> sliceCount(2 * slices + 1, 0);
    cimg_forXY(sliceIndex, x, y) {
        ++sliceCount[sliceIndex(x, y) + slices];
    }

    // Register this effect so that CImg's abort test (cimg_abort_test, see CImgFilter.h)
    // can stop the long vanvliet() calls when the host aborts the render.
    // This plugin has two inputs and its own render loop, so it cannot use CImgFilterPluginHelper,
    // which does the same thing around its render() call.
#if defined(HAVE_THREAD_LOCAL) || defined(HAVE_PTHREAD)
#  if defined(HAVE_THREAD_LOCAL)
    tls::gImageEffect = this;
#  else
    OFX::ImageEffect **_ptr = (OFX::ImageEffect **)pthread_getspecific(tls::gImageEffect_key);
    assert (NULL != _ptr);
    *_ptr = this;
#  endif
#endif
    // blur each slice and merge it over the result, from far (most negative) to near.
    // The last channel of result accumulates the slice masks (merged over), and is used to
    // normalize the result: this fills the holes left in the far slices by the near slices,
    // which would otherwise produce dark halos around in-focus foreground objects.
    CImg<cimgpix_t> result(W, H, 1, nComps + 1, 0.f);
    CImg<cimgpix_t> layer;
    bool aborted = false;
    try {
        for (int k = -slices; k <= slices && !aborted; ++k) {
            if (sliceCount[k + slices] == 0) {
                continue;
            }
            if ( abort() ) {
                aborted = true;
                break;
            }
            // premultiplied slice image, with the slice mask as last channel
            layer.assign(W, H, 1, nComps + 1, 0.f);
            cimg_forXY(layer, x, y) {
                if (sliceIndex(x, y) == k) {
                    for (int c = 0; c < nComps; ++c) {
                        layer(x, y, 0, c) = img(x, y, 0, c);
                    }
                    layer(x, y, 0, nComps) = 1.f;
                }
            }
            const double sliceSize = std::abs(k) * maxSize / slices;
            const double sx = sliceSize * args.renderScale.x;
            const double sy = sliceSize * args.renderScale.y;
            if (aperture == eApertureDisc) {
                discBlur(layer, sx / 2, sy / 2, this);
            } else {
                const float sigmax = (float)(sx * kSizeToSigma);
                const float sigmay = (float)(sy * kSizeToSigma);
                if (sigmax >= 0.1) {
                    layer.vanvliet(sigmax, 0, 'x', true);
                }
                if ( abort() ) {
                    aborted = true;
                    break;
                }
                if (sigmay >= 0.1) {
                    layer.vanvliet(sigmay, 0, 'y', true);
                }
            }
            if ( abort() ) {
                aborted = true;
                break;
            }
            // merge over
            cimg_forXY(result, x, y) {
                const float a = (std::max)( 0.f, (std::min)(1.f, layer(x, y, 0, nComps)) );
                for (int c = 0; c <= nComps; ++c) {
                    result(x, y, 0, c) = layer(x, y, 0, c) + (1.f - a) * result(x, y, 0, c);
                }
            }
        }
    } catch (CImgAbortException&) {
        aborted = true;
    }
#if defined(HAVE_THREAD_LOCAL)
    tls::gImageEffect = 0;
#elif defined(HAVE_PTHREAD)
    *_ptr = 0;
#endif
    if (aborted) {
        return;
    }

    // write the result to the render window
    OfxRectI outRect;
    if ( !Coords::rectIntersection(renderWindow, procRect, &outRect) ) {
        return;
    }
    for (int y = outRect.y1; y < outRect.y2; ++y) {
        float* d = (float*)dst->getPixelAddress(outRect.x1, y);
        if (!d) {
            continue;
        }
        for (int x = outRect.x1; x < outRect.x2; ++x, d += nComps) {
            const int ix = x - procRect.x1;
            const int iy = y - procRect.y1;
            const float coverage = result(ix, iy, 0, nComps);
            const float norm = (coverage > 1e-6f) ? (1.f / coverage) : 0.f;
            for (int c = 0; c < nComps; ++c) {
                d[c] = result(ix, iy, 0, c) * norm;
            }
            if (unpremultiplied) {
                const float a = d[3];
                if (a > 0.f) {
                    d[0] /= a;
                    d[1] /= a;
                    d[2] /= a;
                }
            }
        }
    }
} // ZDefocusPlugin::render

bool
ZDefocusPlugin::isIdentity(const IsIdentityArguments &args,
                           Clip * &identityClip,
                           double & /*identityTime*/,
                           int& /*view*/,
                           std::string& /*plane*/)
{
    const double time = args.time;

    if ( !_depthClip->isConnected() ||
         (_size->getValueAtTime(time) <= 0.) ||
         (_maxSize->getValueAtTime(time) <= 0.) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

bool
ZDefocusPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                      OfxRectD &rod)
{
    if ( !_srcClip->isConnected() ) {
        return false;
    }
    // the RoD is the source RoD
    rod = _srcClip->getRegionOfDefinition(args.time);

    return true;
}

void
ZDefocusPlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                     RegionOfInterestSetter &rois)
{
    const double time = args.time;
    const double maxSize = (std::max)(0., _maxSize->getValueAtTime(time));
    // same padding as in render(), converted from pixels to canonical coordinates
    const double padX = blurPadding(maxSize * args.renderScale.x) * clipPAR(_srcClip) / args.renderScale.x;
    const double padY = blurPadding(maxSize * args.renderScale.y) / args.renderScale.y;
    OfxRectD roi = args.regionOfInterest;
    roi.x1 -= padX;
    roi.x2 += padX;
    roi.y1 -= padY;
    roi.y2 += padY;
    rois.setRegionOfInterest(*_srcClip, roi);
    // the Depth RoI also contains the focus point pixel, which is read at render time
    OfxRectD depthRoI = roi;
    if ( _useFocusPoint->getValueAtTime(time) ) {
        const double depthPar = clipPAR(_depthClip);
        OfxPointI pix;
        Coords::toPixel(_focusPoint->getValueAtTime(time), args.renderScale, depthPar, &pix);
        OfxRectI pixRect = {pix.x, pix.y, pix.x + 1, pix.y + 1};
        OfxRectD pixRectD;
        Coords::toCanonical(pixRect, args.renderScale, depthPar, &pixRectD);
        Coords::rectBoundingBox(depthRoI, pixRectD, &depthRoI);
    }
    rois.setRegionOfInterest(*_depthClip, depthRoI);
}

void
ZDefocusPlugin::updateGUI()
{
    const bool useFocusPoint = _useFocusPoint->getValue();

    _focusPoint->setEnabled(useFocusPoint);
    _focus->setEnabled(!useFocusPoint);
}

void
ZDefocusPlugin::changedParam(const InstanceChangedArgs & /*args*/,
                             const std::string &paramName)
{
    if (paramName == kParamUseFocusPoint) {
        updateGUI();
    }
}

mDeclarePluginFactory(ZDefocusPluginFactory, {ofxsThreadSuiteCheck();}, {});

void
ZDefocusPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(kHostFrameThreading);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
    desc.setSupportsRenderQuality(true);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
ZDefocusPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                         ContextEnum /*context*/)
{
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    ClipDescriptor *depthClip = desc.defineClip(kClipDepth);
    depthClip->setHint(kClipDepthHint);
    depthClip->addSupportedComponent(ePixelComponentRGBA);
    depthClip->addSupportedComponent(ePixelComponentRGB);
    depthClip->addSupportedComponent(ePixelComponentAlpha);
#ifdef OFX_EXTENSIONS_NATRON
    depthClip->addSupportedComponent(ePixelComponentXY);
#endif
    depthClip->setTemporalClipAccess(false);
    depthClip->setOptional(true);
    depthClip->setSupportsTiles(kSupportsTiles);
    depthClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDepthChannel);
        param->setLabel(kParamDepthChannelLabel);
        param->setHint(kParamDepthChannelHint);
        assert(param->getNOptions() == eDepthChannelR);
        param->appendOption(kParamDepthChannelOptionR);
        assert(param->getNOptions() == eDepthChannelG);
        param->appendOption(kParamDepthChannelOptionG);
        assert(param->getNOptions() == eDepthChannelB);
        param->appendOption(kParamDepthChannelOptionB);
        assert(param->getNOptions() == eDepthChannelA);
        param->appendOption(kParamDepthChannelOptionA);
        assert(param->getNOptions() == eDepthChannelLuminance);
        param->appendOption(kParamDepthChannelOptionLuminance);
        param->setDefault( (int)eDepthChannelR );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamDepthType);
        param->setLabel(kParamDepthTypeLabel);
        param->setHint(kParamDepthTypeHint);
        assert(param->getNOptions() == eDepthTypeDepth);
        param->appendOption(kParamDepthTypeOptionDepth);
        assert(param->getNOptions() == eDepthTypeDisparity);
        param->appendOption(kParamDepthTypeOptionDisparity);
        param->setDefault( (int)eDepthTypeDepth );
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamUseFocusPoint);
        param->setLabel(kParamUseFocusPointLabel);
        param->setHint(kParamUseFocusPointHint);
        param->setDefault(true);
        param->setAnimates(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamFocusPoint);
        param->setLabel(kParamFocusPointLabel);
        param->setHint(kParamFocusPointHint);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        param->setDefaultCoordinateSystem(eCoordinatesNormalised);
        param->setDefault(0.5, 0.5);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(-10000, -10000, 10000, 10000); // Resolve requires display range or values are clamped to (-1,1)
        if ( param->getHostHasNativeOverlayHandle() ) {
            param->setUseHostNativeOverlayHandle(true);
        }
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamFocus);
        param->setLabel(kParamFocusLabel);
        param->setHint(kParamFocusHint);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(0., 100.);
        param->setDefault(1.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamDepthOfFocus);
        param->setLabel(kParamDepthOfFocusLabel);
        param->setHint(kParamDepthOfFocusHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 10.);
        param->setDefault(0.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSize);
        param->setLabel(kParamSizeLabel);
        param->setHint(kParamSizeHint);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 100.);
        param->setDefault(20.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamMaxSize);
        param->setLabel(kParamMaxSizeLabel);
        param->setHint(kParamMaxSizeHint);
        param->setRange(0., 1000.);
        param->setDisplayRange(0., 100.);
        param->setDefault(40.);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamSlices);
        param->setLabel(kParamSlicesLabel);
        param->setHint(kParamSlicesHint);
        param->setRange(1, 100);
        param->setDisplayRange(1, 30);
        param->setDefault(10);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamAperture);
        param->setLabel(kParamApertureLabel);
        param->setHint(kParamApertureHint);
        assert(param->getNOptions() == eApertureGaussian);
        param->appendOption(kParamApertureOptionGaussian);
        assert(param->getNOptions() == eApertureDisc);
        param->appendOption(kParamApertureOptionDisc);
        param->setDefault( (int)eApertureGaussian );
        if (page) {
            page->addChild(*param);
        }
    }
} // ZDefocusPluginFactory::describeInContext

ImageEffect*
ZDefocusPluginFactory::createInstance(OfxImageEffectHandle handle,
                                      ContextEnum /*context*/)
{
    return new ZDefocusPlugin(handle);
}

static ZDefocusPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
