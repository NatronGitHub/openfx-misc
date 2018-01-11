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
 * OFX Grade plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <limits>
#include <algorithm>

#include "ofxsProcessing.H"
#include "ofxsMaskMix.h"
#include "ofxsCoords.h"
#include "ofxsMacros.h"
#include "ofxsThreadSuite.h"
#include "ofxsMultiThread.h"
#ifdef OFX_USE_MULTITHREAD_MUTEX
namespace {
    typedef MultiThread::Mutex Mutex;
    typedef MultiThread::AutoMutex AutoMutex;
}
#else
// some OFX hosts do not have mutex handling in the MT-Suite (e.g. Sony Catalyst Edit)
// prefer using the fast mutex by Marcus Geelnard http://tinythreadpp.bitsnbites.eu/
#include "fast_mutex.h"
namespace {
    typedef tthread::fast_mutex Mutex;
    typedef OFX::MultiThread::AutoMutexT<tthread::fast_mutex> AutoMutex;
}
#endif

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "GradeOFX"
#define kPluginGrouping "Color"
#define kPluginDescription "Modify the tonal spread of an image from the white and black points.\n" \
    "This node can also be used to match colors of 2 images: The darkest and lightest points of " \
    "the target image are converted to black and white using the blackpoint and whitepoint values. " \
    "These 2 values are then moved to new values using the black(for dark point) and white(for white point). " \
    "You can also apply multiply/offset/gamma for other color fixing you may need.\n" \
    "Here is the formula used:\n" \
    "A = multiply * (white - black) / (whitepoint - blackpoint)\n" \
    "B = offset + black - A * blackpoint\n" \
    "output = pow(A * input + B, 1 / gamma).\n" \
    "\n" \
    "A special use for Grade is to generate a mask image with soft edges by thresholding an input image. " \
    "Set the \"Black Point\" and \"White Point\" " \
    "to values just below and just above the threshold, and check the \"Clamp Black\" and \"Clamp " \
    "White\" options. If a binary mask containing only 0 and 1 is preferred, the Clamp plugin can be " \
    "used instead.\n" \
    "\n" \
    "See also: http://opticalenquiry.com/nuke/index.php?title=Grade and http://opticalenquiry.com/nuke/index.php?title=Integration#Matching_color"
#define kPluginIdentifier "net.sf.openfx.GradePlugin"
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

#define kParamBlackPoint "blackPoint"
#define kParamBlackPointLabel "Black Point"
#define kParamBlackPointHint "Set the color of the darkest pixels in the image."

#define kParamWhitePoint "whitePoint"
#define kParamWhitePointLabel "White Point"
#define kParamWhitePointHint "Set the color of the brightest pixels in the image."

#define kParamBlack "black"
#define kParamBlackLabel "Black"
#define kParamBlackHint "Colors corresponding to the blackpoint are set to this value."

#define kParamWhite "white"
#define kParamWhiteLabel "White"
#define kParamWhiteHint "Colors corresponding to the whitepoint are set to this value."

#define kParamMultiply "multiply"
#define kParamMultiplyLabel "Multiply"
#define kParamMultiplyHint "Multiplies the result by this value."

#define kParamOffset "offset"
#define kParamOffsetLabel "Offset"
#define kParamOffsetHint "Adds this value to the result (this applies to black and white)."

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Final gamma correction. Negative values are not affected by gamma."

#define kParamClampBlack "clampBlack"
#define kParamClampBlackLabel "Clamp Black"
#define kParamClampBlackHint "All colors below 0 on output are set to 0."

#define kParamClampWhite "clampWhite"
#define kParamClampWhiteLabel "Clamp White"
#define kParamClampWhiteHint "All colors above 1 on output are set to 1."

#define kParamPremultChanged "premultChanged"

#define kParamNormalize "normalize"
#define kParamNormalizeLabel "Normalize", "Normalize the image by setting the white point and black point from the minimum and maximum values of the input."


struct RGBAValues
{
    double r, g, b, a;
    RGBAValues(double v) : r(v), g(v), b(v), a(v) {}

    RGBAValues() : r(0), g(0), b(0), a(0) {}
};

struct Results
{
    Results()
    : min( std::numeric_limits<double>::infinity() )
    , max( -std::numeric_limits<double>::infinity() )
    //, mean(0.)
    //, sdev( std::numeric_limits<double>::infinity() )
    //, skewness( std::numeric_limits<double>::infinity() )
    //, kurtosis( std::numeric_limits<double>::infinity() )
    //, maxVal( -std::numeric_limits<double>::infinity() )
    //, minVal( std::numeric_limits<double>::infinity() )
    {
        //maxPos.x = maxPos.y = minPos.x = minPos.y = 0.;
    }

    RGBAValues min;
    RGBAValues max;
    //RGBAValues mean;
    //RGBAValues sdev;
    //RGBAValues skewness;
    //RGBAValues kurtosis;
    //OfxPointD maxPos; // luma only
    //RGBAValues maxVal; // luma only
    //OfxPointD minPos; // luma only
    //RGBAValues minVal; // luma only
};

class ImageStatisticsProcessorBase
: public ImageProcessor
{
protected:
    Mutex _mutex; //< this is used so we can multi-thread the analysis and protect the shared results
    unsigned long _count;

public:
    ImageStatisticsProcessorBase(ImageEffect &instance)
    : ImageProcessor(instance)
    , _mutex()
    , _count(0)
    {
    }

    virtual ~ImageStatisticsProcessorBase()
    {
    }

    virtual void setPrevResults(double time, const Results &results) = 0;
    virtual void getResults(Results *results) = 0;

protected:

    template<class PIX, int nComponents, int maxValue>
    void toRGBA(const PIX *p,
                RGBAValues* rgba)
    {
        if (nComponents == 4) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = p[2] / (double)maxValue;
            rgba->a = p[3] / (double)maxValue;
        } else if (nComponents == 3) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = p[2] / (double)maxValue;
            rgba->a = 0;
        } else if (nComponents == 2) {
            rgba->r = p[0] / (double)maxValue;
            rgba->g = p[1] / (double)maxValue;
            rgba->b = 0;
            rgba->a = 0;
        } else if (nComponents == 1) {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = p[0] / (double)maxValue;
        } else {
            rgba->r = 0;
            rgba->g = 0;
            rgba->b = 0;
            rgba->a = 0;
        }
    }

    /*
    template<class PIX, int nComponents, int maxValue>
    void pixToHSVL(const PIX *p,
                   float hsvl[4])
    {
        if ( (nComponents == 4) || (nComponents == 3) ) {
            float r, g, b;
            r = p[0] / (float)maxValue;
            g = p[1] / (float)maxValue;
            b = p[2] / (float)maxValue;
            Color::rgb_to_hsv(r, g, b, &hsvl[0], &hsvl[1], &hsvl[2]);
            hsvl[0] *= 360 / OFXS_HUE_CIRCLE;
            float min = std::min(std::min(r, g), b);
            float max = std::max(std::max(r, g), b);
            hsvl[3] = (min + max) / 2;
        } else {
            hsvl[0] = hsvl[1] = hsvl[2] = hsvl[3] = 0.f;
        }
    }
    */

    template<class PIX, int nComponents, int maxValue>
    void toComponents(const RGBAValues& rgba,
                      PIX *p)
    {
        if (nComponents == 4) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[2] = rgba.b * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[3] = rgba.a * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 3) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[2] = rgba.b * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 2) {
            p[0] = rgba.r * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
            p[1] = rgba.g * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        } else if (nComponents == 1) {
            p[0] = rgba.a * maxValue + ( (maxValue != 1) ? 0.5 : 0 );
        }
    }
};


template <class PIX, int nComponents, int maxValue>
class ImageMinMaxProcessor
: public ImageStatisticsProcessorBase
{
private:
    double _min[nComponents];
    double _max[nComponents];

public:
    ImageMinMaxProcessor(ImageEffect &instance)
    : ImageStatisticsProcessorBase(instance)
    {
        std::fill( _min, _min + nComponents, +std::numeric_limits<double>::infinity() );
        std::fill( _max, _max + nComponents, -std::numeric_limits<double>::infinity() );
    }

    ~ImageMinMaxProcessor()
    {
    }

    void setPrevResults(double /* time */,
                        const Results & /*results*/) OVERRIDE FINAL {}

    void getResults(Results *results) OVERRIDE FINAL
    {
        if (_count > 0) {
            toRGBA<double, nComponents, 1>(_min, &results->min);
            toRGBA<double, nComponents, 1>(_max, &results->max);
        }
    }

private:

    void addResults(double min[nComponents],
                    double max[nComponents],
                    unsigned long count)
    {
        AutoMutex l (&_mutex);
        for (int c = 0; c < nComponents; ++c) {
            _min[c] = std::min(_min[c], min[c]);
            _max[c] = std::max(_max[c], max[c]);
        }
        _count += count;
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE FINAL
    {
        double min[nComponents], max[nComponents];

        std::fill( min, min + nComponents, +std::numeric_limits<double>::infinity() );
        std::fill( max, max + nComponents, -std::numeric_limits<double>::infinity() );
        unsigned long count = 0;

        assert(_dstImg->getBounds().x1 <= procWindow.x1 && procWindow.y2 <= _dstImg->getBounds().y2 &&
               _dstImg->getBounds().y1 <= procWindow.y1 && procWindow.y2 <= _dstImg->getBounds().y2);
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; ++x) {
                for (int c = 0; c < nComponents; ++c) {
                    double v = *dstPix;
                    min[c] = std::min(min[c], v);
                    max[c] = std::max(max[c], v);
                    ++dstPix;
                }
            }
            count += procWindow.x2 - procWindow.x1;
        }
        count += (procWindow.x2 - procWindow.x1) * (procWindow.y2 - procWindow.y1);

        addResults(min, max, count);
    }
};

class GradeProcessorBase
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
    bool _processR, _processG, _processB, _processA;

public:

    GradeProcessorBase(ImageEffect &instance)
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
        , _processA(false)
        , _clampBlack(true)
        , _clampWhite(true)
    {
    }

    void setSrcImg(const Image *v) {_srcImg = v; }

    void setMaskImg(const Image *v,
                    bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v; }

    void setValues(const RGBAValues& blackPoint,
                   const RGBAValues& whitePoint,
                   const RGBAValues& black,
                   const RGBAValues& white,
                   const RGBAValues& multiply,
                   const RGBAValues& offset,
                   const RGBAValues& gamma,
                   bool clampBlack,
                   bool clampWhite,
                   bool premult,
                   int premultChannel,
                   double mix,
                   bool processR,
                   bool processG,
                   bool processB,
                   bool processA)
    {
        _blackPoint = blackPoint;
        _whitePoint = whitePoint;
        _black = black;
        _white = white;
        _multiply = multiply;
        _offset = offset;
        _gamma = gamma;
        _clampBlack = clampBlack;
        _clampWhite = clampWhite;
        _premult = premult;
        _premultChannel = premultChannel;
        _mix = mix;
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
    }

    void grade(double* v,
               double wp,
               double bp,
               double white,
               double black,
               double mutiply,
               double offset,
               double gamma)
    {
        double d = wp - bp;
        double A = d != 0 ? mutiply * (white - black) / d : 0;
        double B = offset + black - A * bp;

        A = (A * *v) + B;
        if (gamma <= 0) {
            if (A < 1.) {
                *v = 0.;
            } else if (A == 1.) {
                *v = 1.;
            } else {
                *v = std::numeric_limits<double>::infinity();
            }

            return;
        }
        if (gamma == 1.) {
            *v = A;
        } else if (A <= 0) {
            *v = A; // pow would produce NaNs in that case (in Nuke, negative values produce v on output in Grade and Gamma)
        } else {
            double invgamma = 1. / gamma;
            *v = std::pow(A, invgamma);
        }
    }

    template<bool processR, bool processG, bool processB, bool processA>
    void grade(double *r,
               double *g,
               double *b,
               double *a)
    {
        if (processR) {
            grade(r, _whitePoint.r, _blackPoint.r, _white.r, _black.r, _multiply.r, _offset.r, _gamma.r);
        }
        if (processG) {
            grade(g, _whitePoint.g, _blackPoint.g, _white.g, _black.g, _multiply.g, _offset.g, _gamma.g);
        }
        if (processB) {
            grade(b, _whitePoint.b, _blackPoint.b, _white.b, _black.b, _multiply.b, _offset.b, _gamma.b);
        }
        if (processA) {
            grade(a, _whitePoint.a, _blackPoint.a, _white.a, _black.a, _multiply.a, _offset.a, _gamma.a);
        }
        if (_clampBlack) {
            if (processR) {
                *r = std::max(0., *r);
            }
            if (processG) {
                *g = std::max(0., *g);
            }
            if (processB) {
                *b = std::max(0., *b);
            }
            if (processA) {
                *a = std::max(0., *a);
            }
        }
        if (_clampWhite) {
            if (processR) {
                *r = std::min(1., *r);
            }
            if (processG) {
                *g = std::min(1., *g);
            }
            if (processB) {
                *b = std::min(1., *b);
            }
            if (processA) {
                *a = std::min(1., *a);
            }
        }
    }

private:
    RGBAValues _blackPoint;
    RGBAValues _whitePoint;
    RGBAValues _black;
    RGBAValues _white;
    RGBAValues _multiply;
    RGBAValues _offset;
    RGBAValues _gamma;
    bool _clampBlack;
    bool _clampWhite;
};


template <class PIX, int nComponents, int maxValue>
class GradeProcessor
    : public GradeProcessorBase
{
public:
    GradeProcessor(ImageEffect &instance)
        : GradeProcessorBase(instance)
    {
    }

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
    void process(OfxRectI procWindow)
    {
        assert( (!processR && !processG && !processB) || (nComponents == 3 || nComponents == 4) );
        assert( !processA || (nComponents == 1 || nComponents == 4) );
        assert(nComponents == 3 || nComponents == 4);
        assert(_dstImg);
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
                double t_r = unpPix[0];
                double t_g = unpPix[1];
                double t_b = unpPix[2];
                double t_a = unpPix[3];
                grade<processR, processG, processB, processA>(&t_r, &t_g, &t_b, &t_a);
                tmpPix[0] = (float)t_r;
                tmpPix[1] = (float)t_g;
                tmpPix[2] = (float)t_b;
                tmpPix[3] = (float)t_a;
                ofxsPremultMaskMixPix<PIX, nComponents, maxValue, true>(tmpPix, _premult, _premultChannel, x, y, srcPix, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class GradePlugin
    : public ImageEffect
{
public:
    /** @brief ctor */
    GradePlugin(OfxImageEffectHandle handle)
        : ImageEffect(handle)
        , _dstClip(NULL)
        , _srcClip(NULL)
        , _maskClip(NULL)
        , _processR(NULL)
        , _processG(NULL)
        , _processB(NULL)
        , _processA(NULL)
        , _blackPoint(NULL)
        , _whitePoint(NULL)
        , _black(NULL)
        , _white(NULL)
        , _multiply(NULL)
        , _offset(NULL)
        , _gamma(NULL)
        , _clampBlack(NULL)
        , _clampWhite(NULL)
        , _premult(NULL)
        , _premultChannel(NULL)
        , _mix(NULL)
        , _maskApply(NULL)
        , _maskInvert(NULL)
        , _premultChanged(NULL)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert( _dstClip && (!_dstClip->isConnected() || _dstClip->getPixelComponents() == ePixelComponentRGB ||
                             _dstClip->getPixelComponents() == ePixelComponentRGBA) );
        _srcClip = getContext() == eContextGenerator ? NULL : fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert( (!_srcClip && getContext() == eContextGenerator) ||
                ( _srcClip && (!_srcClip->isConnected() || _srcClip->getPixelComponents() ==  ePixelComponentRGB ||
                               _srcClip->getPixelComponents() == ePixelComponentRGBA) ) );
        _maskClip = fetchClip(getContext() == eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || !_maskClip->isConnected() || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _blackPoint = fetchRGBAParam(kParamBlackPoint);
        _whitePoint = fetchRGBAParam(kParamWhitePoint);
        _black = fetchRGBAParam(kParamBlack);
        _white = fetchRGBAParam(kParamWhite);
        _multiply = fetchRGBAParam(kParamMultiply);
        _offset = fetchRGBAParam(kParamOffset);
        _gamma = fetchRGBAParam(kParamGamma);
        _clampBlack = fetchBooleanParam(kParamClampBlack);
        _clampWhite = fetchBooleanParam(kParamClampWhite);
        assert(_blackPoint && _whitePoint && _black && _white && _multiply && _offset && _gamma && _clampBlack && _clampWhite);
        _premult = fetchBooleanParam(kParamPremult);
        _premultChannel = fetchChoiceParam(kParamPremultChannel);
        assert(_premult && _premultChannel);
        _mix = fetchDoubleParam(kParamMix);
        _maskApply = ( ofxsMaskIsAlwaysConnected( OFX::getImageEffectHostDescription() ) && paramExists(kParamMaskApply) ) ? fetchBooleanParam(kParamMaskApply) : 0;
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);

        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
    }

private:
    /* Override the render */
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

    /* set up and run a processor */
    void setupAndProcess(GradeProcessorBase &, const RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime, int& view, std::string& plane) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /// Set the black and white point from the image minimum/maximum from the given image
    void normalize(const Image* srcImg)
    {
        PixelComponentEnum srcComponents  = srcImg->getPixelComponents();
        Results results;
        assert(srcComponents == ePixelComponentAlpha || srcComponents == ePixelComponentRGB || srcComponents == ePixelComponentRGBA);
        if (srcComponents == ePixelComponentAlpha) {
            normalizeComponents<1>(srcImg, &results);
        } else if (srcComponents == ePixelComponentRGBA) {
            normalizeComponents<4>(srcImg, &results);
        } else if (srcComponents == ePixelComponentRGB) {
            normalizeComponents<3>(srcImg, &results);
        } else {
            // coverity[dead_error_line]
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
        beginEditBlock(kParamNormalize);
        _blackPoint->setValue(results.min.r, results.min.g, results.min.b, results.min.a);
        _whitePoint->setValue(results.max.r, results.max.g, results.max.b, results.max.a);
        endEditBlock();
    }

    template <class PIX, int nComponents, int maxValue>
    void normalizeComponentsDepth(const Image* srcImg,
                                  Results* results)
    {
        ImageMinMaxProcessor<PIX, nComponents, maxValue> fred(*this);
        setupAndProcessImageProcessor(fred, srcImg, results);
    }

    template <int nComponents>
    void normalizeComponents(const Image* srcImg,
                             Results* results)
    {
        BitDepthEnum srcBitDepth = srcImg->getPixelDepth();

        switch (srcBitDepth) {
            case eBitDepthUByte: {
                normalizeComponentsDepth<unsigned char, nComponents, 255>(srcImg, results);
                break;
            }
            case eBitDepthUShort: {
                normalizeComponentsDepth<unsigned short, nComponents, 65535>(srcImg, results);
                break;
            }
            case eBitDepthFloat: {
                normalizeComponentsDepth<float, nComponents, 1>(srcImg, results);
                break;
            }
            default:
                throwSuiteStatusException(kOfxStatErrUnsupported);
        }
        // if all computed components are equal, set the remaining components
        if (nComponents == 3) {
            if ( (results->min.r == results->min.g) && (results->min.r == results->min.b) ) {
                results->min.a = results->min.r;
            }
            if ( (results->max.r == results->max.g) && (results->max.r == results->max.b) ) {
                results->max.a = results->max.r;
            }
        } else if (nComponents == 2) {
            if (results->min.r == results->min.g) {
                results->min.b = results->min.r;
                results->min.a = results->min.r;
            }
            if (results->max.r == results->max.g) {
                results->max.b = results->max.r;
                results->max.a = results->max.r;
            }
        } else if (nComponents == 1) {
            results->min.r = results->min.g = results->min.b = results->min.a;
            results->max.r = results->max.g = results->max.b = results->max.a;
        }
    }

    void setupAndProcessImageProcessor(ImageStatisticsProcessorBase &processor,
                                           const Image* srcImg,
                                           Results *results)
    {
        // set the images
        processor.setDstImg( const_cast<Image*>(srcImg) ); // not a bug: we only set dst

        // set the render window
        processor.setRenderWindow(srcImg->getBounds());

        // Call the base class process member, this will call the derived templated process code
        processor.process();

        if ( !abort() ) {
            processor.getResults(results);
        }
    }
private:
    // do not need to delete these, the ImageEffect is managing them for us
    Clip *_dstClip;
    Clip *_srcClip;
    Clip *_maskClip;
    BooleanParam* _processR;
    BooleanParam* _processG;
    BooleanParam* _processB;
    BooleanParam* _processA;
    RGBAParam* _blackPoint;
    RGBAParam* _whitePoint;
    RGBAParam* _black;
    RGBAParam* _white;
    RGBAParam* _multiply;
    RGBAParam* _offset;
    RGBAParam* _gamma;
    BooleanParam* _clampBlack;
    BooleanParam* _clampWhite;
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
GradePlugin::setupAndProcess(GradeProcessorBase &processor,
                             const RenderArguments &args)
{
    auto_ptr<Image> dst( _dstClip->fetchImage(args.time) );

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
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
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
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
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
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );
    processor.setRenderWindow(args.renderWindow);

    RGBAValues blackPoint, whitePoint, black, white, multiply, offset, gamma;
    _blackPoint->getValueAtTime(args.time, blackPoint.r, blackPoint.g, blackPoint.b, blackPoint.a);
    _whitePoint->getValueAtTime(args.time, whitePoint.r, whitePoint.g, whitePoint.b, whitePoint.a);
    _black->getValueAtTime(args.time, black.r, black.g, black.b, black.a);
    _white->getValueAtTime(args.time, white.r, white.g, white.b, white.a);
    _multiply->getValueAtTime(args.time, multiply.r, multiply.g, multiply.b, multiply.a);
    _offset->getValueAtTime(args.time, offset.r, offset.g, offset.b, offset.a);
    _gamma->getValueAtTime(args.time, gamma.r, gamma.g, gamma.b, gamma.a);
    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    bool premult;
    int premultChannel;
    _premult->getValueAtTime(args.time, premult);
    _premultChannel->getValueAtTime(args.time, premultChannel);
    double mix;
    _mix->getValueAtTime(args.time, mix);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);

    processor.setValues(blackPoint, whitePoint, black, white, multiply, offset, gamma,
                        clampBlack, clampWhite, premult, premultChannel, mix,
                        processR, processG, processB, processA);
    processor.process();
} // GradePlugin::setupAndProcess

// the overridden render function
void
GradePlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
    if (dstComponents == ePixelComponentRGBA) {
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            GradeProcessor<unsigned char, 4, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            GradeProcessor<unsigned short, 4, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            GradeProcessor<float, 4, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    } else {
        assert(dstComponents == ePixelComponentRGB);
        switch (dstBitDepth) {
        case eBitDepthUByte: {
            GradeProcessor<unsigned char, 3, 255> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthUShort: {
            GradeProcessor<unsigned short, 3, 65535> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eBitDepthFloat: {
            GradeProcessor<float, 3, 1> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default:
            throwSuiteStatusException(kOfxStatErrUnsupported);
        }
    }
} // GradePlugin::render

bool
GradePlugin::isIdentity(const IsIdentityArguments &args,
                        Clip * &identityClip,
                        double & /*identityTime*/
                        , int& /*view*/, std::string& /*plane*/)
{
    double mix;

    _mix->getValueAtTime(args.time, mix);

    if (mix == 0.) {
        identityClip = _srcClip;

        return true;
    }

    {
        bool processR;
        bool processG;
        bool processB;
        bool processA;
        _processR->getValueAtTime(args.time, processR);
        _processG->getValueAtTime(args.time, processG);
        _processB->getValueAtTime(args.time, processB);
        _processA->getValueAtTime(args.time, processA);
        if (!processR && !processG && !processB && !processA) {
            identityClip = _srcClip;

            return true;
        }
    }

    bool clampBlack, clampWhite;
    _clampBlack->getValueAtTime(args.time, clampBlack);
    _clampWhite->getValueAtTime(args.time, clampWhite);
    if (clampBlack || clampWhite) {
        return false;
    }
    RGBAValues blackPoint, whitePoint, black, white, multiply, offset, gamma;
    _blackPoint->getValueAtTime(args.time, blackPoint.r, blackPoint.g, blackPoint.b, blackPoint.a);
    _whitePoint->getValueAtTime(args.time, whitePoint.r, whitePoint.g, whitePoint.b, whitePoint.a);
    _black->getValueAtTime(args.time, black.r, black.g, black.b, black.a);
    _white->getValueAtTime(args.time, white.r, white.g, white.b, white.a);
    _multiply->getValueAtTime(args.time, multiply.r, multiply.g, multiply.b, multiply.a);
    _offset->getValueAtTime(args.time, offset.r, offset.g, offset.b, offset.a);
    _gamma->getValueAtTime(args.time, gamma.r, gamma.g, gamma.b, gamma.a);
    if ( (blackPoint.r == 0.) && (blackPoint.g == 0.) && (blackPoint.b == 0.) && (blackPoint.a == 0.) &&
         ( whitePoint.r == 1.) && ( whitePoint.g == 1.) && ( whitePoint.b == 1.) && ( whitePoint.a == 1.) &&
         ( black.r == 0.) && ( black.g == 0.) && ( black.b == 0.) && ( black.a == 0.) &&
         ( white.r == 1.) && ( white.g == 1.) && ( white.b == 1.) && ( white.a == 1.) &&
         ( multiply.r == 1.) && ( multiply.g == 1.) && ( multiply.b == 1.) && ( multiply.a == 1.) &&
         ( offset.r == 0.) && ( offset.g == 0.) && ( offset.b == 0.) && ( offset.a == 0.) &&
         ( gamma.r == 1.) && ( gamma.g == 1.) && ( gamma.b == 1.) && ( gamma.a == 1) ) {
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
} // GradePlugin::isIdentity

void
GradePlugin::changedClip(const InstanceChangedArgs &args,
                         const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_premultChanged->getValue() &&
         ( args.reason == eChangeUserEdit) ) {
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
}

void
GradePlugin::changedParam(const InstanceChangedArgs &args,
                          const std::string &paramName)
{
    if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    }
    if (paramName == kParamNormalize) {
        auto_ptr<Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                            _srcClip->fetchImage(args.time) : 0 );
        normalize( src.get() );
    }
}

mDeclarePluginFactory(GradePluginFactory, {ofxsThreadSuiteCheck();}, {});
void
GradePluginFactory::describe(ImageEffectDescriptor &desc)
{
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
}

static
void
defineRGBAScaleParam(ImageEffectDescriptor &desc,
                     const std::string &name,
                     const std::string &label,
                     const std::string &hint,
                     PageParamDescriptor* page,
                     double def,
                     double min,
                     double max)
{
    RGBAParamDescriptor *param = desc.defineRGBAParam(name);

    param->setLabel(label);
    param->setHint(hint);
    param->setDefault(def, def, def, def);
    param->setRange(-DBL_MAX, -DBL_MAX, -DBL_MAX, -DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
    param->setDisplayRange(min, min, min, min, max, max, max, max);
    if (page) {
        page->addChild(*param);
    }
}

void
GradePluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                      ContextEnum context)
{
    // Source clip only in the filter context
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);

    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
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
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }


    defineRGBAScaleParam(desc, kParamBlackPoint, kParamBlackPointLabel, kParamBlackPointHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamWhitePoint, kParamWhitePointLabel, kParamWhitePointHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamBlack, kParamBlackLabel, kParamBlackHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamWhite, kParamWhiteLabel, kParamWhiteHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamMultiply, kParamMultiplyLabel, kParamMultiplyHint, page, 1., 0., 4.);
    defineRGBAScaleParam(desc, kParamOffset, kParamOffsetLabel, kParamOffsetHint, page, 0., -1., 1.);
    defineRGBAScaleParam(desc, kParamGamma, kParamGammaLabel, kParamGammaHint, page, 1., 0.2, 5.);

    {
        PushButtonParamDescriptor *param = desc.definePushButtonParam(kParamNormalize);
        param->setLabelAndHint(kParamNormalizeLabel);
        if (page) {
            page->addChild(*param);
        }
    }

    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampBlack);
        param->setLabel(kParamClampBlackLabel);
        param->setHint(kParamClampBlackHint);
        param->setDefault(true);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        BooleanParamDescriptor *param = desc.defineBooleanParam(kParamClampWhite);
        param->setLabel(kParamClampWhiteLabel);
        param->setHint(kParamClampWhiteHint);
        param->setDefault(false);
        param->setAnimates(true);
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
} // GradePluginFactory::describeInContext

ImageEffect*
GradePluginFactory::createInstance(OfxImageEffectHandle handle,
                                   ContextEnum /*context*/)
{
    return new GradePlugin(handle);
}

static GradePluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
