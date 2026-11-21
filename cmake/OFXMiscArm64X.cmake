# Arm64X plug-in binaries with MSVC: Microsoft's two-pass recipe
# (https://learn.microsoft.com/windows/arm/arm64x-build), as openfx-gmic's GMIC_ARM64X.
# An Arm64X .ofx holds both ARM64 and Arm64EC code: it loads in Arm64EC hosts (and x64 hosts
# emulated on Windows on Arm that look in Win-arm64ec) and in native ARM64 hosts.
# - OFXMISC_ARM64X=ARM64: an ARM64 build (installed into Win-arm64), whose links also write their
#   inputs (objects, libraries), with full paths, into OFXMISC_ARM64X_DIR/<target>.rsp
#   (/LINKREPROFULLPATHRSP, MSVC 17.11 or later);
# - OFXMISC_ARM64X=ARM64EC: an Arm64EC build (CFLAGS=-arm64EC CXXFLAGS=-arm64EC), configured after
#   the ARM64 build, which links each plug-in with the ARM64 build's inputs as an Arm64X image
#   (/machine:arm64x). openfx's ARCHDIR installs it into Win-arm64ec.
# (.github/tools/msvc-build.sh arm64x runs both.) It applies to every plug-in target (add_ofx_plugin:
# a MODULE library with the .ofx suffix) of this directory: include it after the last one.
# openfx's cmake/OpenFX.cmake does not have this (yet): this file is generic, and could move there.
set(OFXMISC_ARM64X "" CACHE STRING "MSVC Arm64X: ARM64 (first build) or ARM64EC (second build); empty: no Arm64X")
set(OFXMISC_ARM64X_DIR "" CACHE PATH "Arm64X: the linker inputs of the ARM64 build (<target>.rsp; written by it, read by the Arm64EC build)")
if(OFXMISC_ARM64X)
  if(NOT MSVC OR NOT OFXMISC_ARM64X MATCHES "^(ARM64|ARM64EC)$" OR NOT OFXMISC_ARM64X_DIR)
    message(FATAL_ERROR "OFXMISC_ARM64X=ARM64 or ARM64EC needs MSVC and OFXMISC_ARM64X_DIR=<dir>")
  endif()
  string(TOUPPER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" _arch)
  if(NOT _arch STREQUAL OFXMISC_ARM64X)
    message(FATAL_ERROR "OFXMISC_ARM64X=${OFXMISC_ARM64X}, but the compiler targets ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}"
      " (Arm64EC: CFLAGS=-arm64EC CXXFLAGS=-arm64EC, with the ARM64 compiler)")
  endif()
  if(OFXMISC_ARM64X STREQUAL "ARM64EC" AND NOT ARCHDIR STREQUAL "Win-arm64ec")
    message(FATAL_ERROR "OFXMISC_ARM64X=ARM64EC: openfx's ARCHDIR is ${ARCHDIR}, not Win-arm64ec")
  endif()
  file(MAKE_DIRECTORY "${OFXMISC_ARM64X_DIR}")
  get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  set(_n 0)
  foreach(_t IN LISTS _targets)
    get_target_property(_type ${_t} TYPE)
    get_target_property(_suffix ${_t} SUFFIX)
    if(NOT _type STREQUAL "MODULE_LIBRARY" OR NOT _suffix STREQUAL ".ofx")
      continue()
    endif()
    math(EXPR _n "${_n} + 1")
    set(_rsp "${OFXMISC_ARM64X_DIR}/${_t}.rsp")
    if(OFXMISC_ARM64X STREQUAL "ARM64")
      target_link_options(${_t} PRIVATE "/LINKREPROFULLPATHRSP:${_rsp}")
      continue()
    endif()
    if(NOT EXISTS "${_rsp}")
      message(FATAL_ERROR "${_rsp} not found: build the OFXMISC_ARM64X=ARM64 configuration first")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rsp}")
    # The linker writes it in UTF-16LE with CRLF line endings, one quoted input per line (also
    # /manifestinput:..., left out: the Arm64EC link embeds the same manifest)
    file(STRINGS "${_rsp}" _lines ENCODING UTF-16LE)
    set(_objs "")
    set(_libs "")
    foreach(_l IN LISTS _lines)
      string(STRIP "${_l}" _l)
      if(_l MATCHES "^\"(.*\\.[oO][bB][jJ])\"$")
        string(REPLACE "\\" "/" _f "${CMAKE_MATCH_1}")
        list(APPEND _objs "${_f}")
      elseif(_l MATCHES "^\"(.*\\.[lL][iI][bB])\"$")
        string(REPLACE "\\" "/" _f "${CMAKE_MATCH_1}")
        list(APPEND _libs "${_f}")
      elseif(_l MATCHES "\\.[dD][eE][fF]\"?$")
        # Microsoft's recipe passes the ARM64 build's .def with /defArm64Native: not done here,
        # and it must not be dropped silently (the plug-ins export with __declspec(dllexport))
        message(FATAL_ERROR "${_rsp}: a module-definition file (${_l}): Arm64X needs /defArm64Native "
          "for it, which cmake/OFXMiscArm64X.cmake does not pass")
      endif()
    endforeach()
    if(NOT _objs)
      message(FATAL_ERROR "${_rsp}: no object file found")
    endif()
    list(LENGTH _objs _n_objs)
    list(LENGTH _libs _n_libs)
    message(STATUS "Arm64X: ${_t} also links the ${_n_objs} objects and ${_n_libs} libraries of the ARM64 build")
    target_sources(${_t} PRIVATE ${_objs})
    target_link_options(${_t} PRIVATE /machine:arm64x ${_libs})
  endforeach()
  if(_n EQUAL 0)
    message(FATAL_ERROR "OFXMISC_ARM64X: no plug-in target found")
  endif()
  message(STATUS "Arm64X (${OFXMISC_ARM64X} build): ${_n} plug-in targets")
endif()
