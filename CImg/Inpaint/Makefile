PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgInpaint-gpl.o \
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

PLUGINNAME = CImgInpaint

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
#net.sf.cimg.CImgInpaint.png \
#net.sf.cimg.CImgInpaint.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgInpant-gpl.o: CImgInpaint-gpl.cpp CImg.h inpaint.h

CImg.h:
	cd .. && $(MAKE) CImg.h

inpaint.h:
	cd .. && $(MAKE) Inpaint/inpaint.h
