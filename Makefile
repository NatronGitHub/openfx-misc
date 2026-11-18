SUBDIRS = Misc Shadertoy

SUBDIRS_NOMULTI = \
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
DiscBlades \
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
Threshold \
TimeBlur \
TimeDissolve \
TimeOffset \
TrackerPM \
Transform \
VectorToColor \
WhiteBalance

TESTS ?= 0
ifeq ($(CONFIG),debug)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # ReConverge and TimeBuffer are only compiled in debug builds (#ifdef DEBUG):
  # in other configurations, their bundle would contain no plugin.
  SUBDIRS_NOMULTI += ReConverge TimeBuffer
  TESTS = 1
endif
ifeq ($(TESTS),1)
  SUBDIRS += Test
endif

HAVE_CIMG ?= 1

# Build CImg-based plugins separately.
ifneq ($(HAVE_CIMG),0)
# add plugins which may use CImg here
  SUBDIRS += CImg
  SUBDIRS_NOMULTI += \
CImg/Bilateral \
CImg/Blur \
CImg/Convolve \
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
CImg/ZDefocus \

endif

all: subdirs

.PHONY: nomulti subdirs clean distclean install install-nomulti uninstall uninstall-nomulti $(SUBDIRS)

nomulti:
	$(MAKE) $(MFLAGS) SUBDIRS="$(SUBDIRS_NOMULTI)"

subdirs: $(SUBDIRS)

$(SUBDIRS):
	(cd $@ && $(MAKE) $(MFLAGS))

# Download CImg/CImg.h (and CImg/Inpaint/inpaint.h) once, before the CImg
# subplugins are built in parallel by "make nomulti".
$(filter CImg/%,$(SUBDIRS)): CImg/CImg.h

CImg/CImg.h:
	(cd CImg && $(MAKE) $(MFLAGS) CImg.h)

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
