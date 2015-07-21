SUBDIRS = Misc

SUBDIRS_NOMULTI = \
Add \
AdjustRoD \
Anaglyph \
AppendClip \
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
FrameHold \
FrameRange \
Gamma \
GodRays \
Grade \
ColorTransform \
HSVTool \
Distortion \
ImageStatistics \
Invert \
JoinViews \
Keyer \
Merge \
Mirror \
Multiply \
MixViews \
Noise \
NoOp \
OneView \
Position \
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
SlitScan \
Switch \
TimeBlur \
TimeOffset \
TrackerPM \
Transform \
VectorToColor

ifeq ($(CONFIG),debug)
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
