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
 * OFX TestHostBehaviour plugins.
 *
 * Small filters that each exercise one corner of the OFX spec, to check how a
 * host handles it (see https://github.com/NatronGitHub/openfx-misc/issues/17).
 * Tiles, multiresolution and render scale are covered by TestRender.
 */

#include <climits> // INT_MAX
#include <cmath> // std::floor

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsPixelProcessor.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginGrouping "Other/Test"
#define kPluginIdentifier "net.sf.openfx.TestHostBehaviour"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kCheckerSize 64. // checkerboard square size, in canonical coordinates

enum TestBehaviourEnum
{
    eTestIdentityLeftHalf,
    eTestGarbageOutsideRenderWindow,
    eTestInfiniteRoI,
    eTestInfiniteRoD,
};

// Copies the source, with optional modifications that depend on the tested behaviour.
template <class PIX, int nComponents, int maxValue>
class TestHostBehaviourProcessor
    : public PixelProcessorFilterBase
{
public:
    TestHostBehaviourProcessor(ImageEffect &instance)
        : PixelProcessorFilterBase(instance)
        , _invertFromX(INT_MAX)
        , _garbageOutside(false)
        , _checkerOutsideSrc(false)
        , _srcRoD()
        , _par(1.)
    {
    }

    void setInvertFromX(int x) { _invertFromX = x; }
    void setGarbageOutside(bool v) { _garbageOutside = v; }
    void setCheckerOutsideSrc(bool v, const OfxRectI& srcRoD, double par) { _checkerOutsideSrc = v; _srcRoD = srcRoD; _par = par; }

private:
    // Single-threaded, before the multi-threaded render of the renderWindow.
    virtual void preProcess() OVERRIDE FINAL
    {
        if (!_garbageOutside) {
            return;
        }
        // BUG ON PURPOSE: write opaque magenta to every pixel of the output image outside of the renderWindow.
        for (int y = _dstBounds.y1; y < _dstBounds.y2; ++y) {
            for (int x = _dstBounds.x1; x < _dstBounds.x2; ++x) {
                if ( (_renderWindow.x1 <= x) && (x < _renderWindow.x2) && (_renderWindow.y1 <= y) && (y < _renderWindow.y2) ) {
                    continue;
                }
                PIX *dstPix = (PIX *) getDstPixelAddress(x, y);
                for (int c = 0; c < nComponents; ++c) {
                    dstPix[c] = (c == 1 && nComponents >= 3) ? PIX(0) : PIX(maxValue);
                }
            }
        }
    }

    virtual void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }
            PIX *dstPix = (PIX *) getDstPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                // Test the source RoD rather than the image bounds, since the host may give a larger (black) image.
                bool checker = _checkerOutsideSrc && ( (x < _srcRoD.x1) || (x >= _srcRoD.x2) || (y < _srcRoD.y1) || (y >= _srcRoD.y2) );
                const PIX *srcPix = checker ? NULL : (const PIX *) getSrcPixelAddress(x, y);
                // checkerboard in canonical coordinates, so that it looks the same at all render scales
                PIX checkerValue = 0;
                if (checker) {
                    OfxPointI p = {x, y};
                    OfxPointD pc;
                    Coords::toCanonical(p, rs, _par, &pc);
                    bool light = ( (int)std::floor(pc.x / kCheckerSize) + (int)std::floor(pc.y / kCheckerSize) ) & 1;
                    checkerValue = PIX(light ? maxValue : maxValue * 0.5);
                }
                for (int c = 0; c < nComponents; ++c) {
                    PIX v = 0; // transparent black outside of the source image
                    if (srcPix) {
                        v = srcPix[c];
                    } else if (checker) {
                        v = (c == 3) ? PIX(maxValue) : checkerValue; // opaque checkerboard
                    }
                    if ( (x >= _invertFromX) && ( (nComponents == 1) || (c < 3) ) ) {
                        v = PIX(maxValue - v);
                    }
                    dstPix[c] = v;
                }
            }
        }
    }

    int _invertFromX; // color is inverted for pixels with x >= _invertFromX
    bool _garbageOutside;
    bool _checkerOutsideSrc; // fill pixels outside of _srcRoD with a checkerboard
    OfxRectI _srcRoD;
    double _par;
};

template <TestBehaviourEnum behaviour>
class TestHostBehaviourPlugin
    : public ImageEffect
{
public:
    TestHostBehaviourPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip( fetchClip(kOfxImageEffectOutputClipName) )
        , _srcClip( fetchClip(kOfxImageEffectSimpleSourceClipName) )
    {
        assert(_dstClip && _srcClip);
    }

private:
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;
    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime
#ifdef OFX_EXTENSIONS_NUKE
                            , int& view, std::string& plane
#endif
                            ) OVERRIDE FINAL;
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;
    virtual void getRegionsOfInterest(const RegionsOfInterestArguments &args, RegionOfInterestSetter &rois) OVERRIDE FINAL;

    template <class PIX, int nComponents, int maxValue>
    void renderForDepthAndComponents(const RenderArguments &args, Image* dst, const Image* src);
    template <int nComponents>
    void renderForComponents(const RenderArguments &args, Image* dst, const Image* src);

    // For eTestIdentityLeftHalf: pixels with x >= midX(), i.e. the right half of the source RoD, are inverted.
    // isIdentity and render must use the same value, so that the result does not depend on the renderWindow.
    int midX(double time, const OfxPointD& renderScale) const
    {
        OfxRectI rod;
        Coords::toPixelEnclosing(_srcClip->getRegionOfDefinition(time), renderScale, _srcClip->getPixelAspectRatio(), &rod);

        // in double: rod may keep the kOfxFlagInfinite* values, and rod.x2 - rod.x1 would overflow
        return (int)std::floor( 0.5 * ( (double)rod.x1 + (double)rod.x2 ) );
    }

    Clip *_dstClip;
    Clip *_srcClip;
};

template <TestBehaviourEnum behaviour>
template <class PIX, int nComponents, int maxValue>
void
TestHostBehaviourPlugin<behaviour>::renderForDepthAndComponents(const RenderArguments &args,
                                                               Image* dst,
                                                               const Image* src)
{
    TestHostBehaviourProcessor<PIX, nComponents, maxValue> processor(*this);
    processor.setDstImg(dst);
    if (src) {
        processor.setSrcImg(src);
    }
    if (behaviour == eTestIdentityLeftHalf) {
        processor.setInvertFromX( midX(args.time, args.renderScale) );
    }
    processor.setGarbageOutside(behaviour == eTestGarbageOutsideRenderWindow);
    if (behaviour == eTestInfiniteRoD) {
        OfxRectI srcRoD;
        Coords::toPixelEnclosing(_srcClip->getRegionOfDefinition(args.time), args.renderScale, _srcClip->getPixelAspectRatio(), &srcRoD);
        processor.setCheckerOutsideSrc( true, srcRoD, _dstClip->getPixelAspectRatio() );
    }
    processor.setRenderWindow(args.renderWindow, args.renderScale);
    processor.process();
}

template <TestBehaviourEnum behaviour>
template <int nComponents>
void
TestHostBehaviourPlugin<behaviour>::renderForComponents(const RenderArguments &args,
                                                       Image* dst,
                                                       const Image* src)
{
    switch ( dst->getPixelDepth() ) {
    case eBitDepthUByte:
        renderForDepthAndComponents<unsigned char, nComponents, 255>(args, dst, src);
        break;
    case eBitDepthUShort:
        renderForDepthAndComponents<unsigned short, nComponents, 65535>(args, dst, src);
        break;
    case eBitDepthFloat:
        renderForDepthAndComponents<float, nComponents, 1>(args, dst, src);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <TestBehaviourEnum behaviour>
void
TestHostBehaviourPlugin<behaviour>::render(const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    checkBadRenderScaleOrField(dst, args);
    auto_ptr<const Image> src( _srcClip->isConnected() ? _srcClip->fetchImage(args.time) : 0 );
    if ( src.get() ) {
        checkBadRenderScaleOrField(src, args);
        if ( ( src->getPixelDepth() != dst->getPixelDepth() ) || ( src->getPixelComponents() != dst->getPixelComponents() ) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    switch ( dst->getPixelComponentCount() ) {
    case 4:
        renderForComponents<4>( args, dst.get(), src.get() );
        break;
    case 3:
        renderForComponents<3>( args, dst.get(), src.get() );
        break;
    case 1:
        renderForComponents<1>( args, dst.get(), src.get() );
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <TestBehaviourEnum behaviour>
bool
TestHostBehaviourPlugin<behaviour>::isIdentity(const IsIdentityArguments &args,
                                              Clip * &identityClip,
                                              double & /*identityTime*/
#ifdef OFX_EXTENSIONS_NUKE
                                              , int& /*view*/, std::string& /*plane*/
#endif
                                              )
{
    // Identity only if the whole renderWindow is in the left half.
    // A host that reuses this result for another renderWindow (or tile) will show a non-inverted right half.
    if ( (behaviour == eTestIdentityLeftHalf) && ( args.renderWindow.x2 <= midX(args.time, args.renderScale) ) ) {
        identityClip = _srcClip;

        return true;
    }

    return false;
}

template <TestBehaviourEnum behaviour>
bool
TestHostBehaviourPlugin<behaviour>::getRegionOfDefinition(const RegionOfDefinitionArguments & /*args*/,
                                                         OfxRectD &rod)
{
    if (behaviour == eTestInfiniteRoD) {
        rod.x1 = rod.y1 = kOfxFlagInfiniteMin;
        rod.x2 = rod.y2 = kOfxFlagInfiniteMax;

        return true;
    }

    return false; // default RoD: the source RoD
}

template <TestBehaviourEnum behaviour>
void
TestHostBehaviourPlugin<behaviour>::getRegionsOfInterest(const RegionsOfInterestArguments & /*args*/,
                                                        RegionOfInterestSetter &rois)
{
    if (behaviour == eTestInfiniteRoI) {
        OfxRectD roi;
        roi.x1 = roi.y1 = kOfxFlagInfiniteMin;
        roi.x2 = roi.y2 = kOfxFlagInfiniteMax;
        rois.setRegionOfInterest(*_srcClip, roi);
    }
    // else default RoI: the requested region
}

template <TestBehaviourEnum behaviour>
class TestHostBehaviourPluginFactory
    : public PluginFactoryHelper<TestHostBehaviourPluginFactory<behaviour> >
{
public:
    TestHostBehaviourPluginFactory(const std::string& id,
                                   unsigned int verMaj,
                                   unsigned int verMin)
        : PluginFactoryHelper<TestHostBehaviourPluginFactory<behaviour> >(id, verMaj, verMin)
    {
    }

    virtual void load() OVERRIDE FINAL { ofxsThreadSuiteCheck(); }
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum /*context*/) OVERRIDE FINAL
    {
        return new TestHostBehaviourPlugin<behaviour>(handle);
    }
};

template <TestBehaviourEnum behaviour>
void
TestHostBehaviourPluginFactory<behaviour>::describe(ImageEffectDescriptor &desc)
{
    switch (behaviour) {
    case eTestIdentityLeftHalf:
        desc.setLabel("TestIdentityLeftHalfOFX");
        desc.setPluginDescription("Test the renderWindow of isIdentity.\n"
                                  "Copies the input on the left half of its region of definition and inverts the color on the right half. "
                                  "isIdentity returns true only if the renderWindow is entirely in the left half, "
                                  "so the output must not depend on how the host tiles the image. "
                                  "A non-inverted right half means that the host reused an isIdentity result "
                                  "for a different renderWindow or tile.\n"
                                  "Usage: in a viewer, zoom so that only the left half is visible, then pan to the right: "
                                  "the right half must be inverted.\n"
                                  "The RGB values are inverted without taking alpha into account (premultiplication is ignored), "
                                  "so use an opaque input.");
        break;
    case eTestGarbageOutsideRenderWindow:
        desc.setLabel("TestGarbageOutsideRenderWindowOFX");
        desc.setPluginDescription("Test host robustness to a buggy plugin.\n"
                                  "Copies the input inside the renderWindow, but also (wrongly) writes opaque magenta "
                                  "to every pixel of the output image outside the renderWindow. "
                                  "Magenta areas in the output show that the host used pixels it did not ask the plugin to render.\n"
                                  "Host frame threading is enabled, so a host that splits the image into several renderWindows "
                                  "runs concurrent render calls that overwrite each other's areas: "
                                  "the corruption is non-deterministic on purpose.");
        break;
    case eTestInfiniteRoI:
        desc.setLabel("TestInfiniteRoIOFX");
        desc.setPluginDescription("Test infinite regions of interest.\n"
                                  "Copies the input, but asks for an infinite region of interest on the input for any renderWindow. "
                                  "The host should clip it to the input region of definition. "
                                  "A host that does not may hang, run out of memory or crash.");
        break;
    case eTestInfiniteRoD:
        desc.setLabel("TestInfiniteRoDOFX");
        desc.setPluginDescription("Test infinite regions of definition.\n"
                                  "Returns an infinite region of definition, copies the input where it is defined, "
                                  "and fills the rest of the plane with an opaque gray and white checkerboard "
                                  "(squares of 64 canonical units, i.e. 64 pixels at full resolution with PAR 1). "
                                  "On alpha-only images, the checkerboard is in the alpha channel (0.5 and 1). "
                                  "The host should clip the region of definition to an appropriate rectangle, typically the project extent.");
        break;
    }
    desc.setPluginGrouping(kPluginGrouping);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    desc.setSingleInstance(false);
    // GarbageOutsideRenderWindow: let the host run concurrent renders of the same frame, so that they overwrite each other
    desc.setHostFrameThreading(behaviour == eTestGarbageOutsideRenderWindow);
    desc.setSupportsMultiResolution(true);
    desc.setSupportsTiles(true);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(false);
    desc.setSupportsMultipleClipDepths(false);
    desc.setRenderThreadSafety(eRenderFullySafe);
}

template <TestBehaviourEnum behaviour>
void
TestHostBehaviourPluginFactory<behaviour>::describeInContext(ImageEffectDescriptor &desc,
                                                            ContextEnum /*context*/)
{
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);
}

static TestHostBehaviourPluginFactory<eTestIdentityLeftHalf> p1(kPluginIdentifier "_IdentityLeftHalf", kPluginVersionMajor, kPluginVersionMinor);
static TestHostBehaviourPluginFactory<eTestInfiniteRoI> p3(kPluginIdentifier "_InfiniteRoI", kPluginVersionMajor, kPluginVersionMinor);
static TestHostBehaviourPluginFactory<eTestInfiniteRoD> p4(kPluginIdentifier "_InfiniteRoD", kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p3)
mRegisterPluginFactoryInstance(p4)
#ifdef DEBUG
// buggy on purpose: only in debug builds
static TestHostBehaviourPluginFactory<eTestGarbageOutsideRenderWindow> p2(kPluginIdentifier "_GarbageOutsideRenderWindow", kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p2)
#endif

OFXS_NAMESPACE_ANONYMOUS_EXIT
