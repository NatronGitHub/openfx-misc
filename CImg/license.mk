# Licence of the build and FFT backend of the CImg plug-ins, for CImg/Makefile. The same variables,
# values and rules as NatronGitHub/openfx-gmic's license.mk, plus FFT=pow2. CMakeLists.txt has the
# same logic (OFX_LICENSE and FFT).
#
# LICENSE: GPL (default, or empty) or COMMERCIAL (Natron's NATRON_LICENSE, which its
# build-plugins.sh passes as LICENSE="$NATRON_LICENSE", as for openfx-arena). COMMERCIAL builds do
# not link FFTW (GPL).
#
# FFT: the library that computes the Fourier transforms of ConvolveCImg and ZDefocusCImg:
# - fftw3: FFTW (GPL), multithreaded, for GPL builds (default);
# - pocketfft: pocketfft (BSD-3-Clause, header-only, downloaded at POCKETFFTCOMMIT, see
#   CImgVersion.mk), through patches/cimg-pocketfft.sh (applied only for FFT=pocketfft), for
#   COMMERCIAL builds (default). GPL builds may use it too.
# - pow2: CImg's own radix-2 FFT, which only handles power-of-two sizes: the images are padded to
#   the next power of two (slower, and more memory). No library, no patch.
ifeq ($(LICENSE),)
  LICENSE := GPL
endif
ifneq ($(filter-out GPL COMMERCIAL,$(LICENSE)),)
  $(error LICENSE must be GPL or COMMERCIAL, not '$(LICENSE)')
endif
ifeq ($(LICENSE),COMMERCIAL)
  ifeq ($(FFT),)
    FFT := pocketfft
  endif
  ifeq ($(FFT),fftw3)
    $(error LICENSE=COMMERCIAL needs FFT=pocketfft or pow2, not '$(FFT)' (FFTW is GPL))
  endif
else
  ifeq ($(FFT),)
    FFT := fftw3
  endif
endif
ifneq ($(filter-out fftw3 pocketfft pow2,$(FFT)),)
  $(error FFT must be fftw3, pocketfft or pow2, not '$(FFT)')
endif
