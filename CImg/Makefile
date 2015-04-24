PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgEqualize.o CImgErode.o CImgErodeSmooth.o CImgGuided.o CImgHistEQ.o CImgNoise.o CImgPlasma.o CImgRollingGuidance.o CImgSharpenInvDiff.o CImgSharpenShock.o CImgSmooth.o ofxsLut.o PluginRegistration.o
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
net.sf.cimg.CImgLaplacian.png \
net.sf.cimg.CImgLaplacian.svg \
net.sf.cimg.CImgNoise.png \
net.sf.cimg.CImgNoise.svg \
net.sf.cimg.CImgPlasma.png \
net.sf.cimg.CImgPlasma.svg \
net.sf.cimg.CImgRollingGuidance.png \
net.sf.cimg.CImgRollingGuidance.svg \
net.sf.cimg.CImgSharpenInvDiff.png \
net.sf.cimg.CImgSharpenShock.png \
net.sf.cimg.CImgSmooth.png \
net.sf.cimg.CImgSmooth.svg 

include ../Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += -I$(CIMG_INCLUDE)

# For CImg.h versions,
# see http://sourceforge.net/p/cimg/source/ci/master/log/?path=/CImg.h
#
# commit 36807b7ece2e2c196bd05f0a212a75dc8379a493 from 2015-04-15 07:27:02  is CImg 1.6.2.0
CIMGVERSION=36807b7ece2e2c196bd05f0a212a75dc8379a493
CImg.h:
	curl -o $@ http://sourceforge.net/p/cimg/source/ci/$(CIMGVERSION)/tree/CImg.h?format=raw

#git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -

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

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp CImg.h

$(OBJECTPATH)/CImgSharpenShock.o: CImgSharpenShock.cpp CImg.h

$(OBJECTPATH)/CImgSmooth.o: CImgSmooth.cpp CImg.h
