PLUGINOBJECTS = ofxsThreadSuite.o \
ofxsOpenMP.o \
CImgDistance.o \
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

PLUGINNAME = CImgDistance

# ls *png *.svg|sort |sed -e 's!$! \\!'
#RESOURCES = \
#net.sf.cimg.CImgDistance.png \
#net.sf.cimg.CImgDistance.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgDistance.o: CImgDistance.cpp  CImg.h
