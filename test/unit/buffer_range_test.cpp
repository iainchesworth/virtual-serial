//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#include "../detail/test_framework.hpp"

#include <boost/virtual_serial/detail/buffer_range.hpp>

#include <boost/asio/buffer.hpp>

#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace vs = boost::virtual_serial;
namespace asio = boost::asio;

VS_TEST_CASE(buffer_range_single_buffer)
{
    char storage[16] = {};
    const asio::mutable_buffer buffer(storage, sizeof(storage));

    const vs::detail::mutable_buffer_range range(buffer);
    VS_CHECK_EQ(range.size(), 1u);
    VS_CHECK(!range.empty());
    VS_CHECK_EQ(range.total_size(), sizeof(storage));
    VS_CHECK_EQ(range[0].data(), static_cast<void*>(storage));
    VS_CHECK_EQ(range[0].size(), sizeof(storage));
}

VS_TEST_CASE(buffer_range_multiple_buffers)
{
    char first[4] = {};
    char second[8] = {};
    char third[2] = {};

    std::array<asio::mutable_buffer, 3> sequence = {
        asio::buffer(first), asio::buffer(second), asio::buffer(third)};

    const vs::detail::mutable_buffer_range range(sequence);
    VS_CHECK_EQ(range.size(), 3u);
    VS_CHECK_EQ(range.total_size(), 14u);
    VS_CHECK_EQ(range[1].size(), 8u);
    VS_CHECK_EQ(range[2].size(), 2u);
}

VS_TEST_CASE(buffer_range_spills_to_heap)
{
    char storage[12] = {};
    std::vector<asio::mutable_buffer> sequence;
    for (int i = 0; i < 12; ++i)
        sequence.push_back(asio::buffer(storage + i, 1));

    const vs::detail::mutable_buffer_range range(sequence);
    VS_CHECK_EQ(range.size(), 12u);
    VS_CHECK_EQ(range.total_size(), 12u);
    for (std::size_t i = 0; i < range.size(); ++i)
        VS_CHECK_EQ(range[i].data(), static_cast<void*>(storage + i));

    // Copies of the range are deep copies: the descriptors survive, and
    // the overflow storage is independent.
    vs::detail::mutable_buffer_range copy(range);
    VS_CHECK_EQ(copy.size(), 12u);
    for (std::size_t i = 0; i < copy.size(); ++i)
        VS_CHECK_EQ(copy[i].data(), range[i].data());

    vs::detail::mutable_buffer_range moved(std::move(copy));
    VS_CHECK_EQ(moved.size(), 12u);
    VS_CHECK_EQ(copy.size(), 0u);
}

VS_TEST_CASE(buffer_range_empty)
{
    const vs::detail::mutable_buffer_range range;
    VS_CHECK_EQ(range.size(), 0u);
    VS_CHECK(range.empty());
    VS_CHECK_EQ(range.begin(), range.end());
    VS_CHECK_EQ(range.total_size(), 0u);
}

VS_TEST_CASE(buffer_range_accepts_const_sequence_for_writes)
{
    const std::string text = "hello";
    std::array<asio::const_buffer, 2> sequence = {
        asio::buffer(text.data(), 2), asio::buffer(text.data() + 2, 3)};

    const vs::detail::const_buffer_range range(sequence);
    VS_CHECK_EQ(range.size(), 2u);
    VS_CHECK_EQ(range.total_size(), 5u);
}

VS_TEST_CASE(buffer_range_models_asia_buffer_sequence)
{
    char destination[8] = {};
    std::array<asio::mutable_buffer, 2> sequence = {
        asio::buffer(destination, 3), asio::buffer(destination + 3, 5)};

    const vs::detail::mutable_buffer_range range(sequence);
    const std::string source = "abcdefgh";
    const std::size_t copied = asio::buffer_copy(range, asio::buffer(source));
    VS_CHECK_EQ(copied, 8u);
    VS_CHECK_EQ(std::string(destination, 8), source);
}

VS_TEST_CASE(scatter_and_gather_round_trip)
{
    char first[3] = {};
    char second[5] = {};
    std::array<asio::mutable_buffer, 2> sequence = {
        asio::buffer(first), asio::buffer(second)};

    vs::detail::mutable_buffer_range range(sequence);
    const std::string payload = "abcdefgh";
    VS_CHECK_EQ(
        vs::detail::scatter(
            reinterpret_cast<const unsigned char*>(payload.data()),
            payload.size(),
            range),
        8u);
    VS_CHECK_EQ(std::string(first, 3), std::string("abc"));
    VS_CHECK_EQ(std::string(second, 5), std::string("defgh"));

    unsigned char gathered[16] = {};
    VS_CHECK_EQ(vs::detail::gather(range, gathered, sizeof(gathered)), 8u);
    VS_CHECK_EQ(
        std::string(reinterpret_cast<char*>(gathered), 8), payload);

    // Gathering into a smaller buffer truncates.
    VS_CHECK_EQ(vs::detail::gather(range, gathered, 4), 4u);
    VS_CHECK_EQ(std::string(reinterpret_cast<char*>(gathered), 4), std::string("abcd"));
}

VS_TEST_CASE(scatter_respects_capacity)
{
    char storage[2] = {};
    vs::detail::mutable_buffer_range range(asio::buffer(storage));

    const unsigned char source[4] = {1, 2, 3, 4};
    VS_CHECK_EQ(vs::detail::scatter(source, sizeof(source), range), 2u);
    VS_CHECK_EQ(static_cast<int>(storage[0]), 1);
    VS_CHECK_EQ(static_cast<int>(storage[1]), 2);
}

VS_TEST_CASE(buffer_range_total_size_up_to)
{
    char first[4] = {};
    char second[4] = {};
    std::array<asio::mutable_buffer, 2> sequence = {
        asio::buffer(first), asio::buffer(second)};

    const vs::detail::mutable_buffer_range range(sequence);
    VS_CHECK_EQ(range.total_size_up_to(3), 3u);
    VS_CHECK_EQ(range.total_size_up_to(4), 4u);
    VS_CHECK_EQ(range.total_size_up_to(100), 8u);
}
