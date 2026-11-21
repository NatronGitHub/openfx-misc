/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
 * (C) 2018-2021 The Natron Developers
 * (C) 2013-2018 INRIA
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

#include "DistortionModel.h"

#include <cmath>
#include <cfloat> // DBL_EPSILON
#include <algorithm> // max

#include "ofxsMacros.h"

OFXS_NAMESPACE_OFX_ENTER

// Domain guards (no NaN or infinity may be produced, see the NaN audit):
// a denominator whose magnitude is below kDenomMin (e.g. a zero squeeze, filmback, pixel aspect
// or format size, or a radial factor that vanishes at some radius) is replaced by +-kDenomMin.
// Larger values are returned unchanged, so that non-degenerate parameters give the same result.
// A point mapped through such a denominator is more than 1e8 image sizes away, i.e. outside.
#define kDenomMin 1e-8

static inline double
nonZero(double x)
{
    if (std::abs(x) >= kDenomMin) {
        return x;
    }

    return (x < 0.) ? -kDenomMin : kDenomMin;
}

// parameters for Newton method:
#define EPSJAC 1.e-3 // epsilon for Jacobian calculation
#define EPSCONV 1.e-4 // epsilon for convergence test

// A second Newton solver, only used to check convergence for the RoD estimate (distortConverged,
// undistortConverged), when the one used by the render (below) did not converge: the full 2x2
// Jacobian (by finite differences) instead of the column norms, and 50 iterations instead of 10.
// The column-norm step is wrong when the Jacobian is far from diagonal (e.g. a decentered model)
// and converges slowly near a fold, so that ordinary parameters often hit the 10 iterations.
// Finite only: a singular Jacobian stops, and an iterate beyond 1e9 pixels is not converged
// (its function values stay finite for every model). The result may be a root on another branch
// of the model: the caller checks the orientation there.
template<class Func>
static bool
newtonFullJacobian(const Func& f,
                   double tx,
                   double ty,
                   double* xo,
                   double* yo)
{
    double x = tx;
    double y = ty;
    for (int iter = 0; iter < 50; ++iter) {
        double fx, fy, fx10, fy10, fx01, fy01;
        f(x, y, &fx, &fy);
        f(x + EPSJAC, y, &fx10, &fy10);
        f(x, y + EPSJAC, &fx01, &fy01);
        const double a = (fx10 - fx) / EPSJAC;
        const double b = (fx01 - fx) / EPSJAC;
        const double c = (fy10 - fy) / EPSJAC;
        const double d = (fy01 - fy) / EPSJAC;
        const double det = a * d - b * c;
        const double rx = fx - tx;
        const double ry = fy - ty;
        if (std::abs(det) < 1e-12) {
            return false;
        }
        const double dx = (d * rx - b * ry) / det;
        const double dy = (a * ry - c * rx) / det;
        x -= dx;
        y -= dy;
        if (std::abs(x) > 1e9 || std::abs(y) > 1e9) {
            return false;
        }
        if (dx * dx + dy * dy < EPSCONV) {
            double rx1, ry1;
            f(x, y, &rx1, &ry1);
            rx1 -= tx;
            ry1 -= ty;
            if (rx1 * rx1 + ry1 * ry1 < EPSCONV) {
                *xo = x;
                *yo = y;

                return true;
            }
        }
    }

    return false;
}

bool
DistortionModelUndistort::distortNewton(const double xu,
                                        const double yu,
                                        double* xd,
                                        double *yd,
                                        bool checkResidual) const
{
    // build initial guess
    double x = xu;
    double y = yu;
    bool converged = false;

    // usually converges in a couple of iterations, but not beyond a fold of the model (where it is
    // not invertible) or from an initial guess beyond a pole: see the return value
    for (int iter= 0; iter< 10; iter++) {
        // calculate the function gradient at the current guess

        // TODO: analytic derivatives
        double x00, y00, x10, y10, x01, y01;
        undistort(x, y, &x00, &y00);
        undistort(x + EPSJAC, y, &x10, &y10);
        undistort(x, y + EPSJAC, &x01, &y01);

        // perform newton iteration
        x00 -= xu;
        y00 -= yu;
        x10 -= xu;
        y10 -= yu;
        x01 -= xu;
        y01 -= yu;

        x10 -= x00;
        y10 -= y00;
        x01 -= x00;
        y01 -= y00;

        // approximate using finite differences
        const double dx = std::sqrt(x10 * x10 + y10 * y10) / EPSJAC;
        const double dy = std::sqrt(x01 * x01 + y01 * y01) / EPSJAC;

        if (dx < DBL_EPSILON || dy < DBL_EPSILON) { // was dx == 0. || dy == 0.
            break;
        }

        // make a step towards the root
        const double x1 = x - x00 / dx;
        const double y1 = y - y00 / dy;

        x -= x1;
        y -= y1;

        const double dist= x * x + y * y;

        x = x1;
        y = y1;

        //printf("%d : %g,%g: dist= %g\n",iter,x,y,dist);

        // converged?
        if (dist < EPSCONV) {
            converged = true;
            break;
        }
    }

    // default
    *xd = x;
    *yd = y;

    if (converged && checkResidual) {
        // the last step was small, but the derivative may be huge (e.g. near a pole of the
        // model): check that this is a solution. (x,y) is within sqrt(EPSCONV) of the last
        // point where undistort() was evaluated, so this evaluation is as safe as those.
        double xr, yr;
        undistort(x, y, &xr, &yr);
        xr -= xu;
        yr -= yu;
        converged = (xr * xr + yr * yr < EPSCONV);
    }
    if (!converged && checkResidual) {
        // RoD only: try the full-Jacobian solver (the render never gets here)
        struct F {
            const DistortionModelUndistort* m;
            void operator()(double x, double y, double* fx, double* fy) const { m->undistort(x, y, fx, fy); }
        } f = { this };
        converged = newtonFullJacobian(f, xu, yu, xd, yd);
    }

    return converged;
}

void
DistortionModelUndistort::distort(const double xu,
                                  const double yu,
                                  double* xd,
                                  double *yd) const
{
    (void)distortNewton(xu, yu, xd, yd, false);
}

bool
DistortionModelUndistort::distortConverged(double xu,
                                           double yu,
                                           double* xd,
                                           double *yd) const
{
    return distortNewton(xu, yu, xd, yd, true);
}

bool
DistortionModelDistort::undistortNewton(const double xd,
                                        const double yd,
                                        double* xu,
                                        double *yu,
                                        bool checkResidual) const
{
    // build initial guess
    double x = xd;
    double y = yd;
    bool converged = false;

    // usually converges in a couple of iterations, but not beyond a fold of the model (where it is
    // not invertible) or from an initial guess beyond a pole: see the return value
    for (int iter= 0; iter< 10; iter++) {
        // calculate the function gradient at the current guess

        // TODO: analytic derivatives
        double x00, y00, x10, y10, x01, y01;
        distort(x, y, &x00, &y00);
        distort(x + EPSJAC, y, &x10, &y10);
        distort(x, y + EPSJAC, &x01, &y01);

        // perform newton iteration
        x00 -= xd;
        y00 -= yd;
        x10 -= xd;
        y10 -= yd;
        x01 -= xd;
        y01 -= yd;

        x10 -= x00;
        y10 -= y00;
        x01 -= x00;
        y01 -= y00;

        // approximate using finite differences
        const double dx = std::sqrt(x10 * x10 + y10 * y10) / EPSJAC;
        const double dy = std::sqrt(x01 * x01 + y01 * y01) / EPSJAC;

        if (dx < DBL_EPSILON || dy < DBL_EPSILON) { // was dx == 0. || dy == 0.
            break;
        }

        // make a step towards the root
        const double x1 = x - x00 / dx;
        const double y1 = y - y00 / dy;

        x -= x1;
        y -= y1;

        const double dist= x * x + y * y;

        x = x1;
        y = y1;

        //printf("%d : %g,%g: dist= %g\n",iter,x,y,dist);

        // converged?
        if (dist < EPSCONV) {
            converged = true;
            break;
        }
    }
    
    // default
    *xu = x;
    *yu = y;

    if (converged && checkResidual) {
        // the last step was small, but the derivative may be huge (e.g. near a pole of the
        // model): check that this is a solution. (x,y) is within sqrt(EPSCONV) of the last
        // point where distort() was evaluated, so this evaluation is as safe as those.
        double xr, yr;
        distort(x, y, &xr, &yr);
        xr -= xd;
        yr -= yd;
        converged = (xr * xr + yr * yr < EPSCONV);
    }
    if (!converged && checkResidual) {
        // RoD only: try the full-Jacobian solver (the render never gets here)
        struct F {
            const DistortionModelDistort* m;
            void operator()(double x, double y, double* fx, double* fy) const { m->distort(x, y, fx, fy); }
        } f = { this };
        converged = newtonFullJacobian(f, xd, yd, xu, yu);
    }

    return converged;
}

void
DistortionModelDistort::undistort(const double xd,
                                  const double yd,
                                  double* xu,
                                  double *yu) const
{
    (void)undistortNewton(xd, yd, xu, yu, false);
}

bool
DistortionModelDistort::undistortConverged(double xd,
                                           double yd,
                                           double* xu,
                                           double *yu) const
{
    return undistortNewton(xd, yd, xu, yu, true);
}

DistortionModelNuke::DistortionModelNuke(const OfxRectD& format,
                                         double par,
                                         double k1,
                                         double k2,
                                         double cx,
                                         double cy,
                                         double squeeze,
                                         double ax,
                                         double ay)
: _par( nonZero(par) )
, _k1(k1)
, _k2(k2)
, _cx(cx)
, _cy(cy)
, _squeeze(squeeze)
, _ax(ax)
, _ay(ay)
{
    double fx = (format.x2 - format.x1) / 2.;
    double fy = (format.y2 - format.y1) / 2.;
    _f = nonZero( (std::max)(fx, fy) ); // TODO: distortion scaling param for LensDistortion?
    _xSrcCenter = (format.x1 + format.x2) / 2.;
    _ySrcCenter = (format.y1 + format.y2) / 2.;
}


// Nuke's distortion function, reverse engineered from the resulting images on a checkerboard (and a little science, too)
// this function undistorts positions, but is also used to distort the image.
// Similar to the function distortNuke in Obq_LensDistortion.h
static inline void
undistort_nuke(double xd,
               double yd,            // distorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
               double k1,
               double k2,            // radial distortion
               double cx,
               double cy,            // distortion center, (0,0) at center of image
               double squeeze, // anamorphic squeeze
               double ax,
               double ay,            // asymmetric distortion
               double *xu,
               double *yu)             // distorted position in normalized coordinates
{
    // nuke?
    // k1 = radial distortion 1
    // k2 = radial distortion 2
    // squeeze = anamorphic squeeze
    // p1 = asymmetric distortion x
    // p2 = asymmetric distortion y
    double x = (xd - cx);
    double y = (yd - cy);
    double x2 = x * x, y2 = y * y;
    double r2 = x2 + y2;
    double k2r2pk1 = k2 * r2 + k1;
    //double kry = 1 + ((k2r2pk1 + ay)*x2 + k2r2pk1*y2);
    // kry and krx vanish at some radius if k1 or k2 is negative
    double kry = nonZero(1 + (k2r2pk1 * r2 + ay * x2));

    *yu = (y / kry) + cy;
    //double krx = 1 + (k2r2pk1*x2 + (k2r2pk1 + ax)*y2)/squeeze;
    double krx = nonZero(1 + (k2r2pk1 * r2 + ax * y2) / squeeze); // squeeze >= 0.001 (getDistortionModel)
    *xu = (x / krx) + cx;
}


// function used to undistort a point or distort an image
// (xd,yd) = 0,0 at the bottom left of the bottomleft pixel

void
DistortionModelNuke::undistort(double xd, double yd, double* xu, double *yu) const
{
    double xdn = _par * (xd - _xSrcCenter) / _f;
    double ydn = (yd - _ySrcCenter) / _f;
    double sx, sy;
    undistort_nuke(xdn, ydn,
                   _k1, _k2, _cx, _cy, _squeeze, _ax, _ay,
                   &sx, &sy);
    sx /= _par;
    sx *= _f;
    sx += _xSrcCenter;
    sy *= _f;
    sy += _ySrcCenter;

    *xu = sx;
    *yu = sy;
}



DistortionModelPFBarrel::DistortionModelPFBarrel(const OfxRectD& format,
                                                 const OfxPointD& renderScale,
                                                 double c3,
                                                 double c5,
                                                 double xp,
                                                 double yp,
                                                 double squeeze)
: _rs(renderScale)
, _c3(c3)
, _c5(c5)
, _xp(xp)
, _yp(yp)
, _squeeze( nonZero(squeeze) ) // the squeeze parameter may be 0
{
    /*
     double fx = (format.x2 - format.x1) / 2.;
     double fy = (format.y2 - format.y1) / 2.;
     _f = (std::max)(fx, fy); // TODO: distortion scaling param for LensDistortion?
     _xSrcCenter = (format.x1 + format.x2) / 2.;
     _ySrcCenter = (format.y1 + format.y2) / 2.;
     */
    _fw = format.x2 - format.x1;
    _fh = format.y2 - format.y1;
    _normx = std::sqrt( 2.0 / nonZero(_fw * _fw + _fh * _fh) ); // the format may be empty
}

// function used to undistort a point or distort an image
// (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
void
DistortionModelPFBarrel::undistort(double xd, double yd, double* xu, double *yu) const
{
    // PFBarrel model seems to apply to the corner of the corresponding full-res pixel
    // at least that's what the official PFBarrel Nuke plugin does
    xd -= 0.5 * _rs.x;
    yd -= 0.5 * _rs.y;

    double centx = _xp * _fw * _normx;
    double x = xd * _normx;
    // remove anamorphic squeeze
    double centy = _yp * _fh * _normx / _squeeze;
    double y = yd * _normx / _squeeze;

    // distort
    const double px = x - centx;
    const double py = y - centy;

    const double px2 = px * px;
    const double py2 = py * py;
    //const double r = std::sqrt(px2 + py2);
    const double r2 = px2 + py2;
    //#ifdef THREE_POWER
    //const double dr_r= r2*r*(C3C5.x+r2*C3C5.y);
    //#else
    const double dr_r= r2 * (_c3+ r2 * _c5);
    //#endif

    // re-apply squeeze and remove normalization
    x += px * dr_r;
    x /= _normx;
    y += py * dr_r;
    y *= _squeeze / _normx;

    x += 0.5 * _rs.x;
    y += 0.5 * _rs.y;

    *xu = x;
    *yu = y;
}




/////////////////////// 3DEqualizer


/// this base class handles the 4 fov parameters & the seven built-in parameters
DistortionModel3DEBase::DistortionModel3DEBase(const OfxRectD& format,
                                               const OfxPointD& renderScale,
                                               double xa_fov_unit,
                                               double ya_fov_unit,
                                               double xb_fov_unit,
                                               double yb_fov_unit,
                                               double fl_cm,
                                               double fd_cm,
                                               double w_fb_cm,
                                               double h_fb_cm,
                                               double x_lco_cm,
                                               double y_lco_cm,
                                               double pa)
: _format(format)
, _rs(renderScale)
, _w( nonZero(format.x2 - format.x1) ) // the format may be empty
, _h( nonZero(format.y2 - format.y1) )
, _xa_fov_unit(xa_fov_unit)
, _ya_fov_unit(ya_fov_unit)
, _xb_fov_unit(xb_fov_unit)
, _yb_fov_unit(yb_fov_unit)
, _xd_fov_unit( nonZero(_xb_fov_unit - _xa_fov_unit) ) // the fov may be empty
, _yd_fov_unit( nonZero(_yb_fov_unit - _ya_fov_unit) )
, _fl_cm(fl_cm)
, _fd_cm(fd_cm)
, _w_fb_cm( nonZero(w_fb_cm) ) // the filmback size parameters may be 0
, _h_fb_cm( nonZero(h_fb_cm) )
, _x_lco_cm(x_lco_cm)
, _y_lco_cm(y_lco_cm)
, _pa( nonZero(pa) ) // the pixel aspect parameter may be 0
{
    _r_fb_cm = std::sqrt(_w_fb_cm * _w_fb_cm + _h_fb_cm * _h_fb_cm) / 2.0; // > 0
}




/// this class handles the Degree-2 anamorphic and degree-4 radial mixed model
DistortionModel3DEClassic::DistortionModel3DEClassic(const OfxRectD& format,
                                                     const OfxPointD& renderScale,
                                                     double xa_fov_unit,
                                                     double ya_fov_unit,
                                                     double xb_fov_unit,
                                                     double yb_fov_unit,
                                                     double fl_cm,
                                                     double fd_cm,
                                                     double w_fb_cm,
                                                     double h_fb_cm,
                                                     double x_lco_cm,
                                                     double y_lco_cm,
                                                     double pa,
                                                     double ld,
                                                     double sq,
                                                     double cx,
                                                     double cy,
                                                     double qu)
: DistortionModel3DEBase(format, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
, _ld(ld)
, _sq( nonZero(sq) ) // the anamorphic squeeze parameter may be 0
, _cx(cx)
, _cy(cy)
, _qu(qu)
, _cxx(_ld / _sq)
, _cxy( (_ld + _cx) / _sq )
, _cyx(_ld + _cy)
, _cyy(_ld)
, _cxxx(_qu / _sq)
, _cxxy(2.0 * _qu / _sq)
, _cxyy(_qu / _sq)
, _cyxx(_qu)
, _cyyx(2.0 * _qu)
, _cyyy(_qu)
{
}

// Remove distortion. p is a point in diagonally normalized coordinates.
void
DistortionModel3DEClassic::undistort_dn(double xd, double yd, double* xu, double *yu) const
{
    double p0_2 = xd * xd;
    double p1_2 = yd * yd;
    double p0_4 = p0_2 * p0_2;
    double p1_4 = p1_2 * p1_2;
    double p01_2 = p0_2 * p1_2;

    *xu = xd * (1 + _cxx * p0_2 + _cxy * p1_2 + _cxxx * p0_4 + _cxxy * p01_2 + _cxyy * p1_4);
    *yu = yd * (1 + _cyx * p0_2 + _cyy * p1_2 + _cyxx * p0_4 + _cyyx * p01_2 + _cyyy * p1_4);
}


// Degree-6 anamorphic model
DistortionModel3DEAnamorphic6::DistortionModel3DEAnamorphic6(const OfxRectD& format,
                                                             const OfxPointD& renderScale,
                                                             double xa_fov_unit,
                                                             double ya_fov_unit,
                                                             double xb_fov_unit,
                                                             double yb_fov_unit,
                                                             double fl_cm,
                                                             double fd_cm,
                                                             double w_fb_cm,
                                                             double h_fb_cm,
                                                             double x_lco_cm,
                                                             double y_lco_cm,
                                                             double pa,
                                                             double cx02,
                                                             double cy02,
                                                             double cx22,
                                                             double cy22,
                                                             double cx04,
                                                             double cy04,
                                                             double cx24,
                                                             double cy24,
                                                             double cx44,
                                                             double cy44,
                                                             double cx06,
                                                             double cy06,
                                                             double cx26,
                                                             double cy26,
                                                             double cx46,
                                                             double cy46,
                                                             double cx66,
                                                             double cy66)
: DistortionModel3DEBase(format, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
{
    // generic_anamorphic_distortion<VEC2,MAT2,6>::prepare()
    _cx_for_x2 = cx02 + cx22;
    _cx_for_y2 = cx02 - cx22;

    _cx_for_x4 = cx04 + cx24 + cx44;
    _cx_for_x2_y2 = 2 * cx04 - 6 * cx44;
    _cx_for_y4 = cx04 - cx24 + cx44;

    _cx_for_x6 = cx06 + cx26 + cx46 + cx66;
    _cx_for_x4_y2 = 3 * cx06 + cx26 - 5 * cx46 - 15 * cx66;
    _cx_for_x2_y4 = 3 * cx06 - cx26 - 5 * cx46 + 15 * cx66;
    _cx_for_y6 = cx06 - cx26 + cx46 - cx66;

    _cy_for_x2 = cy02 + cy22;
    _cy_for_y2 = cy02 - cy22;

    _cy_for_x4 = cy04 + cy24 + cy44;
    _cy_for_x2_y2 = 2 * cy04 - 6 * cy44;
    _cy_for_y4 = cy04 - cy24 + cy44;

    _cy_for_x6 = cy06 + cy26 + cy46 + cy66;
    _cy_for_x4_y2 = 3 * cy06 + cy26 - 5 * cy46 - 15 * cy66;
    _cy_for_x2_y4 = 3 * cy06 - cy26 - 5 * cy46 + 15 * cy66;
    _cy_for_y6 = cy06 - cy26 + cy46 - cy66;
}

// Remove distortion. p is a point in diagonally normalized coordinates.
void
DistortionModel3DEAnamorphic6::undistort_dn(double xd, double yd, double* xu, double *yu) const
{
    // _anamorphic.eval(
    double x = xd;
    double y = yd;
    double x2 = x * x;
    double x4 = x2 * x2;
    double x6 = x4 * x2;
    double y2 = y * y;
    double y4 = y2 * y2;
    double y6 = y4 * y2;
    double xq = x * (1.0
                     + x2 * _cx_for_x2 + y2 * _cx_for_y2
                     + x4 * _cx_for_x4 + x2 * y2 * _cx_for_x2_y2 + y4 * _cx_for_y4
                     + x6 * _cx_for_x6 + x4 * y2 * _cx_for_x4_y2 + x2 * y4 * _cx_for_x2_y4 + y6 * _cx_for_y6);
    double yq = y * (1.0
                     + x2 * _cy_for_x2 + y2 * _cy_for_y2
                     + x4 * _cy_for_x4 + x2 * y2 * _cy_for_x2_y2 + y4 * _cy_for_y4
                     + x6 * _cy_for_x6 + x4 * y2 * _cy_for_x4_y2 + x2 * y4 * _cy_for_x2_y4 + y6 * _cy_for_y6);

    *xu = xq;
    *yu = yq;
}

// radial lens distortion model with equisolid-angle fisheye projection
DistortionModel3DEFishEye8::DistortionModel3DEFishEye8(const OfxRectD& format,
                                                       const OfxPointD& renderScale,
                                                       double xa_fov_unit,
                                                       double ya_fov_unit,
                                                       double xb_fov_unit,
                                                       double yb_fov_unit,
                                                       double fl_cm,
                                                       double fd_cm,
                                                       double w_fb_cm,
                                                       double h_fb_cm,
                                                       double x_lco_cm,
                                                       double y_lco_cm,
                                                       double pa,
                                                       double c2,
                                                       double c4,
                                                       double c6,
                                                       double c8)
: DistortionModel3DEBase(format, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
, _c2(c2)
, _c4(c4)
, _c6(c6)
, _c8(c8)
{
}

// Remove distortion. p is a point in diagonally normalized coordinates.
void
DistortionModel3DEFishEye8::undistort_dn(double xd, double yd, double* xu, double *yu) const
{
    double x_plain, y_plain;
    esa2plain(xd, yd, &x_plain, &y_plain);

    double r2 = x_plain * x_plain + y_plain * y_plain;
    double r4 = r2 * r2;
    double r6 = r4 * r2;
    double r8 = r4 * r4;

    double q = 1. + _c2 * r2 + _c4 * r4 + _c6 * r6 + _c8 * r8;
    *xu = x_plain * q;
    *yu = y_plain * q;

    // Clipping to a reasonable area, still n times as large as the image.
    //if(norm2(q_dn) > 50.0) q_dn = 50.0 * unit(q_dn);
}

void
DistortionModel3DEFishEye8::esa2plain(double x_esa_dn, double y_esa_dn, double *x_plain_dn, double *y_plain_dn) const
{
    double f_dn = nonZero(_fl_cm / _r_fb_cm); // the focal length parameter may be 0
    // Remove fisheye projection
    double r_esa_dn = std::sqrt(x_esa_dn * x_esa_dn + y_esa_dn * y_esa_dn);
    if (r_esa_dn <= 0) {
        // avoid division by zero
        *x_plain_dn = *y_plain_dn = 0.;
        return;
    }
    double arg = r_esa_dn / (2 * f_dn);
    // Black areas, undefined. Clamp to the domain of asin (f_dn < 0 if the host ignores the range of the focal length).
    double arg_clip = (std::max)( -1., (std::min)(1., arg) );
    double phi = 2 * std::asin(arg_clip);
    double r_plain_dn;
    if (phi >= OFXS_PI / 2.0) {
        r_plain_dn = 5.;
    } else {
        r_plain_dn = (std::min)( 5., f_dn * std::tan(phi) );
    }
    *x_plain_dn = x_esa_dn * r_plain_dn / r_esa_dn;
    *y_plain_dn = y_esa_dn * r_plain_dn / r_esa_dn;
}

/// this class handles the radial distortion with decentering and optional compensation for beam-splitter artefacts model
DistortionModel3DEStandard::DistortionModel3DEStandard(const OfxRectD& format,
                                                       const OfxPointD& renderScale,
                                                       double xa_fov_unit,
                                                       double ya_fov_unit,
                                                       double xb_fov_unit,
                                                       double yb_fov_unit,
                                                       double fl_cm,
                                                       double fd_cm,
                                                       double w_fb_cm,
                                                       double h_fb_cm,
                                                       double x_lco_cm,
                                                       double y_lco_cm,
                                                       double pa,
                                                       double c2,
                                                       double u1,
                                                       double v1,
                                                       double c4,
                                                       double u3,
                                                       double v3,
                                                       double phi,
                                                       double b)
: DistortionModel3DEBase(format, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
, _c2(c2)
, _u1(u1)
, _v1(v1)
, _c4(c4)
, _u3(u3)
, _v3(v3)
{
    //calc_m()
    // b <= -1 has no meaning (and gives sqrt of a negative number or a division by zero): clamp 1 + b
    double q = std::sqrt( (std::max)(kDenomMin * kDenomMin, 1.0 + b) );
    double c = std::cos(phi * OFXS_PI / 180.0);
    double s = std::sin(phi * OFXS_PI / 180.0);
    //mat2_type para = tensq(vec2_type(cos(_phi * OFXS_PI / 180.0),sin(_phi * OFXS_PI / 180.0)));
    //m = _b * para + mat2_type(1.0);
    // m = [[mxx, mxy],[myx,myy]] (m is symmetric)
    _mxx = c*c*q + s*s/q;
    _mxy = (q - 1.0/q)*c*s;
    _myy = c*c/q + s*s*q;
}

// Remove distortion. p is a point in diagonally normalized coordinates.
void
DistortionModel3DEStandard::undistort_dn(double xd, double yd, double* xu, double *yu) const
{
    // _radial.eval(
    double x_dn,y_dn;
    double x = xd;
    double y = yd;
    double x2 = x * x;
    double y2 = y * y;
    double xy = x * y;
    double r2 = x2 + y2;
    double r4 = r2 * r2;
    x_dn = x * (1.0 + _c2 * r2 + _c4 * r4) + (r2 + 2.0 * x2) * (_u1 + _u3 * r2) + 2.0 * xy * (_v1 + _v3 * r2);
    y_dn = y * (1.0 + _c2 * r2 + _c4 * r4) + (r2 + 2.0 * y2) * (_v1 + _v3 * r2) + 2.0 * xy * (_u1 + _u3 * r2);

    // _cylindric.eval(
    // see cylindric_extender_2
    //(xu,yu) = m * (x_dn, y_dn);
    *xu = _mxx * x_dn + _mxy * y_dn;
    *yu = _mxy * x_dn + _myy * y_dn;
}

// Degree-4 anamorphic model with anamorphic lens rotation
DistortionModel3DEAnamorphic4::DistortionModel3DEAnamorphic4(const OfxRectD& format,
                                                             const OfxPointD& renderScale,
                                                             double xa_fov_unit,
                                                             double ya_fov_unit,
                                                             double xb_fov_unit,
                                                             double yb_fov_unit,
                                                             double fl_cm,
                                                             double fd_cm,
                                                             double w_fb_cm,
                                                             double h_fb_cm,
                                                             double x_lco_cm,
                                                             double y_lco_cm,
                                                             double pa,
                                                             double cx02,
                                                             double cy02,
                                                             double cx22,
                                                             double cy22,
                                                             double cx04,
                                                             double cy04,
                                                             double cx24,
                                                             double cy24,
                                                             double cx44,
                                                             double cy44,
                                                             double phi,
                                                             double sqx,
                                                             double sqy)
: DistortionModel3DEBase(format, renderScale, xa_fov_unit, ya_fov_unit, xb_fov_unit, yb_fov_unit, fl_cm, fd_cm, w_fb_cm, h_fb_cm, x_lco_cm, y_lco_cm, pa)
, _cosphi( std::cos(phi * OFXS_PI / 180.0) )
, _sinphi( std::sin(phi * OFXS_PI / 180.0) )
, _sqx(sqx)
, _sqy(sqy)
{
    // generic_anamorphic_distortion<VEC2,MAT2,4>::prepare()
    _cx_for_x2 = cx02 + cx22;
    _cx_for_y2 = cx02 - cx22;
    
    _cx_for_x4 = cx04 + cx24 + cx44;
    _cx_for_x2_y2 = 2 * cx04 - 6 * cx44;
    _cx_for_y4 = cx04 - cx24 + cx44;
    
    _cy_for_x2 = cy02 + cy22;
    _cy_for_y2 = cy02 - cy22;
    
    _cy_for_x4 = cy04 + cy24 + cy44;
    _cy_for_x2_y2 = 2 * cy04 - 6 * cy44;
    _cy_for_y4 = cy04 - cy24 + cy44;
}


// Remove distortion. p is a point in diagonally normalized coordinates.
void
DistortionModel3DEAnamorphic4::undistort_dn(double xd, double yd, double* xu, double *yu) const
{
    /*
     _rotation.eval(
     _squeeze_x.eval(
     _squeeze_y.eval(
     _pa.eval(
     _anamorphic.eval(
     _rotation.eval_inv(
     _pa.eval_inv(
     */
    
    //_pa.eval_inv(
    xd /= _pa;
    // _rotation.eval_inv(
    //   _m_rot = mat2_type(cos(_phi),-sin(_phi),sin(_phi),cos(_phi));
    //   _inv_m_rot = trans(_m_rot);
    double x = _cosphi * xd + _sinphi * yd;
    double y = -_sinphi * xd + _cosphi * yd;
    // _anamorphic.eval(
    double x2 = x * x;
    double x4 = x2 * x2;
    double y2 = y * y;
    double y4 = y2 * y2;
    double xq = x * (1.0
                     + x2 * _cx_for_x2 + y2 * _cx_for_y2
                     + x4 * _cx_for_x4 + x2 * y2 * _cx_for_x2_y2 + y4 * _cx_for_y4);
    double yq = y * (1.0
                     + x2 * _cy_for_x2 + y2 * _cy_for_y2
                     + x4 * _cy_for_x4 + x2 * y2 * _cy_for_x2_y2 + y4 * _cy_for_y4);
    //double xq = x;
    //double yq = y;
    // _pa.eval(
    xq *= _pa;
    // _squeeze_y.eval(
    yq *= _sqy;
    // _squeeze_x.eval(
    xq *= _sqx;
    // _rotation.eval(
    x = _cosphi * xq - _sinphi * yq;
    y = _sinphi * xq + _cosphi * yq;
    
    *xu = x;
    *yu = y;
}

// see:
// http://wiki.panotools.org/Lens_correction_model
// http://hugin.sourceforge.net/docs/manual/Lens_correction_model.html
DistortionModelPanoTools::DistortionModelPanoTools(const OfxRectD& format,
                                                   const OfxPointD& renderScale,
                                                   double par,
                                                   double a,
                                                   double b,
                                                   double c,
                                                   double d,
                                                   double e,
                                                   double g,
                                                   double t)
: _rs(renderScale)
, _par( nonZero(par) )
, _a(a)
, _b(b)
, _c(c)
, _d(d)
, _e(e)
, _g(g)
, _t(t)
{
    // Normalized means here that the largest circle that completely fits into an image is said to have radius=1.0 . (In other words, radius=1.0 is half the smaller side of the image.)
    double fx = format.x2 - format.x1;
    double fy = format.y2 - format.y1;
    _f = nonZero( (std::min)(fx, fy)  / 2. ); // the format may be empty
    _xSrcCenter = (format.x1 + format.x2) / 2.;
    _ySrcCenter = (format.y1 + format.y2) / 2.;
    _g /= nonZero(fy);
    _t /= nonZero(fx);
}


static inline void
distort_panotools(double xu,
                  double yu,            // distorted position in normalized coordinates ([-1..1] on the smallest image dimension, (0,0 at image center))
                  double a,
                  double b,            // radial distortion
                  double c,
                  double *xd,
                  double *yd)             // distorted position in normalized coordinates

{
    double x = xu;
    double y = yu;
    double x2 = x * x, y2 = y * y;
    double r2 = x2 + y2;
    double d = 1 - (a + b + c);
    double r = std::sqrt(r2);
    double scale = (a * r2 + c) * r + b * r2 + d;
    *xd = x * scale;
    *yd = y * scale;
}


// function used to distort a point or undistort an image
// (xd,yd) = 0,0 at the bottom left of the bottomleft pixel

void
DistortionModelPanoTools::distort(double xu, double yu, double* xd, double *yd) const
{
    // see http://wiki.panotools.org/Lens_correction_model#Lens_or_image_shift_d_.26_e_parameters for _d and _e
    double xun = _par * (xu - _xSrcCenter) / _f; // panotools don't shift back to center
    double yun = (yu - _ySrcCenter) / _f;
    double sx, sy;
    distort_panotools(xun, yun,
                        _a, _b, _c,
                        &sx, &sy);
    sx /= _par;
    sx *= _f;
    sy *= _f;
#if 0
    double sx0 = sx, sy0 = sy;
    sx += _xSrcCenter + _d * _rs.x - _g * _rs.x * sy0;
    sy += _ySrcCenter - _e * _rs.y - _t * _rs.y * sx0; // y is reversed
#else
    sx += _d * _rs.x;
    sy -= _e * _rs.y; // y is reversed
    double sx0 = sx, sy0 = sy;
    sx += _xSrcCenter - _g * _rs.x * sy0;
    sy += _ySrcCenter - _t * _rs.y * sx0; // y is reversed
#endif

    *xd = sx;
    *yd = sy;
}


#if 0
// see https://github.com/Itseez/opencv/blob/master/modules/imgproc/src/undistort.cpp
static inline void
distort_opencv(double xu,
               double yu,            // undistorted position in normalized coordinates ([-1..1] on the largest image dimension, (0,0 at image center))
               double k1,
               double k2,
               double k3,
               double p1,
               double p2,
               double cx,
               double cy,
               double squeeze,
               double *xd,
               double *yd)      // distorted position in normalized coordinates
{
    // opencv
    const double k4 = 0.;
    const double k5 = 0.;
    const double k6 = 0.;
    const double s1 = 0.;
    const double s2 = 0.;
    const double s3 = 0.;
    const double s4 = 0.;
    double x = (xu - cx) * squeeze;
    double y = yu - cy;
    double x2 = x * x, y2 = y * y;
    double r2 = x2 + y2;
    double _2xy = 2 * x * y;
    double kr = (1 + ( (k3 * r2 + k2) * r2 + k1 ) * r2) / (1 + ( (k6 * r2 + k5) * r2 + k4 ) * r2);

    *xd = ( (x * kr + p1 * _2xy + p2 * (r2 + 2 * x2) + s1 * r2 + s2 * r2 * r2) ) / squeeze + cx;
    *yd = (y * kr + p1 * (r2 + 2 * y2) + p2 * _2xy + s3 * r2 + s4 * r2 * r2) + cy;
}

#endif

OFXS_NAMESPACE_OFX_EXIT
