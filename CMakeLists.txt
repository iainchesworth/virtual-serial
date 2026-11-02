#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#
# Boost.VirtualSerial - header-only serial stream abstraction for Boost.Asio.
#
# The project is a self-contained monorepo:
#
#   include/boost/virtual_serial/   the header-only library
#   test/                           unit tests, fixtures and integration tests
#   example/                        runnable examples
#   doc/                            Boost-style reference documentation
#   build/                          Boost.Build (b2) metadata
#
# It builds standalone with modern CMake, and inside a Boost superproject
# through the adjacent Jamfile hierarchy.

cmake_minimum_required(VERSION 3.16)

# The version is defined once in version.txt.  CMake needs the numeric part for
# the package version file; the public header, the vcpkg manifest and the
# changelog carry the full semantic version, and tools/check_version.py keeps
# them in step.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt"
  BOOST_VIRTUAL_SERIAL_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${BOOST_VIRTUAL_SERIAL_VERSION_STRING}"
  BOOST_VIRTUAL_SERIAL_VERSION_STRING)
if(NOT BOOST_VIRTUAL_SERIAL_VERSION_STRING MATCHES
    "^([0-9]+)\\.([0-9]+)\\.([0-9]+)(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR
    "version.txt must contain a semantic version, got "
    "'${BOOST_VIRTUAL_SERIAL_VERSION_STRING}'")
endif()
set(BOOST_VIRTUAL_SERIAL_VERSION_NUMBER
  "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")

project(
  boost_virtual_serial
  VERSION "${BOOST_VIRTUAL_SERIAL_VERSION_NUMBER}"
  DESCRIPTION "Drop-in, mockable serial stream abstraction for Boost.Asio"
  HOMEPAGE_URL "https://github.com/iainchesworth/virtual-serial"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------

set(BOOST_VIRTUAL_SERIAL_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(BOOST_VIRTUAL_SERIAL_IS_TOP_LEVEL ON)
endif()

option(BOOST_VIRTUAL_SERIAL_BUILD_TESTS
  "Build the unit and integration test suite" ${BOOST_VIRTUAL_SERIAL_IS_TOP_LEVEL})
option(BOOST_VIRTUAL_SERIAL_BUILD_EXAMPLES
  "Build the runnable examples" ${BOOST_VIRTUAL_SERIAL_IS_TOP_LEVEL})
option(BOOST_VIRTUAL_SERIAL_INSTALL
  "Generate install and CMake package configuration files"
  ${BOOST_VIRTUAL_SERIAL_IS_TOP_LEVEL})
option(BOOST_VIRTUAL_SERIAL_WARNINGS_AS_ERRORS
  "Treat compiler warnings as errors" OFF)
option(BOOST_VIRTUAL_SERIAL_COVERAGE
  "Instrument tests and examples for line and branch coverage" OFF)
option(BOOST_VIRTUAL_SERIAL_BUILD_PERF
  "Build the performance benchmark suite" OFF)
set(BOOST_VIRTUAL_SERIAL_SANITIZERS "" CACHE STRING
  "Sanitizers to enable: address;undefined (ASan+UBSan) or thread (TSan)")

include(Sanitizers)
boost_virtual_serial_enable_sanitizers()

# ---------------------------------------------------------------------------
# Boost headers
# ---------------------------------------------------------------------------

# Inside a Boost superproject the headers are already on the include path via
# the b2 build; standalone consumers need to be pointed at a Boost tree.
if(NOT TARGET Boost::headers)
  if(TARGET Boost::boost)
    add_library(Boost::headers ALIAS Boost::boost)
  else()
    find_path(BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR boost/asio.hpp
      HINTS
        "${BOOST_ROOT}"
        "$ENV{BOOST_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.cache/boost-root"
      PATH_SUFFIXES include)

    if(BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR)
      add_library(Boost::headers INTERFACE IMPORTED GLOBAL)
      set_target_properties(Boost::headers PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
          "${BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR}")
      message(STATUS
        "Boost.VirtualSerial: Boost headers: ${BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR}")
    else()
      find_package(Boost 1.74 REQUIRED)
      if(NOT TARGET Boost::headers AND TARGET Boost::boost)
        add_library(Boost::headers ALIAS Boost::boost)
      endif()
    endif()
  endif()
endif()

if(NOT TARGET Boost::headers)
  message(FATAL_ERROR
    "Boost.Asio headers were not found. Set BOOST_ROOT or install Boost >= 1.74.")
endif()

# ---------------------------------------------------------------------------
# Optional C++20 features
# ---------------------------------------------------------------------------

# The probe needs the Boost include directory; derive it when the project did
# not create the imported target itself (for example inside a Boost superproject
# the target may already exist).
if(NOT BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR)
  get_target_property(_boost_include_dirs Boost::headers
    INTERFACE_INCLUDE_DIRECTORIES)
  if(_boost_include_dirs)
    list(GET _boost_include_dirs 0 _boost_include_dir)
    string(REGEX REPLACE "^\\$<BUILD_INTERFACE:(.*)>$" "\\1"
      _boost_include_dir "${_boost_include_dir}")
    set(BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR "${_boost_include_dir}")
  endif()
endif()

include(DetectCxx20)
boost_virtual_serial_detect_cxx20(
  "${BOOST_VIRTUAL_SERIAL_BOOST_INCLUDE_DIR}")

message(STATUS
  "Boost.VirtualSerial: C++20 concepts "
  "${BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS}, coroutines "
  "${BOOST_VIRTUAL_SERIAL_HAS_COROUTINES}")

# ---------------------------------------------------------------------------
# The library
# ---------------------------------------------------------------------------

add_library(boost_virtual_serial INTERFACE)
add_library(Boost::virtual_serial ALIAS boost_virtual_serial)

# Exported as Boost::virtual_serial, matching the build-tree alias.
set_target_properties(boost_virtual_serial PROPERTIES
  EXPORT_NAME virtual_serial
  VERSION ${PROJECT_VERSION})

target_compile_features(boost_virtual_serial INTERFACE cxx_std_14)

target_include_directories(boost_virtual_serial INTERFACE
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(boost_virtual_serial INTERFACE Boost::headers)

if(MSVC)
  target_compile_definitions(boost_virtual_serial INTERFACE _WIN32_WINNT=0x0601)
endif()

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------

if(BOOST_VIRTUAL_SERIAL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif()

if(BOOST_VIRTUAL_SERIAL_BUILD_EXAMPLES)
  add_subdirectory(example)
endif()

if(BOOST_VIRTUAL_SERIAL_BUILD_PERF)
  add_subdirectory(perf)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------

if(BOOST_VIRTUAL_SERIAL_INSTALL)
  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(FILES
    README.md
    LICENSE_1_0.txt
    version.txt
    DESTINATION ${CMAKE_INSTALL_DATADIR}/boost_virtual_serial)

  install(TARGETS boost_virtual_serial
    EXPORT boost_virtual_serial-targets)

  set(BOOST_VIRTUAL_SERIAL_CMAKE_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/boost_virtual_serial")

  install(EXPORT boost_virtual_serial-targets
    FILE boost_virtual_serial-targets.cmake
    NAMESPACE Boost::
    DESTINATION "${BOOST_VIRTUAL_SERIAL_CMAKE_DIR}")

  configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/boost_virtual_serial-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/boost_virtual_serial-config.cmake"
    INSTALL_DESTINATION "${BOOST_VIRTUAL_SERIAL_CMAKE_DIR}")

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/boost_virtual_serial-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/boost_virtual_serial-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/boost_virtual_serial-config-version.cmake"
    DESTINATION "${BOOST_VIRTUAL_SERIAL_CMAKE_DIR}")
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------

message(STATUS "Boost.VirtualSerial ${PROJECT_VERSION}")
message(STATUS "  tests    : ${BOOST_VIRTUAL_SERIAL_BUILD_TESTS}")
message(STATUS "  examples : ${BOOST_VIRTUAL_SERIAL_BUILD_EXAMPLES}")
message(STATUS "  install  : ${BOOST_VIRTUAL_SERIAL_INSTALL}")
