PLUGINOBJECTS = \
	TestRender.o \
	TestPosition.o \
	TestGroups.o \
	TestOpenGL.o TestOpenGLRenderGL.o  TestOpenGLRenderMesa.o \
	ofxsTransform3x3.o ofxsShutter.o

PLUGINNAME = Test

OSMESA_PATH ?= /opt/mesa

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

## Enable these if you installed a mangled OSMesa (preferably with 32 bits FP support)
#CXXFLAGS += -DHAVE_OSMESA
#LINKFLAGS += -L$(OSMESA_PATH)/lib -lMangledOSMesa32 -lMangledGLU `$(LLVM_PATH)/bin/llvm-config --ldflags --libs engine --system-libs`


$(OBJECTPATH)/TestOpenGL.o: TestOpenGL.cpp TestOpenGL.h

$(OBJECTPATH)/TestOpenGLRenderGL.o: TestOpenGLRenderGL.cpp TestOpenGLRender.h TestOpenGL.h

$(OBJECTPATH)/TestOpenGLRenderMesa.o: TestOpenGLRenderMesa.cpp TestOpenGLRender.h TestOpenGL.h
	@mkdir -p $(OBJECTPATH)
	$(CXX) -c -I$(OSMESA_PATH)/include $(CXXFLAGS) $< -o $@
