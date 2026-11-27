PLUGINOBJECTS = ofxsThreadSuite.o \
ofxsOpenMP.o \
CImgPlasma.o \
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

PLUGINNAME = CImgPlasma

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgPlasma.png \
net.sf.cimg.CImgPlasma.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgPlasma.o: CImgPlasma.cpp  CImg.h
