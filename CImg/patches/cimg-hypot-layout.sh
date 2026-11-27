#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math
# parser's hypot() with one or two values uses cimg::hypot() again (no overflow or underflow), on
# CImg versions whose norm opcodes list their arguments as (slot,size) pairs.
# Usage: cimg-hypot-layout.sh <CImg.h>
# https://github.com/NatronGitHub/openfx-gmic/issues/244
#
# CImg develop (887e2b6 and later) changed the opcode of norm*() and hypot(): opcode[2] is now the
# number of arguments, each given as (slot,size) from opcode[3] on, and mp_vector_norm2() reads
# that layout ('const unsigned int nb_args = (unsigned int)mp.opcode[2];'). mp_vector_hypot() still
# switches on the old opcode heights (5, 6, 7), which can't occur any more: it always falls back to
# mp_vector_norm2(), sqrt(a*a + b*b), so 'hypot(1e200,1e200)' gives inf and 'hypot(1e-200,1e-200)'
# gives 0. CImg 4.0.8 has the old layout, where mp_vector_hypot() is right: there the script does
# nothing. The patch replaces the body of mp_vector_hypot() with one that reads the new layout and
# calls cimg::hypot() for one or two values (the caller only uses it for one or two arguments;
# more values fall back to mp_vector_norm2(), as before).
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h with the old
# layout, or whose mp_vector_hypot() has the patch's body (an upstream fix). The script fails in any other case: mp_vector_hypot() or the layout of mp_vector_norm2()
# not found as expected, including an upstream change of mp_vector_hypot() (then check what
# upstream changed, and update or remove this patch), or CRLF line endings. Only POSIX sh, awk and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

head="static double mp_vector_hypot(_cimg_math_parser& mp) {"
marker="// openfx-gmic patch: hypot() of 1 or 2 values with the (slot,size) opcode layout (#244)"
old1="switch ((unsigned int)mp.opcode[2]) {"
old2="case 5 : return cimg::abs(_mp_arg(4));"
norm2="static double mp_vector_norm2(_cimg_math_parser& mp) {"
newlayout="const unsigned int nb_args = (unsigned int)mp.opcode[2];"
oldlayout="const unsigned int siz = (unsigned int)mp.opcode[2];"
# the patch's body (trimmed lines, '|'-separated), which upstream may adopt as is
body="const unsigned int nb_args = (unsigned int)mp.opcode[2];|double val[2] = { 0, 0 };|unsigned int n = 0;|for (unsigned int i = 0; i<2*nb_args; i+=2) {|const unsigned int siz = (unsigned int)mp.opcode[4 + i];|const double *const ptr = &_mp_arg(3 + i) + (siz?1:0);|for (unsigned int k = 0; k<std::max(1U,siz); ++k) {|if (n==2) return mp_vector_norm2(mp);|val[n++] = ptr[k];|}|}|return n==1?cimg::abs(*val):cimg::hypot(val[0],val[1]);"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-hypot-layout.sh (https://github.com/NatronGitHub/openfx-gmic/issues/244)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<mp_vector_hypot definitions> <markers> <old switches> <lines in its body>
# <mp_vector_norm2 with the new layout> <mp_vector_norm2 with the old layout> <bodies that are the
# patch's, without its marker>"
counts() {
  awk -v head="$head" -v marker="$marker" -v old1="$old1" -v old2="$old2" -v norm2="$norm2" \
      -v newlayout="$newlayout" -v oldlayout="$oldlayout" -v body="$body" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    t == head { h++; inhyp = 1; text = ""; next }
    inhyp && t == "}" && $0 ~ /^      }$/ { inhyp = 0; if (text == body) b++; next }
    inhyp { lines++; if (t == marker) m++; if (t == old1) s1++; if (t == old2) s2++
            if (t != marker) text = (text == "" ? t : text "|" t) }
    t == norm2 { innorm = 1; next }
    innorm { if (t == newlayout) nl++; if (t == oldlayout) ol++; innorm = 0 }
    END { print h + 0, m + 0, (s1 && s2) ? 1 : 0, lines + 0, nl + 0, ol + 0, b + 0 }' "$1"
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
[ "$1" = 1 ] || fail "expected one '$head', found $1"
[ $(($5 + $6)) = 1 ] || fail "expected mp_vector_norm2() to start with '$newlayout' or '$oldlayout'"
if [ "$2" = 1 ]; then
  echo "$f: already patched (hypot(), #244)"
  exit 0
fi
if [ "$2" = 0 ] && [ "$5" = 1 ] && [ "$7" = 1 ]; then
  echo "$f: mp_vector_hypot() already reads the new layout (the patch's code): not patched (fixed upstream, #244)"
  exit 0
fi
if [ "$6" = 1 ] && [ "$3" = 1 ]; then
  echo "$f: the norm opcodes have the old layout, which mp_vector_hypot() reads: nothing to patch (#244)"
  exit 0
fi
[ "$5" = 1 ] && [ "$3" = 1 ] && [ "$4" = 6 ] ||
  fail "expected the 6-line mp_vector_hypot() with the switch on the old opcode heights, and the new layout in mp_vector_norm2()"

awk -v head="$head" -v marker="$marker" '
{ t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
t == head {
  print
  print "        " marker
  print "        const unsigned int nb_args = (unsigned int)mp.opcode[2];"
  print "        double val[2] = { 0, 0 };"
  print "        unsigned int n = 0;"
  print "        for (unsigned int i = 0; i<2*nb_args; i+=2) {"
  print "          const unsigned int siz = (unsigned int)mp.opcode[4 + i];"
  print "          const double *const ptr = &_mp_arg(3 + i) + (siz?1:0);"
  print "          for (unsigned int k = 0; k<std::max(1U,siz); ++k) {"
  print "            if (n==2) return mp_vector_norm2(mp);"
  print "            val[n++] = ptr[k];"
  print "          }"
  print "        }"
  print "        return n==1?cimg::abs(*val):cimg::hypot(val[0],val[1]);"
  skip = 1; next
}
skip && $0 ~ /^      }$/ { skip = 0 }
skip { next }
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$5" = 1 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + 7)) ] || fail "the patch did not add 7 lines"
mv "$f.patched" "$f"
echo "$f: patched so that hypot() of 1 or 2 values doesn't overflow or underflow (#244)"
