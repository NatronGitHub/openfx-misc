SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Switch
ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # ReConverge is not tested yet
  SUBDIRS += ReConverge
endif

HAVE_CIMG ?= 0

# There's only one CImg-based plugin for now. Build it separately.
ifneq ($(HAVE_CIMG),0)
  SUBDIRS += GREYCstoration
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
