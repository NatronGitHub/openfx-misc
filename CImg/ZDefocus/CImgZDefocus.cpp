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
#include "ofxsCopier.h" // fillBlack
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

#include "CImgFilter.h" // CImg.h, with the abort mechanism used by all CImg plugins
#include "CImgFFTConvolve.h" // FFT convolution, shared with ConvolveCImg

using namespace OFX;
using namespace cimg_library;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "ZDefocusCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Depth-dependent defocus (depth of field) effect.\n" \
"Note: draft renders and renders at a reduced scale (proxy) always use the fast Gaussian aperture, whatever the 'Aperture' parameter.\n" \
"The Source image is split into focus slices according to the Depth input (which may contain either a depth map or a disparity map). " \
"Each slice is blurred with a blur size that depends on its distance to the focal plane, and the blurred slices are merged " \
"(using the 'over' operator) from the farthest to the nearest slice, so that blurred foreground objects correctly occlude the background. " \
"The result is normalized by the merged slice coverage, so that the parts of the background hidden by in-focus foreground objects do not produce dark halos.\n" \
"The blur size of a pixel is 'Size' times its normalized defocus, where the normalized defocus is |focus/depth - 1| " \
"for a depth map (or |disparity/focus - 1| for a disparity map), so that 'Size' is the blur size of objects at infinity. " \
"It is then clamped to 'Maximum Size'. Pixels within the depth of focus are not blurred.\n" \
"The Source image is processed as premultiplied.\n" \
"The aperture (the blur kernel, which gives the shape of the bokeh) may be a Gaussian, an antialiased disc, or a custom kernel given by the Filter input " \
"(for example made with the DiscBlades generator), which is scaled so that its larger dimension is the blur size. " \
"Large custom kernels are applied using the Fast Fourier Transform (FFT), which is slow for small images but much faster than a direct convolution for large blur sizes.\n" \
"Pixels outside of the Source image are ignored by all apertures (normalized convolution), so that the image edges are not darkened.\n" \
"Uses the 'vanvliet' and 'FFT' functions from the CImg library.\n" \
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

#define kClipFilter "Filter"
#define kClipFilterHint "Custom aperture (bokeh) kernel, used if 'Aperture' is 'Custom', for example made with the DiscBlades generator. The kernel is the Filter alpha (or its luminance if it has no alpha) within the bounding box of its significant pixels (above 1/1000 of the kernel maximum), and is centered on the center of this bounding box (unlike ConvolveCImg, which centers the kernel on the Filter format center by default). If not connected or empty, a disc aperture is used."

#define kParamDepthChannel "depthChannel"
#define kParamDepthChannelLabel "Depth Channel"
#define kParamDepthChannelHint "Channel of the Depth input that contains the depth or disparity. If the Depth input has a single channel, it is always used. Channels that it does not have read as 0, and the luminance of a two-channel input is its first channel."
#define kParamDepthChannelOptionR "R", "Red channel.", "r"
#define kParamDepthChannelOptionG "G", "Green channel.", "g"
#define kParamDepthChannelOptionB "B", "Blue channel.", "b"
#define kParamDepthChannelOptionA "A", "Alpha channel.", "a"
#define kParamDepthChannelOptionLuminance "Luminance", "Rec.709 luminance of the RGB channels.", "luminance"
// the same channels as CImgKernelChannelEnum (the Depth pixels are read by cimgKernelValue())
enum DepthChannelEnum
{
    eDepthChannelR = eCImgKernelR,
    eDepthChannelG = eCImgKernelG,
    eDepthChannelB = eCImgKernelB,
    eDepthChannelA = eCImgKernelA,
    eDepthChannelLuminance = eCImgKernelLuminance,
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
#define kParamApertureHint "Shape of the aperture (the blur kernel). Draft renders and renders at a reduced scale (proxy) always use Gaussian."
#define kParamApertureOptionGaussian "Gaussian", "Gaussian aperture, computed using a fast recursive filter. The blur size is the same as in the Blur plugin.", "gaussian"
#define kParamApertureOptionDisc "Disc", "Disc (circular) aperture with an antialiased edge, giving a bokeh effect. The blur size is the disc diameter. Slower than Gaussian.", "disc"
#define kParamApertureOptionCustom "Custom", "Custom aperture given by the Filter input, scaled so that its larger dimension is the blur size. Large kernels use the FFT, which is slower than Disc. If the Filter input is not connected or empty, Disc is used.", "custom"
enum ApertureEnum
{
    eApertureGaussian = 0,
    eApertureDisc,
    eApertureCustom,
};

// custom kernel pixels below this fraction of the kernel maximum are outside of the bokeh
constexpr double kKernelThreshold = 1e-3;

// ratio between the size and the Gaussian standard deviation (same as CImgBlur)
constexpr double kSizeToSigma = 1. / 2.4;
// extra margin for the Gaussian, relative to size (same as CImgBlur)
constexpr double kGaussianMargin = 1.5;

// Number of pixels read on each side of a pixel to blur it by sizePixels with the given aperture
// (the kernel half-size). Used for the render window padding, which must be the same in render()
// and getRegionsOfInterest(), and for the slice bounding boxes.
int
blurPadding(double sizePixels,
            ApertureEnum aperture)
{
    if (aperture == eApertureGaussian) {
        return static_cast<int>( std::ceil(sizePixels * kGaussianMargin) ) + 3; // Gaussian tails, as in CImgBlur
    }

    return static_cast<int>( std::ceil(sizePixels / 2) ) + 1; // the disc and custom kernels have a diameter of sizePixels
}

// Whether the render uses the fast Gaussian aperture whatever the Aperture parameter:
// draft renders and renders at a reduced scale (proxy).
bool
isProxyScale(const OfxPointD& renderScale)
{
    return (renderScale.x < 1.) || (renderScale.y < 1.);
}

// The aperture that determines the padding (see blurPadding()).
// At a reduced render scale (proxy), the Gaussian is always used. Draft mode is only known at
// render time ("Property Set - render calls" for kOfxImageEffectPropRenderQualityDraft), so a
// draft render may use the Gaussian with a smaller padding: only the far Gaussian tails are cut.
ApertureEnum
paddingAperture(ApertureEnum aperture,
                const OfxPointD& renderScale)
{
    return isProxyScale(renderScale) ? eApertureGaussian : aperture;
}

// The render window padding, in pixels at the render scale: the kernel half-size of the
// maximum blur size. render() and getRegionsOfInterest() must use the same padding.
OfxPointI
renderPadding(double maxSize,
              ApertureEnum aperture,
              const OfxPointD& renderScale)
{
    const ApertureEnum padAperture = paddingAperture(aperture, renderScale);
    const double size = (std::max)(0., maxSize);

    return { blurPadding(size * renderScale.x, padAperture), blurPadding(size * renderScale.y, padAperture) };
}

// Pixel aspect ratio of a clip, falling back to 1 if the host returns an invalid value.
double
clipPAR(const Clip* clip)
{
    const double par = clip ? clip->getPixelAspectRatio() : 1.;

    return (par > 0.) ? par : 1.;
}

// Convert a depth or disparity value to a signed "normalized defocus":
// 0 at the focal plane, -1 at infinity, positive for objects nearer than the focal plane.
// focus must be > 0 for depth maps.
double
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

// The signed blur size of a pixel, as a function of its Depth value: 0 within the depth of focus,
// negative behind it, positive in front of it, clamped to [-maxSize, maxSize].
class BlurModel
{
public:
    BlurModel(DepthTypeEnum depthType,
              double focus,
              double depthOfFocus,
              double size,
              double maxSize)
        : _depthType(depthType)
        , _focus( ( (depthType == eDepthTypeDepth) && !(focus > 0.) ) ? 1e-6 : focus ) // focus on the nearest possible distance
        , _size(size)
        , _maxSize(maxSize)
    {
        // bounds of the in-focus region, in normalized defocus
        if (depthType == eDepthTypeDepth) {
            const double vNear = _focus - depthOfFocus / 2;
            _focusHi = (vNear > 0.) ? normalizedDefocus(vNear, _focus, depthType) : DBL_MAX;
            _focusLo = normalizedDefocus(_focus + depthOfFocus / 2, _focus, depthType);
        } else {
            _focusHi = normalizedDefocus(_focus + depthOfFocus / 2, _focus, depthType);
            _focusLo = normalizedDefocus(_focus - depthOfFocus / 2, _focus, depthType);
        }
    }

    double blurSize(double v) const
    {
        const double u = normalizedDefocus(v, _focus, _depthType);
        const double defocus = (u < _focusLo) ? (u - _focusLo) : ( (u > _focusHi) ? (u - _focusHi) : 0. );

        return (std::max)( -_maxSize, (std::min)(_maxSize, _size * defocus) );
    }

private:
    DepthTypeEnum _depthType;
    double _focus;
    double _size;
    double _maxSize;
    double _focusLo = 0.;
    double _focusHi = 0.;
};

// The focus slice of a pixel with the given blur size: the blur size is quantized in steps of
// maxSize/slices, so that slice k (in [-slices, slices]) has a blur size of |k|*maxSize/slices.
int
sliceIndexOf(double blurSize,
             double maxSize,
             int slices)
{
    const int k = (maxSize > 0.) ? static_cast<int>( std::floor(blurSize * slices / maxSize + 0.5) ) : 0;

    return (std::max)( -slices, (std::min)(slices, k) );
}

// Gaussian blur of all channels, with zero boundary conditions (as the other apertures)
void
gaussianBlur(CImg<cimgpix_t>& img,
             double sx,
             double sy,
             const ImageEffect& effect)
{
    const float sigmax = static_cast<float>(sx * kSizeToSigma);
    const float sigmay = static_cast<float>(sy * kSizeToSigma);

    if (sigmax >= 0.1) {
        img.vanvliet(sigmax, 0, 'x', 0);
    }
    if ( effect.abort() ) {
        return;
    }
    if (sigmay >= 0.1) {
        img.vanvliet(sigmay, 0, 'y', 0);
    }
}

// In-place disc blur of all channels of a planar image, with elliptic radii rx, ry (in pixels).
// Pixels outside of the image are zero, as with the other apertures (see render()).
// The disc is antialiased: a pixel at distance d from the center of a disc of radius r is covered
// by about clamp(r + 0.5 - d, 0, 1). This is approximated separately along each kernel row dy:
// the row has a weight v (the coverage of its center pixel), and is a box filter whose two end
// pixels have a fractional weight f, so that each row is computed in O(1) using row prefix sums:
// O(width*height*(2*ry+1)) per channel. Exact along the two axes of the ellipse.
void
discBlur(CImg<cimgpix_t>& img,
         double rx,
         double ry,
         const ImageEffect& effect)
{
    const int W = img.width();
    const int H = img.height();
    if ( (W <= 0) || (H <= 0) || ( (rx < 0.5) && (ry < 0.5) ) ) {
        return;
    }
    // row dy: pixels |dx| < hw have weight v, pixels |dx| == hw have weight v*f
    // (hw == 0: only the center pixel, with weight v)
    struct KernelRow { int hw; float f; float v; };
    const int Ry = static_cast<int>( std::floor(ry + 0.5) );
    std::vector<KernelRow> rows(2 * Ry + 1);
    double total = 0.; // sum of the kernel weights
    for (int dy = -Ry; dy <= Ry; ++dy) {
        // the weight of the row pixels is zero at a = the half-chord of the ellipse of radii rx+0.5, ry+0.5
        const double t = dy / (ry + 0.5);
        const double a = (rx + 0.5) * std::sqrt( (std::max)(0., 1. - t * t) );
        KernelRow& r = rows[dy + Ry];
        r.hw = static_cast<int>( std::floor(a) );
        r.f = static_cast<float>(a - r.hw);
        r.v = static_cast<float>( (std::min)( 1., ry + 0.5 - std::abs(dy) ) );
        total += r.v * ( (r.hw == 0) ? 1. : (2 * r.hw - 1 + 2 * r.f) );
    }
    const double norm = 1. / total;
    CImg<double> prefix(W + 1, H);
    CImg<cimgpix_t> res(W, H);
    for (int c = 0; c < img.spectrum(); ++c) {
        if ( effect.abort() ) {
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
                const int yy = y + dy;
                if ( (yy < 0) || (yy >= H) ) {
                    continue;
                }
                const KernelRow& r = rows[dy + Ry];
                double rowSum;
                if (r.hw == 0) {
                    rowSum = img(x, yy, 0, c);
                } else {
                    const int m = r.hw - 1; // half-width of the full-weight part
                    rowSum = prefix( (std::min)(x + m, W - 1) + 1, yy ) - prefix( (std::max)(x - m, 0), yy );
                    if (x - r.hw >= 0) {
                        rowSum += r.f * img(x - r.hw, yy, 0, c);
                    }
                    if (x + r.hw < W) {
                        rowSum += r.f * img(x + r.hw, yy, 0, c);
                    }
                }
                sum += r.v * rowSum;
            }
            res(x, y) = static_cast<cimgpix_t>(sum * norm);
        }
        img.get_shared_channel(c) = res;
    }
} // discBlur

// In-place blur of all channels of a planar image by the custom aperture kernel, scaled so that
// its larger dimension is the blur size (sx, sy in pixels). Pixels outside of the image are zero.
void
customBlur(CImg<cimgpix_t>& img,
           const CImg<cimgpix_t>& kernel,
           double sx,
           double sy,
           const ImageEffect& effect)
{
    const int kw = kernel.width();
    const int kh = kernel.height();
    const double scale = 1. / (std::max)(kw, kh);
    // odd kernel sizes, so that the kernel center is a pixel center: all slices are aligned
    const int nx = 2 * static_cast<int>( std::floor( (kw * sx * scale - 1.) / 2. + 0.5 ) ) + 1;
    const int ny = 2 * static_cast<int>( std::floor( (kh * sy * scale - 1.) / 2. + 0.5 ) ) + 1;
    if ( (nx <= 1) && (ny <= 1) ) {
        return; // less than a pixel: no blur
    }
    // moving average (antialiased) to shrink the kernel; it is nearest-neighbor when enlarging, so use linear then
    const int interpolation = ( (nx <= kw) && (ny <= kh) ) ? 2 : 3;
    const CImg<cimgpix_t> k = kernel.get_resize( (std::max)(1, nx), (std::max)(1, ny), 1, 1, interpolation );
    const double sum = k.sum();
    if ( !(sum > 0.) ) {
        return;
    }
    // Direct convolution costs about 1 ns per pixel and kernel pixel, and the FFT about 100 ns per
    // pixel of the zero-padded power-of-two image (measured with CImg 3.x without FFTW).
    const double fftArea = static_cast<double>( cimg::nearest_pow2( static_cast<unsigned int>(img.width() + k.width() - 1) ) ) *
                           cimg::nearest_pow2( static_cast<unsigned int>(img.height() + k.height() - 1) );
    if ( static_cast<double>( k.size() ) * img.width() * img.height() < 100. * fftArea ) {
        const CImg<cimgpix_t> kn = k / static_cast<cimgpix_t>(sum);
        for (int c = 0; c < img.spectrum() && !effect.abort(); ++c) {
            img.get_shared_channel(c).convolve(kn, 0); // zero boundary conditions, centered kernel
        }

        return;
    }
    std::vector<int> chans( img.spectrum() );
    for (int c = 0; c < img.spectrum(); ++c) {
        chans[c] = c;
    }
    cimgConvolveFFT( img, chans, k, 1. / sum, -(k.width() / 2), -(k.height() / 2), effect );
} // customBlur

// In-place blur of all channels of img by the aperture, with a blur size of (sx, sy) pixels.
// The custom kernel is only used by eApertureCustom. Returns false if the effect was aborted.
bool
apertureBlur(CImg<cimgpix_t>& img,
             ApertureEnum aperture,
             const CImg<cimgpix_t>& kernel,
             double sx,
             double sy,
             const ImageEffect& effect)
{
    switch (aperture) {
    case eApertureCustom:
        customBlur(img, kernel, sx, sy, effect);
        break;
    case eApertureDisc:
        discBlur(img, sx / 2, sy / 2, effect);
        break;
    case eApertureGaussian:
        gaussianBlur(img, sx, sy, effect);
        break;
    }

    return !effect.abort();
}

// A rectangle that bounds nothing, for rectBoundingBox().
OfxRectI
emptyRect(int W,
          int H)
{
    return {W, H, 0, 0};
}

// Grow b so that it contains the pixel (x, y).
void
addPixel(OfxRectI& b,
         int x,
         int y)
{
    b.x1 = (std::min)(b.x1, x);
    b.x2 = (std::max)(b.x2, x + 1);
    b.y1 = (std::min)(b.y1, y);
    b.y2 = (std::max)(b.y2, y + 1);
}

// Merge the blurred slice layer (the slice color, the slice mask and the blurred all-ones image,
// see render()) over result, at the offset (crop.x1, crop.y1). The slice is first normalized by
// the blurred all-ones image (normalized convolution).
void
mergeOver(CImg<cimgpix_t>& result,
          const CImg<cimgpix_t>& layer,
          const OfxRectI& crop,
          int nComps)
{
    cimg_forXY(layer, x, y) {
        const float ones = layer(x, y, 0, nComps + 1);
        if ( !(ones > 1e-6f) ) {
            continue;
        }
        // clamp the mask: it may have small negative values (ringing) after the FFT
        const float a = (std::max)( 0.f, (std::min)(1.f, layer(x, y, 0, nComps) / ones) );
        for (int c = 0; c < nComps; ++c) {
            float& r = result(crop.x1 + x, crop.y1 + y, 0, c);
            r = layer(x, y, 0, c) / ones + (1.f - a) * r;
        }
        float& coverage = result(crop.x1 + x, crop.y1 + y, 0, nComps);
        coverage = a + (1.f - a) * coverage;
    }
}

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ZDefocusPlugin
    : public ImageEffect
{
public:
    explicit ZDefocusPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip( fetchClip(kOfxImageEffectOutputClipName) )
        , _srcClip( fetchClip(kOfxImageEffectSimpleSourceClipName) )
        , _depthClip( fetchClip(kClipDepth) )
        , _filterClip( fetchClip(kClipFilter) )
        , _depthChannel( fetchChoiceParam(kParamDepthChannel) )
        , _depthType( fetchChoiceParam(kParamDepthType) )
        , _useFocusPoint( fetchBooleanParam(kParamUseFocusPoint) )
        , _focusPoint( fetchDouble2DParam(kParamFocusPoint) )
        , _focus( fetchDoubleParam(kParamFocus) )
        , _depthOfFocus( fetchDoubleParam(kParamDepthOfFocus) )
        , _size( fetchDoubleParam(kParamSize) )
        , _maxSize( fetchDoubleParam(kParamMaxSize) )
        , _slices( fetchIntParam(kParamSlices) )
        , _aperture( fetchChoiceParam(kParamAperture) )
    {
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        assert( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                             _srcClip->getPixelComponents() == ePixelComponentRGB ||
                             _srcClip->getPixelComponents() == ePixelComponentAlpha) );
        assert(_depthClip && _filterClip);
        assert(_depthChannel && _depthType && _useFocusPoint && _focusPoint && _focus && _depthOfFocus &&
               _size && _maxSize && _slices && _aperture);

        updateGUI();
    }

private:
    void render(const RenderArguments &args) OVERRIDE FINAL;
    bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    void updateGUI();

    // the focus value at the given time, read from the depth at the focus point if necessary
    double getFocusValue(double time, const OfxPointD& renderScale, const Image* depth, DepthChannelEnum depthChannel) const;

    // the custom aperture kernel from the Filter input; empty if it is not available
    CImg<cimgpix_t> getCustomKernel(const RenderArguments &args);

    // the ImageEffect owns these
    Clip* const _dstClip;
    Clip* const _srcClip;
    Clip* const _depthClip;
    Clip* const _filterClip;
    ChoiceParam* const _depthChannel;
    ChoiceParam* const _depthType;
    BooleanParam* const _useFocusPoint;
    Double2DParam* const _focusPoint;
    DoubleParam* const _focus;
    DoubleParam* const _depthOfFocus;
    DoubleParam* const _size;
    DoubleParam* const _maxSize;
    IntParam* const _slices;
    ChoiceParam* const _aperture;
};


////////////////////////////////////////////////////////////////////////////////
double
ZDefocusPlugin::getFocusValue(double time,
                              const OfxPointD& renderScale,
                              const Image* depth,
                              DepthChannelEnum depthChannel) const
{
    const double focus = _focus->getValueAtTime(time);

    if ( !_useFocusPoint->getValueAtTime(time) || !depth ) {
        return focus;
    }
    OfxPointI pix;
    Coords::toPixel(_focusPoint->getValueAtTime(time), renderScale, clipPAR(_depthClip), &pix);
    const float* pixel = static_cast<const float*>( depth->getPixelAddress(pix.x, pix.y) );

    // depth not available at the focus point (e.g. outside of the Depth RoD): use the focus value
    return pixel ? cimgKernelValue(pixel, depth->getPixelComponentCount(), depthChannel) : focus;
}

CImg<cimgpix_t>
ZDefocusPlugin::getCustomKernel(const RenderArguments &args)
{
    CImg<cimgpix_t> kernel;

    if ( !_filterClip->isConnected() ) {
        return kernel;
    }
    auto_ptr<const Image> filter( _filterClip->fetchImage(args.time) );
    if ( !filter.get() ) {
        return kernel;
    }
    checkBadRenderScaleOrField(filter, args);
    if (filter->getPixelDepth() != eBitDepthFloat) {
        cimgThrowBitDepthUnsupported(*this);
    }
    // the kernel is the Filter image within its (finite) RoD
    OfxRectD rod;
    OfxPointD formatCenter;
    cimgGetFilterRoD(*this, _filterClip, args.time, &rod, &formatCenter);
    OfxRectI kernelRect;
    Coords::toPixelEnclosing(rod, args.renderScale, _filterClip->getPixelAspectRatio(), &kernelRect);
    if ( !Coords::rectIntersection(kernelRect, filter->getBounds(), &kernelRect) ) {
        return kernel;
    }
    // alpha, or luminance if there is no alpha (single-channel filters are alpha)
    const int key = (filter->getPixelComponentCount() == 4) ? eCImgKernelA : eCImgKernelLuminance;
    cimgExtractKernel(*filter, kernelRect, key, &kernel);
    // The bokeh is the bounding box of the significant kernel pixels: ignore faint tails
    // (e.g. a soft edge, or noise), which would make the bokeh smaller than the blur size.
    const cimgpix_t threshold = kKernelThreshold * kernel.max();
    if ( !(threshold > 0) ) {
        return CImg<cimgpix_t>();
    }
    OfxRectI box = emptyRect( kernel.width(), kernel.height() );
    cimg_forXY(kernel, x, y) {
        if (kernel(x, y) > threshold) {
            addPixel(box, x, y);
        }
    }
    kernel.crop(box.x1, box.y1, box.x2 - 1, box.y2 - 1);

    return (kernel.sum() > 0.) ? kernel : CImg<cimgpix_t>();
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
    const int nComps = dst->getPixelComponentCount();
    if (dst->getPixelDepth() != eBitDepthFloat) {
        cimgThrowBitDepthUnsupported(*this);
    }

    auto_ptr<const Image> src( _srcClip->isConnected() ?
                               _srcClip->fetchImage(time) : nullptr );
    if ( src.get() ) {
        if (src->getPixelDepth() != eBitDepthFloat) {
            cimgThrowBitDepthUnsupported(*this);
        }
        if (src->getPixelComponentCount() != nComps) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        checkBadRenderScaleOrField(src, args);
    }
    auto_ptr<const Image> depth( _depthClip->isConnected() ?
                                 _depthClip->fetchImage(time) : nullptr );
    if ( depth.get() && (depth->getPixelDepth() != eBitDepthFloat) ) {
        cimgThrowBitDepthUnsupported(*this);
    }
    // all the images are float: clear the message of cimgThrowBitDepthUnsupported() if a previous
    // render posted it (the Filter image is checked below, in getCustomKernel())
    clearPersistentMessage();

    // the pixels that are not rendered below (outside of the Source) are black
    fillBlack(*this, renderWindow, args.renderScale, dst.get());
    if ( !src.get() ) {
        return;
    }

    // get the parameters
    const DepthChannelEnum depthChannel = static_cast<DepthChannelEnum>( _depthChannel->getValueAtTime(time) );
    const DepthTypeEnum depthType = static_cast<DepthTypeEnum>( _depthType->getValueAtTime(time) );
    const double maxSize = _maxSize->getValueAtTime(time);
    const int slices = (std::max)( 1, _slices->getValueAtTime(time) );
    const ApertureEnum paramAperture = static_cast<ApertureEnum>( _aperture->getValueAtTime(time) );
    // in draft mode or proxy mode, always use the fast Gaussian blur
    ApertureEnum aperture = ( args.renderQualityDraft || isProxyScale(args.renderScale) ) ? eApertureGaussian : paramAperture;
    CImg<cimgpix_t> kernel;
    if (aperture == eApertureCustom) {
        kernel = getCustomKernel(args);
        if ( kernel.is_empty() ) {
            aperture = eApertureDisc;
        }
    }
    const BlurModel blurModel( depthType,
                               getFocusValue(time, args.renderScale, depth.get(), depthChannel),
                               (std::max)( 0., _depthOfFocus->getValueAtTime(time) ),
                               _size->getValueAtTime(time),
                               maxSize );

    // compute the region to process: the render window, padded by the maximum blur size
    // (getRegionsOfInterest() does not know about draft mode: use the aperture parameter)
    const OfxPointI pad = renderPadding(maxSize, paramAperture, args.renderScale);
    OfxRectI procRect = { renderWindow.x1 - pad.x, renderWindow.y1 - pad.y, renderWindow.x2 + pad.x, renderWindow.y2 + pad.y };
    if ( !Coords::rectIntersection(procRect, src->getBounds(), &procRect) ) {
        return;
    }
    const int W = procRect.x2 - procRect.x1;
    const int H = procRect.y2 - procRect.y1;
    const bool unpremultiplied = ( nComps == 4 && src->getPreMultiplication() == eImageUnPreMultiplied );

    // load the source image (premultiplied), and compute the slice index of each pixel and the
    // bounding box of each slice (empty if x1 >= x2)
    CImg<cimgpix_t> img(W, H, 1, nComps, 0.f);
    CImg<int> sliceIndex(W, H, 1, 1, 0);
    std::vector<OfxRectI> sliceBox( 2 * slices + 1, emptyRect(W, H) );
    const int depthComps = depth.get() ? depth->getPixelComponentCount() : 0;
    cimg_forXY(sliceIndex, x, y) {
        const float* s = static_cast<const float*>( src->getPixelAddress(procRect.x1 + x, procRect.y1 + y) );
        if (s) {
            const float a = unpremultiplied ? s[3] : 1.f;
            for (int c = 0; c < nComps; ++c) {
                img(x, y, 0, c) = (unpremultiplied && c < 3) ? s[c] * a : s[c];
            }
        }
        const float* dp = depth.get() ? static_cast<const float*>( depth->getPixelAddress(procRect.x1 + x, procRect.y1 + y) ) : nullptr;
        // no depth: consider that the pixel is at infinity (zero depth or disparity)
        const int k = sliceIndexOf(blurModel.blurSize( dp ? cimgKernelValue(dp, depthComps, depthChannel) : 0. ), maxSize, slices);
        sliceIndex(x, y) = k;
        addPixel(sliceBox[k + slices], x, y);
    }

    // Blur each slice and merge it over the result, from far (most negative) to near.
    // The last channel of result accumulates the slice masks (merged over), and is used to
    // normalize the result: this fills the holes left in the far slices by the near slices,
    // which would otherwise produce dark halos around in-focus foreground objects.
    // Each slice is blurred as a normalized convolution: pixels outside of the processed region
    // are zero for all apertures, and the blurred slice is divided by the blur of an all-ones
    // image. Without this, the masks of the near slices would fall below 1 near the image edges,
    // and the far slices would show through.
    const OfxRectI procBox = {0, 0, W, H};
    CImg<cimgpix_t> result(W, H, 1, nComps + 1, 0.f);
    CImg<cimgpix_t> layer;
    try {
        // Register this effect so that CImg's abort test (cimg_abort_test, see CImgFilter.h)
        // can stop the long vanvliet() calls when the host aborts the render.
        // This plugin has two inputs and its own render loop, so it cannot use CImgFilterPluginHelper,
        // which does the same thing around its render() call. The scope ends with the try block,
        // also on any exception.
        const CImgAbortScope abortScope(this);
        for (int k = -slices; k <= slices; ++k) {
            const OfxRectI& box = sliceBox[k + slices];
            if (box.x1 >= box.x2) {
                continue; // empty slice
            }
            if ( abort() ) {
                return;
            }
            const double sliceSize = std::abs(k) * maxSize / slices;
            const double sx = sliceSize * args.renderScale.x;
            const double sy = sliceSize * args.renderScale.y;
            // Only process the slice bounding box, grown by the kernel half-size (where the blurred
            // slice is non-zero) and by the kernel half-size again (the pixels read to blur it,
            // used by the normalization), so that small slices are cheap.
            const int hx = blurPadding(sx, aperture);
            const int hy = blurPadding(sy, aperture);
            OfxRectI crop = {box.x1 - 2 * hx, box.y1 - 2 * hy, box.x2 + 2 * hx, box.y2 + 2 * hy};
            Coords::rectIntersection(crop, procBox, &crop);
            // premultiplied slice image, with the slice mask and the all-ones image as last channels
            layer.assign(crop.x2 - crop.x1, crop.y2 - crop.y1, 1, nComps + 2, 0.f);
            layer.get_shared_channel(nComps + 1).fill(1.f);
            cimg_forXY(layer, x, y) {
                if (sliceIndex(crop.x1 + x, crop.y1 + y) == k) {
                    for (int c = 0; c < nComps; ++c) {
                        layer(x, y, 0, c) = img(crop.x1 + x, crop.y1 + y, 0, c);
                    }
                    layer(x, y, 0, nComps) = 1.f;
                }
            }
            if ( !apertureBlur(layer, aperture, kernel, sx, sy, *this) ) {
                return;
            }
            mergeOver(result, layer, crop, nComps);
        }
    } catch (CImgAbortException&) {
        return;
    }

    // write the result, divided by the coverage, to the render window
    OfxRectI outRect;
    if ( !Coords::rectIntersection(renderWindow, procRect, &outRect) ) {
        return;
    }
    for (int y = outRect.y1; y < outRect.y2; ++y) {
        float* d = static_cast<float*>( dst->getPixelAddress(outRect.x1, y) );
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
            if ( unpremultiplied && (d[3] > 0.f) ) {
                const float a = d[3];
                d[0] /= a;
                d[1] /= a;
                d[2] /= a;
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
    const ApertureEnum aperture = static_cast<ApertureEnum>( _aperture->getValueAtTime(time) );
    // same padding as in render(), converted from pixels to canonical coordinates
    const OfxPointI pad = renderPadding(_maxSize->getValueAtTime(time), aperture, args.renderScale);
    const double padX = pad.x * clipPAR(_srcClip) / args.renderScale.x;
    const double padY = pad.y / args.renderScale.y;
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
    // The custom aperture needs the whole Filter image. Else, the Filter is not used: set an empty
    // RoI, since the default RoI is the output RoI ("The default RoI is simply the value passed in
    // on the kOfxImageEffectPropRegionOfInterest inArgs property set"), so that the host does not
    // render the kernel generator for nothing. Draft mode is not known here (see paddingAperture()).
    OfxRectD filterRoI = {0., 0., 0., 0.};
    if ( (paddingAperture(aperture, args.renderScale) == eApertureCustom) && _filterClip->isConnected() ) {
        OfxPointD formatCenter;
        cimgGetFilterRoD(*this, _filterClip, time, &filterRoI, &formatCenter);
    }
    rois.setRegionOfInterest(*_filterClip, filterRoI);
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

mDeclarePluginFactory(ZDefocusPluginFactory, {ofxsThreadSuiteCheck(); cimgFFTLoad();}, {});

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

    ClipDescriptor *filterClip = desc.defineClip(kClipFilter);
    filterClip->setHint(kClipFilterHint);
    filterClip->addSupportedComponent(ePixelComponentRGBA);
    filterClip->addSupportedComponent(ePixelComponentRGB);
    filterClip->addSupportedComponent(ePixelComponentAlpha);
    filterClip->setTemporalClipAccess(false);
    filterClip->setOptional(true);
    filterClip->setSupportsTiles(kSupportsTiles);
    filterClip->setIsMask(false);

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
        assert(param->getNOptions() == eApertureCustom);
        param->appendOption(kParamApertureOptionCustom);
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
