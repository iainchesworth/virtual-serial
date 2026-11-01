#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#
# Coverage instrumentation for the test and example executables.
#
# The library is header-only, so the instrumentation has to be requested on
# the executables that instantiate the headers.  Clang (including clang-cl and
# AppleClang) and GCC are supported.  MSVC's own instrumentation provides line
# coverage but no branch coverage, which is why CI uses Clang on Windows.

function(boost_virtual_serial_enable_coverage target)
  if(NOT BOOST_VIRTUAL_SERIAL_COVERAGE)
    return()
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${target} PRIVATE
      -fprofile-instr-generate
      -fcoverage-mapping)
    if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      # clang-cl takes optimisation and debug info from the MSVC flags.
      target_compile_options(${target} PRIVATE -O0 -g)
    endif()
    target_link_options(${target} PRIVATE -fprofile-instr-generate)
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PRIVATE --coverage)
  elseif(MSVC)
    target_compile_options(${target} PRIVATE /Z7)
    target_link_options(${target} PRIVATE /DEBUG)
  endif()
endfunction()
