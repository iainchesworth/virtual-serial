//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// A read or write with a timeout starts the backend operation and arms a
// timer. The backend may complete on another thread of the io_context before
// the caller has armed the timer, so the two must not touch the timer
// unsynchronised. ThreadSanitizer reports it when they do. These tests drive
// many such operations on a pool of threads; they pass without a sanitizer
// and are meaningful with -fsanitize=thread.
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

namespace {

struct threaded_port
{
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> guard;
    std::vector<std::thread> threads;
    std::shared_ptr<vs::mock_port> mock;
    vs::any_serial_port port;

    threaded_port()
        : guard(asio::make_work_guard(io))
        , mock(vs::make_mock_port(io.get_executor()))
        , port(io.get_executor(), mock)
    {
        for (int index = 0; index < 4; ++index)
            threads.emplace_back([this] { io.run(); });

        boost::system::error_code ec;
        port.open("threads", ec);
        VS_REQUIRE(!ec);
        port.set_option(vs::read_timeout(std::chrono::seconds(30)), ec);
        VS_REQUIRE(!ec);
        port.set_option(vs::write_timeout(std::chrono::seconds(30)), ec);
        VS_REQUIRE(!ec);
    }

    ~threaded_port()
    {
        guard.reset();
        io.stop();
        for (std::thread& thread : threads)
            thread.join();
    }

    threaded_port(const threaded_port&) = delete;
    threaded_port& operator=(const threaded_port&) = delete;
};

} // namespace

VS_TEST_CASE(timed_reads_complete_on_a_thread_pool)
{
    threaded_port f;
    char buffer[1] = {};
    for (int round = 0; round < 2000; ++round)
    {
        f.mock->inject_read("x");
        std::promise<boost::system::error_code> done;
        f.port.async_read_some(
            asio::buffer(buffer),
            [&done](boost::system::error_code ec, std::size_t) {
                done.set_value(ec);
            });
        std::future<boost::system::error_code> result = done.get_future();
        VS_REQUIRE(
            result.wait_for(std::chrono::seconds(10)) ==
            std::future_status::ready);
        VS_CHECK(!result.get());
    }
}

VS_TEST_CASE(timed_writes_complete_on_a_thread_pool)
{
    threaded_port f;
    const char data[1] = {'y'};
    for (int round = 0; round < 2000; ++round)
    {
        std::promise<boost::system::error_code> done;
        f.port.async_write_some(
            asio::buffer(data),
            [&done](boost::system::error_code ec, std::size_t) {
                done.set_value(ec);
            });
        std::future<boost::system::error_code> result = done.get_future();
        VS_REQUIRE(
            result.wait_for(std::chrono::seconds(10)) ==
            std::future_status::ready);
        VS_CHECK(!result.get());
    }
}
