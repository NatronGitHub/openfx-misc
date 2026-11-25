PLUGINOBJECTS = ofxsThreadSuite.o \
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
net.sf.cimg.CImgSharpen.png \
net.sf.cimg.CImgSharpen.svg \
eu.cimg.EdgeDetect.png \
eu.cimg.EdgeDetect.svg \
eu.cimg.EdgeExtend.png \
eu.cimg.EdgeExtend.svg \
EdgeBlur.nps

# the EdgeBlur PyPlug, with its icons: in Contents/Resources/PyPlugs (Makefile.master)
PYPLUGS = EdgeBlur.py EdgeBlur.gizmo fr.inria.EdgeBlur.png fr.inria.EdgeBlur.svg

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

include $(TOP_SRCDIR)/CImg/CImgSubplugin.mk

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp  CImg.h
