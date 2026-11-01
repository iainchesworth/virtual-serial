//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_NATIVE_HANDLE_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_NATIVE_HANDLE_HPP

#include <boost/virtual_serial/config.hpp>

#include <cstdint>
#include <type_traits>

namespace boost {
namespace virtual_serial {
namespace detail {

/*  The type-erased front end exposes `native_handle()` as `std::intptr_t`
    so that one signature covers every shape the platforms and Asio
    versions use: `int` on POSIX, `HANDLE` (`void*`) for Windows serial
    ports, `SOCKET` (`UINT_PTR`) for Winsock, and - since Asio 1.86 - a
    small wrapper class around `SOCKET` that converts implicitly to the
    integral handle.  These overloads keep the conversion well-formed in
    every case.
*/

template <class NativeHandle>
inline typename std::enable_if<
    !std::is_pointer<NativeHandle>::value
        && std::is_convertible<NativeHandle, std::intptr_t>::value,
    std::intptr_t>::type
to_intptr(NativeHandle handle) noexcept
{
    return static_cast<std::intptr_t>(handle);
}

template <class NativeHandle>
inline typename std::enable_if<
    std::is_pointer<NativeHandle>::value, std::intptr_t>::type
to_intptr(NativeHandle handle) noexcept
{
    return static_cast<std::intptr_t>(
        reinterpret_cast<std::uintptr_t>(handle));
}

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_NATIVE_HANDLE_HPP
