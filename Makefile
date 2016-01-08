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
MatteMonitor \
Merge \
Mirror \
Multiply \
MixViews \
NoOp \
OneView \
Position \
Premult \
Radial \
Ramp \
Rand \
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
TimeDissolve \
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
  SUBDIRS_NOMULTI += \
CImg/Bilateral \
CImg/Blur \
CImg/Denoise \
CImg/Equalize \
CImg/Erode \
CImg/ErodeSmooth \
CImg/Expression \
CImg/Guided \
CImg/HistEQ \
CImg/Median \
CImg/Noise \
CImg/Plasma \
CImg/RollingGuidance \
CImg/SharpenInvDiff \
CImg/SharpenShock \
CImg/Smooth \

endif

all: subdirs

.PHONY: nomulti subdirs clean install install-nomulti uninstall uninstall-nomulti $(SUBDIRS)

nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	(cd $@ && $(MAKE))

clean:
	@for i in $(SUBDIRS) $(SUBDIRS_NOMULTI); do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

install:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

install-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" install

uninstall:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $@)"; \
	  (cd $$i && $(MAKE) $@); \
	done

uninstall-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" uninstall
