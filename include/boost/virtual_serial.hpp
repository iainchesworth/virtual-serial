//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/virtual_serial
//

#ifndef BOOST_VIRTUAL_SERIAL_HPP
#define BOOST_VIRTUAL_SERIAL_HPP

/** @file boost/virtual_serial.hpp

    Umbrella header for Boost.VirtualSerial, a drop-in, mockable serial
    stream abstraction for Boost.Asio.

    The library provides:

    - `boost::virtual_serial::basic_serial_port<Backend>`, a compile-time
      selected serial stream that models the Asio `SyncReadStream`,
      `SyncWriteStream`, `AsyncReadStream` and `AsyncWriteStream`
      concepts;
    - `boost::virtual_serial::any_serial_port`, the same interface with a
      backend chosen at run time;
    - four backends: `physical_port` (a real `boost::asio::serial_port`),
      `mock_port` (deterministic fixture with data, fault and clocked
      injection), `rfc2217_port` (Serial-over-Telnet) and
      `tcp_bridge_port` / `udp_bridge_port` (raw network bridges);
    - `boost::virtual_serial::parse_uri` and `make_serial_port`, which
      turn a URI into a ready port.

    @code
    #include <boost/virtual_serial.hpp>

    boost::asio::io_context io;
    auto port = boost::virtual_serial::make_serial_port(
        io.get_executor(), "serial://COM3?baud=115200");
    boost::asio::write(port, boost::asio::buffer("AT\r\n", 4));
    @endcode
*/

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/concepts.hpp>
#include <boost/virtual_serial/awaitable.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/option.hpp>
#include <boost/virtual_serial/detail/telnet.hpp>
#include <boost/virtual_serial/backends/mock_port.hpp>
#include <boost/virtual_serial/backends/physical_port.hpp>
#include <boost/virtual_serial/backends/rfc2217_port.hpp>
#include <boost/virtual_serial/backends/tcp_bridge_port.hpp>
#include <boost/virtual_serial/backends/udp_bridge_port.hpp>
#include <boost/virtual_serial/basic_serial_port.hpp>
#include <boost/virtual_serial/any_serial_port.hpp>
#include <boost/virtual_serial/make_serial_port.hpp>

#endif // BOOST_VIRTUAL_SERIAL_HPP
