/*
 OFX Transform plugin.
 
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

/*
   Although the indications from nuke/fnOfxExtensions.h were followed, and the
   kFnOfxImageEffectActionGetTransform action was implemented in the Support
   library, that action is never called by the Nuke host, so it cannot be tested.
   The code is left here for reference or for further extension.

   There is also an open question about how the last plugin in a transform chain
   may get the concatenated transform from upstream, the untransformed source image,
   concatenate its own transform and apply the resulting transform in its render
   action. Should the host be doing this instead?
*/
// Uncomment the following to enable the experimental host transform code.
//#define ENABLE_HOST_TRANSFORM

#include "Transform.h"

#include <cmath>
#include <iostream>
#ifdef _WINDOWS
#include <windows.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif


#include "../include/ofxsProcessing.H"
#ifdef OFX_EXTENSIONS_NUKE
#include "nuke/fnOfxExtensions.h"
#endif

#include "Transform2D.h"

#ifndef ENABLE_HOST_TRANSFORM
#undef OFX_EXTENSIONS_NUKE // host transform is the only nuke extension used
#endif

// NON-GENERIC
#define kTranslateParamName "Translate"
#define kRotateParamName "Rotate"
#define kScaleParamName "Scale"
#define kScaleUniformParamName "Uniform"
#define kScaleUniformParamHint "Use same scale for both directions"
#define kSkewXParamName "Skew X"
#define kSkewYParamName "Skew Y"
#define kSkewOrderParamName "Skew order"
#define kCenterParamName "Center"
#define kInvertParamName "Invert"
#define kShowOverlayParamName "Show overlay"
#define kHostTransformParamName "Host transform"
// GENERIC
#define kFilterParamName "Filter"
#define kFilterParamHint "Filtering algorithm - some filters may produce values outside of the initial range (*) or modify the values even if there is no movement (+)."
#define kClampParamName "Clamp"
#define kClampParamHint "Clamp filter output within the original range - useful to avoid negative values in mattes"
#define kBlackOutsideParamName "Black outside"
#define kBlackOutsideParamHint "Fill the area outside the source image with black"
#define kMaskParamName "Mask"
#define kMixParamName "Mix"

enum FilterEnum {
    eImpulseFilter,
    eBilinearFilter,
    eCubicFilter,
    eKeysFilter,
    eSimonFilter,
    eRifmanFilter,
    eMitchellFilter,
    eParzenFilter,
    eNotchFilter,
};

#define kImpulseFilter "Impulse"
#define kImpulseFilterHint "(nearest neighbor / box) Use original values"
#define kBilinearFilter "Bilinear"
#define kBilinearFilterHint "(tent / triangle) Bilinear interpolation between original values"
#define kCubicFilter "Cubic"
#define kCubicFilterHint "(cubic spline) Some smoothing"
#define kKeysFilter "Keys"
#define kKeysFilterHint "(Catmull-Rom / Hermite spline) Some smoothing, plus minor sharpening (*)"
#define kSimonFilter "Simon"
#define kSimonFilterHint "Some smoothing, plus medium sharpening (*)"
#define kRifmanFilter "Rifman"
#define kRifmanFilterHint "Some smoothing, plus significant sharpening (*)"
#define kMitchellFilter "Mitchell"
#define kMitchellFilterHint "Some smoothing, plus blurring to hide pixelation (*+)"
#define kParzenFilter "Parzen"
#define kParzenFilterHint "(cubic B-spline) Greatest smoothing of all filters (+)"
#define kNotchFilter "Notch"
#define kNotchFilterHint "Flat smoothing (which tends to hide moire' patterns) (+)"

#define CIRCLE_RADIUS_BASE 30.0
#define POINT_SIZE 7.0
#define ELLIPSE_N_POINTS 50.0

using namespace OFX;

class TransformProcessorBase : public OFX::ImageProcessor
{
protected:
    OFX::Image *_srcImg;
    OFX::Image *_maskImg;
    // NON-GENERIC PARAMETERS:
    Transform2D::Matrix3x3 _invtransform;
    // GENERIC PARAMETERS:
    FilterEnum _filter;
    bool _clamp;
    bool _blackOutside;
    bool _domask;
    double _mix;
    
public:
    
    TransformProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _invtransform()
    , _filter(eImpulseFilter)
    , _clamp(false)
    , _blackOutside(false)
    , _domask(false)
    , _mix(1.0)
    {
    }
    
    /** @brief set the src image */
    void setSrcImg(OFX::Image *v)
    {
        _srcImg = v;
    }
    
    
    /** @brief set the optional mask image */
    void setMaskImg(OFX::Image *v) {_maskImg = v;}
    
    // Are we masking. We can't derive this from the mask image being set as NULL is a valid value for an input image
    void doMasking(bool v) {_domask = v;}
    
    void setValues(const Transform2D::Matrix3x3& invtransform, //!< non-generic
                   // all generic parameters below
                   double pixelaspectratio, //!< 1.067 for PAL, where 720x576 pixels occupy 768x576 in canonical coords
                   const OfxPointD& renderscale, //!< 0.5 for a half-resolution image
                   FieldEnum fieldToRender,
                   FilterEnum filter,                 //!< generic
                   bool clamp, //!< generic
                   bool blackOutside, //!< generic
                   double mix)          //!< generic
    {
        bool fielded = fieldToRender == eFieldLower || fieldToRender == eFieldUpper;
        // NON-GENERIC
        _invtransform = (Transform2D::matCanonicalToPixel(pixelaspectratio, renderscale.x, renderscale.y, fielded) *
                         invtransform *
                         Transform2D::matPixelToCanonical(pixelaspectratio, renderscale.x, renderscale.y, fielded));
        // GENERIC
        _filter = filter;
        _clamp = clamp;
        _blackOutside = blackOutside;
        _mix = mix;
    }


};

template <class T> inline T
Clamp(T v, int min, int max)
{
    if(v < T(min)) return T(min);
    if(v > T(max)) return T(max);
    return v;
}

static double sinc(double x)
{
    x = (x * M_PI);

    if ((x < 0.01f) && (x > -0.01f))
        return 1.0f + x*x*(-1.0f/6.0f + x*x*1.0f/120.0f);

    return sin(x) / x;
}

static inline double
clampcn(double I, double Ic, double In)
{
    double Imin = std::min(Ic, In);
    if (I < Imin)
        return Imin;
    double Imax = std::max(Ic, In);
    if (I > Imax)
        return Imax;
    return I;
}

static inline
double
Keys(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-Ip +In ) + d*((2*Ip -5*Ic +4*In -Ia ) + d*(-Ip +3*Ic -3*In +Ia )))/2;
    if (clamp) {
        I = clampcn(I, Ic, In);
    }
    return I;
}

static inline
double
Simon(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-3*Ip +3*In ) + d*((6*Ip -9*Ic +6*In -3*Ia ) + d*(-3*Ip +5*Ic -5*In +3*Ia )))/4;
    if (clamp) {
        I = clampcn(I, Ic, In);
    }
    return I;
}

static inline double
Rifman(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-Ip +In ) + d*((2*Ip -2*Ic +In -Ia ) + d*(-Ip +Ic -In +Ia )));
    if (clamp) {
        I = clampcn(I, Ic, In);
    }
    return I;
}

static inline double
Mitchell(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +16*Ic +In + d*((-9*Ip +9*In ) + d*((15*Ip -36*Ic +27*In -6*Ia ) + d*(-7*Ip +21*Ic -21*In +7*Ia ))))/18;
    if (clamp) {
        I = clampcn(I, Ic, In);
    }
    return I;
}

static inline double
Parzen(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +4*Ic +In + d*((-3*Ip +3*In ) + d*((3*Ip -6*Ic +3*In ) + d*(-Ip +3*Ic -3*In +Ia ))))/6;
    return I;
}

static inline double
Notch(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +2*Ic +In + d*((-2*Ip +2*In ) + d*((Ip -Ic -In +Ia ))))/4;
    return I;
}

#define APPLY4(f,j) double I ## j = f(Ip ## j, Ic ## j, In ## j, Ia ##j, dx, clamp)

#define CUBIC2D(f)                                      \
static inline                                           \
double                                                  \
f ## 2D(double Ipp, double Icp, double Inp, double Iap, \
        double Ipc, double Icc, double Inc, double Iac, \
        double Ipn, double Icn, double Inn, double Ian, \
        double Ipa, double Ica, double Ina, double Iaa, \
        double dx, double dy, bool clamp)               \
{                                                       \
    APPLY4(f,p); APPLY4(f,c); APPLY4(f,n); APPLY4(f,a); \
    return f(Ip , Ic , In , Ia , dy, clamp);            \
}

CUBIC2D(Keys);
CUBIC2D(Simon);
CUBIC2D(Rifman);
CUBIC2D(Mitchell);
CUBIC2D(Parzen);
CUBIC2D(Notch);

template <class PIX, int nComponents, int maxValue, bool masked>
class TransformProcessor : public TransformProcessorBase
{
    
    
    public :
    TransformProcessor(OFX::ImageEffect &instance)
    : TransformProcessorBase(instance)
    {
        
    }
    
    void multiThreadProcessImages(OfxRectI procWindow)
    {
        float maskScale = 1.0f;
        float tmpPix[nComponents];
        
        for (int y = procWindow.y1; y < procWindow.y2; y++)
        {
            if(_effect.abort()) break;
            
            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);
            
            // the coordinates of the center of the pixel in canonical coordinates
            // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
            Transform2D::Point3D canonicalCoords;
            canonicalCoords.z = 1;
            canonicalCoords.y = (double)y + 0.5;
            
            for (int x = procWindow.x1; x < procWindow.x2; x++, dstPix += nComponents)
            {
                // NON-GENERIC TRANSFORM
                
                // the coordinates of the center of the pixel in canonical coordinates
                // see http://openfx.sourceforge.net/Documentation/1.3/ofxProgrammingReference.html#CanonicalCoordinates
                canonicalCoords.x = (double)x + 0.5;
                Transform2D::Point3D transformed = _invtransform * canonicalCoords;
                if (!_srcImg || transformed.z == 0.) {
                    // the back-transformed point is at infinity
                    for (int c = 0; c < nComponents; ++c) {
                        tmpPix[c] = 0;
                    }
                } else {
                    double fx = transformed.x / transformed.z;
                    double fy = transformed.y / transformed.z;
                    
                    // GENERIC TRANSFORM
                    // from here on, everything is generic, and should be moved to a generic transform class
                    // Important: (0,0) is the *corner*, not the *center* of the first pixel (see OpenFX specs)
                    const OfxRectI srcBounds = _srcImg->getBounds();
                    switch (_filter) {
                        case eImpulseFilter: {
                            ///nearest neighboor
                            // the center of pixel (0,0) has coordinates (0.5,0.5)
                            int mx = std::floor(fx); // don't add 0.5
                            int my = std::floor(fy); // don't add 0.5
                            
                            if (!_blackOutside) {
                                //mx = std::max(srcBounds.x1,std::min(mx,srcBounds.x2-1));
                                //my = std::max(srcBounds.y1,std::min(my,srcBounds.y2-1));
#define CLAMPXY(m)              m ## x = std::max(srcBounds.x1,std::min(m ## x,srcBounds.x2-1)); \
                                m ## y = std::max(srcBounds.y1,std::min(m ## y,srcBounds.y2-1))
                                CLAMPXY(m);
                            }
                            PIX *srcPix = (PIX *)_srcImg->getPixelAddress(mx, my);
                            if (srcPix) {
                                for (int c = 0; c < nComponents; ++c) {
                                    tmpPix[c] = srcPix[c];
                                }
                            } else {
                                for (int c = 0; c < nComponents; ++c) {
                                    tmpPix[c] = 0;
                                }
                            }
                        }
                            break;
                        case eBilinearFilter:
                        case eCubicFilter: {
                            // bilinear or cubic
                            // the center of pixel (0,0) has coordinates (0.5,0.5)
                            int cx = std::floor(fx-0.5);
                            int cy = std::floor(fy-0.5);
                            int nx = cx + 1;
                            int ny = cy + 1;
                            if (!_blackOutside) {
                                CLAMPXY(c);
                                CLAMPXY(n);
                            }

                            const double dx = std::max(0., std::min(fx-0.5 - cx, 1.));
                            const double dy = std::max(0., std::min(fy-0.5 - cy, 1.));
                        
                            //PIX *Pcc = (PIX *)_srcImg->getPixelAddress(cx, cy);, etc.
#define GETPIX(i,j)         PIX *P ## i ## j = (PIX *)_srcImg->getPixelAddress(i ## x, j ## y)
                            GETPIX(c,c); GETPIX(n,c); GETPIX(c,n); GETPIX(n,n);
                            if (Pcc || Pnc || Pcn || Pnn) {
                                for (int c = 0; c < nComponents; ++c) {
                                    //const double Icc = get(Pcc,c);, etc.
#define GETI(i,j)                   const double I ## i ## j = get(P ## i ## j,c)
                                    GETI(c,c); GETI(n,c); GETI(c,n); GETI(n,n);
                                    if (_filter == eBilinearFilter) {
                                        tmpPix[c] = Icc + dx*(Inc-Icc + dy*(Icc+Inn-Icn-Inc)) + dy*(Icn-Icc);
                                    } else {
                                        assert(_filter == eCubicFilter);
                                        double Ic = ((2*Icc-2*Inc)*dx + (-3*Icc+3*Inc))*dx*dx + Icc;
                                        double In = ((2*Icn-2*Inn)*dx + (-3*Icn+3*Inn))*dx*dx + Icn;
                                        tmpPix[c] = ((2*Ic -2*In )*dy + (-3*Ic +3*In ))*dy*dy + Ic;
                                    }
                                }
                            } else {
                                for (int c = 0; c < nComponents; ++c) {
                                    tmpPix[c] = 0;
                                }
                            }
                        }
                            break;

                            // (B,C) cubic filters
                        case eKeysFilter:
                        case eSimonFilter:
                        case eRifmanFilter:
                        case eMitchellFilter:
                        case eParzenFilter:
                        case eNotchFilter: {
                            // the center of pixel (0,0) has coordinates (0.5,0.5)
                            int cx = std::floor(fx-0.5);
                            int cy = std::floor(fy-0.5);
                            int px = cx - 1;
                            int py = cy - 1;
                            int nx = cx + 1;
                            int ny = cy + 1;
                            int ax = cx + 2;
                            int ay = cy + 2;
                            if (!_blackOutside) {
                                CLAMPXY(c);
                                CLAMPXY(p);
                                CLAMPXY(n);
                                CLAMPXY(a);
                            }
                            const double dx = std::max(0., std::min(fx-0.5 - cx, 1.));
                            const double dy = std::max(0., std::min(fy-0.5 - cy, 1.));
                        
#define GETPIX4(i)          GETPIX(i,p); GETPIX(i,c); GETPIX(i,n); GETPIX(i,a);
                            GETPIX4(p); GETPIX4(c); GETPIX4(n); GETPIX4(a);
                            if (Ppp || Pcp || Pnp || Pap || Ppc || Pcc || Pnc || Pac || Ppn || Pcn || Pnn || Pan || Ppa || Pca || Pna || Paa) {
                                for (int c = 0; c < nComponents; ++c) {
                                    //double Ipp = get(Ppp,c);, etc.
#define GETI4(i)                    GETI(i,p); GETI(i,c); GETI(i,n); GETI(i,a);
                                    GETI4(p); GETI4(c); GETI4(n); GETI4(a);
                                    double I = 0.;
#define I44                         Ipp, Icp, Inp, Iap, \
                                    Ipc, Icc, Inc, Iac, \
                                    Ipn, Icn, Inn, Ian, \
                                    Ipa, Ica, Ina, Iaa
                                    switch (_filter) {
                                        case eKeysFilter:
                                            I = Keys2D(I44, dx, dy, _clamp);
                                            break;
                                        case eSimonFilter:
                                            I = Simon2D(I44, dx, dy, _clamp);
                                            break;
                                        case eRifmanFilter:
                                            I = Rifman2D(I44, dx, dy, _clamp);
                                            break;
                                        case eMitchellFilter:
                                            I = Mitchell2D(I44, dx, dy, _clamp);
                                            break;
                                        case eParzenFilter:
                                            I = Parzen2D(I44, dx, dy, false);
                                            break;
                                        case eNotchFilter:
                                            I = Notch2D(I44, dx, dy, false);
                                            break;
                                        default:
                                            assert(0);
                                    }
                                    tmpPix[c] = I;
                                }
                            } else {
                                for (int c = 0; c < nComponents; ++c) {
                                    tmpPix[c] = 0;
                                }
                            }
                        }
                            break;

                        //case eLanczos4Filter:
                        //case eSinc4Filter:
                        //    break;

                        //case eLanczos6Filter:
                        //    break;

                        default:
                            assert(0);
                            break;
                    }
                }
                
                PIX *maskPix = NULL;
                PIX *srcPix = NULL;
                
                // are we doing masking
                if (masked) {
                    if (_domask && _maskImg) {
                        // we do, get the pixel from the mask
                        maskPix = (PIX *)_maskImg->getPixelAddress(x, y);
                        // figure the scale factor from that pixel
                        maskScale = maskPix != 0 ? float(*maskPix)/float(maxValue) : 0.0f;
                    }
                    if ((_domask && _maskImg) || _mix != 1.) {
                        srcPix = (PIX *)_srcImg->getPixelAddress(x, y);
                    }
                }
                if (masked && srcPix) {
                    float alpha = maskScale * _mix;
                    for (int c = 0; c < nComponents; ++c) {
                        float v = tmpPix[c] * alpha + (1. - alpha) * srcPix[c];
                        if (maxValue == 1) { // implies floating point and so no clamping
                            dstPix[c] = PIX(v);
                        } else { // integer based and we need to clamp
                            // (e.g. bicubic filter may overflow)
                            dstPix[c] = PIX(Clamp(v, 0, maxValue));
                        }
                    }
                } else {
                    // no mask, no mix
                    for (int c = 0; c < nComponents; ++c) {
                        if (maxValue == 1) { // implies floating point and so no clamping
                            dstPix[c] = tmpPix[c];
                        } else { // integer based and we need to clamp
                            // (e.g. bicubic filter may overflow)
                            dstPix[c] = PIX(Clamp(tmpPix[c], 0, maxValue));
                        }
                    }
                }
            }
        }
    }
private:
    PIX get(const PIX* p, int c)
    {
        return p ? p[c] : PIX();
    }
};





////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class TransformPlugin : public OFX::ImageEffect
{
protected:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *dstClip_;
    OFX::Clip *srcClip_;
    OFX::Clip *maskClip_;
    
public:
    /** @brief ctor */
    TransformPlugin(OfxImageEffectHandle handle, bool masked)
    : ImageEffect(handle)
    , dstClip_(0)
    , srcClip_(0)
    , maskClip_(0)
    , _translate(0)
    , _rotate(0)
    , _scale(0)
    , _scaleUniform(0)
    , _skewX(0)
    , _skewY(0)
    , _skewOrder(0)
    , _center(0)
    , _invert(0)
    , _filter(0)
    , _clamp(0)
    , _blackOutside(0)
    , _masked(masked)
    , _domask(0)
    , _mix(0)
    {
        dstClip_ = fetchClip(kOfxImageEffectOutputClipName);
        srcClip_ = fetchClip(kOfxImageEffectSimpleSourceClipName);
        // name of mask clip depends on the context
        if (masked) {
            maskClip_ = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        }
        // NON-GENERIC
        _translate = fetchDouble2DParam(kTranslateParamName);
        _rotate = fetchDoubleParam(kRotateParamName);
        _scale = fetchDouble2DParam(kScaleParamName);
        _scaleUniform = fetchBooleanParam(kScaleUniformParamName);
        _skewX = fetchDoubleParam(kSkewXParamName);
        _skewY = fetchDoubleParam(kSkewYParamName);
        _skewOrder = fetchChoiceParam(kSkewOrderParamName);
        _center = fetchDouble2DParam(kCenterParamName);
        _invert = fetchBooleanParam(kInvertParamName);
        // GENERIC
        _filter = fetchChoiceParam(kFilterParamName);
        _clamp = fetchBooleanParam(kClampParamName);
        _blackOutside = fetchBooleanParam(kBlackOutsideParamName);
        if (masked) {
            _domask = fetchBooleanParam(kMaskParamName);
            _mix = fetchDoubleParam(kMixParamName);
        }
    }
    
    // override the rod call
    virtual bool getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod);
    
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois);
    
    /* Override the render */
    virtual void render(const OFX::RenderArguments &args);
    
    virtual bool isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime);

#ifdef OFX_EXTENSIONS_NUKE
    /** @brief recover a transform matrix from an effect */
    virtual bool getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9]);
#endif

    /* internal render function */
    template <int nComponents, bool masked>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);

    /* set up and run a processor */
    void setupAndProcess(TransformProcessorBase &, const OFX::RenderArguments &args);
    
private:
    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::DoubleParam* _skewX;
    OFX::DoubleParam* _skewY;
    OFX::ChoiceParam* _skewOrder;
    OFX::Double2DParam* _center;
    OFX::BooleanParam* _invert;
    // GENERIC
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _clamp;
    OFX::BooleanParam* _blackOutside;
    bool _masked;
    OFX::BooleanParam* _domask;
    OFX::DoubleParam* _mix;
};



////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
TransformPlugin::setupAndProcess(TransformProcessorBase &processor, const OFX::RenderArguments &args)
{
    std::auto_ptr<OFX::Image> dst(dstClip_->fetchImage(args.time));
    std::auto_ptr<OFX::Image> src(srcClip_->fetchImage(args.time));
    if (src.get() && dst.get())
    {
        OFX::BitDepthEnum dstBitDepth       = dst->getPixelDepth();
        OFX::PixelComponentEnum dstComponents  = dst->getPixelComponents();
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents)
            OFX::throwSuiteStatusException(kOfxStatFailed);
        
        // NON-GENERIC
        double scaleX, scaleY;
        bool scaleUniform;
        double translateX, translateY;
        double rotate;
        double skewX, skewY;
        int skewOrder;
        double centerX, centerY;
        bool invert;
        // GENERIC
        int filter;
        bool clamp;
        bool blackOutside;
        double mix = 1.;
        
        // NON-GENERIC
        _scale->getValueAtTime(args.time, scaleX, scaleY);
        _scaleUniform->getValue(scaleUniform);
        if (scaleUniform) {
            scaleY = scaleX;
        }
        _translate->getValueAtTime(args.time, translateX, translateY);
        _rotate->getValueAtTime(args.time, rotate);
        rotate = Transform2D::toRadians(rotate);
        
        _skewX->getValueAtTime(args.time, skewX);
        _skewY->getValueAtTime(args.time, skewY);
        _skewOrder->getValueAtTime(args.time, skewOrder);
        
        _center->getValueAtTime(args.time, centerX, centerY);
        
        _invert->getValue(invert);
        
        // GENERIC
        _filter->getValue(filter);
        _clamp->getValue(clamp);
        _blackOutside->getValue(blackOutside);
        if (_masked) {
            _mix->getValueAtTime(args.time, mix);
        }

        Transform2D::Matrix3x3 invtransform;
        if (invert) {
            invtransform = Transform2D::matTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
        } else {
            invtransform = Transform2D::matInverseTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
        }

        processor.setValues(invtransform,
                            srcClip_->getPixelAspectRatio(),
                            args.renderScale, //!< 0.5 for a half-resolution image
                            args.fieldToRender,
                            (FilterEnum)filter, clamp, blackOutside, mix);
    }
    
    // auto ptr for the mask.
    std::auto_ptr<OFX::Image> mask((_masked && (getContext() != OFX::eContextFilter)) ? maskClip_->fetchImage(args.time) : 0);
    
    // do we do masking
    if (_masked && getContext() != OFX::eContextFilter) {
        bool doMasking;
        _domask->getValue(doMasking);
        if (doMasking) {
            // say we are masking
            processor.doMasking(true);
            
            // Set it in the processor
            processor.setMaskImg(mask.get());
        }
    }
    
    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImg(src.get());
    
    // set the render window
    processor.setRenderWindow(args.renderWindow);
    
    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

// override the rod call
// NON-GENERIC
// the RoD should at least contain the region of definition of the source clip,
// which will be filled with black or by continuity.
bool
TransformPlugin::getRegionOfDefinition(const RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    OfxRectD srcRoD = srcClip_->getRegionOfDefinition(args.time);
    double scaleX, scaleY;
    double translateX, translateY;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    double centerX, centerY;
    bool invert;
    
    // NON-GENERIC
    _scale->getValueAtTime(args.time, scaleX, scaleY);
    bool scaleUniform;
    _scaleUniform->getValue(scaleUniform);
    if (scaleUniform) {
        scaleY = scaleX;
    }
    _translate->getValueAtTime(args.time, translateX, translateY);
    _rotate->getValueAtTime(args.time, rotate);
    rotate = Transform2D::toRadians(rotate);
    
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrder);
    
    _center->getValueAtTime(args.time, centerX, centerY);
    
    _invert->getValue(invert);
    
    
    Transform2D::Matrix3x3 transform;
    if (!invert) {
        transform = Transform2D::matTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    } else {
        transform = Transform2D::matInverseTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    }
    /// now transform the 4 corners of the source clip to the output image
    Transform2D::Point3D topLeft = transform * Transform2D::Point3D(srcRoD.x1,srcRoD.y2,1);
    Transform2D::Point3D topRight = transform * Transform2D::Point3D(srcRoD.x2,srcRoD.y2,1);
    Transform2D::Point3D bottomLeft = transform * Transform2D::Point3D(srcRoD.x1,srcRoD.y1,1);
    Transform2D::Point3D bottomRight = transform * Transform2D::Point3D(srcRoD.x2,srcRoD.y1,1);
    
    double l = std::min(std::min(topLeft.x, bottomLeft.x),std::min(topRight.x,bottomRight.x));
    double b = std::min(std::min(topLeft.y, bottomLeft.y),std::min(topRight.y,bottomRight.y));
    double r = std::max(std::max(topLeft.x, bottomLeft.x),std::max(topRight.x,bottomRight.x));
    double t = std::max(std::max(topLeft.y, bottomLeft.y),std::max(topRight.y,bottomRight.y));
    
    // GENERIC
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    
    // No need to round things up here, we must give the *actual* RoD
    if (!blackOutside) {
        // if it's not black outside, the RoD should contain the project (we can't rely on the host to fill it).
        OfxPointD size = getProjectSize();
        OfxPointD offset = getProjectOffset();
        
        rod.x1 = std::min(l, offset.x);
        rod.x2 = std::max(r, offset.x + size.x);
        rod.y1 = std::min(b, offset.y);
        rod.y2 = std::max(t, offset.y + size.y);
    } else {
        // expand the RoD to get at least one black pixel
        rod.x1 = l - 1.;
        rod.x2 = r + 1.;
        rod.y1 = b - 1.;
        rod.y2 = t + 1.;
    }
    assert(rod.x1 < rod.x2 && rod.y1 < rod.y2);
    // say we set it
    return true;
}


// override the roi call
// NON-GENERIC
// Required if the plugin should support tiles.
// It may be difficult to implement for complicated transforms:
// consequently, these transforms cannot support tiles.
void
TransformPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois)
{
    const OfxRectD roi = args.regionOfInterest;
    double scaleX, scaleY;
    double translateX, translateY;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    double centerX, centerY;
    bool invert;
    
    // NON-GENERIC
    _scale->getValueAtTime(args.time, scaleX, scaleY);
    bool scaleUniform;
    _scaleUniform->getValue(scaleUniform);
    if (scaleUniform) {
        scaleY = scaleX;
    }
    _translate->getValueAtTime(args.time, translateX, translateY);
    _rotate->getValueAtTime(args.time, rotate);
    rotate = Transform2D::toRadians(rotate);
    
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrder);
    
    _center->getValueAtTime(args.time, centerX, centerY);
    
    _invert->getValue(invert);
    
    Transform2D::Matrix3x3 invtransform;
    if (!invert) {
        invtransform = Transform2D::matInverseTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    } else {
        invtransform = Transform2D::matTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    }
    /// now find the positions in the src clip of the 4 corners of the roi
    Transform2D::Point3D topLeft = invtransform * Transform2D::Point3D(roi.x1,roi.y2,1);
    Transform2D::Point3D topRight = invtransform * Transform2D::Point3D(roi.x2,roi.y2,1);
    Transform2D::Point3D bottomLeft = invtransform * Transform2D::Point3D(roi.x1,roi.y1,1);
    Transform2D::Point3D bottomRight = invtransform * Transform2D::Point3D(roi.x2,roi.y1,1);
    
    double l = std::min(std::min(topLeft.x, bottomLeft.x),std::min(topRight.x,bottomRight.x));
    double b = std::min(std::min(topLeft.y, bottomLeft.y),std::min(topRight.y,bottomRight.y));
    double r = std::max(std::max(topLeft.x, bottomLeft.x),std::max(topRight.x,bottomRight.x));
    double t = std::max(std::max(topLeft.y, bottomLeft.y),std::max(topRight.y,bottomRight.y));
    
    // GENERIC
    int filter;
    _filter->getValue(filter);
    bool blackOutside;
    _blackOutside->getValue(blackOutside);
    bool doMasking = false;
    double mix = 1.;
    if (_masked) {
        _domask->getValue(doMasking);
        _mix->getValueAtTime(args.time, mix);
    }

    double pixelSizeX = srcClip_->getPixelAspectRatio() / args.renderScale.x;
    double pixelSizeY = 1. / args.renderScale.x;
    switch (filter) {
        case eImpulseFilter:
            // nearest neighbor, the exact region is OK
            break;
        case eBilinearFilter:
        case eCubicFilter:
            // bilinear or cubic, expand by 0.5 pixels
            l -= 0.5*pixelSizeX;
            r += 0.5*pixelSizeX;
            b -= 0.5*pixelSizeY;
            t += 0.5*pixelSizeY;
            break;
        case eKeysFilter:
        case eSimonFilter:
        case eRifmanFilter:
        case eMitchellFilter:
        case eParzenFilter:
        case eNotchFilter:
            // bicubic, expand by 1.5 pixels
            l -= 1.5*pixelSizeX;
            r += 1.5*pixelSizeX;
            b -= 1.5*pixelSizeY;
            t += 1.5*pixelSizeY;
            break;
    }
    
    if (_masked) {
        // set it on the mask only if we are in an interesting context
        // (i.e. eContextGeneral or eContextPaint, see Support/Plugins/Basic)
        if (getContext() != OFX::eContextFilter && doMasking) {
            rois.setRegionOfInterest(*maskClip_, roi);
        }
        if ((getContext() != OFX::eContextFilter && doMasking) || mix != 1.) {
            // for masking, we also need the source image for that same roi.
            // compute the union of both ROIs
            l = std::min(l, roi.x1);
            r = std::max(r, roi.x2);
            b = std::min(b, roi.y1);
            t = std::max(t, roi.y2);
        }
    }
    assert(l < r && b < t);
    // No need to round things up here, we must give the *actual* RoI,
    // the host should compute the right image region from it (by rounding it up/down).
    OfxRectD srcRoI;
    srcRoI.x1 = l;
    srcRoI.x2 = r;
    srcRoI.y1 = b;
    srcRoI.y2 = t;
    
    rois.setRegionOfInterest(*srcClip_, srcRoI);
}


// the internal render function
template <int nComponents, bool masked>
void
TransformPlugin::renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth)
{
    switch(dstBitDepth)
    {
        case OFX::eBitDepthUByte :
        {
            TransformProcessor<unsigned char, 4, 255, masked> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthUShort :
        {
            TransformProcessor<unsigned short, 4, 65535, masked> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case OFX::eBitDepthFloat :
        {
            TransformProcessor<float, 4, 1, masked> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        default :
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
TransformPlugin::render(const OFX::RenderArguments &args)
{
    
    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = dstClip_->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = dstClip_->getPixelComponents();

    assert(dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        if (_masked) {
            renderInternal<4,true>(args, dstBitDepth);
        } else {
            renderInternal<4,false>(args, dstBitDepth);
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        if (_masked) {
            renderInternal<3,true>(args, dstBitDepth);
        } else {
            renderInternal<3,false>(args, dstBitDepth);
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        if (_masked) {
            renderInternal<3,true>(args, dstBitDepth);
        } else {
            renderInternal<3,false>(args, dstBitDepth);
        }
    }
}

// overridden is identity
bool TransformPlugin::isIdentity(const RenderArguments &args, Clip * &identityClip, double &identityTime)
{
    // NON-GENERIC
    OfxPointD scale;
    OfxPointD translate;
    double rotate;
    double skewX, skewY;
    _scale->getValueAtTime(args.time, scale.x, scale.y);
    bool scaleUniform;
    _scaleUniform->getValue(scaleUniform);
    if (scaleUniform) {
        scale.y = scale.x;
    }
    _translate->getValueAtTime(args.time, translate.x, translate.y);
    _rotate->getValueAtTime(args.time, rotate);
    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    
    if (scale.x == 1. && scale.y == 1. && translate.x == 0. && translate.y == 0. && rotate == 0. && skewX == 0. && skewY == 0.) {
        identityClip = srcClip_;
        identityTime = args.time;
        return true;
    }
    
    // GENERIC
    if (_masked) {
        bool doMasking;
        _domask->getValue(doMasking);
        double mix;
        _mix->getValueAtTime(args.time, mix);
        if (doMasking && mix == 0.) {
            identityClip = srcClip_;
            identityTime = args.time;
            return true;
        }
    }
    
    return false;
}

#ifdef OFX_EXTENSIONS_NUKE
// overridden getTransform
bool TransformPlugin::getTransform(const TransformArguments &args, Clip * &transformClip, double transformMatrix[9])
{
    double scaleX, scaleY;
    double translateX, translateY;
    double rotate;
    double skewX, skewY;
    int skewOrder;
    double centerX, centerY;
    bool invert;

    std::cout << "getTransform called!" << std::endl;
    // NON-GENERIC
    _scale->getValueAtTime(args.time, scaleX, scaleY);
    bool scaleUniform;
    _scaleUniform->getValue(scaleUniform);
    if (scaleUniform) {
        scaleY = scaleX;
    }
    _translate->getValueAtTime(args.time, translateX, translateY);
    _rotate->getValueAtTime(args.time, rotate);
    rotate = Transform2D::toRadians(rotate);

    _skewX->getValueAtTime(args.time, skewX);
    _skewY->getValueAtTime(args.time, skewY);
    _skewOrder->getValueAtTime(args.time, skewOrder);

    _center->getValueAtTime(args.time, centerX, centerY);

    _invert->getValueAtTime(args.time, invert);

    Transform2D::Matrix3x3 invtransform;
    if (!invert) {
        invtransform = Transform2D::matInverseTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    } else {
        invtransform = Transform2D::matTransformCanonical(translateX, translateY, scaleX, scaleY, skewX, skewY, (bool)skewOrder, rotate, centerX, centerY);
    }
    double pixelaspectratio = srcClip_->getPixelAspectRatio();
    bool fielded = args.fieldToRender == eFieldLower || args.fieldToRender == eFieldUpper;
    Transform2D::Matrix3x3 invtransformpixel = (Transform2D::matCanonicalToPixel(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded) *
                                                invtransform *
                                                Transform2D::matPixelToCanonical(pixelaspectratio, args.renderScale.x, args.renderScale.y, fielded));
    transformClip = srcClip_;
    transformMatrix[0] = invtransformpixel.a;
    transformMatrix[1] = invtransformpixel.b;
    transformMatrix[2] = invtransformpixel.c;
    transformMatrix[3] = invtransformpixel.d;
    transformMatrix[4] = invtransformpixel.e;
    transformMatrix[5] = invtransformpixel.f;
    transformMatrix[6] = invtransformpixel.g;
    transformMatrix[7] = invtransformpixel.h;
    transformMatrix[8] = invtransformpixel.i;
    return true;
}
#endif

////////////////////////////////////////////////////////////////////////////////
// stuff for the interact

class TransformInteract : public OFX::OverlayInteract {
    protected :
    enum DrawStateEnum {
        eInActive = 0, //< nothing happening
        eCircleHovered, //< the scale circle is hovered
        eLeftPointHovered, //< the left point of the circle is hovered
        eRightPointHovered, //< the right point of the circle is hovered
        eBottomPointHovered, //< the bottom point of the circle is hovered
        eTopPointHovered, //< the top point of the circle is hovered
        eCenterPointHovered, //< the center point of the circle is hovered
        eRotationBarHovered, //< the rotation bar is hovered
        eSkewXBarHoverered, //< the skew bar is hovered
        eSkewYBarHoverered //< the skew bar is hovered
    };
    
    enum MouseStateEnum {
        eReleased = 0,
        eDraggingCircle,
        eDraggingLeftPoint,
        eDraggingRightPoint,
        eDraggingTopPoint,
        eDraggingBottomPoint,
        eDraggingCenterPoint,
        eDraggingRotationBar,
        eDraggingSkewXBar,
        eDraggingSkewYBar
    };
    
    DrawStateEnum _drawState;
    MouseStateEnum _mouseState;
    TransformPlugin* _plugin;
    OfxPointD _lastMousePos;
    
    public :
    TransformInteract(OfxInteractHandle handle, OFX::ImageEffect* effect)
    : OFX::OverlayInteract(handle)
    , _drawState(eInActive)
    , _mouseState(eReleased)
    , _plugin(dynamic_cast<TransformPlugin*>(effect))
    , _lastMousePos()
    {
        
        assert(_plugin);
        _lastMousePos.x = _lastMousePos.y = 0.;
        // NON-GENERIC
        _translate = _plugin->fetchDouble2DParam(kTranslateParamName);
        _rotate = _plugin->fetchDoubleParam(kRotateParamName);
        _scale = _plugin->fetchDouble2DParam(kScaleParamName);
        _scaleUniform = _plugin->fetchBooleanParam(kScaleUniformParamName);
        _skewX = _plugin->fetchDoubleParam(kSkewXParamName);
        _skewY = _plugin->fetchDoubleParam(kSkewYParamName);
        _skewOrder = _plugin->fetchChoiceParam(kSkewOrderParamName);
        _center = _plugin->fetchDouble2DParam(kCenterParamName);
        _invert = _plugin->fetchBooleanParam(kInvertParamName);
        _showOverlay = _plugin->fetchBooleanParam(kShowOverlayParamName);
        addParamToSlaveTo(_translate);
        addParamToSlaveTo(_rotate);
        addParamToSlaveTo(_scale);
        addParamToSlaveTo(_skewX);
        addParamToSlaveTo(_skewY);
        addParamToSlaveTo(_skewOrder);
        addParamToSlaveTo(_center);
        addParamToSlaveTo(_invert);
        addParamToSlaveTo(_showOverlay);
    }
    
    // overridden functions from OFX::Interact to do things
    virtual bool draw(const OFX::DrawArgs &args);
    virtual bool penMotion(const OFX::PenArgs &args);
    virtual bool penDown(const OFX::PenArgs &args);
    virtual bool penUp(const OFX::PenArgs &args);
    
private:
    
    bool isOverlayDisplayed() const {
        bool disp;
        _showOverlay->getValue(disp);
        return disp;
    }
    
    void getCenter(OfxPointD *center)
    {
        OfxPointD translate;
        _center->getValue(center->x, center->y);
        _translate->getValue(translate.x, translate.y);
        center->x += translate.x;
        center->y += translate.y;
    }
    
    void getScale(OfxPointD* scale)
    {
        _scale->getValue(scale->x, scale->y);
        bool scaleUniform;
        _scaleUniform->getValue(scaleUniform);
        if (scaleUniform) {
            scale->y = scale->x;
        }
    }
    
    void getCircleRadius(const OfxPointD& pixelScale, OfxPointD* radius)
    {
        OfxPointD scale;
        getScale(&scale);
        radius->x = scale.x * CIRCLE_RADIUS_BASE;
        radius->y = scale.y * CIRCLE_RADIUS_BASE;
        // don't draw too small. 15 pixels is the limit
        if (std::fabs(radius->x) < 15) {
            radius->y *= std::fabs(15./radius->x);
            radius->x = radius->x > 0 ? 15. : -15.;
        }
        if (std::fabs(radius->y) < 15) {
            radius->x *= std::fabs(15./radius->y);
            radius->y = radius->y > 0 ? 15. : -15.;
        }
        // the circle axes are not aligned with the images axes, so we cannot use the x and y scales separately
        double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
        radius->x *= meanPixelScale;
        radius->y *= meanPixelScale;
    }
    
    void getPoints(const OfxPointD& pixelScale,
                   OfxPointD *center,
                   OfxPointD *left,
                   OfxPointD *bottom,
                   OfxPointD *top,
                   OfxPointD *right)
    {
        getCenter(center);
        OfxPointD radius;
        getCircleRadius(pixelScale, &radius);
        left->x = center->x - radius.x ;
        left->y = center->y;
        right->x = center->x + radius.x ;
        right->y = center->y;
        top->x = center->x;
        top->y = center->y + radius.y ;
        bottom->x = center->x;
        bottom->y = center->y - radius.y ;
    }
    

    OfxRangeD getViewportSize()
    {
        OfxRangeD ret;
        ret.min = getProperties().propGetDouble(kOfxInteractPropViewportSize, 0);
        ret.max = getProperties().propGetDouble(kOfxInteractPropViewportSize, 1);
        return ret;
    }
private:
    // NON-GENERIC
    OFX::Double2DParam* _translate;
    OFX::DoubleParam* _rotate;
    OFX::Double2DParam* _scale;
    OFX::BooleanParam* _scaleUniform;
    OFX::DoubleParam* _skewX;
    OFX::DoubleParam* _skewY;
    OFX::ChoiceParam* _skewOrder;
    OFX::Double2DParam* _center;
    OFX::BooleanParam* _invert;
    OFX::BooleanParam* _showOverlay;
};

static void
drawSquare(const OfxPointD& center,
                              const OfxPointD& pixelScale,
                              bool hovered)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    double halfWidth = (POINT_SIZE / 2.) * meanPixelScale;
    double halfHeight = (POINT_SIZE / 2.) * meanPixelScale;
    glPushMatrix();
    glTranslated(center.x, center.y, 0.);
    glBegin(GL_POLYGON);
    glVertex2d(- halfWidth, - halfHeight); // bottom left
    glVertex2d(- halfWidth, + halfHeight); // top left
    glVertex2d(+ halfWidth, + halfHeight); // bottom right
    glVertex2d(+ halfWidth, - halfHeight); // top right
    glEnd();
    glPopMatrix();
    
}

static void
drawEllipse(const OfxPointD& center,
                               const OfxPointD& radius,
                               const OfxPointD& pixelScale,
                               bool hovered)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    glPushMatrix ();
    //  center the oval at x_center, y_center
    glTranslatef (center.x, center.y, 0);
    //  draw the oval using line segments
    glBegin (GL_LINE_LOOP);
    // we don't need to be pixel-perfect here, it's just an interact!
    // 40 segments is enough.
    for (int i = 0; i < 40; ++i) {
        double theta = i * 2 * Transform2D::pi() / 40.;
        glVertex2f (radius.x * std::cos(theta), radius.y * std::sin(theta));
    }
    glEnd ();
    
    glPopMatrix ();
}

static void
drawSkewBar(const OfxPointD &center,
                               const OfxPointD& pixelScale,
                               double radiusY,
                               bool hovered,
                               double angle)
{
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    // we are not axis-aligned: use the mean pixel scale
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusY + 20. * meanPixelScale;

    glPushMatrix ();
    glTranslatef (center.x, center.y, 0);
    glRotated(angle, 0, 0, 1);
    
    glBegin(GL_LINES);
    glVertex2d(0., - barHalfSize);
    glVertex2d(0., + barHalfSize);
    
    if (hovered) {
        double arrowYPosition = radiusY + 10. * meanPixelScale;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        ///draw the central bar
        glVertex2d(- arrowXHalfSize, - arrowYPosition);
        glVertex2d(+ arrowXHalfSize, - arrowYPosition);
        
        ///left triangle
        glVertex2d(- arrowXHalfSize, -  arrowYPosition);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, - arrowYPosition + arrowHeadOffsetY);
        
        glVertex2d(- arrowXHalfSize,- arrowYPosition);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, - arrowYPosition - arrowHeadOffsetY);
        
        ///right triangle
        glVertex2d(+ arrowXHalfSize,- arrowYPosition);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, - arrowYPosition + arrowHeadOffsetY);
        
        glVertex2d(+ arrowXHalfSize,- arrowYPosition);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, - arrowYPosition - arrowHeadOffsetY);
    }
    glEnd();
    glPopMatrix();
}


static void
drawRotationBar(const OfxPointD& pixelScale,
                double radiusX,
                bool hovered,
                bool inverted)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    if (hovered) {
        glColor4f(1.0, 0.0, 0.0, 1.0);
    } else {
        glColor4f(1.0, 1.0, 1.0, 1.0);
    }
    
    double barExtra = 30. * meanPixelScale;
    glBegin(GL_LINES);
    glVertex2d(0., 0.);
    glVertex2d(0. + radiusX + barExtra, 0.);
    glEnd();
    
    if (hovered) {
        double arrowCenterX = radiusX + barExtra / 2.;
        
        ///draw an arrow slightly bended. This is an arc of circle of radius 5 in X, and 10 in Y.
        OfxPointD arrowRadius;
        arrowRadius.x = 5. * meanPixelScale;
        arrowRadius.y = 10. * meanPixelScale;
        
        glPushMatrix ();
        //  center the oval at x_center, y_center
        glTranslatef (arrowCenterX, 0., 0);
        //  draw the oval using line segments
        glBegin (GL_LINE_STRIP);
        glVertex2f (0, arrowRadius.y);
        glVertex2f (arrowRadius.x, 0.);
        glVertex2f (0, -arrowRadius.y);
        glEnd ();
        

        glBegin(GL_LINES);
        ///draw the top head
        glVertex2f(0., arrowRadius.y);
        glVertex2f(0., arrowRadius.y - 5. * meanPixelScale);
        
        glVertex2f(0., arrowRadius.y);
        glVertex2f(4. * meanPixelScale, arrowRadius.y - 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        ///draw the bottom head
        glVertex2f(0., -arrowRadius.y);
        glVertex2f(0., -arrowRadius.y + 5. * meanPixelScale);
        
        glVertex2f(0., -arrowRadius.y);
        glVertex2f(4. * meanPixelScale, -arrowRadius.y + 3. * meanPixelScale); // 5^2 = 3^2+4^2
        
        glEnd();

        glPopMatrix ();
    }
    if (inverted) {
        double arrowXPosition = radiusX + barExtra * 1.5;
        double arrowXHalfSize = 10 * meanPixelScale;
        double arrowHeadOffsetX = 3 * meanPixelScale;
        double arrowHeadOffsetY = 3 * meanPixelScale;

        glPushMatrix ();
        glTranslatef (arrowXPosition, 0., 0);

        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);

        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();

        glRotated(90., 0., 0., 1.);

        glBegin(GL_LINES);
        ///draw the central bar
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize, 0.);

        ///left triangle
        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(- arrowXHalfSize, 0.);
        glVertex2d(- arrowXHalfSize + arrowHeadOffsetX, -arrowHeadOffsetY);

        ///right triangle
        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, arrowHeadOffsetY);

        glVertex2d(+ arrowXHalfSize, 0.);
        glVertex2d(+ arrowXHalfSize - arrowHeadOffsetX, -arrowHeadOffsetY);
        glEnd();
        
        glPopMatrix ();
    }

}

// draw the interact
bool
TransformInteract::draw(const OFX::DrawArgs &args)
{
    
    if (!isOverlayDisplayed()) {
        return false;
    }
    //std::cout << "pixelScale= "<<args.pixelScale.x << "," << args.pixelScale.y << " renderscale=" << args.renderScale.x << "," << args.renderScale.y << std::endl;
    OfxPointD center, left, right, bottom, top;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(pscale, &center, &left, &bottom, &top, &right);

    double angle;
    _rotate->getValue(angle);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValue(skewX);
    _skewY->getValue(skewY);
    _skewOrder->getValue(skewOrderYX);

    bool inverted;
    _invert->getValue(inverted);

    OfxPointD radius;
    getCircleRadius(pscale, &radius);

    glPushMatrix();
    glTranslated(center.x, center.y, 0.);

    GLdouble skewMatrix[16];
    skewMatrix[0] = (skewOrderYX ? 1. : (1.+skewX*skewY)); skewMatrix[1] = skewY; skewMatrix[2] = 0.; skewMatrix[3] = 0;
    skewMatrix[4] = skewX; skewMatrix[5] = (skewOrderYX ? (1.+skewX*skewY) : 1.); skewMatrix[6] = 0.; skewMatrix[7] = 0;
    skewMatrix[8] = 0.; skewMatrix[9] = 0.; skewMatrix[10] = 1.; skewMatrix[11] = 0;
    skewMatrix[12] = 0.; skewMatrix[13] = 0.; skewMatrix[14] = 0.; skewMatrix[15] = 1.;

    glRotated(angle, 0, 0., 1.);
    drawRotationBar(pscale, radius.x, _mouseState == eDraggingRotationBar || _drawState == eRotationBarHovered, inverted);
    glMultMatrixd(skewMatrix);
    glTranslated(-center.x, -center.y, 0.);
    
    drawEllipse(center, radius, pscale, _mouseState == eDraggingCircle || _drawState == eCircleHovered);
    
    // add 180 to the angle to draw the arrows on the other side. unfortunately, this requires knowing
    // the mouse position in the ellipse frame
    double flip = 0.;
    if (_drawState == eSkewXBarHoverered || _drawState == eSkewYBarHoverered) {
        OfxPointD scale;
        _scale->getValue(scale.x, scale.y);
        bool scaleUniform;
        _scaleUniform->getValue(scaleUniform);
        if (scaleUniform) {
            scale.y = scale.x;
        }
        double rot = Transform2D::toRadians(angle);
        Transform2D::Matrix3x3 transformscale;
        transformscale = Transform2D::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
        
        Transform2D::Point3D previousPos;
        previousPos.x = _lastMousePos.x;
        previousPos.y = _lastMousePos.y;
        previousPos.z = 1.;
        previousPos = transformscale * previousPos;
        previousPos.x /= previousPos.z;
        previousPos.y /= previousPos.z;
        if ((_drawState == eSkewXBarHoverered && previousPos.y > center.y) ||
            (_drawState == eSkewYBarHoverered && previousPos.x > center.x)) {
            flip = 180.;
        }
    }
    drawSkewBar(center, pscale, radius.y, _mouseState == eDraggingSkewXBar || _drawState == eSkewXBarHoverered, flip);
    drawSkewBar(center, pscale, radius.x, _mouseState == eDraggingSkewYBar || _drawState == eSkewYBarHoverered, flip - 90.);
    
    
    drawSquare(center, pscale, _mouseState == eDraggingCenterPoint || _drawState == eCenterPointHovered);
    drawSquare(left, pscale, _mouseState == eDraggingLeftPoint || _drawState == eLeftPointHovered);
    drawSquare(right, pscale, _mouseState == eDraggingRightPoint || _drawState == eRightPointHovered);
    drawSquare(top, pscale, _mouseState == eDraggingTopPoint || _drawState == eTopPointHovered);
    drawSquare(bottom, pscale, _mouseState == eDraggingBottomPoint || _drawState == eBottomPointHovered);
    
    glPopMatrix();
    return true;
}

static bool squareContains(const Transform2D::Point3D& pos,const OfxRectD& rect,double toleranceX= 0.,double toleranceY = 0.)
{
    return (pos.x >= (rect.x1 - toleranceX) && pos.x < (rect.x2 + toleranceX)
            && pos.y >= (rect.y1 - toleranceY) && pos.y < (rect.y2 + toleranceY));
}

static bool isOnEllipseBorder(const Transform2D::Point3D& pos,const OfxPointD& radius,const OfxPointD& center,double epsilon = 0.1)
{

    double v = (((pos.x - center.x) * (pos.x - center.x)) / (radius.x * radius.x)) +
    (((pos.y - center.y) * (pos.y - center.y)) / (radius.y * radius.y));
    if (v <= (1. + epsilon) && v >= (1. - epsilon)) {
        return true;
    }
    return false;
}

static bool isOnSkewXBar(const Transform2D::Point3D& pos,double radiusY,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusY + (20. * meanPixelScale);
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + tolerance) &&
        pos.y >= (center.y - barHalfSize - tolerance) && pos.y <= (center.y + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnSkewYBar(const Transform2D::Point3D& pos,double radiusX,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barHalfSize = radiusX + (20. * meanPixelScale);
    if (pos.y >= (center.y - tolerance) && pos.y <= (center.y + tolerance) &&
        pos.x >= (center.x - barHalfSize - tolerance) && pos.x <= (center.x + barHalfSize + tolerance)) {
        return true;
    }

    return false;
}

static bool isOnRotationBar(const  Transform2D::Point3D& pos,double radiusX,const OfxPointD& center,const OfxPointD& pixelScale,double tolerance)
{
    // we are not axis-aligned
    double meanPixelScale = (pixelScale.x + pixelScale.y) / 2.;
    double barExtra = 30. * meanPixelScale;
    if (pos.x >= (center.x - tolerance) && pos.x <= (center.x + radiusX + barExtra + tolerance) &&
        pos.y >= (center.y  - tolerance) && pos.y <= (center.y + tolerance)) {
        return true;
    }

    return false;
}

static OfxRectD rectFromCenterPoint(const OfxPointD& center)
{
    OfxRectD ret;
    ret.x1 = center.x - POINT_SIZE / 2.;
    ret.x2 = center.x + POINT_SIZE / 2.;
    ret.y1 = center.y - POINT_SIZE / 2.;
    ret.y2 = center.y + POINT_SIZE / 2.;
    return ret;
}


// overridden functions from OFX::Interact to do things
bool TransformInteract::penMotion(const OFX::PenArgs &args)
{
    if (!isOverlayDisplayed()) {
        return false;
    }

    OfxPointD center, left, right, top, bottom;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(pscale, &center, &left, &bottom, &top, &right);

    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(pscale, &ellipseRadius);
    
    double dx = args.penPosition.x - _lastMousePos.x;
    double dy = args.penPosition.y - _lastMousePos.y;
    
    double currentRotation;
    _rotate->getValue(currentRotation);
    double rot = Transform2D::toRadians(currentRotation);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValue(skewX);
    _skewY->getValue(skewY);
    _skewOrder->getValue(skewOrderYX);
    
    OfxPointD scale;
    _scale->getValue(scale.x, scale.y);
    bool scaleUniform;
    _scaleUniform->getValue(scaleUniform);
    if (scaleUniform) {
        scale.y = scale.x;
    }

    Transform2D::Point3D penPos, rotationPos, transformedPos, previousPos, currentPos;
    penPos.x = args.penPosition.x;
    penPos.y = args.penPosition.y;
    penPos.z = 1.;

    Transform2D::Matrix3x3 rotation, transform, transformscale;
    ////for the rotation bar dragging we dont use the same transform, we don't want to undo the rotation transform
    if (_mouseState != eDraggingRotationBar && _mouseState != eDraggingCenterPoint) {
        ///undo skew + rotation to the current position
        rotation = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, center.x, center.y);
        transform = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
        transformscale = Transform2D::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);
    } else {
        rotation = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, 0., center.x, center.y);
        transform = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, 0., center.x, center.y);
        transformscale = Transform2D::matInverseTransformCanonical(0., 0., scale.x, scale.y, skewX, skewY, (bool)skewOrderYX, 0., center.x, center.y);
    }

    rotationPos = rotation * penPos;
    rotationPos.x /= rotationPos.z;
    rotationPos.y /= rotationPos.z;
    
    transformedPos = transform * penPos;
    transformedPos.x /= transformedPos.z;
    transformedPos.y /= transformedPos.z;
    
    previousPos.x = _lastMousePos.x;
    previousPos.y = _lastMousePos.y;
    previousPos.z = 1.;
    previousPos = transformscale * previousPos;
    previousPos.x /= previousPos.z;
    previousPos.y /= previousPos.z;
    
    currentPos = transformscale * penPos;
    currentPos.x /= currentPos.z;
    currentPos.y /= currentPos.z;
    
    bool ret = true;
    if (_mouseState == eReleased) {
        double hoverToleranceX = 5 * pscale.x;
        double hoverToleranceY = 5 * pscale.y;
        if (squareContains(transformedPos, centerPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eCenterPointHovered;
        } else if (squareContains(transformedPos, leftPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eLeftPointHovered;
        } else if (squareContains(transformedPos, rightPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eRightPointHovered;
        } else if (squareContains(transformedPos, topPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eTopPointHovered;
        } else if (squareContains(transformedPos, bottomPoint,hoverToleranceX,hoverToleranceY)) {
            _drawState = eBottomPointHovered;
        } else if (isOnEllipseBorder(transformedPos, ellipseRadius, center)) {
            _drawState = eCircleHovered;
        } else if (isOnRotationBar(rotationPos, ellipseRadius.x, center, pscale, hoverToleranceX)) {
            _drawState = eRotationBarHovered;
        } else if (isOnSkewXBar(transformedPos,ellipseRadius.y,center,pscale,hoverToleranceY)) {
            _drawState = eSkewXBarHoverered;
        } else if (isOnSkewYBar(transformedPos,ellipseRadius.x,center,pscale,hoverToleranceX)) {
            _drawState = eSkewYBarHoverered;
        } else {
            _drawState = eInActive;
            ret = false;
        }
    } else if (_mouseState == eDraggingCircle) {
        double minX,minY,maxX,maxY;
        _scale->getRange(minX, minY, maxX, maxY);
        
        // we need to compute the backtransformed points with the scale
        
        // the scale ratio is the ratio of distances to the center
        double prevDistSq = (center.x - previousPos.x)*(center.x - previousPos.x) + (center.y - previousPos.y)*(center.y - previousPos.y);
        if (prevDistSq != 0.) {
            const double distSq = (center.x - currentPos.x)*(center.x - currentPos.x) + (center.y - currentPos.y)*(center.y - currentPos.y);
            const double distRatio = std::sqrt(distSq/prevDistSq);
            scale.x *= distRatio;
            scale.y *= distRatio;
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingLeftPoint || _mouseState == eDraggingRightPoint) {
        // avoid division by zero
        if (center.x != previousPos.x) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (center.x - currentPos.x)/(center.x - previousPos.x);
            scale.x *= scaleRatio;
            scale.x = std::max(minX, std::min(scale.x, maxX));
            if (scaleUniform) {
                scale.y = scale.x;
            }
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingTopPoint || _mouseState == eDraggingBottomPoint) {
        // avoid division by zero
        if (center.y != previousPos.y) {
            double minX,minY,maxX,maxY;
            _scale->getRange(minX, minY, maxX, maxY);
            const double scaleRatio = (center.y - currentPos.y)/(center.y - previousPos.y);
            scale.y *= scaleRatio;
            scale.y = std::max(minY, std::min(scale.y, maxY));
            if (scaleUniform) {
                scale.x = scale.y;
            }
            _scale->setValue(scale.x, scale.y);
        }
    } else if (_mouseState == eDraggingCenterPoint) {
        OfxPointD currentTranslation;
        _translate->getValue(currentTranslation.x, currentTranslation.y);
        
        dx = args.penPosition.x - _lastMousePos.x;
        dy = args.penPosition.y - _lastMousePos.y;
        currentTranslation.x += dx;
        currentTranslation.y += dy;
        _translate->setValue(currentTranslation.x,currentTranslation.y);
    } else if (_mouseState == eDraggingRotationBar) {
        OfxPointD diffToCenter;
        ///the current mouse position (untransformed) is doing has a certain angle relative to the X axis
        ///which can be computed by : angle = arctan(opposite / adjacent)
        diffToCenter.y = rotationPos.y - center.y;
        diffToCenter.x = rotationPos.x - center.x;
        double angle = std::atan2(diffToCenter.y, diffToCenter.x);
        _rotate->setValue(Transform2D::toDegrees(angle));
        
    } else if (_mouseState == eDraggingSkewXBar) {
        // avoid division by zero
        if (scale.y != 0. && center.y != previousPos.y) {
            const double addSkew = (scale.x/scale.y)*(currentPos.x - previousPos.x)/(currentPos.y - center.y);
            _skewX->setValue(skewX + addSkew);
        }
    } else if (_mouseState == eDraggingSkewYBar) {
        // avoid division by zero
        if (scale.x != 0. && center.x != previousPos.x) {
            const double addSkew = (scale.y/scale.x)*(currentPos.y - previousPos.y)/(currentPos.x - center.x);
            _skewY->setValue(skewY + addSkew);
        }
    } else {
        assert(false);
    }
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
    
}

bool TransformInteract::penDown(const OFX::PenArgs &args)
{
    if (!isOverlayDisplayed()) {
        return false;
    }
    
    using Transform2D::Matrix3x3;
    
    OfxPointD center,left,right,top,bottom;
    OfxPointD pscale;

    pscale.x = args.pixelScale.x / args.renderScale.x;
    pscale.y = args.pixelScale.y / args.renderScale.y;
    getPoints(pscale, &center, &left, &bottom, &top, &right);
    OfxRectD centerPoint = rectFromCenterPoint(center);
    OfxRectD leftPoint = rectFromCenterPoint(left);
    OfxRectD rightPoint = rectFromCenterPoint(right);
    OfxRectD topPoint = rectFromCenterPoint(top);
    OfxRectD bottomPoint = rectFromCenterPoint(bottom);
    
    OfxPointD ellipseRadius;
    getCircleRadius(pscale, &ellipseRadius);
    
    
    double currentRotation;
    _rotate->getValue(currentRotation);
    
    double skewX, skewY;
    int skewOrderYX;
    _skewX->getValue(skewX);
    _skewY->getValue(skewY);
    _skewOrder->getValue(skewOrderYX);
    
    Transform2D::Point3D transformedPos, rotationPos;
    transformedPos.x = args.penPosition.x;
    transformedPos.y = args.penPosition.y;
    transformedPos.z = 1.;
    
    double rot = Transform2D::toRadians(currentRotation);

    ///now undo skew + rotation to the current position
    Transform2D::Matrix3x3 rotation, transform;
    rotation = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., 0., 0., false, rot, center.x, center.y);
    transform = Transform2D::matInverseTransformCanonical(0., 0., 1., 1., skewX, skewY, (bool)skewOrderYX, rot, center.x, center.y);

    rotationPos = rotation * transformedPos;
    rotationPos.x /= rotationPos.z;
    rotationPos.y /= rotationPos.z;
    transformedPos = transform * transformedPos;
    transformedPos.x /= transformedPos.z;
    transformedPos.y /= transformedPos.z;
    
    double pressToleranceX = 5 * pscale.x;
    double pressToleranceY = 5 * pscale.y;
    bool ret = true;
    if (squareContains(transformedPos, centerPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingCenterPoint;
    } else if (squareContains(transformedPos, leftPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingLeftPoint;
    } else if (squareContains(transformedPos, rightPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingRightPoint;
    } else if (squareContains(transformedPos, topPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingTopPoint;
    } else if (squareContains(transformedPos, bottomPoint,pressToleranceX,pressToleranceY)) {
        _mouseState = eDraggingBottomPoint;
    } else if (isOnEllipseBorder(transformedPos,ellipseRadius, center)) {
        _mouseState = eDraggingCircle;
    } else if (isOnRotationBar(rotationPos, ellipseRadius.x, center, pscale, pressToleranceY)) {
        _mouseState = eDraggingRotationBar;
    } else if (isOnSkewXBar(transformedPos,ellipseRadius.y,center,pscale,pressToleranceY)) {
        _mouseState = eDraggingSkewXBar;
    } else if (isOnSkewYBar(transformedPos,ellipseRadius.x,center,pscale,pressToleranceX)) {
        _mouseState = eDraggingSkewYBar;
    } else {
        _mouseState = eReleased;
        ret =  false;
    }
    
    _lastMousePos = args.penPosition;
    
    _effect->redrawOverlays();
    return ret;
}

bool TransformInteract::penUp(const OFX::PenArgs &args)
{
    
    if (!isOverlayDisplayed()) {
        return false;
    }
    
    bool ret = _mouseState != eReleased;
    _mouseState = eReleased;
    _lastMousePos = args.penPosition;
    _effect->redrawOverlays();
    return ret;
}


using namespace OFX;

class TransformOverlayDescriptor : public DefaultEffectOverlayDescriptor<TransformOverlayDescriptor, TransformInteract> {};

void TransformPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("TransformOFX", "TransformOFX", "TransformOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Translate / Rotate / Scale a 2D image.");
    
    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);
    
    // set a few flags
    
    // GENERIC
    
    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);
    
    // NON-GENERIC

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);
    
    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);

    // NON-GENERIC (NON-MASKED)

#ifdef OFX_EXTENSIONS_NUKE
    // Enable transform by the host.
    // It is only possible for transforms which can be represented as a 3x3 matrix.
    desc.setCanTransform(true);
    if (getImageEffectHostDescription()->canTransform) {
        std::cout << "kFnOfxImageEffectCanTransform (describe) =" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
    }
#endif
}


void TransformPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // GENERIC
    
    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->setSupportsTiles(true);
    
    
    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");
    
    // NON-GENERIC PARAMETERS
    //
    Double2DParamDescriptor* translate = desc.defineDouble2DParam(kTranslateParamName);
    translate->setLabels(kTranslateParamName, kTranslateParamName, kTranslateParamName);
    //translate->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    translate->setDoubleType(eDoubleTypeXYAbsolute);
    translate->setDimensionLabels("x","y");
    translate->setDefault(0, 0);
    page->addChild(*translate);
    
    DoubleParamDescriptor* rotate = desc.defineDoubleParam(kRotateParamName);
    rotate->setLabels(kRotateParamName, kRotateParamName, kRotateParamName);
    rotate->setDoubleType(eDoubleTypeAngle);
    rotate->setDefault(0);
    //rotate->setRange(-180, 180); // the angle may be -infinity..+infinity
    rotate->setDisplayRange(-180, 180);
    page->addChild(*rotate);
    
    Double2DParamDescriptor* scale = desc.defineDouble2DParam(kScaleParamName);
    scale->setLabels(kScaleParamName, kScaleParamName, kScaleParamName);
    scale->setDoubleType(eDoubleTypeScale);
    scale->setDimensionLabels("w","h");
    scale->setDefault(1,1);
    //scale->setRange(0.1,0.1,10,10);
    scale->setDisplayRange(0.1, 0.1, 10, 10);
    scale->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*scale);
    
    BooleanParamDescriptor* scaleUniform = desc.defineBooleanParam(kScaleUniformParamName);
    scaleUniform->setLabels(kScaleUniformParamName, kScaleUniformParamName, kScaleUniformParamName);
    scaleUniform->setHint(kScaleUniformParamHint);
    scaleUniform->setDefault(true);
    scaleUniform->setAnimates(false);
    page->addChild(*scaleUniform);

    DoubleParamDescriptor* skewX = desc.defineDoubleParam(kSkewXParamName);
    skewX->setLabels(kSkewXParamName, kSkewXParamName, kSkewXParamName);
    skewX->setDefault(0);
    skewX->setDisplayRange(-1,1);
    page->addChild(*skewX);
    
    DoubleParamDescriptor* skewY = desc.defineDoubleParam(kSkewYParamName);
    skewY->setLabels(kSkewYParamName, kSkewYParamName, kSkewYParamName);
    skewY->setDefault(0);
    skewY->setDisplayRange(-1,1);
    page->addChild(*skewY);
    
    ChoiceParamDescriptor* skewOrder = desc.defineChoiceParam(kSkewOrderParamName);
    skewOrder->setLabels(kSkewOrderParamName, kSkewOrderParamName, kSkewOrderParamName);
    skewOrder->setDefault(0);
    skewOrder->appendOption("XY");
    skewOrder->appendOption("YX");
    skewOrder->setAnimates(false);
    page->addChild(*skewOrder);
    
    Double2DParamDescriptor* center = desc.defineDouble2DParam(kCenterParamName);
    center->setLabels(kCenterParamName, kCenterParamName, kCenterParamName);
    //center->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDimensionLabels("x","y");
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    page->addChild(*center);
    
    BooleanParamDescriptor* invert = desc.defineBooleanParam(kInvertParamName);
    invert->setLabels(kInvertParamName, kInvertParamName, kInvertParamName);
    invert->setDefault(false);
    invert->setAnimates(false);
    page->addChild(*invert);
    
    BooleanParamDescriptor* showOverlay = desc.defineBooleanParam(kShowOverlayParamName);
    showOverlay->setLabels(kShowOverlayParamName, kShowOverlayParamName, kShowOverlayParamName);
    showOverlay->setDefault(true);
    showOverlay->setAnimates(false);
    showOverlay->setEvaluateOnChange(false);
    page->addChild(*showOverlay);
    
    // GENERIC PARAMETERS
    //
    ChoiceParamDescriptor* filter = desc.defineChoiceParam(kFilterParamName);
    filter->setLabels(kFilterParamName, kFilterParamName, kFilterParamName);
    filter->setHint(kFilterParamHint);
    assert(filter->getNOptions() == eImpulseFilter);
    filter->appendOption(kImpulseFilter, kImpulseFilterHint);
    assert(filter->getNOptions() == eBilinearFilter);
    filter->appendOption(kBilinearFilter, kBilinearFilterHint);
    assert(filter->getNOptions() == eCubicFilter);
    filter->appendOption(kCubicFilter, kCubicFilterHint);
    assert(filter->getNOptions() == eKeysFilter);
    filter->appendOption(kKeysFilter, kKeysFilterHint);
    assert(filter->getNOptions() == eSimonFilter);
    filter->appendOption(kSimonFilter, kSimonFilterHint);
    assert(filter->getNOptions() == eRifmanFilter);
    filter->appendOption(kRifmanFilter, kRifmanFilterHint);
    assert(filter->getNOptions() == eMitchellFilter);
    filter->appendOption(kMitchellFilter, kMitchellFilterHint);
    assert(filter->getNOptions() == eParzenFilter);
    filter->appendOption(kParzenFilter, kParzenFilterHint);
    assert(filter->getNOptions() == eNotchFilter);
    filter->appendOption(kNotchFilter, kNotchFilterHint);
    filter->setDefault(eCubicFilter);
    filter->setAnimates(false);
    filter->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*filter);

    BooleanParamDescriptor* clamp = desc.defineBooleanParam(kClampParamName);
    clamp->setLabels(kClampParamName, kClampParamName, kClampParamName);
    clamp->setHint(kClampParamHint);
    clamp->setDefault(false);
    clamp->setAnimates(false);
    clamp->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*clamp);

    BooleanParamDescriptor* blackOutside = desc.defineBooleanParam(kBlackOutsideParamName);
    blackOutside->setLabels(kBlackOutsideParamName, kBlackOutsideParamName, kBlackOutsideParamName);
    blackOutside->setHint(kBlackOutsideParamHint);
    blackOutside->setDefault(true);
    blackOutside->setAnimates(false);
    page->addChild(*blackOutside);

    // NON-GENERIC (NON-MASKED)
    //
#ifdef OFX_EXTENSIONS_NUKE
    if (getImageEffectHostDescription()->canTransform) {
        std::cout << "kFnOfxImageEffectCanTransform in describeincontext(" << context << ")=" << desc.getPropertySet().propGetInt(kFnOfxImageEffectCanTransform) << std::endl;
    }
#endif
}

OFX::ImageEffect* TransformPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new TransformPlugin(handle, false);
}


void TransformMaskedPluginFactory::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    desc.setLabels("TransformMaskedOFX", "TransformMaskedOFX", "TransformMaskedOFX");
    desc.setPluginGrouping("Transform");
    desc.setPluginDescription("Translate / Rotate / Scale a 2D image.");

    desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    desc.addSupportedContext(eContextPaint);
    desc.addSupportedBitDepth(eBitDepthUByte);
    desc.addSupportedBitDepth(eBitDepthUShort);
    desc.addSupportedBitDepth(eBitDepthFloat);

    // set a few flags

    // GENERIC

    desc.setSingleInstance(false);
    desc.setHostFrameThreading(false);
    desc.setTemporalClipAccess(false);
    // each field has to be transformed separately, or you will get combing effect
    // this should be true for all geometric transforms
    desc.setRenderTwiceAlways(true);
    desc.setSupportsMultipleClipPARs(false);
    desc.setRenderThreadSafety(eRenderFullySafe);

    // NON-GENERIC

    // in order to support tiles, the plugin must implement the getRegionOfInterest function
    desc.setSupportsTiles(true);

    // in order to support multiresolution, render() must take into account the pixelaspectratio and the renderscale
    // and scale the transform appropriately.
    // All other functions are usually in canonical coordinates.
    desc.setSupportsMultiResolution(true);

    desc.setOverlayInteractDescriptor(new TransformOverlayDescriptor);
}


void TransformMaskedPluginFactory::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    // GENERIC

    // Source clip only in the filter context
    // create the mandated source clip
    // always declare the source clip first, because some hosts may consider
    // it as the default input clip (e.g. Nuke)
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(true);
    srcClip->setIsMask(false);

    // GENERIC (MASKED)
    //
    // if general or paint context, define the mask clip
    if (context == eContextGeneral || context == eContextPaint) {
        // if paint context, it is a mandated input called 'brush'
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral) {
            maskClip->setOptional(true);
        }
        maskClip->setSupportsTiles(true);
        maskClip->setIsMask(true); // we are a mask input
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(true);


    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    // NON-GENERIC PARAMETERS
    //
    Double2DParamDescriptor* translate = desc.defineDouble2DParam(kTranslateParamName);
    translate->setLabels(kTranslateParamName, kTranslateParamName, kTranslateParamName);
    //translate->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    translate->setDoubleType(eDoubleTypeXYAbsolute);
    translate->setDimensionLabels("x","y");
    translate->setDefault(0, 0);
    page->addChild(*translate);

    DoubleParamDescriptor* rotate = desc.defineDoubleParam(kRotateParamName);
    rotate->setLabels(kRotateParamName, kRotateParamName, kRotateParamName);
    rotate->setDoubleType(eDoubleTypeAngle);
    rotate->setDefault(0);
    //rotate->setRange(-180, 180); // the angle may be -infinity..+infinity
    rotate->setDisplayRange(-180, 180);
    page->addChild(*rotate);

    Double2DParamDescriptor* scale = desc.defineDouble2DParam(kScaleParamName);
    scale->setLabels(kScaleParamName, kScaleParamName, kScaleParamName);
    scale->setDoubleType(eDoubleTypeScale);
    scale->setDimensionLabels("w","h");
    scale->setDefault(1,1);
    //scale->setRange(0.1,0.1,10,10);
    scale->setDisplayRange(0.1, 0.1, 10, 10);
    scale->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*scale);

    BooleanParamDescriptor* scaleUniform = desc.defineBooleanParam(kScaleUniformParamName);
    scaleUniform->setLabels(kScaleUniformParamName, kScaleUniformParamName, kScaleUniformParamName);
    scaleUniform->setHint(kScaleUniformParamHint);
    scaleUniform->setDefault(true);
    scaleUniform->setAnimates(false);
    page->addChild(*scaleUniform);

    DoubleParamDescriptor* skewX = desc.defineDoubleParam(kSkewXParamName);
    skewX->setLabels(kSkewXParamName, kSkewXParamName, kSkewXParamName);
    skewX->setDefault(0);
    skewX->setDisplayRange(-1,1);
    page->addChild(*skewX);

    DoubleParamDescriptor* skewY = desc.defineDoubleParam(kSkewYParamName);
    skewY->setLabels(kSkewYParamName, kSkewYParamName, kSkewYParamName);
    skewY->setDefault(0);
    skewY->setDisplayRange(-1,1);
    page->addChild(*skewY);

    ChoiceParamDescriptor* skewOrder = desc.defineChoiceParam(kSkewOrderParamName);
    skewOrder->setLabels(kSkewOrderParamName, kSkewOrderParamName, kSkewOrderParamName);
    skewOrder->setDefault(0);
    skewOrder->appendOption("XY");
    skewOrder->appendOption("YX");
    skewOrder->setAnimates(false);
    page->addChild(*skewOrder);

    Double2DParamDescriptor* center = desc.defineDouble2DParam(kCenterParamName);
    center->setLabels(kCenterParamName, kCenterParamName, kCenterParamName);
    //center->setDoubleType(eDoubleTypeNormalisedXY); // deprecated in OpenFX 1.2
    center->setDoubleType(eDoubleTypeXYAbsolute);
    center->setDimensionLabels("x","y");
    center->setDefaultCoordinateSystem(eCoordinatesNormalised);
    center->setDefault(0.5, 0.5);
    page->addChild(*center);

    BooleanParamDescriptor* invert = desc.defineBooleanParam(kInvertParamName);
    invert->setLabels(kInvertParamName, kInvertParamName, kInvertParamName);
    invert->setDefault(false);
    invert->setAnimates(false);
    page->addChild(*invert);

    BooleanParamDescriptor* showOverlay = desc.defineBooleanParam(kShowOverlayParamName);
    showOverlay->setLabels(kShowOverlayParamName, kShowOverlayParamName, kShowOverlayParamName);
    showOverlay->setDefault(true);
    showOverlay->setAnimates(false);
    showOverlay->setEvaluateOnChange(false);
    page->addChild(*showOverlay);

    // GENERIC PARAMETERS
    //
    ChoiceParamDescriptor* filter = desc.defineChoiceParam(kFilterParamName);
    filter->setLabels(kFilterParamName, kFilterParamName, kFilterParamName);
    filter->setHint(kFilterParamHint);
    assert(filter->getNOptions() == eImpulseFilter);
    filter->appendOption(kImpulseFilter, kImpulseFilterHint);
    assert(filter->getNOptions() == eBilinearFilter);
    filter->appendOption(kBilinearFilter, kBilinearFilterHint);
    assert(filter->getNOptions() == eCubicFilter);
    filter->appendOption(kCubicFilter, kCubicFilterHint);
    assert(filter->getNOptions() == eKeysFilter);
    filter->appendOption(kKeysFilter, kKeysFilterHint);
    assert(filter->getNOptions() == eSimonFilter);
    filter->appendOption(kSimonFilter, kSimonFilterHint);
    assert(filter->getNOptions() == eRifmanFilter);
    filter->appendOption(kRifmanFilter, kRifmanFilterHint);
    assert(filter->getNOptions() == eMitchellFilter);
    filter->appendOption(kMitchellFilter, kMitchellFilterHint);
    assert(filter->getNOptions() == eParzenFilter);
    filter->appendOption(kParzenFilter, kParzenFilterHint);
    assert(filter->getNOptions() == eNotchFilter);
    filter->appendOption(kNotchFilter, kNotchFilterHint);
    filter->setDefault(eCubicFilter);
    filter->setAnimates(false);
    filter->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*filter);

    BooleanParamDescriptor* clamp = desc.defineBooleanParam(kClampParamName);
    clamp->setLabels(kClampParamName, kClampParamName, kClampParamName);
    clamp->setHint(kClampParamHint);
    clamp->setDefault(false);
    clamp->setAnimates(false);
    clamp->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*clamp);
    
    BooleanParamDescriptor* blackOutside = desc.defineBooleanParam(kBlackOutsideParamName);
    blackOutside->setLabels(kBlackOutsideParamName, kBlackOutsideParamName, kBlackOutsideParamName);
    blackOutside->setDefault(true);
    blackOutside->setAnimates(false);
    page->addChild(*blackOutside);

    // GENERIC (MASKED)
    //
    BooleanParamDescriptor* domask = desc.defineBooleanParam(kMaskParamName);
    domask->setLabels(kMaskParamName, kMaskParamName, kMaskParamName);
    domask->setDefault(false);
    domask->setAnimates(false);
    page->addChild(*domask);

    DoubleParamDescriptor* mix = desc.defineDoubleParam(kMixParamName);
    mix->setLabels(kMixParamName, kMixParamName, kMixParamName);
    mix->setDefault(1.);
    mix->setRange(0.,1.);
    mix->setDisplayRange(0.,1.);
    page->addChild(*mix);
}

OFX::ImageEffect* TransformMaskedPluginFactory::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context)
{
    return new TransformPlugin(handle, true);
}
