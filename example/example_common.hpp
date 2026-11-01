//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_EXAMPLE_COMMON_HPP
#define BOOST_VIRTUAL_SERIAL_EXAMPLE_COMMON_HPP

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>

namespace example {

/// Prints a step and its outcome in the style of the other examples.
inline void step(const std::string& label, const boost::system::error_code& ec)
{
    std::cout << "  " << label << " ... "
              << (ec ? ("FAILED: " + ec.message()) : std::string("ok"))
              << '\n';
}

inline void title(const std::string& text)
{
    std::cout << "\n== " << text << " ==\n";
}

/** Reads once, with the timeout enforced by the front end.

    This is the pattern to copy into application code: set the timeout as
    an option, start the asynchronous read and run the context.  The
    library races the read against an internal timer and reports
    `boost::virtual_serial::error::timeout` if nothing arrives.
*/
template <class Port>
std::size_t read_with_timeout(
    boost::asio::io_context& io,
    Port& port,
    void* data,
    std::size_t size,
    std::chrono::milliseconds timeout,
    boost::system::error_code& ec)
{
    ec.clear();
    port.set_option(boost::virtual_serial::read_timeout(timeout), ec);
    if (ec)
        return 0;

    bool done = false;
    std::size_t transferred = 0;
    port.async_read_some(
        boost::asio::buffer(data, size),
        [&](boost::system::error_code read_ec, std::size_t count) {
            ec = read_ec;
            transferred = count;
            done = true;
        });

    io.restart();
    while (!done)
    {
        if (io.run_one() == 0)
            break;
    }

    // Disable the timeout again so later operations are not affected.
    boost::system::error_code ignored;
    port.set_option(
        boost::virtual_serial::read_timeout(std::chrono::milliseconds(0)),
        ignored);

    return transferred;
}

/// Writes everything, blocking until the backend accepted it.
template <class Port>
std::size_t write_all(Port& port, const std::string& text,
    boost::system::error_code& ec)
{
    return boost::asio::write(port, boost::asio::buffer(text), ec);
}

inline void print_usage(const char* program, const char* expected)
{
    std::cerr << "usage: " << program << ' ' << expected << '\n';
}

} // namespace example

#endif // BOOST_VIRTUAL_SERIAL_EXAMPLE_COMMON_HPP
