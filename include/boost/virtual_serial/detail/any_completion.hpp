//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_ANY_COMPLETION_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_ANY_COMPLETION_HPP

#include <boost/virtual_serial/config.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/associated_allocator.hpp>
#include <boost/asio/associated_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/system/error_code.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace boost {
namespace virtual_serial {
namespace detail {

/** A type-erased, executor-aware completion handler.

    Type erasure of an I/O object must not sever the link between a
    completion handler and the executor it is bound to, otherwise a handler
    belonging to a `boost::asio::strand` or a custom executor would run on
    the wrong context and the `io_context` could stop before the handler is
    invoked.

    `any_completion` captures the concrete handler and, alongside it, the
    concrete type of the handler's associated executor.  Both live in the
    same erased box, so completion goes through the handler's own executor.
    The allocator associated with the handler is used to allocate that box.

    The executor is *not* converted to `boost::asio::any_io_executor`.
    Some executors cannot be stored there - `boost::asio::use_future`'s
    internal promise executor among them - and converting would either fail
    to compile or silently re-parent the completion.  Erasing the executor
    call itself keeps every executor usable, including ones written after
    this library.

    Delivery always uses `boost::asio::post`.  Using `post` rather than
    `dispatch` guarantees the Asio requirement that a completion handler is
    never invoked before its initiating function returns, even when a
    backend can satisfy the operation immediately, and it registers
    outstanding work so `io_context::run()` does not return early.
*/
template <class... Args>
class any_completion
{
public:
    using signature = void(Args...);

    any_completion() noexcept = default;

    /// Erases `handler`, using `fallback` when the handler has no
    /// associated executor of its own.
    template <class Handler>
    any_completion(Handler&& handler, boost::asio::any_io_executor fallback)
    {
        using handler_type = typename std::decay<Handler>::type;
        using executor_type = typename std::decay<decltype(
            boost::asio::get_associated_executor(handler, fallback))>::type;
        using model_type = model<handler_type, executor_type>;
        using allocator_type = typename std::decay<decltype(
            boost::asio::get_associated_allocator(handler))>::type;

        allocator_type allocator(
            boost::asio::get_associated_allocator(handler));
        impl_ = std::allocate_shared<model_type>(
            allocator,
            std::forward<Handler>(handler),
            boost::asio::get_associated_executor(handler, fallback));
    }

    /// Re-erases an already erased handler (rvalue fast path).
    ///
    /// These non-template overloads keep the fast path - sharing the
    /// existing erasure - selected when a backend receives a completion
    /// handler that was erased by the layer above it.  Without them the
    /// generic constructor would be a better match for an rvalue and would
    /// wrap the handler a second time, adding an allocation and a redundant
    /// post.
    any_completion(any_completion&& other, boost::asio::any_io_executor) noexcept
        : impl_(std::move(other.impl_))
    {
    }

    any_completion(any_completion const& other, boost::asio::any_io_executor) noexcept
        : impl_(other.impl_)
    {
    }

    any_completion(any_completion const&) = default;
    any_completion(any_completion&&) noexcept = default;
    any_completion& operator=(any_completion const&) = default;
    any_completion& operator=(any_completion&&) noexcept = default;

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(impl_);
    }

    /// Invokes the handler once, through its own executor.
    void operator()(Args... args)
    {
        std::shared_ptr<base> impl = std::move(impl_);
        if (!impl)
            return;
        impl->dispatch(std::move(args)...);
    }

private:
    struct base
    {
        virtual ~base() = default;
        virtual void dispatch(Args... args) = 0;
    };

    template <class Handler, class Executor>
    class model final
        : public base
        , public std::enable_shared_from_this<model<Handler, Executor> >
    {
    public:
        template <class H, class E>
        model(H&& handler, E&& executor)
            : handler_(std::forward<H>(handler))
            , executor_(std::forward<E>(executor))
        {
        }

        void dispatch(Args... args) override
        {
            std::shared_ptr<model> self = this->shared_from_this();
            boost::asio::post(
                executor_,
                [self, args...]() mutable {
                    self->handler_(std::move(args)...);
                });
        }

    private:
        Handler handler_;
        Executor executor_;
    };

    std::shared_ptr<base> impl_;
};

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_ANY_COMPLETION_HPP
