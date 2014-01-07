SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Reconverge
ifneq ($(DEBUGFLAG),-O3)
  SUBDIRS += DebugProxy
endif

default : 
	@ echo making sub projects... $(SUBDIRS)
	for i in $(SUBDIRS) ; do cd $$i; make ; cd ..; done

clean :
	for i in $(SUBDIRS) ; do cd $$i; make clean; cd ..; done
