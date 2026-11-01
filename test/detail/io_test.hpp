//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_DETAIL_IO_TEST_HPP
#define BOOST_VIRTUAL_SERIAL_TEST_DETAIL_IO_TEST_HPP

#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>

namespace vs_test {

/** Runs the context until no handler is immediately ready.

    Mock completions are posted, so no waiting is required and tests stay
    deterministic.  Never use this with a real socket: it would not wait
    for the network.
*/
inline void pump(boost::asio::io_context& io)
{
    for (int iteration = 0; iteration < 10000; ++iteration)
    {
        io.restart();
        if (io.poll() == 0)
            return;
    }
}

/** Waits until `done()` returns true, or the timeout expires.

    This is the helper for integration tests: it polls the context in small
    slices so a test can wait for a network round trip without hanging
    forever when something is wrong.
*/
template <class Predicate>
inline bool pump_until(
    boost::asio::io_context& io,
    Predicate done,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(4000))
{
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + timeout;
    while (!done() && std::chrono::steady_clock::now() < deadline)
    {
        io.restart();
        io.run_one_for(std::chrono::milliseconds(5));
    }
    return done();
}

/// Runs one handler, waiting at most `timeout`.
inline std::size_t run_one(
    boost::asio::io_context& io,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(4000))
{
    io.restart();
    return io.run_one_for(timeout);
}

} // namespace vs_test

#endif // BOOST_VIRTUAL_SERIAL_TEST_DETAIL_IO_TEST_HPP
