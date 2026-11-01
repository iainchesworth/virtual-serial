//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// C++20 concepts for the serial stream interface.  This header is empty when
// the library is compiled as C++14 or C++17; the concept checks are optional
// compile-time diagnostics, never a requirement for using the library.

#ifndef BOOST_VIRTUAL_SERIAL_CONCEPTS_HPP
#define BOOST_VIRTUAL_SERIAL_CONCEPTS_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/option.hpp>
#include <boost/virtual_serial/serial_config.hpp>

#if BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS

#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

#include <concepts>
#include <cstddef>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {

/** Satisfied by a type that models the serial stream interface.

    The library is C++14 at its core, but every front end and backend is
    already shaped so that this C++20 concept accepts it unchanged: no member
    is hidden behind a macro, no operation returns a proxy type, and the
    synchronous and asynchronous operations accept any Asio buffer sequence.

    @code
    template <boost::virtual_serial::AsyncSerialStream Stream>
    boost::asio::awaitable<void> read_one(Stream& stream)
    {
        char value = 0;
        co_await stream.async_read_some(boost::asio::buffer(&value, 1),
            boost::asio::use_awaitable);
    }
    @endcode
*/
template <class Stream>
concept AsyncSerialStream = requires(
    Stream& stream,
    boost::asio::mutable_buffer readable,
    boost::asio::const_buffer writable,
    boost::system::error_code& ec,
    const std::string& target,
    void (*handler)(boost::system::error_code, std::size_t)) {
    typename Stream::executor_type;
    { stream.get_executor() } -> std::same_as<typename Stream::executor_type>;
    { stream.is_open() } -> std::convertible_to<bool>;
    { stream.open(target, ec) };
    { stream.close(ec) };
    { stream.read_some(readable, ec) } -> std::convertible_to<std::size_t>;
    { stream.write_some(writable, ec) } -> std::convertible_to<std::size_t>;
    { stream.async_read_some(readable, handler) };
    { stream.async_write_some(writable, handler) };
};

/** Satisfied by an option object the front end can forward.

    The five `boost::asio::serial_port_base` options, #read_timeout and
    #write_timeout satisfy it; unknown types do not, and are reported at
    run time as #error::unsupported_option.
*/
template <class Option>
concept SerialOption =
    requires {
        { detail::option_traits<Option>::supported } -> std::convertible_to<bool>;
    } && detail::option_traits<Option>::supported;

/** Satisfied by a backend that can be adopted by #any_serial_port. */
template <class Backend>
concept SerialBackend =
    AsyncSerialStream<Backend> && requires(Backend& backend) {
        { backend.backend() } -> std::convertible_to<backend_kind>;
        { backend.apply_options(std::declval<const serial_config&>(),
              std::declval<boost::system::error_code&>()) };
        { backend.options() } -> std::convertible_to<serial_config>;
    };

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS

#endif // BOOST_VIRTUAL_SERIAL_CONCEPTS_HPP
