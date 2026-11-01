//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BACKENDS_PHYSICAL_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BACKENDS_PHYSICAL_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {

/** A real operating system serial port.

    This backend owns a `boost::asio::serial_port` and forwards every
    operation to it, so the timing, flow control and error behaviour are
    exactly those of Asio.  Serial options are translated from
    #serial_config into the matching `boost::asio::serial_port_base`
    objects.

    Used directly it is a compile-time selected backend with no
    indirection; used through #any_serial_port it is one of the runtime
    choices.
*/
class physical_port
{
public:
    using executor_type = boost::asio::any_io_executor;
    using lowest_layer_type = boost::asio::serial_port;
    using read_completion =
        detail::any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;

    /// Creates a closed port.
    explicit physical_port(executor_type executor)
        : executor_(std::move(executor))
        , port_(executor_)
    {
        config_.backend = backend_kind::physical;
    }

    /// Creates a closed port that remembers `config`.
    explicit physical_port(
        executor_type executor, const serial_config& config)
        : executor_(std::move(executor))
        , port_(executor_)
        , config_(config)
    {
        config_.backend = backend_kind::physical;
    }

    physical_port(physical_port&&) noexcept = default;
    physical_port& operator=(physical_port&&) noexcept = default;
    physical_port(const physical_port&) = delete;
    physical_port& operator=(const physical_port&) = delete;

    // -- Asio stream interface -------------------------------------------

    executor_type get_executor() const noexcept
    {
        return executor_;
    }

    lowest_layer_type& lowest_layer() noexcept
    {
        return port_;
    }

    lowest_layer_type const& lowest_layer() const noexcept
    {
        return port_;
    }

    backend_kind backend() const noexcept
    {
        return backend_kind::physical;
    }

    bool is_open() const noexcept
    {
        return port_.is_open();
    }

    std::intptr_t native_handle() noexcept
    {
        return port_.is_open()
                   ? detail::to_intptr(port_.native_handle())
                   : 0;
    }

    /// Opens the device and applies the stored configuration.
    void open(const std::string& target, boost::system::error_code& ec)
    {
        if (port_.is_open())
        {
            ec = error::already_open;
            return;
        }
        port_.open(target, ec);
        if (ec)
            return;

        config_.target = target;
        apply_options(config_, ec);
        if (ec)
        {
            boost::system::error_code ignored;
            port_.close(ignored);
        }
    }

    void close(boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        port_.close(ec);
    }

    void cancel(boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return;
        }
        port_.cancel(ec);
    }

    std::size_t read_some(
        const detail::mutable_buffer_range& buffers,
        boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return port_.read_some(buffers, ec);
    }

    template <class MutableBufferSequence>
    std::size_t read_some(
        const MutableBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return port_.read_some(buffers, ec);
    }

    std::size_t write_some(
        const detail::const_buffer_range& buffers,
        boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return port_.write_some(buffers, ec);
    }

    template <class ConstBufferSequence>
    std::size_t write_some(
        const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!port_.is_open())
        {
            ec = error::not_connected;
            return 0;
        }
        return port_.write_some(buffers, ec);
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

    /// Applies serial settings to the device.
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
        candidate.backend = backend_kind::physical;

        if (!port_.is_open())
        {
            config_ = candidate;
            ec.clear();
            return;
        }

        using boost::asio::serial_port_base;

        port_.set_option(serial_port_base::baud_rate(candidate.baud_rate), ec);
        if (ec)
            return;
        port_.set_option(
            serial_port_base::character_size(candidate.data_bits), ec);
        if (ec)
            return;
        port_.set_option(
            serial_port_base::parity(to_asio_parity(candidate.parity)), ec);
        if (ec)
            return;
        port_.set_option(
            serial_port_base::stop_bits(to_asio_stop_bits(candidate.stop_bits)),
            ec);
        if (ec)
            return;
        port_.set_option(
            serial_port_base::flow_control(
                to_asio_flow_control(candidate.flow_control)),
            ec);
        if (ec)
            return;

        config_ = candidate;
        ec.clear();
    }

    serial_config options() const
    {
        return config_;
    }

    /// The underlying Asio object, for platform specific extensions.
    lowest_layer_type& native_port() noexcept
    {
        return port_;
    }

private:
    static boost::asio::serial_port_base::parity::type to_asio_parity(
        parity_mode value) noexcept
    {
        using parity = boost::asio::serial_port_base::parity;
        switch (value)
        {
        case parity_mode::none:
            return parity::none;
        case parity_mode::odd:
            return parity::odd;
        case parity_mode::even:
            return parity::even;
        case parity_mode::mark:
        case parity_mode::space:
            // Asio does not model mark/space parity.  The nearest safe
            // setting is no parity; validate() and the front end surface
            // unsupported options instead of silently changing framing.
            return parity::none;
        }
        return parity::none;
    }

    static boost::asio::serial_port_base::stop_bits::type to_asio_stop_bits(
        stop_bits_mode value) noexcept
    {
        using stop_bits = boost::asio::serial_port_base::stop_bits;
        switch (value)
        {
        case stop_bits_mode::one:
            return stop_bits::one;
        case stop_bits_mode::one_point_five:
            return stop_bits::onepointfive;
        case stop_bits_mode::two:
            return stop_bits::two;
        }
        return stop_bits::one;
    }

    static boost::asio::serial_port_base::flow_control::type
    to_asio_flow_control(flow_control_mode value) noexcept
    {
        using flow_control = boost::asio::serial_port_base::flow_control;
        switch (value)
        {
        case flow_control_mode::none:
            return flow_control::none;
        case flow_control_mode::software:
            return flow_control::software;
        case flow_control_mode::hardware:
            return flow_control::hardware;
        }
        return flow_control::none;
    }

    struct read_initiator
    {
        physical_port& self;

        using executor_type = boost::asio::any_io_executor;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class MutableBufferSequence>
        void operator()(
            Handler&& handler, const MutableBufferSequence& buffers) const
        {
            if (!self.port_.is_open())
            {
                read_completion(
                    std::forward<Handler>(handler), self.get_executor())(
                    make_error_code(error::not_connected), 0);
                return;
            }
            self.port_.async_read_some(
                buffers,
                read_completion(
                    std::forward<Handler>(handler), self.get_executor()));
        }
    };

    struct write_initiator
    {
        physical_port& self;

        using executor_type = boost::asio::any_io_executor;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class ConstBufferSequence>
        void operator()(
            Handler&& handler, const ConstBufferSequence& buffers) const
        {
            if (!self.port_.is_open())
            {
                write_completion(
                    std::forward<Handler>(handler), self.get_executor())(
                    make_error_code(error::not_connected), 0);
                return;
            }
            self.port_.async_write_some(
                buffers,
                write_completion(
                    std::forward<Handler>(handler), self.get_executor()));
        }
    };

    executor_type executor_;
    lowest_layer_type port_;
    serial_config config_;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BACKENDS_PHYSICAL_PORT_HPP
