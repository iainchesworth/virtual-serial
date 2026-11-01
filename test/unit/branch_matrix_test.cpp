//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Branch-oriented tests: equality short circuits, query key spellings,
// degenerate containers, empty buffers and null front ends.  They exist to
// keep both directions of every non-trivial branch exercised.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/read_queue.hpp>
#include <boost/virtual_serial/detail/telnet.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
// serial_config: equality short circuits and parser branches
// ---------------------------------------------------------------------------

VS_TEST_CASE(config_equality_short_circuits_each_field)
{
    vs::serial_config base;
    base.backend = vs::backend_kind::tcp;
    base.target = "host:1";
    base.baud_rate = 115200;
    base.data_bits = 7;
    base.parity = vs::parity_mode::even;
    base.stop_bits = vs::stop_bits_mode::two;
    base.flow_control = vs::flow_control_mode::hardware;
    base.read_timeout = std::chrono::milliseconds(1);
    base.write_timeout = std::chrono::milliseconds(2);
    base.exclusive = true;

    vs::serial_config other = base;
    VS_CHECK(base == other);

    other = base;
    other.backend = vs::backend_kind::mock;
    VS_CHECK(base != other);

    other = base;
    other.target = "host:2";
    VS_CHECK(base != other);

    other = base;
    other.baud_rate = 9600;
    VS_CHECK(base != other);

    other = base;
    other.data_bits = 8;
    VS_CHECK(base != other);

    other = base;
    other.parity = vs::parity_mode::odd;
    VS_CHECK(base != other);

    other = base;
    other.stop_bits = vs::stop_bits_mode::one;
    VS_CHECK(base != other);

    other = base;
    other.flow_control = vs::flow_control_mode::none;
    VS_CHECK(base != other);

    other = base;
    other.read_timeout = std::chrono::milliseconds(3);
    VS_CHECK(base != other);

    other = base;
    other.write_timeout = std::chrono::milliseconds(4);
    VS_CHECK(base != other);

    other = base;
    other.exclusive = false;
    VS_CHECK(base != other);
}

VS_TEST_CASE(config_long_form_query_keys)
{
    const vs::serial_config config = vs::parse_uri(
        "serial://COM1?baud_rate=19200&data_bits=7&stop_bits=2"
        "&flow_control=hardware&read_timeout=3ms&write_timeout=4ms"
        "&exclusive=1&parity=odd");

    VS_CHECK_EQ(config.baud_rate, 19200u);
    VS_CHECK_EQ(config.data_bits, 7u);
    VS_CHECK(config.stop_bits == vs::stop_bits_mode::two);
    VS_CHECK(config.flow_control == vs::flow_control_mode::hardware);
    VS_CHECK_EQ(config.read_timeout.count(), 3);
    VS_CHECK_EQ(config.write_timeout.count(), 4);
    VS_CHECK(config.exclusive);
    VS_CHECK(config.parity == vs::parity_mode::odd);
}

VS_TEST_CASE(config_keyword_forms_for_each_option)
{
    VS_CHECK(vs::parse_uri("serial://C1?parity=none").parity == vs::parity_mode::none);
    VS_CHECK(vs::parse_uri("serial://C1?parity=mark").parity == vs::parity_mode::mark);
    VS_CHECK(vs::parse_uri("serial://C1?stop=one").stop_bits == vs::stop_bits_mode::one);
    VS_CHECK(
        vs::parse_uri("serial://C1?flow=software").flow_control
        == vs::flow_control_mode::software);
    VS_CHECK(
        vs::parse_uri("serial://C1?flow=hardware").flow_control
        == vs::flow_control_mode::hardware);
    VS_CHECK(
        vs::parse_uri("serial://C1?flow=0").flow_control
        == vs::flow_control_mode::none);
    VS_CHECK(vs::parse_uri("serial://C1?exclusive=1").exclusive);
    VS_CHECK(!vs::parse_uri("serial://C1?exclusive=0").exclusive);
}

VS_TEST_CASE(config_malformed_unsigned_and_duration_values)
{
    boost::system::error_code ec;

    ec.clear();
    vs::parse_uri("serial://C1?baud=12x", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://C1?baud=", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    // An empty duration has no digits.
    ec.clear();
    vs::parse_uri("serial://C1?timeout=s", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    // A valid number whose scaled value overflows.
    ec.clear();
    vs::parse_uri("serial://C1?timeout=4294967295s", ec);
    VS_CHECK(ec == vs::error::invalid_option);
}

VS_TEST_CASE(config_malformed_bracketed_ipv6)
{
    const char* targets[] = {
        "tcp://[::1",
        "tcp://[::1]",
        "tcp://[::1]x4001",
        "tcp://[]:1",
        "tcp://[::1]:",
    };

    for (const char* target : targets)
    {
        boost::system::error_code ec;
        vs::parse_uri(target, ec);
        VS_CHECK(ec == vs::error::bad_target);
    }
}

// ---------------------------------------------------------------------------
// read queue degenerate drains
// ---------------------------------------------------------------------------

VS_TEST_CASE(read_queue_zero_length_and_limited_drains)
{
    asio::io_context io;
    vs::detail::read_queue queue(io.get_executor());

    queue.drop(0); // no-op
    VS_CHECK_EQ(queue.available(), 0u);

    queue.push(reinterpret_cast<const unsigned char*>("abcdef"), 6);

    // A zero sized destination drains nothing.
    char single = 0;
    boost::system::error_code ec;
    VS_CHECK_EQ(
        queue.read(vs::detail::mutable_buffer_range(asio::buffer(&single, 0)), ec),
        0u);
    VS_CHECK_EQ(queue.available(), 6u);

    // The max read size caps a two buffer drain.
    queue.set_max_read_size(3);
    char first[2] = {};
    char second[4] = {};
    const std::array<asio::mutable_buffer, 2> buffers = {
        asio::buffer(first), asio::buffer(second)};
    VS_CHECK_EQ(
        queue.read(vs::detail::mutable_buffer_range(buffers), ec), 3u);
    VS_CHECK_EQ(queue.available(), 3u);
    VS_CHECK_EQ(first[0], 'a');
    VS_CHECK_EQ(second[0], 'c');
}

VS_TEST_CASE(read_queue_finished_with_only_eof)
{
    asio::io_context io;
    vs::detail::read_queue queue(io.get_executor());
    VS_CHECK(!queue.finished());
    queue.set_eof();
    VS_CHECK(queue.finished());
    VS_CHECK(queue.empty());
}

// ---------------------------------------------------------------------------
// buffer range degenerate scatter/gather
// ---------------------------------------------------------------------------

VS_TEST_CASE(buffer_range_scatter_and_gather_degenerate_cases)
{
    char destination[4] = {};
    char zero_length = 0;

    std::vector<asio::mutable_buffer> buffers;
    buffers.push_back(asio::buffer(&zero_length, 0));
    buffers.push_back(asio::buffer(destination));
    vs::detail::mutable_buffer_range range(buffers);

    const unsigned char payload[4] = {1, 2, 3, 4};
    VS_CHECK_EQ(vs::detail::scatter(payload, 4, range), 4u);
    VS_CHECK_EQ(static_cast<int>(destination[0]), 1);

    // Gathering into no space at all copies nothing.
    unsigned char gathered[4] = {};
    VS_CHECK_EQ(vs::detail::gather(range, gathered, 0), 0u);

    // A zero length source buffer is skipped.
    std::vector<asio::const_buffer> sources;
    sources.push_back(asio::buffer(&zero_length, 0));
    sources.push_back(asio::buffer("xy", 2));
    vs::detail::const_buffer_range const_range(sources);
    VS_CHECK_EQ(vs::detail::gather(const_range, gathered, sizeof(gathered)), 2u);
    VS_CHECK_EQ(gathered[0], static_cast<unsigned char>('x'));
}

// ---------------------------------------------------------------------------
// telnet mapping fallbacks and sink-less decoding
// ---------------------------------------------------------------------------

VS_TEST_CASE(telnet_mappings_reject_unknown_indices)
{
    namespace tn = vs::detail::telnet;
    VS_CHECK_EQ(
        static_cast<int>(tn::data_size_index(9)), 9);
    VS_CHECK_EQ(
        static_cast<int>(tn::parity_index(static_cast<vs::parity_mode>(99))), 1);
    VS_CHECK_EQ(
        static_cast<int>(tn::stop_size_index(static_cast<vs::stop_bits_mode>(99))), 1);
}

VS_TEST_CASE(telnet_decoder_sinkless_commands)
{
    vs::detail::telnet::decoder decoder;
    const unsigned char negotiation[] = {255, 251, 44};
    decoder.feed(negotiation, sizeof(negotiation));

    const unsigned char subnegotiation[] = {255, 250, 44, 6, 8, 255, 240};
    decoder.feed(subnegotiation, sizeof(subnegotiation));

    const unsigned char data[] = {'o', 'k'};
    decoder.feed(data, sizeof(data));
}

// ---------------------------------------------------------------------------
// option branches that need the "one" and "none"/"hardware" paths
// ---------------------------------------------------------------------------

VS_TEST_CASE(option_one_stop_bit_and_flow_modes)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());
    boost::system::error_code ec;
    port.open("branches", ec);
    VS_REQUIRE(!ec);

    port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::one), ec);
    VS_CHECK(!ec);
    asio::serial_port_base::stop_bits stop;
    port.get_option(stop, ec);
    VS_CHECK(!ec);
    VS_CHECK(stop.value() == asio::serial_port_base::stop_bits::one);

    port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::none), ec);
    VS_CHECK(!ec);
    asio::serial_port_base::flow_control flow;
    port.get_option(flow, ec);
    VS_CHECK(flow.value() == asio::serial_port_base::flow_control::none);

    port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::hardware), ec);
    VS_CHECK(!ec);
    port.get_option(flow, ec);
    VS_CHECK(flow.value() == asio::serial_port_base::flow_control::hardware);
}

// ---------------------------------------------------------------------------
// mock fixture degenerate clock inputs
// ---------------------------------------------------------------------------

VS_TEST_CASE(mock_fixture_degenerate_clock_and_empty_injection)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("degenerate", ec);
    VS_REQUIRE(!ec);

    mock.inject_read(std::string()); // zero length: no staging at all
    VS_CHECK_EQ(mock.available(), 0u);
    VS_CHECK_EQ(mock.staged_size(), 0u);

    mock.set_pacing(true);
    mock.inject_read("a");
    mock.step(0);                    // no bytes requested
    VS_CHECK_EQ(mock.available(), 0u);
    mock.step(5);                    // more than staged
    VS_CHECK_EQ(mock.available(), 1u);

    mock.advance(std::chrono::milliseconds(1)); // nothing due
    mock.flush_input();                         // nothing staged
    VS_CHECK_EQ(mock.available(), 1u);
}

// ---------------------------------------------------------------------------
// null front end and erased accessors
// ---------------------------------------------------------------------------

VS_TEST_CASE(any_port_null_backend_accessors)
{
    asio::io_context io;
    vs::any_serial_port port(io.get_executor());

    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::mock);
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK_EQ(port.config().baud_rate, 9600u);
    VS_CHECK(vs::backend_cast<vs::mock_port>(port) == nullptr);
    VS_CHECK(port.native_backend() == nullptr);
}

VS_TEST_CASE(udp_bridge_rejects_invalid_options)
{
    asio::io_context io;
    vs::udp_bridge_port port(io.get_executor());

    vs::serial_config invalid;
    invalid.data_bits = 9;
    boost::system::error_code ec;
    port.apply_options(invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);
}
