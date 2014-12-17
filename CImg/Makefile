PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgEqualize.o CImgErode.o CImgErodeSmooth.o CImgGuided.o CImgHistEQ.o CImgNoise.o CImgPlasma.o CImgRollingGuidance.o CImgSmooth.o ofxsLut.o PluginRegistration.o
PLUGINNAME = CImg
RESOURCES = \
net.sf.cimg.CImgBlur.png \
net.sf.cimg.CImgBlur.svg \
net.sf.cimg.CImgDenoise.png \
net.sf.cimg.CImgDenoise.svg \
net.sf.cimg.CImgDilate.png \
net.sf.cimg.CImgDilate.svg \
net.sf.cimg.CImgErode.png \
net.sf.cimg.CImgErode.svg \
net.sf.cimg.CImgSmooth.png \
net.sf.cimg.CImgSmooth.svg \
net.sf.cimg.CImgPlasma.png \
net.sf.cimg.CImgPlasma.svg \
net.sf.cimg.CImgNoise.png \
net.sf.cimg.CImgNoise.svg

include ../Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += -I$(CIMG_INCLUDE)

# commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3 from Fri Oct 17 09:12:00 2014 +0200 fixes blur_bilateral
# commit ca9df234b937aba77e4d820b70e128e5d60230eb from Thu Oct 30 11:47:06 2014 +0100 adds blur_guided
# commit eccc25012e96872312f4aa33c3596c257fe6d32c from Fri Oct 31 12:54:02 2014 +0100
CIMGVERSION=eccc25012e96872312f4aa33c3596c257fe6d32c
CImg.h:
	git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImg.h

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp CImg.h

$(OBJECTPATH)/CImgDenoise.o: CImgDenoise.cpp CImg.h

$(OBJECTPATH)/CImgDilate.o: CImgDilate.cpp CImg.h

$(OBJECTPATH)/CImgEqualize.o: CImgEqualize.cpp CImg.h

$(OBJECTPATH)/CImgErode.o: CImgErode.cpp CImg.h

$(OBJECTPATH)/CImgErodeSmooth.o: CImgErodeSmooth.cpp CImg.h

$(OBJECTPATH)/CImgGuided.o: CImgGuided.cpp CImg.h

$(OBJECTPATH)/CImgHistEQ.o: CImgHistEQ.cpp CImg.h

$(OBJECTPATH)/CImgNoise.o: CImgNoise.cpp CImg.h

$(OBJECTPATH)/CImgPlasma.o: CImgPlasma.cpp CImg.h

$(OBJECTPATH)/CImgRollingGuidance.o: CImgRollingGuidance.cpp CImg.h

$(OBJECTPATH)/CImgSmooth.o: CImgGuided.cpp CImg.h