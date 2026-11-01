//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_TYPE_ERASURE_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_TYPE_ERASURE_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace boost {
namespace virtual_serial {
namespace detail {

/** The backend contract used by the type-erased front end.

    Every backend is an ordinary Asio stream implementation.  This
    interface is the smallest common denominator the front end needs, and
    it deliberately does *not* mention any concrete executor or buffer
    type, so the front end can hold a `std::unique_ptr<endpoint>` and still
    dispatch every operation to a backend whose type is known only at link
    time.

    The interface is internal.  Backends are used directly (zero overhead,
    compile-time selection) or through #any_serial_port (runtime
    selection); neither path requires users to derive from `endpoint`.
*/
class endpoint
{
public:
    using executor_type = boost::asio::any_io_executor;
    using read_completion =
        any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;

    virtual ~endpoint() = default;

    virtual executor_type get_executor() const noexcept = 0;
    virtual bool is_open() const noexcept = 0;
    virtual backend_kind kind() const noexcept = 0;

    virtual void open(const std::string& target, boost::system::error_code& ec) = 0;
    virtual void close(boost::system::error_code& ec) = 0;
    virtual void cancel(boost::system::error_code& ec) = 0;

    virtual std::size_t read_some(
        const mutable_buffer_range& buffers, boost::system::error_code& ec) = 0;
    virtual std::size_t write_some(
        const const_buffer_range& buffers, boost::system::error_code& ec) = 0;

    virtual void async_read_some(
        mutable_buffer_range buffers, read_completion handler) = 0;
    virtual void async_write_some(
        const_buffer_range buffers, write_completion handler) = 0;

    virtual void apply_options(
        const serial_config& desired, boost::system::error_code& ec) = 0;
    virtual serial_config options() const = 0;

    /// Platform handle, or zero when the backend has none.
    virtual std::intptr_t native_handle() noexcept = 0;

    /// Identity of the concrete backend, used by #backend_cast.
    virtual void* target_address() noexcept = 0;
    virtual const std::type_info& target_type() const noexcept = 0;
};

/** Adapts a concrete backend to the #endpoint interface.

    The adapter holds the backend by `shared_ptr` so that a test fixture
    can keep its typed handle alive while the front end owns the port.
*/
template <class Backend>
class backend_model final : public endpoint
{
public:
    explicit backend_model(std::shared_ptr<Backend> backend)
        : backend_(std::move(backend))
    {
    }

    executor_type get_executor() const noexcept override
    {
        return backend_->get_executor();
    }

    bool is_open() const noexcept override
    {
        return backend_->is_open();
    }

    backend_kind kind() const noexcept override
    {
        return backend_->backend();
    }

    void open(const std::string& target, boost::system::error_code& ec) override
    {
        backend_->open(target, ec);
    }

    void close(boost::system::error_code& ec) override
    {
        backend_->close(ec);
    }

    void cancel(boost::system::error_code& ec) override
    {
        backend_->cancel(ec);
    }

    std::size_t read_some(
        const mutable_buffer_range& buffers,
        boost::system::error_code& ec) override
    {
        return backend_->read_some(buffers, ec);
    }

    std::size_t write_some(
        const const_buffer_range& buffers,
        boost::system::error_code& ec) override
    {
        return backend_->write_some(buffers, ec);
    }

    void async_read_some(
        mutable_buffer_range buffers, read_completion handler) override
    {
        backend_->async_read_some(std::move(buffers), std::move(handler));
    }

    void async_write_some(
        const_buffer_range buffers, write_completion handler) override
    {
        backend_->async_write_some(std::move(buffers), std::move(handler));
    }

    void apply_options(
        const serial_config& desired, boost::system::error_code& ec) override
    {
        backend_->apply_options(desired, ec);
    }

    serial_config options() const override
    {
        return backend_->options();
    }

    std::intptr_t native_handle() noexcept override
    {
        return backend_->native_handle();
    }

    void* target_address() noexcept override
    {
        return backend_.get();
    }

    const std::type_info& target_type() const noexcept override
    {
        return typeid(Backend);
    }

private:
    std::shared_ptr<Backend> backend_;
};

/// Creates the type-erasing adapter for a concrete backend.
template <class Backend>
std::unique_ptr<endpoint> make_endpoint(std::shared_ptr<Backend> backend)
{
    return std::unique_ptr<endpoint>(
        new backend_model<Backend>(std::move(backend)));
}

/** Recovers the concrete backend from an erased #endpoint.

    Returns `nullptr` when the endpoint does not hold a `Backend`.  When
    RTTI is disabled this always returns `nullptr`; keep a typed
    `shared_ptr` instead in that configuration.
*/
template <class Backend>
Backend* backend_cast(endpoint* value) noexcept
{
#if BOOST_VIRTUAL_SERIAL_NO_RTTI
    (void)value;
    return nullptr;
#else
    if (value == nullptr || value->target_type() != typeid(Backend))
        return nullptr;
    return static_cast<Backend*>(value->target_address());
#endif
}

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_TYPE_ERASURE_HPP
