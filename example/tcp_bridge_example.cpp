//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Talk to a serial device exposed as a raw TCP stream.
//
//   tcp_bridge_example 192.168.1.50:4001
//

#include "example_common.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        example::print_usage(argv[0], "<host:port>");
        return 1;
    }

    std::cout << "Boost.VirtualSerial - raw TCP bridge example\n";
    std::cout << "  endpoint: " << argv[1] << '\n';

    try
    {
        asio::io_context io;
        vs::basic_serial_port<vs::tcp_bridge_port> port(io.get_executor());
        port.open(argv[1]);
        port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));

        boost::system::error_code ec;
        example::write_all(port, "PING\r\n", ec);
        example::step("write PING", ec);

        char buffer[128] = {};
        const std::size_t received = example::read_with_timeout(
            io, port, buffer, sizeof(buffer),
            std::chrono::milliseconds(1000), ec);
        example::step("read response", ec);
        if (!ec)
            std::cout << "  received " << received << " byte(s): "
                      << std::string(buffer, received) << '\n';

        port.close();
    }
    catch (const boost::system::system_error& error)
    {
        std::cerr << "bridge error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
