PLUGINOBJECTS = \
        Switch.o \
        TimeOffset.o \
        RGBLut.o \
        SideBySide.o \
        MixViews.o \
        OneView.o \
        JoinViews.o \
        Anaglyph.o \
	ColorCorrect.o \
	Grade.o \
    Transform.o \
    GenericTransform.o \
        PluginRegistrationCombined.o

PLUGINNAME = Misc
VPATH += ../Switch ../TimeOffset ../RGBLut ../SideBySide ../MixViews ../OneView ../JoinViews ../Anaglyph ../ColorCorrect ../Grade ../Transform

include ../Makefile.master

CXXFLAGS += -I../Anaglyph -I../ColorCorrect -I../Grade -I../JoinViews -I../OneView -I../RGBLut -I../SideBySide -I../Switch -I../TimeOffset -I../MixViews
