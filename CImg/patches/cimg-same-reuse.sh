#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's same(A,B) writes its result into a new slot, not into a live temporary.
# Usage: cimg-same-reuse.sh <CImg.h>
# https://github.com/NatronGitHub/openfx-gmic/issues/246
#
# same() compiles to '_cimg_mp_scalar6(vector_eq,arg1,p1,arg2,p2,arg3,arg4);', and scalar6() may
# put its result in the slot of any argument that is a computed scalar. But p1 and p2 are vector
# sizes, not slots: with a size of 36, the result goes into slot 36, which may hold a live
# temporary. 'a=1;sin(a)+same(vector36(a),vector36(a))' gives 2 instead of sin(1) + 1. CImg 4.0.8
# and develop have the bug; develop (87f4170) added scalar6()'s 'enable_reuse' argument for the
# same problem elsewhere, not here. The patch replaces the line with code that gets a new slot,
# as same()'s neighbours (e.g. s2v()) do, which works with and without 'enable_reuse':
# 'pos = scalar(); add_opcode(mp_vector_eq,pos,arg1,p1,arg2,p2,arg3,arg4); return_comp = true;
# _cimg_mp_return(pos);'.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose same()
# passes 'false' for 'enable_reuse' (an upstream fix: '_cimg_mp_return(scalar6(mp_vector_eq,
# arg1,p1,arg2,p2,arg3,arg4,false));'). The script fails in any other case: the line not found
# exactly once (e.g. after a GMICVER bump: then check what upstream changed, and update or remove
# this patch), or CRLF line endings. Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

old="_cimg_mp_scalar6(vector_eq,arg1,p1,arg2,p2,arg3,arg4);"
upstream="_cimg_mp_return(scalar6(mp_vector_eq,arg1,p1,arg2,p2,arg3,arg4,false));"
marker="pos = scalar(); // openfx-gmic patch: same() never reuses an argument's slot (#246)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-same-reuse.sh (https://github.com/NatronGitHub/openfx-gmic/issues/246)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<old lines> <upstream fixes> <markers>"
counts() {
  awk -v old="$old" -v upstream="$upstream" -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == old { o++ } t == upstream { u++ } t == marker { m++ }
    END { print o + 0, u + 0, m + 0 }' "$1"
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
if [ "$1" = 0 ] && [ "$2" = 0 ] && [ "$3" = 1 ]; then
  echo "$f: already patched (same(), #246)"
  exit 0
fi
if [ "$1" = 0 ] && [ "$2" = 1 ] && [ "$3" = 0 ]; then
  echo "$f: same() already passes enable_reuse=false to scalar6(): not patched (fixed upstream, #246)"
  exit 0
fi
[ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = 0 ] ||
  fail "expected one '$old' in same(), found $1 ($2 upstream fixes, $3 patch markers)"

awk -v old="$old" -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = $0; sub(/[^ \t].*$/, "", ind) }
t == old {
  print ind marker
  print ind "add_opcode(mp_vector_eq,pos,arg1,p1,arg2,p2,arg3,arg4);"
  print ind "return_comp = true;"
  print ind "_cimg_mp_return(pos);"
  next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 0 ] && [ "$2" = 0 ] && [ "$3" = 1 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 3)) ] || fail "the patch did not add 3 lines"
mv "$f.patched" "$f"
echo "$f: patched so that same() writes its result into a new slot (#246)"
