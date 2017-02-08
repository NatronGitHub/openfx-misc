/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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
 * OFX Premult plugin.
 */

#include <cfloat> // FLT_EPSILON
#include <algorithm>

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
#include <windows.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"

#include "ofxsProcessing.H"
#include "ofxsCopier.h"
#include "ofxsMacros.h"
#ifdef OFX_EXTENSIONS_NATRON
#include "ofxNatron.h"
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginPremultName "PremultOFX"
#define kPluginPremultGrouping "Merge"
#define kPluginPremultDescription \
    "Multiply the selected channels by alpha (or another channel).\n" \
    "\n" \
    "If no channel is selected, or the premultChannel is set to None, the " \
    "image data is left untouched, but its premultiplication state is set to PreMultiplied.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Premultiplication"

#define kPluginPremultIdentifier "net.sf.openfx.Premult"
#define kPluginUnpremultName "UnpremultOFX"
#define kPluginUnpremultGrouping "Merge"
#define kPluginUnpremultDescription \
    "Divide the selected channels by alpha (or another channel)\n" \
    "\n" \
    "If no channel is selected, or the premultChannel is set to None, the " \
    "image data is left untouched, but its premultiplication state is set to UnPreMultiplied.\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Premultiplication"

#define kPluginUnpremultIdentifier "net.sf.openfx.Unpremult"
// History:
// version 1.0: initial version
// version 2.0: use kNatronOfxParamProcess* parameters
// version 2.1: do not guess checkbox values from input premult, leave kParamPremultChanged for backward compatibility
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

#define kParamProcessRHint  " the red component."
#define kParamProcessGHint  " the green component."
#define kParamProcessBHint  " the blue component."
#define kParamProcessAHint  " the alpha component."

#define kParamPremultOptionNone "None"
#define kParamPremultOptionNoneHint "Don't multiply/divide"
#define kParamPremultOptionR "R"
#define kParamPremultOptionRHint "R channel from input"
#define kParamPremultOptionG "G"
#define kParamPremultOptionGHint "G channel from input"
#define kParamPremultOptionB "B"
#define kParamPremultOptionBHint "B channel from input"
#define kParamPremultOptionA "A"
#define kParamPremultOptionAHint "A channel from input"
#define kParamClipInfo "clipInfo"
#define kParamClipInfoLabel "Clip Info..."
#define kParamClipInfoHint "Display information about the inputs"

#define kParamPremultChanged "premultChanged" // left for backward compatibility

// TODO: sRGB conversions for short and byte types

enum InputChannelEnum
{
    eInputChannelNone = 0,
    eInputChannelR,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
};


// Base class for the RGBA and the Alpha processor
class PremultBase
    : public ImageProcessor
{
protected:
    const Image *_srcImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    int _p;

public:
    /** @brief no arg ctor */
    PremultBase(ImageEffect &instance)
        : ImageProcessor(instance)
        , _srcImg(0)
        , _processR(true)
        , _processG(true)
        , _processB(true)
        , _processA(false)
        , _p(3)
    {
    }

    /** @brief set the src image */
    void setSrcImg(const Image *v) {_srcImg = v; }

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   InputChannelEnum premultChannel)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        switch (premultChannel) {
        case eInputChannelNone:
            _p = -1;
            break;
        case eInputChannelR:
            _p = 0;
            break;
        case eInputChannelG:
            _p = 1;
            break;
        case eInputChannelB:
            _p = 2;
            break;
        case eInputChannelA:
            _p = 3;
            break;
        }
    }
};

template <class PIX, int maxValue>
static
PIX
ClampNonFloat(float v)
{
    if (maxValue == 1) {
        // assume float
        return v;
    }

    return (v > maxValue) ? maxValue : v;
}

// template to do the RGBA processing
template <class PIX, int nComponents, int maxValue, bool isPremult>
class ImagePremulter
    : public PremultBase
{
public:
    // ctor
    ImagePremulter(ImageEffect &instance)
        : PremultBase(instance)
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
        bool doc[4];

        doc[0] = processR;
        doc[1] = processG;
        doc[2] = processB;
        doc[3] = processA;
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);

                // do we have a source image to scale up
                if (srcPix) {
                    if ( (_p >= 0) && (processR || processG || processB || processA) ) {
                        PIX alpha = srcPix[_p];
                        for (int c = 0; c < nComponents; c++) {
                            if (isPremult) {
                                dstPix[c] = doc[c] ? ( ( (float)srcPix[c] * alpha ) / maxValue ) : srcPix[c];
                            } else {
                                PIX val;
                                if ( !doc[c] || ( alpha <= (PIX)(FLT_EPSILON * maxValue) ) ) {
                                    val = srcPix[c];
                                } else {
                                    val = ClampNonFloat<PIX, maxValue>( ( (float)srcPix[c] * maxValue ) / alpha );
                                }
                                dstPix[c] = val;
                            }
                        }
                    } else {
                        for (int c = 0; c < nComponents; c++) {
                            dstPix[c] = srcPix[c];
                        }
                    }
                } else {
                    // no src pixel here, be black and transparent
                    for (int c = 0; c < nComponents; c++) {
                        dstPix[c] = 0;
                    }
                }

                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    } // process
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
template<bool isPremult>
class PremultPlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    PremultPlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(0)
        , _srcClip(0)
        , _processR(0)
        , _processG(0)
        , _processB(0)
        , _processA(0)
        , _premult(0)
        //, _premultChanged(0)
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
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        _premult = fetchChoiceParam(kParamPremultChannel);
        assert(_premult);
        //_premultChanged = fetchBooleanParam(kParamPremultChanged);
        //assert(_premultChanged);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(PremultBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;
    virtual void getClipPreferences(ClipPreferencesSetter &clipPreferences) OVERRIDE FINAL;

    /* override changedParam */
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;

private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    ChoiceParam* _premult;
    //BooleanParam* _premultChanged; // set to true the first time the user connects src
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from


/* set up and run a processor */
template<bool isPremult>
void
PremultPlugin<isPremult>::setupAndProcess(PremultBase &processor,
                                          const RenderArguments &args)
{
    // get a dst image
    std::auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

    if ( !dst.get() ) {
        throwSuiteStatusException(kOfxStatFailed);
    }
    const double time = args.time;
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
    std::auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
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

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    InputChannelEnum premult = (InputChannelEnum)_premult->getValueAtTime(time);
    processor.setValues(processR, processG, processB, processA, premult);

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    // set the render window
    processor.setRenderWindow(args.renderWindow);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // >::setupAndProcess

// the overridden render function
template<bool isPremult>
void
PremultPlugin<isPremult>::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    // do the rendering
    if ( !_srcClip || !_srcClip->isConnected() ) {
        // get a dst image
        std::auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
        if ( !dst.get() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }

        fillBlack( *this, args.renderWindow, dst.get() );
    } else if (_srcClip->getPreMultiplication() == eImageOpaque) {
        // Opaque images can have alpha set to anything, but it should always be considered 1

        // fetch main input image
        std::auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                        _srcClip->fetchImage(args.time) : 0 );
        // get a dst image
        std::auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );
        if ( !dst.get() ) {
            throwSuiteStatusException(kOfxStatFailed);
        }
        if ( !src.get() ) {
            setPersistentMessage(Message::eMessageError, "", "Could not fetch source image");
            throwSuiteStatusException(kOfxStatFailed);
        }

        copyPixelsOpaque( *this, args.renderWindow, src.get(), dst.get() );
    } else {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            ImagePremulter<unsigned char, 4, 255, isPremult> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthUShort: {
            ImagePremulter<unsigned short, 4, 65535, isPremult> fred(*this);
            setupAndProcess(fred, args);
            break;
        }

        case eBitDepthFloat: {
            ImagePremulter<float, 4, 1, isPremult> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // >::render

template<bool isPremult>
bool
PremultPlugin<isPremult>::isIdentity(const IsIdentityArguments &args,
                                     Clip * &identityClip,
                                     double & /*identityTime*/)
{
    const double time = args.time;

    if (!_srcClip) {
        return false;
    }
    if (isPremult) {
        if (_srcClip->getPreMultiplication() != eImagePreMultiplied) {
            // input is UnPremult, output is Premult: no identity
            return false;
        }
    } else {
        if (_srcClip->getPreMultiplication() != eImageUnPreMultiplied) {
            // input is Premult, output is UnPremult: no identity
            return false;
        }
    }
    bool processR, processG, processB, processA;
    _processR->getValueAtTime(args.time, processR);
    _processG->getValueAtTime(args.time, processG);
    _processB->getValueAtTime(args.time, processB);
    _processA->getValueAtTime(args.time, processA);
    InputChannelEnum premult = (InputChannelEnum)_premult->getValueAtTime(time);

    if ( (premult == eInputChannelNone) || (!processR && !processG && !processB && !processA) ) {
        // no processing: identity
        identityClip = _srcClip;

        return true;
    } else {
        // data is changed: no identity
        return false;
    }
}

/* Override the clip preferences */
template<bool isPremult>
void
PremultPlugin<isPremult>::getClipPreferences(ClipPreferencesSetter &clipPreferences)
{
    // Whatever the input is or the processed channels are, set the output premiltiplication.
    // This allows setting the output premult without changing the image data.
    clipPreferences.setOutputPremultiplication(isPremult ? eImagePreMultiplied : eImageUnPreMultiplied);
}

static std::string
premultString(PreMultiplicationEnum e)
{
    switch (e) {
    case eImageOpaque:

        return "Opaque";
    case eImagePreMultiplied:

        return "PreMultiplied";
    case eImageUnPreMultiplied:

        return "UnPreMultiplied";
    }

    return "Unknown";
}

template<bool isPremult>
void
PremultPlugin<isPremult>::changedParam(const InstanceChangedArgs &args,
                                       const std::string &paramName)
{
    if ( (paramName == kParamClipInfo) && _srcClip && (args.reason == eChangeUserEdit) ) {
        std::string msg;
        msg += "Input; ";
        if (!_srcClip) {
            msg += "N/A";
        } else {
            msg += premultString( _srcClip->getPreMultiplication() );
        }
        msg += "\n";
        msg += "Output: ";
        if (!_dstClip) {
            msg += "N/A";
        } else {
            msg += premultString( _dstClip->getPreMultiplication() );
        }
        msg += "\n";
        sendMessage(Message::eMessageMessage, "", msg);
        //} else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        //    _premultChanged->setValue(true);
    }
}

template<bool isPremult>
void
PremultPlugin<isPremult>::changedClip(const InstanceChangedArgs & /*args*/,
                                      const std::string & /*clipName*/)
{
    // It is very dangerous to set this from the input premult, which is sometimes wrong.
    // If the user wants to premult/unpremul, the default should always be to premult/unpremult
    /*
       if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
        if (_srcClip->getPixelComponents() != ePixelComponentRGBA) {
            _processR->setValue(false);
            _processG->setValue(false);
            _processB->setValue(false);
            _processA->setValue(false);
        } else {
            switch ( _srcClip->getPreMultiplication() ) {
            case eImageOpaque:
                _processR->setValue(false);
                _processG->setValue(false);
                _processB->setValue(false);
                _processA->setValue(false);
                break;
            case eImagePreMultiplied:
                if (isPremult) {
                    _processR->setValue(false);
                    _processG->setValue(false);
                    _processB->setValue(false);
                    _processA->setValue(false);
                    //_premult->setValue(eInputChannelNone);
                } else {
                    _processR->setValue(true);
                    _processG->setValue(true);
                    _processB->setValue(true);
                    _processA->setValue(false);
                    _premult->setValue(eInputChannelA);
                }
                break;
            case eImageUnPreMultiplied:
                if (!isPremult) {
                    _processR->setValue(false);
                    _processG->setValue(false);
                    _processB->setValue(false);
                    _processA->setValue(false);
                    //_premult->setValue(eInputChannelNone);
                } else {
                    _processR->setValue(true);
                    _processG->setValue(true);
                    _processB->setValue(true);
                    _processA->setValue(false);
                    _premult->setValue(eInputChannelA);
                }
                break;
            }
            _premultChanged->setValue(true);
        }
       }
     */
} // >::changedClip

//mDeclarePluginFactory(PremultPluginFactory, {ofxsThreadSuiteCheck();}, {});

template<bool isPremult>
class PremultPluginFactory
    : public PluginFactoryHelper<PremultPluginFactory<isPremult> >
{
public:
    PremultPluginFactory(const std::string& id,
                         unsigned int verMaj,
                         unsigned int verMin) : PluginFactoryHelper<PremultPluginFactory<isPremult> >(id, verMaj, verMin) {}

    virtual void load() OVERRIDE FINAL {ofxsThreadSuiteCheck();}
    //virtual void unload() OVERRIDE FINAL {};
    virtual void describe(ImageEffectDescriptor &desc) OVERRIDE FINAL;
    virtual void describeInContext(ImageEffectDescriptor &desc, ContextEnum context) OVERRIDE FINAL;
    virtual ImageEffect* createInstance(OfxImageEffectHandle handle, ContextEnum context) OVERRIDE FINAL;
};


template<bool isPremult>
void
PremultPluginFactory<isPremult>::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    if (isPremult) {
        desc.setLabel(kPluginPremultName);
        desc.setPluginGrouping(kPluginPremultGrouping);
        desc.setPluginDescription(kPluginPremultDescription);
    } else {
        desc.setLabel(kPluginUnpremultName);
        desc.setPluginGrouping(kPluginUnpremultGrouping);
        desc.setPluginDescription(kPluginUnpremultDescription);
    }

    // add the supported contexts
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);

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

template<bool isPremult>
void
PremultPluginFactory<isPremult>::describeInContext(ImageEffectDescriptor &desc,
                                                   ContextEnum /*context*/)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    //srcClip->addSupportedComponent(ePixelComponentRGB);
    //srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    //dstClip->addSupportedComponent(ePixelComponentRGB);
    //dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    const std::string premultString = isPremult ? "Multiply " : "Divide ";

    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(premultString + kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(premultString + kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(premultString + kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(premultString + kParamProcessAHint);
        param->setDefault(false);
        param->setLayoutHint(eLayoutHintNoNewLine, 1);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamPremultChannel);
        param->setLabel(kParamPremultChannelLabel);
        param->setHint(kParamPremultChannelHint);
        assert(param->getNOptions() == eInputChannelNone);
        param->appendOption(kParamPremultOptionNone, kParamPremultOptionNoneHint);
        assert(param->getNOptions() == eInputChannelR);
        param->appendOption(kParamPremultOptionR, kParamPremultOptionRHint);
        assert(param->getNOptions() == eInputChannelG);
        param->appendOption(kParamPremultOptionG, kParamPremultOptionGHint);
        assert(param->getNOptions() == eInputChannelB);
        param->appendOption(kParamPremultOptionB, kParamPremultOptionBHint);
        assert(param->getNOptions() == eInputChannelA);
        param->appendOption(kParamPremultOptionA, kParamPremultOptionAHint);
        param->setDefault( (int)eInputChannelA );
        if (page) {
            page->addChild(*param);
        }
    }

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamClipInfo);
        param->setLabel(kParamClipInfoLabel);
        param->setHint(kParamClipInfoHint);
        if (page) {
            page->addChild(*param);
        }
    }

    // this parameter is left for backward-compatibility reasons, but it is never used
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
} // >::describeInContext

template<bool isPremult>
ImageEffect*
PremultPluginFactory<isPremult>::createInstance(OfxImageEffectHandle handle,
                                                ContextEnum /*context*/)
{
    return new PremultPlugin<isPremult>(handle);
}

static PremultPluginFactory<true> p1(kPluginPremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
static PremultPluginFactory<false> p2(kPluginUnpremultIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p1)
mRegisterPluginFactoryInstance(p2)

OFXS_NAMESPACE_ANONYMOUS_EXIT
