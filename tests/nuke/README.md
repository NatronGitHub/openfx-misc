# Compatibility tests

These tests render small synthetic images through openfx-misc plug-ins
(with openfx's hostDemo) and through the nodes of the same name in Foundry's
Nuke, and compare the pixels. They are local tests: CI does not run them,
and they are separate from openfx-misc's own tests (`.github/tools`).

- `cases.py`: the input images and the cases (one test each).
- `harness.py`: the Nuke and hostDemo runners, PFM files and comparison.
- `test_compatibility.py`, `conftest.py`, `pytest.ini`: the pytest suite.

A case that matches is a plain test. A known difference is a strict
expected failure whose reason is the GitHub issue that records it (label
`compatibility`); a test that starts to match fails, so update the case. A
plug-in that is not in the install (e.g. a Debug-only one) is skipped.
A PR that fixes a known difference removes its `known=` in the same PR
and runs `pytest tests/nuke -k <plug-in>`.

## Requirements

- An openfx-misc install with hostDemo: configure with
  `-DOFX_BUILD_HOSTDEMO=ON`, then `cmake --install build --prefix PREFIX`.
- Nuke (the newest `/Applications/Nuke*/Nuke*.app` is found, or `--nuke`).
  A Nuke Non-Commercial licence is enough once you are signed in: the tests
  run `nuke --nc -t`, one process per case, because a Non-Commercial
  session allows only 10 node objects. Without a working Nuke, the whole
  session is skipped.
- pytest (MacPorts: `/opt/local/bin/pytest-3.11`). pytest-xdist is
  optional; two Non-Commercial sessions can run at the same time, so
  `-n 2` works (do not use more).

## Running

    pytest tests/nuke --ofx-prefix PREFIX [--nuke NUKE] [--keep DIR] [-k Merge]

`--ofx-prefix` defaults to `$OFX_PREFIX` and `--nuke` to `$NUKE`;
`$HOSTDEMO` (with `$HOSTDEMO_REAL`) runs hostDemo through a wrapper.
Test ids are `<plug-in>/<case>`, e.g. `Merge/over alpha outside 0-1`:
`-k Merge` selects one plug-in. `--keep DIR` keeps the scripts, logs and
images of each case for inspection.
