PLUGINOBJECTS = CImgBilateral.o CImgBlur.o CImgDenoise.o CImgDilate.o CImgEqualize.o CImgErode.o CImgErodeSmooth.o CImgExpression.o CImgGuided.o CImgHistEQ.o CImgNoise.o CImgPlasma.o CImgRollingGuidance.o CImgSharpenInvDiff.o CImgSharpenShock.o CImgSmooth.o ofxsLut.o PluginRegistration.o CImgFilter.o CImgOperator.o
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
# commit b99c968d1594e52005d60145f11c71b3f5db8679 is CImg 1.6.3
# commit 852e71429760c2695f6b02b1a49400c31ad60154 is CImg 1.6.4
# commit d5e8da7bcc594a6df2878d19268c9217689703e6 has the math parser with loops!
# commit d89fbcf399b3663ce3ebac2ee387642fd64014b6 has abortable functions
CIMGVERSION=d89fbcf399b3663ce3ebac2ee387642fd64014b6

CImg.h:
	curl -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/CImg.h

#git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImg.h

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp CImg.h

$(OBJECTPATH)/CImgDenoise.o: CImgDenoise.cpp CImg.h

$(OBJECTPATH)/CImgDilate.o: CImgDilate.cpp CImg.h

$(OBJECTPATH)/CImgEqualize.o: CImgEqualize.cpp CImg.h

$(OBJECTPATH)/CImgErode.o: CImgErode.cpp CImg.h

$(OBJECTPATH)/CImgErodeSmooth.o: CImgErodeSmooth.cpp CImg.h

$(OBJECTPATH)/CImgExpression.o: CImgExpression.cpp CImg.h

$(OBJECTPATH)/CImgGuided.o: CImgGuided.cpp CImg.h

$(OBJECTPATH)/CImgHistEQ.o: CImgHistEQ.cpp CImg.h

$(OBJECTPATH)/CImgNoise.o: CImgNoise.cpp CImg.h

$(OBJECTPATH)/CImgPlasma.o: CImgPlasma.cpp CImg.h

$(OBJECTPATH)/CImgRollingGuidance.o: CImgRollingGuidance.cpp CImg.h

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp CImg.h

$(OBJECTPATH)/CImgSharpenShock.o: CImgSharpenShock.cpp CImg.h

$(OBJECTPATH)/CImgSmooth.o: CImgSmooth.cpp CImg.h
