//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial/detail/telnet.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace telnet = boost::virtual_serial::detail::telnet;

namespace {

/// Records decoder callbacks in the order they happen.
struct event_log
{
    std::vector<std::string> events;

    telnet::decoder make_decoder()
    {
        telnet::decoder decoder;
        decoder.set_data_sink(
            [this](const unsigned char* data, std::size_t size) {
                events.push_back(
                    "data:" + std::string(reinterpret_cast<const char*>(data), size));
            });
        decoder.set_negotiation_sink(
            [this](unsigned char command, unsigned char option) {
                events.push_back(
                    "neg:" + std::to_string(static_cast<int>(command)) + ":"
                    + std::to_string(static_cast<int>(option)));
            });
        decoder.set_subnegotiation_sink(
            [this](
                unsigned char option,
                const unsigned char* payload,
                std::size_t size) {
                std::string event =
                    "sub:" + std::to_string(static_cast<int>(option)) + ":";
                for (std::size_t i = 0; i < size; ++i)
                {
                    if (i > 0)
                        event += ",";
                    event += std::to_string(static_cast<int>(payload[i]));
                }
                events.push_back(event);
            });
        return decoder;
    }

    void feed(telnet::decoder& decoder, std::initializer_list<int> bytes)
    {
        std::vector<unsigned char> data;
        for (const int byte : bytes)
            data.push_back(static_cast<unsigned char>(byte));
        decoder.feed(data.data(), data.size());
    }
};

} // namespace

VS_TEST_CASE(telnet_encode_negotiation)
{
    const std::vector<unsigned char> will =
        telnet::encode_negotiation(telnet::command_will, telnet::option_com_port);
    VS_CHECK_EQ(will.size(), 3u);
    VS_CHECK_EQ(static_cast<int>(will[0]), 255);
    VS_CHECK_EQ(static_cast<int>(will[1]), 251);
    VS_CHECK_EQ(static_cast<int>(will[2]), 44);
}

VS_TEST_CASE(telnet_encode_baud_rate)
{
    const std::vector<unsigned char> encoded = telnet::make_baud_rate(115200u);
    const std::vector<unsigned char> expected = {
        255, 250, 44, 1, 0, 1, 194, 0, 255, 240};
    VS_CHECK_EQ(encoded.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        VS_CHECK_EQ(static_cast<int>(encoded[i]), static_cast<int>(expected[i]));
}

VS_TEST_CASE(telnet_encode_single_byte_command)
{
    const std::vector<unsigned char> encoded = telnet::make_rfc2217(
        telnet::rfc2217::set_parity, telnet::parity_index(
            boost::virtual_serial::parity_mode::even));
    const std::vector<unsigned char> expected = {255, 250, 44, 3, 3, 255, 240};
    VS_CHECK_EQ(encoded.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        VS_CHECK_EQ(static_cast<int>(encoded[i]), static_cast<int>(expected[i]));
}

VS_TEST_CASE(telnet_encode_escapes_iac_in_payload)
{
    const unsigned char payload[2] = {255, 65};
    const std::vector<unsigned char> encoded =
        telnet::encode_rfc2217(0, payload, 2);
    const std::vector<unsigned char> expected = {
        255, 250, 44, 0, 255, 255, 65, 255, 240};
    VS_CHECK_EQ(encoded.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        VS_CHECK_EQ(static_cast<int>(encoded[i]), static_cast<int>(expected[i]));
}

VS_TEST_CASE(telnet_decode_plain_data)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {'h', 'e', 'l', 'l', 'o'});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("data:hello"));
}

VS_TEST_CASE(telnet_decode_negotiation_split_across_chunks)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();

    log.feed(decoder, {255});
    VS_CHECK_EQ(log.events.size(), 0u);

    log.feed(decoder, {251});
    VS_CHECK_EQ(log.events.size(), 0u);

    log.feed(decoder, {44});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("neg:251:44"));
}

VS_TEST_CASE(telnet_decode_escaped_iac_in_data)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {'A', 255, 255, 'B'});

    // The decoder flushes the data run before it consumes the IAC pair,
    // so the escaped byte arrives in its own callback.
    VS_REQUIRE(log.events.size() == 2);
    VS_CHECK_EQ(log.events[0], std::string("data:A"));
    const std::string expected =
        std::string("data:") + std::string(1, '\xff') + "B";
    VS_CHECK_EQ(log.events[1], expected);
}

VS_TEST_CASE(telnet_decode_subnegotiation)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {255, 250, 44, 6, 8, 255, 240});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("sub:44:6,8"));
}

VS_TEST_CASE(telnet_decode_subnegotiation_with_escapes)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {255, 250, 44, 0, 65, 255, 255, 66, 255, 240});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("sub:44:0,65,255,66"));
}

VS_TEST_CASE(telnet_decode_preserves_data_command_order)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {'A', 'B', 255, 251, 44, 'C', 'D'});
    VS_REQUIRE(log.events.size() == 3);
    VS_CHECK_EQ(log.events[0], std::string("data:AB"));
    VS_CHECK_EQ(log.events[1], std::string("neg:251:44"));
    VS_CHECK_EQ(log.events[2], std::string("data:CD"));
}

VS_TEST_CASE(telnet_decode_ignores_two_byte_commands)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    // IAC NOP (241) followed by data.
    log.feed(decoder, {255, 241, 'x'});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("data:x"));
}

VS_TEST_CASE(telnet_decode_recovers_from_stray_iac_in_subnegotiation)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    // IAC followed by 65 inside a subnegotiation: lenient recovery keeps
    // the byte and stays inside the subnegotiation.
    log.feed(decoder, {255, 250, 44, 7, 255, 65, 255, 240});
    VS_CHECK_EQ(log.events.size(), 1u);
    VS_CHECK_EQ(log.events[0], std::string("sub:44:7,65"));
}

VS_TEST_CASE(telnet_decode_truncates_oversized_payload)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    VS_CHECK(!decoder.truncated());

    std::vector<unsigned char> bytes = {255, 250, 44, 6};
    bytes.resize(4 + telnet::decoder::max_payload + 16, 'x');
    bytes.push_back(255);
    bytes.push_back(240);
    decoder.feed(bytes.data(), bytes.size());

    VS_CHECK(decoder.truncated());
    VS_REQUIRE(log.events.size() == 1u);
    VS_CHECK_EQ(log.events[0].substr(0, 8), std::string("sub:44:6"));
}

VS_TEST_CASE(telnet_decode_reset)
{
    event_log log;
    telnet::decoder decoder = log.make_decoder();
    log.feed(decoder, {255});
    decoder.reset();
    decoder.truncated();
    log.feed(decoder, {'x'});
    VS_REQUIRE(log.events.size() == 1u);
    VS_CHECK_EQ(log.events[0], std::string("data:x"));
}

VS_TEST_CASE(telnet_index_mappings)
{
    using boost::virtual_serial::parity_mode;
    using boost::virtual_serial::stop_bits_mode;
    using boost::virtual_serial::flow_control_mode;

    VS_CHECK_EQ(static_cast<int>(telnet::parity_index(parity_mode::none)), 1);
    VS_CHECK_EQ(static_cast<int>(telnet::parity_index(parity_mode::odd)), 2);
    VS_CHECK_EQ(static_cast<int>(telnet::parity_index(parity_mode::even)), 3);
    VS_CHECK_EQ(static_cast<int>(telnet::parity_index(parity_mode::mark)), 4);
    VS_CHECK_EQ(static_cast<int>(telnet::parity_index(parity_mode::space)), 5);
    VS_CHECK(telnet::parity_from_index(4) == parity_mode::mark);
    VS_CHECK(telnet::parity_from_index(1) == parity_mode::none);

    VS_CHECK_EQ(static_cast<int>(telnet::stop_size_index(stop_bits_mode::one)), 1);
    VS_CHECK_EQ(static_cast<int>(telnet::stop_size_index(stop_bits_mode::two)), 2);
    VS_CHECK_EQ(static_cast<int>(telnet::stop_size_index(stop_bits_mode::one_point_five)), 3);
    VS_CHECK(telnet::stop_size_from_index(3) == stop_bits_mode::one_point_five);

    VS_CHECK_EQ(
        static_cast<int>(telnet::control_index(flow_control_mode::none)), 1);
    VS_CHECK_EQ(
        static_cast<int>(telnet::control_index(flow_control_mode::software)), 2);
    VS_CHECK_EQ(
        static_cast<int>(telnet::control_index(flow_control_mode::hardware)), 3);

    VS_CHECK_EQ(static_cast<int>(telnet::data_size_index(7)), 7);
}
