//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// The runtime "API switcher": every factory overload, every backend kind and
// the failure paths that must leave a usable, closed port.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/rfc2217_test_server.hpp"
#include "../mock/tcp_echo_server.hpp"
#include "../mock/udp_echo_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

VS_TEST_CASE(factory_make_mock_port_with_configuration)
{
    asio::io_context io;
    vs::serial_config config;
    config.baud_rate = 57600;
    config.parity = vs::parity_mode::even;
    config.target = "configured";

    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor(), config);

    VS_CHECK(mock->backend() == vs::backend_kind::mock);
    VS_CHECK_EQ(mock->options().baud_rate, 57600u);
    VS_CHECK(mock->options().parity == vs::parity_mode::even);
    VS_CHECK_EQ(mock->options().target, std::string("configured"));
}

VS_TEST_CASE(factory_uri_selects_each_backend)
{
    asio::io_context io;

    // mock
    {
        vs::any_serial_port port =
            vs::make_serial_port(io.get_executor(), "mock://factory");
        VS_CHECK(port.is_open());
        VS_CHECK(port.backend() == vs::backend_kind::mock);
        VS_CHECK(vs::backend_cast<vs::mock_port>(port) != nullptr);
    }

    // tcp
    {
        vs_test::tcp_echo_server server(io);
        server.start();
        boost::system::error_code ec;
        vs::any_serial_port port = vs::make_serial_port(
            io.get_executor(),
            "tcp://127.0.0.1:" + std::to_string(server.port()),
            ec);
        VS_CHECK(!ec);
        VS_CHECK(port.is_open());
        VS_CHECK(port.backend() == vs::backend_kind::tcp);
    }

    // udp
    {
        vs_test::udp_echo_server server(io);
        server.start();
        boost::system::error_code ec;
        vs::any_serial_port port = vs::make_serial_port(
            io.get_executor(),
            "udp://127.0.0.1:" + std::to_string(server.port()),
            ec);
        VS_CHECK(!ec);
        VS_CHECK(port.backend() == vs::backend_kind::udp);
    }

    // rfc2217
    {
        vs_test::rfc2217_test_server server(io);
        server.start();
        boost::system::error_code ec;
        vs::any_serial_port port = vs::make_serial_port(
            io.get_executor(),
            "rfc2217://127.0.0.1:" + std::to_string(server.port()),
            ec);
        VS_CHECK(!ec);
        VS_CHECK(port.backend() == vs::backend_kind::rfc2217);
    }

    // physical (no device: open fails, the port object still exists)
    {
        boost::system::error_code ec;
        vs::any_serial_port port = vs::make_serial_port(
            io.get_executor(), "serial://VIRTUAL_SERIAL_ABSENT", ec);
        VS_CHECK(static_cast<bool>(ec));
        VS_CHECK(!port.is_open());
        VS_CHECK(port.backend() == vs::backend_kind::physical);
        VS_CHECK(vs::backend_cast<vs::physical_port>(port) != nullptr);
    }
}

VS_TEST_CASE(factory_invalid_configuration)
{
    asio::io_context io;
    vs::serial_config invalid;
    invalid.data_bits = 9;

    boost::system::error_code ec;
    vs::any_serial_port port = vs::make_serial_port(io.get_executor(), invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);
    VS_CHECK(port.native_backend() == nullptr);

    VS_CHECK_THROW(
        vs::make_serial_port(io.get_executor(), invalid),
        boost::system::system_error);

    vs::serial_config unknown;
    unknown.backend = static_cast<vs::backend_kind>(99);
    ec.clear();
    vs::any_serial_port missing = vs::make_serial_port(io.get_executor(), unknown, ec);
    VS_CHECK(ec == vs::error::bad_target);
    VS_CHECK(missing.native_backend() == nullptr);
}

VS_TEST_CASE(factory_backend_kind_overload)
{
    asio::io_context io;
    vs::any_serial_port mock = vs::make_serial_port(
        io.get_executor(), vs::backend_kind::mock, "kind");
    VS_CHECK(mock.is_open());
    VS_CHECK(mock.backend() == vs::backend_kind::mock);

    vs_test::tcp_echo_server server(io);
    server.start();
    vs::any_serial_port tcp = vs::make_serial_port(
        io.get_executor(),
        vs::backend_kind::tcp,
        "127.0.0.1:" + std::to_string(server.port()));
    VS_CHECK(tcp.is_open());
    VS_CHECK(tcp.backend() == vs::backend_kind::tcp);

    VS_CHECK_THROW(
        vs::make_serial_port(
            io.get_executor(), vs::backend_kind::physical, "VIRTUAL_SERIAL_ABSENT"),
        boost::system::system_error);
}

VS_TEST_CASE(factory_uri_overloads)
{
    asio::io_context io;

    // Throwing overload, success.
    vs::any_serial_port port =
        vs::make_serial_port(io.get_executor(), "mock://throwing");
    VS_CHECK(port.is_open());

    // Throwing overload, failure.
    VS_CHECK_THROW(
        vs::make_serial_port(io.get_executor(), "bogus://thing"),
        boost::system::system_error);
    VS_CHECK_THROW(
        vs::make_serial_port(io.get_executor(), "tcp://"),
        boost::system::system_error);

    // Error-code overload, failure leaves a closed port.
    boost::system::error_code ec;
    vs::any_serial_port failed =
        vs::make_serial_port(io.get_executor(), "serial://", ec);
    VS_CHECK(ec == vs::error::bad_target);
    VS_CHECK(!failed.is_open());
    VS_CHECK(failed.native_backend() == nullptr);
}

VS_TEST_CASE(factory_tcp_bridge_through_uri_exchanges_data)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "tcp://127.0.0.1:" + std::to_string(server.port()));
    VS_CHECK_EQ(asio::write(port, asio::buffer("FACTORY", 7)), 7u);
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received() == "FACTORY"; }));

    char buffer[8] = {};
    bool done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            VS_CHECK_EQ(count, 7u);
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
}
