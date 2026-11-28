# Contributing to openfx-misc

openfx-misc is a set of OpenFX image-processing plug-ins in C++, written mainly
for [Natron](https://natrongithub.github.io/) but usable in any OpenFX host. It
is licensed under the GPL, version 2 or later; new source files start with the
header in `gpl-header-c.txt`.

This file covers what is specific to this repository. The general rules
(pull requests, commits, reviews, issues, CI) are those of all the Natron
repositories, in the
[Natron Maintainer Guide](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html).
In particular:

- [Pull requests](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#pull-requests),
  [Commits](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#commits) and
  [Descriptions and the CHANGELOG](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#descriptions-and-the-changelog):
  every pull request ends with a `### CHANGELOG` section; this repository has no
  changelog file.
- [Review standards](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#review-standards):
  the C++ Core Guidelines.
- [C++ and Python versions](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#c-and-python-versions):
  the code compiles as both C++17 and C++20.
- [OpenFX specification and ABI](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#openfx-specification-and-abi):
  check the [OpenFX specification](https://openfx.readthedocs.io) (and the
  headers of `openfx/include`) for any code that touches the OFX API, and the
  plug-ins must keep working on OFX 1.4 hosts.
- [Code (plug-ins)](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#code-plug-ins):
  look in openfx-supportext first, and use `OFXS_PI`, never `M_PI`.
- [Tests](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#tests)
  and [Differences between the repositories](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#differences-between-the-repositories).

## Getting the sources

    git clone https://github.com/NatronGitHub/openfx-misc.git
    cd openfx-misc
    git submodule update --init --recursive

Two submodules are needed:

- `openfx/`: the OFX API headers, the C++ Support library (`openfx/Support`),
  HostSupport and the test host `hostDemo`, and the CMake helpers
  (`openfx/cmake/OpenFX.cmake`).
- `SupportExt/` ([openfx-supportext](https://github.com/NatronGitHub/openfx-supportext)):
  helpers shared by many plug-ins (`ofxsMacros.h`, `ofxsCoords.h`,
  `ofxsTransform3x3`, `ofxsGenerator`, `ofxsMaskMix.h`, `ofxsLut`,
  `ofxsMultiPlane`, `ofxsRGBAProcessing.h`, `ofxsNonFinite.h`, the OpenGL
  interacts, `ofxsThreadSuite`...). Before writing a new helper, check whether
  SupportExt has it; a helper that more than one plug-in could use goes there,
  not in `Misc/`.

## Building

CMake is the only build system (3.21 or later; out-of-tree builds only):

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    cmake --install build --prefix <prefix>   # installs into <prefix>/OFX/Plugins

Use `--prefix /usr` on Linux and `--prefix /Library` on macOS for the standard
OpenFX plug-in directories. The main options (all of them are described in the
[README](README.md#using-cmake) and at the top of `CMakeLists.txt`):

| Option | Effect |
|---|---|
| `CMAKE_BUILD_TYPE` | `Release` (default), `Debug` (adds `-Wall -Wextra` and the debug-only plug-ins) or `RelWithDebInfo` |
| `OFX_CXX_STANDARD` | `17` (default) or `20` |
| `OFX_USE_OPENMP` | OpenMP in the CImg plug-ins and DenoiseSharpen (default `ON`) |
| `OFX_PLUGIN_BUNDLES` | `COMBINED` (default: Misc, CImg, Shadertoy) or `SEPARATE` (one bundle per plug-in directory) |
| `OFX_DEPLOY_BUNDLES` | copy the non-system libraries into the bundles at install |
| `BUILD_MISC`, `BUILD_CIMG_PLUGINS`, `BUILD_SHADERTOY` | which bundles to build (default `ON`) |
| `BUILD_TESTING`, `BUILD_DEBUGPROXY` | the Test and DebugProxy bundles (default: `ON` in Debug builds) |
| `FFT`, `OFX_LICENSE` | the FFT of the CImg plug-ins (`pocketfft`, `pow2`, `fftw3`), and `GPL` or `COMMERCIAL` (no FFTW) |
| `USE_OSMESA`, `OSMESA_PATH`, `LLVM_PATH` | Shadertoy CPU rendering with OSMesa |
| `OFX_BUILD_HOSTDEMO` | also build openfx's `hostDemo` into `<prefix>/bin`, for the tests below |
| `OFXMISC_SUPPORTEXT_DIR` | build with another openfx-supportext checkout |

For Xcode or Visual Studio, let CMake generate the project (`-G Xcode`,
`-G "Visual Studio 17 2022"`); there are no hand-written IDE projects.

The CMake build uses the shared helpers of openfx (`add_ofx_plugin`,
`ofx_plugin_bundle_group`, `ofx_plugin_resources`, `ofx_target_openmp`,
`ofx_deploy_bundle`) and SupportExt (`supportext_add_sources`). Don't copy or
fork them: report a missing feature to
[openfx](https://github.com/NatronGitHub/openfx) or
[openfx-supportext](https://github.com/NatronGitHub/openfx-supportext).

## Layout

- **One directory per plug-in** (e.g. `Invert/`), with its sources and its
  icons, named after the plug-in identifier (`net.sf.openfx.Invert.png` and
  `.svg`). A directory may hold several plug-ins (e.g. `Merge/`,
  `ColorTransform/`) and PyPlugs (`.py`, `.gizmo` and their icons).
- **Three combined bundles:**
  - `Misc.ofx.bundle`: every plug-in directory at the top level (its
    `Info.plist` and shared files are in `Misc/`);
  - `CImg.ofx.bundle`: the plug-ins in `CImg/<Name>/` (`CImg<Name>.cpp`), built
    on the `CImgFilter.h` and `CImgOperator.h` base classes and on `CImg.h`;
  - `Shadertoy.ofx.bundle`: Shadertoy (OpenGL, with an optional OSMesa CPU
    renderer).
- **Developer bundles:** `Test/` (TestRender, TestPosition, TestGroups,
  TestOpenGL, TestHostBehaviour: they exercise host features) and
  `DebugProxy/` (wraps the plug-in binary given by `OFX_DEBUGPROXY_BINARY`
  and logs its calls).
- `Templates/`: model plug-ins (SimpleFilter, MixableFilter, MaskableFilter) to
  copy when starting a new one; they are not built.
- `cmake/OFXMiscPlugins.cmake`: the plug-in table (below).
- `README-hosts.txt`: what each host supports, and its quirks.
- `TODO.md`: planned enhancements and missing nodes.

## Adding a plug-in

1. Create its directory, with the sources and the icons
   (`<pluginIdentifier>.png` and `.svg`). Start from a small plug-in of the same
   kind, or from `Templates/`.
2. Add one `ofxmisc_plugin()` line to `cmake/OFXMiscPlugins.cmake`, in its
   bundle's section: its name, `ID` (the plug-in identifier, or the identifier
   prefix plus the directory name if the directory has several plug-ins), the
   SupportExt `COMPONENTS` its sources use, and the flags it needs (`PYPLUGS`,
   `OPENMP`, `FFT`, `LINK_GL`, `DEBUG_ONLY`; see the comment at the top of
   that file). That line alone defines its sources (`<dir>/*.cpp`), its
   resources (`<dir>/*.png`, `.svg`, `.py`, `.gizmo`, `.nps`) and its bundles.
   The configure fails if a directory registers a plug-in but has no line in
   the table.
3. Add it to the plug-in list of `README.md`.
4. Update the plug-in counts that the tests check: `BUNDLES` in
   `.github/tools/smoketest.sh`, and `plugins:` of the "separate bundles" job
   in `.github/workflows/nightly.yml`.

## Plug-in structure

The plug-ins use the OFX C++ Support library (namespace `OFX`):

- Each plug-in defines `kPluginName`, `kPluginGrouping`, `kPluginDescription`,
  `kPluginIdentifier` (e.g. `net.sf.openfx.Invert`), `kPluginVersionMajor` and
  `kPluginVersionMinor`.
- It has an `ImageEffect` subclass (`render`, `isIdentity`,
  `getRegionOfDefinition`, `changedParam`, `changedClip`...), usually with a
  templated `ImageProcessor` specialized by pixel type and number of
  components.
- Its factory is declared with `mDeclarePluginFactory(...)`: `describe` and
  `describeInContext` define the clips and parameters, `createInstance` creates
  the effect. It is registered with
  `static XFactory p(kPluginIdentifier, kPluginVersionMajor, kPluginVersionMinor); mRegisterPluginFactoryInstance(p)`.
  One source file may register several plug-ins (e.g. `Merge/Merge.cpp`).
- A plug-in that keeps an older major version for saved projects uses
  `mDeclarePluginFactoryVersioned(...)`: the factory is a template on the
  major version, its `describe` calls
  `desc.setIsObsolete(majorVersion < kPluginVersionMajor)` (hosts then hide
  the old version from their menus but still load it for old projects), and
  each major is registered (see `AdjustRoD/AdjustRoD.cpp`). The two majors
  share the code; the old one keeps its parameters, defaults and output.
- Parameter descriptions and hints describe the plug-in's own behaviour.

## Versions and saved projects

Saved projects must keep loading and rendering as they did. The rules, shared
with the other plug-in repositories, are in
[Differences between the repositories](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#differences-between-the-repositories)
("Saved projects"). For openfx-misc:

- Compare with the version that shipped in Natron 2.5.0:
  `git show Natron-2.5.0:<file>`. A version that is not in that tag hasn't
  been released, and is fixed in place.
- A bug fix (plainly wrong output) or a new parameter whose default keeps the
  old output: a minor version bump.
- An incompatible change (a parameter removed, renamed or retyped, choice
  options reordered, or a default or output that must change) on a major
  version that shipped in Natron 2.5.0: a new major version, with the old one
  still registered and obsolete, as above.
- New choice options go at the end: some hosts restore a choice by its index.
- Parameters are defined in the order that `tests/nuke/test_param_order.py`
  checks; inserting a new parameter is fine (OFX gives no access by index).

## Compile flags and hosts

The plug-ins are compiled with `OFX_EXTENSIONS_NATRON`, `OFX_EXTENSIONS_NUKE`,
`OFX_EXTENSIONS_VEGAS`, `OFX_EXTENSIONS_TUTTLE` and `OFX_SUPPORTS_OPENGLRENDER`
defined (openfx's CMake options of the same names, all `ON` by default).
The host-specific code (multiview, multiplane, Natron properties) is guarded by
these macros, and must also work on hosts that don't have the extension, or
only part of OFX. `README-hosts.txt` records each host's support and quirks,
each with its source. For example, DaVinci Resolve ignores
`kOfxParamPropDefaultCoordinateSystem`: use SupportExt's
`ofxsNormalisedDefaults.h` for normalised defaults.

Debug builds use `-Wall -Wextra`; new code should compile without warnings.

## CImg.h and openfx-gmic

The CImg plug-ins use the same `CImg.h` as
[openfx-gmic](https://github.com/NatronGitHub/openfx-gmic), which is the source
of truth:

- `CIMGVERSION` in `CImg/CImgVersion.mk` is the CImg commit of the tag of
  openfx-gmic's `GMICVER`.
- `CImg/patches/cimg-*.sh` are verbatim copies of openfx-gmic's
  `patches/cimg-*.sh`, applied in the order of its `CIMG_PATCHES`;
  `CIMG_PATCHES_FROM` records the openfx-gmic commit they come from. Fix or
  update a patch in openfx-gmic first, then copy it here.
- The CMake configure downloads `CImg.h` at that commit, checks its SHA-256,
  and applies the patches.

To check that they still match openfx-gmic's master, run
`.github/tools/check-cimg-patches.sh [REF]` from the top of the source tree
(the weekly CI runs it too). It reads `GMICVER`, `CIMG_PATCHES`,
`POCKETFFTCOMMIT` and `POCKETFFT_SHA256` from openfx-gmic's `gmic_sources.txt`
(its `Makefile` before
[openfx-gmic#263](https://github.com/NatronGitHub/openfx-gmic/pull/263)), and
compares them with `CImg/CImgVersion.mk`. If they
differ, update the version and the patches together, in one pull request. The
default FFT is pocketfft; `pow2` is the fallback, and `fftw3` is opt-in and GPL
only.

## Tests

The tests use openfx's `hostDemo`: configure with `-DOFX_BUILD_HOSTDEMO=ON`
(and `-DBUILD_TESTING=ON` for the Test bundle), then `cmake --install` into a
prefix. Run the tests that cover your change locally; the CI of each pull
request runs the full suites.

- `.github/tools/loadtest.sh <prefix>`: loads every bundle and lists its
  plug-ins (identifier and version), checking for duplicates and missing
  PyPlugs.
- `.github/tools/smoketest.sh [--quick] [--only REGEX] [-j N] [--ofx14] <prefix>`:
  describes and renders every plug-in (every identifier and major version) with
  its default parameters. SupportExt's CI also runs it, so keep its interface
  stable.
- `.github/tools/rendertest.sh <prefix>`: checks the output values of selected
  renders; `RENDERTEST_ONLY=<regex>` runs some of them. Add a case for each fix,
  and a `-m <major>` case for an obsolete major version.
- `tests/nuke`: compares plug-ins with a reference host, pixel by pixel, and
  checks their parameter order; local only, see `tests/nuke/README.md`.
- `.github/tools/bundlediff.sh <dir_a> <dir_b>`: compares two sets of installed
  bundles (files, resources, exported symbols, linked libraries, registered
  plug-ins), e.g. before and after a build change.

The test suites are moving to the `functional/` and `regression/` layout of
the [Tests](https://natron.readthedocs.io/en/rb-2.6/maintainers/guidelines.html#tests)
rules: a functional test checks a contract (the OFX specification, a
parameter's meaning), a regression test reproduces one past bug and names its
issue. Every test says what it checks and why.

## Overlays

- A V1 overlay (`kOfxImageEffectPluginPropOverlayInteractV1`) draws only with
  OpenGL; a V2 overlay (`kOfxImageEffectPluginPropOverlayInteractV2`) draws
  only with the OFX DrawSuite.
- Keep an overlay's OpenGL code, and add a DrawSuite version where possible,
  sharing the drawing code (e.g. through `OFX::DrawArgs::getDrawContext()`).
  Register with `setOverlayInteractDescriptorV2(desc, true)` when `draw()` has
  its own OpenGL path.
- Shadows stay in the OpenGL drawing; DrawSuite elements get their shadows from
  the host.

How Natron uses the two kinds of overlay is described in
[OpenFX host](https://natron.readthedocs.io/en/rb-2.6/maintainers/openfx-host.html).
