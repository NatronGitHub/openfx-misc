SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Switch
ifneq ($(DEBUGFLAG),-O3)
  # DebugProxy is only useful to debug the communication between a host and a plugin
  SUBDIRS += DebugProxy
  # ReConverge is not tested yet
  SUBDIRS += ReConverge
endif

# There's only one CImg-based plugin for now. Build it separately.
idef HAVE_CIMG
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
