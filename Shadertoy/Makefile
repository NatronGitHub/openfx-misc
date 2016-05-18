PLUGINOBJECTS = \
	Shadertoy.o ShadertoyRenderGL.o  ShadertoyRenderMesa.o

PLUGINNAME = Shadertoy

OSMESA_PATH ?= /opt/osmesa

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

## Enable these if you installed a mangled OSMesa (preferably with 32 bits FP support)
#CXXFLAGS += -DHAVE_OSMESA
#LINKFLAGS += -L$(OSMESA_PATH)/lib -lMangledOSMesa32 `$(LLVM_PATH)/bin/llvm-config --ldflags --libs engine mcjit mcdisassembler --system-libs`

$(OBJECTPATH)/Shadertoy.o: Shadertoy.cpp Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderGL.o: ShadertoyRenderGL.cpp ShadertoyRender.h Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderMesa.o: ShadertoyRenderMesa.cpp ShadertoyRender.h Shadertoy.h
	@mkdir -p $(OBJECTPATH)
	$(CXX) -c -I$(OSMESA_PATH)/include $(CXXFLAGS) $< -o $@
