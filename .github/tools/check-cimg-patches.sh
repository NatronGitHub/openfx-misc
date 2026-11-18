#!/bin/bash
# Check that the CImg.h patches of openfx-misc are still those of openfx-gmic (run by nightly.yml):
# - CImg/patches/*.sh are byte-identical to openfx-gmic's patches/cimg-*.sh at REF (default:
#   master), and both sets have the same scripts;
# - CIMGVERSION (CImg/CImgVersion.mk) is the commit of the CImg tag v.<GMICVER>, GMICVER being
#   the G'MIC version of openfx-gmic's Makefile at REF.
# openfx-gmic is the source of truth: on a drift, copy its scripts into CImg/patches, update
# CIMG_PATCHES_FROM (and the patch lists) in CImg/CImgVersion.mk, or CIMGVERSION.
# Usage (from the top of the source tree): .github/tools/check-cimg-patches.sh [REF]
# Needs curl, and git (for git ls-remote). GH_TOKEN, if set, is used for the GitHub API.
set -eo pipefail

ref=${1:-master}
repo=NatronGitHub/openfx-gmic
raw=https://raw.githubusercontent.com/$repo/$ref
work=$(mktemp -d "${TMPDIR:-/tmp}/check-cimg-patches.XXXXXX")
trap 'rm -rf "$work"' EXIT
curl_() { curl -fsSL --retry 3 "$@"; }
status=0

# the commit of REF, for the messages
api=(-H "Accept: application/vnd.github+json")
[ -z "$GH_TOKEN" ] || api+=(-H "Authorization: Bearer $GH_TOKEN")
sha=$(curl_ "${api[@]}" "https://api.github.com/repos/$repo/commits/$ref" |
          sed -n 's/^  "sha": "\([0-9a-f]*\)".*/\1/p' | head -n 1)
from=$(sed -n 's/^CIMG_PATCHES_FROM *= *//p' CImg/CImgVersion.mk)
echo "openfx-gmic $ref is ${sha:-unknown}; CImg/patches come from openfx-gmic $from"

# the scripts of openfx-gmic at REF (patches/cimg-*.sh)
names=$(curl_ "${api[@]}" "https://api.github.com/repos/$repo/contents/patches?ref=$ref" |
            sed -n 's/^ *"name": "\(cimg-[^"]*\.sh\)".*/\1/p' | sort)
if [ -z "$names" ]; then
    echo "ERROR: could not list patches/cimg-*.sh in $repo at $ref"
    exit 1
fi
ours=$(cd CImg/patches && ls cimg-*.sh | sort)
if [ "$names" != "$ours" ]; then
    echo "DRIFT: the CImg patch scripts differ:"
    diff <(echo "$ours") <(echo "$names") | sed 's/^</  only in openfx-misc CImg\/patches:/; s/^>/  only in openfx-gmic patches:/' | grep only || true
    status=1
fi
for n in $names; do
    [ -f "CImg/patches/$n" ] || continue
    curl_ -o "$work/$n" "$raw/patches/$n"
    if cmp -s "$work/$n" "CImg/patches/$n"; then
        echo "ok: CImg/patches/$n"
    else
        echo "DRIFT: CImg/patches/$n differs from openfx-gmic's patches/$n at $ref:"
        diff -u "CImg/patches/$n" "$work/$n" | head -n 40 || true
        status=1
    fi
done
# every script listed in CImgVersion.mk exists
for p in $(sed -n -e 's/^CIMG_PATCHES *= *//p' -e 's/^CIMG_PATCHES_POCKETFFT *= *//p' CImg/CImgVersion.mk); do
    [ -f "CImg/$p" ] || { echo "ERROR: CImg/CImgVersion.mk lists $p, which does not exist"; status=1; }
done

# CIMGVERSION: the CImg tag v.<GMICVER>
gmicver=$(curl_ "$raw/Makefile" | sed -n 's/^GMICVER *= *\([0-9.]*\).*/\1/p' | head -n 1)
cimgversion=$(sed -n 's/^CIMGVERSION *= *//p' CImg/CImgVersion.mk)
# the commit of the tag (the peeled ^{} entry for an annotated tag)
tagsha=$(git ls-remote https://github.com/GreycLab/CImg.git "refs/tags/v.$gmicver" "refs/tags/v.$gmicver^{}" |
             sort -k2 | tail -n 1 | cut -f1)
if [ -z "$gmicver" ]; then
    echo "ERROR: could not read GMICVER from openfx-gmic's Makefile at $ref"
    status=1
elif [ -z "$tagsha" ]; then
    echo "DRIFT: openfx-gmic uses G'MIC $gmicver, but CImg has no tag v.$gmicver (yet?): CIMGVERSION is $cimgversion"
    status=1
elif [ "$tagsha" != "$cimgversion" ]; then
    echo "DRIFT: openfx-gmic uses G'MIC $gmicver (CImg tag v.$gmicver = $tagsha), but CIMGVERSION is $cimgversion"
    status=1
else
    echo "ok: CIMGVERSION $cimgversion is CImg v.$gmicver, openfx-gmic's GMICVER"
fi

if [ $status != 0 ]; then
    echo
    echo "CImg.h is no longer patched as openfx-gmic's ($repo $ref): update CImg/patches and"
    echo "CImg/CImgVersion.mk (CIMG_PATCHES_FROM, CIMG_PATCHES, CIMGVERSION), see CImg/CImgVersion.mk."
fi
exit $status
