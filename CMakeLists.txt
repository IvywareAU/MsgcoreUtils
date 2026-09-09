# Copyright © 2026 Khrustal & Mann
#              MELBOURNE, VICTORIA, AUSTRALIA, 3000
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied. See the License for the specific language governing
# permissions and limitations under the License.
#
# MsgcoreUtils -> libmsgcoreutils.so / MsgcoreUtils.dll
#
# TOP-LEVEL, and the only CMake entry point this component has: Msgcore's CMakeLists.txt
# does not add this directory any more. The two are separate checkouts that sit side by
# side, and the dependency runs one way only - upward, from here.
#
# What that costs is one add_subdirectory in this direction. target_link_libraries below
# names msgcore, and CMake resolves a target name at the call rather than deferring it,
# so the library's CMakeLists has to have run first. The explicit binary directory is
# required, not stylistic: add_subdirectory() derives one from the source path and cannot
# do that for a directory outside this tree.
#
# Every source here reaches ../Msgcore/stdafx.h and every header reaches
# ../Msgcore/Msgcore.h, so the sibling checkout is not optional. Its absence is a
# FATAL_ERROR naming the directory rather than a wall of missing-header errors forty
# lines into the first TU.
#
# On Windows MsgcoreUtils(2022).vcxproj remains authoritative, exactly as
# Msgcore(2022).vcxproj does for the library beside it: it is the build that carries all
# eight configurations. This file is the Linux port and the parity check.
#
# THE TU LIST IS THE COMPILED SET IN MsgcoreUtils(2022).vcxproj, minus stdafx.cpp -
# target_precompile_headers re-creates the PCH on both platforms, so the classic /Yc TU
# is CMake's to generate.

cmake_minimum_required(VERSION 3.24)
project(MsgcoreUtils LANGUAGES CXX)

# The guard is for the day something adds both checkouts from above: adding a directory
# that has already defined its targets is a hard error, and the target test is what makes
# both entry points work - the same shape Msgcore uses for Platform/.
set(MSGCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Msgcore)
if(NOT TARGET msgcore)
    if(NOT EXISTS ${MSGCORE_DIR}/CMakeLists.txt)
        message(FATAL_ERROR
                "The Msgcore checkout was not found at ${MSGCORE_DIR}. MsgcoreUtils builds "
                "against it as a SIBLING directory: every source here includes "
                "../Msgcore/stdafx.h and this library links msgcore. Clone Msgcore beside "
                "this directory and configure again.")
    endif()
    add_subdirectory(${MSGCORE_DIR} msgcore)
endif()

set(MSGCOREUTILS_SOURCES
    MsgPrint.cpp
    MsgValue.cpp
    PrintJson.cpp
    PrintPHP.cpp
    PrintXML.cpp
    PrintHTML.cpp)

if(WIN32)
    # Same MFC shape as msgcore: a shared-runtime MFC extension DLL, Unicode. The flag is
    # consumed by add_library(), so it has to be set before it.
    set(CMAKE_MFC_FLAG 2)
    enable_language(RC)
    list(APPEND MSGCOREUTILS_SOURCES MsgcoreUtils.rc)
endif()

add_library(msgcoreutils SHARED ${MSGCOREUTILS_SOURCES})

# C++17, for the reason the parent file spells out at length: "latest" is a moving
# target across toolset updates, the .vcxproj pins stdcpp17 in all eight configurations,
# and the two build systems compiling the same sources at different language levels is
# the failure that costs a release.
target_compile_features(msgcoreutils PUBLIC cxx_std_17)
set_target_properties(msgcoreutils PROPERTIES
                      CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

# PUBLIC msgcore, and that is a real dependency rather than a convenience: this module's
# headers include ../P2Pmsg.h and its object files call into the library, so a consumer
# of msgcoreutils needs msgcore on its link line too.
target_link_libraries(msgcoreutils PUBLIC msgcore)
target_compile_definitions(msgcoreutils PRIVATE MsgcoreUtils_EXPORTS)

target_precompile_headers(msgcoreutils PRIVATE stdafx.h)

if(WIN32)
    target_compile_definitions(msgcoreutils PRIVATE WIN32 _WINDOWS _AFXEXT _UNICODE UNICODE)
    # Targets are identifiers and must be lowercase; the artifact must not be. Same
    # reasoning as msgcore's OUTPUT_NAME - a lowercase msgcoreutils.dll and a
    # MsgcoreUtils.dll are one file on a case-insensitive filesystem, and the winner is
    # whichever was copied last.
    set_target_properties(msgcoreutils PROPERTIES OUTPUT_NAME MsgcoreUtils)
    # No system libraries of its own. Everything this module touches - the heap, the
    # object model, the exception type - is Msgcore's, and Msgcore already links the
    # Winsock/COM-support/Propsys set PRIVATE for its own use.
else()
    # GCC/Clang reject constructs MSVC accepts throughout this codebase, and these TUs
    # include its headers. Same downgrade the library below uses, for the same reason.
    target_compile_options(msgcoreutils PRIVATE -fpermissive)
endif()

# ---------------------------------------------------------------------------
# The DebugLib/ReleaseLib shape (MSCS_BUILD_STATIC_LIBS, off by default).
#
# BOTH definitions, not one. Msgcore_STATIC stops Msgcore's headers decorating the
# symbols this archive CALLS; MsgcoreUtils_STATIC stops this module's headers decorating
# the ones it DEFINES. Setting only the second produces an archive whose call sites are
# still dllimport, which links against nothing.
# ---------------------------------------------------------------------------
if(MSCS_BUILD_STATIC_LIBS)
    add_library(msgcoreutils_static STATIC ${MSGCOREUTILS_SOURCES})
    target_compile_features(msgcoreutils_static PUBLIC cxx_std_17)
    set_target_properties(msgcoreutils_static PROPERTIES
                          CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_link_libraries(msgcoreutils_static PUBLIC msgcore_static)
    target_compile_definitions(msgcoreutils_static PUBLIC MsgcoreUtils_STATIC)
    target_precompile_headers(msgcoreutils_static PRIVATE stdafx.h)
    if(WIN32)
        target_compile_definitions(msgcoreutils_static PRIVATE WIN32 _WINDOWS _UNICODE UNICODE)
    else()
        target_compile_options(msgcoreutils_static PRIVATE -fpermissive)
    endif()
endif()

# ---------------------------------------------------------------------------
# NOT INSTALLED, and NOT in the Msgcore export set. This is the same call the parent
# file makes about Msgcore's C++ headers, followed to its conclusion.
#
# Msgcore installs its shared library, Msgcore_c.h and Msgcore_version.h, and
# DELIBERATELY no C++ header - every one of them reaches stdafx.h, which reaches
# Platform/ and MFC, so installing them would publish a surface that cannot compile
# outside this solution. MsgcoreUtils has NO flat C surface: PrintJson and PrintPHP are
# C++ classes over the C++ object model, so the whole of its API is in the half that is
# not shipped.
#
# Installing the binary anyway would stage a DLL no installed consumer could declare a
# call to - a file in a package that nothing in that package can use. Add the install
# rule the day a flat C entry point exists to install beside it.
# ---------------------------------------------------------------------------
