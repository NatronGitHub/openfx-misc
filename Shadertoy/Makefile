PLUGINOBJECTS = \
	Shadertoy.o ShadertoyRenderGL.o  ShadertoyRenderMesa.o ShadertoyParse.o

PLUGINNAME = Shadertoy

RESOURCES = \
resources/README.txt \
resources/ball.frag.glsl \
resources/basicfractal.frag.glsl \
resources/bleepyblocks.frag.glsl \
resources/cellular.frag.glsl \
resources/colorcircles.frag.glsl \
resources/deformflower.frag.glsl \
resources/dotdotdot.frag.glsl \
resources/fastclouds.frag.glsl \
resources/fireball.frag.glsl \
resources/flaring.frag.glsl \
resources/fractaltiling.frag.glsl \
resources/glowingthing.frag.glsl \
resources/infinitefall.frag.glsl \
resources/inputtime.frag.glsl \
resources/juliasm.frag.glsl \
resources/juliatrap.frag.glsl \
resources/noise.frag.glsl \
resources/noiseanimelectric.frag.glsl \
resources/noiseanimlava.frag.glsl \
resources/noiseanimwatery.frag.glsl \
resources/plasma2.frag.glsl \
resources/plasmatriangle.frag.glsl \
resources/settings.xml \
resources/silexarst.frag.glsl \
resources/simplefire.frag.glsl \
resources/sinebands.frag.glsl \
resources/skyatnight.frag.glsl \
resources/spiral.frag.glsl \
resources/startunnel.frag.glsl \
resources/stellar.frag.glsl \
resources/test.frag.glsl \
resources/venus.frag.glsl \
resources/warp.frag.glsl \
resources/watercaustic.frag.glsl \
resources/worleynoisewaters.frag.glsl \
resources/tex00.png \
resources/tex01.png \
resources/tex02.png \
resources/tex03.png \
resources/tex04.png \
resources/tex05.png \
resources/tex06.png \
resources/tex07.png \
resources/tex08.png \
resources/tex09.png \
resources/tex10.png \
resources/tex11.png \
resources/tex12.png \
resources/tex14.png \
resources/tex15.png \
resources/tex16.png \

OSMESA_PATH ?= /opt/osmesa
LLVM_PATH ?= /opt/llvm

## Enable these if you installed a mangled OSMesa (preferably with 32 bits FP support)
#CXXFLAGS_MESA += -DHAVE_OSMESA
#LDFLAGS_MESA += -L$(OSMESA_PATH)/lib -lMangledOSMesa32 `$(LLVM_PATH)/bin/llvm-config --ldflags --libs engine mcjit mcdisassembler --system-libs`

CXXFLAGS_EXTRA = $(CXXFLAGS_MESA)
LDFLAGS_EXTRA = $(LDFLAGS_MESA)

TOP_SRCDIR = ..
include $(TOP_SRCDIR)/Makefile.master

$(OBJECTPATH)/Shadertoy.o: Shadertoy.cpp Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderGL.o: ShadertoyRenderGL.cpp ShadertoyRender.h Shadertoy.h

$(OBJECTPATH)/ShadertoyRenderMesa.o: ShadertoyRenderMesa.cpp ShadertoyRender.h Shadertoy.h
	@mkdir -p $(OBJECTPATH)
	$(CXX) -c -I$(OSMESA_PATH)/include $(CXXFLAGS) $< -o $@
