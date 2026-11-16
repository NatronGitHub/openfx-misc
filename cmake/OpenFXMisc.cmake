# ***** BEGIN LICENSE BLOCK *****
# This file is part of openfx-misc <https://github.com/NatronGitHub/openfx-misc>,
# (C) 2026 The Natron Developers
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

# Temporary local stand-in for the plug-in helpers that openfx's cmake/OpenFX.cmake is
# getting (NatronGitHub/Natron#1181): the same names and arguments, so that this file can
# be deleted once the openfx submodule provides them. Nothing here is specific to
# openfx-misc: the bundle composition is in CMakeLists.txt.
#
#   add_ofx_plugin(<target> [DIR <dir>] [BUNDLE_NAME <name>] [INFO_PLIST <file>]
#                  [IDENTIFIER <id>] [VERSION <x.y.z>] [SOURCES <src>...])
#     openfx's add_ofx_plugin (a MODULE on macOS, SHARED elsewhere, hidden visibility,
#     <target>.ofx.bundle/Contents/{Info.plist,<ARCHDIR>/<target>.ofx} assembled in
#     OFX_PLUGIN_BUILD_DIR and installed to PLUGIN_INSTALLDIR), plus what
#     openfx/Support/Plugins/Makefile.master does:
#     - Info.plist: INFO_PLIST (a configure_file @ONLY template, with PLUGIN_NAME,
#       PLUGIN_EXE, PLUGIN_IDENTIFIER and PLUGIN_VERSION; a file without @VAR@ is copied
#       as is), or openfx's template with IDENTIFIER and VERSION;
#     - only the entry points are exported (Support/include/osxSymbols, linuxSymbols);
#     - rpath @loader_path/../Frameworks and @loader_path/../Libraries (macOS),
#       $ORIGIN/../../Libraries (Linux, FreeBSD), also in the build tree;
#     - DEBUG in Debug builds, NDEBUG otherwise;
#     - /bigobj (MSVC), -Wa,-mbig-obj (MinGW GCC);
#     - CXX_STANDARD ${OFX_CXX_STANDARD}.
#   ofx_plugin_resources(<target> [DESTINATION <subdir>] <file>...)
#     Copy files to Contents/Resources[/<subdir>], in the build tree and at install.
#   ofx_target_openmp(<target> [REQUIRED])
#     Link OpenMP::OpenMP_CXX if OFX_USE_OPENMP (Apple clang: MacPorts libomp hints).
#   ofx_deploy_bundle(<target>)
#     If OFX_DEPLOY_BUNDLES, at install: macOS: osxDeploy.sh (non-system dylibs to
#     Contents/Libraries, @rpath install names; a copied library whose signature is not
#     valid is signed again, ad hoc); Windows: the runtime DLLs next to the .ofx.
#
# Include it after openfx's OpenFX.cmake (it uses ARCHDIR, OFX_PLUGIN_BUILD_DIR,
# PLUGIN_INSTALLDIR and its add_ofx_plugin).

include_guard(GLOBAL)

if(COMMAND ofx_plugin_resources)
  # openfx's OpenFX.cmake already has these helpers: this file is no longer needed
  return()
endif()
if(NOT COMMAND add_ofx_plugin OR NOT DEFINED ARCHDIR)
  message(FATAL_ERROR "OpenFXMisc.cmake: include openfx's OpenFX.cmake first")
endif()

get_property(_OFX_SOURCE_DIR GLOBAL PROPERTY OFX_PROJECT_SOURCE_DIR)
if(NOT _OFX_SOURCE_DIR)
  get_filename_component(_OFX_SOURCE_DIR "${_OFX_CMAKE_MODULE_DIR}/.." ABSOLUTE)
endif()

if(NOT DEFINED OFX_CXX_STANDARD)
  set(OFX_CXX_STANDARD 17 CACHE STRING "C++ standard of the plug-ins (17, 20...)")
endif()
option(OFX_USE_OPENMP "Use OpenMP in the plug-ins that support it (ofx_target_openmp)" ON)
option(OFX_DEPLOY_BUNDLES
  "At install, copy the non-system libraries into the bundles (macOS: osxDeploy.sh, Windows: DLLs)" OFF)

# The bundle directory of a target, in the build tree and in the install tree.
macro(_ofx_bundle_dirs TARGET)
  set(_ofx_bundle "${OFX_PLUGIN_BUILD_DIR}/${TARGET}.ofx.bundle")
  set(_ofx_install_bundle "${PLUGIN_INSTALLDIR}/${TARGET}.ofx.bundle")
  if(IS_ABSOLUTE "${PLUGIN_INSTALLDIR}")
    set(_ofx_install_path "\$ENV{DESTDIR}${_ofx_install_bundle}")
  else()
    set(_ofx_install_path "\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${_ofx_install_bundle}")
  endif()
endmacro()

# openfx's add_ofx_plugin(<target> [<dir>]): CMake keeps the previous definition of a
# command as _<command> when it is redefined.
function(add_ofx_plugin TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DIR;BUNDLE_NAME;INFO_PLIST;IDENTIFIER;VERSION" "SOURCES")
  if(ARG_UNPARSED_ARGUMENTS)
    list(LENGTH ARG_UNPARSED_ARGUMENTS _n)
    if(_n EQUAL 1 AND NOT ARG_DIR)
      set(ARG_DIR ${ARG_UNPARSED_ARGUMENTS}) # openfx's add_ofx_plugin(<target> <dir>)
    else()
      message(FATAL_ERROR "add_ofx_plugin(${TARGET}): unknown arguments ${ARG_UNPARSED_ARGUMENTS}")
    endif()
  endif()
  if(ARG_BUNDLE_NAME AND NOT ARG_BUNDLE_NAME STREQUAL TARGET)
    message(FATAL_ERROR "add_ofx_plugin(${TARGET}): BUNDLE_NAME other than the target name "
                        "is not supported by cmake/OpenFXMisc.cmake")
  endif()
  if(ARG_DIR)
    _add_ofx_plugin(${TARGET} ${ARG_DIR})
  else()
    _add_ofx_plugin(${TARGET})
  endif()
  _ofx_bundle_dirs(${TARGET})

  if(ARG_SOURCES)
    target_sources(${TARGET} PRIVATE ${ARG_SOURCES})
  endif()

  # Info.plist: _add_ofx_plugin configured openfx's template with fixed values, and
  # installs ${_ofx_bundle}/Contents/Info.plist: overwrite it.
  set(PLUGIN_NAME ${TARGET})
  set(PLUGIN_EXE ${TARGET}.ofx)
  set(PLUGIN_IDENTIFIER org.openeffects.plugins.${TARGET})
  if(ARG_IDENTIFIER)
    set(PLUGIN_IDENTIFIER ${ARG_IDENTIFIER})
  endif()
  set(PLUGIN_VERSION 1.0.0)
  if(ARG_VERSION)
    set(PLUGIN_VERSION ${ARG_VERSION})
  endif()
  set(BUNDLE_IDENTIFIER ${PLUGIN_IDENTIFIER}) # openfx's Examples/Info.plist.in
  set(BUNDLE_SIGNATURE "????")
  if(ARG_INFO_PLIST)
    get_filename_component(_plist "${ARG_INFO_PLIST}" ABSOLUTE)
    configure_file("${_plist}" "${_ofx_bundle}/Contents/Info.plist" @ONLY)
  elseif(ARG_IDENTIFIER OR ARG_VERSION)
    foreach(_plist "${_OFX_CMAKE_MODULE_DIR}/Info.plist.in" "${_OFX_SOURCE_DIR}/Examples/Info.plist.in")
      if(EXISTS "${_plist}")
        configure_file("${_plist}" "${_ofx_bundle}/Contents/Info.plist")
        break()
      endif()
    endforeach()
  endif()

  # Export only the OFX entry points, as Makefile.master does
  if(APPLE)
    set(_syms "${_OFX_SOURCE_DIR}/Support/include/osxSymbols")
    target_link_options(${TARGET} PRIVATE "LINKER:-exported_symbols_list,${_syms}")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_syms}")
  elseif(NOT MSVC) # Linux, FreeBSD, MinGW (MSVC: OfxExport is __declspec(dllexport))
    set(_syms "${_OFX_SOURCE_DIR}/Support/include/linuxSymbols")
    target_link_options(${TARGET} PRIVATE "LINKER:--version-script=${_syms}")
    set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${_syms}")
  endif()

  # rpath of Makefile.master, for the libraries that osxDeploy.sh (macOS) or the
  # host's installer (Linux) put in the bundle. The build tree bundle uses it too,
  # so that the installed binary is the one that was built.
  if(APPLE)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "@loader_path/../Frameworks;@loader_path/../Libraries"
      BUILD_WITH_INSTALL_RPATH ON)
  elseif(UNIX)
    set_target_properties(${TARGET} PROPERTIES
      INSTALL_RPATH "$ORIGIN/../../Libraries"
      BUILD_WITH_INSTALL_RPATH ON)
  endif()

  target_compile_definitions(${TARGET} PRIVATE $<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>)
  if(MSVC)
    target_compile_options(${TARGET} PRIVATE /bigobj)
  elseif(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${TARGET} PRIVATE -Wa,-mbig-obj)
  endif()
  if(OFX_CXX_STANDARD)
    set_target_properties(${TARGET} PROPERTIES CXX_STANDARD ${OFX_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
  endif()
endfunction()

function(ofx_plugin_resources TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "" "DESTINATION" "")
  _ofx_bundle_dirs(${TARGET})
  set(_sub "Contents/Resources")
  if(ARG_DESTINATION)
    set(_sub "${_sub}/${ARG_DESTINATION}")
  endif()
  set(_srcs)
  set(_outs)
  foreach(_f IN LISTS ARG_UNPARSED_ARGUMENTS)
    get_filename_component(_abs "${_f}" ABSOLUTE)
    if(IS_DIRECTORY "${_abs}" OR NOT EXISTS "${_abs}")
      message(FATAL_ERROR "ofx_plugin_resources(${TARGET}): ${_f} is not a file")
    endif()
    get_filename_component(_name "${_abs}" NAME)
    set(_out "${_ofx_bundle}/${_sub}/${_name}")
    add_custom_command(OUTPUT "${_out}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${_ofx_bundle}/${_sub}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${_abs}" "${_out}"
      DEPENDS "${_abs}" VERBATIM)
    list(APPEND _srcs "${_abs}")
    list(APPEND _outs "${_out}")
  endforeach()
  if(NOT _srcs)
    return()
  endif()
  install(FILES ${_srcs} DESTINATION "${_ofx_install_bundle}/${_sub}")
  # one target per call, built before the plug-in
  get_property(_n TARGET ${TARGET} PROPERTY _OFX_RESOURCES_COUNT)
  if(NOT _n)
    set(_n 0)
  endif()
  math(EXPR _n "${_n} + 1")
  set_property(TARGET ${TARGET} PROPERTY _OFX_RESOURCES_COUNT ${_n})
  add_custom_target(${TARGET}_resources${_n} DEPENDS ${_outs})
  add_dependencies(${TARGET} ${TARGET}_resources${_n})
endfunction()

# find_package(OpenMP), once. Apple clang has no OpenMP runtime: use MacPorts' libomp
# (port install libomp) if no OpenMP_CXX_* hints were given, e.g. for another libomp:
#   -DOpenMP_CXX_FLAGS="-Xclang -fopenmp -I<prefix>/include" -DOpenMP_CXX_LIB_NAMES=omp
#   -DOpenMP_omp_LIBRARY=<prefix>/lib/libomp.dylib
macro(_ofx_find_openmp)
  if(NOT TARGET OpenMP::OpenMP_CXX AND NOT _OFX_OPENMP_SEARCHED)
    set(_OFX_OPENMP_SEARCHED ON)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang" AND NOT DEFINED OpenMP_CXX_FLAGS
       AND EXISTS "/opt/local/include/libomp/omp.h" AND EXISTS "/opt/local/lib/libomp/libomp.dylib")
      message(STATUS "OpenMP: using MacPorts libomp (/opt/local/lib/libomp)")
      set(OpenMP_CXX_FLAGS "-Xclang -fopenmp -I/opt/local/include/libomp" CACHE STRING "OpenMP compiler flags")
      set(OpenMP_CXX_LIB_NAMES "omp" CACHE STRING "OpenMP libraries")
      set(OpenMP_omp_LIBRARY "/opt/local/lib/libomp/libomp.dylib" CACHE FILEPATH "libomp")
    endif()
    find_package(OpenMP COMPONENTS CXX)
  endif()
endmacro()

function(ofx_target_openmp TARGET)
  cmake_parse_arguments(PARSE_ARGV 1 ARG "REQUIRED" "" "")
  if(NOT OFX_USE_OPENMP)
    return()
  endif()
  _ofx_find_openmp()
  if(TARGET OpenMP::OpenMP_CXX)
    target_link_libraries(${TARGET} PRIVATE OpenMP::OpenMP_CXX)
  elseif(ARG_REQUIRED)
    message(FATAL_ERROR "${TARGET}: OpenMP not found (configure with -DOFX_USE_OPENMP=OFF to build without it)")
  else()
    message(STATUS "${TARGET}: OpenMP not found, building without it")
  endif()
endfunction()

function(ofx_deploy_bundle TARGET)
  if(NOT OFX_DEPLOY_BUNDLES)
    return()
  endif()
  _ofx_bundle_dirs(${TARGET})
  set(_bin "${_ofx_install_path}/Contents/${ARCHDIR}/${TARGET}.ofx")
  if(APPLE)
    # osxDeploy.sh needs GNU sed
    find_program(OFX_GSED_EXECUTABLE NAMES gsed)
    if(NOT OFX_GSED_EXECUTABLE)
      message(FATAL_ERROR "OFX_DEPLOY_BUNDLES needs GNU sed (gsed): port install gsed")
    endif()
    set(_deploy "${_OFX_SOURCE_DIR}/Support/include/osxDeploy.sh")
    install(CODE "
      execute_process(COMMAND \"${CMAKE_COMMAND}\" -E env \"SED=${OFX_GSED_EXECUTABLE}\"
                              bash \"${_deploy}\" \"${_ofx_install_path}\" \"${TARGET}.ofx\"
                      RESULT_VARIABLE _r)
      if(NOT _r EQUAL 0)
        message(FATAL_ERROR \"osxDeploy.sh failed on ${_ofx_install_path}\")
      endif()
      # install_name_tool keeps ad hoc (linker) signatures valid; sign again (ad hoc) a
      # copied library whose signature it invalidated. The plug-in binary is not signed
      # again: that would add Contents/_CodeSignature to the bundle.
      file(GLOB _libs \"${_ofx_install_path}/Contents/Libraries/*.dylib\")
      foreach(_lib IN LISTS _libs)
        execute_process(COMMAND codesign --verify \"\${_lib}\" RESULT_VARIABLE _r OUTPUT_QUIET ERROR_QUIET)
        if(NOT _r EQUAL 0)
          execute_process(COMMAND codesign --force --sign - \"\${_lib}\" RESULT_VARIABLE _r)
          if(NOT _r EQUAL 0)
            message(FATAL_ERROR \"codesign failed on \${_lib}\")
          endif()
        endif()
      endforeach()
    ")
  elseif(WIN32)
    # the DLLs the plug-in needs, except the system ones, found next to the compiler
    get_filename_component(_cxxdir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    install(CODE "
      file(GET_RUNTIME_DEPENDENCIES LIBRARIES \"${_bin}\"
           RESOLVED_DEPENDENCIES_VAR _deps UNRESOLVED_DEPENDENCIES_VAR _unresolved
           DIRECTORIES \"${_cxxdir}\" \$ENV{PATH}
           PRE_EXCLUDE_REGEXES \"^api-ms-\" \"^ext-ms-\"
           POST_EXCLUDE_REGEXES \"[Ss][Yy][Ss][Tt][Ee][Mm]32\")
      if(_deps)
        file(INSTALL \${_deps} DESTINATION \"${_ofx_install_path}/Contents/${ARCHDIR}\")
      endif()
    ")
  endif()
endfunction()
