#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#
# Detects whether the toolchain and Boost version support C++20 concepts and
# coroutines together.  The probe uses the same flags as the targets that
# depend on it, so a positive result means the examples and tests compile.
#

include(CheckCXXSourceCompiles)

function(boost_virtual_serial_detect_cxx20 boost_include_dir)
  if(NOT boost_include_dir)
    set(BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS OFF PARENT_SCOPE)
    set(BOOST_VIRTUAL_SERIAL_HAS_COROUTINES OFF PARENT_SCOPE)
    message(STATUS
      "Boost.VirtualSerial: C++20 feature detection skipped "
      "(Boost include directory unknown)")
    return()
  endif()

  if(MSVC)
    set(_standard_flag "/std:c++20")
  else()
    set(_standard_flag "-std=c++20")
  endif()

  set(_saved_flags "${CMAKE_REQUIRED_FLAGS}")
  set(_saved_includes "${CMAKE_REQUIRED_INCLUDES}")
  set(CMAKE_REQUIRED_INCLUDES "${boost_include_dir}")

  set(CMAKE_REQUIRED_FLAGS "${_standard_flag}")
  check_cxx_source_compiles("
#include <concepts>
template <class T> concept Addable = requires(T value) { value + value; };
static_assert(Addable<int>);
int main() { return 0; }
" BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS)

  set(_coroutine_flags "${_standard_flag}")
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    string(APPEND _coroutine_flags " -fcoroutines")
  endif()
  set(CMAKE_REQUIRED_FLAGS "${_coroutine_flags}")
  check_cxx_source_compiles("
#include <coroutine>
#if !defined(__cpp_impl_coroutine)
#error coroutines are not enabled
#endif
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
boost::asio::awaitable<int> answer() { co_return 42; }
int main() { return 0; }
" BOOST_VIRTUAL_SERIAL_HAS_COROUTINES)

  set(CMAKE_REQUIRED_FLAGS "${_saved_flags}")
  set(CMAKE_REQUIRED_INCLUDES "${_saved_includes}")

  set(BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS
    "${BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS}" PARENT_SCOPE)
  set(BOOST_VIRTUAL_SERIAL_HAS_COROUTINES
    "${BOOST_VIRTUAL_SERIAL_HAS_COROUTINES}" PARENT_SCOPE)
endfunction()
