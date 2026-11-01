//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// C++20 concept example: the public concepts accept every front end and
// backend, reject unrelated types, and constrain ordinary function templates.
//

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/concepts.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <iostream>
#include <string>

#if !BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS

int main()
{
    std::cout << "This example needs a C++20 compiler with concepts.\n";
    return 0;
}

#else

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

static_assert(vs::AsyncSerialStream<vs::mock_port>);
static_assert(vs::AsyncSerialStream<vs::physical_port>);
static_assert(vs::AsyncSerialStream<vs::rfc2217_port>);
static_assert(vs::AsyncSerialStream<vs::tcp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::udp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::any_serial_port>);
static_assert(vs::SerialBackend<vs::mock_port>);
static_assert(vs::SerialOption<vs::read_timeout>);

static_assert(!vs::AsyncSerialStream<int>);
static_assert(!vs::SerialBackend<std::string>);

// An ordinary function template, constrained by the public concept.
template <vs::AsyncSerialStream Stream>
std::size_t send(Stream& stream, const char* data, std::size_t size)
{
    boost::system::error_code error;
    const std::size_t written =
        stream.write_some(asio::buffer(data, size), error);
    if (error)
        throw boost::system::system_error(error);
    return written;
}

// A backend can be described without knowing its concrete type.
template <vs::SerialBackend Backend>
const char* kind_name(const Backend& backend)
{
    switch (backend.backend())
    {
    case vs::backend_kind::physical:
        return "physical";
    case vs::backend_kind::mock:
        return "mock";
    case vs::backend_kind::rfc2217:
        return "rfc2217";
    case vs::backend_kind::tcp:
        return "tcp";
    case vs::backend_kind::udp:
        return "udp";
    }
    return "unknown";
}

int main()
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);

    boost::system::error_code error;
    port.open("mock://concepts", error);
    if (error)
    {
        std::cerr << "open failed: " << error.message() << '\n';
        return 1;
    }

    std::cout << "backend: " << kind_name(*mock) << '\n';
    const std::size_t written = send(port, "AT\r\n", 4);
    std::cout << "wrote " << written << " bytes: " << mock->written() << '\n';

    return mock->written() == "AT\r\n" ? 0 : 1;
}

#endif // BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS
