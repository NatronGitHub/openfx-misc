# Licence of the build and FFT backend of the CImg plug-ins, for CImg/Makefile. The same variables,
# values and rules as NatronGitHub/openfx-gmic's license.mk, plus FFT=pow2, except that pocketfft
# is the default for all licences. CMakeLists.txt has the same logic (OFX_LICENSE and FFT).
#
# LICENSE: GPL (default, or empty) or COMMERCIAL (Natron's NATRON_LICENSE, which its
# build-plugins.sh passes as LICENSE="$NATRON_LICENSE", as for openfx-arena). COMMERCIAL builds
# refuse FFTW (GPL).
#
# FFT: the library that computes the Fourier transforms of ConvolveCImg, ZDefocusCImg
# and the fft() of ExpressionCImg:
# - pocketfft (default): pocketfft (BSD-3-Clause, header-only, multithreaded, downloaded at
#   POCKETFFTCOMMIT, see CImgVersion.mk), through CImg's cimg_use_pocketfft (CImg >= 4.0.8). Any
#   size, no dependency, no global state shared with other plug-ins, and faster than FFTW in our
#   benchmarks.
# - fftw3: FFTW (GPL), multithreaded, opt-in, GPL builds only. FFTW is a shared library with a
#   single global planner, which other plug-ins of the host (e.g. openfx-gmic) also use: see
#   cimgFFTLoad() in CImgFFTConvolve.h.
# - pow2: CImg's own radix-2 FFT, which only handles power-of-two sizes: the images are padded to
#   the next power of two (slower, and more memory). No library, no patch.
ifeq ($(LICENSE),)
  LICENSE := GPL
endif
ifneq ($(filter-out GPL COMMERCIAL,$(LICENSE)),)
  $(error LICENSE must be GPL or COMMERCIAL, not '$(LICENSE)')
endif
ifeq ($(FFT),)
  FFT := pocketfft
endif
ifneq ($(filter-out fftw3 pocketfft pow2,$(FFT)),)
  $(error FFT must be pocketfft, pow2 or fftw3, not '$(FFT)')
endif
ifeq ($(LICENSE)-$(FFT),COMMERCIAL-fftw3)
  $(error LICENSE=COMMERCIAL needs FFT=pocketfft or pow2, not '$(FFT)' (FFTW is GPL))
endif
