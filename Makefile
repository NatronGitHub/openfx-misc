SUBDIRS = Misc

SUBDIRS_NOMULTI = \
Add \
AdjustRoD \
Anaglyph \
CheckerBoard \
ChromaKeyer \
Clamp \
ClipTest \
ColorCorrect \
ColorLookup \
ColorMatrix \
Constant \
CopyRectangle \
CornerPin \
Crop \
Deinterlace \
Difference \
Dissolve \
FrameBlend \
Gamma \
GodRays \
Grade \
ColorTransform \
HSVTool \
ImageStatistics \
Invert \
JoinViews \
Keyer \
Merge \
Multiply \
MixViews \
Noise \
NoOp \
OneView \
Premult \
Radial \
Ramp \
ReConverge \
Rectangle \
Retime \
Roto \
Saturation \
Shuffle \
SideBySide \
Switch \
TimeOffset \
TrackerPM \
Transform \
VectorToColor

ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy Test
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
