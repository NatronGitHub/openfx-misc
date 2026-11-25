#!/usr/bin/env python3
"""Checks that the plug-ins' string literals contain no percent sign.

Some OFX hosts may pass the strings that a plug-in gives them (labels, hints,
descriptions, option labels, messages) to a printf-style function: a percent
sign there is then read as a conversion, and can crash the host. VEGAS Pro 23
and later crash when NoOp's settings panel opens, and the only percent signs in
NoOp's strings were in "%TEMP%" (issue #66). So no string literal of a plug-in
may contain one, except the format strings of the printf and scanf families,
OFX::Log and DPRINT, which never reach the host.

A literal is a format string if it is an argument of a call to one of these
functions, at any depth (DPRINT(("%d", n)) too). For any other literal that
must contain a percent sign, put "percent-ok" in a comment on its line, with
the reason. Write "percent" in user-visible text.

Usage: check-percent-strings.py [--root DIR] [FILE...]
  With no FILE, checks the C and C++ files tracked by git in DIR (default: the
  repository of this script), except the submodules and the third-party code in
  EXCLUDED_PREFIXES. Exits with status 1 and lists the literals if any is found.
"""

import argparse
import os
import re
import subprocess
import sys

# The functions (the last component of their name) whose arguments may be format strings.
FORMAT_CALLS = re.compile(r"(?:\w*printf\w*|\w*scanf\w*|strftime|wcsftime|DPRINT|"
                          r"error|warning|print|trace)$")
# OFX::Log's functions: only when they are called as Log::name.
LOG_ONLY = frozenset(("error", "warning", "print", "trace"))
EXCLUDED_PREFIXES = ("openfx/", "SupportExt/", "GL/", "CImg/CImg.h")
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".hxx")
CALL_NAME = re.compile(r"([A-Za-z_][\w:]*)\s*$")
RAW_STRING = re.compile(r'R"([^ ()\\\t\n]{0,16})\(')
CHAR_LITERAL = re.compile(r"'(?:\\.|[^'\\\n])*'")
STRING_LITERAL = re.compile(r'"(?:\\.|[^"\\\n])*"')


def _is_format_call(text, paren):
    """Returns whether the parenthesis at index paren of text opens a format call."""
    m = CALL_NAME.search(text, max(0, paren - 80), paren)
    if not m:
        return False
    name = m.group(1)
    last = name.split("::")[-1]
    if not FORMAT_CALLS.match(last):
        return False
    if last in LOG_ONLY:
        return name.endswith("Log::" + last)
    return True


def find_percent_literals(text):
    """Yields the string literals of text that have a percent sign.

    The literals that are arguments of a format call, comments and character
    literals are skipped.

    Args:
        text: C or C++ source code.

    Yields:
        (line number, literal source text) tuples.
    """
    i, n = 0, len(text)
    parens = []  # the indices of the open parentheses of the current statement
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
        if c == "'":
            m = CHAR_LITERAL.match(text, i)
            i = m.end() if m else i + 1
            continue
        if c == '"':
            m = RAW_STRING.match(text, i - 1) if i > 0 and text[i - 1] == "R" else None
            if m:
                end = text.find(")" + m.group(1) + '"', m.end())
                j = n if end < 0 else end + len(m.group(1)) + 2
            else:
                m = STRING_LITERAL.match(text, i)
                j = m.end() if m else i + 1
            literal = text[i:j]
            if "%" in literal and not any(_is_format_call(text, p) for p in parens):
                yield text.count("\n", 0, i) + 1, literal
            i = j
            continue
        if c == "(":
            parens.append(i)
        elif c == ")":
            if parens:
                parens.pop()
        elif c in ";{}":
            parens = []
        i += 1


def tracked_sources(root):
    """Returns the C and C++ files tracked by git in root, without the excluded ones."""
    out = subprocess.run(["git", "-C", root, "ls-files", "-z"], check=True,
                         stdout=subprocess.PIPE).stdout.decode()
    return [f for f in out.split("\0")
            if f.endswith(SOURCE_SUFFIXES) and not f.startswith(EXCLUDED_PREFIXES)]


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root",
                        default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
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
            shown = literal if len(literal) <= 100 else literal[:97] + "..."
            print("%s:%d: percent sign in a string literal: %s" % (f, line, shown))
    if found:
        print("%d string literal(s) with a percent sign: write \"percent\" or reword, since a host "
              "may read the string as a printf format (issue #66). A format string must be an "
              "argument of a printf, scanf, OFX::Log or DPRINT call; mark any other exception "
              "with a \"percent-ok\" comment." % found)
        return 1
    print("no percent sign in the string literals of %d files" % len(files))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
