//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Exercises the physical backend against a real serial device pair.
//
// The test is skipped when no pair is configured, so the standard suite still
// passes on machines without hardware.  CI configures one:
//
//   Linux/macOS:  socat -d -d pty,raw,echo=0 pty,raw,echo=0
//                 VIRTUAL_SERIAL_LOOPBACK_A=/dev/pts/N
//                 VIRTUAL_SERIAL_LOOPBACK_B=/dev/pts/M
//   Windows:      VIRTUAL_SERIAL_LOOPBACK_A=COM10
//                 VIRTUAL_SERIAL_LOOPBACK_B=COM11   (com0com or similar)
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

constexpr std::chrono::milliseconds kTimeout(3000);

struct loopback_pair
{
    const char* first = nullptr;
    const char* second = nullptr;

    explicit loopback_pair(bool enable)
    {
        const char* a = std::getenv("VIRTUAL_SERIAL_LOOPBACK_A");
        const char* b = std::getenv("VIRTUAL_SERIAL_LOOPBACK_B");
        if (enable && a != nullptr && b != nullptr && *a != '\0' && *b != '\0')
        {
            first = a;
            second = b;
        }
    }

    bool available() const noexcept
    {
        return first != nullptr;
    }
};

/// One asynchronous read with a stable buffer for the duration of the op.
struct pending_read
{
    char data[64] = {};
    boost::system::error_code ec;
    std::size_t transferred = 0;
    int completions = 0;

    template <class Port>
    void start(Port& port, std::size_t size = sizeof(pending_read::data))
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
};

void configure(vs::basic_serial_port<vs::physical_port>& port)
{
    boost::system::error_code ec;
    port.set_option(asio::serial_port_base::baud_rate(115200), ec);
    port.set_option(asio::serial_port_base::character_size(8), ec);
    port.set_option(asio::serial_port_base::parity(
        asio::serial_port_base::parity::none), ec);
    port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::one), ec);
    port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::none), ec);
    port.set_option(vs::read_timeout(kTimeout), ec);
}

bool wait_for(
    asio::io_context& io, const pending_read& read,
    std::chrono::milliseconds timeout = kTimeout)
{
    return vs_test::pump_until(io, [&read] { return read.done(); }, timeout);
}

} // namespace

VS_TEST_CASE(physical_loopback_is_available)
{
    const loopback_pair pair(true);
    if (!pair.available())
    {
        // CI sets VIRTUAL_SERIAL_REQUIRE_LOOPBACK so that a silently skipped
        // hardware path cannot masquerade as a passing run.
        const char* required = std::getenv("VIRTUAL_SERIAL_REQUIRE_LOOPBACK");
        if (required != nullptr && *required != '\0'
            && std::string(required) != "0")
        {
            VS_FAIL(
                "VIRTUAL_SERIAL_LOOPBACK_A/B are required by "
                "VIRTUAL_SERIAL_REQUIRE_LOOPBACK");
            return;
        }
        std::printf(
            "    skipped: set VIRTUAL_SERIAL_LOOPBACK_A/B to a serial pair "
            "(socat pty on POSIX, com0com on Windows)\n");
        return;
    }
    std::printf("    using loopback %s <-> %s\n", pair.first, pair.second);
}

VS_TEST_CASE(physical_loopback_bidirectional_sync_and_async)
{
    const loopback_pair pair(true);
    if (!pair.available())
    {
        std::printf("    skipped: no serial loopback configured\n");
        return;
    }

    asio::io_context io;
    vs::basic_serial_port<vs::physical_port> first(io.get_executor());
    vs::basic_serial_port<vs::physical_port> second(io.get_executor());

    boost::system::error_code ec;
    first.open(pair.first, ec);
    VS_REQUIRE(!ec);
    second.open(pair.second, ec);
    VS_REQUIRE(!ec);

    VS_CHECK(first.is_open());
    VS_CHECK(second.is_open());
    VS_CHECK(first.backend() == vs::backend_kind::physical);
    VS_CHECK(first.native_handle() != 0);
    VS_CHECK_EQ(first.native_backend()->options().baud_rate, 9600u);

    configure(first);
    configure(second);
    VS_CHECK_EQ(first.config().baud_rate, 115200u);

    // Direction 1: synchronous write on the first end, asynchronous read on
    // the second.
    VS_CHECK_EQ(asio::write(first, asio::buffer("PING", 4)), 4u);
    pending_read inbound;
    inbound.start(second);
    VS_REQUIRE(wait_for(io, inbound));
    VS_CHECK(!inbound.ec);
    VS_CHECK_EQ(inbound.text(), std::string("PING"));

    // Direction 2: synchronous write on the second end, asynchronous read on
    // the first.
    VS_CHECK_EQ(asio::write(second, asio::buffer("PONG", 4)), 4u);
    pending_read reverse;
    reverse.start(first);
    VS_REQUIRE(wait_for(io, reverse));
    VS_CHECK(!reverse.ec);
    VS_CHECK_EQ(reverse.text(), std::string("PONG"));

    // Asynchronous write in both directions.
    bool wrote = false;
    first.async_write_some(asio::buffer("ASYNC", 5),
        [&wrote](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            VS_CHECK_EQ(count, 5u);
            wrote = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return wrote; }, kTimeout));

    pending_read async_inbound;
    async_inbound.start(second);
    VS_REQUIRE(wait_for(io, async_inbound));
    VS_CHECK_EQ(async_inbound.text(), std::string("ASYNC"));

    first.close(ec);
    VS_CHECK(!ec);
    second.close(ec);
    VS_CHECK(!ec);
}

VS_TEST_CASE(physical_loopback_timeout_and_recovery)
{
    const loopback_pair pair(true);
    if (!pair.available())
    {
        std::printf("    skipped: no serial loopback configured\n");
        return;
    }

    asio::io_context io;
    vs::basic_serial_port<vs::physical_port> reader(io.get_executor());
    vs::basic_serial_port<vs::physical_port> writer(io.get_executor());

    boost::system::error_code ec;
    reader.open(pair.first, ec);
    VS_REQUIRE(!ec);
    writer.open(pair.second, ec);
    VS_REQUIRE(!ec);

    reader.set_option(vs::read_timeout(std::chrono::milliseconds(50)), ec);
    VS_CHECK(!ec);

    // Nothing is in flight: the front-end timeout must fire.
    pending_read timed_out;
    timed_out.start(reader);
    VS_REQUIRE(vs_test::pump_until(
        io, [&] { return timed_out.done(); }, std::chrono::milliseconds(3000)));
    VS_CHECK(timed_out.ec == vs::error::timeout);

    // Recovery: after the timeout the same port must still receive data.
    reader.set_option(vs::read_timeout(kTimeout), ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(asio::write(writer, asio::buffer("AFTER", 5)), 5u);

    pending_read recovered;
    recovered.start(reader);
    VS_REQUIRE(wait_for(io, recovered));
    VS_CHECK(!recovered.ec);
    VS_CHECK_EQ(recovered.text(), std::string("AFTER"));

    reader.close(ec);
    writer.close(ec);
}

VS_TEST_CASE(physical_loopback_cancel_close_and_reopen)
{
    const loopback_pair pair(true);
    if (!pair.available())
    {
        std::printf("    skipped: no serial loopback configured\n");
        return;
    }

    asio::io_context io;
    vs::basic_serial_port<vs::physical_port> reader(io.get_executor());
    vs::basic_serial_port<vs::physical_port> writer(io.get_executor());

    boost::system::error_code ec;
    reader.open(pair.first, ec);
    VS_REQUIRE(!ec);
    writer.open(pair.second, ec);
    VS_REQUIRE(!ec);
    reader.set_option(vs::read_timeout(std::chrono::milliseconds(0)), ec);

    // A pending read that is cancelled completes with operation_aborted and
    // leaves the port usable.
    pending_read cancelled;
    cancelled.start(reader);
    vs_test::pump(io);
    reader.cancel(ec);
    VS_CHECK(!ec);
    VS_REQUIRE(vs_test::pump_until(io, [&] { return cancelled.done(); }, kTimeout));
    VS_CHECK(cancelled.ec == asio::error::operation_aborted);

    VS_CHECK_EQ(asio::write(writer, asio::buffer("NEXT", 4)), 4u);
    pending_read after_cancel;
    after_cancel.start(reader);
    VS_REQUIRE(wait_for(io, after_cancel));
    VS_CHECK_EQ(after_cancel.text(), std::string("NEXT"));

    // Closing the port aborts a pending read, fails new operations, and a
    // reopen restores the data path.
    pending_read before_close;
    before_close.start(reader);
    vs_test::pump(io);
    reader.close(ec);
    VS_CHECK(!ec);
    VS_REQUIRE(vs_test::pump_until(io, [&] { return before_close.done(); }, kTimeout));
    VS_CHECK(before_close.ec == asio::error::operation_aborted);

    char buffer[8] = {};
    ec.clear();
    VS_CHECK_EQ(reader.read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    reader.open(pair.first, ec);
    VS_REQUIRE(!ec);
    VS_CHECK_EQ(asio::write(writer, asio::buffer("AGAIN", 5)), 5u);
    pending_read reopened;
    reopened.start(reader);
    VS_REQUIRE(wait_for(io, reopened));
    VS_CHECK(!reopened.ec);
    VS_CHECK_EQ(reopened.text(), std::string("AGAIN"));

    reader.close(ec);
    writer.close(ec);
}

VS_TEST_CASE(physical_loopback_option_errors)
{
    const loopback_pair pair(true);
    if (!pair.available())
    {
        std::printf("    skipped: no serial loopback configured\n");
        return;
    }

    asio::io_context io;
    vs::basic_serial_port<vs::physical_port> port(io.get_executor());

    // Options set before open are remembered and applied on open.
    boost::system::error_code ec;
    port.set_option(asio::serial_port_base::baud_rate(57600), ec);
    VS_CHECK(!ec);

    port.open(pair.first, ec);
    VS_REQUIRE(!ec);
    VS_CHECK_EQ(port.native_backend()->options().baud_rate, 57600u);
    asio::serial_port_base::baud_rate baud;
    port.get_option(baud);
    VS_CHECK_EQ(baud.value(), 57600u);
    port.close(ec);
    VS_CHECK(!ec);

    // Opening the same port twice is rejected.
    port.open(pair.first, ec);
    VS_REQUIRE(!ec);
    port.open(pair.first, ec);
    VS_CHECK(ec == vs::error::already_open);
    port.close(ec);
}
