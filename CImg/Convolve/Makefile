PLUGINOBJECTS = ofxsThreadSuite.o \
CImgConvolve.o \
CImgFilter.o \

# no ofxsInteract.o
SUPPORTOBJECTS = \
ofxsMultiThread.o \
ofxsProperty.o \
ofxsLog.o \
ofxsCore.o \
ofxsPropertyValidation.o \
ofxsImageEffect.o \
ofxsParams.o 

PLUGINNAME = CImgConvolve

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
#net.sf.cimg.CImgConvolve.png \
#net.sf.cimg.CImgConvolve.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgConvolve.o: CImgConvolve.cpp CImgFFTConvolve.h CImgFilter.h CImg.h

# CImg.h and, with FFT=pocketfft, pocketfft_hdronly.h, made by ../Makefile (CImg.h is the same for
# every FFT)
CImg.h:
	cd .. && $(MAKE) sources

# LICENSE and FFT (see ../license.mk): the FFT flags, and pocketfft
include $(TOP_SRCDIR)/CImg/CImgFFT.mk
ifeq ($(FFT),pocketfft)
$(FFT_HEADERS) $(FFT_RESOURCES):
	cd .. && $(MAKE) sources
endif
