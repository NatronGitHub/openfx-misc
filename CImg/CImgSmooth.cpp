/*
 OFX CImgSmooth plugin.

 Copyright (C) 2014 INRIA

 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:

 Redistributions of source code must retain the above copyright notice, this
 list of conditions and the following disclaimer.

 Redistributions in binary form must reproduce the above copyright notice, this
 list of conditions and the following disclaimer in the documentation and/or
 other materials provided with the distribution.

 Neither the name of the {organization} nor the names of its
 contributors may be used to endorse or promote products derived from
 this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 INRIA
 Domaine de Voluceau
 Rocquencourt - B.P. 105
 78153 Le Chesnay Cedex - France


 The skeleton for this source file is from:
 OFX Basic Example plugin, a plugin that illustrates the use of the OFX Support library.

 Copyright (C) 2004-2005 The Open Effects Association Ltd
 Author Bruno Nicoletti bruno@thefoundry.co.uk

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

 * Redistributions of source code must retain the above copyright notice,
 this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice,
 this list of conditions and the following disclaimer in the documentation
 and/or other materials provided with the distribution.
 * Neither the name The Open Effects Association Ltd, nor the names of its
 contributors may be used to endorse or promote products derived from this
 software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 The Open Effects Association Ltd
 1 Wardour St
 London W1D 6PA
 England

 */

// TODO: masking/mixing
// TODO: handle RGB, Alpha components
// TODO: support tiles (process src, and copy just the renderWindow to dst)

#include "CImgSmooth.h"

#include <memory>
#include <cmath>
#include <cstring>
#ifdef _WINDOWS
#include <windows.h>
#endif

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsMerging.h"
#include "ofxsCopier.h"

#define cimg_display 0
#include <CImg.h>

#define kPluginName          "SmoothCImg"
#define kPluginGrouping      "Filter"
#define kPluginDescription \
"Smooth/Denoise input stream using anisotropic PDE-based smoothing.\n" \
"If input is RGBA or RGB, only RGB data is smoothed.\n" \
"Uses the 'blur_anisotropic' function from the CImg library.\n" \
"CImg is a free, open-source library distributed under the CeCILL-C " \
"(close to the GNU LGPL) or CeCILL (compatible with the GNU GPL) licenses. " \
"It can be used in commercial applications (see http://cimg.sourceforge.net)."

#define kPluginIdentifier    "net.sf.cimg.CImgSmooth"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kRenderThreadSafety eRenderFullySafe

#define kParamAmplitude "amplitude"
#define kParamAmplitudeLabel "Amplitude"
#define kParamAmplitudeHint "Amplitude of the smoothing, in pixel units (>=0). This is the maximum length of streamlines used to smooth the data."
#define kParamAmplitudeDefault 60.0

#define kParamSharpness "sharpness"
#define kParamSharpnessLabel "Sharpness"
#define kParamSharpnessHint "Contour preservation (>=0)"
#define kParamSharpnessDefault 0.7

#define kParamAnisotropy "anisotropy"
#define kParamAnisotropyLabel "Anisotropy"
#define kParamAnisotropyHint "Smoothing anisotropy (0<=a<=1)"
#define kParamAnisotropyDefault 0.3

#define kParamAlpha "alpha"
#define kParamAlphaLabel "Alpha"
#define kParamAlphaHint "Noise scale, in pixels units (>=0)"
#define kParamAlphaDefault 0.6

#define kParamSigma "sigma"
#define kParamSigmaLabel "Sigma"
#define kParamSigmaHint "Geometry regularity, in pixels units (>=0)"
#define kParamSigmaDefault 1.1

#define kParamDl "dl"
#define kParamDlLabel "dl"
#define kParamDlHint "Spatial discretization, in pixel units (0<=dl<=1)"
#define kParamDlDefault 0.8

#define kParamDa "da"
#define kParamDaLabel "da"
#define kParamDaHint "Angular integration step, in degrees (0<=da<=90). If da=0, Iterated oriented Laplacians is used instead of LIC-based smoothing."
#define kParamDaDefault 30.0

#define kParamGaussPrec "prec"
#define kParamGaussPrecLabel "Precision"
#define kParamGaussPrecHint "Precision of the diffusion process (>0)."
#define kParamGaussPrecDefault 2.0

#define kParamInterp "interpolation"
#define kParamInterpLabel "Interpolation"
#define kParamInterpHint "Interpolation type"
#define kParamInterpOptionNearest "Nearest-neighbor"
#define kParamInterpOptionNearestHint "Nearest-neighbor."
#define kParamInterpOptionLinear "Linear"
#define kParamInterpOptionLinearHint "Linear interpolation."
#define kParamInterpOptionRungeKutta "Runge-Kutta"
#define kParamInterpOptionRungeKuttaHint "Runge-Kutta interpolation."
#define kParamInterpDefault eInterpNearest
enum InterpEnum
{
    eInterpNearest = 0,
    eInterpLinear,
    eInterpRungeKutta,
};

#define kParamFastApprox "is_fast_approximation"
#define kParamFastApproxLabel "fast Approximation"
#define kParamFastApproxHint "Tells if a fast approximation of the gaussian function is used or not"
#define kParamFastApproxDafault true

using namespace OFX;

template <class Params>
class CImgFilterPluginHelper : public OFX::ImageEffect
{
public:

    CImgFilterPluginHelper(OfxImageEffectHandle handle)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        assert(dstClip_ && (dstClip_->getPixelComponents() == ePixelComponentRGB || dstClip_->getPixelComponents() == ePixelComponentRGBA));
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(srcClip_ && (srcClip_->getPixelComponents() == ePixelComponentRGB || srcClip_->getPixelComponents() == ePixelComponentRGBA));
        maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!maskClip_ || maskClip_->getPixelComponents() == ePixelComponentAlpha);

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
    }

    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    virtual void getValuesAtTime(double time, Params& params) = 0;

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    virtual void getRoI(const OfxRectI rect, const OfxPointD& renderScale, const Params& params, OfxRectI* roi) = 0;

    virtual void render(const OfxPointD& renderScale, const Params& params, cimg_library::CImg<float>& cimg) = 0;

    virtual bool isIdentity(const Params& params) { return false; };

private:
    void
    setupAndFill(OFX::PixelProcessorFilterBase & processor,
                 const OfxRectI &renderWindow,
                 void *dstPixelData,
                 const OfxRectI& dstBounds,
                 OFX::PixelComponentEnum dstPixelComponents,
                 OFX::BitDepthEnum dstPixelDepth,
                 int dstRowBytes);

    void
    setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                 double time,
                 const OfxRectI &renderWindow,
                 const void *srcPixelData,
                 const OfxRectI& srcBounds,
                 OFX::PixelComponentEnum srcPixelComponents,
                 OFX::BitDepthEnum srcBitDepth,
                 int srcRowBytes,
                 void *dstPixelData,
                 const OfxRectI& dstBounds,
                 OFX::PixelComponentEnum dstPixelComponents,
                 OFX::BitDepthEnum dstPixelDepth,
                 int dstRowBytes,
                 bool premult,
                 int premultChannel,
                 double mix,
                 bool maskInvert);

    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;

    // params
    OFX::BooleanParam* _premult;
    OFX::ChoiceParam* _premultChannel;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
};


/* set up and run a copy processor */
template <class Params>
void
CImgFilterPluginHelper<Params>::setupAndFill(OFX::PixelProcessorFilterBase & processor,
                               const OfxRectI &renderWindow,
                               void *dstPixelData,
                               const OfxRectI& dstBounds,
                               OFX::PixelComponentEnum dstPixelComponents,
                               OFX::BitDepthEnum dstPixelDepth,
                               int dstRowBytes)
{
    // set the images
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}


static inline
bool
isEmpty(const OfxRectI& r)
{
    return r.x1 >= r.x2 || r.y1 >= r.y2;
}

/* set up and run a copy processor */
template <class Params>
void
CImgFilterPluginHelper<Params>::setupAndCopy(OFX::PixelProcessorFilterBase & processor,
                               double time,
                               const OfxRectI &renderWindow,
                               const void *srcPixelData,
                               const OfxRectI& srcBounds,
                               OFX::PixelComponentEnum srcPixelComponents,
                               OFX::BitDepthEnum srcBitDepth,
                               int srcRowBytes,
                               void *dstPixelData,
                               const OfxRectI& dstBounds,
                               OFX::PixelComponentEnum dstPixelComponents,
                               OFX::BitDepthEnum dstPixelDepth,
                               int dstRowBytes,
                               bool premult,
                               int premultChannel,
                               double mix,
                               bool maskInvert)
{
    assert(srcPixelData && dstPixelData);

    // make sure bit depths are sane
    if(srcBitDepth != dstPixelDepth/* || srcPixelComponents != dstPixelComponents*/) {
        OFX::throwSuiteStatusException(kOfxStatErrFormat);
    }

    if (isEmpty(renderWindow)) {
        return;
    }
    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(time) : 0);
    std::auto_ptr<OFX::Image> orig(srcClip_->fetchImage(time));
    if (getContext() != OFX::eContextFilter && maskClip_->isConnected()) {
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    assert(orig.get() && dstPixelData && srcPixelData);
    processor.setOrigImg(orig.get());
    processor.setDstImg(dstPixelData, dstBounds, dstPixelComponents, dstPixelDepth, dstRowBytes);
    processor.setSrcImg(srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes);

    // set the render window
    processor.setRenderWindow(renderWindow);

    processor.setPremultMaskMix(premult, premultChannel, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

static
bool
maskLineIsZero(const OFX::Image* mask, int x1, int x2, int y, bool maskInvert)
{
    assert(!mask || (mask->getPixelComponents() == ePixelComponentAlpha && mask->getPixelDepth() == eBitDepthFloat));

    if (maskInvert) {
        if (!mask) {
            return false;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the line is out of maskbounds, then mask is 1 at these places
        if (y < maskBounds.y1 || maskBounds.y2 <= y || x1 < maskBounds.x1 || maskBounds.x2 <= x2) {
            return false;
        }
        // the whole line is within the mask
        const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x1, y));
        assert(p);
        for (int x = x1; x < x2; ++x, ++p) {
            if (*p != 1.) {
                return false;
            }
        }
    } else {
        if (!mask) {
            return true;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if the line is completely out of the mask, it is 0
        if (y < maskBounds.y1 || maskBounds.y2 <= y) {
            return true;
        }
        // restrict the search to the part of the line which is within the mask
        x1 = std::max(x1, maskBounds.x1);
        x2 = std::min(x2, maskBounds.x2);
        if (x1 < x2) { // the line is not empty
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x1, y));
            assert(p);

            for (int x = x1; x < x2; ++x, ++p) {
                if (*p != 0.) {
                    return false;
                }
            }
        }
    }

    return true;
}

static
bool
maskColumnIsZero(const OFX::Image* mask, int x, int y1, int y2, bool maskInvert)
{
    assert(mask->getPixelComponents() == ePixelComponentAlpha && mask->getPixelDepth() == eBitDepthFloat);
    const int rowElems = mask->getRowBytes() / sizeof(float);

    if (maskInvert) {
        if (!mask) {
            return false;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if part of the column is out of maskbounds, then mask is 1 at these places
        if (x < maskBounds.x1 || maskBounds.x2 <= x || y1 < maskBounds.y1 || maskBounds.y2 <= y2) {
            return false;
        }
        // the whole column is within the mask
        const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y1));
        assert(p);
        for (int y = y1; y < y2; ++y) {
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y));
            if (p && *p != 1.) {
                return false;
            }
        }
    } else {
        if (!mask) {
            return true;
        }
        const OfxRectI& maskBounds = mask->getBounds();
        // if the column is completely out of the mask, it is 0
        if (x < maskBounds.x1 || maskBounds.x2 <= x) {
            return true;
        }
        // restrict the search to the part of the column which is within the mask
        y1 = std::max(y1, maskBounds.y1);
        y2 = std::min(y2, maskBounds.y2);
        if (y1 < y2) { // the column is not empty
            const float *p = reinterpret_cast<const float*>(mask->getPixelAddress(x, y1));
            assert(p);

            for (int y = y1; y < y2; ++y,  p += rowElems) {
                if (*p != 0.) {
                    return false;
                }
            }
        }
    }

    return true;
}

template <class Params>
void
CImgFilterPluginHelper<Params>::render(const OFX::RenderArguments &args)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    const double time = args.time;
    const OfxPointD& renderScale = args.renderScale;
    const OfxRectI& renderWindow = args.renderWindow;
    const FieldEnum fieldToRender = args.fieldToRender;

    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != renderScale.x ||
        dst->getRenderScale().y != renderScale.y ||
        dst->getField() != fieldToRender) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
    const OFX::PixelComponentEnum dstPixelComponents  = dst->getPixelComponents();
    assert(dstBitDepth == eBitDepthFloat); // only float is supported for now (others are untested)

    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(time));
    if (src.get()) {
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcPixelComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcPixelComponents != dstPixelComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
        if (src->getRenderScale().x != renderScale.x ||
            src->getRenderScale().y != renderScale.y ||
            src->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
    } else {
        // src is considered black and transparent, just fill black to dst and return
        void* dstPixelData = NULL;
        OfxRectI dstBounds;
        OFX::PixelComponentEnum dstPixelComponents;
        OFX::BitDepthEnum dstBitDepth;
        int dstRowBytes;
        getImageData(dst.get(), &dstPixelData, &dstBounds, &dstPixelComponents, &dstBitDepth, &dstRowBytes);

        if (dstPixelComponents == OFX::ePixelComponentRGBA) {
            OFX::BlackFiller<float, 4> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
            OFX::BlackFiller<float, 3> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
            OFX::BlackFiller<float, 1> fred(*this);
            setupAndFill(fred, renderWindow, dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes);
        } // switch

        return;
    }

    const void *srcPixelData = src->getPixelData();
    const OfxRectI srcBounds = src->getBounds();
    const OFX::PixelComponentEnum srcPixelComponents = src->getPixelComponents();
    const OFX::BitDepthEnum srcBitDepth = src->getPixelDepth();
    //srcPixelBytes = getPixelBytes(srcPixelComponents, srcBitDepth);
    const int srcRowBytes = src->getRowBytes();

    void *dstPixelData = dst->getPixelData();
    const OfxRectI dstBounds = dst->getBounds();
    //const OFX::PixelComponentEnum dstPixelComponents = dst->getPixelComponents();
    //const OFX::BitDepthEnum dstBitDepth = dst->getPixelDepth();
    //dstPixelBytes = getPixelBytes(dstPixelComponents, dstBitDepth);
    const int dstRowBytes = dst->getRowBytes();

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);
    bool maskInvert;
    _maskInvert->getValueAtTime(time, maskInvert);

    std::auto_ptr<OFX::Image> mask(getContext() != OFX::eContextFilter ? maskClip_->fetchImage(time) : 0);
    OfxRectI processWindow = renderWindow; //!< the window where pixels have to be computed (may be smaller than renderWindow if mask is zero on the borders)

    if (mix == 0.) {
        // no processing at all
        processWindow.x2 = processWindow.x1;
        processWindow.y2 = processWindow.y1;
    }
    if (mask.get()) {
        if (mask->getRenderScale().x != renderScale.x ||
            mask->getRenderScale().y != renderScale.y ||
            mask->getField() != fieldToRender) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }


        // shrink the processWindow at much as possible
        // top
        while (processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y2-1, maskInvert)) {
            --processWindow.y2;
        }
        // bottom
        while (processWindow.y2 > processWindow.y1 && maskLineIsZero(mask.get(), processWindow.x1, processWindow.x2, processWindow.y1, maskInvert)) {
            ++processWindow.y1;
        }
        // left
        while (processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x1, processWindow.y1, processWindow.y2, maskInvert)) {
            ++processWindow.x1;
        }
        // right
        while (processWindow.x2 > processWindow.x1 && maskColumnIsZero(mask.get(), processWindow.x2-1, processWindow.y1, processWindow.y2, maskInvert)) {
            --processWindow.x2;
        }
    }

    // copy areas of renderWindow that are not within processWindow to dst

    OfxRectI copyWindowN, copyWindowS, copyWindowE, copyWindowW;
    // top
    copyWindowN.x1 = renderWindow.x1;
    copyWindowN.x2 = renderWindow.x2;
    copyWindowN.y1 = processWindow.y2;
    copyWindowN.y2 = renderWindow.y2;
    // bottom
    copyWindowS.x1 = renderWindow.x1;
    copyWindowS.x2 = renderWindow.x2;
    copyWindowS.y1 = renderWindow.y1;
    copyWindowS.y2 = processWindow.y1;
    // left
    copyWindowW.x1 = renderWindow.x1;
    copyWindowW.x2 = processWindow.x1;
    copyWindowW.y1 = processWindow.y1;
    copyWindowW.y2 = processWindow.y2;
    // right
    copyWindowE.x1 = processWindow.x2;
    copyWindowE.x2 = renderWindow.x2;
    copyWindowE.y1 = processWindow.y1;
    copyWindowE.y2 = processWindow.y2;
    if (dstPixelComponents == OFX::ePixelComponentRGBA) {
        OFX::PixelCopier<float, 4, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else if (dstPixelComponents == OFX::ePixelComponentRGB) {
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    }  else if (dstPixelComponents == OFX::ePixelComponentAlpha) {
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, time, copyWindowN,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowS,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowW,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
        setupAndCopy(fred, time, copyWindowE,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } // switch

    if (isEmpty(processWindow)) {
        // the area that actually has to be processed is empty, the job is finished!
        return;
    }
    assert(mix != 0.); // mix == 0. should give an empty processWindow

    cimg_library::CImg<float> src_img;

    Params params;
    getValuesAtTime(time, params);

    // compute the src ROI (must be consistent with getRegionsOfInterest())
    OfxRectI srcRoI;
    getRoI(processWindow, renderScale, params, &srcRoI);
    OfxRectI srcRoD = src->getRegionOfDefinition();
    MergeImages2D::rectIntersection(srcRoI, srcRoD, &srcRoI);
    assert(src->getBounds().x1 <= srcRoI.x1 && srcRoI.x2 <= src->getBounds().x2 && src->getBounds().y1 <= srcRoI.y1 && srcRoI.y2 <= src->getBounds().y2);
    if(src->getBounds().x1 > srcRoI.x1 || srcRoI.x2 > src->getBounds().x2 || src->getBounds().y1 > srcRoI.y1 || srcRoI.y2 > src->getBounds().y2) {
        throwSuiteStatusException(kOfxStatFailed);
    }

    // allocate the cimg data to hold the src ROI
    const int srcCImgNComponents = (srcPixelComponents == ePixelComponentAlpha) ? 1 : 3;
    const OfxRectI srcCImgBounds = srcRoI;
    const OFX::PixelComponentEnum srcCImgPixelComponents = (srcPixelComponents == ePixelComponentRGBA) ? ePixelComponentRGB : srcPixelComponents;
    const OFX::BitDepthEnum srcCImgBitDepth = eBitDepthFloat;
    const int srcCImgWidth = srcCImgBounds.x2 - srcCImgBounds.x1;
    const int srcCImgHeight = srcCImgBounds.y2 - srcCImgBounds.y1;
    const int srcCImgRowBytes = getPixelBytes(srcCImgPixelComponents, srcCImgBitDepth) * srcCImgWidth;
    size_t srcCImgSize = srcCImgRowBytes * srcCImgHeight;
    std::auto_ptr<ImageMemory> srcCImgData(new ImageMemory(srcCImgSize));
    float *srcCImgPixelData = (float*)srcCImgData->lock();

    // unpremult and copy ROI to CImg
    // special case for CImgSmooth and other color-based processors: don't process the alpha component, it is just copied at the end
    if (srcPixelComponents == OFX::ePixelComponentRGBA) {
        assert(srcCImgNComponents == 3);
        OFX::PixelCopierUnPremult<float, 4, 1, float, 3, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else if (srcPixelComponents == OFX::ePixelComponentRGB) {
        // just copy, no premult
        assert(srcCImgNComponents == 3);
        OFX::PixelCopier<float, 3, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                     premult, premultChannel, mix, maskInvert);
    } else {
        // just copy, no premult
        assert(srcPixelComponents == OFX::ePixelComponentAlpha);
        assert(srcCImgNComponents == 1);
        OFX::PixelCopier<float, 1, 1> fred(*this);
        setupAndCopy(fred, time, srcRoI,
                     srcPixelData, srcBounds, srcPixelComponents, srcBitDepth, srcRowBytes,
                     srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                     premult, premultChannel, mix, maskInvert);
    }

    if (mix != 0.) {
        // srcCImg uses srcCImgData as its data area
        cimg_library::CImg<float> srcCImg(srcCImgPixelData, srcCImgNComponents, srcCImgWidth, srcCImgHeight, 1, true);
        srcCImg.permute_axes("yzcx");
        assert(srcCImg.width() == srcCImgWidth);
        assert(srcCImg.height() == srcCImgHeight);
        assert(srcCImg.depth() == 1);
        assert(srcCImg.spectrum() == srcCImgNComponents);

        render(renderScale, params, srcCImg);

        srcCImg.permute_axes("cxyz");
    }

    if (srcPixelComponents != OFX::ePixelComponentRGBA) {
        // trivial case: RGB or ALpha
        // just copy and mask/mix the results, no premult
        const bool doMasking = getContext() != OFX::eContextFilter && maskClip_->isConnected();
        if (srcPixelComponents == OFX::ePixelComponentRGB) {
            if (doMasking) {
                OFX::PixelCopierMaskMix<float, 3, 1, true> fred(*this);
                setupAndCopy(fred, time, processWindow,
                             srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                             dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                             premult, premultChannel, mix, maskInvert);
            } else {
                OFX::PixelCopierMaskMix<float, 3, 1, false> fred(*this);
                setupAndCopy(fred, time, processWindow,
                             srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                             dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                             premult, premultChannel, mix, maskInvert);
            }
        } else if (srcPixelComponents == OFX::ePixelComponentAlpha) {
            if (doMasking) {
                OFX::PixelCopierMaskMix<float, 1, 1, true> fred(*this);
                setupAndCopy(fred, time, processWindow,
                             srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                             dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                             premult, premultChannel, mix, maskInvert);
            } else {
                OFX::PixelCopierMaskMix<float, 1, 1, false> fred(*this);
                setupAndCopy(fred, time, processWindow,
                             srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                             dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                             premult, premultChannel, mix, maskInvert);
            }
        }
    } else {
        // take alpha from the original image, and premult
        OFX::PixelCopierPremultOrigMaskMix<float, 3, 1, float, 4, 1> fred(*this);
        setupAndCopy(fred, time, processWindow,
                     srcCImgPixelData, srcCImgBounds, srcCImgPixelComponents, srcCImgBitDepth, srcCImgRowBytes,
                     dstPixelData, dstBounds, dstPixelComponents, dstBitDepth, dstRowBytes,
                     premult, premultChannel, mix, maskInvert);
    }
}


static void
toPixelEnclosing(const OfxRectD& regionOfInterest,
                 const OfxPointD& renderScale,
                 double par,
                 OfxRectI *rect)
{
    rect->x1 = std::floor(regionOfInterest.x1 * renderScale.x / par);
    rect->y1 = std::floor(regionOfInterest.y1 * renderScale.y);
    rect->x2 = std::ceil(regionOfInterest.x2 * renderScale.x / par);
    rect->y2 = std::ceil(regionOfInterest.y2 * renderScale.y);
}

static void
toCanonical(const OfxRectI& rect,
                 const OfxPointD& renderScale,
                 double par,
                 OfxRectD *regionOfInterest)
{
    regionOfInterest->x1 = rect.x1 * par / renderScale.x;
    regionOfInterest->y1 = rect.y1 / renderScale.y;
    regionOfInterest->x2 = rect.x2 * par / renderScale.x;
    regionOfInterest->y2 = rect.y2 / renderScale.y;
}

static void
enlargeRectI(const OfxRectI& rect,
                 int delta_pix,
                 const OfxRectI& bounds,
                 OfxRectI* rectOut)
{
    rectOut->x1 = std::max(bounds.x1, rect.x1 - delta_pix);
    rectOut->x2 = std::min(bounds.x2, rect.x2 + delta_pix);
    rectOut->y1 = std::max(bounds.y1, rect.y1 - delta_pix);
    rectOut->y2 = std::min(bounds.y2, rect.y2 + delta_pix);
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
template <class Params>
void
CImgFilterPluginHelper<Params>::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    const OfxRectD& regionOfInterest = args.regionOfInterest;
    OfxRectD srcRoI;

    double mix = 1.;
    const bool doMasking = getContext() != OFX::eContextFilter && maskClip_->isConnected();
    if (doMasking) {
        _mix->getValueAtTime(time, mix);
        if (mix == 0.) {
            // identity transform
            //srcRoI = regionOfInterest;
            //rois.setRegionOfInterest(*srcClip_, srcRoI);
            return;
        }
    }

    Params params;
    getValuesAtTime(args.time, params);

    double pixelaspectratio = srcClip_->getPixelAspectRatio();

    OfxRectI rectPixel;
    toPixelEnclosing(regionOfInterest, args.renderScale, pixelaspectratio, &rectPixel);
    getRoI(rectPixel, args.renderScale, params, &rectPixel);
    toCanonical(rectPixel, args.renderScale, pixelaspectratio, &srcRoI);

    if (doMasking && mix != 1.) {
        // for masking or mixing, we also need the source image.
        // compute the bounding box with the default ROI
        MergeImages2D::rectBoundingBox(srcRoI, regionOfInterest, &srcRoI);
    }

    // no need to set it on mask (the default ROI is OK)
    rois.setRegionOfInterest(*srcClip_, srcRoI);
}

template <class Params>
bool
CImgFilterPluginHelper<Params>::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args,
                                         OfxRectD &/*rod*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }

    return false;
}

template <class Params>
bool
CImgFilterPluginHelper<Params>::isIdentity(const OFX::IsIdentityArguments &args,
                              OFX::Clip * &identityClip,
                              double &/*identityTime*/)
{
    if (!kSupportsRenderScale && (args.renderScale.x != 1. || args.renderScale.y != 1.)) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;

    double mix;
    _mix->getValueAtTime(time, mix);
    if (mix == 0.) {
        identityClip = srcClip_;
        return true;
    }
    Params params;
    getValuesAtTime(time, params);
    if (isIdentity(params)) {
        identityClip = srcClip_;
        return true;
    }
    return false;
}



/// Smooth plugin
struct CImgSmoothParams
{
    double amplitude;
    double sharpness;
    double anisotropy;
    double alpha;
    double sigma;
    double dl;
    double da;
    double gprec;
    int interp_i;
    //InterpEnum interp;
    bool fast_approx;
};

class CImgSmoothPlugin : public CImgFilterPluginHelper<CImgSmoothParams>
{
public:

    CImgSmoothPlugin(OfxImageEffectHandle handle)
    : CImgFilterPluginHelper<CImgSmoothParams>(handle)
    {
        _amplitude  = fetchDoubleParam(kParamAmplitude);
        _sharpness  = fetchDoubleParam(kParamSharpness);
        _anisotropy = fetchDoubleParam(kParamAnisotropy);
        _alpha      = fetchDoubleParam(kParamAlpha);
        _sigma      = fetchDoubleParam(kParamSigma);
        _dl         = fetchDoubleParam(kParamDl);
        _da         = fetchDoubleParam(kParamDa);
        _gprec      = fetchDoubleParam(kParamGaussPrec);
        _interp     = fetchChoiceParam(kParamInterp);
        _fast_approx = fetchBooleanParam(kParamFastApprox);
        assert(_amplitude && _sharpness && _anisotropy && _alpha && _sigma && _dl && _da && _gprec && _interp && _fast_approx);
    }

    virtual void getValuesAtTime(double time, CImgSmoothParams& params) OVERRIDE FINAL {
        _amplitude->getValueAtTime(time, params.amplitude);
        _sharpness->getValueAtTime(time, params.sharpness);
        _anisotropy->getValueAtTime(time, params.anisotropy);
        _alpha->getValueAtTime(time, params.alpha);
        _sigma->getValueAtTime(time, params.sigma);
        _dl->getValueAtTime(time, params.dl);
        _da->getValueAtTime(time, params.da);
        _gprec->getValueAtTime(time, params.gprec);
        _interp->getValueAtTime(time, params.interp_i);
        _fast_approx->getValueAtTime(time, params.fast_approx);
    }

    // compute the roi required to compute rect, given params. This roi is then intersected with the image rod.
    // only called if mix != 0.
    virtual void getRoI(const OfxRectI rect, const OfxPointD& renderScale, const CImgSmoothParams& params, OfxRectI* roi) OVERRIDE FINAL {
        int delta_pix = std::ceil((params.amplitude + params.alpha + params.sigma) * renderScale.x);
        roi->x1 = rect.x1 - delta_pix;
        roi->x2 = rect.x2 + delta_pix;
        roi->y1 = rect.y1 - delta_pix;
        roi->y2 = rect.y2 + delta_pix;
    }

    virtual void render(const OfxPointD& renderScale, const CImgSmoothParams& params, cimg_library::CImg<float>& cimg) OVERRIDE FINAL {
        // PROCESSING.
        // This is the only place where the actual processing takes place
        cimg.blur_anisotropic(params.amplitude * renderScale.x, // in pixels
                              params.sharpness,
                              params.anisotropy,
                              params.alpha * renderScale.x, // in pixels
                              params.sigma * renderScale.x, // in pixels
                              params.dl, // in pixel, but we don't discretize more
                              params.da,
                              params.gprec,
                              params.interp_i,
                              params.fast_approx);

    }

    virtual bool isIdentity(const CImgSmoothParams& params) OVERRIDE FINAL {
        return (params.amplitude <= 0. || params.dl < 0.);
    };

private:

    // params
    OFX::DoubleParam *_amplitude;
    OFX::DoubleParam *_sharpness;
    OFX::DoubleParam *_anisotropy;
    OFX::DoubleParam *_alpha;
    OFX::DoubleParam *_sigma;
    OFX::DoubleParam *_dl;
    OFX::DoubleParam *_da;
    OFX::DoubleParam *_gprec;
    OFX::ChoiceParam *_interp;
    OFX::BooleanParam *_fast_approx;
};


mDeclarePluginFactory(CImgSmoothPluginFactory, {}, {});

void CImgSmoothPluginFactory::describe(OFX::ImageEffectDescriptor& desc)
{
    // basic labels
    desc.setLabels(kPluginName, kPluginName, kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add supported context
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    // add supported pixel depths
    //desc.addSupportedBitDepth(eBitDepthUByte);
    //desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(true);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(kRenderThreadSafety);
}

void CImgSmoothPluginFactory::describeInContext(OFX::ImageEffectDescriptor& desc, OFX::ContextEnum context)
{
    OFX::ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    srcClip->addSupportedComponent(OFX::ePixelComponentRGB);
    srcClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    OFX::ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGBA);
    dstClip->addSupportedComponent(OFX::ePixelComponentRGB);
    dstClip->addSupportedComponent(OFX::ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }

    // create the params
    OFX::PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAmplitude);
        param->setLabels(kParamAmplitudeLabel, kParamAmplitudeLabel, kParamAmplitudeLabel);
        param->setHint(kParamAmplitudeHint);
        param->setRange(0, 1000);
        param->setDefault(kParamAmplitudeDefault);
        param->setIncrement(1);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSharpness);
        param->setLabels(kParamSharpnessLabel, kParamSharpnessLabel, kParamSharpnessLabel);
        param->setRange(0, 1);
        param->setDefault(kParamSharpnessDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAnisotropy);
        param->setLabels(kParamAnisotropyLabel, kParamAnisotropyLabel, kParamAnisotropyLabel);
        param->setHint(kParamAnisotropyHint);
        param->setRange(0, 1);
        param->setDefault(kParamAnisotropyDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamAlpha);
        param->setLabels(kParamAlphaLabel, kParamAlphaLabel, kParamAlphaLabel);
        param->setRange(0, 1);
        param->setDefault(kParamAlphaDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSigma);
        param->setLabels(kParamSigmaLabel, kParamSigmaLabel, kParamSigmaLabel);
        param->setHint(kParamSigmaHint);
        param->setRange(0, 3);
        param->setDefault(kParamSigmaDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDl);
        param->setLabels(kParamDlLabel, kParamDlLabel, kParamDlLabel);
        param->setHint(kParamDlHint);
        param->setRange(0, 1);
        param->setDefault(kParamDlDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamDa);
        param->setLabels(kParamDaLabel, kParamDaLabel, kParamDaLabel);
        param->setHint(kParamDaHint);
        param->setRange(0, 90);
        param->setDefault(kParamDaDefault);
        param->setIncrement(0.5);
        page->addChild(*param);
    }
    {
        OFX::DoubleParamDescriptor *param = desc.defineDoubleParam(kParamGaussPrec);
        param->setLabels(kParamGaussPrecLabel, kParamGaussPrecLabel, kParamGaussPrecLabel);
        param->setHint(kParamGaussPrecHint);
        param->setRange(0, 5);
        param->setDefault(kParamGaussPrecDefault);
        param->setIncrement(0.05);
        page->addChild(*param);
    }
    {
        OFX::ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamInterp);
        param->setLabels(kParamInterpLabel, kParamInterpLabel, kParamInterpLabel);
        param->setHint(kParamInterpHint);
        assert(param->getNOptions() == eInterpNearest && param->getNOptions() == 0);
        param->appendOption(kParamInterpOptionNearest, kParamInterpOptionNearestHint);
        assert(param->getNOptions() == eInterpLinear && param->getNOptions() == 1);
        param->appendOption(kParamInterpOptionLinear, kParamInterpOptionLinearHint);
        assert(param->getNOptions() == eInterpRungeKutta && param->getNOptions() == 2);
        param->appendOption(kParamInterpOptionRungeKutta, kParamInterpOptionRungeKuttaHint);
        param->setDefault((int)kParamInterpDefault);
        page->addChild(*param);
    }
    {
        OFX::BooleanParamDescriptor *param = desc.defineBooleanParam(kParamFastApprox);
        param->setLabels(kParamFastApproxLabel, kParamFastApproxLabel, kParamFastApproxLabel);
        param->setHint(kParamFastApproxHint);
        param->setDefault(kParamFastApproxDafault);
        page->addChild(*param);
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
}

OFX::ImageEffect* CImgSmoothPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new CImgSmoothPlugin(handle);
}


void getCImgSmoothPluginID(OFX::PluginFactoryArray &ids)
{
    static CImgSmoothPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
    ids.push_back(&p);
}
