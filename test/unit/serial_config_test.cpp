//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial/serial_config.hpp>

#include <boost/system/error_code.hpp>

#include <chrono>
#include <string>

namespace vs = boost::virtual_serial;

VS_TEST_CASE(config_defaults)
{
    const vs::serial_config config;
    VS_CHECK(config.backend == vs::backend_kind::physical);
    VS_CHECK(config.target.empty());
    VS_CHECK_EQ(config.baud_rate, 9600u);
    VS_CHECK_EQ(config.data_bits, 8u);
    VS_CHECK(config.parity == vs::parity_mode::none);
    VS_CHECK(config.stop_bits == vs::stop_bits_mode::one);
    VS_CHECK(config.flow_control == vs::flow_control_mode::none);
    VS_CHECK_EQ(config.read_timeout.count(), 0);
    VS_CHECK_EQ(config.write_timeout.count(), 0);
    VS_CHECK(!config.exclusive);
}

VS_TEST_CASE(parse_bare_device_name)
{
    const vs::serial_config config = vs::parse_uri("COM3");
    VS_CHECK(config.backend == vs::backend_kind::physical);
    VS_CHECK_EQ(config.target, std::string("COM3"));
}

VS_TEST_CASE(parse_physical_uris)
{
    const vs::serial_config windows = vs::parse_uri("serial://COM12");
    VS_CHECK(windows.backend == vs::backend_kind::physical);
    VS_CHECK_EQ(windows.target, std::string("COM12"));

    const vs::serial_config posix = vs::parse_uri("serial:///dev/ttyUSB0");
    VS_CHECK(posix.backend == vs::backend_kind::physical);
    VS_CHECK_EQ(posix.target, std::string("/dev/ttyUSB0"));
}

VS_TEST_CASE(parse_mock_uri)
{
    const vs::serial_config unlabelled = vs::parse_uri("mock://");
    VS_CHECK(unlabelled.backend == vs::backend_kind::mock);
    VS_CHECK(unlabelled.target.empty());

    const vs::serial_config labelled = vs::parse_uri("mock://fixture-1");
    VS_CHECK(labelled.backend == vs::backend_kind::mock);
    VS_CHECK_EQ(labelled.target, std::string("fixture-1"));
}

VS_TEST_CASE(parse_network_uris)
{
    const vs::serial_config rfc = vs::parse_uri("rfc2217://lab-host:7001");
    VS_CHECK(rfc.backend == vs::backend_kind::rfc2217);
    VS_CHECK_EQ(rfc.target, std::string("lab-host:7001"));

    const vs::serial_config tcp = vs::parse_uri("tcp://10.0.0.5:4001");
    VS_CHECK(tcp.backend == vs::backend_kind::tcp);
    VS_CHECK_EQ(tcp.target, std::string("10.0.0.5:4001"));

    const vs::serial_config udp = vs::parse_uri("udp://10.0.0.5:4002");
    VS_CHECK(udp.backend == vs::backend_kind::udp);
    VS_CHECK_EQ(udp.target, std::string("10.0.0.5:4002"));

    const vs::serial_config ipv6 = vs::parse_uri("tcp://[::1]:4001");
    VS_CHECK(ipv6.backend == vs::backend_kind::tcp);
    VS_CHECK_EQ(ipv6.target, std::string("[::1]:4001"));
}

VS_TEST_CASE(parse_query_options)
{
    const vs::serial_config config = vs::parse_uri(
        "rfc2217://lab-host:7001?baud=115200&data=7&parity=even"
        "&stop=2&flow=hardware&read_timeout=250ms&write_timeout=1s"
        "&exclusive=1");

    VS_CHECK(config.backend == vs::backend_kind::rfc2217);
    VS_CHECK_EQ(config.target, std::string("lab-host:7001"));
    VS_CHECK_EQ(config.baud_rate, 115200u);
    VS_CHECK_EQ(config.data_bits, 7u);
    VS_CHECK(config.parity == vs::parity_mode::even);
    VS_CHECK(config.stop_bits == vs::stop_bits_mode::two);
    VS_CHECK(config.flow_control == vs::flow_control_mode::hardware);
    VS_CHECK_EQ(config.read_timeout.count(), 250);
    VS_CHECK_EQ(config.write_timeout.count(), 1000);
    VS_CHECK(config.exclusive);
}

VS_TEST_CASE(parse_query_aliases)
{
    const vs::serial_config config = vs::parse_uri(
        "serial://COM4?baudrate=19200&datasize=5&parity=O&stopsize=1.5"
        "&control=xonxoff&timeout=50");

    VS_CHECK_EQ(config.baud_rate, 19200u);
    VS_CHECK_EQ(config.data_bits, 5u);
    VS_CHECK(config.parity == vs::parity_mode::odd);
    VS_CHECK(config.stop_bits == vs::stop_bits_mode::one_point_five);
    VS_CHECK(config.flow_control == vs::flow_control_mode::software);
    VS_CHECK_EQ(config.read_timeout.count(), 50);
}

VS_TEST_CASE(parse_rejects_bad_input)
{
    boost::system::error_code ec;
    vs::parse_uri("", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("serial://", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("rfc2217://host-without-service", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("bogus://thing", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("serial://COM3?unknown=1", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("serial://COM3?baud=fast", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://COM3?data=9", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://COM3?stop=1.5&data=8", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://COM3?parity=weird", ec);
    VS_CHECK(ec == vs::error::invalid_option);
}

VS_TEST_CASE(parse_throws_with_system_error)
{
    VS_CHECK_THROW(vs::parse_uri("serial://"), boost::system::system_error);

    try
    {
        vs::parse_uri("serial://COM3?baud=0");
        VS_FAIL("expected an exception");
    }
    catch (const boost::system::system_error& error)
    {
        VS_CHECK(error.code() == vs::error::invalid_option);
    }
}

VS_TEST_CASE(to_uri_round_trips)
{
    vs::serial_config config;
    config.backend = vs::backend_kind::rfc2217;
    config.target = "lab-host:7001";
    config.baud_rate = 115200;
    config.data_bits = 7;
    config.parity = vs::parity_mode::even;
    config.stop_bits = vs::stop_bits_mode::two;
    config.flow_control = vs::flow_control_mode::hardware;
    config.read_timeout = std::chrono::milliseconds(250);
    config.write_timeout = std::chrono::milliseconds(1500);
    config.exclusive = true;

    const std::string uri = vs::to_uri(config);
    VS_CHECK_EQ(uri.substr(0, 10), std::string("rfc2217://"));
    const vs::serial_config reparsed = vs::parse_uri(uri);
    VS_CHECK(reparsed == config);
}

VS_TEST_CASE(to_uri_omits_defaults)
{
    vs::serial_config config;
    config.backend = vs::backend_kind::mock;
    VS_CHECK_EQ(vs::to_uri(config), std::string("mock://"));

    config.backend = vs::backend_kind::physical;
    config.target = "COM3";
    VS_CHECK_EQ(vs::to_uri(config), std::string("serial://COM3"));

    config.baud_rate = 115200;
    VS_CHECK_EQ(vs::to_uri(config), std::string("serial://COM3?baud=115200"));
}

VS_TEST_CASE(config_equality)
{
    vs::serial_config a;
    a.backend = vs::backend_kind::tcp;
    a.target = "host:1";
    vs::serial_config b = a;
    VS_CHECK(a == b);
    b.target = "host:2";
    VS_CHECK(a != b);
    b = a;
    b.read_timeout = std::chrono::milliseconds(1);
    VS_CHECK(a != b);
}

VS_TEST_CASE(backend_names)
{
    VS_CHECK_EQ(std::string(vs::to_string(vs::backend_kind::physical)), std::string("physical"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::backend_kind::mock)), std::string("mock"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::backend_kind::rfc2217)), std::string("rfc2217"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::backend_kind::tcp)), std::string("tcp"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::backend_kind::udp)), std::string("udp"));
}
