//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// The "all modes, all directions" conformance matrix.
//
// Each backend that can be brought up in-process is driven through:
//   * host -> device and device -> host traffic,
//   * synchronous and asynchronous entry points,
//   * every injectable fault followed by a successful recovery,
//   * timeout, disconnect and reopen recovery.
//
// The physical backend has its own loopback test (physical_loopback_test.cpp)
// because it needs a real device pair.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/rfc2217_test_server.hpp"
#include "../mock/tcp_echo_server.hpp"
#include "../mock/udp_echo_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

constexpr std::chrono::milliseconds kTimeout(4000);

/// One asynchronous read whose buffer outlives the operation.
struct read_op
{
    char data[128] = {};
    boost::system::error_code ec;
    std::size_t transferred = 0;
    int completions = 0;

    template <class Port>
    void start(Port& port, std::size_t size = sizeof(read_op::data))
    {
        Port* target = &port;
        target->async_read_some(
            asio::buffer(data, size),
            [this](boost::system::error_code error, std::size_t count) {
                ++completions;
                ec = error;
                transferred = count;
            });
    }

    bool done() const noexcept
    {
        return completions != 0;
    }

    std::string text() const
    {
        return std::string(data, transferred);
    }

    void reset()
    {
        ec.clear();
        transferred = 0;
        completions = 0;
    }
};

bool wait(asio::io_context& io, const read_op& op)
{
    return vs_test::pump_until(io, [&op] { return op.done(); }, kTimeout);
}

/// Runs one async read to completion and returns it.
read_op read_once(asio::io_context& io, vs::any_serial_port& port)
{
    read_op op;
    op.start(port);
    wait(io, op);
    return op;
}

template <class Port>
read_op read_once(asio::io_context& io, Port& port)
{
    read_op op;
    op.start(port);
    wait(io, op);
    return op;
}

} // namespace

// ---------------------------------------------------------------------------
// Mock backend: every injection mode, both directions, recovery after faults.
// ---------------------------------------------------------------------------

VS_TEST_CASE(modes_mock_bidirectional_and_fault_recovery)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("matrix", ec);
    VS_REQUIRE(!ec);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)), ec);

    // host -> device
    VS_CHECK_EQ(asio::write(port, asio::buffer("OUT", 3)), 3u);
    VS_CHECK_EQ(mock->written(), std::string("OUT"));

    // device -> host, asynchronous
    mock->inject_read("IN-ASYNC");
    read_op async_in = read_once(io, port);
    VS_CHECK(!async_in.ec);
    VS_CHECK_EQ(async_in.text(), std::string("IN-ASYNC"));

    // host -> device, asynchronous
    bool wrote = false;
    port.async_write_some(asio::buffer("ASY", 3),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            VS_CHECK_EQ(count, 3u);
            wrote = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return wrote; }, kTimeout));
    VS_CHECK_EQ(mock->written(), std::string("OUTASY"));

    // device -> host, synchronous
    mock->inject_read("IN-SYNC");
    char buffer[16] = {};
    ec.clear();
    const std::size_t received = port.read_some(asio::buffer(buffer), ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(std::string(buffer, received), std::string("IN-SYNC"));

    // Every line fault, each followed by a successful recovery read.
    struct fault_case
    {
        vs::fault value;
        vs::error expected;
    };
    const fault_case faults[] = {
        {vs::fault::framing, vs::error::framing_error},
        {vs::fault::parity, vs::error::parity_error},
        {vs::fault::overrun, vs::error::overrun},
        {vs::fault::break_condition, vs::error::break_detected}};

    for (const fault_case& injected : faults)
    {
        mock->inject_line_error(injected.value);
        read_op failure = read_once(io, port);
        VS_CHECK(failure.ec == injected.expected);

        mock->inject_read("RECOVERED");
        read_op recovered = read_once(io, port);
        VS_CHECK(!recovered.ec);
        VS_CHECK_EQ(recovered.text(), std::string("RECOVERED"));
    }

    // Hangup is terminal until the port is reopened.
    mock->inject_hangup();
    read_op hung_up = read_once(io, port);
    VS_CHECK(hung_up.ec == vs::error::hangup);
    port.close(ec);
    VS_REQUIRE(!ec);
    port.open("matrix", ec);
    VS_REQUIRE(!ec);
    mock->inject_read("AFTER-HANGUP");
    read_op after_hangup = read_once(io, port);
    VS_CHECK(!after_hangup.ec);
    VS_CHECK_EQ(after_hangup.text(), std::string("AFTER-HANGUP"));

    // Sudden disconnect closes the port; reopening restores both directions.
    mock->inject_disconnect();
    read_op disconnected = read_once(io, port);
    VS_CHECK(disconnected.ec == vs::error::not_connected);
    VS_CHECK(!port.is_open());
    port.open("matrix", ec);
    VS_REQUIRE(!ec);
    VS_CHECK_EQ(asio::write(port, asio::buffer("BACK", 4)), 4u);
    mock->inject_read("ONLINE");
    read_op online = read_once(io, port);
    VS_CHECK_EQ(online.text(), std::string("ONLINE"));

    // A read timeout must not poison the port.
    port.set_option(vs::read_timeout(std::chrono::milliseconds(20)), ec);
    read_op timed_out = read_once(io, port);
    VS_CHECK(timed_out.ec == vs::error::timeout);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)), ec);
    mock->inject_read("AFTER-TIMEOUT");
    read_op after_timeout = read_once(io, port);
    VS_CHECK(!after_timeout.ec);
    VS_CHECK_EQ(after_timeout.text(), std::string("AFTER-TIMEOUT"));

    // Overflow is reported, and the queue is usable again afterwards.
    mock->set_read_capacity(2);
    mock->inject_read("abcdef");
    read_op overflow = read_once(io, port);
    VS_CHECK(overflow.ec == vs::error::overrun);
    mock->set_read_capacity(0);
    mock->reset_state();
    mock->inject_read("CLEAN");
    read_op clean = read_once(io, port);
    VS_CHECK(!clean.ec);
    VS_CHECK_EQ(clean.text(), std::string("CLEAN"));
}

// ---------------------------------------------------------------------------
// TCP bridge
// ---------------------------------------------------------------------------

VS_TEST_CASE(modes_tcp_bidirectional_and_recovery)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "tcp://127.0.0.1:" + std::to_string(server.port()));
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));
    VS_REQUIRE(port.is_open());

    // host -> device
    VS_CHECK_EQ(asio::write(port, asio::buffer("PING", 4)), 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "PING"; }, kTimeout));

    // device -> host (echo), asynchronous
    read_op echo = read_once(io, port);
    VS_CHECK(!echo.ec);
    VS_CHECK_EQ(echo.text(), std::string("PING"));

    // device -> host, pushed by the peer
    server.send("PUSH");
    read_op pushed = read_once(io, port);
    VS_CHECK(!pushed.ec);
    VS_CHECK_EQ(pushed.text(), std::string("PUSH"));

    // host -> device, asynchronous
    bool wrote = false;
    port.async_write_some(asio::buffer("ASYN", 4),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(!error);
            wrote = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return wrote; }, kTimeout));
    VS_CHECK(vs_test::pump_until(
        io,
        [&] { return server.received().find("ASYN") != std::string::npos; },
        kTimeout));

    // The echo server sends the asynchronous write straight back; consume it
    // before testing the timeout path.
    read_op async_echo = read_once(io, port);
    VS_CHECK(!async_echo.ec);
    VS_CHECK_EQ(async_echo.text(), std::string("ASYN"));

    // timeout and recovery
    port.set_option(vs::read_timeout(std::chrono::milliseconds(30)));
    read_op timed_out = read_once(io, port);
    VS_CHECK(timed_out.ec == vs::error::timeout);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));
    server.send("RECOVERED");
    read_op recovered = read_once(io, port);
    VS_CHECK(!recovered.ec);
    VS_CHECK_EQ(recovered.text(), std::string("RECOVERED"));

    // peer loss surfaces as a read error, and a reconnect restores service
    read_op pending;
    pending.start(port);
    vs_test::pump(io);
    server.close_peer();
    VS_CHECK(vs_test::pump_until(io, [&] { return pending.done(); }, kTimeout));
    VS_CHECK(static_cast<bool>(pending.ec));

    boost::system::error_code ec;
    port.close(ec);
    VS_CHECK(!ec);

    vs_test::tcp_echo_server replacement(io);
    replacement.start();
    port.open(
        "127.0.0.1:" + std::to_string(replacement.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK_EQ(asio::write(port, asio::buffer("LIVE", 4)), 4u);
    read_op live = read_once(io, port);
    VS_CHECK(!live.ec);
    VS_CHECK_EQ(live.text(), std::string("LIVE"));
}

// ---------------------------------------------------------------------------
// UDP bridge
// ---------------------------------------------------------------------------

VS_TEST_CASE(modes_udp_bidirectional_and_recovery)
{
    asio::io_context io;
    vs_test::udp_echo_server server(io);
    server.start();

    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "udp://127.0.0.1:" + std::to_string(server.port()));
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));
    VS_REQUIRE(port.is_open());

    VS_CHECK_EQ(asio::write(port, asio::buffer("PING", 4)), 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "PING"; }, kTimeout));

    read_op echo = read_once(io, port);
    VS_CHECK(!echo.ec);
    VS_CHECK_EQ(echo.text(), std::string("PING"));

    server.send("PUSH");
    read_op pushed = read_once(io, port);
    VS_CHECK(!pushed.ec);
    VS_CHECK_EQ(pushed.text(), std::string("PUSH"));

    bool wrote = false;
    port.async_write_some(asio::buffer("ASYN", 4),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(!error);
            wrote = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return wrote; }, kTimeout));
    VS_CHECK(vs_test::pump_until(
        io,
        [&] { return server.received().find("ASYN") != std::string::npos; },
        kTimeout));
    read_op async_echo = read_once(io, port);
    VS_CHECK(!async_echo.ec);
    VS_CHECK_EQ(async_echo.text(), std::string("ASYN"));

    // timeout and recovery
    port.set_option(vs::read_timeout(std::chrono::milliseconds(30)));
    read_op timed_out = read_once(io, port);
    VS_CHECK(timed_out.ec == vs::error::timeout);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));
    server.send("RECOVERED");
    read_op recovered = read_once(io, port);
    VS_CHECK(!recovered.ec);
    VS_CHECK_EQ(recovered.text(), std::string("RECOVERED"));

    // close/reopen recovery
    boost::system::error_code ec;
    port.close(ec);
    VS_CHECK(!ec);
    port.open(
        "127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK_EQ(asio::write(port, asio::buffer("AGAIN", 5)), 5u);
    read_op again = read_once(io, port);
    VS_CHECK(!again.ec);
    VS_CHECK_EQ(again.text(), std::string("AGAIN"));
}

// ---------------------------------------------------------------------------
// RFC 2217
// ---------------------------------------------------------------------------

VS_TEST_CASE(modes_rfc2217_bidirectional_and_fault_recovery)
{
    asio::io_context io;
    vs_test::rfc2217_test_server server(io);
    server.start();

    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "rfc2217://127.0.0.1:" + std::to_string(server.port()));
    VS_REQUIRE(port.is_open());
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));

    // The initial configuration must reach the access server.
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.commands().size() >= 6; }, kTimeout));

    // host -> device
    VS_CHECK_EQ(asio::write(port, asio::buffer("PING", 4)), 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "PING"; }, kTimeout));

    // device -> host
    server.send_data("PONG");
    read_op inbound = read_once(io, port);
    VS_CHECK(!inbound.ec);
    VS_CHECK_EQ(inbound.text(), std::string("PONG"));

    // host -> device, asynchronous
    bool wrote = false;
    port.async_write_some(asio::buffer("ASYN", 4),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(!error);
            wrote = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return wrote; }, kTimeout));
    VS_CHECK(vs_test::pump_until(
        io,
        [&] { return server.received().find("ASYN") != std::string::npos; },
        kTimeout));

    // A line-state notification becomes a line fault; data after it is still
    // delivered, which is the recovery path an application depends on.
    read_op pending;
    pending.start(port);
    vs_test::pump(io);
    server.send_line_state(static_cast<unsigned char>(
        vs::detail::telnet::line_state::parity));
    VS_CHECK(vs_test::pump_until(io, [&] { return pending.done(); }, kTimeout));
    VS_CHECK(pending.ec == vs::error::parity_error);

    server.send_data("AFTER-FAULT");
    read_op recovered = read_once(io, port);
    VS_CHECK(!recovered.ec);
    VS_CHECK_EQ(recovered.text(), std::string("AFTER-FAULT"));

    // Front-end timeout, then recovery.
    port.set_option(vs::read_timeout(std::chrono::milliseconds(40)));
    read_op timed_out = read_once(io, port);
    VS_CHECK(timed_out.ec == vs::error::timeout);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)));
    server.send_data("AFTER-TIMEOUT");
    read_op after_timeout = read_once(io, port);
    VS_CHECK(!after_timeout.ec);
    VS_CHECK_EQ(after_timeout.text(), std::string("AFTER-TIMEOUT"));

    // Peer loss, then a fresh connection to a replacement server.
    read_op before_close;
    before_close.start(port);
    vs_test::pump(io);
    server.close_peer();
    VS_CHECK(vs_test::pump_until(
        io, [&] { return before_close.done(); }, kTimeout));
    VS_CHECK(static_cast<bool>(before_close.ec));

    boost::system::error_code ec;
    port.close(ec);
    // The read loop already closed the socket when the peer vanished, so a
    // second close reports not_connected; either way the port is closed and
    // can be reopened.
    VS_CHECK(!ec || ec == vs::error::not_connected);
    ec.clear();

    vs_test::rfc2217_test_server replacement(io);
    replacement.start();
    port.open(
        "127.0.0.1:" + std::to_string(replacement.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return replacement.commands().size() >= 6; }, kTimeout));

    VS_CHECK_EQ(asio::write(port, asio::buffer("LIVE", 4)), 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return replacement.received() == "LIVE"; }, kTimeout));
    replacement.send_data("ONLINE");
    read_op online = read_once(io, port);
    VS_CHECK(!online.ec);
    VS_CHECK_EQ(online.text(), std::string("ONLINE"));
}
