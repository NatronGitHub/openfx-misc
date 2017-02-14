PLUGINOBJECTS = ofxsThreadSuite.o tinythread.o \
ofxsTransform3x3.o ofxsShutter.o \
TestRender.o \
TestPosition.o \
TestGroups.o \
TestOpenGL.o TestOpenGLRenderGL.o  TestOpenGLRenderMesa.o \

PLUGINNAME = Test

OSMESA_PATH ?= /opt/mesa

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

## Enable these if you installed a mangled OSMesa (preferably with 32 bits FP support)
#CXXFLAGS_MESA += -DHAVE_OSMESA
#LDFLAGS_MESA += -L$(OSMESA_PATH)/lib -lMangledOSMesa32 `$(LLVM_PATH)/bin/llvm-config --ldflags --libs engine mcjit mcdisassembler --system-libs`

CXXFLAGS_EXTRA = ${CXXFLAGS_MESA}
LDFLAGS_EXTRA = ${LDFLAGS_MESA}

$(OBJECTPATH)/TestOpenGL.o: TestOpenGL.cpp TestOpenGL.h

$(OBJECTPATH)/TestOpenGLRenderGL.o: TestOpenGLRenderGL.cpp TestOpenGLRender.h TestOpenGL.h

$(OBJECTPATH)/TestOpenGLRenderMesa.o: TestOpenGLRenderMesa.cpp TestOpenGLRender.h TestOpenGL.h
	@mkdir -p $(OBJECTPATH)
	$(AM_V_CXX)$(CXX) -c -I$(OSMESA_PATH)/include $(CXXFLAGS) $< -o $@
