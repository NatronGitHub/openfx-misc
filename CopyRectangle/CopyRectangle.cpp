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
 * OFX CopyRectangle plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsNormalisedDefaults.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMaskMix.h"
#include "ofxsRGBAProcessing.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "SoftEdge.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "CopyRectangleOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Copies a rectangle from the input A to the input B in output.\n" \
    "It can be used to limit an effect to a rectangle of the original image by plugging the original image into the input B.\n" \
    "See also https://web.archive.org/web/20220807200909/http://www.opticalenquiry.com/nuke/index.php?title=CopyRectange"

#define kPluginIdentifier "net.sf.openfx.CopyRectanglePlugin"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: on hosts that ignore normalised defaults (e.g. DaVinci Resolve), the default rectangle
//              is the project's, not (1,1) pixels (#236)
// version 3.0: the Softness falloff is a smoothstep over [-0.5, Softness + 0.5] pixels from each edge (the last
//              pixel of the fade at the top and right edges is A), in canonical units (Softness is divided by
//              the pixel aspect ratio in x); the rectangle edges may be fractional (in y, even without softness,
//              the edge pixels are partly A; in x, the rectangle is from the left edge rounded up to the right
//              edge rounded up); the default rectangle is the middle half of the project (#282). Version 2 is
//              kept, obsolete, for old projects.
#define kPluginVersionMajor 3 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.
#define kPluginVersionMinorV2 1 // the minor version of the obsolete version 2

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kClipA "A"
#define kClipAHint "The image from which the rectangle is copied."
#define kClipB "B"
#define kClipBHint "The image onto which the rectangle is copied."

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red channel."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green channel."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue channel."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha channel."
#endif

#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Size of the fade from B to A around the edges of the rectangle, in canonical coordinates (pixels at render scale 1, times the pixel aspect ratio in x). Along each axis, A is mixed over B by smoothstep((d + 1/2) / (Softness + 1)), where d is the distance in pixels from the pixel centre to the nearest edge (the fade starts half a pixel outside the rectangle); at the top and right edges, the last pixel of the fade is A; in x, only the pixels from the left edge, rounded up, to the right edge, rounded up, are in the rectangle; in the corners, the two factors are multiplied."
#define kParamSoftnessHintV2 "Size of the fade around edges of the rectangle to apply, in pixels: at d pixels from the nearest edge pixel, A is mixed over B by d / Softness; in the corners, the two factors are multiplied."

// The major versions, whose softness differs (see the History)
enum class CopyRectangleVersion
{
    e2 = 2, // integer rectangle, linear fade
    e3 = 3, // fractional rectangle, smoothstep fade
};

// The softness version of a major version: the only place that maps one to the other
static constexpr CopyRectangleVersion
versionOf(unsigned majorVersion)
{
    return (majorVersion >= 3) ? CopyRectangleVersion::e3 : CopyRectangleVersion::e2;
}

// Version 3: the factor along one axis of the rectangle [r1, r2), in pixels at render scale 1, at the pixel whose
// left (or bottom) edge is at p1 and centre at c, for a softness s >= 0 in pixels: a smoothstep from half a pixel
// outside the nearest edge to s + 1/2 pixels inside it. At the top or right edge, a pixel whose left (bottom) edge
// is not after r2 - s is not faded, so that the last pixel of the fade there is A; the pixels from r2 rounded up
// are not in the rectangle.
static inline
double
softnessFactorV3(double r1,
                 double r2,
                 double p1,
                 double c,
                 double s)
{
    const double dLow = c - r1;
    const double dHigh = r2 - c;

    if ( p1 >= std::ceil(r2) ) {
        // after the top (right) edge, rounded up

        return 0.;
    }
    if (dLow <= dHigh) {
        return SoftEdge::smoothstep( (dLow + 0.5) / (s + 1.) );
    }
    if ( !(p1 > r2 - s) ) {
        return 1.;
    }

    return SoftEdge::smoothstep( (dHigh + 0.5) / (s + 1.) );
}

// Version 3, along x: the pixels before r1 rounded up are not in the rectangle either, and without softness the
// pixels in it are all A.
static inline
double
softnessFactorV3X(double r1,
                  double r2,
                  double p1,
                  double c,
                  double s)
{
    if ( (p1 < std::ceil(r1)) || (p1 >= std::ceil(r2)) ) {
        return 0.;
    }
    if (s <= 0.) {
        return 1.;
    }

    return softnessFactorV3(r1, r2, p1, c, s);
}

class CopyRectangleProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImgA;
    const Image *_srcImgB;
    const Image *_maskImg;
    double _softness;
    bool _process[4];
    OfxRectI _rectangle;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    const CopyRectangleVersion _version;
    OfxRectD _rectangleCanonical; // version 3
    double _softnessCanonical; // version 3
    double _par;

public:
    CopyRectangleProcessorBase(ImageEffect &instance,
                               CopyRectangleVersion version)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _maskImg(NULL)
        , _softness(0.)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _version(version)
        , _softnessCanonical(0.)
        , _par(1.)
    {
        _rectangleCanonical.x1 = _rectangleCanonical.y1 = _rectangleCanonical.x2 = _rectangleCanonical.y2 = 0.;
        _process[0] = _process[1] = _process[2] = _process[3] = false;
        _rectangle.x1 = _rectangle.y1 = _rectangle.x2 = _rectangle.y2 = 0;
    }

    /** @brief set the src image */
    void setSrcImgs(const Image *A,
                    const Image* B)
    {
        _srcImgA = A;
        _srcImgB = B;
    }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(const OfxRectI& rectangle,
                   double softness,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   double mix)
    {
        _rectangle = rectangle;
        _softness = softness;
        _process[0] = processR;
        _process[1] = processG;
        _process[2] = processB;
        _process[3] = processA;
        _mix = mix;
    }

    // version 3
    void setValuesV3(const OfxRectD& rectangle,
                     double softness,
                     double par)
    {
        _rectangleCanonical = rectangle;
        _softnessCanonical = softness;
        _par = par;
    }

};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class CopyRectangleProcessor
    : public CopyRectangleProcessorBase
{
public:
    CopyRectangleProcessor(ImageEffect &instance,
                           CopyRectangleVersion version)
        : CopyRectangleProcessorBase(instance, version)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        if (_version >= CopyRectangleVersion::e3) {
            process<CopyRectangleVersion::e3>(procWindow, rs);
        } else {
            process<CopyRectangleVersion::e2>(procWindow, rs);
        }
    }

    // A is mixed over B by the multiplier of the version, in the rectangle. Version 2: in the integer
    // rectangle, a linear fade in pixels at the render scale. Version 3: the factor is computed in pixels at
    // render scale 1, from the pixel edges and centres, and the rectangle is where it is positive.
    template<CopyRectangleVersion version>
    void process(const OfxRectI& procWindow,
                 const OfxPointD& rs)
    {
        // version 3: the rectangle and the softness in pixels at render scale 1
        const double x1 = _rectangleCanonical.x1 / _par;
        const double x2 = _rectangleCanonical.x2 / _par;
        const double y1 = _rectangleCanonical.y1;
        const double y2 = _rectangleCanonical.y2;
        const double sx = _softnessCanonical / _par;
        const double sy = _softnessCanonical;
        float tmpPix[nComponents];

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            double yFactor = 0.; // version 3
            float yMultiplier = 1.f; // version 2
            bool yInRectangle = false; // version 2
            if (version >= CopyRectangleVersion::e3) {
                yFactor = softnessFactorV3(y1, y2, y / rs.y, (y + 0.5) / rs.y, sy);
            } else {
                // distance to the nearest rectangle area horizontal edge
                const int yDistance =  (std::min)(y - _rectangle.y1, _rectangle.y2 - 1 - y);
                yInRectangle = y >= _rectangle.y1 && y < _rectangle.y2;
                if (yInRectangle) {
                    ///apply softness only within the rectangle
                    yMultiplier = yDistance < _softness ? yDistance / (float)_softness : 1.f;
                }
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                float multiplier;
                bool inRectangle;
                if (version >= CopyRectangleVersion::e3) {
                    const double xFactor = (yFactor <= 0.) ? 0. : softnessFactorV3X(x1, x2, x / rs.x, (x + 0.5) / rs.x, sx);
                    multiplier = static_cast<float>(xFactor * yFactor);
                    inRectangle = multiplier > 0.f;
                } else {
                    // distance to the nearest rectangle area vertical edge
                    const int xDistance =  (std::min)(x - _rectangle.x1, _rectangle.x2 - 1 - x);
                    const bool xInRectangle = x >= _rectangle.x1 && x < _rectangle.x2;
                    ///apply softness only within the rectangle
                    const float xMultiplier = (xInRectangle && xDistance < _softness) ? xDistance / (float)_softness : 1.f;
                    multiplier = xMultiplier * yMultiplier;
                    inRectangle = xInRectangle && yInRectangle;
                }

                const PIX *srcPixB = _srcImgB ? (const PIX*)_srcImgB->getPixelAddress(x, y) : NULL;

                if (inRectangle) {
                    const PIX *srcPixA = _srcImgA ? (const PIX*)_srcImgA->getPixelAddress(x, y) : NULL;
                    for (int k = 0; k < nComponents; ++k) {
                        if (!_process[(nComponents) == 1 ? 3 : k]) {
                            tmpPix[k] = srcPixB ? srcPixB[k] : 0.f;
                        } else {
                            const PIX A = srcPixA ? srcPixA[k] : PIX();
                            const PIX B = srcPixB ? srcPixB[k] : PIX();
                            tmpPix[k] = A * multiplier + B * (1.f - multiplier);
                        }
                    }
                    ofxsMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, x, y, srcPixB, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                } else {
                    for (int k = 0; k < nComponents; ++k) {
                        dstPix[k] = srcPixB ? srcPixB[k] : PIX();
                    }
                }
            }
        }
    } // process
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CopyRectanglePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    CopyRectanglePlugin(OfxImageEffectHandle handle,
                        CopyRectangleVersion version)
        : ImageEffect(handle)
        , _version(version)
        , _dstClip(NULL)
        , _srcClipA(NULL)
        , _srcClipB(NULL)
        , _btmLeft(NULL)
        , _size(NULL)
        , _softness(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
    {

        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentAlpha || _dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClipA = fetchClip(kClipA);
        assert( _srcClipA && (!_srcClipA->isConnected() || _srcClipA->getPixelComponents() == ePixelComponentAlpha || _srcClipA->getPixelComponents() == ePixelComponentRGB || _srcClipA->getPixelComponents() == ePixelComponentRGBA) );
        _srcClipB = fetchClip(kClipB);
        assert( _srcClipB && (!_srcClipB->isConnected() || _srcClipB->getPixelComponents() == ePixelComponentAlpha || _srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        _btmLeft = fetchDouble2DParam(kParamRectangleInteractBtmLeft);
        _size = fetchDouble2DParam(kParamRectangleInteractSize);
        _softness = fetchDoubleParam(kParamSoftness);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        assert(_btmLeft && _size && _softness);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        // Hosts that ignore normalised defaults (e.g. Resolve) give the normalised values as is: convert
        // the ones that still equal their default (#236, see ofxsNormalisedDefaults.h). Last, since
        // setValue() may call changedParam().
        // the size only if the position is untouched too: (1,1) may be a deliberate one-pixel rectangle
        if ( ofxsDenormaliseUntouchedDefault(*this, *_btmLeft, /*isPosition=*/true) ) {
            ofxsDenormaliseUntouchedDefault(*this, *_size, /*isPosition=*/false);
        }
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(CopyRectangleProcessorBase &, const RenderArguments &args);

    void getRectanglecanonical(OfxTime time, OfxRectD& rect) const;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    const CopyRectangleVersion _version;
    Clip *_dstClip;
    Clip *_srcClipA;
    Clip *_srcClipB;
    Clip *_maskClip;
    Double2DParam* _btmLeft;
    Double2DParam* _size;
    DoubleParam* _softness;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};

void
CopyRectanglePlugin::getRectanglecanonical(OfxTime time,
                                           OfxRectD& rect) const
{
    _btmLeft->getValueAtTime(time, rect.x1, rect.y1);
    double w, h;
    _size->getValueAtTime(time, w, h);
    rect.x2 = rect.x1 + w;
    rect.y2 = rect.y1 + h;
}

////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
CopyRectanglePlugin::setupAndProcess(CopyRectangleProcessorBase &processor,
                                     const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
# ifndef NDEBUG
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(args.time) : 0 );
    if ( srcA.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(srcA, args);
# endif
        BitDepthEnum srcBitDepth      = srcA->getPixelDepth();
        PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(args.time) : 0 );
    if ( srcB.get() ) {
# ifndef NDEBUG
        checkBadRenderScaleOrField(srcB, args);
# endif
        BitDepthEnum srcBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if ( mask.get() ) {
        checkBadRenderScaleOrField(mask, args);
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImgs( srcA.get(), srcB.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    OfxRectD rectangle;
    getRectanglecanonical(args.time, rectangle);
    OfxRectI rectanglePixel;
    double par = dst->getPixelAspectRatio();
    Coords::toPixelEnclosing(rectangle, args.renderScale, par, &rectanglePixel);
    double softness;
    _softness->getValueAtTime(args.time, softness);
    softness *= args.renderScale.x;

    bool processR;
    bool processG;
    bool processB;
    bool processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);

    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(rectanglePixel, softness, processR, processG, processB, processA, mix);
    if (_version >= CopyRectangleVersion::e3) {
        double softnessCanonical;
        _softness->getValueAtTime(args.time, softnessCanonical);
        processor.setValuesV3(rectangle, softnessCanonical, par);
    }

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // CopyRectanglePlugin::setupAndProcess

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
CopyRectanglePlugin::getRegionsOfInterest(const RegionsOfInterestArguments &args,
                                          RegionOfInterestSetter &rois)
{
    if (!getImageEffectHostDescription()->supportsTiles) {
        return;
    }
    OfxRectD rectangle;

    getRectanglecanonical(args.time, rectangle);
    if (_version >= CopyRectangleVersion::e3) {
        // the fade starts half a pixel outside the rectangle
        const double par = _srcClipA->getPixelAspectRatio();
        rectangle.x1 -= 0.5 * par;
        rectangle.x2 += 0.5 * par;
        rectangle.y1 -= 0.5;
        rectangle.y2 += 0.5;
    }

    // intersect the crop rectangle with args.regionOfInterest
    Coords::rectIntersection(rectangle, args.regionOfInterest, &rectangle);
    double mix = 1.;
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        _mix->getValueAtTime(args.time, mix);
    }
    if ( doMasking && (mix != 1.) ) {
        // for masking or mixing, we also need the source image.
        // compute the bounding box with the default ROI
        Coords::rectBoundingBox(rectangle, args.regionOfInterest, &rectangle);
    }
    rois.setRegionOfInterest(*_srcClipA, rectangle);
    // no need to set the RoI on _srcClipB, since it's the same as the output RoI
}

bool
CopyRectanglePlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args,
                                           OfxRectD &rod)
{
    OfxRectD rect;

    getRectanglecanonical(args.time, rect);
    const OfxRectD& srcB_rod = _srcClipB->getRegionOfDefinition(args.time);
    Coords::rectBoundingBox(rect, srcB_rod, &rod);

    return true;
}

// the internal render function
template <int nComponents>
void
CopyRectanglePlugin::renderInternal(const RenderArguments &args,
                                    BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        CopyRectangleProcessor<unsigned char, nComponents, 255> fred(*this, _version);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        CopyRectangleProcessor<unsigned short, nComponents, 65535> fred(*this, _version);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        CopyRectangleProcessor<float, nComponents, 1> fred(*this, _version);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

bool
CopyRectanglePlugin::isIdentity(const IsIdentityArguments &args,
                                Clip * &identityClip,
                                double & /*identityTime*/
                                , int& /*view*/, std::string& /*plane*/)
{
    double mix;

    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClipB;

        return true;
    }

    if ( ofxsMaskRoDExcludesRenderWindow(args, _maskClip, _maskApply, _maskInvert) ) {
        // effect is identity if the renderWindow doesn't intersect the mask RoD
        identityClip = _srcClipB;

        return true;
    }

    return false;
}

void
CopyRectanglePlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    PixelComponentEnum outputComps = getDefaultOutputClipComponents();

    clipPreferences.setClipComponents(*_srcClipA, outputComps);
    clipPreferences.setClipComponents(*_srcClipB, outputComps);
}

// the overridden render function
void
CopyRectanglePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || _srcClipA->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipA->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert( kSupportsMultipleClipPARs   || _srcClipB->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || _srcClipB->getPixelDepth()       == _dstClip->getPixelDepth() );
#ifdef OFX_EXTENSIONS_NATRON
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentAlpha);
#else
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentAlpha);
#endif
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
#ifdef OFX_EXTENSIONS_NATRON
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
#endif
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

mDeclarePluginFactoryVersioned(CopyRectanglePluginFactory, {ofxsThreadSuiteCheck();}, {});
template<unsigned int majorVersion>
void
CopyRectanglePluginFactory<majorVersion>::describe(ImageEffectDescriptor &desc)
{
    // version 2 (Natron 2.5 and earlier) is kept for old projects
    desc.setIsObsolete(majorVersion < kPluginVersionMajor);
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGeneral);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);


    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    desc.setSupportsTiles(kSupportsTiles);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    ofxsSetOverlayInteractDescriptorV2(desc, new RectangleOverlayDescriptorV2);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

template<unsigned int majorVersion>
ImageEffect*
CopyRectanglePluginFactory<majorVersion>::createInstance(OfxImageEffectHandle handle,
                                                         ContextEnum /*context*/)
{
    return new CopyRectanglePlugin( handle, versionOf(majorVersion) );
}

static void
defineComponentParam(ImageEffectDescriptor &desc,
                     PageParamDescriptor* page,
                     const std::string& name,
                     const std::string& label,
                     bool newLine)
{
    BooleanParamDescriptor* param = desc.defineBooleanParam(name);

    param->setLabel(label);
    param->setDefault(true);
    param->setHint("Copy " + name);
    if (!newLine) {
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
    }
    if (page) {
        page->addChild(*param);
    }
}

template<unsigned int majorVersion>
void
CopyRectanglePluginFactory<majorVersion>::describeInContext(ImageEffectDescriptor &desc,
                                                            ContextEnum context)
{
    ClipDescriptor *srcClipB = desc.defineClip(kClipB);

    srcClipB->setHint(kClipAHint);
    srcClipB->addSupportedComponent(ePixelComponentRGBA);
    srcClipB->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClipB->addSupportedComponent(ePixelComponentXY);
#endif
    srcClipB->addSupportedComponent(ePixelComponentAlpha);
    srcClipB->setTemporalClipAccess(false);
    srcClipB->setSupportsTiles(kSupportsTiles);
    srcClipB->setIsMask(false);

    ClipDescriptor *srcClipA = desc.defineClip(kClipA);
    srcClipA->setHint(kClipAHint);
    srcClipA->addSupportedComponent(ePixelComponentRGBA);
    srcClipA->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    srcClipA->addSupportedComponent(ePixelComponentXY);
#endif
    srcClipA->addSupportedComponent(ePixelComponentAlpha);
    srcClipA->setTemporalClipAccess(false);
    srcClipA->setSupportsTiles(kSupportsTiles);
    srcClipA->setIsMask(false);


    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
#ifdef OFX_EXTENSIONS_NATRON
    dstClip->addSupportedComponent(ePixelComponentXY);
#endif
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    ClipDescriptor *maskClip = (context == eContextPaint) ? desc.defineClip("Brush") : desc.defineClip("Mask");
    maskClip->addSupportedComponent(ePixelComponentAlpha);
    maskClip->setTemporalClipAccess(false);
    if (context != eContextPaint) {
        maskClip->setOptional(true);
    }
    maskClip->setSupportsTiles(kSupportsTiles);
    maskClip->setIsMask(true);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // the channels come before the rectangle (#379)
    defineComponentParam(desc, page, kParamProcessR, kParamProcessRLabel, false);
    defineComponentParam(desc, page, kParamProcessG, kParamProcessGLabel, false);
    defineComponentParam(desc, page, kParamProcessB, kParamProcessBLabel, false);
    defineComponentParam(desc, page, kParamProcessA, kParamProcessALabel, true);

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        ofxsSetDefaultCoordinateSystemNormalised(*param); // see ofxsNormalisedDefaults.h (#236)
        // version 3: the middle half of the project (#282)
        if (versionOf(majorVersion) >= CopyRectangleVersion::e3) {
            param->setDefault(0.25, 0.25);
        } else {
            param->setDefault(0., 0.);
        }
        param->setDisplayRange(-10000., -10000., 10000., 10000.); // Resolve requires display range or values are clamped to (-1,1)
        param->setIncrement(1.);
        param->setHint(kParamRectangleInteractBtmLeftHint);
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // size
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractSize);
        param->setLabel(kParamRectangleInteractSizeLabel);
        param->setDoubleType(eDoubleTypeXY);
        ofxsSetDefaultCoordinateSystemNormalised(*param); // see ofxsNormalisedDefaults.h (#236)
        if (versionOf(majorVersion) >= CopyRectangleVersion::e3) {
            param->setDefault(0.5, 0.5);
        } else {
            param->setDefault(1., 1.);
        }
        param->setRange(0., 0., DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0., 0., 10000., 10000.); // Resolve requires display range or values are clamped to (-1,1)
        param->setDimensionLabels(kParamRectangleInteractSizeDim1, kParamRectangleInteractSizeDim2);
        param->setHint(kParamRectangleInteractSizeHint);
        param->setIncrement(1.);
        param->setDigits(0);
        if (page) {
            page->addChild(*param);
        }
    }

    // interactive
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamRectangleInteractInteractive);
        param->setLabel(kParamRectangleInteractInteractiveLabel);
        param->setHint(kParamRectangleInteractInteractiveHint);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }

    hiDPIDescribeParams(desc, NULL, page);

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabel(kParamSoftnessLabel);
        param->setDefault(0);
        param->setRange(0., 100.);
        param->setDisplayRange(0., 100.);
        param->setIncrement(1.);
        param->setHint( (versionOf(majorVersion) >= CopyRectangleVersion::e3) ? kParamSoftnessHint : kParamSoftnessHintV2 );
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // CopyRectanglePluginFactory::describeInContext

static CopyRectanglePluginFactory<kPluginVersionMajor> p(kPluginIdentifier, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)
static CopyRectanglePluginFactory<2> p_v2(kPluginIdentifier, kPluginVersionMinorV2);
mRegisterPluginFactoryInstance(p_v2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
