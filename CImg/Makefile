# (ofxsLut required for ChromaBlur)
PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
ofxsLut.o \
CImgBilateral.o \
CImgBlur.o \
CImgDenoise.o \
CImgDilate.o \
CImgDistance.o \
CImgEqualize.o \
CImgErode.o \
CImgErodeSmooth.o \
CImgExpression.o \
CImgGuided.o \
CImgHistEQ.o \
CImgInpaint-gpl.o \
CImgMatrix.o \
CImgMedian.o \
CImgNoise.o \
CImgPlasma.o \
CImgRollingGuidance.o \
CImgSharpenInvDiff.o \
CImgSharpenShock.o \
CImgSmooth.o \
CImgFilter.o \
CImgOperator.o \

# no ofxsInteract.o
SUPPORTOBJECTS = \
ofxsMultiThread.o \
ofxsProperty.o \
ofxsLog.o \
ofxsCore.o \
ofxsPropertyValidation.o \
ofxsImageEffect.o \
ofxsParams.o \

PLUGINNAME = CImg

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
Bilateral/net.sf.cimg.CImgBilateral.png \
Bilateral/net.sf.cimg.CImgBilateral.svg \
Blur/net.sf.cimg.CImgBlur.png \
Blur/net.sf.cimg.CImgBlur.svg \
Blur/net.sf.cimg.CImgChromaBlur.png \
Blur/net.sf.cimg.CImgChromaBlur.svg \
Blur/net.sf.cimg.CImgBloom.png \
Blur/net.sf.cimg.CImgBloom.svg \
Blur/eu.cimg.EdgeDetect.png \
Blur/eu.cimg.EdgeDetect.svg \
Blur/eu.cimg.EdgeExtend.png \
Blur/eu.cimg.EdgeExtend.svg \
Blur/fr.inria.EdgeBlur.png \
Blur/fr.inria.EdgeBlur.svg \
Blur/net.sf.cimg.CImgLaplacian.png \
Blur/net.sf.cimg.CImgLaplacian.svg \
Blur/net.sf.cimg.CImgSharpen.png \
Blur/net.sf.cimg.CImgSharpen.svg \
Denoise/net.sf.cimg.CImgDenoise.png \
Denoise/net.sf.cimg.CImgDenoise.svg \
Equalize/net.sf.cimg.CImgEqualize.png \
Equalize/net.sf.cimg.CImgEqualize.svg \
Erode/net.sf.cimg.CImgDilate.png \
Erode/net.sf.cimg.CImgDilate.svg \
Erode/net.sf.cimg.CImgErode.png \
Erode/net.sf.cimg.CImgErode.svg \
ErodeSmooth/net.sf.cimg.CImgErodeSmooth.png \
ErodeSmooth/net.sf.cimg.CImgErodeSmooth.svg \
Expression/net.sf.cimg.CImgExpression.png \
Expression/net.sf.cimg.CImgExpression.svg \
Guided/net.sf.cimg.CImgGuided.png \
Guided/net.sf.cimg.CImgGuided.svg \
HistEQ/net.sf.cimg.CImgHistEQ.png \
HistEQ/net.sf.cimg.CImgHistEQ.svg \
Inpaint/eu.cimg.Inpaint.png \
Inpaint/eu.cimg.Inpaint.svg \
Noise/net.sf.cimg.CImgNoise.png \
Noise/net.sf.cimg.CImgNoise.svg \
Plasma/net.sf.cimg.CImgPlasma.png \
Plasma/net.sf.cimg.CImgPlasma.svg \
RollingGuidance/net.sf.cimg.CImgRollingGuidance.png \
RollingGuidance/net.sf.cimg.CImgRollingGuidance.svg \
SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.png \
SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.svg \
SharpenShock/net.sf.cimg.CImgSharpenShock.png \
Smooth/net.sf.cimg.CImgSmooth.png \
Smooth/net.sf.cimg.CImgSmooth.svg \

VPATH += \
Bilateral \
Blur \
Denoise \
Distance \
Equalize \
Erode \
ErodeSmooth \
Expression \
Guided \
HistEQ \
Inpaint \
Matrix \
Median \
Noise \
Plasma \
RollingGuidance \
SharpenInvDiff \
SharpenShock \
Smooth \

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += \
-I$(CIMG_INCLUDE) \
-I. \
-IBilateral \
-IBlur \
-IDenoise \
-IDistance \
-IEqualize \
-IErode \
-IErodeSmooth \
-IExpression \
-IGuided \
-IHistEQ \
-IInpaint \
-IMatrix \
-IMedian \
-INoise \
-IPlasma \
-IRollingGuidance \
-ISharpenInvDiff \
-ISharpenShock \
-ISmooth \

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

# For CImg.h versions,
# see https://github.com/dtschump/CImg/commits/master/CImg.h
#
# commit a841cc3080ee7bc00c5781e94b7f06c2920453ac is CImg 2.2.3+
# commit 4d54d452896fdf9347af68d3854f98f4986eea4c is CImg 2.3.1
# commit 548744b47c9316f656c4bdd1206e78646226c6be is CImg 2.3.3
CIMGVERSION=548744b47c9316f656c4bdd1206e78646226c6be

CImg.h: Inpaint/inpaint.h
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/CImg.h
#	patch -p0 -d. < CImg-2.0.0-anisotropic.patch
#	patch -p0 -d. < CImg-1.7.1-omp.patch

Inpaint/inpaint.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/inpaint.h
	patch -p0 -d. < Inpaint/inpaint.h.patch

nlmeans.h:
	curl -L -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/nlmeans.h

#git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImgOperator.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDenoise.o: CImgDenoise.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDistance.o: CImgDistance.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgEqualize.o: CImgEqualize.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDilate.o: CImgDilate.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgErode.o: CImgErode.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgErodeSmooth.o: CImgErodeSmooth.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgExpression.o: CImgExpression.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgFilter.o: CImgOperator.cpp CImgOperator.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgGuided.o: CImgGuided.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgHistEQ.o: CImgHistEQ.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgInpaint-gpl.o: CImgInpaint-gpl.cpp CImgFilter.h CImg.h Inpaint/inpaint.h

$(OBJECTPATH)/CImgMatrix.o: CImgMatrix.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgMedian.o: CImgMedian.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgNoise.o: CImgNoise.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgOperator.o: CImgOperator.cpp CImgOperator.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgPlasma.o: CImgPlasma.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgRollingGuidance.o: CImgRollingGuidance.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSharpenShock.o: CImgSharpenShock.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSmooth.o: CImgSmooth.cpp CImgFilter.h CImg.h
