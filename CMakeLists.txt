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
IF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
MESSAGE(FATAL_ERROR "Prevented in-tree built. This is bad practice.")
ENDIF(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})

########################################################################
# Project setup
########################################################################
CMAKE_MINIMUM_REQUIRED(VERSION 3.5)
PROJECT(OpenFXMisc CXX)
ENABLE_TESTING()

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
if(NOT CMAKE_GENERATOR MATCHES "Xcode|Visual Studio")
  MESSAGE (STATUS "    Configuration:"  ${CMAKE_BUILD_TYPE})
endif()

include(FindOpenGL REQUIRED)

set (CMAKE_CXX_STANDARD 11)

option(BUILD_CIMG_PLUGINS "Build the CImg plugins (CImg.ofx.bundle)" ON)

# Use -Ofast when applicable (implies -ffast-math)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Wextra -Ofast -DNDEBUG")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Ofast -DNDEBUG")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set (CMAKE_CXX_FLAGS_RELEASE "-Wall -Wextra -Ofast -DNDEBUG")
    set (CMAKE_C_FLAGS_RELEASE   "-Wall -Wextra -Ofast -DNDEBUG")
endif()
SET(OPENFX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/openfx)

# Check that submodule have been initialized and updated
if(NOT EXISTS ${PROJECT_SOURCE_DIR}/openfx/include)
  message(FATAL_ERROR
    "\n submodule(s) are missing, please update your repository:\n"
    "  > git submodule update -i\n")
endif()

# C++ Include directories
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/SupportExt)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/Misc)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/Plugins/include)

# Define "DEBUG" on debug builds
SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

# /MP on MSVC
ADD_COMPILE_OPTIONS($<$<CXX_COMPILER_ID:MSVC>:/MP>)

SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

set(OFX_SUPPORT_HEADER_DIR "${OPENFX_PATH}/Support/include")
set(OFX_SUPPORT_LIBRARY_DIR "${OPENFX_PATH}/Support/Library")

FILE(GLOB SUPPORT_SOURCES
    "${OFX_SUPPORT_HEADER_DIR}/*.h"

    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsCore.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsImageEffect.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsInteract.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsLog.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsMultiThread.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsParams.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsProperty.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/ofxsPropertyValidation.cpp"
    "${OFX_SUPPORT_LIBRARY_DIR}/*.h"
)

#ADD_LIBRARY(Support STATIC )
#TARGET_COMPILE_DEFINITIONS(Support PRIVATE OFX_SUPPORTS_OPENGLRENDER OFX_EXTENSIONS_TUTTLE OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_VEGAS NOMINMAX)

FILE(GLOB MISC_SOURCES
  "Add/*.cpp"
  "AdjustRoD/*.cpp"
  "Anaglyph/*.cpp"
  "AppendClip/*.cpp"
  "Card3D/*.cpp"
  "CheckerBoard/*.cpp"
  "ChromaKeyer/*.cpp"
  "Clamp/*.cpp"
  "ClipTest/*.cpp"
  "ColorBars/*.cpp"
  "ColorCorrect/*.cpp"
  "ColorLookup/*.cpp"
  "ColorMatrix/*.cpp"
  "ColorSuppress/*.cpp"
  "ColorTransform/*.cpp"
  "ColorWheel/*.cpp"
  "Constant/*.cpp"
  "ContactSheet/*.cpp"
  "CopyRectangle/*.cpp"
  "CornerPin/*.cpp"
  "Crop/*.cpp"
  "Deinterlace/*.cpp"
  "DenoiseSharpen/*.cpp"
  "Despill/*.cpp"
  "Difference/*.cpp"
  "DiscBlades/*.cpp"
  "Dissolve/*.cpp"
  "Distortion/*.cpp"
  "FrameBlend/*.cpp"
  "FrameHold/*.cpp"
  "FrameRange/*.cpp"
  "Gamma/*.cpp"
  "GodRays/*.cpp"
  "Grade/*.cpp"
  "HSVTool/*.cpp"
  "HueCorrect/*.cpp"
  "ImageStatistics/*.cpp"
  "Invert/*.cpp"
  "JoinViews/*.cpp"
  "KeyMix/*.cpp"
  "Keyer/*.cpp"
  "LayerContactSheet/*.cpp"
  "Log2Lin/*.cpp"
  "MatteMonitor/*.cpp"
  "Merge/*.cpp"
  "Mirror/*.cpp"
  "MixViews/*.cpp"
  "Multiply/*.cpp"
  "NoOp/*.cpp"
  "OneView/*.cpp"
  "PIK/*.cpp"
  "PLogLin/*.cpp"
  "Position/*.cpp"
  "Premult/*.cpp"
  "Quantize/*.cpp"
  "Radial/*.cpp"
  "Ramp/*.cpp"
  "Rand/*.cpp"
  "ReConverge/*.cpp"
  "Rectangle/*.cpp"
  "Reformat/*.cpp"
  "Retime/*.cpp"
  "Roto/*.cpp"
  "Saturation/*.cpp"
  "Shadertoy/*.cpp"
  "Shadertoy/*.h"
  "Shuffle/*.cpp"
  "SideBySide/*.cpp"
  "SlitScan/*.cpp"
  "SpriteSheet/*.cpp"
  "Switch/*.cpp"
  "Threshold/*.cpp"
  "TimeBlur/*.cpp"
  "TimeBuffer/*.cpp"
  "TimeDissolve/*.cpp"
  "TimeOffset/*.cpp"
  "TrackerPM/*.cpp"
  "Transform/*.cpp"
  "VectorToColor/*.cpp"
  "SupportExt/tinythread.cpp"
  "SupportExt/ofxsThreadSuite.cpp"
  "SupportExt/ofxsFileOpen.cpp"
  "SupportExt/ofxsGenerator.cpp"
  "SupportExt/ofxsLut.cpp"
  "SupportExt/ofxsMultiPlane.cpp"
  "SupportExt/ofxsOGLTextRenderer.cpp"
  "SupportExt/ofxsOGLFontData.cpp"
  "SupportExt/ofxsRamp.cpp"
  "SupportExt/ofxsShutter.cpp"
  "SupportExt/ofxsTracking.cpp"
  "SupportExt/ofxsTransform3x3.cpp"
  "SupportExt/ofxsTransformInteract.cpp"
  "SupportExt/ofxsRectangleInteract.cpp"
  "SupportExt/*.h"
)

FILE(GLOB MISC_RESOURCES
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

ADD_LIBRARY(Misc SHARED ${MISC_SOURCES} ${SUPPORT_SOURCES})
SET_TARGET_PROPERTIES(Misc PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(Misc PROPERTIES SUFFIX ".ofx")

# Add extra flags to the link step of the plugin
if(APPLE)
    set_target_properties(Misc PROPERTIES LINK_FLAGS "-fvisibility=hidden -Wl,-exported_symbols_list,${OFX_SUPPORT_HEADER_DIR}/osxSymbols")
    set_target_properties(Misc PROPERTIES INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
elseif(MINGW)
    set_target_properties(Misc PROPERTIES LINK_FLAGS "-shared -fvisibility=hidden -Xlinker --version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD" OR ${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
# Linux & FreeBSD
    set_target_properties(Misc PROPERTIES LINK_FLAGS "-Wl,-fvisibility=hidden,--version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
    set_target_properties(Misc PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
endif()

TARGET_COMPILE_DEFINITIONS(Misc PRIVATE OFX_EXTENSIONS_VEGAS OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_TUTTLE OFX_SUPPORTS_OPENGLRENDER NOMINMAX)
#TARGET_LINK_LIBRARIES(Misc Support ${OPENGL_gl_LIBRARY})
TARGET_LINK_LIBRARIES(Misc ${OPENGL_gl_LIBRARY} ${CMAKE_DL_LIBS}) # dl: Shadertoy looks up eglGetCurrentContext at runtime

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

FILE(GLOB CIMG_SOURCES
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
  "SupportExt/tinythread.cpp"
  "SupportExt/ofxsThreadSuite.cpp"
#  "SupportExt/ofxsFileOpen.cpp"
#  "SupportExt/ofxsGenerator.cpp"
  "SupportExt/ofxsLut.cpp"
#  "SupportExt/ofxsMultiPlane.cpp"
#  "SupportExt/ofxsOGLTextRenderer.cpp"
#  "SupportExt/ofxsOGLFontData.cpp"
#  "SupportExt/ofxsRamp.cpp"
#  "SupportExt/ofxsShutter.cpp"
#  "SupportExt/ofxsTracking.cpp"
#  "SupportExt/ofxsTransform3x3.cpp"
#  "SupportExt/ofxsTransformInteract.cpp"
#  "SupportExt/ofxsRectangleInteract.cpp"
  "SupportExt/*.h"
)

FILE(GLOB CIMG_RESOURCES
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


ADD_LIBRARY(CImg SHARED ${CIMG_SOURCES} ${SUPPORT_SOURCES})
TARGET_INCLUDE_DIRECTORIES(CImg PRIVATE ${CIMG_DIR})
SET_TARGET_PROPERTIES(CImg PROPERTIES PREFIX "")
SET_TARGET_PROPERTIES(CImg PROPERTIES SUFFIX ".ofx")

# Add extra flags to the link step of the plugin
if(APPLE)
    set_target_properties(CImg PROPERTIES LINK_FLAGS "-fvisibility=hidden -Wl,-exported_symbols_list,${OFX_SUPPORT_HEADER_DIR}/osxSymbols")
    set_target_properties(CImg PROPERTIES INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries")
elseif(WIN32)
    set_target_properties(CImg PROPERTIES LINK_FLAGS "-fvisibility=hidden,--version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
else()
# Linux & FreeBSD
    set_target_properties(CImg PROPERTIES LINK_FLAGS "-Wl,-fvisibility=hidden,--version-script=${OFX_SUPPORT_HEADER_DIR}/linuxSymbols")
    set_target_properties(CImg PROPERTIES INSTALL_RPATH "$ORIGIN/../../Libraries")
endif()

TARGET_COMPILE_DEFINITIONS(CImg PRIVATE OFX_EXTENSIONS_VEGAS OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_TUTTLE OFX_SUPPORTS_OPENGLRENDER NOMINMAX)
#TARGET_LINK_LIBRARIES(CImg Support)
TARGET_LINK_LIBRARIES(CImg)

# Find OpenMP
# To compile on homebrew:
# env CXX=/usr/local/opt/llvm/bin/clang++ CC=/usr/local/opt/llvm/bin/clang LDFLAGS=-L/usr/local/opt/llvm/lib cmake ..
find_package(OpenMP)
# MSVC is OpenMP 2.0 and has issues, see https://github.com/dtschump/CImg/issues/208 and https://msdn.microsoft.com/fr-fr/library/ff520162.aspx
if (OPENMP_FOUND AND NOT MSVC)
#if (OPENMP_FOUND)
    #include_directories("${OPENMP_INCLUDES}")
    #link_directories("${OPENMP_LIBRARIES}")
    # set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    # set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    # set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${OpenMP_SHARED_LINKER_FLAGS}")
    # set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    SET_TARGET_PROPERTIES(CImg PROPERTIES COMPILE_FLAGS "${OpenMP_CXX_FLAGS}")
    SET_TARGET_PROPERTIES(CImg PROPERTIES LINK_FLAGS "${OpenMP_EXE_LINKER_FLAGS}")
    TARGET_LINK_LIBRARIES(CImg ${OpenMP_CXX_LIB_NAMES})
endif(OPENMP_FOUND AND NOT MSVC)
endif(BUILD_CIMG_PLUGINS)

IF (MSVC)
  # Some files require this option. This breaks compatibility with older linkers.
  SET_TARGET_PROPERTIES(Misc PROPERTIES COMPILE_FLAGS "/bigobj")
  # If the build system is at least Windows Vista (6.0),
  # compile for Windows Vista, to get interruptible CImg plugins (see CImgFilter.h)
  IF (NOT ${CMAKE_SYSTEM_VERSION} VERSION_LESS 6.0) # Windows Vista
    ADD_DEFINITIONS(-D_WIN32_WINNT=0x0600 -DWINVER=0x0600)
    MESSAGE(STATUS "CImg plugins should be interruptible")
  ELSE ()
    MESSAGE(STATUS "CImg plugins may not be interruptible")
  ENDIF ()
ENDIF ()

# Bundle architecture folder (OFX packaging spec, ofxPackaging.rst): copied from openfx
# cmake/OpenFX.cmake (NatronGitHub/openfx master c1d69ca3), so that the names match what HostSupport
# looks for and openfx Support/Plugins/Makefile.master. Keep the two in sync.
SET(OFX_ARCH UNKNOWN)
if(APPLE)
    set(OFX_ARCH "MacOS") # universal or arch-specific: no arm64 folder
elseif(WIN32)
    # MSVC: the target architecture (ARM64EC, ARM64, x64, X86); other compilers: pointer size
    string(TOUPPER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID};${CMAKE_GENERATOR_PLATFORM}" _OFX_WINARCH)
    if(NOT CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        # MinGW (MSYS2 CLANGARM64, llvm-mingw) on CMake < 4.1: the architecture ID is only set for MSVC, and
        # CMAKE_SYSTEM_PROCESSOR is the build machine's unless a toolchain file sets it: ask the compiler for
        # its target (CMAKE_CXX_COMPILER_TARGET, or `-dumpmachine`), e.g. aarch64-w64-mingw32 or
        # arm64ec-w64-windows-gnu (llvm-mingw Arm64EC)
        set(_OFX_TARGET "${CMAKE_CXX_COMPILER_TARGET}")
        if(NOT _OFX_TARGET)
            # CMAKE_CXX_COMPILER_ARG1 is one string (e.g. " --target=aarch64-w64-mingw32"): split it
            # (UNIX_COMMAND works with any CMake version, for projects that copy this block)
            separate_arguments(_OFX_CXX_ARGS UNIX_COMMAND "${CMAKE_CXX_COMPILER_ARG1}")
            execute_process(COMMAND ${CMAKE_CXX_COMPILER} ${_OFX_CXX_ARGS} -dumpmachine
                            OUTPUT_VARIABLE _OFX_TARGET OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        endif()
        string(TOLOWER "${_OFX_TARGET}" _OFX_TARGET)
        if(_OFX_TARGET MATCHES "^arm64ec")
            set(_OFX_WINARCH "ARM64EC")
        elseif(_OFX_TARGET MATCHES "^(aarch64|arm64)")
            set(_OFX_WINARCH "ARM64")
        endif()
    endif()
    if(_OFX_WINARCH MATCHES "ARM64EC" OR CMAKE_CXX_FLAGS MATCHES "/arm64EC")
        set(OFX_ARCH "Win-arm64ec") # normative (upstream main, 2026): Arm64EC (and Arm64X) plug-ins
    elseif(_OFX_WINARCH MATCHES "ARM64|AARCH64")
        # CMake >= 4.1 reports "aarch64" as the MinGW architecture ID
        set(OFX_ARCH "Win-arm64")   # reserved, not yet normative: native Arm64 hosts
        message(WARNING "Native Arm64 plug-in: installed to Win-arm64, a reserved, not yet normative folder. "
                        "Current Windows-on-Arm hosts (Resolve, Vegas) are Arm64EC: build for Arm64EC for them.")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OFX_ARCH "Win64")
    else()
        set(OFX_ARCH "Win32")
    endif()
elseif(UNIX)
    if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
        set(_OFX_OS "FreeBSD")
    else()
        set(_OFX_OS "Linux")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _OFX_CPU)
    if(_OFX_CPU MATCHES "^(x86_64|amd64|i[3-6]86|x86)$")
        # x86: use the pointer size, the userland may be 32-bit on a 64-bit kernel
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OFX_ARCH "${_OFX_OS}-x86-64")
        else()
            set(OFX_ARCH "${_OFX_OS}-x86")
        endif()
    elseif(_OFX_OS STREQUAL "Linux" AND NOT _OFX_CPU STREQUAL "")
        # the spec's name, literally: Linux-${uname -m} (CMAKE_SYSTEM_PROCESSOR is uname -m for native builds;
        # cross toolchain files set it). HostSupport also searches the compiled architecture's name.
        set(OFX_ARCH "Linux-${CMAKE_SYSTEM_PROCESSOR}")
    elseif(_OFX_CPU MATCHES "^armv[2-6]" AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OFX_ARCH "FreeBSD-armv6")
    elseif(_OFX_CPU MATCHES "^(aarch64|arm64|armv8l|arm.*)$")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OFX_ARCH "FreeBSD-aarch64")
        else()
            set(OFX_ARCH "FreeBSD-armv7")
        endif()
    elseif(_OFX_OS STREQUAL "FreeBSD" AND _OFX_CPU MATCHES "^riscv")
        set(OFX_ARCH "FreeBSD-riscv64")
    elseif(_OFX_OS STREQUAL "FreeBSD" AND _OFX_CPU MATCHES "^powerpc")
        # uname -p: powerpc64, powerpc64le (uname -m only says powerpc)
        # CMake < 3.20 (TestBigEndian supports CXX-only projects since 3.12)
        if(NOT DEFINED CMAKE_CXX_BYTE_ORDER AND NOT CMAKE_VERSION VERSION_LESS 3.12)
            include(TestBigEndian)
            test_big_endian(_OFX_BIG_ENDIAN)
            if(NOT _OFX_BIG_ENDIAN)
                set(CMAKE_CXX_BYTE_ORDER "LITTLE_ENDIAN")
            endif()
        endif()
        if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND CMAKE_CXX_BYTE_ORDER STREQUAL "LITTLE_ENDIAN")
            set(OFX_ARCH "FreeBSD-powerpc64le")
        elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(OFX_ARCH "FreeBSD-powerpc64")
        else()
            set(OFX_ARCH "FreeBSD-powerpc")
        endif()
    elseif(NOT _OFX_CPU STREQUAL "")
        set(OFX_ARCH "${_OFX_OS}-${CMAKE_SYSTEM_PROCESSOR}") # Linux: uname -m (riscv64, ppc64le, s390x, ...)
    endif()
endif()

SET(OFX_ARCH_NAME ${OFX_ARCH} CACHE STRING "OpenFX target OS and architecture")
IF("${OFX_ARCH_NAME}" STREQUAL "UNKNOWN")
  MESSAGE(SEND_ERROR "OFX_ARCH_NAME was not detected. Install targets will be unavailable. See http://openeffects.org/documentation/reference/ch02s02.html to pick an arch name.")
ELSE()
  INSTALL(TARGETS Misc
    RUNTIME DESTINATION Misc.ofx.bundle/Contents/${OFX_ARCH_NAME}
    LIBRARY DESTINATION Misc.ofx.bundle/Contents/${OFX_ARCH_NAME}
  )

  INSTALL(DIRECTORY "Shadertoy/presets"
    DESTINATION Misc.ofx.bundle/Contents/Resources
  )

  INSTALL(FILES ${MISC_RESOURCES}
    DESTINATION Misc.ofx.bundle/Contents/Resources
  )

  INSTALL(FILES Info.plist
    DESTINATION Misc.ofx.bundle/Contents
  )
  if(BUILD_CIMG_PLUGINS)
    INSTALL(TARGETS CImg
      RUNTIME DESTINATION CImg.ofx.bundle/Contents/${OFX_ARCH_NAME}
      LIBRARY DESTINATION CImg.ofx.bundle/Contents/${OFX_ARCH_NAME}
    )

    INSTALL(FILES ${CIMG_RESOURCES}
      DESTINATION CImg.ofx.bundle/Contents/Resources
    )

    INSTALL(FILES CImg/Info.plist
      DESTINATION CImg.ofx.bundle/Contents
    )
  endif()
ENDIF()
