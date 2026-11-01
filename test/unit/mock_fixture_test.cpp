//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Fixture API coverage: every injection, clock and inspection helper on the
// mock backend, including the degenerate inputs.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

VS_TEST_CASE(mock_fixture_default_construction_and_accessors)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());

    VS_CHECK(mock.get_executor() == io.get_executor());
    VS_CHECK(mock.backend() == vs::backend_kind::mock);
    VS_CHECK(!mock.is_open());
    VS_CHECK_EQ(mock.native_handle(), 0);
    VS_CHECK_EQ(&mock.lowest_layer(), &mock);
    const vs::mock_port& const_mock = mock;
    VS_CHECK_EQ(&const_mock.lowest_layer(), &mock);

    VS_CHECK_EQ(mock.options().baud_rate, 9600u);
    VS_CHECK_EQ(mock.pacing_enabled(), false);
    VS_CHECK_EQ(mock.read_latency().count(), 0);
    VS_CHECK_EQ(mock.write_latency().count(), 0);
    VS_CHECK_EQ(mock.read_capacity(), 0u);
    VS_CHECK(mock.get_overflow_policy() == vs::overflow_policy::drop_newest);
}

VS_TEST_CASE(mock_fixture_injection_overloads)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("", ec); // empty target is allowed for a mock
    VS_REQUIRE(!ec);

    mock.inject_read("literal");           // char array overload, strips NUL
    mock.inject_read(std::string("str"));  // std::string overload
    const char* raw = "raw";
    mock.inject_read(raw, 3);              // pointer + length
    const std::vector<unsigned char> bytes = {'v', 'e', 'c'};
    mock.inject_read(bytes);               // vector overload
    mock.inject_read(std::vector<unsigned char>()); // empty vector is a no-op
    mock.inject_line("line");              // adds CRLF
    mock.inject_read_after(std::chrono::milliseconds(1), "later");

    VS_CHECK_EQ(mock.available(), 7u + 3u + 3u + 3u + 6u);
    VS_CHECK_EQ(mock.staged_size(), 5u);

    char buffer[32] = {};
    std::string collected;
    while (mock.available() > 0)
    {
        ec.clear();
        const std::size_t n = mock.read_some(asio::buffer(buffer), ec);
        if (ec)
            break;
        collected.append(buffer, n);
    }
    VS_CHECK_EQ(collected, std::string("literalstrrawvecline\r\n"));

    mock.advance(std::chrono::milliseconds(1));
    VS_CHECK_EQ(mock.available(), 5u);
}

VS_TEST_CASE(mock_fixture_write_paths)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;

    // Closed port.
    char one = 'x';
    VS_CHECK_EQ(mock.write_some(asio::buffer(&one, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    mock.open("writes", ec);
    VS_REQUIRE(!ec);

    // Empty write is a no-op.
    ec.clear();
    VS_CHECK_EQ(mock.write_some(asio::buffer(&one, 0), ec), 0u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(mock.bytes_written(), 0u);

    // Synchronous write with latency advances the virtual clock.
    mock.set_write_latency(std::chrono::milliseconds(3));
    ec.clear();
    VS_CHECK_EQ(mock.write_some(asio::buffer("abc", 3), ec), 3u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(mock.now(), std::chrono::milliseconds(3));

    // Write error is consumed once.
    mock.inject_write_error(vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(mock.write_some(asio::buffer("d", 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(mock.write_some(asio::buffer("e", 1), ec), 1u);
    VS_CHECK(!ec);

    // Captured output helpers.
    VS_CHECK_EQ(mock.written(), std::string("abce"));
    mock.clear_written();
    VS_CHECK(mock.written().empty());
    VS_CHECK_EQ(mock.bytes_written(), 4u);
}

VS_TEST_CASE(mock_fixture_clock_controls)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("clock", ec);
    VS_REQUIRE(!ec);

    mock.advance_to_next(); // no scheduled events: must be a no-op
    mock.set_byte_duration(std::chrono::microseconds(50));
    VS_CHECK_EQ(mock.byte_duration(), std::chrono::microseconds(50));
    mock.reset_byte_duration();
    VS_CHECK(mock.byte_duration() != std::chrono::microseconds(50));

    // advance_to_next drives a delayed write to completion.
    mock.set_write_latency(std::chrono::milliseconds(2));
    bool done = false;
    mock.async_write_some(asio::buffer("z", 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(!error);
            done = true;
        });
    vs_test::pump(io);
    VS_CHECK(!done);
    mock.advance_to_next();
    vs_test::pump(io);
    VS_CHECK(done);
}

VS_TEST_CASE(mock_fixture_pacing_helpers)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("pacing", ec);
    VS_REQUIRE(!ec);

    mock.set_pacing(true);
    VS_CHECK(mock.pacing_enabled());
    mock.inject_read("ab");
    VS_CHECK_EQ(mock.staged_size(), 2u);

    // advance_to_next releases exactly one byte per call.
    mock.advance_to_next();
    VS_CHECK_EQ(mock.available(), 1u);
    mock.flush_input();
    VS_CHECK_EQ(mock.available(), 2u);
    VS_CHECK_EQ(mock.staged_size(), 0u);
    mock.set_pacing(false);
    VS_CHECK(!mock.pacing_enabled());
}

VS_TEST_CASE(mock_fixture_overflow_policy_drops_staged_bytes)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("overflow", ec);
    VS_REQUIRE(!ec);

    mock.set_pacing(true);
    mock.set_read_capacity(2);
    mock.set_overflow_policy(vs::overflow_policy::drop_oldest);
    VS_CHECK(mock.get_overflow_policy() == vs::overflow_policy::drop_oldest);

    mock.inject_read("ab");   // staged, nothing visible yet
    VS_CHECK_EQ(mock.staged_size(), 2u);
    mock.inject_read("c");    // overflows and must evict a staged byte
    VS_CHECK_EQ(mock.overruns(), 1u);
    VS_CHECK_EQ(mock.staged_size(), 2u);

    mock.flush_input();
    char buffer[8] = {};

    // The overflow fault is reported first, then the surviving bytes.
    ec.clear();
    VS_CHECK_EQ(mock.read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == vs::error::overrun);

    ec.clear();
    VS_CHECK_EQ(mock.read_some(asio::buffer(buffer), ec), 2u);
    VS_CHECK_EQ(std::string(buffer, 2), std::string("bc"));
}

VS_TEST_CASE(mock_fixture_closed_async_operations)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());

    char buffer[4] = {};
    bool read_done = false;
    bool write_done = false;
    mock.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            read_done = true;
        });
    mock.async_write_some(asio::buffer(buffer, 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            write_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(read_done);
    VS_CHECK(write_done);
}

VS_TEST_CASE(mock_fixture_write_error_on_async_path)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("async-write", ec);
    VS_REQUIRE(!ec);

    mock.inject_write_error(vs::error::queue_full);
    bool done = false;
    mock.async_write_some(asio::buffer("q", 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::queue_full);
            done = true;
        });
    vs_test::pump(io);
    VS_CHECK(done);
    VS_CHECK(mock.written().empty());
}

VS_TEST_CASE(mock_fixture_cancel_aborts_pending_write)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("cancel", ec);
    VS_REQUIRE(!ec);

    mock.set_write_latency(std::chrono::milliseconds(10));
    bool done = false;
    mock.async_write_some(asio::buffer("w", 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == asio::error::operation_aborted);
            done = true;
        });
    vs_test::pump(io);
    VS_CHECK(!done);
    mock.cancel(ec);
    vs_test::pump(io);
    VS_CHECK(done);
}

VS_TEST_CASE(mock_fixture_framing_of_custom_configurations)
{
    asio::io_context io;

    // 1.5 stop bits at 5 data bits, with parity.
    vs::serial_config one_and_a_half;
    one_and_a_half.baud_rate = 1200;
    one_and_a_half.data_bits = 5;
    one_and_a_half.parity = vs::parity_mode::even;
    one_and_a_half.stop_bits = vs::stop_bits_mode::one_point_five;
    vs::mock_port first(io.get_executor(), one_and_a_half);
    VS_CHECK(first.byte_duration() > std::chrono::nanoseconds(0));

    // Two stop bits, no parity.
    vs::serial_config two_stop;
    two_stop.baud_rate = 1200;
    two_stop.stop_bits = vs::stop_bits_mode::two;
    vs::mock_port second(io.get_executor(), two_stop);
    VS_CHECK(second.byte_duration() > first.byte_duration() / 2);

    // A zero baud rate yields a zero pacing interval rather than dividing
    // by zero (the constructor does not validate).
    vs::serial_config zero;
    zero.baud_rate = 0;
    vs::mock_port third(io.get_executor(), zero);
    VS_CHECK_EQ(third.byte_duration(), std::chrono::nanoseconds(0));

    // The fault-to-error mapping is exhaustive; an out-of-range value maps
    // to success.
    vs::mock_port fourth(io.get_executor());
    fourth.inject_line_error(static_cast<vs::fault>(99));
    char one = 0;
    boost::system::error_code ec;
    fourth.open("fault", ec);
    VS_REQUIRE(!ec);
    ec.clear();
    fourth.read_some(asio::buffer(&one, 1), ec);
    VS_CHECK(!ec);
}

VS_TEST_CASE(mock_fixture_expectations_and_handlers)
{
    asio::io_context io;
    vs::mock_port mock(io.get_executor());
    boost::system::error_code ec;
    mock.open("expect", ec);
    VS_REQUIRE(!ec);

    mock.expect_write("AB");
    mock.write_some(asio::buffer("AB", 2), ec);
    VS_CHECK(mock.expectations_satisfied());
    mock.clear_expectations();
    VS_CHECK(mock.expectations_satisfied());

    int handled = 0;
    mock.set_write_handler(
        [&](const unsigned char*, std::size_t) { ++handled; });
    mock.write_some(asio::buffer("C", 1), ec);
    VS_CHECK_EQ(handled, 1);

    mock.set_echo(true);
    mock.write_some(asio::buffer("D", 1), ec);
    VS_CHECK_EQ(mock.available(), 1u);
    mock.set_echo(false);
    mock.write_some(asio::buffer("E", 1), ec);
    VS_CHECK_EQ(mock.available(), 1u);
}
