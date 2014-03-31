SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Switch ColorCorrect Grade Transform
ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # ReConverge is not tested yet
  SUBDIRS += ReConverge
endif

HAVE_CIMG ?= 0

# Build CImg-based plugins separately.
ifneq ($(HAVE_CIMG),0)
# add plugins which may use CImg here
#  SUBDIRS += GMIC
endif

all: subdirs

multibundle:
	$(MAKE) SUBDIRS=Misc

.PHONY: subdirs clean $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean :
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i clean; \
	done
