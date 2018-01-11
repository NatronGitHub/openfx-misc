# ***** BEGIN LICENSE BLOCK *****
# This file is part of openfx-misc <https://github.com/devernay/openfx-misc>,
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
CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
PROJECT(OpenFXMisc CXX)
ENABLE_TESTING()

include(FindOpenGL REQUIRED)

set (CMAKE_CXX_STANDARD 11)

SET(OPENFX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/openfx)

# C++ Include directories
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/SupportExt)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/include)
INCLUDE_DIRECTORIES(${OPENFX_PATH}/Support/Plugins/include)

# /MP on MSVC
ADD_COMPILE_OPTIONS($<$<CXX_COMPILER_ID:MSVC>:/MP>)

FILE(GLOB SUPPORT_SOURCES
    "${OPENFX_PATH}/Support/include/*.h"

    "${OPENFX_PATH}/Support/Library/ofxsCore.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsImageEffect.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsInteract.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsLog.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsMultiThread.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsParams.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsProperty.cpp"
    "${OPENFX_PATH}/Support/Library/ofxsPropertyValidation.cpp"
    "${OPENFX_PATH}/Support/Library/*.h"

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
    "SupportExt/**.h"
)

ADD_LIBRARY(Support STATIC ${SUPPORT_SOURCES})
TARGET_COMPILE_DEFINITIONS(Support PRIVATE OFX_SUPPORTS_OPENGLRENDER OFX_EXTENSIONS_TUTTLE OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_VEGAS NOMINMAX)

FILE(GLOB MISC_SOURCES
  "TimeBuffer/*.cpp"
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
  # "DenoiseSharpen/*.cpp"
  "Despill/*.cpp"
  "Difference/*.cpp"
  "Dissolve/*.cpp"
  "FrameBlend/*.cpp"
  "FrameHold/*.cpp"
  "FrameRange/*.cpp"
  "Gamma/*.cpp"
  "GodRays/*.cpp"
  "Grade/*.cpp"
  "HSVTool/*.cpp"
  "HueCorrect/*.cpp"
  "Distortion/*.cpp"
  "ImageStatistics/*.cpp"
  "Invert/*.cpp"
  "JoinViews/*.cpp"
  "Keyer/*.cpp"
  "KeyMix/*.cpp"
  "LayerContactSheet/*.cpp"
  "Log2Lin/*.cpp"
  "MatteMonitor/*.cpp"
  "Merge/*.cpp"
  "Mirror/*.cpp"
  "Multiply/*.cpp"
  "MixViews/*.cpp"
  "NoOp/*.cpp"
  "OneView/*.cpp"
  "PIK/*.cpp"
  "PLogLin/*.cpp"
  "Position/*.cpp"
  "Premult/*.cpp"
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
  "TimeBlur/*.cpp"
  "TimeDissolve/*.cpp"
  "TimeOffset/*.cpp"
  "TrackerPM/*.cpp"
  "Transform/*.cpp"
  "VectorToColor/*.cpp"
)

FILE(GLOB_RECURSE MISC_RESOURCES
  # Blanket add all svg/png - this is wrong but I'm lazy.
  "*.svg"
  "*.png"
)

ADD_LIBRARY(Misc SHARED ${MISC_SOURCES})
SET_TARGET_PROPERTIES(Misc PROPERTIES SUFFIX ".ofx")
TARGET_COMPILE_DEFINITIONS(Misc PRIVATE OFX_EXTENSIONS_VEGAS OFX_EXTENSIONS_NUKE OFX_EXTENSIONS_NATRON OFX_EXTENSIONS_TUTTLE OFX_SUPPORTS_OPENGLRENDER NOMINMAX)
TARGET_LINK_LIBRARIES(Misc Support ${OPENGL_gl_LIBRARY})

IF (MSVC)
  # Some files require this option. This breaks compatibility with older linkers.
  SET_TARGET_PROPERTIES(Misc PROPERTIES COMPILE_FLAGS "/bigobj")
ENDIF ()

# Find and set the arch name.
# http://openeffects.org/documentation/reference/ch02s02.html
SET(ARCH_NAME UNKNOWN)
IF (WIN32)
  IF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    SET(ARCH_NAME Win64)
  ELSE()
    SET(ARCH_NAME Win32)
  ENDIF()
ELSEIF (APPLE)
  IF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    SET(ARCH_NAME MacOS-x86-64)
  ELSE()
    SET(ARCH_NAME MacOS)
  ENDIF()
ELSEIF (UNIX)
  IF("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
    SET(ARCH_NAME Linux-x86-64)
  ELSE()
    SET(ARCH_NAME Linux-x86)
  ENDIF()
ENDIF ()

SET(OFX_ARCH_NAME ${ARCH_NAME} CACHE STRING "OpenFX target OS and architecture")
IF("${OFX_ARCH_NAME}" EQUAL "UNKNOWN")
  MESSAGE(SEND_ERROR "OFX_ARCH_NAME was not detected. Install targets will be unavailable. See http://openeffects.org/documentation/reference/ch02s02.html to pick an arch name.")
ELSE()
  INSTALL(TARGETS Misc
    RUNTIME DESTINATION Contents/${OFX_ARCH_NAME}
  )

  INSTALL(DIRECTORY "Shadertoy/presets"
    DESTINATION Contents/Resources
  )

  INSTALL(FILES ${MISC_RESOURCES}
    DESTINATION Contents/Resources
  )

  INSTALL(FILES Info.plist
    DESTINATION Contents
  )
ENDIF()