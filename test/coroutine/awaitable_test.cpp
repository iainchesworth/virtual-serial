//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// C++20 coroutine coverage: the awaitable helpers and direct use_awaitable
// tokens over the erased port, the typed port and a TCP bridge, including a
// partial delivery completed by a timer coroutine and a timeout.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/tcp_echo_server.hpp"

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/awaitable.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>

#include <chrono>
#include <cstddef>
#include <future>
#include <istream>
#include <memory>
#include <string>
#include <utility>

#if !BOOST_VIRTUAL_SERIAL_HAS_COROUTINES
# error "this test requires a compiler with C++20 coroutine support"
#endif

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

/// Runs one coroutine to completion on a private io_context.
///
/// The context is pumped until the future is ready rather than run to
/// exhaustion: a loopback test server keeps a read outstanding, so
/// io_context::run() would not return even after the coroutine finished.
///
/// The factory keeps the coroutine lambda alive for the whole run.  A
/// coroutine lambda stores its closure in the frame as `this`, so passing
/// `[...]{...}()` straight to co_spawn would leave that closure dangling once
/// the call expression ends; the named factory parameter outlives the run.
template <class T, class Factory>
T run(asio::io_context& io, Factory task)
{
    std::future<T> result =
        asio::co_spawn(io, task(), asio::use_future);
    const bool finished = vs_test::pump_until(io, [&] {
        return result.wait_for(std::chrono::milliseconds(0))
            == std::future_status::ready;
    }, std::chrono::milliseconds(5000));
    VS_REQUIRE(finished);
    return result.get();
}

} // namespace

VS_TEST_CASE(awaitable_read_some_on_erased_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);
    mock->inject_read("abc");

    char buffer[3] = {};
    const std::size_t count = run<std::size_t>(io, [&]() -> asio::awaitable<std::size_t> {
        co_return co_await vs::read_some(port, asio::buffer(buffer));
    });

    VS_CHECK_EQ(count, 3u);
    VS_CHECK_EQ(std::string(buffer, count), std::string("abc"));
}

VS_TEST_CASE(awaitable_write_all_on_erased_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);

    const std::size_t count = run<std::size_t>(io, [&]() -> asio::awaitable<std::size_t> {
        co_return co_await vs::write_all(port, asio::buffer("hello", 5));
    });

    VS_CHECK_EQ(count, 5u);
    VS_CHECK_EQ(mock->written(), std::string("hello"));
}

VS_TEST_CASE(awaitable_read_exactly_waits_for_partial_deliveries)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);

    mock->inject_read("ab");
    char buffer[4] = {};

    asio::steady_timer timer(io);
    timer.expires_after(std::chrono::milliseconds(20));
    auto injector = [&]() -> asio::awaitable<void> {
        co_await timer.async_wait(asio::use_awaitable);
        mock->inject_read("cd");
    };
    asio::co_spawn(io, injector(), asio::detached);

    auto reader = [&]() -> asio::awaitable<std::size_t> {
        co_return co_await vs::read_exactly(port, asio::buffer(buffer));
    };
    std::future<std::size_t> result =
        asio::co_spawn(io, reader(), asio::use_future);
    io.run();

    VS_CHECK_EQ(result.get(), 4u);
    VS_CHECK_EQ(std::string(buffer, 4), std::string("abcd"));
}

VS_TEST_CASE(awaitable_read_until_delimiter)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);
    mock->inject_read("hello\nrest");

    asio::streambuf buffer;
    const std::size_t count = run<std::size_t>(io, [&]() -> asio::awaitable<std::size_t> {
        co_return co_await vs::read_until(port, buffer, '\n');
    });

    VS_CHECK_EQ(count, 6u);
    std::istream stream(&buffer);
    std::string line;
    std::getline(stream, line);
    VS_CHECK_EQ(line, std::string("hello"));
}

VS_TEST_CASE(awaitable_timeout_surfaces_as_an_exception)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(20)));

    const bool timed_out = run<bool>(io, [&]() -> asio::awaitable<bool> {
        char value = 0;
        try
        {
            co_await port.async_read_some(
                asio::buffer(&value, 1), asio::use_awaitable);
            co_return false;
        }
        catch (const boost::system::system_error& failure)
        {
            co_return failure.code() == vs::error::timeout;
        }
    });

    VS_CHECK(timed_out);
}

VS_TEST_CASE(awaitable_round_trip_over_tcp_bridge)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::tcp_bridge_port client(io.get_executor());
    boost::system::error_code error;
    client.open("127.0.0.1:" + std::to_string(server.port()), error);
    VS_REQUIRE(!error);

    char reply[4] = {};
    const std::size_t count = run<std::size_t>(io, [&]() -> asio::awaitable<std::size_t> {
        co_await vs::write_all(client, asio::buffer("PING", 4));
        co_return co_await vs::read_exactly(client, asio::buffer(reply));
    });

    VS_CHECK_EQ(count, 4u);
    VS_CHECK_EQ(std::string(reply, 4), std::string("PING"));
}

VS_TEST_CASE(awaitable_read_some_on_typed_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("awaitable", error);
    VS_REQUIRE(!error);
    mock->inject_read("T");

    char value = 0;
    const std::size_t count = run<std::size_t>(io, [&]() -> asio::awaitable<std::size_t> {
        co_return co_await vs::read_some(port, asio::buffer(&value, 1));
    });

    VS_CHECK_EQ(count, 1u);
    VS_CHECK_EQ(value, 'T');
}
