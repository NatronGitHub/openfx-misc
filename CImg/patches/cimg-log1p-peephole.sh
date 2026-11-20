#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's log(1 + x) -> log1p(x) optimization only rewrites code compiled for log()'s own
# argument, into a temporary. Usage: cimg-log1p-peephole.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/502
#
# CImg 4.0.8's log() handler rewrites the last compiled opcode (code.back()) into log1p (or
# vector_log1p) when it is an increment (mp_increment, mp_vector_increment) that writes log()'s
# argument. It doesn't check that this opcode was emitted while compiling the argument, nor that
# it writes a temporary:
# - when the argument is a variable, which compiles to no code, the opcode is the one that
#   computed the variable earlier, and the variable is overwritten: 'y=1+x;log(y);y' gives
#   log(1 + x) instead of 1 + x, and 'Y=[x,x]+1;Z=log(Y);Y[0]' gives log(1 + x) instead of x + 1;
# - when the argument is an assignment, the increment writes the variable directly (the
#   assignment takes the temporary's slot): 'log(y=x+1);y' gives log(1 + x) instead of x + 1.
# The patch adds both guards: 'p3 = code.size();' before compiling the argument and
# 'is_sth = code.size()>p3;', as in the older peepholes of the '+', '-', '*' and '/' operators,
# and is_comp_vector(arg1) / is_comp_scalar(arg1) in the vector and scalar rewrites.
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h whose log()
# handler has no log1p peephole (it mentions neither mp_log1p nor mp_vector_log1p, and has none of
# the lines to patch, e.g. CImg 4.0.7), or tests both is_comp_scalar(arg1) and
# is_comp_vector(arg1) (an upstream fix: a 'code.size()>p3' guard alone is not enough). The
# script fails in any other case: the expected code missing or partly fixed (e.g. after a GMICVER
# bump: then check what upstream changed, and update or remove this patch), a log() handler that
# doesn't end with '_cimg_mp_scalar1(log,arg1);' before the next handler, or CRLF line endings.
# Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

head='if (!std::strncmp(ss,"log(",4)) { // Natural logarithm'
end="_cimg_mp_scalar1(log,arg1);"
comp="arg1 = compile(ss4,se1,depth1,0,block_flags);"
marker="p3 = code.size(); // openfx-gmic patch: log1p peephole only on a temporary computed by log()'s argument"
# the lines to replace (old1..old3) and their replacements (new1..new3), trimmed
old1="is_sth = code.size()?true:false;"
new1="is_sth = code.size()>p3;"
old2="if (is_sth && fn==mp_vector_increment && pop[1]==arg1) { // Spot case 'log(1 + X)'"
new2="if (is_sth && fn==mp_vector_increment && pop[1]==arg1 && is_comp_vector(arg1)) { // Spot case 'log(1 + X)'"
old3="if (is_sth && fn==mp_increment && pop[1]==arg1) { // Spot case 'log(1 + x)'"
new3="if (is_sth && fn==mp_increment && pop[1]==arg1 && is_comp_scalar(arg1)) { // Spot case 'log(1 + x)'"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-log1p-peephole.sh (https://github.com/GreycLab/CImg/issues/502)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<log() handlers> <markers> <argument compilations> <lines to patch> <patched
# lines> <log1p rewrites> <is_comp_scalar(arg1) tests> <is_comp_vector(arg1) tests> <handler
# ends> <other handlers before the end> <lines mentioning mp_log1p or mp_vector_log1p>", inside the
# log() handler, from its first line to its last one ('_cimg_mp_scalar1(log,arg1);')
counts() {
  awk -v head="$head" -v end="$end" -v comp="$comp" -v marker="$marker" \
      -v o1="$old1" -v o2="$old2" -v o3="$old3" -v n1="$new1" -v n2="$new2" -v n3="$new3" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == head { h++; inlog = 1; next }
    !inlog { next }
    t == end { e++; inlog = 0; next }
    index(t, "if (!std::strncmp(ss,\"") == 1 { x++; inlog = 0; next }
    t == marker { m++ }
    t == comp { c++ }
    t == o1 || t == o2 || t == o3 { o++ }
    t == n1 || t == n2 || t == n3 { n++ }
    index(t, "pop[0]") && (index(t, "mp_log1p") || index(t, "mp_vector_log1p")) { r++ }
    index(t, "mp_log1p") || index(t, "mp_vector_log1p") { a++ }
    index(t, "is_comp_scalar(arg1)") { gs++ }
    index(t, "is_comp_vector(arg1)") { gv++ }
    END { print h + 0, m + 0, c + 0, o + 0, n + 0, r + 0, gs + 0, gv + 0, e + 0, x + 0, a + 0 }' "$1"
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
[ "$1" = 1 ] || fail "expected one log() handler, found $1"
[ "$9" = 1 ] && [ "${10}" = 0 ] || fail "the log() handler doesn't end with '$end' before the next handler"
if [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 3 ] && [ "$6" = 2 ]; then
  echo "$f: already patched (log1p peephole, CImg#502)"
  exit 0
fi
if [ "$2" = 0 ] && [ "${11}" = 0 ] && [ "$4" = 0 ] && [ "$5" = 0 ]; then
  echo "$f: the log() handler has no log1p peephole: nothing to patch (CImg#502)"
  exit 0
fi
if [ "$2" = 0 ] && [ "$7" -ge 1 ] && [ "$8" -ge 1 ]; then
  echo "$f: the log() handler's log1p peephole already tests is_comp_scalar(arg1) and is_comp_vector(arg1): not patched (fixed upstream, CImg#502)"
  exit 0
fi
[ "$2" = 0 ] && [ "$3" = 1 ] && [ "$4" = 3 ] && [ "$5" = 0 ] && [ "$6" = 2 ] && [ "$7" = 0 ] && [ "$8" = 0 ] ||
  fail "expected one log() handler with '$comp', the 3 lines to patch and 2 log1p rewrites; found $3 argument compilations, $4 lines to patch, $5 patched lines, $6 rewrites, $7 is_comp_scalar(arg1) and $8 is_comp_vector(arg1) tests ($2 patch markers)"

awk -v head="$head" -v end="$end" -v comp="$comp" -v marker="$marker" \
    -v o1="$old1" -v o2="$old2" -v o3="$old3" -v n1="$new1" -v n2="$new2" -v n3="$new3" '
{ line = $0; t = line; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = line; sub(/[^ \t].*$/, "", ind) }
t == head { inlog = 1; print; next }
inlog && t == end { inlog = 0 }
inlog && t == comp { print ind marker; print; sawcomp = 1; next }
inlog && (t == o1 || t == o2 || t == o3) {
  if (!sawcomp) { print "line " NR ": peephole before the argument is compiled" > "/dev/stderr"; exit 1 }
  print ind (t == o1 ? n1 : t == o2 ? n2 : n3); next
}
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 3 ] && [ "$6" = 2 ] && [ "$7" = 1 ] && [ "$8" = 1 ] && [ "$9" = 1 ] && [ "${10}" = 0 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 1)) ] || fail "the patch did not add 1 line"
mv "$f.patched" "$f"
echo "$f: patched so that log() no longer rewrites the code that computed a variable (CImg#502)"
