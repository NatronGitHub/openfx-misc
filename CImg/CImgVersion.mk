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
CIMGVERSION=81f0dd58ac6a178e8d2d20153827af76fe65357a

# CImg.h is patched with the same scripts as NatronGitHub/openfx-gmic, which uses the same CImg
# version (the CImg.h of its GMICVER tarball), so that both get the same CImg code: the scripts in
# CImg/patches/ are verbatim copies of openfx-gmic's patches/cimg-*.sh at the openfx-gmic commit
# CIMG_PATCHES_FROM. openfx-gmic is the source of truth: fix or update the scripts there, then copy
# them again and update CIMG_PATCHES_FROM (.github/tools/check-cimg-patches.sh, run nightly, reports
# a drift). They are applied in this order, as in openfx-gmic's Makefile: CIMG_PATCHES, then
# CIMG_PATCHES_POCKETFFT with FFT=pocketfft (see license.mk). Changing CIMGVERSION,
# CIMG_PATCHES_FROM or FFT downloads and patches CImg.h again (CImg.h.version records them).
# - cimg-math-macro-overread.sh: the math parser read past the end of a macro body ending with an
#   argument, e.g. "f(a)=1+a" (GreycLab/CImg#496, fixed upstream after 4.0.7)
# - cimg-omp-rand.sh: the random numbers of rand(), noise() and the math parser in OpenMP loops
#   do not depend on thread scheduling (NatronGitHub/openfx-gmic#44, local fix)
# - cimg-ij-boundary.sh: i(), j(), I() and J() read out of bounds with linear or cubic
#   interpolation when an integer coordinate is outside the image (GreycLab/CImg#498, backport of
#   GreycLab/CImg 41e121c)
# - cimg-symmetric-eigen.sh: NaN eigenvectors of rank-deficient symmetric matrices
#   (GreycLab/gmic#221, backport of GreycLab/CImg e3de7b0)
# - cimg-pocketfft.sh: FFTs of any size with pocketfft, when cimg_use_pocketfft is defined
#   (GreycLab/CImg#497, merged upstream after 4.0.7)
# CMakeLists.txt reads these variables: keep each on one line, as a plain "NAME=value" assignment.
CIMG_PATCHES_FROM=780671f3f6ad4418802b840720f8f40e1d25451b
CIMG_PATCHES=patches/cimg-math-macro-overread.sh patches/cimg-omp-rand.sh patches/cimg-ij-boundary.sh patches/cimg-symmetric-eigen.sh
CIMG_PATCHES_POCKETFFT=patches/cimg-pocketfft.sh

# pocketfft (FFT=pocketfft), C++ header-only version, at a pinned commit of its 'cpp' branch: the
# same commit and checksum as openfx-gmic's Makefile
POCKETFFTCOMMIT=4d865cba24359b05b37da4e75aee24498c3b78e7
POCKETFFT_SHA256=518a95c552aeaee352aa30122137cd9e135d23c91e2863d9272aaa758744c86d
