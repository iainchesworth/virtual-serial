//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Option forwarding: every Asio serial option in both directions, the two
// library timeouts, the invalid-value paths and the front end's behaviour
// with a missing backend.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/serial_port_base.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

VS_TEST_CASE(option_matrix_all_serial_options)
{
    asio::io_context io;
    // Exercises the "construct the backend in place" constructor.
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());
    VS_CHECK(port.native_backend() != nullptr);

    boost::system::error_code ec;
    port.open("options", ec);
    VS_REQUIRE(!ec);

    port.set_option(asio::serial_port_base::baud_rate(115200), ec);
    VS_CHECK(!ec);
    port.set_option(asio::serial_port_base::character_size(7), ec);
    VS_CHECK(!ec);
    port.set_option(asio::serial_port_base::parity(
        asio::serial_port_base::parity::even), ec);
    VS_CHECK(!ec);
    port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::two), ec);
    VS_CHECK(!ec);
    port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::software), ec);
    VS_CHECK(!ec);

    asio::serial_port_base::baud_rate baud;
    port.get_option(baud, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(baud.value(), 115200u);

    asio::serial_port_base::character_size size;
    port.get_option(size, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(size.value(), 7u);

    asio::serial_port_base::parity parity;
    port.get_option(parity, ec);
    VS_CHECK(!ec);
    VS_CHECK(parity.value() == asio::serial_port_base::parity::even);

    asio::serial_port_base::stop_bits stop;
    port.get_option(stop, ec);
    VS_CHECK(!ec);
    VS_CHECK(stop.value() == asio::serial_port_base::stop_bits::two);

    asio::serial_port_base::flow_control flow;
    port.get_option(flow, ec);
    VS_CHECK(!ec);
    VS_CHECK(flow.value() == asio::serial_port_base::flow_control::software);

    // Odd, no parity and one-and-a-half stop bits round trip as well.
    port.set_option(asio::serial_port_base::parity(
        asio::serial_port_base::parity::odd), ec);
    port.get_option(parity, ec);
    VS_CHECK(parity.value() == asio::serial_port_base::parity::odd);

    port.set_option(asio::serial_port_base::parity(
        asio::serial_port_base::parity::none), ec);
    port.get_option(parity, ec);
    VS_CHECK(parity.value() == asio::serial_port_base::parity::none);

    port.set_option(asio::serial_port_base::character_size(5), ec);
    VS_CHECK(!ec);
    port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::onepointfive), ec);
    VS_CHECK(!ec);
    port.get_option(stop, ec);
    VS_CHECK(!ec);
    VS_CHECK(stop.value() == asio::serial_port_base::stop_bits::onepointfive);

    port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::none), ec);
    port.get_option(flow, ec);
    VS_CHECK(flow.value() == asio::serial_port_base::flow_control::none);

    // The throwing overloads behave the same on success.
    port.set_option(asio::serial_port_base::baud_rate(9600));
    port.get_option(baud);
    VS_CHECK_EQ(baud.value(), 9600u);

    VS_CHECK_EQ(port.config().data_bits, 5u);
    VS_CHECK_EQ(port.native().options().baud_rate, 9600u);
}

VS_TEST_CASE(option_matrix_timeouts_round_trip)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());

    port.set_option(vs::read_timeout(std::chrono::milliseconds(120)));
    port.set_option(vs::write_timeout(std::chrono::milliseconds(340)));

    vs::read_timeout read(std::chrono::milliseconds(0));
    port.get_option(read);
    VS_CHECK_EQ(read.value().count(), 120);

    vs::write_timeout write(std::chrono::milliseconds(0));
    port.get_option(write);
    VS_CHECK_EQ(write.value().count(), 340);

    VS_CHECK_EQ(port.config().read_timeout.count(), 120);
    VS_CHECK_EQ(port.config().write_timeout.count(), 340);
}

VS_TEST_CASE(option_matrix_invalid_values_are_rejected)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());
    boost::system::error_code ec;

    port.open("invalid", ec);
    VS_REQUIRE(!ec);

    ec.clear();
    port.set_option(asio::serial_port_base::baud_rate(0), ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::onepointfive), ec);
    VS_CHECK(ec == vs::error::invalid_option);

    VS_CHECK_THROW(
        port.set_option(asio::serial_port_base::baud_rate(0)),
        boost::system::system_error);

    // The invalid attempt left the previous settings untouched.
    VS_CHECK_EQ(port.config().baud_rate, 9600u);
}

VS_TEST_CASE(option_matrix_mark_and_space_parity_are_not_ascii_options)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());
    boost::system::error_code ec;
    port.open("parity", ec);
    VS_REQUIRE(!ec);

    vs::serial_config config = port.native_backend()->options();
    config.parity = vs::parity_mode::mark;
    port.native_backend()->apply_options(config, ec);
    VS_REQUIRE(!ec);

    asio::serial_port_base::parity parity;
    ec.clear();
    port.get_option(parity, ec);
    VS_CHECK(ec == vs::error::unsupported_option);

    config.parity = vs::parity_mode::space;
    port.native_backend()->apply_options(config, ec);
    VS_REQUIRE(!ec);
    ec.clear();
    port.get_option(parity, ec);
    VS_CHECK(ec == vs::error::unsupported_option);
}

VS_TEST_CASE(option_matrix_unrecognised_option_types)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor());
    boost::system::error_code ec;
    port.open("options", ec);
    VS_REQUIRE(!ec);

    struct vendor_option
    {
        int value;
    };

    ec.clear();
    port.set_option(vendor_option{7}, ec);
    VS_CHECK(ec == vs::error::unsupported_option);

    vendor_option read_back{0};
    ec.clear();
    port.get_option(read_back, ec);
    VS_CHECK(ec == vs::error::unsupported_option);

    VS_CHECK_THROW(port.set_option(vendor_option{1}), boost::system::system_error);
    VS_CHECK_THROW(port.get_option(read_back), boost::system::system_error);
}

VS_TEST_CASE(basic_port_without_backend_reports_not_connected)
{
    asio::io_context io;
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), nullptr);

    VS_CHECK(!port.is_open());
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK_EQ(port.native_backend(), nullptr);

    boost::system::error_code ec;
    port.open("nowhere", ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);

    char buffer[4] = {};
    ec.clear();
    VS_CHECK_EQ(port.read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    VS_CHECK_EQ(port.write_some(asio::buffer(buffer, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.set_option(asio::serial_port_base::baud_rate(9600), ec);
    VS_CHECK(ec == vs::error::not_connected);

    asio::serial_port_base::baud_rate baud;
    ec.clear();
    port.get_option(baud, ec);
    VS_CHECK(ec == vs::error::not_connected);

    bool read_done = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            read_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(read_done);

    bool write_done = false;
    port.async_write_some(asio::buffer(buffer, 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            write_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(write_done);
}

VS_TEST_CASE(basic_port_async_write_on_closed_backend)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("closed", ec);
    VS_REQUIRE(!ec);
    port.close(ec);
    VS_REQUIRE(!ec);

    bool done = false;
    port.async_write_some(asio::buffer("x", 1),
        [&](boost::system::error_code error, std::size_t) {
            VS_CHECK(error == vs::error::not_connected);
            done = true;
        });
    vs_test::pump(io);
    VS_CHECK(done);
}

VS_TEST_CASE(basic_port_timed_write_completes_and_cancels_timer)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("timed", ec);
    VS_REQUIRE(!ec);
    port.set_option(vs::write_timeout(std::chrono::milliseconds(1000)), ec);

    bool done = false;
    std::size_t transferred = 0;
    port.async_write_some(asio::buffer("abc", 3),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            transferred = count;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
    VS_CHECK_EQ(transferred, 3u);
    VS_CHECK_EQ(mock->written(), std::string("abc"));
}

VS_TEST_CASE(basic_port_timed_read_completes_and_cancels_timer)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("timed", ec);
    VS_REQUIRE(!ec);
    port.set_option(vs::read_timeout(std::chrono::milliseconds(1000)), ec);

    mock->inject_read("xy");
    bool done = false;
    char buffer[4] = {};
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            VS_CHECK_EQ(count, 2u);
            done = true;
        });
    VS_CHECK(vs_test::pump_until(io, [&] { return done; }));
}
