PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgHistEQ.o \
CImgFilter.o \
ofxsLut.o

# no ofxsInteract.o
SUPPORTOBJECTS = \
ofxsMultiThread.o \
ofxsProperty.o \
ofxsLog.o \
ofxsCore.o \
ofxsPropertyValidation.o \
ofxsImageEffect.o \
ofxsParams.o 

PLUGINNAME = CImgHistEQ

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgHistEQ.png \
net.sf.cimg.CImgHistEQ.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgHistEQ.o: CImgHistEQ.cpp  CImg.h
