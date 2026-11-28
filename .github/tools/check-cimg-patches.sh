#!/bin/bash
# Check that the CImg.h patches of openfx-misc are still those of openfx-gmic (run weekly by nightly.yml):
# - CImg/patches/*.sh are byte-identical to openfx-gmic's patches/cimg-*.sh at REF (default:
#   master), and both sets have the same scripts;
# - CIMG_PATCHES (the patch order), POCKETFFTCOMMIT and POCKETFFT_SHA256 (the pocketfft pin) in
#   CImg/CImgVersion.mk are those of openfx-gmic's gmic_sources.txt at REF (whitespace around
#   "=" and between words is normalised);
# - CIMGVERSION (CImg/CImgVersion.mk) is the commit of the CImg tag v.<GMICVER>, GMICVER being
#   the G'MIC version of openfx-gmic's gmic_sources.txt at REF.
# openfx-gmic's Makefile held these variables before openfx-gmic #263 (which moved them to
# gmic_sources.txt, in the same "NAME = value" format): it is read instead when REF has no
# gmic_sources.txt.
# Every file is read from the same openfx-gmic commit: REF is resolved to a commit sha first.
# openfx-gmic is the source of truth: on a drift, copy its scripts into CImg/patches, update
# CIMG_PATCHES_FROM (and the patch lists) in CImg/CImgVersion.mk, or CIMGVERSION.
# Usage (from the top of the source tree): .github/tools/check-cimg-patches.sh [REF]
# Needs curl, and git (for git ls-remote). GH_TOKEN, if set, is used for the GitHub API.
set -eo pipefail

ref=${1:-master}
repo=NatronGitHub/openfx-gmic
work=$(mktemp -d "${TMPDIR:-/tmp}/check-cimg-patches.XXXXXX")
trap 'rm -rf "$work"' EXIT
curl_() { curl -fsSL --retry 3 "$@"; }
status=0

# the commit of REF: every file below is read from this commit, not from REF, which may move
api=(-H "Accept: application/vnd.github+json")
[ -z "$GH_TOKEN" ] || api+=(-H "Authorization: Bearer $GH_TOKEN")
sha=$(curl_ "${api[@]}" "https://api.github.com/repos/$repo/commits/$ref" |
          sed -n 's/^  "sha": "\([0-9a-f]*\)".*/\1/p' | head -n 1)
if [ -z "$sha" ]; then
    echo "ERROR: could not resolve $ref in $repo"
    exit 1
fi
raw=https://raw.githubusercontent.com/$repo/$sha
from=$(sed -n 's/^CIMG_PATCHES_FROM *= *//p' CImg/CImgVersion.mk)
echo "openfx-gmic $ref is $sha; CImg/patches come from openfx-gmic $from"
# the file that sets the variables: gmic_sources.txt, or the Makefile before openfx-gmic #263
src=gmic_sources.txt
if ! curl_ -o "$work/$src" "$raw/$src" 2>/dev/null; then
    src=Makefile
    curl_ -o "$work/$src" "$raw/$src"
fi
srcfile=$work/$src
echo "reading the variables from openfx-gmic's $src"

# the scripts of openfx-gmic at REF (patches/cimg-*.sh)
names=$(curl_ "${api[@]}" "https://api.github.com/repos/$repo/contents/patches?ref=$sha" |
            sed -n 's/^ *"name": "\(cimg-[^"]*\.sh\)".*/\1/p' | sort)
if [ -z "$names" ]; then
    echo "ERROR: could not list patches/cimg-*.sh in $repo at $sha"
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
        echo "DRIFT: CImg/patches/$n differs from openfx-gmic's patches/$n at $sha:"
        diff -u "CImg/patches/$n" "$work/$n" | head -n 40 || true
        status=1
    fi
done
# every script listed in CImgVersion.mk exists
for p in $(sed -n -e 's/^CIMG_PATCHES *= *//p' CImg/CImgVersion.mk); do
    [ -f "CImg/$p" ] || { echo "ERROR: CImg/CImgVersion.mk lists $p, which does not exist"; status=1; }
done

# the patch order and the pocketfft pin: the plain "NAME = value" line of each variable,
# whitespace normalised
var() { sed -n "s/^$1[[:space:]]*=[[:space:]]*//p" "$2" | head -n 1 | tr -s '[:space:]' ' ' | sed 's/ $//'; }
for v in CIMG_PATCHES POCKETFFTCOMMIT POCKETFFT_SHA256; do
    theirs=$(var $v "$srcfile")
    mine=$(var $v CImg/CImgVersion.mk)
    if [ -z "$theirs" ]; then
        echo "ERROR: could not read $v from openfx-gmic's $src at $sha"
        status=1
    elif [ "$mine" != "$theirs" ]; then
        echo "DRIFT: $v differs from openfx-gmic's $src at $sha:"
        echo "  openfx-misc CImg/CImgVersion.mk: $v=$mine"
        echo "  openfx-gmic $src: $v=$theirs"
        status=1
    else
        echo "ok: $v"
    fi
done

# openfx-gmic's CIMG_PATCHES must be its only CIMG_PATCHES* variable, assigned once, with a plain
# "=": an append ("CIMG_PATCHES += ...", e.g. a conditional patch inside an ifeq), another
# assignment, or another variable (e.g. the former CIMG_PATCHES_POCKETFFT) would be a patch that
# CImg/CImgVersion.mk does not apply
assigns=$(tr -d '\r' < "$srcfile" |
              grep -E '^[[:space:]]*(override[[:space:]]+|export[[:space:]]+)?CIMG_PATCHES[A-Za-z0-9_]*[[:space:]]*([:+?!]|::)?=' || true)
unhandled=$(echo "$assigns" | grep -Ev '^CIMG_PATCHES[[:space:]]*=' | grep . || true)
nplain=$(echo "$assigns" | grep -Ec '^CIMG_PATCHES[[:space:]]*=' || true)
if [ -n "$unhandled" ] || [ "$nplain" != 1 ]; then
    echo "DRIFT: openfx-gmic's $src at $sha sets CImg patches that CImg/CImgVersion.mk does not handle"
    echo "  (only one plain 'CIMG_PATCHES = ...' line is supported):"
    echo "$assigns" | sed 's/^/  /'
    status=1
else
    echo "ok: CIMG_PATCHES is openfx-gmic's only CImg patch list"
fi

# CIMGVERSION: the CImg tag v.<GMICVER>
gmicver=$(sed -n 's/^GMICVER *= *\([0-9.]*\).*/\1/p' "$srcfile" | head -n 1)
cimgversion=$(sed -n 's/^CIMGVERSION *= *//p' CImg/CImgVersion.mk)
# the commit of the tag (the peeled ^{} entry for an annotated tag)
tagsha=$(git ls-remote https://github.com/GreycLab/CImg.git "refs/tags/v.$gmicver" "refs/tags/v.$gmicver^{}" |
             sort -k2 | tail -n 1 | cut -f1)
if [ -z "$gmicver" ]; then
    echo "ERROR: could not read GMICVER from openfx-gmic's $src at $sha"
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
    echo "CImg.h is no longer patched as openfx-gmic's ($repo $ref = $sha): update CImg/patches"
    echo "and CImg/CImgVersion.mk (CIMG_PATCHES_FROM, CIMG_PATCHES, POCKETFFTCOMMIT, POCKETFFT_SHA256,"
    echo "CIMGVERSION), see CImg/CImgVersion.mk."
fi
exit $status
