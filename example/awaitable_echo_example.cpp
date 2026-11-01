//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// C++20 coroutine example: a session that reads a line from the device and
// writes it back, using co_await instead of callbacks.  The mock fixture
// supplies the device bytes, so the example runs on every platform.
//
// Build with -DBOOST_VIRTUAL_SERIAL_BUILD_EXAMPLES=ON and a C++20 compiler.
//

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/awaitable.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <iostream>
#include <memory>
#include <string>

#if !BOOST_VIRTUAL_SERIAL_HAS_COROUTINES

int main()
{
    std::cout << "This example needs a C++20 compiler with coroutines.\n";
    return 0;
}

#else

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

asio::awaitable<void> session(vs::any_serial_port& port)
{
    // A device would deliver bytes asynchronously; the mock fixture stages
    // them, and read_exactly suspends until all five have arrived.
    char buffer[5] = {};
    const std::size_t received =
        co_await vs::read_exactly(port, asio::buffer(buffer, 5));
    std::cout << "device sent " << received << " bytes: "
              << std::string(buffer, received) << '\n';

    // Direct use of the Asio token is equally valid; the helper above simply
    // wraps the composed operation.
    const std::size_t sent =
        co_await vs::write_all(port, asio::buffer(buffer, received));
    std::cout << "wrote " << sent << " bytes back to the device\n";

    // Read one more byte with an explicit use_awaitable token.
    char marker = 0;
    co_await port.async_read_some(
        asio::buffer(&marker, 1), asio::use_awaitable);
    std::cout << "marker: " << marker << '\n';
}

int main()
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);

    boost::system::error_code error;
    port.open("mock://awaitable", error);
    if (error)
    {
        std::cerr << "open failed: " << error.message() << '\n';
        return 1;
    }

    mock->inject_read("PING\n!");
    asio::co_spawn(io, session(port), asio::detached);
    io.run();

    const std::string expected = "PING\n";
    if (mock->written() != expected)
    {
        std::cerr << "unexpected device output: " << mock->written() << '\n';
        return 1;
    }

    std::cout << "device received: " << mock->written() << '\n';
    return 0;
}

#endif // BOOST_VIRTUAL_SERIAL_HAS_COROUTINES
