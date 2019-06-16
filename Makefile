SUBDIRS = Misc Shadertoy

SUBDIRS_NOMULTI = \
TimeBuffer \
Add \
AdjustRoD \
Anaglyph \
AppendClip \
Card3D \
CheckerBoard \
ChromaKeyer \
Clamp \
ClipTest \
ColorBars \
ColorCorrect \
ColorLookup \
ColorMatrix \
ColorSuppress \
ColorTransform \
ColorWheel \
Constant \
ContactSheet \
CopyRectangle \
CornerPin \
Crop \
Deinterlace \
DenoiseSharpen \
Despill \
Difference \
Dissolve \
FrameBlend \
FrameHold \
FrameRange \
Gamma \
GodRays \
Grade \
HSVTool \
HueCorrect \
Distortion \
ImageStatistics \
Invert \
JoinViews \
Keyer \
KeyMix \
LayerContactSheet \
Log2Lin \
MatteMonitor \
Merge \
Mirror \
Multiply \
MixViews \
NoOp \
OneView \
PIK \
PLogLin \
Position \
Premult \
Radial \
Ramp \
Rand \
ReConverge \
Rectangle \
Reformat \
Retime \
Roto \
Saturation \
Shuffle \
SideBySide \
SlitScan \
SpriteSheet \
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

.PHONY: nomulti subdirs clean distclean install install-nomulti uninstall uninstall-nomulti $(SUBDIRS)

nomulti:
	$(MAKE) $(MFLAGS) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	(cd $@ && $(MAKE) $(MFLAGS))

clean:
	@for i in $(SUBDIRS) $(SUBDIRS_NOMULTI); do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

distclean: clean
	@for i in CImg; do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

install:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

install-nomulti:
	$(MAKE) SUBDIRS="$(SUBDIRS_NOMULTI)" install

uninstall:
	@for i in $(SUBDIRS) ; do \
	  echo "(cd $$i && $(MAKE) $(MFLAGS) $@)"; \
	  (cd $$i && $(MAKE) $(MFLAGS) $@); \
	done

uninstall-nomulti:
	$(MAKE) $(MFLAGS) SUBDIRS="$(SUBDIRS_NOMULTI)" uninstall
