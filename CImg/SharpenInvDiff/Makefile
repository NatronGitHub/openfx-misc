PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgSharpenInvDiff.o \
CImgFilter.o \

# no ofxsInteract.o
SUPPORTOBJECTS = \
ofxsMultiThread.o \
ofxsProperty.o \
ofxsLog.o \
ofxsCore.o \
ofxsPropertyValidation.o \
ofxsImageEffect.o \
ofxsParams.o 

PLUGINNAME = CImgSharpenInvDiff

# ls *png *.svg|sort |sed -e 's!$! \\!'
RESOURCES = \
net.sf.cimg.CImgSharpenInvDiff.png \
net.sf.cimg.CImgSharpenInvDiff.svg \

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

# the shared CImg sources also need CImg.h (downloaded by ../Makefile)
$(OBJECTPATH)/CImgFilter.o $(OBJECTPATH)/CImgOperator.o: CImg.h

$(OBJECTPATH)/CImgSharpenInvDiff.o: CImgSharpenInvDiff.cpp  CImg.h

CImg.h:
	cd .. && $(MAKE) $@
