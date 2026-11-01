//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Production quick start: talk to a real serial port.
//
//   physical_echo_example COM3 115200
//   physical_echo_example /dev/ttyUSB0 9600
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
        example::print_usage(argv[0], "<device> [baud-rate]");
        return 1;
    }

    const std::string device = argv[1];
    unsigned int baud_rate = 115200;
    if (argc > 2)
        baud_rate = static_cast<unsigned int>(std::stoul(argv[2]));

    std::cout << "Boost.VirtualSerial - physical port example\n";
    std::cout << "  device: " << device << " at " << baud_rate << " baud\n";

    try
    {
        asio::io_context io;
        vs::basic_serial_port<vs::physical_port> port(io.get_executor());

        // Options applied before open() are remembered and applied when the
        // device is opened.
        port.set_option(asio::serial_port_base::baud_rate(baud_rate));
        port.set_option(asio::serial_port_base::character_size(8));
        port.set_option(asio::serial_port_base::parity(
            asio::serial_port_base::parity::none));
        port.set_option(asio::serial_port_base::stop_bits(
            asio::serial_port_base::stop_bits::one));

        port.set_option(vs::read_timeout(std::chrono::milliseconds(500)));
        port.open(device);
        std::cout << "  port is open\n";

        boost::system::error_code ec;
        example::write_all(port, "AT\r\n", ec);
        example::step("write AT", ec);

        char buffer[128] = {};
        const std::size_t received = example::read_with_timeout(
            io, port, buffer, sizeof(buffer),
            std::chrono::milliseconds(500), ec);
        example::step("read response", ec);
        if (!ec)
            std::cout << "  received " << received << " byte(s): "
                      << std::string(buffer, received) << '\n';

        port.close();
        std::cout << "  port closed\n";
    }
    catch (const boost::system::system_error& error)
    {
        std::cerr << "serial error: " << error.what() << '\n';
        return 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
