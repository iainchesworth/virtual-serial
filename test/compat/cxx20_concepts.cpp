//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// The library targets C++14 at its core, but its types are already shaped
// for C++20 concepts.  This translation unit turns that claim into a
// compile-time contract.
//

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/concepts.hpp>

#include <boost/asio/serial_port_base.hpp>

#if !BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS
# error "this test requires a compiler with C++20 concepts"
#endif

namespace vs = boost::virtual_serial;

// Every front end and every backend models the serial stream concept.
static_assert(vs::AsyncSerialStream<vs::mock_port>);
static_assert(vs::AsyncSerialStream<vs::physical_port>);
static_assert(vs::AsyncSerialStream<vs::rfc2217_port>);
static_assert(vs::AsyncSerialStream<vs::tcp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::udp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::any_serial_port>);
static_assert(vs::AsyncSerialStream<vs::basic_serial_port<vs::mock_port>>);

// A backend additionally reports its kind and carries options.
static_assert(vs::SerialBackend<vs::mock_port>);
static_assert(vs::SerialBackend<vs::physical_port>);
static_assert(!vs::SerialBackend<int>);

// Option objects are recognised structurally.
static_assert(vs::SerialOption<boost::asio::serial_port_base::baud_rate>);
static_assert(vs::SerialOption<boost::asio::serial_port_base::parity>);
static_assert(vs::SerialOption<vs::read_timeout>);
static_assert(vs::SerialOption<vs::write_timeout>);

struct vendor_option
{
    int value;
};

static_assert(!vs::SerialOption<vendor_option>);
static_assert(!vs::SerialOption<int>);

// Unrelated types are rejected.
static_assert(!vs::AsyncSerialStream<int>);
static_assert(!vs::AsyncSerialStream<vs::serial_config>);

int main()
{
    boost::asio::io_context io;
    vs::any_serial_port port(io.get_executor());
    return port.is_open() ? 1 : 0;
}
