#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the math parser
# does not read past the end of a macro body. Usage: cimg-math-macro-overread.sh <CImg.h>
#
# When _cimg_math_parser::compile() substitutes a macro argument, it skips the blanks after it to
# decide whether to add parentheses, with 'while (cimg::is_blank(*pa)) ++pa;'. cimg::is_blank()
# is also true for the terminating '\0': when the macro body ends with an argument name (e.g.
# 'f(a)=1+a', or 'and(a,b)=a&b' in the begin() of the filter "Lavander Binary Map"), the loop
# reads beyond the buffer, which can crash at the end of a memory page (SIGBUS seen once).
# https://github.com/GreycLab/CImg/issues/496, fixed upstream the same way in GreycLab/CImg 73b0e45
# (develop, after G'MIC 4.0.7).
#
# Only the line after the two lines of context below is changed. The script fails if the source
# does not contain the expected code (e.g. after a GMICVER bump): then check whether upstream
# fixed it, and update or remove this patch. Running it again on a patched file does nothing, and
# so does running it on a CImg.h that already has the fix. Only POSIX sh, awk and wc are used,
# for Linux, macOS and MSYS2.
set -e
f="$1"

context1='const char *pb = ps - 1, *pa = ps + p2;'
context2='while (pb>=macro_body[0]._data && cimg::is_blank(*pb)) --pb;'
line_old='while (cimg::is_blank(*pa)) ++pa;'
line_new='while (*pa && cimg::is_blank(*pa)) ++pa;'

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-math-macro-overread.sh (GreycLab/CImg#496)" >&2
  rm -f "$f.patched"
  exit 1
}

# occurrences <file> <line>: number of times <line> follows the two context lines (lines compared
# without their leading and trailing blanks)
occurrences() {
  awk -v c1="$context1" -v c2="$context2" -v l="$2" '
    { gsub(/^[ \t]+|[ \t\r]+$/, ""); if ($0 == l && p1 == c1 && p2 == c2) n++; p1 = p2; p2 = $0 }
    END { print n + 0 }' "$1"
}

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"

old=$(occurrences "$f" "$line_old")
new=$(occurrences "$f" "$line_new")
# already patched, or fixed upstream: nothing to do
if [ "$old" = 0 ] && [ "$new" = 1 ]; then
  echo "$f: already patched (GreycLab/CImg#496)"
  exit 0
fi
# the code to patch must be found exactly
[ "$old" = 1 ] && [ "$new" = 0 ] || fail "expected the line '$line_old' once in _cimg_math_parser::compile(), after '$context1' and '$context2' (found $old times, and the fixed line $new times)"

awk -v c1="$context1" -v c2="$context2" -v l="$line_old" '
  { t = $0; gsub(/^[ \t]+|[ \t\r]+$/, "", t)
    if (t == l && p1 == c1 && p2 == c2) sub(/while \(cimg::is_blank\(\*pa\)\)/, "while (*pa \\&\\& cimg::is_blank(*pa))")
    print; p1 = p2; p2 = t }' "$f" > "$f.patched" || fail "awk failed"
[ "$(occurrences "$f.patched" "$line_old")" = 0 ] && [ "$(occurrences "$f.patched" "$line_new")" = 1 ] || fail "the patch did not apply"
[ "$(wc -l < "$f.patched")" -eq "$(wc -l < "$f")" ] || fail "the patch changed the number of lines"
# number of lines that differ (no diff: MSYS2 does not install it by default)
changed=$(awk 'NR == FNR { a[FNR] = $0; next } $0 != a[FNR] { n++ } END { print n + 0 }' "$f" "$f.patched")
[ "$changed" = 1 ] || fail "the patch changed $changed lines, not 1"
mv "$f.patched" "$f"
echo "$f: patched for a read past the end of math parser macros (GreycLab/CImg#496)"
