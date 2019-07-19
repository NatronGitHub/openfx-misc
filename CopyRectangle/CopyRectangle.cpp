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
 * OFX CopyRectangle plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsCoords.h"
#include "ofxsRectangleInteract.h"
#include "ofxsMaskMix.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "CopyRectangleOFX"
#define kPluginGrouping "Merge"
#define kPluginDescription "Copies a rectangle from the input A to the input B in output.\n" \
    "It can be used to limit an effect to a rectangle of the original image by plugging the original image into the input B.\n" \
    "See also http://opticalenquiry.com/nuke/index.php?title=CopyRectange"

#define kPluginIdentifier "net.sf.openfx.CopyRectanglePlugin"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
#define kPluginVersionMajor 2 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

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
#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Size of the fade around edges of the rectangle to apply"

// Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
#define kParamDefaultsNormalised "defaultsNormalised"

static bool gHostSupportsDefaultCoordinateSystem = true; // for kParamDefaultsNormalised
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

public:
    CopyRectangleProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImgA(NULL)
        , _srcImgB(NULL)
        , _maskImg(NULL)
        , _softness(0.)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
    {
        _process[0] = _process[1] = _process[2] = _process[3] = false;
        _rectangle.x1 = _rectangle.y1 = _rectangle.x2 = _rectangle.y2 = 0.f;
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
};


// The "masked", "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue>
class CopyRectangleProcessor
    : public CopyRectangleProcessorBase
{
public:
    CopyRectangleProcessor(ImageEffect &instance)
        : CopyRectangleProcessorBase(instance)
    {
    }

private:
    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        float yMultiplier, xMultiplier;
        float tmpPix[nComponents];

        //assert(filter == _filter);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // distance to the nearest rectangle area horizontal edge
            int yDistance =  (std::min)(y - _rectangle.y1, _rectangle.y2 - 1 - y);

            // handle softness
            bool yInRectangle = y >= _rectangle.y1 && y < _rectangle.y2;
            if (yInRectangle) {
                ///apply softness only within the rectangle
                yMultiplier = yDistance < _softness ? yDistance / (float)_softness : 1.f;
            } else {
                yMultiplier = 1.f;
            }

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                // distance to the nearest rectangle area vertical edge
                int xDistance =  (std::min)(x - _rectangle.x1, _rectangle.x2 - 1 - x);
                // handle softness
                bool xInRectangle = x >= _rectangle.x1 && x < _rectangle.x2;

                if (xInRectangle) {
                    ///apply softness only within the rectangle
                    xMultiplier = xDistance < _softness ? xDistance / (float)_softness : 1.f;
                } else {
                    xMultiplier = 1.f;
                }

                const PIX *srcPixB = _srcImgB ? (const PIX*)_srcImgB->getPixelAddress(x, y) : NULL;

                if (xInRectangle && yInRectangle) {
                    const PIX *srcPixA = _srcImgA ? (const PIX*)_srcImgA->getPixelAddress(x, y) : NULL;
                    float multiplier = xMultiplier * yMultiplier;

                    for (int k = 0; k < nComponents; ++k) {
                        if (!_process[(nComponents) == 1 ? 3 : k]) {
                            tmpPix[k] = srcPixB ? srcPixB[k] : 0.f;
                        } else {
                            PIX A = srcPixA ? srcPixA[k] : PIX();
                            PIX B = srcPixB ? srcPixB[k] : PIX();
                            tmpPix[k] = A *  multiplier + B * (1.f - multiplier);
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
    } // multiThreadProcessImages
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class CopyRectanglePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    CopyRectanglePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
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
        assert( _srcClipB && (!_srcClipA->isConnected() || _srcClipB->getPixelComponents() == ePixelComponentAlpha || _srcClipB->getPixelComponents() == ePixelComponentRGB || _srcClipB->getPixelComponents() == ePixelComponentRGBA) );
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

        // honor kParamDefaultsNormalised
        if ( paramExists(kParamDefaultsNormalised) ) {
            // Some hosts (e.g. Resolve) may not support normalized defaults (setDefaultCoordinateSystem(eCoordinatesNormalised))
            // handle these ourselves!
            BooleanParam* param = fetchBooleanParam(kParamDefaultsNormalised);
            assert(param);
            bool normalised = param->getValue();
            if (normalised) {
                OfxPointD size = getProjectExtent();
                OfxPointD origin = getProjectOffset();
                OfxPointD p;
                // we must denormalise all parameters for which setDefaultCoordinateSystem(eCoordinatesNormalised) couldn't be done
                beginEditBlock(kParamDefaultsNormalised);
                p = _btmLeft->getValue();
                _btmLeft->setValue(p.x * size.x + origin.x, p.y * size.y + origin.y);
                p = _size->getValue();
                _size->setValue(p.x * size.x, p.y * size.y);
                param->setValue(false);
                endEditBlock();
            }
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
# ifndef NDEBUG
    BitDepthEnum dstBitDepth    = dst->getPixelDepth();
    PixelComponentEnum dstComponents  = dst->getPixelComponents();
    if ( ( dstBitDepth != _dstClip->getPixelDepth() ) ||
         ( dstComponents != _dstClip->getPixelComponents() ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
# endif
    auto_ptr<const Image> srcA( ( _srcClipA && _srcClipA->isConnected() ) ?
                                     _srcClipA->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( srcA.get() ) {
        checkBadRenderScaleOrField(srcA, args);
        BitDepthEnum srcBitDepth      = srcA->getPixelDepth();
        PixelComponentEnum srcComponents = srcA->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
# endif
    auto_ptr<const Image> srcB( ( _srcClipB && _srcClipB->isConnected() ) ?
                                     _srcClipB->fetchImage(args.time) : 0 );
# ifndef NDEBUG
    if ( srcB.get() ) {
        checkBadRenderScaleOrField(srcB, args);
        BitDepthEnum srcBitDepth      = srcB->getPixelDepth();
        PixelComponentEnum srcComponents = srcB->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
# endif
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
        CopyRectangleProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        CopyRectangleProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        CopyRectangleProcessor<float, nComponents, 1> fred(*this);
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

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            if (getImageEffectHostDescription()->supportsMultiResolution) {
                // In Sony Catalyst Edit, clipGetRegionOfDefinition returns the RoD in pixels instead of canonical coordinates.
                // In hosts that do not support multiResolution (e.g. Sony Catalyst Edit), all inputs have the same RoD anyway.
                Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(args.time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
                // effect is identity if the renderWindow doesn't intersect the mask RoD
                if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                    identityClip = _srcClipB;

                    return true;
                }
            }
        }
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

mDeclarePluginFactory(CopyRectanglePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
CopyRectanglePluginFactory::describe(ImageEffectDescriptor &desc)
{
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
    desc.setOverlayInteractDescriptor(new RectangleOverlayDescriptor);

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
}

ImageEffect*
CopyRectanglePluginFactory::createInstance(OfxImageEffectHandle handle,
                                           ContextEnum /*context*/)
{
    return new CopyRectanglePlugin(handle);
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

void
CopyRectanglePluginFactory::describeInContext(ImageEffectDescriptor &desc,
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

    // btmLeft
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamRectangleInteractBtmLeft);
        param->setLabel(kParamRectangleInteractBtmLeftLabel);
        param->setDoubleType(eDoubleTypeXYAbsolute);
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
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
        if ( param->supportsDefaultCoordinateSystem() ) {
            param->setDefaultCoordinateSystem(eCoordinatesNormalised); // no need of kParamDefaultsNormalised
        } else {
            gHostSupportsDefaultCoordinateSystem = false; // no multithread here, see kParamDefaultsNormalised
        }
        param->setDefault(1., 1.);
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

    defineComponentParam(desc, page, kParamProcessR, kParamProcessRLabel, false);
    defineComponentParam(desc, page, kParamProcessG, kParamProcessGLabel, false);
    defineComponentParam(desc, page, kParamProcessB, kParamProcessBLabel, false);
    defineComponentParam(desc, page, kParamProcessA, kParamProcessALabel, true);

    // softness
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabel(kParamSoftnessLabel);
        param->setDefault(0);
        param->setRange(0., 100.);
        param->setDisplayRange(0., 100.);
        param->setIncrement(1.);
        param->setHint(kParamSoftnessHint);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsMaskMixDescribeParams(desc, page);
} // CopyRectanglePluginFactory::describeInContext

static CopyRectanglePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
