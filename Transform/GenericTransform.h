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
#ifndef __Misc__GenericTransform__
#define __Misc__GenericTransform__

#include "ofxsImageEffect.h"


namespace Transform2D {
    
    // NEVER define a variable/constant in a header (said it 100 times already)
    inline double pi() { return 3.14159265358979323846264338327950288419717; }

    
    /**
     * @brief A simple 3 * 3 matrix class layed out as such:
     *  a b c
     *  d e f
     *  g h i
     **/
    
    inline double toDegrees(double rad) {
        rad = rad * 180.0 / pi();
        return rad;
    }
    
    inline double toRadians(double deg) {
        deg = deg * pi() / 180.0;
        return deg;
    }
    
    struct Point3D {
        double x,y,z;
        
        Point3D()
        : x(0), y(0) , z(0) {}
        
        Point3D(double x,double y,double z)
        : x(x),y(y),z(z) {}
        
        Point3D(const Point3D& p)
        : x(p.x),y(p.y),z(p.z)
        {}
    };
    
    class Matrix3x3 {
        
    public:
        
        Matrix3x3();
        
        Matrix3x3(double a, double b, double c, double d, double e, double f,double g,double h,double i);
        
        Matrix3x3(const Matrix3x3& mat);
        
        Matrix3x3& operator=(const Matrix3x3& m);
        
        Matrix3x3& operator*=(const Matrix3x3& m);
        
        double determinant() const;
        
        Matrix3x3 getScaleAdjoint(double s) const;
        
        Matrix3x3 getInverse() const;
        
        static Matrix3x3 getRotate(double rads);
        static Matrix3x3 getRotateAroundPoint(double rads, double pointX, double pointY);
        
        static Matrix3x3 getTranslate(double translateX, double translateY);
        
        static Matrix3x3 getScale(double scaleX, double scaleY);
        static Matrix3x3 getScale(double scale);
        static Matrix3x3 getScaleAroundPoint(double scaleX, double scaleY, double pointX, double pointY);
        
        static Matrix3x3 getSkewXY(double skewX, double skewY, bool skewOrderYX);

        // matrix transform from destination to source
        static Matrix3x3 getInverseTransform(double translateX, double translateY, double scaleX, double scaleY, double skewX, double skewY, bool skewOrderYX, double rads, double centerX, double centerY);

        // matrix transform from source to destination
        static Matrix3x3 getTransform(double translateX, double translateY, double scaleX, double scaleY, double skewX, double skewY, bool skewOrderYX, double rads, double centerX, double centerY);

        double a,b,c,d,e,f,g,h,i;
    };
    
    inline Matrix3x3 operator*(const Matrix3x3& m1, const Matrix3x3& m2) {
        return Matrix3x3(m1.a * m2.a + m1.b * m2.d + m1.c * m2.g,
                         m1.a * m2.b + m1.b * m2.e + m1.c * m2.h,
                         m1.a * m2.c + m1.b * m2.f + m1.c * m2.i,
                         m1.d * m2.a + m1.e * m2.d + m1.f * m2.g,
                         m1.d * m2.b + m1.e * m2.e + m1.f * m2.h,
                         m1.d * m2.c + m1.e * m2.f + m1.f * m2.i,
                         m1.g * m2.a + m1.h * m2.d + m1.i * m2.g,
                         m1.g * m2.b + m1.h * m2.e + m1.i * m2.h,
                         m1.g * m2.c + m1.h * m2.f + m1.i * m2.i);
    }
    
    inline Point3D operator*(const Matrix3x3& m,const Point3D& p) {
        Point3D ret;
        ret.x = m.a * p.x + m.b * p.y + m.c * p.z;
        ret.y = m.d * p.x + m.e * p.y + m.f * p.z;
        ret.z = m.g * p.x + m.h * p.y + m.i * p.z;
        return ret;
    }
    
}



#endif /* defined(__Misc__GenericTransform__) */
