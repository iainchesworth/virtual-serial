//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Direct backend API coverage: the operations a port normally performs, plus
// the lifecycle and option paths that only the front end usually reaches.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/tcp_echo_server.hpp"
#include "../mock/udp_echo_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <boost/asio/write.hpp>

#include <cstddef>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
// TCP bridge
// ---------------------------------------------------------------------------

VS_TEST_CASE(tcp_bridge_direct_api)
{
    asio::io_context io;
    vs::tcp_bridge_port port(io.get_executor());

    VS_CHECK(port.get_executor() == io.get_executor());
    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::tcp);
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK(&port.lowest_layer() == &port.native_socket());
    VS_CHECK(port.options().backend == vs::backend_kind::tcp);

    boost::system::error_code ec;
    VS_CHECK_EQ(port.read_some(vs::detail::mutable_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(port.write_some(vs::detail::const_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);

    // Opening with an unparseable or empty target fails cleanly.
    ec.clear();
    port.open("no-service", ec);
    VS_CHECK(ec == vs::error::bad_target);
    ec.clear();
    port.open("", ec);
    VS_CHECK(ec == vs::error::bad_target);

    // A refused connection is reported, not thrown.
    ec.clear();
    port.open("127.0.0.1:1", ec);
    VS_CHECK(static_cast<bool>(ec));
    VS_CHECK(!port.is_open());
}

VS_TEST_CASE(tcp_bridge_direct_open_and_exchange)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::tcp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(port.native_handle() != 0);
    VS_CHECK_EQ(
        port.options().target,
        std::string("127.0.0.1:" + std::to_string(server.port())));

    // Double open is rejected.
    ec.clear();
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_CHECK(ec == vs::error::already_open);

    // Options: invalid values are rejected, valid ones are stored.
    vs::serial_config invalid;
    invalid.data_bits = 9;
    ec.clear();
    port.apply_options(invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);

    vs::serial_config options;
    options.baud_rate = 57600;
    options.backend = vs::backend_kind::physical; // normalised to tcp
    ec.clear();
    port.apply_options(options, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(port.options().baud_rate, 57600u);
    VS_CHECK(port.options().backend == vs::backend_kind::tcp);

    // Synchronous traffic in both directions.
    VS_CHECK_EQ(asio::write(port, asio::buffer("SYNC", 4)), 4u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "SYNC"; }));

    char buffer[8] = {};
    VS_CHECK_EQ(port.read_some(asio::buffer(buffer), ec), 4u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(std::string(buffer, 4), std::string("SYNC"));

    ec.clear();
    port.cancel(ec);
    VS_CHECK(!ec);
    port.close(ec);
    VS_CHECK(!ec);
    VS_CHECK(!port.is_open());
    VS_CHECK_EQ(port.native_handle(), 0);
}

VS_TEST_CASE(tcp_bridge_async_operations_on_closed_port)
{
    asio::io_context io;
    vs::tcp_bridge_port port(io.get_executor());

    char buffer[4] = {};
    bool read_done = false;
    bool write_done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            read_done = true;
        });
    port.async_write_some(asio::buffer(buffer, 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            write_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(read_done);
    VS_CHECK(write_done);
}

// ---------------------------------------------------------------------------
// UDP bridge
// ---------------------------------------------------------------------------

VS_TEST_CASE(udp_bridge_direct_api)
{
    asio::io_context io;
    vs::udp_bridge_port port(io.get_executor());

    VS_CHECK(port.get_executor() == io.get_executor());
    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::udp);
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK(&port.lowest_layer() == &port.native_socket());
    VS_CHECK(port.options().backend == vs::backend_kind::udp);

    boost::system::error_code ec;
    VS_CHECK_EQ(port.read_some(vs::detail::mutable_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(port.write_some(vs::detail::const_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.open("", ec);
    VS_CHECK(ec == vs::error::bad_target);
}

VS_TEST_CASE(udp_bridge_direct_open_and_exchange)
{
    asio::io_context io;
    vs_test::udp_echo_server server(io);
    server.start();

    vs::udp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(port.native_handle() != 0);
    VS_CHECK_EQ(port.remote_endpoint().port(), server.port());

    ec.clear();
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_CHECK(ec == vs::error::already_open);

    vs::serial_config options;
    options.baud_rate = 38400;
    ec.clear();
    port.apply_options(options, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(port.options().baud_rate, 38400u);
    VS_CHECK(port.options().backend == vs::backend_kind::udp);

    VS_CHECK_EQ(asio::write(port, asio::buffer("DGRAM", 5)), 5u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "DGRAM"; }));

    char buffer[16] = {};
    VS_CHECK_EQ(port.read_some(asio::buffer(buffer), ec), 5u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(std::string(buffer, 5), std::string("DGRAM"));

    ec.clear();
    port.cancel(ec);
    VS_CHECK(!ec);
    port.close(ec);
    VS_CHECK(!ec);
}

VS_TEST_CASE(udp_bridge_async_operations_on_closed_port)
{
    asio::io_context io;
    vs::udp_bridge_port port(io.get_executor());

    char buffer[4] = {};
    bool read_done = false;
    bool write_done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            read_done = true;
        });
    port.async_write_some(asio::buffer(buffer, 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            write_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(read_done);
    VS_CHECK(write_done);
}

// ---------------------------------------------------------------------------
// Physical port (paths that do not need hardware)
// ---------------------------------------------------------------------------

VS_TEST_CASE(physical_port_direct_closed_paths)
{
    asio::io_context io;
    vs::physical_port port(io.get_executor());

    VS_CHECK(port.get_executor() == io.get_executor());
    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::physical);
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK(port.options().backend == vs::backend_kind::physical);
    VS_CHECK(&port.lowest_layer() == &port.native_port());

    boost::system::error_code ec;
    VS_CHECK_EQ(port.read_some(vs::detail::mutable_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(port.write_some(vs::detail::const_buffer_range(), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);

    vs::serial_config invalid;
    invalid.baud_rate = 0;
    ec.clear();
    port.apply_options(invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);

    // While closed, a valid configuration is remembered for the next open.
    vs::serial_config options;
    options.baud_rate = 57600;
    ec.clear();
    port.apply_options(options, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(port.options().baud_rate, 57600u);

    ec.clear();
    port.open("VIRTUAL_SERIAL_NO_SUCH_DEVICE", ec);
    VS_CHECK(static_cast<bool>(ec));
    VS_CHECK(!port.is_open());
}

VS_TEST_CASE(physical_port_async_operations_on_closed_port)
{
    asio::io_context io;
    vs::physical_port port(io.get_executor());

    char buffer[4] = {};
    bool read_done = false;
    bool write_done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            read_done = true;
        });
    port.async_write_some(asio::buffer(buffer, 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            write_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(read_done);
    VS_CHECK(write_done);
}
