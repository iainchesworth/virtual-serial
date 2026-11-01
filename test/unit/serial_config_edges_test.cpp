//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// URI grammar edge cases: every alias, every malformed form and the
// canonical serialiser for every backend.
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial/serial_config.hpp>

#include <boost/system/error_code.hpp>

#include <chrono>
#include <string>

namespace vs = boost::virtual_serial;

VS_TEST_CASE(config_to_string_covers_every_value_and_fallback)
{
    VS_CHECK_EQ(
        std::string(vs::to_string(vs::backend_kind::physical)),
        std::string("physical"));
    VS_CHECK_EQ(
        std::string(vs::to_string(static_cast<vs::backend_kind>(99))),
        std::string("unknown"));

    VS_CHECK_EQ(std::string(vs::to_string(vs::parity_mode::none)), std::string("none"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::parity_mode::odd)), std::string("odd"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::parity_mode::even)), std::string("even"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::parity_mode::mark)), std::string("mark"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::parity_mode::space)), std::string("space"));
    VS_CHECK_EQ(
        std::string(vs::to_string(static_cast<vs::parity_mode>(99))),
        std::string("unknown"));

    VS_CHECK_EQ(std::string(vs::to_string(vs::stop_bits_mode::one)), std::string("1"));
    VS_CHECK_EQ(
        std::string(vs::to_string(vs::stop_bits_mode::one_point_five)),
        std::string("1.5"));
    VS_CHECK_EQ(std::string(vs::to_string(vs::stop_bits_mode::two)), std::string("2"));
    VS_CHECK_EQ(
        std::string(vs::to_string(static_cast<vs::stop_bits_mode>(99))),
        std::string("unknown"));

    VS_CHECK_EQ(
        std::string(vs::to_string(vs::flow_control_mode::none)), std::string("none"));
    VS_CHECK_EQ(
        std::string(vs::to_string(vs::flow_control_mode::software)),
        std::string("software"));
    VS_CHECK_EQ(
        std::string(vs::to_string(vs::flow_control_mode::hardware)),
        std::string("hardware"));
    VS_CHECK_EQ(
        std::string(vs::to_string(static_cast<vs::flow_control_mode>(99))),
        std::string("unknown"));
}

VS_TEST_CASE(config_parse_aliases)
{
    // Parity aliases.
    VS_CHECK(vs::parse_uri("serial://COM1?parity=n").parity == vs::parity_mode::none);
    VS_CHECK(vs::parse_uri("serial://COM1?parity=o").parity == vs::parity_mode::odd);
    VS_CHECK(vs::parse_uri("serial://COM1?parity=e").parity == vs::parity_mode::even);
    VS_CHECK(vs::parse_uri("serial://COM1?parity=m").parity == vs::parity_mode::mark);
    VS_CHECK(vs::parse_uri("serial://COM1?parity=s").parity == vs::parity_mode::space);
    VS_CHECK(
        vs::parse_uri("serial://COM1?parity=SPACE").parity == vs::parity_mode::space);

    // Stop bit aliases.
    VS_CHECK(
        vs::parse_uri("serial://COM1?stop=one").stop_bits == vs::stop_bits_mode::one);
    VS_CHECK(
        vs::parse_uri("serial://COM1?stop=two").stop_bits == vs::stop_bits_mode::two);
    VS_CHECK(
        vs::parse_uri("serial://COM1?stop=onepointfive&data=5").stop_bits
        == vs::stop_bits_mode::one_point_five);

    // Flow control aliases.
    VS_CHECK(
        vs::parse_uri("serial://COM1?flow=off").flow_control
        == vs::flow_control_mode::none);
    VS_CHECK(
        vs::parse_uri("serial://COM1?flow=sw").flow_control
        == vs::flow_control_mode::software);
    VS_CHECK(
        vs::parse_uri("serial://COM1?flow=hw").flow_control
        == vs::flow_control_mode::hardware);

    // Boolean aliases.
    VS_CHECK(vs::parse_uri("serial://COM1?exclusive=true").exclusive);
    VS_CHECK(!vs::parse_uri("serial://COM1?exclusive=no").exclusive);

    // Scheme aliases.
    VS_CHECK(
        vs::parse_uri("com://COM2").backend == vs::backend_kind::physical);
    VS_CHECK(
        vs::parse_uri("physical://COM2").backend == vs::backend_kind::physical);
    VS_CHECK(vs::parse_uri("virtual://x").backend == vs::backend_kind::mock);
    VS_CHECK(vs::parse_uri("fixture://x").backend == vs::backend_kind::mock);
    VS_CHECK(vs::parse_uri("SERIAL://COM2").backend == vs::backend_kind::physical);
    VS_CHECK_EQ(vs::parse_uri("SERIAL://COM2").target, std::string("COM2"));
}

VS_TEST_CASE(config_parse_timeout_forms)
{
    VS_CHECK_EQ(
        vs::parse_uri("serial://COM1?timeout=250ms").read_timeout.count(), 250);
    VS_CHECK_EQ(
        vs::parse_uri("serial://COM1?timeout=2s").read_timeout.count(), 2000);
    VS_CHECK_EQ(
        vs::parse_uri("serial://COM1?write_timeout=5").write_timeout.count(), 5);

    boost::system::error_code ec;
    vs::parse_uri("serial://COM1?timeout=1h", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://COM1?timeout=999999999999s", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    ec.clear();
    vs::parse_uri("serial://COM1?baud=99999999999", ec);
    VS_CHECK(ec == vs::error::invalid_option);
}

VS_TEST_CASE(config_parse_empty_and_missing_values)
{
    // An empty query is accepted.
    const vs::serial_config empty = vs::parse_uri("mock://?");
    VS_CHECK(empty.backend == vs::backend_kind::mock);
    VS_CHECK(empty.target.empty());

    // Empty query items are skipped.
    const vs::serial_config skipped = vs::parse_uri("mock://?&&baud=1200");
    VS_CHECK_EQ(skipped.baud_rate, 1200u);

    // A key without a value is rejected.
    boost::system::error_code ec;
    vs::parse_uri("mock://?baud", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    // An empty value for a string-ish option is rejected.
    ec.clear();
    vs::parse_uri("mock://?parity=", ec);
    VS_CHECK(ec == vs::error::invalid_option);

    // An empty value for the boolean option is rejected too.
    ec.clear();
    vs::parse_uri("mock://?exclusive=", ec);
    VS_CHECK(ec == vs::error::invalid_option);
}

VS_TEST_CASE(config_parse_host_service_edge_cases)
{
    boost::system::error_code ec;

    ec.clear();
    vs::parse_uri("tcp://hostonly", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("tcp://:4001", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("tcp://host:", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("tcp://[::1]", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("tcp://[]:1", ec);
    VS_CHECK(ec == vs::error::bad_target);

    ec.clear();
    vs::parse_uri("tcp://[::1]:0", ec);
    VS_CHECK(!ec);
    VS_CHECK_EQ(vs::parse_uri("tcp://[::1]:0").target, std::string("[::1]:0"));
}

VS_TEST_CASE(config_validate_rejects_impossible_framing)
{
    vs::serial_config config;
    vs::error failure = vs::error::success;

    config.data_bits = 4;
    VS_CHECK(!vs::detail::validate(config, failure));
    VS_CHECK(failure == vs::error::invalid_option);

    config.data_bits = 9;
    VS_CHECK(!vs::detail::validate(config, failure));

    config.data_bits = 8;
    config.baud_rate = 0;
    VS_CHECK(!vs::detail::validate(config, failure));

    config.baud_rate = 9600;
    config.stop_bits = vs::stop_bits_mode::one_point_five;
    config.data_bits = 8;
    VS_CHECK(!vs::detail::validate(config, failure));

    config.data_bits = 5;
    VS_CHECK(vs::detail::validate(config, failure));

    config.data_bits = 8;
    config.stop_bits = vs::stop_bits_mode::one;
    config.read_timeout = std::chrono::milliseconds(-1);
    VS_CHECK(!vs::detail::validate(config, failure));

    config.read_timeout = std::chrono::milliseconds(0);
    config.write_timeout = std::chrono::milliseconds(-5);
    VS_CHECK(!vs::detail::validate(config, failure));
}

VS_TEST_CASE(config_to_uri_covers_every_backend)
{
    vs::serial_config config;
    config.target = "COM9";
    VS_CHECK_EQ(vs::to_uri(config), std::string("serial://COM9"));

    config.backend = vs::backend_kind::mock;
    config.target = "label";
    VS_CHECK_EQ(vs::to_uri(config), std::string("mock://label"));

    config.backend = vs::backend_kind::rfc2217;
    config.target = "host:7001";
    config.baud_rate = 115200;
    config.data_bits = 7;
    config.parity = vs::parity_mode::odd;
    config.stop_bits = vs::stop_bits_mode::two;
    config.flow_control = vs::flow_control_mode::hardware;
    config.read_timeout = std::chrono::milliseconds(10);
    config.write_timeout = std::chrono::milliseconds(20);
    config.exclusive = true;

    const std::string uri = vs::to_uri(config);
    VS_CHECK(uri.find("rfc2217://host:7001") == 0);
    VS_CHECK(uri.find("baud=115200") != std::string::npos);
    VS_CHECK(uri.find("data=7") != std::string::npos);
    VS_CHECK(uri.find("parity=odd") != std::string::npos);
    VS_CHECK(uri.find("stop=2") != std::string::npos);
    VS_CHECK(uri.find("flow=hardware") != std::string::npos);
    VS_CHECK(uri.find("read_timeout=10ms") != std::string::npos);
    VS_CHECK(uri.find("write_timeout=20ms") != std::string::npos);
    VS_CHECK(uri.find("exclusive=1") != std::string::npos);

    const vs::serial_config reparsed = vs::parse_uri(uri);
    VS_CHECK(reparsed == config);

    config.backend = vs::backend_kind::tcp;
    config.target = "host:4001";
    VS_CHECK_EQ(vs::to_uri(config).find("tcp://host:"), std::size_t(0));
    config.backend = vs::backend_kind::udp;
    VS_CHECK_EQ(vs::to_uri(config).find("udp://host:"), std::size_t(0));
}
