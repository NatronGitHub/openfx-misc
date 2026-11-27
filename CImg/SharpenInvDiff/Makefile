PLUGINOBJECTS = ofxsThreadSuite.o \
ofxsOpenMP.o \
CImgSharpenInvDiff.o \
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

PLUGINNAME = CImgSharpenInvDiff

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgSharpenInvDiff.png \
net.sf.cimg.CImgSharpenInvDiff.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp  CImg.h
