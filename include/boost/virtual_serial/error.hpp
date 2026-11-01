//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_ERROR_HPP
#define BOOST_VIRTUAL_SERIAL_ERROR_HPP

#include <boost/virtual_serial/config.hpp>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>
#include <boost/throw_exception.hpp>

#include <string>

namespace boost {
namespace virtual_serial {

/** Error conditions reported by the virtual serial front end.

    Backends translate their native failures into this enumeration so that
    application code can be backend agnostic.  A real serial port reports
    line errors (framing, parity, overrun) through the operating system;
    the mock backend reports the same values through explicit fault
    injection, and the RFC 2217 backend derives them from Telnet
    NOTIFY-LINESTATE subnegotiations.
*/
enum class error
{
    /// No error.
    success = 0,

    /// The port has not been opened, or a network peer closed it.
    not_connected,

    /// `open` was called on a port that is already open.
    already_open,

    /// The target string could not be parsed, or the peer address is bad.
    bad_target,

    /// An option value is outside the range accepted by the backend.
    invalid_option,

    /// The backend cannot represent this option (for example, a raw TCP
    /// bridge has no notion of parity).
    unsupported_option,

    /// The UART detected a framing error on the received byte.
    framing_error,

    /// The UART detected a parity error on the received byte.
    parity_error,

    /// The receive FIFO overflowed and data was lost.
    overrun,

    /// A break condition was detected on the line.
    break_detected,

    /// The modem line was lost (carrier detect dropped).
    hangup,

    /// A read or write exceeded its configured timeout.
    timeout,

    /// The peer violated the framing rules of the transport protocol.
    protocol_error,

    /// The read side of the mock backend is full and the bytes were
    /// dropped or rejected.  See `mock_port::set_read_capacity`.
    queue_full,

    /// The operation was cancelled.
    aborted,

    /// The requested feature is not implemented by this backend.
    not_supported
};

namespace detail {

class error_category_impl final : public boost::system::error_category
{
public:
    const char* name() const noexcept override
    {
        return "virtual_serial";
    }

    std::string message(int value) const override
    {
        switch (static_cast<error>(value))
        {
        case error::success:
            return "no error";
        case error::not_connected:
            return "port is not connected";
        case error::already_open:
            return "port is already open";
        case error::bad_target:
            return "invalid port target";
        case error::invalid_option:
            return "invalid serial option value";
        case error::unsupported_option:
            return "option is not supported by this backend";
        case error::framing_error:
            return "framing error";
        case error::parity_error:
            return "parity error";
        case error::overrun:
            return "receive buffer overrun";
        case error::break_detected:
            return "break condition detected";
        case error::hangup:
            return "modem hangup";
        case error::timeout:
            return "operation timed out";
        case error::protocol_error:
            return "serial protocol violation";
        case error::queue_full:
            return "read queue is full";
        case error::aborted:
            return "operation aborted";
        case error::not_supported:
            return "operation not supported";
        }
        return "unknown virtual_serial error";
    }
};

} // namespace detail

/** Returns the singleton error category for #error.
*/
inline const boost::system::error_category& error_category() noexcept
{
    static const detail::error_category_impl instance;
    return instance;
}

/** Creates an error code from a #error value.
*/
inline boost::system::error_code make_error_code(error value) noexcept
{
    return boost::system::error_code(
        static_cast<int>(value), error_category());
}

} // namespace virtual_serial

namespace system {

template <>
struct is_error_code_enum<virtual_serial::error>
{
    static const bool value = true;
};

} // namespace system
} // namespace boost

namespace boost {
namespace virtual_serial {
namespace detail {

inline void throw_error(const boost::system::error_code& ec)
{
#if BOOST_VIRTUAL_SERIAL_NO_EXCEPTIONS
    (void)ec;
#else
    if (ec)
        BOOST_THROW_EXCEPTION(boost::system::system_error(ec));
#endif
}

/// True when a blocking backend operation should be retried.
inline bool is_would_block(const boost::system::error_code& ec) noexcept
{
    return ec == boost::system::errc::make_error_code(
                     boost::system::errc::operation_would_block);
}

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_ERROR_HPP
