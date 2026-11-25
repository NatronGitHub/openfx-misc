#!/usr/bin/env python3
"""Checks that the plug-ins' string literals contain no percent sign.

Some OFX hosts may pass the strings that a plug-in gives them (labels, hints,
descriptions, option labels, messages) to a printf-style function: a percent
sign there is then read as a conversion, and can crash the host. VEGAS Pro 23
and later crash when NoOp's settings panel opens, and the only percent signs
in NoOp's strings were in "%TEMP%" (issue #66). So no string or character
literal of a plug-in may contain one, except the format argument of the
printf and scanf families, strftime, OFX::Log and DPRINT, which never reaches
the host, and the code of asm statements. "%%" is flagged even there.

For any other literal that must contain a percent sign, put "percent-ok" in a
comment on its line, with the reason. Write "percent" in user-visible text.

The smoke test (smoketest.sh) checks the describe-time strings that hostDemo
reports (labels, grouping, hints, choice options) after the plug-in built
them; this scan covers the rest (descriptions, option hints, messages).
Strings built at run time from user or host data are not covered.

Usage: check-percent-strings.py [--root DIR] [FILE...]
  With no FILE, checks the C and C++ files tracked by git in DIR (default:
  the repository of this script), except the submodules and the third-party
  code in EXCLUDED_PREFIXES. Exits with status 1 and lists the literals if
  any is found.
"""

import argparse
import os
import re
import subprocess
import sys
from typing import Iterator, List, Optional, Sequence, Tuple

# The index of the format argument of each format function (by the last
# component of its name). DPRINT((format, ...)): see _FORMAT_GROUP_CALLS.
FORMAT_ARG = {
    "printf": 0, "wprintf": 0, "vprintf": 0, "vwprintf": 0,
    "fprintf": 1, "fwprintf": 1, "vfprintf": 1, "vfwprintf": 1,
    "sprintf": 1, "vsprintf": 1, "sprintf_s": 2, "vsprintf_s": 2,
    "snprintf": 2, "vsnprintf": 2, "_snprintf": 2, "_snwprintf": 2,
    "swprintf": 2, "vswprintf": 2, "snprintf_s": 3, "_snprintf_s": 3,
    "scanf": 0, "wscanf": 0, "scanf_s": 0,
    "sscanf": 1, "swscanf": 1, "fscanf": 1, "sscanf_s": 1, "fscanf_s": 1,
    "strftime": 2, "wcsftime": 2,
    "Log::print": 0, "Log::trace": 0, "Log::warning": 1, "Log::error": 1,
}
# Macros whose single argument is a parenthesized printf argument list.
_FORMAT_GROUP_CALLS = frozenset(("DPRINT",))
_ASM = frozenset(("asm", "__asm", "__asm__"))
EXCLUDED_PREFIXES = ("openfx/", "SupportExt/", "GL/", "CImg/CImg.h")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx")
_CALL_NAME = re.compile(r"([A-Za-z_][\w:]*)\s*$")
_RAW_STRING = re.compile(r'R"([^ ()\\\t\n]{0,16})\(')
_CHAR_LITERAL = re.compile(r"'(?:\\.|[^'\\\n])*'")
_STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\\n])*"')


class _Paren:
    """An open parenthesis: the call it opens, and its current argument."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.arg = 0


def _call_name(text: str, paren: int) -> str:
    """Returns the name before the parenthesis at index paren of text.

    Args:
        text: C or C++ source code.
        paren: the index of a "(" in text.

    Returns:
        The identifier (with its "::" qualifiers) just before the
        parenthesis, or "" if there is none (a grouping parenthesis).
    """
    m = _CALL_NAME.search(text, max(0, paren - 80), paren)
    return m.group(1) if m else ""


def _format_index(name: str) -> Optional[int]:
    """Returns the index of the format argument of the call name, if any.

    Args:
        name: a function name, possibly qualified (OFX::Log::error).

    Returns:
        The index of the format argument, or None if name is not a format
        function.
    """
    parts = name.split("::")
    if len(parts) >= 2 and parts[-2] == "Log":
        return FORMAT_ARG.get("Log::" + parts[-1])
    key = parts[-1]
    if key.startswith("Log::"):
        return None
    return FORMAT_ARG.get(key)


def _is_exempt(parens: Sequence[_Paren]) -> bool:
    """Returns whether a literal at this point is a format or asm string.

    Args:
        parens: the open parentheses of the statement, innermost last.

    Returns:
        True if the literal is in an asm statement, or is the format
        argument of the innermost call.
    """
    if any(p.name in _ASM for p in parens):
        return True
    if not parens:
        return False
    inner = parens[-1]
    index = _format_index(inner.name)
    if index is not None:
        return inner.arg == index
    if not inner.name and len(parens) >= 2:
        outer = parens[-2]
        return (outer.name.split("::")[-1] in _FORMAT_GROUP_CALLS
                and outer.arg == 0 and inner.arg == 0)
    return False


def find_percent_literals(text: str) -> Iterator[Tuple[int, str]]:
    """Yields the string and character literals of text with a percent sign.

    Comments are skipped, and so are the format and asm strings (see
    _is_exempt) unless they contain "%%".

    Args:
        text: C or C++ source code.

    Yields:
        (line number, literal source text) tuples.
    """
    i, n = 0, len(text)
    parens: List[_Paren] = []  # the open parentheses of the statement
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c in "'\"":
            m = None
            if c == '"' and i > 0 and text[i - 1] == "R":
                m = _RAW_STRING.match(text, i - 1)
            if m:
                end = text.find(")" + m.group(1) + '"', m.end())
                j = n if end < 0 else end + len(m.group(1)) + 2
            else:
                pattern = _CHAR_LITERAL if c == "'" else _STRING_LITERAL
                m = pattern.match(text, i)
                j = m.end() if m else i + 1
            literal = text[i:j]
            if "%" in literal and ("%%" in literal or c == "'"
                                   or not _is_exempt(parens)):
                yield text.count("\n", 0, i) + 1, literal
            i = j
            continue
        if c == "(":
            parens.append(_Paren(_call_name(text, i)))
        elif c == ")":
            if parens:
                parens.pop()
        elif c == ",":
            if parens:
                parens[-1].arg += 1
        elif c in ";{}":
            parens = []
        i += 1


def tracked_sources(root: str) -> List[str]:
    """Returns the C and C++ files tracked by git in root.

    Args:
        root: a directory in a git work tree.

    Returns:
        The paths, relative to root, without the excluded ones.
    """
    out = subprocess.run(["git", "-C", root, "ls-files", "-z"], check=True,
                         stdout=subprocess.PIPE).stdout.decode()
    return [f for f in out.split("\0")
            if f.endswith(SOURCE_SUFFIXES)
            and not f.startswith(EXCLUDED_PREFIXES)]


def main(argv: Sequence[str]) -> int:
    """Checks the files and prints the literals with a percent sign.

    Args:
        argv: the command-line arguments, without the program name.

    Returns:
        The exit status: 0 if no literal has a percent sign, else 1.
    """
    parser = argparse.ArgumentParser(
        description=(__doc__ or "").partition("\n")[0])
    parser.add_argument(
        "--root",
        default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                             "..", ".."))
    parser.add_argument("files", nargs="*")
    args = parser.parse_args(argv)
    files = args.files or tracked_sources(args.root)
    found = 0
    for f in files:
        path = f if args.files else os.path.join(args.root, f)
        with open(path, encoding="utf-8", errors="replace") as source:
            text = source.read()
        lines = text.split("\n")
        for line, literal in find_percent_literals(text):
            if "percent-ok" in lines[line - 1]:
                continue
            found += 1
            shown = literal if len(literal) <= 60 else literal[:57] + "..."
            print("%s:%d: percent sign in a literal: %s" % (f, line, shown))
    if found:
        print("%d literal(s) with a percent sign: write \"percent\" or "
              "reword, since a host may read the string as a printf format "
              "(issue #66). Only the format argument of a printf, scanf, "
              "strftime, OFX::Log or DPRINT call may have one (not \"%%%%\"); "
              "mark any other exception with a \"percent-ok\" comment."
              % found)
        return 1
    print("no percent sign in the literals of %d files" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
