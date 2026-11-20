# (ofxsLut required for ChromaBlur)
PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
ofxsLut.o \
CImgBilateral.o \
CImgBlur.o \
CImgConvolve.o \
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
CImgZDefocus.o \
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
Convolve \
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
ZDefocus \

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

CIMG_INCLUDE ?= /opt/local/include
CXXFLAGS += \
-I$(CIMG_INCLUDE) \
-I. \
-IBilateral \
-IBlur \
-IConvolve \
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
-IZDefocus \

# openmp
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

# CIMGVERSION is declared in CImgVersion.mk (also read by CMakeLists.txt)
include $(TOP_SRCDIR)/CImg/CImgVersion.mk
# LICENSE and FFT (license.mk), and the FFT flags
CIMG_DIR = .
include $(TOP_SRCDIR)/CImg/CImgFFT.mk

# -f: fail on an HTTP error, instead of saving the error page as the header
# (and remove what was written, so that the next make tries again).
# --retry 3 retries transient errors (timeouts, HTTP 408/429/5xx).
# --retry-all-errors is not used: it needs curl >= 7.71, and older curl
# (e.g. 7.29 on CentOS 7) rejects unknown options.
CURL = curl -fsSL --retry 3
SHA256SUM = $(shell command -v sha256sum >/dev/null 2>&1 && echo sha256sum || echo shasum -a 256)

# The downloaded sources: CImg.h, Inpaint/inpaint.h, and pocketfft with FFT=pocketfft. The
# top-level Makefile makes them once, before the "make nomulti" builds, which share them.
.PHONY: sources
sources: CImg.h $(FFT_HEADERS) $(FFT_RESOURCES)

# CImg.h, patched with the scripts of openfx-gmic (see CImgVersion.mk): CIMG_PATCHES, in this
# order. It is the same for every FFT: CImg 4.0.8 has pocketfft (cimg_use_pocketfft) itself, and
# includes pocketfft_hdronly.h, next to it, only when FFT=pocketfft defines that macro.
# CImg.h.version (also written by CMakeLists.txt, in the same format) records CIMGVERSION,
# CIMG_PATCHES_FROM and the patches: CImg.h is downloaded and patched again when they change, or
# when a patch script changes.
# The header is patched in a temporary directory (the scripts want a file named CImg.h), and only
# replaces CImg.h once all the patches are applied: if the download or a patch fails, there is no
# CImg.h, and make fails.
CIMG_STAMP = $(strip $(CIMGVERSION) $(CIMG_PATCHES_FROM) $(CIMG_PATCHES))
CImg.h.version: FORCE
	@echo '$(CIMG_STAMP)' | cmp -s - $@ || echo '$(CIMG_STAMP)' > $@

CImg.h: CImg.h.version $(CIMG_PATCHES) Inpaint/inpaint.h
	@rm -rf $@ $@.tmp && mkdir $@.tmp
	$(CURL) -o $@.tmp/CImg.h https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/CImg.h || { rm -rf $@.tmp; exit 1; }
	@for p in $(CIMG_PATCHES); do \
	  sh $$p $@.tmp/CImg.h || { echo "Error: $$p failed on CImg.h $(CIMGVERSION)"; rm -rf $@.tmp; exit 1; }; \
	done
	mv $@.tmp/CImg.h $@ && rmdir $@.tmp

# Depend on CImgVersion.mk so that the headers are downloaded again when CIMGVERSION changes.
Inpaint/inpaint.h: $(TOP_SRCDIR)/CImg/CImgVersion.mk
	$(CURL) -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/inpaint.h || { rm -f $@; exit 1; }
	patch -p0 -d. < Inpaint/inpaint.h.patch || { rm -f $@; exit 1; }

# pocketfft (FFT=pocketfft), at POCKETFFTCOMMIT, checked against POCKETFFT_SHA256 (as in openfx-gmic's
# Makefile), next to CImg.h, which includes it with cimg_use_pocketfft. Downloaded again when the
# file's checksum differs (e.g. after a POCKETFFTCOMMIT change).
POCKETFFT_URL = https://raw.githubusercontent.com/mreineck/pocketfft/$(POCKETFFTCOMMIT)/pocketfft_hdronly.h
pocketfft_hdronly.h: FORCE
	@if [ -f $@ ] && [ "`$(SHA256SUM) $@ | cut -d' ' -f1`" = "$(POCKETFFT_SHA256)" ]; then exit 0; fi; \
	rm -f $@ $@.tmp; \
	echo "Fetching $(POCKETFFT_URL)"; \
	$(CURL) -o $@.tmp $(POCKETFFT_URL) || { rm -f $@.tmp; exit 1; }; \
	sum=`$(SHA256SUM) $@.tmp | cut -d' ' -f1`; \
	if [ "$$sum" != "$(POCKETFFT_SHA256)" ]; then rm -f $@.tmp; echo "Error: checksum mismatch for $@ (got $$sum)"; exit 1; fi; \
	mv $@.tmp $@
# its licence (BSD-3-Clause, with the copyright notices), from the header's first comment, for the
# bundle's Resources (the licence asks for its notice in binary distributions)
Licence_pocketfft.txt: pocketfft_hdronly.h
	awk 'NR == 1 && $$0 != "/*" { exit 1 } NR > 1 && $$0 == "*/" { done = 1; exit } NR > 1 { print }  END { if (!done) exit 1 }' $< > $@.tmp
	grep -q 'Redistributions in binary form must reproduce' $@.tmp
	mv $@.tmp $@

#nlmeans.h:
#	$(CURL) -o $@ https://raw.githubusercontent.com/dtschump/CImg/$(CIMGVERSION)/plugins/nlmeans.h

.PHONY: distclean

distclean: clean
	-rm -rf CImg.h CImg.h.tmp CImg.h.version Inpaint/inpaint.h pocketfft_hdronly.h Licence_pocketfft.txt

#git archive --remote=git://git.code.sf.net/p/gmic/source $(CIMGVERSION):src CImg.h | tar xf -

$(OBJECTPATH)/CImgBilateral.o: CImgBilateral.cpp CImgOperator.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgBlur.o: CImgBlur.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgConvolve.o: CImgConvolve.cpp CImgFFTConvolve.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDenoise.o: CImgDenoise.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDistance.o: CImgDistance.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgEqualize.o: CImgEqualize.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgDilate.o: CImgDilate.cpp CImgErodeDilate.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgErode.o: CImgErode.cpp CImgErodeDilate.h CImgFilter.h CImg.h

$(OBJECTPATH)/CImgErodeSmooth.o: CImgErodeSmooth.cpp CImgFilter.h CImg.h

$(OBJECTPATH)/CImgExpression.o: CImgExpression.cpp CImgFFTConvolve.h CImgFilter.h CImg.h

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

$(OBJECTPATH)/CImgZDefocus.o: CImgZDefocus.cpp CImgFFTConvolve.h CImgFilter.h CImg.h
