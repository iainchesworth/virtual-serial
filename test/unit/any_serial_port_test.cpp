//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <chrono>
#include <memory>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

struct port_fixture
{
    asio::io_context io;
    vs::any_serial_port port;
    vs::mock_port* mock = nullptr;

    port_fixture()
        : port(vs::make_serial_port(io.get_executor(), "mock://unit"))
    {
        mock = vs::backend_cast<vs::mock_port>(port);
        VS_REQUIRE(mock != nullptr);
    }

    port_fixture(const port_fixture&) = delete;
    port_fixture& operator=(const port_fixture&) = delete;
};

} // namespace

VS_TEST_CASE(any_port_factory_selects_mock)
{
    asio::io_context io;
    vs::any_serial_port port =
        vs::make_serial_port(io.get_executor(), "mock://unit-test");

    VS_CHECK(port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::mock);
    VS_CHECK_EQ(port.config().target, std::string("unit-test"));
    VS_CHECK(port.native_backend() != nullptr);

    vs::mock_port* mock = vs::backend_cast<vs::mock_port>(port);
    VS_REQUIRE(mock != nullptr);
    VS_CHECK(mock->is_open());

    // A different backend type does not match.
    VS_CHECK(vs::backend_cast<vs::physical_port>(port) == nullptr);
}

VS_TEST_CASE(any_port_factory_rejects_bad_uri)
{
    asio::io_context io;
    boost::system::error_code ec;
    vs::any_serial_port port =
        vs::make_serial_port(io.get_executor(), "tcp://", ec);

    VS_CHECK(ec == vs::error::bad_target);
    VS_CHECK(!port.is_open());
    VS_CHECK(port.native_backend() == nullptr);

    // Operations on a backend-less port fail cleanly instead of crashing.
    char one = 0;
    ec.clear();
    VS_CHECK_EQ(port.read_some(asio::buffer(&one, 1), ec), 0u);
    VS_CHECK(ec == vs::error::not_connected);

    bool async_done = false;
    port.async_read_some(asio::buffer(&one, 1),
        [&](boost::system::error_code error, std::size_t) {
            ec = error;
            async_done = true;
        });
    vs_test::pump(io);
    VS_CHECK(async_done);
    VS_CHECK(ec == vs::error::not_connected);
}

VS_TEST_CASE(any_port_factory_physical_failure_is_reported)
{
    asio::io_context io;
    boost::system::error_code ec;
    vs::any_serial_port port = vs::make_serial_port(
        io.get_executor(), "serial://VIRTUAL_SERIAL_NO_SUCH_PORT", ec);

    VS_CHECK(static_cast<bool>(ec));
    VS_CHECK(!port.is_open());
    VS_CHECK(port.backend() == vs::backend_kind::physical);
}

VS_TEST_CASE(any_port_option_forwarding)
{
    port_fixture f;

    f.port.set_option(asio::serial_port_base::baud_rate(115200));
    f.port.set_option(asio::serial_port_base::character_size(7));
    f.port.set_option(asio::serial_port_base::parity(
        asio::serial_port_base::parity::even));
    f.port.set_option(asio::serial_port_base::stop_bits(
        asio::serial_port_base::stop_bits::two));
    f.port.set_option(asio::serial_port_base::flow_control(
        asio::serial_port_base::flow_control::hardware));

    const vs::serial_config config = f.port.config();
    VS_CHECK_EQ(config.baud_rate, 115200u);
    VS_CHECK_EQ(config.data_bits, 7u);
    VS_CHECK(config.parity == vs::parity_mode::even);
    VS_CHECK(config.stop_bits == vs::stop_bits_mode::two);
    VS_CHECK(config.flow_control == vs::flow_control_mode::hardware);

    // The backend saw the same values.
    VS_CHECK_EQ(f.mock->options().baud_rate, 115200u);

    asio::serial_port_base::baud_rate baud;
    f.port.get_option(baud);
    VS_CHECK_EQ(baud.value(), 115200u);

    asio::serial_port_base::stop_bits stop;
    f.port.get_option(stop);
    VS_CHECK(stop.value() == asio::serial_port_base::stop_bits::two);

    asio::serial_port_base::flow_control flow;
    f.port.get_option(flow);
    VS_CHECK(flow.value() == asio::serial_port_base::flow_control::hardware);
}

VS_TEST_CASE(any_port_rejects_unknown_option)
{
    port_fixture f;

    struct vendor_option
    {
        int value;
    };

    boost::system::error_code ec;
    f.port.set_option(vendor_option{42}, ec);
    VS_CHECK(ec == vs::error::unsupported_option);

    vendor_option read_back{0};
    ec.clear();
    f.port.get_option(read_back, ec);
    VS_CHECK(ec == vs::error::unsupported_option);
}

VS_TEST_CASE(any_port_mark_parity_cannot_be_read_as_ascii_option)
{
    asio::io_context io;
    vs::serial_config config;
    config.backend = vs::backend_kind::mock;
    config.parity = vs::parity_mode::mark;
    const std::shared_ptr<vs::mock_port> mock =
        std::make_shared<vs::mock_port>(io.get_executor(), config);
    vs::any_serial_port port(io.get_executor(), mock);

    asio::serial_port_base::parity parity;
    boost::system::error_code ec;
    port.get_option(parity, ec);
    VS_CHECK(ec == vs::error::unsupported_option);
}

VS_TEST_CASE(any_port_timeout_options_round_trip)
{
    port_fixture f;
    f.port.set_option(vs::read_timeout(std::chrono::milliseconds(250)));
    f.port.set_option(vs::write_timeout(std::chrono::milliseconds(500)));

    VS_CHECK_EQ(f.port.config().read_timeout.count(), 250);
    VS_CHECK_EQ(f.port.config().write_timeout.count(), 500);

    vs::read_timeout read(std::chrono::milliseconds(0));
    f.port.get_option(read);
    VS_CHECK_EQ(read.value().count(), 250);

    vs::write_timeout write(std::chrono::milliseconds(0));
    f.port.get_option(write);
    VS_CHECK_EQ(write.value().count(), 500);
}

VS_TEST_CASE(any_port_read_timeout_is_enforced)
{
    port_fixture f;
    f.port.set_option(vs::read_timeout(std::chrono::milliseconds(10)));

    boost::system::error_code result;
    bool done = false;
    char buffer[8] = {};
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t) {
            result = ec;
            done = true;
        });

    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(result == vs::error::timeout);
    VS_CHECK_EQ(f.mock->pending_reads(), 0u);
}

VS_TEST_CASE(any_port_read_timeout_does_not_fire_when_data_arrives)
{
    port_fixture f;
    f.port.set_option(vs::read_timeout(std::chrono::milliseconds(200)));

    char buffer[8] = {};
    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;
    f.port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code ec, std::size_t count) {
            result = ec;
            transferred = count;
            done = true;
        });

    f.mock->inject_read("ok");
    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 2u);
}

VS_TEST_CASE(any_port_write_timeout_is_enforced)
{
    port_fixture f;
    f.mock->set_write_latency(std::chrono::milliseconds(100));
    f.port.set_option(vs::write_timeout(std::chrono::milliseconds(10)));

    boost::system::error_code result;
    bool done = false;
    f.port.async_write_some(asio::buffer("abc", 3),
        [&](boost::system::error_code ec, std::size_t) {
            result = ec;
            done = true;
        });

    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(result == vs::error::timeout);
}

VS_TEST_CASE(any_port_composed_read_and_write)
{
    port_fixture f;

    const std::size_t written =
        asio::write(f.port, asio::buffer("AT\r\n", 4));
    VS_CHECK_EQ(written, 4u);
    VS_CHECK_EQ(f.mock->written(), std::string("AT\r\n"));

    f.mock->inject_read("RESPONSE");
    char buffer[8] = {};
    const std::size_t read = asio::read(f.port, asio::buffer(buffer));
    VS_CHECK_EQ(read, 8u);
    VS_CHECK_EQ(std::string(buffer, read), std::string("RESPONSE"));
}

VS_TEST_CASE(any_port_async_read_until)
{
    port_fixture f;
    f.mock->inject_read("line1\r\nline2\r\n");

    asio::streambuf buffer;
    bool done = false;
    boost::system::error_code result;
    std::size_t transferred = 0;
    asio::async_read_until(f.port, buffer, "\r\n",
        [&](boost::system::error_code ec, std::size_t count) {
            result = ec;
            transferred = count;
            done = true;
        });

    VS_CHECK(vs_test::pump_until(f.io, [&] { return done; }));
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 7u);

    std::string line;
    std::istream stream(&buffer);
    std::getline(stream, line);
    if (!line.empty() && line[line.size() - 1] == '\r')
        line.erase(line.size() - 1);
    VS_CHECK_EQ(line, std::string("line1"));
}

VS_TEST_CASE(any_port_strand_bound_handler_stays_on_strand)
{
    port_fixture f;
    asio::strand<asio::io_context::executor_type> strand =
        asio::make_strand(f.io);

    f.mock->inject_read("x");
    bool done = false;
    bool on_strand = false;
    char buffer[4] = {};
    f.port.async_read_some(asio::buffer(buffer),
        asio::bind_executor(strand,
            [&](boost::system::error_code ec, std::size_t) {
                VS_CHECK(!ec);
                on_strand = strand.running_in_this_thread();
                done = true;
            }));

    vs_test::pump(f.io);
    VS_CHECK(done);
    VS_CHECK(on_strand);
}

VS_TEST_CASE(any_port_use_future)
{
    port_fixture f;
    f.mock->inject_read("future");

    char buffer[8] = {};
    std::future<std::size_t> future =
        f.port.async_read_some(asio::buffer(buffer), asio::use_future);

    vs_test::pump_until(f.io, [&] {
        return future.wait_for(std::chrono::milliseconds(0))
            == std::future_status::ready;
    });

    VS_REQUIRE(
        future.wait_for(std::chrono::milliseconds(0))
        == std::future_status::ready);
    VS_CHECK_EQ(future.get(), 6u);
    VS_CHECK_EQ(std::string(buffer, 6), std::string("future"));
}

VS_TEST_CASE(any_port_move_semantics)
{
    port_fixture f;
    f.mock->inject_read("moved");

    vs::any_serial_port moved(std::move(f.port));
    VS_CHECK(moved.is_open());
    VS_CHECK(f.port.native_backend() == nullptr);

    char buffer[8] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(moved.read_some(asio::buffer(buffer), ec), 5u);
    VS_CHECK_EQ(std::string(buffer, 5), std::string("moved"));
}

VS_TEST_CASE(any_port_closed_operations)
{
    asio::io_context io;
    vs::any_serial_port port(io.get_executor());
    VS_CHECK(!port.is_open());
    VS_CHECK_EQ(port.native_handle(), 0);

    boost::system::error_code ec;
    port.open("x", ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    port.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);

    VS_CHECK_THROW(port.open("x"), boost::system::system_error);
}

VS_TEST_CASE(any_port_double_open)
{
    port_fixture f;
    boost::system::error_code ec;
    f.port.open("again", ec);
    VS_CHECK(ec == vs::error::already_open);
}

VS_TEST_CASE(any_port_direct_backend_adoption)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());

    vs::any_serial_port port(io.get_executor(), mock);
    VS_CHECK(!port.is_open());
    port.open("adopted");
    VS_CHECK(port.is_open());

    mock->inject_read("direct");
    char buffer[8] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(port.read_some(asio::buffer(buffer), ec), 6u);
    VS_CHECK_EQ(std::string(buffer, 6), std::string("direct"));
}

VS_TEST_CASE(any_port_async_write)
{
    port_fixture f;
    bool done = false;
    std::size_t transferred = 0;
    f.port.async_write_some(asio::buffer("hello", 5),
        [&](boost::system::error_code ec, std::size_t count) {
            VS_CHECK(!ec);
            transferred = count;
            done = true;
        });

    vs_test::pump(f.io);
    VS_CHECK(done);
    VS_CHECK_EQ(transferred, 5u);
    VS_CHECK_EQ(f.mock->written(), std::string("hello"));
}

VS_TEST_CASE(any_port_fault_propagates)
{
    port_fixture f;
    f.mock->inject_framing_error();

    char one = 0;
    boost::system::error_code ec;
    f.port.read_some(asio::buffer(&one, 1), ec);
    VS_CHECK(ec == vs::error::framing_error);
}

VS_TEST_CASE(any_port_native_backend_access)
{
    port_fixture f;
    VS_CHECK(f.port.native_backend().get() != nullptr);
    VS_CHECK(f.port.native().backend() == vs::backend_kind::mock);

    vs::mock_port* mock = vs::backend_cast<vs::mock_port>(f.port);
    VS_REQUIRE(mock != nullptr);
    VS_CHECK_EQ(mock->bytes_written(), 0u);
}
