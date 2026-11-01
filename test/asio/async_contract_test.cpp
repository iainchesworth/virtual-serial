//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Evidence that the asynchronous, Asio-facing side of the library behaves:
//
//   * every front end and backend models the four Asio stream concepts;
//   * an outstanding operation keeps io_context::run() alive (work tracking);
//   * handlers are never invoked inline, are invoked exactly once, and run on
//     their associated executor (including a strand shared by four threads);
//   * generic composition works: async_read/async_write/async_read_until and
//     a user-written async_compose operation;
//   * tokens compose: detached, redirect_error and use_future.
//

#include "../detail/io_test.hpp"
#include "../detail/test_framework.hpp"
#include "../mock/tcp_echo_server.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/compose.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/coroutine.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/write.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <istream>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
// Compile-time: the Asio stream concepts
// ---------------------------------------------------------------------------

namespace {

template <class T, class = void>
struct async_read_stream : std::false_type
{
};

template <class T>
struct async_read_stream<
    T,
    decltype(void(std::declval<T&>().async_read_some(
        std::declval<asio::mutable_buffer>(),
        std::declval<void (*)(boost::system::error_code, std::size_t)>())))>
    : std::true_type
{
};

template <class T, class = void>
struct async_write_stream : std::false_type
{
};

template <class T>
struct async_write_stream<
    T,
    decltype(void(std::declval<T&>().async_write_some(
        std::declval<asio::const_buffer>(),
        std::declval<void (*)(boost::system::error_code, std::size_t)>())))>
    : std::true_type
{
};

template <class T, class = void>
struct sync_read_stream : std::false_type
{
};

template <class T>
struct sync_read_stream<
    T,
    decltype(void(std::declval<T&>().read_some(
        std::declval<asio::mutable_buffer>(),
        std::declval<boost::system::error_code&>())))> : std::true_type
{
};

template <class T, class = void>
struct sync_write_stream : std::false_type
{
};

template <class T>
struct sync_write_stream<
    T,
    decltype(void(std::declval<T&>().write_some(
        std::declval<asio::const_buffer>(),
        std::declval<boost::system::error_code&>())))> : std::true_type
{
};

template <class T, class = void>
struct has_executor : std::false_type
{
};

template <class T>
struct has_executor<
    T,
    typename std::enable_if<std::is_same<
        decltype(std::declval<const T&>().get_executor()),
        typename T::executor_type>::value>::type> : std::true_type
{
};

#define VS_CHECK_STREAM_CONCEPTS(TYPE)                                         \
    static_assert(                                                             \
        async_read_stream<TYPE>::value, #TYPE " is not an AsyncReadStream");   \
    static_assert(                                                             \
        async_write_stream<TYPE>::value, #TYPE " is not an AsyncWriteStream"); \
    static_assert(                                                             \
        sync_read_stream<TYPE>::value, #TYPE " is not a SyncReadStream");      \
    static_assert(                                                             \
        sync_write_stream<TYPE>::value, #TYPE " is not a SyncWriteStream");    \
    static_assert(                                                             \
        has_executor<TYPE>::value, #TYPE " has no executor_type/get_executor")

VS_CHECK_STREAM_CONCEPTS(vs::mock_port);
VS_CHECK_STREAM_CONCEPTS(vs::physical_port);
VS_CHECK_STREAM_CONCEPTS(vs::rfc2217_port);
VS_CHECK_STREAM_CONCEPTS(vs::tcp_bridge_port);
VS_CHECK_STREAM_CONCEPTS(vs::udp_bridge_port);
VS_CHECK_STREAM_CONCEPTS(vs::basic_serial_port<vs::mock_port>);
VS_CHECK_STREAM_CONCEPTS(vs::any_serial_port);

#undef VS_CHECK_STREAM_CONCEPTS

static_assert(
    std::is_same<vs::any_serial_port::executor_type, asio::any_io_executor>::value,
    "the erased front end must expose the executor it forwards");
static_assert(
    !std::is_copy_constructible<vs::any_serial_port>::value,
    "Asio I/O objects are move-only");
static_assert(
    std::is_move_constructible<vs::any_serial_port>::value,
    "Asio I/O objects are movable");

} // namespace

// ---------------------------------------------------------------------------
// Work tracking: run() must not return while an operation is outstanding
// ---------------------------------------------------------------------------

namespace {

struct running_context
{
    asio::io_context& io;
    std::atomic<bool> returned{false};
    std::thread thread;

    explicit running_context(asio::io_context& context)
        : io(context)
        , thread([this] {
            io.run();
            returned = true;
        })
    {
    }

    ~running_context()
    {
        if (thread.joinable())
        {
            io.stop();
            thread.join();
        }
    }

    /// True when run() has returned within `timeout`.
    bool returned_within(std::chrono::milliseconds timeout)
    {
        const std::chrono::steady_clock::time_point deadline =
            std::chrono::steady_clock::now() + timeout;
        while (!returned.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return returned.load();
    }

    void join()
    {
        if (thread.joinable())
            thread.join();
    }
};

} // namespace

VS_TEST_CASE(async_pending_read_keeps_io_context_alive)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("work", ec);
    VS_REQUIRE(!ec);

    char buffer[4] = {};
    std::atomic<bool> done{false};
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { done = true; });

    running_context runner(io);
    VS_CHECK(!runner.returned_within(std::chrono::milliseconds(100)));

    // Deliver from the io thread; then the context must drain and return.
    asio::post(io, [&] { mock->inject_read("x"); });
    runner.join();
    VS_CHECK(done.load());
    VS_CHECK(runner.returned.load());
}

VS_TEST_CASE(async_pending_write_keeps_io_context_alive)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("work", ec);
    VS_REQUIRE(!ec);
    mock->set_write_latency(std::chrono::milliseconds(1000));

    std::atomic<bool> done{false};
    port.async_write_some(asio::buffer("abc", 3),
        [&](boost::system::error_code, std::size_t) { done = true; });

    running_context runner(io);
    VS_CHECK(!runner.returned_within(std::chrono::milliseconds(100)));

    // Complete the delayed write through the virtual clock, on the io thread.
    asio::post(io, [&] { mock->advance(std::chrono::milliseconds(1000)); });
    runner.join();
    VS_CHECK(done.load());
    VS_CHECK(runner.returned.load());
}

VS_TEST_CASE(async_close_releases_pending_work)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("work", ec);
    VS_REQUIRE(!ec);

    char buffer[4] = {};
    boost::system::error_code result;
    std::atomic<bool> done{false};
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code error, std::size_t) {
            result = error;
            done = true;
        });

    running_context runner(io);
    VS_CHECK(!runner.returned_within(std::chrono::milliseconds(100)));

    asio::post(io, [&] {
        boost::system::error_code ignored;
        port.close(ignored);
    });
    runner.join();
    VS_CHECK(done.load());
    VS_CHECK(result == asio::error::operation_aborted);
    VS_CHECK(runner.returned.load());
}

namespace {

/// Accepts one connection, pushes one payload, then closes.
///
/// A persistent echo server would keep a read outstanding and therefore keep
/// run() alive forever; this helper leaves nothing behind, which is what the
/// work-tracking assertion needs.
class one_shot_server
{
public:
    explicit one_shot_server(asio::io_context& io)
        : acceptor_(io)
    {
    }

    void start(std::string payload)
    {
        payload_ = std::move(payload);
        const asio::ip::tcp::endpoint endpoint(
            asio::ip::address_v4::loopback(), 0);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        port_ = acceptor_.local_endpoint().port();
        accept();
    }

    unsigned short port() const
    {
        return port_;
    }

    /// Starts the single write; safe to call before the peer connects.
    void release()
    {
        if (!accepted_)
        {
            release_pending_ = true;
            return;
        }
        write_now();
    }

private:
    void accept()
    {
        peer_ = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
        acceptor_.async_accept(*peer_, [this](boost::system::error_code ec) {
            if (ec)
                return;
            accepted_ = true;
            if (release_pending_)
                write_now();
        });
    }

    void write_now()
    {
        asio::async_write(*peer_, asio::buffer(payload_),
            [this](boost::system::error_code, std::size_t) { close(); });
    }

    void close()
    {
        boost::system::error_code ignored;
        if (peer_)
            peer_->close(ignored);
        acceptor_.close(ignored);
    }

    asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<asio::ip::tcp::socket> peer_;
    std::string payload_;
    unsigned short port_ = 0;
    bool accepted_ = false;
    bool release_pending_ = false;
};

} // namespace

VS_TEST_CASE(async_tcp_pending_read_keeps_io_context_alive)
{
    asio::io_context io;
    one_shot_server server(io);
    server.start("go");

    vs::tcp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);

    char buffer[8] = {};
    std::atomic<bool> done{false};
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { done = true; });

    running_context runner(io);
    VS_CHECK(!runner.returned_within(std::chrono::milliseconds(100)));

    asio::post(io, [&] { server.release(); });
    runner.join();
    VS_CHECK(done.load());
    VS_CHECK(runner.returned.load());
}

// ---------------------------------------------------------------------------
// Handler guarantees
// ---------------------------------------------------------------------------

VS_TEST_CASE(async_handler_is_not_invoked_before_returning)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("inline", ec);
    VS_REQUIRE(!ec);

    // Data is already available: a naive implementation would complete inline.
    mock->inject_read("ready");
    char buffer[8] = {};
    bool called = false;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { called = true; });
    VS_CHECK(!called);

    vs_test::pump(io);
    VS_CHECK(called);
}

VS_TEST_CASE(async_handler_is_invoked_exactly_once)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("once", ec);
    VS_REQUIRE(!ec);

    char buffer[8] = {};
    int completions = 0;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { ++completions; });
    vs_test::pump(io);
    VS_CHECK_EQ(completions, 0);

    port.cancel(ec);
    VS_REQUIRE(!ec);
    vs_test::pump(io);
    VS_CHECK_EQ(completions, 1);

    // Later data must not resurrect the completed operation.
    mock->inject_read("late");
    vs_test::pump(io);
    VS_CHECK_EQ(completions, 1);
}

VS_TEST_CASE(async_handler_runs_on_its_associated_executor)
{
    asio::io_context port_io;
    asio::io_context handler_io;

    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(port_io.get_executor());
    vs::any_serial_port port(port_io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("executor", ec);
    VS_REQUIRE(!ec);

    char buffer[4] = {};
    bool called = false;
    port.async_read_some(asio::buffer(buffer),
        asio::bind_executor(handler_io.get_executor(),
            [&](boost::system::error_code, std::size_t) { called = true; }));

    mock->inject_read("x");
    vs_test::pump(port_io);
    VS_CHECK(!called); // the completion was posted to the other context

    vs_test::pump(handler_io);
    VS_CHECK(called);
}

VS_TEST_CASE(async_strand_serialises_handlers_across_threads)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("strand", ec);
    VS_REQUIRE(!ec);

    asio::strand<asio::io_context::executor_type> strand =
        asio::make_strand(io);

    const int total = 100;
    std::vector<char> storage(static_cast<std::size_t>(total), '\0');
    std::atomic<int> concurrent{0};
    std::atomic<int> max_concurrent{0};
    std::atomic<int> completed{0};
    std::atomic<int> failures{0};

    for (int i = 0; i < total; ++i)
    {
        port.async_read_some(
            asio::buffer(&storage[static_cast<std::size_t>(i)], 1),
            asio::bind_executor(strand,
                [&](boost::system::error_code error, std::size_t) {
                    if (error)
                        failures.fetch_add(1);
                    const int now = concurrent.fetch_add(1) + 1;
                    int previous = max_concurrent.load();
                    while (now > previous
                           && !max_concurrent.compare_exchange_weak(previous, now))
                    {
                    }
                    // Widen the window: a broken strand would overlap here.
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    concurrent.fetch_sub(1);
                    completed.fetch_add(1);
                }));
    }

    // Complete every pending read at once; 100 handlers are now queued.
    mock->inject_read(std::string(static_cast<std::size_t>(total), 'a'));

    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i)
        threads.emplace_back([&io] { io.run(); });
    for (std::thread& thread : threads)
        thread.join();

    VS_CHECK_EQ(failures.load(), 0);
    VS_CHECK_EQ(completed.load(), total);
    VS_CHECK_EQ(max_concurrent.load(), 1);
}

// ---------------------------------------------------------------------------
// Generic composition on top of the stream
// ---------------------------------------------------------------------------

VS_TEST_CASE(async_composed_read_collects_partial_deliveries)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("compose", ec);
    VS_REQUIRE(!ec);

    char buffer[10] = {};
    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;

    asio::async_read(port, asio::buffer(buffer, sizeof(buffer)),
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });

    mock->inject_read("abc");
    vs_test::pump(io);
    VS_CHECK(!done);
    mock->inject_read("def");
    vs_test::pump(io);
    VS_CHECK(!done);
    mock->inject_read("ghij");
    vs_test::pump(io);

    VS_CHECK(done);
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 10u);
    VS_CHECK_EQ(std::string(buffer, 10), std::string("abcdefghij"));
}

VS_TEST_CASE(async_composed_scatter_write)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("scatter", ec);
    VS_REQUIRE(!ec);

    const std::array<asio::const_buffer, 3> buffers = {
        asio::buffer("AB", 2), asio::buffer("CD", 2), asio::buffer("EF", 2)};

    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;
    asio::async_write(port, buffers,
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });
    vs_test::pump(io);

    VS_CHECK(done);
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 6u);
    VS_CHECK_EQ(mock->written(), std::string("ABCDEF"));
}

VS_TEST_CASE(async_read_until_delimiter)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("lines", ec);
    VS_REQUIRE(!ec);

    asio::streambuf buffer;
    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;
    asio::async_read_until(port, buffer, '\n',
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });

    mock->inject_read("partial");
    vs_test::pump(io);
    VS_CHECK(!done);
    mock->inject_read("-line\nrest");
    vs_test::pump(io);

    VS_CHECK(done);
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 13u);

    std::istream stream(&buffer);
    std::string line;
    std::getline(stream, line);
    VS_CHECK_EQ(line, std::string("partial-line"));
}

namespace {

/// A user-written composed operation: reads exactly `total` bytes.
template <class Stream>
class read_exactly_operation : public asio::coroutine
{
public:
    read_exactly_operation(
        Stream& stream, asio::mutable_buffer buffer, std::size_t total)
        : stream_(stream)
        , buffer_(buffer)
        , remaining_(total)
    {
    }

    template <class Self>
    void operator()(Self& self,
        boost::system::error_code error = boost::system::error_code(),
        std::size_t transferred = 0)
    {
        BOOST_ASIO_CORO_REENTER(this)
        {
            offset_ = 0;
            while (remaining_ > 0)
            {
                BOOST_ASIO_CORO_YIELD
                stream_.async_read_some(
                    asio::buffer(
                        static_cast<char*>(buffer_.data()) + offset_,
                        remaining_),
                    std::move(self));
                if (error)
                {
                    self.complete(error, offset_);
                    return;
                }
                offset_ += transferred;
                remaining_ -= transferred;
            }
            self.complete(boost::system::error_code(), offset_);
        }
    }

private:
    Stream& stream_;
    asio::mutable_buffer buffer_;
    std::size_t remaining_;
    std::size_t offset_ = 0;
};

template <class Stream, class CompletionToken>
auto async_read_exactly(
    Stream& stream,
    asio::mutable_buffer buffer,
    std::size_t total,
    CompletionToken&& token)
{
    return asio::async_compose<
        CompletionToken,
        void(boost::system::error_code, std::size_t)>(
        read_exactly_operation<Stream>(stream, buffer, total), token, stream);
}

} // namespace

VS_TEST_CASE(async_user_composed_operation_on_erased_port)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("user-compose", ec);
    VS_REQUIRE(!ec);

    char buffer[8] = {};
    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;

    async_read_exactly(port, asio::buffer(buffer, sizeof(buffer)), sizeof(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });

    mock->inject_read("123");
    vs_test::pump(io);
    VS_CHECK(!done);
    mock->inject_read("45678");
    vs_test::pump(io);

    VS_CHECK(done);
    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 8u);
    VS_CHECK_EQ(std::string(buffer, 8), std::string("12345678"));
}

VS_TEST_CASE(async_composed_operation_on_tcp_bridge)
{
    asio::io_context io;
    vs_test::tcp_echo_server server(io);
    server.start();

    vs::tcp_bridge_port port(io.get_executor());
    boost::system::error_code ec;
    port.open("127.0.0.1:" + std::to_string(server.port()), ec);
    VS_REQUIRE(!ec);

    char buffer[6] = {};
    boost::system::error_code result;
    std::size_t transferred = 0;
    bool done = false;
    async_read_exactly(port, asio::buffer(buffer, sizeof(buffer)), sizeof(buffer),
        [&](boost::system::error_code error, std::size_t count) {
            result = error;
            transferred = count;
            done = true;
        });

    // The echo server returns the payload; TCP may split it into any number
    // of segments, which is exactly what the composed operation must handle.
    VS_CHECK_EQ(asio::write(port, asio::buffer("abcdef", 6)), 6u);
    VS_CHECK(vs_test::pump_until(io, [&] { return done; },
        std::chrono::milliseconds(2000)));

    VS_CHECK(!result);
    VS_CHECK_EQ(transferred, 6u);
    VS_CHECK_EQ(std::string(buffer, 6), std::string("abcdef"));
}

// ---------------------------------------------------------------------------
// Completion tokens
// ---------------------------------------------------------------------------

VS_TEST_CASE(async_detached_and_redirect_error_tokens)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("tokens", ec);
    VS_REQUIRE(!ec);

    char buffer[8] = {};

    // detached: fire and forget.
    port.async_read_some(asio::buffer(buffer), asio::detached);
    mock->inject_read("d");
    vs_test::pump(io);
    VS_CHECK_EQ(mock->bytes_read(), 1u);

    // redirect_error: the error lands in an lvalue.
    boost::system::error_code captured =
        vs::make_error_code(vs::error::timeout);
    port.async_read_some(asio::buffer(buffer),
        asio::redirect_error(asio::detached, captured));
    mock->inject_read("r");
    vs_test::pump(io);
    VS_CHECK(!captured);

    // ... and an injected fault is captured too.
    port.async_read_some(asio::buffer(buffer),
        asio::redirect_error(asio::detached, captured));
    mock->inject_parity_error();
    vs_test::pump(io);
    VS_CHECK(captured == vs::error::parity_error);
}

VS_TEST_CASE(async_use_future_completes)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("future", ec);
    VS_REQUIRE(!ec);

    mock->inject_read("future");
    char buffer[8] = {};
    std::future<std::size_t> result =
        port.async_read_some(asio::buffer(buffer), asio::use_future);

    VS_CHECK(vs_test::pump_until(io, [&] {
        return result.wait_for(std::chrono::milliseconds(0))
            == std::future_status::ready;
    }));
    VS_CHECK_EQ(result.get(), 6u);
    VS_CHECK_EQ(std::string(buffer, 6), std::string("future"));
}

// ---------------------------------------------------------------------------
// Ordering and work release
// ---------------------------------------------------------------------------

VS_TEST_CASE(async_reads_complete_in_submission_order)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("order", ec);
    VS_REQUIRE(!ec);

    char first[4] = {};
    char second[4] = {};
    int first_completions = 0;
    int second_completions = 0;

    port.async_read_some(asio::buffer(first),
        [&](boost::system::error_code, std::size_t) { ++first_completions; });
    port.async_read_some(asio::buffer(second),
        [&](boost::system::error_code, std::size_t) { ++second_completions; });

    mock->inject_read("a");
    vs_test::pump(io);
    VS_CHECK_EQ(first_completions, 1);
    VS_CHECK_EQ(second_completions, 0);

    mock->inject_read("b");
    vs_test::pump(io);
    VS_CHECK_EQ(second_completions, 1);
}

VS_TEST_CASE(async_context_stops_when_all_work_is_done)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code ec;
    port.open("drain", ec);
    VS_REQUIRE(!ec);

    mock->inject_read("x");
    char buffer[1] = {};
    int completions = 0;
    port.async_read_some(asio::buffer(buffer),
        [&](boost::system::error_code, std::size_t) { ++completions; });

    const std::size_t handled = io.run();
    VS_CHECK_EQ(handled, 1u);
    VS_CHECK_EQ(completions, 1);
    VS_CHECK(io.stopped());
}
