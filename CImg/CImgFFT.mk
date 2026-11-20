# The FFT backend of ConvolveCImg, ZDefocusCImg and ExpressionCImg's fft() (CImgFFTConvolve.h),
# for CImg/Makefile and for the "make nomulti" Makefiles of CImg/Convolve, CImg/Expression and
# CImg/ZDefocus. Include it after Makefile.master and PLUGINOBJECTS, with
# CIMG_DIR set to the CImg directory (default: $(TOP_SRCDIR)/CImg).
# LICENSE and FFT: see license.mk. CMakeLists.txt does the same (OFX_LICENSE and FFT).
CIMG_DIR ?= $(TOP_SRCDIR)/CImg
include $(CIMG_DIR)/license.mk

FFT_CXXFLAGS =
FFT_LINKFLAGS =
FFT_HEADERS =
FFT_RESOURCES =
ifeq ($(FFT),fftw3)
# FFTW (GPL, opt-in), multithreaded (CImg only uses its double precision library, libfftw3, and
# plans under its own lock): as openfx-gmic's GMIC_OFX/Makefile, with fftw3_threads first (it uses
# fftw3: the order matters for static links, e.g. MinGW -static)
FFT_CXXFLAGS = `pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFT_LINKFLAGS = -lfftw3_threads `pkg-config --libs fftw3`
# FFTW >= 3.3.5: fftw_make_planner_thread_safe() (cimgFFTLoad(), CImgFFTConvolve.h), as openfx-gmic
# (NatronGitHub/openfx-gmic#93). Only checked when compiling (not for clean or sources).
ifeq ($(filter clean distclean sources CImg.h,$(MAKECMDGOALS)),)
ifneq ($(shell pkg-config --atleast-version=3.3.5 fftw3 && echo ok),ok)
$(error FFT=fftw3 needs FFTW >= 3.3.5 (pkg-config fftw3: $(shell pkg-config --modversion fftw3 2>&1)): install it, or use the default FFT=pocketfft)
endif
endif
endif
ifeq ($(FFT),pocketfft)
# pocketfft (BSD-3-Clause), header-only, multithreaded (std::thread) except on Windows: CImg's
# cimg_use_pocketfft (CImg >= 4.0.8, GreycLab/CImg#497), which includes pocketfft_hdronly.h, next to
# CImg.h. With cimg_use_fftw3 too, CImg uses FFTW.
# Its licence asks for its notice in binary distributions: Licence_pocketfft.txt, in the bundle.
FFT_CXXFLAGS = -Dcimg_use_pocketfft
# Windows (MinGW): single-threaded pocketfft, as openfx-gmic (NatronGitHub/openfx-gmic#94). Its
# static thread pool is destroyed when the host unloads the plug-in (FreeLibrary runs the DLL's
# static destructors under the loader lock): ~thread_pool() joins the worker threads, which wait
# for that lock in LdrShutdownThread (deadlock). CImg.h is unchanged; OpenMP is not involved.
ifneq ($(MINGW_OS),)
FFT_CXXFLAGS += -DPOCKETFFT_NO_MULTITHREADING
endif
ifneq ($(OS),Darwin)
FFT_LINKFLAGS = -pthread
endif
FFT_HEADERS = $(CIMG_DIR)/pocketfft_hdronly.h
FFT_RESOURCES = $(CIMG_DIR)/Licence_pocketfft.txt
endif
# pow2: CImg's own FFT (power-of-two sizes only): nothing to add
CXXFLAGS += $(FFT_CXXFLAGS)
LINKFLAGS += $(FFT_LINKFLAGS)
RESOURCES += $(FFT_RESOURCES)

# LICENSE and FFT change the compiled code (pocketfft, pow2 and fftw3 all use the same CImg.h, with
# other macros) and the bundled files: when they change, rebuild the objects and the bundle.
FFT_BUILD_KEY = LICENSE=$(LICENSE) FFT=$(FFT)
$(OBJECTPATH)/build.fft: FORCE
	@mkdir -p $(OBJECTPATH)
	@echo '$(FFT_BUILD_KEY)' | cmp -s - $@ || { echo '$(FFT_BUILD_KEY)' > $@; rm -rf $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle; }
$(addprefix $(OBJECTPATH)/,$(PLUGINOBJECTS)): $(OBJECTPATH)/build.fft $(FFT_HEADERS)
$(OBJECTPATH)/$(PLUGINNAME).ofx.bundle: $(FFT_RESOURCES)

.PHONY: FORCE
FORCE:
