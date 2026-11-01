//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_OPTION_HPP
#define BOOST_VIRTUAL_SERIAL_OPTION_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>

#include <boost/asio/serial_port_base.hpp>

#include <chrono>
#include <type_traits>

namespace boost {
namespace virtual_serial {

/** Option object that sets the front-end read timeout.

    The timeout is enforced by the type-erased front end rather than by a
    backend, so it behaves identically for physical devices, RFC 2217
    connections and the mock fixture:

    @code
    port.set_option(read_timeout(std::chrono::milliseconds(250)));
    @endcode

    A zero duration disables the timeout.
*/
class read_timeout
{
public:
    explicit read_timeout(std::chrono::milliseconds value) noexcept
        : value_(value)
    {
    }

    std::chrono::milliseconds value() const noexcept
    {
        return value_;
    }

private:
    std::chrono::milliseconds value_;
};

/** Option object that sets the front-end write timeout.

    A zero duration disables the timeout.
*/
class write_timeout
{
public:
    explicit write_timeout(std::chrono::milliseconds value) noexcept
        : value_(value)
    {
    }

    std::chrono::milliseconds value() const noexcept
    {
        return value_;
    }

private:
    std::chrono::milliseconds value_;
};

namespace detail {

/// Maps the five Asio serial options and the two library options onto
/// #serial_config.  Unknown option types report #error::unsupported_option
/// instead of failing to compile, which keeps the type-erased front end
/// usable with vendor specific option objects.
template <class Option>
struct option_traits
{
    static const bool supported = false;
};

template <>
struct option_traits<boost::asio::serial_port_base::baud_rate>
{
    static const bool supported = true;

    static void set(
        serial_config& config,
        const boost::asio::serial_port_base::baud_rate& option)
    {
        config.baud_rate = option.value();
    }

    static boost::system::error_code get(
        const serial_config& config,
        boost::asio::serial_port_base::baud_rate& option)
    {
        option = boost::asio::serial_port_base::baud_rate(config.baud_rate);
        return boost::system::error_code();
    }
};

template <>
struct option_traits<boost::asio::serial_port_base::character_size>
{
    static const bool supported = true;

    static void set(
        serial_config& config,
        const boost::asio::serial_port_base::character_size& option)
    {
        config.data_bits = option.value();
    }

    static boost::system::error_code get(
        const serial_config& config,
        boost::asio::serial_port_base::character_size& option)
    {
        option = boost::asio::serial_port_base::character_size(config.data_bits);
        return boost::system::error_code();
    }
};

template <>
struct option_traits<boost::asio::serial_port_base::parity>
{
    static const bool supported = true;

    static void set(
        serial_config& config,
        const boost::asio::serial_port_base::parity& option)
    {
        switch (option.value())
        {
        case boost::asio::serial_port_base::parity::none:
            config.parity = parity_mode::none;
            break;
        case boost::asio::serial_port_base::parity::odd:
            config.parity = parity_mode::odd;
            break;
        case boost::asio::serial_port_base::parity::even:
            config.parity = parity_mode::even;
            break;
        }
    }

    static boost::system::error_code get(
        const serial_config& config,
        boost::asio::serial_port_base::parity& option)
    {
        switch (config.parity)
        {
        case parity_mode::none:
            option = boost::asio::serial_port_base::parity(
                boost::asio::serial_port_base::parity::none);
            return boost::system::error_code();
        case parity_mode::odd:
            option = boost::asio::serial_port_base::parity(
                boost::asio::serial_port_base::parity::odd);
            return boost::system::error_code();
        case parity_mode::even:
            option = boost::asio::serial_port_base::parity(
                boost::asio::serial_port_base::parity::even);
            return boost::system::error_code();
        default:
            // mark/space parity cannot be represented by Asio.
            return error::unsupported_option;
        }
    }
};

template <>
struct option_traits<boost::asio::serial_port_base::stop_bits>
{
    static const bool supported = true;

    static void set(
        serial_config& config,
        const boost::asio::serial_port_base::stop_bits& option)
    {
        switch (option.value())
        {
        case boost::asio::serial_port_base::stop_bits::one:
            config.stop_bits = stop_bits_mode::one;
            break;
        case boost::asio::serial_port_base::stop_bits::onepointfive:
            config.stop_bits = stop_bits_mode::one_point_five;
            break;
        case boost::asio::serial_port_base::stop_bits::two:
            config.stop_bits = stop_bits_mode::two;
            break;
        }
    }

    static boost::system::error_code get(
        const serial_config& config,
        boost::asio::serial_port_base::stop_bits& option)
    {
        switch (config.stop_bits)
        {
        case stop_bits_mode::one:
            option = boost::asio::serial_port_base::stop_bits(
                boost::asio::serial_port_base::stop_bits::one);
            return boost::system::error_code();
        case stop_bits_mode::one_point_five:
            option = boost::asio::serial_port_base::stop_bits(
                boost::asio::serial_port_base::stop_bits::onepointfive);
            return boost::system::error_code();
        case stop_bits_mode::two:
            option = boost::asio::serial_port_base::stop_bits(
                boost::asio::serial_port_base::stop_bits::two);
            return boost::system::error_code();
        }
        return error::unsupported_option;
    }
};

template <>
struct option_traits<boost::asio::serial_port_base::flow_control>
{
    static const bool supported = true;

    static void set(
        serial_config& config,
        const boost::asio::serial_port_base::flow_control& option)
    {
        switch (option.value())
        {
        case boost::asio::serial_port_base::flow_control::none:
            config.flow_control = flow_control_mode::none;
            break;
        case boost::asio::serial_port_base::flow_control::software:
            config.flow_control = flow_control_mode::software;
            break;
        case boost::asio::serial_port_base::flow_control::hardware:
            config.flow_control = flow_control_mode::hardware;
            break;
        }
    }

    static boost::system::error_code get(
        const serial_config& config,
        boost::asio::serial_port_base::flow_control& option)
    {
        switch (config.flow_control)
        {
        case flow_control_mode::none:
            option = boost::asio::serial_port_base::flow_control(
                boost::asio::serial_port_base::flow_control::none);
            return boost::system::error_code();
        case flow_control_mode::software:
            option = boost::asio::serial_port_base::flow_control(
                boost::asio::serial_port_base::flow_control::software);
            return boost::system::error_code();
        case flow_control_mode::hardware:
            option = boost::asio::serial_port_base::flow_control(
                boost::asio::serial_port_base::flow_control::hardware);
            return boost::system::error_code();
        }
        return error::unsupported_option;
    }
};

template <>
struct option_traits<read_timeout>
{
    static const bool supported = true;

    static void set(serial_config& config, const read_timeout& option)
    {
        config.read_timeout = option.value();
    }

    static boost::system::error_code get(
        const serial_config& config, read_timeout& option)
    {
        option = read_timeout(config.read_timeout);
        return boost::system::error_code();
    }
};

template <>
struct option_traits<write_timeout>
{
    static const bool supported = true;

    static void set(serial_config& config, const write_timeout& option)
    {
        config.write_timeout = option.value();
    }

    static boost::system::error_code get(
        const serial_config& config, write_timeout& option)
    {
        option = write_timeout(config.write_timeout);
        return boost::system::error_code();
    }
};

template <class Option>
inline typename std::enable_if<option_traits<Option>::supported, void>::type
apply_option(
    const Option& option,
    serial_config& config,
    boost::system::error_code& ec)
{
    serial_config candidate = config;
    option_traits<Option>::set(candidate, option);

    error failure = error::success;
    if (!validate(candidate, failure))
    {
        ec = failure;
        return;
    }
    config = candidate;
    ec.clear();
}

template <class Option>
inline typename std::enable_if<!option_traits<Option>::supported, void>::type
apply_option(
    const Option&,
    serial_config&,
    boost::system::error_code& ec)
{
    ec = error::unsupported_option;
}

template <class Option>
inline typename std::enable_if<option_traits<Option>::supported, void>::type
extract_option(
    const serial_config& config,
    Option& option,
    boost::system::error_code& ec)
{
    ec = option_traits<Option>::get(config, option);
}

template <class Option>
inline typename std::enable_if<!option_traits<Option>::supported, void>::type
extract_option(
    const serial_config&,
    Option&,
    boost::system::error_code& ec)
{
    ec = error::unsupported_option;
}

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_OPTION_HPP
