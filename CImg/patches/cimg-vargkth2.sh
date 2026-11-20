#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's vargkth() with exactly two arguments gives 0, the index of its single value, as with
# more arguments.
# Usage: cimg-vargkth2.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/501
#
# CImg 4.0.8 compiles vargkth(k,a) (two arguments) to the new 2-argument variant mp_vargkth2,
# which returns 1. mp_vargkth, used with more arguments (and by CImg 4.0.7 with two), returns the
# 0-based index of the k-th smallest value among the arguments after k: vargkth(1,5,3) is 1,
# vargkth(1,3,5) is 0, and with a single value, vargkth(1,5), the index is 0 (as in 4.0.7), not 1.
# The patch makes mp_vargkth2 return 0. It doesn't reproduce mp_vargkth's result for a NaN value
# (1, because no value then equals the k-th smallest): vargkth(k,NaN) gives 0 with 2 arguments,
# while the n-argument form gives 1, for NaN input only. Code that depends on detecting NaN is
# avoided (openfx-misc, which copies this script, forbids it).
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose
# mp_vargkth2 already returns 0, 'e1==e1?0:1' or 'cimg::type<double>::is_nan(e1)?1:0' (an
# upstream fix). The script fails in any other case: mp_vargkth2
# missing or with another body (e.g. after a GMICVER bump: then check what upstream changed, and
# update or remove this patch), or CRLF line endings. Only POSIX sh, awk and wc are used, for
# Linux, macOS and MSYS2.
set -e
f="$1"

head="static double mp_vargkth2(_cimg_math_parser& mp) {"
old="_cimg_mp_vfunc2(1);"
fixed0="_cimg_mp_vfunc2(0);"
fixed1="_cimg_mp_vfunc2(e1==e1?0:1);"
fixed2="_cimg_mp_vfunc2(cimg::type<double>::is_nan(e1)?1:0);"
marker="_cimg_mp_vfunc2(0); // openfx-gmic patch: 0-based index, as mp_vargkth"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-vargkth2.sh (https://github.com/GreycLab/CImg/issues/501)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<mp_vargkth2 definitions> <bodies returning 1> <fixed bodies> <markers>"
counts() {
  awk -v head="$head" -v old="$old" -v fixed0="$fixed0" -v fixed1="$fixed1" -v fixed2="$fixed2" -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    infn { if (t == old) o++; else if (t == fixed0 || t == fixed1 || t == fixed2) z++; else if (t == marker) m++; infn = 0 }
    t == head { h++; infn = 1 }
    END { print h + 0, o + 0, z + 0, m + 0 }' "$1"
}

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (extract CImg.h with LF line endings)"
fi

set -- `counts "$f"`
if [ "$1" = 1 ] && [ "$4" = 1 ]; then
  echo "$f: already patched (vargkth() with 2 arguments, CImg#501)"
  exit 0
fi
if [ "$1" = 1 ] && [ "$3" = 1 ]; then
  echo "$f: mp_vargkth2 already returns a 0-based index: not patched (fixed upstream, CImg#501)"
  exit 0
fi
[ "$1" = 1 ] && [ "$2" = 1 ] ||
  fail "expected one mp_vargkth2 whose body is '$old'; found $1 definitions, $2 such bodies"

awk -v head="$head" -v old="$old" -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
infn && t == old { line = $0; ind = line; sub(/[^ \t].*$/, "", ind); print ind marker; infn = 0; next }
{ infn = 0; print }
t == head { infn = 1 }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 0 ] && [ "$4" = 1 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq `wc -l < "$f"` ] || fail "the patch changed the number of lines"
mv "$f.patched" "$f"
echo "$f: patched so that vargkth() with 2 arguments gives a 0-based index (CImg#501)"
