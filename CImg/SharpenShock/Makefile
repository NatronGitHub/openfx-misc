PLUGINOBJECTS = ofxsThreadSuite.o \
ofxsOpenMP.o \
CImgSharpenShock.o \
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

PLUGINNAME = CImgSharpenShock

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgSharpenShock.png \
#net.sf.cimg.CImgSharpenShock.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgSharpenShock.o: CImgSharpenShock.cpp  CImg.h
