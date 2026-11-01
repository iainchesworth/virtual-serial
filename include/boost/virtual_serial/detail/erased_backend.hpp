//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_ERASED_BACKEND_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_ERASED_BACKEND_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/type_erasure.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {
namespace detail {

/** A backend whose concrete type is erased.

    `erased_backend` is the runtime-selected member of the backend family.
    It implements exactly the same interface as the concrete backends, with
    each call forwarding through the #endpoint vtable, which means the
    generic #basic_serial_port template - and therefore the option
    forwarding, the timeout logic and the whole public API - is shared
    verbatim between the compile-time and the runtime paths.
*/
class erased_backend
{
public:
    using executor_type = boost::asio::any_io_executor;
    using read_completion =
        any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;

    explicit erased_backend(std::unique_ptr<endpoint> impl) noexcept
        : impl_(std::move(impl))
    {
    }

    erased_backend(erased_backend&&) noexcept = default;
    erased_backend& operator=(erased_backend&&) noexcept = default;
    erased_backend(const erased_backend&) = delete;
    erased_backend& operator=(const erased_backend&) = delete;

    template <class Backend>
    static std::shared_ptr<erased_backend> make(std::shared_ptr<Backend> backend)
    {
        return std::make_shared<erased_backend>(
            make_endpoint(std::move(backend)));
    }

    executor_type get_executor() const noexcept
    {
        if (!impl_)
            return executor_type();
        return impl_->get_executor();
    }

    bool is_open() const noexcept
    {
        return impl_ && impl_->is_open();
    }

    backend_kind backend() const noexcept
    {
        return impl_ ? impl_->kind() : backend_kind::mock;
    }

    void open(const std::string& target, boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return;
        }
        impl_->open(target, ec);
    }

    void close(boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return;
        }
        impl_->close(ec);
    }

    void cancel(boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return;
        }
        impl_->cancel(ec);
    }

    std::size_t read_some(
        const mutable_buffer_range& buffers, boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return 0;
        }
        return impl_->read_some(buffers, ec);
    }

    std::size_t write_some(
        const const_buffer_range& buffers, boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return 0;
        }
        return impl_->write_some(buffers, ec);
    }

    void async_read_some(
        mutable_buffer_range buffers, read_completion handler)
    {
        if (!impl_)
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }
        impl_->async_read_some(std::move(buffers), std::move(handler));
    }

    void async_write_some(
        const_buffer_range buffers, write_completion handler)
    {
        if (!impl_)
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }
        impl_->async_write_some(std::move(buffers), std::move(handler));
    }

    void apply_options(
        const serial_config& desired, boost::system::error_code& ec)
    {
        if (!impl_)
        {
            ec = error::not_connected;
            return;
        }
        impl_->apply_options(desired, ec);
    }

    serial_config options() const
    {
        return impl_ ? impl_->options() : serial_config();
    }

    std::intptr_t native_handle() noexcept
    {
        return impl_ ? impl_->native_handle() : 0;
    }

    /// Recovers the concrete backend; `nullptr` when the type does not
    /// match.  Requires RTTI.
    template <class Backend>
    Backend* backend_as() noexcept
    {
        return backend_cast<Backend>(impl_.get());
    }

    endpoint* get() noexcept
    {
        return impl_.get();
    }

    endpoint const* get() const noexcept
    {
        return impl_.get();
    }

private:
    std::unique_ptr<endpoint> impl_;
};

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_ERASED_BACKEND_HPP
