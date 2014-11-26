SUBDIRS = Misc

SUBDIRS_NOMULTI = \
AdjustRoD \
Anaglyph \
ChromaKeyer \
Clamp \
ColorCorrect \
ColorLookup \
ColorMatrix \
Constant \
CornerPin \
Crop \
Deinterlace \
Difference \
Dissolve \
Grade \
HSV \
HSVTool \
Invert \
JoinViews \
Keyer \
Merge \
MixViews \
Noise \
NoOp \
OneView \
Premult \
Roto \
Shuffle \
SideBySide \
Switch \
Test \
TimeOffset \
TrackerPM \
Transform \
VectorToColor

ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # TrackerPM is not well tested yet
  SUBDIRS += TrackerPM
endif

HAVE_CIMG ?= 1

# Build CImg-based plugins separately.
ifneq ($(HAVE_CIMG),0)
# add plugins which may use CImg here
  SUBDIRS += CImg
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
