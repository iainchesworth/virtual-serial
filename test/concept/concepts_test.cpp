//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Runtime evidence for the C++20 concepts: every front end and backend
// satisfies them, unrelated and half-complete types do not, and a function
// constrained on the concept accepts the ports unchanged.
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/concepts.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>

#include <cstddef>
#include <string>

#if !BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS
# error "this test requires a compiler with C++20 concepts"
#endif

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// Every front end and every backend models the serial stream concept.
static_assert(vs::AsyncSerialStream<vs::mock_port>);
static_assert(vs::AsyncSerialStream<vs::physical_port>);
static_assert(vs::AsyncSerialStream<vs::rfc2217_port>);
static_assert(vs::AsyncSerialStream<vs::tcp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::udp_bridge_port>);
static_assert(vs::AsyncSerialStream<vs::basic_serial_port<vs::mock_port>>);
static_assert(vs::AsyncSerialStream<vs::any_serial_port>);

// A backend additionally reports its kind and carries options.
static_assert(vs::SerialBackend<vs::mock_port>);
static_assert(vs::SerialBackend<vs::tcp_bridge_port>);
static_assert(!vs::SerialBackend<int>);
static_assert(!vs::SerialBackend<vs::serial_config>);

// Option objects are recognised structurally.
static_assert(vs::SerialOption<asio::serial_port_base::baud_rate>);
static_assert(vs::SerialOption<asio::serial_port_base::flow_control>);
static_assert(vs::SerialOption<vs::read_timeout>);
static_assert(vs::SerialOption<vs::write_timeout>);
static_assert(!vs::SerialOption<int>);
static_assert(!vs::SerialOption<vs::serial_config>);

// Unrelated types are rejected.
static_assert(!vs::AsyncSerialStream<int>);
static_assert(!vs::AsyncSerialStream<std::string>);
static_assert(!vs::AsyncSerialStream<vs::serial_config>);

namespace {

// A type with only the synchronous operations must not satisfy the concept.
struct sync_only_stream
{
    using executor_type = asio::any_io_executor;

    executor_type get_executor() const;
    bool is_open() const;
    void open(const std::string&, boost::system::error_code&);
    void close(boost::system::error_code&);
    std::size_t read_some(asio::mutable_buffer, boost::system::error_code&);
    std::size_t write_some(asio::const_buffer, boost::system::error_code&);
};

static_assert(!vs::AsyncSerialStream<sync_only_stream>);

template <vs::AsyncSerialStream Stream>
std::size_t read_byte(Stream& stream, char& value,
    boost::system::error_code& error)
{
    return stream.read_some(asio::buffer(&value, 1), error);
}

template <vs::AsyncSerialStream Stream>
std::size_t write_byte(Stream& stream, char value)
{
    boost::system::error_code error;
    const std::size_t count = stream.write_some(asio::buffer(&value, 1), error);
    VS_REQUIRE(!error);
    return count;
}

template <vs::SerialBackend Backend>
vs::backend_kind kind_of(const Backend& backend)
{
    return backend.backend();
}

template <vs::SerialOption Option>
Option make_option(Option option)
{
    return option;
}

} // namespace

VS_TEST_CASE(concept_constrained_helpers_accept_the_erased_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("concepts", error);
    VS_REQUIRE(!error);

    mock->inject_read("Q");
    char value = 0;
    VS_CHECK_EQ(read_byte(port, value, error), 1u);
    VS_CHECK(!error);
    VS_CHECK_EQ(value, 'Q');

    VS_CHECK_EQ(write_byte(port, 'W'), 1u);
    VS_CHECK_EQ(mock->written(), std::string("W"));
}

VS_TEST_CASE(concept_constrained_helpers_accept_the_typed_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("concepts", error);
    VS_REQUIRE(!error);

    mock->inject_read("Z");
    char value = 0;
    VS_CHECK_EQ(read_byte(port, value, error), 1u);
    VS_CHECK_EQ(value, 'Z');
}

VS_TEST_CASE(concept_constrained_backend_helper_reports_kind)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    VS_CHECK(kind_of(*mock) == vs::backend_kind::mock);
}

VS_TEST_CASE(concept_constrained_option_helper_returns_the_option)
{
    const vs::read_timeout timeout(std::chrono::milliseconds(25));
    const vs::read_timeout copy = make_option(timeout);
    VS_CHECK_EQ(copy.value().count(), 25);
}
