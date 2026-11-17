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
# <build>/Plugins and installed by "cmake --install" into CMAKE_INSTALL_PREFIX, which
# defaults to the OpenFX plug-in directory (as "make install").
# The "make nomulti" bundles (one per plug-in directory) are only built by make.
#
# Options:
#   -DCMAKE_BUILD_TYPE=Release|Debug|RelWithDebInfo (default: Release)
#   -DOFX_CXX_STANDARD=17 (default) or 20 (-DCMAKE_CXX_STANDARD works too)
#   -DOFX_USE_OPENMP=ON|OFF (default ON): OpenMP in the CImg plug-ins and DenoiseSharpen
#      ("make OPENMP=1"). Apple clang uses MacPorts' libomp (port install libomp).
#   -DOFX_DEPLOY_BUNDLES=ON: at install, copy the non-system libraries into the bundles
#      (macOS: osxDeploy.sh, as "make CONFIG=release" does; needs gsed)
#   -DBUILD_CIMG_PLUGINS=OFF ("make HAVE_CIMG=0"), -DBUILD_SHADERTOY=OFF
#   -DBUILD_TESTING=ON|OFF, -DBUILD_DEBUGPROXY=ON|OFF (default: ON in Debug builds)
#   -DUSE_OSMESA=ON ("make -C Shadertoy OSMESA=1"): Shadertoy CPU rendering with a mangled
#      OSMesa in OSMESA_PATH (default /opt/osmesa) and LLVM in LLVM_PATH (default /opt/llvm)
#   -DPLUGIN_INSTALLDIR=<dir>: where the bundles go, relative to CMAKE_INSTALL_PREFIX
########################################################################
IF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
MESSAGE(FATAL_ERROR "Prevented in-tree built. This is bad practice.")
ENDIF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

########################################################################
# Project setup
########################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 3.16...3.31) # 3.16: openfx's CMakeLists.txt; policies up to 3.31 (CMP0177)
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

# "make install" installs into the OpenFX plug-in directory (Makefile.master PLUGINPATH)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(APPLE)
    set(_ofx_plugin_path "/Library/OFX/Plugins")
  elseif(WIN32)
    set(_ofx_plugin_path "C:/Program Files/Common Files/OFX/Plugins")
  else()
    set(_ofx_plugin_path "/usr/OFX/Plugins")
  endif()
  set(CMAKE_INSTALL_PREFIX "${_ofx_plugin_path}" CACHE PATH "Install prefix (the OpenFX plug-in directory)" FORCE)
endif()
# the bundles go to the root of CMAKE_INSTALL_PREFIX (openfx's default is an absolute path)
set(PLUGIN_INSTALLDIR "." CACHE PATH "Bundle install directory, relative to CMAKE_INSTALL_PREFIX (or absolute)")

# Check that submodule have been initialized and updated
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include OR NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/cmake/OpenFX.cmake
   OR NOT EXISTS ${PROJECT_SOURCE_DIR}/SupportExt/ofxsMacros.h)
  message(FATAL_ERROR
    "\n submodule(s) are missing, please update your repository:\n"
    "  > git submodule update --init --recursive\n")
endif()

# ========================== options ==========================

option(BUILD_CIMG_PLUGINS "Build the CImg plugins (CImg.ofx.bundle)" ON)
option(BUILD_SHADERTOY "Build the Shadertoy plugin (Shadertoy.ofx.bundle, needs OpenGL)" ON)
# Debug-only bundles, as "make CONFIG=debug" (Test also with "make TESTS=1")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_debug_build ON)
else()
  set(_debug_build OFF)
endif()
option(BUILD_TESTING "Build the Test bundle (host tests, for developers)" ${_debug_build})
option(BUILD_DEBUGPROXY "Build the DebugProxy bundle (wraps the plugin given by OFX_DEBUGPROXY_BINARY)" ${_debug_build})
option(USE_OSMESA "Shadertoy: CPU rendering with OSMesa (OSMESA_PATH) and LLVM (LLVM_PATH)" OFF)
set(OSMESA_PATH "/opt/osmesa" CACHE PATH "USE_OSMESA: OSMesa prefix (mangled OSMesa, libMangledOSMesa32)")
set(LLVM_PATH "/opt/llvm" CACHE PATH "USE_OSMESA: LLVM prefix (bin/llvm-config)")

# C++ standard: C++17 by default (Natron RB-2.6 Qt5 builds); the code also builds as C++20
# (Qt6 builds, RB-2.7): configure with -DOFX_CXX_STANDARD=20 (or -DCMAKE_CXX_STANDARD=20).
# add_ofx_plugin and openfx::Support use OFX_CXX_STANDARD, without compiler extensions
# (-std=c++17, as the Makefile CI builds).
if(DEFINED CACHE{CMAKE_CXX_STANDARD})
  set(OFX_CXX_STANDARD "${CMAKE_CXX_STANDARD}" CACHE STRING "C++ standard of the plug-ins (17, 20...)" FORCE)
else()
  set(OFX_CXX_STANDARD 17 CACHE STRING "C++ standard of the plug-ins (17, 20...)")
endif()
set(CMAKE_CXX_STANDARD ${OFX_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON) # fail instead of silently using an older standard

# ========================== compiler flags ==========================
# Release: CMake's default flags (-O3 -DNDEBUG with GCC and Clang; Makefile.master uses
# -O2 -DNDEBUG). For -O2: -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" (or openfx's upcoming
# OFX_OPTFLAG). Debug: -g, plus DEBUG (add_ofx_plugin) and -Wall -Wextra, as Makefile.master.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|Intel")
  add_compile_options($<$<CONFIG:Debug>:-Wall> $<$<CONFIG:Debug>:-Wextra>)
endif()
# openfx::Api adds the MinGW definitions of Makefile.master (_USE_MATH_DEFINES,
# __USE_MINGW_ANSI_STDIO=1) and MSVC's /Zc:__cplusplus.
if(MSVC)
  ADD_COMPILE_OPTIONS(/MP)
  add_compile_definitions(NOMINMAX)
  # If the build system is at least Windows Vista (6.0),
  # compile for Windows Vista, to get interruptible CImg plugins (see CImgFilter.h)
  IF (NOT ${CMAKE_SYSTEM_VERSION} VERSION_LESS 6.0) # Windows Vista
    add_compile_definitions(_WIN32_WINNT=0x0600 WINVER=0x0600)
    MESSAGE(STATUS "CImg plugins should be interruptible")
  ELSE ()
    MESSAGE(STATUS "CImg plugins may not be interruptible")
  ENDIF ()
endif()

# ========================== openfx ==========================
# openfx::Support (the C++ Support library, static), openfx::HostSupport (DebugProxy),
# add_ofx_plugin and ARCHDIR (cmake/OpenFX.cmake). The OFX_EXTENSIONS_* (NATRON, NUKE,
# VEGAS, TUTTLE) and OFX_SUPPORTS_OPENGLRENDER options of openfx default to what the
# Makefiles use, and are usage requirements of these targets.
# EXCLUDE_FROM_ALL: only what the bundles link is built, and openfx is not installed.
# HostSupport is only needed by DebugProxy (openfx's OFX_BUILD_HOSTSUPPORT option;
# CMP0077: option() uses this variable)
set(OFX_BUILD_HOSTSUPPORT ${BUILD_DEBUGPROXY})
# openfx's functions run with openfx's policies (3.16): normalize "./<bundle>" install
# destinations (PLUGIN_INSTALLDIR=.) without a CMP0177 warning
set(CMAKE_POLICY_DEFAULT_CMP0177 NEW)
add_subdirectory(openfx EXCLUDE_FROM_ALL)
# add_ofx_plugin, ofx_plugin_resources, ofx_target_openmp, ofx_deploy_bundle and ARCHDIR
# come from openfx/cmake/OpenFX.cmake (included by openfx's CMakeLists.txt).
message(STATUS "    OFX architecture:  ${ARCHDIR}")
message(STATUS "    Install directory: ${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALLDIR}")

# Linux: link libGL, as the Makefiles (-lGL), not GLVND's libOpenGL and libGLX
if(NOT DEFINED OpenGL_GL_PREFERENCE)
  set(OpenGL_GL_PREFERENCE LEGACY)
endif()
find_package(OpenGL REQUIRED)

# Include directories of Makefile.master and Misc/Makefile
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/SupportExt)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/Misc)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/openfx/Support/Plugins/include)

# supportext_add_sources(): the SupportExt sources of each bundle, compiled with its flags
include(${CMAKE_CURRENT_SOURCE_DIR}/SupportExt/cmake/SupportExt.cmake)

########################################################################
# Misc.ofx.bundle (Misc/Makefile)
########################################################################
# Every plug-in directory listed in Misc/Makefile (all but CImg, Shadertoy and DebugProxy).
# Test only contributes TestRender (debug builds); ReConverge, TestRender and TimeBuffer
# are compiled out in other builds (#ifdef DEBUG).
set(MISC_PLUGIN_DIRS
  Add AdjustRoD Anaglyph AppendClip Card3D CheckerBoard ChromaKeyer Clamp ClipTest
  ColorBars ColorCorrect ColorLookup ColorMatrix ColorSuppress ColorTransform ColorWheel
  Constant ContactSheet CopyRectangle CornerPin Crop Deinterlace DenoiseSharpen Despill
  Difference DiscBlades Dissolve Distortion FrameBlend FrameHold FrameRange Gamma GodRays
  Grade HSVTool HueCorrect ImageStatistics Invert JoinViews KeyMix Keyer LayerContactSheet
  Log2Lin MatteMonitor Merge Mirror MixViews Multiply NoOp OneView PIK PLogLin Position
  Premult Quantize Radial Ramp Rand ReConverge Rectangle Reformat Retime Roto Saturation
  Shuffle SideBySide SlitScan SpriteSheet Switch Threshold TimeBlur TimeBuffer
  TimeDissolve TimeOffset TrackerPM Transform VectorToColor)
set(MISC_SOURCES Misc/randomGenerator.cpp Test/TestRender.cpp)
foreach(_dir IN LISTS MISC_PLUGIN_DIRS)
  file(GLOB _srcs "${_dir}/*.cpp")
  list(APPEND MISC_SOURCES ${_srcs})
endforeach()

add_ofx_plugin(Misc INFO_PLIST Misc/Info.plist SOURCES ${MISC_SOURCES})
# the sources of Misc/Makefile; POSITION_INTERACT: CornerPin, Transform... (header-only)
supportext_add_sources(Misc COMPONENTS THREADS FILEOPEN GENERATOR LUT MULTIPLANE TEXT RAMP SHUTTER
  TRACKING TRANSFORM3X3 TRANSFORM_INTERACT RECTANGLE_INTERACT POSITION_INTERACT)
target_include_directories(Misc PRIVATE ${MISC_PLUGIN_DIRS} Test)
target_link_libraries(Misc PRIVATE openfx::Support OpenGL::GL)
if(NOT MSVC) # MSVC has OpenMP 2.0 only
  ofx_target_openmp(Misc) # DenoiseSharpen (Misc/Makefile: OPENMP=1)
endif()
ofx_plugin_resources(Misc
  "Add/net.sf.openfx.AddPlugin.png"
  "AdjustRoD/net.sf.openfx.AdjustRoDPlugin.png"
  "AdjustRoD/net.sf.openfx.AdjustRoDPlugin.svg"
  "Anaglyph/net.sf.openfx.anaglyphPlugin.png"
  "Anaglyph/net.sf.openfx.anaglyphPlugin.svg"
  "CheckerBoard/net.sf.openfx.CheckerBoardPlugin.png"
  "CheckerBoard/net.sf.openfx.CheckerBoardPlugin.svg"
  "ChromaKeyer/net.sf.openfx.ChromaKeyerPlugin.png"
  "ChromaKeyer/net.sf.openfx.ChromaKeyerPlugin.svg"
  "Clamp/net.sf.openfx.Clamp.png"
  "Clamp/net.sf.openfx.Clamp.svg"
  "ClipTest/net.sf.openfx.ClipTestPlugin.png"
  "ClipTest/net.sf.openfx.ClipTestPlugin.svg"
  "ColorBars/net.sf.openfx.ColorBars.png"
  "ColorBars/net.sf.openfx.ColorBars.svg"
  "ColorCorrect/net.sf.openfx.ColorCorrectPlugin.png"
  "ColorCorrect/net.sf.openfx.ColorCorrectPlugin.svg"
  "ColorLookup/net.sf.openfx.ColorLookupPlugin.png"
  "ColorLookup/net.sf.openfx.ColorLookupPlugin.svg"
  "ColorMatrix/net.sf.openfx.ColorMatrixPlugin.png"
  "ColorMatrix/net.sf.openfx.ColorMatrixPlugin.svg"
  "ColorWheel/net.sf.openfx.ColorWheel.png"
  "ColorWheel/net.sf.openfx.ColorWheel.svg"
  "ColorTransform/net.sf.openfx.HSVToRGB.png"
  "ColorTransform/net.sf.openfx.HSVToRGB.svg"
  "ColorTransform/net.sf.openfx.LabToRGB709.png"
  "ColorTransform/net.sf.openfx.RGBToHSV.png"
  "ColorTransform/net.sf.openfx.RGBToHSV.svg"
  "ColorTransform/net.sf.openfx.RGB709ToLab.png"
  "ColorTransform/net.sf.openfx.RGB709ToXYZ.png"
  "ColorTransform/net.sf.openfx.XYZToRGB709.png"
  "Constant/net.sf.openfx.ConstantPlugin.png"
  "Constant/net.sf.openfx.ConstantPlugin.svg"
  "CornerPin/net.sf.openfx.CornerPinMaskedPlugin.png"
  "CornerPin/net.sf.openfx.CornerPinMaskedPlugin.svg"
  "CornerPin/net.sf.openfx.CornerPinPlugin.png"
  "CornerPin/net.sf.openfx.CornerPinPlugin.svg"
  "Crop/net.sf.openfx.CropPlugin.png"
  "Crop/net.sf.openfx.CropPlugin.svg"
  "Deinterlace/net.sf.openfx.Deinterlace.png"
  "Deinterlace/net.sf.openfx.Deinterlace.svg"
  "Despill/net.sf.openfx.Despill.png"
  "Despill/net.sf.openfx.Despill.svg"
  "Difference/net.sf.openfx.DifferencePlugin.png"
  "Difference/net.sf.openfx.DifferencePlugin.svg"
  "Dissolve/net.sf.openfx.DissolvePlugin.png"
  "Dissolve/net.sf.openfx.DissolvePlugin.svg"
  "Distortion/net.sf.openfx.IDistort.png"
  "Distortion/net.sf.openfx.IDistort.svg"
  "Distortion/net.sf.openfx.STMap.png"
  "Distortion/net.sf.openfx.STMap.svg"
  "Distortion/net.sf.openfx.LensDistortion.png"
  "Distortion/net.sf.openfx.LensDistortion.svg"
  "FrameBlend/net.sf.openfx.FrameBlend.png"
  "FrameBlend/net.sf.openfx.FrameBlend.svg"
  "FrameHold/net.sf.openfx.FrameHold.png"
  "FrameHold/net.sf.openfx.FrameHold.svg"
  "FrameRange/net.sf.openfx.FrameRange.png"
  "FrameRange/net.sf.openfx.FrameRange.svg"
  "Gamma/net.sf.openfx.GammaPlugin.png"
  "GodRays/net.sf.openfx.GodRays.png"
  "GodRays/net.sf.openfx.GodRays.svg"
  "Grade/net.sf.openfx.GradePlugin.png"
  "Grade/net.sf.openfx.GradePlugin.svg"
  "HueCorrect/net.sf.openfx.HueCorrect.png"
  "HueCorrect/net.sf.openfx.HueCorrect.svg"
  "HueCorrect/net.sf.openfx.HueKeyer.png"
  "HueCorrect/net.sf.openfx.HueKeyer.svg"
  "HSVTool/net.sf.openfx.HSVToolPlugin.png"
  "HSVTool/net.sf.openfx.HSVToolPlugin.svg"
  "ImageStatistics/net.sf.openfx.ImageStatistics.png"
  "ImageStatistics/net.sf.openfx.ImageStatistics.svg"
  "Invert/net.sf.openfx.Invert.png"
  "Invert/net.sf.openfx.Invert.svg"
  "JoinViews/net.sf.openfx.joinViewsPlugin.png"
  "JoinViews/net.sf.openfx.joinViewsPlugin.svg"
  "Keyer/net.sf.openfx.KeyerPlugin.png"
  "Keyer/net.sf.openfx.KeyerPlugin.svg"
  "Merge/net.sf.openfx.MergePlugin.png"
  "Merge/net.sf.openfx.MergePlugin.svg"
  "Merge/net.sf.openfx.MergeDifference.png"
  "Merge/net.sf.openfx.MergeIn.png"
  "Merge/net.sf.openfx.MergeMax.png"
  "Merge/net.sf.openfx.MergeMin.png"
  "Merge/net.sf.openfx.MergeMultiply.png"
  "Merge/net.sf.openfx.MergeOut.png"
  "Merge/net.sf.openfx.MergePlus.png"
  "Merge/net.sf.openfx.MergeScreen.png"
  "Mirror/net.sf.openfx.Mirror.png"
  "Mirror/net.sf.openfx.Mirror.svg"
  "MixViews/net.sf.openfx.mixViewsPlugin.png"
  "MixViews/net.sf.openfx.mixViewsPlugin.svg"
  "Multiply/net.sf.openfx.MultiplyPlugin.png"
  "NoOp/net.sf.openfx.NoOpPlugin.png"
  "NoOp/net.sf.openfx.NoOpPlugin.svg"
  "Rand/net.sf.openfx.Noise.png"
  "Rand/net.sf.openfx.Noise.svg"
  "OneView/net.sf.openfx.oneViewPlugin.png"
  "OneView/net.sf.openfx.oneViewPlugin.svg"
  "PIK/net.sf.openfx.PIK.png"
  "PIK/net.sf.openfx.PIK.svg"
  "PIK/fr.inria.PIKColor.png"
  "PIK/fr.inria.PIKColor.svg"
  "PIK/PIKColor.gizmo"
  "PIK/PIKColor.py"
  "Position/net.sf.openfx.Position.png"
  "Position/net.sf.openfx.Position.svg"
  "Premult/net.sf.openfx.Premult.png"
  "Premult/net.sf.openfx.Premult.svg"
  "Premult/net.sf.openfx.Unpremult.png"
  "Premult/net.sf.openfx.Unpremult.svg"
  "Radial/net.sf.openfx.Radial.png"
  "Radial/net.sf.openfx.Radial.svg"
  "Ramp/net.sf.openfx.Ramp.png"
  "Ramp/net.sf.openfx.Ramp.svg"
  "Rectangle/net.sf.openfx.Rectangle.png"
  "Rectangle/net.sf.openfx.Rectangle.svg"
  "Reformat/net.sf.openfx.Reformat.png"
  "Reformat/net.sf.openfx.Reformat.svg"
  "Retime/net.sf.openfx.Retime.png"
  "Retime/net.sf.openfx.Retime.svg"
  "Roto/net.sf.openfx.RotoPlugin.png"
  "Roto/net.sf.openfx.RotoPlugin.svg"
  "Saturation/net.sf.openfx.SaturationPlugin.png"
  "Saturation/net.sf.openfx.SaturationPlugin.svg"
  "Shuffle/net.sf.openfx.ShufflePlugin.png"
  "Shuffle/net.sf.openfx.ShufflePlugin.svg"
  "SideBySide/net.sf.openfx.sideBySidePlugin.png"
  "SideBySide/net.sf.openfx.sideBySidePlugin.svg"
  "SlitScan/net.sf.openfx.SlitScan.png"
  "SlitScan/net.sf.openfx.SlitScan.svg"
  "Switch/net.sf.openfx.switchPlugin.png"
  "Switch/net.sf.openfx.switchPlugin.svg"
  "TimeOffset/net.sf.openfx.timeOffset.png"
  "TimeOffset/net.sf.openfx.timeOffset.svg"
  "TrackerPM/net.sf.openfx.TrackerPM.png"
  "TrackerPM/net.sf.openfx.TrackerPM.svg"
  "Transform/net.sf.openfx.DirBlur.png"
  "Transform/net.sf.openfx.DirBlur.svg"
  "Transform/net.sf.openfx.TransformMaskedPlugin.png"
  "Transform/net.sf.openfx.TransformMaskedPlugin.svg"
  "Transform/net.sf.openfx.TransformPlugin.png"
  "Transform/net.sf.openfx.TransformPlugin.svg"
  "VectorToColor/net.sf.openfx.VectorToColorPlugin.png"
  "VectorToColor/net.sf.openfx.VectorToColorPlugin.svg"
)
ofx_deploy_bundle(Misc)

########################################################################
# Shadertoy.ofx.bundle (Shadertoy/Makefile)
########################################################################
if(BUILD_SHADERTOY)
  add_ofx_plugin(Shadertoy INFO_PLIST Shadertoy/Info.plist SOURCES
    Shadertoy/Shadertoy.cpp Shadertoy/ShadertoyParse.cpp
    Shadertoy/ShadertoyRenderGL.cpp Shadertoy/ShadertoyRenderMesa.cpp)
  supportext_add_sources(Shadertoy COMPONENTS THREADS) # as Shadertoy/Makefile
  target_link_libraries(Shadertoy PRIVATE openfx::Support OpenGL::GL ${CMAKE_DL_LIBS}) # dl: eglGetCurrentContext lookup
  if(USE_OSMESA)
    # Shadertoy/Makefile with OSMESA=1: -DHAVE_OSMESA, OSMesa's headers for ShadertoyRenderMesa.cpp
    # only, and -L$(OSMESA_PATH)/lib -lMangledOSMesa32 -lz `llvm-config ...`.
    # Natron's build-plugins.sh may instead give OSMESA_INCLUDES and OSMESA_LIBRARIES (all the
    # link flags, LLVM included).
    target_compile_definitions(Shadertoy PRIVATE HAVE_OSMESA)
    if(DEFINED OSMESA_LIBRARIES)
      if(NOT DEFINED OSMESA_INCLUDES)
        set(OSMESA_INCLUDES "${OSMESA_PATH}/include")
      endif()
      separate_arguments(_osmesa_libs UNIX_COMMAND "${OSMESA_LIBRARIES}")
      set(_osmesa_includes ${OSMESA_INCLUDES})
    else()
      find_package(PkgConfig)
      if(PKG_CONFIG_FOUND)
        set(_pkg_config_path "$ENV{PKG_CONFIG_PATH}")
        set(ENV{PKG_CONFIG_PATH} "${OSMESA_PATH}/lib/pkgconfig:${OSMESA_PATH}/lib64/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        pkg_check_modules(PC_OSMESA IMPORTED_TARGET osmesa) # not OSMESA_: OSMESA_LIBRARIES is an input
        set(ENV{PKG_CONFIG_PATH} "${_pkg_config_path}")
      endif()
      if(TARGET PkgConfig::PC_OSMESA)
        get_target_property(_osmesa_includes PkgConfig::PC_OSMESA INTERFACE_INCLUDE_DIRECTORIES)
        set(_osmesa_libs PkgConfig::PC_OSMESA)
      else()
        message(STATUS "osmesa.pc not found in ${OSMESA_PATH}: using ${OSMESA_PATH}/include and -lMangledOSMesa32")
        set(_osmesa_includes "${OSMESA_PATH}/include")
        set(_osmesa_libs "-L${OSMESA_PATH}/lib" -lMangledOSMesa32)
      endif()
      if(NOT EXISTS "${_osmesa_includes}/GL/osmesa.h")
        message(FATAL_ERROR "USE_OSMESA: ${_osmesa_includes}/GL/osmesa.h not found (set OSMESA_PATH)")
      endif()
      # LLVM, as in Shadertoy/Makefile
      set(_llvm_config "${LLVM_PATH}/bin/llvm-config")
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
    set_property(SOURCE Shadertoy/ShadertoyRenderMesa.cpp APPEND PROPERTY INCLUDE_DIRECTORIES ${_osmesa_includes})
    target_link_libraries(Shadertoy PRIVATE ${_osmesa_libs})
  endif()
  ofx_plugin_resources(Shadertoy Shadertoy/net.sf.openfx.Shadertoy.png Shadertoy/net.sf.openfx.Shadertoy.svg)
  # the presets directory, as "cp -r presets" in Makefile.master
  file(GLOB _presets LIST_DIRECTORIES false RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/Shadertoy"
       "${CMAKE_CURRENT_SOURCE_DIR}/Shadertoy/presets/*" "${CMAKE_CURRENT_SOURCE_DIR}/Shadertoy/presets/*/*")
  set(_preset_dirs)
  foreach(_p IN LISTS _presets)
    get_filename_component(_d "${_p}" DIRECTORY)
    list(APPEND _preset_dirs "${_d}")
  endforeach()
  list(REMOVE_DUPLICATES _preset_dirs)
  foreach(_d IN LISTS _preset_dirs)
    file(GLOB _files LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/Shadertoy/${_d}/*")
    ofx_plugin_resources(Shadertoy DESTINATION "${_d}" ${_files})
  endforeach()
  ofx_deploy_bundle(Shadertoy)
endif()

########################################################################
# CImg.ofx.bundle (CImg/Makefile)
########################################################################
if(BUILD_CIMG_PLUGINS)
# CImg.h and Inpaint/inpaint.h are not distributed with the sources.
# Download them at the version used by CImg/Makefile (see CImg/CImgVersion.mk)
# if they are missing, as "cd CImg && make CImg.h" would do.
set(CIMG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/CImg")
file(STRINGS "${CIMG_DIR}/CImgVersion.mk" CIMGVERSION REGEX "^CIMGVERSION *=")
string(REGEX REPLACE "^CIMGVERSION *= *([0-9a-fA-F]+).*$" "\\1" CIMGVERSION "${CIMGVERSION}")
if(NOT CIMGVERSION MATCHES "^[0-9a-fA-F]+$")
  message(FATAL_ERROR "Could not read CIMGVERSION from ${CIMG_DIR}/CImgVersion.mk")
endif()
set(CIMG_URL "https://raw.githubusercontent.com/dtschump/CImg/${CIMGVERSION}")

function(cimg_download url dest)
  message(STATUS "Downloading ${url}")
  file(DOWNLOAD "${url}" "${dest}.tmp" STATUS _status TLS_VERIFY ON)
  list(GET _status 0 _code)
  if(NOT _code EQUAL 0)
    file(REMOVE "${dest}.tmp")
    list(GET _status 1 _msg)
    message(FATAL_ERROR "Failed to download ${url}: ${_msg}\n"
      "Download it manually to ${dest} (e.g. \"cd CImg && make CImg.h\"), "
      "or configure with -DBUILD_CIMG_PLUGINS=OFF.")
  endif()
  file(RENAME "${dest}.tmp" "${dest}")
endfunction()

# Download the headers again when CIMGVERSION changes: the downloaded version is
# recorded in CImg/CImg.h.version (not tracked by git).
set(CIMG_VERSION_STAMP "${CIMG_DIR}/CImg.h.version")
if(EXISTS "${CIMG_VERSION_STAMP}")
  file(READ "${CIMG_VERSION_STAMP}" _cimg_downloaded_version)
  string(STRIP "${_cimg_downloaded_version}" _cimg_downloaded_version)
else()
  set(_cimg_downloaded_version "")
endif()
if(NOT _cimg_downloaded_version STREQUAL CIMGVERSION)
  file(REMOVE "${CIMG_DIR}/CImg.h" "${CIMG_DIR}/Inpaint/inpaint.h")
endif()
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
if(NOT EXISTS "${CIMG_DIR}/CImg.h")
  cimg_download("${CIMG_URL}/CImg.h" "${CIMG_DIR}/CImg.h")
endif()
file(WRITE "${CIMG_VERSION_STAMP}" "${CIMGVERSION}\n")

add_ofx_plugin(CImg INFO_PLIST CImg/Info.plist SOURCES
  "CImg/CImg.h"
  "CImg/CImgFilter.cpp"
  "CImg/CImgFFTConvolve.h"
  "CImg/CImgFilter.h"
  "CImg/CImgOperator.cpp"
  "CImg/CImgOperator.h"
  "CImg/Bilateral/CImgBilateral.cpp"
  "CImg/Blur/CImgBlur.cpp"
  "CImg/Blur/blurmaple.map"
  "CImg/Convolve/CImgConvolve.cpp"
  "CImg/Denoise/CImgDenoise.cpp"
  "CImg/Distance/CImgDistance.cpp"
  "CImg/Equalize/CImgEqualize.cpp"
  "CImg/Erode/CImgDilate.cpp"
  "CImg/Erode/CImgErodeDilate.h"
  "CImg/Erode/CImgErode.cpp"
  "CImg/ErodeSmooth/CImgErodeSmooth.cpp"
  "CImg/Expression/CImgExpression.cpp"
  "CImg/Guided/CImgGuided.cpp"
  "CImg/HistEQ/CImgHistEQ.cpp"
  "CImg/Inpaint/CImgInpaint-gpl.cpp"
  "CImg/Inpaint/inpaint.h"
  "CImg/Matrix/CImgMatrix.cpp"
  "CImg/Median/CImgMedian.cpp"
  "CImg/Noise/CImgNoise.cpp"
  "CImg/Plasma/CImgPlasma.cpp"
  "CImg/RollingGuidance/CImgRollingGuidance.cpp"
  "CImg/SharpenInvDiff/CImgSharpenInvDiff.cpp"
  "CImg/SharpenShock/CImgSharpenShock.cpp"
  "CImg/Smooth/CImgSmooth.cpp"
  "CImg/ZDefocus/CImgZDefocus.cpp"
)
supportext_add_sources(CImg COMPONENTS THREADS LUT) # as CImg/Makefile (ofxsLut: ChromaBlur)
target_include_directories(CImg PRIVATE ${CIMG_DIR})
target_link_libraries(CImg PRIVATE openfx::Support)
if(NOT MSVC)
  # MSVC is OpenMP 2.0 and has issues, see https://github.com/dtschump/CImg/issues/208
  # and https://msdn.microsoft.com/fr-fr/library/ff520162.aspx
  ofx_target_openmp(CImg)
  if(TARGET OpenMP::OpenMP_CXX)
    target_compile_definitions(CImg PRIVATE cimg_use_openmp) # CImg/Makefile: OPENMP=1
  endif()
endif()
ofx_plugin_resources(CImg
  "CImg/Bilateral/net.sf.cimg.CImgBilateral.png"
  "CImg/Bilateral/net.sf.cimg.CImgBilateral.svg"
  "CImg/Blur/net.sf.cimg.CImgBlur.png"
  "CImg/Blur/net.sf.cimg.CImgBlur.svg"
  "CImg/Blur/net.sf.cimg.CImgChromaBlur.png"
  "CImg/Blur/net.sf.cimg.CImgChromaBlur.svg"
  "CImg/Blur/net.sf.cimg.CImgBloom.png"
  "CImg/Blur/net.sf.cimg.CImgBloom.svg"
  "CImg/Blur/eu.cimg.EdgeDetect.png"
  "CImg/Blur/eu.cimg.EdgeDetect.svg"
  "CImg/Blur/eu.cimg.EdgeExtend.png"
  "CImg/Blur/eu.cimg.EdgeExtend.svg"
  "CImg/Blur/fr.inria.EdgeBlur.png"
  "CImg/Blur/fr.inria.EdgeBlur.svg"
  "CImg/Blur/net.sf.cimg.CImgLaplacian.png"
  "CImg/Blur/net.sf.cimg.CImgLaplacian.svg"
  "CImg/Blur/net.sf.cimg.CImgSharpen.png"
  "CImg/Blur/net.sf.cimg.CImgSharpen.svg"
  "CImg/Denoise/net.sf.cimg.CImgDenoise.png"
  "CImg/Denoise/net.sf.cimg.CImgDenoise.svg"
  "CImg/Equalize/net.sf.cimg.CImgEqualize.png"
  "CImg/Equalize/net.sf.cimg.CImgEqualize.svg"
  "CImg/Erode/net.sf.cimg.CImgDilate.png"
  "CImg/Erode/net.sf.cimg.CImgDilate.svg"
  "CImg/Erode/net.sf.cimg.CImgErode.png"
  "CImg/Erode/net.sf.cimg.CImgErode.svg"
  "CImg/ErodeSmooth/net.sf.cimg.CImgErodeSmooth.png"
  "CImg/ErodeSmooth/net.sf.cimg.CImgErodeSmooth.svg"
  "CImg/Expression/net.sf.cimg.CImgExpression.png"
  "CImg/Expression/net.sf.cimg.CImgExpression.svg"
  "CImg/Guided/net.sf.cimg.CImgGuided.png"
  "CImg/Guided/net.sf.cimg.CImgGuided.svg"
  "CImg/HistEQ/net.sf.cimg.CImgHistEQ.png"
  "CImg/HistEQ/net.sf.cimg.CImgHistEQ.svg"
  "CImg/Inpaint/eu.cimg.Inpaint.png"
  "CImg/Inpaint/eu.cimg.Inpaint.svg"
  "CImg/Noise/net.sf.cimg.CImgNoise.png"
  "CImg/Noise/net.sf.cimg.CImgNoise.svg"
  "CImg/Plasma/net.sf.cimg.CImgPlasma.png"
  "CImg/Plasma/net.sf.cimg.CImgPlasma.svg"
  "CImg/RollingGuidance/net.sf.cimg.CImgRollingGuidance.png"
  "CImg/RollingGuidance/net.sf.cimg.CImgRollingGuidance.svg"
  "CImg/SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.png"
  "CImg/SharpenInvDiff/net.sf.cimg.CImgSharpenInvDiff.svg"
  "CImg/SharpenShock/net.sf.cimg.CImgSharpenShock.png"
  "CImg/Smooth/net.sf.cimg.CImgSmooth.png"
  "CImg/Smooth/net.sf.cimg.CImgSmooth.svg"
)
ofx_deploy_bundle(CImg)
endif(BUILD_CIMG_PLUGINS)

########################################################################
# Test.ofx.bundle (Test/Makefile; make CONFIG=debug or TESTS=1)
########################################################################
if(BUILD_TESTING)
  add_ofx_plugin(Test INFO_PLIST Test/Info.plist SOURCES
    Test/TestRender.cpp Test/TestPosition.cpp Test/TestGroups.cpp Test/TestHostBehaviour.cpp
    Test/TestOpenGL.cpp Test/TestOpenGLRenderGL.cpp Test/TestOpenGLRenderMesa.cpp)
  supportext_add_sources(Test COMPONENTS THREADS TRANSFORM3X3) # + SHUTTER, as Test/Makefile
  target_link_libraries(Test PRIVATE openfx::Support OpenGL::GL)
  ofx_deploy_bundle(Test)
endif()

########################################################################
# DebugProxy.ofx.bundle (DebugProxy/Makefile; make CONFIG=debug)
########################################################################
if(BUILD_DEBUGPROXY)
  # not linked with openfx::Support: it uses HostSupport's ofxhBinary and ofxhUtilities
  add_ofx_plugin(DebugProxy INFO_PLIST DebugProxy/Info.plist SOURCES DebugProxy/DebugProxy.cpp)
  target_link_libraries(DebugProxy PRIVATE openfx::HostSupport)
  ofx_deploy_bundle(DebugProxy)
endif()
