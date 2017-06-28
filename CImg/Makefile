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
-IMedian \
-INoise \
-IPlasma \
-IRollingGuidance \
-ISharpenInvDiff \
-ISharpenShock \
-ISmooth \


# For CImg.h versions,
# see https://github.com/dtschump/CImg/commits/master/CImg.h
#
# commit b99c968d1594e52005d60145f11c71b3f5db8679 is CImg 1.6.3
# commit 852e71429760c2695f6b02b1a49400c31ad60154 is CImg 1.6.4
# commit d5e8da7bcc594a6df2878d19268c9217689703e6 has the math parser with loops!
# commit d89fbcf399b3663ce3ebac2ee387642fd64014b6 has abortable functions
# commit 9e1e9884f3553d84f73dd20a898b4ba53c95af65 is CImg 1.6.5
# commit bb34c1a7d6a0911cbc80c8310f0508301d58780d is CImg 1.6.7
# commit 689bf7d839609147d81963032953e385e5de1ab8 is CImg 1.6.8
# commit 035ae8fb8b90dcaf9e186b3712a2b5a8e4b3a255 is CImg 1.6.9
# commit 7f84548e5ee0e2eaaf473ad936eb54abb0c72d45 is CImg 1.7.0
# commit aa2f7451cfa3a881948453cce0be175acbc6ca10 is CImg 1.7.1
# commit 81e3aebe14544ac975125a50e52dc35130dd6d0d is CImg 1.7.1 + abort fixes
# commit 4f8f21f3cf1707e8a9b6d2100fc61d3b3768ce34 is CImg 1.7.1 + use macros for openmp
# commit 72c6b1f8aad6af32f27acbedd07b0ec72475c4c6 is CImg 1.7.2
# commit 4d607387b95ba7608b5ae451971e1a0528990bd0 is CImg 1.7.2 + windows fixes
# commit 93d77710300f4ac96aa5bfb0a2d7a5322216fe66 is CImg 1.7.4
# commit 1d31b3cdb7b7c102183e1e52d6d293c36a88760c is CImg 1.7.5
# commit 24d8d833b2ae937f87d88da299314b9cd4561e63 is CImg 1.7.6
# commit 02ec481605f4dd68dec9d8c5b7b4bd763ad31cac is CImg 1.7.7
# commit 572c12d82b2f59ece21be8f52645c38f1dd407e6 is CImg 1.7.8
# commit 6cf2f433edd9ef07a9c865282c3fd84cc4e9b327 is CImg 1.7.9
# commit 0728d3e6833de1dfe16734fdef416b5051d4df7e is CImg pre-2.0.0 with nb_iter argument to boxfilter
# commit 2067bd0765d23afc4c41b706bc57d094a2dce51d is CImg pre-2.0.0 with precision fix to boxfilter
# commit 63a4356e9e93ff3cd407e1ae1264a616ea4ba0f2 is CImg pre-2.0.0 with the clamping on sigma_r fixed in blur_bilateral
# commit 7c83bdad65ab7447220b54851a5a1035976777fa is CImg pre-2.0.0 with a fix to compile inpaint.h
# commit 79fce46b2b1b54bc44276dccca040139f62835d7 is CImg 2.0.0
CIMGVERSION=79fce46b2b1b54bc44276dccca040139f62835d7

CImg.h: Inpaint/inpaint.h
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/CImg.h
	patch -p0 -d. < CImg-2.0.0-anisotropic.patch
#	patch -p0 -d. < CImg-1.7.1-omp.patch

Inpaint/inpaint.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/inpaint.h

nlmeans.h:
	curl -s -S -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/nlmeans.h

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

$(OBJECTPATH)/CImgMedian.o: CImgMedian.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgNoise.o: CImgNoise.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgOperator.o: CImgOperator.cpp CImgOperator.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgPlasma.o: CImgPlasma.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgRollingGuidance.o: CImgRollingGuidance.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSharpenShock.o: CImgSharpenShock.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgSmooth.o: CImgSmooth.cpp CImgFilter.h CImg.h
