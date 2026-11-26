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
# The sources of the CImg plug-ins (included by CMakeLists.txt, with BUILD_CIMG_PLUGINS, after the
# FFT selection, CIMG_FFT): CImg/CImg.h, downloaded at CIMGVERSION and patched as openfx-gmic's,
# CImg/Inpaint/inpaint.h, and, with FFT=pocketfft, CImg/pocketfft_hdronly.h and its licence
# (CIMG_POCKETFFT_LICENCE), as CImg/Makefile does (CImg/CImgVersion.mk). Sets CIMG_DIR,
# CIMGVERSION and CIMG_PATCHES_FROM.

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
