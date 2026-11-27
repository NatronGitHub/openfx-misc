PLUGINOBJECTS = ofxsThreadSuite.o ofxsOpenMP.o DenoiseSharpen.o ofxsLut.o ofxsRectangleInteract.o
PLUGINNAME = DenoiseSharpen
#RESOURCES = net.sf.openfx.DenoiseSharpen.png net.sf.openfx.DenoiseSharpen.svg

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

# openmp, as Misc/Makefile
ifeq ($(OPENMP),1)
  CXXFLAGS += -fopenmp
  LINKFLAGS += -fopenmp
endif
