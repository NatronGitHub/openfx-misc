/*
 OFX Masking/Mixing help functions

 Author: Frederic Devernay <frederic.devernay@inria.fr>

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
 */

#ifndef Misc_ofxsMaskMix_h
#define Misc_ofxsMaskMix_h

#include <ofxsImageEffect.h>

//#define kMaskParamName "Mask"
#define kMixParamName "mix"
#define kMixParamLabel "Mix"
#define kMixParamHint "Mix factor between the original and the transformed image"
#define kMaskInvertParamName "maskInvert"
#define kMaskInvertParamLabel "Invert Mask"
#define kMaskInvertParamHint "When checked, the effect is fully applied where the mask is 0"


inline
void
ofxsMaskMixDescribeParams(OFX::ImageEffectDescriptor &desc, OFX::PageParamDescriptor *page)
{
    // GENERIC (MASKED)
    //
    OFX::DoubleParamDescriptor* mix = desc.defineDoubleParam(kMixParamName);
    mix->setLabels(kMixParamLabel, kMixParamLabel, kMixParamLabel);
    mix->setHint(kMixParamHint);
    mix->setDefault(1.);
    mix->setRange(0.,1.);
    mix->setDisplayRange(0.,1.);
    page->addChild(*mix);
    OFX::BooleanParamDescriptor* maskInvert = desc.defineBooleanParam(kMaskInvertParamName);
    maskInvert->setLabels(kMaskInvertParamLabel, kMaskInvertParamLabel, kMaskInvertParamLabel);
    maskInvert->setHint(kMaskInvertParamHint);
    page->addChild(*maskInvert);
}

template <class T>
inline
T ofxsClamp(T v, int min, int max)
{
    if(v < T(min)) return T(min);
    if(v > T(max)) return T(max);
    return v;
}

template <int maxValue>
inline
float ofxsClampIfInt(float v, int min, int max)
{
    if (maxValue == 1) {
        return v;
    }
    return ofxsClamp(v, min, max);
}


template <class PIX, int nComponents, int maxValue, bool masked>
void
ofxsMaskMixPix(const float *tmpPix, //!< interpolated pixel
               int x, //!< coordinates for the pixel to be computed (PIXEL coordinates)
               int y,
               const PIX *srcPix, //!< the background image (the output is srcImg where maskImg=0, else it is tmpPix)
               bool domask, //!< apply the mask?
               const OFX::Image *maskImg, //!< the mask image (ignored if masked=false or domask=false)
               float mix, //!< mix factor between the output and bkImg
               bool maskInvert, //<! invert mask behavior
               PIX *dstPix) //!< destination pixel
{
    const PIX *maskPix = NULL;
    float maskScale = 1.;

    // are we doing masking
    if (!masked) {
        // no mask, no mix
        for (int c = 0; c < nComponents; ++c) {
            dstPix[c] = PIX(ofxsClampIfInt<maxValue>(tmpPix[c], 0, maxValue));
        }
    } else {
        if (domask && maskImg) {
            // we do, get the pixel from the mask
            maskPix = (const PIX *)maskImg->getPixelAddress(x, y);
            // figure the scale factor from that pixel
            if (maskPix == 0) {
                maskScale = 0.;
            } else {
                maskScale = *maskPix/float(maxValue);
                if (maskInvert) {
                    maskScale = 1. - maskScale;
                }
            }
        }
        float alpha = maskScale * mix;
        if (srcPix) {
            for (int c = 0; c < nComponents; ++c) {
                float v = tmpPix[c] * alpha + (1. - alpha) * srcPix[c];
                dstPix[c] = PIX(ofxsClampIfInt<maxValue>(v, 0, maxValue));
            }
        } else {
            for (int c = 0; c < nComponents; ++c) {
                float v = tmpPix[c] * alpha;
                dstPix[c] = PIX(ofxsClampIfInt<maxValue>(v, 0, maxValue));
            }
        }
    }
}

template <class PIX, int nComponents, int maxValue, bool masked>
void
ofxsMaskMix(const float *tmpPix, //!< interpolated pixel
            int x, //!< coordinates for the pixel to be computed (PIXEL coordinates)
            int y,
            const OFX::Image *srcImg, //!< the background image (the output is srcImg where maskImg=0, else it is tmpPix)
            bool domask, //!< apply the mask?
            const OFX::Image *maskImg, //!< the mask image (ignored if masked=false or domask=false)
            float mix, //!< mix factor between the output and bkImg
            bool maskInvert, //<! invert mask behavior
            PIX *dstPix) //!< destination pixel
{
    const PIX *srcPix = NULL;

    // are we doing masking/mixing? in this case, retrieve srcPix
    if (masked && srcImg) {
        if ((domask && maskImg) || mix != 1.) {
            srcPix = (const PIX *)srcImg->getPixelAddress(x, y);
        }
    }

    return ofxsMaskMixPix<PIX,nComponents,maxValue,masked>(tmpPix, x, y, srcPix, domask, maskImg, mix, maskInvert, dstPix);
}


#endif
