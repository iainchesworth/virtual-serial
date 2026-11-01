//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// Performance suite: throughput, operation rate, latency, type-erasure cost,
// codec cost and allocations per asynchronous operation.
//
// Results are written as JSON and rendered by tools/perf_report.py.  Run
//
//     virtual_serial_perf --quick --output perf.json
//
// for the short version used in CI, or without --quick for the full run.
//

#include "benchmark.hpp"

#include <boost/virtual_serial.hpp>
#include <boost/virtual_serial/detail/telnet.hpp>
#include <boost/version.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

// ---------------------------------------------------------------------------
// Allocation accounting
//
// The replaceable global allocation functions are overridden so the suite can
// report how many allocations one asynchronous operation performs, including
// the completion dispatch.  Counting is only enabled around the measurement
// window.
// ---------------------------------------------------------------------------

namespace {

std::atomic<bool> g_counting{false};
std::atomic<unsigned long long> g_allocations{0};
std::atomic<unsigned long long> g_bytes{0};

void count_allocation(std::size_t size) noexcept
{
    if (g_counting.load(std::memory_order_relaxed))
    {
        g_allocations.fetch_add(1, std::memory_order_relaxed);
        g_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}

void* allocate(std::size_t size)
{
    count_allocation(size);
    void* pointer = std::malloc(size ? size : 1);
    if (!pointer)
        throw std::bad_alloc();
    return pointer;
}

} // namespace

void* operator new(std::size_t size)
{
    return allocate(size);
}

void* operator new[](std::size_t size)
{
    return allocate(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    count_allocation(size);
    return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    count_allocation(size);
    return std::malloc(size ? size : 1);
}

void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept
{
    std::free(pointer);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

double megabytes_per_second(std::size_t bytes, double seconds)
{
    if (seconds <= 0.0)
        return 0.0;
    return static_cast<double>(bytes) / seconds / 1.0e6;
}


/// Accepts one TCP connection and then reads everything it is sent, optionally
/// echoing it back.  It runs on its own io_context thread so the measured
/// client can use the blocking API on the main thread.
class loop_server
{
public:
    explicit loop_server(bool echo)
        : echo_(echo)
    {
    }

    ~loop_server()
    {
        stop();
    }

    unsigned short start()
    {
        const asio::ip::tcp::endpoint endpoint(
            asio::ip::address_v4::loopback(), 0);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        const unsigned short port = acceptor_.local_endpoint().port();
        accept();
        thread_ = std::thread([this] { io_.run(); });
        return port;
    }

    void stop()
    {
        io_.stop();
        if (thread_.joinable())
            thread_.join();
        boost::system::error_code ignored;
        acceptor_.close(ignored);
        if (peer_)
            peer_->close(ignored);
    }

private:
    void accept()
    {
        peer_ = std::make_shared<asio::ip::tcp::socket>(io_);
        acceptor_.async_accept(*peer_,
            [this](const boost::system::error_code& error) {
                if (!error)
                    read_next();
            });
    }

    void read_next()
    {
        peer_->async_read_some(asio::buffer(scratch_),
            [this](const boost::system::error_code& error, std::size_t count) {
                if (error)
                    return;
                if (!echo_)
                {
                    read_next();
                    return;
                }
                asio::async_write(*peer_,
                    asio::buffer(scratch_.data(), count),
                    [this](const boost::system::error_code& write_error,
                        std::size_t) {
                        if (!write_error)
                            read_next();
                    });
            });
    }

    asio::io_context io_;
    asio::ip::tcp::acceptor acceptor_{io_};
    std::shared_ptr<asio::ip::tcp::socket> peer_;
    std::array<char, 1u << 16> scratch_{};
    std::thread thread_;
    bool echo_;
};


// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

void run_mock_sync_write(bool quick, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t chunk_size = 4096;
    const std::size_t total = quick ? (1u << 20) : (16u << 20);
    std::vector<char> chunk(chunk_size, 'x');
    std::vector<double> samples;

    for (int repetition = 0; repetition < 3; ++repetition)
    {
        mock->clear_written();
        const perf::clock_type::time_point start = perf::clock_type::now();
        std::size_t written = 0;
        while (written < total)
        {
            const std::size_t count =
                port.write_some(asio::buffer(chunk), error);
            if (error)
                throw std::runtime_error("mock write failed");
            written += count;
        }
        samples.push_back(
            megabytes_per_second(total, perf::seconds_since(start)));
    }

    suite.add(perf::result{"mock_sync_write", "MB/s", "higher",
        perf::median(samples), total, samples,
        "4 KiB chunks, typed front end"});
}

void run_mock_sync_read(bool quick, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t chunk_size = 4096;
    const std::size_t total = quick ? (1u << 20) : (16u << 20);
    std::vector<char> chunk(chunk_size, 'r');
    std::vector<double> samples;

    for (int repetition = 0; repetition < 3; ++repetition)
    {
        mock->inject_read(std::string(total, 'r'));
        const perf::clock_type::time_point start = perf::clock_type::now();
        std::size_t read = 0;
        while (read < total)
        {
            const std::size_t count = port.read_some(asio::buffer(chunk), error);
            if (error)
                throw std::runtime_error("mock read failed");
            read += count;
        }
        samples.push_back(
            megabytes_per_second(total, perf::seconds_since(start)));
    }

    suite.add(perf::result{"mock_sync_read", "MB/s", "higher",
        perf::median(samples), total, samples, "4 KiB chunks"});
}

void run_erasure_cost(bool quick, perf::suite& suite)
{
    const std::size_t chunk_size = 4096;
    const std::size_t total = quick ? (1u << 20) : (8u << 20);
    std::vector<char> chunk(chunk_size, 'x');
    boost::system::error_code error;

    std::vector<double> typed_samples;
    {
        asio::io_context io;
        const std::shared_ptr<vs::mock_port> mock =
            vs::make_mock_port(io.get_executor());
        vs::basic_serial_port<vs::mock_port> port(io.get_executor(), mock);
        port.open("bench", error);
        require(!error, "could not open the mock port");
        for (int repetition = 0; repetition < 3; ++repetition)
        {
            mock->clear_written();
            const perf::clock_type::time_point start =
                perf::clock_type::now();
            std::size_t written = 0;
            while (written < total)
                written += port.write_some(asio::buffer(chunk), error);
            typed_samples.push_back(
                megabytes_per_second(total, perf::seconds_since(start)));
        }
    }

    std::vector<double> erased_samples;
    {
        asio::io_context io;
        const std::shared_ptr<vs::mock_port> mock =
            vs::make_mock_port(io.get_executor());
        vs::any_serial_port port(io.get_executor(), mock);
        port.open("bench", error);
        require(!error, "could not open the mock port");
        for (int repetition = 0; repetition < 3; ++repetition)
        {
            mock->clear_written();
            const perf::clock_type::time_point start =
                perf::clock_type::now();
            std::size_t written = 0;
            while (written < total)
                written += port.write_some(asio::buffer(chunk), error);
            erased_samples.push_back(
                megabytes_per_second(total, perf::seconds_since(start)));
        }
    }

    const double typed = perf::median(typed_samples);
    const double erased = perf::median(erased_samples);
    suite.add(perf::result{"erased_sync_write", "MB/s", "higher", erased,
        total, erased_samples,
        "same workload through any_serial_port"});
    suite.add(perf::result{"erasure_overhead", "x", "lower",
        erased > 0.0 ? typed / erased : 0.0, total, {},
        "typed throughput divided by erased throughput"});
}

void run_mock_async_read_ops(bool quick, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t operations = quick ? 5000u : 50000u;
    std::vector<char> storage(operations, 'r');
    std::vector<double> samples;

    for (int repetition = 0; repetition < 3; ++repetition)
    {
        mock->inject_read(std::string(operations, 'r'));
        std::size_t completed = 0;
        const perf::clock_type::time_point start = perf::clock_type::now();
        for (std::size_t index = 0; index < operations; ++index)
        {
            port.async_read_some(asio::buffer(&storage[index], 1),
                [&completed](boost::system::error_code, std::size_t) {
                    ++completed;
                });
        }
        io.restart();
        io.run();
        require(completed == operations, "not every read completed");
        samples.push_back(
            static_cast<double>(operations) / perf::seconds_since(start));
    }

    suite.add(perf::result{"mock_async_read_ops", "ops/s", "higher",
        perf::median(samples), operations, samples,
        "one byte per operation, one thread"});
}

void run_mock_async_write_ops(bool quick, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t operations = quick ? 5000u : 50000u;
    const std::array<char, 32> payload{};
    std::vector<double> samples;

    for (int repetition = 0; repetition < 3; ++repetition)
    {
        mock->clear_written();
        std::size_t completed = 0;
        const perf::clock_type::time_point start = perf::clock_type::now();
        for (std::size_t index = 0; index < operations; ++index)
        {
            port.async_write_some(asio::buffer(payload),
                [&completed](boost::system::error_code, std::size_t) {
                    ++completed;
                });
        }
        io.restart();
        io.run();
        require(completed == operations, "not every write completed");
        samples.push_back(
            static_cast<double>(operations) / perf::seconds_since(start));
    }

    suite.add(perf::result{"mock_async_write_ops", "ops/s", "higher",
        perf::median(samples), operations, samples,
        "32 byte writes, one thread"});
}

void run_mock_async_latency(bool quick, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t rounds = quick ? 200u : 2000u;
    char byte = 0;
    std::vector<double> samples;

    for (std::size_t round = 0; round < rounds; ++round)
    {
        io.restart();
        double micros = 0.0;
        const perf::clock_type::time_point start = perf::clock_type::now();
        port.async_read_some(asio::buffer(&byte, 1),
            [&micros, start](boost::system::error_code, std::size_t) {
                micros = perf::seconds_since(start) * 1.0e6;
            });
        mock->inject_read("x");
        io.run();
        samples.push_back(micros);
    }

    suite.add(perf::result{"mock_async_latency", "us", "lower",
        perf::median(samples), rounds, samples,
        "initiation to handler, one byte"});
}

void run_tcp_throughput(bool quick, perf::suite& suite)
{
    loop_server server(false);
    const unsigned short port_number = server.start();

    asio::io_context io;
    vs::tcp_bridge_port client(io.get_executor());
    boost::system::error_code connect_error;
    client.open("127.0.0.1:" + std::to_string(port_number), connect_error);
    require(!connect_error, "could not connect to the loop server");

    const std::size_t chunk_size = 16 * 1024;
    const std::size_t total = quick ? (4u << 20) : (64u << 20);
    std::vector<char> chunk(chunk_size, 't');
    std::vector<double> samples;

    for (int repetition = 0; repetition < 3; ++repetition)
    {
        const perf::clock_type::time_point start = perf::clock_type::now();
        std::size_t written = 0;
        while (written < total)
        {
            boost::system::error_code error;
            const std::size_t count =
                client.write_some(asio::buffer(chunk), error);
            require(!error, "TCP write failed");
            written += count;
        }
        samples.push_back(
            megabytes_per_second(total, perf::seconds_since(start)));
    }

    suite.add(perf::result{"tcp_sync_write", "MB/s", "higher",
        perf::median(samples), total, samples,
        "IPv4 loopback, 16 KiB chunks"});
}

void run_tcp_pingpong(bool quick, perf::suite& suite)
{
    loop_server server(true);
    const unsigned short port_number = server.start();

    asio::io_context io;
    vs::tcp_bridge_port client(io.get_executor());
    boost::system::error_code connect_error;
    client.open("127.0.0.1:" + std::to_string(port_number), connect_error);
    require(!connect_error, "could not connect to the loop server");

    const std::size_t payload_size = 32;
    const std::size_t rounds = quick ? 200u : 2000u;
    std::vector<char> payload(payload_size, 'p');
    std::vector<char> reply(payload_size, '\0');
    std::vector<double> samples;

    for (std::size_t round = 0; round < rounds; ++round)
    {
        const perf::clock_type::time_point start = perf::clock_type::now();

        std::size_t sent = 0;
        while (sent < payload_size)
        {
            boost::system::error_code error;
            const std::size_t count = client.write_some(
                asio::buffer(payload.data() + sent, payload_size - sent),
                error);
            require(!error, "ping write failed");
            sent += count;
        }

        std::size_t received = 0;
        while (received < payload_size)
        {
            boost::system::error_code error;
            const std::size_t count = client.read_some(
                asio::buffer(reply.data() + received,
                    payload_size - received),
                error);
            require(!error, "ping read failed");
            received += count;
        }

        samples.push_back(perf::seconds_since(start) * 1.0e6);
    }

    const double round_trip = perf::median(samples);
    suite.add(perf::result{"tcp_pingpong", "us", "lower", round_trip, rounds,
        samples, "32 byte round trip over loopback"});
    suite.add(perf::result{"tcp_pingpong_rps", "ops/s", "higher",
        round_trip > 0.0 ? 1.0e6 / round_trip : 0.0, rounds, {},
        "round trips per second derived from the median"});
}

void run_telnet_codec(bool quick, perf::suite& suite)
{
    const std::size_t block_size = 4096;
    const std::size_t repetitions = quick ? 1024u : 4096u;
    std::vector<unsigned char> block(block_size);
    for (std::size_t index = 0; index < block_size; ++index)
    {
        block[index] = (index % 10 == 0)
            ? vs::detail::telnet::command_iac
            : static_cast<unsigned char>(index & 0x7f);
    }

    const std::size_t total = block_size * repetitions;
    std::vector<unsigned char> encoded;
    encoded.reserve(block_size * 2);

    {
        const perf::clock_type::time_point start = perf::clock_type::now();
        for (std::size_t repetition = 0; repetition < repetitions;
             ++repetition)
        {
            encoded.clear();
            vs::detail::telnet::append_escaped(
                encoded, block.data(), block.size());
        }
        suite.add(perf::result{"telnet_encode", "MB/s", "higher",
            megabytes_per_second(total, perf::seconds_since(start)),
            repetitions, {}, "IAC escaping, 10% escaped bytes"});
    }

    {
        // Feed the escaped stream so the decoder sees real IAC quoting; the
        // reported throughput is decoded payload per second.
        std::vector<unsigned char> escaped;
        escaped.reserve(block_size * 2);
        vs::detail::telnet::append_escaped(escaped, block.data(), block.size());

        std::size_t received = 0;
        vs::detail::telnet::decoder decoder;
        decoder.set_data_sink(
            [&received](const unsigned char*, std::size_t count) {
                received += count;
            });
        const perf::clock_type::time_point start = perf::clock_type::now();
        for (std::size_t repetition = 0; repetition < repetitions;
             ++repetition)
            decoder.feed(escaped.data(), escaped.size());
        const double seconds = perf::seconds_since(start);
        require(received == total, "the decoder lost data");
        suite.add(perf::result{"telnet_decode", "MB/s", "higher",
            megabytes_per_second(total, seconds), repetitions, {},
            "incremental decoder, IAC quoting"});
    }
}

void run_async_allocations(bool /*quick*/, perf::suite& suite)
{
    asio::io_context io;
    const std::shared_ptr<vs::mock_port> mock =
        vs::make_mock_port(io.get_executor());
    vs::any_serial_port port(io.get_executor(), mock);
    boost::system::error_code error;
    port.open("bench", error);
    require(!error, "could not open the mock port");

    const std::size_t operations = 1000;
    std::vector<char> storage(operations, 'a');

    // Warm up allocator and dispatch paths outside the counting window.
    mock->inject_read("w");
    port.async_read_some(asio::buffer(&storage[0], 1),
        [](boost::system::error_code, std::size_t) {});
    io.restart();
    io.run();

    mock->inject_read(std::string(operations, 'a'));
    g_allocations.store(0, std::memory_order_relaxed);
    g_bytes.store(0, std::memory_order_relaxed);
    g_counting.store(true, std::memory_order_relaxed);

    for (std::size_t index = 0; index < operations; ++index)
    {
        port.async_read_some(asio::buffer(&storage[index], 1),
            [](boost::system::error_code, std::size_t) {});
    }
    io.restart();
    io.run();

    g_counting.store(false, std::memory_order_relaxed);

    const double allocations =
        static_cast<double>(g_allocations.load(std::memory_order_relaxed))
        / static_cast<double>(operations);
    const double bytes =
        static_cast<double>(g_bytes.load(std::memory_order_relaxed))
        / static_cast<double>(operations);

    suite.add(perf::result{"async_allocations_per_op", "allocs/op", "lower",
        allocations, operations, {},
        "allocation calls per erased read, including dispatch"});
    suite.add(perf::result{"async_bytes_per_op", "B/op", "lower", bytes,
        operations, {}, "bytes allocated per erased read"});
}

// ---------------------------------------------------------------------------
// Driver
// ---------------------------------------------------------------------------

struct benchmark_entry
{
    const char* name;
    void (*run)(bool, perf::suite&);
};

const benchmark_entry benchmarks[] = {
    {"mock_sync_write", &run_mock_sync_write},
    {"mock_sync_read", &run_mock_sync_read},
    {"erasure_cost", &run_erasure_cost},
    {"mock_async_read_ops", &run_mock_async_read_ops},
    {"mock_async_write_ops", &run_mock_async_write_ops},
    {"mock_async_latency", &run_mock_async_latency},
    {"tcp_throughput", &run_tcp_throughput},
    {"tcp_pingpong", &run_tcp_pingpong},
    {"telnet_codec", &run_telnet_codec},
    {"async_allocations", &run_async_allocations},
};

perf::environment current_environment()
{
    perf::environment env;
#if defined(_WIN32)
    env.os = "windows";
#elif defined(__APPLE__)
    env.os = "macos";
#elif defined(__linux__)
    env.os = "linux";
#else
    env.os = "unknown";
#endif

#if defined(_MSC_VER)
    env.compiler = "msvc " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    env.compiler = "clang " + std::to_string(__clang_major__) + "."
        + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
    env.compiler = "gcc " + std::to_string(__GNUC__) + "."
        + std::to_string(__GNUC_MINOR__);
#else
    env.compiler = "unknown";
#endif

#if defined(_MSVC_LANG)
    env.cxx_standard = std::to_string(_MSVC_LANG);
#else
    env.cxx_standard = std::to_string(__cplusplus);
#endif
    env.boost_version = std::to_string(BOOST_VERSION / 100000) + "."
        + std::to_string(BOOST_VERSION / 100 % 1000) + "."
        + std::to_string(BOOST_VERSION % 100);
#if defined(NDEBUG)
    env.build_type = "Release";
#else
    env.build_type = "Debug";
#endif
    return env;
}

void usage(std::ostream& out)
{
    out << "usage: virtual_serial_perf [--quick] [--filter NAME] "
           "[--list] [--output PATH]\n";
}

} // namespace

int main(int argc, char** argv)
{
    bool quick = false;
    bool list = false;
    std::string filter;
    std::string output = "perf.json";

    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--quick")
            quick = true;
        else if (argument == "--list")
            list = true;
        else if (argument == "--filter" && index + 1 < argc)
            filter = argv[++index];
        else if (argument == "--output" && index + 1 < argc)
            output = argv[++index];
        else if (argument == "--help" || argument == "-h")
        {
            usage(std::cout);
            return 0;
        }
        else
        {
            std::cerr << "unknown argument: " << argument << '\n';
            usage(std::cerr);
            return 2;
        }
    }

    if (list)
    {
        for (const benchmark_entry& entry : benchmarks)
            std::cout << entry.name << '\n';
        return 0;
    }

    perf::suite suite(quick);
    try
    {
        for (const benchmark_entry& entry : benchmarks)
        {
            if (!filter.empty() && std::string(entry.name).find(filter)
                    == std::string::npos)
                continue;
            entry.run(quick, suite);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }

    if (suite.results().empty())
    {
        std::cerr << "no benchmarks matched the filter\n";
        return 2;
    }

    suite.print_table(std::cout);
    suite.write_json(current_environment(), output);
    std::cout << "\nwrote " << output << '\n';
    return 0;
}
