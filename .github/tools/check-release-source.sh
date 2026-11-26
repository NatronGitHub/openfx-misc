#!/bin/bash
# Check that a source tree (the built tag or branch) can be built and packaged by release.yml.
# release.yml takes its release tools (this script, fetch-osmesa.sh, build-osmesa.sh,
# bundle-linux-libs.sh, package-release.sh, .github/parameters/) from the workflow's own commit,
# and builds the source tree with its own CMakeLists.txt and test scripts (loadtest.sh,
# smoketest.sh, whose lists belong to its plug-ins): the source must have the CMake options and
# the test scripts that release.yml uses, else the run fails here, before any build.
#
# Usage (from the top of the source tree): <tools>/check-release-source.sh
set -eo pipefail

status=0
for o in BUILD_MISC OFXMISC_STANDALONE_BUNDLES OSMESA_PKGCONFIG_STATIC OSMESA_LICENSES_DIR OFX_BUILD_HOSTDEMO; do
    if ! grep -q "$o" CMakeLists.txt; then
        echo "ERROR: $PWD/CMakeLists.txt has no $o option"
        status=1
    fi
done
for f in .github/tools/lib.sh .github/tools/loadtest.sh .github/tools/smoketest.sh \
         .github/tools/smoketest_lists.py .github/tools/ofxload.cpp; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $PWD/$f not found"
        status=1
    fi
done
# release.yml runs smoketest.sh with SMOKETEST_NO_TEST=1, and package-release.sh copies the
# pocketfft licence that CMake puts in CImg.ofx.bundle
if [ -f .github/tools/smoketest.sh ] && ! grep -q SMOKETEST_NO_TEST .github/tools/smoketest.sh; then
    echo "ERROR: $PWD/.github/tools/smoketest.sh has no SMOKETEST_NO_TEST"
    status=1
fi
if ! grep -qs Licence_pocketfft.txt CMakeLists.txt cmake/*.cmake; then
    echo "ERROR: the CMake files of $PWD do not put Licence_pocketfft.txt in CImg.ofx.bundle"
    status=1
fi
if [ $status != 0 ]; then
    echo "ERROR: this source predates the release tooling of this workflow (openfx-misc#173): build a"
    echo "newer tag, or run the release.yml of a ref that matches it"
fi
exit $status
