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
 * OFX PLogLin plugin.
 */

#include <cmath>
#include <algorithm>
//#include <iostream>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "PLogLinOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Convert between logarithmic and linear encoding.\n" \
    "This method uses the so-called \"Josh Pines log conversion\" or \"printing density transform\" (as described in http://lists.gnu.org/archive/html/openexr-devel/2005-03/msg00006.html), which is based on a single gray point, rather than the white and black points in the Cineon formula (as implemented in the Log2Lin plugin).\n" \
    "Log to Lin conversion: xLin = linRef * pow( 10.0, (xLog * 1023. - logRef)*density/nGamma )\n" \
    "Lin to Log conversion: xLog = (logRef + log10(max( xLin, 1e-10 ) / linRef)*nGamma/density) / 1023."

#define kPluginIdentifier "net.sf.openfx.PLogLin"
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
//#define kParamProcessA kNatronOfxParamProcessA
//#define kParamProcessALabel kNatronOfxParamProcessALabel
//#define kParamProcessAHint kNatronOfxParamProcessAHint
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
//#define kParamProcessA      "processA"
//#define kParamProcessALabel "A"
//#define kParamProcessAHint  "Process alpha component."
#endif

/*
   http://lists.gnu.org/archive/html/openexr-devel/2005-03/msg00006.html
   From:        Ken McGaugh
   Subject:     Re: [Openexr-devel] Cineon -> half conversion?
   Date:        Wed, 02 Mar 2005 10:24:21 +0000

   Paul Miller wrote:

   Does anyone have a good reference for Cineon log -> linear half conversion?


   The method I recommend comes from Josh Pines (who monitors this list).  I'll
   summarize it by showing how we do it here, including the header attributes we
   use to store the parameters controlling the conversion.

   The attributes are prefixed with "pdx" which stands for Printing Density Xform.
   They look like

   pdxLinReference (type v3f): (0.18 0.18 0.18)
   pdxLogReference (type v3f): (445 445 445)
   pdxNegativeGamma (type v3f): (0.6 0.6 0.6)
   pdxDensityPerCodeValue (type v3f): (0.002 0.002 0.002)

   Note that we typically leave out the pdxDensityPerCodeValue and just assume
   a value of 0.002.

   Say you have a cineon code value named xLog in the range 0-1023.  To convert
   it into a linear value xLin you would do

   xLin = pow( 10.0, (xLog - pdxLogReference)*pdxDensityPerCodeValue/pdxnegativeGamma );
   xLin *= pdxLinReference;

   Similarly, to convert back

   xLog = max( xLin, 1e-10 ) / pdxLinReference;
   xLog = pdxLogReference + log10(xLog)*pdxNegativeGamma/pdxDensityPerCodeValue;

   Note that nowhere is there any reference to "black" or "white".  They are video
   concepts and should only be used when preparing pixel values for display.

   --Ken
 */
/*

   Default:
   linref=0.18
   logref=445
   ngamma=0.6
   density=0.002
 */

#define kParamOperation "operation"
#define kParamOperationLabel "Operation"
#define kParamOperationHint "The operation to perform."
#define kParamOperationOptionLog2Lin "Log to Lin", "Convert the input from logarithmic to linear colorspace (usually after a Read node).", "log2lin"
#define kParamOperationOptionLin2Log "Lin to Log", "Convert the input from linear to logarithmic colorspace (usually before a Write node).", "lin2log"

enum OperationEnum
{
    eOperationLog2Lin = 0,
    eOperationLin2Log,
};

#define kParamLinRef "linRef"
#define kParamLinRefLabel "Linear Reference"
#define kParamLinRefHint "Linear value of the reference gray point. Set this to the linear value that corresponds with the log reference value."
#define kParamLinRefDefault 0.18

#define kParamLogRef "logRef"
#define kParamLogRefLabel "Log Reference"
#define kParamLogRefHint "Log value of the reference gray point. Set this to the log value that corresponds with the lin reference value."
#define kParamLogRefDefault 445.

#define kParamNGamma "nGamma"
#define kParamNGammaLabel "Negative Gamma"
#define kParamNGammaHint "The film response gamma value."
#define kParamNGammaDefault 0.6

#define kParamDensity "density"
#define kParamDensityLabel "Density"
#define kParamDensityHint "Density per code value. The change in the negative gamma for each log space code value. This is usually left to the default value of 0.002."
#define kParamDensityDefault 0.002

using namespace OFX;

class PLogLinProcessorBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _premult;
    int _premultChannel;
    bool _doMasking;
    double _mix;
    bool _maskInvert;
    bool _processR, _processG, _processB;
    double _linRef[3];
    double _logRef[3];
    double _nGamma[3];
    double _density[3];

    double log2lin(double xLog,
                   int c)
    {
        return _linRef[c] * std::pow( 10.0, (xLog * 1023. - _logRef[c]) * _density[c] / _nGamma[c] );
    }

    double lin2log(double xLin,
                   int c)
    {
        return (_logRef[c] + std::log10(std::max(xLin, 1e-10) / _linRef[c]) * _nGamma[c] / _density[c]) / 1023.;
    }

public:
    PLogLinProcessorBase(ImageEffect &instance,
                         const RenderArguments & /*args*/)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _premult(false)
        , _premultChannel(3)
        , _doMasking(false)
        , _mix(1.)
        , _maskInvert(false)
        , _processR(false)
        , _processG(false)
        , _processB(false)
        // TODO: initialize plugin parameter values
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) {_maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   double linRef[3],
                   double logRef[3],
                   double nGamma[3],
                   double density[3])
    {
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        for (int c = 0; c < 3; ++c) {
            _linRef[c] = linRef[c];
            _logRef[c] = logRef[c];
            _nGamma[c] = nGamma[c];
            _density[c] = density[c];
        }
    }
};


template <class PIX, int nComponents, int maxValue>
class PLog2LinProcessor
    : public PLogLinProcessorBase
{
public:
    PLog2LinProcessor(ImageEffect &instance,
                      const RenderArguments &args)
        : PLogLinProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        if (r) {
            if (g) {
                if (b) {
                    return process<true, true, true>(procWindow);     // RGBa
                } else {
                    return process<true, true, false>(procWindow);     // RGba
                }
            } else {
                if (b) {
                    return process<true, false, true>(procWindow);     // RgBa
                } else {
                    return process<true, false, false>(procWindow);     // Rgba
                }
            }
        } else {
            if (g) {
                if (b) {
                    return process<false, true, true>(procWindow);     // rGBa
                } else {
                    return process<false, true, false>(procWindow);     // rGba
                }
            } else {
                if (b) {
                    return process<false, false, true>(procWindow);     // rgBa
                } else {
                    return process<false, false, false>(procWindow);     // rgba
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:

    template<bool processR, bool processG, bool processB>
    void process(OfxRectI procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);

                // process the pixel (the actual computation goes here)
                for (int c = 0; c < 3; ++c) {
                    tmpPix[c] = log2lin(unpPix[c], c);
                }
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (!processR) {
                    dstPix[0] = srcPix ? srcPix[0] : PIX();
                }
                if (!processG) {
                    dstPix[1] = srcPix ? srcPix[1] : PIX();
                }
                if (!processB) {
                    dstPix[2] = srcPix ? srcPix[2] : PIX();
                }
                if (nComponents == 4) {
                    dstPix[3] = srcPix ? srcPix[3] : PIX();
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

template <class PIX, int nComponents, int maxValue>
class PLin2LogProcessor
    : public PLogLinProcessorBase
{
public:
    PLin2LogProcessor(ImageEffect &instance,
                      const RenderArguments &args)
        : PLogLinProcessorBase(instance, args)
    {
        //const double time = args.time;

        // TODO: any pre-computation goes here (such as computing a LUT)
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        if (r) {
            if (g) {
                if (b) {
                    return process<true, true, true>(procWindow);     // RGBa
                } else {
                    return process<true, true, false>(procWindow);     // RGba
                }
            } else {
                if (b) {
                    return process<true, false, true>(procWindow);     // RgBa
                } else {
                    return process<true, false, false>(procWindow);     // Rgba
                }
            }
        } else {
            if (g) {
                if (b) {
                    return process<false, true, true>(procWindow);     // rGBa
                } else {
                    return process<false, true, false>(procWindow);     // rGba
                }
            } else {
                if (b) {
                    return process<false, false, true>(procWindow);     // rgBa
                } else {
                    return process<false, false, false>(procWindow);     // rgba
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:

    template<bool processR, bool processG, bool processB>
    void process(OfxRectI procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        float unpPix[4];
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);

                // process the pixel (the actual computation goes here)
                for (int c = 0; c < 3; ++c) {
                    tmpPix[c] = lin2log(unpPix[c], c);
                }
                tmpPix[3] = unpPix[3];
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (!processR) {
                    dstPix[0] = srcPix ? srcPix[0] : PIX();
                }
                if (!processG) {
                    dstPix[1] = srcPix ? srcPix[1] : PIX();
                }
                if (!processB) {
                    dstPix[2] = srcPix ? srcPix[2] : PIX();
                }
                if (nComponents == 4) {
                    dstPix[3] = srcPix ? srcPix[3] : PIX();
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class PLogLinPlugin
    : public ImageEffect
{
public:

    /** @brief ctor */
    PLogLinPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _operation(NULL)
        , _linRef(NULL)
        , _logRef(NULL)
        , _nGamma(NULL)
        , _density(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);

        // TODO: fetch noise parameters

        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        assert(_processR && _processG && _processB);

        _operation = fetchChoiceParam(kParamOperation);
        _linRef = fetchRGBParam(kParamLinRef);
        _logRef = fetchRGBParam(kParamLogRef);
        _nGamma = fetchRGBParam(kParamNGamma);
        _density = fetchRGBParam(kParamDensity);
        assert(_operation && _linRef && _logRef && _nGamma && _density);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template<int nComponents>
    void renderForComponents(const RenderArguments &args);

    template <class PIX, int nComponents, int maxValue>
    void renderForBitDepth(const RenderArguments &args);

    /* set up and run a processor */
    void setupAndProcess(PLogLinProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    ChoiceParam* _operation;
    RGBParam* _linRef;
    RGBParam* _logRef;
    RGBParam* _nGamma;
    RGBParam* _density;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
PLogLinPlugin::setupAndProcess(PLogLinProcessorBase &processor,
                               const RenderArguments &args)
{
    const double time = args.time;

    auto_ptr<Image> dst( _dstClip->fetchImage(time) );

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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(time) : 0 );
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(time) : 0);
    if ( mask.get() ) {
        if ( (mask->getRenderScale().x != args.renderScale.x) ||
             ( mask->getRenderScale().y != args.renderScale.y) ||
             ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
    }
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    // TODO: fetch noise parameter values

    bool premult;
    int premultChannel;
    _premult->getValueAtTime(time, premult);
    _premultChannel->getValueAtTime(time, premultChannel);
    double mix;
    _mix->getValueAtTime(time, mix);

    bool processR, processG, processB;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);

    double linRef[3] = { 0. };
    _linRef->getValueAtTime(time, linRef[0], linRef[1], linRef[2]);
    double logRef[3] = { 0. };
    _logRef->getValueAtTime(time, logRef[0], logRef[1], logRef[2]);
    double nGamma[3] = { 0. };
    _nGamma->getValueAtTime(time, nGamma[0], nGamma[1], nGamma[2]);
    double density[3] = { 0. };
    _density->getValueAtTime(time, density[0], density[1], density[2]);

    processor.setValues(premult, premultChannel, mix,
                        processR, processG, processB, linRef, logRef, nGamma, density);
    processor.process();
} // PLogLinPlugin::setupAndProcess

// the overridden render function
void
PLogLinPlugin::render(const RenderArguments &args)
{
    //std::cout << "render!\n";
    // instantiate the render code based on the pixel depth of the dst clip
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGBA || dstComponents == ePixelComponentRGB);
    // do the rendering
    switch (dstComponents) {
    case ePixelComponentRGBA:
        renderForComponents<4>(args);
        break;
    case ePixelComponentRGB:
        renderForComponents<3>(args);
        break;
    //case ePixelComponentXY:
    //    renderForComponents<2>(args);
    //    break;
    //case ePixelComponentAlpha:
    //    renderForComponents<1>(args);
    //    break;
    default:
        //std::cout << "components usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
        break;
    } // switch
      //std::cout << "render! OK\n";
}

template<int nComponents>
void
PLogLinPlugin::renderForComponents(const RenderArguments &args)
{
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderForBitDepth<unsigned char, nComponents, 255>(args);
        break;

    case eBitDepthUShort:
        renderForBitDepth<unsigned short, nComponents, 65535>(args);
        break;

    case eBitDepthFloat:
        renderForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        //std::cout << "depth usupported\n";
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

template <class PIX, int nComponents, int maxValue>
void
PLogLinPlugin::renderForBitDepth(const RenderArguments &args)
{
    OperationEnum operation = (OperationEnum)_operation->getValueAtTime(args.time);

    switch (operation) {
    case eOperationLog2Lin: {
        PLog2LinProcessor<PIX, nComponents, maxValue> fred(*this, args);
        setupAndProcess(fred, args);
        break;
    }
    case eOperationLin2Log: {
        PLin2LogProcessor<PIX, nComponents, maxValue> fred(*this, args);
        setupAndProcess(fred, args);
        break;
    }
    }
}

bool
PLogLinPlugin::isIdentity(const IsIdentityArguments &args,
                          Clip * &identityClip,
                          double & /*identityTime*/
                          , int& /*view*/, std::string& /*plane*/)
{
    //std::cout << "isIdentity!\n";
    const double time = args.time;
    double mix;

    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB)*/) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        _processR->getValueAtTime(time, processR);
        _processG->getValueAtTime(time, processG);
        _processB->getValueAtTime(time, processB);
        if (!processR && !processG && !processB) {
            identityClip = _srcClip;

            return true;
        }
    }

    // TODO: which plugin parameter values give identity?
    //if (...) {
    //    identityClip = _srcClip;
    //    //std::cout << "isIdentity! true\n";
    //    return true;
    //}

    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(time) ) && _maskClip && _maskClip->isConnected() );
    if (doMasking) {
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        if (!maskInvert) {
            OfxRectI maskRoD;
            Coords::toPixelEnclosing(_maskClip->getRegionOfDefinition(time), args.renderScale, _maskClip->getPixelAspectRatio(), &maskRoD);
            // effect is identity if the renderWindow doesn't intersect the mask RoD
            if ( !Coords::rectIntersection<OfxRectI>(args.renderWindow, maskRoD, 0) ) {
                identityClip = _srcClip;

                return true;
            }
        }
    }

    //std::cout << "isIdentity! false\n";
    return false;
} // PLogLinPlugin::isIdentity

void
PLogLinPlugin::changedClip(const InstanceChangedArgs &args,
                           const std::string &clipName)
{
    //std::cout << "changedClip!\n";
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) && _srcClip && (args.reason == eChangeUserEdit) ) {
        switch ( _srcClip->getPreMultiplication() ) {
        case eImageOpaque:
            _premult->setValue(false);
            break;
        case eImagePreMultiplied:
            _premult->setValue(true);
            break;
        case eImageUnPreMultiplied:
            _premult->setValue(false);
            break;
        }
    }
    //std::cout << "changedClip OK!\n";
}

mDeclarePluginFactory(PLogLinPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
PLogLinPluginFactory::describe(ImageEffectDescriptor &desc)
{
    //std::cout << "describe!\n";
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

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

#ifdef OFX_EXTENSIONS_NATRON
    desc.setChannelSelector(ePixelComponentNone); // we have our own channel selector
#endif
    //std::cout << "describe! OK\n";
}

void
PLogLinPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    //std::cout << "describeInContext!\n";
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentXY);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentXY);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
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

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    // describe plugin params
    {
        ChoiceParamDescriptor* param = desc.defineChoiceParam(kParamOperation);
        param->setLabel(kParamOperationLabel);
        param->setHint(kParamOperationHint);
        assert(param->getNOptions() == eOperationLog2Lin);
        param->appendOption(kParamOperationOptionLog2Lin);
        assert(param->getNOptions() == eOperationLin2Log);
        param->appendOption(kParamOperationOptionLin2Log);
        param->setDefault( (int)eOperationLog2Lin );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamLinRef);
        param->setLabel(kParamLinRefLabel);
        param->setHint(kParamLinRefHint);
        param->setRange(0, 0, 0, 1, 1, 1);
        param->setDisplayRange(0, 0, 0, 1, 1, 1);
        param->setDefault(kParamLinRefDefault, kParamLinRefDefault, kParamLinRefDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamLogRef);
        param->setLabel(kParamLogRefLabel);
        param->setHint(kParamLogRefHint);
        param->setRange(0, 0, 0, 1023, 1023, 1023);
        param->setDisplayRange(0, 0, 0, 1023, 1023, 1023);
        param->setDefault(kParamLogRefDefault, kParamLogRefDefault, kParamLogRefDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamNGamma);
        param->setLabel(kParamNGammaLabel);
        param->setHint(kParamNGammaHint);
        param->setRange(0, 0, 0, 1, 1, 1);
        param->setDisplayRange(0, 0, 0, 1, 1, 1);
        param->setDefault(kParamNGammaDefault, kParamNGammaDefault, kParamNGammaDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        RGBParamDescriptor* param = desc.defineRGBParam(kParamDensity);
        param->setLabel(kParamDensityLabel);
        param->setHint(kParamDensityHint);
        param->setRange(0, 0, 0, 0.01, 0.01, 0.01);
        param->setDisplayRange(0, 0, 0, 0.01, 0.01, 0.01);
        param->setDefault(kParamDensityDefault, kParamDensityDefault, kParamDensityDefault);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);
    //std::cout << "describeInContext! OK\n";
} // PLogLinPluginFactory::describeInContext

ImageEffect*
PLogLinPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new PLogLinPlugin(handle);
}

static PLogLinPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
