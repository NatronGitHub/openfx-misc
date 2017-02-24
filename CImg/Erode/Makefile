PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgDilate.o \
CImgErode.o \
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

PLUGINNAME = CImgDilate

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgDilate.png \
net.sf.cimg.CImgDilate.svg \
net.sf.cimg.CImgErode.png \
net.sf.cimg.CImgErode.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgDilate.o: CImgDilate.cpp ../CImg.h

$(OBJECTPATH)/CImgErode.o: CImgErode.cpp ../CImg.h
