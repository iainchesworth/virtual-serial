//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
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

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

struct fixture
{
    asio::io_context io;
    std::shared_ptr<vs::mock_port> mock;

    fixture()
        : mock(vs::make_mock_port(io.get_executor()))
    {
        boost::system::error_code ec;
        mock->open("mock", ec);
        VS_REQUIRE(!ec);
    }

    fixture(const fixture&) = delete;
    fixture& operator=(const fixture&) = delete;
};

/// One asynchronous read with its own buffer.
struct async_read
{
    char buffer[32] = {};
    boost::system::error_code ec;
    std::size_t transferred = 0;
    int completions = 0;

    template <class Port>
    void start(Port& port, std::size_t size = sizeof(async_read::buffer))
    {
        Port* target = &port;
        target->async_read_some(
            asio::buffer(buffer, size),
            [this](boost::system::error_code error, std::size_t count) {
                ++completions;
                ec = error;
                transferred = count;
            });
    }

    std::string text() const
    {
        return std::string(buffer, transferred);
    }
};

} // namespace

VS_TEST_CASE(mock_open_close_state)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());

    VS_CHECK(!mock->is_open());
    VS_CHECK(mock->backend() == vs::backend_kind::mock);
    VS_CHECK_EQ(mock->native_handle(), 0);

    boost::system::error_code ec;
    mock->open("unit", ec);
    VS_CHECK(!ec);
    VS_CHECK(mock->is_open());
    VS_CHECK_EQ(mock->options().backend, vs::backend_kind::mock);
    VS_CHECK_EQ(mock->options().target, std::string("unit"));

    ec.clear();
    mock->open("unit", ec);
    VS_CHECK(ec == vs::error::already_open);

    ec.clear();
    mock->close(ec);
    VS_CHECK(!ec);
    VS_CHECK(!mock->is_open());

    ec.clear();
    mock->close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    mock->cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);
}

VS_TEST_CASE(mock_inject_and_synchronous_read)
{
    fixture f;
    f.mock->inject_read("hello");
    VS_CHECK_EQ(f.mock->available(), 5u);
    VS_CHECK_EQ(f.mock->injected_bytes(), 5u);

    char buffer[16] = {};
    boost::system::error_code ec;
    const std::size_t transferred =
        f.mock->read_some(asio::buffer(buffer, sizeof(buffer)), ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(transferred, 5u);
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("hello"));
    VS_CHECK_EQ(f.mock->bytes_read(), 5u);
    VS_CHECK_EQ(f.mock->available(), 0u);
}

VS_TEST_CASE(mock_sync_read_would_block_when_empty)
{
    fixture f;
    char buffer[4] = {};
    boost::system::error_code ec;
    const std::size_t transferred =
        f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK_EQ(transferred, 0u);
    VS_CHECK(ec == asio::error::would_block);

    boost::system::error_code closed_ec;
    f.mock->close(closed_ec);
    VS_REQUIRE(!closed_ec);
    char one = 0;
    closed_ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(&one, 1), closed_ec), 0u);
    VS_CHECK(closed_ec == vs::error::not_connected);
}

VS_TEST_CASE(mock_async_read_waits_for_injection)
{
    fixture f;
    async_read read;
    read.start(*f.mock);
    vs_test::pump(f.io);
    VS_CHECK_EQ(read.completions, 0);
    VS_CHECK_EQ(f.mock->pending_reads(), 1u);

    f.mock->inject_read("hi");
    vs_test::pump(f.io);
    VS_CHECK_EQ(read.completions, 1);
    VS_CHECK(!read.ec);
    VS_CHECK_EQ(read.transferred, 2u);
    VS_CHECK_EQ(read.text(), std::string("hi"));
    VS_CHECK_EQ(f.mock->pending_reads(), 0u);
}

VS_TEST_CASE(mock_async_reads_complete_in_order)
{
    fixture f;
    async_read first;
    async_read second;
    first.start(*f.mock);
    second.start(*f.mock);
    vs_test::pump(f.io);
    VS_CHECK_EQ(first.completions, 0);
    VS_CHECK_EQ(second.completions, 0);

    f.mock->inject_read("a");
    vs_test::pump(f.io);
    VS_CHECK_EQ(first.completions, 1);
    VS_CHECK_EQ(second.completions, 0);
    VS_CHECK_EQ(first.text(), std::string("a"));

    f.mock->inject_read("b");
    vs_test::pump(f.io);
    VS_CHECK_EQ(second.completions, 1);
    VS_CHECK_EQ(second.text(), std::string("b"));
}

VS_TEST_CASE(mock_partial_reads)
{
    fixture f;
    f.mock->set_max_read_size(2);
    f.mock->inject_read("abcdef");

    char buffer[16] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 2u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(std::string(buffer, 2), std::string("ab"));

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 2u);
    VS_CHECK_EQ(std::string(buffer, 2), std::string("cd"));

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 2u);
    VS_CHECK_EQ(std::string(buffer, 2), std::string("ef"));

    ec.clear();
    boost::system::error_code final_ec;
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), final_ec), 0u);
    VS_CHECK(final_ec == asio::error::would_block);
}

VS_TEST_CASE(mock_fault_is_reported_before_buffered_data)
{
    fixture f;
    f.mock->inject_read("abc");
    f.mock->inject_framing_error();

    char buffer[8] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == vs::error::framing_error);

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 3u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(std::string(buffer, 3), std::string("abc"));
}

VS_TEST_CASE(mock_all_line_faults)
{
    fixture f;
    f.mock->inject_parity_error();
    f.mock->inject_overrun();
    f.mock->inject_break();

    char buffer[4] = {};
    boost::system::error_code ec;

    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::parity_error);

    ec.clear();
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::overrun);

    ec.clear();
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::break_detected);

    ec.clear();
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == asio::error::would_block);
}

VS_TEST_CASE(mock_arbitrary_error_injection)
{
    fixture f;
    f.mock->inject_error(asio::error::bad_descriptor);
    char one = 0;
    boost::system::error_code ec;
    f.mock->read_some(asio::buffer(&one, 1), ec);
    VS_CHECK(ec == asio::error::bad_descriptor);
}

VS_TEST_CASE(mock_corrupt_byte)
{
    fixture f;
    f.mock->inject_corrupt_byte(vs::fault::framing, 0xAA);

    char buffer[4] = {};
    boost::system::error_code ec;
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::framing_error);

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 1u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(static_cast<int>(static_cast<unsigned char>(buffer[0])), 0xAA);
}

VS_TEST_CASE(mock_eof_semantics)
{
    fixture f;
    f.mock->inject_read("x");
    f.mock->inject_eof();

    char buffer[4] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 1u);
    VS_CHECK(!ec);

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == asio::error::eof);

    // Sticky until reopen.
    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == asio::error::eof);

    boost::system::error_code close_ec;
    f.mock->close(close_ec);
    boost::system::error_code reopen_ec;
    f.mock->open("mock", reopen_ec);
    VS_CHECK(!reopen_ec);
    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == asio::error::would_block);
}

VS_TEST_CASE(mock_hangup_keeps_port_open)
{
    fixture f;
    f.mock->inject_hangup();
    char one = 0;
    boost::system::error_code ec;
    f.mock->read_some(asio::buffer(&one, 1), ec);
    VS_CHECK(ec == vs::error::hangup);
    VS_CHECK(f.mock->is_open());
}

VS_TEST_CASE(mock_disconnect_closes_port)
{
    fixture f;
    async_read pending;
    pending.start(*f.mock);
    vs_test::pump(f.io);
    VS_CHECK_EQ(pending.completions, 0);

    f.mock->inject_disconnect();
    vs_test::pump(f.io);
    VS_CHECK_EQ(pending.completions, 1);
    VS_CHECK(pending.ec == vs::error::not_connected);
    VS_CHECK(!f.mock->is_open());
}

VS_TEST_CASE(mock_capacity_drop_newest)
{
    fixture f;
    f.mock->set_read_capacity(3);
    f.mock->inject_read("abcde");

    VS_CHECK_EQ(f.mock->available(), 3u);
    VS_CHECK_EQ(f.mock->overruns(), 1u);
    VS_CHECK_EQ(f.mock->injected_bytes(), 3u);

    char buffer[8] = {};
    boost::system::error_code ec;
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::overrun);

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 3u);
    VS_CHECK_EQ(std::string(buffer, 3), std::string("abc"));
}

VS_TEST_CASE(mock_capacity_drop_oldest)
{
    fixture f;
    f.mock->set_read_capacity(3);
    f.mock->set_overflow_policy(vs::overflow_policy::drop_oldest);
    f.mock->inject_read("abc");
    f.mock->inject_read("de");

    VS_CHECK_EQ(f.mock->available(), 3u);
    VS_CHECK_EQ(f.mock->overruns(), 1u);

    char buffer[8] = {};
    boost::system::error_code ec;
    f.mock->read_some(asio::buffer(buffer), ec);
    VS_CHECK(ec == vs::error::overrun);

    ec.clear();
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 3u);
    VS_CHECK_EQ(std::string(buffer, 3), std::string("cde"));
}

VS_TEST_CASE(mock_write_capture_and_expectations)
{
    fixture f;
    f.mock->expect_write("AT\r\n");

    boost::system::error_code ec;
    const std::size_t written = f.mock->write_some(asio::buffer("AT\r\n", 4), ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(written, 4u);
    VS_CHECK_EQ(f.mock->written(), std::string("AT\r\n"));
    VS_CHECK_EQ(f.mock->bytes_written(), 4u);
    VS_CHECK(f.mock->expectations_satisfied());

    f.mock->expect_write("OK");
    ec.clear();
    f.mock->write_some(asio::buffer("NO", 2), ec);
    VS_CHECK(!f.mock->expectations_satisfied());

    f.mock->clear_expectations();
    VS_CHECK(f.mock->expectations_satisfied());

    const std::string taken = f.mock->take_written();
    VS_CHECK_EQ(taken, std::string("AT\r\nNO"));
    VS_CHECK(f.mock->written().empty());
    VS_CHECK_EQ(f.mock->bytes_written(), 6u);
}

VS_TEST_CASE(mock_echo)
{
    fixture f;
    f.mock->set_echo(true);
    boost::system::error_code ec;
    f.mock->write_some(asio::buffer("ping", 4), ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(f.mock->available(), 4u);

    char buffer[8] = {};
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 4u);
    VS_CHECK_EQ(std::string(buffer, 4), std::string("ping"));
}

VS_TEST_CASE(mock_write_handler_responds)
{
    fixture f;
    f.mock->set_write_handler(
        [&f](const unsigned char*, std::size_t) {
            f.mock->inject_read("PONG");
        });

    boost::system::error_code ec;
    f.mock->write_some(asio::buffer("PING", 4), ec);
    VS_CHECK(!ec);

    char buffer[8] = {};
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 4u);
    VS_CHECK_EQ(std::string(buffer, 4), std::string("PONG"));
}

VS_TEST_CASE(mock_write_error_injection)
{
    fixture f;
    f.mock->inject_write_error(vs::error::not_connected);

    boost::system::error_code ec;
    VS_CHECK_EQ(f.mock->write_some(asio::buffer("x", 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    VS_CHECK(f.mock->written().empty());

    ec.clear();
    VS_CHECK_EQ(f.mock->write_some(asio::buffer("y", 1), ec), 1u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(f.mock->written(), std::string("y"));
}

VS_TEST_CASE(mock_clocked_pacing_and_step)
{
    vs::serial_config config;
    config.baud_rate = 9600;
    config.data_bits = 8;

    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        std::make_shared<vs::mock_port>(io.get_executor(), config);
    boost::system::error_code ec;
    mock->open("clocked", ec);
    VS_REQUIRE(!ec);

    mock->set_pacing(true);
    mock->inject_read("abc");
    VS_CHECK_EQ(mock->available(), 0u);
    VS_CHECK_EQ(mock->staged_size(), 3u);

    mock->step();
    VS_CHECK_EQ(mock->available(), 1u);
    VS_CHECK_EQ(mock->staged_size(), 2u);
    VS_CHECK_EQ(mock->now(), mock->byte_duration());

    mock->advance(mock->byte_duration() * 2);
    VS_CHECK_EQ(mock->available(), 3u);
    VS_CHECK_EQ(mock->staged_size(), 0u);

    char buffer[4] = {};
    VS_CHECK_EQ(mock->read_some(asio::buffer(buffer), ec), 3u);
    VS_CHECK_EQ(std::string(buffer, 3), std::string("abc"));
}

VS_TEST_CASE(mock_clocked_advance_to_next)
{
    vs::serial_config config;
    config.baud_rate = 4800;

    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        std::make_shared<vs::mock_port>(io.get_executor(), config);
    boost::system::error_code ec;
    mock->open("clocked", ec);
    mock->set_pacing(true);
    mock->inject_read("ab");

    VS_CHECK_EQ(mock->available(), 0u);
    mock->advance_to_next();
    VS_CHECK_EQ(mock->available(), 1u);
    mock->advance_to_next();
    VS_CHECK_EQ(mock->available(), 2u);
    mock->advance_to_next();
    VS_CHECK_EQ(mock->available(), 2u);
}

VS_TEST_CASE(mock_byte_duration_follows_framing)
{
    vs::serial_config config;
    config.baud_rate = 9600;
    config.data_bits = 8;
    config.parity = vs::parity_mode::even;
    config.stop_bits = vs::stop_bits_mode::two;

    asio::io_context io;
    vs::mock_port mock(io.get_executor(), config);
    // start + 8 data + 1 parity + 2 stop = 12 bits at 9600 baud = 1.25 ms.
    VS_CHECK_EQ(mock.byte_duration(), std::chrono::nanoseconds(1250000));

    vs::serial_config fast;
    fast.baud_rate = 115200;
    vs::mock_port fast_port(io.get_executor(), fast);
    const std::chrono::nanoseconds duration = fast_port.byte_duration();
    VS_CHECK(duration > std::chrono::nanoseconds(86000));
    VS_CHECK(duration < std::chrono::nanoseconds(87000));
}

VS_TEST_CASE(mock_read_latency)
{
    fixture f;
    f.mock->set_read_latency(std::chrono::milliseconds(1));
    f.mock->inject_read("x");
    VS_CHECK_EQ(f.mock->available(), 0u);
    VS_CHECK_EQ(f.mock->staged_size(), 1u);

    f.mock->advance(std::chrono::microseconds(999));
    VS_CHECK_EQ(f.mock->available(), 0u);

    f.mock->advance(std::chrono::microseconds(1));
    VS_CHECK_EQ(f.mock->available(), 1u);
}

VS_TEST_CASE(mock_write_latency_async)
{
    fixture f;
    f.mock->set_write_latency(std::chrono::milliseconds(5));

    std::size_t transferred = 0;
    int completions = 0;
    f.mock->async_write_some(
        asio::buffer("abc", 3),
        [&](boost::system::error_code ec, std::size_t count) {
            VS_CHECK(!ec);
            ++completions;
            transferred = count;
        });

    vs_test::pump(f.io);
    VS_CHECK_EQ(completions, 0);
    VS_CHECK_EQ(f.mock->written(), std::string("abc"));

    f.mock->advance(std::chrono::milliseconds(5));
    vs_test::pump(f.io);
    VS_CHECK_EQ(completions, 1);
    VS_CHECK_EQ(transferred, 3u);
}

VS_TEST_CASE(mock_flush_input_releases_staged_bytes)
{
    fixture f;
    f.mock->set_pacing(true);
    f.mock->inject_read("abcd");
    VS_CHECK_EQ(f.mock->available(), 0u);
    f.mock->flush_input();
    VS_CHECK_EQ(f.mock->available(), 4u);
    VS_CHECK_EQ(f.mock->staged_size(), 0u);
}

VS_TEST_CASE(mock_cancel_aborts_pending_reads)
{
    fixture f;
    async_read pending;
    pending.start(*f.mock);
    vs_test::pump(f.io);

    boost::system::error_code ec;
    f.mock->cancel(ec);
    VS_CHECK(!ec);
    vs_test::pump(f.io);
    VS_CHECK_EQ(pending.completions, 1);
    VS_CHECK(pending.ec == asio::error::operation_aborted);
    VS_CHECK(f.mock->is_open());

    async_read afterwards;
    afterwards.start(*f.mock);
    f.mock->inject_read("z");
    vs_test::pump(f.io);
    VS_CHECK_EQ(afterwards.completions, 1);
    VS_CHECK(!afterwards.ec);
}

VS_TEST_CASE(mock_inject_before_open_is_preserved)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    mock->inject_read("pre");

    boost::system::error_code ec;
    mock->open("later", ec);
    VS_REQUIRE(!ec);

    char buffer[4] = {};
    VS_CHECK_EQ(mock->read_some(asio::buffer(buffer), ec), 3u);
    VS_CHECK_EQ(std::string(buffer, 3), std::string("pre"));
}

VS_TEST_CASE(mock_apply_options_validates)
{
    fixture f;
    vs::serial_config invalid;
    invalid.data_bits = 9;
    boost::system::error_code ec;
    f.mock->apply_options(invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);

    vs::serial_config zero_baud;
    zero_baud.baud_rate = 0;
    ec.clear();
    f.mock->apply_options(zero_baud, ec);
    VS_CHECK(ec == vs::error::invalid_option);

    vs::serial_config valid;
    valid.baud_rate = 57600;
    valid.parity = vs::parity_mode::odd;
    ec.clear();
    f.mock->apply_options(valid, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(f.mock->options().baud_rate, 57600u);
    VS_CHECK(f.mock->options().parity == vs::parity_mode::odd);
}

VS_TEST_CASE(mock_reset_state)
{
    fixture f;
    f.mock->inject_read("data");
    boost::system::error_code ec;
    f.mock->write_some(asio::buffer("out", 3), ec);
    f.mock->set_read_capacity(2);
    f.mock->inject_read("overflow");

    f.mock->reset_state();
    VS_CHECK_EQ(f.mock->available(), 0u);
    VS_CHECK_EQ(f.mock->staged_size(), 0u);
    VS_CHECK(f.mock->written().empty());
    VS_CHECK_EQ(f.mock->bytes_read(), 0u);
    VS_CHECK_EQ(f.mock->bytes_written(), 0u);
    VS_CHECK_EQ(f.mock->overruns(), 0u);
    VS_CHECK_EQ(f.mock->injected_bytes(), 0u);
    VS_CHECK_EQ(f.mock->now(), std::chrono::nanoseconds(0));
}

VS_TEST_CASE(mock_sync_and_async_interoperate)
{
    fixture f;
    f.mock->inject_read("sync");
    char buffer[8] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.mock->read_some(asio::buffer(buffer), ec), 4u);

    async_read pending;
    pending.start(*f.mock);
    vs_test::pump(f.io);
    f.mock->inject_read("async");
    vs_test::pump(f.io);
    VS_CHECK_EQ(pending.completions, 1);
    VS_CHECK_EQ(pending.text(), std::string("async"));
}

VS_TEST_CASE(mock_moved_port_keeps_working)
{
    fixture f;
    f.mock->inject_read("moved");
    vs::mock_port moved(std::move(*f.mock));
    VS_CHECK(moved.is_open());

    char buffer[8] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(moved.read_some(asio::buffer(buffer), ec), 5u);
    VS_CHECK_EQ(std::string(buffer, 5), std::string("moved"));
}
