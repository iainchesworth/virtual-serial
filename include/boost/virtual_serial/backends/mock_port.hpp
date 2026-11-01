//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_BACKENDS_MOCK_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_BACKENDS_MOCK_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/read_queue.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace boost {
namespace virtual_serial {

/** Line faults that the mock can inject.

    Each fault is delivered by the next read operation and is reported
    through the matching #error value, mirroring the error reporting of a
    real UART.
*/
enum class fault
{
    /// The stop bit was not where the receiver expected it.
    framing,

    /// The parity bit disagreed with the configured parity.
    parity,

    /// The receive FIFO overflowed before the byte could be read.
    overrun,

    /// A break condition was detected on the line.
    break_condition
};

/** What the mock does when injecting more bytes than its FIFO can hold. */
enum class overflow_policy
{
    /// Keep the bytes that already arrived; drop the new ones.
    drop_newest,

    /// Evict the oldest buffered bytes to make room for the new ones.
    drop_oldest
};

/** A deterministic in-memory serial port.

    `mock_port` is a complete Asio stream implementation
    (`SyncReadStream`, `SyncWriteStream`, `AsyncReadStream`,
    `AsyncWriteStream`) with a test fixture attached.  It is used in two
    ways:

    - directly, as a compile-time selected backend, giving zero-overhead
      tests that exercise the same template code as production; and
    - wrapped in #any_serial_port, to test the runtime switching path.

    @par Data injection
    #inject_read pushes bytes into the receive path exactly as if they had
    arrived from the wire.  No COM port, driver or operating system
    object is involved.

    @par Fault injection
    #inject_line_error, #inject_framing_error, #inject_parity_error,
    #inject_overrun, #inject_break, #inject_hangup, #inject_disconnect and
    #inject_error force the next read to fail, which lets a test drive
    error-handling and reconnection paths that are otherwise very hard to
    reproduce.

    @par Clocked data
    By default injected bytes are available immediately.  With
    #set_pacing enabled every byte is stamped with a due time derived from
    the configured baud rate (see #byte_duration), and only becomes
    visible when the virtual clock reaches it.  #advance, #step and
    #flush_input move that clock, so a test can deliver one byte at a time
    and interleave assertions with each delivery - the deterministic
    counterpart of a race-condition test.

    The virtual clock is manual on purpose: tests never sleep, never race
    the scheduler and never depend on machine speed.

    @par Thread Safety
    Distinct objects: safe.  Shared objects: unsafe.  Use a strand, as you
    would with `boost::asio::serial_port`.
*/
class mock_port
{
public:
    using executor_type = boost::asio::any_io_executor;
    using lowest_layer_type = mock_port;
    using read_completion =
        detail::any_completion<boost::system::error_code, std::size_t>;
    using write_completion = read_completion;
    using write_handler =
        std::function<void(const unsigned char*, std::size_t)>;

    /// Creates a closed mock port with default serial settings.
    explicit mock_port(executor_type executor)
        : executor_(std::move(executor))
        , queue_(executor_)
        , byte_duration_(compute_byte_duration(config_))
    {
        config_.backend = backend_kind::mock;
    }

    /// Creates a closed mock port with the supplied serial settings.
    explicit mock_port(executor_type executor, const serial_config& config)
        : executor_(std::move(executor))
        , queue_(executor_)
        , config_(config)
        , byte_duration_(compute_byte_duration(config_))
    {
        config_.backend = backend_kind::mock;
    }

    mock_port(mock_port&&) noexcept = default;
    mock_port& operator=(mock_port&&) noexcept = default;
    mock_port(const mock_port&) = delete;
    mock_port& operator=(const mock_port&) = delete;

    // -- Asio stream interface -------------------------------------------

    executor_type get_executor() const noexcept
    {
        return executor_;
    }

    /// Present for compatibility with the layered stream conventions.
    lowest_layer_type& lowest_layer() noexcept
    {
        return *this;
    }

    lowest_layer_type const& lowest_layer() const noexcept
    {
        return *this;
    }

    /// The backend kind, used by the type-erasing front end.
    backend_kind backend() const noexcept
    {
        return backend_kind::mock;
    }

    bool is_open() const noexcept
    {
        return open_;
    }

    std::intptr_t native_handle() noexcept
    {
        return 0;
    }

    /// Opens the fixture.  The target string is only a diagnostic label.
    void open(const std::string& target, boost::system::error_code& ec)
    {
        if (open_)
        {
            ec = error::already_open;
            return;
        }
        if (!target.empty())
            config_.target = target;
        queue_.clear_terminal();
        open_ = true;
        ec.clear();
    }

    /// Closes the fixture and aborts every pending operation.
    void close(boost::system::error_code& ec)
    {
        if (!open_)
        {
            ec = error::not_connected;
            return;
        }
        open_ = false;
        queue_.cancel(boost::asio::error::operation_aborted);
        abort_pending_writes();
        staged_.clear();
        ec.clear();
    }

    /// Aborts pending operations without closing the port.
    void cancel(boost::system::error_code& ec)
    {
        if (!open_)
        {
            ec = error::not_connected;
            return;
        }
        queue_.cancel(boost::asio::error::operation_aborted);
        abort_pending_writes();
        ec.clear();
    }

    /// Synchronous read.

    /// Reads whatever is available and completes with
    /// `boost::asio::error::would_block` when the inbox is empty.  A mock
    /// cannot block the calling thread while the test drives its clock,
    /// so the non-blocking contract is the honest one; every blocking
    /// convenience in Asio (`read`, `read_until`, ...) remains usable with
    /// the asynchronous API.
    std::size_t read_some(
        const detail::mutable_buffer_range& buffers,
        boost::system::error_code& ec)
    {
        if (!open_)
        {
            ec = error::not_connected;
            return 0;
        }
        return queue_.read(buffers, ec);
    }

    /// Synchronous read from any Asio `MutableBufferSequence`.
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
        if (!open_)
        {
            ec = error::not_connected;
            return 0;
        }
        if (write_error_)
        {
            ec = write_error_;
            write_error_.clear();
            return 0;
        }
        const std::vector<unsigned char> bytes = capture(buffers);
        const std::size_t written = record_write(bytes.data(), bytes.size());
        if (write_latency_.count() > 0)
        {
            now_ += write_latency_;
            release_due();
        }
        ec.clear();
        return written;
    }

    template <class ConstBufferSequence>
    std::size_t write_some(
        const ConstBufferSequence& buffers, boost::system::error_code& ec)
    {
        return write_some(detail::const_buffer_range(buffers), ec);
    }

    /// Asynchronous read.

    /// Completes as soon as at least one byte is available, or with an
    /// error.  Completion is posted to the handler's associated executor.
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

    /// Applies serial settings, validating them first.
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
        config_.backend = backend_kind::mock;
        byte_duration_ = compute_byte_duration(config_);
        ec.clear();
    }

    /// Returns the settings currently in force.
    serial_config options() const
    {
        return config_;
    }

    // -- Data injection ---------------------------------------------------

    /// Injects raw bytes into the receive path.
    void inject_read(const unsigned char* data, std::size_t count)
    {
        stage_bytes(data, count, std::chrono::nanoseconds::zero());
    }

    /// Injects raw bytes into the receive path.
    void inject_read(const char* data, std::size_t count)
    {
        stage_bytes(
            reinterpret_cast<const unsigned char*>(data),
            count,
            std::chrono::nanoseconds::zero());
    }

    /// Injects a string verbatim.
    void inject_read(const std::string& text)
    {
        inject_read(text.data(), text.size());
    }

    /// Injects a byte vector verbatim.
    void inject_read(const std::vector<unsigned char>& data)
    {
        if (!data.empty())
            inject_read(data.data(), data.size());
    }

    /// Injects a string literal, without its terminating NUL.
    template <std::size_t N>
    void inject_read(const char (&text)[N])
    {
        inject_read(text, N - 1);
    }

    /// Injects `text` followed by CRLF, the usual line terminator.
    void inject_line(const std::string& text)
    {
        inject_read(text);
        inject_read("\r\n", 2);
    }

    /// Injects bytes that become visible only after the virtual clock has
    /// advanced by `delay`.
    void inject_read_after(
        std::chrono::nanoseconds delay, const std::string& text)
    {
        stage_bytes(
            reinterpret_cast<const unsigned char*>(text.data()),
            text.size(),
            delay);
    }

    /// Reports end-of-stream once the buffered data has been consumed.
    void inject_eof()
    {
        queue_.set_eof();
    }

    /// Reports #error::hangup on every subsequent read, leaving the port
    /// open.  Use #inject_disconnect to model a lost connection.
    void inject_hangup()
    {
        queue_.set_error(make_error_code(error::hangup));
    }

    /// Models a sudden cable disconnection: buffered bytes are discarded,
    /// pending reads fail with #error::not_connected and the port is
    /// closed.
    void inject_disconnect()
    {
        open_ = false;
        queue_.set_error(make_error_code(error::not_connected));
        abort_pending_writes();
    }

    /// Queues an arbitrary error to be reported by the next read.
    void inject_error(const boost::system::error_code& ec)
    {
        queue_.push_fault(ec);
    }

    /// Queues a UART line fault reported by the next read.
    void inject_line_error(fault value)
    {
        queue_.push_fault(make_error_code(to_error(value)));
    }

    void inject_framing_error()
    {
        inject_line_error(fault::framing);
    }

    void inject_parity_error()
    {
        inject_line_error(fault::parity);
    }

    void inject_overrun()
    {
        inject_line_error(fault::overrun);
    }

    void inject_break()
    {
        inject_line_error(fault::break_condition);
    }

    /// Injects `replacement` in place of the next byte and reports a line
    /// fault, modelling a corrupted character on the wire.
    void inject_corrupt_byte(fault value, unsigned char replacement)
    {
        inject_read(&replacement, 1);
        inject_line_error(value);
    }

    /// Makes the next write fail with `ec`.
    void inject_write_error(const boost::system::error_code& ec)
    {
        write_error_ = ec;
    }

    // -- Clocked data -----------------------------------------------------

    /// Current virtual time, starting at zero when the port is created.
    std::chrono::nanoseconds now() const noexcept
    {
        return now_;
    }

    /// Wire time of a single character at the configured baud rate.
    std::chrono::nanoseconds byte_duration() const noexcept
    {
        return byte_duration_;
    }

    /// Enables baud-rate pacing of injected bytes.
    void set_pacing(bool enabled) noexcept
    {
        pacing_enabled_ = enabled;
    }

    bool pacing_enabled() const noexcept
    {
        return pacing_enabled_;
    }

    /// Overrides the pacing interval, for example to model a slow link.
    void set_byte_duration(std::chrono::nanoseconds duration) noexcept
    {
        byte_duration_ = duration;
    }

    /// Restores the pacing interval derived from the baud rate.
    void reset_byte_duration()
    {
        byte_duration_ = compute_byte_duration(config_);
    }

    /// Advances the virtual clock, delivering every byte that becomes due.
    void advance(std::chrono::nanoseconds delta)
    {
        now_ += delta;
        release_due();
    }

    /// Advances to the next scheduled byte or write completion.
    void advance_to_next()
    {
        bool have_event = false;
        std::chrono::nanoseconds due{};
        if (!staged_.empty())
        {
            due = staged_.front().due;
            have_event = true;
        }
        if (!pending_writes_.empty()
            && (!have_event || pending_writes_.front().due < due))
        {
            due = pending_writes_.front().due;
            have_event = true;
        }
        if (!have_event)
            return;
        if (due > now_)
            now_ = due;
        release_due();
    }

    /// Releases exactly `count` staged bytes, regardless of their due
    /// time, and moves the virtual clock to the last released byte.
    void step(std::size_t count = 1)
    {
        std::vector<unsigned char> bytes;
        bytes.reserve(count);
        while (count-- > 0 && !staged_.empty())
        {
            bytes.push_back(staged_.front().byte);
            now_ = std::max(now_, staged_.front().due);
            staged_.pop_front();
        }
        if (!bytes.empty())
            queue_.push(bytes.data(), bytes.size());
        complete_due_writes();
    }

    /// Releases every staged byte immediately.
    void flush_input()
    {
        std::vector<unsigned char> bytes;
        bytes.reserve(staged_.size());
        while (!staged_.empty())
        {
            bytes.push_back(staged_.front().byte);
            now_ = std::max(now_, staged_.front().due);
            staged_.pop_front();
        }
        if (!bytes.empty())
            queue_.push(bytes.data(), bytes.size());
        complete_due_writes();
    }

    /// Bytes that have been injected but are not yet visible.
    std::size_t staged_size() const noexcept
    {
        return staged_.size();
    }

    /// Bytes visible to a reader right now.
    std::size_t available() const noexcept
    {
        return queue_.available();
    }

    /// Number of read operations waiting for data.
    std::size_t pending_reads() const noexcept
    {
        return queue_.pending();
    }

    // -- Latency, flow control and back pressure -------------------------

    /// Delays the next injected burst by `delay` of virtual time.
    void set_read_latency(std::chrono::nanoseconds delay) noexcept
    {
        read_latency_ = delay;
    }

    std::chrono::nanoseconds read_latency() const noexcept
    {
        return read_latency_;
    }

    /// Delays completion of asynchronous writes by `delay` of virtual
    /// time.  Synchronous writes advance the virtual clock instead.
    void set_write_latency(std::chrono::nanoseconds delay) noexcept
    {
        write_latency_ = delay;
    }

    std::chrono::nanoseconds write_latency() const noexcept
    {
        return write_latency_;
    }

    /// Limits the unfilled receive buffer; zero means unlimited.
    void set_read_capacity(std::size_t bytes) noexcept
    {
        capacity_ = bytes;
    }

    std::size_t read_capacity() const noexcept
    {
        return capacity_;
    }

    void set_overflow_policy(overflow_policy policy) noexcept
    {
        overflow_policy_ = policy;
    }

    overflow_policy get_overflow_policy() const noexcept
    {
        return overflow_policy_;
    }

    /// Number of injections that overflowed the receive buffer.
    std::size_t overruns() const noexcept
    {
        return overruns_;
    }

    /// Caps how many bytes one read may return, modelling a small driver
    /// buffer and forcing callers to handle partial reads.
    void set_max_read_size(std::size_t bytes) noexcept
    {
        queue_.set_max_read_size(bytes);
    }

    // -- Device emulation -------------------------------------------------

    /// Installs a callback invoked for every byte written by the host.
    void set_write_handler(write_handler handler)
    {
        write_handler_ = std::move(handler);
    }

    /// Echoes every written byte back into the receive path.
    void set_echo(bool enabled) noexcept
    {
        echo_ = enabled;
    }

    /// Requires the concatenation of all writes to start with `text`.
    void expect_write(const std::string& text)
    {
        expectation_ += text;
    }

    /// True when every #expect_write has been matched in order.
    bool expectations_satisfied() const noexcept
    {
        return !expectation_mismatch_ && match_offset_ == expectation_.size();
    }

    /// Everything the host has written since construction or
    /// #clear_written.
    const std::string& written() const noexcept
    {
        return written_;
    }

    std::string take_written()
    {
        std::string result;
        result.swap(written_);
        return result;
    }

    void clear_written()
    {
        written_.clear();
    }

    void clear_expectations()
    {
        expectation_.clear();
        match_offset_ = 0;
        expectation_mismatch_ = false;
    }

    // -- Statistics -------------------------------------------------------

    std::size_t bytes_written() const noexcept
    {
        return bytes_written_;
    }

    std::size_t bytes_read() const noexcept
    {
        return queue_.delivered();
    }

    std::size_t injected_bytes() const noexcept
    {
        return injected_;
    }

    /// Clears payload, faults, statistics and the virtual clock.  The
    /// open/closed state and the configuration are preserved.
    void reset_state()
    {
        queue_.reset(boost::asio::error::operation_aborted);
        abort_pending_writes();
        staged_.clear();
        written_.clear();
        expectation_.clear();
        match_offset_ = 0;
        expectation_mismatch_ = false;
        write_error_.clear();
        injected_ = 0;
        bytes_written_ = 0;
        overruns_ = 0;
        now_ = std::chrono::nanoseconds::zero();
        last_due_ = std::chrono::nanoseconds::zero();
    }

private:
    struct staged_byte
    {
        unsigned char byte;
        std::chrono::nanoseconds due;
    };

    struct write_op
    {
        std::chrono::nanoseconds due;
        std::size_t transferred;
        write_completion handler;
        // A delayed write is not backed by a native operation, so it has to
        // register work or io_context::run() could return while it is
        // pending.
        boost::asio::executor_work_guard<executor_type> work;
    };

    struct read_initiator
    {
        mock_port& self;

        using executor_type = boost::asio::any_io_executor;

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
        mock_port& self;

        using executor_type = boost::asio::any_io_executor;

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

    static std::chrono::nanoseconds compute_byte_duration(
        const serial_config& config) noexcept
    {
        if (config.baud_rate == 0)
            return std::chrono::nanoseconds(0);
        double bits = 1.0; // start bit
        bits += static_cast<double>(config.data_bits);
        if (config.parity != parity_mode::none)
            bits += 1.0;
        switch (config.stop_bits)
        {
        case stop_bits_mode::one:
            bits += 1.0;
            break;
        case stop_bits_mode::one_point_five:
            bits += 1.5;
            break;
        case stop_bits_mode::two:
            bits += 2.0;
            break;
        }
        const double seconds = bits / static_cast<double>(config.baud_rate);
        const long long nanoseconds =
            static_cast<long long>(seconds * 1e9 + 0.5);
        return std::chrono::nanoseconds(nanoseconds > 0 ? nanoseconds : 1);
    }

    static error to_error(fault value) noexcept
    {
        switch (value)
        {
        case fault::framing:
            return error::framing_error;
        case fault::parity:
            return error::parity_error;
        case fault::overrun:
            return error::overrun;
        case fault::break_condition:
            return error::break_detected;
        }
        return error::success;
    }

    void start_read(
        detail::mutable_buffer_range buffers, read_completion handler)
    {
        if (!open_)
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }
        queue_.async_read(std::move(buffers), std::move(handler));
    }

    void start_write(
        detail::const_buffer_range buffers, write_completion handler)
    {
        if (!open_)
        {
            handler(make_error_code(error::not_connected), 0);
            return;
        }
        if (write_error_)
        {
            boost::system::error_code failure = write_error_;
            write_error_.clear();
            handler(failure, 0);
            return;
        }
        const std::vector<unsigned char> bytes = capture(buffers);
        const std::size_t written = record_write(bytes.data(), bytes.size());
        if (write_latency_.count() > 0)
        {
            pending_writes_.push_back(write_op{
                now_ + write_latency_,
                written,
                std::move(handler),
                boost::asio::executor_work_guard<executor_type>(executor_)});
        }
        else
        {
            handler(boost::system::error_code(), written);
        }
    }

    static std::vector<unsigned char> capture(
        const detail::const_buffer_range& buffers)
    {
        std::vector<unsigned char> bytes(buffers.total_size());
        if (!bytes.empty())
            detail::gather(buffers, bytes.data(), bytes.size());
        return bytes;
    }

    std::size_t record_write(const unsigned char* data, std::size_t count)
    {
        if (count == 0)
            return 0;
        written_.append(reinterpret_cast<const char*>(data), count);
        bytes_written_ += count;

        for (std::size_t i = 0; i < count; ++i)
        {
            if (match_offset_ < expectation_.size())
            {
                if (data[i]
                    == static_cast<unsigned char>(expectation_[match_offset_]))
                    ++match_offset_;
                else
                    expectation_mismatch_ = true;
            }
        }

        if (write_handler_)
            write_handler_(data, count);
        if (echo_)
            stage_bytes(data, count, std::chrono::nanoseconds::zero());
        return count;
    }

    void stage_bytes(
        const unsigned char* data,
        std::size_t count,
        std::chrono::nanoseconds extra_delay)
    {
        if (count == 0)
            return;

        std::size_t accepted = count;
        if (capacity_ != 0)
        {
            std::size_t buffered = queue_.available() + staged_.size();
            if (buffered + count > capacity_)
            {
                ++overruns_;
                if (overflow_policy_ == overflow_policy::drop_oldest)
                {
                    drop_oldest(buffered + count - capacity_);
                    buffered = queue_.available() + staged_.size();
                }
                if (buffered + count > capacity_)
                    accepted = capacity_ > buffered ? capacity_ - buffered : 0;
                queue_.push_fault(make_error_code(error::overrun));
            }
        }

        if (accepted == 0)
            return;

        std::vector<unsigned char> immediate;
        immediate.reserve(accepted);
        std::chrono::nanoseconds due = now_ + read_latency_ + extra_delay;
        const bool paced = pacing_enabled_;
        if (paced)
            due = std::max(due, last_due_);

        for (std::size_t i = 0; i < accepted; ++i)
        {
            if (paced)
                due += byte_duration_;
            if (due <= now_)
                immediate.push_back(data[i]);
            else
                staged_.push_back(staged_byte{data[i], due});
        }
        if (paced)
            last_due_ = due;
        if (!immediate.empty())
            queue_.push(immediate.data(), immediate.size());
        injected_ += accepted;
    }

    void drop_oldest(std::size_t count)
    {
        const std::size_t from_queue = std::min(count, queue_.available());
        queue_.drop(from_queue);
        count -= from_queue;
        while (count > 0 && !staged_.empty())
        {
            staged_.pop_front();
            --count;
        }
    }

    void release_due()
    {
        std::vector<unsigned char> bytes;
        while (!staged_.empty() && staged_.front().due <= now_)
        {
            bytes.push_back(staged_.front().byte);
            staged_.pop_front();
        }
        if (!bytes.empty())
            queue_.push(bytes.data(), bytes.size());
        complete_due_writes();
    }

    void complete_due_writes()
    {
        while (!pending_writes_.empty()
               && pending_writes_.front().due <= now_)
        {
            write_op op = std::move(pending_writes_.front());
            pending_writes_.pop_front();
            op.handler(boost::system::error_code(), op.transferred);
        }
    }

    void abort_pending_writes()
    {
        while (!pending_writes_.empty())
        {
            write_op op = std::move(pending_writes_.front());
            pending_writes_.pop_front();
            op.handler(boost::asio::error::operation_aborted, 0);
        }
    }

    executor_type executor_;
    detail::read_queue queue_;
    serial_config config_;
    std::chrono::nanoseconds byte_duration_;
    std::chrono::nanoseconds now_{0};
    std::chrono::nanoseconds last_due_{0};
    std::chrono::nanoseconds read_latency_{0};
    std::chrono::nanoseconds write_latency_{0};
    std::deque<staged_byte> staged_;
    std::deque<write_op> pending_writes_;
    std::string written_;
    std::string expectation_;
    write_handler write_handler_;
    boost::system::error_code write_error_;
    std::size_t match_offset_ = 0;
    std::size_t injected_ = 0;
    std::size_t bytes_written_ = 0;
    std::size_t overruns_ = 0;
    std::size_t capacity_ = 0;
    overflow_policy overflow_policy_ = overflow_policy::drop_newest;
    bool expectation_mismatch_ = false;
    bool pacing_enabled_ = false;
    bool echo_ = false;
    bool open_ = false;
};

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_BACKENDS_MOCK_PORT_HPP
