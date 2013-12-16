SUBDIRS = RGBLut JoinViews OneView Anaglyph MixViews SideBySide Reconverge DebugProxy

default : 
	@ echo making sub projects... $(SUBDIRS)
	for i in $(SUBDIRS) ; do cd $$i; make ; cd ..; done

clean :
	for i in $(SUBDIRS) ; do cd $$i; make clean; cd ..; done
