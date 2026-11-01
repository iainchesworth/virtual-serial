#
# Copyright (c) 2026 VirtualSerial contributors
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# https://www.boost.org/LICENSE_1_0.txt)
#
# Sanitizer support.  The library is header-only, so the instrumentation has
# to be applied to the executables that instantiate the headers; the option is
# therefore applied at directory scope before any target is created.
#
#   -DBOOST_VIRTUAL_SERIAL_SANITIZERS="address;undefined"   # ASan + UBSan
#   -DBOOST_VIRTUAL_SERIAL_SANITIZERS=thread                # TSan
#

function(boost_virtual_serial_enable_sanitizers)
  if(NOT BOOST_VIRTUAL_SERIAL_SANITIZERS)
    return()
  endif()

  if(BOOST_VIRTUAL_SERIAL_COVERAGE)
    message(FATAL_ERROR
      "Sanitizers and coverage instrumentation cannot be combined in one build.")
  endif()

  # The sanitizer runtimes are wired through the compiler driver.  CMake links
  # clang-cl through link.exe, which drops the runtime, so the MSVC toolchain
  # (including clang-cl) is not supported here.
  if(MSVC)
    message(FATAL_ERROR
      "Sanitizers are supported with Clang or GCC on Linux and macOS; "
      "MSVC and clang-cl are not supported.")
  endif()


  set(_compile_flags "")
  set(_link_flags "")

  foreach(sanitizer IN LISTS BOOST_VIRTUAL_SERIAL_SANITIZERS)
    if(sanitizer STREQUAL "address")
      list(APPEND _compile_flags
        -fsanitize=address
        -fsanitize-address-use-after-scope)
      list(APPEND _link_flags -fsanitize=address)
    elseif(sanitizer STREQUAL "undefined")
      if(MSVC)
        message(FATAL_ERROR
          "UndefinedBehaviorSanitizer is not supported by clang-cl on Windows.")
      endif()
      list(APPEND _compile_flags
        -fsanitize=undefined
        -fno-sanitize-recover=undefined)
      list(APPEND _link_flags -fsanitize=undefined)
    elseif(sanitizer STREQUAL "thread")
      if(MSVC)
        message(FATAL_ERROR
          "ThreadSanitizer is not supported by clang-cl on Windows.")
      endif()
      list(APPEND _compile_flags -fsanitize=thread)
      list(APPEND _link_flags -fsanitize=thread)
    else()
      message(FATAL_ERROR
        "Unknown sanitizer '${sanitizer}'. Use address, undefined or thread.")
    endif()
  endforeach()

  add_compile_options(${_compile_flags} -fno-omit-frame-pointer -g)
  add_link_options(${_link_flags})

  message(STATUS
    "Boost.VirtualSerial: sanitizers ${BOOST_VIRTUAL_SERIAL_SANITIZERS}")
endfunction()
