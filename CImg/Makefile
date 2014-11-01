PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgErode.o CImgErodeSmooth.o CImgEqualize.o CImgGuided.o CImgNoise.o CImgPlasma.o CImgSmooth.o CImgRollingGuidance.o  PluginRegistration.o
PLUGINNAME = CImg
#RESOURCES = net.sf.cimg.CImg.png net.sf.cimg.CImg.svg

include ../Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += -I$(CIMG_INCLUDE)

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImg.h

# commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3 from Fri Oct 17 09:12:00 2014 +0200 fixes blur_bilateral
# commit ca9df234b937aba77e4d820b70e128e5d60230eb from Thu Oct 30 11:47:06 2014 +0100 adds blur_guided
# commit eccc25012e96872312f4aa33c3596c257fe6d32c from Fri Oct 31 12:54:02 2014 +0100
CIMGVERSION=eccc25012e96872312f4aa33c3596c257fe6d32c
CImg.h:
	git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -
