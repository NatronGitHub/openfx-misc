PLUGINOBJECTS = \
	Shadertoy.o ShadertoyRenderGL.o  ShadertoyRenderMesa.o ShadertoyParse.o

PLUGINNAME = Shadertoy

RESOURCES = \
presets

OSMESA_PATH ?= /opt/osmesa
LLVM_PATH ?= /opt/llvm

## Enable these if you installed a mangled OSMesa (preferably with 32 bits FP support)
#CXXFLAGS_MESA += -DHAVE_OSMESA
#LDFLAGS_MESA += -L$(OSMESA_PATH)/lib -lMangledOSMesa32 `$(LLVM_PATH)/bin/llvm-config --ldflags --libs engine mcjit mcdisassembler --system-libs`

CXXFLAGS_EXTRA = $(CXXFLAGS_MESA)
LDFLAGS_EXTRA = $(LDFLAGS_MESA)

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

# to include all textures:
#$(OBJECTPATH)/Shadertoy.o: Shadertoy.cpp Shadertoy.h presets/tex00.jpg

$(OBJECTPATH)/Shadertoy.o: Shadertoy.cpp Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderGL.o: ShadertoyRenderGL.cpp ShadertoyRender.h Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderMesa.o: ShadertoyRenderMesa.cpp ShadertoyRender.h Shadertoy.h
	@mkdir -p $(OBJECTPATH)
	$(AM_V_CXX)$(CXX) -c -I$(OSMESA_PATH)/include $(CXXFLAGS) $< -o $@

presets/tex00.jpg:
	$(MAKE) -C presets
