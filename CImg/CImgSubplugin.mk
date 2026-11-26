# Rules shared by the standalone CImg subplugin Makefiles (CImg/*/Makefile),
# which are used by "make nomulti". Include this file after Makefile.master.

# CImg.h, CImgFilter.cpp and CImgOperator.cpp are in the parent directory.
VPATH += $(TOP_SRCDIR)/CImg
CXXFLAGS += -I$(TOP_SRCDIR)/CImg

# openmp, as ../Makefile
ifeq ($(OPENMP),1)
  CXXFLAGS += -Dcimg_use_openmp -fopenmp
  LINKFLAGS += -fopenmp
endif

# The shared CImg sources include CImg.h, which must be downloaded first.
$(OBJECTPATH)/CImgFilter.o $(OBJECTPATH)/CImgOperator.o: CImg.h

# CImg.h is downloaded by ../Makefile, at the version given in CImgVersion.mk.
CImg.h:
	cd .. && $(MAKE) $@
