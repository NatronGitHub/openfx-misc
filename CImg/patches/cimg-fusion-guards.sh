#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's fusions of '+', '-', '*' and '/' only rewrite an operand's last opcode when that operand
# is a temporary. Usage: cimg-fusion-guards.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/502, https://github.com/NatronGitHub/openfx-gmic/issues/232
#
# When it compiles 'a + b' (and '-', '*', '/'), CImg fuses the operation with the one that computed
# an operand, e.g. '(a*b) + c' into one opcode: it rewrites the operand's last opcode. It checks
# that the operand emitted code and that this opcode writes the operand, but not that the operand
# is a temporary: when the operand is an assignment ('y=x*2'), the variable takes the temporary's
# slot, and the fusion rewrites what the variable holds. 'z=1+(y=x*2);y' gives nan instead of
# 2*x, and in CImg 4.0.8 'y=0;(y=x*2)+1;y' gives 0 and 'z=(q=x/2)/3;q' gives nan.
# CImg 4.0.8 has the seven guards (left operand of '+', '-', '*' and '/', right operand of '+',
# '-' and '*') as 'if (is_scalar(arg1) && code.size()>p3) {' and
# 'if (!ptr1 && is_scalar(arg2) && code.size()>p3) {'. CImg develop (2f5e7e8 and later) tests
# is_comp_scalar(arg1) for the left operands of '+', '-' and '*', but still has
# 'if (code.size()>p3 && is_scalar(arg1)) {' for '/' (harmless there, thanks to a later check) and
# 'if (!ptr1 && code.size()>p3 && is_scalar(arg2)) {' for the right operands. The patch replaces
# is_scalar() with is_comp_scalar() in each of these guards that doesn't test it yet, and marks
# the lines it changed.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose seven
# guards all test is_comp_scalar() (an upstream fix). The script fails in any other case: not
# exactly seven guards in one of the known forms (e.g. after a GMICVER bump: then check what
# upstream changed, and update or remove this patch), or CRLF line endings. Only POSIX sh, awk and
# wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

marker=" // openfx-gmic patch: fusion only on a temporary (CImg#502)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-fusion-guards.sh (https://github.com/GreycLab/CImg/issues/502)" >&2
  rm -f "$f.patched"
  exit 1
}

# The guards, unfixed and fixed (same order), as trimmed lines; awk reads them from these
# variables.
unfixed="if (is_scalar(arg1) && code.size()>p3) {|if (!ptr1 && is_scalar(arg2) && code.size()>p3) {|if (!ptr1 && code.size()>p3 && is_scalar(arg2)) {|if (code.size()>p3 && is_scalar(arg1)) {"
fixed="if (is_comp_scalar(arg1) && code.size()>p3) {|if (!ptr1 && is_comp_scalar(arg2) && code.size()>p3) {|if (!ptr1 && code.size()>p3 && is_comp_scalar(arg2)) {|if (code.size()>p3 && is_comp_scalar(arg1)) {"

# counts <file>: "<unfixed guards> <fixed guards> <patched guards>"
counts() {
  awk -v unfixed="$unfixed" -v fixed="$fixed" -v marker="$marker" '
    BEGIN { n = split(unfixed, u, "|"); split(fixed, x, "|") }
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t)
      for (i = 1; i <= n; ++i) {
        if (t == u[i]) a++
        else if (t == x[i]) b++
        else if (t == x[i] marker) c++
      }
    }
    END { print a + 0, b + 0, c + 0 }' "$1"
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
[ $(($1 + $2 + $3)) = 7 ] ||
  fail "expected 7 fusion guards of '+', '-', '*' and '/' in a known form, found $1 unfixed, $2 fixed and $3 patched ones"
if [ "$1" = 0 ] && [ "$3" -ge 1 ]; then
  echo "$f: already patched (fusion guards, CImg#502)"
  exit 0
fi
if [ "$1" = 0 ]; then
  echo "$f: the 7 fusion guards already test is_comp_scalar(): not patched (fixed upstream, CImg#502)"
  exit 0
fi
unfixed_count=$1

awk -v unfixed="$unfixed" -v fixed="$fixed" -v marker="$marker" '
BEGIN { n = split(unfixed, u, "|"); split(fixed, x, "|") }
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = $0; sub(/[^ \t].*$/, "", ind)
  for (i = 1; i <= n; ++i) if (t == u[i]) { print ind x[i] marker; next }
  print
}' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 0 ] && [ "$3" = "$unfixed_count" ] && [ $(($2 + $3)) = 7 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq "`wc -l < "$f"`" ] || fail "the patch changed the number of lines"
mv "$f.patched" "$f"
echo "$f: patched so that the fusions of '+', '-', '*' and '/' only rewrite a temporary ($unfixed_count guards, CImg#502)"
