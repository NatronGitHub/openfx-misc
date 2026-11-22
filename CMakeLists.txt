# ***** BEGIN LICENSE BLOCK *****
# This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
# Copyright (C) 2013-2018 INRIA
#
# openfx-misc is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# openfx-misc is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with openfx-misc.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
# ***** END LICENSE BLOCK *****
########################################################################
# CMake build of the openfx-misc bundles. It builds what the Makefiles build:
# - Misc.ofx.bundle (Misc/Makefile), CImg.ofx.bundle (CImg/Makefile, BUILD_CIMG_PLUGINS)
#   and Shadertoy.ofx.bundle (Shadertoy/Makefile, BUILD_SHADERTOY);
# - in Debug builds only (make CONFIG=debug): Test.ofx.bundle (Test/Makefile,
#   BUILD_TESTING, also make TESTS=1) and DebugProxy.ofx.bundle (BUILD_DEBUGPROXY);
# with the bundle layout of openfx/Support/Plugins/Makefile.master, assembled in
# <build>/Plugins and installed by "cmake --install" into ${CMAKE_INSTALL_PREFIX}/OFX/Plugins
# (openfx's PLUGIN_INSTALLDIR): use --prefix /usr (Linux) or /Library (macOS) to get the
# directories of "make install".
# -DOFX_PLUGIN_BUNDLES=SEPARATE (openfx's option) builds instead one bundle per plug-in directory,
# as "make nomulti", named after its OFX identifier (e.g. net.sf.openfx.Invert.ofx.bundle): see
# the plug-in table, cmake/OFXMiscPlugins.cmake (adding a plug-in = one line there).
#
# Options:
#   -DCMAKE_BUILD_TYPE=Release|Debug|RelWithDebInfo (default: Release)
#   -DOFX_CXX_STANDARD=17 (default) or 20
#   -DOFX_USE_OPENMP=ON|OFF (default ON): OpenMP in the CImg plug-ins and DenoiseSharpen
#      ("make OPENMP=1"). Apple clang uses MacPorts' libomp (port install libomp).
#   -DOFX_DEPLOY_BUNDLES=ON: at install, copy the non-system libraries into the bundles
#      (macOS: osxDeploy.sh, as "make CONFIG=release" does; needs gsed)
#   -DBUILD_CIMG_PLUGINS=OFF ("make HAVE_CIMG=0"), -DBUILD_SHADERTOY=OFF, -DBUILD_MISC=OFF (e.g.
#      -DBUILD_MISC=OFF -DBUILD_CIMG_PLUGINS=OFF builds only Shadertoy.ofx.bundle)
#   -DOFX_PLUGIN_BUNDLES=COMBINED (default) or SEPARATE: the combined bundles above, or one bundle
#      per plug-in directory ("make nomulti"; cmake/OFXMiscPlugins.cmake), which replaces them (the
#      two sets have the same plug-ins: a host must not load both). With SEPARATE, Debug builds
#      also have Misc.ofx.bundle, with TestRender only (as Test.ofx.bundle).
#   -DOFXMISC_STANDALONE_BUNDLES="DenoiseSharpen;NoOp": with COMBINED, also build these Misc
#      plug-ins as their own bundle (net.sf.openfx.DenoiseSharpen.ofx.bundle, install component
#      DenoiseSharpen; net.sf.openfx.NoOpPlugin.ofx.bundle, component NoOp), for the binary
#      releases. Their plug-ins are also in Misc.ofx.bundle, with the same identifiers and
#      versions: a host must not load both (install one or the other).
#   -DOFXMISC_ARM64X=ARM64|ARM64EC -DOFXMISC_ARM64X_DIR=<dir> (MSVC): Arm64X plug-ins, from an ARM64
#      build then an Arm64EC build (cmake/OFXMiscArm64X.cmake; .github/tools/msvc-build.sh arm64x)
#   -DOFX_LICENSE=GPL (default) or COMMERCIAL ("make LICENSE=...", Natron's NATRON_LICENSE, as in
#      openfx-gmic): COMMERCIAL builds refuse FFT=fftw3 (FFTW is GPL)
#   -DFFT=pocketfft|pow2|fftw3 ("make FFT=..."): the FFT of the CImg plugins: pocketfft
#      (default: header-only, downloaded), pow2 (CImg's own FFT, images padded to a power of two:
#      slower), or fftw3 (opt-in, GPL only: FFTW >= 3.3.5, found with pkg-config)
#   -DBUILD_TESTING=ON|OFF, -DBUILD_DEBUGPROXY=ON|OFF (default: ON in Debug builds)
#   -DUSE_OSMESA=ON ("make -C Shadertoy OSMESA=1"): Shadertoy CPU rendering with a mangled
#      OSMesa in OSMESA_PATH (default /opt/osmesa) and LLVM in LLVM_PATH (default /opt/llvm)
#   -DOSMESA_PKGCONFIG_STATIC=ON (default OFF): with USE_OSMESA and OSMESA_PATH's osmesa.pc, link
#      the static libMangledOSMesa32.a with all the flags of "pkg-config --static" (its
#      Libs.private, e.g. -lsynchronization -lws2_32 on Windows), as the binary releases
#      (release.yml) do with osmesa-install's OSMesa. Off: the flags of "pkg-config --libs" (the
#      osmesa.pc of a Natron SDK may have Libs.private with paths of the machine that built it).
#   -DOSMESA_LICENSES_DIR=<dir> (default empty): with USE_OSMESA, the license texts of the OSMesa
#      and LLVM linked statically into Shadertoy (e.g. osmesa-install's licenses/), installed into
#      Shadertoy.ofx.bundle/Contents/Resources/licenses (the binary releases need them)
#   --prefix /usr (Linux) or /Library (macOS) at install: the bundles go to <prefix>/OFX/Plugins
#   -DPLUGIN_INSTALLDIR=<dir>: where the bundles go, relative to CMAKE_INSTALL_PREFIX (OFX/Plugins)
#   -DOFX_BUILD_HOSTDEMO=ON (default OFF): also build openfx's test host hostDemo (openfx's
#      BUILD_EXAMPLE_HOSTS) and install it into <prefix>/bin, for the CI's render tests
#      (.github/tools/rendertest.sh and smoketest.sh). Needs the system expat (openfx's
#      HostSupport): libexpat1-dev, MSYS2 mingw-w64-ucrt-x86_64-expat, vcpkg expat; on macOS,
#      the SDK's or MacPorts' expat. If expat is missing, openfx's configure error suggests
#      -DOFX_BUILD_HOSTSUPPORT=OFF: here, use -DOFX_BUILD_HOSTDEMO=OFF instead (it sets that)
#   -DOFXMISC_SUPPORTEXT_DIR=<dir>: the openfx-supportext sources to build with (default: the
#      SupportExt submodule), e.g. a SupportExt pull request in SupportExt's CI
########################################################################
IF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
MESSAGE(FATAL_ERROR "Prevented in-tree built. This is bad practice.")
ENDIF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

########################################################################
# Project setup
########################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 3.21...3.31) # 3.21: openfx's cmake/OpenFX.cmake; policies up to 3.31 (CMP0177)
# macOS: the bundles load on macOS 14 and later (Natron 2.6) by default. -DCMAKE_OSX_DEPLOYMENT_TARGET,
# or the MACOSX_DEPLOYMENT_TARGET environment variable (which CMake reads at the first configure),
# overrides it: the default is only set when neither is given (nor a toolchain file or a
# cross-compiling CMAKE_SYSTEM_NAME), as openfx does. It must be set before the first project(),
# which reads it (openfx's cmake/OpenFX.cmake is included later and can't set it).
if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE AND (NOT CMAKE_SYSTEM_NAME OR CMAKE_SYSTEM_NAME STREQUAL "Darwin") AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET AND "$ENV{MACOSX_DEPLOYMENT_TARGET}" STREQUAL "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET 14.0 CACHE STRING "Minimum macOS version")
endif()
PROJECT(OpenFXMisc CXX)

# ========================== build platform ==========================

MESSAGE (STATUS "  Platform:")
MESSAGE (STATUS "    Host:"             ${CMAKE_HOST_SYSTEM_NAME} ${CMAKE_HOST_SYSTEM_VERSION} ${CMAKE_HOST_SYSTEM_PROCESSOR})
if(CMAKE_CROSSCOMPILING)
  MESSAGE (STATUS "    Target:"         ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_VERSION} ${CMAKE_SYSTEM_PROCESSOR})
endif()
MESSAGE (STATUS "    CMake:"            ${CMAKE_VERSION})
MESSAGE (STATUS "    CMake generator:"  ${CMAKE_GENERATOR})
MESSAGE (STATUS "    CMake build tool:" ${CMAKE_BUILD_TOOL})
if(MSVC)
  MESSAGE (STATUS "    MSVC:"           ${MSVC_VERSION})
endif()
if(CMAKE_GENERATOR MATCHES Xcode)
  MESSAGE (STATUS "    Xcode:"          ${XCODE_VERSION})
endif()

get_property(_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type (Release, Debug, RelWithDebInfo)" FORCE)
endif()
if(NOT _multi_config)
  MESSAGE (STATUS "    Configuration:"  ${CMAKE_BUILD_TYPE})
endif()

# Install: the bundles go to ${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}, where PLUGIN_INSTALLDIR
# is openfx's OFX/Plugins (cmake/OpenFX.cmake): "cmake --install build --prefix /usr" installs
# into /usr/OFX/Plugins and "--prefix /Library" into /Library/OFX/Plugins, as "make install".

# The SupportExt sources: the submodule, or another checkout (SupportExt's CI tests its pull
# requests with openfx-misc)
set(OFXMISC_SUPPORTEXT_DIR "${PROJECT_SOURCE_DIR}/SupportExt" CACHE PATH "The openfx-supportext sources (default: the SupportExt submodule)")
get_filename_component(OFXMISC_SUPPORTEXT_DIR "${OFXMISC_SUPPORTEXT_DIR}" ABSOLUTE BASE_DIR "${CMAKE_BINARY_DIR}")
if(NOT OFXMISC_SUPPORTEXT_DIR STREQUAL "${PROJECT_SOURCE_DIR}/SupportExt")
  message(STATUS "    SupportExt:        ${OFXMISC_SUPPORTEXT_DIR}")
endif()

# Check that submodule have been initialized and updated
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include OR NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/cmake/OpenFX.cmake
   OR NOT EXISTS ${OFXMISC_SUPPORTEXT_DIR}/ofxsMacros.h)
  message(FATAL_ERROR
    "\n submodule(s) are missing, please update your repository:\n"
    "  > git submodule update --init --recursive\n")
endif()

# ========================== options ==========================

option(BUILD_MISC "Build the Misc plugins (Misc.ofx.bundle)" ON)
option(BUILD_CIMG_PLUGINS "Build the CImg plugins (CImg.ofx.bundle)" ON)
option(BUILD_SHADERTOY "Build the Shadertoy plugin (Shadertoy.ofx.bundle, needs OpenGL)" ON)
# Debug-only bundles, as "make CONFIG=debug" (Test also with "make TESTS=1"). The defaults
# follow CMAKE_BUILD_TYPE at the first configure; set BUILD_TESTING and BUILD_DEBUGPROXY
# explicitly when changing CMAKE_BUILD_TYPE in an existing build directory.
string(TOUPPER "${CMAKE_BUILD_TYPE}" _build_type)
if(_build_type STREQUAL "DEBUG")
  set(_debug_build ON)
else()
  set(_debug_build OFF)
endif()
option(BUILD_TESTING "Build the Test bundle (host tests, for developers)" ${_debug_build})
option(BUILD_DEBUGPROXY "Build the DebugProxy bundle (wraps the plugin given by OFX_DEBUGPROXY_BINARY)" ${_debug_build})
option(USE_OSMESA "Shadertoy: CPU rendering with OSMesa (OSMESA_PATH) and LLVM (LLVM_PATH)" OFF)
set(OSMESA_PATH "/opt/osmesa" CACHE PATH "USE_OSMESA: OSMesa prefix (mangled OSMesa, libMangledOSMesa32)")
set(LLVM_PATH "/opt/llvm" CACHE PATH "USE_OSMESA: LLVM prefix (bin/llvm-config)")
set(OSMESA_LICENSES_DIR "" CACHE PATH "USE_OSMESA: license texts of OSMesa and LLVM, installed into Shadertoy.ofx.bundle/Contents/Resources/licenses")
option(OSMESA_PKGCONFIG_STATIC "USE_OSMESA: link the static OSMesa with the flags of pkg-config --static (osmesa.pc's Libs.private)" OFF)
option(OFX_BUILD_HOSTDEMO "Build openfx's hostDemo test host and install it into <prefix>/bin (CI render tests)" OFF)

# C++ standard: openfx's OFX_CXX_STANDARD (cmake/OpenFX.cmake), 17 by default (Natron RB-2.6,
# Qt5), or 20 (Qt6, RB-2.7): -DOFX_CXX_STANDARD=20. It is used by add_ofx_plugin and
# openfx::Support, without compiler extensions (-std=c++17, not -std=gnu++17). The Makefiles
# pass no -std: they use the compiler's default, or the -std that Natron passes in CXX.

# ========================== compiler flags ==========================
# Release: CMake's default flags (-O3 -DNDEBUG with GCC and Clang; Makefile.master uses
# -O2 -DNDEBUG). For -O2: -DOFX_OPTFLAG=-O2 (openfx: appended to the plug-ins and
# openfx::Support), or -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" (all targets). Debug: -g, plus DEBUG (add_ofx_plugin) and -Wall -Wextra, as Makefile.master.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Intel")
  add_compile_options($<$<CONFIG:Debug>:-Wall> $<$<CONFIG:Debug>:-Wextra>)
endif()
# openfx::Api adds the MinGW definitions of Makefile.master (_USE_MATH_DEFINES,
# __USE_MINGW_ANSI_STDIO=1) and MSVC's /Zc:__cplusplus.
if(MSVC)
  ADD_COMPILE_OPTIONS(/MP)
  add_compile_definitions(NOMINMAX)
  # If the build system is at least Windows Vista (6.0), compile for Windows Vista. (The CImg
  # plug-ins no longer need it to be interruptible: they use C++11 thread_local, see CImgFilter.h.)
  IF (NOT ${CMAKE_SYSTEM_VERSION} VERSION_LESS 6.0) # Windows Vista
    add_compile_definitions(_WIN32_WINNT=0x0600 WINVER=0x0600)
  ENDIF ()
endif()

# ========================== openfx ==========================
# openfx::Support (the C++ Support library, static), openfx::HostSupport (hostDemo),
# add_ofx_plugin and ARCHDIR (cmake/OpenFX.cmake). The OFX_EXTENSIONS_* (NATRON, NUKE,
# VEGAS, TUTTLE) and OFX_SUPPORTS_OPENGLRENDER options of openfx default to what the
# Makefiles use, and are usage requirements of these targets.
# EXCLUDE_FROM_ALL: only what the bundles link is built, and openfx is not installed.
# HostSupport is only needed by hostDemo (OFX_BUILD_HOSTDEMO, openfx's BUILD_EXAMPLE_HOSTS),
# and it needs the system expat (find_package(EXPAT)); DebugProxy compiles the two HostSupport
# sources it uses. These are openfx options (CMP0077: option() uses these variables).
if(OFX_BUILD_HOSTDEMO)
  set(OFX_BUILD_HOSTSUPPORT ON)
else()
  set(OFX_BUILD_HOSTSUPPORT OFF)
endif()
set(BUILD_EXAMPLE_HOSTS ${OFX_BUILD_HOSTDEMO})
add_subdirectory(openfx EXCLUDE_FROM_ALL)
if(OFX_BUILD_HOSTDEMO AND NOT TARGET hostDemo)
  # openfx skips the example hosts with OFX_SUPPORTS_MULTITHREAD (hostDemo doesn't implement
  # the multi-thread suite)
  message(FATAL_ERROR "OFX_BUILD_HOSTDEMO=ON: openfx did not define the hostDemo target "
    "(hostDemo does not support OFX_SUPPORTS_MULTITHREAD=ON): set OFX_SUPPORTS_MULTITHREAD=OFF "
    "or OFX_BUILD_HOSTDEMO=OFF")
elseif(OFX_BUILD_HOSTDEMO)
  # hostDemo is built with the same OFX_EXTENSIONS_* as the plug-ins (so it renders the two
  # views of a stereo frame), and is installed next to the bundles' OFX directory
  add_custom_target(ofxmisc_hostdemo ALL)
  add_dependencies(ofxmisc_hostdemo hostDemo)
  # openfx's install rules are not used (EXCLUDE_FROM_ALL, and openfx's OFX_INSTALL is OFF).
  # Component hostDemo (each bundle is the component named after its target, e.g. Misc), so
  # that "cmake --install --component" can install it with the bundles (release.yml).
  install(PROGRAMS $<TARGET_FILE:hostDemo> DESTINATION bin COMPONENT hostDemo)
endif()
# add_ofx_plugin, ofx_plugin_resources, ofx_target_openmp, ofx_deploy_bundle and ARCHDIR
# come from openfx/cmake/OpenFX.cmake (included by openfx's CMakeLists.txt).
message(STATUS "    OFX architecture:  ${ARCHDIR}")
message(STATUS "    Install directory: ${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}")

# Linux: link libGL, as the Makefiles (-lGL), not GLVND's libOpenGL and libGLX
if(NOT DEFINED OpenGL_GL_PREFERENCE)
  set(OpenGL_GL_PREFERENCE LEGACY)
endif()
find_package(OpenGL REQUIRED)

# The Windows SDK has GL/gl.h (OpenGL 1.1) but no GL/glext.h (MinGW-w64 and the Linux/Mesa
# GL headers have one): MSVC builds use the copy bundled in GL/ (Khronos glext.h, no
# KHR/khrplatform.h needed), for ColorLookup (Misc), Shadertoy and TestOpenGL (Test).
if(MSVC)
  set(OFXMISC_GLEXT_INCLUDE_DIR ${PROJECT_SOURCE_DIR})
endif()

# Include directory of Makefile.master (ofxsProcessing.H...); SupportExt's comes with
# supportext_add_sources, Misc's (Misc/Makefile) is on the Misc target
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/openfx/Support/Plugins/include)

# supportext_add_sources(): the SupportExt sources of each bundle, compiled with its flags
include(${OFXMISC_SUPPORTEXT_DIR}/cmake/SupportExt.cmake)

########################################################################
# The plug-in bundles: openfx's ofx_plugin_bundle_group(), with the table cmake/OFXMiscPlugins.cmake
########################################################################
# OFX_PLUGIN_BUNDLES (openfx's option) chooses, for each group (Misc, CImg, Shadertoy):
# - COMBINED (default): Misc.ofx.bundle (Misc/Makefile), CImg.ofx.bundle and Shadertoy.ofx.bundle;
# - SEPARATE ("make nomulti"): one bundle per plug-in directory, named after its OFX identifier
#   (net.sf.openfx.Invert.ofx.bundle, net.sf.cimg.CImgBlur.ofx.bundle, ...; see the table), with
#   a generated Info.plist, and the icons and PyPlugs of that directory only. Its target and
#   install component are the table's short name (Invert, CImgBlur, ...).
# SEPARATE replaces the combined bundles (except Misc.ofx.bundle with TestRender alone in Debug
# builds): the two sets register the same identifiers, and a host must not load both.
include(cmake/OFXMiscPlugins.cmake)

########################################################################
# Misc.ofx.bundle (Misc/Makefile)
########################################################################
# OFXMISC_STANDALONE_BUNDLES: with OFX_PLUGIN_BUNDLES=COMBINED, these Misc plug-ins also get their
# own bundle (ALSO_SEPARATE), for the binary releases (release.yml: DenoiseSharpen and NoOp).
# Their plug-ins are also in Misc.ofx.bundle, with the same identifiers and versions: a host must
# not load both (install one or the other).
set(OFXMISC_STANDALONE_BUNDLES "" CACHE STRING
  "Misc plug-ins (names of cmake/OFXMiscPlugins.cmake) also built as their own bundle, e.g. DenoiseSharpen")
if(BUILD_MISC)
  foreach(_name IN LISTS OFXMISC_STANDALONE_BUNDLES)
    if(NOT _name IN_LIST OFXMISC_MISC_NAMES)
      message(FATAL_ERROR "OFXMISC_STANDALONE_BUNDLES: ${_name} is not a Misc plug-in of cmake/OFXMiscPlugins.cmake")
    endif()
  endforeach()
  # Test/TestRender.cpp (#ifdef DEBUG) is also in Misc.ofx.bundle, as in Misc/Makefile, in Debug
  # builds (it compiles to nothing in the others); its own bundle is Test.ofx.bundle
  set(_testrender)
  if(_debug_build)
    set(_testrender PLUGIN TestRender SOURCES Test/TestRender.cpp COMBINED_ONLY
      PROPERTIES OFXMISC_SUPPORTEXT_COMPONENTS THREADS)
  endif()
  ofx_plugin_bundle_group(Misc
    COMBINED_INFO_PLIST Misc/Info.plist
    ALSO_SEPARATE ${OFXMISC_STANDALONE_BUNDLES}
    COMMON_INCLUDE_DIRECTORIES ${PROJECT_SOURCE_DIR}/Misc ${OFXMISC_GLEXT_INCLUDE_DIR} # randomGenerator.H...
    OUT_TARGETS _misc_targets
    ${OFXMISC_MISC_PLUGINS} ${_testrender})
  foreach(_t IN LISTS _misc_targets)
    # openfx::Support and OpenGL::GL come with supportext_add_sources
    get_target_property(_components ${_t} OFXMISC_SUPPORTEXT_COMPONENTS)
    if(NOT _components) # ContactSheet uses no SupportExt source
      set(_components)
    endif()
    supportext_add_sources(${_t} COMPONENTS ${_components})
  endforeach()
endif(BUILD_MISC)

########################################################################
# Shadertoy.ofx.bundle (Shadertoy/Makefile)
########################################################################
if(BUILD_SHADERTOY)
  # one plug-in: the target is Shadertoy in both modes (Shadertoy.ofx.bundle, or
  # net.sf.openfx.Shadertoy.ofx.bundle with OFX_PLUGIN_BUNDLES=SEPARATE); NO_DEPLOY: the OSMesa
  # licences are added below, before ofx_deploy_bundle
  ofx_plugin_bundle_group(Shadertoy COMBINED_INFO_PLIST Shadertoy/Info.plist NO_DEPLOY
    PLUGIN Shadertoy ID net.sf.openfx.Shadertoy
      SOURCES Shadertoy/Shadertoy.cpp Shadertoy/ShadertoyParse.cpp
              Shadertoy/ShadertoyRenderGL.cpp Shadertoy/ShadertoyRenderMesa.cpp
      RESOURCES Shadertoy/net.sf.openfx.Shadertoy.png Shadertoy/net.sf.openfx.Shadertoy.svg
                Shadertoy/presets) # with its contents, as "cp -r presets"
  supportext_add_sources(Shadertoy COMPONENTS THREADS) # as Shadertoy/Makefile
  target_link_libraries(Shadertoy PRIVATE OpenGL::GL ${CMAKE_DL_LIBS}) # dl: eglGetCurrentContext lookup
  # MSVC: GL/glext.h. With USE_OSMESA on MSVC, this bundled (2017) glext.h would come before
  # Mesa's for ShadertoyRenderMesa.cpp; that case is theoretical (Natron's OSMesa builds use MinGW).
  target_include_directories(Shadertoy PRIVATE ${OFXMISC_GLEXT_INCLUDE_DIR})
  if(USE_OSMESA)
    # Shadertoy/Makefile with OSMESA=1: -DHAVE_OSMESA, OSMesa's headers for ShadertoyRenderMesa.cpp
    # only, and -L$(OSMESA_PATH)/lib -lMangledOSMesa32 -lz `llvm-config ...`.
    # Natron's build-plugins.sh may instead give OSMESA_INCLUDES and OSMESA_LIBRARIES (all the
    # link flags, LLVM included).
    target_compile_definitions(Shadertoy PRIVATE HAVE_OSMESA)
    # The OSMesa (and LLVM) link flags go in the imported target OSMesa::OSMesa (SupportExt's
    # convention); its include directories are only used by ShadertoyRenderMesa.cpp, as in
    # Shadertoy/Makefile.
    if(DEFINED OSMESA_LIBRARIES)
      if(NOT DEFINED OSMESA_INCLUDES)
        set(OSMESA_INCLUDES "${OSMESA_PATH}/include")
      endif()
      separate_arguments(_osmesa_libs UNIX_COMMAND "${OSMESA_LIBRARIES}")
      set(_osmesa_includes ${OSMESA_INCLUDES})
    else()
      find_package(PkgConfig)
      if(PKG_CONFIG_FOUND)
        # only OSMESA_PATH's osmesa.pc, not the system's (PKG_CONFIG_LIBDIR replaces the default path)
        set(_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
        set(_pkg_config_libdir "$ENV{PKG_CONFIG_LIBDIR}")
        set(ENV{PKG_CONFIG_PATH} "")
        if(WIN32) # MSYS2's pkgconf is a native program: ';' separator (the paths have drive letters)
          set(ENV{PKG_CONFIG_LIBDIR} "${OSMESA_PATH}/lib/pkgconfig;${OSMESA_PATH}/lib64/pkgconfig")
        else()
          set(ENV{PKG_CONFIG_LIBDIR} "${OSMESA_PATH}/lib/pkgconfig:${OSMESA_PATH}/lib64/pkgconfig")
        endif()
        pkg_check_modules(PC_OSMESA IMPORTED_TARGET osmesa) # not OSMESA_: OSMESA_LIBRARIES is an input
        set(ENV{PKG_CONFIG_PATH} "${_pkg_config_path}")
        set(ENV{PKG_CONFIG_LIBDIR} "${_pkg_config_libdir}")
      endif()
      if(TARGET PkgConfig::PC_OSMESA)
        get_target_property(_osmesa_includes PkgConfig::PC_OSMESA INTERFACE_INCLUDE_DIRECTORIES)
        get_target_property(_osmesa_libs PkgConfig::PC_OSMESA INTERFACE_LINK_LIBRARIES)
        get_target_property(_osmesa_opts PkgConfig::PC_OSMESA INTERFACE_LINK_OPTIONS)
        if(_osmesa_opts)
          list(PREPEND _osmesa_libs ${_osmesa_opts})
        endif()
        if(OSMESA_PKGCONFIG_STATIC)
          # the static library (osmesa-install's default): also its Libs.private (pkg-config
          # --static), e.g. -lsynchronization -lws2_32 on Windows. Only with OSMESA_PKGCONFIG_STATIC
          # (release.yml): a Natron SDK's osmesa.pc may have build-machine paths there.
          if(NOT EXISTS "${OSMESA_PATH}/lib/libMangledOSMesa32.a")
            message(FATAL_ERROR "OSMESA_PKGCONFIG_STATIC: ${OSMESA_PATH}/lib/libMangledOSMesa32.a not found (a static OSMesa is needed)")
          endif()
          if(NOT PC_OSMESA_STATIC_LDFLAGS)
            message(FATAL_ERROR "OSMESA_PKGCONFIG_STATIC: pkg-config --static gave no link flags for ${OSMESA_PATH}'s osmesa.pc")
          endif()
          set(_osmesa_libs ${PC_OSMESA_STATIC_LDFLAGS})
        endif()
      elseif(OSMESA_PKGCONFIG_STATIC)
        message(FATAL_ERROR "OSMESA_PKGCONFIG_STATIC: pkg-config (${PKG_CONFIG_EXECUTABLE}) or a usable osmesa.pc in ${OSMESA_PATH}/lib/pkgconfig was not found (only that prefix is searched, so its Requires must be there too)")
      else()
        message(STATUS "osmesa.pc not found or unusable in ${OSMESA_PATH} (only that prefix is searched, so its Requires must be there too): using ${OSMESA_PATH}/include and -lMangledOSMesa32")
        set(_osmesa_includes "${OSMESA_PATH}/include")
        set(_osmesa_libs "-L${OSMESA_PATH}/lib" -lMangledOSMesa32)
      endif()
      if(NOT EXISTS "${_osmesa_includes}/GL/osmesa.h")
        message(FATAL_ERROR "USE_OSMESA: ${_osmesa_includes}/GL/osmesa.h not found (set OSMESA_PATH)")
      endif()
      # LLVM, as in Shadertoy/Makefile
      set(_llvm_config "${LLVM_PATH}/bin/llvm-config")
      if(WIN32 AND NOT EXISTS "${_llvm_config}" AND EXISTS "${_llvm_config}.exe")
        set(_llvm_config "${_llvm_config}.exe")
      endif()
      if(NOT EXISTS "${_llvm_config}")
        message(FATAL_ERROR "USE_OSMESA: ${_llvm_config} not found (set LLVM_PATH)")
      endif()
      execute_process(COMMAND "${_llvm_config}" --version OUTPUT_VARIABLE _llvm_version
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(_llvm_version VERSION_GREATER_EQUAL 15)
        # Recent Mesa (e.g. 25.0) needs LLVM >= 15 and more LLVM components.
        # --link-static: also link LLVM statically if it was built in shared mode (Homebrew, MacPorts).
        set(_llvm_config_flags --link-static --ldflags --libs bitwriter engine mcdisassembler mcjit core
            executionengine scalaropts transformutils instcombine coroutines native passes --system-libs)
      else()
        set(_llvm_config_flags --ldflags --libs engine mcjit mcdisassembler --system-libs)
      endif()
      execute_process(COMMAND "${_llvm_config}" ${_llvm_config_flags} OUTPUT_VARIABLE _llvm_libs
                      RESULT_VARIABLE _r OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR "USE_OSMESA: ${_llvm_config} ${_llvm_config_flags} failed")
      endif()
      string(REPLACE "\n" " " _llvm_libs "${_llvm_libs}")
      separate_arguments(_llvm_libs UNIX_COMMAND "${_llvm_libs}")
      list(APPEND _osmesa_libs -lz ${_llvm_libs})
      message(STATUS "Shadertoy: OSMesa in ${OSMESA_PATH}, LLVM ${_llvm_version} in ${LLVM_PATH}")
    endif()
    add_library(OSMesa::OSMesa INTERFACE IMPORTED)
    set_target_properties(OSMesa::OSMesa PROPERTIES INTERFACE_LINK_LIBRARIES "${_osmesa_libs}")
    set_property(SOURCE Shadertoy/ShadertoyRenderMesa.cpp APPEND PROPERTY INCLUDE_DIRECTORIES ${_osmesa_includes})
    target_link_libraries(Shadertoy PRIVATE OSMesa::OSMesa)
  endif()
  if(USE_OSMESA AND OSMESA_LICENSES_DIR)
    # the licenses of the statically linked OSMesa and LLVM (before ofx_deploy_bundle, which signs)
    file(GLOB _osmesa_licenses LIST_DIRECTORIES true "${OSMESA_LICENSES_DIR}/*")
    if(NOT _osmesa_licenses)
      message(FATAL_ERROR "OSMESA_LICENSES_DIR: no file in ${OSMESA_LICENSES_DIR}")
    endif()
    ofx_plugin_resources(Shadertoy DESTINATION licenses ${_osmesa_licenses})
  endif()
  ofx_deploy_bundle(Shadertoy)
endif()

########################################################################
# CImg.ofx.bundle (CImg/Makefile)
########################################################################
if(BUILD_CIMG_PLUGINS)
# Licence and FFT backend of the CImg plug-ins, as CImg/license.mk (LICENSE and FFT), with the same
# names, values and rules as openfx-gmic's CMakeLists.txt (OFX_LICENSE and FFT), plus FFT=pow2,
# except that pocketfft is the default for all licences (no dependency, no global FFTW planner
# shared with other plug-ins, and faster than FFTW in our benchmarks).
# OFX_LICENSE: GPL (default), or COMMERCIAL (Natron's NATRON_LICENSE), which does not use FFTW (GPL).
# FFT: the library of the Fourier transforms of ConvolveCImg, ZDefocusCImg and the fft() of
# ExpressionCImg (CImgFFTConvolve.h):
# fftw3 (FFTW, GPL, multithreaded, opt-in), pocketfft (BSD-3-Clause, header-only, downloaded at
# POCKETFFTCOMMIT, through CImg's cimg_use_pocketfft, CImg >= 4.0.8), or pow2 (CImg's own FFT,
# which only handles power-of-two sizes: the images are padded to the next power of two). Empty
# (default): pocketfft. COMMERCIAL builds can't use FFTW.
set(OFX_LICENSE "GPL" CACHE STRING "GPL, or COMMERCIAL (no FFTW)")
set_property(CACHE OFX_LICENSE PROPERTY STRINGS GPL COMMERCIAL)
if(NOT OFX_LICENSE MATCHES "^(GPL|COMMERCIAL)$")
  message(FATAL_ERROR "OFX_LICENSE must be GPL or COMMERCIAL, not \"${OFX_LICENSE}\"")
endif()
set(FFT "" CACHE STRING "FFT of the CImg plug-ins: pocketfft (empty: the default), pow2, or fftw3 (GPL only)")
set_property(CACHE FFT PROPERTY STRINGS "" pocketfft pow2 fftw3)
if(FFT)
  set(CIMG_FFT "${FFT}")
else()
  set(CIMG_FFT pocketfft)
endif()
if(NOT CIMG_FFT MATCHES "^(fftw3|pocketfft|pow2)$")
  message(FATAL_ERROR "FFT must be fftw3, pocketfft or pow2, not \"${CIMG_FFT}\"")
endif()
if(OFX_LICENSE STREQUAL "COMMERCIAL" AND CIMG_FFT STREQUAL "fftw3")
  message(FATAL_ERROR "OFX_LICENSE=COMMERCIAL needs FFT=pocketfft or pow2, not \"${CIMG_FFT}\" (FFTW is GPL)")
endif()

# CImg.h and Inpaint/inpaint.h are not distributed with the sources.
# Download them at the version used by CImg/Makefile (see CImg/CImgVersion.mk)
# if they are missing, as "cd CImg && make sources" would do.
set(CIMG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CImg")
# reconfigure (and download again) when CImgVersion.mk changes, as CImg/Makefile does
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CIMG_DIR}/CImgVersion.mk")
# cimg_mk_var(NAME VAR): the value of the one-line assignment "NAME=value" of CImgVersion.mk
function(cimg_mk_var name var)
  file(STRINGS "${CIMG_DIR}/CImgVersion.mk" _line REGEX "^${name} *=")
  if(NOT _line)
    message(FATAL_ERROR "Could not read ${name} from ${CIMG_DIR}/CImgVersion.mk")
  endif()
  string(REGEX REPLACE "^${name} *= *" "" _value "${_line}")
  string(STRIP "${_value}" _value)
  set(${var} "${_value}" PARENT_SCOPE)
endfunction()
cimg_mk_var(CIMGVERSION CIMGVERSION)
if(NOT CIMGVERSION MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "Could not read CIMGVERSION from ${CIMG_DIR}/CImgVersion.mk")
endif()
cimg_mk_var(CIMG_SHA256 _cimg_sha256)
cimg_mk_var(CIMG_PATCHES_FROM CIMG_PATCHES_FROM)
cimg_mk_var(CIMG_PATCHES _cimg_patches)
set(CIMG_URL "https://raw.githubusercontent.com/dtschump/CImg/${CIMGVERSION}")

function(cimg_download url dest)
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}.tmp" STATUS _status TLS_VERIFY ON)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${dest}.tmp")
    list(GET _status 1 _msg)
    message(FATAL_ERROR "Failed to download ${url}: ${_msg}\n"
      "Download it manually to ${dest} (e.g. \"cd CImg && make sources\"), "
      "or configure with -DBUILD_CIMG_PLUGINS=OFF.")
  endif()
  file(RENAME "${dest}.tmp" "${dest}")
endfunction()

# CImg.h is patched with the scripts of openfx-gmic (CImg/patches, see CImgVersion.mk): CIMG_PATCHES,
# in this order, as CImg/Makefile does, whatever the FFT (CImg.h is the same for every FFT). Each
# runs as "sh <script> <dir>/CImg.h", and fails if CImg.h does not have the expected code. They need
# a POSIX sh, awk, head, tr and wc (Linux, macOS, MSYS2; with MSVC, e.g. Git for Windows' sh.exe in the PATH,
# or -DCIMG_PATCH_SH=<path>).
separate_arguments(_cimg_applied_patches UNIX_COMMAND "${_cimg_patches}")
foreach(_script IN LISTS _cimg_applied_patches)
  if(NOT _script MATCHES "^patches/[A-Za-z0-9_.-]+\\.sh$" OR NOT EXISTS "${CIMG_DIR}/${_script}")
    message(FATAL_ERROR "${CIMG_DIR}/CImgVersion.mk lists '${_script}', not an existing patches/<name>.sh")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CIMG_DIR}/${_script}")
endforeach()

# Download the headers again when CIMGVERSION, CIMG_PATCHES_FROM or the applied patches change, or
# when a patch script is newer than CImg.h: they are recorded in CImg/CImg.h.version (not tracked
# by git), which CImg/Makefile writes in the same format, so that both builds share CImg.h.
string(REPLACE ";" " " _cimg_stamp "${CIMGVERSION} ${CIMG_PATCHES_FROM} ${_cimg_applied_patches}")
set(CIMG_VERSION_STAMP "${CIMG_DIR}/CImg.h.version")
set(_cimg_downloaded "")
if(EXISTS "${CIMG_VERSION_STAMP}")
  file(READ "${CIMG_VERSION_STAMP}" _cimg_downloaded)
  string(STRIP "${_cimg_downloaded}" _cimg_downloaded)
endif()
string(REGEX REPLACE " .*$" "" _cimg_downloaded_version "${_cimg_downloaded}")
if(NOT _cimg_downloaded_version STREQUAL CIMGVERSION)
  file(REMOVE "${CIMG_DIR}/CImg.h" "${CIMG_DIR}/Inpaint/inpaint.h")
elseif(NOT _cimg_downloaded STREQUAL _cimg_stamp)
  file(REMOVE "${CIMG_DIR}/CImg.h")
endif()
foreach(_script IN LISTS _cimg_applied_patches)
  if(EXISTS "${CIMG_DIR}/CImg.h" AND "${CIMG_DIR}/${_script}" IS_NEWER_THAN "${CIMG_DIR}/CImg.h")
    file(REMOVE "${CIMG_DIR}/CImg.h")
  endif()
endforeach()
if(NOT EXISTS "${CIMG_DIR}/Inpaint/inpaint.h")
  find_program(PATCH_EXECUTABLE patch)
  if(NOT PATCH_EXECUTABLE)
    message(FATAL_ERROR "patch is required to prepare CImg/Inpaint/inpaint.h")
  endif()
  cimg_download("${CIMG_URL}/plugins/inpaint.h" "${CIMG_DIR}/Inpaint/inpaint.h")
  execute_process(COMMAND "${PATCH_EXECUTABLE}" -p0 -d "${CIMG_DIR}"
    INPUT_FILE "${CIMG_DIR}/Inpaint/inpaint.h.patch"
    RESULT_VARIABLE _patch_result)
  if(NOT _patch_result EQUAL 0)
    file(REMOVE "${CIMG_DIR}/Inpaint/inpaint.h")
    message(FATAL_ERROR "Failed to apply CImg/Inpaint/inpaint.h.patch")
  endif()
endif()
# The stamp is written before CImg.h, which must be newer than it for CImg/Makefile (CImg.h depends
# on CImg.h.version there): otherwise a later make would download and patch CImg.h again. If the
# download or a patch fails, there is no CImg.h, and the next configure tries again.
if(NOT _cimg_downloaded STREQUAL _cimg_stamp)
  file(WRITE "${CIMG_VERSION_STAMP}" "${_cimg_stamp}\n")
endif()
if(NOT EXISTS "${CIMG_DIR}/CImg.h")
  find_program(CIMG_PATCH_SH sh)
  if(NOT CIMG_PATCH_SH)
    message(FATAL_ERROR "sh not found: it is needed to apply the CImg.h patches (CImg/patches): put "
      "a POSIX sh in the PATH, or configure with -DCIMG_PATCH_SH=<path to sh>")
  endif()
  # patched in a temporary directory (the scripts want a file named CImg.h), and only copied to
  # CImg/CImg.h once all the patches are applied: if one fails, there is no CImg.h
  set(_cimg_tmp "${CMAKE_CURRENT_BINARY_DIR}/cimg-patch")
  file(REMOVE_RECURSE "${_cimg_tmp}")
  file(MAKE_DIRECTORY "${_cimg_tmp}")
  cimg_download("${CIMG_URL}/CImg.h" "${_cimg_tmp}/CImg.h")
  # the download, before the patches, is checked against CIMG_SHA256 (CImgVersion.mk)
  file(SHA256 "${_cimg_tmp}/CImg.h" _sum)
  if(NOT _sum STREQUAL _cimg_sha256)
    file(REMOVE_RECURSE "${_cimg_tmp}")
    message(FATAL_ERROR "checksum mismatch for ${CIMG_URL}/CImg.h: got ${_sum}, expected "
      "CIMG_SHA256=${_cimg_sha256} (CImg/CImgVersion.mk)")
  endif()
  foreach(_script IN LISTS _cimg_applied_patches)
    execute_process(COMMAND "${CIMG_PATCH_SH}" "${CIMG_DIR}/${_script}" "${_cimg_tmp}/CImg.h"
      RESULT_VARIABLE _r)
    if(NOT _r EQUAL 0)
      file(REMOVE_RECURSE "${_cimg_tmp}")
      message(FATAL_ERROR "CImg/${_script} failed on CImg.h ${CIMGVERSION}")
    endif()
  endforeach()
  file(COPY "${_cimg_tmp}/CImg.h" DESTINATION "${CIMG_DIR}")
  file(REMOVE_RECURSE "${_cimg_tmp}")
  # file(COPY) keeps the time of the temporary file: make CImg.h newer than CImg.h.version and
  # Inpaint/inpaint.h, its prerequisites in CImg/Makefile
  file(TOUCH "${CIMG_DIR}/CImg.h")
endif()
# reconfigure if another build (e.g. make, or a build directory with another FFT) changes CImg.h
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CIMG_VERSION_STAMP}")

# pocketfft (FFT=pocketfft): CImg/pocketfft_hdronly.h (next to CImg.h, which includes it when
# cimg_use_pocketfft is defined and cimg_use_fftw3 is not), at POCKETFFTCOMMIT and checked against
# POCKETFFT_SHA256 (CImgVersion.mk), as CImg/Makefile does, and
# its licence (BSD-3-Clause, which asks for its notice in binary distributions), from the header's
# first comment, for the bundle's Resources.
if(CIMG_FFT STREQUAL "pocketfft")
  cimg_mk_var(POCKETFFTCOMMIT _pocketfft_commit)
  cimg_mk_var(POCKETFFT_SHA256 _pocketfft_sha256)
  set(_pocketfft "${CIMG_DIR}/pocketfft_hdronly.h")
  set(_sum "")
  if(EXISTS "${_pocketfft}")
    file(SHA256 "${_pocketfft}" _sum)
  endif()
  if(NOT _sum STREQUAL _pocketfft_sha256)
    set(_url "https://raw.githubusercontent.com/mreineck/pocketfft/${_pocketfft_commit}/pocketfft_hdronly.h")
    cimg_download("${_url}" "${_pocketfft}")
    file(SHA256 "${_pocketfft}" _sum)
    if(NOT _sum STREQUAL _pocketfft_sha256)
      file(REMOVE "${_pocketfft}")
      message(FATAL_ERROR "checksum mismatch for ${_url} (got ${_sum})")
    endif()
  endif()
  file(READ "${_pocketfft}" _text)
  string(FIND "${_text}" "\n*/\n" _end)
  string(SUBSTRING "${_text}" 0 3 _begin)
  if(NOT _begin STREQUAL "/*\n" OR _end LESS 0)
    message(FATAL_ERROR "${_pocketfft}: no licence comment at the top")
  endif()
  math(EXPR _len "${_end} - 2")
  string(SUBSTRING "${_text}" 3 ${_len} _licence) # the lines between "/*" and "*/"
  if(NOT _licence MATCHES "Redistributions in binary form must reproduce")
    message(FATAL_ERROR "${_pocketfft}: the licence comment is not the expected one")
  endif()
  set(CIMG_POCKETFFT_LICENCE "${CMAKE_CURRENT_BINARY_DIR}/generated/Licence_pocketfft.txt")
  file(WRITE "${CIMG_POCKETFFT_LICENCE}.tmp" "${_licence}")
  configure_file("${CIMG_POCKETFFT_LICENCE}.tmp" "${CIMG_POCKETFFT_LICENCE}" COPYONLY)
  file(REMOVE "${CIMG_POCKETFFT_LICENCE}.tmp")
endif()

set(_cimg_fft_licence)
# The settings of every CImg bundle: _cimg_defs, _cimg_libs and _cimg_incs (the group's COMMON_*)
set(_cimg_defs)
set(_cimg_libs)
set(_cimg_incs ${CIMG_DIR})
# The FFT library (FFT), as CImg/CImgFFT.mk: defined for all the sources of the bundle, since they
# share CImg's inline code.
if(CIMG_FFT STREQUAL "fftw3")
  # FFTW, multithreaded (fftw3_threads), as openfx-gmic: CImg only uses its double precision library
  # (libfftw3), and creates its plans under its own lock. Linked with -l flags (pkg-config's
  # LDFLAGS), not full paths, so that -static (MinGW) links the static libraries.
  find_package(PkgConfig)
  # FFTW >= 3.3.5: fftw_make_planner_thread_safe() (cimgFFTLoad(), CImg/CImgFFTConvolve.h), as
  # openfx-gmic (NatronGitHub/openfx-gmic#93)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(FFTW3 "fftw3>=3.3.5")
    if(NOT FFTW3_FOUND)
      pkg_check_modules(_FFTW3_ANY QUIET fftw3)
      if(_FFTW3_ANY_FOUND)
        message(FATAL_ERROR "FFTW ${_FFTW3_ANY_VERSION} is too old: FFT=fftw3 needs FFTW >= 3.3.5 "
          "(fftw_make_planner_thread_safe), or use the default FFT=pocketfft")
      endif()
    endif()
  endif()
  if(FFTW3_FOUND)
    pkg_get_variable(_fftw3_libdir fftw3 libdir)
    find_library(FFTW3_THREADS_LIBRARY fftw3_threads HINTS ${_fftw3_libdir} ${FFTW3_LIBRARY_DIRS})
    if(NOT FFTW3_THREADS_LIBRARY)
      message(FATAL_ERROR "fftw3_threads not found (in ${_fftw3_libdir})")
    endif()
    list(APPEND _cimg_incs ${FFTW3_INCLUDE_DIRS})
    # pkg-config --libs fftw3, with fftw3_threads first (it uses fftw3)
    set(_fftw3_libs ${FFTW3_LDFLAGS})
    list(FILTER _fftw3_libs EXCLUDE REGEX "^-l")
    list(APPEND _cimg_libs ${_fftw3_libs} -lfftw3_threads ${FFTW3_LIBRARIES})
  else()
    # no pkg-config (e.g. MSVC): FFTW's headers and libraries (fftw.org's Windows DLLs include the
    # threads in libfftw3-3)
    find_path(FFTW3_INCLUDE_DIR fftw3.h)
    find_library(FFTW3_LIBRARY NAMES fftw3 libfftw3-3)
    find_library(FFTW3_THREADS_LIBRARY NAMES fftw3_threads)
    if(NOT FFTW3_INCLUDE_DIR OR NOT FFTW3_LIBRARY)
      message(FATAL_ERROR "FFTW3 (FFT=fftw3) not found: install it "
        "(MacPorts: fftw-3, MSYS2: \${MINGW_PACKAGE_PREFIX}-fftw, Ubuntu: libfftw3-dev), or configure "
        "with -DFFT=pocketfft (or pow2)")
    endif()
    list(APPEND _cimg_incs ${FFTW3_INCLUDE_DIR})
    set(_fftw3_check_libs ${FFTW3_LIBRARY})
    if(FFTW3_THREADS_LIBRARY)
      list(PREPEND _fftw3_check_libs ${FFTW3_THREADS_LIBRARY})
    endif()
    # no version from pkg-config: check for fftw_make_planner_thread_safe() (FFTW >= 3.3.5)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${FFTW3_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${_fftw3_check_libs})
    check_cxx_source_compiles("#include <fftw3.h>
int main() { fftw_make_planner_thread_safe(); return 0; }" CIMG_FFTW3_HAS_PLANNER_THREAD_SAFE)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT CIMG_FFTW3_HAS_PLANNER_THREAD_SAFE)
      message(FATAL_ERROR "FFTW in ${FFTW3_INCLUDE_DIR} has no fftw_make_planner_thread_safe(): "
        "FFT=fftw3 needs FFTW >= 3.3.5 with its threads library, or use the default FFT=pocketfft")
    endif()
    list(APPEND _cimg_libs ${_fftw3_check_libs})
  endif()
  list(APPEND _cimg_defs cimg_use_fftw3)
elseif(CIMG_FFT STREQUAL "pocketfft")
  # pocketfft, header-only (CImg/pocketfft_hdronly.h), multithreaded (std::thread) except on Windows
  find_package(Threads REQUIRED)
  list(APPEND _cimg_libs Threads::Threads)
  list(APPEND _cimg_defs cimg_use_pocketfft)
  # Windows (MinGW and MSVC): single-threaded pocketfft, as openfx-gmic (NatronGitHub/openfx-gmic#94).
  # Its static thread pool is destroyed when the host unloads the plug-in (FreeLibrary runs the DLL's
  # static destructors under the loader lock): ~thread_pool() joins the worker threads, which wait
  # for that lock in LdrShutdownThread (deadlock). CImg.h is unchanged; OpenMP is not involved.
  if(WIN32)
    list(APPEND _cimg_defs POCKETFFT_NO_MULTITHREADING)
  endif()
  set(_cimg_fft_licence "${CIMG_POCKETFFT_LICENCE}")
endif()
message(STATUS "CImg plug-ins: ${OFX_LICENSE}, FFT: ${CIMG_FFT}, CImg ${CIMGVERSION} patched as openfx-gmic ${CIMG_PATCHES_FROM}")
# MSVC is OpenMP 2.0 and has issues (https://github.com/dtschump/CImg/issues/208): the table's
# OPENMP is not passed with MSVC. NO_DEPLOY: the FFT licence is added below.
ofx_plugin_bundle_group(CImg
  COMBINED_INFO_PLIST CImg/Info.plist
  COMMON_COMPILE_DEFINITIONS ${_cimg_defs}
  COMMON_LINK_LIBRARIES ${_cimg_libs}
  COMMON_INCLUDE_DIRECTORIES ${_cimg_incs}
  NO_DEPLOY
  OUT_TARGETS _cimg_targets
  ${OFXMISC_CIMG_PLUGINS})
foreach(_t IN LISTS _cimg_targets)
  get_target_property(_components ${_t} OFXMISC_SUPPORTEXT_COMPONENTS)
  supportext_add_sources(${_t} COMPONENTS ${_components}) # LUT: ChromaBlur, HistEQ
  if(NOT MSVC AND TARGET OpenMP::OpenMP_CXX) # MSVC: no OpenMP (the table's OPENMP is not passed)
    target_compile_definitions(${_t} PRIVATE cimg_use_openmp) # CImg/Makefile: OPENMP=1
  endif()
  get_target_property(_fft ${_t} OFXMISC_FFT)
  if(_fft AND _cimg_fft_licence) # the plug-ins that use CImg/CImgFFTConvolve.h
    ofx_plugin_resources(${_t} "${_cimg_fft_licence}")
  endif()
  ofx_deploy_bundle(${_t})
endforeach()
endif(BUILD_CIMG_PLUGINS)

########################################################################
# Test.ofx.bundle (Test/Makefile; make CONFIG=debug or TESTS=1)
########################################################################
if(BUILD_TESTING)
  add_ofx_plugin(Test INFO_PLIST Test/Info.plist SOURCES
    Test/TestRender.cpp Test/TestPosition.cpp Test/TestGroups.cpp Test/TestHostBehaviour.cpp
    Test/TestOpenGL.cpp Test/TestOpenGLRenderGL.cpp Test/TestOpenGLRenderMesa.cpp)
  supportext_add_sources(Test COMPONENTS THREADS TRANSFORM3X3) # + SHUTTER, as Test/Makefile
  target_link_libraries(Test PRIVATE OpenGL::GL)
  target_include_directories(Test PRIVATE ${OFXMISC_GLEXT_INCLUDE_DIR}) # MSVC: GL/glext.h
  ofx_deploy_bundle(Test)
endif()

########################################################################
# DebugProxy.ofx.bundle (DebugProxy/Makefile; make CONFIG=debug)
########################################################################
if(BUILD_DEBUGPROXY)
  # not linked with openfx::Support: it uses HostSupport's ofxhBinary and ofxhUtilities, compiled
  # here as in DebugProxy/Makefile, and not openfx::HostSupport, which would link expat (for its
  # plug-in cache, which DebugProxy doesn't use) into the bundle, and deploy it
  add_ofx_plugin(DebugProxy INFO_PLIST DebugProxy/Info.plist SOURCES DebugProxy/DebugProxy.cpp
    openfx/HostSupport/src/ofxhBinary.cpp openfx/HostSupport/src/ofxhUtilities.cpp)
  target_include_directories(DebugProxy PRIVATE openfx/HostSupport/include
    ${OFXMISC_SUPPORTEXT_DIR}) # ofxsMacros.h
  target_link_libraries(DebugProxy PRIVATE ${CMAKE_DL_LIBS})
  ofx_deploy_bundle(DebugProxy)
endif()

########################################################################
# Arm64X (MSVC, two builds): -DOFXMISC_ARM64X=ARM64|ARM64EC, see cmake/OFXMiscArm64X.cmake
########################################################################
# after the last plug-in target: it applies to all of them
include(cmake/OFXMiscArm64X.cmake)
