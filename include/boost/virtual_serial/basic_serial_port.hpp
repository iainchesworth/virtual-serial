//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BASIC_SERIAL_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BASIC_SERIAL_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/option.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/erased_backend.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace boost {
namespace virtual_serial {

/** A serial port over a selectable backend.

    `basic_serial_port<Backend>` is the single front end used for both
    compile-time and runtime backend selection:

    - `basic_serial_port<mock_port>`, `basic_serial_port<physical_port>`,
      `basic_serial_port<rfc2217_port>`, `basic_serial_port<tcp_bridge_port>`
      and `basic_serial_port<udp_bridge_port>` resolve every call
      statically, giving inlining and zero indirection;
    - `basic_serial_port<detail::erased_backend>` - spelled
      #any_serial_port - adds one vtable dispatch per operation and lets
      the backend be chosen at run time from a URI.

    Both forms expose exactly the same member functions, including the
    Asio `serial_port_base` option objects, the front-end read and write
    timeouts and the `read_some` / `write_some` / `async_read_some` /
    `async_write_some` quartet.  Code written against
    `basic_serial_port<B>` therefore compiles unchanged against
    `any_serial_port`.

    @par Executor binding
    The port is constructed from an executor and forwards that executor to
    its backend.  Completions are dispatched through each handler's
    associated executor, which defaults to `get_executor()`.  A handler
    bound to a `boost::asio::strand` stays on the strand; a handler bound
    to `boost::asio::use_future` still produces a future; nothing about
    the `io_context` is bypassed or re-parented.

    @par Thread Safety
    Distinct objects: safe.  Shared objects: unsafe.  As with any Asio
    stream, operations on the same object must be serialised - typically
    with a strand, or by keeping the object on one thread.
*/
template <class Backend>
class basic_serial_port
{
private:
    struct timer_state
    {
        timer_state(boost::asio::any_io_executor executor)
            : timer(std::move(executor))
        {
        }

        boost::asio::steady_timer timer;
        typename Backend::read_completion handler;
        bool completed = false;
        bool timed_out = false;
    };

public:
    using backend_type = Backend;
    using executor_type = typename Backend::executor_type;
    using read_completion = typename Backend::read_completion;
    using write_completion = typename Backend::write_completion;
    using native_handle_type = std::intptr_t;

    /// Adopts an existing backend.
    basic_serial_port(executor_type executor, std::shared_ptr<Backend> backend)
        : executor_(std::move(executor))
        , backend_(std::move(backend))
    {
        if (backend_)
            config_ = backend_->options();
    }

    /// Constructs the backend in place from the executor.
    template <
        class B = Backend,
        typename std::enable_if<
            std::is_constructible<B, executor_type>::value, int>::type = 0>
    explicit basic_serial_port(executor_type executor)
        : executor_(std::move(executor))
        , backend_(std::make_shared<Backend>(executor_))
    {
        config_ = backend_->options();
    }

    /// Erases a concrete backend into the runtime-selected front end.
    template <
        class OtherBackend,
        typename std::enable_if<
            std::is_same<Backend, detail::erased_backend>::value
                && !std::is_same<OtherBackend, detail::erased_backend>::value,
            int>::type = 0>
    basic_serial_port(
        executor_type executor, std::shared_ptr<OtherBackend> backend)
        : executor_(std::move(executor))
        , backend_(detail::erased_backend::template make<OtherBackend>(
              std::move(backend)))
    {
        config_ = backend_->options();
    }

    basic_serial_port(basic_serial_port&&) noexcept = default;
    basic_serial_port& operator=(basic_serial_port&&) noexcept = default;
    basic_serial_port(const basic_serial_port&) = delete;
    basic_serial_port& operator=(const basic_serial_port&) = delete;

    /// Creates the runtime-selected front end with no backend attached.
    template <
        class B = Backend,
        typename std::enable_if<
            std::is_same<B, detail::erased_backend>::value, int>::type = 0>
    explicit basic_serial_port(executor_type executor)
        : executor_(std::move(executor))
    {
    }

    // -- Observers --------------------------------------------------------

    executor_type get_executor() const noexcept
    {
        return backend_ ? backend_->get_executor() : executor_;
    }

    bool is_open() const noexcept
    {
        return backend_ && backend_->is_open();
    }

    backend_kind backend() const noexcept
    {
        return backend_ ? backend_->backend() : backend_kind::mock;
    }

    std::intptr_t native_handle() noexcept
    {
        return backend_ ? backend_->native_handle() : 0;
    }

    /// The backend, for backend specific extensions.
    std::shared_ptr<Backend> const& native_backend() const noexcept
    {
        return backend_;
    }

    Backend& native() noexcept
    {
        return *backend_;
    }

    /// The settings currently in force.
    serial_config config() const
    {
        return backend_ ? backend_->options() : config_;
    }

    // -- Lifecycle --------------------------------------------------------

    /// Opens the target stored in the configuration.
    void open()
    {
        open(config_.target);
    }

    void open(const std::string& target)
    {
        boost::system::error_code ec;
        open(target, ec);
        detail::throw_error(ec);
    }

    void open(const std::string& target, boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return;
        }
        if (backend_->is_open())
        {
            ec = error::already_open;
            return;
        }

        backend_->open(target, ec);
        if (ec)
            return;

        backend_->apply_options(config_, ec);
        if (ec)
            return;

        config_ = backend_->options();
        ec.clear();
    }

    void close()
    {
        boost::system::error_code ec;
        close(ec);
        detail::throw_error(ec);
    }

    void close(boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return;
        }
        backend_->close(ec);
    }

    void cancel()
    {
        boost::system::error_code ec;
        cancel(ec);
        detail::throw_error(ec);
    }

    void cancel(boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return;
        }
        backend_->cancel(ec);
    }

    // -- Synchronous I/O --------------------------------------------------

    /// Reads at least one byte, or fails with `would_block` when the
    /// backend cannot produce one without blocking.
    template <class MutableBufferSequence>
    std::size_t read_some(
        const MutableBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return 0;
        }
        const detail::mutable_buffer_range range(buffers);
        return backend_->read_some(range, ec);
    }

    template <class MutableBufferSequence>
    std::size_t read_some(const MutableBufferSequence& buffers)
    {
        boost::system::error_code ec;
        const std::size_t transferred = read_some(buffers, ec);
        detail::throw_error(ec);
        return transferred;
    }

    template <class ConstBufferSequence>
    std::size_t write_some(
        const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return 0;
        }
        const detail::const_buffer_range range(buffers);
        return backend_->write_some(range, ec);
    }

    template <class ConstBufferSequence>
    std::size_t write_some(const ConstBufferSequence& buffers)
    {
        boost::system::error_code ec;
        const std::size_t transferred = write_some(buffers, ec);
        detail::throw_error(ec);
        return transferred;
    }

    // -- Asynchronous I/O -------------------------------------------------

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

    // -- Options ----------------------------------------------------------

    /// Applies an option object.

    /// The five `boost::asio::serial_port_base` options are translated
    /// into #serial_config and forwarded to the backend.  #read_timeout
    /// and #write_timeout are enforced by the front end.  Any other type
    /// reports #error::unsupported_option at run time instead of failing
    /// to compile.
    template <class Option>
    void set_option(const Option& option, boost::system::error_code& ec)
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return;
        }
        serial_config candidate = backend_->options();
        detail::apply_option(option, candidate, ec);
        if (ec)
            return;

        backend_->apply_options(candidate, ec);
        if (ec)
            return;

        config_ = candidate;
        ec.clear();
    }

    template <class Option>
    void set_option(const Option& option)
    {
        boost::system::error_code ec;
        set_option(option, ec);
        detail::throw_error(ec);
    }

    /// Reads an option back into `option`.
    template <class Option>
    void get_option(Option& option, boost::system::error_code& ec) const
    {
        if (!backend_)
        {
            ec = error::not_connected;
            return;
        }
        detail::extract_option(backend_->options(), option, ec);
    }

    template <class Option>
    void get_option(Option& option) const
    {
        boost::system::error_code ec;
        get_option(option, ec);
        detail::throw_error(ec);
    }

private:
    struct read_initiator
    {
        basic_serial_port& self;

        using executor_type = typename basic_serial_port::executor_type;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class MutableBufferSequence>
        void operator()(
            Handler&& handler, const MutableBufferSequence& buffers) const
        {
            self.start_read(
                detail::mutable_buffer_range(buffers),
                read_completion(
                    std::forward<Handler>(handler), self.get_executor()));
        }
    };

    struct write_initiator
    {
        basic_serial_port& self;

        using executor_type = typename basic_serial_port::executor_type;

        executor_type get_executor() const noexcept
        {
            return self.get_executor();
        }

        template <class Handler, class ConstBufferSequence>
        void operator()(
            Handler&& handler, const ConstBufferSequence& buffers) const
        {
            self.start_write(
                detail::const_buffer_range(buffers),
                write_completion(
                    std::forward<Handler>(handler), self.get_executor()));
        }
    };

    void start_read(
        detail::mutable_buffer_range buffers, read_completion handler)
    {
        if (!backend_ || !backend_->is_open())
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }

        if (config_.read_timeout.count() <= 0)
        {
            backend_->async_read_some(std::move(buffers), std::move(handler));
            return;
        }

        std::shared_ptr<timer_state> state =
            std::make_shared<timer_state>(get_executor());
        state->handler = std::move(handler);

        backend_->async_read_some(
            std::move(buffers),
            read_completion(
                [state](
                    boost::system::error_code ec, std::size_t transferred) mutable {
                    if (state->completed)
                        return;
                    state->completed = true;
                    state->timer.cancel();
                    read_completion handler_out = std::move(state->handler);
                    if (state->timed_out)
                    {
                        handler_out(make_error_code(error::timeout), 0);
                        return;
                    }
                    handler_out(ec, transferred);
                },
                get_executor()));

        state->timer.expires_after(config_.read_timeout);
        state->timer.async_wait(
            [this, state](boost::system::error_code ec) {
                if (ec || state->completed)
                    return;
                state->timed_out = true;
                boost::system::error_code ignored;
                this->cancel(ignored);
            });
    }

    void start_write(
        detail::const_buffer_range buffers, write_completion handler)
    {
        if (!backend_ || !backend_->is_open())
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }

        if (config_.write_timeout.count() <= 0)
        {
            backend_->async_write_some(std::move(buffers), std::move(handler));
            return;
        }

        std::shared_ptr<timer_state> state =
            std::make_shared<timer_state>(get_executor());
        state->handler = std::move(handler);

        backend_->async_write_some(
            std::move(buffers),
            write_completion(
                [state](
                    boost::system::error_code ec, std::size_t transferred) mutable {
                    if (state->completed)
                        return;
                    state->completed = true;
                    state->timer.cancel();
                    write_completion handler_out = std::move(state->handler);
                    if (state->timed_out)
                    {
                        handler_out(make_error_code(error::timeout), 0);
                        return;
                    }
                    handler_out(ec, transferred);
                },
                get_executor()));

        state->timer.expires_after(config_.write_timeout);
        state->timer.async_wait(
            [this, state](boost::system::error_code ec) {
                if (ec || state->completed)
                    return;
                state->timed_out = true;
                boost::system::error_code ignored;
                this->cancel(ignored);
            });
    }

    executor_type executor_;
    std::shared_ptr<Backend> backend_;
    serial_config config_;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BASIC_SERIAL_PORT_HPP
