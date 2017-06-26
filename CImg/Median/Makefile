PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgMedian.o \
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

PLUGINNAME = CImgMedian

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
#net.sf.cimg.CImgMedian.png \
#net.sf.cimg.CImgMedian.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgMedian.o: CImgMedian.cpp  CImg.h

CImg.h:
	cd .. && $(MAKE) $@
