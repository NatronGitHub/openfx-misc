SUBDIRS = Misc

SUBDIRS_NOMULTI = \
Anaglyph \
ChromaKeyer \
ColorCorrect \
ColorMatrix \
Constant \
CornerPin \
Crop \
Difference \
Grade \
Invert \
JoinViews \
Merge \
MixViews \
Noise \
NoOp \
OneView \
Premult \
RGBLut \
Roto \
Shuffle \
SideBySide \
Switch \
TestRender \
TimeOffset \
TrackerPM \
Transform

ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # TrackerPM is not well tested yet
  SUBDIRS += TrackerPM
endif

HAVE_CIMG ?= 0

# Build CImg-based plugins separately.
ifneq ($(HAVE_CIMG),0)
# add plugins which may use CImg here
#  SUBDIRS += GMIC
endif

all: subdirs

.PHONY: nomulti subdirs clean $(SUBDIRS)

nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean :
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i clean; \
	done
