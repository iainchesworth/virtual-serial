//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/rfc2217_test_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;
namespace telnet = boost::virtual_serial::detail::telnet;

namespace {

/// Connects a client port to a freshly started loopback server.
struct rfc_fixture
{
    asio::io_context io;
    vs_test::rfc2217_test_server server;
    vs::rfc2217_port port;

    rfc_fixture()
        : server(io)
        , port(io.get_executor())
    {
        server.start();
        boost::system::error_code ec;
        port.open("127.0.0.1:" + std::to_string(server.port()), ec);
        VS_REQUIRE(!ec);
    }

    bool wait_for_commands(std::size_t count)
    {
        return vs_test::pump_until(
            io, [&] { return server.commands().size() >= count; });
    }

    rfc_fixture(const rfc_fixture&) = delete;
    rfc_fixture& operator=(const rfc_fixture&) = delete;
};

} // namespace

VS_TEST_CASE(rfc2217_negotiation_and_initial_configuration)
{
    rfc_fixture f;

    VS_CHECK(f.wait_for_commands(6));
    VS_CHECK(f.server.negotiated());

    const std::vector<unsigned char>* baud = f.server.find_command(
        telnet::rfc2217::set_baud_rate);
    VS_REQUIRE(baud != nullptr);
    VS_CHECK_EQ(baud->size(), 5u);
    // 9600 == 0x00002580
    VS_CHECK_EQ(static_cast<int>((*baud)[1]), 0);
    VS_CHECK_EQ(static_cast<int>((*baud)[2]), 0);
    VS_CHECK_EQ(static_cast<int>((*baud)[3]), 0x25);
    VS_CHECK_EQ(static_cast<int>((*baud)[4]), 0x80);

    const std::vector<unsigned char>* size = f.server.find_command(
        telnet::rfc2217::set_data_size);
    VS_REQUIRE(size != nullptr);
    VS_CHECK_EQ(static_cast<int>((*size)[1]), 8);

    const std::vector<unsigned char>* parity = f.server.find_command(
        telnet::rfc2217::set_parity);
    VS_REQUIRE(parity != nullptr);
    VS_CHECK_EQ(static_cast<int>((*parity)[1]), 1); // none

    const std::vector<unsigned char>* stop = f.server.find_command(
        telnet::rfc2217::set_stop_size);
    VS_REQUIRE(stop != nullptr);
    VS_CHECK_EQ(static_cast<int>((*stop)[1]), 1);

    const std::vector<unsigned char>* control = f.server.find_command(
        telnet::rfc2217::set_control);
    VS_REQUIRE(control != nullptr);
    VS_CHECK_EQ(static_cast<int>((*control)[1]), 1); // no flow control

    const std::vector<unsigned char>* mask = f.server.find_command(
        telnet::rfc2217::set_line_state_mask);
    VS_REQUIRE(mask != nullptr);
    const int expected_mask =
        telnet::line_state::framing | telnet::line_state::parity
        | telnet::line_state::overrun | telnet::line_state::break_detect;
    VS_CHECK_EQ(static_cast<int>((*mask)[1]), expected_mask);
}

VS_TEST_CASE(rfc2217_data_both_directions)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    const std::size_t written = asio::write(f.port, asio::buffer("hello", 5));
    VS_CHECK_EQ(written, 5u);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.received().size() >= 5; }));
    VS_CHECK_EQ(f.server.received(), std::string("hello"));

    char buffer[16] = {};
    bool done = false;
    std::size_t transferred = 0;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t count) {
            VS_CHECK(!ec);
            transferred = count;
            done = true;
        });

    f.server.send_data("world");
    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK_EQ(std::string(buffer, transferred), std::string("world"));
}

VS_TEST_CASE(rfc2217_escapes_iac_in_both_directions)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    // Write 0xFF followed by 'X'; the client must send 0xFF 0xFF 0x58.
    const unsigned char payload[2] = {0xFF, 'X'};
    boost::system::error_code ec;
    VS_CHECK_EQ(
        f.port.write_some(asio::buffer(payload, 2), ec), 2u);
    VS_CHECK(!ec);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.received().size() >= 2; }));
    VS_REQUIRE(f.server.received().size() == 2);
    VS_CHECK_EQ(
        static_cast<int>(static_cast<unsigned char>(f.server.received()[0])),
        0xFF);
    VS_CHECK_EQ(f.server.received()[1], 'X');

    // The server sends 0xFF 'A' 'B'; the client must unescape it.
    char buffer[8] = {};
    bool done = false;
    std::size_t transferred = 0;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            VS_CHECK(!error);
            transferred = count;
            done = true;
        });

    const std::string escaped =
        std::string(1, static_cast<char>(0xFF)) + "AB";
    f.server.send_data(escaped);
    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_REQUIRE(transferred == 3);
    VS_CHECK_EQ(
        static_cast<int>(static_cast<unsigned char>(buffer[0])), 0xFF);
    VS_CHECK_EQ(buffer[1], 'A');
    VS_CHECK_EQ(buffer[2], 'B');
}

VS_TEST_CASE(rfc2217_line_state_becomes_line_faults)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    char buffer[4] = {};
    boost::system::error_code result;
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t) {
            result = ec;
            done = true;
        });

    f.server.send_line_state(static_cast<unsigned char>(
        telnet::line_state::framing | telnet::line_state::overrun));
    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(result == vs::error::framing_error);

    done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t) {
            result = ec;
            done = true;
        });
    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(result == vs::error::overrun);

    VS_CHECK_EQ(
        static_cast<int>(f.port.line_state()),
        static_cast<int>(telnet::line_state::framing | telnet::line_state::overrun));
}

VS_TEST_CASE(rfc2217_modem_state_notification)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    unsigned char observed = 0;
    bool called = false;
    f.port.set_modem_state_handler([&](unsigned char state) {
        observed = state;
        called = true;
    });

    char buffer[4] = {};
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { done = true; });

    f.server.send_modem_state(
        static_cast<unsigned char>(telnet::modem_state::carrier_detect));
    VS_CHECK(vs_test::pump_until(f.io, [&] { return called; }));
    VS_CHECK_EQ(static_cast<int>(observed),
                static_cast<int>(telnet::modem_state::carrier_detect));
    VS_CHECK_EQ(static_cast<int>(f.port.modem_state()),
                static_cast<int>(telnet::modem_state::carrier_detect));
}

VS_TEST_CASE(rfc2217_signature_exchange)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    char buffer[4] = {};
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { done = true; });

    f.server.send_signature_request();
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.signature_received(); }));
    VS_CHECK(f.server.client_signature().find("VirtualSerial")
             != std::string::npos);
}

VS_TEST_CASE(rfc2217_option_changes_send_only_the_difference)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));
    const std::size_t before = f.server.commands().size();

    vs::serial_config desired = f.port.options();
    desired.baud_rate = 19200;
    boost::system::error_code ec;
    f.port.apply_options(desired, ec);
    VS_CHECK(!ec);

    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.commands().size() > before; }));
    VS_CHECK_EQ(f.server.commands().size(), before + 1);

    const std::vector<unsigned char>& command = f.server.commands().back();
    VS_REQUIRE(command.size() == 5);
    VS_CHECK_EQ(static_cast<int>(command[0]), static_cast<int>(telnet::rfc2217::set_baud_rate));
    // 19200 == 0x00004B00
    VS_CHECK_EQ(static_cast<int>(command[3]), 0x4B);
    VS_CHECK_EQ(static_cast<int>(command[4]), 0x00);
    VS_CHECK_EQ(f.port.options().baud_rate, 19200u);
}

VS_TEST_CASE(rfc2217_break_and_purge)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));
    const std::size_t before = f.server.commands().size();

    boost::system::error_code ec;
    f.port.set_break(true, ec);
    VS_CHECK(!ec);
    f.port.purge(3, ec);
    VS_CHECK(!ec);

    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.commands().size() >= before + 2; }));

    const std::vector<unsigned char>& control = f.server.commands()[before];
    VS_CHECK_EQ(static_cast<int>(control[0]), static_cast<int>(telnet::rfc2217::set_control));
    VS_CHECK_EQ(static_cast<int>(control[1]), static_cast<int>(telnet::rfc2217::control_break_on));

    const std::vector<unsigned char>& purge = f.server.commands()[before + 1];
    VS_CHECK_EQ(static_cast<int>(purge[0]), static_cast<int>(telnet::rfc2217::purge_data));
    VS_CHECK_EQ(static_cast<int>(purge[1]), 3);
}

VS_TEST_CASE(rfc2217_flow_control_suspend_is_recorded)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));
    VS_CHECK(!f.port.flow_suspended());

    char buffer[4] = {};
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { done = true; });

    f.server.send_flow_control(true);
    VS_CHECK(vs_test::pump_until(f.io, [&] { return f.port.flow_suspended(); }));

    f.server.send_flow_control(false);
    VS_CHECK(vs_test::pump_until(f.io, [&] { return !f.port.flow_suspended(); }));
}

VS_TEST_CASE(rfc2217_peer_close_surfaces_as_eof)
{
    rfc_fixture f;
    VS_REQUIRE(f.wait_for_commands(6));

    char buffer[4] = {};
    boost::system::error_code result;
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t) {
            result = ec;
            done = true;
        });

    f.server.close_peer();
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return done; }, std::chrono::milliseconds(2000)));
    VS_CHECK(result == asio::error::eof || result == vs::error::not_connected);
}

VS_TEST_CASE(rfc2217_through_any_serial_port)
{
    asio::io_context io;
    vs_test::rfc2217_test_server server(io);
    server.start();

    boost::system::error_code ec;
    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(),
        "rfc2217://127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);
    VS_CHECK(port.backend() == vs::backend_kind::rfc2217);

    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.commands().size() >= 6; }));

    asio::write(port, asio::buffer("through", 7));
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.received().size() >= 7; }));
    VS_CHECK_EQ(server.received(), std::string("through"));
}
