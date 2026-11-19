#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser functions i(), j(), I() and J() do not read out of bounds when an integer coordinate is
# outside the image. Usage: cimg-ij-boundary.sh <CImg.h>
#
# With linear or cubic interpolation, the coordinates that are not interpolated (c of i()/j(), and
# z, or y and z, when only x and y, or only x, are interpolated) were not always brought back
# inside the image before calling _cubic_atX() and the like, which then read outside it: e.g.
# 'gmic 320,240 f "J(0,-500,0,2,2)"' (periodic), or J(0.5,-500,0,2,0) (Dirichlet). The filter
# "Spiral Distortion" crashes on small inputs this way (its preshift uses J(...,2)).
# https://github.com/GreycLab/CImg/issues/498
#
# Upstream fixed it after G'MIC 4.0.7, in GreycLab/CImg 41e121c (develop,
# https://github.com/GreycLab/CImg/commit/41e121cf9047910a30d5f0336ca0edc0d8ed2473): the
# functions behind i()/j() (mp_ijxyzc1/2/3) and I()/J() (mp_IJxyz1/2) first bring the integer
# coordinates back inside the image (or return 0 for Dirichlet boundaries), for every boundary
# condition. This script backports that commit: in each of the five functions, it replaces the
# code from the integer coordinates to the end of the function with upstream's, except that the
# declarations of the coordinates keep their 4.0.7 _mp_arg() indices (upstream also changed how
# these functions are called; 'const int' becomes 'int', as upstream).
#
# "If the fix is already there, don't apply it": the script does nothing on a CImg.h that has the
# fix (patched, or fixed upstream), applies it to the 4.0.7 code, and fails on anything else (e.g.
# after a GMICVER bump that changed this code otherwise): then check what upstream did, and update
# or remove this patch. Lines are compared without their leading and trailing blanks, and with
# the indices of _mp_arg() ignored. Only POSIX sh, awk, grep and wc are used, for Linux, macOS and
# MSYS2.
set -e
f="$1"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-ij-boundary.sh (GreycLab/CImg#498)" >&2
  rm -f "$f.patched" "$f.ij_old" "$f.ij_new" "$f.stripped1" "$f.stripped2"
  exit 1
}
trap 'rm -f "$f.patched" "$f.ij_old" "$f.ij_new" "$f.stripped1" "$f.stripped2"' INT TERM HUP

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

# The five blocks (mp_ijxyzc3, mp_ijxyzc2, mp_ijxyzc1, mp_IJxyz2, mp_IJxyz1), separated by '@@'
# lines: in G'MIC 4.0.7's CImg.h, then as fixed upstream (GreycLab/CImg 41e121c, verbatim)
cat > "$f.ij_old" <<'END_OF_OLD'
        const int c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(7)):_mp_arg(7));
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mc = cimg::mod(c,s2);
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              mz = cimg::mod((float)z,(float)d2);
            return (double)img._cubic_atXYZ(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                            mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._cubic_atXYZ_p((float)x,(float)y,(float)z,cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._cubic_atXYZ((float)x,(float)y,(float)z,c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.cubic_atXYZ((float)x,(float)y,(float)z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mc = cimg::mod(c,s2);
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              mz = cimg::mod((float)z,(float)d2);
            return (double)img._linear_atXYZ(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                             mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._linear_atXYZ_p((float)x,(float)y,(float)z,cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._linear_atXYZ((float)x,(float)y,(float)z,c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.linear_atXYZ((float)x,(float)y,(float)z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mc = cimg::mod(c,s2),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2),
              mz = cimg::mod((int)z,d2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                               mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),(int)cimg::mod(y,(double)img._height),
                               (int)cimg::mod(z,(double)img._depth),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,(int)y,(int)z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,(int)y,(int)z,c,(T)0);
          }
        }
      }
@@
        const int
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(6)):_mp_arg(6)),
          c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(7)):_mp_arg(7));
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2);
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2);
            return (double)img._cubic_atXY(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                           mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._cubic_atXY_p((float)x,(float)y,cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._cubic_atXY((float)x,(float)y,
                                           z<0?0:z>=img.depth()?img.depth() - 1:z,
                                           c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (z<0 || z>=img.depth() || c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.cubic_atXY((float)x,(float)y,z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2);
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2);
            return (double)img._linear_atXY(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                            mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._linear_atXY_p((float)x,(float)y,cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._linear_atXY((float)x,(float)y,
                                            z<0?0:z>=img.depth()?img.depth() - 1:z,
                                            c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (z<0 || z>=img.depth() || c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.linear_atXY((float)x,(float)y,z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                               mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),(int)cimg::mod(y,(double)img._height),
                               cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,(int)y,z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,(int)y,z,c,(T)0);
          }
        }
      }
@@
        const int
          y = (int)(is_relative?(mp.mem[_cimg_mp_slot_y] + _mp_arg(5)):_mp_arg(5)),
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(6)):_mp_arg(6)),
          c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(7)):_mp_arg(7));
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2);
            const float mx = cimg::mod((float)x,(float)w2);
            return (double)img._cubic_atX(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                          mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._cubic_atX_p((float)x,cimg::mod(y,img.height()),
                                            cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._cubic_atX((float)x,
                                          y<0?0:y>=img.height()?img.height() - 1:y,
                                          z<0?0:z>=img.depth()?img.depth() - 1:z,
                                          c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (y<0 || y>=img.height() || z<0 || z>=img.depth() || c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.cubic_atX((float)x,y,z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2);
            const float mx = cimg::mod((float)x,(float)w2);
            return (double)img._linear_atX(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                           mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img._linear_atX_p((float)x,cimg::mod(y,img.height()),
                                             cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._linear_atX((float)x,
                                           y<0?0:y>=img.height()?img.height() - 1:y,
                                           z<0?0:z>=img.depth()?img.depth() - 1:z,
                                           c<0?0:c>=img.spectrum()?img.spectrum() - 1:c);
          default : // Dirichlet
            if (y<0 || y>=img.height() || z<0 || z>=img.depth() || c<0 || c>=img.spectrum()) return (T)0;
            return (double)img.linear_atX((float)x,y,z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(), s2 = 2*img.spectrum(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              mc = cimg::mod(c,s2),
              mx = cimg::mod((int)x,w2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                               mz<img.depth()?mz:d2 - mz - 1,mc<img.spectrum()?mc:s2 - mc - 1);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),cimg::mod(y,img.height()),
                               cimg::mod(z,img.depth()),cimg::mod(c,img.spectrum()));
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,y,z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,y,z,c,(T)0);
          }
        }
      }
@@
        const int
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(6)):_mp_arg(6));
        const ulongT whd = (ulongT)img._width*img._height*img._depth;
        const T *ptrs;
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              mz = cimg::mod(z,d2),
              cz = mz<img.depth()?mz:d2 - mz - 1;
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY(cx,cy,cz,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY_p((float)x,(float)y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY((float)x,(float)y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.cubic_atXY((float)x,(float)y,z,c,(T)0);
          } break;
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              mz = cimg::mod(z,d2),
              cz = mz<img.depth()?mz:d2 - mz - 1;
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY(cx,cy,cz,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY_p((float)x,(float)y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY((float)x,(float)y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.linear_atXY((float)x,(float)y,z,c,(T)0);
          } break;
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              mz = cimg::mod(z,d2),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1,
              cz = mz<img.depth()?mz:d2 - mz - 1;
            ptrs = &img(cx,cy,cz);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 2 : { // Periodic
            const int
              cx = (int)cimg::mod(x,(double)img._width),
              cy = (int)cimg::mod(y,(double)img._height),
              cz = cimg::mod(z,img.depth());
            ptrs = &img(cx,cy,cz);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 1 : { // Neumann
            ptrs = &img._atXYZ((int)x,(int)y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          default : // Dirichlet
            if (img.containsXYZC((int)x,(int)y,z)) {
              ptrs = &img((int)x,(int)y,z);
              cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
            } else std::memset(ptrd,0,siz*sizeof(double));
          }
        }
        return cimg::type<double>::nan();
      }
@@
        const int
          y = (int)(is_relative?(mp.mem[_cimg_mp_slot_y] + _mp_arg(5)):_mp_arg(5)),
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(6)):_mp_arg(6));
        const ulongT whd = (ulongT)img._width*img._height*img._depth;
        const T *ptrs;
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              cy = my<img.height()?my:h2 - my - 1,
              cz = mz<img.depth()?mz:d2 - mz - 1;
            const float
              mx = cimg::mod((float)x,(float)w2),
              cx = mx<img.width()?mx:w2 - mx - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX(cx,cy,cz,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX_p((float)x,y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX((float)x,y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.cubic_atX((float)x,y,z,c,(T)0);
          } break;
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              cy = my<img.height()?my:h2 - my - 1,
              cz = mz<img.depth()?mz:d2 - mz - 1;
            const float
              mx = cimg::mod((float)x,(float)w2),
              cx = mx<img.width()?mx:w2 - mx - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX(cx,cy,cz,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX_p((float)x,y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX((float)x,y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.linear_atX((float)x,y,z,c,(T)0);
          } break;
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              my = cimg::mod(y,h2),
              mz = cimg::mod(z,d2),
              mx = cimg::mod((int)x,w2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1,
              cz = mz<img.depth()?mz:d2 - mz - 1;
            ptrs = &img(cx,cy,cz);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 2 : { // Periodic
            const int
              cx = (int)cimg::mod(x,(double)img._width),
              cy = cimg::mod(y,img.height()),
              cz = cimg::mod(z,img.depth());
            ptrs = &img(cx,cy,cz);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 1 : { // Neumann
            ptrs = &img._atXYZ((int)x,y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          default : // Dirichlet
            if (img.containsXYZC((int)x,y,z)) {
              ptrs = &img((int)x,y,z);
              cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
            } else std::memset(ptrd,0,siz*sizeof(double));
          }
        }
        return cimg::type<double>::nan();
      }
END_OF_OLD
cat > "$f.ij_new" <<'END_OF_NEW'
        int c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(6)):_mp_arg(6));

        if (c<0 || c>=img.spectrum()) switch (boundary_conditions) { // Out-of-bound access along c
          case 0 : return 0; // Dirichlet
          case 1 : c = cimg::cut(c,0,img.spectrum() - 1); break; // Neumann
          case 2 : c = cimg::mod(c,img.spectrum()); break; // Periodic
          default : { // Mirror
            const int s2 = 2*img.spectrum(), mc = cimg::mod(c,s2);
            c = mc<img.spectrum()?mc:s2 - mc - 1;
          }
          }

        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              mz = cimg::mod((float)z,(float)d2);
            return (double)img._cubic_atXYZ(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                            mz<img.depth()?mz:d2 - mz - 1,c);
          }
          case 2 : // Periodic
            return (double)img._cubic_atXYZ_p((float)x,(float)y,(float)z,c);
          case 1 : // Neumann
            return (double)img._cubic_atXYZ((float)x,(float)y,(float)z,c);
          default : // Dirichlet
            return (double)img.cubic_atXYZ((float)x,(float)y,(float)z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              mz = cimg::mod((float)z,(float)d2);
            return (double)img._linear_atXYZ(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                                             mz<img.depth()?mz:d2 - mz - 1,c);
          }
          case 2 : // Periodic
            return (double)img._linear_atXYZ_p((float)x,(float)y,(float)z,c);
          case 1 : // Neumann
            return (double)img._linear_atXYZ((float)x,(float)y,(float)z,c);
          default : // Dirichlet
            return (double)img.linear_atXYZ((float)x,(float)y,(float)z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(), d2 = 2*img.depth(),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2),
              mz = cimg::mod((int)z,d2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,
                               mz<img.depth()?mz:d2 - mz - 1,c);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),(int)cimg::mod(y,(double)img._height),
                               (int)cimg::mod(z,(double)img._depth),c);
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,(int)y,(int)z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,(int)y,(int)z,c,(T)0);
          }
        }
      }
@@
        int
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(5)):_mp_arg(5)),
          c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(6)):_mp_arg(6));

        if (c<0 || c>=img.spectrum()) switch (boundary_conditions) { // Out-of-bound access along c
          case 0 : return 0; // Dirichlet
          case 1 : c = cimg::cut(c,0,img.spectrum() - 1); break; // Neumann
          case 2 : c = cimg::mod(c,img.spectrum()); break; // Periodic
          default : { // Mirror
            const int s2 = 2*img.spectrum(), mc = cimg::mod(c,s2);
            c = mc<img.spectrum()?mc:s2 - mc - 1;
          }
          }

        if (z<0 || z>=img.depth()) switch (boundary_conditions) { // Out-of-bound access along z
          case 0 : return 0; // Dirichlet
          case 1 : z = cimg::cut(z,0,img.depth() - 1); break; // Neumann
          case 2 : z = cimg::mod(z,img.depth()); break; // Periodic
          default : { // Mirror
            const int d2 = 2*img.depth(), mz = cimg::mod(z,d2);
            z = mz<img.depth()?mz:d2 - mz - 1;
          }
          }

        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2);
            return (double)img._cubic_atXY(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,z,c);
          }
          case 2 : // Periodic
            return (double)img._cubic_atXY_p((float)x,(float)y,z,c);
          case 1 : // Neumann
            return (double)img._cubic_atXY((float)x,(float)y,z,c);
          default : // Dirichlet
            return (double)img.cubic_atXY((float)x,(float)y,z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2);
            return (double)img._linear_atXY(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,z,c);
          }
          case 2 : // Periodic
            return (double)img._linear_atXY_p((float)x,(float)y,z,c);
          case 1 : // Neumann
            return (double)img._linear_atXY((float)x,(float)y,z,c);
          default : // Dirichlet
            return (double)img.linear_atXY((float)x,(float)y,z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height(),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,my<img.height()?my:h2 - my - 1,z,c);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),(int)cimg::mod(y,(double)img._height),z,c);
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,(int)y,z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,(int)y,z,c,(T)0);
          }
        }
      }
@@
        int
          y = (int)(is_relative?(mp.mem[_cimg_mp_slot_y] + _mp_arg(4)):_mp_arg(4)),
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(5)):_mp_arg(5)),
          c = (int)(is_relative?(mp.mem[_cimg_mp_slot_c] + _mp_arg(6)):_mp_arg(6));

        if (c<0 || c>=img.spectrum()) switch (boundary_conditions) { // Out-of-bound access along c
          case 0 : return 0; // Dirichlet
          case 1 : c = cimg::cut(c,0,img.spectrum() - 1); break; // Neumann
          case 2 : c = cimg::mod(c,img.spectrum()); break; // Periodic
          default : { // Mirror
            const int s2 = 2*img.spectrum(), mc = cimg::mod(c,s2);
            c = mc<img.spectrum()?mc:s2 - mc - 1;
          }
          }

        if (z<0 || z>=img.depth()) switch (boundary_conditions) { // Out-of-bound access along z
          case 0 : return 0; // Dirichlet
          case 1 : z = cimg::cut(z,0,img.depth() - 1); break; // Neumann
          case 2 : z = cimg::mod(z,img.depth()); break; // Periodic
          default : { // Mirror
            const int d2 = 2*img.depth(), mz = cimg::mod(z,d2);
            z = mz<img.depth()?mz:d2 - mz - 1;
          }
          }

        if (y<0 || y>=img.height()) switch (boundary_conditions) { // Out-of-bound access along y
          case 0 : return 0; // Dirichlet
          case 1 : y = cimg::cut(y,0,img.height() - 1); break; // Neumann
          case 2 : y = cimg::mod(y,img.height()); break; // Periodic
          default : { // Mirror
            const int h2 = 2*img.height(), my = cimg::mod(y,h2);
            y = my<img.height()?my:h2 - my - 1;
          }
          }

        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width();
            const float mx = cimg::mod((float)x,(float)w2);
            return (double)img._cubic_atX(mx<img.width()?mx:w2 - mx - 1,y,z,c);
          }
          case 2 : // Periodic
            return (double)img._cubic_atX_p((float)x,y,z,c);
          case 1 : // Neumann
            return (double)img._cubic_atX((float)x,y,z,c);
          default : // Dirichlet
            return (double)img.cubic_atX((float)x,y,z,c,(T)0);
          }
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width();
            const float mx = cimg::mod((float)x,(float)w2);
            return (double)img._linear_atX(mx<img.width()?mx:w2 - mx - 1,y,z,c);
          }
          case 2 : // Periodic
            return (double)img._linear_atX_p((float)x,y,z,c);
          case 1 : // Neumann
            return (double)img._linear_atX((float)x,y,z,c);
          default : // Dirichlet
            return (double)img.linear_atX((float)x,y,z,c,(T)0);
          }
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(),
              mx = cimg::mod((int)x,w2);
            return (double)img(mx<img.width()?mx:w2 - mx - 1,y,z,c);
          }
          case 2 : // Periodic
            return (double)img((int)cimg::mod(x,(double)img._width),y,z,c);
          case 1 : // Neumann
            return (double)img._atXYZC((int)x,y,z,c);
          default : // Dirichlet
            return (double)img.atXYZC((int)x,y,z,c,(T)0);
          }
        }
      }
@@
        int
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(5)):_mp_arg(5));

        if (z<0 || z>=img.depth()) switch (boundary_conditions) { // Out-of-bound access along z
          case 0 : std::memset(ptrd,0,siz*sizeof(double)); return cimg::type<double>::nan(); // Dirichlet
          case 1 : z = cimg::cut(z,0,img.depth() - 1); break; // Neumann
          case 2 : z = cimg::mod(z,img.depth()); break; // Periodic
          default : { // Mirror
            const int d2 = 2*img.depth(), mz = cimg::mod(z,d2);
            z = mz<img.depth()?mz:d2 - mz - 1;
          }
          }

        const ulongT whd = (ulongT)img._width*img._height*img._depth;
        const T *ptrs;
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(), h2 = 2*img.height();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY(cx,cy,z,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY_p((float)x,(float)y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atXY((float)x,(float)y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.cubic_atXY((float)x,(float)y,z,c,(T)0);
          } break;
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height();
            const float
              mx = cimg::mod((float)x,(float)w2),
              my = cimg::mod((float)y,(float)h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY(cx,cy,z,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY_p((float)x,(float)y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atXY((float)x,(float)y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.linear_atXY((float)x,(float)y,z,c,(T)0);
          } break;
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width(), h2 = 2*img.height(),
              mx = cimg::mod((int)x,w2),
              my = cimg::mod((int)y,h2),
              cx = mx<img.width()?mx:w2 - mx - 1,
              cy = my<img.height()?my:h2 - my - 1;
            ptrs = &img(cx,cy,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 2 : { // Periodic
            const int
              cx = (int)cimg::mod(x,(double)img._width),
              cy = (int)cimg::mod(y,(double)img._height);
            ptrs = &img(cx,cy,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 1 : { // Neumann
            ptrs = &img._atXYZ((int)x,(int)y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          default : // Dirichlet
            if (img.containsXYZC((int)x,(int)y,z)) {
              ptrs = &img((int)x,(int)y,z);
              cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
            } else std::memset(ptrd,0,siz*sizeof(double));
          }
        }
        return cimg::type<double>::nan();
      }
@@
        int
          y = (int)(is_relative?(mp.mem[_cimg_mp_slot_y] + _mp_arg(4)):_mp_arg(4)),
          z = (int)(is_relative?(mp.mem[_cimg_mp_slot_z] + _mp_arg(5)):_mp_arg(5));

        if (z<0 || z>=img.depth()) switch (boundary_conditions) { // Out-of-bound access along z
          case 0 : std::memset(ptrd,0,siz*sizeof(double)); return cimg::type<double>::nan(); // Dirichlet
          case 1 : z = cimg::cut(z,0,img.depth() - 1); break; // Neumann
          case 2 : z = cimg::mod(z,img.depth()); break; // Periodic
          default : { // Mirror
            const int d2 = 2*img.depth(), mz = cimg::mod(z,d2);
            z = mz<img.depth()?mz:d2 - mz - 1;
          }
          }

        if (y<0 || y>=img.height()) switch (boundary_conditions) { // Out-of-bound access along y
          case 0 : std::memset(ptrd,0,siz*sizeof(double)); return cimg::type<double>::nan(); // Dirichlet
          case 1 : y = cimg::cut(y,0,img.height() - 1); break; // Neumann
          case 2 : y = cimg::mod(y,img.height()); break; // Periodic
          default : { // Mirror
            const int h2 = 2*img.height(), my = cimg::mod(y,h2);
            y = my<img.height()?my:h2 - my - 1;
          }
          }

        const ulongT whd = (ulongT)img._width*img._height*img._depth;
        const T *ptrs;
        switch (interpolation) {
        case 2 : // Cubic interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width();
            const float
              mx = cimg::mod((float)x,(float)w2),
              cx = mx<img.width()?mx:w2 - mx - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX(cx,y,z,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX_p((float)x,y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._cubic_atX((float)x,y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.cubic_atX((float)x,y,z,c,(T)0);
          } break;
        case 1 : // Linear interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int w2 = 2*img.width();
            const float
              mx = cimg::mod((float)x,(float)w2),
              cx = mx<img.width()?mx:w2 - mx - 1;
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX(cx,y,z,c);
          } break;
          case 2 : // Periodic
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX_p((float)x,y,z,c);
            break;
          case 1 : // Neumann
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img._linear_atX((float)x,y,z,c);
            break;
          default : // Dirichlet
            cimg_for_inC(img,0,siz - 1,c) *(ptrd++) = (double)img.linear_atX((float)x,y,z,c,(T)0);
          } break;
        default : // Nearest neighbor interpolation
          switch (boundary_conditions) {
          case 3 : { // Mirror
            const int
              w2 = 2*img.width(),
              mx = cimg::mod((int)x,w2),
              cx = mx<img.width()?mx:w2 - mx - 1;
            ptrs = &img(cx,y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 2 : { // Periodic
            const int cx = (int)cimg::mod(x,(double)img._width);
            ptrs = &img(cx,y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          case 1 : { // Neumann
            ptrs = &img._atXYZ((int)x,y,z);
            cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
          } break;
          default : // Dirichlet
            if (img.containsXYZC((int)x,y,z)) {
              ptrs = &img((int)x,y,z);
              cimg_for_inC(img,0,siz - 1,c) { *(ptrd++) = (double)*ptrs; ptrs+=whd; }
            } else std::memset(ptrd,0,siz*sizeof(double));
          }
        }
        return cimg::type<double>::nan();
      }
END_OF_NEW

# ijawk <mode> <file>: mode 'count' prints how many of the five old blocks, then of the five fixed
# blocks, are found once ("duplicate" if a block is found more than once), 'apply' prints the
# patched file, and 'strip' prints the file without the blocks (old or fixed), to check that
# nothing else changed. A block is replaced by its fixed version, except that its first lines, up
# to the first one ending with ';' (the declarations of the integer coordinates), are the original
# ones with 'const int' changed into 'int'.
ijawk() {
  awk -v mode="$1" -v old="$f.ij_old" -v new="$f.ij_new" '
    function norm(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); gsub(/_mp_arg\([0-9]+\)/, "_mp_arg()", s); return s }
    # load(file, b, z, n): the lines of the blocks of file in b[k, j], normalized in z[k, j]
    function load(file, b, z, n,   l, k) {
      k = 1
      while ((getline l < file) > 0) {
        if (l == "@@") { k++; continue }
        b[k, ++n[k]] = l; z[k, n[k]] = norm(l)
      }
      close(file)
      return k
    }
    # found(i, z, n, k): whether the lines of block k start at line i
    function found(i, z, n, k,   j) {
      for (j = 1; j <= n[k] && t[i + j - 1] == z[k, j]; j++) ;
      return j > n[k]
    }
    BEGIN { nb = load(old, bo, zo, no); if (load(new, bn, zn, nn) != nb) { bad = 1; exit 2 } }
    { r[NR] = $0; t[NR] = norm($0) }
    END {
      if (bad) exit 2
      for (i = 1; i <= NR; i++) for (k = 1; k <= nb; k++) {
        if (found(i, zo, no, k)) { co[k]++; ko[i] = k }
        if (found(i, zn, nn, k)) { cn[k]++; kn[i] = k }
      }
      if (mode == "count") {
        o = n2 = 0
        for (k = 1; k <= nb; k++) { if (co[k] > 1 || cn[k] > 1) { print "duplicate"; exit }
                                    if (co[k] == 1) o++; if (cn[k] == 1) n2++ }
        print o " " n2; exit
      }
      for (i = 1; i <= NR; i++) {
        if (i in ko) {
          k = ko[i]
          if (mode == "apply") {
            for (d = 1; d <= no[k] && r[i + d - 1] !~ /;[ \t\r]*$/; d++) ;
            for (j = 1; j <= nn[k]; j++) {
              if (j <= d) { l = r[i + j - 1]; sub(/const int/, "int", l); print l } else print bn[k, j]
            }
          }
          i += no[k] - 1; continue
        }
        if (mode == "strip" && (i in kn)) { i += nn[kn[i]] - 1; continue }
        print r[i]
      }
    }' "$2"
}

counts=$(ijawk count "$f") || fail "awk failed"
# already patched, or fixed upstream: nothing to do
if [ "$counts" = "0 5" ]; then
  rm -f "$f.ij_old" "$f.ij_new"
  echo "$f: already patched or fixed upstream (GreycLab/CImg#498)"
  exit 0
fi
# the code to patch must be found exactly
[ "$counts" = "5 0" ] || fail "expected the ends of mp_ijxyzc1/2/3() and mp_IJxyz1/2() once each, either all as in G'MIC 4.0.7 or all as fixed in GreycLab/CImg 41e121c (found, old then fixed: $counts)"

ijawk apply "$f" > "$f.patched" || fail "awk failed"
[ "$(ijawk count "$f.patched")" = "0 5" ] || fail "the patch did not apply"
added=$(($(grep -cv '^@@$' "$f.ij_new") - $(grep -cv '^@@$' "$f.ij_old")))
[ "$(wc -l < "$f.patched")" -eq "$(($(wc -l < "$f") + added))" ] || fail "the patch did not add $added lines"
# the rest of the file must be unchanged (no diff: MSYS2 does not install it by default)
ijawk strip "$f" > "$f.stripped1" || fail "awk failed"
ijawk strip "$f.patched" > "$f.stripped2" || fail "awk failed"
[ "$(wc -l < "$f.stripped1")" -eq "$(wc -l < "$f.stripped2")" ] || fail "the patch changed other lines"
changed=$(awk 'NR == FNR { a[FNR] = $0; next } $0 != a[FNR] { n++ } END { print n + 0 }' "$f.stripped1" "$f.stripped2")
[ "$changed" = 0 ] || fail "the patch changed $changed other lines"
rm -f "$f.stripped1" "$f.stripped2" "$f.ij_old" "$f.ij_new"
mv "$f.patched" "$f"
echo "$f: patched for out-of-bounds reads in i(), j(), I() and J() (GreycLab/CImg#498)"
