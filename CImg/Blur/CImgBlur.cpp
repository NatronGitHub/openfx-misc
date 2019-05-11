/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2018 INRIA
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
 * OFX CImgBlur and CImgLaplacian plugins.
 */

#include <memory>
#include <cmath>
#include <cstring>
#include <climits>
#include <cfloat> // DBL_MAX, FLT_EPSILON
#include <algorithm>
#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif
//#include <iostream>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsCoords.h"
#include "ofxsCopier.h"
#include "ofxsLut.h"

#include "CImgFilter.h"

#if cimg_version < 161
#error "This plugin requires CImg 1.6.1, please upgrade CImg."
#endif

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName          "BlurCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
    "Blur input stream or compute derivatives.\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "\n" \
    "Note that the Gaussian filter [1] is implemented as an IIR (infinite impulse response) filter [2][3], whereas most compositing software implement the Gaussian as a FIR (finite impulse response) filter by cropping the Gaussian impulse response. Consequently, when blurring a white dot on black background, it produces very small values very far away from the dot. The quasi-Gaussian filter is also IIR.\n" \
    "\n" \
    "A very common process in compositing to expand colors on the edge of a matte is to use the premult-blur-unpremult combination [4][5]. The very small values produced by the IIR Gaussian filter produce undesirable artifacts after unpremult. For this process, the FIR quadratic filter (or the faster triangle or box filters) should be preferred over the IIR Gaussian filter.\n" \
    "\n" \
    "References:\n" \
    "[1] https://en.wikipedia.org/wiki/Gaussian_filter\n" \
    "[2] I.T. Young, L.J. van Vliet, M. van Ginkel, Recursive Gabor filtering. IEEE Trans. Sig. Proc., vol. 50, pp. 2799-2805, 2002. (this is an improvement over Young-Van Vliet, Sig. Proc. 44, 1995)\n" \
    "[3] B. Triggs and M. Sdika. Boundary conditions for Young-van Vliet recursive filtering. IEEE Trans. Signal Processing, vol. 54, pp. 2365-2367, 2006.\n" \
    "[4] Nuke Expand Edges or how to get rid of outlines. http://franzbrandstaetter.com/?p=452\n" \
    "[5] Colour Smear for Nuke. http://richardfrazer.com/tools-tutorials/colour-smear-for-nuke/\n" \
    "\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define STRINGIZE_CPP_NAME_(token) # token
#define STRINGIZE_CPP_(token) STRINGIZE_CPP_NAME_(token)

#ifdef DEBUG
#define kPluginDescriptionDebug " with debug"
#else
#define kPluginDescriptionDebug " without debug"
#endif

#ifdef NDEBUG
#define kPluginDescriptionNDebug ", without assertions"
#else
#define kPluginDescriptionNDebug ", with assertions"
#endif

#if defined(__OPTIMIZE__)
#define kPluginDescriptionOptimize ", with optimizations"
#else
#define kPluginDescriptionOptimize "" // don't know (maybe not gcc or clang)
#endif

#if defined(__NO_INLINE__)
#define kPluginDescriptionInline ", without inlines"
#else
#define kPluginDescriptionInline "" // don't know (maybe not gcc or clang)
#endif

#if cimg_use_openmp!=0
#define kPluginDescriptionOpenMP ", with OpenMP " STRINGIZE_CPP_(_OPENMP)
#else
#define kPluginDescriptionOpenMP ", without OpenMP"
#endif

#ifdef __clang__
#define kPluginDescriptionCompiler ", using Clang version " __clang_version__
#else
#ifdef __GNUC__
#define kPluginDescriptionCompiler ", using GNU C++ version " __VERSION__
#else
#define kPluginDescriptionCompiler ""
#endif
#endif

#define kPluginDescriptionFull kPluginDescription "\n\nThis plugin was compiled " kPluginDescriptionDebug kPluginDescriptionNDebug kPluginDescriptionOptimize kPluginDescriptionInline kPluginDescriptionOpenMP kPluginDescriptionCompiler "."

#define kPluginNameLaplacian          "LaplacianCImg"
#define kPluginDescriptionLaplacian \
    "Blur input stream, and subtract the result from the input image. This is not a mathematically correct Laplacian (which would be the sum of second derivatives over X and Y).\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameSharpen          "SharpenCImg"
#define kPluginDescriptionSharpen \
    "Sharpen the input stream by enhancing its Laplacian.\n" \
    "The effects adds the Laplacian (as computed by the Laplacian plugin) times the 'Amount' parameter to the input stream.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameSoften          "SoftenCImg"
#define kPluginDescriptionSoften \
    "Soften the input stream by reducing its Laplacian.\n" \
    "The effects subtracts the Laplacian (as computed by the Laplacian plugin) times the 'Amount' parameter from the input stream.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameChromaBlur          "ChromaBlurCImg"
#define kPluginDescriptionChromaBlur \
    "Blur the chrominance of an input stream. Smoothing is done on the x and y components in the CIE xyY color space. " \
    "Used to prep strongly compressed and chroma subsampled footage for keying.\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameBloom          "BloomCImg"
#define kPluginDescriptionBloom \
    "Apply a Bloom filter (Kawase 2004) that sums multiple blur filters of different radii,\n" \
    "resulting in a larger but sharper glare than a simple blur.\n" \
    "It is similar to applying 'Count' separate Blur filters to the same input image with sizes 'Size', 'Size'*'Ratio', 'Size'*'Ratio'^2, etc., and averaging the results.\n" \
    "The blur radii follow a geometric progression (of common ratio 2 in the original implementation, " \
    "bloomRatio in this implementation), and a total of bloomCount blur kernels are summed up (bloomCount=5 " \
    "in the original implementation, and the kernels are Gaussian).\n" \
    "The blur filter can be a quasi-Gaussian, a Gaussian, a box, a triangle or a quadratic filter.\n" \
    "Ref.: Masaki Kawase, \"Practical Implementation of High Dynamic Range Rendering\", GDC 2004.\n" \
    "Uses the 'vanvliet' and 'deriche' functions from the CImg library.\n" \
    "CImg is a free, open-source library distributed under the CeCILL-C " \
    "(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
    "It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameErodeBlur          "ErodeBlurCImg"
#define kPluginDescriptionErodeBlur \
"Performs an operation that looks like an erosion or a dilation by smoothing the image and then remapping the values of the result.\n" \
"The image is first smoothed by a triangle filter of width 2*abs(size).\n" \
"Now suppose the image is a 0-1 step edge (I=0 for x less than 0, I=1 for x greater than 0). The intensities are linearly remapped so that the value at x=size-0.5 is mapped to 0 and the value at x=size+0.5 is mapped to 1.\n" \
"This process usually works well for mask images (i.e. images which are either 0 or 1), but may give strange results on images with real intensities, where another Erode filter has to be used.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameEdgeExtend          "EdgeExtendCImg"
#define kPluginDescriptionEdgeExtend \
"Fill a matte (i.e. a non-opaque color image with an alpha channel) by extending the edges of the matte. This effect does nothing an an opaque image.\n" \
"If the input matte comes from a keyer, the alpha channel of the matte should be first eroded by a small amount to remove pixels containing mixed foreground/background colors. If not, these mixed colors may be extended instead of the pure foreground colors.\n" \
"The filling process works by iteratively blurring the image, and merging the non-blurred image over the image to get to the next iteration. There are exactly 'Slices' such operations. The blur size at each iteration is linearly increasing.\n" \
"'Size' is thus the total size of the edge extension, and 'Slices' is an indicator of the precision: the more slices there are, the sharper is the final image near the original edges.\n" \
"Optionally, the image can be multiplied by the alpha channel on input (premultiplied), and divided by the alpha channel on output (unpremultiplied), so that if RGB contain an image and Alpha contains a mask, the output is an image where the RGB is smeared from the non-zero areas of the mask to the zero areas of the same mask.\n" \
"The 'Size' parameter gives the size of the largest blur kernel, 'Count' gives the number of blur kernels, and 'Ratio' gives the ratio between consecutive blur kernel sizes. The size of the smallest blur kernel is thus 'Size'/'Ratio'^('Count'-1)\n" \
"To get the classical single unpremult-blur-premult, use 'Count'=1 and set the size to the size of the blur kernel. However, near the mask borders, a frontier can be seen between the non-blurred area (this inside of the mask) and the blurred area. Using more blur sizes will give a much smoother transition.\n" \
"The idea for the builtup blurs to expand RGB comes from the EdgeExtend effect for Nuke by Frank Rueter (except the blurs were merged from the smallest to the largest, and here it is done the other way round), with suggestions by Lucas Pfaff.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginNameEdgeDetect          "EdgeDetectCImg"
#define kPluginDescriptionEdgeDetect \
"Perform edge detection by computing the image gradient magnitude. Optionally, edge detection can be preceded by blurring, and followed by erosion and thresholding. In most cases, EdgeDetect is followed a Grade node to extract the proper edges and generate a mask from these.\n" \
"\n" \
"For color or multi-channel images, several edge detection algorithms are proposed to combine the gradients computed in each channel:\n" \
"- Separate: the gradient magnitude is computed in each channel separately, and the output is a color edge image.\n" \
"- RMS: the RMS of per-channel gradients magnitudes is computed.\n" \
"- Max: the maximum per-channel gradient magnitude is computed.\n" \
"- Tensor: the tensor gradient norm [1].\n" \
"\n" \
"References:\n" \
"- [1] Silvano Di Zenzo, A note on the gradient of a multi-image, CVGIP 33, 116-125 (1986). http://people.csail.mit.edu/tieu/notebook/imageproc/dizenzo86.pdf\n" \
"\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.eu)."

#define kPluginIdentifier    "net.sf.cimg.CImgBlur"
#define kPluginIdentifierLaplacian    "net.sf.cimg.CImgLaplacian"
#define kPluginIdentifierSharpen    "net.sf.cimg.CImgSharpen"
#define kPluginIdentifierSoften    "net.sf.cimg.CImgSoften"
#define kPluginIdentifierChromaBlur    "net.sf.cimg.CImgChromaBlur"
#define kPluginIdentifierBloom    "net.sf.cimg.CImgBloom"
#define kPluginIdentifierErodeBlur    "eu.cimg.ErodeBlur"
#define kPluginIdentifierEdgeExtend    "eu.cimg.EdgeExtend"
#define kPluginIdentifierEdgeDetect    "eu.cimg.EdgeDetect"

// History:
// version 1.0: initial version
// version 2.0: size now has two dimensions
// version 3.0: use kNatronOfxParamProcess* parameters
// version 4.0: the default is to blur all channels including alpha (see processAlpha in describeInContext)
// version 4.1: added cropToFormat parameter on Natron
#define kPluginVersionMajor 4 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsComponentRemapping 1 // except for ChromaBlur
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
#define kSupportsRGBA true
#define kSupportsRGB true
#define kSupportsXY true // except for ChromaBlue
#define kSupportsAlpha true // except for ChromaBlue

#define kDefaultUnpremult false // Blur works on premultiplied RGBA by default

#define kParamSharpenSoftenAmount "amount"
#define kParamSharpenSoftenAmountLabel "Amount"
#define kParamSharpenAmountHint "Amount of sharpening to apply."
#define kParamSoftenAmountHint "Amount of softening to apply."

#define kParamSize "size"
#define kParamSizeLabel "Size"
#define kParamSizeHint "Size (diameter) of the filter kernel, in pixel units (>=0). The standard deviation of the corresponding Gaussian is size/2.4. No filter is applied if size < 1.2."
#define kParamSizeDefault 0.
#define kParamSizeDefaultLaplacian 3.

#define kParamErodeSize "size"
#define kParamErodeSizeLabel "Size"
#define kParamErodeSizeHint "How much to shrink the black and white mask, in pixels (can be negative to dilate)."

#define kParamErodeBlur "blur"
#define kParamErodeBlurLabel "Blur"
#define kParamErodeBlurHint "Soften the borders of the generated mask."

#define kParamUniform "uniform"
#define kParamUniformLabel "Uniform"
#define kParamUniformHint "Apply the same amount of blur on X and Y."

#define kParamOrderX "orderX"
#define kParamOrderXLabel "X derivation order"
#define kParamOrderXHint "Derivation order in the X direction. (orderX=0,orderY=0) does smoothing, (orderX=1,orderY=0) computes the X component of the image gradient."

#define kParamOrderY "orderY"
#define kParamOrderYLabel "Y derivation order"
#define kParamOrderYHint "Derivation order in the Y direction. (orderX=0,orderY=0) does smoothing, (orderX=0,orderY=1) computes the X component of the image gradient."

#define kParamBloomRatio "bloomRatio"
#define kParamBloomRatioLabel "Ratio"
#define kParamBloomRatioHint "Ratio between successive kernel sizes of the bloom filter. A ratio of 1 gives no Bloom effect, just the original blur. A higher ratio gives a blur kernel with a heavier tail. The original implementation uses a value of 2."
#define kParamBloomRatioDefault 2.

#define kParamBloomCount "bloomCount"
#define kParamBloomCountLabel "Count"
#define kParamBloomCountHint "Number of blur kernels of the bloom filter. The original implementation uses a value of 5. Higher values give a wider of heavier tail (the size of the largest blur kernel is 2**bloomCount * size). A count of 1 is just the original blur."
#define kParamBloomCountDefault 5

#define kParamEdgeExtendPremult "edgeExtendPremult"
#define kParamEdgeExtendPremultLabel "Premult Source"
#define kParamEdgeExtendPremultHint "Premultiply the source image by its alpha channel before processing. Do not check if the source matte is already premultiplied"

#define kParamEdgeExtendSize "edgeExtendSize"
#define kParamEdgeExtendSizeLabel "Size"
#define kParamEdgeExtendSizeHint "Maximum blur kernel size applied in the ExtendSlices filter. Raise to extend the edges further."
#define kParamEdgeExtendSizeDefault 10

#define kParamEdgeExtendCount "edgeExtendSlices"
#define kParamEdgeExtendCountLabel "Slices"
#define kParamEdgeExtendCountHint "Number of blur kernels applied in the ExtendSlices filter. A count of 1 just merges the source image over the source image blurred by a kernel of size Size."
#define kParamEdgeExtendCountDefault 5

#define kParamEdgeExtendUnpremult "edgeExtendUnpremult"
#define kParamEdgeExtendUnpremultLabel "Unpremult Result"
#define kParamEdgeExtendUnpremultHint "Unpremultiply the result image by its alpha channel after processing."


#define kParamBoundary "boundary"
#define kParamBoundaryLabel "Border Conditions" //"Boundary Conditions"
#define kParamBoundaryHint "Specifies how pixel values are computed out of the image domain. This mostly affects values at the boundary of the image. If the image represents intensities, Nearest (Neumann) conditions should be used. If the image represents gradients or derivatives, Black (Dirichlet) boundary conditions should be used."
#define kParamBoundaryOptionDirichlet "Black", "Dirichlet boundary condition: pixel values out of the image domain are zero.", "black"
#define kParamBoundaryOptionNeumann "Nearest", "Neumann boundary condition: pixel values out of the image domain are those of the closest pixel location in the image domain.", "nearest"
#define kParamBoundaryOptionPeriodic "Periodic", "Image is considered to be periodic out of the image domain.", "periodic"
#define kParamBoundaryDefault eBoundaryDirichlet
#define kParamBoundaryDefaultLaplacian eBoundaryNeumann
#define kParamBoundaryDefaultBloom eBoundaryNeumann
#define kParamBoundaryDefaultEdgeExtend eBoundaryNeumann

enum BoundaryEnum
{
    eBoundaryDirichlet = 0,
    eBoundaryNeumann,
    //eBoundaryPeriodic,
};

#define kParamColorspace "colorspace"
#define kParamColorspaceLabel "Colorspace"
#define kParamColorspaceHint "Formula used to compute chrominance from RGB values."
#define kParamColorspaceOptionRec709 "Rec. 709", "Use Rec. 709 with D65 illuminant.", "rec709"
#define kParamColorspaceOptionRec2020 "Rec. 2020", "Use Rec. 2020 with D65 illuminant.", "rec2020"
#define kParamColorspaceOptionACESAP0 "ACES AP0", "Use ACES AP0 with ACES (approx. D60) illuminant.", "acesap0"
#define kParamColorspaceOptionACESAP1 "ACES AP1", "Use ACES AP1 with ACES (approx. D60) illuminant.", "acesap1"

enum ColorspaceEnum
{
    eColorspaceRec709,
    eColorspaceRec2020,
    eColorspaceACESAP0,
    eColorspaceACESAP1,
};

#define kParamFilter "filter"
#define kParamFilterLabel "Filter"
#define kParamFilterHint "Bluring filter. The quasi-Gaussian filter should be appropriate in most cases. The Gaussian filter is more isotropic (its impulse response has rotational symmetry), but slower."
#define kParamFilterOptionQuasiGaussian "Quasi-Gaussian", "Quasi-Gaussian filter (0-order recursive Deriche filter, faster) - IIR (infinite support / impulsional response).", "quasigaussian"
#define kParamFilterOptionGaussian "Gaussian", "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower) - IIR (infinite support / impulsional response).", "gaussian"
#define kParamFilterOptionBox "Box", "Box filter - FIR (finite support / impulsional response).", "box"
#define kParamFilterOptionTriangle "Triangle", "Triangle/tent filter - FIR (finite support / impulsional response).", "triangle"
#define kParamFilterOptionQuadratic "Quadratic", "Quadratic filter - FIR (finite support / impulsional response).", "quadratic"
#define kParamFilterDefault eFilterGaussian
#define kParamFilterDefaultBloom eFilterQuasiGaussian
#define kParamFilterDefaultEdgeExtend eFilterQuasiGaussian
enum FilterEnum
{
    eFilterQuasiGaussian = 0,
    eFilterGaussian,
    eFilterBox,
    eFilterTriangle,
    eFilterQuadratic,
};

#define kParamEdgeDetectFilter "filter"
#define kParamEdgeDetectFilterLabel "Filter"
#define kParamEdgeDetectFilterHint "Edge detection filter. If the blur size is not zero, it is used as the kernel size for quasi-Gaussian, Gaussian, box, triangle and quadratic filters. For the simple, rotation-invariant and Sobel filters, the image is pre-blurred with a Gaussian filter."
#define kParamEdgeDetectFilterOptionSimple "Simple", "Gradient is estimated by centered finite differences.", "simple"
#define kParamEdgeDetectFilterOptionSobel "Sobel", "Compute gradient using the Sobel 3x3 filter.", "sobel"
#define kParamEdgeDetectFilterOptionRotationInvariant "Rotation Invariant", "Compute gradient using a 3x3 rotation-invariant filter.", "rotinvariant"
#define kParamEdgeDetectFilterOptionQuasiGaussian "Quasi-Gaussian", "Quasi-Gaussian filter (0-order recursive Deriche filter, faster) - IIR (infinite support / impulsional response).", "quasigaussian"
#define kParamEdgeDetectFilterOptionGaussian "Gaussian", "Gaussian filter (Van Vliet recursive Gaussian filter, more isotropic, slower) - IIR (infinite support / impulsional response).", "gaussian"
#define kParamEdgeDetectFilterOptionBox "Box", "Box filter - FIR (finite support / impulsional response).", "box"
#define kParamEdgeDetectFilterOptionTriangle "Triangle", "Triangle/tent filter - FIR (finite support / impulsional response).", "triangle"
#define kParamEdgeDetectFilterOptionQuadratic "Quadratic", "Quadratic filter - FIR (finite support / impulsional response).", "quadratic"
#define kParamEdgeDetectFilterDefault eEdgeDetectFilterGaussian
enum EdgeDetectFilterEnum
{
    eEdgeDetectFilterSimple = 0,
    eEdgeDetectFilterSobel,
    eEdgeDetectFilterRotationInvariant,
    eEdgeDetectFilterQuasiGaussian,
    eEdgeDetectFilterGaussian,
    eEdgeDetectFilterBox,
    eEdgeDetectFilterTriangle,
    eEdgeDetectFilterQuadratic,
};

#define kParamEdgeDetectMultiChannel "multiChannel"
#define kParamEdgeDetectMultiChannelLabel "Multi-Channel"
#define kParamEdgeDetectMultiChannelHint "Operation used to combine multi-channel (e.g. color) gradients into an edge detector. This parameter has no effect if a single channel (e.g. alpha) is processed."
#define kParamEdgeDetectMultiChannelOptionSeparate "Separate", "The gradient magnitude is computed in each channel separately, and the output is a color edge image.", "separate"
#define kParamEdgeDetectMultiChannelOptionRMS "RMS", "The RMS of per-channel gradients magnitudes is computed.", "rms"
#define kParamEdgeDetectMultiChannelOptionMax "Max", "The maximum per-channel gradient magnitude is computed.", "max"
#define kParamEdgeDetectMultiChannelOptionTensor "Tensor", "The tensor gradient norm is computed. See Silvano Di Zenzo, A note on the gradient of a multi-image, CVGIP 33, 116-125 (1986).", "tensor"
enum EdgeDetectMultiChannelEnum
{
    eEdgeDetectMultiChannelSeparate = 0,
    eEdgeDetectMultiChannelRMS,
    eEdgeDetectMultiChannelMax,
    eEdgeDetectMultiChannelTensor,
};
#define kParamEdgeDetectMultiChannelDefault eEdgeDetectMultiChannelTensor

#define kParamEdgeDetectNMS "nms"
#define kParamEdgeDetectNMSLabel "Non-Maxima Suppression"
#define kParamEdgeDetectNMSHint "Perform non-maxima suppression (after edge detection and erosion): only values that are maximal in the direction orthogonal to the contour are kept. For multi-channel images, the contour direction estimation depends on the multi-channel operation."

#define kParamEdgeDetectErode "erodeSize"
#define kParamEdgeDetectErodeLabel "Erode Size"
#define kParamEdgeDetectErodeHint "Size of the erosion performed after edge detection."

#define kParamEdgeDetectBlur "blurSize"
#define kParamEdgeDetectBlurLabel "Blur Size"
#define kParamEdgeDetectBlurHint "Size of the blur kernel applied before edge detection."

#define kParamExpandRoD "expandRoD"
#define kParamExpandRoDLabel "Expand RoD"
#define kParamExpandRoDHint "Expand the source region of definition by 1.5*size (3.6*sigma)."

#define kParamCropToFormat "cropToFormat"
#define kParamCropToFormatLabel "Crop To Format"
#define kParamCropToFormatHint "If the source is inside the format and the effect extends it outside of the format, crop it to avoid unnecessary calculations. To avoid unwanted crops, only the borders that were inside of the format in the source clip will be cropped."

#define kParamAlphaThreshold "alphaThreshold"
#define kParamAlphaThresholdLabel "Alpha Threshold"
#define kParamAlphaThresholdHint "If this value is non-zero, any alpha value below this is set to zero. This is only useful for IIR filters (Gaussian and Quasi-Gaussian), which may produce alpha values very close to zero due to arithmetic precision. Remind that, in theory, a black image with a single white pixel should produce non-zero values everywhere, but a few VFX tricks rely on the fact that alpha should be zero far from the alpha edges (e.g. the premult-blur-unpremult trick to fill holes)). A threshold value of 0.003 is reasonable, and values between 0.001 and 0.01 are usually enough to remove these artifacts."

typedef cimgpix_t T;
using namespace cimg_library;

#if cimg_version < 160
#define cimgblur_internal_vanvliet
#endif

#if cimg_version < 200
// cimg pre-2.0.0's box filter broke TestPIK (check the output of the PIKColor, it should be almost solid green)
// the reason is:
// - sum in _cimg_blur_box_apply should be double, not Tfloat
// other possible issues (which do not affect BlurCImg, since T=float in our case):
// - win shound be CImg<T> not CImg<Tfloat>
// - next and prev should be T not Tfloat
// this was fixed in https://github.com/dtschump/CImg/commit/2067bd0765d23afc4c41b706bc57d094a2dce51d
#define cimgblur_internal_boxfilter
#endif

// Exponentiation by squaring
// works with positive or negative integer exponents
template<typename T>
T
ipow(T base,
     int exp)
{
    T result = T(1);

    if (exp >= 0) {
        while (exp) {
            if (exp & 1) {
                result *= base;
            }
            exp >>= 1;
            base *= base;
        }
    } else {
        exp = -exp;
        while (exp) {
            if (exp & 1) {
                result /= base;
            }
            exp >>= 1;
            base *= base;
        }
    }

    return result;
}

#ifdef cimgblur_internal_boxfilter

// see cimg_library::CImg::cimg_blur_box_apply
static inline
T
get_data(const T *ptr,
         const int N,
         const unsigned long off,
         const bool boundary_conditions,
         const int x)
{
    assert(N >= 1);
    if (x < 0) {
        return boundary_conditions ? ptr[0] : T();
    }
    if (x >= N) {
        return boundary_conditions ? ptr[(N - 1) * off] : T();
    }

    return ptr[x * off];
}

// [internal] Apply a box/triangle/quadratic filter (used by CImg<T>::boxfilter() and CImg<T>::blur_box()).
/**
   \param ptr the pointer of the data
   \param N size of the data
   \param boxsize Size of the box filter (can be subpixel).
   \param off the offset between two data point
   \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
   \param order the order of the filter 0 (smoothing), 1st derivtive, 2nd derivative, 3rd derivative
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
 **/
// see cimg_library::CImg::cimg_blur_box_apply
static void
_cimg_blur_box_apply(T *ptr,
                     const float boxsize,
                     const int N,
                     const unsigned long off,
                     const int order,
                     const bool boundary_conditions,
                     const unsigned int nb_iter)
{
    // smooth
    if ( (boxsize > 1.) && (nb_iter > 0) ) {
        int w2 = (int)(boxsize - 1) / 2;
        int winsize = 2 * w2 + 1;
        double frac = (boxsize - winsize) / 2.;
        std::vector<T> win(winsize);
        for (unsigned int iter = 0; iter<nb_iter; ++iter) {
            // prepare for first iteration
            double sum = 0; // window sum
            for (int x = -w2; x <= w2; ++x) {
                win[x + w2] = get_data(ptr, N, off, boundary_conditions, x);
                sum += win[x + w2];
            }
            int ifirst = 0;
            int ilast = 2 * w2;
            T prev = get_data(ptr, N, off, boundary_conditions, -w2 - 1);
            T next = get_data(ptr, N, off, boundary_conditions, +w2 + 1);
            // main loop
            for (int x = 0; x < N - 1; ++x) {
                // add partial pixels
                const double sum2 = sum + frac * (prev + next);
                // fill result
                ptr[x * off] = sum2 / boxsize;
                // advance for next iteration
                prev = win[ifirst];
                sum -= prev;
                ifirst = (ifirst + 1) % winsize;
                ilast = (ilast + 1) % winsize;
                assert( (ilast + 1) % winsize == ifirst ); // it is a circular buffer
                win[ilast] = next;
                sum += next;
                next = get_data(ptr, N, off, boundary_conditions, x + w2 + 2);
            }
            // last iteration
            // add partial pixels
            const double sum2 = sum + frac * (prev + next);
            // fill result
            ptr[(N - 1) * off] = (T)(sum2 / boxsize);
        }
    }
    // derive
    switch (order) {
    case 0:
        // nothing to do
        break;
    case 1: {
        T p = get_data(ptr, N, off, boundary_conditions, -1);
        T c = get_data(ptr, N, off, boundary_conditions, 0);
        T n = get_data(ptr, N, off, boundary_conditions, +1);
        for (int x = 0; x < N - 1; ++x) {
            ptr[x * off] = (n - p) / 2.;
            // advance
            p = c;
            c = n;
            n = get_data(ptr, N, off, boundary_conditions, x + 2);
        }
        // last pixel
        ptr[(N - 1) * off] = (n - p) / 2.;
    }
    break;
    case 2: {
        T p = get_data(ptr, N, off, boundary_conditions, -1);
        T c = get_data(ptr, N, off, boundary_conditions, 0);
        T n = get_data(ptr, N, off, boundary_conditions, +1);
        for (int x = 0; x < N - 1; ++x) {
            ptr[x * off] = n - 2 * c + p;
            // advance
            p = c;
            c = n;
            n = get_data(ptr, N, off, boundary_conditions, x + 2);
        }
        // last pixel
        ptr[(N - 1) * off] = n - 2 * c + p;
    }
    break;
    }
} // _cimg_box_apply

//! Box/Triangle/Quadratic filter.
/**
   \param width width of the box filter
   \param iter number of iterations (1 = box, 2 = triangle, 3 = quadratic)
   \param order the order of the filter 0,1,2,3
   \param axis  Axis along which the filter is computed. Can be <tt>{ 'x' | 'y' | 'z' | 'c' }</tt>.
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
 **/
/*
 The box filter of width s has the impulse response:
 x < -s/2 or x > s/2: y = 0
 x >= -s/2 and x <= s/2: y = 1/s

 The triangle filter of width s has the impulse response:
 x < -s or x > s: y = 0
 x >= -s and x <= 0: y = 1/s + x/(s*s) = int(1/s^2, t = -(1/2)*s .. x+(1/2)*s)
 x > 0 and x <= s: y = 1/s - x/(s*s)

 The quadratic filter of width s has the impulse response:
 x < -s*3/2 or x > s*3/2: y = 0
 x >= -s*3/2 and x <= -s/2: y = (4*x^2 + 12*x*s + 9s^2) / (8s^3) = simplify(int((1/s)*(1/s+t/(s*s)), t = -s .. x+(1/2)*s));

 x >= -s/2 and x <= s/2: y = (-4x^2 + 3s^2) / (4s^3) = simplify(int((1/s)*(1/s+t/(s*s)), t = x-(1/2)*s .. 0)+int((1/s)*(1/s-t/(s*s)), t = 0 .. x+(1/2)*s));

 x >= s/2 and x <= s*3/2: y = (4x^2 - 12xs + 9s^2) / (8s^3)

 The equation of a step edge is:
 x<=0: y=0
 x>0: y=1

 The convolution of the step edge with the box filter of width s is:
 x < -s/2: y = 0
 x >= -s/2 and x <= s/2: y = 1/2 + x/s
 x > s/2: y = 1
 
 The convolution of the step edge with the triangle filter of width s is:
 x < -s: y = 0
 x >= -s and x <= 0: y = 1/2 + x/s + x^2/(2*s^2)
 x > 0 and x <= s: y = 1/2 + x/s - x^2/(2*s^2)
 x > s: y = 1
 */
// see cimg_library::CImg::boxfilter(), this function has the additional parameter "order" to compute derivatives
static void
boxfilter(CImg<T>& img,
          const float boxsize,
          const int order,
          const char axis = 'x',
          const bool boundary_conditions = true,
          const unsigned int nb_iter = 1)
{
    if ( img.is_empty() || !boxsize || ( (boxsize <= 1.f) && !order ) ) {
        return /* *this*/;
    }
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis = cimg::lowercase(axis); // was cimg::uncase(axis) before CImg 1.7.2
    const float nboxsize = (boxsize >= 0) ? boxsize : -boxsize * (naxis=='x'?_width:
                                                                  naxis=='y'?_height:
                                                                  naxis=='z'?_depth:
                                                                  _spectrum) / 100;
    switch (naxis) {
    case 'x': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forYZC(img, y, z, c)
        _cimg_blur_box_apply(img.data(0, y, z, c), nboxsize, img._width, 1U, order, boundary_conditions, nb_iter);
        break;
    }
    case 'y': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXZC(img, x, z, c)
        _cimg_blur_box_apply(img.data(x, 0, z, c), nboxsize, _height, (unsigned long)_width, order, boundary_conditions, nb_iter);
        break;
    }
    case 'z': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYC(img, x, y, c)
        _cimg_blur_box_apply(img.data(x, y, 0, c), nboxsize, _depth, (unsigned long)(_width * _height),
                             order, boundary_conditions, nb_iter);
        break;
    }
    default: {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYZ(img, x, y, z)
        _cimg_blur_box_apply(img.data(x, y, z, 0), nboxsize, _spectrum, (unsigned long)(_width * _height * _depth),
                             order, boundary_conditions, nb_iter);
    }
    }
    /* *this*/
}
#endif // cimgblur_internal_boxfilter

#ifdef cimgblur_internal_vanvliet
// [internal] Apply a recursive filter (used by CImg<T>::vanvliet()).
/**
   \param ptr the pointer of the data
   \param filter the coefficient of the filter in the following order [n,n-1,n-2,n-3].
   \param N size of the data
   \param off the offset between two data point
   \param order the order of the filter 0 (smoothing), 1st derivtive, 2nd derivative, 3rd derivative
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
   \note dirichlet boundary conditions have a strange behavior. And
   boundary condition should be corrected using Bill Triggs method (IEEE trans on Sig Proc 2005).
 **/
template <int K>
static void
_cimg_recursive_apply(T *data,
                      const double filter[],
                      const int N,
                      const unsigned long off,
                      const int order,
                      const bool boundary_conditions)
{
    double val[K];  // res[n,n-1,n-2,n-3,..] or res[n,n+1,n+2,n+3,..]
    const double
        sumsq = filter[0],
        sum = sumsq * sumsq,
        b1 = filter[1], b2 = filter[2], b3 = filter[3],
        a3 = b3,
        a2 = b2,
        a1 = b1,
        scaleM = 1.0 / ( (1.0 + a1 - a2 + a3) * (1.0 - a1 - a2 - a3) * (1.0 + a2 + (a1 - a3) * a3) );
    double M[9]; // Triggs matrix (for K == 4)

    if (K == 4) {
        M[0] = scaleM * (-a3 * a1 + 1.0 - a3 * a3 - a2);
        M[1] = scaleM * (a3 + a1) * (a2 + a3 * a1);
        M[2] = scaleM * a3 * (a1 + a3 * a2);
        M[3] = scaleM * (a1 + a3 * a2);
        M[4] = -scaleM * (a2 - 1.0) * (a2 + a3 * a1);
        M[5] = -scaleM * a3 * (a3 * a1 + a3 * a3 + a2 - 1.0);
        M[6] = scaleM * (a3 * a1 + a2 + a1 * a1 - a2 * a2);
        M[7] = scaleM * (a1 * a2 + a3 * a2 * a2 - a1 * a3 * a3 - a3 * a3 * a3 - a3 * a2 + a3);
        M[8] = scaleM * a3 * (a1 + a3 * a2);
    }
    switch (order) {
    case 0: {
        const double iplus = (boundary_conditions ? data[(N - 1) * off] : 0);
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 1; k < K; ++k) {
                    val[k] = (boundary_conditions ? *data / sumsq : 0);
                }
            } else {
                /* apply Triggs border condition */
                const double
                    uplus = iplus / (1.0 - a1 - a2 - a3),
                    vplus = uplus / (1.0 - b1 - b2 - b3),
                    p1 = val[1],
                    p2 = val[2],
                    p3 = val[3],
                    unp = p1 - uplus,
                    unp1 = p2 - uplus,
                    unp2 = p3 - uplus;
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2 + vplus) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2 + vplus) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2 + vplus) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N; ++n) {
                val[0] = (*data);
                if (pass) {
                    val[0] *= sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            if (!pass) {
                data -= off;
            }
        }
    }
    break;
    case 1: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off);
                    val[0] = 0.5f * (x[0] - x[2]);
                } else {
                    val[0] = (*data) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                    for (int k = 2; k > 0; --k) {
                        x[k] = x[k - 1];
                    }
                } else {
                    data -= off;
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    case 2: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off); val[0] = (x[1] - x[2]);
                } else                                                            {
                    x[0] = *(data - off); val[0] = (x[2] - x[1]) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = 2; k > 0; --k) {
                    x[k] = x[k - 1];
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    case 3: {
        double x[3];     // [front,center,back]
        for (int pass = 0; pass < 2; ++pass) {
            if ( !pass || ( K != 4) ) {
                for (int k = 0; k < 3; ++k) {
                    x[k] = (boundary_conditions ? *data : 0);
                }
                for (int k = 0; k < K; ++k) {
                    val[k] = 0;
                }
            } else {
                /* apply Triggs border condition */
                const double
                    unp = val[1],
                    unp1 = val[2],
                    unp2 = val[3];
                val[0] = (M[0] * unp + M[1] * unp1 + M[2] * unp2) * sum;
                val[1] = (M[3] * unp + M[4] * unp1 + M[5] * unp2) * sum;
                val[2] = (M[6] * unp + M[7] * unp1 + M[8] * unp2) * sum;
                *data = (T)val[0];
                data -= off;
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            for (int n = (pass && K == 4); n < N - 1; ++n) {
                if (!pass) {
                    x[0] = *(data + off); val[0] = (x[0] - 2 * x[1] + x[2]);
                } else                                                                     {
                    x[0] = *(data - off); val[0] = 0.5f * (x[2] - x[0]) * sum;
                }
                for (int k = 1; k < K; ++k) {
                    val[0] += val[k] * filter[k];
                }
                *data = (T)val[0];
                if (!pass) {
                    data += off;
                } else {
                    data -= off;
                }
                for (int k = 2; k > 0; --k) {
                    x[k] = x[k - 1];
                }
                for (int k = K - 1; k > 0; --k) {
                    val[k] = val[k - 1];
                }
            }
            *data = (T)0;
        }
    }
    break;
    } // switch
} // _cimg_recursive_apply

//! Van Vliet recursive Gaussian filter.
/**
   \param sigma standard deviation of the Gaussian filter
   \param order the order of the filter 0,1,2,3
   \param axis  Axis along which the filter is computed. Can be <tt>{ 'x' | 'y' | 'z' | 'c' }</tt>.
   \param boundary_conditions Boundary conditions. Can be <tt>{ 0=dirichlet | 1=neumann }</tt>.
   \note dirichlet boundary condition has a strange behavior

   I.T. Young, L.J. van Vliet, M. van Ginkel, Recursive Gabor filtering.
   IEEE Trans. Sig. Proc., vol. 50, pp. 2799-2805, 2002.

   (this is an improvement over Young-Van Vliet, Sig. Proc. 44, 1995)

   Boundary conditions (only for order 0) using Triggs matrix, from
   B. Triggs and M. Sdika. Boundary conditions for Young-van Vliet
   recursive filtering. IEEE Trans. Signal Processing,
   vol. 54, pp. 2365-2367, 2006.
 **/
void
vanvliet(CImg<T>& img,
         const float sigma,
         const int order,
         const char axis = 'x',
         const bool boundary_conditions = true)
{
    if ( img.is_empty() ) {
        return /* *this*/;
    }
    const unsigned int _width = img._width, _height = img._height, _depth = img._depth, _spectrum = img._spectrum;
    const char naxis = cimg::uncase(axis);
    const float nsigma = sigma >= 0 ? sigma : -sigma * (naxis == 'x' ? _width : naxis == 'y' ? _height : naxis == 'z' ? _depth : _spectrum) / 100;
    if ( img.is_empty() || ( (nsigma < 0.1f) && !order ) ) {
        return /* *this*/;
    }
    const double
        nnsigma = nsigma < 0.1f ? 0.1f : nsigma,
        m0 = 1.16680, m1 = 1.10783, m2 = 1.40586,
        m1sq = m1 * m1, m2sq = m2 * m2,
        q = ( nnsigma < 3.556 ? -0.2568 + 0.5784 * nnsigma + 0.0561 * nnsigma * nnsigma : 2.5091 + 0.9804 * (nnsigma - 3.556) ),
        qsq = q * q,
        scale = (m0 + q) * (m1sq + m2sq + 2 * m1 * q + qsq),
        b1 = -q * (2 * m0 * m1 + m1sq + m2sq + (2 * m0 + 4 * m1) * q + 3 * qsq) / scale,
        b2 = qsq * (m0 + 2 * m1 + 3 * q) / scale,
        b3 = -qsq * q / scale,
        B = ( m0 * (m1sq + m2sq) ) / scale;
    double filter[4];
    filter[0] = B; filter[1] = -b1; filter[2] = -b2; filter[3] = -b3;
    switch (naxis) {
    case 'x': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forYZC(img, y, z, c)
        _cimg_recursive_apply<4>(img.data(0, y, z, c), filter, img._width, 1U, order, boundary_conditions);
    }
    break;
    case 'y': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXZC(img, x, z, c)
        _cimg_recursive_apply<4>(img.data(x, 0, z, c), filter, _height, (unsigned long)_width, order, boundary_conditions);
    }
    break;
    case 'z': {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYC(img, x, y, c)
        _cimg_recursive_apply<4>(img.data(x, y, 0, c), filter, _depth, (unsigned long)(_width * _height),
                                 order, boundary_conditions);
    }
    break;
    default: {
        cimg_pragma_openmp(parallel for collapse(3) if (_width>=256 && _height*_depth*_spectrum>=16))
        cimg_forXYZ(img, x, y, z)
        _cimg_recursive_apply<4>(img.data(x, y, z, 0), filter, _spectrum, (unsigned long)(_width * _height * _depth),
                                 order, boundary_conditions);
    }
    }
    /* *this*/
} // vanvliet

#endif // cimg_version < 160

/// Blur plugin
struct CImgBlurParams
{
    double sizex, sizey; // sizex takes PixelAspectRatio into account
    double par;
    double erodeSize;
    double erodeBlur;
    double sharpenSoftenAmount;
    int orderX;
    int orderY;
    double bloomRatio;
    int count;
    bool edgeExtendPremult;
    double edgeExtendSize;
    bool edgeExtendUnpremult;
    ColorspaceEnum colorspace;
    int boundary_i;
    FilterEnum filter;
    EdgeDetectFilterEnum edgeDetectFilter;
    EdgeDetectMultiChannelEnum edgeDetectMultiChannel;
    bool edgeDetectNMS;
    bool expandRoD;
    bool cropToFormat;
    double alphaThreshold;
};

enum BlurPluginEnum
{
    eBlurPluginBlur,
    eBlurPluginLaplacian,
    eBlurPluginSharpen,
    eBlurPluginSoften,
    eBlurPluginChromaBlur,
    eBlurPluginBloom,
    eBlurPluginErodeBlur,
    eBlurPluginEdgeExtend,
    eBlurPluginEdgeDetect,
};

/*
The convolution of the step edge with the triangle filter of width s is:
x < -s: y = 0
x >= -s and x <= 0: y = 1/2 + x/s + x^2/(2*s^2)
x > 0 and x <= s: y = 1/2 + x/s - x^2/(2*s^2)
x > s: y = 1
*/
static double
blurredStep(double s, double x)
{
    if (s <= 0) {
        return (x >= 0) ? 1. : 0.;
    }
    if (x <= -s) {
        return 0.;
    }
    if (x >= s) {
        return 1.;
    }
    double x_over_s = x/s;
    return 0.5 + x_over_s * (1. + (x < 0 ? 0.5 : -0.5) * x_over_s);
}

static void
mergeOver(const CImg<cimgpix_t> &cimgA, const CImg<cimgpix_t> &cimgB, CImg<cimgpix_t> &cimgOut)
{
    assert(cimgA.width() == cimgB.width() &&
           cimgA.height() == cimgB.height() &&
           cimgA.depth() == cimgB.depth() &&
           cimgA.spectrum() == cimgB.spectrum() &&
           cimgA.width() == cimgOut.width() &&
           cimgA.width() == cimgOut.width() &&
           cimgA.depth() == cimgOut.depth() &&
           cimgA.spectrum() == cimgOut.spectrum() &&
           cimgA.depth() == 1 &&
           cimgA.spectrum() == 4);

    cimg_pragma_openmp(parallel for collapse(2) if (cimgA.width()>=256 && cimgA.height()>=16))
    cimg_forXY(cimgOut, x, y) {
        cimgpix_t alphaA = cimgA(x, y, 0, 3);
        for (int c = 0; c < 4; ++c) {
            cimgOut(x, y, 0, c) = cimgA(x, y, 0, c) + cimgB(x, y, 0, c) * (1 - alphaA);
        }
    }
}


static void
premult(CImg<cimgpix_t> &cimg)
{
    assert(cimg.depth() == 1 &&
           cimg.spectrum() == 4);

    cimg_pragma_openmp(parallel for collapse(2) if (cimg.width()>=256 && cimg.height()>=16))
    cimg_forXY(cimg, x, y) {
        cimgpix_t alpha = cimg(x, y, 0, 3);
        for (int c = 0; c < 3; ++c) {
            cimg(x, y, 0, c) *= alpha;
        }
    }
}

static void
unpremult(CImg<cimgpix_t> &cimg)
{
    assert(cimg.depth() == 1 &&
           cimg.spectrum() == 4);

    cimg_pragma_openmp(parallel for collapse(2) if (cimg.width()>=256 && cimg.height()>=16))
    cimg_forXY(cimg, x, y) {
        cimgpix_t alpha = cimg(x, y, 0, 3);
        if (alpha > FLT_EPSILON) {
            for (int c = 0; c < 3; ++c) {
                cimg(x, y, 0, c) /= alpha;
            }
        }
    }
}


class CImgBlurPlugin
    : public CImgFilterPluginHelper<CImgBlurParams, false>
{
public:

    CImgBlurPlugin(OfxImageEffectHandle handle,
                   BlurPluginEnum blurPlugin = eBlurPluginBlur)
        : CImgFilterPluginHelper<CImgBlurParams, false>(handle, /*usesMask=*/false,
                                                        blurPlugin == eBlurPluginChromaBlur ? false : kSupportsComponentRemapping,
                                                        kSupportsTiles,
                                                        kSupportsMultiResolution,
                                                        kSupportsRenderScale,
                                                        kDefaultUnpremult)
        , _blurPlugin(blurPlugin)
        , _sharpenSoftenAmount(NULL)
        , _size(NULL)
        , _erodeSize(NULL)
        , _erodeBlur(NULL)
        , _uniform(NULL)
        , _orderX(NULL)
        , _orderY(NULL)
        , _bloomRatio(NULL)
        , _bloomCount(NULL)
        , _edgeExtendPremult(NULL)
        , _edgeExtendSize(NULL)
        , _edgeExtendCount(NULL)
        , _edgeExtendUnpremult(NULL)
        , _colorspace(NULL)
        , _boundary(NULL)
        , _filter(NULL)
        , _edgeDetectFilter(NULL)
        , _edgeDetectMultiChannel(NULL)
        , _edgeDetectBlur(NULL)
        , _edgeDetectErode(NULL)
        , _edgeDetectNMS(NULL)
        , _expandRoD(NULL)
        , _cropToFormat(NULL)
        , _alphaThreshold(NULL)
    {
        if (_blurPlugin == eBlurPluginSharpen ||
            _blurPlugin == eBlurPluginSoften) {
            _sharpenSoftenAmount = fetchDoubleParam(kParamSharpenSoftenAmount);
        } else {
            assert( !paramExists(kParamSharpenSoftenAmount) );
        }
        if (_blurPlugin == eBlurPluginErodeBlur) {
            _erodeSize  = fetchDoubleParam(kParamErodeSize);
            _erodeBlur  = fetchDoubleParam(kParamErodeBlur);
        } else {
            // kParamErodeSize and kParamSize have the same value
            assert( /*!paramExists(kParamErodeSize) &&*/ !paramExists(kParamErodeBlur) );
        }
        if (blurPlugin == eBlurPluginEdgeExtend) {
            _edgeExtendPremult = fetchBooleanParam(kParamEdgeExtendPremult);
            _edgeExtendSize = fetchDoubleParam(kParamEdgeExtendSize);
            _edgeExtendCount = fetchIntParam(kParamEdgeExtendCount);
            _edgeExtendUnpremult = fetchBooleanParam(kParamEdgeExtendUnpremult);
            assert(_edgeExtendSize && _edgeExtendCount);
        } else {
            assert( !paramExists(kParamEdgeExtendPremult) && !paramExists(kParamEdgeExtendSize) && !paramExists(kParamEdgeExtendCount) && !paramExists(kParamEdgeExtendUnpremult) );
        }
        if (blurPlugin == eBlurPluginBlur ||
            blurPlugin == eBlurPluginLaplacian ||
            blurPlugin == eBlurPluginSharpen ||
            blurPlugin == eBlurPluginSoften ||
            blurPlugin == eBlurPluginChromaBlur ||
            blurPlugin == eBlurPluginBloom) { // not eBlurPluginErodeBlur, eBlurPluginEdgeExtend, eBlurPluginEdgeExtend, eBlurPluginEdgeDetect
            _size  = fetchDouble2DParam(kParamSize);
            _uniform = fetchBooleanParam(kParamUniform);
            assert(_size && _uniform);
        } else {
            // kParamErodeSize and kParamSize have the same value
            assert( /*!paramExists(kParamSize) &&*/ !paramExists(kParamUniform) );
        }
        if (blurPlugin == eBlurPluginBlur) {
            _orderX = fetchIntParam(kParamOrderX);
            _orderY = fetchIntParam(kParamOrderY);
            assert(_orderX && _orderY);
        } else {
            assert( !paramExists(kParamOrderX) && !paramExists(kParamOrderY) );
        }
        if (blurPlugin == eBlurPluginBloom) {
            _bloomRatio = fetchDoubleParam(kParamBloomRatio);
            _bloomCount = fetchIntParam(kParamBloomCount);
            assert(_bloomRatio && _bloomCount);
        } else {
            assert(!paramExists(kParamBloomRatio) && !paramExists(kParamBloomCount) );
        }
        if (blurPlugin == eBlurPluginChromaBlur) {
            _colorspace = fetchChoiceParam(kParamColorspace);
            assert(_colorspace);
        } else {
            assert( !paramExists(kParamColorspace) );
        }
        if (_blurPlugin == eBlurPluginBlur ||
            _blurPlugin == eBlurPluginBloom) {
            _boundary  = fetchChoiceParam(kParamBoundary);
            assert(_boundary);
        } else {
            assert( !paramExists(kParamBoundary) );
        }
        if (_blurPlugin == eBlurPluginEdgeDetect) {
            _edgeDetectFilter = fetchChoiceParam(kParamEdgeDetectFilter);
            _edgeDetectMultiChannel = fetchChoiceParam(kParamEdgeDetectMultiChannel);
            _edgeDetectBlur = fetchDoubleParam(kParamEdgeDetectBlur);
            _edgeDetectErode = fetchDoubleParam(kParamEdgeDetectErode);
            _edgeDetectNMS = fetchBooleanParam(kParamEdgeDetectNMS);
            assert(_edgeDetectFilter && _edgeDetectMultiChannel && _edgeDetectBlur && _edgeDetectErode && _edgeDetectNMS);
        } else {
            // kParamFilter and kParamEdgeDetectFilter have the same value
            assert( /*!paramExists(kParamEdgeDetectFilter) &&*/ !paramExists(kParamEdgeDetectMultiChannel) && !paramExists(kParamEdgeDetectBlur) && !paramExists(kParamEdgeDetectErode) && !paramExists(kParamEdgeDetectNMS) );
        }
        if (_blurPlugin == eBlurPluginBlur ||
            _blurPlugin == eBlurPluginLaplacian ||
            _blurPlugin == eBlurPluginSharpen ||
            _blurPlugin == eBlurPluginSoften ||
            _blurPlugin == eBlurPluginChromaBlur ||
            _blurPlugin == eBlurPluginBloom ||
            _blurPlugin == eBlurPluginEdgeExtend) {
            _filter = fetchChoiceParam(kParamFilter);
            assert(_filter);
        } else {
            // kParamFilter and kParamEdgeDetectFilter have the same value
            assert( (_blurPlugin == eBlurPluginEdgeDetect) || !paramExists(kParamFilter) );
        }
        if (_blurPlugin == eBlurPluginBlur ||
            _blurPlugin == eBlurPluginBloom ||
            _blurPlugin == eBlurPluginErodeBlur ||
            _blurPlugin == eBlurPluginEdgeExtend ||
            _blurPlugin == eBlurPluginEdgeDetect) {
            _expandRoD = fetchBooleanParam(kParamExpandRoD);
            assert(_expandRoD);
        } else {
            assert( !paramExists(kParamExpandRoD) );
        }
        if ( paramExists(kParamCropToFormat) ) {
            _cropToFormat = fetchBooleanParam(kParamCropToFormat);
            assert(_cropToFormat);
        } else {
            assert( !paramExists(kParamCropToFormat) );
        }
        if (blurPlugin == eBlurPluginBlur || blurPlugin == eBlurPluginBloom) {
            _alphaThreshold = fetchDoubleParam(kParamAlphaThreshold);
            assert(_alphaThreshold);
        } else {
            assert( !paramExists(kParamAlphaThreshold) );
        }
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             _uniform &&
             !_uniform->getValue() &&
             ( _uniform->getNumKeys() == 0) ) {
            _uniform->setIsSecretAndDisabled(true);
        }
    }

    virtual void getValuesAtTime(double time,
                                 CImgBlurParams& params) OVERRIDE FINAL
    {
        assert(_blurPlugin == eBlurPluginBlur ||
               _blurPlugin == eBlurPluginLaplacian ||
               _blurPlugin == eBlurPluginSharpen ||
               _blurPlugin == eBlurPluginSoften ||
               _blurPlugin == eBlurPluginChromaBlur ||
               _blurPlugin == eBlurPluginBloom ||
               _blurPlugin == eBlurPluginErodeBlur ||
               _blurPlugin == eBlurPluginEdgeExtend ||
               _blurPlugin == eBlurPluginEdgeDetect);

        if (_blurPlugin == eBlurPluginSharpen) {
            params.sharpenSoftenAmount = _sharpenSoftenAmount->getValueAtTime(time);
        } else if (_blurPlugin == eBlurPluginSoften) {
            params.sharpenSoftenAmount = -_sharpenSoftenAmount->getValueAtTime(time);
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeExtend ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            assert(!_sharpenSoftenAmount);
            params.sharpenSoftenAmount = 0.;
        }

        if (_blurPlugin == eBlurPluginErodeBlur) {
            params.erodeSize = _erodeSize->getValueAtTime(time);
            params.erodeBlur = _erodeBlur->getValueAtTime(time);
            assert(!_size && !_uniform);
            params.sizey = params.sizex = 2 * std::abs(params.erodeSize);
            assert(!_edgeExtendPremult && !_edgeExtendSize);
            params.edgeExtendPremult = false;
            params.edgeExtendUnpremult = false;
        } else if (_blurPlugin == eBlurPluginEdgeDetect) {
            assert(!_erodeSize && !_erodeBlur);
            params.erodeSize = _edgeDetectErode->getValueAtTime(time);
            params.erodeBlur = 0.;
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginEdgeExtend);
            assert(!_erodeSize && !_erodeBlur);
            params.erodeSize = 0.;
            params.erodeBlur = 0.;
        }
        if (_blurPlugin == eBlurPluginEdgeExtend) {
            params.edgeExtendPremult = _edgeExtendPremult->getValueAtTime(time);
            params.edgeExtendSize = (std::max)( 0., _edgeExtendSize->getValueAtTime(time) );
            params.edgeExtendUnpremult = _edgeExtendUnpremult->getValueAtTime(time);
            params.count = (std::max)( 1, _edgeExtendCount->getValueAtTime(time) );
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            assert(!_edgeExtendPremult && !_edgeExtendSize);
            params.edgeExtendPremult = false;
            params.edgeExtendSize = 0.;
            params.edgeExtendUnpremult = false;
        }

        if (_blurPlugin == eBlurPluginEdgeExtend) {
            params.sizey = params.sizex = params.edgeExtendSize; // used for RoD/RoI/identity
        } else if (_blurPlugin == eBlurPluginEdgeDetect) {
            params.sizey = params.sizex = _edgeDetectBlur->getValueAtTime(time);
        } else if (_blurPlugin != eBlurPluginErodeBlur) {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom);

            _size->getValueAtTime(time, params.sizex, params.sizey);
            bool uniform = _uniform->getValueAtTime(time);
            if (uniform) {
                params.sizey = params.sizex;
            }
        }
        params.par = (_srcClip && _srcClip->isConnected()) ? _srcClip->getPixelAspectRatio() : 0.;
        if (params.par != 0.) {
            params.sizex /= params.par;
        }
        if (_blurPlugin == eBlurPluginBlur) {
            params.orderX = (std::max)( 0, _orderX->getValueAtTime(time) );
            params.orderY = (std::max)( 0, _orderY->getValueAtTime(time) );
        } else {
            assert(_blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeExtend ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            assert(!_orderX && !_orderY);
            params.orderX = params.orderY = 0;
        }

        if (_blurPlugin == eBlurPluginBloom) {
            params.bloomRatio = _bloomRatio->getValueAtTime(time);
            params.count = (std::max)( 1, _bloomCount->getValueAtTime(time) );
            if (params.bloomRatio <= 1.) {
                params.count = 1;
            }
            if (params.count == 1) {
                params.bloomRatio = 1.;
            }
        } else if (_blurPlugin == eBlurPluginEdgeExtend) {
            assert(!_bloomRatio && !_bloomCount);
            params.bloomRatio = 1.;
            //params.count = ; // defined above
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            assert(!_bloomRatio && !_bloomCount);
            params.bloomRatio = 1.;
            params.count = 1;
        }
        if (_blurPlugin == eBlurPluginChromaBlur) {
            params.colorspace = (ColorspaceEnum)_colorspace->getValueAtTime(time);
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeExtend ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            params.colorspace = eColorspaceRec709;
        }
        if (_blurPlugin == eBlurPluginBlur ||
            _blurPlugin == eBlurPluginBloom) {
            params.boundary_i = _boundary->getValueAtTime(time);
        } else if (_blurPlugin == eBlurPluginErodeBlur) {
            assert(!_boundary);
            params.boundary_i = 0; // black
        } else {
            assert(_blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginEdgeExtend ||
                   _blurPlugin == eBlurPluginEdgeDetect);
            assert(!_boundary);
            params.boundary_i = 1; // nearest
        }
        if (_blurPlugin == eBlurPluginEdgeDetect) {
            params.edgeDetectFilter = (EdgeDetectFilterEnum)_edgeDetectFilter->getValueAtTime(time);
            // set params.orderX, orderY, filter for RoD computation
            assert(!_orderX && !_orderY);
            params.orderX = params.orderY = 1;
            assert(!_filter);
            switch (params.edgeDetectFilter) {
                case eEdgeDetectFilterSimple:
                case eEdgeDetectFilterSobel:
                case eEdgeDetectFilterRotationInvariant:
                    params.filter = eFilterGaussian;
                    break;
                case eEdgeDetectFilterQuasiGaussian:
                    params.filter = eFilterQuasiGaussian;
                    break;
                case eEdgeDetectFilterGaussian:
                    params.filter = eFilterGaussian;
                    break;
                case eEdgeDetectFilterBox:
                    params.filter = eFilterBox;
                    break;
                case eEdgeDetectFilterTriangle:
                    params.filter = eFilterTriangle;
                    break;
                case eEdgeDetectFilterQuadratic:
                    params.filter = eFilterQuadratic;
                    break;
            }
        } else if (_blurPlugin == eBlurPluginErodeBlur) {
            assert(!_filter);
            params.filter = eFilterTriangle;
            assert(!_edgeDetectFilter && !_edgeDetectMultiChannel);
            params.edgeDetectFilter = eEdgeDetectFilterSimple;
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginEdgeExtend);

            params.filter = (FilterEnum)_filter->getValueAtTime(time);
            assert(!_edgeDetectFilter && !_edgeDetectMultiChannel);
            params.edgeDetectFilter = eEdgeDetectFilterSimple;
            params.edgeDetectMultiChannel = eEdgeDetectMultiChannelSeparate;
        }
        if (_blurPlugin == eBlurPluginBlur ||
            _blurPlugin == eBlurPluginBloom ||
            _blurPlugin == eBlurPluginErodeBlur ||
            _blurPlugin == eBlurPluginEdgeExtend ||
            _blurPlugin == eBlurPluginEdgeDetect) {
            params.expandRoD = _expandRoD->getValueAtTime(time);
        } else if (_blurPlugin == eBlurPluginErodeBlur) {
            params.expandRoD = true;
        } else {
            assert(_blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur);
            params.expandRoD = false;
        }
        if (_blurPlugin == eBlurPluginEdgeDetect) {
            params.edgeDetectMultiChannel = (EdgeDetectMultiChannelEnum)_edgeDetectMultiChannel->getValueAtTime(time);
            // _edgeDetectErode already used as params.erodeSize
            // _edgeDetectBlur already used for params.sizex/y
            params.edgeDetectNMS = _edgeDetectNMS->getValueAtTime(time);
        } else {
            assert(_blurPlugin == eBlurPluginBlur ||
                   _blurPlugin == eBlurPluginLaplacian ||
                   _blurPlugin == eBlurPluginSharpen ||
                   _blurPlugin == eBlurPluginSoften ||
                   _blurPlugin == eBlurPluginChromaBlur ||
                   _blurPlugin == eBlurPluginBloom ||
                   _blurPlugin == eBlurPluginErodeBlur ||
                   _blurPlugin == eBlurPluginEdgeExtend);
            params.edgeDetectMultiChannel = eEdgeDetectMultiChannelSeparate;
            params.edgeDetectNMS = false;
        }

        params.cropToFormat = _cropToFormat ? _cropToFormat->getValueAtTime(time) : false;
        params.alphaThreshold = _alphaThreshold ? _alphaThreshold->getValueAtTime(time) : 0.;
    }

    bool getRegionOfDefinition(const OfxRectI& srcRoD,
                               const OfxPointD& renderScale,
                               const CImgBlurParams& params,
                               OfxRectI* dstRoD) OVERRIDE FINAL
    {
        double sx = renderScale.x * params.sizex;
        double sy = renderScale.y * params.sizey;

        bool retval = false;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            double scale = ipow( params.bloomRatio, (params.count - 1) );
            sx *= scale;
            sy *= scale;
        }
        *dstRoD = srcRoD;
        if ( params.expandRoD && !Coords::rectIsEmpty(srcRoD) ) {
            if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
                float sigmax = (float)(sx / 2.4);
                float sigmay = (float)(sy / 2.4);
                if ( (sigmax < 0.1) && (sigmay < 0.1) && (params.orderX == 0) && (params.orderY == 0) ) {
                    return false; // identity
                }
                int delta_pixX = (std::max)( 3, (int)std::ceil(sx * 1.5) );
                int delta_pixY = (std::max)( 3, (int)std::ceil(sy * 1.5) );
                dstRoD->x1 -= delta_pixX + params.orderX;
                dstRoD->x2 += delta_pixX + params.orderX;
                dstRoD->y1 -= delta_pixY + params.orderY;
                dstRoD->y2 += delta_pixY + params.orderY;
            } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
                if ( (sx <= 1) && (sy <= 1) && (params.orderX == 0) && (params.orderY == 0) ) {
                    return false; // identity
                }
                int iter = ( params.filter == eFilterBox ? 1 :
                             (params.filter == eFilterTriangle ? 2 : 3) );
                int delta_pixX = iter * (int)std::ceil( (sx - 1) / 2 );
                int delta_pixY = iter * (int)std::ceil( (sy - 1) / 2 );
                dstRoD->x1 -= delta_pixX + (params.orderX > 0);
                dstRoD->x2 += delta_pixX + (params.orderX > 0);
                dstRoD->y1 -= delta_pixY + (params.orderY > 0);
                dstRoD->y2 += delta_pixY + (params.orderY > 0);
            } else {
                assert(false);
            }

            retval = true;
        }
#ifdef OFX_EXTENSIONS_NATRON
        if ( params.cropToFormat ) {
            // crop to format works by clamping the output rod to the format wherever the input rod was inside the format
            // this avoids unwanted crops (cropToFormat is checked by default)
            OfxRectI srcFormat;
            _srcClip->getFormat(srcFormat);
            OfxRectI srcFormatEnclosing;
            srcFormatEnclosing.x1 = std::floor(srcFormat.x1 * renderScale.x);
            srcFormatEnclosing.x2 = std::ceil(srcFormat.x2 * renderScale.x);
            srcFormatEnclosing.y1 = std::floor(srcFormat.y1 * renderScale.y);
            srcFormatEnclosing.y2 = std::ceil(srcFormat.y2 * renderScale.y);
            srcFormat.x1 = std::ceil(srcFormat.x1 * renderScale.x);
            srcFormat.x2 = std::floor(srcFormat.x2 * renderScale.x);
            srcFormat.y1 = std::ceil(srcFormat.y1 * renderScale.y);
            srcFormat.y2 = std::floor(srcFormat.y2 * renderScale.y);
            if ( !Coords::rectIsEmpty(srcFormat) ) {
                if (dstRoD->x1 < srcFormat.x1 && srcRoD.x1 >= srcFormatEnclosing.x1) {
                    dstRoD->x1 = srcFormat.x1;
                    retval = true;
                }
                if (dstRoD->x2 > srcFormat.x2 && srcRoD.x2 <= srcFormatEnclosing.x2) {
                    dstRoD->x2 = srcFormat.x2;
                    retval = true;
                }
                if (dstRoD->y1 < srcFormat.y1 && srcRoD.y1 >= srcFormatEnclosing.y1) {
                    dstRoD->y1 = srcFormat.y1;
                    retval = true;
                }
                if (dstRoD->y2 > srcFormat.y2 && srcRoD.y2 <= srcFormatEnclosing.y2) {
                    dstRoD->y2 = srcFormat.y2;
                    retval = true;
                }
            }
        }
#endif

        return retval;
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI& rect,
                        const OfxPointD& renderScale,
                        const CImgBlurParams& params,
                        OfxRectI* roi) OVERRIDE FINAL
    {
        double sx = renderScale.x * params.sizex;
        double sy = renderScale.y * params.sizey;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            // note: for eBlurPluginEdgeExtend, the largest kernel is the first one
            double scale = ipow( params.bloomRatio, (params.count - 1) );
            sx *= scale;
            sy *= scale;
        }
        if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
            float sigmax = (float)(sx / 2.4);
            float sigmay = (float)(sy / 2.4);
            if ( (sigmax < 0.1) && (sigmay < 0.1) && (params.orderX == 0) && (params.orderY == 0) ) {
                *roi = rect;

                return;
            }

            int delta_pixX = (std::max)( 3, (int)std::ceil(sx * 1.5) );
            int delta_pixY = (std::max)( 3, (int)std::ceil(sy * 1.5) );
            roi->x1 = rect.x1 - delta_pixX - params.orderX;
            roi->x2 = rect.x2 + delta_pixX + params.orderX;
            roi->y1 = rect.y1 - delta_pixY - params.orderY;
            roi->y2 = rect.y2 + delta_pixY + params.orderY;
        } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
            int iter = ( params.filter == eFilterBox ? 1 :
                         (params.filter == eFilterTriangle ? 2 : 3) );
            int delta_pixX = iter * (std::floor( (sx - 1) / 2 ) + 1);
            int delta_pixY = iter * (std::floor( (sy - 1) / 2 ) + 1);
            roi->x1 = rect.x1 - delta_pixX - (params.orderX > 0);
            roi->x2 = rect.x2 + delta_pixX + (params.orderX > 0);
            roi->y1 = rect.y1 - delta_pixY - (params.orderY > 0);
            roi->y2 = rect.y2 + delta_pixY + (params.orderY > 0);
        } else {
            assert(false);
        }
    }

    virtual void render(const RenderArguments &args,
                        const CImgBlurParams& params,
                        int /*x1*/,
                        int /*y1*/,
                        cimg_library::CImg<cimgpix_t>& /*mask*/,
                        cimg_library::CImg<cimgpix_t>& cimg,
                        int alphaChannel) OVERRIDE FINAL
    {
        //printf("blur render %g %dx%d+%d+%d (%dx%d)\n", args.time, args.renderWindow.x2-args.renderWindow.x1, args.renderWindow.y2-args.renderWindow.y1, args.renderWindow.x1, args.renderWindow.y1, cimg.width(), cimg.height());

        // eBlurPluginEdgeExtend only works on RGBA
        assert( !(_blurPlugin == eBlurPluginEdgeExtend && cimg.spectrum() != 4) );

        if (_blurPlugin == eBlurPluginEdgeDetect) {
            // EdgeDetect has its own render function
            return renderEdgeDetect(args, params, cimg);
        }
       // PROCESSING.
        // This is the only place where the actual processing takes place
        double sx = args.renderScale.x * params.sizex;
        double sy = args.renderScale.y * params.sizey;
        double sxBase = sx;
        double syBase = sy;
        if (_blurPlugin == eBlurPluginEdgeExtend) {
            // compute first size and the size ratio
            // if we smooth with kernels s, 2s, 3s, 4s, 5s … ns, where n is the number of slices
            //    the resulting variance is s^2*(1+2^2 + 3^2 + 4^2 + … + n^2) = s^2(n*(n+1)*(n+0.5)/3)
            //    the resulting sigma is thus: s * sqrt(n*(n+1)*(n+0.5)/3) = s_total
            //    so s = s_total/sqrt(n*(n+1)*(n+0.5)/3)
            //    for example, for 10 slices, s = s_total/19.62141687
            const int n = params.count;
            double sRatio = 1. / std::sqrt(n*(n+1)*(n+0.5)/3);
            sxBase = sx * sRatio; // sx is the total size (see getValuesAtTime())
            syBase = sy * sRatio;
        }
        double t0 = 0.;
        double t1 = 0.;
        if (_blurPlugin == eBlurPluginErodeBlur) {
            //t0 = blurredStep( 2 * std::abs(params.erodeSize), (params.erodeSize * ((params.erodeSize > 0) ? (1 - params.erodeBlur) : 1) - 0.5) );
            t0 = blurredStep( 2 * std::abs(params.erodeSize), (params.erodeSize - 0.5) * ((params.erodeSize > 0) ? (1 - params.erodeBlur) : 1) );
            //t1 = blurredStep( 2 * std::abs(params.erodeSize), (params.erodeSize * ((params.erodeSize < 0) ? (1 - params.erodeBlur) : 1) + 0.5) );
            t1 = blurredStep( 2 * std::abs(params.erodeSize), (params.erodeSize + 0.5) * ((params.erodeSize < 0) ? (1 - params.erodeBlur) : 1) );
        }
        //std::cout << "renderScale=" << args.renderScale.x << ',' << args.renderScale.y << std::endl;
        //std::cout << "renderWindow=" << args.renderWindow.x1 << ',' << args.renderWindow.y1 << ',' << args.renderWindow.x2 << ',' << args.renderWindow.y2 << std::endl;
        //std::cout << "cimg=" << cimg.width() << ',' << cimg.height() << std::endl;
        if (_blurPlugin == eBlurPluginEdgeExtend && params.edgeExtendPremult) {
            premult(cimg);
        }
        CImg<cimgpix_t> cimg0;
        CImg<cimgpix_t> cimg1;
        if (_blurPlugin == eBlurPluginLaplacian || _blurPlugin == eBlurPluginSharpen || _blurPlugin == eBlurPluginSoften) {
            cimg0 = cimg;
        } else if (_blurPlugin == eBlurPluginChromaBlur) {
            // ChromaBlur only supports RGBA and RGBA, and components cannot be remapped
            assert(cimg.spectrum() >= 3);
            // allocate chrominance image
            cimg0.resize(cimg.width(), cimg.height(), cimg.depth(), 2);
            // we use the CIE xyZ colorspace to separate luminance from chrominance
            // chrominance (x+y) goes into cimg0, luminance goes into first channel of cimg
            cimgpix_t *pr = &cimg(0, 0, 0, 0);
            const cimgpix_t *pg = &cimg(0, 0, 0, 1);
            const cimgpix_t *pb = &cimg(0, 0, 0, 2);
            cimgpix_t *px = &cimg0(0, 0, 0, 0), *py = &cimg0(0, 0, 0, 1);
            switch (params.colorspace) {
                case eColorspaceRec709: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float X, Y, Z;
                        Color::rgb709_to_xyz(*pr, *pg, *pb, &X, &Y, &Z);
                        float XYZ = X + Y + Z;
                        float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        *px = X * invXYZ;
                        *pr = Y;
                        *py = Y * invXYZ;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceRec2020: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float X, Y, Z;
                        Color::rgb2020_to_xyz(*pr, *pg, *pb, &X, &Y, &Z);
                        float XYZ = X + Y + Z;
                        float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        *px = X * invXYZ;
                        *pr = Y;
                        *py = Y * invXYZ;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceACESAP0: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float X, Y, Z;
                        Color::rgbACESAP0_to_xyz(*pr, *pg, *pb, &X, &Y, &Z);
                        float XYZ = X + Y + Z;
                        float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        *px = X * invXYZ;
                        *pr = Y;
                        *py = Y * invXYZ;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceACESAP1: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float X, Y, Z;
                        Color::rgbACESAP1_to_xyz(*pr, *pg, *pb, &X, &Y, &Z);
                        float XYZ = X + Y + Z;
                        float invXYZ = XYZ <= 0 ? 0. : (1. / XYZ);
                        *px = X * invXYZ;
                        *pr = Y;
                        *py = Y * invXYZ;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
            }
        } else if (_blurPlugin == eBlurPluginBloom) {
            // allocate a zero-valued result image to store the sum (eBlurPluginBloom)
            cimg1.assign(cimg.width(), cimg.height(), cimg.depth(), cimg.spectrum(), 0.);
        }

        // the loop is used only for eBlurPluginBloom and eBlurPluginEdgeExtend, other filters only do one iteration
        for (int i = 0; i < params.count; ++i) {
            if ( abort() ) { return; }
            if (_blurPlugin == eBlurPluginBloom ||
                _blurPlugin == eBlurPluginEdgeExtend) {
                // copy original image (bloom) or previous image (edgeextend)
                cimg0 = cimg;
            }
            if (_blurPlugin == eBlurPluginEdgeExtend) {
                // compute blur size
                sx = sxBase * (i+1);
                sy = syBase * (i+1);
            }
            cimg_library::CImg<cimgpix_t>& cimg_blur = (_blurPlugin == eBlurPluginChromaBlur ||
                                                        _blurPlugin == eBlurPluginBloom ||
                                                        _blurPlugin == eBlurPluginEdgeExtend) ? cimg0 : cimg;
            double scale = 1.;
            if (_blurPlugin == eBlurPluginBloom) {
                scale = ipow(params.bloomRatio, i);
            }

            bool blurred = blur(params.filter,
                                sx, params.orderX,
                                sy, params.orderY,
                                scale, (bool)params.boundary_i,
                                cimg_blur);

            if ( !blurred && (_blurPlugin != eBlurPluginBloom) && (_blurPlugin != eBlurPluginEdgeExtend) && (_blurPlugin != eBlurPluginLaplacian) ) {
                return;
            }

            if (_blurPlugin == eBlurPluginBloom) {
                // accumulate result
                cimg1 += cimg0;
            }
            if (_blurPlugin == eBlurPluginEdgeExtend) {
                // merge blurred image over the result
                // cimg (previous result) over cimg0 (blurred) -> cimg
                mergeOver(cimg, cimg0, cimg);
            }
        }

        if (_blurPlugin == eBlurPluginLaplacian) {
            cimg *= -1;
            cimg += cimg0;
        } else if (_blurPlugin == eBlurPluginSharpen || _blurPlugin == eBlurPluginSoften) {
            cimg *= -params.sharpenSoftenAmount;
            cimg0 *= 1. + params.sharpenSoftenAmount;
            cimg += cimg0;
        } else if (_blurPlugin == eBlurPluginChromaBlur) {
            // recombine luminance in cimg0 & chrominance in cimg to cimg
            // chrominance (U+V) is in cimg0, luminance is in first channel of cimg
            cimgpix_t *pr = &cimg(0, 0, 0, 0);
            cimgpix_t *pg = &cimg(0, 0, 0, 1);
            cimgpix_t *pb = &cimg(0, 0, 0, 2);
            const cimgpix_t *px = &cimg0(0, 0, 0, 0);
            const cimgpix_t *py = &cimg0(0, 0, 0, 1);
            switch (params.colorspace) {
                case eColorspaceRec709: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float Y = *pr;
                        float X = *px * Y / *py;
                        float Z = (1. - *px - *py) * Y / *py;
                        float r, g, b;
                        Color::xyz_to_rgb709(X, Y, Z, &r, &g, &b);
                        *pr = r;
                        *pg = g;
                        *pb = b;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceRec2020: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float Y = *pr;
                        float X = *px * Y / *py;
                        float Z = (1. - *px - *py) * Y / *py;
                        float r, g, b;
                        Color::xyz_to_rgb2020(X, Y, Z, &r, &g, &b);
                        *pr = r;
                        *pg = g;
                        *pb = b;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceACESAP0: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float Y = *pr;
                        float X = *px * Y / *py;
                        float Z = (1. - *px - *py) * Y / *py;
                        float r, g, b;
                        Color::xyz_to_rgbACESAP0(X, Y, Z, &r, &g, &b);
                        *pr = r;
                        *pg = g;
                        *pb = b;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
                case eColorspaceACESAP1: {
                    for (unsigned long N = (unsigned long)cimg.width() * cimg.height() * cimg.depth(); N; --N) {
                        float Y = *pr;
                        float X = *px * Y / *py;
                        float Z = (1. - *px - *py) * Y / *py;
                        float r, g, b;
                        Color::xyz_to_rgbACESAP1(X, Y, Z, &r, &g, &b);
                        *pr = r;
                        *pg = g;
                        *pb = b;
                        ++pr;
                        ++pg;
                        ++pb;
                        ++px;
                        ++py;
                    }
                    break;
                }
            }
        } else if (_blurPlugin == eBlurPluginBloom) {
            cimg = cimg1 / params.count;
        } else if (_blurPlugin == eBlurPluginErodeBlur) {
            /*
            The convolution of the step edge with the triangle filter of width s is:
            x < -s: y = 0
            x >= -s and x <= 0: y = 1/2 + x/s + x^2/(2*s^2)
            x > 0 and x <= s: y = 1/2 + x/s - x^2/(2*s^2)
            x > s: y = 1

            ErodeBlur works by rescaling and claping the image blurred with a triangle filter of width 2*abs(erodeSize) using two threashold:
             - the first threshold is the value of the blurred step edge at erodeSize - 0.5, which is mapped to 0
             - the second threshold is the value of the blurred step edge at erodeSize + 0.5, which is mapped to 1
             */
            cimg_for(cimg, ptr, float) {
                *ptr = (*ptr < t0) ? 0. : ((*ptr > t1) ? 1. : (*ptr - t0) / (t1 - t0) );
            }
        }
        if (params.alphaThreshold > 0. && alphaChannel != -1) {
            cimg_forXY(cimg, x, y) {
                float& alpha = cimg(x, y, 0, alphaChannel);
                if (alpha < params.alphaThreshold) {
                    alpha = 0.f;
                }
            }
        }
        if (_blurPlugin == eBlurPluginEdgeExtend && params.edgeExtendUnpremult) {
            unpremult(cimg);
        }
    } // render

    virtual bool isIdentity(const IsIdentityArguments &args,
                            const CImgBlurParams& params) OVERRIDE FINAL
    {
        if (_blurPlugin == eBlurPluginLaplacian) {
            return false;
        }
        if ( (_blurPlugin == eBlurPluginSharpen || _blurPlugin == eBlurPluginSoften) && params.sharpenSoftenAmount == 0. ) {
            return true;
        }
        if (params.edgeExtendPremult != params.edgeExtendUnpremult) {
            return false;
        }

        double sx, sy;
        sx = args.renderScale.x * params.sizex;
        sy = args.renderScale.y * params.sizey;

        if (_blurPlugin == eBlurPluginBloom) {
            // size of the largest blur kernel
            double scale = ipow( params.bloomRatio, (params.count - 1) );
            sx *= scale;
            sy *= scale;
        }
        bool ret = false;
        if ( (params.filter == eFilterQuasiGaussian) || (params.filter == eFilterGaussian) ) {
            float sigmax = (float)(sx / 2.4);
            float sigmay = (float)(sy / 2.4);
            ret = (sigmax < 0.1 && sigmay < 0.1 && params.orderX == 0 && params.orderY == 0);
        } else if ( (params.filter == eFilterBox) || (params.filter == eFilterTriangle) || (params.filter == eFilterQuadratic) ) {
            ret = (sx <= 1 && sy <= 1 && params.orderX == 0 && params.orderY == 0);
        } else {
            assert(false);
        }

        return ret;
    };

    // 0: Black/Dirichlet, 1: Nearest/Neumann, 2: Repeat/Periodic
    virtual int getBoundary(const CImgBlurParams& params)  OVERRIDE FINAL { return params.boundary_i; }

    // describe function for plugin factories
    static void describe(ImageEffectDescriptor& desc, int majorVersion, int minorVersion, BlurPluginEnum blurPlugin = eBlurPluginBlur);

    // describeInContext function for plugin factories
    static void describeInContext(ImageEffectDescriptor& desc, ContextEnum context, int majorVersion, int minorVersion, BlurPluginEnum blurPlugin = eBlurPluginBlur);

    // returns true if the image was actually modified
    bool blur(FilterEnum filter,
              double sx, int orderX,
              double sy, int orderY,
              double scale, bool boundary,
              cimg_library::CImg<cimgpix_t>& cimg_blur)
    {
        if ( (filter == eFilterQuasiGaussian) || (filter == eFilterGaussian) ) {
            float sigmax = (float)(sx * scale / 2.4);
            float sigmay = (float)(sy * scale / 2.4);
            if ( (_blurPlugin != eBlurPluginBloom) && (_blurPlugin != eBlurPluginEdgeExtend) && (_blurPlugin != eBlurPluginLaplacian) && (sigmax < 0.1) && (sigmay < 0.1) && (orderX == 0) && (orderY == 0) ) {
                return false;
            }
            // VanVliet filter was inexistent before 1.53, and buggy before CImg.h from
            // 57ffb8393314e5102c00e5f9f8fa3dcace179608 Thu Dec 11 10:57:13 2014 +0100
            if (filter == eFilterGaussian) {
#             ifdef cimgblur_internal_vanvliet
                vanvliet(cimg_blur, /*cimg_blur.vanvliet(*/ sigmax, orderX, 'x', boundary);
                vanvliet(cimg_blur, /*cimg_blur.vanvliet(*/ sigmay, orderY, 'y', boundary);
#             else
                cimg_blur.vanvliet(sigmax, orderX, 'x', boundary);
                if ( abort() ) { return false; }
                cimg_blur.vanvliet(sigmay, orderY, 'y', boundary);
#             endif
            } else {
                cimg_blur.deriche(sigmax, orderX, 'x', boundary);
                if ( abort() ) { return false; }
                cimg_blur.deriche(sigmay, orderY, 'y', boundary);
            }

            return true;
        } else if ( (filter == eFilterBox) || (filter == eFilterTriangle) || (filter == eFilterQuadratic) ) {
            int iter = ( filter == eFilterBox ? 1 :
                        (filter == eFilterTriangle ? 2 : 3) );

#         ifdef cimgblur_internal_boxfilter
            boxfilter(cimg_blur, /*cimg_blur.boxfilter(*/ sx * scale, orderX, 'x', boundary, iter);
            if ( abort() ) { return false; }
            boxfilter(cimg_blur, /*cimg_blur.boxfilter(*/ sy * scale,orderY, 'y', boundary, iter);
#         else
            cimg_blur.boxfilter(sx * scale, orderX, 'x', boundary, iter);
            if ( abort() ) { return false; }
            cimg_blur.boxfilter(sy * scale, orderY, 'y', boundary, iter);
#         endif

            return true;
        }
        assert(false);

        return false;
    }

    void renderEdgeDetect(const RenderArguments &args,
                          const CImgBlurParams& params,
                          cimg_library::CImg<cimgpix_t>& cimg)
    {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        double sx = args.renderScale.x * params.sizex;
        double sy = args.renderScale.y * params.sizey;
        //std::cout << "renderScale=" << args.renderScale.x << ',' << args.renderScale.y << std::endl;
        //std::cout << "renderWindow=" << args.renderWindow.x1 << ',' << args.renderWindow.y1 << ',' << args.renderWindow.x2 << ',' << args.renderWindow.y2 << std::endl;
        //std::cout << "cimg=" << cimg.width() << ',' << cimg.height() << std::endl;
        CImgList<cimgpix_t> grad;


        if (params.edgeDetectFilter == eEdgeDetectFilterSimple ||
            params.edgeDetectFilter == eEdgeDetectFilterSobel ||
            params.edgeDetectFilter == eEdgeDetectFilterRotationInvariant) {
            /*
            - -1 = Backward finite differences
            - 0 = Centered finite differences
            - 1 = Forward finite differences
            - 2 = Using Sobel kernels
            - 3 = Using rotation invariant kernels
            - 4 = Using Deriche recusrsive filter.
            - 5 = Using Van Vliet recusrsive filter.
             */
            int scheme = 0;
            if (params.edgeDetectFilter == eEdgeDetectFilterSobel) {
                scheme = 2;
            }
            if (params.edgeDetectFilter == eEdgeDetectFilterRotationInvariant) {
                scheme = 3;
            }
            // blur cimg, then compute gradients
            blur(params.filter, // should be box
                 sx, 0,
                 sy, 0,
                 1., (bool)params.boundary_i,
                 cimg);
            grad = cimg.get_gradient(0, scheme);
            if (params.edgeDetectFilter == eEdgeDetectFilterSobel) {
                grad[0] /= 8;
                grad[1] /= 8;
            }
            
        } else {
            // compute gradients
            grad.assign(cimg, cimg);
            blur(params.filter,
                 sx, 1,
                 sy, 0,
                 1., (bool)params.boundary_i,
                 grad[0]);
            blur(params.filter,
                 sx, 0,
                 sy, 1,
                 1., (bool)params.boundary_i,
                 grad[1]);
        }

        if ( abort() ) { return; }

        // if blur > 0 normalize the gradients so that the response of a vertical step-edge is exactly 1
        // for Gaussian and quasi-gaussian:
        // if blurred, multiply by 2 * pi * sigma^2, with sigma = (sx * scale / 2.4)
        // > int(x*exp(-x^2/(2*sigma^2))/(2*pi*sigma^4), x = 0 .. infinity);
        //
        // for Box, Triangle, and quadratic, derivatives are obtained using centered finite differences:
        // (I(1) - I(-1))/2
        // It can easily be shouwn that the derivative at 0 is the integral from 0 to 1 of the impulse response.
        //
        // for Box: if blurred, multiply by size
        // > simplify(int(1/s, x = 0 .. 1));
        //
        // for Triangle: if blurred, multiply by (2*s^2)/ (2*s+1) =
        // > simplify(int(1/s+x/(s*s), x = 0 .. 1));
        //
        // for Quadratic, multiply by (12*s^3)/(9*s^2-4)
        // > simplify(int((-4*(x-1)^2+3*s^2)/(4*s^3), x = 0 .. 1));
        switch (params.filter) {
            case eFilterBox:
                if (sx > 1.) {
                    grad[0] *= sx;
                }
                if (sy > 1.) {
                    grad[1] *= sy;
                }
                break;
            case eFilterTriangle:
                if (sx > 1.) {
                    grad[0] *= 2*sx*sx / (2*sx+1);
                }
                if (sy > 1.) {
                    grad[1] *= 2*sy*sy / (2*sy+1);
                }
               break;
            case eFilterQuadratic:
                if (sx > 1.) {
                    grad[0] *= (12*sx*sx*sx)/(9*sx*sx-4);
                }
                if (sy > 1.) {
                    grad[1] *= (12*sy*sy*sy)/(9*sy*sy-4);
                }
                break;
            case eFilterGaussian:
            case eFilterQuasiGaussian:
                if (sx / 2.4 >= 0.1) {
                    grad[0] *= 2. * M_PI * (sx/2.4) * (1. /* should be sx */ /2.4);
                }
                if (sy / 2.4 >= 0.1) {
                    grad[1] *= 2. * M_PI * (sy/2.4) * (1. /* should be sy */ /2.4);
                }
                break;
        }

        bool separate = (cimg.spectrum() == 1) || (params.edgeDetectMultiChannel == eEdgeDetectMultiChannelSeparate);
        if (separate) {
            cimg_forXYC(cimg, x, y, c) {
                double gx = grad[0](x, y, 0, c);
                double gy = grad[1](x, y, 0, c);
                // gradient direction is unchanged
                double sqe = gx * gx + gy * gy;
                double e = std::sqrt( (std::max)(sqe, 0.) );
                cimg(x, y, 0, c) = e;
            }
        } else if (params.edgeDetectMultiChannel == eEdgeDetectMultiChannelRMS) {
            cimg_forXY(cimg, x, y) {
                double sumsq = 0;
                // estimate gradient direction by making the largest component positive and summing
                double gradx = 0.;
                double grady = 0.;
                cimg_forC(cimg, c) {
                    double gx = grad[0](x, y, 0, c);
                    double gy = grad[1](x, y, 0, c);
                    sumsq += gx * gx + gy * gy;
                    if (std::abs(gx) > std::abs(gy)) {
                        if (std::abs(gx) < 0) {
                            gx = -gx;
                            gy = -gy;
                        }
                    } else {
                        if (std::abs(gy) < 0) {
                            gx = -gx;
                            gy = -gy;
                        }
                    }
                    gradx += gx;
                    grady += gy;
                }
                double sqe = sumsq / cimg.spectrum();
                double e = std::sqrt( (std::max)(sqe, 0.) );
                cimg_forC(cimg, c) {
                    cimg(x, y, 0, c) = e;
                    grad[0](x, y, 0, c) = gradx;
                    grad[1](x, y, 0, c) = grady;
                }
            }
        } else if (params.edgeDetectMultiChannel == eEdgeDetectMultiChannelMax) {
            cimg_forXY(cimg, x, y) {
                double maxsq = 0;
                double gradx = 0.;
                double grady = 0.;
                cimg_forC(cimg, c) {
                    double gx = grad[0](x, y, 0, c);
                    double gy = grad[1](x, y, 0, c);
                    double sq = gx * gx + gy * gy;
                    if (sq > maxsq) {
                        maxsq = sq;
                        gradx = gx;
                        grady = gy;
                    }
                }
                double e = std::sqrt( (std::max)(maxsq, 0.) );
                cimg_forC(cimg, c) {
                    cimg(x, y, 0, c) = e;
                    grad[0](x, y, 0, c) = gradx;
                    grad[1](x, y, 0, c) = grady;
                }
            }
        } else if (params.edgeDetectMultiChannel == eEdgeDetectMultiChannelTensor) {
            /*
             Di Zenzo edge magnitude:

             Jx = rx.^2 + gx.^2 + bx.^2;
             Jy = ry.^2 + gy.^2 + by.^2;
             Jxy = rx.*ry + gx.*gy + bx.*by;

             %compute first (greatest) eigenvalue of 2x2 matrix J'*J.
             %note that the abs() is only needed because some values may be slightly
             %negative due to round-off error.
             %See Section 1.3 of The Matrix Cookbook (available online).
             D = sqrt(abs(Jx.^2 - 2*Jx.*Jy + Jy.^2 + 4*Jxy.^2));
             e1 = (Jx + Jy + D) / 2;
             %the 2nd eigenvalue would be:  e2 = (Jx + Jy - D) / 2;
             
             edge_magnitude = sqrt(e1);
             
             %eigenvector corresponding to this eigenvalue: (Jxy, e1 - Jx)

             */
            cimg_forXY(cimg, x, y) {
                double Jx = 0.;
                double Jy = 0.;
                double Jxy = 0.;
                cimg_forC(cimg, c) {
                    double gx = grad[0](x, y, 0, c);
                    double gy = grad[1](x, y, 0, c);
                    Jx += gx * gx;
                    Jy += gy * gy;
                    Jxy += gx * gy;
                }
                double sqD = Jx * Jx - 2 * Jx * Jy + Jy * Jy + 4 * Jxy * Jxy;
                double D = std::sqrt( (std::max)(sqD, 0.) );
                double e1 = (Jx + Jy + D) / 2;
                double e = std::sqrt( (std::max)(e1, 0.) );
                cimg_forC(cimg, c) {
                    cimg(x, y, 0, c) = e;
                    grad[0](x, y, 0, c) = Jxy;
                    grad[1](x, y, 0, c) = e1 - Jx;
                }
            }
        }

        if ( abort() ) { return; }

        if ( params.erodeSize > 0 ) {
            cimg.erode( (unsigned int)std::floor((std::max)(0., params.erodeSize / params.par) * args.renderScale.x) * 2 + 1,
                       (unsigned int)std::floor((std::max)(0., params.erodeSize) * args.renderScale.y) * 2 + 1 );
        }
        if ( abort() ) { return; }

        if ( params.erodeSize < 0 ) {
            cimg.dilate( (unsigned int)std::floor((std::max)(0., -params.erodeSize / params.par) * args.renderScale.x) * 2 + 1,
                        (unsigned int)std::floor((std::max)(0., -params.erodeSize) * args.renderScale.y) * 2 + 1 );
        }
        if (params.edgeDetectNMS) {
            nms(cimg, grad[0], grad[1], separate);
        }
    }

    static double parabola(double Ip, double Ic, double In, double alpha)
    {
        /* equation of the parabola:
         y = f(x) = ax^2 + bx + c
         with:
         In = f(1) = a + b + c
         Ip = f(-1) = a - b - c
         Ic = f(0) = c
         
         so that
         */
        double a = ((In-Ic) + (Ip -Ic) ) / 2;
        double b = (In - Ip) / 2;
        double c = Ic;
        return a * alpha * alpha + b * alpha + c;
    }

    // maximul value of the parabola
    static double parabola_maxval(double Ip, double Ic, double In)
    {
        /* equation of the parabola:
         y = f(x) = ax^2 + bx + c
         with:
         In = f(1) = a + b + c
         Ip = f(-1) = a - b - c
         Ic = f(0) = c

         so that
         */
        double a = ((In-Ic) + (Ip -Ic) ) / 2;
        double b = (In - Ip) / 2;
        double c = Ic;
        /*
         The extremum is obtained for x such that f'(x) = 2ax + b = 0 => x = -b/2a
         The value at the extremum is
         */
        return c - b * b / (4*a);
    }

    void nms(cimg_library::CImg<cimgpix_t>& cimg,
             const cimg_library::CImg<cimgpix_t>& gx,
             const cimg_library::CImg<cimgpix_t>& gy,
             bool separate)
    {
        int cmax = separate ? cimg.spectrum() : 1;
        const cimg_library::CImg<cimgpix_t> I(cimg, /* is_shared=*/false); // copy input image
        cimg_forXY(cimg, x, y) {
            if ( x == 0 || x == (cimg.width() - 1) || y == 0 || y == (cimg.height() - 1) ) {
                cimg_forC(cimg, c) {
                    cimg(x,y,0,c) = 0.;
                }
                continue;
            }
            for (int c = 0; c < cmax; ++c) {
                cimgpix_t gradx = gx(x,y,0,c);
                cimgpix_t grady = gy(x,y,0,c);
                if (gradx == 0. && grady == 0.) {
                    // suppress the non-maximum
                    if (separate) {
                        cimg(x,y,0,c) = 0.;
                    } else {
                        cimg_forC(cimg, c1) {
                            cimg(x,y,0,c1) = 0.;
                        }
                    }
                    continue;
                }
                bool horiz = std::abs(gradx) >= std::abs(grady);
                // the values that are used for gradient magnitude interp
                cimgpix_t Ipp, Ipc, Ipn, Inp, Inc, Inn;
                if (horiz) {
                    Ipp = I(x-1,y-1,0,c);
                    Ipc = I(x-1,y  ,0,c);
                    Ipn = I(x-1,y+1,0,c);
                    Inp = I(x+1,y-1,0,c);
                    Inc = I(x+1,y  ,0,c);
                    Inn = I(x+1,y+1,0,c);
                } else {
                    // switch to the horizontal gradient case
                    std::swap(gradx, grady);
                    Ipp = I(x-1,y-1,0,c);
                    Ipc = I(x,  y-1,0,c);
                    Ipn = I(x+1,y-1,0,c);
                    Inp = I(x-1,y+1,0,c);
                    Inc = I(x,  y+1,0,c);
                    Inn = I(x+1,y+1,0,c);
                }
                double alpha = grady / gradx;
                assert(-1. <= alpha && alpha <= 1);
                // parabolic interpolation of the image values
                double Ip = parabola(Ipp, Ipc, Ipn, -alpha);
                double In = parabola(Inp, Inc, Inn, +alpha);
                double Ic = I(x,y,0,c);
                if (Ip < Ic && Ic >= In) {
                    // local maximum. If we were doing subpixel edge detection, we would interpolate the position here
                    // (see [Devernay1995])
                    // compute maximum value
                    double Imax = parabola_maxval(Ip, Ic, In);
                    if (separate) {
                        cimg(x,y,0,c) = Imax;
                    } else {
                        cimg_forC(cimg, c1) {
                            cimg(x,y,0,c1) = Imax;
                        }
                    }
                } else {
                    // suppress the non-maximum
                    if (separate) {
                        cimg(x,y,0,c) = 0.;
                    } else {
                        cimg_forC(cimg, c1) {
                            cimg(x,y,0,c1) = 0.;
                        }
                    }
                }
            }
        }
    }


private:

    // params
    const BlurPluginEnum _blurPlugin;
    DoubleParam *_sharpenSoftenAmount;
    Double2DParam *_size;
    DoubleParam *_erodeSize;
    DoubleParam *_erodeBlur;
    BooleanParam *_uniform;
    IntParam *_orderX;
    IntParam *_orderY;
    DoubleParam *_bloomRatio;
    IntParam *_bloomCount;
    BooleanParam *_edgeExtendPremult;
    DoubleParam *_edgeExtendSize;
    IntParam *_edgeExtendCount;
    BooleanParam *_edgeExtendUnpremult;
    ChoiceParam *_colorspace;
    ChoiceParam *_boundary;
    ChoiceParam *_filter;
    ChoiceParam *_edgeDetectFilter;
    ChoiceParam *_edgeDetectMultiChannel;
    DoubleParam *_edgeDetectBlur;
    DoubleParam *_edgeDetectErode;
    BooleanParam *_edgeDetectNMS;
    BooleanParam *_expandRoD;
    BooleanParam *_cropToFormat;
    DoubleParam *_alphaThreshold;
};


void
CImgBlurPlugin::describe(ImageEffectDescriptor& desc,
                         int majorVersion,
                         int /*minorVersion*/,
                         BlurPluginEnum blurPlugin)
{
    if (majorVersion < kPluginVersionMajor) {
        desc.setIsDeprecated(true);
    }
    // basic labels
    switch (blurPlugin) {
    case eBlurPluginBlur:
        desc.setLabel(kPluginName);
        desc.setPluginDescription(kPluginDescriptionFull);
        break;
    case eBlurPluginLaplacian:
        desc.setLabel(kPluginNameLaplacian);
        desc.setPluginDescription(kPluginDescriptionLaplacian);
        break;
   case eBlurPluginSharpen:
        desc.setLabel(kPluginNameSharpen);
        desc.setPluginDescription(kPluginDescriptionSharpen);
        break;
   case eBlurPluginSoften:
        desc.setLabel(kPluginNameSoften);
        desc.setPluginDescription(kPluginDescriptionSoften);
        break;
    case eBlurPluginChromaBlur:
        desc.setLabel(kPluginNameChromaBlur);
        desc.setPluginDescription(kPluginDescriptionChromaBlur);
        break;
    case eBlurPluginBloom:
        desc.setLabel(kPluginNameBloom);
        desc.setPluginDescription(kPluginDescriptionBloom);
        break;
    case eBlurPluginErodeBlur:
        desc.setLabel(kPluginNameErodeBlur);
        desc.setPluginDescription(kPluginDescriptionErodeBlur);
        break;
    case eBlurPluginEdgeExtend:
        desc.setLabel(kPluginNameEdgeExtend);
        desc.setPluginDescription(kPluginDescriptionEdgeExtend);
        break;
    case eBlurPluginEdgeDetect:
        desc.setLabel(kPluginNameEdgeDetect);
        desc.setPluginDescription(kPluginDescriptionEdgeDetect);
        break;
    }
    desc.setPluginGrouping(kPluginGrouping);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(kHostFrameThreading);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void
CImgBlurPlugin::describeInContext(ImageEffectDescriptor& desc,
                                  ContextEnum context,
                                  int majorVersion,
                                  int /*minorVersion*/,
                                  BlurPluginEnum blurPlugin)
{
    // create the clips and params

    bool processRGB;
    bool processAlpha;
    if (blurPlugin == eBlurPluginErodeBlur) {
        processRGB = false;
        processAlpha = true;
    } else if (blurPlugin == eBlurPluginEdgeDetect) {
        processRGB = true;
        processAlpha = false;
    } else {
        processRGB = true;
        if ( majorVersion >= 4 ) {
            processAlpha = true;
        } else {
            processAlpha = false; // wrong default before 3.1
        }
    }

    PageParamDescriptor *page = CImgBlurPlugin::describeInContextBegin(desc, context,
                                                                            kSupportsRGBA,
                                                                            (blurPlugin == eBlurPluginEdgeExtend) ? false : kSupportsRGB,
                                                                            (blurPlugin == eBlurPluginEdgeExtend || blurPlugin == eBlurPluginChromaBlur) ? false : kSupportsXY,
                                                                            (blurPlugin == eBlurPluginEdgeExtend || blurPlugin == eBlurPluginChromaBlur) ? false : kSupportsAlpha,
                                                                            kSupportsTiles,
                                                                            processRGB,
                                                                            processAlpha,
                                                                            /*processIsSecret=*/ (blurPlugin == eBlurPluginEdgeExtend));
    if (blurPlugin == eBlurPluginSharpen ||
        blurPlugin == eBlurPluginSoften) {
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSharpenSoftenAmount);
            param->setLabel(kParamSharpenSoftenAmountLabel);
            param->setHint(blurPlugin == eBlurPluginSharpen ? kParamSharpenAmountHint : kParamSoftenAmountHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(0., 1.);
            param->setDefault(blurPlugin == eBlurPluginSharpen ? 1. : 0.5);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginErodeBlur) {
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamErodeSize);
            param->setLabel(kParamErodeSizeLabel);
            param->setHint(kParamErodeSizeHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(-100, 100);
            param->setDefault(-1);
            param->setDigits(1);
            param->setIncrement(0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamErodeBlur);
            param->setLabel(kParamErodeBlurLabel);
            param->setHint(kParamErodeBlurHint);
            param->setRange(-0.5, DBL_MAX);
            param->setDisplayRange(0, 1);
            param->setDefault(0);
            param->setDigits(1);
            param->setIncrement(0.1);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginEdgeExtend) {
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamEdgeExtendPremult);
            param->setLabel(kParamEdgeExtendPremultLabel);
            param->setHint(kParamEdgeExtendPremultHint);
            param->setDefault(false);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEdgeExtendSize);
            param->setLabel(kParamEdgeExtendSizeLabel);
            param->setHint(kParamEdgeExtendSizeHint);
            param->setRange(0, DBL_MAX);
            param->setDisplayRange(0, 100);
            param->setDefault(20.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            IntParamDescriptor *param = desc.defineIntParam(kParamEdgeExtendCount);
            param->setLabel(kParamEdgeExtendCountLabel);
            param->setHint(kParamEdgeExtendCountHint);
            param->setRange(1, INT_MAX);
            param->setDisplayRange(1, 10);
            param->setDefault(kParamEdgeExtendCountDefault);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamEdgeExtendUnpremult);
            param->setLabel(kParamEdgeExtendUnpremultLabel);
            param->setHint(kParamEdgeExtendUnpremultHint);
            param->setDefault(false);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginBlur ||
        blurPlugin == eBlurPluginLaplacian ||
        blurPlugin == eBlurPluginSharpen ||
        blurPlugin == eBlurPluginSoften ||
        blurPlugin == eBlurPluginChromaBlur ||
        blurPlugin == eBlurPluginBloom) {
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamSize);
            param->setLabel(kParamSizeLabel);
            param->setHint(kParamSizeHint);
            param->setRange(0, 0, 1000, 1000);
            if (blurPlugin == eBlurPluginChromaBlur) {
                param->setDisplayRange(0, 0, 10, 10);
            } else {
                param->setDisplayRange(0, 0, 100, 100);
            }
            if (blurPlugin == eBlurPluginLaplacian || blurPlugin == eBlurPluginSharpen || blurPlugin == eBlurPluginSoften) {
                param->setDefault(kParamSizeDefaultLaplacian, kParamSizeDefaultLaplacian);
            } else {
                param->setDefault(kParamSizeDefault, kParamSizeDefault);
            }
            param->setDoubleType(eDoubleTypeXY);
            param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
            param->setDigits(1);
            param->setIncrement(0.1);
            param->setLayoutHint(eLayoutHintNoNewLine, 1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamUniform);
            param->setLabel(kParamUniformLabel);
            param->setHint(kParamUniformHint);
            // uniform parameter is false by default on Natron
            // https://github.com/MrKepzie/Natron/issues/1204
            param->setDefault(!getImageEffectHostDescription()->isNatron);
            if (page) {
                page->addChild(*param);
            }
        }
    }

    if (blurPlugin == eBlurPluginBlur) {
        {
            IntParamDescriptor *param = desc.defineIntParam(kParamOrderX);
            param->setLabel(kParamOrderXLabel);
            param->setHint(kParamOrderXHint);
            param->setRange(0, 2);
            param->setDisplayRange(0, 2);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            IntParamDescriptor *param = desc.defineIntParam(kParamOrderY);
            param->setLabel(kParamOrderYLabel);
            param->setHint(kParamOrderYHint);
            param->setRange(0, 2);
            param->setDisplayRange(0, 2);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginBloom) {
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamBloomRatio);
            param->setLabel(kParamBloomRatioLabel);
            param->setHint(kParamBloomRatioHint);
            param->setRange(1., DBL_MAX);
            param->setDisplayRange(1., 4.);
            param->setDefault(kParamBloomRatioDefault);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            IntParamDescriptor *param = desc.defineIntParam(kParamBloomCount);
            param->setLabel(kParamBloomCountLabel);
            param->setHint(kParamBloomCountHint);
            param->setRange(1, INT_MAX);
            param->setDisplayRange(1, 10);
            param->setDefault(kParamBloomCountDefault);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginChromaBlur) {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamColorspace);
        param->setLabel(kParamColorspaceLabel);
        param->setHint(kParamColorspaceHint);
        assert(param->getNOptions() == eColorspaceRec709);
        param->appendOption(kParamColorspaceOptionRec709);
        assert(param->getNOptions() == eColorspaceRec2020);
        param->appendOption(kParamColorspaceOptionRec2020);
        assert(param->getNOptions() == eColorspaceACESAP0);
        param->appendOption(kParamColorspaceOptionACESAP0);
        assert(param->getNOptions() == eColorspaceACESAP1);
        param->appendOption(kParamColorspaceOptionACESAP1);
        param->setDefault( (int)eColorspaceRec709 );
        if (page) {
            page->addChild(*param);
        }
    }

    if (blurPlugin == eBlurPluginBlur ||
        blurPlugin == eBlurPluginBloom) {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamBoundary);
        param->setLabel(kParamBoundaryLabel);
        param->setHint(kParamBoundaryHint);
        assert(param->getNOptions() == eBoundaryDirichlet && param->getNOptions() == 0);
        param->appendOption(kParamBoundaryOptionDirichlet);
        assert(param->getNOptions() == eBoundaryNeumann && param->getNOptions() == 1);
        param->appendOption(kParamBoundaryOptionNeumann);
        //assert(param->getNOptions() == eBoundaryPeriodic && param->getNOptions() == 2);
        //param->appendOption(kParamBoundaryOptionPeriodic, kParamBoundaryOptionPeriodicHint);
        if (blurPlugin == eBlurPluginLaplacian || blurPlugin == eBlurPluginSharpen || blurPlugin == eBlurPluginSoften) {
            // coverity[dead_error_line]
            param->setDefault( (int)kParamBoundaryDefaultLaplacian );
        } else if (blurPlugin == eBlurPluginBloom) {
            // coverity[dead_error_line]
            param->setDefault( (int)kParamBoundaryDefaultBloom );
        } else if (blurPlugin == eBlurPluginEdgeExtend) {
            // coverity[dead_error_line]
            param->setDefault( (int)kParamBoundaryDefaultEdgeExtend );
        } else {
            // coverity[dead_error_line]
            param->setDefault( (int)kParamBoundaryDefault );
        }
        if (page) {
            page->addChild(*param);
        }
    }
    if (blurPlugin == eBlurPluginEdgeDetect) {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamEdgeDetectFilter);
        param->setLabel(kParamEdgeDetectFilterLabel);
        param->setHint(kParamEdgeDetectFilterHint);
        assert(param->getNOptions() == eEdgeDetectFilterSimple);
        param->appendOption(kParamEdgeDetectFilterOptionSimple);
        assert(param->getNOptions() == eEdgeDetectFilterSobel);
        param->appendOption(kParamEdgeDetectFilterOptionSobel);
        assert(param->getNOptions() == eEdgeDetectFilterRotationInvariant);
        param->appendOption(kParamEdgeDetectFilterOptionRotationInvariant);
        assert(param->getNOptions() == eEdgeDetectFilterQuasiGaussian);
        param->appendOption(kParamEdgeDetectFilterOptionQuasiGaussian);
        assert(param->getNOptions() == eEdgeDetectFilterGaussian);
        param->appendOption(kParamEdgeDetectFilterOptionGaussian);
        assert(param->getNOptions() == eEdgeDetectFilterBox);
        param->appendOption(kParamEdgeDetectFilterOptionBox);
        assert(param->getNOptions() == eEdgeDetectFilterTriangle);
        param->appendOption(kParamEdgeDetectFilterOptionTriangle);
        assert(param->getNOptions() == eEdgeDetectFilterQuadratic);
        param->appendOption(kParamEdgeDetectFilterOptionQuadratic);
        param->setDefault( (int)kParamEdgeDetectFilterDefault );
        if (page) {
            page->addChild(*param);
        }
    } else if (blurPlugin == eBlurPluginBlur ||
        blurPlugin == eBlurPluginLaplacian ||
        blurPlugin == eBlurPluginSharpen ||
        blurPlugin == eBlurPluginSoften ||
        blurPlugin == eBlurPluginChromaBlur ||
        blurPlugin == eBlurPluginBloom ||
        blurPlugin == eBlurPluginEdgeExtend) { // all except EdgeDetect and ErodeBlur
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamFilter);
        param->setLabel(kParamFilterLabel);
        param->setHint(kParamFilterHint);
        assert(param->getNOptions() == eFilterQuasiGaussian && param->getNOptions() == 0);
        param->appendOption(kParamFilterOptionQuasiGaussian);
        assert(param->getNOptions() == eFilterGaussian && param->getNOptions() == 1);
        param->appendOption(kParamFilterOptionGaussian);
        assert(param->getNOptions() == eFilterBox && param->getNOptions() == 2);
        param->appendOption(kParamFilterOptionBox);
        assert(param->getNOptions() == eFilterTriangle && param->getNOptions() == 3);
        param->appendOption(kParamFilterOptionTriangle);
        assert(param->getNOptions() == eFilterQuadratic && param->getNOptions() == 4);
        param->appendOption(kParamFilterOptionQuadratic);
        if (blurPlugin == eBlurPluginBloom) {
            param->setDefault( (int)kParamFilterDefaultBloom );
        } else if (blurPlugin == eBlurPluginEdgeExtend) {
            param->setDefault( (int)kParamFilterDefaultEdgeExtend );
        } else {
            param->setDefault( (int)kParamFilterDefault );
        }
        if (page) {
            page->addChild(*param);
        }
    }
    if (blurPlugin == eBlurPluginEdgeDetect) {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamEdgeDetectMultiChannel);
            param->setLabel(kParamEdgeDetectMultiChannelLabel);
            param->setHint(kParamEdgeDetectMultiChannelHint);
            assert(param->getNOptions() == eEdgeDetectMultiChannelSeparate);
            param->appendOption(kParamEdgeDetectMultiChannelOptionSeparate);
            assert(param->getNOptions() == eEdgeDetectMultiChannelRMS);
            param->appendOption(kParamEdgeDetectMultiChannelOptionRMS);
            assert(param->getNOptions() == eEdgeDetectMultiChannelMax);
            param->appendOption(kParamEdgeDetectMultiChannelOptionMax);
            assert(param->getNOptions() == eEdgeDetectMultiChannelTensor);
            param->appendOption(kParamEdgeDetectMultiChannelOptionTensor);
            param->setDefault( (int)kParamEdgeDetectMultiChannelDefault);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEdgeDetectBlur);
            param->setLabel(kParamEdgeDetectBlurLabel);
            param->setHint(kParamEdgeDetectBlurHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 100.);
            param->setDefault(0);
            param->setDigits(1);
            param->setIncrement(0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamEdgeDetectErode);
            param->setLabel(kParamEdgeDetectErodeLabel);
            param->setHint(kParamEdgeDetectErodeHint);
            param->setRange(-DBL_MAX, DBL_MAX);
            param->setDisplayRange(-10, 10);
            param->setDefault(0);
            param->setDigits(1);
            param->setIncrement(0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamEdgeDetectNMS);
            param->setLabel(kParamEdgeDetectNMSLabel);
            param->setHint(kParamEdgeDetectNMSHint);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    if (blurPlugin == eBlurPluginBlur ||
        blurPlugin == eBlurPluginBloom ||
        blurPlugin == eBlurPluginErodeBlur ||
        blurPlugin == eBlurPluginEdgeExtend ||
        blurPlugin == eBlurPluginEdgeDetect) {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamExpandRoD);
        param->setLabel(kParamExpandRoDLabel);
        param->setHint(kParamExpandRoDHint);
        param->setDefault(blurPlugin != eBlurPluginBloom); // the expanded RoD of Bloom may be very large
#ifdef OFX_EXTENSIONS_NATRON
        if (getImageEffectHostDescription()->isNatron) {
            param->setLayoutHint(eLayoutHintNoNewLine); // on the same line as crop to format
        }
#endif
        if (page) {
            page->addChild(*param);
        }
    }
#ifdef OFX_EXTENSIONS_NATRON
    if (getImageEffectHostDescription()->isNatron) {
        if (blurPlugin != eBlurPluginChromaBlur && blurPlugin != eBlurPluginLaplacian && blurPlugin != eBlurPluginSharpen && blurPlugin != eBlurPluginSoften) {
            BooleanParamDescriptor *param = desc.defineBooleanParam(kParamCropToFormat);
            param->setLabel(kParamCropToFormatLabel);
            param->setHint(kParamCropToFormatHint);
            param->setDefault(true);
            if (page) {
                page->addChild(*param);
            }
        }
    }
#endif
    if (blurPlugin == eBlurPluginBlur || blurPlugin == eBlurPluginBloom) {
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlphaThreshold);
            param->setLabel(kParamAlphaThresholdLabel);
            param->setHint(kParamAlphaThresholdHint);
            param->setRange(0., DBL_MAX);
            param->setDisplayRange(0., 1.);
            param->setDefault(0.);
            if (page) {
                page->addChild(*param);
            }
        }
    }
    CImgBlurPlugin::describeInContextEnd(desc, context, page, blurPlugin != eBlurPluginEdgeExtend);
} // CImgBlurPlugin::describeInContext

//
// CImgBlurPluginFactory
//
mDeclarePluginFactory(CImgBlurPluginFactory0, {ofxsThreadSuiteCheck();}, {});

void
CImgBlurPluginFactory0::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe( desc, getMajorVersion(), getMinorVersion() );
}

void
CImgBlurPluginFactory0::describeInContext(ImageEffectDescriptor& desc,
                                                       ContextEnum context)
{
    return CImgBlurPlugin::describeInContext( desc, context, getMajorVersion(), getMinorVersion() );
}

ImageEffect*
CImgBlurPluginFactory0::createInstance(OfxImageEffectHandle handle,
                                                    ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle);
}

mDeclarePluginFactoryVersioned(CImgBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgBlurPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe( desc, this->getMajorVersion(), this->getMinorVersion() );
}

template<unsigned int majorVersion>
void
CImgBlurPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                       ContextEnum context)
{
    return CImgBlurPlugin::describeInContext( desc, context, this->getMajorVersion(), this->getMinorVersion() );
}

template<unsigned int majorVersion>
ImageEffect*
CImgBlurPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                    ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle);
}

//
// CImgLaplacianPluginFactory
//
mDeclarePluginFactoryVersioned(CImgLaplacianPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgLaplacianPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginLaplacian);
}

template<unsigned int majorVersion>
void
CImgLaplacianPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                            ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginLaplacian);
}

template<unsigned int majorVersion>
ImageEffect*
CImgLaplacianPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                         ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginLaplacian);
}

//
// CImgChromaBlurPluginFactory
//
mDeclarePluginFactoryVersioned(CImgChromaBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgChromaBlurPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginChromaBlur);
}

template<unsigned int majorVersion>
void
CImgChromaBlurPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                             ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginChromaBlur);
}

template<unsigned int majorVersion>
ImageEffect*
CImgChromaBlurPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                          ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginChromaBlur);
}

//
// CImgBloomPluginFactory
//
mDeclarePluginFactoryVersioned(CImgBloomPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgBloomPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginBloom);
}

template<unsigned int majorVersion>
void
CImgBloomPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                        ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginBloom);
}

template<unsigned int majorVersion>
ImageEffect*
CImgBloomPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                     ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginBloom);
}

//
// CImgErodeBlurPluginFactory
//
mDeclarePluginFactoryVersioned(CImgErodeBlurPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgErodeBlurPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginErodeBlur);
}

template<unsigned int majorVersion>
void
CImgErodeBlurPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                            ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginErodeBlur);
}

template<unsigned int majorVersion>
ImageEffect*
CImgErodeBlurPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                         ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginErodeBlur);
}

//
// CImgSharpenPluginFactory
//
mDeclarePluginFactoryVersioned(CImgSharpenPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgSharpenPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginSharpen);
}

template<unsigned int majorVersion>
void
CImgSharpenPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                          ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginSharpen);
}

template<unsigned int majorVersion>
ImageEffect*
CImgSharpenPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                       ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginSharpen);
}

//
// CImgSoftenPluginFactory
//
mDeclarePluginFactoryVersioned(CImgSoftenPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgSoftenPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginSoften);
}

template<unsigned int majorVersion>
void
CImgSoftenPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                         ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginSoften);
}

template<unsigned int majorVersion>
ImageEffect*
CImgSoftenPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                      ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginSoften);
}

//
// CImgEdgeExtendPluginFactory
//
mDeclarePluginFactoryVersioned(CImgEdgeExtendPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgEdgeExtendPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginEdgeExtend);
}

template<unsigned int majorVersion>
void
CImgEdgeExtendPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                         ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginEdgeExtend);
}

template<unsigned int majorVersion>
ImageEffect*
CImgEdgeExtendPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                      ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginEdgeExtend);
}

//
// CImgEdgeDetectPluginFactory
//
mDeclarePluginFactoryVersioned(CImgEdgeDetectPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<unsigned int majorVersion>
void
CImgEdgeDetectPluginFactory<majorVersion>::describe(ImageEffectDescriptor& desc)
{
    return CImgBlurPlugin::describe(desc, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginEdgeDetect);
}

template<unsigned int majorVersion>
void
CImgEdgeDetectPluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor& desc,
                                                             ContextEnum context)
{
    return CImgBlurPlugin::describeInContext(desc, context, this->getMajorVersion(), this->getMinorVersion(), eBlurPluginEdgeDetect);
}

template<unsigned int majorVersion>
ImageEffect*
CImgEdgeDetectPluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                          ContextEnum /*context*/)
{
    return new CImgBlurPlugin(handle, eBlurPluginEdgeDetect);
}


// Declare old versions for backward compatibility.
// They have default for processAlpha set to false
static CImgBlurPluginFactory<3> oldp1(kPluginIdentifier, 0);
static CImgLaplacianPluginFactory<3> oldp2(kPluginIdentifierLaplacian, 0);
static CImgChromaBlurPluginFactory<3> oldp3(kPluginIdentifierChromaBlur, 0);
static CImgBloomPluginFactory<3> oldp4(kPluginIdentifierBloom, 0);
mRegisterPluginFactoryInstance(oldp1)
mRegisterPluginFactoryInstance(oldp2)
mRegisterPluginFactoryInstance(oldp3)
mRegisterPluginFactoryInstance(oldp4)

static CImgBlurPluginFactory<kPluginVersionMajor> p1(kPluginIdentifier, kPluginVersionMinor);
static CImgLaplacianPluginFactory<kPluginVersionMajor> p2(kPluginIdentifierLaplacian, kPluginVersionMinor);
static CImgChromaBlurPluginFactory<kPluginVersionMajor> p3(kPluginIdentifierChromaBlur, kPluginVersionMinor);
static CImgBloomPluginFactory<kPluginVersionMajor> p4(kPluginIdentifierBloom, kPluginVersionMinor);
static CImgErodeBlurPluginFactory<kPluginVersionMajor> p5(kPluginIdentifierErodeBlur, kPluginVersionMinor);
static CImgSharpenPluginFactory<kPluginVersionMajor> p6(kPluginIdentifierSharpen, kPluginVersionMinor);
static CImgSoftenPluginFactory<kPluginVersionMajor> p7(kPluginIdentifierSoften, kPluginVersionMinor);
static CImgEdgeExtendPluginFactory<kPluginVersionMajor> p8(kPluginIdentifierEdgeExtend, kPluginVersionMinor);
static CImgEdgeDetectPluginFactory<kPluginVersionMajor> p9(kPluginIdentifierEdgeDetect, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)
mRegisterPluginFactoryInstance(p5)
mRegisterPluginFactoryInstance(p6)
mRegisterPluginFactoryInstance(p7)
mRegisterPluginFactoryInstance(p8)
mRegisterPluginFactoryInstance(p9)

OFXS_NAMESPACE_ANONYMOUS_EXIT
