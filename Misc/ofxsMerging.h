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

namespace MergeImages2D {

    
    enum MergingFunction
    {
        Merge_ATop = 0,
        Merge_Average,
        Merge_ColorBurn,
        Merge_ColorDodge,
        Merge_ConjointOver,
        Merge_Copy,
        Merge_Difference,
        Merge_DisjointOver,
        Merge_Divide,
        Merge_Exclusion,
        Merge_Freeze,
        Merge_From,
        Merge_Geometric,
        Merge_HardLight,
        Merge_Hypot,
        Merge_In,
        Merge_Interpolated,
        Merge_Mask,
        Merge_Matte,
        Merge_Lighten,
        Merge_Darken,
        Merge_Minus,
        Merge_Multiply,
        Merge_Out,
        Merge_Over,
        Merge_Overlay,
        Merge_PinLight,
        Merge_Plus,
        Merge_Reflect,
        Merge_Screen,
        Merge_Stencil,
        Merge_Under,
        Merge_XOR
    };
    
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
    
    template <typename PIX>
    PIX exclusionFunctor(PIX A,PIX B)
    {
        return A + B - 2 * A * B;
    }
    
    template <typename PIX>
    PIX fromFunctor(PIX A,PIX B)
    {
        return B - A;
    }
    
    template <typename PIX>
    PIX geometricFunctor(PIX A,PIX B)
    {
        return 2 * A * B / ( A + B );
    }

    template <typename PIX>
    PIX multiplyFunctor(PIX A,PIX B)
    {
        return A * B;
    }
    
    template <typename PIX>
    PIX screenFunctor(PIX A,PIX B)
    {
        return A + B - A * B;
    }
    
    template <typename PIX,int maxValue>
    PIX hardLightFunctor(PIX A,PIX B)
    {
        if (A < ((double)maxValue / 2.)) {
            return 2 * A * B;
        } else {
            return maxValue - 2 * ( maxValue - A ) * ( maxValue - B );
        }
    }
    
    template <typename PIX,int maxValue>
    PIX softLightFunctor(PIX A,PIX B)
    {
        if (A < ((double)maxValue / 2.)) {
            return B - ((double)maxValue - 2 * A) * B * ((double)maxValue - B);
        } else {
            return B + (2 * A - (double)maxValue) * (4 * B * (4 * B + (double)maxValue) * (B - (double)maxValue) + 7 * B);
        }
    }
    
    template <typename PIX>
    PIX hypotFunctor(PIX A,PIX B)
    {
        return std::sqrt((double)( A * A + B * B ));
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
        if(B < ((double)maxValue / 2.)) {
			// multiply
			return 2 * A * B;
        } else {
			// screen
			return maxValue - 2 * ( maxValue - A ) * ( maxValue - B );
        }
    }
    
    template <typename PIX,int maxValue>
    PIX colorDodgeFunctor(PIX A,PIX B)
    {
        if(A < maxValue) {
			PIX dst = B / (maxValue - A);
            return dst > maxValue ? maxValue : dst;
		} else {
			return maxValue;
        }
    }

    template <typename PIX,int maxValue>
    PIX colorBurnFunctor(PIX A,PIX B)
    {
        if(B != 0) {
			PIX ret = maxValue - (maxValue - A) / B ;
			if(ret <= 0)
				return 0;
            else
                return ret;
		}
		else
			return 0;
    }
    
    template <typename PIX,int maxValue>
    PIX pinLightFunctor(PIX A,PIX B)
    {
        PIX max2 = PIX((double)maxValue / 2.);
        return B >= max2 ? std::max(A,(B - max2) * 2)
        : std::min( A,B * 2);
    }
    
    template <typename PIX,int maxValue>
    PIX reflectFunctor(PIX A,PIX B)
    {
        if(B >= maxValue) {
			return maxValue;
		} else {
			PIX ret = PIX((double)A * A / (double)(maxValue - B));
            return ret > maxValue ? maxValue : ret;
		}
    }
    
    template <typename PIX,int maxValue>
    PIX freezeFunctor(PIX A,PIX B)
    {
        if(B == 0) {
			return 0;
		} else {
			PIX ret = maxValue - std::sqrt((double)maxValue - A) / B;
            return ret <= 0 ? 0 : ret;
		}
    }
    
    template <typename PIX,int maxValue>
    PIX interpolatedFunctor(PIX A,PIX B)
    {
        static const double pi =  3.14159265358979323846264338327950288419717;
        PIX max4 = PIX((double)maxValue / 4.);
        return  ((double)maxValue / 2.) - max4 * std::cos(pi * (double)A) - max4 * std::cos(pi* (double)B);
    }
    
    template <typename PIX,int maxValue>
    PIX atopFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return A * alphaB + B * (maxValue - alphaA);
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
        return A * alphaB;
    }
    
    template <typename PIX,int maxValue>
    PIX matteFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return A * alphaA + B * (maxValue - alphaA);
    }
    
    template <typename PIX,int maxValue>
    PIX maskFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return B * alphaA;
    }
    
    template <typename PIX,int maxValue>
    PIX outFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A * (maxValue - alphaB);
    }
    
    template <typename PIX,int maxValue>
    PIX overFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A + B * (maxValue - alphaA);
    }
    
    template <typename PIX,int maxValue>
    PIX stencilFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  B * (maxValue - alphaA);
    }
    
    template <typename PIX,int maxValue>
    PIX underFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return  A * (maxValue - alphaB) + B;
    }
    
    template <typename PIX,int maxValue>
    PIX xorFunctor(PIX A,PIX B,PIX alphaA,PIX alphaB)
    {
        return   A * (maxValue - alphaB) + B * (maxValue - alphaA);
    }

    template <typename PIX,int nComponents,int maxValue>
    void mergePixel(MergingFunction f,bool doAlphaMasking,const PIX* A,const PIX* B,PIX* dst)
    {
        PIX a = nComponents == 4 ? A[3] : maxValue;
        PIX b = nComponents == 4 ? B[3] : maxValue;
        
        ///When doAlphaMasking is enabled and we're in RGBA the output alpha is set to alphaA+alphaB-alphA*alphaB
        int maxComp = doAlphaMasking && nComponents == 4 ? 3 : nComponents;
        if (doAlphaMasking && nComponents == 4) {
            dst[3] = A[3] + B[3] - A[3] * B[3];
        }
        for (int i = 0; i < maxComp; ++i) {
            switch (f) {
                case Merge_ATop:
                    dst[i] = atopFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Average:
                    dst[i] = averageFunctor(A[i], B[i]);
                    break;
                case Merge_ColorBurn:
                    dst[i] = colorBurnFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_ColorDodge:
                    dst[i] = colorDodgeFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_ConjointOver:
                    dst[i] = conjointOverFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Copy:
                    dst[i] = copyFunctor(A[i], B[i]);
                    break;
                case Merge_Difference:
                    dst[i] = differenceFunctor(A[i], B[i]);
                    break;
                case Merge_DisjointOver:
                    dst[i] = disjointOverFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Divide:
                    dst[i] = divideFunctor(A[i], B[i]);
                    break;
                case Merge_Exclusion:
                    dst[i] = exclusionFunctor(A[i], B[i]);
                    break;
                case Merge_Freeze:
                    dst[i] = freezeFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_From:
                    dst[i] = fromFunctor(A[i], B[i]);
                    break;
                case Merge_Geometric:
                    dst[i] = geometricFunctor(A[i], B[i]);
                    break;
                case Merge_HardLight:
                    dst[i] = hardLightFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_Hypot:
                    dst[i] = hypotFunctor(A[i], B[i]);
                    break;
                case Merge_In:
                    dst[i] = inFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Interpolated:
                    dst[i] = interpolatedFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_Mask:
                    dst[i] = maskFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Matte:
                    dst[i] = matteFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Lighten:
                    dst[i] = lightenFunctor(A[i], B[i]);
                    break;
                case Merge_Darken:
                    dst[i] = darkenFunctor(A[i], B[i]);
                    break;
                case Merge_Minus:
                    dst[i] = minusFunctor(A[i], B[i]);
                    break;
                case Merge_Multiply:
                    dst[i] = multiplyFunctor(A[i], B[i]);
                    break;
                case Merge_Out:
                    dst[i] = outFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Over:
                    dst[i] = overFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Overlay:
                    dst[i] = overlayFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_PinLight:
                    dst[i] = pinLightFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_Plus:
                    dst[i] = plusFunctor(A[i], B[i]);
                    break;
                case Merge_Reflect:
                    dst[i] = reflectFunctor<PIX, maxValue>(A[i], B[i]);
                    break;
                case Merge_Screen:
                    dst[i] = screenFunctor(A[i], B[i]);
                    break;
                case Merge_Stencil:
                    dst[i] = stencilFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_Under:
                    dst[i] = underFunctor<PIX,maxValue>(A[i], B[i], a, b);
                    break;
                case Merge_XOR:
                    dst[i] = xorFunctor<PIX, maxValue>(A[i], B[i], a, b);
                    break;
                default:
                    dst[i] = 0;
                    break;
            }
        }
    }
    
    inline OfxRectD rectanglesBoundingBox( const OfxRectD& a, const OfxRectD& b )
    {
        OfxRectD res;
        res.x1 = std::min( a.x1, b.x1 );
        res.x2 = std::max( res.x1, std::max( a.x2, b.x2 ) );
        res.y1 = std::min( a.y1, b.y1 );
        res.y2 = std::max( res.y1, std::max( a.y2, b.y2 ) );
        return res;
    }
    
    
    template <typename Rect>
    bool isRectNull(const Rect& r) { return (r.x2 <= r.x1) || (r.y2 <= r.y1); }
    
    template <typename Rect>
    bool rectangleIntersect(const Rect& r1,const Rect& r2,Rect* intersection) {
        if (isRectNull(r1) || isRectNull(r2))
            return false;
        
        if (r1.x1 > r2.x2 || r2.x1 > r1.x2 || r1.y1 > r2.y2 || r2.y1 > r1.y2)
            return false;
        
        intersection->x1 = std::max(r1.x1,r2.x1);
        intersection->x2 = std::min(r1.x2,r2.x2);
        intersection->y1 = std::max(r1.y1,r2.y1);
        intersection->y2 = std::min(r1.y2,r2.y2);
        return true;
    }
    
    /**
     * @brief Scales down the rectangle by the given power of 2, and return the smallest *enclosing* rectangle
     **/
    inline OfxRectI downscalePowerOfTwoSmallestEnclosing(const OfxRectI& r,unsigned int thisLevel)
    {
        if (thisLevel == 0) {
            return r;
        }
        OfxRectI ret;
        int pot = (1<<thisLevel);
        int pot_minus1 = pot - 1;
        ret.x1 = r.x1 >> thisLevel;
        ret.x2 = (r.x2 + pot_minus1) >> thisLevel;
        ret.y1 = r.y1 >> thisLevel;
        ret.y2 = (r.y2 + pot_minus1) >> thisLevel;
        // check that it's enclosing
        assert(ret.x1*pot <= r.x1 && ret.x2*pot >= r.x2 && ret.y1*pot <= r.y1 && ret.y2*pot >= r.y2);
        return ret;
    }

    
    inline double getScaleFromMipMapLevel(unsigned int level)
    {
        return 1./(1<<level);
    }
    
#ifndef M_LN2
#define M_LN2       0.693147180559945309417232121458176568  /* loge(2)        */
#endif
    inline unsigned int getLevelFromScale(double s)
    {
        assert(0. < s && s <= 1.);
        int retval = -std::floor(std::log(s)/M_LN2 + 0.5);
        assert(retval >= 0);
        return retval;
    }

}




#endif // Misc_Merging_helper_h
