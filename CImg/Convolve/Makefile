PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
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

CImg.h:
	cd .. && $(MAKE) $@
