//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Optional C++20 coroutine helpers.  The port types are ordinary Asio streams,
// so `co_await port.async_read_some(buffers, asio::use_awaitable)` already
// works; these helpers wrap the common composed operations so an application
// does not have to spell them out.  The buffer sequence is taken by value and
// stored in the coroutine frame, so a temporary buffer() expression is safe.
// The header is empty without coroutine
// support; check BOOST_VIRTUAL_SERIAL_HAS_COROUTINES before using it.

#ifndef BOOST_VIRTUAL_SERIAL_AWAITABLE_HPP
#define BOOST_VIRTUAL_SERIAL_AWAITABLE_HPP

#include <boost/virtual_serial/config.hpp>

#if BOOST_VIRTUAL_SERIAL_HAS_COROUTINES

#include <boost/asio/awaitable.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <cstddef>

namespace boost {
namespace virtual_serial {

/** Reads whatever is available, suspending the coroutine.

    @code
    char buffer[64];
    const std::size_t count = co_await
        boost::virtual_serial::read_some(port, boost::asio::buffer(buffer));
    @endcode
*/
template <class Stream, class MutableBufferSequence>
boost::asio::awaitable<std::size_t> read_some(
    Stream& stream, MutableBufferSequence buffers)
{
    co_return co_await stream.async_read_some(
        buffers, boost::asio::use_awaitable);
}

/** Reads exactly `buffers` bytes, or throws `boost::system::system_error`.

    Equivalent to `boost::asio::async_read` with `use_awaitable`, without
    having to write the token at every call site.
*/
template <class Stream, class MutableBufferSequence>
boost::asio::awaitable<std::size_t> read_exactly(
    Stream& stream, MutableBufferSequence buffers)
{
    co_return co_await boost::asio::async_read(
        stream, buffers, boost::asio::use_awaitable);
}

/** Writes the whole buffer sequence, or throws on error. */
template <class Stream, class ConstBufferSequence>
boost::asio::awaitable<std::size_t> write_all(
    Stream& stream, ConstBufferSequence buffers)
{
    co_return co_await boost::asio::async_write(
        stream, buffers, boost::asio::use_awaitable);
}

/** Reads until `delimiter` is seen, storing the data in `buffer`.

    Returns the number of bytes consumed, including the delimiter, exactly as
    `boost::asio::async_read_until` does.
*/
template <class Stream, class DynamicBuffer>
boost::asio::awaitable<std::size_t> read_until(
    Stream& stream, DynamicBuffer& buffer, char delimiter)
{
    co_return co_await boost::asio::async_read_until(
        stream, buffer, delimiter, boost::asio::use_awaitable);
}

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_HAS_COROUTINES

#endif // BOOST_VIRTUAL_SERIAL_AWAITABLE_HPP
