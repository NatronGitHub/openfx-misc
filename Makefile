SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Reconverge
ifneq ($(DEBUGFLAG),-O3)
  SUBDIRS += DebugProxy
endif

all: subdirs

.PHONY: subdirs clean $(SUBDIRS)

subdirs: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

clean :
	for i in $(SUBDIRS) ; do \
	  $(MAKE) -C $$i clean; \
	done
