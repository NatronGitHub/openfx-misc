PLUGINOBJECTS = \
	TestRender.o \
	TestPosition.o \
	TestGroups.o \
	TestOpenGL.o TestOpenGLRenderGL.o  TestOpenGLRenderMesa.o \
	PluginRegistration.o ofxsTransform3x3.o ofxsShutter.o

PLUGINNAME = Test

include ../Makefile.master

CXXFLAGS += -DHAVE_OSMESA -I$(OSMESA_PATH)/include
LINKFLAGS += -L$(OSMESA_PATH)/lib -l
