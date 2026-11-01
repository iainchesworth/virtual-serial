//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial/error.hpp>

#include <boost/system/error_code.hpp>

#include <string>

namespace vs = boost::virtual_serial;

VS_TEST_CASE(error_category_is_named)
{
    VS_CHECK_EQ(std::string(vs::error_category().name()), std::string("virtual_serial"));
}

VS_TEST_CASE(error_enum_converts_to_error_code)
{
    boost::system::error_code ec = vs::error::timeout;
    VS_CHECK(static_cast<bool>(ec));
    VS_CHECK_EQ(ec.category(), vs::error_category());
    VS_CHECK_EQ(ec.value(), static_cast<int>(vs::error::timeout));
    VS_CHECK(ec == vs::error::timeout);
    VS_CHECK(ec != vs::error::hangup);
}

VS_TEST_CASE(error_messages_are_distinct)
{
    const vs::error values[] = {
        vs::error::success,
        vs::error::not_connected,
        vs::error::already_open,
        vs::error::bad_target,
        vs::error::invalid_option,
        vs::error::unsupported_option,
        vs::error::framing_error,
        vs::error::parity_error,
        vs::error::overrun,
        vs::error::break_detected,
        vs::error::hangup,
        vs::error::timeout,
        vs::error::protocol_error,
        vs::error::queue_full,
        vs::error::aborted,
        vs::error::not_supported};

    for (const vs::error value : values)
    {
        const std::string message =
            vs::error_category().message(static_cast<int>(value));
        VS_CHECK(!message.empty());
        VS_CHECK_NE(message, std::string("unknown virtual_serial error"));
    }

    VS_CHECK_EQ(
        vs::error_category().message(static_cast<int>(vs::error::timeout)),
        std::string("operation timed out"));
}

VS_TEST_CASE(error_success_is_falsy)
{
    const boost::system::error_code ec = vs::make_error_code(vs::error::success);
    VS_CHECK(!ec);
    VS_CHECK_EQ(ec.value(), 0);
}
