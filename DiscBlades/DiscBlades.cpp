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
 * OFX DiscBlades plugin.
 * Generate the shape of a lens aperture (disc or polygon with curved blades).
 */

#include <cmath>
#include <algorithm>
#include <cfloat> // DBL_MAX
#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

#include "ofxsProcessing.H"
#include "ofxsMacros.h"
#include "ofxsGenerator.h"
#include "ofxsLut.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "DiscBladesOFX"
#define kPluginGrouping "Draw"
#define kPluginDescription \
    "Generate the shape of a lens aperture: a disc, or a polygon formed by the given number of diaphragm blades, " \
    "with optionally curved blades and soft edges.\n" \
    "The shape is white, with an antialiased edge, and the output is premultiplied (alpha is the shape coverage).\n" \
    "The shape is centered on the center of the output extent (plus the given offset), " \
    "so that it can be used directly as the Filter input of the ConvolveCImg node to simulate lens defocus (bokeh)."

#define kPluginIdentifier "net.sf.openfx.DiscBlades"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsByte true
#define kSupportsUShort true
#define kSupportsHalf false
#define kSupportsFloat true

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

#define kParamRadius "radius"
#define kParamRadiusLabel "Radius"
#define kParamRadiusHint "Radius of the shape (distance from the center to the polygon vertices), in pixels."
#define kParamRadiusDefault 50.

#define kParamBlades "blades"
#define kParamBladesLabel "Blades"
#define kParamBladesHint "Number of diaphragm blades. 3 or more gives a polygon with that many sides; 0, 1 or 2 give a perfect disc."
#define kParamBladesDefault 0

#define kParamRotation "rotation"
#define kParamRotationLabel "Rotation"
#define kParamRotationHint "Rotation of the polygon, in degrees (counter-clockwise). At 0, a vertex points to the right."
#define kParamRotationDefault 0.

#define kParamCurvature "curvature"
#define kParamCurvatureLabel "Blade Curvature"
#define kParamCurvatureHint "Curvature of the blades. 0 gives straight polygon edges, 1 gives a circle going through the polygon vertices."
#define kParamCurvatureDefault 0.

#define kParamSoftness "softness"
#define kParamSoftnessLabel "Softness"
#define kParamSoftnessHint "Width of the soft edge, inside the shape, as a fraction of the radius. 0 gives a sharp (but antialiased) edge."
#define kParamSoftnessDefault 0.

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "Offset of the shape center with respect to the center of the output extent, in pixels."

#ifdef OFX_EXTENSIONS_NATRON
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentXY || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#else
#define OFX_COMPONENTS_OK(c) ((c)== ePixelComponentAlpha || (c) == ePixelComponentRGB || (c) == ePixelComponentRGBA)
#endif


class DiscBladesProcessorBase
    : public ImageProcessor
{
protected:
    // all values in pixel coordinates at the current render scale, y-axis units
    double _cx, _cy; // center of the shape
    double _par; // x pixel coordinate * par = canonical x coordinate
    double _radius;
    int _blades;
    double _rotation; // radians
    double _curvature;
    double _softness; // width of the soft edge, in pixels

public:
    DiscBladesProcessorBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _cx(0.)
        , _cy(0.)
        , _par(1.)
        , _radius(0.)
        , _blades(0)
        , _rotation(0.)
        , _curvature(0.)
        , _softness(0.)
    {
    }

    void setValues(const OfxPointD &renderScale,
                   double pixelAspectRatio,
                   const OfxRectD &rod,
                   const OfxPointD &offset,
                   double radius,
                   int blades,
                   double rotation,
                   double curvature,
                   double softness)
    {
        if (pixelAspectRatio <= 0.) {
            pixelAspectRatio = 1.;
        }
        // we work in a coordinate system where the y unit is a pixel, and x is scaled by the PAR,
        // so that the shape is not distorted.
        _par = pixelAspectRatio;
        _cx = ( (rod.x1 + rod.x2) * 0.5 + offset.x ) * renderScale.x;
        _cy = ( (rod.y1 + rod.y2) * 0.5 + offset.y ) * renderScale.y;
        // radius is measured along y (x is already corrected by the PAR)
        _radius = (std::max)(0., radius) * renderScale.y;
        _blades = blades < 3 ? 0 : blades;
        _rotation = rotation * M_PI / 180.;
        _curvature = (std::max)( 0., (std::min)(1., curvature) );
        _softness = (std::max)(0., softness) * _radius;
    }

    // coverage of the pixel centered at (x,y), in canonical-like coordinates with pixel units
    double coverage(double x,
                    double y) const
    {
        const double dx = x - _cx;
        const double dy = y - _cy;
        const double r = std::sqrt(dx * dx + dy * dy);
        // signed distance to the edge (positive inside), in pixels
        double d = _radius - r; // disc
        if ( (_blades >= 3) && (_curvature < 1.) ) {
            const double sector = 2. * M_PI / _blades;
            double phi = std::atan2(dy, dx) - _rotation;
            // angle within the sector, in [0, sector)
            phi -= sector * std::floor(phi / sector);
            // distance to the polygon edge line of this sector
            const double dline = _radius * std::cos(M_PI / _blades) - r * std::cos(phi - M_PI / _blades);
            d = (1. - _curvature) * dline + _curvature * d;
        }
        // antialiasing over one pixel, plus the soft edge inside the shape
        double t = (d + 0.5) / (_softness + 1.);
        if (t <= 0.) {
            return 0.;
        }
        if (t >= 1.) {
            return 1.;
        }

        return t * t * (3. - 2. * t); // smoothstep
    }
};

template <class PIX, int nComponents, int maxValue>
class DiscBladesProcessor
    : public DiscBladesProcessorBase
{
public:
    DiscBladesProcessor(ImageEffect &instance)
        : DiscBladesProcessorBase(instance)
    {
    }

private:
    // coverage is written linearly (no transfer function), in all channels, so that the
    // premultiplied output is consistent (color == alpha) and can be used as a linear kernel
    static PIX toPIX(float v)
    {
        if (maxValue == 1) { // implies float, don't clamp
            return static_cast<PIX>(v);
        }

        return static_cast<PIX>( Color::floatToInt<maxValue + 1>(v) );
    }

    void multiThreadProcessImages(const OfxRectI& procWindow, const OfxPointD& rs) OVERRIDE FINAL
    {
        unused(rs);
        const PIX zero = toPIX(0.f);
        // bounding box of the shape, in pixel coordinates, to skip pixels quickly
        const double extent = _radius + 1.;
        const double bx1 = (_cx - extent) / _par - 1., bx2 = (_cx + extent) / _par + 1.;
        const double by1 = _cy - extent - 1., by2 = _cy + extent + 1.;

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            const bool yout = (y + 1 < by1) || (y > by2);
            for (int x = procWindow.x1; x < procWindow.x2; x++, dstPix += nComponents) {
                const bool outside = yout || (x + 1 < bx1) || (x > bx2);
                // white, premultiplied by the coverage (coverage() already returns a value in [0,1])
                const PIX v = outside ? zero : toPIX( (float)coverage( (x + 0.5) * _par, y + 0.5 ) );
                for (int c = 0; c < nComponents; ++c) {
                    dstPix[c] = v;
                }
            }
        }
    } // multiThreadProcessImages
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DiscBladesPlugin
    : public GeneratorPlugin
{
public:
    /** @brief ctor */
    DiscBladesPlugin(OfxImageEffectHandle handle)
        : GeneratorPlugin(handle, true, kSupportsByte, kSupportsUShort, kSupportsHalf, kSupportsFloat)
        , _srcClip(NULL)
        , _radius(NULL)
        , _blades(NULL)
        , _rotation(NULL)
        , _curvature(NULL)
        , _softness(NULL)
        , _offset(NULL)
    {
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( _srcClip && (!_srcClip->isConnected() || OFX_COMPONENTS_OK(_srcClip->getPixelComponents())) );
        _radius = fetchDoubleParam(kParamRadius);
        _blades = fetchIntParam(kParamBlades);
        _rotation = fetchDoubleParam(kParamRotation);
        _curvature = fetchDoubleParam(kParamCurvature);
        _softness = fetchDoubleParam(kParamSoftness);
        _offset = fetchDouble2DParam(kParamOffset);
        assert(_radius && _blades && _rotation && _curvature && _softness && _offset);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(DiscBladesProcessorBase &, const RenderArguments &args);

    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

private:
    Clip *_srcClip;
    DoubleParam *_radius;
    IntParam *_blades;
    DoubleParam *_rotation;
    DoubleParam *_curvature;
    DoubleParam *_softness;
    Double2DParam *_offset;
};


/* set up and run a processor */
void
DiscBladesPlugin::setupAndProcess(DiscBladesProcessorBase &processor,
                                  const RenderArguments &args)
{
    const double time = args.time;

    // get a dst image
    auto_ptr<Image>  dst( _dstClip->fetchImage(time) );

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

    // set the images
    processor.setDstImg( dst.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow, args.renderScale);

    const double radius = _radius->getValueAtTime(time);
    const int blades = _blades->getValueAtTime(time);
    const double rotation = _rotation->getValueAtTime(time);
    const double curvature = _curvature->getValueAtTime(time);
    const double softness = _softness->getValueAtTime(time);
    OfxPointD offset;
    _offset->getValueAtTime(time, offset.x, offset.y);
    OfxRectD rod = {0, 0, 0, 0};
    if ( !getRegionOfDefinition(time, rod) ) {
        // "Default" extent: the host uses its default RoD, which in the general context is
        // the Source RoD if Source is connected, else the project window (OFX spec,
        // kOfxImageEffectActionGetRegionOfDefinition), so center the shape on that.
        if ( _srcClip && _srcClip->isConnected() ) {
            rod = _srcClip->getRegionOfDefinition(time);
        } else {
            OfxPointD siz = getProjectSize();
            OfxPointD off = getProjectOffset();
            rod.x1 = off.x;
            rod.x2 = off.x + siz.x;
            rod.y1 = off.y;
            rod.y2 = off.y + siz.y;
        }
    }
    processor.setValues(args.renderScale, dst->getPixelAspectRatio(), rod, offset, radius, blades, rotation, curvature, softness);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // DiscBladesPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
DiscBladesPlugin::renderInternal(const RenderArguments &args,
                                 BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        DiscBladesProcessor<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        DiscBladesProcessor<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        DiscBladesProcessor<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
DiscBladesPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(OFX_COMPONENTS_OK(dstComponents));

    checkComponents(dstBitDepth, dstComponents);

    // do the rendering
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

void
DiscBladesPlugin::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // output is continuous
    clipPreferences.setOutputHasContinuousSamples(true);

    GeneratorPlugin::getClipPreferences(clipPreferences);
    clipPreferences.setOutputPremultiplication(eImagePreMultiplied);
}

mDeclarePluginFactory(DiscBladesPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
DiscBladesPluginFactory::describe(ImageEffectDescriptor &desc)
{
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextGenerator);
    desc.addSupportedContext(eContextGeneral);
    if (kSupportsByte) {
        desc.addSupportedBitDepth(eBitDepthUByte);
    }
    if (kSupportsUShort) {
        desc.addSupportedBitDepth(eBitDepthUShort);
    }
    if (kSupportsFloat) {
        desc.addSupportedBitDepth(eBitDepthFloat);
    }

    // set a few flags
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setSupportsMultiResolution(kSupportsMultiResolution);
    desc.setSupportsTiles(kSupportsTiles);
    desc.setTemporalClipAccess(false);
    desc.setRenderTwiceAlways(false);
    desc.setSupportsMultipleClipPARs(kSupportsMultipleClipPARs);
    desc.setSupportsMultipleClipDepths(kSupportsMultipleClipDepths);
    desc.setRenderThreadSafety(kRenderThreadSafety);

    generatorDescribe(desc);
#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone);
#endif
}

void
DiscBladesPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                           ContextEnum context)
{
    // there has to be an input clip, even for generators
    ClipDescriptor* srcClip = desc.defineClip( kOfxImageEffectSimpleSourceClipName );

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setOptional(true);

    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    PageParamDescriptor *page = desc.definePageParam("Controls");

    generatorDescribeInContext(page, desc, *dstClip, eGeneratorExtentDefault, ePixelComponentRGBA, true, context);

    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRadius);
        param->setLabel(kParamRadiusLabel);
        param->setHint(kParamRadiusHint);
        param->setDefault(kParamRadiusDefault);
        param->setRange(0., DBL_MAX);
        param->setDisplayRange(0., 500.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamBlades);
        param->setLabel(kParamBladesLabel);
        param->setHint(kParamBladesHint);
        param->setDefault(kParamBladesDefault);
        param->setRange(0, 100);
        param->setDisplayRange(0, 16);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamRotation);
        param->setLabel(kParamRotationLabel);
        param->setHint(kParamRotationHint);
        param->setDoubleType(eDoubleTypeAngle);
        param->setDefault(kParamRotationDefault);
        param->setRange(-DBL_MAX, DBL_MAX);
        param->setDisplayRange(-180., 180.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamCurvature);
        param->setLabel(kParamCurvatureLabel);
        param->setHint(kParamCurvatureHint);
        param->setDefault(kParamCurvatureDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamSoftness);
        param->setLabel(kParamSoftnessLabel);
        param->setHint(kParamSoftnessHint);
        param->setDefault(kParamSoftnessDefault);
        param->setRange(0., 1.);
        param->setDisplayRange(0., 1.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        Double2DParamDescriptor* param = desc.defineDouble2DParam(kParamOffset);
        param->setLabel(kParamOffsetLabel);
        param->setHint(kParamOffsetHint);
        param->setDoubleType(eDoubleTypeXY);
        param->setDefaultCoordinateSystem(eCoordinatesCanonical); // Nuke defaults to Normalized for XY and XYAbsolute!
        param->setDefault(0., 0.);
        param->setRange(-DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX);
        param->setDisplayRange(-100., -100., 100., 100.);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
} // DiscBladesPluginFactory::describeInContext

ImageEffect*
DiscBladesPluginFactory::createInstance(OfxImageEffectHandle handle,
                                        ContextEnum /*context*/)
{
    return new DiscBladesPlugin(handle);
}

static DiscBladesPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
