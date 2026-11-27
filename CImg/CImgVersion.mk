# For CImg.h versions,
# see https://github.com/dtschump/CImg/commits/master/CImg.h
# This file is included by CImg/Makefile and parsed by the toplevel
# CMakeLists.txt, so the version is declared in a single place.
#
# commit a841cc3080ee7bc00c5781e94b7f06c2920453ac is CImg 2.2.3+
# commit 4d54d452896fdf9347af68d3854f98f4986eea4c is CImg 2.3.1
# commit 548744b47c9316f656c4bdd1206e78646226c6be is CImg 2.3.3
# commit 0c333883a9d16d007e2db6a1792c97aeb2021b51 is CImg 2.6.1
# commit f3d1a1a0684f14ec2d7ab2bbc7b59863b2d9b9e2 is CImg 2.6.2
# commit bd64cb5415f127e62f390ed57e22bc752a77a549 is CImg 2.6.5
# commit 54779282070f1e2e38b5b9c2c652822f9f74412b is CImg 2.6.6
# commit 17c15abb5b49208731e2dd5cd45958dc13603779 is CImg 2.6.7
# commit 81a66d06d29763641ae533451543fa60d07f3a66 is CImg 2.7.0
# commit a3b57fe7f0cfbf84635e3070fc3c896b25ff6c5f is CImg 2.7.2
# commit 3efdd8fb35e76873edcb01f9c30af762aeccb97a is CImg 2.7.4
# commit 39215451d0b839b4e72d32afb15ea69701a7f4eb is CImg 2.8.0
# commit 530715cfe254d325cf8cec345210d9935fe4cf50 is CImg 2.8.1
# commit b29d5cb0ed4ca3d94ceae31232668f646c9e45a9 is CImg 2.8.2
# commit 12f1fac5dc1221eca3c0ecdca7bd77da7392ddbb is CImg 2.8.3
# commit 9e01d8868613bf31e305044d8e2e9d9f0217f030 is CImg 2.8.4
# commit 89b9d062ec472df3d33989e6d5d2a8b50ba0775c is CImg 2.9.0
# commit c0becdf881b0f3e2445975cac01c2422170d1fd9 is CImg 2.9.7
# commit b33dcc8f9f1acf1f276ded92c04f8231f6c23fcd is CImg 2.9.9
# commit 81f0dd58ac6a178e8d2d20153827af76fe65357a is CImg 4.0.7 (tag v.4.0.7)
# commit 1a9fef439d17b73d9af8d8e59b65df9c989fde7a is CImg 4.0.8 (tag v.4.0.8; its CImg.h is the
#   one of gmic_4.0.8.tar.gz, git blob 5230131435f958221f15d703e0dbfc9871e07965)
CIMGVERSION=1a9fef439d17b73d9af8d8e59b65df9c989fde7a
# sha256 of the CImg.h of CIMGVERSION, as downloaded (before the patches): CImg/Makefile and
# CMakeLists.txt check it, and fail before patching on a mismatch. Update it with CIMGVERSION.
CIMG_SHA256=adf13c3e991f6812c2db484ff07aa614421830117924590d3def10e8ac6a485b

# CImg.h is patched with the same scripts as NatronGitHub/openfx-gmic, which uses the same CImg
# version (the CImg.h of its GMICVER tarball), so that both get the same CImg code: the scripts in
# CImg/patches/ are verbatim copies of openfx-gmic's patches/cimg-*.sh at the openfx-gmic commit
# CIMG_PATCHES_FROM. openfx-gmic is the source of truth: fix or update the scripts there, then copy
# them again and update CIMG_PATCHES_FROM (.github/tools/check-cimg-patches.sh, run weekly, reports
# a drift). They are applied in the order of CIMG_PATCHES, as in openfx-gmic's Makefile, whatever
# the FFT: CImg.h is the same for every FFT. Changing CIMGVERSION, CIMG_PATCHES_FROM or the patches
# downloads and patches CImg.h again (CImg.h.version records them).
# - cimg-omp-rand.sh: the random numbers of rand(), noise() and the math parser in OpenMP loops
#   do not depend on thread scheduling (NatronGitHub/openfx-gmic#44, local fix)
# - cimg-vvar2.sh: vvar() with two arguments called a NULL function pointer (crash)
#   (GreycLab/CImg#501)
# - cimg-vargkth2.sh: vargkth() with two arguments gave 1; it gives 0 again, as in CImg 4.0.7
#   (GreycLab/CImg#501)
# - cimg-log1p-peephole.sh: the log(1 + x) -> log1p(x) optimization rewrote the code that computed
#   a variable, e.g. 'y=1+x;log(y);y' gave log(1 + x) (GreycLab/CImg#502)
# - cimg-abs-peephole.sh: the same for abs(maxabs()) -> absmaxabs() (and minabs), e.g.
#   'abs(m=maxabs(a,b,c));m' gave abs(maxabs(a,b,c)) for m (GreycLab/CImg#502, also in CImg 4.0.7)
# - cimg-fusion-guards.sh: the fusions of '+', '-', '*' and '/' with the code of an operand
#   rewrote the code of a variable assigned in that operand, e.g. 'z=1+(y=x*2);y' gave NaN
#   instead of 2*x (GreycLab/CImg#502, NatronGitHub/openfx-gmic#232)
# - cimg-vector-assign.sh: the vector assignments 'V = expr' and '(V) = expr' let the opcode that
#   computed the value write into V while it still read V, or take the slot of another variable, e.g. 'A=[1,2,3,4];B=[0,1,1,0];(A)=mul(A,B,2);A' gave
#   2,2,4,4 instead of 2,1,4,3 (GreycLab/CImg#505, NatronGitHub/openfx-gmic#245)
# - cimg-same-reuse.sh: same(A,B) wrote its result into a live temporary when A or B has as many
#   values as that temporary's slot number, e.g. 'a=1;sin(a)+same(vector36(a),vector36(a))' gave
#   2 instead of sin(1) + 1 (NatronGitHub/openfx-gmic#246)
# - cimg-hypot-layout.sh: hypot() with one or two values on CImg develop's new layout of the norm
#   opcodes (overflow and underflow); does nothing on CImg 4.0.8 (NatronGitHub/openfx-gmic#244)
# - cimg-fftw-no-cleanup-at-unload.sh: ~FFTW3_static() no longer calls fftw_cleanup_threads(): at
#   dlclose, FFTW's detached workers were still exiting through the unmapped code (crash after the
#   render with FFT=fftw3; GreycLab/CImg#500). Changes nothing in CImg.h's other code.
# - cimg-msvc-snprintf.sh: with MSVC, cimg_snprintf() called the CRT's _vsnprintf, which leaves
#   no '\0' when it truncates (buffer overread, crash; GreycLab/CImg#507). MSVC builds only.
# CImg 4.0.8 has the fixes of the other scripts that 4.0.7 needed (removed in openfx-gmic#98): the
# math parser's macro overread (GreycLab/CImg#496), i()/j()/I()/J() out of bounds
# (GreycLab/CImg#498), the NaN eigenvectors of symmetric_eigen() (GreycLab/gmic#221), FFTW's
# fftw_cleanup_threads() after each FFT (4.0.8 moved it to unload, which the script above removes),
# and pocketfft itself (cimg_use_pocketfft, GreycLab/CImg#497).
# CMakeLists.txt reads these variables: keep each on one line, as a plain "NAME=value" assignment.
CIMG_PATCHES_FROM=d3c0c802628c47378c72c519088746a4421457d4
CIMG_PATCHES=patches/cimg-omp-rand.sh patches/cimg-vvar2.sh patches/cimg-vargkth2.sh patches/cimg-log1p-peephole.sh patches/cimg-abs-peephole.sh patches/cimg-fusion-guards.sh patches/cimg-vector-assign.sh patches/cimg-same-reuse.sh patches/cimg-hypot-layout.sh patches/cimg-fftw-no-cleanup-at-unload.sh patches/cimg-msvc-snprintf.sh

# pocketfft (FFT=pocketfft), C++ header-only version, at a pinned commit of its 'cpp' branch: the
# same commit and checksum as openfx-gmic's Makefile
POCKETFFTCOMMIT=4d865cba24359b05b37da4e75aee24498c3b78e7
POCKETFFT_SHA256=518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d
