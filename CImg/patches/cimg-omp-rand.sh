#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that the random
# numbers drawn by rand and noise in OpenMP parallel regions, and by the per-thread copies of the
# math parser, do not depend on thread scheduling. Usage: cimg-omp-rand.sh <CImg.h>
# https://github.com/NatronGitHub/openfx-gmic/issues/44
#
# CImg's parallel random loops start each thread with
#   cimg_uint64 rng = (cimg::_rand(),cimg::rng());   // advance the global generator, copy it
#   rng+=omp_get_thread_num();
# so a thread's seed depends on the order in which the threads arrive, and they all write back
# their state at the end (cimg::srand(rng)): the last thread to finish sets the global seed. The
# per-thread copies of the math parser (fill, eval) also seed from the global generator in
# arrival order. With more than one OpenMP thread, "-srand 42 noise 5" gave a different image
# at each run.
#
# After the patch, with a team of more than one thread:
# - every thread of the parallel loops of rand() and noise() (those whose parallel region is
#   'cimg_openmp_if_size(size(),...)') starts from the same state, one step after the global
#   state, which is read without being modified (cimg::_rand_omp_begin()), plus its thread number
#   (unchanged); only thread 0 writes its final state back (cimg::_rand_omp_end()), after the
#   implicit barrier of the loop's 'omp for' (checked below: the other threads have all read the
#   global state by then);
# - a per-thread copy of the math parser starts from the state of the parser it copies (made
#   before the barrier that precedes the parallel loop, so before thread 0 uses it), plus its
#   thread number; the copies' write-backs are overwritten by the original parser's destructor,
#   after the parallel region.
# The 'omp for' loops have the default (static) schedule, so the result is the same at each run
# for a given number of OpenMP threads (it differs between thread counts). With one thread (or a
# region too small to be parallel), the code does exactly what it did before.
#
# Not fixed: patchmatch (its propagation loops read the rows of other threads and update the
# penalty atomically, so it varies with scheduling whatever the seeds: its loops are left as they
# are), G'MIC's 'parallel' command (threads of one render sharing the global generator), a team
# size changed by OMP_DYNAMIC, and concurrent renders (they share the global generator).
#
# Running it again on a patched file does nothing, and so does running it on a CImg.h where the
# patched code no longer seeds itself from the global generator (an upstream fix). The script fails
# in any other case: some of the expected code missing (e.g. after a GMICVER bump: then check what
# upstream changed, and update or remove this patch), or CRLF line endings. Only POSIX sh, awk and
# wc are used, for Linux, macOS and MSYS2.
set -e
f="$1"

marker="inline cimg_uint64 _rand_omp_begin() { // openfx-gmic patch (issue #44)"
# expected number of parallel random loops (rand, noise) and of math parser copy constructors,
# and number of occurrences of '_rand(),cimg::rng()' (spaces allowed) in CImg.h (G'MIC 4.0.7)
nloops=8
ncopies=1
nloose=15
# lines added by the patch (the two helper functions, with a blank line before each)
nadded=25

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-omp-rand.sh (openfx-gmic issue #44)" >&2
  rm -f "$f.patched"
  exit 1
}

# counts <file>: "<markers> <loops to patch> <patched loops> <copy constructors
# to patch> <'_rand(),cimg::rng()' in the places to patch> <'_rand(),cimg::rng()' anywhere>"
counts() {
  awk -v marker="$marker" '
    { t = $0; gsub(/^[ \t]+|[ \t]+$/, "", t)
      loose = (t ~ /_rand\(\) *, *cimg::rng\(\)/)
      loop = (prev ~ /cimg_pragma_openmp\(parallel cimg_openmp_if_size\(size\(\),/)
      copy = (prev ~ /result_end\(mp\.result_end\?/) }
    t == marker { m++ }
    loop && t == "cimg_uint64 rng = (cimg::_rand(),cimg::rng());" { old++ }
    loop && t == "cimg_uint64 rng = cimg::_rand_omp_begin();" { new++ }
    copy && t == "rng((cimg::_rand(),cimg::rng())) {" { cp++ }
    loose && (loop || copy) { lt++ }
    loose { la++ }
    t != "" { prev = t }
    END { print m + 0, old + 0, new + 0, cp + 0, lt + 0, la + 0 }' "$1"
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
if [ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = $nloops ] && [ "$4" = 0 ] && [ "$5" = 0 ] && [ "$6" = $((nloose - nloops - ncopies + 1)) ]; then
  echo "$f: already patched (issue #44)"
  exit 0
fi
# fixed upstream: none of the places to patch seeds itself from the global generator, which is
# used at most as often as in the other places of the 4.0.7 code
if [ "$1" = 0 ] && [ "$3" = 0 ] && [ "$5" = 0 ] && [ "$6" -le $((nloose - nloops - ncopies)) ]; then
  echo "$f: the parallel loops of rand and noise and the math parser copies no longer seed themselves from the global generator: not patched (fixed upstream) (issue #44)"
  exit 0
fi
[ "$1" = 0 ] && [ "$2" = $nloops ] && [ "$3" = 0 ] && [ "$4" = $ncopies ] && [ "$5" = $((nloops + ncopies)) ] && [ "$6" = $nloose ] ||
  fail "expected $nloops parallel random loops (rand, noise) and $ncopies math parser copy constructor seeded with '(cimg::_rand(),cimg::rng())', and that expression $nloose times in all; found $2 loops, $4 copy constructors, $5 occurrences in these places and $6 in all ($3 patched loops, $1 patch markers)"

awk -v marker="$marker" '
function bad(msg) { print "line " NR ": " msg > "/dev/stderr"; err = 1 }
BEGIN { inloop = 0; sawfor = 0; insrand = 0; prev = ""; err = 0 }
{
  line = $0
  t = line; gsub(/^[ \t]+|[ \t]+$/, "", t)
  if (t == "inline void srand(const cimg_uint64 seed) {") insrand = 1
  if (t == "cimg_uint64 rng = (cimg::_rand(),cimg::rng());" && prev ~ /cimg_pragma_openmp\(parallel cimg_openmp_if_size\(size\(\),/) {
    if (inloop) bad("nested parallel random loop")
    sub(/\(cimg::_rand\(\),cimg::rng\(\)\)/, "cimg::_rand_omp_begin()", line)
    inloop = 1; sawfor = 0
  } else if (inloop && t ~ /^cimg_pragma_openmp\(for/) {
    if (t ~ /nowait/) bad("omp for nowait in a parallel random loop")
    sawfor = 1
  } else if (inloop && t == "cimg::srand(rng);") {
    if (!sawfor) bad("no omp for loop (barrier) before the write-back")
    sub(/cimg::srand\(rng\)/, "cimg::_rand_omp_end(rng)", line)
    inloop = 0
  } else if (t == "rng((cimg::_rand(),cimg::rng())) {" && prev ~ /result_end\(mp\.result_end\?/) {
    sub(/rng\(\(cimg::_rand\(\),cimg::rng\(\)\)\) \{/, "rng(mp.rng) { // openfx-gmic patch (issue #44): the state of the copied parser", line)
  }
  print line
  if (insrand && t == "}") {
    insrand = 0
    print ""
    print "    // Seed of each thread of a parallel random loop: with several threads, the state that"
    print "    // cimg::_rand() would give, without modifying the global state, so that it does not"
    print "    // depend on the order in which the threads arrive."
    print "    " marker
    print "#if cimg_use_openmp!=0"
    print "      if (omp_get_num_threads()>1) {"
    print "        cimg::mutex(4);"
    print "        cimg_uint64 x = cimg::rng();"
    print "        cimg::mutex(4,0);"
    print "        cimg::_rand(&x);"
    print "        return x;"
    print "      }"
    print "#endif"
    print "      return (cimg::_rand(),cimg::rng());"
    print "    }"
    print ""
    print "    // End of a parallel random loop, after the barrier of its omp for: with several threads,"
    print "    // only thread 0 writes its state back (instead of the last thread to finish)."
    print "    inline void _rand_omp_end(const cimg_uint64 rng) {"
    print "#if cimg_use_openmp!=0"
    print "      if (omp_get_num_threads()>1 && omp_get_thread_num()) return;"
    print "#endif"
    print "      cimg::srand(rng);"
    print "    }"
  }
  if (t != "") prev = t
}
END {
  if (inloop) bad("unterminated parallel random loop")
  exit err
}' "$f" > "$f.patched" || fail "the code to patch is not as expected"

set -- `counts "$f.patched"`
[ "$1" = 1 ] && [ "$2" = 0 ] && [ "$3" = $nloops ] && [ "$4" = 0 ] && [ "$5" = 0 ] && [ "$6" = $((nloose - nloops - ncopies + 1)) ] || fail "the patch did not apply"
[ "`wc -l < "$f.patched"`" -eq $((`wc -l < "$f"` + nadded)) ] || fail "the patch did not add $nadded lines"
# number of lines that differ (no diff: MSYS2 does not install it by default): the added lines
# are skipped (a blank line, which the original also has after srand(seed), then nadded - 1 lines
# starting with the first comment), then each line is compared with the original one
changed=`awk -v nadded=$nadded '
  NR == FNR { a[FNR] = $0; next }
  skip > 0 { skip--; next }
  !done && $0 ~ /^ *\/\/ Seed of each thread of a parallel random loop/ { skip = nadded - 2; done = 1; off = nadded; next }
  $0 != a[FNR - off] { n++ }
  END { print n + 0 }' "$f" "$f.patched"`
[ "$changed" = $((2 * nloops + ncopies)) ] || fail "the patch changed $changed lines, not $((2 * nloops + ncopies))"
mv "$f.patched" "$f"
echo "$f: patched for deterministic random numbers with OpenMP (issue #44)"
