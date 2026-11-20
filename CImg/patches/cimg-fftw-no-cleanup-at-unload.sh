#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the destructor
# of its static FFTW3_static object doesn't call fftw_cleanup_threads(). Usage:
# cimg-fftw-no-cleanup-at-unload.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/500
#
# With multithreaded FFTW (cimg_use_fftw3 without cimg_use_fftw3_singlethread), CImg 4.0.8 calls
# fftw_init_threads() in the constructor of a static object and fftw_cleanup_threads() in its
# destructor (its fix for CImg#500, which removed the cleanup after each FFT). In a plug-in, that
# destructor runs inside the host's dlclose(). FFTW's cleanup (3.3.10, kill_workforce()) wakes
# each of its worker threads, which are detached, and returns as soon as each one has signalled
# that it is terminating; the worker then calls pthread_exit(), which unwinds through FFTW's
# worker() frame. When FFTW is linked statically into GMIC.ofx (Linux, GMIC_STATIC_DEPS), or when
# a shared libfftw3 is unloaded with it, dlclose() unmaps that code and its unwind tables while
# the workers are still exiting: SIGSEGV in libgcc_s's unwinder, in an FFTW worker, after the
# render (eu.gmic.ABNFiligrees, whose 480x480 FFT leaves more workers, crashed at every run of
# hostDemo on Linux). Without the cleanup, the idle workers stay blocked on their semaphores, and
# the planner is kept, until the process exits: after an unload, they are never woken again (a
# small leak per unload and reload of the plug-in). It is also safer with a shared libfftw3: the
# cleanup freed FFTW's process-wide planner and threads, which another plug-in of the host may be
# using. fftw_init_threads() stays in the constructor.
#
# This script replaces the line 'fftw_cleanup_threads();' of '~FFTW3_static() { ... }' with a
# one-line comment, leaving an empty destructor. Running it again on a patched file does nothing,
# and so does running it on a CImg.h that no longer calls any fftw_cleanup* function (fixed
# upstream). The script fails in any other case: a destructor with other code, a cleanup
# elsewhere (e.g. after each FFT, as in 4.0.7), several FFTW3_static structs (e.g. after a
# GMICVER bump: then check what upstream changed, and update or remove this patch), or CRLF line
# endings. Lines are compared without their leading and trailing blanks. Only POSIX sh, awk and
# wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

head="struct FFTW3_static {"
dtor="~FFTW3_static() {"
call="fftw_cleanup_threads();"
marker="// openfx-gmic patch: no fftw_cleanup_threads() at unload (https://github.com/GreycLab/CImg/issues/500)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-fftw-no-cleanup-at-unload.sh (https://github.com/GreycLab/CImg/issues/500)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<FFTW3_static structs> <destructors in them> <cleanup calls in the destructor>
# <markers in the destructor> <other lines in the destructor> <code lines calling fftw_cleanup*
# anywhere (not comments)>"
counts() {
  awk -v head="$head" -v dtor="$dtor" -v call="$call" -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t) }
    index(t, "fftw_cleanup") && index(t, "//") != 1 { a++ }
    t == head { s++; ins = 1; next }
    ins && index(t, "}; // struct FFTW3_static") == 1 { ins = 0; next }
    ins && t == dtor { d++; ind = 1; next }
    ind && t == "}" { ind = 0; next }
    ind && t == call { c++; next }
    ind && t == marker { m++; next }
    ind && t != "" { o++ }
    END { print s + 0, d + 0, c + 0, m + 0, o + 0, a + 0 }' "$1"
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
if [ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$4" = 1 ] && [ "$5" = 0 ] && [ "$6" = 0 ]; then
  echo "$f: already patched (no fftw_cleanup_threads() at unload, CImg#500)"
  exit 0
fi
if [ "$4" = 0 ] && [ "$6" = 0 ]; then
  echo "$f: no fftw_cleanup* call: not patched (fixed upstream, CImg#500)"
  exit 0
fi
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 1 ] && [ "$4" = 0 ] && [ "$5" = 0 ] && [ "$6" = 1 ] ||
  fail "expected one '$head' whose '$dtor' only calls '$call', and no other fftw_cleanup* call; found $1 structs, $2 destructors, $3 calls and $5 other lines in the destructor, $6 fftw_cleanup* calls in all ($4 patch markers)"

awk -v head="$head" -v dtor="$dtor" -v call="$call" -v marker="$marker" '
{ line = $0; t = line; gsub(/^[ \t]+|[ \t]+$/, "", t); ind = line; sub(/[^ \t].*$/, "", ind) }
t == head { ins = 1 }
ins && t == dtor { indtor = 1 }
indtor && t == call { print ind marker; indtor = 0; ins = 0; next }
{ print }' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 1 ] && [ "$3" = 0 ] && [ "$4" = 1 ] && [ "$5" = 0 ] && [ "$6" = 0 ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq "`wc -l < "$f"`" ] || fail "the patch changed the number of lines"
changed=`awk 'NR == FNR { a[FNR] = $0; next } $0 != a[FNR] { n++ } END { print n + 0 }' "$f" "$f.patched"`
[ "$changed" = 1 ] || fail "the patch changed $changed lines, not 1"
mv "$f.patched" "$f"
echo "$f: patched: no fftw_cleanup_threads() when the plug-in is unloaded (CImg#500)"
