#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's vvar() with exactly two arguments no longer crashes. Usage: cimg-vvar2.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/501
#
# CImg 4.0.8 compiles the multi-argument vector functions (vavg, vmax, vstd, vvar...) with two
# arguments to an optimized variant (mp_vavg2, mp_vmax2...), chosen by a ternary chain after the
# comment "Switch to optimized function is 2 arguments only". mp_vvar is missing from the chain,
# which then ends with (mp_func)0: vvar(a,b) calls a NULL function pointer (EXC_BAD_ACCESS /
# SIGSEGV), e.g. 'gmic e "{vvar(1,2)}"'. mp_vvar2, ((a - b)/2)^2, is defined but never used; it
# gives what mp_vvar gives for two values (vec.variance(), whose default method 0 divides by N).
# The patch adds 'op==mp_vvar?mp_vvar2:' at the end of the chain.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose chain
# already has mp_vvar (an upstream fix). The script fails in any other case: the chain or mp_vvar2
# missing (e.g. after a GMICVER bump: then check what upstream changed, and update or remove this
# patch), or CRLF line endings. Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

last="op==mp_vsum?mp_vsum2:(mp_func)0;"
marker="op==mp_vvar?mp_vvar2:(mp_func)0; // openfx-gmic patch: vvar() with 2 arguments"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-vvar2.sh" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<markers> <chains> <chains ending with mp_vsum2:(mp_func)0> <mp_vvar in a chain>
# <mp_vvar2 definitions>"
counts() {
  awk -v last="$last" -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == "static double mp_vvar2(_cimg_math_parser& mp) {" { def++ }
    index(t, "// Switch to optimized function is 2 arguments only") { chains++; inchain = 1; next }
    inchain && t == marker { m++ }
    inchain && t == last { ends++ }
    inchain && index(t, "op==mp_vvar?mp_vvar2") { vvar++ }
    inchain && index(t, "(mp_func)0;") { inchain = 0 }
    END { print m + 0, chains + 0, ends + 0, vvar + 0, def + 0 }' "$1"
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
if [ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$4" = 1 ] && [ "$5" = 1 ]; then
  echo "$f: already patched (vvar() with 2 arguments)"
  exit 0
fi
if [ "$1" = 0 ] && [ "$2" = 1 ] && [ "$4" = 1 ] && [ "$5" = 1 ]; then
  echo "$f: the math parser's 2-argument chain already maps mp_vvar to mp_vvar2: not patched (fixed upstream)"
  exit 0
fi
[ "$1" = 0 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 1 ] ||
  fail "expected one 2-argument chain ending with '$last', without mp_vvar, and one mp_vvar2 definition; found $2 chains, $3 such ends, $4 mp_vvar entries, $5 mp_vvar2 definitions ($1 patch markers)"

awk -v last="$last" -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
index(t, "// Switch to optimized function is 2 arguments only") { inchain = 1 }
inchain && t == last {
  line = $0; ind = line; sub(/[^ \t].*$/, "", ind)
  sub(/\(mp_func\)0;$/, "", line)
  print line
  print ind marker
  inchain = 0; next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$4" = 1 ] && [ "$5" = 1 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 1)) ] || fail "the patch did not add 1 line"
mv "$f.patched" "$f"
echo "$f: patched so that vvar() with 2 arguments does not crash"
