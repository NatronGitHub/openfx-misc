/*
 OFX Distortion plugins.
 
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
// This node concatenates transforms upstream.

#include "Distortion.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ofxsImageEffect.h"
#include "ofxsProcessing.H"
#include "ofxsMerging.h"
#include "ofxsMaskMix.h"
#include "ofxsFilter.h"
#include "ofxsMatrix2D.h"
#include "ofxsCopier.h"
#include "ofxsMacros.h"

#define kPluginIDistortName "IDistortOFX"
#define kPluginIDistortGrouping "Transform"
#define kPluginIDistortDescription \
"Distort an image, based on a displacement map.\n" \
"The U and V channels give the offset in pixels in the destination image to the pixel where the color is taken. " \
"For example, if at pixel (45,12) the UV value is (-1.5,3.2), then the color at this pixel is taken from (43.5,15.2) in the source image. " \
"This plugin concatenates transforms upstream, so that if the nodes upstream output a 3x3 transform " \
"(e.g. Transform, CornerPin, Dot, NoOp, Switch), the original image is sampled only once." \

#define kPluginIDistortIdentifier "net.sf.openfx.IDistort"

#define kPluginSTMapName "STMapOFX"
#define kPluginSTMapGrouping "Transform"
#define kPluginSTMapDescription \
"Move pixels around an image, based on a UVmap.\n" \
"The U and V channels give, for each pixel in the destination image, the normalized position of the pixel where the color is taken. " \
"(0,0) is the bottom left corner of the input image, while (1,1) is the top right corner. " \
"This plugin concatenates transforms upstream, so that if the nodes upstream output a 3x3 transform " \
"(e.g. Transform, CornerPin, Dot, NoOp, Switch), the original image is sampled only once." \

#define kPluginSTMapIdentifier "net.sf.openfx.STMap"

#define kPluginLensDistortionName "LensDistortionOFX"
#define kPluginLensDistortionGrouping "Transform"
#define kPluginLensDistortionDescription \
"Add or remove lens distortion." \

#define kPluginLensDistortionIdentifier "net.sf.openfx.LensDistortion"

/* LensDistortion TODO:
 - cache the STmap for a set of input parameter and input image size
 - output the STmap (which is not frame-varying even if the input changes, so isIdentity should use this on Natron if no parameter is animated)
 - compute the inverse map and undistort
 - implement other distortion models (PFBarrel, OpenCV)
*/

#define kPluginVersionMajor 1 // Incrementing this number means that you have broken backwards compatibility of the plug-in.
#define kPluginVersionMinor 0 // Increment this when you have fixed a bug or made it faster.

#define kSupportsTiles 1
#define kSupportsMultiResolution 1
#define kSupportsRenderScale 1
#define kSupportsMultipleClipPARs false
#define kSupportsMultipleClipDepths false
#define kRenderThreadSafety eRenderFullySafe

enum DistortionPluginEnum {
    eDistortionPluginSTMap,
    eDistortionPluginIDistort,
    eDistortionPluginLensDistortion,
};

#define kParamProcessR      "r"
#define kParamProcessRLabel "R"
#define kParamProcessRHint  "Process red component"
#define kParamProcessG      "g"
#define kParamProcessGLabel "G"
#define kParamProcessGHint  "Process green component"
#define kParamProcessB      "b"
#define kParamProcessBLabel "B"
#define kParamProcessBHint  "Process blue component"
#define kParamProcessA      "a"
#define kParamProcessALabel "A"
#define kParamProcessAHint  "Process alpha component"

#define kParamChannelU "channelU"
#define kParamChannelULabel "U Channel"
#define kParamChannelUHint "Input channel for U from UV"

#define kParamChannelV "channelV"
#define kParamChannelVLabel "V Channel"
#define kParamChannelVHint "Input channel for V from UV"

#define kParamChannelOptionR "UV.r"
#define kParamChannelOptionRHint "R channel from UV"
#define kParamChannelOptionG "UV.g"
#define kParamChannelOptionGHint "G channel from UV"
#define kParamChannelOptionB "UV.b"
#define kParamChannelOptionBHint "B channel from UV"
#define kParamChannelOptionA "UV.a"
#define kParamChannelOptionAHint "A channel from UV"
#define kParamChannelOption0 "0"
#define kParamChannelOption0Hint "0 constant channel"
#define kParamChannelOption1 "1"
#define kParamChannelOption1Hint "1 constant channel"

enum InputChannelEnum {
    eInputChannelR = 0,
    eInputChannelG,
    eInputChannelB,
    eInputChannelA,
    eInputChannel0,
    eInputChannel1,
};

#define kParamWrapU "wrapU"
#define kParamWrapULabel "U Wrap Mode"
#define kParamWrapUHint "Wrap mode for U coordinate."

#define kParamWrapV "wrapV"
#define kParamWrapVLabel "V Wrap Mode"
#define kParamWrapVHint "Wrap mode for V coordinate."

#define kParamWrapOptionClamp "Clamp"
#define kParamWrapOptionClampHint "Texture edges are black (if blackOutside is checked) or stretched indefinitely."
#define kParamWrapOptionRepeat "Repeat"
#define kParamWrapOptionRepeatHint "Texture is repeated."
#define kParamWrapOptionMirror "Mirror"
#define kParamWrapOptionMirrorHint "Texture is mirrored alternatively."

enum WrapEnum {
    eWrapClamp = 0,
    eWrapRepeat,
    eWrapMirror,
};

#define kClipUV "UV"

#define kParamUVOffset "uvOffset"
#define kParamUVOffsetLabel "UV Offset"
#define kParamUVOffsetHint "Offset to apply to the U and V channel (useful if these were stored in a file that cannot handle negative numbers)"

#define kParamUVScale "uvScale"
#define kParamUVScaleLabel "UV Scale"
#define kParamUVScaleHint "Scale factor to apply to the U and V channel (useful if these were stored in a file that can only store integer values)"

#define kParamDistortionModel "model"
#define kParamDistortionModelLabel "Model"
#define kParamDistortionModelHint "Choice of the distortion model, i.e. the function that goes from distorted to undistorted image coordinates."
#define kParamDistortionModelOptionNuke "Nuke"
#define kParamDistortionModelOptionNukeHint "The model used in Nuke's LensDistortion plugin (reverse engineered)."

/*
 Possible distortion models:
 (see also <http://michaelkarp.net/distortion.htm>)

 From Oblique <http://s3aws.obliquefx.com/public/shaders/help_files/Obq_LensDistortion.html>
 PFBarrel	:	PFTrack's distortion model.
 Nuke	:	Nuke's distortion model.
 3DE Classic LD Model	:	Science-D-Visions LDPK (3DEqualizer). see <http://www.3dequalizer.com/user_daten/tech_docs/pdf/ldpk.pdf>
 3DE4 Anamorphic, Degree 6	:
 3DE4 Radial - Fisheye, Degree 8	:
 3DE4 Radial - Standard, Degree 4	:	A depricated model.
 3DE4 Radial - Decentered Cylindric, Degree 4	:
 3DE4 Anamorphic Rotate Squeeze, Degree 4	:
 
 From RV4 <http://www.tweaksoftware.com/static/documentation/rv/rv-4.0.17/html/rv_reference.html#RVLensWarp>
 “brown”, “opencv”, “pfbarrel”, “adobe”, “3de4_anamorphic_degree_6”
 */

enum DistortionModelEnum {
    eDistortionModelNuke,
};

#define kParamK1 "k1"
#define kParamK1Label "K1"
#define kParamK1Hint "First radial distortion coefficient (coefficient for r^2)."

#define kParamK2 "k2"
#define kParamK2Label "K2"
#define kParamK2Hint "Second radial distortion coefficient (coefficient for r^4)."

#define kParamK3 "k3"
#define kParamK3Label "K3"
#define kParamK3Hint "Third radial distortion coefficient (coefficient for r^6)."

#define kParamP1 "p1"
#define kParamP1Label "P1"
#define kParamP1Hint "First tangential distortion coefficient."

#define kParamP2 "p2"
#define kParamP2Label "P2"
#define kParamP2Hint "Second tangential distortion coefficient."

#define kParamCenter "center"
#define kParamCenterLabel "Center"
#define kParamCenterHint "Offset of the distortion center from the image center."

#define kParamSqueeze "anamorphicSqueeze"
#define kParamSqueezeLabel "Squeeze"
#define kParamSqueezeHint "Anamorphic squeeze (only for anamorphic lens)."

#define kParamAsymmetric "asymmetricDistortion"
#define kParamAsymmetricLabel "Asymmetric"
#define kParamAsymmetricHint "Asymmetric distortion (only for anamorphic lens)."

using namespace OFX;

class DistortionProcessorBase : public OFX::ImageProcessor
{
protected:
    const OFX::Image *_srcImg;
    const OFX::Image *_uvImg;
    const OFX::Image *_maskImg;
    bool _processR;
    bool _processG;
    bool _processB;
    bool _processA;
    bool _transformIsIdentity;
    OFX::Matrix3x3 _srcTransformInverse;
    InputChannelEnum _uChannel;
    InputChannelEnum _vChannel;
    double _uOffset;
    double _vOffset;
    double _uScale;
    double _vScale;
    WrapEnum _uWrap;
    WrapEnum _vWrap;
    DistortionModelEnum _distortionModel;
    double _k1;
    double _k2;
    double _k3;
    double _p1;
    double _p2;
    double _cx;
    double _cy;
    double _squeeze;
    double _ax;
    double _ay;
    bool _blackOutside;
    bool _doMasking;
    double _mix;
    bool _maskInvert;

public:

    DistortionProcessorBase(OFX::ImageEffect &instance)
    : OFX::ImageProcessor(instance)
    , _srcImg(0)
    , _maskImg(0)
    , _processR(true)
    , _processG(true)
    , _processB(true)
    , _processA(false)
    , _transformIsIdentity(true)
    , _srcTransformInverse()
    , _uOffset(0.)
    , _vOffset(0.)
    , _uScale(1.)
    , _vScale(1.)
    , _uWrap(eWrapClamp)
    , _vWrap(eWrapClamp)
    , _distortionModel(eDistortionModelNuke)
    , _k1(0.)
    , _k2(0.)
    , _k3(0.)
    , _p1(0.)
    , _p2(0.)
    , _cx(0.)
    , _cy(0.)
    , _squeeze(1.)
    , _ax(0.)
    , _ay(0.)
    , _blackOutside(false)
    , _doMasking(false)
    , _mix(1.)
    , _maskInvert(false)
    {
    }

    void setSrcImgs(const OFX::Image *src, const OFX::Image *uv) {_srcImg = src; _uvImg = uv;}

    void setMaskImg(const OFX::Image *v, bool maskInvert) { _maskImg = v; _maskInvert = maskInvert; }

    void doMasking(bool v) {_doMasking = v;}

    void setValues(bool processR,
                   bool processG,
                   bool processB,
                   bool processA,
                   bool transformIsIdentity,
                   const OFX::Matrix3x3 &srcTransformInverse,
                   InputChannelEnum uChannel,
                   InputChannelEnum vChannel,
                   double uOffset,
                   double vOffset,
                   double uScale,
                   double vScale,
                   WrapEnum uWrap,
                   WrapEnum vWrap,
                   DistortionModelEnum distortionModel,
                   double k1, double k2, double k3,
                   double p1, double p2,
                   double cx, double cy,
                   double squeeze,
                   double ax, double ay,
                   bool blackOutside,
                   double mix)
    {
        _processR = processR;
        _processG = processG;
        _processB = processB;
        _processA = processA;
        _transformIsIdentity = transformIsIdentity;
        _srcTransformInverse = srcTransformInverse;
        _uChannel = uChannel;
        _vChannel = vChannel;
        _uOffset = uOffset;
        _vOffset = vOffset;
        _uScale = uScale;
        _vScale = vScale;
        _uWrap = uWrap;
        _vWrap = vWrap;
        _distortionModel = distortionModel;
        _k1 = k1;
        _k2 = k2;
        _k3 = k3;
        _p1 = p1;
        _p2 = p2;
        _cx = cx,
        _cy = cy;
        _squeeze = squeeze;
        _ax = ax;
        _ay = ay;
        _blackOutside = blackOutside;
        _mix = mix;
    }

private:
};


// Nuke's distortion function, reverse engineered from the resulting images on a checkerboard (and a little science, too)
static inline void
distort_nuke(double xu, double yu, // undistorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
             double k1, double k2, // radial distortion
             double cx, double cy, // distortion center, (0,0) at center of image
             double squeeze, // anamorphic squeeze
             double ax, double ay, // asymmetric distortion
             double *xd, double *yd) // distorted position in normalized coordinates
{
    // nuke?
    // k1 = radial distortion 1
    // k2 = radial distortion 2
    // squeeze = anamorphic squeeze
    // p1 = asymmetric distortion x
    // p2 = asymmetric distortion y
    double x = (xu - cx);
    double y = (yu - cy);
    double x2 = x*x, y2 = y*y;
    double r2 = x2 + y2;
    double k2r2pk1 = k2*r2 + k1;
    //double kry = 1 + ((k2r2pk1 + ay)*x2 + k2r2pk1*y2);
    double kry = 1 + (k2r2pk1*r2 + ay*x2);
    *yd = (y/kry) + cy;
    //double krx = 1 + (k2r2pk1*x2 + (k2r2pk1 + ax)*y2)/squeeze;
    double krx = 1 + (k2r2pk1*r2 + ax*y2)/squeeze;
    *xd = (x/krx) + cx;
}

#if 0
// see https://github.com/Itseez/opencv/blob/master/modules/imgproc/src/undistort.cpp
static inline void
distort_opencv(double xu, double yu, // undistorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
        double k1, double k2, double k3,
        double p1, double p2,
        double cx, double cy,
        double squeeze,
        double *xd, double *yd) // distorted position in normalized coordinates
{
    // opencv
    const double k4 = 0.;
    const double k5 = 0.;
    const double k6 = 0.;
    const double s1 = 0.;
    const double s2 = 0.;
    const double s3 = 0.;
    const double s4 = 0.;
    double x = (xu - cx)*squeeze;
    double y = yu - cy;
    double x2 = x*x, y2 = y*y;
    double r2 = x2 + y2;
    double _2xy = 2*x*y;
    double kr = (1 + ((k3*r2 + k2)*r2 + k1)*r2)/(1 + ((k6*r2 + k5)*r2 + k4)*r2);
    *xd = ((x*kr + p1*_2xy + p2*(r2 + 2*x2) + s1*r2+s2*r2*r2))/squeeze + cx;
    *yd = (y*kr + p1*(r2 + 2*y2) + p2*_2xy + s3*r2+s4*r2*r2) + cy;
}
#endif

// The "filter" and "clamp" template parameters allow filter-specific optimization
// by the compiler, using the same generic code for all filters.
template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin, FilterEnum filter, bool clamp>
class DistortionProcessor : public DistortionProcessorBase
{
public:
    DistortionProcessor(OFX::ImageEffect &instance)
    : DistortionProcessorBase(instance)
    {
    }

private:
    void
    compFromChannel(InputChannelEnum channel, OFX::Image const **img, int *comp)
    {
        switch (channel) {
            case eInputChannelR:
                if (_uvImg) {
                    if (nComponents >= 3) {
                        *img = _uvImg;
                        *comp = 0;
                    }
                }
                break;
            case eInputChannelG:
                if (_uvImg) {
                    if (nComponents >= 3) {
                        *img = _uvImg;
                        *comp = 1;
                    }
                }
                break;
            case eInputChannelB:
                if (_uvImg) {
                    if (nComponents >= 3) {
                        *img = _uvImg;
                        *comp = 2;
                    }
                }
                break;
            case eInputChannelA:
                if (_uvImg) {
                    if (nComponents >= 4) {
                        *img = _uvImg;
                        *comp = 3;
                    } else if (nComponents == 1) {
                        *img = _uvImg;
                        *comp = 0;
                    }
                }
                break;
            case eInputChannel0:
                *img = 0;
                *comp = 0;
                break;
            case eInputChannel1:
                *img = 0;
                *comp = 1;
                break;
        }
    }

    static inline double wrap(double x, WrapEnum wrap)
    {
        switch(wrap) {
            case eWrapClamp:
                return x;
            case eWrapRepeat:
                return x - std::floor(x);
            case eWrapMirror: {
                double x2 = x/2 - std::floor(x/2);
                return (x2 <= 0.5) ? (2 * x2) : (2 - 2 * x2);
            }
        }
    }

    void multiThreadProcessImages(OfxRectI procWindow)
    {
        assert(nComponents == 1 || nComponents == 3 || nComponents == 4);
        assert(_dstImg);
        const OFX::Image* uImg = 0;
        const OFX::Image* vImg = 0;
        int uComp = 0;
        int vComp = 0;
        compFromChannel(_uChannel, &uImg, &uComp);
        compFromChannel(_vChannel, &vImg, &vComp);
        int srcx1 = 0, srcx2 = 1, srcy1 = 0, srcy2 = 0;
        double f = 0;//, cx = 0, cy = 0;
        if ((plugin == eDistortionPluginSTMap || plugin == eDistortionPluginLensDistortion) && _srcImg) {
            const OfxRectI& srcBounds = _srcImg->getBounds();
            srcx1 = srcBounds.x1;
            srcx2 = srcBounds.x2;
            srcy1 = srcBounds.y1;
            srcy2 = srcBounds.y2;
            if (plugin == eDistortionPluginLensDistortion) {
                double fx = (srcBounds.x2-srcBounds.x1)/2.;
                double fy = (srcBounds.y2-srcBounds.y1)/2.;
                f = std::max(fx, fy); // TODO: distortion scaling param for LensDistortion?
                //cx = (_cx * fx) / f;
                //cy = (_cy * fy) / f;
            }
        }
        float tmpPix[4];
        for (int y = procWindow.y1; y < procWindow.y2; y++) {
            if (_effect.abort()) {
                break;
            }

            PIX *dstPix = (PIX *) _dstImg->getPixelAddress(procWindow.x1, y);

            for (int x = procWindow.x1; x < procWindow.x2; x++) {
                double sx, sy, sxx, sxy, syx, syy; // the source pixel coordinates and their derivatives

                switch (plugin) {
                    case eDistortionPluginSTMap:
                    case eDistortionPluginIDistort: {
                        const PIX *uvPix = (const PIX *)  (_uvImg ? _uvImg->getPixelAddress(x, y) : 0);
                        const PIX *uvPix_xn = (const PIX *)  (_uvImg ? _uvImg->getPixelAddress(x+1, y) : 0);
                        const PIX *uvPix_xp = (const PIX *)  (_uvImg ? _uvImg->getPixelAddress(x-1, y) : 0);
                        const PIX *uvPix_yn = (const PIX *)  (_uvImg ? _uvImg->getPixelAddress(x, y+1) : 0);
                        const PIX *uvPix_yp = (const PIX *)  (_uvImg ? _uvImg->getPixelAddress(x, y-1) : 0);
                        double u, v, ux, uy, vx, vy;
                        // compute gradients before wrapping
                        if (!uImg) {
                            u = uComp;
                            ux = uy = 0.;
                        } else if (!uvPix) {
                            u = PIX();
                            ux = uy = 0.;
                        } else {
                            u = uvPix[uComp];
                            ux = (uvPix_xn && uvPix_xp) ? ((uvPix_xn[uComp] - uvPix_xp[uComp]) / 2.) : 0.;
                            uy = (uvPix_yn && uvPix_yp) ? ((uvPix_yn[uComp] - uvPix_yp[uComp]) / 2.) : 0.;
                        }
                        if (!vImg) {
                            v = vComp;
                            vx = vy = 0.;
                        } else if (!uvPix) {
                            v = PIX();
                            vx = vy = 0.;
                        } else {
                            v = uvPix[vComp];
                            vx = (uvPix_xn && uvPix_xp) ? ((uvPix_xn[vComp] - uvPix_xp[vComp]) / 2.) : 0.;
                            vy = (uvPix_yn && uvPix_yp) ? ((uvPix_yn[vComp] - uvPix_yp[vComp]) / 2.) : 0.;
                        }
                        u = (u - _uOffset) * _uScale;
                        ux *= _uScale;
                        uy *= _uScale;
                        v = (v - _vOffset) * _vScale;
                        vx *= _vScale;
                        vy *= _vScale;
                        switch (plugin) {
                            case eDistortionPluginSTMap:
                                // wrap u and v
                                u = wrap(u, _uWrap);
                                v = wrap(v, _vWrap);
                                sx = srcx1 + u * (srcx2 - srcx1);
                                sy = srcy1 + v * (srcy2 - srcy1); // 0,0 corresponds to the lower left corner of the first pixel
                                // scale gradients by (srcx2 - srcx1)
                                if (filter != eFilterImpulse) {
                                    sxx = ux * (srcx2 - srcx1);
                                    sxy = uy * (srcx2 - srcx1);
                                    syx = vx * (srcy2 - srcy1);
                                    syy = vy * (srcy2 - srcy1);
                                }
                                break;
                            case eDistortionPluginIDistort:
                                // 0,0 corresponds to the lower left corner of the first pixel, so we have to add 0.5
                                // (x,y) = (0,0) and (u,v) = (0,0) means to pick color at (0.5,0.5)
                                sx = x + u + 0.5;
                                sy = y + v + 0.5;
                                if (filter != eFilterImpulse) {
                                    sxx = 1 + ux;
                                    sxy = uy;
                                    syx = vx;
                                    syy = 1 + vy;
                                }
                                break;
                            default:
                                assert(false);
                                break;
                        }
                    }
                        break;
                    case eDistortionPluginLensDistortion: {
                        switch (_distortionModel) {
                            case eDistortionModelNuke: {
                                double xu = (x + 0.5 - (srcx2+srcx1)/2.)/f;
                                double yu = (y + 0.5 - (srcy2+srcy1)/2.)/f;
                                distort_nuke(xu, yu,
                                             _k1, _k2, _cx, _cy, _squeeze, _ax, _ay,
                                             &sx, &sy);
                            }
                                break;
                        }
                        sx *= f;
                        sx += (srcx2+srcx1)/2.;
                        sy *= f;
                        sy += (srcy2+srcy1)/2.;
                        sxx = 1; // TODO: Jacobian
                        sxy = 0;
                        syx = 0;
                        syy = 1;
                    }
                        break;
                }
                double Jxx = 0., Jxy = 0., Jyx = 0., Jyy = 0.;
                if (_transformIsIdentity) {
                    if (filter != eFilterImpulse) {
                        Jxx = sxx;
                        Jxy = sxy;
                        Jyx = syx;
                        Jyy = syy;
                    }
                } else {
                    const OFX::Matrix3x3 & H = _srcTransformInverse;
                    double transformedx = H.a*sx + H.b*sy + H.c;
                    double transformedy = H.d*sx + H.e*sy + H.f;
                    double transformedz = H.g*sx + H.h*sy + H.i;
                    if (transformedz == 0) {
                        sx = sy = std::numeric_limits<double>::infinity();
                    } else {
                        sx = transformedx/transformedz;
                        sy = transformedy/transformedz;
                        if (filter != eFilterImpulse) {
                            Jxx = (H.a*transformedz - transformedx*H.g)/(transformedz*transformedz);
                            Jxy = (H.b*transformedz - transformedx*H.h)/(transformedz*transformedz);
                            Jyx = (H.d*transformedz - transformedy*H.g)/(transformedz*transformedz);
                            Jyy = (H.e*transformedz - transformedy*H.h)/(transformedz*transformedz);
                        }
                    }
                }

                // TODO: ofxsFilterInterpolate2DSuper
                if (filter == eFilterImpulse) {
                    ofxsFilterInterpolate2D<PIX,nComponents,filter,clamp>(sx, sy, _srcImg, _blackOutside, tmpPix);
                } else {
                    ofxsFilterInterpolate2DSuper<PIX,nComponents,filter,clamp>(sx, sy, Jxx, Jxy, Jyx, Jyy, _srcImg, _blackOutside, tmpPix);
                }
                ofxsMaskMix<PIX, nComponents, maxValue, true>(tmpPix, x, y, _srcImg, _doMasking, _maskImg, (float)_mix, _maskInvert, dstPix);
                // copy back original values from unprocessed channels
                if (nComponents == 1) {
                    if (!_processA) {
                        const PIX *srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                } else if (nComponents == 3 || nComponents == 4) {
                    const PIX *srcPix = 0;
                    if (!_processR || !_processG || !_processB || (!_processA && nComponents == 4)) {
                        srcPix = (const PIX *)  (_srcImg ? _srcImg->getPixelAddress(x, y) : 0);
                    }
                    if (!_processR) {
                        dstPix[0] = srcPix ? srcPix[0] : PIX();
                    }
                    if (!_processG) {
                        dstPix[1] = srcPix ? srcPix[1] : PIX();
                    }
                    if (!_processB) {
                        dstPix[2] = srcPix ? srcPix[2] : PIX();
                    }
                    if (!_processA && nComponents == 4) {
                        dstPix[3] = srcPix ? srcPix[3] : PIX();
                    }
                }
                // increment the dst pixel
                dstPix += nComponents;
            }
        }
    }
};


////////////////////////////////////////////////////////////////////////////////
/** @brief The plugin that does our work */
class DistortionPlugin : public OFX::ImageEffect
{
public:
    /** @brief ctor */
    DistortionPlugin(OfxImageEffectHandle handle, DistortionPluginEnum plugin)
    : ImageEffect(handle)
    , _dstClip(0)
    , _srcClip(0)
    , _uvClip(0)
    , _maskClip(0)
    , _processR(0)
    , _processG(0)
    , _processB(0)
    , _processA(0)
    , _uChannel(0)
    , _vChannel(0)
    , _uvOffset(0)
    , _uvScale(0)
    , _uWrap(0)
    , _vWrap(0)
    , _distortionModel(0)
    , _k1(0)
    , _k2(0)
    , _k3(0)
    , _p1(0)
    , _p2(0)
    , _center(0)
    , _squeeze(0)
    , _asymmetric(0)
    , _filter(0)
    , _clamp(0)
    , _blackOutside(0)
    , _mix(0)
    , _maskInvert(0)
    , _plugin(plugin)
    {
        _dstClip = fetchClip(kOfxImageEffectOutputClipName);
        assert(_dstClip && (_dstClip->getPixelComponents() == ePixelComponentRGB || _dstClip->getPixelComponents() == ePixelComponentRGBA || _dstClip->getPixelComponents() == ePixelComponentAlpha));
        _srcClip = fetchClip(kOfxImageEffectSimpleSourceClipName);
        assert(_srcClip && (_srcClip->getPixelComponents() == ePixelComponentRGB || _srcClip->getPixelComponents() == ePixelComponentRGBA|| _srcClip->getPixelComponents() == ePixelComponentAlpha));
        if (_plugin == eDistortionPluginIDistort || _plugin == eDistortionPluginSTMap) {
            _uvClip = fetchClip(kClipUV);
            assert(_uvClip && (_uvClip->getPixelComponents() == ePixelComponentRGB || _uvClip->getPixelComponents() == ePixelComponentRGBA || _uvClip->getPixelComponents() == ePixelComponentAlpha));
        }
        _maskClip = getContext() == OFX::eContextFilter ? NULL : fetchClip(getContext() == OFX::eContextPaint ? "Brush" : "Mask");
        assert(!_maskClip || _maskClip->getPixelComponents() == ePixelComponentAlpha);
        _processR = fetchBooleanParam(kParamProcessR);
        _processG = fetchBooleanParam(kParamProcessG);
        _processB = fetchBooleanParam(kParamProcessB);
        _processA = fetchBooleanParam(kParamProcessA);
        assert(_processR && _processG && _processB && _processA);
        if (plugin == eDistortionPluginIDistort || plugin == eDistortionPluginSTMap) {
            _uChannel = fetchChoiceParam(kParamChannelU);
            _vChannel = fetchChoiceParam(kParamChannelV);
            _uvOffset = fetchDouble2DParam(kParamUVOffset);
            _uvScale = fetchDouble2DParam(kParamUVScale);
            assert(_uChannel && _vChannel && _uvOffset && _uvScale);
            if (plugin == eDistortionPluginSTMap) {
                _uWrap = fetchChoiceParam(kParamWrapU);
                _vWrap = fetchChoiceParam(kParamWrapV);
                assert(_uWrap && _vWrap);
            }
        }
        if (_plugin == eDistortionPluginLensDistortion) {
            _distortionModel = fetchChoiceParam(kParamDistortionModel);
            _k1 = fetchDoubleParam(kParamK1);
            _k2 = fetchDoubleParam(kParamK2);
            _k3 = fetchDoubleParam(kParamK3);
            _p1 = fetchDoubleParam(kParamP1);
            _p2 = fetchDoubleParam(kParamP2);
            _center = fetchDouble2DParam(kParamCenter);
            _squeeze = fetchDoubleParam(kParamSqueeze);
            _asymmetric = fetchDouble2DParam(kParamAsymmetric);
            assert(_k1 && _k2 && _k3 && _p1 && _p2 && _center && _squeeze && _asymmetric);
        }
        _filter = fetchChoiceParam(kParamFilterType);
        _clamp = fetchBooleanParam(kParamFilterClamp);
        _blackOutside = fetchBooleanParam(kParamFilterBlackOutside);
        assert(_filter && _clamp && _blackOutside);
        _mix = fetchDoubleParam(kParamMix);
        _maskInvert = fetchBooleanParam(kParamMaskInvert);
        assert(_mix && _maskInvert);

        updateVisibility();
    }

private:
    // override the roi call
    virtual void getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args, OFX::RegionOfInterestSetter &rois) OVERRIDE FINAL;

    virtual bool getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod) OVERRIDE FINAL;

    /* Override the render */
    virtual void render(const OFX::RenderArguments &args) OVERRIDE FINAL;

    /* internal render function */
    template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin>
    void renderInternalForBitDepth(const OFX::RenderArguments &args);

    template <int nComponents, DistortionPluginEnum plugin>
    void renderInternal(const OFX::RenderArguments &args, OFX::BitDepthEnum dstBitDepth);
    
    /* set up and run a processor */
    void setupAndProcess(DistortionProcessorBase &, const OFX::RenderArguments &args);

    virtual bool isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &identityTime) OVERRIDE FINAL;

    /** @brief called when a param has just had its value changed */
    void changedParam(const InstanceChangedArgs &args, const std::string &paramName)
    {
        if (_plugin == eDistortionPluginLensDistortion) {
            if (paramName == kParamDistortionModel && args.reason == eChangeUserEdit) {
                updateVisibility();
            }
        }
    }

    void updateVisibility()
    {
        if (_plugin == eDistortionPluginLensDistortion) {
            int distortionModel_i;
            _distortionModel->getValue(distortionModel_i);
            DistortionModelEnum distortionModel = (DistortionModelEnum)distortionModel_i;
            switch (distortionModel) {
                case eDistortionModelNuke:
                    _k1->setIsSecret(false);
                    _k2->setIsSecret(false);
                    _k3->setIsSecret(true);
                    _p1->setIsSecret(true);
                    _p2->setIsSecret(true);
                    _center->setIsSecret(false);
                    _squeeze->setIsSecret(false);
                    _asymmetric->setIsSecret(false);
                    break;
            }
        }
    }
private:
    // do not need to delete these, the ImageEffect is managing them for us
    OFX::Clip *_dstClip;
    OFX::Clip *_srcClip;
    OFX::Clip *_uvClip;
    OFX::Clip *_maskClip;
    OFX::BooleanParam* _processR;
    OFX::BooleanParam* _processG;
    OFX::BooleanParam* _processB;
    OFX::BooleanParam* _processA;
    OFX::ChoiceParam* _uChannel;
    OFX::ChoiceParam* _vChannel;
    OFX::Double2DParam *_uvOffset;
    OFX::Double2DParam *_uvScale;
    OFX::ChoiceParam* _uWrap;
    OFX::ChoiceParam* _vWrap;
    OFX::ChoiceParam* _distortionModel;
    OFX::DoubleParam* _k1;
    OFX::DoubleParam* _k2;
    OFX::DoubleParam* _k3;
    OFX::DoubleParam* _p1;
    OFX::DoubleParam* _p2;
    OFX::Double2DParam* _center;
    OFX::DoubleParam* _squeeze;
    OFX::Double2DParam* _asymmetric;
    OFX::ChoiceParam* _filter;
    OFX::BooleanParam* _clamp;
    OFX::BooleanParam* _blackOutside;
    OFX::DoubleParam* _mix;
    OFX::BooleanParam* _maskInvert;
    DistortionPluginEnum _plugin;
};


////////////////////////////////////////////////////////////////////////////////
/** @brief render for the filter */

////////////////////////////////////////////////////////////////////////////////
// basic plugin render function, just a skelington to instantiate templates from

/* set up and run a processor */
void
DistortionPlugin::setupAndProcess(DistortionProcessorBase &processor, const OFX::RenderArguments &args)
{
    const double time = args.time;
    std::auto_ptr<OFX::Image> dst(_dstClip->fetchImage(time));
    if (!dst.get()) {
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    OFX::BitDepthEnum         dstBitDepth    = dst->getPixelDepth();
    OFX::PixelComponentEnum   dstComponents  = dst->getPixelComponents();
    if (dstBitDepth != _dstClip->getPixelDepth() ||
        dstComponents != _dstClip->getPixelComponents()) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong depth or components");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    if (dst->getRenderScale().x != args.renderScale.x ||
        dst->getRenderScale().y != args.renderScale.y ||
        (dst->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && dst->getField() != args.fieldToRender)) {
        setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
        OFX::throwSuiteStatusException(kOfxStatFailed);
    }
    std::auto_ptr<const OFX::Image> src((_srcClip && _srcClip->isConnected()) ?
                                        _srcClip->fetchImage(time) : 0);
    if (src.get()) {
        if (src->getRenderScale().x != args.renderScale.x ||
            src->getRenderScale().y != args.renderScale.y ||
            (src->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && src->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    srcBitDepth      = src->getPixelDepth();
        OFX::PixelComponentEnum srcComponents = src->getPixelComponents();
        if (srcBitDepth != dstBitDepth || srcComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }

    std::auto_ptr<const OFX::Image> uv((_uvClip && _uvClip->isConnected()) ?
                                        _uvClip->fetchImage(time) : 0);
    if (uv.get()) {
        if (uv->getRenderScale().x != args.renderScale.x ||
            uv->getRenderScale().y != args.renderScale.y ||
            (uv->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && uv->getField() != args.fieldToRender)) {
            setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
            OFX::throwSuiteStatusException(kOfxStatFailed);
        }
        OFX::BitDepthEnum    uvBitDepth      = uv->getPixelDepth();
        OFX::PixelComponentEnum uvComponents = uv->getPixelComponents();
        if (uvBitDepth != dstBitDepth || uvComponents != dstComponents) {
            OFX::throwSuiteStatusException(kOfxStatErrImageFormat);
        }
    }
    std::auto_ptr<const OFX::Image> mask((getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) ?
                                         _maskClip->fetchImage(time) : 0);
    // do we do masking
    if (getContext() != OFX::eContextFilter && _maskClip && _maskClip->isConnected()) {
        if (mask.get()) {
            if (mask->getRenderScale().x != args.renderScale.x ||
                mask->getRenderScale().y != args.renderScale.y ||
                (mask->getField() != OFX::eFieldNone /* for DaVinci Resolve */ && mask->getField() != args.fieldToRender)) {
                setPersistentMessage(OFX::Message::eMessageError, "", "OFX Host gave image with wrong scale or field properties");
                OFX::throwSuiteStatusException(kOfxStatFailed);
            }
        }
        bool maskInvert;
        _maskInvert->getValueAtTime(time, maskInvert);
        processor.doMasking(true);
        processor.setMaskImg(mask.get(), maskInvert);
    }

    // set the images
    processor.setDstImg(dst.get());
    processor.setSrcImgs(src.get(), uv.get());
    // set the render window
    processor.setRenderWindow(args.renderWindow);

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);
    InputChannelEnum uChannel = eInputChannelR;
    InputChannelEnum vChannel = eInputChannelG;
    double uScale = 1., vScale = 1.;
    double uOffset = 0., vOffset = 0.;
    WrapEnum uWrap = eWrapClamp;
    WrapEnum vWrap = eWrapClamp;
    if (_plugin == eDistortionPluginIDistort || _plugin == eDistortionPluginSTMap) {
        int uChannel_i, vChannel_i;
        _uChannel->getValueAtTime(time, uChannel_i);
        _vChannel->getValueAtTime(time, vChannel_i);
        uChannel = (InputChannelEnum)uChannel_i;
        vChannel = (InputChannelEnum)vChannel_i;
        _uvOffset->getValueAtTime(time, uOffset, vOffset);
        _uvScale->getValueAtTime(time, uScale, vScale);
        if (_plugin == eDistortionPluginSTMap) {
            int uWrap_i = 0;
            int vWrap_i = 0;
            _uWrap->getValueAtTime(time, uWrap_i);
            uWrap = (WrapEnum)uWrap_i;
            _vWrap->getValueAtTime(time, vWrap_i);
            vWrap = (WrapEnum)vWrap_i;
        }
    }
    bool blackOutside;
    _blackOutside->getValueAtTime(time, blackOutside);
    double mix;
    _mix->getValueAtTime(time, mix);

    bool transformIsIdentity = src.get() ? src->getTransformIsIdentity() : true;
    OFX::Matrix3x3 srcTransformInverse;
    if (!transformIsIdentity) {
        double srcTransform[9]; // transform to apply to the source image, in pixel coordinates, from source to destination
        src->getTransform(srcTransform);
        OFX::Matrix3x3 srcTransformMat;
        srcTransformMat.a = srcTransform[0];
        srcTransformMat.b = srcTransform[1];
        srcTransformMat.c = srcTransform[2];
        srcTransformMat.d = srcTransform[3];
        srcTransformMat.e = srcTransform[4];
        srcTransformMat.f = srcTransform[5];
        srcTransformMat.g = srcTransform[6];
        srcTransformMat.h = srcTransform[7];
        srcTransformMat.i = srcTransform[8];
        // invert it
        double det = ofxsMatDeterminant(srcTransformMat);
        if (det != 0.) {
            srcTransformInverse = ofxsMatInverse(srcTransformMat, det);
        } else {
            transformIsIdentity = true; // no transform
        }
    }
    if (_plugin == eDistortionPluginIDistort) {
        // in IDistort, displacement is given in full-scale pixels
        uScale *= args.renderScale.x;
        vScale *= args.renderScale.y;
    }
    DistortionModelEnum distortionModel = eDistortionModelNuke;
    double k1 = 0., k2 = 0., k3 = 0., p1 = 0., p2 = 0., cx = 0., cy = 0., squeeze = 1., ax = 0., ay = 0.;
    if (_plugin == eDistortionPluginLensDistortion) {
        int distortionModel_i;
        _distortionModel->getValue(distortionModel_i);
        distortionModel = (DistortionModelEnum)distortionModel_i;
        switch (distortionModel) {
            case eDistortionModelNuke:
                _k1->getValueAtTime(time, k1);
                _k2->getValueAtTime(time, k2);
                //_k3->getValueAtTime(time, k3);
                //_p1->getValueAtTime(time, p1);
                //_p2->getValueAtTime(time, p2);
                _center->getValueAtTime(time, cx, cy);
                _squeeze->getValueAtTime(time, squeeze);
                _asymmetric->getValueAtTime(time, ax, ay);
                break;
        }

    }
    processor.setValues(processR, processG, processB, processA,
                        transformIsIdentity, srcTransformInverse,
                        uChannel, vChannel,
                        uOffset, vOffset,
                        uScale, vScale,
                        uWrap, vWrap,
                        distortionModel, k1, k2, k3, p1, p2, cx, cy, squeeze, ax, ay,
                        blackOutside, mix);

    // Call the base class process member, this will call the derived templated process code
    processor.process();
}

template <class PIX, int nComponents, int maxValue, DistortionPluginEnum plugin>
void
DistortionPlugin::renderInternalForBitDepth(const OFX::RenderArguments &args)
{
    const double time = args.time;
    int filter = eFilterCubic;
    if (_filter) {
        _filter->getValueAtTime(time, filter);
    }
    bool clamp = false;
    if (_clamp) {
        _clamp->getValueAtTime(time, clamp);
    }

    // as you may see below, some filters don't need explicit clamping, since they are
    // "clamped" by construction.
    switch ( (FilterEnum)filter ) {
        case eFilterImpulse: {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterImpulse, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterBilinear: {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterBilinear, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterCubic: {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterCubic, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterKeys:
            if (clamp) {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterKeys, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterKeys, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterSimon:
            if (clamp) {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterSimon, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterSimon, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterRifman:
            if (clamp) {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterRifman, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterRifman, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterMitchell:
            if (clamp) {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterMitchell, true> fred(*this);
                setupAndProcess(fred, args);
            } else {
                DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterMitchell, false> fred(*this);
                setupAndProcess(fred, args);
            }
            break;
        case eFilterParzen: {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterParzen, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
        case eFilterNotch: {
            DistortionProcessor<PIX, nComponents, maxValue, plugin, eFilterNotch, false> fred(*this);
            setupAndProcess(fred, args);
            break;
        }
    } // switch
} // renderInternalForBitDepth

// the internal render function
template <int nComponents, DistortionPluginEnum plugin>
void
DistortionPlugin::renderInternal(const OFX::RenderArguments &args,
                                   OFX::BitDepthEnum dstBitDepth)
{
    switch (dstBitDepth) {
        case OFX::eBitDepthUByte:
            renderInternalForBitDepth<unsigned char, nComponents, 255, plugin>(args);
            break;
        case OFX::eBitDepthUShort:
            renderInternalForBitDepth<unsigned short, nComponents, 65535, plugin>(args);
            break;
        case OFX::eBitDepthFloat:
            renderInternalForBitDepth<float, nComponents, 1, plugin>(args);
            break;
        default:
            OFX::throwSuiteStatusException(kOfxStatErrUnsupported);
    }
}

// the overridden render function
void
DistortionPlugin::render(const OFX::RenderArguments &args)
{

    // instantiate the render code based on the pixel depth of the dst clip
    OFX::BitDepthEnum       dstBitDepth    = _dstClip->getPixelDepth();
    OFX::PixelComponentEnum dstComponents  = _dstClip->getPixelComponents();

    assert(kSupportsMultipleClipPARs   || !_srcClip || _srcClip->getPixelAspectRatio() == _dstClip->getPixelAspectRatio());
    assert(kSupportsMultipleClipDepths || !_srcClip || _srcClip->getPixelDepth()       == _dstClip->getPixelDepth());
    assert(dstComponents == OFX::ePixelComponentAlpha || dstComponents == OFX::ePixelComponentRGB || dstComponents == OFX::ePixelComponentRGBA);
    if (dstComponents == OFX::ePixelComponentRGBA) {
        switch (_plugin) {
            case eDistortionPluginSTMap:
                renderInternal<4, eDistortionPluginSTMap>(args, dstBitDepth);
                break;
            case eDistortionPluginIDistort:
                renderInternal<4, eDistortionPluginIDistort>(args, dstBitDepth);
                break;
            case eDistortionPluginLensDistortion:
                renderInternal<4, eDistortionPluginLensDistortion>(args, dstBitDepth);
                break;
        }
    } else if (dstComponents == OFX::ePixelComponentRGB) {
        switch (_plugin) {
            case eDistortionPluginSTMap:
                renderInternal<3, eDistortionPluginSTMap>(args, dstBitDepth);
                break;
            case eDistortionPluginIDistort:
                renderInternal<3, eDistortionPluginIDistort>(args, dstBitDepth);
                break;
            case eDistortionPluginLensDistortion:
                renderInternal<3, eDistortionPluginLensDistortion>(args, dstBitDepth);
                break;
        }
    } else {
        assert(dstComponents == OFX::ePixelComponentAlpha);
        switch (_plugin) {
            case eDistortionPluginSTMap:
                renderInternal<1, eDistortionPluginSTMap>(args, dstBitDepth);
                break;
            case eDistortionPluginIDistort:
                renderInternal<1, eDistortionPluginIDistort>(args, dstBitDepth);
                break;
            case eDistortionPluginLensDistortion:
                renderInternal<1, eDistortionPluginLensDistortion>(args, dstBitDepth);
                break;
        }
    }
}


bool
DistortionPlugin::isIdentity(const IsIdentityArguments &args, Clip * &identityClip, double &/*identityTime*/)
{
    const double time = args.time;
    if (_plugin == eDistortionPluginIDistort || _plugin == eDistortionPluginSTMap) {
        if (!_uvClip || !_uvClip->isConnected()) {
            identityClip = _srcClip;
            return true;
        }
    }

    double mix;
    _mix->getValueAtTime(time, mix);

    if (mix == 0. /*|| (!processR && !processG && !processB && !processA)*/) {
        identityClip = _srcClip;
        return true;
    }

    bool processR, processG, processB, processA;
    _processR->getValueAtTime(time, processR);
    _processG->getValueAtTime(time, processG);
    _processB->getValueAtTime(time, processB);
    _processA->getValueAtTime(time, processA);
    if (!processR && !processG && !processB && !processA) {
        identityClip = _srcClip;
        return true;
    }

    return false;
}

// override the roi call
// Required if the plugin requires a region from the inputs which is different from the rendered region of the output.
// (this is the case here)
void
DistortionPlugin::getRegionsOfInterest(const OFX::RegionsOfInterestArguments &args,
                                     OFX::RegionOfInterestSetter &rois)
{
    const double time = args.time;
    if (!_srcClip) {
        return;
    }
    // ask for full RoD of srcClip
    const OfxRectD& srcRod = _srcClip->getRegionOfDefinition(time);
    rois.setRegionOfInterest(*_srcClip, srcRod);
    // only ask for the renderWindow (intersected with the RoD) from uvClip
    if (_uvClip) {
        OfxRectD uvRoI = _uvClip->getRegionOfDefinition(time);
        MergeImages2D::rectIntersection(uvRoI, args.regionOfInterest, &uvRoI);
        rois.setRegionOfInterest(*_uvClip, uvRoI);
    }
}


bool
DistortionPlugin::getRegionOfDefinition(const OFX::RegionOfDefinitionArguments &args, OfxRectD &rod)
{
    const double time = args.time;
    switch (_plugin) {
        case eDistortionPluginSTMap:
            if (_uvClip) {
                // IDistort: RoD is the same as uv map
                rod = _uvClip->getRegionOfDefinition(time);
                return true;
            }
            break;
        case eDistortionPluginIDistort:
            if (_srcClip) {
                // IDistort: RoD is the same as srcClip
                rod = _srcClip->getRegionOfDefinition(time);
                return true;
            }
            break;
        case eDistortionPluginLensDistortion:
            return false; // use source RoD
            break;
    }
    return false;
}

//mDeclarePluginFactory(DistortionPluginFactory, {}, {});
template<DistortionPluginEnum plugin>
class DistortionPluginFactory : public OFX::PluginFactoryHelper<DistortionPluginFactory<plugin> >
{
public:
    DistortionPluginFactory<plugin>(const std::string& id, unsigned int verMaj, unsigned int verMin):OFX::PluginFactoryHelper<DistortionPluginFactory>(id, verMaj, verMin){}
    virtual void describe(OFX::ImageEffectDescriptor &desc);
    virtual void describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context);
    virtual OFX::ImageEffect* createInstance(OfxImageEffectHandle handle, OFX::ContextEnum context);
};

template<DistortionPluginEnum plugin>
void DistortionPluginFactory<plugin>::describe(OFX::ImageEffectDescriptor &desc)
{
    // basic labels
    switch (plugin) {
        case eDistortionPluginSTMap:
            desc.setLabel(kPluginSTMapName);
            desc.setPluginGrouping(kPluginSTMapGrouping);
            desc.setPluginDescription(kPluginSTMapDescription);
            break;
        case eDistortionPluginIDistort:
            desc.setLabel(kPluginIDistortName);
            desc.setPluginGrouping(kPluginIDistortGrouping);
            desc.setPluginDescription(kPluginIDistortDescription);
            break;
        case eDistortionPluginLensDistortion:
            desc.setLabel(kPluginLensDistortionName);
            desc.setPluginGrouping(kPluginLensDistortionGrouping);
            desc.setPluginDescription(kPluginLensDistortionDescription);
            break;
    }

    //desc.addSupportedContext(eContextFilter);
    desc.addSupportedContext(eContextGeneral);
    //desc.addSupportedContext(eContextPaint);
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
}

static void
addInputChannelOptions(ChoiceParamDescriptor* channel, InputChannelEnum def)
{
    assert(channel->getNOptions() == eInputChannelR);
    channel->appendOption(kParamChannelOptionR, kParamChannelOptionRHint);
    assert(channel->getNOptions() == eInputChannelG);
    channel->appendOption(kParamChannelOptionG, kParamChannelOptionGHint);
    assert(channel->getNOptions() == eInputChannelB);
    channel->appendOption(kParamChannelOptionB, kParamChannelOptionBHint);
    assert(channel->getNOptions() == eInputChannelA);
    channel->appendOption(kParamChannelOptionA, kParamChannelOptionAHint);
    assert(channel->getNOptions() == eInputChannel0);
    channel->appendOption(kParamChannelOption0, kParamChannelOption0Hint);
    assert(channel->getNOptions() == eInputChannel1);
    channel->appendOption(kParamChannelOption1, kParamChannelOption1Hint);
    channel->setDefault(def);
}

static void
addWrapOptions(ChoiceParamDescriptor* channel, WrapEnum def)
{
    assert(channel->getNOptions() == eWrapClamp);
    channel->appendOption(kParamWrapOptionClamp, kParamWrapOptionClampHint);
    assert(channel->getNOptions() == eWrapRepeat);
    channel->appendOption(kParamWrapOptionRepeat, kParamWrapOptionRepeatHint);
    assert(channel->getNOptions() == eWrapMirror);
    channel->appendOption(kParamWrapOptionMirror, kParamWrapOptionMirrorHint);
    channel->setDefault(def);
}

template<DistortionPluginEnum plugin>
void DistortionPluginFactory<plugin>::describeInContext(OFX::ImageEffectDescriptor &desc, OFX::ContextEnum context)
{
    if (plugin ==  eDistortionPluginSTMap) {
        // create the uv clip
        // uv clip is defined first, because the output format is taken from the RoD of the first clip in Nuke
        ClipDescriptor *uvClip = desc.defineClip(kClipUV);
        uvClip->addSupportedComponent(ePixelComponentRGBA);
        uvClip->addSupportedComponent(ePixelComponentRGB);
        uvClip->addSupportedComponent(ePixelComponentAlpha);
        uvClip->setTemporalClipAccess(false);
        uvClip->setSupportsTiles(kSupportsTiles);
        uvClip->setIsMask(false);
    }
    // create the mandated source clip
    ClipDescriptor *srcClip = desc.defineClip(kOfxImageEffectSimpleSourceClipName);
    srcClip->addSupportedComponent(ePixelComponentRGBA);
    srcClip->addSupportedComponent(ePixelComponentRGB);
    srcClip->addSupportedComponent(ePixelComponentAlpha);
    srcClip->setTemporalClipAccess(false);
    srcClip->setSupportsTiles(kSupportsTiles);
    srcClip->setCanTransform(true); // we can concatenate transforms upwards on srcClip only
    srcClip->setIsMask(false);
    if (plugin == eDistortionPluginIDistort) {
        // create the uv clip
        ClipDescriptor *uvClip = desc.defineClip(kClipUV);
        uvClip->addSupportedComponent(ePixelComponentRGBA);
        uvClip->addSupportedComponent(ePixelComponentRGB);
        uvClip->addSupportedComponent(ePixelComponentAlpha);
        uvClip->setTemporalClipAccess(false);
        uvClip->setSupportsTiles(kSupportsTiles);
        uvClip->setIsMask(false);
    }

    // create the mandated output clip
    ClipDescriptor *dstClip = desc.defineClip(kOfxImageEffectOutputClipName);
    dstClip->addSupportedComponent(ePixelComponentRGBA);
    dstClip->addSupportedComponent(ePixelComponentRGB);
    dstClip->addSupportedComponent(ePixelComponentAlpha);
    dstClip->setSupportsTiles(kSupportsTiles);

    if (context == eContextGeneral || context == eContextPaint) {
        ClipDescriptor *maskClip = context == eContextGeneral ? desc.defineClip("Mask") : desc.defineClip("Brush");
        maskClip->addSupportedComponent(ePixelComponentAlpha);
        maskClip->setTemporalClipAccess(false);
        if (context == eContextGeneral)
            maskClip->setOptional(true);
        maskClip->setSupportsTiles(kSupportsTiles);
        maskClip->setIsMask(true);
    }

    // make some pages and to things in
    PageParamDescriptor *page = desc.definePageParam("Controls");

    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessR);
        param->setLabel(kParamProcessRLabel);
        param->setHint(kParamProcessRHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessG);
        param->setLabel(kParamProcessGLabel);
        param->setHint(kParamProcessGHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessB);
        param->setLabel(kParamProcessBLabel);
        param->setHint(kParamProcessBHint);
        param->setDefault(true);
        param->setLayoutHint(eLayoutHintNoNewLine);
        if (page) {
            page->addChild(*param);
        }
    }
    {
        OFX::BooleanParamDescriptor* param = desc.defineBooleanParam(kParamProcessA);
        param->setLabel(kParamProcessALabel);
        param->setHint(kParamProcessAHint);
        param->setDefault(true);
        if (page) {
            page->addChild(*param);
        }
    }

    if (plugin == eDistortionPluginIDistort ||
        plugin == eDistortionPluginSTMap) {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelU);
            param->setLabel(kParamChannelULabel);
            param->setHint(kParamChannelUHint);
            param->setLayoutHint(eLayoutHintNoNewLine);
            addInputChannelOptions(param, eInputChannelR);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamChannelV);
            param->setLabel(kParamChannelVLabel);
            param->setHint(kParamChannelVHint);
            addInputChannelOptions(param, eInputChannelG);
            if (page) {
                page->addChild(*param);
            }
        }

        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVOffset);
            param->setLabel(kParamUVOffsetLabel);
            param->setHint(kParamUVOffsetHint);
            param->setDefault(0., 0.);
            param->setDisplayRange(0., 0., 1., 1.);
            param->setDimensionLabels("U", "V");
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamUVScale);
            param->setLabel(kParamUVScaleLabel);
            param->setHint(kParamUVScaleHint);
            param->setDoubleType(eDoubleTypeScale);
            param->setDefault(1., 1.);
            param->setDisplayRange(0., 0., 100., 100.);
            param->setDimensionLabels("U", "V");
            if (page) {
                page->addChild(*param);
            }
        }

        if (plugin == eDistortionPluginSTMap) {
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamWrapU);
                param->setLabel(kParamWrapULabel);
                param->setHint(kParamWrapUHint);
                param->setLayoutHint(eLayoutHintNoNewLine);
                addWrapOptions(param, eWrapClamp);
                if (page) {
                    page->addChild(*param);
                }
            }
            {
                ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamWrapV);
                param->setLabel(kParamWrapVLabel);
                param->setHint(kParamWrapVHint);
                addWrapOptions(param, eWrapClamp);
                if (page) {
                    page->addChild(*param);
                }
            }
        }
    }

    if (plugin == eDistortionPluginLensDistortion) {
        {
            ChoiceParamDescriptor *param = desc.defineChoiceParam(kParamDistortionModel);
            param->setLabel(kParamDistortionModelLabel);
            param->setHint(kParamDistortionModelHint);
            assert(param->getNOptions() == eDistortionModelNuke);
            param->appendOption(kParamDistortionModelOptionNuke, kParamDistortionModelOptionNukeHint);
            if (page) {
                page->addChild(*param);
            }

        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK1);
            param->setLabel(kParamK1Label);
            param->setHint(kParamK1Hint);
            param->setDisplayRange(-0.3, 0.3);
            //param->setLayoutHint(eLayoutHintNoNewLine);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK2);
            param->setLabel(kParamK2Label);
            param->setHint(kParamK2Hint);
            param->setDisplayRange(-0.1, 0.1);
            //param->setLayoutHint(eLayoutHintNoNewLine);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamK3);
            param->setLabel(kParamK3Label);
            param->setHint(kParamK3Hint);
            param->setDisplayRange(-0.1, 0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamP1);
            param->setLabel(kParamP1Label);
            param->setHint(kParamP1Hint);
            param->setDisplayRange(-0.1, 0.1);
            //param->setLayoutHint(eLayoutHintNoNewLine);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamP2);
            param->setLabel(kParamP2Label);
            param->setHint(kParamP2Hint);
            param->setDisplayRange(-0.1, 0.1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamCenter);
            param->setLabel(kParamCenterLabel);
            param->setHint(kParamCenterHint);
            param->setDisplayRange(-1, -1, 1, 1);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            DoubleParamDescriptor *param = desc.defineDoubleParam(kParamSqueeze);
            param->setLabel(kParamSqueezeLabel);
            param->setHint(kParamSqueezeHint);
            param->setDefault(1.);
            param->setDisplayRange(0., 1.);
            if (page) {
                page->addChild(*param);
            }
        }
        {
            Double2DParamDescriptor *param = desc.defineDouble2DParam(kParamAsymmetric);
            param->setLabel(kParamAsymmetricLabel);
            param->setHint(kParamAsymmetricHint);
            param->setDisplayRange(-0.5, -0.5, 0.5, 0.5);
            if (page) {
                page->addChild(*param);
            }
        }


    }

    ofxsFilterDescribeParamsInterpolate2D(desc, page, (plugin == eDistortionPluginSTMap));
    ofxsMaskMixDescribeParams(desc, page);
}

template<DistortionPluginEnum plugin>
OFX::ImageEffect* DistortionPluginFactory<plugin>::createInstance(OfxImageEffectHandle handle, OFX::ContextEnum /*context*/)
{
    return new DistortionPlugin(handle, plugin);
}

void getDistortionPluginIDs(OFX::PluginFactoryArray &ids)
{
    {
        static DistortionPluginFactory<eDistortionPluginIDistort> p(kPluginIDistortIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static DistortionPluginFactory<eDistortionPluginSTMap> p(kPluginSTMapIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
    {
        static DistortionPluginFactory<eDistortionPluginLensDistortion> p(kPluginLensDistortionIdentifier, kPluginVersionMajor, kPluginVersionMinor);
        ids.push_back(&p);
    }
}
