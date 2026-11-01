//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_READ_QUEUE_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_READ_QUEUE_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cstddef>
#include <deque>
#include <utility>

namespace boost {
namespace virtual_serial {
namespace detail {

/** A byte inbox with a queue of waiting readers.

    Several backends face the same problem: bytes arrive from somewhere
    (an injected fixture, a decoded Telnet stream, a socket) independently
    of the caller's buffers, and readers may be waiting when they arrive.
    `read_queue` owns that bookkeeping so the mock, RFC 2217 and bridge
    backends share one implementation of the tricky parts:

    - completion in arrival order with at least one byte, or an error;
    - queued line faults (framing, parity, overrun, break) delivered
      before the data that follows them;
    - sticky end-of-stream and terminal errors;
    - optional partial reads to exercise resumable user state machines.

    The queue never calls a handler inline: completions are posted through
    the erased handler's executor, which keeps Asio's "handler is never
    invoked before the initiating function returns" guarantee intact.
*/
class read_queue
{
public:
    using executor_type = boost::asio::any_io_executor;
    using completion =
        any_completion<boost::system::error_code, std::size_t>;

    explicit read_queue(executor_type executor)
        : executor_(std::move(executor))
    {
    }

    read_queue(const read_queue&) = delete;
    read_queue& operator=(const read_queue&) = delete;
    read_queue(read_queue&&) noexcept = default;
    read_queue& operator=(read_queue&&) noexcept = default;

    executor_type get_executor() const noexcept
    {
        return executor_;
    }

    // -- producer side ----------------------------------------------------

    /// Appends bytes and completes as many waiting readers as possible.
    void push(const unsigned char* data, std::size_t count)
    {
        for (std::size_t i = 0; i < count; ++i)
            input_.push_back(data[i]);
        pump();
    }

    void push(unsigned char byte)
    {
        input_.push_back(byte);
        pump();
    }

    /// Marks the stream as finished.  Buffered data is still readable.
    void set_eof()
    {
        eof_ = true;
        pump();
    }

    /// Drops buffered data and makes every current and future read fail
    /// with `ec`.  The terminal state is sticky until #reset.
    void set_error(boost::system::error_code ec)
    {
        error_ = ec;
        input_.clear();
        pump();
    }

    /// Queues a line fault to be reported by the next read.
    void push_fault(boost::system::error_code ec)
    {
        faults_.push_back(ec);
        pump();
    }

    /// Drops buffered bytes without affecting faults or the terminal state.
    void clear_data()
    {
        input_.clear();
    }

    /// Drops up to `count` bytes from the front of the inbox.
    void drop(std::size_t count)
    {
        while (count-- > 0 && !input_.empty())
            input_.pop_front();
    }

    /// Forgets end-of-stream and terminal errors but keeps buffered bytes
    /// and queued faults.  Used when a mock port is reopened.
    void clear_terminal()
    {
        eof_ = false;
        error_.clear();
    }

    /// Returns the queue to its initial state and aborts waiting readers.
    void reset(boost::system::error_code abort_code)
    {
        cancel(abort_code);
        input_.clear();
        faults_.clear();
        eof_ = false;
        error_.clear();
        delivered_ = 0;
    }

    /// Completes every waiting reader with `ec` without changing the
    /// terminal state.
    void cancel(boost::system::error_code ec)
    {
        while (!pending_.empty())
        {
            read_op op = std::move(pending_.front());
            pending_.pop_front();
            op.handler(ec, 0);
        }
    }

    /// Limits how many bytes a single read may report.  Zero means "as
    /// many as fit", which is what a real UART does.
    void set_max_read_size(std::size_t value) noexcept
    {
        max_read_size_ = value;
    }

    std::size_t max_read_size() const noexcept
    {
        return max_read_size_;
    }

    // -- consumer side ----------------------------------------------------

    /// Enqueues a read and completes it immediately if data is available.
    void async_read(mutable_buffer_range buffers, completion handler)
    {
        pending_.push_back(read_op{
            std::move(buffers),
            std::move(handler),
            work_guard(executor_)});
        pump();
    }

    /// Non-blocking read used by the synchronous API.
    std::size_t read(
        const mutable_buffer_range& buffers, boost::system::error_code& ec)
    {
        if (!faults_.empty())
        {
            ec = faults_.front();
            faults_.pop_front();
            return 0;
        }
        if (!input_.empty())
        {
            ec.clear();
            return drain(buffers);
        }
        if (error_)
        {
            ec = error_;
            return 0;
        }
        if (eof_)
        {
            ec = boost::asio::error::eof;
            return 0;
        }
        ec = boost::asio::error::would_block;
        return 0;
    }

    std::size_t available() const noexcept
    {
        return input_.size();
    }

    bool empty() const noexcept
    {
        return input_.empty();
    }

    std::size_t pending() const noexcept
    {
        return pending_.size();
    }

    /// Number of bytes handed to readers since construction or #reset.
    std::size_t delivered() const noexcept
    {
        return delivered_;
    }

    bool finished() const noexcept
    {
        return eof_ || static_cast<bool>(error_);
    }

private:
    /// Keeps the io_context alive while a read is outstanding.
    ///
    /// Asio requires an outstanding asynchronous operation to count as work;
    /// a queued read in this class is not backed by a native operation, so
    /// the queue has to register the work itself or `io_context::run()` would
    /// return while the operation is still pending.
    using work_guard = boost::asio::executor_work_guard<executor_type>;

    struct read_op
    {
        mutable_buffer_range buffers;
        completion handler;
        work_guard work;
    };

    std::size_t drain(const mutable_buffer_range& buffers)
    {
        std::size_t total = 0;
        const std::size_t limit = max_read_size_ == 0
                                      ? static_cast<std::size_t>(-1)
                                      : max_read_size_;
        for (std::size_t i = 0; i < buffers.size() && !input_.empty()
                               && total < limit;
             ++i)
        {
            const std::size_t capacity = buffers[i].size();
            const std::size_t wanted = std::min<std::size_t>(
                capacity, std::min<std::size_t>(input_.size(), limit - total));
            if (wanted == 0)
                continue;
            unsigned char* destination =
                static_cast<unsigned char*>(buffers[i].data());
            for (std::size_t j = 0; j < wanted; ++j)
            {
                destination[j] = input_.front();
                input_.pop_front();
            }
            total += wanted;
        }
        delivered_ += total;
        return total;
    }

    void pump()
    {
        while (!pending_.empty())
        {
            boost::system::error_code ec;
            std::size_t transferred = 0;

            if (!faults_.empty())
            {
                ec = faults_.front();
                faults_.pop_front();
            }
            else if (!input_.empty())
            {
                transferred = drain(pending_.front().buffers);
            }
            else if (error_)
            {
                ec = error_;
            }
            else if (eof_)
            {
                ec = boost::asio::error::eof;
            }
            else
            {
                return;
            }

            read_op op = std::move(pending_.front());
            pending_.pop_front();
            op.handler(ec, transferred);
        }
    }

    executor_type executor_;
    std::deque<unsigned char> input_;
    std::deque<boost::system::error_code> faults_;
    std::deque<read_op> pending_;
    boost::system::error_code error_;
    std::size_t delivered_ = 0;
    std::size_t max_read_size_ = 0;
    bool eof_ = false;
};

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_READ_QUEUE_HPP
