//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// RFC 2217 protocol details: negotiation of unrelated options, acknowledgement
// decoding, incremental option diffs, synchronous access, break/purge and the
// signature exchange.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/rfc2217_test_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;
namespace telnet = boost::virtual_serial::detail::telnet;

namespace {

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

    bool ready()
    {
        return vs_test::pump_until(
            io, [&] { return server.commands().size() >= 6; });
    }

    /// Keeps the client's read loop alive so notifications are decoded.
    void start_background_read()
    {
        background.start(port);
    }

    struct pending
    {
        char data[64] = {};
        boost::system::error_code ec;
        std::size_t transferred = 0;
        int completions = 0;

        template <class Port>
        void start(Port& port)
        {
            Port* target = &port;
            target->async_read_some(
                asio::buffer(data, sizeof(data)),
                [this](boost::system::error_code error, std::size_t count) {
                    ++completions;
                    ec = error;
                    transferred = count;
                });
        }

        bool done() const
        {
            return completions != 0;
        }
    } background;

    rfc_fixture(const rfc_fixture&) = delete;
    rfc_fixture& operator=(const rfc_fixture&) = delete;
};

} // namespace

VS_TEST_CASE(rfc2217_direct_closed_paths)
{
    asio::io_context io;
    vs::rfc2217_port port(io.get_executor());

    VS_CHECK(port.get_executor() == io.get_executor());
    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::rfc2217);
    VS_CHECK_EQ(port.native_handle(), 0);
    VS_CHECK(&port.lowest_layer() == &port.native_socket());
    VS_CHECK(port.options().backend == vs::backend_kind::rfc2217);
    VS_CHECK(port.server_signature().empty());
    VS_CHECK(!port.flow_suspended());
    port.set_client_signature("direct");

    boost::system::error_code ec;
    char buffer[4] = {};
    VS_CHECK_EQ(port.read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    VS_CHECK_EQ(port.write_some(asio::buffer(buffer, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.set_break(true, ec);
    VS_CHECK(ec == vs::error::not_connected);
    ec.clear();
    port.purge(1, ec);
    VS_CHECK(ec == vs::error::not_connected);

    // Invalid options are rejected; valid ones are stored while closed.
    vs::serial_config invalid;
    invalid.data_bits = 9;
    ec.clear();
    port.apply_options(invalid, ec);
    VS_CHECK(ec == vs::error::invalid_option);

    vs::serial_config options;
    options.baud_rate = 57600;
    ec.clear();
    port.apply_options(options, ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(port.options().baud_rate, 57600u);
    VS_CHECK(port.options().backend == vs::backend_kind::rfc2217);

    // Unparseable, unresolvable and refused endpoints are reported.
    ec.clear();
    port.open("no-service", ec);
    VS_CHECK(ec == vs::error::bad_target);
    ec.clear();
    port.open("127.0.0.1:not-a-service", ec);
    VS_CHECK(static_cast<bool>(ec) || !port.is_open());
    ec.clear();
    port.open("127.0.0.1:1", ec);
    VS_CHECK(static_cast<bool>(ec));
    VS_CHECK(!port.is_open());
}

VS_TEST_CASE(rfc2217_async_operations_on_closed_port)
{
    asio::io_context io;
    vs::rfc2217_port port(io.get_executor());

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

VS_TEST_CASE(rfc2217_negotiation_refuses_unrelated_options)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    f.server.send_negotiation(telnet::command_do, 1); // server wants option 1
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.client_sent(telnet::command_wont, 1); }));

    f.server.send_negotiation(telnet::command_will, 3);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.client_sent(telnet::command_dont, 3); }));
}

VS_TEST_CASE(rfc2217_negotiation_dont_disables_will)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    f.server.send_negotiation(telnet::command_dont, telnet::option_com_port);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] {
            return f.server.client_sent(
                telnet::command_wont, telnet::option_com_port);
        }));

    // After WONT, a new DO must be answered with a fresh WILL.
    f.server.send_negotiation(telnet::command_do, telnet::option_com_port);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] {
            return f.server.client_sent(
                telnet::command_will, telnet::option_com_port);
        }));
}

VS_TEST_CASE(rfc2217_acknowledgements_update_options)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    const unsigned char baud[4] = {0, 0, 0x4B, 0x00}; // 19200
    f.server.send_server_ack(telnet::rfc2217::set_baud_rate,
        std::vector<unsigned char>(baud, baud + 4));
    f.server.send_server_ack(
        telnet::rfc2217::set_data_size, std::vector<unsigned char>(1, 7));
    f.server.send_server_ack(
        telnet::rfc2217::set_parity, std::vector<unsigned char>(1, 2)); // odd
    f.server.send_server_ack(
        telnet::rfc2217::set_stop_size, std::vector<unsigned char>(1, 2)); // two
    f.server.send_server_ack(
        telnet::rfc2217::set_control, std::vector<unsigned char>(1, 2)); // xon

    VS_CHECK(vs_test::pump_until(f.io, [&] {
        const vs::serial_config applied = f.port.options();
        return applied.baud_rate == 19200u && applied.data_bits == 7u
            && applied.parity == vs::parity_mode::odd
            && applied.stop_bits == vs::stop_bits_mode::two
            && applied.flow_control == vs::flow_control_mode::software;
    }));

    // Control acknowledgements map to each flow control mode.
    f.server.send_server_ack(
        telnet::rfc2217::set_control, std::vector<unsigned char>(1, 1));
    VS_CHECK(vs_test::pump_until(
        f.io,
        [&] { return f.port.options().flow_control == vs::flow_control_mode::none; }));
    f.server.send_server_ack(
        telnet::rfc2217::set_control, std::vector<unsigned char>(1, 3));
    VS_CHECK(vs_test::pump_until(
        f.io,
        [&] { return f.port.options().flow_control == vs::flow_control_mode::hardware; }));

    // Nonsense acknowledgements are ignored rather than corrupting the port.
    f.server.send_server_ack(telnet::rfc2217::set_baud_rate,
        std::vector<unsigned char>(4, 0));
    f.server.send_server_ack(
        telnet::rfc2217::set_data_size, std::vector<unsigned char>(1, 9));
    f.server.send_server_ack(
        telnet::rfc2217::set_parity, std::vector<unsigned char>(1, 99));
    f.server.send_server_ack(
        telnet::rfc2217::set_control, std::vector<unsigned char>(1, 99));
    f.server.send_server_ack(
        telnet::rfc2217::purge_data, std::vector<unsigned char>(1, 1));
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>(1, 77)); // unknown opcode

    vs_test::pump_until(f.io, [] { return false; }, std::chrono::milliseconds(50));
    VS_CHECK_EQ(f.port.options().baud_rate, 19200u);
    VS_CHECK_EQ(f.port.options().data_bits, 7u);
    VS_CHECK(f.port.options().parity == vs::parity_mode::odd);
}

VS_TEST_CASE(rfc2217_option_changes_are_sent_incrementally)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    const std::size_t before = f.server.commands().size();

    boost::system::error_code ec;
    vs::serial_config config = f.port.options();

    config.data_bits = 7;
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);
    config.parity = vs::parity_mode::even;
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);
    config.stop_bits = vs::stop_bits_mode::two;
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);
    config.flow_control = vs::flow_control_mode::hardware;
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);
    config.baud_rate = 19200;
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);

    // Re-applying the same configuration sends nothing.
    f.port.apply_options(config, ec);
    VS_CHECK(!ec);

    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.commands().size() >= before + 5; }));
    VS_CHECK_EQ(f.server.commands().size(), before + 5);

    const unsigned char expected[] = {
        telnet::rfc2217::set_data_size,
        telnet::rfc2217::set_parity,
        telnet::rfc2217::set_stop_size,
        telnet::rfc2217::set_control,
        telnet::rfc2217::set_baud_rate};
    for (std::size_t i = 0; i < 5; ++i)
    {
        const std::vector<unsigned char>& command = f.server.commands()[before + i];
        VS_REQUIRE(!command.empty());
        VS_CHECK_EQ(static_cast<int>(command[0]), static_cast<int>(expected[i]));
    }
}

VS_TEST_CASE(rfc2217_synchronous_read_and_write)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());

    // No asynchronous read has been issued, so the synchronous path drives
    // the socket itself.
    f.server.send_data("SYNC-DATA");
    vs_test::pump_until(f.io, [] { return false; }, std::chrono::milliseconds(50));

    char buffer[32] = {};
    boost::system::error_code ec;
    const std::size_t received = f.port.read_some(asio::buffer(buffer), ec);
    VS_CHECK(!ec);
    VS_REQUIRE(received == 9u);
    VS_CHECK_EQ(std::string(buffer, received), std::string("SYNC-DATA"));

    VS_CHECK_EQ(f.port.write_some(asio::buffer("REPLY", 5), ec), 5u);
    VS_CHECK(!ec);
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.received() == "REPLY"; }));
}

VS_TEST_CASE(rfc2217_break_and_purge_values)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    const std::size_t before = f.server.commands().size();

    boost::system::error_code ec;
    f.port.set_break(true, ec);
    VS_CHECK(!ec);
    f.port.set_break(false, ec);
    VS_CHECK(!ec);
    f.port.purge(1, ec);
    VS_CHECK(!ec);
    f.port.purge(2, ec);
    VS_CHECK(!ec);
    f.port.purge(3, ec);
    VS_CHECK(!ec);

    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.commands().size() >= before + 5; }));

    const std::vector<unsigned char>& break_on = f.server.commands()[before];
    VS_CHECK_EQ(static_cast<int>(break_on[0]), static_cast<int>(telnet::rfc2217::set_control));
    VS_CHECK_EQ(static_cast<int>(break_on[1]), static_cast<int>(telnet::rfc2217::control_break_on));

    const std::vector<unsigned char>& break_off = f.server.commands()[before + 1];
    VS_CHECK_EQ(static_cast<int>(break_off[1]), static_cast<int>(telnet::rfc2217::control_break_off));

    const std::vector<unsigned char>& purge_receive = f.server.commands()[before + 2];
    VS_CHECK_EQ(static_cast<int>(purge_receive[0]), static_cast<int>(telnet::rfc2217::purge_data));
    VS_CHECK_EQ(static_cast<int>(purge_receive[1]), 1);

    VS_CHECK_EQ(static_cast<int>(f.server.commands()[before + 3][1]), 2);
    VS_CHECK_EQ(static_cast<int>(f.server.commands()[before + 4][1]), 3);
}

VS_TEST_CASE(rfc2217_close_and_cancel)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());

    boost::system::error_code ec;
    f.port.cancel(ec);
    VS_CHECK(!ec);
    VS_CHECK(f.port.is_open());

    f.port.close(ec);
    VS_CHECK(!ec);
    VS_CHECK(!f.port.is_open());

    ec.clear();
    f.port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    f.port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);
}

VS_TEST_CASE(rfc2217_signature_exchange_details)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();
    f.port.set_client_signature("custom-client");

    f.server.send_signature_request();
    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.server.signature_received(); }));
    VS_CHECK_EQ(f.server.client_signature(), std::string("custom-client"));

    // The server's own signature is recorded on the client.
    std::vector<unsigned char> payload;
    payload.push_back(telnet::rfc2217::signature);
    const std::string text = "lab-server";
    payload.insert(payload.end(), text.begin(), text.end());
    f.server.send_subnegotiation(telnet::option_com_port, payload);

    VS_CHECK(vs_test::pump_until(
        f.io, [&] { return f.port.server_signature() == text; }));
}

VS_TEST_CASE(rfc2217_notify_line_state_all_bits)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());

    f.server.send_all_line_states();

    struct expectation
    {
        vs::error value;
    };
    const expectation expected[] = {
        {vs::error::framing_error},
        {vs::error::parity_error},
        {vs::error::overrun},
        {vs::error::break_detected}};

    for (const expectation& item : expected)
    {
        auto read = std::make_shared<rfc_fixture::pending>();
        read->start(f.port);
        VS_REQUIRE(vs_test::pump_until(f.io, [&] { return read->done(); }));
        VS_CHECK(read->ec == item.value);
    }

    VS_CHECK_EQ(
        static_cast<int>(f.port.line_state()), 0xFF);
}

VS_TEST_CASE(rfc2217_open_twice_is_rejected)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());

    boost::system::error_code ec;
    f.port.open("127.0.0.1:" + std::to_string(f.server.port()), ec);
    VS_CHECK(ec == vs::error::already_open);
}

// ---------------------------------------------------------------------------
// Robustness against non-conforming servers and degenerate commands
// ---------------------------------------------------------------------------

VS_TEST_CASE(rfc2217_accepts_client_opcoded_commands)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    // Some servers acknowledge with the client-side opcode instead of the
    // server-side one; both spellings must update the stored configuration.
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::set_data_size, 7});
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::set_parity, 3});
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::set_stop_size, 2});
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::set_control, 2});
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::set_baud_rate, 0, 0, 0x25, 0x80});

    VS_CHECK(vs_test::pump_until(f.io, [&] {
        const vs::serial_config applied = f.port.options();
        return applied.data_bits == 7u && applied.baud_rate == 9600u
            && applied.parity == vs::parity_mode::even
            && applied.stop_bits == vs::stop_bits_mode::two
            && applied.flow_control == vs::flow_control_mode::software;
    }));

    // A notification with the client opcode is understood as well.
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::notify_line_state,
                                   telnet::line_state::overrun});
    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>{telnet::rfc2217::notify_modem_state,
                                   telnet::modem_state::clear_to_send});

    VS_CHECK(vs_test::pump_until(f.io, [&] {
        return f.port.line_state() == telnet::line_state::overrun
            && f.port.modem_state() == telnet::modem_state::clear_to_send;
    }));
}

VS_TEST_CASE(rfc2217_ignores_degenerate_subnegotiations)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    f.server.send_subnegotiation(telnet::option_com_port,
        std::vector<unsigned char>());          // empty payload
    f.server.send_subnegotiation(1,
        std::vector<unsigned char>{1, 2});      // unrelated option
    f.server.send_subnegotiation(1,
        std::vector<unsigned char>{0});
    vs_test::pump_until(f.io, [] { return false; }, std::chrono::milliseconds(50));

    // The port is still healthy after the junk: the background read that
    // keeps the loop alive receives the next payload.
    f.server.send_data("ALIVE");
    VS_CHECK(vs_test::pump_until(f.io, [&] { return f.background.done(); }));
    VS_CHECK_EQ(
        std::string(f.background.data, f.background.transferred),
        std::string("ALIVE"));
}

VS_TEST_CASE(rfc2217_modem_state_without_handler)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    // No handler installed: the notification is only recorded.
    f.server.send_modem_state(telnet::modem_state::ring_indicator);
    VS_CHECK(vs_test::pump_until(
        f.io,
        [&] { return f.port.modem_state() == telnet::modem_state::ring_indicator; }));
}

VS_TEST_CASE(rfc2217_open_uses_configured_target)
{
    asio::io_context io;
    vs_test::rfc2217_test_server server(io);
    server.start();

    vs::serial_config config;
    config.backend = vs::backend_kind::rfc2217;
    config.target = "127.0.0.1:" + std::to_string(server.port());

    vs::rfc2217_port port(io.get_executor(), config);
    VS_CHECK_EQ(port.options().target, config.target);

    boost::system::error_code ec;
    port.open("", ec); // empty target falls back to the configured one
    VS_REQUIRE(!ec);
    VS_CHECK(port.is_open());
    VS_CHECK(vs_test::pump_until(
        io, [&] { return server.commands().size() >= 6; }));
    VS_CHECK(port.native_handle() != 0);
}

VS_TEST_CASE(rfc2217_sync_read_after_peer_loss)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());

    f.server.close_peer();
    vs_test::pump_until(f.io, [] { return false; }, std::chrono::milliseconds(50));

    char buffer[4] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.port.read_some(asio::buffer(buffer), ec), 0u);
    VS_CHECK(static_cast<bool>(ec));
}

VS_TEST_CASE(rfc2217_second_async_read_keeps_single_loop)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    rfc_fixture::pending second;
    second.start(f.port);

    // The oldest read is satisfied first; the newer one stays queued.
    f.server.send_data("PAIR");
    VS_CHECK(vs_test::pump_until(f.io, [&] { return f.background.done(); }));
    VS_CHECK_EQ(
        std::string(f.background.data, f.background.transferred),
        std::string("PAIR"));
    VS_CHECK(!second.done());

    f.server.send_data("NEXT");
    VS_CHECK(vs_test::pump_until(f.io, [&] { return second.done(); }));
    VS_CHECK_EQ(std::string(second.data, second.transferred), std::string("NEXT"));
}

VS_TEST_CASE(rfc2217_negotiation_after_dont_offers_again)
{
    rfc_fixture f;
    VS_REQUIRE(f.ready());
    f.start_background_read();

    f.server.send_negotiation(telnet::command_dont, telnet::option_com_port);
    VS_CHECK(vs_test::pump_until(f.io, [&] {
        return f.server.client_sent(telnet::command_wont, telnet::option_com_port);
    }));

    f.server.send_negotiation(telnet::command_will, telnet::option_com_port);
    VS_CHECK(vs_test::pump_until(f.io, [&] {
        return f.server.client_sent(telnet::command_do, telnet::option_com_port);
    }));
}
