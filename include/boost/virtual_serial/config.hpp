//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/virtual_serial
//

#ifndef BOOST_VIRTUAL_SERIAL_CONFIG_HPP
#define BOOST_VIRTUAL_SERIAL_CONFIG_HPP

#include <boost/config.hpp>

// The library is header-only: there is no compiled translation unit and no
// link-time configuration.  The macros below are the only knobs a user may
// set before including any library header.

// Version numbering follows the Boost convention: major * 100000 +
// minor * 100 + patch.
#define BOOST_VIRTUAL_SERIAL_VERSION 100
#define BOOST_VIRTUAL_SERIAL_VERSION_STRING "0.1.0-beta.1"

#if defined(_MSVC_LANG)
# define BOOST_VIRTUAL_SERIAL_CPLUSPLUS _MSVC_LANG
#else
# define BOOST_VIRTUAL_SERIAL_CPLUSPLUS __cplusplus
#endif

// C++17 is the baseline for new code, but every public header must also
// compile as C++14 so that the library can be dropped into a Boost
// superproject that still advertises C++14.  Features introduced in C++17
// are therefore optional and guarded by this macro.
#if BOOST_VIRTUAL_SERIAL_CPLUSPLUS >= 201703L
# define BOOST_VIRTUAL_SERIAL_HAS_CXX17 1
#else
# define BOOST_VIRTUAL_SERIAL_HAS_CXX17 0
#endif

// C++20 coroutines are used only by the optional awaitable helpers.
#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HAS_CO_AWAIT) \
    && BOOST_VIRTUAL_SERIAL_CPLUSPLUS >= 202002L
# define BOOST_VIRTUAL_SERIAL_HAS_COROUTINES 1
#else
# define BOOST_VIRTUAL_SERIAL_HAS_COROUTINES 0
#endif

// C++20 concepts are used only for optional, compile-time diagnostics.
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L \
    && BOOST_VIRTUAL_SERIAL_CPLUSPLUS >= 202002L
# define BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS 1
#else
# define BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS 0
#endif

#if defined(BOOST_NO_EXCEPTIONS)
# define BOOST_VIRTUAL_SERIAL_NO_EXCEPTIONS 1
#else
# define BOOST_VIRTUAL_SERIAL_NO_EXCEPTIONS 0
#endif

#if defined(BOOST_NO_RTTI)
# define BOOST_VIRTUAL_SERIAL_NO_RTTI 1
#else
# define BOOST_VIRTUAL_SERIAL_NO_RTTI 0
#endif

#if defined(BOOST_VIRTUAL_SERIAL_DOCS)
# define BOOST_VIRTUAL_SERIAL_DOC
#endif

#endif // BOOST_VIRTUAL_SERIAL_CONFIG_HPP
