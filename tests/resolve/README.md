# DaVinci Resolve tests

These tests render openfx-misc plug-ins in DaVinci Resolve and check the
pixels. They catch Resolve-specific problems that hostDemo can't show (see
the Resolve section of `README-hosts.txt`). They are local and manual: CI
does not run them (#437).

The free Resolve can't be scripted from outside: external scripting (the
`DaVinciResolveScript` module in another process) is a Studio feature. So a
script runs inside Resolve, from its Workspace > Scripts menu, and writes a
results directory; pytest checks it afterwards.

- `openfx_misc_tests.py`: the in-Resolve script, and the cases (`CASES`).
- `install-script.sh`: copies the script into Resolve's Scripts menu.
- `test_resolve.py`, `conftest.py`, `pytest.ini`: the checks.
- `resolve_harness.py`: results, Resolve's logs and PNG decoding.

## Running

1. Install the bundles to a prefix:
   `cmake --install build --prefix PREFIX`.
2. Install the script: `tests/resolve/install-script.sh PREFIX`. It copies
   `openfx_misc_tests.py` to
   `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/Fusion/Scripts/Utility/`
   and prints the next command. Run it again after changing the script.
3. Save your work and quit Resolve, then start it from Terminal with the
   prefix's plug-ins:

       OFX_PLUGIN_PATH=PREFIX/OFX/Plugins "/Applications/DaVinci Resolve/DaVinci Resolve.app/Contents/MacOS/Resolve"

4. Run Workspace > Scripts > Utility > openfx_misc_tests. Workspace >
   Console shows its progress, and "results in DIR" at the end. It should
   take under a minute, and it switches projects while it runs.
5. Quit Resolve normally (DaVinci Resolve > Quit). Don't kill it: on
   SIGTERM, Resolve 21.2 crashes at exit (a shutdown race in its Fairlight
   page, which locks a destroyed mutex).
6. Run `pytest tests/resolve` (MacPorts: `/opt/local/bin/pytest-3.11`). It
   checks the newest results directory, or the one given with
   `--results DIR`.

## What the script does

It needs Resolve 21.1.2 or later (`Timeline.ExportFrame`), free or Studio.
It creates a scratch project `openfx-misc-tests-<timestamp>` (DaVinci YRGB
colour science, so values pass through, and 256x256 timelines). Each case is
a Fusion composition clip on its own timeline: the plug-ins' Fusion tools
(`ofx.<plug-in identifier>`), chained, the last one connected to
`MediaOut1`. (Comps made with `fu.NewComp()` can't render in Resolve.) OFX
params are set through the Fusion inputs whose IDs match the param name
(`color` sets `colorRed`, `colorGreen`... if those exist). Frame 0 is exported
with `Timeline.ExportFrame` as PNG, and for the first case also as TIFF and
EXR, to see which formats work. Then the scratch project is deleted, the
project that was open is reopened, and Resolve keeps running: the script
never quits it.

The results go to `~/openfx-misc-resolve-results/<timestamp>/`, or under
`$OPENFX_MISC_RESOLVE_RESULTS` if Resolve was started with it; a
`config.json` in that root, `{"output_root": "DIR"}`, moves them to DIR.
A results directory holds `results.json` (Resolve's version, the
`OFX_PLUGIN_PATH`, Resolve's OFX effect IDs and, per case, each tool's Fusion
RegID, all its input IDs with their types and limits, what was set and read
back, and the export results with the PNG bit depth), `log.txt` and the
frames.

## What pytest checks

- The scratch project was set up and deleted.
- Each plug-in was loaded only from `OFX_PLUGIN_PATH`, per Resolve's plug-in
  cache (`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/OFXPluginCacheV2.xml`,
  which Resolve rewrites at each start, as the script records it). Another
  install, e.g. in `/Library/OFX/Plugins`, would make the results
  ambiguous. Also, when a bundle registers several major versions of a
  plug-in, Resolve keeps only the newest ("ignoring plugin ... Newer version
  of plugin is found" in its log), so obsolete majors can't be tested here.
- Each case: the tools were created, their params set, and the PNG has the
  expected values at a few pixels (within 2/255).
- Resolve's log of the run (`~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/ResolveDebug.txt`,
  and `davinci_resolve.log`), copied into the results directory once
  Resolve has quit, has no termination signal or crash dump. Before Resolve
  quits, this check skips.

Without results, the Resolve checks skip; `test_decode_png` needs no Resolve.
