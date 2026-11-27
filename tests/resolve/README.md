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
  External scripting using: Local, set once by hand. It is off by default:
  the first run (free Resolve 21.2, `-nogui`) got no connection in 180 s and
  skipped. The README of Resolve's scripting API documents this preference
  for Studio; whether the free version offers it is still unchecked.
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
by script, waiting up to 60 s. It never sends Resolve a signal: on
SIGTERM, Resolve 21.2 crashes at exit (a shutdown race in its Fairlight page,
which locks a destroyed mutex) and starts its crash reporter
(`Resolve -reportCrash ...`). If the script can't quit Resolve (no scripting
connection) or it doesn't quit in time, the tests leave it running and print
its pid: quit it by hand. `--keep DIR` keeps Resolve's output
(`resolve.log`) and the exported frames (`caseN.png`).

Each case is a Fusion composition clip on its own timeline: the plug-in's
Fusion tool (`ofx.<plug-in identifier>`) connected to `MediaOut1`. Comps
made with `fu.NewComp()` can't render in Resolve.

Resolve loads the plug-ins of `OFX_PLUGIN_PATH` as well as those of its
usual folders and of its cache; when a plug-in is found twice, it keeps
the newer version (its log says "Newer version of plugin is found in ...").
Its log is
`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/ResolveDebug.txt`.
Resolve caches plug-in descriptions in
`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml`.
