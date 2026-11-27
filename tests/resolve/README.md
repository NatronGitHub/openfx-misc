# DaVinci Resolve tests

These tests render openfx-misc plug-ins in DaVinci Resolve and check the
pixels. They catch Resolve-specific problems that hostDemo can't show (see
the Resolve section of `README-hosts.txt`). They are local and manual: CI
does not run them (#437).

Resolve version and edition: DaVinci Resolve 21.2.0 (free version) on
macOS. Its scripting environment was probed on 2026-11-27 (the facts below);
the suite itself has not run in Resolve yet. That environment sets what the
tests can do:
- There is no external scripting: the `DaVinciResolveScript` module in
  another process, and the "External scripting" preference, are Studio
  features. So the script runs inside Resolve.
- Workspace > Scripts lists only Lua scripts: Python files, even
  Blackmagic's own examples, don't appear, and there is no Python Console.
- The menu scripts' Lua has no `io` and no `os.execute`: it can't write
  files or run pytest. `os.getenv` works, and so do Resolve's and Fusion's
  APIs. `print()` goes to Resolve's stdout, not to Workspace > Console.

So `openfx_misc_tests.lua` renders the cases, has Resolve write the frames,
and prints its results to Resolve's stdout; Resolve is started from Terminal
with its stdout in a file; pytest reads that file after Resolve quits.

- `openfx_misc_tests.lua`: the in-Resolve script, and the cases (`CASES`).
- `install-script.sh`: copies the script into Resolve's Scripts menu.
- `test_resolve.py`, `conftest.py`, `pytest.ini`: the checks.
- `resolve_harness.py`: the results block, Resolve's logs and plug-in cache,
  the host description, and PNG decoding.
- `fake_resolve.lua`, `fixtures/`: a fake Resolve and a sample log, for the
  tests of the tests (they need no Resolve).

## Running

1. Build and install the bundles to a prefix. To also collect Resolve's OFX
   host description, build with `DEBUG_HOSTDESCRIPTION` (only Rectangle
   uses it: it prints the description in its describe action):

       port_switch; cmake -S . -B build-resolve -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS=-DDEBUG_HOSTDESCRIPTION
       cmake --build build-resolve -j4 && cmake --install build-resolve --prefix PREFIX

2. Run `tests/resolve/install-script.sh PREFIX`. It copies
   `openfx_misc_tests.lua` to
   `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Utility/`
   (and removes an `openfx_misc_tests.py` left there by an earlier version),
   creates `~/openfx-misc-resolve-results/`, and prints the commands below
   with the paths filled in. Run it again after changing the script.
3. Save your work and quit Resolve, then start it from Terminal with the
   prefix's plug-ins and its output in `resolve-stdout.log`:

       OFX_PLUGIN_PATH=PREFIX/OFX/Plugins "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/MacOS/Resolve" > ~/openfx-misc-resolve-results/resolve-stdout.log 2>&1

4. Run Workspace > Scripts > Utility > openfx_misc_tests. Nothing shows in
   the Console. The script should take under a minute, and it switches
   projects while it runs. It ends with the line
   "openfx-misc tests: done (run STAMP)" in `resolve-stdout.log`.
5. Quit Resolve normally (DaVinci Resolve > Quit). Don't kill it: on
   SIGTERM, Resolve 21.2 crashes at exit (a shutdown race in its Fairlight
   page, which locks a destroyed mutex).
6. Run `pytest tests/resolve -rs` (MacPorts: `/opt/local/bin/pytest-3.11`).

`$OPENFX_MISC_RESOLVE_RESULTS` moves `~/openfx-misc-resolve-results`: set it
for install-script.sh, Resolve and pytest alike. `pytest --log FILE` reads
another log, and `--results DIR` checks a results directory again.

## What the script does

It needs Resolve 21.1.2 or later (`Timeline:ExportFrame`). It creates a
scratch project `openfx-misc-tests-STAMP` (DaVinci YRGB colour science, so
values pass through, and 256x256 timelines). Each case is a Fusion
composition clip on its own timeline: the plug-ins' Fusion tools
(`ofx.<plug-in identifier>`), chained, the last one connected to
`MediaOut1`. (Comps made with `fu.NewComp()` can't render in Resolve.) OFX
params are set through the Fusion inputs whose IDs match the param name
(`color` sets `colorRed`, `colorGreen`... if those exist). Frame 0 is exported
with `Timeline:ExportFrame` to `~/openfx-misc-resolve-results/STAMP-CASE.png`,
and for the first case also as TIFF and EXR, to see which formats work. Then
the scratch project is deleted, the project that was open is reopened, and
Resolve keeps running: the script never quits it.

Its results (Resolve's version and edition, `OFX_PLUGIN_PATH`, Resolve's OFX
effect IDs and, per case, each tool's Fusion RegID, all its input IDs with
their types and limits, what was set and read back, the export results and
the expected pixels) are one JSON line between `OPENFX-MISC-RESULTS-BEGIN
STAMP` and `OPENFX-MISC-RESULTS-END STAMP` on stdout. Lua has no JSON
library: the script has a small encoder (an empty table is written `{}`).

## What pytest checks

pytest takes the newest results block of `resolve-stdout.log` and makes
`~/openfx-misc-resolve-results/STAMP/`: a copy of the log, the frames
(`CASE.png`...), and `results.json` (the block, plus each plug-in's entries
in Resolve's plug-in cache). Then it checks:

- the scratch project was set up and deleted;
- each plug-in was loaded only from `OFX_PLUGIN_PATH`, per Resolve's plug-in
  cache (`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml`,
  which Resolve rewrites at each start: run pytest before starting Resolve
  again). Another install, e.g. in `/Library/OFX/Plugins`, would make the
  results ambiguous. Also, when a bundle registers several major versions
  of a plug-in, Resolve keeps only the newest ("ignoring plugin ... Newer
  version of plugin is found" in its log), so obsolete majors can't be
  tested here;
- each case: the tools were created, their params set, and the PNG has the
  expected values at a few pixels (within 2/255);
- Resolve's log (`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/ResolveDebug.txt`,
  and `davinci_resolve.log`), copied into the results directory if written
  within 10 minutes of `resolve-stdout.log` (both are written when Resolve
  quits), has no termination signal or crash dump;
- the OFX host description: the block that Rectangle prints between
  "OFX host description follows" and "OFX DebugProxy: host description
  finished" goes to `host-description.txt`, as a README-hosts.txt entry
  ("Version 21 (21.2.0.6):", then `key=value` lines). This skips without
  the dump.

Without results, these checks skip. `test_decode_png`, `test_sample_log`
and `test_sample_wrong_pixel` need no Resolve, and
`test_lua_script_with_fake_resolve` runs the Lua script with
`fake_resolve.lua` through Resolve's `fuscript` (offline, without
connecting to Resolve), if Resolve is installed.
