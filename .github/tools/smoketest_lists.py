"""Helpers of smoketest.sh's Python scripts for its lists (SKIP, OPENGL, IDENTITY, INPUTS, PARAMS, XFAIL).

smoketest.sh writes each list to a file of its work directory, one entry per line, and its
scripts import this module from the directory of smoketest.sh ($SMOKETEST_TOOLS).
"""

import os

# "identifier regex[:major][@context][/depth]" of PARAMS and XFAIL: the groups are the regex,
# the major version, the context and the depth
SELECTOR = r"([^:@/|]*)(?::([0-9]+))?(?:@(filter|general))?(?:/(byte|short|float))?"
_PREFIXES = ("debug", "ofx14", "ofx11")


def entries(work, name):
    """Reads a list file of the work directory.

    Args:
        work: the work directory of smoketest.sh.
        name: the file name of the list, e.g. "xfail.txt".

    Returns:
        The entries of the list, one per non-empty line, without the newline.
    """
    with open(os.path.join(work, name)) as f:
        return [line.rstrip("\n") for line in f if line.strip()]


def only_in(desc, debug, profile):
    """Splits the build and host prefixes off an entry.

    The "debug:" entries are only used in debug builds, the "ofx14:" ones with --ofx14 and the
    "ofx11:" ones with --ofx11; the prefixes can be combined.

    Args:
        desc: the entry, with its prefixes, if any.
        debug: True for a debug build.
        profile: the hostDemo profile: "ofx14", "ofx11" or "default".

    Returns:
        A (keep, desc) tuple: keep is True if the entry applies to this build and profile, and
        desc is the entry without its prefixes.
    """
    keep = True
    parts = desc.split(":", 1)
    while len(parts) == 2 and parts[0] in _PREFIXES:
        keep = keep and (debug if parts[0] == "debug" else parts[0] == profile)
        desc = parts[1]
        parts = desc.split(":", 1)
    return keep, desc
