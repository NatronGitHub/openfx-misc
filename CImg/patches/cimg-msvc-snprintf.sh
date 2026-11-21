#!/bin/sh
# Patch CImg.h, extracted from the G'MIC sources by the top-level Makefile, so that cimg_snprintf()
# always ends its output with a '\0' with MSVC. Usage: cimg-msvc-snprintf.sh <CImg.h>
# https://github.com/GreycLab/CImg/issues/507 (https://github.com/NatronGitHub/openfx-gmic/issues/111)
#
# With MSVC (and clang-cl), cimg_snprintf is cimg::_snprintf, whose body calls
# _vsnprintf(s,size,format,ap), meant to be cimg::_vsnprintf (_vsnprintf_s with _TRUNCATE, which
# always adds the '\0'). But cimg::_vsnprintf is declared after cimg::_snprintf, so the call
# resolves to the CRT's ::_vsnprintf, which adds no '\0' when it truncates (what CImg's comment just
# above warns about). G'MIC's 'input' command then leaves its 256-byte image title unterminated
# when the input values are longer (e.g. MontagePappusChain's math expression), and
# CImg<char>::string(title) reads past the end of the buffer: a crash when that reaches an unmapped
# page (native ARM64 builds, about 1 render in 8), garbage in the title otherwise (x64 too). MinGW
# and the other compilers use std::snprintf and are not affected.
#
# The patch declares cimg::_vsnprintf just before MSVC's cimg::_snprintf, inside its
# '#if cimg_OS==2 && defined(_MSC_VER)' block (also when another platform's block comes first; the
# result is checked), so that the call finds it.
# Running it again on a patched file does nothing (it says so), and so does running it on a CImg.h
# where the call no longer resolves to the CRT's function: cimg::_vsnprintf declared or defined
# before cimg::_snprintf, or no unqualified _vsnprintf( call left in its body (an upstream fix,
# https://github.com/GreycLab/CImg/issues/507; the script says so too). A whitespace change in the
# call still gets the patch. The script fails in any other case (MSVC's cimg::_snprintf or
# cimg::_vsnprintf not found, or more than one call: e.g. after a GMICVER bump, then check what
# upstream changed, and update or remove this patch), or with CRLF line endings. Only POSIX sh,
# awk, head, tr and wc are used, for Linux, macOS, MSYS2 and Git for Windows.
set -e
f="$1"

decl='inline int _vsnprintf(char *const s, const size_t size, const char *const format, va_list ap);'
marker="$decl // openfx-gmic patch: not the CRT's _vsnprintf (#111)"

fail() {
  echo "Error: $f: $1" >&2
  echo "Error: the CImg sources changed: update or remove patches/cimg-msvc-snprintf.sh" >&2
  rm -f "$f.patched"
  exit 1
}

if [ "$(head -c 4096 "$f" | tr -d '\n' | wc -c)" != "$(head -c 4096 "$f" | tr -d '\r\n' | wc -c)" ]; then
  fail "CRLF line endings"
fi

# state <file>: "<markers> <MSVC _snprintf> <unqualified _vsnprintf calls in its body>
# <_vsnprintf declarations or definitions before it, other than the marker>
# <_vsnprintf definitions after it>". The MSVC cimg::_snprintf is the one in an
# '#if cimg_OS==2 && defined(_MSC_VER)' block (not the macOS one, in the #elif); its body ends
# where its braces balance. An unqualified call is '_vsnprintf(' not preceded by '::' or an identifier character.
state() {
  awk -v marker="$marker" '
    { t = $0; sub(/^[ \t]+/, "", t); sub(/[ \t]+$/, "", t) }
    # the MSVC block: from "#if (or #elif) cimg_OS==2 && defined(_MSC_VER)" to its next #elif, #else
    # or #endif
    t ~ /^#[ \t]*if/ { if (msvc) nest++; else if (t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) msvc = 1 }
    t ~ /^#[ \t]*(elif|else|endif)/ { if (nest) { if (t ~ /^#[ \t]*endif/) nest-- } else msvc = (t ~ /^#[ \t]*elif/ && t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) }
    t == marker { m++; next }
    # (declarations anywhere before it; definitions only in the MSVC block, not e.g. the macOS ones)
    !seen && t ~ /^inline[ \t]+int[ \t]+_vsnprintf[ \t]*\(/ && (msvc || t ~ /;$/) { d++ }
    !seen && msvc && t ~ /^inline[ \t]+int[ \t]+_snprintf[ \t]*\(/ { seen = 1; fn = 1; infn = 1; depth = 0 }
    infn {
      if (match(t, /(^|[^:_A-Za-z0-9])_vsnprintf[ \t]*\(/)) c++
      u = t; o = gsub(/\{/, "", u); u = t; cl = gsub(/\}/, "", u)
      depth += o - cl; if (o) opened = 1
      if (opened && depth <= 0) infn = 0
      next
    }
    seen && msvc && t ~ /^inline[ \t]+int[ \t]+_vsnprintf[ \t]*\(/ && t ~ /\{$/ { v++ }
    END { printf "%d %d %d %d %d\n", m, fn, c, d, v }' "$1"
}

set -- $(state "$f")
markers=$1 fns=$2 calls=$3 decls=$4 defs=$5
[ "$fns" = 1 ] || fail "MSVC's cimg::_snprintf not found"
if [ "$markers" = 1 ] && [ "$decls" = 0 ]; then
  echo "$f: already patched (cimg_snprintf with MSVC, #111)"
  exit 0
fi
[ "$markers" = 0 ] || fail "$markers patch markers"
if [ "$decls" != 0 ] || [ "$calls" = 0 ]; then
  echo "$f: not patched: cimg::_vsnprintf is declared before cimg::_snprintf, or cimg::_snprintf no longer calls _vsnprintf unqualified (fixed upstream?): check and remove patches/cimg-msvc-snprintf.sh"
  exit 0
fi
[ "$defs" -ge 1 ] || fail "MSVC's cimg::_vsnprintf not found after cimg::_snprintf"
[ "$calls" = 1 ] || fail "$calls unqualified calls of _vsnprintf in cimg::_snprintf, expected one"

# the forward declaration, just before the MSVC cimg::_snprintf (the one in the '#if cimg_OS==2
# && defined(_MSC_VER)' block, as in state()), with its indentation
awk -v marker="$marker" '
  { t = $0; sub(/^[ \t]+/, "", t); ind = substr($0, 1, length($0) - length(t)) }
  t ~ /^#[ \t]*if/ { if (msvc) nest++; else if (t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) msvc = 1 }
  t ~ /^#[ \t]*(elif|else|endif)/ { if (nest) { if (t ~ /^#[ \t]*endif/) nest-- } else msvc = (t ~ /^#[ \t]*elif/ && t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) }
  !done && msvc && t ~ /^inline[ \t]+int[ \t]+_snprintf[ \t]*\(/ { print ind marker; done = 1 }
  { print }' "$f" > "$f.patched"
set -- $(state "$f.patched")
[ "$1 $2 $3 $4" = "1 1 1 0" ] || fail "the patched file is not as expected ($*)"
# and the declaration is in the MSVC block, right before the MSVC cimg::_snprintf
inblock=$(awk -v marker="$marker" '
  { t = $0; sub(/^[ \t]+/, "", t); sub(/[ \t]+$/, "", t) }
  t ~ /^#[ \t]*if/ { if (msvc) nest++; else if (t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) msvc = 1 }
  t ~ /^#[ \t]*(elif|else|endif)/ { if (nest) { if (t ~ /^#[ \t]*endif/) nest-- } else msvc = (t ~ /^#[ \t]*elif/ && t ~ /cimg_OS[ \t]*==[ \t]*2[ \t]*&&[ \t]*defined[ \t]*\([ \t]*_MSC_VER[ \t]*\)/) }
  prev && msvc && t ~ /^inline[ \t]+int[ \t]+_snprintf[ \t]*\(/ { ok = 1 }
  { prev = (t == marker && msvc) }
  END { print ok + 0 }' "$f.patched")
[ "$inblock" = 1 ] || fail "the declaration is not right before the MSVC cimg::_snprintf, in its #if block"
mv "$f.patched" "$f"
printf '%s: patched so that cimg_snprintf() always ends its output with a null character with MSVC (#111)\n' "$f"
