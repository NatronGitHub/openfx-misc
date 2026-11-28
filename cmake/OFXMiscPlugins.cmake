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
# The plug-in table: one ofxmisc_plugin() line per plug-in directory, i.e. per bundle of
# OFX_PLUGIN_BUNDLES=SEPARATE (openfx's ofx_plugin_bundle_group). Adding a plug-in: one line here
# (and its entry in the plug-in list of README.md). This table alone defines the sources, the
# resources and the bundles: the separate bundles get a generated Info.plist, the combined ones
# Misc/Info.plist and CImg/Info.plist (CMakeLists.txt).
#
#   ofxmisc_plugin(<name> [DIR <dir>] ID <ofx id> [COMPONENTS <SupportExt component>...]
#                  [SOURCES <extra source>...] [PYPLUGS <resource>...] [OPENMP] [DEBUG_ONLY] [FFT]
#                  [LINK_GL] [NO_ID_CHECK])
#
# - <name>: the CMake target and install component of its separate bundle (DIR defaults to it);
# - ID: names the separate bundle (<ID>.ofx.bundle): the plug-in's OFX identifier, as it is, if
#   the directory registers one plug-in; otherwise the prefix of its plug-ins' identifiers plus
#   the directory name (CImg: CImg<dir>), e.g. net.sf.openfx.Merge, net.sf.cimg.CImgBlur;
# - sources: <dir>/*.cpp plus SOURCES; resources: <dir>/*.{png,svg,py,gizmo,nps}, i.e. the icons
#   (<pluginIdentifier>.png/.svg) of the plug-ins of <dir> and its PyPlugs (in both modes: the
#   combined bundle gets the union);
# - PYPLUGS: the resources of <dir> (file names) that go to Contents/Resources/PyPlugs instead of
#   Contents/Resources, where Natron loads the PyPlugs of a bundle: a PyPlug's .py, its .gizmo and
#   its icons (Natron finds the icon of a PyPlug next to its .py);
# - COMPONENTS: the SupportExt components (supportext_add_sources) that its sources use;
# - DEBUG_ONLY: only compiled in debug builds (#ifdef DEBUG): no bundle in other builds;
# - NO_ID_CHECK: an ID (prefix + dir) that no plug-in identifier of the sources starts with;
# - LINK_GL: its own code calls OpenGL (an overlay), without a SupportExt component that links it;
# - FFT: uses CImg/CImgFFTConvolve.h (its bundle gets the FFT licence, e.g. Licence_pocketfft.txt);
# - OPENMP: compiled with OpenMP (not with MSVC), plus the SupportExt component OPENMP
#   (ofxsOpenMP.cpp, OFX::ofxsPinOpenMPRuntime(); property OFXMISC_OPENMP);
# - the CImg ones (DIR CImg/...) also get CImg/CImgFilter.cpp and OPENMP.
# Group: the first plug-ins go to Misc.ofx.bundle, the CImg ones (DIR CImg/...) to CImg.ofx.bundle.
########################################################################
# ofxmisc_check_table() (end of this file) stops the configure if a directory registers a plug-in
# (mRegisterPluginFactoryInstance in one of its .cpp files) but has no line in the table.
#
# ofxmisc_plugin(): appends "PLUGIN <name> ..." to OFXMISC_<group>_PLUGINS (the arguments of
# ofx_plugin_bundle_group) and its name to OFXMISC_<group>_NAMES
function(ofxmisc_plugin NAME)
  cmake_parse_arguments(PARSE_ARGV 1 _p "OPENMP;DEBUG_ONLY;FFT;LINK_GL;NO_ID_CHECK" "DIR;ID" "COMPONENTS;SOURCES;PYPLUGS")
  if(_p_UNPARSED_ARGUMENTS OR NOT _p_ID)
    message(FATAL_ERROR "ofxmisc_plugin(${NAME}): bad arguments (ID is required): ${_p_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT _p_DIR)
    set(_p_DIR ${NAME})
  endif()
  set_property(GLOBAL APPEND PROPERTY OFXMISC_TABLE_DIRS ${_p_DIR}) # for ofxmisc_check_table()
  foreach(_f IN LISTS _p_PYPLUGS)
    if(NOT EXISTS "${PROJECT_SOURCE_DIR}/${_p_DIR}/${_f}")
      message(FATAL_ERROR "ofxmisc_plugin(${NAME}): PYPLUGS ${_f} is not in ${_p_DIR}")
    endif()
  endforeach()
  if(_p_DEBUG_ONLY AND NOT _debug_build)
    return()
  endif()
  if(_p_DIR MATCHES "^CImg/")
    set(_group CIMG)
    set(_p_OPENMP TRUE) # CImg.ofx.bundle uses OpenMP (cimg_use_openmp)
    list(APPEND _p_SOURCES CImg/CImgFilter.cpp) # the CImg plug-ins' base class
  else()
    set(_group MISC)
  endif()
  file(GLOB _srcs "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.cpp")
  file(GLOB _res LIST_DIRECTORIES false
    "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.png" "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.svg"
    "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.py" "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.gizmo"
    "${PROJECT_SOURCE_DIR}/${_p_DIR}/*.nps")
  list(TRANSFORM _p_SOURCES PREPEND "${PROJECT_SOURCE_DIR}/")
  # no INCLUDE_DIRECTORIES: #include "..." finds the headers next to each source
  set(_args PLUGIN ${NAME} ID ${_p_ID} SOURCES ${_srcs} ${_p_SOURCES})
  if(_res)
    list(APPEND _args RESOURCES)
    foreach(_r IN LISTS _res)
      list(APPEND _args "${_r}")
      get_filename_component(_rn "${_r}" NAME)
      if(_rn IN_LIST _p_PYPLUGS)
        list(APPEND _args DESTINATION PyPlugs)
      endif()
    endforeach()
  endif()
  if(_p_OPENMP)
    # SupportExt's OPENMP component (ofxsOpenMP.cpp: OFX::ofxsPinOpenMPRuntime(), called at render
    # entry, a no-op without OpenMP), added by CMakeLists.txt; not in OFXMISC_SUPPORTEXT_COMPONENTS,
    # since OPENMP is a keyword of ofx_plugin_bundle_group, which would end the PROPERTIES values
    list(APPEND _args PROPERTIES OFXMISC_OPENMP ON)
  endif()
  if(_p_OPENMP AND NOT MSVC) # MSVC has OpenMP 2.0 only
    list(APPEND _args OPENMP)
  endif()
  if(_p_COMPONENTS)
    list(APPEND _args PROPERTIES OFXMISC_SUPPORTEXT_COMPONENTS ${_p_COMPONENTS})
    # (not SUPPORTEXT_COMPONENTS: supportext_add_sources records the components it added there)
  endif()
  if(_p_NO_ID_CHECK) # a prefix + dir ID that no identifier of its sources starts with
    list(APPEND _args NO_ID_CHECK)
  endif()
  if(_p_LINK_GL) # calls OpenGL itself (the SupportExt interacts and TEXT link it too)
    list(APPEND _args LINK_LIBRARIES OpenGL::GL)
  endif()
  if(_p_FFT) # its bundle gets the FFT library's licence (CMakeLists.txt)
    list(APPEND _args PROPERTIES OFXMISC_FFT ON)
  endif()
  set(OFXMISC_${_group}_PLUGINS ${OFXMISC_${_group}_PLUGINS} ${_args} PARENT_SCOPE)
  set(OFXMISC_${_group}_NAMES ${OFXMISC_${_group}_NAMES} ${NAME} PARENT_SCOPE)
endfunction()

# Misc.ofx.bundle
ofxmisc_plugin(Add ID net.sf.openfx.AddPlugin COMPONENTS THREADS)
ofxmisc_plugin(AdjustRoD ID net.sf.openfx.AdjustRoDPlugin COMPONENTS THREADS)
ofxmisc_plugin(Anaglyph ID net.sf.openfx.anaglyphPlugin COMPONENTS THREADS)
ofxmisc_plugin(AppendClip ID net.sf.openfx.AppendClip COMPONENTS THREADS)
ofxmisc_plugin(Card3D ID net.sf.openfx.Card3D COMPONENTS THREADS TRANSFORM3X3 SHUTTER FILEOPEN)
ofxmisc_plugin(CheckerBoard ID net.sf.openfx.CheckerBoardPlugin COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR)
ofxmisc_plugin(ChromaKeyer ID net.sf.openfx.ChromaKeyerPlugin COMPONENTS THREADS LUT)
ofxmisc_plugin(Clamp ID net.sf.openfx.Clamp COMPONENTS THREADS)
ofxmisc_plugin(ClipTest ID net.sf.openfx.ClipTestPlugin COMPONENTS THREADS)
ofxmisc_plugin(ColorBars ID net.sf.openfx.ColorBars COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR LUT)
ofxmisc_plugin(ColorCorrect ID net.sf.openfx.ColorCorrectPlugin COMPONENTS THREADS)
ofxmisc_plugin(ColorLookup ID net.sf.openfx.ColorLookupPlugin COMPONENTS THREADS LINK_GL)
ofxmisc_plugin(ColorMatrix ID net.sf.openfx.ColorMatrixPlugin COMPONENTS THREADS)
ofxmisc_plugin(ColorSuppress ID net.sf.openfx.ColorSuppress COMPONENTS THREADS)
ofxmisc_plugin(ColorTransform ID net.sf.openfx.ColorTransform COMPONENTS THREADS LUT NO_ID_CHECK)
ofxmisc_plugin(ColorWheel ID net.sf.openfx.ColorWheel COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR LUT)
ofxmisc_plugin(Constant ID net.sf.openfx.Constant COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR)
ofxmisc_plugin(ContactSheet ID net.sf.openfx.ContactSheetOFX LINK_GL)
ofxmisc_plugin(CopyRectangle ID net.sf.openfx.CopyRectanglePlugin COMPONENTS THREADS RECTANGLE_INTERACT)
ofxmisc_plugin(CornerPin ID net.sf.openfx.CornerPin COMPONENTS THREADS TRANSFORM3X3 TEXT SHUTTER)
ofxmisc_plugin(Crop ID net.sf.openfx.CropPlugin COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR)
ofxmisc_plugin(Deinterlace ID net.sf.openfx.Deinterlace COMPONENTS THREADS)
ofxmisc_plugin(DenoiseSharpen ID net.sf.openfx.DenoiseSharpen COMPONENTS THREADS LUT RECTANGLE_INTERACT OPENMP)
ofxmisc_plugin(Despill ID net.sf.openfx.Despill COMPONENTS THREADS)
ofxmisc_plugin(Difference ID net.sf.openfx.DifferencePlugin COMPONENTS THREADS)
ofxmisc_plugin(DiscBlades ID net.sf.openfx.DiscBlades COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR)
ofxmisc_plugin(Dissolve ID net.sf.openfx.DissolvePlugin COMPONENTS THREADS)
ofxmisc_plugin(Distortion ID net.sf.openfx.Distortion COMPONENTS THREADS TEXT MULTIPLANE RECTANGLE_INTERACT GENERATOR NO_ID_CHECK)
ofxmisc_plugin(FrameBlend ID net.sf.openfx.FrameBlend COMPONENTS THREADS)
ofxmisc_plugin(FrameHold ID net.sf.openfx.FrameHold COMPONENTS THREADS)
ofxmisc_plugin(FrameRange ID net.sf.openfx.FrameRange COMPONENTS THREADS)
ofxmisc_plugin(Gamma ID net.sf.openfx.GammaPlugin COMPONENTS THREADS)
ofxmisc_plugin(GodRays ID net.sf.openfx.GodRays COMPONENTS THREADS TRANSFORM3X3 TRANSFORM_INTERACT SHUTTER)
ofxmisc_plugin(Grade ID net.sf.openfx.GradePlugin COMPONENTS THREADS)
ofxmisc_plugin(HSVTool ID net.sf.openfx.HSVToolPlugin COMPONENTS THREADS LUT RECTANGLE_INTERACT)
ofxmisc_plugin(HueCorrect ID net.sf.openfx.HueCorrect COMPONENTS THREADS LUT LINK_GL)
ofxmisc_plugin(ImageStatistics ID net.sf.openfx.ImageStatistics COMPONENTS THREADS LUT RECTANGLE_INTERACT)
ofxmisc_plugin(Invert ID net.sf.openfx.Invert COMPONENTS THREADS)
ofxmisc_plugin(JoinViews ID net.sf.openfx.joinViewsPlugin COMPONENTS THREADS)
ofxmisc_plugin(Keyer ID net.sf.openfx.KeyerPlugin COMPONENTS THREADS)
ofxmisc_plugin(KeyMix ID net.sf.openfx.KeyMix COMPONENTS THREADS)
ofxmisc_plugin(LayerContactSheet ID net.sf.openfx.LayerContactSheetOFX COMPONENTS TEXT)
ofxmisc_plugin(LayerRename ID net.sf.openfx.LayerRename COMPONENTS THREADS MULTIPLANE)
ofxmisc_plugin(Log2Lin ID net.sf.openfx.Log2Lin COMPONENTS THREADS LUT)
ofxmisc_plugin(MatteMonitor ID net.sf.openfx.MatteMonitorPlugin COMPONENTS THREADS)
ofxmisc_plugin(Merge ID net.sf.openfx.Merge COMPONENTS THREADS MULTIPLANE)
ofxmisc_plugin(Mirror ID net.sf.openfx.Mirror COMPONENTS THREADS)
ofxmisc_plugin(MixViews ID net.sf.openfx.mixViewsPlugin COMPONENTS THREADS)
ofxmisc_plugin(Multiply ID net.sf.openfx.MultiplyPlugin COMPONENTS THREADS)
ofxmisc_plugin(NoOp ID net.sf.openfx.NoOpPlugin COMPONENTS THREADS GENERATOR RECTANGLE_INTERACT)
ofxmisc_plugin(OneView ID net.sf.openfx.oneViewPlugin COMPONENTS THREADS)
ofxmisc_plugin(PIK ID net.sf.openfx.PIK COMPONENTS THREADS LUT
  PYPLUGS PIKColor.py PIKColor.gizmo fr.inria.PIKColor.png fr.inria.PIKColor.svg)
ofxmisc_plugin(PLogLin ID net.sf.openfx.PLogLin COMPONENTS THREADS)
ofxmisc_plugin(Position ID net.sf.openfx.Position COMPONENTS THREADS TEXT)
ofxmisc_plugin(Premult ID net.sf.openfx.Premult COMPONENTS THREADS MULTIPLANE)
ofxmisc_plugin(Quantize ID net.sf.openfx.Quantize COMPONENTS THREADS SOURCES Misc/randomGenerator.cpp)
ofxmisc_plugin(Radial ID net.sf.openfx.Radial COMPONENTS THREADS GENERATOR RECTANGLE_INTERACT)
ofxmisc_plugin(Ramp ID net.sf.openfx.Ramp COMPONENTS THREADS TEXT RAMP)
ofxmisc_plugin(Rand ID net.sf.openfx.Noise COMPONENTS THREADS GENERATOR RECTANGLE_INTERACT SOURCES Misc/randomGenerator.cpp)
ofxmisc_plugin(ReConverge ID net.sf.openfx.reConvergePlugin COMPONENTS THREADS TEXT)
ofxmisc_plugin(Rectangle ID net.sf.openfx.Rectangle COMPONENTS THREADS GENERATOR RECTANGLE_INTERACT)
ofxmisc_plugin(Reformat ID net.sf.openfx.Reformat COMPONENTS THREADS TRANSFORM3X3 TRANSFORM_INTERACT SHUTTER)
ofxmisc_plugin(Retime ID net.sf.openfx.Retime COMPONENTS THREADS)
ofxmisc_plugin(Roto ID net.sf.openfx.RotoPlugin COMPONENTS THREADS)
ofxmisc_plugin(Saturation ID net.sf.openfx.SaturationPlugin COMPONENTS THREADS)
ofxmisc_plugin(Shuffle ID net.sf.openfx.ShufflePlugin COMPONENTS THREADS MULTIPLANE)
ofxmisc_plugin(SideBySide ID net.sf.openfx.sideBySidePlugin COMPONENTS THREADS)
ofxmisc_plugin(SlitScan ID net.sf.openfx.SlitScan COMPONENTS THREADS)
ofxmisc_plugin(SpriteSheet ID net.sf.openfx.SpriteSheet COMPONENTS THREADS RECTANGLE_INTERACT GENERATOR)
ofxmisc_plugin(Switch ID net.sf.openfx.switchPlugin COMPONENTS THREADS)
ofxmisc_plugin(Threshold ID net.sf.openfx.Threshold COMPONENTS THREADS)
ofxmisc_plugin(TimeBlur ID net.sf.openfx.TimeBlur COMPONENTS THREADS SHUTTER)
ofxmisc_plugin(TimeBuffer ID net.sf.openfx.TimeBuffer COMPONENTS THREADS DEBUG_ONLY)
ofxmisc_plugin(TimeDissolve ID net.sf.openfx.TimeDissolvePlugin COMPONENTS THREADS)
ofxmisc_plugin(TimeOffset ID net.sf.openfx.timeOffset COMPONENTS THREADS)
ofxmisc_plugin(TrackerPM ID net.sf.openfx.TrackerPM COMPONENTS THREADS TRACKING TEXT)
ofxmisc_plugin(Transform ID net.sf.openfx.Transform COMPONENTS THREADS TRANSFORM3X3 TRANSFORM_INTERACT SHUTTER)
ofxmisc_plugin(VectorToColor ID net.sf.openfx.VectorToColorPlugin COMPONENTS THREADS LUT)
ofxmisc_plugin(WhiteBalance ID net.sf.openfx.WhiteBalance COMPONENTS THREADS)

# CImg.ofx.bundle (they all share CImg/CImgFilter.cpp)
ofxmisc_plugin(CImgBilateral DIR CImg/Bilateral ID net.sf.cimg.CImgBilateral COMPONENTS THREADS SOURCES CImg/CImgOperator.cpp)
ofxmisc_plugin(CImgBlur DIR CImg/Blur ID net.sf.cimg.CImgBlur COMPONENTS THREADS LUT
  PYPLUGS EdgeBlur.py EdgeBlur.gizmo fr.inria.EdgeBlur.png fr.inria.EdgeBlur.svg)
ofxmisc_plugin(CImgConvolve DIR CImg/Convolve ID net.sf.cimg.CImgConvolve COMPONENTS THREADS FFT)
ofxmisc_plugin(CImgDenoise DIR CImg/Denoise ID net.sf.cimg.CImgDenoise COMPONENTS THREADS)
ofxmisc_plugin(CImgDistance DIR CImg/Distance ID eu.cimg.Distance COMPONENTS THREADS)
ofxmisc_plugin(CImgEqualize DIR CImg/Equalize ID net.sf.cimg.CImgEqualize COMPONENTS THREADS)
ofxmisc_plugin(CImgErode DIR CImg/Erode ID net.sf.cimg.CImgErode COMPONENTS THREADS)
ofxmisc_plugin(CImgErodeSmooth DIR CImg/ErodeSmooth ID net.sf.cimg.CImgErodeSmooth COMPONENTS THREADS)
ofxmisc_plugin(CImgExpression DIR CImg/Expression ID net.sf.cimg.CImgExpression COMPONENTS THREADS FFT)
ofxmisc_plugin(CImgFilterErode DIR CImg/FilterErode ID net.sf.cimg.CImgFilterErode COMPONENTS THREADS)
ofxmisc_plugin(CImgGuided DIR CImg/Guided ID net.sf.cimg.CImgGuided COMPONENTS THREADS)
ofxmisc_plugin(CImgHistEQ DIR CImg/HistEQ ID net.sf.cimg.CImgHistEQ COMPONENTS THREADS LUT)
ofxmisc_plugin(CImgInpaint DIR CImg/Inpaint ID eu.cimg.Inpaint COMPONENTS THREADS)
ofxmisc_plugin(CImgMatrix DIR CImg/Matrix ID eu.cimg.CImgMatrix COMPONENTS THREADS)
ofxmisc_plugin(CImgMedian DIR CImg/Median ID net.sf.cimg.CImgMedian COMPONENTS THREADS)
ofxmisc_plugin(CImgNoise DIR CImg/Noise ID net.sf.cimg.CImgNoise COMPONENTS THREADS)
ofxmisc_plugin(CImgPlasma DIR CImg/Plasma ID net.sf.cimg.CImgPlasma COMPONENTS THREADS)
ofxmisc_plugin(CImgRollingGuidance DIR CImg/RollingGuidance ID net.sf.cimg.CImgRollingGuidance COMPONENTS THREADS)
ofxmisc_plugin(CImgSharpenInvDiff DIR CImg/SharpenInvDiff ID net.sf.cimg.CImgSharpenInvDiff COMPONENTS THREADS)
ofxmisc_plugin(CImgSharpenShock DIR CImg/SharpenShock ID net.sf.cimg.CImgSharpenShock COMPONENTS THREADS)
ofxmisc_plugin(CImgSmooth DIR CImg/Smooth ID net.sf.cimg.CImgSmooth COMPONENTS THREADS)
ofxmisc_plugin(CImgZDefocus DIR CImg/ZDefocus ID net.sf.cimg.CImgZDefocus COMPONENTS THREADS FFT)

# ofxmisc_check_table(): every directory whose sources register a plug-in factory
# (mRegisterPluginFactoryInstance) is in the table, so that a new plug-in can't be left out of the
# bundles; Shadertoy and Test have their own bundles (CMakeLists.txt), and Templates holds models
# that are not built.
function(ofxmisc_check_table)
  get_property(_table GLOBAL PROPERTY OFXMISC_TABLE_DIRS)
  file(GLOB _srcs RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/*/*.cpp" "${PROJECT_SOURCE_DIR}/CImg/*/*.cpp")
  set(_missing)
  foreach(_s IN LISTS _srcs)
    get_filename_component(_d "${_s}" DIRECTORY)
    if(_d IN_LIST _table OR _d MATCHES "^(Shadertoy|Test|Templates|openfx|SupportExt)$")
      continue()
    endif()
    file(STRINGS "${PROJECT_SOURCE_DIR}/${_s}" _reg REGEX "^[ \t]*mRegisterPluginFactoryInstance")
    if(_reg)
      list(APPEND _missing ${_d})
    endif()
  endforeach()
  if(_missing)
    list(REMOVE_DUPLICATES _missing)
    message(FATAL_ERROR "cmake/OFXMiscPlugins.cmake: these directories register plug-ins but have "
      "no ofxmisc_plugin() line: [${_missing}]")
  endif()
endfunction()
ofxmisc_check_table()
