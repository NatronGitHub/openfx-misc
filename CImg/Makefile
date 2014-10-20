PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgErode.o CImgErodeSmooth.o CImgEqualize.o CImgNoise.o CImgPlasma.o CImgSmooth.o CImgRollingGuidance.o  PluginRegistration.o
PLUGINNAME = CImg
#RESOURCES = net.sf.cimg.CImg.png net.sf.cimg.CImg.svg

include ../Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += -I$(CIMG_INCLUDE)

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImg.h

# commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3 from Fri Oct 17 09:12:00 2014 +0200 fixes bilateral filter
CImg.h:
	git archive --remote=git://git.code.sf.net/p/gmic/source 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3:src CImg.h | tar xf -
