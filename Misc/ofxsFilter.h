/*
 OFX Filter/Interpolation help functions
 
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

#ifndef _OFXS_FILTER_H_
#define _OFXS_FILTER_H_

#include <cmath>
#include <cassert>

#include "ofxsImageEffect.h"

// GENERIC
#define kFilterTypeParamName "filter"
#define kFilterTypeParamLabel "Filter"
#define kFilterTypeParamHint "Filtering algorithm - some filters may produce values outside of the initial range (*) or modify the values even if there is no movement (+)."
#define kFilterClampParamName "clamp"
#define kFilterClampParamLabel "Clamp"
#define kFilterClampParamHint "Clamp filter output within the original range - useful to avoid negative values in mattes"
#define kFilterBlackOutsideParamName "black_outside"
#define kFilterBlackOutsideParamLabel "Black outside"
#define kFilterBlackOutsideParamHint "Fill the area outside the source image with black"

enum FilterEnum {
    eFilterImpulse,
    eFilterBilinear,
    eFilterCubic,
    eFilterKeys,
    eFilterSimon,
    eFilterRifman,
    eFilterMitchell,
    eFilterParzen,
    eFilterNotch,
};

#define kFilterImpulse "Impulse"
#define kFilterImpulseHint "(nearest neighbor / box) Use original values"
#define kFilterBilinear "Bilinear"
#define kFilterBilinearHint "(tent / triangle) Bilinear interpolation between original values"
#define kFilterCubic "Cubic"
#define kFilterCubicHint "(cubic spline) Some smoothing"
#define kFilterKeys "Keys"
#define kFilterKeysHint "(Catmull-Rom / Hermite spline) Some smoothing, plus minor sharpening (*)"
#define kFilterSimon "Simon"
#define kFilterSimonHint "Some smoothing, plus medium sharpening (*)"
#define kFilterRifman "Rifman"
#define kFilterRifmanHint "Some smoothing, plus significant sharpening (*)"
#define kFilterMitchell "Mitchell"
#define kFilterMitchellHint "Some smoothing, plus blurring to hide pixelation (*+)"
#define kFilterParzen "Parzen"
#define kFilterParzenHint "(cubic B-spline) Greatest smoothing of all filters (+)"
#define kFilterNotch "Notch"
#define kFilterNotchHint "Flat smoothing (which tends to hide moire' patterns) (+)"

inline
void
ofxsFilterDescribeParamsInterpolate2D(OFX::ImageEffectDescriptor &desc, OFX::PageParamDescriptor *page)
{
    // GENERIC PARAMETERS
    //
    OFX::ChoiceParamDescriptor* filter = desc.defineChoiceParam(kFilterTypeParamName);
    filter->setLabels(kFilterTypeParamLabel, kFilterTypeParamLabel, kFilterTypeParamLabel);
    filter->setHint(kFilterTypeParamHint);
    assert(filter->getNOptions() == eFilterImpulse);
    filter->appendOption(kFilterImpulse, kFilterImpulseHint);
    assert(filter->getNOptions() == eFilterBilinear);
    filter->appendOption(kFilterBilinear, kFilterBilinearHint);
    assert(filter->getNOptions() == eFilterCubic);
    filter->appendOption(kFilterCubic, kFilterCubicHint);
    assert(filter->getNOptions() == eFilterKeys);
    filter->appendOption(kFilterKeys, kFilterKeysHint);
    assert(filter->getNOptions() == eFilterSimon);
    filter->appendOption(kFilterSimon, kFilterSimonHint);
    assert(filter->getNOptions() == eFilterRifman);
    filter->appendOption(kFilterRifman, kFilterRifmanHint);
    assert(filter->getNOptions() == eFilterMitchell);
    filter->appendOption(kFilterMitchell, kFilterMitchellHint);
    assert(filter->getNOptions() == eFilterParzen);
    filter->appendOption(kFilterParzen, kFilterParzenHint);
    assert(filter->getNOptions() == eFilterNotch);
    filter->appendOption(kFilterNotch, kFilterNotchHint);
    filter->setDefault(eFilterCubic);
    filter->setAnimates(true);
    filter->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*filter);

    OFX::BooleanParamDescriptor* clamp = desc.defineBooleanParam(kFilterClampParamName);
    clamp->setLabels(kFilterClampParamLabel, kFilterClampParamLabel, kFilterClampParamLabel);
    clamp->setHint(kFilterClampParamHint);
    clamp->setDefault(false);
    clamp->setAnimates(true);
    clamp->setLayoutHint(OFX::eLayoutHintNoNewLine);
    page->addChild(*clamp);

    OFX::BooleanParamDescriptor* blackOutside = desc.defineBooleanParam(kFilterBlackOutsideParamName);
    blackOutside->setLabels(kFilterBlackOutsideParamLabel, kFilterBlackOutsideParamLabel, kFilterBlackOutsideParamLabel);
    blackOutside->setHint(kFilterBlackOutsideParamHint);
    blackOutside->setDefault(true);
    blackOutside->setAnimates(true);
    page->addChild(*blackOutside);
}


/*
 Maple code to compute the filters.

 # Mitchell, D. and A. Netravali, "Reconstruction Filters in Computer Graphics."
 # http://www.cs.utexas.edu/users/fussell/courses/cs384g/lectures/mitchell/Mitchell.pdf
 # Computer Graphics, Vol. 22, No. 4, pp. 221-228.
 # (B, C)
 # (1/3, 1/3) - Defaults recommended by Mitchell and Netravali
 # (1, 0) - Equivalent to the Cubic B-Spline
 # (0, 0.5) - Equivalent to the Catmull-Rom Spline
 # (0, C) - The family of Cardinal Cubic Splines
 # (B, 0) - Duff's tensioned B-Splines.
 unassign('Ip'):unassign('Ic'):unassign('In'):unassign('Ia'):
 unassign('Jp'):unassign('Jc'):unassign('Jn'):unassign('Ja'):
 P:= x -> ((12-9*B-6*C)*x**3 + (-18+12*B+6*C)*x**2+(6-2*B))/6;
 Q:= x -> ((-B-6*C)*x**3 + (6*B+30*C)*x**2 + (-12*B-48*C)*x + (8*B+24*C))/6;

 R := d -> Q(d+1)*Ip + P(d)*Ic + P(1-d) * In + Q(2-d)*Ia;

 # how does it perform on a linear function?
 R0 :=  d -> Q(d+1)*(Ic-1) + P(d)*Ic + P(1-d) * (Ic+1) + Q(2-d)*(Ic+2);

 # Cubic (cubic splines - depends only on In and Ic, derivatives are 0 at the center of each sample)
 collect(subs({B=0,C=0},R(d)),d);
 collect(subs({B=0,C=0},R0(d)),d);

 # Catmull-Rom / Keys / Hermite spline - gives linear func if input is linear
 collect(subs({B=0,C=0.5},R(d)),d);
 collect(subs({B=0,C=0.5},R0(d)),d);

 # Simon
 collect(subs({B=0,C=0.75},R(d)),d);
 collect(subs({B=0,C=0.75},R0(d)),d);

 # Rifman
 collect(subs({B=0,C=1.},R(d)),d);
 collect(subs({B=0,C=1.},R0(d)),d);

 # Mitchell - gives linear func if input is linear
 collect(subs({B=1/3, C=1/3},R(d)),d);
 collect(subs({B=1/3, C=1/3},R0(d)),d);

 # Parzen (Cubic B-spline) - gives linear func if input is linear
 collect(subs({B=1,C=0},R(d)),d);
 collect(subs({B=1,C=0},R0(d)),d);

 # Notch - gives linear func if input is linear
 collect(subs({B=3/2,C=-1/4},R(d)),d);
 collect(subs({B=3/2,C=-1/4},R0(d)),d);
*/
inline double
ofxsFilterClampVal(double I, double Ic, double In)
{
    double Imin = std::min(Ic, In);
    if (I < Imin)
        return Imin;
    double Imax = std::max(Ic, In);
    if (I > Imax)
        return Imax;
    return I;
}

inline
double
ofxsFilterLinear(double Ic, double In, double d)
{
    return Ic + d*(In - Ic);
}

static inline
double
ofxsFilterCubic(double Ic, double In, double d)
{
    return Ic + d*d*((-3*Ic +3*In ) + d*(2*Ic -2*In ));
}

inline
double
ofxsFilterKeys(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-Ip +In ) + d*((2*Ip -5*Ic +4*In -Ia ) + d*(-Ip +3*Ic -3*In +Ia )))/2;
    if (clamp) {
        I = ofxsFilterClampVal(I, Ic, In);
    }
    return I;
}

inline
double
ofxsFilterSimon(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-3*Ip +3*In ) + d*((6*Ip -9*Ic +6*In -3*Ia ) + d*(-3*Ip +5*Ic -5*In +3*Ia )))/4;
    if (clamp) {
        I = ofxsFilterClampVal(I, Ic, In);
    }
    return I;
}

inline
double
ofxsFilterRifman(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = Ic  + d*((-Ip +In ) + d*((2*Ip -2*Ic +In -Ia ) + d*(-Ip +Ic -In +Ia )));
    if (clamp) {
        I = ofxsFilterClampVal(I, Ic, In);
    }
    return I;
}

inline
double
ofxsFilterMitchell(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +16*Ic +In + d*((-9*Ip +9*In ) + d*((15*Ip -36*Ic +27*In -6*Ia ) + d*(-7*Ip +21*Ic -21*In +7*Ia ))))/18;
    if (clamp) {
        I = ofxsFilterClampVal(I, Ic, In);
    }
    return I;
}

inline
double
ofxsFilterParzen(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +4*Ic +In + d*((-3*Ip +3*In ) + d*((3*Ip -6*Ic +3*In ) + d*(-Ip +3*Ic -3*In +Ia ))))/6;
    return I;
}

inline
double
ofxsFilterNotch(double Ip, double Ic, double In, double Ia, double d, bool clamp)
{
    double I = (Ip +2*Ic +In + d*((-2*Ip +2*In ) + d*((Ip -Ic -In +Ia ))))/4;
    return I;
}

#define OFXS_APPLY4(f,j) double I ## j = f(Ip ## j, Ic ## j, In ## j, Ia ##j, dx, clamp)

#define OFXS_CUBIC2D(f)                                      \
inline                                           \
double                                                  \
f ## 2D(double Ipp, double Icp, double Inp, double Iap, \
        double Ipc, double Icc, double Inc, double Iac, \
        double Ipn, double Icn, double Inn, double Ian, \
        double Ipa, double Ica, double Ina, double Iaa, \
        double dx, double dy, bool clamp)               \
{                                                       \
    OFXS_APPLY4(f,p); OFXS_APPLY4(f,c); OFXS_APPLY4(f,n); OFXS_APPLY4(f,a); \
    return f(Ip , Ic , In , Ia , dy, clamp);            \
}

OFXS_CUBIC2D(ofxsFilterKeys);
OFXS_CUBIC2D(ofxsFilterSimon);
OFXS_CUBIC2D(ofxsFilterRifman);
OFXS_CUBIC2D(ofxsFilterMitchell);
OFXS_CUBIC2D(ofxsFilterParzen);
OFXS_CUBIC2D(ofxsFilterNotch);

#undef OFXS_CUBIC2D
#undef OFXS_APPLY4

template <class PIX>
PIX ofxsGetPixComp(const PIX* p, int c)
{
    return p ? p[c] : PIX();
}

// Macros used in ofxsFilterInterpolate2D
#define OFXS_CLAMPXY(m) \
                         m ## x = std::max(srcImg->getBounds().x1,std::min(m ## x,srcImg->getBounds().x2-1)); \
                         m ## y = std::max(srcImg->getBounds().y1,std::min(m ## y,srcImg->getBounds().y2-1))

#define OFXS_GETPIX(i,j) PIX *P ## i ## j = (PIX *)srcImg->getPixelAddress(i ## x, j ## y)

#define OFXS_GETI(i,j)   const double I ## i ## j = ofxsGetPixComp(P ## i ## j,c)

#define OFXS_GETPIX4(i)  OFXS_GETPIX(i,p); OFXS_GETPIX(i,c); OFXS_GETPIX(i,n); OFXS_GETPIX(i,a);

#define OFXS_GETI4(i)    OFXS_GETI(i,p); OFXS_GETI(i,c); OFXS_GETI(i,n); OFXS_GETI(i,a);


#define OFXS_I44         Ipp, Icp, Inp, Iap, \
                         Ipc, Icc, Inc, Iac, \
                         Ipn, Icn, Inn, Ian, \
                         Ipa, Ica, Ina, Iaa

// note that the center of pixel (0,0) has canonical coordinated (0.5,0.5)
template <class PIX, int nComponents, FilterEnum filter, bool clamp>
void
ofxsFilterInterpolate2D(double fx, double fy, //!< coordinates of the pixel to be interpolated in srcImg in CANONICAL coordinates
                        const OFX::Image *srcImg, //!< image to be transformed
                        bool blackOutside,
                        float *tmpPix) //!< destination pixel in float format
{
    // GENERIC TRANSFORM
    // from here on, everything is generic, and should be moved to a generic transform class
    // Important: (0,0) is the *corner*, not the *center* of the first pixel (see OpenFX specs)
    switch (filter) {
        case eFilterImpulse: {
            ///nearest neighboor
            // the center of pixel (0,0) has coordinates (0.5,0.5)
            int mx = std::floor(fx); // don't add 0.5
            int my = std::floor(fy); // don't add 0.5

            if (!blackOutside) {
                OFXS_CLAMPXY(m);
            }
            OFXS_GETPIX(m,m);
            if (Pmm) {
                for (int c = 0; c < nComponents; ++c) {
                    tmpPix[c] = Pmm[c];
                }
            } else {
                for (int c = 0; c < nComponents; ++c) {
                    tmpPix[c] = 0;
                }
            }
        }
            break;
        case eFilterBilinear:
        case eFilterCubic: {
            // bilinear or cubic
            // the center of pixel (0,0) has coordinates (0.5,0.5)
            int cx = std::floor(fx-0.5);
            int cy = std::floor(fy-0.5);
            int nx = cx + 1;
            int ny = cy + 1;
            if (!blackOutside) {
                OFXS_CLAMPXY(c);
                OFXS_CLAMPXY(n);
            }

            const double dx = std::max(0., std::min(fx-0.5 - cx, 1.));
            const double dy = std::max(0., std::min(fy-0.5 - cy, 1.));

            OFXS_GETPIX(c,c); OFXS_GETPIX(n,c); OFXS_GETPIX(c,n); OFXS_GETPIX(n,n);
            if (Pcc || Pnc || Pcn || Pnn) {
                for (int c = 0; c < nComponents; ++c) {
                    OFXS_GETI(c,c); OFXS_GETI(n,c); OFXS_GETI(c,n); OFXS_GETI(n,n);
                    if (filter == eFilterBilinear) {
                        double Ic = ofxsFilterLinear(Icc, Inc, dx);
                        double In = ofxsFilterLinear(Icn, Inn, dx);
                        tmpPix[c] = ofxsFilterLinear(Ic , In , dy);
                    } else if (filter == eFilterCubic) {
                        double Ic = ofxsFilterCubic(Icc, Inc, dx);
                        double In = ofxsFilterCubic(Icn, Inn, dx);
                        tmpPix[c] = ofxsFilterCubic(Ic , In , dy);
                    } else {
                        assert(0);
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
        case eFilterKeys:
        case eFilterSimon:
        case eFilterRifman:
        case eFilterMitchell:
        case eFilterParzen:
        case eFilterNotch: {
            // the center of pixel (0,0) has coordinates (0.5,0.5)
            int cx = std::floor(fx-0.5);
            int cy = std::floor(fy-0.5);
            int px = cx - 1;
            int py = cy - 1;
            int nx = cx + 1;
            int ny = cy + 1;
            int ax = cx + 2;
            int ay = cy + 2;
            if (!blackOutside) {
                OFXS_CLAMPXY(c);
                OFXS_CLAMPXY(p);
                OFXS_CLAMPXY(n);
                OFXS_CLAMPXY(a);
            }
            const double dx = std::max(0., std::min(fx-0.5 - cx, 1.));
            const double dy = std::max(0., std::min(fy-0.5 - cy, 1.));

            OFXS_GETPIX4(p); OFXS_GETPIX4(c); OFXS_GETPIX4(n); OFXS_GETPIX4(a);
            if (Ppp || Pcp || Pnp || Pap || Ppc || Pcc || Pnc || Pac || Ppn || Pcn || Pnn || Pan || Ppa || Pca || Pna || Paa) {
                for (int c = 0; c < nComponents; ++c) {
                    //double Ipp = get(Ppp,c);, etc.
                    OFXS_GETI4(p); OFXS_GETI4(c); OFXS_GETI4(n); OFXS_GETI4(a);
                    double I = 0.;
                    switch (filter) {
                        case eFilterKeys:
                            I = ofxsFilterKeys2D(OFXS_I44, dx, dy, clamp);
                            break;
                        case eFilterSimon:
                            I = ofxsFilterSimon2D(OFXS_I44, dx, dy, clamp);
                            break;
                        case eFilterRifman:
                            I = ofxsFilterRifman2D(OFXS_I44, dx, dy, clamp);
                            break;
                        case eFilterMitchell:
                            I = ofxsFilterMitchell2D(OFXS_I44, dx, dy, clamp);
                            break;
                        case eFilterParzen:
                            I = ofxsFilterParzen2D(OFXS_I44, dx, dy, false);
                            break;
                        case eFilterNotch:
                            I = ofxsFilterNotch2D(OFXS_I44, dx, dy, false);
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

        default:
            assert(0);
            break;
    }
}

#undef OFXS_CLAMPXY
#undef OFXS_GETPIX
#undef OFXS_GETI
#undef OFXS_GETPIX4
#undef OFXS_GETI
#undef OFXS_I44


inline void
ofxsFilterExpandRoD(OFX::ImageEffect* effect, double pixelAspectRatio, const OfxPointD& renderScale, bool blackOutside, OfxRectD *rod)
{
    // No need to round things up here, we must give the *actual* RoD

    if (!blackOutside) {
        // if it's not black outside, the RoD should contain the project (we can't rely on the host to fill it).
        OfxPointD size = effect->getProjectSize();
        OfxPointD offset = effect->getProjectOffset();

        rod->x1 = std::min(rod->x1, offset.x);
        rod->x2 = std::max(rod->x2, offset.x + size.x);
        rod->y1 = std::min(rod->y1, offset.y);
        rod->y2 = std::max(rod->y2, offset.y + size.y);
    } else {
        // expand the RoD to get at least one black pixel
        double pixelSizeX = pixelAspectRatio / renderScale.x;
        double pixelSizeY = 1. / renderScale.x;
        if (rod->x1 > kOfxFlagInfiniteMin) {
            rod->x1 = rod->x1 - pixelSizeX;
        }
        if (rod->x2 < kOfxFlagInfiniteMax) {
            rod->x2 = rod->x2 + pixelSizeX;
        }
        if (rod->y1 > kOfxFlagInfiniteMin) {
            rod->y1 = rod->y1 - pixelSizeY;
        }
        if (rod->y2 < kOfxFlagInfiniteMax) {
            rod->y2 = rod->y2 + pixelSizeY;
        }
    }
    assert(rod->x1 <= rod->x2 && rod->y1 <= rod->y2);
}

inline void
ofxsFilterExpandRoI(const OfxRectD &roi, double pixelAspectRatio, const OfxPointD& renderScale, FilterEnum filter, bool doMasking, double mix, OfxRectD *srcRoI)
{
    // No need to round things up here, we must give the *actual* RoI,
    // the host should compute the right image region from it (by rounding it up/down).

    double pixelSizeX = pixelAspectRatio / renderScale.x;
    double pixelSizeY = 1. / renderScale.x;
    switch (filter) {
        case eFilterImpulse:
            // nearest neighbor, the exact region is OK
            break;
        case eFilterBilinear:
        case eFilterCubic:
            // bilinear or cubic, expand by 0.5 pixels
            if (srcRoI->x1 > kOfxFlagInfiniteMin) {
                srcRoI->x1 -= 0.5*pixelSizeX;
            }
            if (srcRoI->x2 < kOfxFlagInfiniteMax) {
                srcRoI->x2 += 0.5*pixelSizeX;
            }
            if (srcRoI->y1 > kOfxFlagInfiniteMin) {
                srcRoI->y1 -= 0.5*pixelSizeY;
            }
            if (srcRoI->y2 < kOfxFlagInfiniteMax) {
                srcRoI->y2 += 0.5*pixelSizeY;
            }
            break;
        case eFilterKeys:
        case eFilterSimon:
        case eFilterRifman:
        case eFilterMitchell:
        case eFilterParzen:
        case eFilterNotch:
            // bicubic, expand by 1.5 pixels
            if (srcRoI->x1 > kOfxFlagInfiniteMin) {
                srcRoI->x1 -= 1.5*pixelSizeX;
            }
            if (srcRoI->x2 < kOfxFlagInfiniteMax) {
                srcRoI->x2 += 1.5*pixelSizeX;
            }
            if (srcRoI->y1 > kOfxFlagInfiniteMin) {
                srcRoI->y1 -= 1.5*pixelSizeY;
            }
            if (srcRoI->y2 < kOfxFlagInfiniteMax) {
                srcRoI->y2 += 1.5*pixelSizeY;
            }
            break;
    }
    if (doMasking || mix != 1.) {
        // for masking or mixing, we also need the source image for that same roi.
        // compute the union of both ROIs
        srcRoI->x1 = std::min(srcRoI->x1, roi.x1);
        srcRoI->x2 = std::max(srcRoI->x2, roi.x2);
        srcRoI->y1 = std::min(srcRoI->y1, roi.y1);
        srcRoI->y2 = std::max(srcRoI->y2, roi.y2);
    }
    assert(srcRoI->x1 < srcRoI->x2 && srcRoI->y1 < srcRoI->y2);
}

#endif
