#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's vector assignments 'V = expr' and '(V) = expr' don't rewrite an opcode they shouldn't.
# Usage: cimg-vector-assign.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/505, https://github.com/NatronGitHub/openfx-gmic/issues/245
#
# Both assignments have two peepholes on the opcode that computed the value:
# - 'X = func(X)' (sort(), mirror()...) becomes an in-place call. Without 'is_comp_vector(value)',
#   it also fires when the value is another variable, whose last opcode was 'B = func(X)': in
#   'A=[3,1,2];B=sort(A);(A)=B;size(B)', B loses its slot (size 0, expected 3).
# - 'X = f(...)' makes f() write straight into X. Without checking that f() doesn't also read X,
#   'A=[1,2,3,4];B=[0,1,1,0];(A)=mul(A,B,2);A' gives 2,2,4,4 (expected 2,1,4,3).
# CImg 4.0.8 has neither check in either form. CImg develop has both in 'V = expr' (21d0846, CImg#505;
# 0685d59), as 'is_comp_vector(arg3) && pop[1]==arg3 && pop[2]==arg1) {' and
# 'if (pop[k]==arg3 || pop[k]==arg1) { is_sth = false; break; }', but not in '(V) = expr' (arg2).
# The patch adds each missing check: 'is_comp_vector(arg<n>) && ' at the start of the in-place
# guard 'pop[1]==arg<n> && pop[2]==arg1) {', and '|| pop[k]==arg1' in the first
# 'for (unsigned int k = 2; k<pop.size(); ++k) if (pop[k]==arg<n>) { is_sth = false; break; }'
# after it (the vector branch; the scalar branches don't need it), for n = 3 ('V = expr') and
# n = 2 ('(V) = expr').
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h that has
# all four checks (an upstream fix). The script fails in any other case: the two in-place guards
# not found in either form, or no aliasing loop after one (e.g. after a GMICVER bump: then check
# what upstream changed, and update or remove this patch), or CRLF line endings. Only POSIX sh,
# awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

marker=" // openfx-gmic patch: vector assignment peepholes (CImg#505)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-vector-assign.sh (https://github.com/NatronGitHub/openfx-gmic/issues/245)" >&2
  rm -f "$f.patched"
  exit 1
}

# scan <file> <mode>: with mode "count", prints "<n=3 guard> <n=3 loop> <n=2 guard> <n=2 loop>",
# each 0 (missing), 1 (unfixed), 2 (fixed) or 3 (patched); with mode "patch", prints the patched
# file. The loop of n is the first aliasing loop of n after its guard.
scan() {
  awk -v marker="$marker" -v mode="$2" '
    function state(t, unfixed, fixed) {
      if (t == unfixed) return 1
      if (t == fixed) return 2
      if (t == fixed marker) return 3
      return 0
    }
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = $0; sub(/[^ \t].*$/, "", ind); out = $0
      for (n = 3; n >= 2; --n) {
        a = "arg" n
        g = state(t, "pop[1]==" a " && pop[2]==arg1) {", "is_comp_vector(" a ") && pop[1]==" a " && pop[2]==arg1) {")
        if (g) {
          if (guard[n]) bad = 1
          guard[n] = g; pending[n] = 1
          if (g == 1) out = ind "is_comp_vector(" a ") && pop[1]==" a " && pop[2]==arg1) {" marker
          continue
        }
        if (!pending[n]) continue
        l = state(t, "for (unsigned int k = 2; k<pop.size(); ++k) if (pop[k]==" a ") { is_sth = false; break; }",
                  "for (unsigned int k = 2; k<pop.size(); ++k) if (pop[k]==" a " || pop[k]==arg1) { is_sth = false; break; }")
        if (!l && t == "if (pop[k]==" a " || pop[k]==arg1) { is_sth = false; break; }") l = 2
        if (l) {
          loop[n] = l; pending[n] = 0
          if (l == 1) out = ind "for (unsigned int k = 2; k<pop.size(); ++k) if (pop[k]==" a " || pop[k]==arg1) { is_sth = false; break; }" marker
        }
      }
      if (mode == "patch") print out
    }
    END { if (mode == "count") print (bad ? 9 : guard[3] + 0), loop[3] + 0, (bad ? 9 : guard[2] + 0), loop[2] + 0 }' "$1"
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

set -- `scan "$f" count`
for s in "$@"; do
  case $s in 1|2|3) ;; *) fail "expected the in-place guard and the aliasing loop of 'V = expr' and '(V) = expr', found states $*";; esac
done
case "$*" in
  *1*) ;;
  *3*) echo "$f: already patched (vector assignment peepholes, CImg#505)"; exit 0;;
  *) echo "$f: the vector assignments already check is_comp_vector() and aliasing: not patched (fixed upstream, CImg#505)"; exit 0;;
esac

scan "$f" patch > "$f.patched" || fail "the code to patch is not as expected"
set -- `scan "$f.patched" count`
case "$*" in *1*|*0*|*9*) fail "the patch did not apply";; esac
[ "`wc -l < "$f.patched"`" -eq "`wc -l < "$f"`" ] || fail "the patch changed the number of lines"
mv "$f.patched" "$f"
echo "$f: patched so that 'V = expr' and '(V) = expr' check that the value is a temporary that f() doesn't read (CImg#505)"
