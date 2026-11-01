//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/tcp_echo_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

VS_TEST_CASE(tcp_bridge_round_trip)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::tcp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open(
        "127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::tcp);
    VS_CHECK(port.native_handle() != 0);

    // Client to server.
    const std::size_t written = asio::write(port, asio::buffer("PING", 4));
    VS_CHECK_EQ(written, 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.bytes_received() >= 4; }));
    VS_CHECK_EQ(server.received(), std::string("PING"));

    // The echo comes back asynchronously.
    char buffer[16] = {};
    bool done = false;
    std::size_t transferred = 0;
    boost::system::error_code result;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });

    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK(!result);
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("PING"));

    // Server initiated data.
    server.send("PUSH");
    done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK(!result);
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("PUSH"));

    port.close(ec);
    VS_CHECK(!ec);
    VS_CHECK(!port.is_open());
}

VS_TEST_CASE(tcp_bridge_peer_close_surfaces_as_eof)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::tcp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(vs_test::pump_until(io, [&] { return server.connected(); }));

    char buffer[8] = {};
    bool done = false;
    boost::system::error_code result;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            result = error;
            done = true;
        });

    server.close_peer();
    VS_CHECK(vs_test::pump_until(
        io, [&] { return done; }, std::chrono::milliseconds(2000)));

    // A raw bridge forwards the transport error verbatim, so a peer that
    // goes away can surface as EOF, a reset or an abort depending on how
    // the platform tears the connection down.
    const bool peer_gone = result == asio::error::eof
        || result == asio::error::connection_reset
        || result == asio::error::connection_aborted
        || result == asio::error::broken_pipe
        || result == vs::error::not_connected;
    VS_CHECK(peer_gone);
}

VS_TEST_CASE(tcp_bridge_operations_on_closed_port)
{
    asio::io_context io;
    vs::tcp_bridge_port port(io.get_executor());

    boost::system::error_code ec;
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    char one = 0;
    VS_CHECK_EQ(port.read_some(asio::buffer(&one, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    VS_CHECK_EQ(port.write_some(asio::buffer(&one, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.open("not-a-host-port", ec);
    VS_CHECK(ec == vs::error::bad_target);
}

VS_TEST_CASE(tcp_bridge_through_any_serial_port)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    boost::system::error_code ec;
    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "tcp://127.0.0.1:" + std::to_string(server.port()),
        ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::tcp);

    const std::size_t written = asio::write(port, asio::buffer("AB", 2));
    VS_CHECK_EQ(written, 2u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.bytes_received() >= 2; }));

    char buffer[8] = {};
    bool done = false;
    std::size_t transferred = 0;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("AB"));
}
