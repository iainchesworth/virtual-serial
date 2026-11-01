//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
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

VS_TEST_CASE(udp_bridge_round_trip)
{
    asio::io_context io;
    vs_test::udp_echo_server server(io);
    server.start();

    vs::udp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::udp);

    const std::size_t written = asio::write(port, asio::buffer("HELLO", 5));
    VS_CHECK_EQ(written, 5u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received().size() >= 5; }));
    VS_CHECK_EQ(server.received(), std::string("HELLO"));

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
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("HELLO"));

    // Server initiated datagram.
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
}

VS_TEST_CASE(udp_bridge_datagram_boundaries)
{
    asio::io_context io;
    vs_test::udp_echo_server server(io);
    server.start();

    vs::udp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);

    asio::write(port, asio::buffer("ONE", 3));
    asio::write(port, asio::buffer("TWO", 3));
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "ONETWO"; }));

    // Each datagram is delivered as its own read, exactly as UDP does.
    char buffer[16] = {};
    std::size_t transferred = 0;
    bool done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t count) {
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("ONE"));

    done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t count) {
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("TWO"));
}

VS_TEST_CASE(udp_bridge_through_any_serial_port)
{
    asio::io_context io;
    vs_test::udp_echo_server server(io);
    server.start();

    boost::system::error_code ec;
    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "udp://127.0.0.1:" + std::to_string(server.port()),
        ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.backend() == vs::backend_kind::udp);

    asio::write(port, asio::buffer("X", 1));
    VS_CHECK(vs_test::pump_until(io, [&] { return server.received() == "X"; }));

    char buffer[8] = {};
    bool done = false;
    std::size_t transferred = 0;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t count) {
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("X"));
}
