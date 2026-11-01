//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Connect to a serial device published by an RFC 2217 access server.
//
//   rfc2217_client_example lab-host:7001 115200
//

#include "example_common.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>

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
        example::print_usage(argv[0], "<host:port> [baud-rate]");
        return 1;
    }

    const std::string endpoint = argv[1];
    unsigned int baud_rate = 115200;
    if (argc > 2)
        baud_rate = static_cast<unsigned int>(std::stoul(argv[2]));

    std::cout << "Boost.VirtualSerial - RFC 2217 client example\n";
    std::cout << "  endpoint: " << endpoint << " at " << baud_rate << " baud\n";

    try
    {
        asio::io_context io;
        vs::basic_serial_port<vs::rfc2217_port> port(io.get_executor());
        port.open(endpoint);

        // Setting options after open sends the RFC 2217 configuration
        // commands to the access server.
        port.set_option(asio::serial_port_base::baud_rate(baud_rate));
        port.set_option(asio::serial_port_base::character_size(8));
        port.set_option(asio::serial_port_base::parity(
            asio::serial_port_base::parity::none));
        port.set_option(asio::serial_port_base::stop_bits(
            asio::serial_port_base::stop_bits::one));
        port.set_option(asio::serial_port_base::flow_control(
            asio::serial_port_base::flow_control::none));

        port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));

        std::cout << "  connected; server signature: '"
                  << port.native_backend()->server_signature() << "'\n";

        boost::system::error_code ec;
        example::write_all(port, "AT\r\n", ec);
        example::step("write AT", ec);

        char buffer[128] = {};
        const std::size_t received = example::read_with_timeout(
            io, port, buffer, sizeof(buffer),
            std::chrono::milliseconds(1000), ec);
        example::step("read response", ec);
        if (!ec)
            std::cout << "  received " << received << " byte(s): "
                      << std::string(buffer, received) << '\n';

        // Break signalling and buffer purging are part of RFC 2217.
        port.native_backend()->set_break(true, ec);
        example::step("assert BREAK", ec);
        port.native_backend()->purge(3, ec);
        example::step("purge both buffers", ec);

        port.close();
        std::cout << "  closed\n";
    }
    catch (const boost::system::system_error& error)
    {
        std::cerr << "rfc2217 error: " << error.what() << '\n';
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
