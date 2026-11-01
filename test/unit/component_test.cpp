//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Direct unit tests for the small internal components: the byte inbox, the
// native-handle conversion, the erased completion and the erased backend.
// They are deterministic and need no I/O beyond an io_context executor.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/erased_backend.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>
#include <boost/virtual_serial/detail/read_queue.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
// native handle conversion
// ---------------------------------------------------------------------------

VS_TEST_CASE(native_handle_converts_integral_and_pointer)
{
    VS_CHECK_EQ(vs::detail::to_intptr(7), static_cast<std::intptr_t>(7));
    VS_CHECK_EQ(
        vs::detail::to_intptr(static_cast<void*>(nullptr)),
        static_cast<std::intptr_t>(0));

    int value = 0;
    const std::intptr_t pointer_value =
        vs::detail::to_intptr(static_cast<void*>(&value));
    VS_CHECK_EQ(pointer_value, reinterpret_cast<std::intptr_t>(&value));
}

// ---------------------------------------------------------------------------
// erased completion
// ---------------------------------------------------------------------------

VS_TEST_CASE(any_completion_default_is_inert)
{
    vs::detail::any_completion<boost::system::error_code, std::size_t> empty;
    VS_CHECK(!empty);
    empty(boost::system::error_code(), 0); // must be a harmless no-op
}

VS_TEST_CASE(any_completion_invokes_once)
{
    asio::io_context io;
    int calls = 0;
    std::size_t received = 0;

    vs::detail::any_completion<boost::system::error_code, std::size_t> handler(
        [&](boost::system::error_code, std::size_t count) {
            ++calls;
            received = count;
        },
        io.get_executor());

    VS_CHECK(static_cast<bool>(handler));
    handler(boost::system::error_code(), 5);
    VS_CHECK_EQ(calls, 0); // delivery is posted, never inline
    vs_test::pump(io);
    VS_CHECK_EQ(calls, 1);
    VS_CHECK_EQ(received, 5u);

    // A second invocation is a no-op.
    handler(boost::system::error_code(), 9);
    vs_test::pump(io);
    VS_CHECK_EQ(calls, 1);
}

VS_TEST_CASE(any_completion_re_erasure_shares_state)
{
    asio::io_context io;
    int calls = 0;
    vs::detail::any_completion<boost::system::error_code, std::size_t> original(
        [&](boost::system::error_code, std::size_t) { ++calls; },
        io.get_executor());

    vs::detail::any_completion<boost::system::error_code, std::size_t> copy(
        original, io.get_executor());
    VS_CHECK(static_cast<bool>(copy));
    copy(boost::system::error_code(), 1);
    vs_test::pump(io);
    VS_CHECK_EQ(calls, 1);
}

// ---------------------------------------------------------------------------
// read queue
// ---------------------------------------------------------------------------

namespace {

struct queue_fixture
{
    asio::io_context io;
    vs::detail::read_queue queue{io.get_executor()};
};

struct queued_read
{
    char data[16] = {};
    boost::system::error_code ec;
    std::size_t transferred = 0;
    int completions = 0;

    void operator()(boost::system::error_code error, std::size_t count)
    {
        ++completions;
        ec = error;
        transferred = count;
    }
};

} // namespace

VS_TEST_CASE(read_queue_single_byte_push)
{
    queue_fixture f;
    const unsigned char byte = 'Z';
    f.queue.push(byte);
    VS_CHECK_EQ(f.queue.available(), 1u);
    VS_CHECK(!f.queue.empty());

    char buffer[4] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec), 1u);
    VS_CHECK(!ec);
    VS_CHECK_EQ(buffer[0], 'Z');
}

VS_TEST_CASE(read_queue_clear_data_and_terminal_state)
{
    queue_fixture f;
    f.queue.push(reinterpret_cast<const unsigned char*>("abc"), 3);
    f.queue.clear_data();
    VS_CHECK_EQ(f.queue.available(), 0u);

    f.queue.set_error(vs::make_error_code(vs::error::hangup));
    VS_CHECK(f.queue.finished());

    char buffer[4] = {};
    boost::system::error_code ec;
    f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec);
    VS_CHECK(ec == vs::error::hangup);

    f.queue.clear_terminal();
    VS_CHECK(!f.queue.finished());
    ec.clear();
    f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec);
    VS_CHECK(ec == asio::error::would_block);
}

VS_TEST_CASE(read_queue_drop_and_reset)
{
    queue_fixture f;
    f.queue.push(reinterpret_cast<const unsigned char*>("abcdef"), 6);
    f.queue.drop(2);
    VS_CHECK_EQ(f.queue.available(), 4u);

    f.queue.push_fault(vs::make_error_code(vs::error::parity_error));
    f.queue.reset(asio::error::operation_aborted);
    VS_CHECK_EQ(f.queue.available(), 0u);
    VS_CHECK_EQ(f.queue.delivered(), 0u);
    VS_CHECK(!f.queue.finished());
}

VS_TEST_CASE(read_queue_cancel_aborts_waiters)
{
    queue_fixture f;
    const std::shared_ptr<queued_read> read = std::make_shared<queued_read>();
    f.queue.async_read(
        vs::detail::mutable_buffer_range(asio::buffer(read->data)),
        vs::detail::read_queue::completion(
            [read](boost::system::error_code error, std::size_t count) {
                (*read)(error, count);
            },
            f.io.get_executor()));
    VS_CHECK_EQ(f.queue.pending(), 1u);

    f.queue.cancel(asio::error::operation_aborted);
    vs_test::pump(f.io);
    VS_CHECK_EQ(read->completions, 1);
    VS_CHECK(read->ec == asio::error::operation_aborted);
    VS_CHECK_EQ(f.queue.pending(), 0u);
}

VS_TEST_CASE(read_queue_max_read_size)
{
    queue_fixture f;
    VS_CHECK_EQ(f.queue.max_read_size(), 0u);
    f.queue.set_max_read_size(3);
    VS_CHECK_EQ(f.queue.max_read_size(), 3u);

    f.queue.push(reinterpret_cast<const unsigned char*>("abcdefgh"), 8);
    char buffer[16] = {};
    boost::system::error_code ec;
    VS_CHECK_EQ(f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec), 3u);
}

VS_TEST_CASE(read_queue_executor_and_delivered)
{
    queue_fixture f;
    VS_CHECK(f.queue.get_executor() == f.io.get_executor());

    f.queue.push(reinterpret_cast<const unsigned char*>("xy"), 2);
    char buffer[4] = {};
    boost::system::error_code ec;
    f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec);
    VS_CHECK_EQ(f.queue.delivered(), 2u);
}

VS_TEST_CASE(read_queue_fault_precedes_eof_and_data)
{
    queue_fixture f;
    f.queue.push(reinterpret_cast<const unsigned char*>("ok"), 2);
    f.queue.set_eof();
    f.queue.push_fault(vs::make_error_code(vs::error::framing_error));

    char buffer[4] = {};
    boost::system::error_code ec;
    f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec);
    VS_CHECK(ec == vs::error::framing_error);

    ec.clear();
    VS_CHECK_EQ(f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec), 2u);
    VS_CHECK(!ec);

    ec.clear();
    f.queue.read(vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec);
    VS_CHECK(ec == asio::error::eof);
}

// ---------------------------------------------------------------------------
// erased backend with no endpoint
// ---------------------------------------------------------------------------

VS_TEST_CASE(erased_backend_without_endpoint_is_safe)
{
    vs::detail::erased_backend backend(nullptr);
    VS_CHECK(backend.get() == nullptr);
    VS_CHECK(!backend.is_open());
    VS_CHECK(backend.backend() == vs::backend_kind::mock);
    VS_CHECK_EQ(backend.native_handle(), 0);
    VS_CHECK_EQ(backend.options().baud_rate, 9600u);
    VS_CHECK(backend.backend_as<vs::mock_port>() == nullptr);
    static_cast<void>(backend.get_executor());

    boost::system::error_code ec;
    backend.open("nowhere", ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    backend.close(ec);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    backend.cancel(ec);
    VS_CHECK(ec == vs::error::not_connected);

    char buffer[4] = {};
    ec.clear();
    VS_CHECK_EQ(
        backend.read_some(
            vs::detail::mutable_buffer_range(asio::buffer(buffer)), ec),
        0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    VS_CHECK_EQ(
        backend.write_some(
            vs::detail::const_buffer_range(asio::buffer(buffer, 1)), ec),
        0u);
    VS_CHECK(ec == vs::error::not_connected);

    ec.clear();
    backend.apply_options(vs::serial_config(), ec);
    VS_CHECK(ec == vs::error::not_connected);

    asio::io_context io;
    int completions = 0;
    backend.async_read_some(
        vs::detail::mutable_buffer_range(asio::buffer(buffer)),
        vs::detail::erased_backend::read_completion(
            [&](boost::system::error_code error, std::size_t) {
                VS_CHECK(error == vs::error::not_connected);
                ++completions;
            },
            io.get_executor()));
    backend.async_write_some(
        vs::detail::const_buffer_range(asio::buffer(buffer, 1)),
        vs::detail::erased_backend::write_completion(
            [&](boost::system::error_code error, std::size_t) {
                VS_CHECK(error == vs::error::not_connected);
                ++completions;
            },
            io.get_executor()));
    vs_test::pump(io);
    VS_CHECK_EQ(completions, 2);
}

VS_TEST_CASE(erased_backend_type_mismatch)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    std::shared_ptr<vs::detail::erased_backend> backend =
        vs::detail::erased_backend::make(mock);

    VS_CHECK(backend->get() != nullptr);
    VS_CHECK(backend->is_open() == false);
    VS_CHECK(backend->backend_as<vs::mock_port>() == mock.get());
    VS_CHECK(backend->backend_as<vs::physical_port>() == nullptr);

    // Move construction and assignment keep the endpoint alive.
    vs::detail::erased_backend moved(std::move(*backend));
    VS_CHECK(moved.get() != nullptr);
    vs::detail::erased_backend assigned(nullptr);
    assigned = std::move(moved);
    VS_CHECK(assigned.get() != nullptr);
    VS_CHECK_EQ(assigned.native_handle(), 0);
}

VS_TEST_CASE(backend_cast_handles_null_and_mismatch)
{
    VS_CHECK(vs::detail::backend_cast<vs::mock_port>(nullptr) == nullptr);

    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    VS_CHECK(vs::backend_cast<vs::mock_port>(port) == mock.get());
    VS_CHECK(vs::backend_cast<vs::physical_port>(port) == nullptr);
}

// ---------------------------------------------------------------------------
// error category fallback
// ---------------------------------------------------------------------------

VS_TEST_CASE(error_category_unknown_value)
{
    VS_CHECK_EQ(
        vs::error_category().message(424242),
        std::string("unknown virtual_serial error"));
}

// ---------------------------------------------------------------------------
// telnet mapping helpers
// ---------------------------------------------------------------------------

VS_TEST_CASE(telnet_index_round_trips)
{
    namespace tn = vs::detail::telnet;

    VS_CHECK(tn::parity_from_index(2) == vs::parity_mode::odd);
    VS_CHECK(tn::parity_from_index(3) == vs::parity_mode::even);
    VS_CHECK(tn::parity_from_index(5) == vs::parity_mode::space);
    VS_CHECK(tn::parity_from_index(1) == vs::parity_mode::none);

    VS_CHECK(tn::stop_size_from_index(2) == vs::stop_bits_mode::two);
    VS_CHECK(tn::stop_size_from_index(1) == vs::stop_bits_mode::one);

    VS_CHECK_EQ(
        static_cast<int>(tn::control_index(
            static_cast<vs::flow_control_mode>(99))),
        static_cast<int>(tn::rfc2217::control_no_flow));
}

VS_TEST_CASE(telnet_decoder_without_sinks)
{
    vs::detail::telnet::decoder decoder;
    const unsigned char data[] = {'a', 'b'};
    decoder.feed(data, sizeof(data)); // exercises the "no sink" flush path
}
