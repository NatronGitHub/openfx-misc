PLUGINOBJECTS = ofxsThreadSuite.o \
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
eu.cimg.Inpaint.png \
eu.cimg.Inpaint.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgInpaint-gpl.o: CImgInpaint-gpl.cpp CImg.h inpaint.h

inpaint.h:
	cd .. && $(MAKE) Inpaint/inpaint.h
