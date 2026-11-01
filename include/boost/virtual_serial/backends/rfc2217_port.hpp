//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BACKENDS_RFC2217_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BACKENDS_RFC2217_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>
#include <boost/virtual_serial/detail/read_queue.hpp>
#include <boost/virtual_serial/detail/telnet.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace boost {
namespace virtual_serial {

/** A serial port reached through an RFC 2217 (Telnet Com Port Control)
    server.

    This is the fully featured network backend.  It performs the Telnet
    option negotiation, sends the com port configuration commands
    (`SET-BAUDRATE`, `SET-DATASIZE`, `SET-PARITY`, `SET-STOPSIZE`,
    `SET-CONTROL`), decodes the server's acknowledgements and turns
    `NOTIFY-LINESTATE` notifications into the same #error values the mock
    and physical backends report.

    Wire behaviour:

    - outgoing data escapes every `0xFF` byte as `0xFF 0xFF` (Telnet IAC
      quoting);
    - incoming data is unescaped by the incremental decoder, which also
      handles commands split across TCP segments;
    - line errors (framing, parity, overrun, break) are queued as faults
      so the next read reports them before any subsequent data;
    - setter commands are only sent when the value changes, in the order
      recommended by the RFC.

    @par Example
    @code
    rfc2217_port port(io.get_executor());
    port.open("lab-host:7001", ec);
    port.apply_options(parse_uri("rfc2217://lab-host:7001?baud=115200"), ec);
    @endcode
*/
class rfc2217_port
{
public:
    using executor_type = boost::asio::any_io_executor;
    using protocol_type = boost::asio::ip::tcp;
    using socket_type = boost::asio::ip::tcp::socket;
    using lowest_layer_type = socket_type;
    using read_completion =
        detail::any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;
    using modem_state_handler = std::function<void(unsigned char)>;

    /// Size of the socket read buffer used by the background read loop.
    enum : std::size_t
    {
        read_chunk = 4096
    };

    explicit rfc2217_port(executor_type executor)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
        , queue_(executor_)
    {
        config_.backend = backend_kind::rfc2217;
        wire_decoder();
    }

    explicit rfc2217_port(
        executor_type executor, const serial_config& config)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
        , queue_(executor_)
        , config_(config)
    {
        config_.backend = backend_kind::rfc2217;
        wire_decoder();
    }

    rfc2217_port(rfc2217_port&&) = delete;
    rfc2217_port& operator=(rfc2217_port&&) = delete;
    rfc2217_port(const rfc2217_port&) = delete;
    rfc2217_port& operator=(const rfc2217_port&) = delete;

    // -- Asio stream interface -------------------------------------------

    executor_type get_executor() const noexcept
    {
        return executor_;
    }

    lowest_layer_type& lowest_layer() noexcept
    {
        return socket_;
    }

    lowest_layer_type const& lowest_layer() const noexcept
    {
        return socket_;
    }

    backend_kind backend() const noexcept
    {
        return backend_kind::rfc2217;
    }

    bool is_open() const noexcept
    {
        return socket_.is_open();
    }

    std::intptr_t native_handle() noexcept
    {
        return socket_.is_open() ? detail::to_intptr(socket_.native_handle()) : 0;
    }

    /// Connects, negotiates Telnet, starts the read loop and applies the
    /// stored configuration.
    void open(const std::string& target, boost::system::error_code& ec)
    {
        if (socket_.is_open())
        {
            ec = error::already_open;
            return;
        }

        const std::string endpoint_text =
            target.empty() ? config_.target : target;
        std::string host;
        std::string service;
        if (!detail::split_host_service(endpoint_text, host, service))
        {
            ec = error::bad_target;
            return;
        }

        const protocol_type::resolver::results_type results =
            resolver_.resolve(host, service, ec);
        if (ec)
            return;

        boost::asio::connect(socket_, results, ec);
        if (ec)
        {
            // A failed connect leaves the descriptor open; close it so the
            // port is visibly closed and can be retried.
            boost::system::error_code ignored;
            socket_.close(ignored);
            return;
        }

        config_.target = endpoint_text;
        boost::system::error_code ignored;
        socket_.set_option(protocol_type::no_delay(true), ignored);
        socket_.set_option(boost::asio::socket_base::keep_alive(true), ignored);
        queue_.clear_terminal();
        decoder_.reset();
        configured_ = false;
        will_sent_ = false;
        do_sent_ = false;
        suspended_ = false;

        send_negotiation(
            detail::telnet::command_will, detail::telnet::option_com_port, ec);
        if (ec)
            return;
        send_negotiation(
            detail::telnet::command_do, detail::telnet::option_com_port, ec);
        if (ec)
            return;

        apply_options(config_, ec);
    }

    void close(boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        boost::system::error_code ignored;
        socket_.close(ignored);
        queue_.cancel(boost::asio::error::operation_aborted);
        read_loop_active_ = false;
        ec.clear();
    }

    void cancel(boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        socket_.cancel(ec);
        queue_.cancel(boost::asio::error::operation_aborted);
    }

    std::size_t read_some(
        const detail::mutable_buffer_range& buffers,
        boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        if (queue_.available() > 0 || queue_.finished())
            return queue_.read(buffers, ec);

        // Synchronous callers need to drive the socket themselves; the
        // background read loop only runs while the io_context does.
        for (;;)
        {
            const std::size_t received =
                socket_.read_some(boost::asio::buffer(read_buffer_), ec);
            if (ec)
                return 0;
            decoder_.feed(read_buffer_.data(), received);
            if (queue_.available() > 0 || queue_.finished())
                return queue_.read(buffers, ec);
        }
    }

    template <class MutableBufferSequence>
    std::size_t read_some(
        const MutableBufferSequence& buffers, boost::system::error_code& ec)
    {
        return read_some(detail::mutable_buffer_range(buffers), ec);
    }

    std::size_t write_some(
        const detail::const_buffer_range& buffers,
        boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        std::vector<unsigned char> escaped;
        escaped.reserve(buffers.total_size() + 8);
        append_escaped_payload(buffers, escaped);
        const std::size_t user_bytes = buffers.total_size();
        boost::asio::write(socket_, boost::asio::buffer(escaped), ec);
        if (ec)
            return 0;
        return user_bytes;
    }

    template <class ConstBufferSequence>
    std::size_t write_some(
        const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        return write_some(detail::const_buffer_range(buffers), ec);
    }

    template <class MutableBufferSequence, class CompletionToken>
    auto async_read_some(
        const MutableBufferSequence& buffers, CompletionToken&& token)
    {
        return boost::asio::async_initiate<
            CompletionToken,
            void(boost::system::error_code, std::size_t)>(
            read_initiator{*this}, token, buffers);
    }

    template <class ConstBufferSequence, class CompletionToken>
    auto async_write_some(
        const ConstBufferSequence& buffers, CompletionToken&& token)
    {
        return boost::asio::async_initiate<
            CompletionToken,
            void(boost::system::error_code, std::size_t)>(
            write_initiator{*this}, token, buffers);
    }

    /// Sends the com port configuration commands that changed.
    ///
    /// This performs a short blocking write so that the configuration is
    /// on the wire before the next read; call it from setup code rather
    /// than from a completion handler.
    void apply_options(
        const serial_config& desired, boost::system::error_code& ec)
    {
        error failure = error::success;
        if (!detail::validate(desired, failure))
        {
            ec = failure;
            return;
        }

        serial_config candidate = desired;
        candidate.backend = backend_kind::rfc2217;

        if (!socket_.is_open())
        {
            config_ = candidate;
            ec.clear();
            return;
        }

        const bool send_all = !configured_;
        std::vector<std::vector<unsigned char> > commands;

        if (send_all || candidate.baud_rate != config_.baud_rate)
            commands.push_back(detail::telnet::make_baud_rate(candidate.baud_rate));
        if (send_all || candidate.data_bits != config_.data_bits)
            commands.push_back(detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_data_size,
                detail::telnet::data_size_index(candidate.data_bits)));
        if (send_all || candidate.parity != config_.parity)
            commands.push_back(detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_parity,
                detail::telnet::parity_index(candidate.parity)));
        if (send_all || candidate.stop_bits != config_.stop_bits)
            commands.push_back(detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_stop_size,
                detail::telnet::stop_size_index(candidate.stop_bits)));
        if (send_all || candidate.flow_control != config_.flow_control)
            commands.push_back(detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_control,
                detail::telnet::control_index(candidate.flow_control)));

        if (send_all)
        {
            // Ask for the line states that map onto UART errors:
            // framing (8) | parity (4) | overrun (2) | break (16).
            commands.push_back(detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_line_state_mask,
                static_cast<unsigned char>(
                    detail::telnet::line_state::framing
                    | detail::telnet::line_state::parity
                    | detail::telnet::line_state::overrun
                    | detail::telnet::line_state::break_detect)));
        }

        config_ = candidate;
        configured_ = true;

        for (std::size_t i = 0; i < commands.size(); ++i)
        {
            boost::asio::write(
                socket_, boost::asio::buffer(commands[i]), ec);
            if (ec)
                return;
        }
        ec.clear();
    }

    serial_config options() const
    {
        return config_;
    }

    /// Sends `BREAK` on (true) or off (false).
    void set_break(bool enabled, boost::system::error_code& ec)
    {
        send_command(
            detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::set_control,
                enabled ? detail::telnet::rfc2217::control_break_on
                        : detail::telnet::rfc2217::control_break_off),
            ec);
    }

    /// Purges one or both server buffers.
    ///
    /// `scope` is 1 (receive), 2 (transmit) or 3 (both), matching
    /// RFC 2217 PURGE-DATA.
    void purge(unsigned char scope, boost::system::error_code& ec)
    {
        send_command(
            detail::telnet::make_rfc2217(
                detail::telnet::rfc2217::purge_data, scope),
            ec);
    }

    /// True while the peer asked us to pause (FLOWCONTROL-SUSPEND).
    ///
    /// The state is surfaced rather than enforced: this client uses
    /// blocking configuration writes, so the application decides when to
    /// stop issuing writes.
    bool flow_suspended() const noexcept
    {
        return suspended_;
    }

    /// Last NOTIFY-LINESTATE value received from the server.
    unsigned char line_state() const noexcept
    {
        return line_state_;
    }

    /// Last NOTIFY-MODEMSTATE value received from the server.
    unsigned char modem_state() const noexcept
    {
        return modem_state_;
    }

    /// Invoked for every NOTIFY-MODEMSTATE notification.
    void set_modem_state_handler(modem_state_handler handler)
    {
        modem_state_handler_ = std::move(handler);
    }

    /// Text the server advertised in its SIGNATURE subnegotiation.
    const std::string& server_signature() const noexcept
    {
        return server_signature_;
    }

    /// Signature this client advertises when asked.
    void set_client_signature(std::string signature)
    {
        client_signature_ = std::move(signature);
    }

    socket_type& native_socket() noexcept
    {
        return socket_;
    }

private:
    struct read_initiator
    {
        rfc2217_port& self;

        using executor_type = boost::asio::any_io_executor;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class MutableBufferSequence>
        void operator()(
            Handler&& handler, const MutableBufferSequence& buffers) const
        {
            read_completion completion(
                std::forward<Handler>(handler), self.get_executor());
            if (!self.socket_.is_open())
            {
                completion(make_error_code(error::not_connected), 0);
                return;
            }
            self.ensure_read_loop();
            self.queue_.async_read(
                detail::mutable_buffer_range(buffers), std::move(completion));
        }
    };

    struct write_initiator
    {
        rfc2217_port& self;

        using executor_type = boost::asio::any_io_executor;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class ConstBufferSequence>
        void operator()(
            Handler&& handler, const ConstBufferSequence& buffers) const
        {
            write_completion completion(
                std::forward<Handler>(handler), self.get_executor());
            if (!self.socket_.is_open())
            {
                completion(make_error_code(error::not_connected), 0);
                return;
            }

            const detail::const_buffer_range range(buffers);
            const std::size_t user_bytes = range.total_size();
            std::shared_ptr<std::vector<unsigned char> > escaped =
                std::make_shared<std::vector<unsigned char> >();
            escaped->reserve(user_bytes + 8);
            self.append_escaped_payload(range, *escaped);

            boost::asio::async_write(
                self.socket_,
                boost::asio::buffer(*escaped),
                [escaped, completion, user_bytes](
                    boost::system::error_code error,
                    std::size_t) mutable {
                    completion(error, error ? 0 : user_bytes);
                });
        }
    };

    void wire_decoder()
    {
        decoder_.set_data_sink(
            [this](const unsigned char* data, std::size_t size) {
                queue_.push(data, size);
            });
        decoder_.set_negotiation_sink(
            [this](unsigned char command, unsigned char option) {
                handle_negotiation(command, option);
            });
        decoder_.set_subnegotiation_sink(
            [this](
                unsigned char option,
                const unsigned char* payload,
                std::size_t size) {
                handle_subnegotiation(option, payload, size);
            });
    }

    /// Starts the background read loop if it is not already running.
    ///
    /// The loop is started lazily by the first asynchronous read so that a
    /// purely synchronous user can drive the socket directly without two
    /// readers competing for the same bytes.
    void ensure_read_loop()
    {
        if (!socket_.is_open() || read_loop_active_)
            return;
        read_loop_active_ = true;
        socket_.async_read_some(
            boost::asio::buffer(read_buffer_),
            [this](boost::system::error_code error, std::size_t received) {
                read_loop_active_ = false;
                if (error)
                {
                    handle_transport_error(error);
                    return;
                }
                decoder_.feed(read_buffer_.data(), received);
                ensure_read_loop();
            });
    }

    void handle_transport_error(const boost::system::error_code& error)
    {
        if (error == boost::asio::error::operation_aborted)
            return;
        boost::system::error_code mapped = error;
        if (error == boost::asio::error::connection_reset
            || error == boost::asio::error::connection_aborted
            || error == boost::asio::error::broken_pipe)
            mapped = make_error_code(error::not_connected);
        queue_.set_error(mapped);
        configured_ = false;
        boost::system::error_code ignored;
        socket_.close(ignored);
    }

    void send_negotiation(
        unsigned char command,
        unsigned char option,
        boost::system::error_code& ec)
    {
        const std::vector<unsigned char> bytes =
            detail::telnet::encode_negotiation(command, option);
        boost::asio::write(socket_, boost::asio::buffer(bytes), ec);
        if (ec)
            return;
        if (command == detail::telnet::command_will)
            will_sent_ = true;
        else if (command == detail::telnet::command_do)
            do_sent_ = true;
    }

    void send_command(
        const std::vector<unsigned char>& bytes,
        boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        boost::asio::write(socket_, boost::asio::buffer(bytes), ec);
    }

    void handle_negotiation(unsigned char command, unsigned char option)
    {
        boost::system::error_code ignored;
        if (option != detail::telnet::option_com_port)
        {
            // Refuse every other Telnet option: this is a raw byte pipe.
            if (command == detail::telnet::command_do)
                send_negotiation(
                    detail::telnet::command_wont, option, ignored);
            else if (command == detail::telnet::command_will)
                send_negotiation(
                    detail::telnet::command_dont, option, ignored);
            return;
        }

        if (command == detail::telnet::command_do && !will_sent_)
            send_negotiation(detail::telnet::command_will, option, ignored);
        else if (command == detail::telnet::command_will && !do_sent_)
            send_negotiation(detail::telnet::command_do, option, ignored);
        else if (command == detail::telnet::command_dont)
        {
            // The peer refused the option: forget both sides of the
            // negotiation so the next offer is answered again.
            will_sent_ = false;
            do_sent_ = false;
            send_negotiation(detail::telnet::command_wont, option, ignored);
        }
    }

    void handle_subnegotiation(
        unsigned char option, const unsigned char* payload, std::size_t size)
    {
        if (option != detail::telnet::option_com_port || size == 0)
            return;

        const unsigned char opcode = payload[0];
        const unsigned char* body = payload + 1;
        const std::size_t body_size = size - 1;
        namespace tn = detail::telnet;

        switch (opcode)
        {
        case tn::rfc2217::signature:
            server_signature_.assign(
                reinterpret_cast<const char*>(body), body_size);
            if (body_size == 0 && socket_.is_open())
            {
                const std::vector<unsigned char> reply =
                    tn::encode_rfc2217(
                        tn::rfc2217::signature,
                        reinterpret_cast<const unsigned char*>(
                            client_signature_.data()),
                        client_signature_.size());
                boost::system::error_code ignored;
                boost::asio::write(
                    socket_, boost::asio::buffer(reply), ignored);
            }
            break;

        case tn::rfc2217::set_baud_rate:
        case tn::rfc2217::set_baud_rate + tn::rfc2217::server_offset:
            if (body_size == 4)
            {
                const unsigned int baud =
                    (static_cast<unsigned int>(body[0]) << 24)
                    | (static_cast<unsigned int>(body[1]) << 16)
                    | (static_cast<unsigned int>(body[2]) << 8)
                    | static_cast<unsigned int>(body[3]);
                if (baud != 0)
                    config_.baud_rate = baud;
            }
            break;

        case tn::rfc2217::set_data_size:
        case tn::rfc2217::set_data_size + tn::rfc2217::server_offset:
            if (body_size == 1 && body[0] >= 5 && body[0] <= 8)
                config_.data_bits = body[0];
            break;

        case tn::rfc2217::set_parity:
        case tn::rfc2217::set_parity + tn::rfc2217::server_offset:
            // Indices 1 through 5 are defined; anything else is ignored
            // rather than silently mapped onto a different parity.
            if (body_size == 1 && body[0] >= 1 && body[0] <= 5)
                config_.parity = tn::parity_from_index(body[0]);
            break;

        case tn::rfc2217::set_stop_size:
        case tn::rfc2217::set_stop_size + tn::rfc2217::server_offset:
            if (body_size == 1 && body[0] >= 1 && body[0] <= 3)
                config_.stop_bits = tn::stop_size_from_index(body[0]);
            break;

        case tn::rfc2217::set_control:
        case tn::rfc2217::set_control + tn::rfc2217::server_offset:
            if (body_size == 1)
            {
                if (body[0] == tn::rfc2217::control_no_flow)
                    config_.flow_control = flow_control_mode::none;
                else if (body[0] == tn::rfc2217::control_xon_xoff)
                    config_.flow_control = flow_control_mode::software;
                else if (body[0] == tn::rfc2217::control_hardware)
                    config_.flow_control = flow_control_mode::hardware;
            }
            break;

        case tn::rfc2217::notify_line_state:
        case tn::rfc2217::notify_line_state + tn::rfc2217::server_offset:
            if (body_size == 1)
                handle_line_state(body[0]);
            break;

        case tn::rfc2217::notify_modem_state:
        case tn::rfc2217::notify_modem_state + tn::rfc2217::server_offset:
            if (body_size == 1)
            {
                modem_state_ = body[0];
                if (modem_state_handler_)
                    modem_state_handler_(modem_state_);
            }
            break;

        case tn::rfc2217::flow_control_suspend:
            suspended_ = true;
            break;

        case tn::rfc2217::flow_control_resume:
            suspended_ = false;
            break;

        default:
            break;
        }
    }

    void handle_line_state(unsigned char state)
    {
        namespace tn = detail::telnet;
        line_state_ = state;
        if (state & tn::line_state::framing)
            queue_.push_fault(make_error_code(error::framing_error));
        if (state & tn::line_state::parity)
            queue_.push_fault(make_error_code(error::parity_error));
        if (state & tn::line_state::overrun)
            queue_.push_fault(make_error_code(error::overrun));
        if (state & tn::line_state::break_detect)
            queue_.push_fault(make_error_code(error::break_detected));
    }

    static void append_escaped_payload(
        const detail::const_buffer_range& buffers,
        std::vector<unsigned char>& out)
    {
        for (std::size_t i = 0; i < buffers.size(); ++i)
        {
            const unsigned char* data =
                static_cast<const unsigned char*>(buffers[i].data());
            detail::telnet::append_escaped(out, data, buffers[i].size());
        }
    }

    executor_type executor_;
    socket_type socket_;
    protocol_type::resolver resolver_;
    detail::read_queue queue_;
    detail::telnet::decoder decoder_;
    std::array<unsigned char, read_chunk> read_buffer_{};
    serial_config config_;
    std::string server_signature_;
    std::string client_signature_ = "Boost.VirtualSerial/1.0 (RFC 2217)";
    modem_state_handler modem_state_handler_;
    unsigned char line_state_ = 0;
    unsigned char modem_state_ = 0;
    bool read_loop_active_ = false;
    bool configured_ = false;
    bool will_sent_ = false;
    bool do_sent_ = false;
    bool suspended_ = false;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BACKENDS_RFC2217_PORT_HPP
