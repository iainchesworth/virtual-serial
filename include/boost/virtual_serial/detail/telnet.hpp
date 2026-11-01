//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_TELNET_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_TELNET_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/serial_config.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace boost {
namespace virtual_serial {
namespace detail {
namespace telnet {

// RFC 854 command bytes.
constexpr unsigned char command_se = 240;
constexpr unsigned char command_sb = 250;
constexpr unsigned char command_will = 251;
constexpr unsigned char command_wont = 252;
constexpr unsigned char command_do = 253;
constexpr unsigned char command_dont = 254;
constexpr unsigned char command_iac = 255;

/// Telnet option number for RFC 2217 com port control.
constexpr unsigned char option_com_port = 44;

/** RFC 2217 command codes.

    The client sends the low numbers; the access server acknowledges with
    the same command plus 100, which is why the decoder accepts both.
*/
namespace rfc2217 {

constexpr unsigned char set_baud_rate = 1;
constexpr unsigned char set_data_size = 2;
constexpr unsigned char set_parity = 3;
constexpr unsigned char set_stop_size = 4;
constexpr unsigned char set_control = 5;
constexpr unsigned char notify_line_state = 6;
constexpr unsigned char notify_modem_state = 7;
constexpr unsigned char flow_control_suspend = 8;
constexpr unsigned char flow_control_resume = 9;
constexpr unsigned char set_line_state_mask = 10;
constexpr unsigned char set_modem_state_mask = 11;
constexpr unsigned char purge_data = 12;
constexpr unsigned char signature = 0;

constexpr unsigned char server_offset = 100;

// SET-CONTROL values.
constexpr unsigned char control_request_flow = 0;
constexpr unsigned char control_no_flow = 1;
constexpr unsigned char control_xon_xoff = 2;
constexpr unsigned char control_hardware = 3;
constexpr unsigned char control_request_break = 4;
constexpr unsigned char control_break_on = 5;
constexpr unsigned char control_break_off = 6;

// PURGE-DATA values.
constexpr unsigned char purge_receive = 1;
constexpr unsigned char purge_transmit = 2;
constexpr unsigned char purge_both = 3;

} // namespace rfc2217

/// Bit masks used by NOTIFY-LINESTATE.
namespace line_state {

constexpr unsigned char data_ready = 1;
constexpr unsigned char overrun = 2;
constexpr unsigned char parity = 4;
constexpr unsigned char framing = 8;
constexpr unsigned char break_detect = 16;
constexpr unsigned char transmit_holding_empty = 32;
constexpr unsigned char transmit_shift_empty = 64;
constexpr unsigned char timeout = 128;

} // namespace line_state

/// Bit masks used by NOTIFY-MODEMSTATE.
namespace modem_state {

constexpr unsigned char delta_clear_to_send = 1;
constexpr unsigned char delta_data_set_ready = 2;
constexpr unsigned char trailing_ring = 4;
constexpr unsigned char delta_carrier_detect = 8;
constexpr unsigned char clear_to_send = 16;
constexpr unsigned char data_set_ready = 32;
constexpr unsigned char ring_indicator = 64;
constexpr unsigned char carrier_detect = 128;

} // namespace modem_state

/** Encodes a Telnet command such as `IAC WILL 44`. */
inline std::vector<unsigned char> encode_negotiation(
    unsigned char command, unsigned char option)
{
    std::vector<unsigned char> out;
    out.reserve(3);
    out.push_back(command_iac);
    out.push_back(command);
    out.push_back(option);
    return out;
}

/** Appends `payload` to `out`, doubling every IAC byte.

    Telnet requires 0xFF inside data or subnegotiation payloads to be
    escaped as 0xFF 0xFF so it cannot be mistaken for a command.
*/
inline void append_escaped(
    std::vector<unsigned char>& out,
    const unsigned char* payload,
    std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i)
    {
        out.push_back(payload[i]);
        if (payload[i] == command_iac)
            out.push_back(command_iac);
    }
}

/** Encodes `IAC SB <option> <payload> IAC SE`. */
inline std::vector<unsigned char> encode_subnegotiation(
    unsigned char option, const unsigned char* payload, std::size_t size)
{
    std::vector<unsigned char> out;
    out.reserve(size + 5);
    out.push_back(command_iac);
    out.push_back(command_sb);
    out.push_back(option);
    append_escaped(out, payload, size);
    out.push_back(command_iac);
    out.push_back(command_se);
    return out;
}

/// Encodes an RFC 2217 com port command inside a Telnet subnegotiation.
inline std::vector<unsigned char> encode_rfc2217(
    unsigned char opcode, const unsigned char* payload, std::size_t size)
{
    std::vector<unsigned char> body;
    body.reserve(size + 1);
    body.push_back(opcode);
    for (std::size_t i = 0; i < size; ++i)
        body.push_back(payload[i]);
    return encode_subnegotiation(option_com_port, body.data(), body.size());
}

/// Builds `SET-BAUDRATE` with a four byte big-endian value.
inline std::vector<unsigned char> make_baud_rate(unsigned int baud_rate)
{
    const unsigned char payload[4] = {
        static_cast<unsigned char>((baud_rate >> 24) & 0xffu),
        static_cast<unsigned char>((baud_rate >> 16) & 0xffu),
        static_cast<unsigned char>((baud_rate >> 8) & 0xffu),
        static_cast<unsigned char>(baud_rate & 0xffu)};
    std::vector<unsigned char> out;
    out.push_back(command_iac);
    out.push_back(command_sb);
    out.push_back(option_com_port);
    out.push_back(rfc2217::set_baud_rate);
    append_escaped(out, payload, 4);
    out.push_back(command_iac);
    out.push_back(command_se);
    return out;
}

/// Builds a one byte RFC 2217 command.
inline std::vector<unsigned char> make_rfc2217(unsigned char opcode, unsigned char value)
{
    const unsigned char payload[1] = {value};
    std::vector<unsigned char> out;
    out.push_back(command_iac);
    out.push_back(command_sb);
    out.push_back(option_com_port);
    out.push_back(opcode);
    append_escaped(out, payload, 1);
    out.push_back(command_iac);
    out.push_back(command_se);
    return out;
}

/// RFC 2217 index for a data bit count (5 through 8).
inline unsigned char data_size_index(unsigned int data_bits) noexcept
{
    return static_cast<unsigned char>(data_bits);
}

/// RFC 2217 index for a parity mode.
inline unsigned char parity_index(parity_mode value) noexcept
{
    switch (value)
    {
    case parity_mode::none:
        return 1;
    case parity_mode::odd:
        return 2;
    case parity_mode::even:
        return 3;
    case parity_mode::mark:
        return 4;
    case parity_mode::space:
        return 5;
    }
    return 1;
}

/// Decodes an RFC 2217 parity index.
inline parity_mode parity_from_index(unsigned char value) noexcept
{
    switch (value)
    {
    case 2:
        return parity_mode::odd;
    case 3:
        return parity_mode::even;
    case 4:
        return parity_mode::mark;
    case 5:
        return parity_mode::space;
    default:
        return parity_mode::none;
    }
}

/// RFC 2217 index for a stop bit setting.
inline unsigned char stop_size_index(stop_bits_mode value) noexcept
{
    switch (value)
    {
    case stop_bits_mode::one:
        return 1;
    case stop_bits_mode::two:
        return 2;
    case stop_bits_mode::one_point_five:
        return 3;
    }
    return 1;
}

/// Decodes an RFC 2217 stop bit index.
inline stop_bits_mode stop_size_from_index(unsigned char value) noexcept
{
    switch (value)
    {
    case 2:
        return stop_bits_mode::two;
    case 3:
        return stop_bits_mode::one_point_five;
    default:
        return stop_bits_mode::one;
    }
}

/// RFC 2217 SET-CONTROL value for a flow control mode.
inline unsigned char control_index(flow_control_mode value) noexcept
{
    switch (value)
    {
    case flow_control_mode::none:
        return rfc2217::control_no_flow;
    case flow_control_mode::software:
        return rfc2217::control_xon_xoff;
    case flow_control_mode::hardware:
        return rfc2217::control_hardware;
    }
    return rfc2217::control_no_flow;
}

/** Incremental Telnet decoder.

    Bytes arrive from the network in arbitrary chunks, so the decoder keeps
    the state of any partially received command between calls.  Data runs
    are accumulated and handed to the sink in contiguous blocks, always
    *before* the command that follows them, which preserves the ordering
    between payload bytes and line state notifications.

    The payload size of a subnegotiation is capped so that a hostile or
    broken peer cannot make the client allocate without bound.
*/
class decoder
{
public:
    using data_sink =
        std::function<void(const unsigned char*, std::size_t)>;
    using negotiation_sink =
        std::function<void(unsigned char command, unsigned char option)>;
    using subnegotiation_sink = std::function<void(
        unsigned char option, const unsigned char* payload, std::size_t size)>;

    /// Maximum accepted subnegotiation payload.
    enum : std::size_t
    {
        max_payload = 8192
    };

    void set_data_sink(data_sink sink)
    {
        data_sink_ = std::move(sink);
    }

    void set_negotiation_sink(negotiation_sink sink)
    {
        negotiation_sink_ = std::move(sink);
    }

    void set_subnegotiation_sink(subnegotiation_sink sink)
    {
        subnegotiation_sink_ = std::move(sink);
    }

    /// True when a subnegotiation payload had to be truncated.
    bool truncated() const noexcept
    {
        return truncated_;
    }

    void reset()
    {
        state_ = state::data;
        option_ = 0;
        command_ = 0;
        payload_.clear();
        pending_.clear();
        truncated_ = false;
    }

    /// Feeds bytes read from the transport.
    void feed(const unsigned char* data, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
        {
            const unsigned char byte = data[i];
            switch (state_)
            {
            case state::data:
                if (byte == command_iac)
                {
                    flush();
                    state_ = state::iac;
                }
                else
                {
                    pending_.push_back(byte);
                }
                break;

            case state::iac:
                if (byte == command_iac)
                {
                    pending_.push_back(command_iac);
                    state_ = state::data;
                }
                else if (byte == command_sb)
                {
                    flush();
                    state_ = state::subnegotiation_option;
                }
                else if (
                    byte == command_will || byte == command_wont
                    || byte == command_do || byte == command_dont)
                {
                    flush();
                    command_ = byte;
                    state_ = state::negotiation;
                }
                else
                {
                    // Two byte command we do not care about (NOP, GA, ...).
                    flush();
                    state_ = state::data;
                }
                break;

            case state::negotiation:
                flush();
                if (negotiation_sink_)
                    negotiation_sink_(command_, byte);
                state_ = state::data;
                break;

            case state::subnegotiation_option:
                option_ = byte;
                payload_.clear();
                state_ = state::subnegotiation_data;
                break;

            case state::subnegotiation_data:
                if (byte == command_iac)
                    state_ = state::subnegotiation_iac;
                else
                    push_payload(byte);
                break;

            case state::subnegotiation_iac:
                if (byte == command_se)
                {
                    flush();
                    if (subnegotiation_sink_)
                        subnegotiation_sink_(
                            option_, payload_.data(), payload_.size());
                    payload_.clear();
                    state_ = state::data;
                }
                else if (byte == command_iac)
                {
                    push_payload(command_iac);
                    state_ = state::subnegotiation_data;
                }
                else
                {
                    // Lenient recovery: keep the byte and stay in the
                    // subnegotiation rather than desynchronising.
                    push_payload(byte);
                    state_ = state::subnegotiation_data;
                }
                break;
            }
        }
        flush();
    }

private:
    enum class state
    {
        data,
        iac,
        negotiation,
        subnegotiation_option,
        subnegotiation_data,
        subnegotiation_iac
    };

    void push_payload(unsigned char byte)
    {
        if (payload_.size() < max_payload)
            payload_.push_back(byte);
        else
            truncated_ = true;
    }

    void flush()
    {
        if (!pending_.empty() && data_sink_)
        {
            data_sink_(pending_.data(), pending_.size());
            pending_.clear();
        }
        else if (!pending_.empty())
        {
            pending_.clear();
        }
    }

    state state_ = state::data;
    unsigned char option_ = 0;
    unsigned char command_ = 0;
    std::vector<unsigned char> payload_;
    std::vector<unsigned char> pending_;
    data_sink data_sink_;
    negotiation_sink negotiation_sink_;
    subnegotiation_sink subnegotiation_sink_;
    bool truncated_ = false;
};

} // namespace telnet
} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_TELNET_HPP
