//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Demonstrates the API switcher: one piece of application code runs
// unchanged against a compile-time backend and against a runtime-selected
// one.
//

#include "example_common.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

// ---------------------------------------------------------------------------
// The application: written once, templated only on the port type.
// ---------------------------------------------------------------------------
template <class Port>
void request_status(asio::io_context& io, Port& port, const std::string& label)
{
    example::title(label);

    boost::system::error_code ec;
    example::write_all(port, "STATUS?\r\n", ec);
    example::step("write STATUS?", ec);

    char buffer[64] = {};
    const std::size_t received = example::read_with_timeout(
        io, port, buffer, sizeof(buffer), std::chrono::milliseconds(100), ec);
    example::step("read status", ec);
    if (!ec)
        std::cout << "  device replied: " << std::string(buffer, received);
}

/// Installs a tiny scripted device on any mock backend.
void attach_mock_device(const std::shared_ptr<vs::mock_port>& mock)
{
    mock->set_write_handler([mock](const unsigned char*, std::size_t) {
        mock->inject_line("STATUS=READY");
    });
}

} // namespace

int main()
{
    std::cout << "Boost.VirtualSerial - backend switcher example\n";

    asio::io_context io;

    // 1. Compile-time selection: the backend type is fixed, every call is
    //    resolved statically and can be inlined.
    {
        vs::basic_serial_port<vs::mock_port> port(io.get_executor());
        attach_mock_device(port.native_backend());
        port.open("compile-time");
        request_status(io, port, "compile-time backend (basic_serial_port<mock_port>)");
    }

    // 2. Runtime selection: same interface, backend chosen from a URI.
    {
        vs::any_serial_port port =
            vs::make_serial_port(io.get_executor(), "mock://runtime");
        vs::mock_port* mock = vs::backend_cast<vs::mock_port>(port);
        if (mock != nullptr)
        {
            mock->set_write_handler([mock](const unsigned char*, std::size_t) {
                mock->inject_line("STATUS=READY");
            });
        }
        request_status(io, port, "runtime backend (any_serial_port over mock://)");
    }

    // 3. The same code compiles for real hardware and for a network
    //    bridge; only the instantiation changes.
    {
        vs::basic_serial_port<vs::physical_port> hardware(io.get_executor());
        vs::basic_serial_port<vs::rfc2217_port> network(io.get_executor());
        vs::basic_serial_port<vs::tcp_bridge_port> bridge(io.get_executor());

        std::cout << "\n  physical backend available : "
                  << (hardware.is_open() ? "open" : "closed") << '\n';
        std::cout << "  rfc2217 backend available  : "
                  << (network.is_open() ? "open" : "closed") << '\n';
        std::cout << "  tcp bridge available       : "
                  << (bridge.is_open() ? "open" : "closed") << '\n';
        std::cout << "  (open them with a device name or host:port to use them)\n";
    }

    return 0;
}
