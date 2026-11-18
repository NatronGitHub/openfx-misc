# The FFT backend of ConvolveCImg and ZDefocusCImg (CImgFFTConvolve.h), for CImg/Makefile and for
# the "make nomulti" Makefiles of CImg/Convolve and CImg/ZDefocus. Include it after Makefile.master
# and PLUGINOBJECTS, with CIMG_DIR set to the CImg directory (default: $(TOP_SRCDIR)/CImg).
# LICENSE and FFT: see license.mk. CMakeLists.txt does the same (OFX_LICENSE and FFT).
CIMG_DIR ?= $(TOP_SRCDIR)/CImg
include $(CIMG_DIR)/license.mk

FFT_CXXFLAGS =
FFT_LINKFLAGS =
FFT_HEADERS =
FFT_RESOURCES =
ifeq ($(FFT),fftw3)
# FFTW (GPL), multithreaded (CImg only uses its double precision library, libfftw3, and plans
# under its own lock): as openfx-gmic's GMIC_OFX/Makefile
FFT_CXXFLAGS = `pkg-config --cflags fftw3` -Dcimg_use_fftw3
FFT_LINKFLAGS = `pkg-config --libs fftw3` -lfftw3_threads
endif
ifeq ($(FFT),pocketfft)
# pocketfft (BSD-3-Clause), header-only, multithreaded (std::thread): patches/cimg-pocketfft.sh.
# Its licence asks for its notice in binary distributions: Licence_pocketfft.txt, in the bundle.
FFT_CXXFLAGS = -Dcimg_use_pocketfft
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

# LICENSE and FFT change the compiled code (fftw3 and pow2 use the same CImg.h) and the bundled
# files: when they change, rebuild the objects and the bundle.
FFT_BUILD_KEY = LICENSE=$(LICENSE) FFT=$(FFT)
$(OBJECTPATH)/build.fft: FORCE
	@mkdir -p $(OBJECTPATH)
	@echo '$(FFT_BUILD_KEY)' | cmp -s - $@ || { echo '$(FFT_BUILD_KEY)' > $@; rm -rf $(OBJECTPATH)/$(PLUGINNAME).ofx.bundle; }
$(addprefix $(OBJECTPATH)/,$(PLUGINOBJECTS)): $(OBJECTPATH)/build.fft $(FFT_HEADERS)
$(OBJECTPATH)/$(PLUGINNAME).ofx.bundle: $(FFT_RESOURCES)

.PHONY: FORCE
FORCE:
