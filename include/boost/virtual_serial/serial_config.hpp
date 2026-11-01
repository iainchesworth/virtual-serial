//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_SERIAL_CONFIG_HPP
#define BOOST_VIRTUAL_SERIAL_SERIAL_CONFIG_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>

#include <chrono>
#include <cstdint>
#include <string>

namespace boost {
namespace virtual_serial {

/** Identifies the transport that backs a #serial_port.

    The value is carried in the URI scheme (see #parse_uri), which makes a
    port constructible from configuration data alone:

    @code
    auto port = make_serial_port(io, "rfc2217://lab-host:7001?baud=115200");
    @endcode
*/
enum class backend_kind
{
    /// A real operating system serial port (`boost::asio::serial_port`).
    physical,

    /// A deterministic in-memory fixture.
    mock,

    /// Serial-over-Telnet, RFC 2217.
    rfc2217,

    /// A raw TCP byte stream bridged to a serial device.
    tcp,

    /// A datagram (UDP) bridge.
    udp
};

/// Line parity.
enum class parity_mode
{
    none,
    odd,
    even,
    mark,
    space
};

/// Number of stop bits.
enum class stop_bits_mode
{
    one,
    one_point_five,
    two
};

/// Flow control mode.
enum class flow_control_mode
{
    none,
    software,
    hardware
};

/// Returns the canonical spelling of a backend kind.
inline const char* to_string(backend_kind kind) noexcept
{
    switch (kind)
    {
    case backend_kind::physical:
        return "physical";
    case backend_kind::mock:
        return "mock";
    case backend_kind::rfc2217:
        return "rfc2217";
    case backend_kind::tcp:
        return "tcp";
    case backend_kind::udp:
        return "udp";
    }
    return "unknown";
}

/// Returns the canonical spelling of a parity mode.
inline const char* to_string(parity_mode value) noexcept
{
    switch (value)
    {
    case parity_mode::none:
        return "none";
    case parity_mode::odd:
        return "odd";
    case parity_mode::even:
        return "even";
    case parity_mode::mark:
        return "mark";
    case parity_mode::space:
        return "space";
    }
    return "unknown";
}

/// Returns the canonical spelling of a stop-bit setting.
inline const char* to_string(stop_bits_mode value) noexcept
{
    switch (value)
    {
    case stop_bits_mode::one:
        return "1";
    case stop_bits_mode::one_point_five:
        return "1.5";
    case stop_bits_mode::two:
        return "2";
    }
    return "unknown";
}

/// Returns the canonical spelling of a flow-control setting.
inline const char* to_string(flow_control_mode value) noexcept
{
    switch (value)
    {
    case flow_control_mode::none:
        return "none";
    case flow_control_mode::software:
        return "software";
    case flow_control_mode::hardware:
        return "hardware";
    }
    return "unknown";
}

/** Immutable description of a serial port.

    A `serial_config` is a value type: it can be copied, compared, stored in
    a configuration file and converted to and from a URI.  It is the single
    currency of the library: every backend accepts one through
    `apply_options`, and the type-erased front end forwards Asio serial
    options into it.

    The `target` member is backend specific:

    - `physical`: device name, for example `COM3` or `/dev/ttyUSB0`
    - `rfc2217`: `host:service`, for example `lab-host:7001`
    - `tcp` / `udp`: `host:service` or `[ipv6]:service`
    - `mock`: ignored, may be empty (a label is allowed for diagnostics)
*/
struct serial_config
{
    /// Transport that should service the port.
    backend_kind backend = backend_kind::physical;

    /// Backend specific target (device name or network endpoint).
    std::string target;

    /// Bits per second.
    unsigned int baud_rate = 9600;

    /// Number of data bits, 5 through 8.
    unsigned int data_bits = 8;

    /// Line parity.
    parity_mode parity = parity_mode::none;

    /// Number of stop bits.
    stop_bits_mode stop_bits = stop_bits_mode::one;

    /// Flow control mode.
    flow_control_mode flow_control = flow_control_mode::none;

    /// Read timeout.  A zero duration disables the timeout.  Enforced by
    /// the front end, so it works for every backend.
    std::chrono::milliseconds read_timeout{0};

    /// Write timeout.  A zero duration disables the timeout.
    std::chrono::milliseconds write_timeout{0};

    /// Request exclusive access to the device where the platform supports
    /// it.
    bool exclusive = false;
};

/// Equality compares every field.
inline bool operator==(const serial_config& lhs, const serial_config& rhs) noexcept
{
    return lhs.backend == rhs.backend && lhs.target == rhs.target
        && lhs.baud_rate == rhs.baud_rate && lhs.data_bits == rhs.data_bits
        && lhs.parity == rhs.parity && lhs.stop_bits == rhs.stop_bits
        && lhs.flow_control == rhs.flow_control
        && lhs.read_timeout == rhs.read_timeout
        && lhs.write_timeout == rhs.write_timeout
        && lhs.exclusive == rhs.exclusive;
}

inline bool operator!=(const serial_config& lhs, const serial_config& rhs) noexcept
{
    return !(lhs == rhs);
}

namespace detail {

inline std::string lowercase(std::string text)
{
    for (std::string::size_type i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c >= 'A' && c <= 'Z')
            text[i] = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

inline bool parse_unsigned(const std::string& text, unsigned int& out)
{
    if (text.empty())
        return false;
    unsigned int value = 0;
    for (std::string::size_type i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c < '0' || c > '9')
            return false;
        const unsigned int digit = static_cast<unsigned int>(c - '0');
        if (value > (0xffffffffu - digit) / 10u)
            return false;
        value = value * 10u + digit;
    }
    out = value;
    return true;
}

inline bool parse_milliseconds(const std::string& text, std::chrono::milliseconds& out)
{
    std::string digits = text;
    std::chrono::milliseconds::rep scale = 1;
    if (digits.size() > 2 && digits.compare(digits.size() - 2, 2, "ms") == 0)
        digits.erase(digits.size() - 2);
    else if (!digits.empty() && digits[digits.size() - 1] == 's')
    {
        scale = 1000;
        digits.erase(digits.size() - 1);
    }
    unsigned int value = 0;
    if (!parse_unsigned(digits, value))
        return false;
    if (value > 0xffffffffu / static_cast<unsigned int>(scale))
        return false;
    out = std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(value) * scale);
    return true;
}

inline bool parse_parity(const std::string& text, parity_mode& out)
{
    const std::string value = lowercase(text);
    if (value == "none" || value == "n")
        out = parity_mode::none;
    else if (value == "odd" || value == "o")
        out = parity_mode::odd;
    else if (value == "even" || value == "e")
        out = parity_mode::even;
    else if (value == "mark" || value == "m")
        out = parity_mode::mark;
    else if (value == "space" || value == "s")
        out = parity_mode::space;
    else
        return false;
    return true;
}

inline bool parse_stop_bits(const std::string& text, stop_bits_mode& out)
{
    const std::string value = lowercase(text);
    if (value == "1" || value == "one")
        out = stop_bits_mode::one;
    else if (value == "1.5" || value == "one_point_five" || value == "onepointfive")
        out = stop_bits_mode::one_point_five;
    else if (value == "2" || value == "two")
        out = stop_bits_mode::two;
    else
        return false;
    return true;
}

inline bool parse_flow_control(const std::string& text, flow_control_mode& out)
{
    const std::string value = lowercase(text);
    if (value == "none" || value == "off" || value == "0")
        out = flow_control_mode::none;
    else if (value == "software" || value == "sw" || value == "xonxoff")
        out = flow_control_mode::software;
    else if (value == "hardware" || value == "hw" || value == "rtscts")
        out = flow_control_mode::hardware;
    else
        return false;
    return true;
}

inline bool parse_bool(const std::string& text, bool& out)
{
    const std::string value = lowercase(text);
    if (value == "1" || value == "true" || value == "yes" || value == "on")
        out = true;
    else if (value == "0" || value == "false" || value == "no" || value == "off")
        out = false;
    else
        return false;
    return true;
}

inline bool parse_backend(const std::string& text, backend_kind& out)
{
    const std::string value = lowercase(text);
    if (value == "serial" || value == "physical" || value == "com")
        out = backend_kind::physical;
    else if (value == "mock" || value == "virtual" || value == "fixture")
        out = backend_kind::mock;
    else if (value == "rfc2217")
        out = backend_kind::rfc2217;
    else if (value == "tcp")
        out = backend_kind::tcp;
    else if (value == "udp")
        out = backend_kind::udp;
    else
        return false;
    return true;
}

/// Splits `host:service` into its two halves, supporting bracketed IPv6
/// literals (`[::1]:7001`).
inline bool split_host_service(
    const std::string& text, std::string& host, std::string& service)
{
    if (text.empty())
        return false;
    if (text[0] == '[')
    {
        const std::string::size_type close = text.find(']');
        if (close == std::string::npos || close + 1 >= text.size()
            || text[close + 1] != ':')
            return false;
        host = text.substr(1, close - 1);
        service = text.substr(close + 2);
        return !host.empty() && !service.empty();
    }
    const std::string::size_type colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size())
        return false;
    host = text.substr(0, colon);
    service = text.substr(colon + 1);
    return true;
}

/// Validates the parts of a configuration that do not depend on a backend.
inline bool validate(const serial_config& config, error& failure) noexcept
{
    if (config.data_bits < 5 || config.data_bits > 8)
    {
        failure = error::invalid_option;
        return false;
    }
    if (config.baud_rate == 0)
    {
        failure = error::invalid_option;
        return false;
    }
    if (config.stop_bits == stop_bits_mode::one_point_five && config.data_bits > 5)
    {
        // RFC 2217 and POSIX both restrict 1.5 stop bits to 5 data bits.
        failure = error::invalid_option;
        return false;
    }
    if (config.read_timeout.count() < 0 || config.write_timeout.count() < 0)
    {
        failure = error::invalid_option;
        return false;
    }
    return true;
}

class uri_appender
{
public:
    explicit uri_appender(std::string& out) : out_(out) {}

    void operator()(const char* key, const std::string& value)
    {
        out_ += first_ ? '?' : '&';
        first_ = false;
        out_ += key;
        out_ += '=';
        out_ += value;
    }

    void operator()(const char* key, unsigned int value)
    {
        (*this)(key, std::to_string(value));
    }

private:
    std::string& out_;
    bool first_ = true;
};

inline serial_config parse_uri_impl(
    const std::string& uri, boost::system::error_code& ec)
{
    serial_config config;
    ec.clear();

    std::string rest = uri;
    const std::string::size_type scheme_pos = uri.find("://");
    if (scheme_pos != std::string::npos)
    {
        const std::string scheme = lowercase(uri.substr(0, scheme_pos));
        if (!parse_backend(scheme, config.backend))
        {
            ec = error::bad_target;
            return config;
        }
        rest = uri.substr(scheme_pos + 3);
    }
    else
    {
        // A bare device name such as "COM3" or "/dev/ttyUSB0".
        config.backend = backend_kind::physical;
    }

    std::string query;
    const std::string::size_type question = rest.find('?');
    if (question != std::string::npos)
    {
        query = rest.substr(question + 1);
        rest.erase(question);
    }
    config.target = rest;

    if (config.backend == backend_kind::physical && config.target.empty())
    {
        ec = error::bad_target;
        return config;
    }
    if (config.backend == backend_kind::rfc2217
        || config.backend == backend_kind::tcp
        || config.backend == backend_kind::udp)
    {
        std::string host;
        std::string service;
        if (!split_host_service(config.target, host, service))
        {
            ec = error::bad_target;
            return config;
        }
    }

    std::string::size_type pos = 0;
    while (pos <= query.size())
    {
        const std::string::size_type amp = query.find('&', pos);
        const std::string item = query.substr(
            pos,
            amp == std::string::npos ? std::string::npos : amp - pos);
        pos = amp == std::string::npos ? query.size() + 1 : amp + 1;
        if (item.empty())
            continue;

        const std::string::size_type equals = item.find('=');
        const std::string key = lowercase(item.substr(0, equals));
        const std::string value =
            equals == std::string::npos ? std::string() : item.substr(equals + 1);

        bool ok = true;
        if (key == "baud" || key == "baud_rate" || key == "baudrate")
            ok = parse_unsigned(value, config.baud_rate);
        else if (key == "data" || key == "data_bits" || key == "datasize")
            ok = parse_unsigned(value, config.data_bits);
        else if (key == "parity")
            ok = parse_parity(value, config.parity);
        else if (key == "stop" || key == "stop_bits" || key == "stopsize")
            ok = parse_stop_bits(value, config.stop_bits);
        else if (key == "flow" || key == "flow_control" || key == "control")
            ok = parse_flow_control(value, config.flow_control);
        else if (key == "read_timeout" || key == "timeout")
            ok = parse_milliseconds(value, config.read_timeout);
        else if (key == "write_timeout")
            ok = parse_milliseconds(value, config.write_timeout);
        else if (key == "exclusive")
            ok = parse_bool(value, config.exclusive);
        else
        {
            ec = error::bad_target;
            return config;
        }

        if (!ok)
        {
            ec = error::invalid_option;
            return config;
        }
    }

    error failure = error::success;
    if (!validate(config, failure))
    {
        ec = failure;
        return config;
    }
    return config;
}

} // namespace detail

/** Parses a URI into a #serial_config.

    Accepted forms:

    | URI                                   | Backend                  |
    |---------------------------------------|--------------------------|
    | `serial://COM3`                       | `backend_kind::physical` |
    | `serial:///dev/ttyUSB0`               | `backend_kind::physical` |
    | `COM3`                                | `backend_kind::physical` |
    | `mock://`                             | `backend_kind::mock`     |
    | `rfc2217://host:7001`                 | `backend_kind::rfc2217`  |
    | `tcp://host:7001`                     | `backend_kind::tcp`      |
    | `udp://host:7001`                     | `backend_kind::udp`      |

    Any form accepts a query string:

    @code
    rfc2217://lab-host:7001?baud=115200&data=8&parity=even&stop=1&flow=none
                           &read_timeout=250ms&write_timeout=1s&exclusive=1
    @endcode

    Unknown query keys are rejected rather than ignored so that a typo in
    a deployment configuration is not silently ignored.

    @par Exception Safety
    Strong guarantee.

    @throws boost::system::system_error with #error::bad_target or
    #error::invalid_option on failure.
*/
inline serial_config parse_uri(const std::string& uri)
{
    boost::system::error_code ec;
    serial_config config = detail::parse_uri_impl(uri, ec);
    detail::throw_error(ec);
    return config;
}

/** Parses a URI into a #serial_config, reporting failure through `ec`.
*/
inline serial_config parse_uri(const std::string& uri, boost::system::error_code& ec)
{
    return detail::parse_uri_impl(uri, ec);
}

/** Serialises a #serial_config to its canonical URI form.

    Only non-default settings are emitted, so the result round-trips
    through #parse_uri and stays readable in logs.
*/
inline std::string to_uri(const serial_config& config)
{
    std::string result;
    switch (config.backend)
    {
    case backend_kind::physical:
        result = "serial://";
        break;
    case backend_kind::mock:
        result = "mock://";
        break;
    case backend_kind::rfc2217:
        result = "rfc2217://";
        break;
    case backend_kind::tcp:
        result = "tcp://";
        break;
    case backend_kind::udp:
        result = "udp://";
        break;
    }
    result += config.target;

    detail::uri_appender append(result);
    if (config.baud_rate != 9600u)
        append("baud", config.baud_rate);
    if (config.data_bits != 8u)
        append("data", config.data_bits);
    if (config.parity != parity_mode::none)
        append("parity", to_string(config.parity));
    if (config.stop_bits != stop_bits_mode::one)
        append("stop", to_string(config.stop_bits));
    if (config.flow_control != flow_control_mode::none)
        append("flow", to_string(config.flow_control));
    if (config.read_timeout.count() > 0)
        append("read_timeout", std::to_string(config.read_timeout.count()) + "ms");
    if (config.write_timeout.count() > 0)
        append("write_timeout", std::to_string(config.write_timeout.count()) + "ms");
    if (config.exclusive)
        append("exclusive", "1");

    return result;
}

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_SERIAL_CONFIG_HPP
