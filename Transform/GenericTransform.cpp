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
#include "GenericTransform.h"

#include <cmath>

using namespace Transform2D;

Matrix3x3::Matrix3x3()
: a(1), b(0), c(0), d(1), e(0), f(0) , g(0) , h(0) , i(0) {}

Matrix3x3::Matrix3x3(double a, double b, double c, double d, double e, double f,double g,double h,double i)
: a(a),b(b),c(c),d(d),e(e),f(f),g(g),h(h),i(i) {}

Matrix3x3::Matrix3x3(const Matrix3x3& mat) : a(mat.a), b(mat.b), c(mat.c), d(mat.d), e(mat.e), f(mat.f) , g(mat.g) , h(mat.h) , i(mat.i) {}

Matrix3x3& Matrix3x3::operator=(const Matrix3x3& m)
{ a = m.a; b = m.b; c = m.c; d = m.d; e = m.e; f = m.f; g = m.g; h = m.h; i = m.i; return *this; }


Matrix3x3& Matrix3x3::operator*=(const Matrix3x3& m) { (*this) = (*this)*m; return *this; }

double Matrix3x3::determinant() const
{
    double ret;
    ret = a * (e * i - h * f);
    ret -= b * (d * i - g * f);
    ret += c * (d * h - g * e);
    return ret;
}

Matrix3x3 Matrix3x3::getScaleAdjoint(double s) const
{
    Matrix3x3 ret;
    ret.a = (s) * (e * i - h * f);
    ret.d = (s) * (f * g - d * i);
    ret.g = (s) * (d * h - e * g);
    
    ret.b = (s) * (c * h - b * i);
    ret.e = (s) * (a * i - c * g);
    ret.h = (s) * (b * g - a * h);
    
    ret.c = (s) * (b * f - c * e);
    ret.f = (s) * (c * d - a * f);
    ret.i = (s) * (a * e - b * d);
    return ret;
}

Matrix3x3 Matrix3x3::getInverse() const
{
    return getScaleAdjoint(1. / determinant());
}

Matrix3x3 Matrix3x3::getRotate(double rads)
{
    double c = std::cos(rads);
    double s = std::sin(rads);
    return Matrix3x3(c,s,0,-s,c,0,0,0,1);
}

Matrix3x3 Matrix3x3::getRotateAroundPoint(double rads, double px, double py)
{
    return getTranslate(px, py) * (getRotate(rads) * getTranslate(-px, -py));
}

Matrix3x3 Matrix3x3::getTranslate(double x, double y)
{
    return Matrix3x3(1., 0., x,
                     0., 1., y,
                     0., 0., 1.);
}

Matrix3x3 Matrix3x3::getScale(double x, double y)
{
    return Matrix3x3(x,  0., 0.,
                     0., y,  0.,
                     0., 0., 1.);
}

Matrix3x3 Matrix3x3::getScale(double s)
{
    return getScale(s, s);
}

Matrix3x3 Matrix3x3::getScaleAroundPoint(double scaleX, double scaleY, double px, double py)
{
    return getTranslate(px,py) * (getScale(scaleX,scaleY) * getTranslate(-px, -py));
}

Matrix3x3 Matrix3x3::getSkewXY(double skewX, double skewY, bool skewOrderYX)
{
    return Matrix3x3(skewOrderYX ? 1. : (1. + skewX*skewY), skewX, 0.,
                     skewY, skewOrderYX ? (1. + skewX*skewY) : 1, 0.,
                     0., 0., 1.);
}

// matrix transform from destination to source
Matrix3x3 Matrix3x3::getInverseTransform(double translateX, double translateY,
                                  double scaleX, double scaleY,
                                  double skewX,
                                  double skewY,
                                  bool skewOrderYX,
                                  double rads,
                                  double centerX, double centerY)
{
    ///1) We translate to the origin.
    ///2) We scale
    ///3) We rotate
    ///4) We translate back to the center
    ///5) We apply the global translation
    ///6) We apply skewX and skewY in the right order

    // since this is the inverse, oerations are in reverse order
    return (getTranslate(centerX, centerY) *
            getScale(1. / scaleX, 1. / scaleY) *
            getSkewXY(-skewX, -skewY, !skewOrderYX) *
            getRotate(rads) *
            getTranslate(-translateX,-translateY) *
            getTranslate(-centerX,-centerY));
}

// matrix transform from source to destination
Matrix3x3 Matrix3x3::getTransform(double translateX, double translateY,
                                  double scaleX, double scaleY,
                                  double skewX,
                                  double skewY,
                                  bool skewOrderYX,
                                  double rads,
                                  double centerX, double centerY)
{
    ///1) We translate to the origin.
    ///2) We scale
    ///3) We rotate
    ///4) We translate back to the center
    ///5) We apply the global translation
    ///6) We apply skewX and skewY in the right order

    return (getTranslate(centerX, centerY) *
            getTranslate(translateX, translateY) *
            getRotate(-rads) *
            getSkewXY(skewX, skewY, skewOrderYX) *
            getScale(scaleX, scaleY) *
            getTranslate(-centerX,-centerY));
}