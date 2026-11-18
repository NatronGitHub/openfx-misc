#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that CImg can compute
# its Fourier transforms with pocketfft (https://github.com/mreineck/pocketfft, BSD-3-Clause,
# header-only C++), selected by defining 'cimg_use_pocketfft', as 'cimg_use_fftw3' selects FFTW.
# Usage: cimg-pocketfft.sh <CImg.h>
#
# Without FFTW (GPL), CImg falls back to a radix-2 FFT that throws "non 2^N dimension" for any
# other image size, so that the G'MIC filters that use the FFT (Add Grain, Descreen, Fourier
# Transform, ...) fail on e.g. 720x576 or 1920x1080 images. Natron's COMMERCIAL builds cannot use
# FFTW: pocketfft handles any size (mixed radix, Bluestein), in double precision, multithreaded.
# The patch adds an '#elif defined(cimg_use_pocketfft)' branch to the two CImg<T>::FFT() functions
# (along one axis, and n-D), with the same conventions as the FFTW branch (forward transform with
# exp(-2i.pi.k.n/N), inverse transform divided by the number of samples), and includes
# "pocketfft_hdronly.h" when cimg_use_pocketfft is defined. FFTW takes precedence when both are
# defined. Upstream merged it after G'MIC 4.0.7 (https://github.com/GreycLab/CImg/pull/497), with
# the same macro and header name (and cimg::info() reporting it).
#
# The script fails if the source does not contain the expected code (e.g. after a GMICVER bump):
# then check whether upstream changed its pocketfft support, and update or remove this patch.
# Running it again on a patched file does nothing, and so does running it on a CImg.h that already
# supports cimg_use_pocketfft as this patch does, as upstream does: the include block below found
# once, '#include "pocketfft_hdronly.h"' once, and a non-empty '#elif defined(cimg_use_pocketfft)'
# branch right after the FFTW branch of both FFT() functions. Only POSIX sh, awk, grep and wc are used,
# for Linux, macOS and MSYS2.
set -e
f="$1"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-pocketfft.sh" >&2
  rm -f "$f.patched" "$f.block1" "$f.block2" "$f.block3"
  exit 1
}
trap 'rm -f "$f.patched" "$f.block1" "$f.block2" "$f.block3"' INT TERM HUP

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

# Include pocketfft, after the FFTW3 include block.
cat > "$f.block1" <<'EOF'

// Configure pocketfft support.
// (https://github.com/mreineck/pocketfft, C++ header-only version)
//
// Define 'cimg_use_pocketfft' to compute the Fast Fourier Transform of image data with
// pocketfft (BSD license), without restriction on the image size, when FFTW3 is not used
// (see function 'CImg[List]<T>::FFT()').
#if defined(cimg_use_pocketfft) && !defined(cimg_use_fftw3)
#include <complex>
#include <exception>
#include "pocketfft_hdronly.h"
#endif
EOF

# FFT along one axis: before the '#else' of the built-in radix-2 FFT.
cat > "$f.block2" <<'EOF'
#elif defined(cimg_use_pocketfft)
      try {
        const ulongT whd = (ulongT)real._width*real._height*real._depth;
        CImg<double> buf(2*whd);
        double *const ptrf = buf._data;
        std::complex<double> *const data = (std::complex<double>*)ptrf;
        const ptrdiff_t s = (ptrdiff_t)sizeof(std::complex<double>);
        const pocketfft::shape_t shape{(size_t)real._width,(size_t)real._height,(size_t)real._depth};
        const pocketfft::stride_t stride{s,s*(ptrdiff_t)real._width,s*(ptrdiff_t)real._width*(ptrdiff_t)real._height};
        const pocketfft::shape_t axes{(size_t)(_axis=='x'?0:_axis=='y'?1:2)};
        const double a = is_inverse?1.0/(_axis=='x'?real.width():_axis=='y'?real.height():real.depth()):1.0;
        const size_t nthreads = nb_threads?(size_t)nb_threads:(size_t)cimg::nb_cpus();
        cimg_forC(real,c) {
          CImg<T> realc = real.get_shared_channel(c), imagc = imag.get_shared_channel(c);
          cimg_pragma_openmp(parallel for cimg_openmp_if_size(real.width()*real.height()*real.depth(),125000))
          cimg_rofoff(realc,i) { const ulongT i2 = 2*i; ptrf[i2] = (double)realc[i]; ptrf[i2 + 1] = (double)imagc[i]; }
          pocketfft::c2c(shape,stride,stride,axes,!is_inverse,data,data,a,nthreads);
          cimg_pragma_openmp(parallel for cimg_openmp_if_size(real.width()*real.height()*real.depth(),125000))
          cimg_rofoff(realc,i) { const ulongT i2 = 2*i; realc[i] = (T)ptrf[i2]; imagc[i] = (T)ptrf[i2 + 1]; }
        }
      } catch (const CImgException&) {
        throw;
      } catch (const std::exception& e) {
        throw CImgInstanceException("CImgList<%s>::FFT(): pocketfft failed for image (%u,%u,%u,%u) along the %c-axis: %s",
                                    pixel_type(),real._width,real._height,real._depth,real._spectrum,_axis,e.what());
      }
EOF

# n-D FFT: before the '#else' that calls the 1D FFT along each axis.
cat > "$f.block3" <<'EOF'
#elif defined(cimg_use_pocketfft)
      try {
        const ulongT whd = (ulongT)real._width*real._height*real._depth;
        CImg<double> buf(2*whd);
        double *const ptrf = buf._data;
        std::complex<double> *const data = (std::complex<double>*)ptrf;
        const ptrdiff_t s = (ptrdiff_t)sizeof(std::complex<double>);
        const pocketfft::shape_t shape{(size_t)real._width,(size_t)real._height,(size_t)real._depth};
        const pocketfft::stride_t stride{s,s*(ptrdiff_t)real._width,s*(ptrdiff_t)real._width*(ptrdiff_t)real._height};
        pocketfft::shape_t axes;
        if (real._width>1) axes.push_back(0);
        if (real._height>1) axes.push_back(1);
        if (real._depth>1) axes.push_back(2);
        const double a = is_inverse?1.0/whd:1.0;
        const size_t nthreads = nb_threads?(size_t)nb_threads:(size_t)cimg::nb_cpus();
        if (!axes.empty()) cimg_forC(real,c) {
          CImg<T> realc = real.get_shared_channel(c), imagc = imag.get_shared_channel(c);
          cimg_pragma_openmp(parallel for cimg_openmp_if_size(real.width()*real.height()*real.depth(),125000))
          cimg_rofoff(realc,i) { const ulongT i2 = 2*i; ptrf[i2] = (double)realc[i]; ptrf[i2 + 1] = (double)imagc[i]; }
          pocketfft::c2c(shape,stride,stride,axes,!is_inverse,data,data,a,nthreads);
          cimg_pragma_openmp(parallel for cimg_openmp_if_size(real.width()*real.height()*real.depth(),125000))
          cimg_rofoff(realc,i) { const ulongT i2 = 2*i; realc[i] = (T)ptrf[i2]; imagc[i] = (T)ptrf[i2 + 1]; }
        }
      } catch (const CImgException&) {
        throw;
      } catch (const std::exception& e) {
        throw CImgInstanceException("CImgList<%s>::FFT(): pocketfft failed for image (%u,%u,%u,%u): %s",
                                    pixel_type(),real._width,real._height,real._depth,real._spectrum,e.what());
      }
EOF

# supported <file>: the number of times the include block (block1 without its first, empty line)
# occurs, then of the '#elif defined(cimg_use_pocketfft)' lines that follow the end of an FFTW
# branch ('cimg::mutex(12,0);') and start a non-empty branch, then of the
# '#include "pocketfft_hdronly.h"' lines
supported() {
  awk -v blk="$f.block1" '
    function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
    BEGIN { while ((getline l < blk) > 0) if (++k > 1) b[++m] = trim(l) }
    { t[++n] = trim($0) }
    END {
      for (i = 1; i <= n; i++) {
        if (t[i] == "#include \"pocketfft_hdronly.h\"") ninc++
        if (i > 1 && t[i] == "#elif defined(cimg_use_pocketfft)" && t[i-1] == "cimg::mutex(12,0);" &&
            t[i+1] != "" && t[i+1] !~ /^#(else|elif|endif)/) nbr++
        if (t[i] != b[1]) continue
        for (j = 2; j <= m && t[i + j - 1] == b[j]; j++) ;
        if (j > m) nblk++
      }
      print nblk + 0, nbr + 0, ninc + 0
    }' "$1"
}

# already patched, or supported upstream: nothing to do
if grep -q 'cimg_use_pocketfft' "$f"; then
  found=$(supported "$f") || fail "awk failed"
  [ "$found" = "1 2 1" ] ||
    fail "cimg_use_pocketfft is already used, but not as this patch does (found: $found; expected the include block once, the FFT() branches twice, and the include line once)"
  rm -f "$f.block1" "$f.block2" "$f.block3"
  echo "$f: already supports cimg_use_pocketfft"
  exit 0
fi

# Insert the blocks at their anchors (lines compared without their leading and trailing blanks):
# block1 after the 5 lines '#ifdef cimg_use_fftw3' .. '#endif' that include fftw3.h; block2 and
# block3 before the '#else' that follows 'cimg::mutex(12,0);' (end of the FFTW branch), when the
# line after '#else' is respectively 'switch (_axis) {' and the n-D fallback. Each anchor must be
# found exactly once.
awk -v b1="$f.block1" -v b2="$f.block2" -v b3="$f.block3" '
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  function emit(file,   l) { while ((getline l < file) > 0) print l; close(file) }
  { line[NR] = $0; t[NR] = trim($0) }
  END {
    n1 = n2 = n3 = 0
    for (i = 1; i <= NR; i++) {
      if (t[i] == "#else" && i > 1 && t[i-1] == "cimg::mutex(12,0);") {
        if (t[i+1] == "switch (_axis) {") { emit(b2); n2++ }
        else if (t[i+1] == "if (real._depth>1) FFT(real,imag,\047z\047,is_inverse);") { emit(b3); n3++ }
      }
      print line[i]
      if (i >= 5 && t[i] == "#endif" && t[i-1] == "}" && t[i-2] == "#include \"fftw3.h\"" &&
          t[i-3] == "extern \"C\" {" && t[i-4] == "#ifdef cimg_use_fftw3") { emit(b1); n1++ }
    }
    if (n1 != 1 || n2 != 1 || n3 != 1) { printf "anchors found: %d %d %d\n", n1, n2, n3 > "/dev/stderr"; exit 1 }
  }' "$f" > "$f.patched" || fail "expected the FFTW3 include block and the end of the FFTW branch of both CImg<T>::FFT() functions exactly once"
blocks=$(cat "$f.block1" "$f.block2" "$f.block3" | wc -l)
[ "$(supported "$f.patched")" = "1 2 1" ] || fail "the patch did not apply"
rm -f "$f.block1" "$f.block2" "$f.block3"
[ "$(wc -l < "$f.patched")" -eq "$(( $(wc -l < "$f") + blocks ))" ] || fail "the patch changed other lines"
mv "$f.patched" "$f"
echo "$f: patched for pocketfft support (cimg_use_pocketfft)"
