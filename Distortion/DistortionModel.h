/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
 * Copyright (C) 2013-2017 INRIA
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

#ifndef DistortionModels_h
#define DistortionModels_h

#include "ofxCore.h"
#include "ofxsMacros.h"

OFXS_NAMESPACE_OFX_ENTER

// a generic distortion model abstract class (distortion parameters are added by the derived class)
class DistortionModel
{
protected:
    DistortionModel() {};

public:
    virtual ~DistortionModel() {};

private:  // noncopyable
    DistortionModel( const DistortionModel& );
    DistortionModel& operator=( const DistortionModel& );

public:
    // function used to distort a point or undistort an image
    virtual void distort(double xu, double yu, double* xd, double *yd) const = 0;

    // function used to undistort a point or distort an image
    virtual void undistort(double xd, double yd, double* xu, double *yu) const = 0;
};

// a distortion model class where ony the undistort function is given, and distort is solved by Newton
class DistortionModelUndistort
: public DistortionModel
{
protected:
    DistortionModelUndistort() {};

    virtual ~DistortionModelUndistort() {};

private:
    // function used to distort a point or undistort an image
    virtual void distort(const double xu,
                         const double yu,
                         double* xd,
                         double *yd) const OVERRIDE FINAL;

    // function used to undistort a point or distort an image
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE = 0;
};

class DistortionModelNuke
: public DistortionModelUndistort
{
public:
    DistortionModelNuke(const OfxRectI& format,
                        double par,
                        double k1,
                        double k2,
                        double cx,
                        double cy,
                        double squeeze,
                        double ax,
                        double ay);

    virtual ~DistortionModelNuke() {};

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

    double _par;
    double _f;
    double _xSrcCenter;
    double _ySrcCenter;
    double _k1;
    double _k2;
    double _cx;
    double _cy;
    double _squeeze;
    double _ax;
    double _ay;
};



class DistortionModelPFBarrel
: public DistortionModelUndistort
{
public:
    DistortionModelPFBarrel(const OfxRectI& format,
                            const OfxPointD& renderScale,
                            double c3,
                            double c5,
                            double xp,
                            double yp,
                            double squeeze);

    virtual ~DistortionModelPFBarrel() {};

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

    OfxPointD _rs;
    double _c3;
    double _c5;
    double _xp;
    double _yp;
    double _squeeze;
    double _normx;
    double _fw, _fh;
};



/////////////////////// 3DEqualizer


/// this base class handles the 4 fov parameters & the seven built-in parameters
class DistortionModel3DEBase
: public DistortionModelUndistort
{
protected:
    DistortionModel3DEBase(const OfxRectI& format,
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
                           double pa);

    virtual ~DistortionModel3DEBase() {};

    // the following must be implemented by each model, and corresponds to operator () in ldpk
    // Remove distortion. p is a point in diagonally normalized coordinates.
    virtual void undistort_dn(double xd, double yd, double* xu, double *yu) const = 0;

private:
    // function used to undistort a point or distort an image
    // (xd,yd) = 0,0 at the bottom left of the bottomleft pixel
    virtual void undistort(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL
    {
        OfxPointD p_pix = {xd, yd};
        OfxPointD p_dn;
        map_pix_to_dn(p_pix, &p_dn);
        OfxPointD p_dn_u;
        undistort_dn(p_dn.x, p_dn.y, &p_dn_u.x, &p_dn_u.y);
        map_dn_to_pix(p_dn_u, &p_pix);
        *xu = p_pix.x;
        *yu = p_pix.y;
    }

private:
    void
    map_pix_to_dn(const OfxPointD& p_pix, OfxPointD* p_dn) const
    {
        OfxPointD p_unit;
        map_pix_to_unit(p_pix, &p_unit);
        map_unit_to_dn(p_unit, p_dn);
    }

    // The result already contains the (half,half) shift.
    void
    map_dn_to_pix(const OfxPointD& p_dn, OfxPointD* p_pix) const
    {
        OfxPointD p_unit;
        map_dn_to_unit(p_dn, &p_unit);
        map_unit_to_pix(p_unit, p_pix);
    }

    void
    map_unit_to_dn(const OfxPointD& p_unit, OfxPointD* p_dn) const
    {
        double p_cm_x = (p_unit.x - 1.0/2.0) * _w_fb_cm - _x_lco_cm;
        double p_cm_y = (p_unit.y - 1.0/2.0) * _h_fb_cm - _y_lco_cm;
        p_dn->x = p_cm_x / _r_fb_cm;
        p_dn->y = p_cm_y / _r_fb_cm;
    }

    void
    map_dn_to_unit(const OfxPointD& p_dn, OfxPointD* p_unit) const
    {
        double p_cm_x = p_dn.x * _r_fb_cm + _w_fb_cm / 2 + _x_lco_cm;
        double p_cm_y = p_dn.y * _r_fb_cm + _h_fb_cm / 2 + _y_lco_cm;
        p_unit->x = p_cm_x / _w_fb_cm;
        p_unit->y = p_cm_y / _h_fb_cm;
    }

#if 0
    // the following twho funcs are for when 0,0 is the center of the bottomleft pixel (see nuke plugin)
    void
    map_pix_to_unit(const OfxPointI& p_pix, OfxPointD* p_unit) const
    {
        // We construct (x_s,y_s) in a way, so that the image area is mapped to the unit interval [0,1]^2,
        // which is required by our 3DE4 lens distortion plugin class. Nuke's coordinates are pixel based,
        // (0,0) is the left lower corner of the left lower pixel, while (1,1) is the right upper corner
        // of that pixel. The center of any pixel (ix,iy) is (ix+0.5,iy+0.5), so we add 0.5 here.
        double x_s = (0.5 + p_pix.x) / _w;
        double y_s = (0.5 + p_pix.y) / _h;
        p_unit->x = map_in_fov_x(x_s);
        p_unit->y = map_in_fov_y(y_s);
    }

    void
    map_unit_to_pix(const OfxPointD& p_unit, OfxPointD* p_pix) const
    {
        // The result already contains the (half,half) shift. Reformulate in Nuke's coordinates. Weave "out" 3DE4's field of view.
        p_pix->x = map_out_fov_x(p_unit.x) * _w;
        p_pix->y = map_out_fov_y(p_unit.y) * _h;
    }
#endif

    void
    map_pix_to_unit(const OfxPointD& p_pix, OfxPointD* p_unit) const
    {
        double x_s = p_pix.x / _w;
        double y_s = p_pix.y / _h;
        p_unit->x = map_in_fov_x(x_s);
        p_unit->y = map_in_fov_y(y_s);
    }

    void
    map_unit_to_pix(const OfxPointD& p_unit, OfxPointD* p_pix) const
    {
        // The result already contains the (half,half) shift. Reformulate in Nuke's coordinates. Weave "out" 3DE4's field of view.
        p_pix->x = map_out_fov_x(p_unit.x) * _w;
        p_pix->y = map_out_fov_y(p_unit.y) * _h;
    }

    // Map x-coordinate from unit cordinates to fov coordinates.
    double
    map_in_fov_x(double x_unit) const
    {
        return  (x_unit - _xa_fov_unit) / _xd_fov_unit;
    }

    // Map y-coordinate from unit cordinates to fov coordinates.
    double
    map_in_fov_y(double y_unit) const
    {
        return  (y_unit - _ya_fov_unit) / _yd_fov_unit;
    }

    // Map x-coordinate from fov cordinates to unit coordinates.
    double
    map_out_fov_x(double x_fov) const
    {
        return x_fov * _xd_fov_unit + _xa_fov_unit;
    }

    // Map y-coordinate from fov cordinates to unit coordinates.
    double
    map_out_fov_y(double y_fov) const
    {
        return y_fov * _yd_fov_unit + _ya_fov_unit;
    }

protected:

    OfxRectI _format;
    OfxPointD _rs;
        double _w;
        double _h;
    double _xa_fov_unit;
    double _ya_fov_unit;
    double _xb_fov_unit;
    double _yb_fov_unit;
    double _xd_fov_unit;
    double _yd_fov_unit;
    double _fl_cm;
    double _fd_cm;
    double _w_fb_cm;
    double _h_fb_cm;
    double _x_lco_cm;
    double _y_lco_cm;
    double _pa;
    double _r_fb_cm;
};

/// this class handles the Degree-2 anamorphic and degree-4 radial mixed model
class DistortionModel3DEClassic
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEClassic(const OfxRectI& format,
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
                              double qu);

    virtual ~DistortionModel3DEClassic() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

private:
    double _ld;
    double _sq;
    double _cx;
    double _cy;
    double _qu;
    double _cxx;
    double _cxy;
    double _cyx;
    double _cyy;
    double _cxxx;
    double _cxxy;
    double _cxyy;
    double _cyxx;
    double _cyyx;
    double _cyyy;
};

// Degree-6 anamorphic model
class DistortionModel3DEAnamorphic6
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEAnamorphic6(const OfxRectI& format,
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
                                  double cy66);

    virtual ~DistortionModel3DEAnamorphic6() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

private:
    double _cx_for_x2, _cx_for_y2, _cx_for_x4, _cx_for_x2_y2, _cx_for_y4, _cx_for_x6, _cx_for_x4_y2, _cx_for_x2_y4, _cx_for_y6;
    double _cy_for_x2, _cy_for_y2, _cy_for_x4, _cy_for_x2_y2, _cy_for_y4, _cy_for_x6, _cy_for_x4_y2, _cy_for_x2_y4, _cy_for_y6;
};

// radial lens distortion model with equisolid-angle fisheye projection
class DistortionModel3DEFishEye8
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEFishEye8(const OfxRectI& format,
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
                               double c8);

    virtual ~DistortionModel3DEFishEye8() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

    void
    esa2plain(double x_esa_dn, double y_esa_dn, double *x_plain_dn, double *y_plain_dn) const;

private:
    double _c2;
    double _c4;
    double _c6;
    double _c8;
};

/// this class handles the radial distortion with decentering and optional compensation for beam-splitter artefacts model
class DistortionModel3DEStandard
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEStandard(const OfxRectI& format,
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
                               double b);

    virtual ~DistortionModel3DEStandard() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

private:
    double _c2;
    double _u1;
    double _v1;
    double _c4;
    double _u3;
    double _v3;
    double _mxx;
    double _mxy;
    double _myy;
};

// Degree-4 anamorphic model with anamorphic lens rotation
class DistortionModel3DEAnamorphic4
: public DistortionModel3DEBase
{
public:
    DistortionModel3DEAnamorphic4(const OfxRectI& format,
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
                                  double sqy);

    virtual ~DistortionModel3DEAnamorphic4() {};

private:
    // Remove distortion. p is a point in diagonally normalized coordinates.
    void undistort_dn(double xd, double yd, double* xu, double *yu) const OVERRIDE FINAL;

private:
    double _cx_for_x2, _cx_for_y2, _cx_for_x4, _cx_for_x2_y2, _cx_for_y4;
    double _cy_for_x2, _cy_for_y2, _cy_for_x4, _cy_for_x2_y2, _cy_for_y4;
    double _cosphi;
    double _sinphi;
    double _sqx;
    double _sqy;
};

OFXS_NAMESPACE_OFX_EXIT

#endif // DistortionModels_h
