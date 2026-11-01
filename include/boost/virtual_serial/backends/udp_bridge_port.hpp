//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BACKENDS_UDP_BRIDGE_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BACKENDS_UDP_BRIDGE_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {

/** A serial port bridged over connected UDP datagrams.

    Some embedded gateways expose a serial port as a UDP endpoint.  This
    backend connects a datagram socket to the peer and presents it as a
    byte stream: every `write_some` becomes one datagram and every read
    returns the payload of one datagram.

    Because datagrams preserve boundaries, a read whose buffer is smaller
    than the incoming datagram completes with
    `boost::asio::error::message_size` and the datagram is truncated,
    exactly as a UDP socket behaves.

    @par Example
    @code
    udp_bridge_port port(io.get_executor());
    port.open("192.168.1.50:4002", ec);
    @endcode
*/
class udp_bridge_port
{
public:
    using executor_type = boost::asio::any_io_executor;
    using protocol_type = boost::asio::ip::udp;
    using socket_type = boost::asio::ip::udp::socket;
    using endpoint_type = boost::asio::ip::udp::endpoint;
    using lowest_layer_type = socket_type;
    using read_completion =
        detail::any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;

    explicit udp_bridge_port(executor_type executor)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
    {
        config_.backend = backend_kind::udp;
    }

    explicit udp_bridge_port(
        executor_type executor, const serial_config& config)
        : executor_(std::move(executor))
        , socket_(executor_)
        , resolver_(executor_)
        , config_(config)
    {
        config_.backend = backend_kind::udp;
    }

    udp_bridge_port(udp_bridge_port&&) noexcept = default;
    udp_bridge_port& operator=(udp_bridge_port&&) noexcept = default;
    udp_bridge_port(const udp_bridge_port&) = delete;
    udp_bridge_port& operator=(const udp_bridge_port&) = delete;

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
        return backend_kind::udp;
    }

    bool is_open() const noexcept
    {
        return socket_.is_open();
    }

    std::intptr_t native_handle() noexcept
    {
        return socket_.is_open() ? detail::to_intptr(socket_.native_handle()) : 0;
    }

    /// Resolves `host:service` and connects the datagram socket to it.
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
        if (results.begin() == results.end())
        {
            ec = error::bad_target;
            return;
        }

        remote_ = results.begin()->endpoint();
        socket_.open(remote_.protocol(), ec);
        if (ec)
            return;
        socket_.connect(remote_, ec);
        if (ec)
        {
            boost::system::error_code ignored;
            socket_.close(ignored);
            return;
        }

        config_.target = endpoint_text;
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
        return socket_.receive(buffers, 0, ec);
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
        return socket_.receive(buffers, 0, ec);
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
        return socket_.send(buffers, 0, ec);
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
        return socket_.send(buffers, 0, ec);
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
        config_.backend = backend_kind::udp;
        ec.clear();
    }

    serial_config options() const
    {
        return config_;
    }

    socket_type& native_socket() noexcept
    {
        return socket_;
    }

    const endpoint_type& remote_endpoint() const noexcept
    {
        return remote_;
    }

private:
    struct read_initiator
    {
        udp_bridge_port& self;

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
            self.socket_.async_receive(buffers, std::move(completion));
        }
    };

    struct write_initiator
    {
        udp_bridge_port& self;

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
            self.socket_.async_send(buffers, std::move(completion));
        }
    };

    executor_type executor_;
    socket_type socket_;
    protocol_type::resolver resolver_;
    endpoint_type remote_;
    serial_config config_;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BACKENDS_UDP_BRIDGE_PORT_HPP
