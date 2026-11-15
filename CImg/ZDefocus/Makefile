PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
CImgZDefocus.o \
CImgFilter.o

# no ofxsInteract.o
SUPPORTOBJECTS = \
ofxsMultiThread.o \
ofxsProperty.o \
ofxsLog.o \
ofxsCore.o \
ofxsPropertyValidation.o \
ofxsImageEffect.o \
ofxsParams.o 

PLUGINNAME = CImgZDefocus

RESOURCES =

TOP_SRCDIR = ../..
include $(TOP_SRCDIR)/Makefile.master

VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

$(OBJECTPATH)/CImgZDefocus.o: CImgZDefocus.cpp CImgFilter.h CImg.h

CImg.h:
	cd .. && $(MAKE) $@
