#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#

include(CompilerWarnings)
include(Coverage)

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Unit and integration tests
# ---------------------------------------------------------------------------

add_executable(boost_virtual_serial_tests
  main.cpp
  unit/error_test.cpp
  unit/serial_config_test.cpp
  unit/buffer_range_test.cpp
  unit/telnet_codec_test.cpp
  unit/mock_port_test.cpp
  unit/mock_fixture_test.cpp
  unit/any_serial_port_test.cpp
  unit/component_test.cpp
  unit/option_matrix_test.cpp
  unit/backend_direct_test.cpp
  unit/factory_test.cpp
  unit/serial_config_edges_test.cpp
  unit/branch_matrix_test.cpp
  integration/tcp_bridge_test.cpp
  integration/udp_bridge_test.cpp
  integration/rfc2217_test.cpp
  integration/rfc2217_protocol_test.cpp
  integration/physical_loopback_test.cpp
  integration/serial_modes_test.cpp
  asio/async_contract_test.cpp)

target_link_libraries(boost_virtual_serial_tests
  PRIVATE Boost::virtual_serial Threads::Threads)

target_compile_features(boost_virtual_serial_tests PRIVATE cxx_std_17)
target_include_directories(boost_virtual_serial_tests
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

boost_virtual_serial_set_warnings(boost_virtual_serial_tests)
# Tests are header-only: never rely on MSVC auto-linking Boost libraries.
target_compile_definitions(boost_virtual_serial_tests PRIVATE BOOST_ALL_NO_LIB)
boost_virtual_serial_enable_coverage(boost_virtual_serial_tests)

add_test(NAME boost_virtual_serial_tests COMMAND boost_virtual_serial_tests)
set_tests_properties(boost_virtual_serial_tests PROPERTIES TIMEOUT 180)

# ---------------------------------------------------------------------------
# C++14 compatibility of every public header
# ---------------------------------------------------------------------------

add_executable(boost_virtual_serial_cxx14_headers compat/cxx14_headers.cpp)
target_link_libraries(boost_virtual_serial_cxx14_headers
  PRIVATE Boost::virtual_serial)
target_compile_features(boost_virtual_serial_cxx14_headers PRIVATE cxx_std_14)
boost_virtual_serial_set_warnings(boost_virtual_serial_cxx14_headers)
target_compile_definitions(boost_virtual_serial_cxx14_headers PRIVATE BOOST_ALL_NO_LIB)

add_test(
  NAME boost_virtual_serial_cxx14_headers
  COMMAND boost_virtual_serial_cxx14_headers)

# ---------------------------------------------------------------------------
# C++20 targets.  They are built only when the detection in
# cmake/DetectCxx20.cmake proved that the toolchain and Boost support the
# feature, so the default C++14/C++17 build is unaffected.
# ---------------------------------------------------------------------------

function(boost_virtual_serial_add_cxx20_test name source)
  add_executable(${name} ${source} main.cpp)
  target_link_libraries(${name}
    PRIVATE Boost::virtual_serial Threads::Threads)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_include_directories(${name}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  # GCC 11 still needs the explicit coroutine switch.
  target_compile_options(${name}
    PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fcoroutines>)
  boost_virtual_serial_set_warnings(${name})
  target_compile_definitions(${name} PRIVATE BOOST_ALL_NO_LIB)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

if(BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS)
  add_executable(
    boost_virtual_serial_cxx20_concepts compat/cxx20_concepts.cpp)
  target_link_libraries(boost_virtual_serial_cxx20_concepts
    PRIVATE Boost::virtual_serial)
  target_compile_features(boost_virtual_serial_cxx20_concepts
    PRIVATE cxx_std_20)
  boost_virtual_serial_set_warnings(boost_virtual_serial_cxx20_concepts)
  target_compile_definitions(boost_virtual_serial_cxx20_concepts PRIVATE BOOST_ALL_NO_LIB)

  add_test(
    NAME boost_virtual_serial_cxx20_concepts
    COMMAND boost_virtual_serial_cxx20_concepts)

  boost_virtual_serial_add_cxx20_test(boost_virtual_serial_concept_tests
    concept/concepts_test.cpp)
endif()

if(BOOST_VIRTUAL_SERIAL_HAS_COROUTINES)
  boost_virtual_serial_add_cxx20_test(boost_virtual_serial_awaitable_tests
    coroutine/awaitable_test.cpp)
endif()
