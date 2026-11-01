//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// A complete, self-contained example of testing serial code with the mock
// backend: no COM port, no driver, no hardware, no sleeping.
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

/// A toy device that answers "AT" with "OK".
void demonstrate_scripted_device(asio::io_context& io)
{
    example::title("Scripted device: no hardware involved");

    const std::shared_ptr<vs::mock_port> mock = vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);

    const std::weak_ptr<vs::mock_port> weak = mock;
    mock->set_write_handler([weak](const unsigned char*, std::size_t) {
        // The device answers every command with OK.  The handler lives
        // inside the mock, so a shared_ptr capture would be a cycle.
        if (const std::shared_ptr<vs::mock_port> target = weak.lock())
            target->inject_read("OK\r\n");
    });

    port.open("at-modem");
    std::cout << "  port opened: " << (port.is_open() ? "yes" : "no") << '\n';

    boost::system::error_code ec;
    example::write_all(port, "AT\r\n", ec);
    example::step("write AT", ec);

    char buffer[32] = {};
    const std::size_t received = example::read_with_timeout(
        io, port, buffer, sizeof(buffer), std::chrono::milliseconds(100), ec);
    example::step("read response", ec);
    std::cout << "  device said: " << std::string(buffer, received);
}

void demonstrate_fault_injection(asio::io_context& io)
{
    example::title("Fault injection: framing error on the wire");

    const std::shared_ptr<vs::mock_port> mock = vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    port.open("faulty-line");

    // The next byte is corrupted: a framing error followed by the garbage.
    mock->inject_corrupt_byte(vs::fault::framing, 0x00);

    char buffer[8] = {};
    boost::system::error_code ec;
    const std::size_t received =
        port.read_some(asio::buffer(buffer, sizeof(buffer)), ec);
    // The fault is the expected outcome here, so report it as such rather
    // than as an error.
    std::cout << "  read after corruption: " << ec.message() << ", "
              << received << " byte(s) of payload\n";
}

void demonstrate_clocked_data(asio::io_context& io)
{
    example::title("Clocked data: one byte at a time at 9600 8N1");

    vs::serial_config config;
    config.baud_rate = 9600;
    config.data_bits = 8;

    const std::shared_ptr<vs::mock_port> mock =
        std::make_shared<vs::mock_port>(io.get_executor(), config);
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    port.open("clocked");

    mock->set_pacing(true);
    mock->inject_read("GO");

    std::cout << "  byte duration at 9600 8N1: "
              << mock->byte_duration().count() << " ns\n";
    std::cout << "  available before stepping: " << mock->available() << '\n';

    mock->step();
    std::cout << "  after one step: " << mock->available()
              << " byte available, virtual clock at "
              << mock->now().count() << " ns\n";

    // Run the pending read between the two bytes: this is where a user
    // state machine would race if the timing were wrong.
    char buffer[8] = {};
    boost::system::error_code ec;
    std::size_t received = port.read_some(asio::buffer(buffer), ec);
    std::cout << "  first read returned " << received << " byte(s)\n";

    mock->advance(mock->byte_duration() * 4);
    received = port.read_some(asio::buffer(buffer + received, 8 - received), ec);
    std::cout << "  second read returned " << received << " byte(s)\n";
    std::cout << "  payload: " << std::string(buffer, 2) << '\n';
}

} // namespace

int main()
{
    std::cout << "Boost.VirtualSerial - mock fixture example\n";

    asio::io_context io;
    demonstrate_scripted_device(io);
    demonstrate_fault_injection(io);
    demonstrate_clocked_data(io);

    std::cout << "\nAll interactions were simulated in memory.\n";
    return 0;
}
