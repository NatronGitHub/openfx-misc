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
 * OFX Clamp plugin.
 */

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "ClampOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Clamp the values of the selected channels.\n" \
    "\n" \
    "A special use case for the Clamp plugin is to generate a binary mask image " \
    "(i.e. each pixel is either 0 or 1) by thresholding an image. Let us say one wants " \
    "all input pixels whose value is above or equal to some threshold value to " \
    "become 1, and all values below this threshold to become 0. Set the \"Minimum\" value " \
    "to the threshold, set the \"Maximum\" to any value strictly below the threshold " \
    "(e.g. 0 if the threshold is positive), and " \
    "check \"Enable MinClampTo\" and \"Enable MaxClampTo\" while keeping the default " \
    "values for \"MinClampTo\" (0.0) and \"MaxClampTop\" (1.0). The result is a binary " \
    "mask image. To create a non-binary mask, with softer edges, either blur the output " \
    "of Clamp, or use the Grade plugin instead, setting the \"Black Point\" and \"White Point\" " \
    "to values close to the threshold, and checking the \"Clamp Black\" and \"Clamp " \
    "White\" options.\n" \
    "\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Clamp"
#define kPluginIdentifier "net.sf.openfx.Clamp"
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

#ifdef OFX_EXTENSIONS_NATRON
#define kParamProcessR kNatronOfxParamProcessR
#define kParamProcessRLabel kNatronOfxParamProcessRLabel
//#define kParamProcessRHint kNatronOfxParamProcessRHint
#define kParamProcessG kNatronOfxParamProcessG
#define kParamProcessGLabel kNatronOfxParamProcessGLabel
//#define kParamProcessGHint kNatronOfxParamProcessGHint
#define kParamProcessB kNatronOfxParamProcessB
#define kParamProcessBLabel kNatronOfxParamProcessBLabel
//#define kParamProcessBHint kNatronOfxParamProcessBHint
#define kParamProcessA kNatronOfxParamProcessA
#define kParamProcessALabel kNatronOfxParamProcessALabel
//#define kParamProcessAHint kNatronOfxParamProcessAHint
#else
#define kParamProcessR      "processR"
#define kParamProcessRLabel "R"
//#define kParamProcessRHint  "Process red component."
#define kParamProcessG      "processG"
#define kParamProcessGLabel "G"
//#define kParamProcessGHint  "Process green component."
#define kParamProcessB      "processB"
#define kParamProcessBLabel "B"
//#define kParamProcessBHint  "Process blue component."
#define kParamProcessA      "processA"
#define kParamProcessALabel "A"
//#define kParamProcessAHint  "Process alpha component."
#endif

#define kParamProcessRHint  "Clamp red component."
#define kParamProcessGHint  "Clamp green component."
#define kParamProcessBHint  "Clamp blue component."
#define kParamProcessAHint  "Clamp alpha component."

#define kParamMinimum "minimum"
#define kParamMinimumLabel "Minimum"
#define kParamMinimumHint "If enabled, all values that are lower than this number are set to this value, or to the minClampTo value if minClampTo is enabled."

#define kParamMinimumEnable "minimumEnable"
#define kParamMinimumEnableLabel "Enable Minimum"
#define kParamMinimumEnableHint "Whether to clamp selected channels to a minimum value."

#define kParamMaximum "maximum"
#define kParamMaximumLabel "Maximum"
#define kParamMaximumHint "If enabled, all values that are higher than this number are set to this value, or to the maxClampTo value if maxClampTo is enabled."

#define kParamMaximumEnable "maximumEnable"
#define kParamMaximumEnableLabel "Enable Maximum"
#define kParamMaximumEnableHint "Whether to clamp selected channels to a maximum value."

#define kParamMinClampTo "minClampTo"
#define kParamMinClampToLabel "MinClampTo"
#define kParamMinClampToHint "The value to which values below minimum are clamped when minClampTo is enabled. Setting this to a custom color helps visualizing the clamped areas or create graphic effects."

#define kParamMinClampToEnable "minClampToEnable"
#define kParamMinClampToEnableLabel "Enable MinClampTo"
#define kParamMinClampToEnableHint "When enabled, all values below minimum are set to the minClampTo value.\nWhen disabled, all values below minimum are clamped to the minimum value."

#define kParamMaxClampTo "maxClampTo"
#define kParamMaxClampToLabel "MaxClampTo"
#define kParamMaxClampToHint "The value to which values above maximum are clamped when maxClampTo is enabled. Setting this to a custom color helps visualizing the clamped areas or create graphic effects."

#define kParamMaxClampToEnable "maxClampToEnable"
#define kParamMaxClampToEnableLabel "Enable MaxClampTo"
#define kParamMaxClampToEnableHint "When enabled, all values above maximum are set to the maxClampTo value.\nWhen disabled, all values above maximum are clamped to the maximum value."

#define kParamPremultChanged "premultChanged"


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

// Base class for the RGBA and the Alpha processor
class ClampBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    const Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    RGBAValues _minimum;
    bool _minimumEnable;
    RGBAValues _maximum;
    bool _maximumEnable;
    RGBAValues _minClampTo;
    bool _minClampToEnable;
    RGBAValues _maxClampTo;
    bool _maxClampToEnable;
    bool _doMasking;
    bool _premult;
    int _premultChannel;
    double _mix;
    bool _maskInvert;

public:
    /** @brief no arg ctor */
    ClampBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(NULL)
        , _maskImg(NULL)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _minimum(0.)
        , _minimumEnable(true)
        , _maximum(1.)
        , _maximumEnable(true)
        , _minClampTo(0.)
        , _minClampToEnable(false)
        , _maxClampTo(1.)
        , _maxClampToEnable(false)
        , _doMasking(false)
        , _premult(false)
        , _premultChannel(3)
        , _mix(1.)
        , _maskInvert(false)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   const RGBAValues& minimum,
                   bool minimumEnable,
                   const RGBAValues& maximum,
                   bool maximumEnable,
                   const RGBAValues& minClampTo,
                   bool minClampToEnable,
                   const RGBAValues& maxClampTo,
                   bool maxClampToEnable,
                   bool premult,
                   int premultChannel,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _minimum = minimum;
        _minimumEnable = minimumEnable;
        _maximum = maximum;
        _maximumEnable = maximumEnable;
        _minClampTo = minClampTo;
        _minClampToEnable = minClampToEnable;
        _maxClampTo = maxClampTo;
        _maxClampToEnable = maxClampToEnable;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
    }
};

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue>
class ImageClamper
    : public ClampBase
{
public:
    // ctor
    ImageClamper(ImageEffect &instance)
        : ClampBase(instance)
    {
    }

private:
    // and do some processing
    void multiThreadProcessImages(OfxRectI procWindow)
    {
#     ifndef __COVERITY__ // too many coverity[dead_error_line] errors
        const bool r = _processR && (nComponents != 1);
        const bool g = _processG && (nComponents >= 2);
        const bool b = _processB && (nComponents >= 3);
        const bool a = _processA && (nComponents == 1 || nComponents == 4);
        if (r) {
            if (g) {
                if (b) {
                    if (a) {
                        return process<true, true, true, true >(procWindow); // RGBA
                    } else {
                        return process<true, true, true, false>(procWindow); // RGBa
                    }
                } else {
                    if (a) {
                        return process<true, true, false, true >(procWindow); // RGbA
                    } else {
                        return process<true, true, false, false>(procWindow); // RGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<true, false, true, true >(procWindow); // RgBA
                    } else {
                        return process<true, false, true, false>(procWindow); // RgBa
                    }
                } else {
                    if (a) {
                        return process<true, false, false, true >(procWindow); // RgbA
                    } else {
                        return process<true, false, false, false>(procWindow); // Rgba
                    }
                }
            }
        } else {
            if (g) {
                if (b) {
                    if (a) {
                        return process<false, true, true, true >(procWindow); // rGBA
                    } else {
                        return process<false, true, true, false>(procWindow); // rGBa
                    }
                } else {
                    if (a) {
                        return process<false, true, false, true >(procWindow); // rGbA
                    } else {
                        return process<false, true, false, false>(procWindow); // rGba
                    }
                }
            } else {
                if (b) {
                    if (a) {
                        return process<false, false, true, true >(procWindow); // rgBA
                    } else {
                        return process<false, false, true, false>(procWindow); // rgBa
                    }
                } else {
                    if (a) {
                        return process<false, false, false, true >(procWindow); // rgbA
                    } else {
                        return process<false, false, false, false>(procWindow); // rgba
                    }
                }
            }
        }
#     endif // ifndef __COVERITY__
    } // multiThreadProcessImages

private:
    template<bool processR, bool processG, bool processB, bool processA>
    void process(const OfxRectI& procWindow)
    {
        if (_minimumEnable) {
            if (_maximumEnable) {
                processClamp<processR, processG, processB, processA, true, true>(procWindow);
            } else {
                processClamp<processR, processG, processB, processA, true, false>(procWindow);
            }
        } else {
            if (_maximumEnable) {
                processClamp<processR, processG, processB, processA, false, true>(procWindow);
            } else {
                processClamp<processR, processG, processB, processA, false, false>(procWindow);
            }
        }
    }

    template<bool processR, bool processG, bool processB, bool processA, bool minimumEnable, bool maximumEnable>
    void processClamp(const OfxRectI& procWindow)
    {
        if (minimumEnable && _minClampToEnable) {
            if (maximumEnable && _maxClampToEnable) {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, true, true>(procWindow);
            } else {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, true, false>(procWindow);
            }
        } else {
            if (maximumEnable && _maxClampToEnable) {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, false, true>(procWindow);
            } else {
                processClampTo<processR, processG, processB, processA, minimumEnable, maximumEnable, false, false>(procWindow);
            }
        }
    }

    template<bool processR, bool processG, bool processB, bool processA, bool minimumEnable, bool maximumEnable, bool minClampToEnable, bool maxClampToEnable>
    void processClampTo(const OfxRectI& procWindow)
    {
        float unpPix[4];
        float tmpPix[4];

        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                ofxsUnPremult<PIX, nComponents, maxValue>(srcPix, unpPix, _premult, _premultChannel);
                if (!processR) {
                    tmpPix[0] = unpPix[0];
                } else {
                    tmpPix[0] = (float)clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[0],
                                                                                                               _minimum.r, _maximum.r,
                                                                                                               _minClampTo.r, _maxClampTo.r);
                }
                if (!processG) {
                    tmpPix[1] = unpPix[1];
                } else {
                    tmpPix[1] = (float)clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[1],
                                                                                                               _minimum.g, _maximum.g,
                                                                                                               _minClampTo.g, _maxClampTo.g);
                }
                if (!processB) {
                    tmpPix[2] = unpPix[2];
                } else {
                    tmpPix[2] = (float)clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[2],
                                                                                                               _minimum.b, _maximum.b,
                                                                                                               _minClampTo.b, _maxClampTo.b);
                }
                if (!processA) {
                    tmpPix[3] = unpPix[3];
                } else {
                    tmpPix[3] = (float)clamp<minimumEnable, maximumEnable, minClampToEnable, maxClampToEnable>(unpPix[3],
                                                                                                               _minimum.a, _maximum.a,
                                                                                                               _minClampTo.a, _maxClampTo.a);
                }
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // processClampTo

    template<bool minimumEnable, bool maximumEnable, bool minClampToEnable, bool maxClampToEnable>
    static double clamp(double value,
                        double minimum,
                        double maximum,
                        double minClampTo,
                        double maxClampTo)
    {
        if ( minimumEnable && (value < minimum) ) {
            return minClampToEnable ? minClampTo : minimum;
        }
        if ( maximumEnable && (value > maximum) ) {
            return maxClampToEnable ? maxClampTo : maximum;
        }

        return value;
    }
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class ClampPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    ClampPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _premultChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA ||
                             _dstClip->getPixelComponents() == ePixelComponentAlpha) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA ||
                               _srcClip->getPixelComponents() == ePixelComponentAlpha) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _minimum = fetchRGBAParam(kParamMinimum);
        _minimumEnable = fetchBooleanParam(kParamMinimumEnable);
        assert(_minimum && _minimumEnable);
        _maximum = fetchRGBAParam(kParamMaximum);
        _maximumEnable = fetchBooleanParam(kParamMaximumEnable);
        assert(_maximum && _maximumEnable);
        _minClampTo = fetchRGBAParam(kParamMinClampTo);
        _minClampToEnable = fetchBooleanParam(kParamMinClampToEnable);
        assert(_minClampTo && _minClampToEnable);
        _maxClampTo = fetchRGBAParam(kParamMaxClampTo);
        _maxClampToEnable = fetchBooleanParam(kParamMaxClampToEnable);
        assert(_maxClampTo && _maxClampToEnable);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(ClampBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    RGBAParam* _minimum;
    BooleanParam* _minimumEnable;
    RGBAParam* _maximum;
    BooleanParam* _maximumEnable;
    RGBAParam* _minClampTo;
    BooleanParam* _minClampToEnable;
    RGBAParam* _maxClampTo;
    BooleanParam* _maxClampToEnable;
    BooleanParam* _premult;
    ChoiceParam* _premultChannel;
    DoubleParam* _mix;
    BooleanParam* _maskApply;
    BooleanParam* _maskInvert;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
void
ClampPlugin::setupAndProcess(ClampBase &processor,
                             const RenderArguments &args)
{
    // get a dst image
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
    if ( (dst->getRenderScale().x != args.renderScale.x) ||
         ( dst->getRenderScale().y != args.renderScale.y) ||
         ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }

    // fetch main input image
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );

    // make sure bit depths are sane
    if ( src.get() ) {
        if ( (src->getRenderScale().x != args.renderScale.x) ||
             ( src->getRenderScale().y != args.renderScale.y) ||
             ( ( src->getField() != eFieldNone) /* for DaVinci Resolve */ && ( src->getField() != args.fieldToRender) ) ) {
            setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            throwSuiteStatusException(kOfxStatFailed);
        }
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();

        // see if they have the same depths and bytes and all
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);

    // do we do masking
    if (doMasking) {
        if ( mask.get() ) {
            if ( (mask->getRenderScale().x != args.renderScale.x) ||
                 ( mask->getRenderScale().y != args.renderScale.y) ||
                 ( ( mask->getField() != eFieldNone) /* for DaVinci Resolve */ && ( mask->getField() != args.fieldToRender) ) ) {
                setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(args.time, maskInvert);

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    RGBAValues minimum(0.);
    _minimum->getValueAtTime(args.time, minimum.r, minimum.g, minimum.b, minimum.a);
    bool minimumEnable;
    _minimumEnable->getValueAtTime(args.time, minimumEnable);
    RGBAValues maximum(1.);
    _maximum->getValueAtTime(args.time, maximum.r, maximum.g, maximum.b, maximum.a);
    bool maximumEnable;
    _maximumEnable->getValueAtTime(args.time, maximumEnable);
    RGBAValues minClampTo(0.);
    _minClampTo->getValueAtTime(args.time, minClampTo.r, minClampTo.g, minClampTo.b, minClampTo.a);
    bool minClampToEnable;
    _minClampToEnable->getValueAtTime(args.time, minClampToEnable);
    RGBAValues maxClampTo(1.);
    _maxClampTo->getValueAtTime(args.time, maxClampTo.r, maxClampTo.g, maxClampTo.b, maxClampTo.a);
    bool maxClampToEnable;
    _maxClampToEnable->getValueAtTime(args.time, maxClampToEnable);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);
    processor.setValues(processR, processG, processB, processA,
                        minimum, minimumEnable, maximum, maximumEnable,
                        minClampTo, minClampToEnable, maxClampTo, maxClampToEnable,
                        premult, premultChannel, mix);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // ClampPlugin::setupAndProcess

// the internal render function
template <int nComponents>
void
ClampPlugin::renderInternal(const RenderArguments &args,
                            BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte: {
        ImageClamper<unsigned char, nComponents, 255> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthUShort: {
        ImageClamper<unsigned short, nComponents, 65535> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eBitDepthFloat: {
        ImageClamper<float, nComponents, 1> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
ClampPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    if (dstComponents == ePixelComponentRGBA) {
        renderInternal<4>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentRGB) {
        renderInternal<3>(args, dstBitDepth);
    } else if (dstComponents == ePixelComponentXY) {
        renderInternal<2>(args, dstBitDepth);
    } else {
        assert(dstComponents == ePixelComponentAlpha);
        renderInternal<1>(args, dstBitDepth);
    }
}

bool
ClampPlugin::isIdentity(const IsIdentityArguments &args,
                        Clip * &identityClip,
                        double & /*identityTime*/
                        , int& /*view*/, std::string& /*plane*/)
{
    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        double mix;
        _processR->getValueAtTime(args.time, processR);
        _processG->getValueAtTime(args.time, processG);
        _processB->getValueAtTime(args.time, processB);
        _processA->getValueAtTime(args.time, processA);
        _mix->getValueAtTime(args.time, mix);

        if ( (mix == 0.) || (!processR && !processG && !processB && !processA) ) {
            identityClip = _srcClip;

            return true;
        }
    }

    bool minimumEnable, maximumEnable;

    _minimumEnable->getValueAtTime(args.time, minimumEnable);
    _maximumEnable->getValueAtTime(args.time, maximumEnable);

    if (!minimumEnable && !maximumEnable) {
        identityClip = _srcClip;

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
                    identityClip = _srcClip;

                    return true;
                }
            }
        }
    }

    return false;
} // ClampPlugin::isIdentity

void
ClampPlugin::changedClip(const InstanceChangedArgs &args,
                         const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         ( args.reason == eChangeUserEdit) ) {
        if ( !_premultChanged->getValue() ) {
            if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
                _premult->setValue(false);
            } else {
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
        }
        switch ( _srcClip->getPixelComponents() ) {
        case ePixelComponentAlpha:
            _processR->setValue(false);
            _processG->setValue(false);
            _processB->setValue(false);
            _processA->setValue(true);
            break;
        case ePixelComponentRGB:
            _processR->setValue(true);
            _processG->setValue(true);
            _processB->setValue(true);
            _processA->setValue(false);
            break;
        case ePixelComponentRGBA:
            _processR->setValue(true);
            _processG->setValue(true);
            _processB->setValue(true);
            _processA->setValue(true);
            break;
        default:
            break;
        }
    }
}

void
ClampPlugin::changedParam(const InstanceChangedArgs &args,
                          const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
}

mDeclarePluginFactory(ClampPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
ClampPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);

    // add supported pixel depths
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
}

void
ClampPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                      ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentXY);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentXY);
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
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMinimum);
        param->setLabel(kParamMinimumLabel);
        param->setHint(kParamMinimumHint);
        param->setDefault(0., 0., 0., 0.);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMinimumEnable);
        param->setLabel(kParamMinimumEnableLabel);
        param->setHint(kParamMinimumEnableHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMaximum);
        param->setLabel(kParamMaximumLabel);
        param->setHint(kParamMaximumHint);
        param->setDefault(1., 1., 1., 1.);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMaximumEnable);
        param->setLabel(kParamMaximumEnableLabel);
        param->setHint(kParamMaximumEnableHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMinClampTo);
        param->setLabel(kParamMinClampToLabel);
        param->setHint(kParamMinClampToHint);
        param->setDefault(0., 0., 0., 0.);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMinClampToEnable);
        param->setLabel(kParamMinClampToEnableLabel);
        param->setHint(kParamMinClampToEnableHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamMaxClampTo);
        param->setLabel(kParamMaxClampToLabel);
        param->setHint(kParamMaxClampToHint);
        param->setDefault(1., 1., 1., 1.);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMaxClampToEnable);
        param->setLabel(kParamMaxClampToEnableLabel);
        param->setHint(kParamMaxClampToEnableHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

    ofxsPremultDescribeParams(desc, page);
    ofxsMaskMixDescribeParams(desc, page);

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamPremultChanged);
        param->setDefault(false);
        param->setIsSecretAndDisabled(true);
        param->setAnimates(false);
        param->setEvaluateOnChange(false);
        if (page) {
            page->addChild(*param);
        }
    }
} // ClampPluginFactory::describeInContext

ImageEffect*
ClampPluginFactory::createInstance(OfxImageEffectHandle handle,
                                   ContextEnum /*context*/)
{
    return new ClampPlugin(handle);
}

static ClampPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
