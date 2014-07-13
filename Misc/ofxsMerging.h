/*
 OFX Merge helpers

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

#ifndef Misc_Merging_helper_h
#define Misc_Merging_helper_h

#include <cmath>
#include "ofxsImageEffect.h"

#ifndef M_PI
#define M_PI        3.14159265358979323846264338327950288   /* pi             */
#endif

// References:
//
// SVG Compositing Specification:
//   http://www.w3.org/TR/SVGCompositing/
// PDF Reference v1.7:
//   http://www.adobe.com/content/dam/Adobe/en/devnet/acrobat/pdfs/pdf_reference_1-7.pdf
//   http://www.adobe.com/devnet/pdf/pdf_reference_archive.html
// Adobe photoshop blending modes:
//   http://helpx.adobe.com/en/photoshop/using/blending-modes.html
//   http://www.deepskycolors.com/archive/2010/04/21/formulas-for-Photoshop-blending-modes.html
// ImageMagick:
//   http://www.imagemagick.org/Usage/compose/
//
// Note about the Soft-Light operation:
// Soft-light as implemented in Nuke comes from the SVG 2004 specification, which is wrong.
// In SVG 2004, 'Soft_Light' did not work as expected, producing a brightening for any non-gray shade
// image overlay.
// It was fixed in the March 2009 SVG specification, which was used for this implementation.

namespace MergeImages2D {


    enum MergingFunctionEnum
    {
        eMergeATop = 0,
        eMergeAverage,
        eMergeColorBurn,
        eMergeColorDodge,
        eMergeConjointOver,
        eMergeCopy,
        eMergeDifference,
        eMergeDisjointOver,
        eMergeDivide,
        eMergeExclusion,
        eMergeFreeze,
        eMergeFrom,
        eMergeGeometric,
        eMergeHardLight,
        eMergeHypot,
        eMergeIn,
        eMergeInterpolated,
        eMergeMask,
        eMergeMatte,
        eMergeLighten,
        eMergeDarken,
        eMergeMinus,
        eMergeMultiply,
        eMergeOut,
        eMergeOver,
        eMergeOverlay,
        eMergePinLight,
        eMergePlus,
        eMergeReflect,
        eMergeScreen,
        eMergeSoftLight,
        eMergeStencil,
        eMergeUnder,
        eMergeXOR
    };

    inline bool
    isMaskable(MergingFunctionEnum operation)
    {
        switch (operation) {
            case eMergeAverage:
            case eMergeColorBurn:
            case eMergeColorDodge:
            case eMergeDifference:
            case eMergeDivide:
            case eMergeExclusion:
            case eMergeFrom:
            case eMergeFreeze:
            case eMergeGeometric:
            case eMergeHardLight:
            case eMergeHypot:
            case eMergeInterpolated:
            case eMergeLighten:
            case eMergeDarken:
            case eMergeMinus:
            case eMergeMultiply:
            case eMergeOverlay:
            case eMergePinLight:
            case eMergePlus:
            case eMergeReflect:
            case eMergeSoftLight:
                return true;
            case eMergeATop:
            case eMergeConjointOver:
            case eMergeCopy:
            case eMergeDisjointOver:
            case eMergeIn:
            case eMergeMask:
            case eMergeMatte:
            case eMergeOut:
            case eMergeOver:
            case eMergeScreen:
            case eMergeStencil:
            case eMergeUnder:
            case eMergeXOR:
                return false;
        }
    }
    
    inline std::string
    getOperationString(MergingFunctionEnum operation) {
        switch (operation) {
            case eMergeATop:
                return "atop";
            case eMergeAverage:
                return "average";
            case eMergeColorBurn:
                return "color-burn";
            case eMergeColorDodge:
                return "color-dodge";
            case eMergeConjointOver:
                return "conjoint-over";
            case eMergeCopy:
                return "copy";
            case eMergeDifference:
                return "difference";
            case eMergeDisjointOver:
                return "disjoint-over";
            case eMergeDivide:
                return "divide";
            case eMergeExclusion:
                return "exclusion";
            case eMergeFreeze:
                return "freeze";
            case eMergeFrom:
                return "from";
            case eMergeGeometric:
                return "geometric";
            case eMergeHardLight:
                return "hard-light";
            case eMergeHypot:
                return "hypot";
            case eMergeIn:
                return "in";
            case eMergeInterpolated:
                return "interpolated";
            case eMergeMask:
                return "mask";
            case eMergeMatte:
                return "matte";
            case eMergeLighten:
                return "max";
            case eMergeDarken:
                return "min";
            case eMergeMinus:
                return "minus";
            case eMergeMultiply:
                return "multiply";
            case eMergeOut:
                return "out";
            case eMergeOver:
                return "over";
            case eMergeOverlay:
                return "overlay";
            case eMergePinLight:
                return "pinlight";
            case eMergePlus:
                return "plus";
            case eMergeReflect:
                return "reflect";
            case eMergeScreen:
                return "screen";
            case eMergeSoftLight:
                return "soft-light";
            case eMergeStencil:
                return "stencil";
            case eMergeUnder:
                return "under";
            case eMergeXOR:
                return "xor";
            default:
                break;
        }
    }
    

    template <typename PIX>
    PIX averageFunctor(PIX A,PIX B)
    {
        return (A + B) * 0.5;
    }

    template <typename PIX>
    PIX copyFunctor(PIX A,PIX /*B*/)
    {
        return A;
    }

    template <typename PIX>
    PIX plusFunctor(PIX A,PIX B)
    {
        return A + B;
    }

    template <typename PIX>
    PIX differenceFunctor(PIX A,PIX B)
    {
        return std::abs(A - B);
    }

    template <typename PIX>
    PIX divideFunctor(PIX A,PIX B)
    {
        if (B <= 0) {
            return 0;
        }
        return A / B;
    }

    template <typename PIX,int maxValue>
    PIX exclusionFunctor(PIX A,PIX B)
    {
        return A + B - 2 * A * B/(double)maxValue;
    }

    template <typename PIX>
    PIX fromFunctor(PIX A,PIX B)
    {
        return B - A;
    }

    template <typename PIX>
    PIX geometricFunctor(PIX A,PIX B)
    {
        return 2 * A * B / (A + B);
    }

    template <typename PIX,int maxValue>
    PIX multiplyFunctor(PIX A,PIX B)
    {
        return A * B/(double)maxValue;
    }

    template <typename PIX,int maxValue>
    PIX screenFunctor(PIX A,PIX B)
    {
        return A + B - A * B/(double)maxValue;
    }

    template <typename PIX,int maxValue>
    PIX hardLightFunctor(PIX A,PIX B)
    {
        if (A < ((double)maxValue / 2.)) {
            return 2 * A * B / (double)maxValue;
        } else {
            return maxValue * (1. - 2 * (1. - A /(double)maxValue) * (1. - B/(double)maxValue));
        }
    }

    template <typename PIX,int maxValue>
    PIX softLightFunctor(PIX A,PIX B)
    {
        double An = A/(double)maxValue;
        double Bn = B/(double)maxValue;

        if (2*An <= 1) {
            return maxValue * (Bn - (1 - 2 * An) * Bn * (1 - Bn));
        } else if (4*Bn <= 1) {
            return maxValue * (Bn + (2 * An - 1) * (4 * Bn * (4 * Bn + 1) * (Bn - 1) + 7 * Bn));
        } else {
            return maxValue * (Bn + (2 * An - 1) * (sqrt(Bn) - Bn));
        }
    }

    template <typename PIX>
    PIX hypotFunctor(PIX A,PIX B)
    {
        return std::sqrt((double)(A * A + B * B));
    }

    template <typename PIX>
    PIX minusFunctor(PIX A,PIX B)
    {
        return A - B;
    }

    template <typename PIX>
    PIX darkenFunctor(PIX A,PIX B)
    {
        return std::min(A,B);
    }

    template <typename PIX>
    PIX lightenFunctor(PIX A,PIX B)
    {
        return std::max(A,B);
    }

    template <typename PIX,int maxValue>
    PIX overlayFunctor(PIX A,PIX B)
    {
        double An = A/(double)maxValue;
        double Bn = B/(double)maxValue;

        if (2*Bn <= 1.) {
            // multiply
            return maxValue * (2 * An * Bn);
        } else {
            // screen
            return maxValue * (1 - 2 * (1 - Bn) * (1 - An));
        }
    }

    template <typename PIX,int maxValue>
    PIX colorDodgeFunctor(PIX A,PIX B)
    {
        if (A >= maxValue) {
            return A;
        } else {
            return maxValue * std::min(1., B/(maxValue - (double)A));
        }
    }

    template <typename PIX,int maxValue>
    PIX colorBurnFunctor(PIX A,PIX B)
    {
        if (A <= 0) {
            return A;
        } else {
            return maxValue * (1. - std::min(1., (maxValue - B)/(double)A));
        }
    }

    template <typename PIX,int maxValue>
    PIX pinLightFunctor(PIX A,PIX B)
    {
        PIX max2 = PIX((double)maxValue / 2.);
        return A >= max2 ? std::max(B,(A - max2) * 2) : std::min(B,A * 2);
    }

    template <typename PIX,int maxValue>
    PIX reflectFunctor(PIX A,PIX B)
    {
        if (B >= maxValue) {
            return maxValue;
        } else {
            return std::min((double)maxValue, A * A / (double)(maxValue - B));
        }
    }

    template <typename PIX,int maxValue>
    PIX freezeFunctor(PIX A,PIX B)
    {
        if (B <= 0) {
            return 0;
        } else {
            double An = A/(double)maxValue;
            double Bn = B/(double)maxValue;

            return std::max(0., maxValue * (1 - std::sqrt(1. - An) / Bn));
        }
    }

    template <typename PIX,int maxValue>
    PIX interpolatedFunctor(PIX A,PIX B)
    {
        double An = A/(double)maxValue;
        double Bn = B/(double)maxValue;

        return maxValue * (0.5 - 0.25 * (std::cos(M_PI * An) - std::cos(M_PI * Bn)));
    }

    template <typename PIX,int maxValue>
    PIX atopFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return A * alphaB/(double)maxValue + B * (1. - alphaA/(double)maxValue);
    }

    template <typename PIX,int maxValue>
    PIX conjointOverFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        if (alphaA > alphaB) {
            return A;
        } else {
            return A + B * (maxValue - alphaA) / alphaB;
        }
    }

    template <typename PIX,int maxValue>
    PIX disjointOverFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        if ((alphaA + alphaB) < maxValue) {
            return A + B;
        } else {
            return A + B * (maxValue - alphaA) / alphaB;
        }
    }

    template <typename PIX,int maxValue>
    PIX inFunctor(PIX A,PIX /*B*/,PIX /*alphaA*/,PIX alphaB)
    {
        return A * alphaB/(double)maxValue;
    }

    template <typename PIX,int maxValue>
    PIX matteFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return A * alphaA/(double)maxValue + B * (1. - alphaA/(double)maxValue);
    }

    template <typename PIX,int maxValue>
    PIX maskFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return B * alphaA/(double)maxValue;
    }

    template <typename PIX,int maxValue>
    PIX outFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A * (1. - alphaB/(double)maxValue);
    }

    template <typename PIX,int maxValue>
    PIX overFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A + B * (1 - alphaA/(double)maxValue);
    }

    template <typename PIX,int maxValue>
    PIX stencilFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  B * (1 - alphaA/(double)maxValue);
    }

    template <typename PIX,int maxValue>
    PIX underFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A * (1 - alphaB/(double)maxValue) + B;
    }

    template <typename PIX,int maxValue>
    PIX xorFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return   A * (1 - alphaB/(double)maxValue) + B * (1 - alphaA/(double)maxValue);
    }

    template <typename PIX,int nComponents,int maxValue>
    void mergePixel(MergingFunctionEnum f, bool doAlphaMasking, const PIX* A, const PIX* B, PIX* dst)
    {
        PIX a = nComponents == 4 ? A[3] : maxValue;
        PIX b = nComponents == 4 ? B[3] : maxValue;

        ///When doAlphaMasking is enabled and we're in RGBA the output alpha is set to alphaA+alphaB-alphA*alphaB
        int maxComp = doAlphaMasking && nComponents == 4 ? 3 : nComponents;
        if (doAlphaMasking && nComponents == 4) {
            dst[3] = A[3] + B[3] - A[3] * B[3]/(double)maxValue;
        }
        for (int i = 0; i < maxComp; ++i) {
            switch (f) {
                case eMergeATop:
                    dst[i] = atopFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeAverage:
                    dst[i] = averageFunctor(A[i], B[i]);
                    break;
                case eMergeColorBurn:
                    dst[i] = colorBurnFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeColorDodge:
                    dst[i] = colorDodgeFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeConjointOver:
                    dst[i] = conjointOverFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeCopy:
                    dst[i] = copyFunctor(A[i], B[i]);
                    break;
                case eMergeDifference:
                    dst[i] = differenceFunctor(A[i], B[i]);
                    break;
                case eMergeDisjointOver:
                    dst[i] = disjointOverFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeDivide:
                    dst[i] = divideFunctor(A[i], B[i]);
                    break;
                case eMergeExclusion:
                    dst[i] = exclusionFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeFreeze:
                    dst[i] = freezeFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeFrom:
                    dst[i] = fromFunctor(A[i], B[i]);
                    break;
                case eMergeGeometric:
                    dst[i] = geometricFunctor(A[i], B[i]);
                    break;
                case eMergeHardLight:
                    dst[i] = hardLightFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeHypot:
                    dst[i] = hypotFunctor(A[i], B[i]);
                    break;
                case eMergeIn:
                    dst[i] = inFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeInterpolated:
                    dst[i] = interpolatedFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeMask:
                    dst[i] = maskFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeMatte:
                    dst[i] = matteFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeLighten:
                    dst[i] = lightenFunctor(A[i], B[i]);
                    break;
                case eMergeDarken:
                    dst[i] = darkenFunctor(A[i], B[i]);
                    break;
                case eMergeMinus:
                    dst[i] = minusFunctor(A[i], B[i]);
                    break;
                case eMergeMultiply:
                    dst[i] = multiplyFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeOut:
                    dst[i] = outFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeOver:
                    dst[i] = overFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeOverlay:
                    dst[i] = overlayFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergePinLight:
                    dst[i] = pinLightFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergePlus:
                    dst[i] = plusFunctor(A[i], B[i]);
                    break;
                case eMergeReflect:
                    dst[i] = reflectFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeScreen:
                    dst[i] = screenFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeSoftLight:
                    dst[i] = softLightFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case eMergeStencil:
                    dst[i] = stencilFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeUnder:
                    dst[i] = underFunctor<PIX,maxValue>(A[i], B[i], a, b);
                    break;
                case eMergeXOR:
                    dst[i] = xorFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                default:
                    dst[i] = 0;
                    break;
            }
        }
    }

    ///Bounding box of two rectangles
    inline void rectBoundingBox(const OfxRectD& a, const OfxRectD& b, OfxRectD* bbox)
    {
        bbox->x1 = std::min(a.x1, b.x1);
        bbox->x2 = std::max(bbox->x1, std::max(a.x2, b.x2));
        bbox->y1 = std::min(a.y1, b.y1);
        bbox->y2 = std::max(bbox->x1, std::max(a.y2, b.y2));
    }


    template <typename Rect>
    bool rectIsEmpty(const Rect& r)
    {
        return (r.x2 <= r.x1) || (r.y2 <= r.y1);
    }

    template <typename Rect>
    bool rectIsInfinite(const Rect& r)
    {
        return ((r.x1 <= kOfxFlagInfiniteMin) || (r.x2 >= kOfxFlagInfiniteMax) ||
                (r.y1 <= kOfxFlagInfiniteMin) || (r.y2 >= kOfxFlagInfiniteMax));
    }

    /// compute the intersection of two rectangles, and return true if they intersect
    template <typename Rect>
    bool rectIntersection(const Rect& r1, const Rect& r2, Rect* intersection)
    {
        if (rectIsEmpty(r1) || rectIsEmpty(r2)) {
            intersection->x1 = 0;
            intersection->x2 = 0;
            intersection->y1 = 0;
            intersection->y2 = 0;
            return false;
        }

        if (r1.x1 > r2.x2 || r2.x1 > r1.x2 || r1.y1 > r2.y2 || r2.y1 > r1.y2) {
            intersection->x1 = 0;
            intersection->x2 = 0;
            intersection->y1 = 0;
            intersection->y2 = 0;
            return false;
        }

        intersection->x1 = std::max(r1.x1,r2.x1);
        // the region must be *at least* empty, thus the maximin.
        intersection->x2 = std::max(intersection->x1,std::min(r1.x2,r2.x2));
        intersection->y1 = std::max(r1.y1,r2.y1);
        // the region must be *at least* empty, thus the maximin.
        intersection->y2 = std::max(intersection->y1,std::min(r1.y2,r2.y2));
        return true;
    }

    /**
     * @brief Scales down the rectangle by the given power of 2, and return the smallest *enclosing* rectangle
     **/
    inline
    OfxRectI downscalePowerOfTwoSmallestEnclosing(const OfxRectI& r,unsigned int thisLevel)
    {
        if (thisLevel == 0) {
            return r;
        }
        OfxRectI ret;
        int pot = (1<<thisLevel);
        int pot_minus1 = pot - 1;
        if (r.x1 <= kOfxFlagInfiniteMin) {
            ret.x1 = kOfxFlagInfiniteMin;
        } else {
            ret.x1 = r.x1 >> thisLevel;
            assert(ret.x1*pot <= r.x1);
        }
        if (r.x2 >= kOfxFlagInfiniteMax) {
            ret.x2 = kOfxFlagInfiniteMax;
        } else {
            ret.x2 = (r.x2 + pot_minus1) >> thisLevel;
            assert(ret.x2*pot >= r.x2);
        }
        if (r.y1 <= kOfxFlagInfiniteMin) {
            ret.y1 = kOfxFlagInfiniteMin;
        } else {
            ret.y1 = r.y1 >> thisLevel;
            assert(ret.y1*pot <= r.y1);
        }
        if (r.y2 >= kOfxFlagInfiniteMax) {
            ret.y2 = kOfxFlagInfiniteMax;
        } else {
            ret.y2 = (r.y2 + pot_minus1) >> thisLevel;
            assert(ret.y2*pot >= r.y2);
        }
        return ret;
    }


    inline
    double scaleFromMipmapLevel(unsigned int level)
    {
        return 1./(1<<level);
    }

#ifndef M_LN2
#define M_LN2       0.693147180559945309417232121458176568  /* loge(2)        */
#endif
    inline
    unsigned int mipmapLevelFromScale(double s)
    {
        assert(0. < s && s <= 1.);
        int retval = -std::floor(std::log(s)/M_LN2 + 0.5);
        assert(retval >= 0);
        return retval;
    }

}




#endif // Misc_Merging_helper_h
