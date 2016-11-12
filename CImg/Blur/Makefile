PLUGINOBJECTS = \
CImgBlur.o \
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

PLUGINNAME = CImgBlur

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgBlur.png \
net.sf.cimg.CImgBlur.svg \
net.sf.cimg.CImgBloom.png \
net.sf.cimg.CImgBloom.svg \
net.sf.cimg.CImgChromaBlur.png \
net.sf.cimg.CImgChromaBlur.svg \
net.sf.cimg.CImgLaplacian.png \
net.sf.cimg.CImgLaplacian.svg \
EdgeBlur.py \
EdgeBlur.nps \
EdgeBlur.gizmo

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp ../CImg.h
