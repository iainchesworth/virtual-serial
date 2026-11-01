#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#

include(CompilerWarnings)
include(Coverage)

# Examples that need no hardware and no network are also run as tests, so
# that a broken example cannot silently rot.
set(BOOST_VIRTUAL_SERIAL_SELF_CONTAINED_EXAMPLES
  mock_fixture_example
  backend_switcher_example)

set(BOOST_VIRTUAL_SERIAL_DEVICE_EXAMPLES
  physical_echo_example
  rfc2217_client_example
  tcp_bridge_example)

foreach(example IN LISTS BOOST_VIRTUAL_SERIAL_SELF_CONTAINED_EXAMPLES
                       BOOST_VIRTUAL_SERIAL_DEVICE_EXAMPLES)
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE Boost::virtual_serial)
  target_compile_features(${example} PRIVATE cxx_std_17)
  boost_virtual_serial_set_warnings(${example})
  boost_virtual_serial_enable_coverage(${example})
endforeach()

if(BOOST_VIRTUAL_SERIAL_BUILD_TESTS)
  foreach(example IN LISTS BOOST_VIRTUAL_SERIAL_SELF_CONTAINED_EXAMPLES)
    add_test(NAME example_${example} COMMAND ${example})
    set_tests_properties(example_${example} PROPERTIES TIMEOUT 60)
  endforeach()
endif()

# ---------------------------------------------------------------------------
# C++20 examples, built only when the feature probe succeeded.
# ---------------------------------------------------------------------------

function(boost_virtual_serial_add_cxx20_example name source)
  add_executable(${name} ${source})
  target_link_libraries(${name} PRIVATE Boost::virtual_serial)
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_options(${name}
    PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fcoroutines>)
  target_compile_definitions(${name} PRIVATE BOOST_ALL_NO_LIB)
  boost_virtual_serial_set_warnings(${name})
endfunction()

if(BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS)
  boost_virtual_serial_add_cxx20_example(
    concepts_demo_example concepts_demo_example.cpp)
endif()

if(BOOST_VIRTUAL_SERIAL_HAS_COROUTINES)
  boost_virtual_serial_add_cxx20_example(
    awaitable_echo_example awaitable_echo_example.cpp)
endif()

if(BOOST_VIRTUAL_SERIAL_BUILD_TESTS)
  if(TARGET concepts_demo_example)
    add_test(NAME example_concepts_demo_example COMMAND concepts_demo_example)
    set_tests_properties(example_concepts_demo_example PROPERTIES TIMEOUT 60)
  endif()
  if(TARGET awaitable_echo_example)
    add_test(NAME example_awaitable_echo_example COMMAND awaitable_echo_example)
    set_tests_properties(example_awaitable_echo_example PROPERTIES TIMEOUT 60)
  endif()
endif()