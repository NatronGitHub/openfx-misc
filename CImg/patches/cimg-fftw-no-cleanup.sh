#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that its FFTs don't
# call fftw_cleanup_threads(). Usage: cimg-fftw-no-cleanup.sh <CImg.h>
# Reported upstream: https://github.com/GreycLab/CImg/issues/500
#
# With multithreaded FFTW (cimg_use_fftw3 without cimg_use_fftw3_singlethread), both FFT()
# functions of CImg call fftw_cleanup_threads() after each FFT, under CImg's own mutex only. That
# frees FFTW's process-wide planner (all its plans and accumulated wisdom) and its thread pool. FFTW
# is a shared library: another plug-in of the same host (e.g. openfx-misc's CImg plug-ins, which
# link the same libfftw3) may be planning or executing an FFT at that moment, under its own mutex,
# and then uses freed memory. fftw_make_planner_thread_safe() (gmic_plugin.cpp) doesn't protect
# against it: FFTW's cleanup functions don't take the planner lock. Not cleaning up is safe: the
# planner and the threads are kept until the process exits, and fftw_init_threads() stays in
# CImg's static initialisation. Same in GreycLab/CImg develop.
#
# This script replaces each of the two blocks
#   #ifndef cimg_use_fftw3_singlethread
#         fftw_cleanup_threads();
#   #endif
# with a one-line comment. "If the fix is already there, don't apply it": the script does nothing
# on a CImg.h that has the fix (patched, or fixed upstream: no fftw_cleanup* at all), applies it
# to the 4.0.7 or develop code (the two blocks, and no other fftw_cleanup*), and fails on anything
# else (e.g. after a GMICVER bump that changed this code otherwise), leaving the file untouched:
# then check what upstream did, and update or remove this patch. Lines are compared without their
# leading and trailing blanks. Only POSIX sh, awk, cmp and wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

# the three lines of each block to remove, and the comment that replaces it
blk1='#ifndef cimg_use_fftw3_singlethread'
blk2='fftw_cleanup_threads();'
blk3='#endif'
comment="// fftw_cleanup_threads() removed by patches/cimg-fftw-no-cleanup.sh (FFTW's planner is process-wide)."

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-fftw-no-cleanup.sh" >&2
  rm -f "$f.patched" "$f.stripped1" "$f.stripped2"
  exit 1
}
trap 'rm -f "$f.patched" "$f.stripped1" "$f.stripped2"; exit 1' INT TERM HUP

case "$f" in
  *CImg.h) ;;
  *) echo "Error: $0: unexpected file '$f'" >&2; exit 1;;
esac
[ -f "$f" ] || fail "file not found"
cr=`printf '\r'`
if awk -v cr="$cr" 'index($0, cr) { found = 1; exit } END { exit !found }' "$f"; then
  fail "CRLF line endings are not supported (extract CImg.h with LF line endings)"
fi

# state <file>: number of blocks, of comment lines, and of other lines that contain fftw_cleanup
state() {
  awk -v b1="$blk1" -v b2="$blk2" -v b3="$blk3" -v cm="$comment" '
    function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
    { t[++n] = trim($0) }
    END {
      for (i = 1; i <= n; ++i) {
        if (i + 2 <= n && t[i] == b1 && t[i + 1] == b2 && t[i + 2] == b3) { ++blocks; i += 2; continue }
        if (t[i] == cm) ++comments
        else if (t[i] ~ /fftw[fl]?_cleanup/) ++others
      }
      print blocks + 0, comments + 0, others + 0
    }' "$1"
}

found=$(state "$f")
if [ "$found" = "0 2 0" ]; then
  echo "$f: already patched (no fftw_cleanup_threads() after each FFT)"
  exit 0
fi
if [ "$found" = "0 0 0" ]; then
  echo "$f: already fixed upstream (no fftw_cleanup* call)"
  exit 0
fi
[ "$found" = "2 0 0" ] || fail "expected the block '$blk1' '$blk2' '$blk3' twice and no other fftw_cleanup*, as in G'MIC 4.0.7 and GreycLab/CImg develop, or the patched code (found blocks, comments, other fftw_cleanup* lines: $found)"

awk -v b1="$blk1" -v b2="$blk2" -v b3="$blk3" -v cm="$comment" '
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  { o[++n] = $0; t[n] = trim($0) }
  END {
    for (i = 1; i <= n; ++i) {
      if (i + 2 <= n && t[i] == b1 && t[i + 1] == b2 && t[i + 2] == b3) {
        match(o[i + 1], /^[ \t]*/)
        print substr(o[i + 1], 1, RLENGTH) cm
        i += 2
        continue
      }
      print o[i]
    }
  }' "$f" > "$f.patched" || fail "awk failed"
[ "$(state "$f.patched")" = "0 2 0" ] || fail "the patch did not apply"
[ "$(wc -l < "$f.patched")" -eq $(($(wc -l < "$f") - 4)) ] || fail "the patch changed the number of lines unexpectedly"
# nothing else changed: without the blocks and the comments, both files are the same
awk -v b1="$blk1" -v b2="$blk2" -v b3="$blk3" '
  function trim(s) { gsub(/^[ \t]+|[ \t\r]+$/, "", s); return s }
  { o[++n] = $0; t[n] = trim($0) }
  END { for (i = 1; i <= n; ++i) { if (i + 2 <= n && t[i] == b1 && t[i + 1] == b2 && t[i + 2] == b3) { i += 2; continue } print o[i] } }' \
  "$f" > "$f.stripped1" || fail "awk failed"
awk -v cm="$comment" '{ t = $0; gsub(/^[ \t]+|[ \t\r]+$/, "", t) } t != cm' "$f.patched" > "$f.stripped2" || fail "awk failed"
cmp -s "$f.stripped1" "$f.stripped2" || fail "the patch changed other lines"
rm -f "$f.stripped1" "$f.stripped2"
mv "$f.patched" "$f"
echo "$f: patched to not call fftw_cleanup_threads() after each FFT"
