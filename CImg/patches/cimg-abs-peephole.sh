#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's abs(maxabs(...)) -> absmaxabs(...) optimization (and the same for minabs) only rewrites
# code compiled for abs()'s own argument, into a temporary. Usage: cimg-abs-peephole.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/502
#
# CImg's abs() handler (in 4.0.7 and 4.0.8) rewrites the last compiled opcode (code.back()) into
# absmaxabs (absminabs) whenever it is a maxabs (minabs) with three or more arguments, and then
# returns that opcode's result. It checks neither that this opcode was emitted while compiling
# the argument (code.size()>p3, as the peepholes of the '+', '-', '*' and '/' operators do), nor
# that it writes the argument, nor that the argument is a temporary. With a=-3, b=1, c=-5:
# - 'm=maxabs(a,b,c);abs(a)' gives 5 instead of 3, and m becomes 5 instead of -5 (the opcode is
#   the one that computed m earlier);
# - 'abs(m=maxabs(a,b,c));m' gives 5 for m instead of -5 (the assignment takes the temporary's
#   slot, so maxabs writes m directly).
# The patch adds the guards: 'p3 = code.size();' before compiling the argument, and
# 'if (code.size()>p3 && code.back()[1]==arg1 && is_comp_scalar(arg1)) {' instead of
# 'if (code) {'.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose abs()
# handler has no such peephole (it mentions neither mp_absmaxabs nor mp_absminabs, and has no
# line to patch), or tests is_comp_scalar(arg1) (an upstream fix: a
# 'code.size()>p3' guard alone is not enough). The script fails in any other case: the expected
# code missing or partly fixed (e.g. after a GMICVER bump: then check what upstream changed, and
# update or remove this patch), an abs() handler that doesn't end with
# '_cimg_mp_scalar1(abs,arg1);' before the next handler, or CRLF line endings. Only POSIX sh, awk
# and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

head='if (!std::strncmp(ss,"abs(",4)) { // Absolute value'
end="_cimg_mp_scalar1(abs,arg1);"
comp="arg1 = compile(ss4,se1,depth1,0,block_flags);"
old="if (code) { // Spot 'abs(maxabs())' and 'abs(minabs())' cases"
marker="p3 = code.size(); // openfx-gmic patch: abs(maxabs()) peephole only on a temporary computed by abs()'s argument"
new="if (code.size()>p3 && code.back()[1]==arg1 && is_comp_scalar(arg1)) { // Spot 'abs(maxabs())' and 'abs(minabs())' cases"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-abs-peephole.sh (https://github.com/GreycLab/CImg/issues/502)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<abs() handlers> <markers> <argument compilations> <unguarded tests> <guarded
# tests> <absmaxabs/absminabs rewrites> <is_comp_scalar(arg1) tests> <handler ends> <other
# handlers before the end> <lines mentioning mp_absmaxabs or mp_absminabs>", inside the abs()
# handler, from its first line to its last one ('_cimg_mp_scalar1(abs,arg1);')
counts() {
  awk -v head="$head" -v end="$end" -v comp="$comp" -v old="$old" -v marker="$marker" -v new="$new" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == head { h++; inabs = 1; next }
    !inabs { next }
    t == end { e++; inabs = 0; next }
    index(t, "if (!std::strncmp(ss,\"") == 1 { x++; inabs = 0; next }
    t == marker { m++ }
    t == comp { c++ }
    t == old { o++ }
    t == new { n++ }
    index(t, "pop[0] = (ulongT)mp_absmaxabs;") || index(t, "pop[0] = (ulongT)mp_absminabs;") { r++ }
    index(t, "mp_absmaxabs") || index(t, "mp_absminabs") { a++ }
    index(t, "is_comp_scalar(arg1)") { g++ }
    END { print h + 0, m + 0, c + 0, o + 0, n + 0, r + 0, g + 0, e + 0, x + 0, a + 0 }' "$1"
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
[ "$1" = 1 ] || fail "expected one abs() handler, found $1"
[ "$8" = 1 ] && [ "$9" = 0 ] || fail "the abs() handler doesn't end with '$end' before the next handler"
if [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 1 ] && [ "$6" = 2 ]; then
  echo "$f: already patched (abs(maxabs()) peephole, CImg#502)"
  exit 0
fi
if [ "$2" = 0 ] && [ "${10}" = 0 ] && [ "$4" = 0 ] && [ "$5" = 0 ]; then
  echo "$f: the abs() handler has no abs(maxabs()) peephole: nothing to patch (CImg#502)"
  exit 0
fi
if [ "$2" = 0 ] && [ "$7" -ge 1 ]; then
  echo "$f: the abs() handler's abs(maxabs()) peephole already tests is_comp_scalar(arg1): not patched (fixed upstream, CImg#502)"
  exit 0
fi
[ "$2" = 0 ] && [ "$3" = 1 ] && [ "$4" = 1 ] && [ "$5" = 0 ] && [ "$6" = 2 ] && [ "$7" = 0 ] ||
  fail "expected one abs() handler with '$comp', then '$old' and 2 rewrites; found $3 argument compilations, $4 unguarded and $5 guarded tests, $6 rewrites, $7 is_comp_scalar(arg1) tests ($2 patch markers)"

awk -v head="$head" -v end="$end" -v comp="$comp" -v old="$old" -v marker="$marker" -v new="$new" '
{ line = $0; t = line; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = line; sub(/[^ \t].*$/, "", ind) }
t == head { inabs = 1; print; next }
inabs && t == end { inabs = 0 }
inabs && t == comp { print ind marker; print; sawcomp = 1; next }
inabs && t == old {
  if (!sawcomp) { print "line " NR ": peephole test before the argument is compiled" > "/dev/stderr"; exit 1 }
  print ind new; inabs = 0; next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 1 ] && [ "$6" = 2 ] && [ "$7" = 1 ] && [ "$8" = 1 ] && [ "$9" = 0 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 1)) ] || fail "the patch did not add 1 line"
mv "$f.patched" "$f"
echo "$f: patched so that abs() no longer rewrites the code that computed a variable (CImg#502)"
