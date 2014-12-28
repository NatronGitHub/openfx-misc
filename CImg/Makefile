PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgEqualize.o CImgErode.o CImgErodeSmooth.o CImgGuided.o CImgHistEQ.o CImgNoise.o CImgPlasma.o CImgRollingGuidance.o CImgSmooth.o ofxsLut.o PluginRegistration.o
PLUGINNAME = CImg

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgBilateral.png \
net.sf.cimg.CImgBilateral.svg \
net.sf.cimg.CImgBlur.png \
net.sf.cimg.CImgBlur.svg \
net.sf.cimg.CImgDenoise.png \
net.sf.cimg.CImgDenoise.svg \
net.sf.cimg.CImgDilate.png \
net.sf.cimg.CImgDilate.svg \
net.sf.cimg.CImgErode.png \
net.sf.cimg.CImgErode.svg \
net.sf.cimg.CImgErodeSmooth.png \
net.sf.cimg.CImgErodeSmooth.svg \
net.sf.cimg.CImgGuided.png \
net.sf.cimg.CImgGuided.svg \
net.sf.cimg.CImgNoise.png \
net.sf.cimg.CImgNoise.svg \
net.sf.cimg.CImgPlasma.png \
net.sf.cimg.CImgPlasma.svg \
net.sf.cimg.CImgRollingGuidance.png \
net.sf.cimg.CImgRollingGuidance.svg \
net.sf.cimg.CImgSmooth.png \
net.sf.cimg.CImgSmooth.svg \

include ../Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += -I$(CIMG_INCLUDE)

# commit 9b52016cab3368744ea9f3cc20a3e9b4f0c66eb3 from Fri Oct 17 09:12:00 2014 +0200 fixes blur_bilateral
# commit ca9df234b937aba77e4d820b70e128e5d60230eb from Thu Oct 30 11:47:06 2014 +0100 adds blur_guided
# commit 57ffb8393314e5102c00e5f9f8fa3dcace179608 from Thu Dec 11 10:57:13 2014 +0100 fixes vanvliet
# commit c52e49eee959b4c59881a0c7a960c4592f1a03a9 from Fri Dec 12 09:47:13 2014 +0100 enhances get_displacement
# commit cc874bf2c6c9940d75dd40becc234b5369f3fb30 from Sun Dec 28 12:01:19 2014 +0100 fixes LabToXYZ
CIMGVERSION=cc874bf2c6c9940d75dd40becc234b5369f3fb30
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
