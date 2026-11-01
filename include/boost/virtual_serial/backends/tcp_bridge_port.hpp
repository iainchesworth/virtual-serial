//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BACKENDS_TCP_BRIDGE_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BACKENDS_TCP_BRIDGE_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {

/** A serial port bridged over a raw TCP byte stream.

    Many serial servers expose the device as plain TCP: bytes written to
    the socket appear on the UART and vice versa, with no protocol on top.
    This backend adapts such a socket to the serial port interface, so the
    same application code can talk to a device on the bench and to the
    same device over the network.

    Serial settings (baud rate, parity, ...) cannot be conveyed over a raw
    byte stream.  They are validated and stored - so that configuration
    code is identical across backends - but only the RFC 2217 backend can
    actually apply them on the remote device.

    @par Example
    @code
    tcp_bridge_port port(io.get_executor());
    port.open("192.168.1.50:4001", ec);
    @endcode
*/
class tcp_bridge_port
{
public:
    using executor_type = boost::asio::any_io_executor;
    using protocol_type = boost::asio::ip::tcp;
    using socket_type = boost::asio::ip::tcp::socket;
    using lowest_layer_type = socket_type;
    using read_completion =
        detail::any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;

    explicit tcp_bridge_port(executor_type executor)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
    {
        config_.backend = backend_kind::tcp;
    }

    explicit tcp_bridge_port(
        executor_type executor, const serial_config& config)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
        , config_(config)
    {
        config_.backend = backend_kind::tcp;
    }

    tcp_bridge_port(tcp_bridge_port&&) noexcept = default;
    tcp_bridge_port& operator=(tcp_bridge_port&&) noexcept = default;
    tcp_bridge_port(const tcp_bridge_port&) = delete;
    tcp_bridge_port& operator=(const tcp_bridge_port&) = delete;

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
        return backend_kind::tcp;
    }

    bool is_open() const noexcept
    {
        return socket_.is_open();
    }

    std::intptr_t native_handle() noexcept
    {
        return socket_.is_open() ? detail::to_intptr(socket_.native_handle()) : 0;
    }

    /// Resolves and connects to `host:service`.
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
    }

    void close(boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        socket_.close(ec);
    }

    void cancel(boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        socket_.cancel(ec);
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
        return socket_.read_some(buffers, ec);
    }

    template <class MutableBufferSequence>
    std::size_t read_some(
        const MutableBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return socket_.read_some(buffers, ec);
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
        return socket_.write_some(buffers, ec);
    }

    template <class ConstBufferSequence>
    std::size_t write_some(
        const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!socket_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return socket_.write_some(buffers, ec);
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

    /// Stores the serial settings.  A raw TCP bridge cannot apply them;
    /// they are kept so configuration code is uniform.
    void apply_options(
        const serial_config& desired, boost::system::error_code& ec)
    {
        error failure = error::success;
        if (!detail::validate(desired, failure))
        {
            ec = failure;
            return;
        }
        config_ = desired;
        config_.backend = backend_kind::tcp;
        ec.clear();
    }

    serial_config options() const
    {
        return config_;
    }

    /// The underlying socket, for keep-alive or TLS layering experiments.
    socket_type& native_socket() noexcept
    {
        return socket_;
    }

private:
    struct read_initiator
    {
        tcp_bridge_port& self;

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
            self.socket_.async_read_some(buffers, std::move(completion));
        }
    };

    struct write_initiator
    {
        tcp_bridge_port& self;

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
            self.socket_.async_write_some(buffers, std::move(completion));
        }
    };

    executor_type executor_;
    socket_type socket_;
    protocol_type::resolver resolver_;
    serial_config config_;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BACKENDS_TCP_BRIDGE_PORT_HPP
