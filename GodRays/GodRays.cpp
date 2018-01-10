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
 * OFX GodRays plugin.
 */

#include <cmath>
#include <cfloat> // DBL_MAX
#include <iostream>
#include <algorithm>

#include "ofxsTransform3x3.h"
#include "ofxsTransformInteract.h"
#include "ofxsCoords.h"
#include "ofxsThreadSuite.h"

using namespace OFX;

OFXS_NAMESPACE_ANONYMOUS_ENTER

#define kPluginName "GodRaysOFX"
#define kPluginGrouping "Filter"
#define kPluginDescription \
    "Average an image over a range of transforms.\n" \
    "This can be used to create crepuscular rays (also called God rays) by setting the scale and center parameters: scale governs the length of rays, and center should be set to the Sun or light position (which may be outside of the image).\n" \
    "Setting toColor to black and gamma to 1 causes an exponential decay which is very similar to the real crepuscular rays.\n" \
    "This can also be used to create directional blur using a fixed number of samples (as opposed to DirBlur, which uses an adaptive sampling method).\n" \
    "This plugin concatenates transforms upstream."

#define kPluginIdentifier "net.sf.openfx.GodRays"
#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false

#define kParamFromColor "fromColor"
#define kParamFromColorLabel "From Color"
#define kParamFromColorHint "Color by which the initial image is multiplied."

#define kParamToColor "toColor"
#define kParamToColorLabel "To Color"
#define kParamToColorHint "Color by which the final image is multiplied."

#define kParamGamma "gamma"
#define kParamGammaLabel "Gamma"
#define kParamGammaHint "Gamma space in which the colors are interpolated. Higher values yield brighter intermediate images."

#define USE_STEPS

#ifdef USE_STEPS
#define kParamSteps "steps"
#define kParamStepsLabel "Steps"
#define kParamStepsHint "The number of intermediate images is 2^steps, i.e. 32 for steps=5."
#endif

#define kParamMax "max"
#define kParamMaxLabel "Max"
#define kParamMaxHint "Output the brightest value at each pixel rather than the average."

#ifndef USE_STEPS
#define kTransform3x3MotionBlurCount 1000 // number of transforms used in the motion
#endif

#define kParamPremultChanged "premultChanged"


class GodRaysProcessorBase
    : public Transform3x3ProcessorBase
{
protected:
    double _fromColor[4];
    double _toColor[4];
    double _gamma[4];
#ifdef USE_STEPS
    int _steps;
#endif
    bool _max;

public:

    GodRaysProcessorBase(ImageEffect &instance)
        : Transform3x3ProcessorBase(instance)
#ifdef USE_STEPS
        , _steps(5)
#endif
        , _max(false)
    {
        for (int c = 0; c < 4; ++c) {
            _fromColor[c] = _toColor[c] = _gamma[c] = 1.;
        }
    }

    virtual void setValues(const Matrix3x3* invtransform, //!< non-generic - must be in PIXEL coords
                           size_t invtransformsize,
                           // all generic parameters below
                           bool blackOutside, //!< generic
                           double motionblur,
                           double mix,
                           double fromColor[4],
                           double toColor[4],
                           double gamma[4],
#ifdef USE_STEPS
                           int steps,
#endif
                           bool max)
    {
        Transform3x3ProcessorBase::setValues(invtransform, 0, invtransformsize, blackOutside, motionblur, mix);

        for (int c = 0; c < 4; ++c) {
            _fromColor[c] = fromColor[c];
            _toColor[c] = toColor[c];
            _gamma[c] = (gamma[c] > 0.) ? gamma[c] : 1.;
        }
#ifdef USE_STEPS
        _steps = steps;
#endif
        _max = max;
    }
};

// The "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, FilterEnum filter, bool clamp>
class GodRaysProcessor
    : public GodRaysProcessorBase
{
public:
    GodRaysProcessor(ImageEffect &instance)
        : GodRaysProcessorBase(instance)
    {
    }

private:
    virtual FilterEnum getFilter() const OVERRIDE FINAL
    {
        return filter;
    }

    virtual bool getClamp() const OVERRIDE FINAL
    {
        return clamp;
    }

    virtual void setValues(const Matrix3x3* invtransform, //!< non-generic - must be in PIXEL coords
                           size_t invtransformsize,
                           // all generic parameters below
                           bool blackOutside, //!< generic
                           double motionblur,
                           double mix,
                           double fromColor[4],
                           double toColor[4],
                           double gamma[4],
#ifdef USE_STEPS
                           int steps,
#endif
                           bool max) OVERRIDE FINAL
    {
        GodRaysProcessorBase::setValues(invtransform, invtransformsize, blackOutside, motionblur, mix, fromColor, toColor, gamma, steps, max);

        _color.resize(invtransformsize);
#ifdef GODRAYS_LINEAR_INTERPOLATION
        // Linear interpolation is usually not whant the user wants, because in real life crepuscular rays have an exponential decrease in intensity.
        int range = std::max(1, (int)invtransformsize); // works even if invtransformsize = 1
        // Same as Nuke: toColor is never completely reached.
        for (int i = 0; i < (int)invtransformsize; ++i) {
            double alpha = (i + 1) / (double)range; // alpha is never 0
            for (int c = 0; c < nComponents; ++c) {
                int ci = (nComponents == 1) ? 3 : c;
                double g = gamma[ci];
                if (g != 1.) {
                    _color[i][c] = std::pow(std::pow(std::max(0., fromColor[ci]), g) * alpha +
                                            std::pow(std::max(0., toColor[ci]),  g) * (1 - alpha), 1. / g);
                } else {
                    _color[i][c] = fromColor[ci] * alpha + toColor[ci] * (1. - alpha);
                }
            }
        }
#else
        // exponential decrease for gamma = 1, less than exponential for gamma > 1
        for (int c = 0; c < nComponents; ++c) {
            int ci = (nComponents == 1) ? 3 : c;
            double g = gamma[ci];
            double col1 = std::max(0.001, fromColor[ci]);
            double col2 = std::max(0.001, toColor[ci]);
            for (int i = invtransformsize - 1; i >= 0; --i) {
                double col = col1 * std::pow(col2 / col1, (invtransformsize - 1 - i) / (double)invtransformsize);
                if ( (g == 1.) || (col1 == col2) ) {
                    _color[i][c] = col;
                } else {
                    // "gamma"-interpolation
                    // reinterpret the color descrease in a different gamma space
                    double alpha = (col - col2) / (col1 - col2);
                    _color[i][c] = std::pow(std::pow(col1, g) * alpha +
                                            std::pow(col2, g) * (1 - alpha), 1. / g);
                }
            }
        }
#endif
    }

    void multiThreadProcessImages(OfxRectI procWindow) OVERRIDE
    {
        assert(_invtransform);
        if (_motionblur == 0.) { // no motion blur
            return multiThreadProcessImagesNoBlur(procWindow);
        } else { // motion blur
            return multiThreadProcessImagesMotionBlur(procWindow);
        }
    } // multiThreadProcessImages

private:
    void multiThreadProcessImagesNoBlur(const OfxRectI &procWindow)
    {
        float tmpPix[nComponents];
        const Matrix3x3 & H = _invtransform[0];

        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // the coordinates of the center of the pixel in canonical coordinates
            // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
            Point3D canonicalCoords;
            canonicalCoords.z = 1;
            canonicalCoords.y = (double)y + 0.5;

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                // NON-GENERIC TRANSFORM

                // the coordinates of the center of the pixel in canonical coordinates
                // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
                canonicalCoords.x = (double)x + 0.5;
                Point3D transformed = H * canonicalCoords;
                if ( !_srcImg || (transformed.z <= 0.) ) {
                    // the back-transformed point is at infinity (==0) or behind the camera (<0)
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = 0;
                    }
                } else {
                    double fx = transformed.z != 0 ? transformed.x / transformed.z : transformed.x;
                    double fy = transformed.z != 0 ? transformed.y / transformed.z : transformed.y;
                    if (filter == eFilterImpulse) {
                        ofxsFilterInterpolate2D<PIX, nComponents, filter, clamp>(fx, fy, _srcImg, _blackOutside, tmpPix);
                    } else {
                        double Jxx = (H(0,0) * transformed.z - transformed.x * H(2,0)) / (transformed.z * transformed.z);
                        double Jxy = (H(0,1) * transformed.z - transformed.x * H(2,1)) / (transformed.z * transformed.z);
                        double Jyx = (H(1,0) * transformed.z - transformed.y * H(2,0)) / (transformed.z * transformed.z);
                        double Jyy = (H(1,1) * transformed.z - transformed.y * H(2,1)) / (transformed.z * transformed.z);
                        ofxsFilterInterpolate2DSuper<PIX, nComponents, filter, clamp>(fx, fy, Jxx, Jxy, Jyx, Jyy, _srcImg, _blackOutside, tmpPix);
                    }
                }

                ofxsMaskMix<PIX, nComponents, maxValue, true>(tmpPix, x, y, _srcImg, _domask, _maskImg, (float)_mix, _maskInvert, dstPix);
            }
        }
    }

    void multiThreadProcessImagesMotionBlur(const OfxRectI &procWindow)
    {
        float tmpPix[nComponents];

#ifndef USE_STEPS
        const double maxErr2 = kTransform3x3ProcessorMotionBlurMaxError * kTransform3x3ProcessorMotionBlurMaxError; // maximum expected squared error
        const int maxIt = kTransform3x3ProcessorMotionBlurMaxIterations; // maximum number of iterations
        // Monte Carlo integration, starting with at least 13 regularly spaced samples, and then low discrepancy
        // samples from the van der Corput sequence.
#endif
        for (int y = procWindow.y1; y < procWindow.y2; ++y) {
            if ( _effect.abort() ) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            // the coordinates of the center of the pixel in canonical coordinates
            // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
            Point3D canonicalCoords;
            canonicalCoords.z = 1;
            canonicalCoords.y = (double)y + 0.5;

            for (int x = procWindow.x1; x < procWindow.x2; ++x, dstPix += nComponents) {
                float max[nComponents];
                double accPix[nComponents];
                double mean[nComponents];
#ifndef USE_STEPS
                double accPix2[nComponents];
                double var[nComponents];
#endif
                for (int c = 0; c < nComponents; ++c) {
                    max[c] = 0;
                    accPix[c] = 0;
                    mean[c] = 0.;
#ifndef USE_STEPS
                    accPix2[c] = 0;
                    var[c] = (double)maxValue * maxValue;
#endif
                }
                int sample = 0;
#ifdef USE_STEPS
                const int minsamples = _invtransformsize;
#else
                const int minsamples = kTransform3x3ProcessorMotionBlurMinIterations; // minimum number of samples (at most maxIt/3
                unsigned int seed = (unsigned int)( hash(hash( x + (unsigned int)(0x10000 * _motionblur) ) + y) );
#endif
                int maxsamples = minsamples;
                while (sample < maxsamples) {
                    for (; sample < maxsamples; ++sample) {
                        int t;
#ifdef USE_STEPS
                        t = sample;
#else
                        //int t = 0.5*(van_der_corput<2>(seed1) + van_der_corput<3>(seed2)) * _invtransform.size();
                        if (sample < minsamples) {
                            // distribute the first samples evenly over the interval
                            t = (int)( ( sample  + van_der_corput<2>(seed) ) * _invtransformsize / (double)minsamples );
                        } else {
                            t = (int)(van_der_corput<2>(seed) * _invtransformsize);
                        }
                        ++seed;
#endif
                        // NON-GENERIC TRANSFORM

                        // the coordinates of the center of the pixel in canonical coordinates
                        // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
                        canonicalCoords.x = (double)x + 0.5;
                        const Matrix3x3& H = _invtransform[t];
                        Point3D transformed = H * canonicalCoords;
                        if ( !_srcImg || (transformed.z <= 0.) ) {
                            // the back-transformed point is at infinity (==0) or behind the camera (<0)
                            for (int c = 0; c < nComponents; ++c) {
                                tmpPix[c] = 0;
                            }
                        } else {
                            double fx = transformed.z != 0 ? transformed.x / transformed.z : transformed.x;
                            double fy = transformed.z != 0 ? transformed.y / transformed.z : transformed.y;
                            if (filter == eFilterImpulse) {
                                ofxsFilterInterpolate2D<PIX, nComponents, filter, clamp>(fx, fy, _srcImg, _blackOutside, tmpPix);
                            } else {
                                double Jxx = (H(0,0) * transformed.z - transformed.x * H(2,0)) / (transformed.z * transformed.z);
                                double Jxy = (H(0,1) * transformed.z - transformed.x * H(2,1)) / (transformed.z * transformed.z);
                                double Jyx = (H(1,0) * transformed.z - transformed.y * H(2,0)) / (transformed.z * transformed.z);
                                double Jyy = (H(1,1) * transformed.z - transformed.y * H(2,1)) / (transformed.z * transformed.z);
                                ofxsFilterInterpolate2DSuper<PIX, nComponents, filter, clamp>(fx, fy, Jxx, Jxy, Jyx, Jyy, _srcImg, _blackOutside, tmpPix);
                            }
                        }
                        for (int c = 0; c < nComponents; ++c) {
                            // multiply by color
                            tmpPix[c] *= _color[t][c];
                            if (_max) {
                                max[c] = std::max(max[c], tmpPix[c]);
                            }
                            accPix[c] += tmpPix[c];
#ifndef USE_STEPS
                            accPix2[c] += tmpPix[c] * tmpPix[c];
#endif
                        }
                    }
#ifndef USE_STEPS
                    // compute mean and variance (unbiased)
                    for (int c = 0; c < nComponents; ++c) {
                        mean[c] = sample ? accPix[c] / sample : 0;
                        if (sample <= 1) {
                            var[c] = (double)maxValue * maxValue;
                        } else {
                            var[c] = (accPix2[c] - mean[c] * mean[c] * sample) / (sample - 1);
                            // the variance of the mean is var[c]/n, so compute n so that it falls below some threashold (maxErr2).
                            // Note that this could be improved/optimized further by variance reduction and importance sampling
                            // http://www.scratchapixel.com/lessons/3d-basic-lessons/lesson-17-monte-carlo-methods-in-practice/variance-reduction-methods-a-quick-introduction-to-importance-sampling/
                            // http://www.scratchapixel.com/lessons/3d-basic-lessons/lesson-xx-introduction-to-importance-sampling/
                            // The threshold is computed by a simple rule of thumb:
                            // - the error should be less than motionblur*maxValue/100
                            // - the total number of iterations should be less than motionblur*100
                            if (maxsamples < maxIt) {
                                maxsamples = std::max( maxsamples, std::min( (int)(var[c] / maxErr2), maxIt ) );
                            }
                        }
                    }
#endif
                }
                if (_max) {
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = (float)max[c];
                    }
                } else {
#ifdef USE_STEPS
                    for (int c = 0; c < nComponents; ++c) {
                        mean[c] = sample ? accPix[c] / sample : 0;
                    }
#endif
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = (float)mean[c];
                    }
                }
                ofxsMaskMix<PIX, nComponents, maxValue, true>(tmpPix, x, y, _srcImg, _domask, _maskImg, (float)_mix, _maskInvert, dstPix);
            }
        }
    } // multiThreadProcessImagesMotionBlur

#ifndef USE_STEPS
    // Compute the /seed/th element of the van der Corput sequence
    // see http://en.wikipedia.org/wiki/Van_der_Corput_sequence
    template <int base>
    double van_der_corput(unsigned int seed)
    {
        double base_inv;
        int digit;
        double r;

        r = 0.0;

        base_inv = 1.0 / ( (double)base );

        while (seed != 0) {
            digit = seed % base;
            r = r + ( (double)digit ) * base_inv;
            base_inv = base_inv / ( (double)base );
            seed = seed / base;
        }

        return r;
    }

    unsigned int hash(unsigned int a)
    {
        a = (a ^ 61) ^ (a >> 16);
        a = a + (a << 3);
        a = a ^ (a >> 4);
        a = a * 0x27d4eb2d;
        a = a ^ (a >> 15);

        return a;
    }

#endif

private:
    class Pix
    {
public:
        Pix() { std::fill(_data, _data + nComponents, 0.f); }

        float operator [](size_t c) const { return _data[c]; }

        float& operator [](size_t c) { return _data[c]; }

private:
        float _data[nComponents];
    };

    std::vector<Pix > _color;
};

////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class GodRaysPlugin
    : public Transform3x3Plugin
{
public:
    /** @brief ctor */
    GodRaysPlugin(OfxImageEffectHandle handle)
        : Transform3x3Plugin(handle, true, eTransform3x3ParamsTypeDirBlur)
        , _translate(NULL)
        , _rotate(NULL)
        , _scale(NULL)
        , _scaleUniform(NULL)
        , _skewX(NULL)
        , _skewY(NULL)
        , _skewOrder(NULL)
        , _center(NULL)
        , _interactive(NULL)
        , _fromColor(NULL)
        , _toColor(NULL)
        , _gamma(NULL)
#ifdef USE_STEPS
        , _steps(NULL)
#endif
        , _max(NULL)
        , _premultChanged(NULL)
    {
        // NON-GENERIC
        if ( paramExists(kParamTransformTranslateOld) ) {
            _translate = fetchDouble2DParam(kParamTransformTranslateOld);
            assert(_translate);
        }
        _rotate = fetchDoubleParam(kParamTransformRotateOld);
        _scale = fetchDouble2DParam(kParamTransformScaleOld);
        _scaleUniform = fetchBooleanParam(kParamTransformScaleUniformOld);
        _skewX = fetchDoubleParam(kParamTransformSkewXOld);
        _skewY = fetchDoubleParam(kParamTransformSkewYOld);
        _skewOrder = fetchChoiceParam(kParamTransformSkewOrderOld);
        _center = fetchDouble2DParam(kParamTransformCenterOld);
        _centerChanged = fetchBooleanParam(kParamTransformCenterChanged);
        _interactive = fetchBooleanParam(kParamTransformInteractiveOld);
        assert(_rotate && _scale && _scaleUniform && _skewX && _skewY && _skewOrder && _center && _interactive);

        _fromColor = fetchRGBAParam(kParamFromColor);
        _toColor = fetchRGBAParam(kParamToColor);
        _gamma = fetchRGBAParam(kParamGamma);
#ifdef USE_STEPS
        _steps = fetchIntParam(kParamSteps);
        assert(_steps);
#endif
        _max = fetchBooleanParam(kParamMax);

        assert(_fromColor && _toColor && _gamma && _max);
        _premultChanged = fetchBooleanParam(kParamPremultChanged);
        assert(_premultChanged);
        // On Natron, hide the uniform parameter if it is false and not animated,
        // since uniform scaling is easy through Natron's GUI.
        // The parameter is kept for backward compatibility.
        // Fixes https://github.com/MrKepzie/Natron/issues/1204
        if ( getImageEffectHostDescription()->isNatron &&
             !_scaleUniform->getValue() &&
             ( _scaleUniform->getNumKeys() == 0) ) {
            _scaleUniform->setIsSecretAndDisabled(true);
        }
    }

private:
    virtual bool isIdentity(double time) OVERRIDE FINAL;
    virtual bool getInverseTransformCanonical(double time, int view, double amount, bool invert, Matrix3x3* invtransform) const OVERRIDE FINAL;

    void resetCenter(double time);

    virtual void changedParam(const InstanceChangedArgs &args, const std::string &paramName) OVERRIDE FINAL;

    /** @brief called when a clip has just been changed in some way (a rewire maybe) */
    virtual void changedClip(const InstanceChangedArgs &args, const std::string &clipName) OVERRIDE FINAL;
    virtual void render(const RenderArguments &args) OVERRIDE FINAL;

private:
    /* internal render function */
    template <class PIX, int nComponents, int maxValue>
    void renderInternalForBitDepth(const RenderArguments &args);

    template <int nComponents>
    void renderInternal(const RenderArguments &args, BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(GodRaysProcessorBase &, const RenderArguments &args);

    // NON-GENERIC
    Double2DParam* _translate;
    DoubleParam* _rotate;
    Double2DParam* _scale;
    BooleanParam* _scaleUniform;
    DoubleParam* _skewX;
    DoubleParam* _skewY;
    ChoiceParam* _skewOrder;
    Double2DParam* _center;
    BooleanParam* _centerChanged;
    BooleanParam* _interactive;
    RGBAParam* _fromColor;
    RGBAParam* _toColor;
    RGBAParam* _gamma;
    IntParam* _steps;
    BooleanParam* _max;
    BooleanParam* _premultChanged; // set to true the first time the user connects src
};

// overridden is identity
bool
GodRaysPlugin::isIdentity(double time)
{
    // NON-GENERIC
    OfxPointD scale = { 1., 1. };
    OfxPointD translate = { 0., 0. };
    double rotate = 0.;
    double skewX = 0., skewY = 0.;

    if (_scale) {
        _scale->getValueAtTime(time, scale.x, scale.y);
    }
    bool scaleUniform = false;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }
    if (scaleUniform) {
        scale.y = scale.x;
    }
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    if (_rotate) {
        _rotate->getValueAtTime(time, rotate);
    }
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }

    if ( (scale.x == 1.) && (scale.y == 1.) && (translate.x == 0.) && (translate.y == 0.) && (rotate == 0.) && (skewX == 0.) && (skewY == 0.) ) {
        return true;
    }

    int steps;
    _steps->getValueAtTime(time, steps);
    if (steps == 0) {
        return true;
    }

    return false;
}

bool
GodRaysPlugin::getInverseTransformCanonical(double time,
                                            int /*view*/,
                                            double amount,
                                            bool invert,
                                            Matrix3x3* invtransform) const
{
    // NON-GENERIC
    OfxPointD center = { 0., 0. };

    if (_center) {
        _center->getValueAtTime(time, center.x, center.y);
    }
    OfxPointD translate = { 0., 0. };
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    OfxPointD scaleParam = { 1., 1. };
    if (_scale) {
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    }
    bool scaleUniform = false;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }
    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);
    double rotate = 0.;
    if (_rotate) {
        _rotate->getValueAtTime(time, rotate);
    }
    double skewX = 0.;
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    double skewY = 0.;
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }
    int skewOrder = 0;
    if (_skewOrder) {
        _skewOrder->getValueAtTime(time, skewOrder);
    }

    if (amount != 1.) {
        translate.x *= amount;
        translate.y *= amount;
        if (scale.x <= 0. || amount <= 0.) {
            // linear interpolation
            scale.x = 1. + (scale.x - 1.) * amount;
        } else {
            // geometric interpolation
            scale.x = std::pow(scale.x, amount);
        }
        if (scale.y <= 0 || amount <= 0.) {
            // linear interpolation
            scale.y = 1. + (scale.y - 1.) * amount;
        } else {
            // geometric interpolation
            scale.y = std::pow(scale.y, amount);
        }
        rotate *= amount;
        skewX *= amount;
        skewY *= amount;
    }

    double rot = ofxsToRadians(rotate);

    if (!invert) {
        *invtransform = ofxsMatInverseTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    } else {
        *invtransform = ofxsMatTransformCanonical(translate.x, translate.y, scale.x, scale.y, skewX, skewY, (bool)skewOrder, rot, center.x, center.y);
    }

    return true;
} // GodRaysPlugin::getInverseTransformCanonical

void
GodRaysPlugin::resetCenter(double time)
{
    if (!_srcClip || !_srcClip->isConnected()) {
        return;
    }
    OfxRectD rod = _srcClip->getRegionOfDefinition(time);
    if ( (rod.x1 <= kOfxFlagInfiniteMin) || (kOfxFlagInfiniteMax <= rod.x2) ||
         ( rod.y1 <= kOfxFlagInfiniteMin) || ( kOfxFlagInfiniteMax <= rod.y2) ) {
        return;
    }
    if ( Coords::rectIsEmpty(rod) ) {
        // default to project window
        OfxPointD offset = getProjectOffset();
        OfxPointD size = getProjectSize();
        rod.x1 = offset.x;
        rod.x2 = offset.x + size.x;
        rod.y1 = offset.y;
        rod.y2 = offset.y + size.y;
    }
    double currentRotation = 0.;
    if (_rotate) {
        _rotate->getValueAtTime(time, currentRotation);
    }
    double rot = ofxsToRadians(currentRotation);
    double skewX = 0.;
    if (_skewX) {
        _skewX->getValueAtTime(time, skewX);
    }
    double skewY = 0.;
    if (_skewY) {
        _skewY->getValueAtTime(time, skewY);
    }
    int skewOrder = 0;
    if (_skewOrder) {
        _skewOrder->getValueAtTime(time, skewOrder);
    }

    OfxPointD scaleParam = { 1., 1. };
    if (_scale) {
        _scale->getValueAtTime(time, scaleParam.x, scaleParam.y);
    }
    bool scaleUniform = false;
    if (_scaleUniform) {
        _scaleUniform->getValueAtTime(time, scaleUniform);
    }
    OfxPointD scale = { 1., 1. };
    ofxsTransformGetScale(scaleParam, scaleUniform, &scale);

    OfxPointD translate = { 0., 0. };
    if (_translate) {
        _translate->getValueAtTime(time, translate.x, translate.y);
    }
    OfxPointD center = { 0., 0. };
    if (_center) {
        _center->getValueAtTime(time, center.x, center.y);
    }

    Matrix3x3 Rinv = ( ofxsMatRotation(-rot) *
                       ofxsMatSkewXY(skewX, skewY, skewOrder) *
                       ofxsMatScale(scale.x, scale.y) );
    OfxPointD newCenter;
    newCenter.x = (rod.x1 + rod.x2) / 2;
    newCenter.y = (rod.y1 + rod.y2) / 2;
    beginEditBlock("resetCenter");
    if (_center) {
        _center->setValue(newCenter.x, newCenter.y);
    }
    if (_translate) {
        double dxrot = newCenter.x - center.x;
        double dyrot = newCenter.y - center.y;
        Point3D dRot;
        dRot.x = dxrot;
        dRot.y = dyrot;
        dRot.z = 1;
        dRot = Rinv * dRot;
        if (dRot.z != 0) {
            dRot.x /= dRot.z;
            dRot.y /= dRot.z;
        }
        double dx = dRot.x;
        double dy = dRot.y;
        OfxPointD newTranslate;
        newTranslate.x = translate.x + dx - dxrot;
        newTranslate.y = translate.y + dy - dyrot;
        _translate->setValue(newTranslate.x, newTranslate.y);
    }
    endEditBlock();
} // GodRaysPlugin::resetCenter

void
GodRaysPlugin::changedParam(const InstanceChangedArgs &args,
                            const std::string &paramName)
{
    if (paramName == kParamTransformResetCenterOld) {
        resetCenter(args.time);
        _centerChanged->setValue(false);
    } else if ( (paramName == kParamTransformTranslateOld) ||
                ( paramName == kParamTransformRotateOld) ||
                ( paramName == kParamTransformScaleOld) ||
                ( paramName == kParamTransformScaleUniformOld) ||
                ( paramName == kParamTransformSkewXOld) ||
                ( paramName == kParamTransformSkewYOld) ||
                ( paramName == kParamTransformSkewOrderOld) ||
                ( paramName == kParamTransformCenterOld) ) {
        if ( (paramName == kParamTransformCenterOld) &&
             ( (args.reason == eChangeUserEdit) || (args.reason == eChangePluginEdit) ) ) {
            _centerChanged->setValue(true);
        }
        changedTransform(args);
    } else if ( (paramName == kParamPremult) && (args.reason == eChangeUserEdit) ) {
        _premultChanged->setValue(true);
    } else {
        Transform3x3Plugin::changedParam(args, paramName);
    }
}

void
GodRaysPlugin::changedClip(const InstanceChangedArgs &args,
                           const std::string &clipName)
{
    if ( (clipName == kOfxImageEffectSimpleSourceClipName) &&
         _srcClip && _srcClip->isConnected() &&
         !_centerChanged->getValueAtTime(args.time) &&
         ( args.reason == eChangeUserEdit) ) {
        resetCenter(args.time);
    }
}

/* set up and run a processor */
void
GodRaysPlugin::setupAndProcess(GodRaysProcessorBase &processor,
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
         ( ( ( dst->getField() != eFieldNone) /* for DaVinci Resolve */ && ( dst->getField() != args.fieldToRender) ) ) ) {
        setPersistentMessage(Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        throwSuiteStatusException(kOfxStatFailed);
    }
    auto_ptr<const Image> src( ( _srcClip && _srcClip->isConnected() ) ?
                                    _srcClip->fetchImage(args.time) : 0 );
    size_t invtransformsizealloc = 0;
    size_t invtransformsize = 0;
    std::vector<Matrix3x3> invtransform;
    bool blackOutside = true;
    double motionblur = 1.;
    double mix = 1.;
#ifdef USE_STEPS
    int steps = 5;
#endif

    if ( !src.get() ) {
        // no source image, use a dummy transform
        invtransformsizealloc = 1;
        invtransform.resize(invtransformsizealloc);
        invtransformsize = 1;
        invtransform[0](0,0) = 0.;
        invtransform[0](0,1) = 0.;
        invtransform[0](0,2) = 0.;
        invtransform[0](1,0) = 0.;
        invtransform[0](1,1) = 0.;
        invtransform[0](1,2) = 0.;
        invtransform[0](2,0) = 0.;
        invtransform[0](2,1) = 0.;
        invtransform[0](2,2) = 1.;
    } else {
        BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        PixelComponentEnum dstComponents  = dst->getPixelComponents();
        BitDepthEnum srcBitDepth      = src->getPixelDepth();
        PixelComponentEnum srcComponents = src->getPixelComponents();
        if ( (srcBitDepth != dstBitDepth) || (srcComponents != dstComponents) ) {
            throwSuiteStatusException(kOfxStatFailed);
        }

        bool invert = false;
        if (_invert) {
            _invert->getValueAtTime(time, invert);
        }
        if (_blackOutside) {
            _blackOutside->getValueAtTime(time, blackOutside);
        }
        if (_mix) {
            _mix->getValueAtTime(time, mix);
        }
#ifndef USE_STEPS
        if (_motionblur) {
            _motionblur->getValueAtTime(time, motionblur);
        }
#endif
        const bool fielded = args.fieldToRender == eFieldLower || args.fieldToRender == eFieldUpper;
        const double srcpixelAspectRatio = src->getPixelAspectRatio();
        const double dstpixelAspectRatio = dst->getPixelAspectRatio();

#ifdef USE_STEPS
        if (_steps) {
            _steps->getValueAtTime(time, steps);
        }
        invtransformsizealloc = 1 << std::max(0, steps);
#else
        invtransformsizealloc = kTransform3x3MotionBlurCount;
#endif
        invtransform.resize(invtransformsizealloc);
        invtransformsize = getInverseTransformsBlur(time, args.renderView, args.renderScale, fielded, srcpixelAspectRatio, dstpixelAspectRatio, invert, 0., 1., &invtransform.front(), 0, invtransformsizealloc);
        if (invtransformsize == 1) {
            motionblur  = 0.;
        }
        // compose with the input transform
        if ( !src->getTransformIsIdentity() ) {
            double srcTransform[9]; // transform to apply to the source image, in pixel coordinates, from source to destination
            src->getTransform(srcTransform);
            Matrix3x3 srcTransformMat;
            srcTransformMat(0,0) = srcTransform[0];
            srcTransformMat(0,1) = srcTransform[1];
            srcTransformMat(0,2) = srcTransform[2];
            srcTransformMat(1,0) = srcTransform[3];
            srcTransformMat(1,1) = srcTransform[4];
            srcTransformMat(1,2) = srcTransform[5];
            srcTransformMat(2,0) = srcTransform[6];
            srcTransformMat(2,1) = srcTransform[7];
            srcTransformMat(2,2) = srcTransform[8];
            // invert it
            Matrix3x3 srcTransformInverse;
            if ( srcTransformMat.inverse(&srcTransformInverse) ) {
                for (size_t i = 0; i < invtransformsize; ++i) {
                    invtransform[i] = srcTransformInverse * invtransform[i];
                }
            }
        }
    }

    // auto ptr for the mask.
    bool doMasking = ( ( !_maskApply || _maskApply->getValueAtTime(args.time) ) && _maskClip && _maskClip->isConnected() );
    auto_ptr<const Image> mask(doMasking ? _maskClip->fetchImage(args.time) : 0);
    if (doMasking) {
        bool maskInvert = false;
        if (_maskInvert) {
            _maskInvert->getValueAtTime(time, maskInvert);
        }

        // say we are masking
        processor.doMasking(true);

        // Set it in the processor
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg( dst.get() );
    processor.setSrcImg( src.get() );

    double fromColor[4] = {1., 1., 1., 1.};
    double toColor[4] = {1., 1., 1., 1.};
    double gamma[4] = {1., 1., 1., 1.};
    bool max = false;
    if (_fromColor) {
        _fromColor->getValueAtTime(time, fromColor[0], fromColor[1], fromColor[2], fromColor[3]);
    }
    if (_toColor) {
        _toColor->getValueAtTime(time, toColor[0], toColor[1], toColor[2], toColor[3]);
    }
    if (_gamma) {
        _gamma->getValueAtTime(time, gamma[0], gamma[1], gamma[2], gamma[3]);
    }
    if (_max) {
        _max->getValueAtTime(time, max);
    }
#ifdef USE_STEPS
    if (invtransformsize > 1) {
        // instruct the processor to use all transforms
        motionblur = -1.;
    }
#endif

    // set the render window
    processor.setRenderWindow(args.renderWindow);
    assert(invtransform.size() && invtransformsize);
    processor.setValues(&invtransform.front(),
                        invtransformsize,
                        blackOutside,
                        motionblur,
                        mix,
                        fromColor,
                        toColor,
                        gamma,
#ifdef USE_STEPS
                        steps,
#endif
                        max);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
} // setupAndProcess

template <class PIX, int nComponents, int maxValue>
void
GodRaysPlugin::renderInternalForBitDepth(const RenderArguments &args)
{
    const double time = args.time;
    FilterEnum filter = args.renderQualityDraft ? eFilterImpulse : eFilterCubic;

    if (!args.renderQualityDraft && _filter) {
        filter = (FilterEnum)_filter->getValueAtTime(time);
    }
    bool clamp = false;
    if (_clamp) {
        _clamp->getValueAtTime(time, clamp);
    }

    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch (filter) {
    case eFilterImpulse: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterImpulse, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterBox: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterBox, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterBilinear: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterBilinear, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterCubic: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterCubic, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterKeys:
        if (clamp) {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterKeys, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterKeys, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterSimon:
        if (clamp) {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterSimon, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterSimon, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterRifman:
        if (clamp) {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterRifman, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterRifman, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterMitchell:
        if (clamp) {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterMitchell, true> fred(*this);
            setupAndProcess(fred, args);
        } else {
            GodRaysProcessor<PIX, nComponents, maxValue, eFilterMitchell, false> fred(*this);
            setupAndProcess(fred, args);
        }
        break;
    case eFilterParzen: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterParzen, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    case eFilterNotch: {
        GodRaysProcessor<PIX, nComponents, maxValue, eFilterNotch, false> fred(*this);
        setupAndProcess(fred, args);
        break;
    }
    } // switch
} // renderInternalForBitDepth

// the internal render function
template <int nComponents>
void
GodRaysPlugin::renderInternal(const RenderArguments &args,
                              BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
    case eBitDepthUByte:
        renderInternalForBitDepth<unsigned char, nComponents, 255>(args);
        break;
    case eBitDepthUShort:
        renderInternalForBitDepth<unsigned short, nComponents, 65535>(args);
        break;
    case eBitDepthFloat:
        renderInternalForBitDepth<float, nComponents, 1>(args);
        break;
    default:
        throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
GodRaysPlugin::render(const RenderArguments &args)
{
    // instantiate the render code based on the pixel depth of the dst clip
    BitDepthEnum dstBitDepth    = _dstClip->getPixelDepth();
    PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert( kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio() );
    assert( kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth() );
    assert(dstComponents == ePixelComponentAlpha || dstComponents == ePixelComponentXY || dstComponents == ePixelComponentRGB || dstComponents == ePixelComponentRGBA);
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

mDeclarePluginFactory(GodRaysPluginFactory, {ofxsThreadSuiteCheck();}, {});
void
GodRaysPluginFactory::describe(ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabel(kPluginName);
    desc.setPluginGrouping(kPluginGrouping);
    desc.setPluginDescription(kPluginDescription);

    Transform3x3Describe(desc, true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptorOldParams);
}

void
GodRaysPluginFactory::describeInContext(ImageEffectDescriptor &desc,
                                        ContextEnum context)
{
    // make some pages and to things in
    PageParamDescriptor *page = Transform3x3DescribeInContextBegin(desc, context, true);

    // NON-GENERIC PARAMETERS
    //
    ofxsTransformDescribeParams(desc, page, NULL, /*isOpen=*/ true, /*oldParams=*/ true, /*hasAmount=*/ true, /*noTranslate=*/ true);

    // invert
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamTransform3x3Invert);
        param->setLabel(kParamTransform3x3InvertLabel);
        param->setHint(kParamTransform3x3InvertHint);
        param->setDefault(false);
        param->setAnimates(true);
        if (page) {
            page->addChild(*param);
        }
    }
    // GENERIC PARAMETERS
    //

    ofxsFilterDescribeParamsInterpolate2D(desc, page, false);

    // fromColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamFromColor);
        param->setLabel(kParamFromColorLabel);
        param->setHint(kParamFromColorHint);
        param->setDefault(1., 1., 1., 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    // toColor
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamToColor);
        param->setLabel(kParamToColorLabel);
        param->setHint(kParamToColorHint);
        param->setDefault(1., 1., 1., 1.);
        if (page) {
            page->addChild(*param);
        }
    }

    // gamma
    {
        RGBAParamDescriptor* param = desc.defineRGBAParam(kParamGamma);
        param->setLabel(kParamGammaLabel);
        param->setHint(kParamGammaHint);
        param->setDefault(1., 1., 1., 1.);
        param->setRange(0., 0., 0., 0., DBL_MAX, DBL_MAX, DBL_MAX, DBL_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0.2, 0.2, 0.2, 0.2, 5., 5., 5., 5.);
        if (page) {
            page->addChild(*param);
        }
    }

#ifdef USE_STEPS
    // steps
    {
        IntParamDescriptor* param = desc.defineIntParam(kParamSteps);
        param->setLabel(kParamStepsLabel);
        param->setHint(kParamStepsHint);
        param->setDefault(5);
        param->setRange(0, INT_MAX); // Resolve requires range and display range or values are clamped to (-1,1)
        param->setDisplayRange(0, 10);
        if (page) {
            page->addChild(*param);
        }
    }
#else
    // motionBlur
    {
        DoubleParamDescriptor* param = desc.defineDoubleParam(kParamTransform3x3MotionBlur);
        param->setLabel(kParamTransform3x3MotionBlurLabel);
        param->setHint(kParamTransform3x3MotionBlurHint);
        param->setDefault(1.);
        param->setRange(0., 100.);
        param->setIncrement(0.01);
        param->setDisplayRange(0., 4.);
        if (page) {
            page->addChild(*param);
        }
    }
#endif

    // max
    {
        BooleanParamDescriptor* param = desc.defineBooleanParam(kParamMax);
        param->setLabel(kParamMaxLabel);
        param->setHint(kParamMaxHint);
        param->setDefault(false);
        if (page) {
            page->addChild(*param);
        }
    }

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
} // GodRaysPluginFactory::describeInContext

ImageEffect*
GodRaysPluginFactory::createInstance(OfxImageEffectHandle handle,
                                     ContextEnum /*context*/)
{
    return new GodRaysPlugin(handle);
}

static GodRaysPluginFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor);
mRegisterPluginFactoryInstance(p)

OFXS_NAMESPACE_ANONYMOUS_EXIT
