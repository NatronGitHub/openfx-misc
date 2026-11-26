PLUGINOBJECTS = ofxsThreadSuite.o \
ofxsOpenMP.o \
CImgExpression.o \
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

PLUGINNAME = CImgExpression

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgExpression.png \
net.sf.cimg.CImgExpression.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgExpression.o: CImgExpression.cpp CImgFFTConvolve.h CImgFilter.h CImg.h

# LICENSE and FFT (see ../license.mk): the FFT flags, and pocketfft (fft() of the expressions,
# CImgFFTConvolve.h), as Convolve and ZDefocus
include $(TOP_SRCDIR)/CImg/CImgFFT.mk
ifeq ($(FFT),pocketfft)
$(FFT_HEADERS) $(FFT_RESOURCES):
	cd .. && $(MAKE) sources
endif
