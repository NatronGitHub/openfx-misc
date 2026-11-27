# DaVinci Resolve tests

These tests start DaVinci Resolve with an openfx-misc install, build small
Fusion comps around our plug-ins through Resolve's scripting API, export a
frame and check its pixels. They catch Resolve-specific problems that
hostDemo can't show (see the Resolve section of `README-hosts.txt`). They are
local tests: CI does not run them (#437).

- `resolve_harness.py`: starts and stops Resolve, builds and renders a case,
  and decodes the exported PNG.
- `test_resolve.py`, `conftest.py`, `pytest.ini`: the pytest suite.
  `test_decode_png` needs no Resolve.

## Requirements

- macOS, with DaVinci Resolve 21.1.2 or later (free or Studio; `ExportFrame`
  is new in 21.1.2) in `/Applications/DaVinci Resolve/`, or `--resolve APP`. Its scripting module
  is `/Library/Application Support/Blackmagic Design/DaVinci Resolve/Developer/Scripting/Modules/DaVinciResolveScript.py`.
- External scripting allowed in Resolve: Preferences > System > General >
  External scripting using: Local. The README of Resolve's scripting API
  documents this preference for Studio; if the free version has no such
  setting and refuses the connection, the tests skip.
- An openfx-misc install: `cmake --install build --prefix PREFIX`. Resolve
  gets its plug-ins from `OFX_PLUGIN_PATH=PREFIX/OFX/Plugins`.
- No Resolve already running: the tests start their own, and never attach
  to, or quit, one that someone is using.
- pytest (MacPorts: `/opt/local/bin/pytest-3.11`).

## Running

    pytest tests/resolve --ofx-prefix PREFIX --launch-resolve [--resolve APP] [--resolve-gui] [--keep DIR]

Resolve is a GUI application, so nothing starts without `--launch-resolve`.
It starts with `-nogui` unless `--resolve-gui` is given. A session creates
one scratch project (`openfx-misc-tests-<pid>`, with DaVinci YRGB colour
science and 256x256 timelines), deletes it at the end, and quits Resolve
(by script, then SIGTERM). `--keep DIR` keeps Resolve's output
(`resolve.log`) and the exported frames (`caseN.png`).

Each case is a Fusion composition clip on its own timeline: the plug-in's
Fusion tool (`ofx.<plug-in identifier>`) connected to `MediaOut1`. Comps
made with `fu.NewComp()` can't render in Resolve.

Resolve caches plug-in descriptions in
`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml`.
