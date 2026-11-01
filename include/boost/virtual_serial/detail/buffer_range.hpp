//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_DETAIL_BUFFER_RANGE_HPP
#define BOOST_VIRTUAL_SERIAL_DETAIL_BUFFER_RANGE_HPP

#include <boost/virtual_serial/config.hpp>

#include <boost/asio/buffer.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <memory>
#include <type_traits>

namespace boost {
namespace virtual_serial {
namespace detail {

/** A type-erased, owning sequence of buffer descriptors.

    Asio requires that the buffer *sequence* passed to an asynchronous
    operation outlives the operation, even though the memory it refers to
    belongs to the caller.  The type-erasing front end must therefore
    materialise the sequence in a stable form before handing it to a
    backend whose concrete buffer type it does not know.

    `buffer_range` copies descriptors only - never payload bytes - into
    inline storage, so an asynchronous read or write costs no heap
    allocation for the common one-to-eight buffer case.  The resulting
    object models the Asio `MutableBufferSequence` / `ConstBufferSequence`
    concepts, which means it can be passed straight back to
    `boost::asio::buffer_copy` or to a native socket operation.
*/
template <class Buffer>
class buffer_range
{
public:
    using buffer_type = Buffer;
    using value_type = Buffer;
    using const_iterator = const Buffer*;
    using iterator = const Buffer*;

    /// Number of descriptors stored without allocating.
    enum : std::size_t
    {
        inline_capacity = 8
    };

    buffer_range() noexcept = default;

    /// Copies the descriptors of an Asio buffer sequence.
    template <class BufferSequence>
    explicit buffer_range(const BufferSequence& sequence)
    {
        const BufferSequence& seq = sequence;
        const std::size_t count = static_cast<std::size_t>(
            std::distance(
                boost::asio::buffer_sequence_begin(seq),
                boost::asio::buffer_sequence_end(seq)));
        allocate(count);
        std::size_t index = 0;
        for (auto it = boost::asio::buffer_sequence_begin(seq);
             it != boost::asio::buffer_sequence_end(seq);
             ++it, ++index)
        {
            at(index) = Buffer(*it);
        }
    }

    buffer_range(const buffer_range& other)
        : count_(other.count_)
    {
        if (count_ > inline_capacity)
            overflow_.reset(new Buffer[count_ - inline_capacity]);
        for (std::size_t i = 0; i < count_; ++i)
            at(i) = other[i];
    }

    buffer_range(buffer_range&& other) noexcept
        : inline_(other.inline_)
        , overflow_(std::move(other.overflow_))
        , count_(other.count_)
    {
        other.count_ = 0;
    }

    buffer_range& operator=(const buffer_range& other)
    {
        if (this != &other)
        {
            buffer_range copy(other);
            swap(copy);
        }
        return *this;
    }

    buffer_range& operator=(buffer_range&& other) noexcept
    {
        if (this != &other)
        {
            inline_ = other.inline_;
            overflow_ = std::move(other.overflow_);
            count_ = other.count_;
            other.count_ = 0;
        }
        return *this;
    }

    void swap(buffer_range& other) noexcept
    {
        inline_.swap(other.inline_);
        overflow_.swap(other.overflow_);
        std::swap(count_, other.count_);
    }

    std::size_t size() const noexcept
    {
        return count_;
    }

    bool empty() const noexcept
    {
        return count_ == 0;
    }

    Buffer const& operator[](std::size_t index) const noexcept
    {
        return at(index);
    }

    const_iterator begin() const noexcept
    {
        return count_ == 0 ? nullptr : &at(0);
    }

    const_iterator end() const noexcept
    {
        return count_ == 0 ? nullptr : &at(0) + count_;
    }

    /// Sum of the capacities of every descriptor.
    std::size_t total_size() const noexcept
    {
        std::size_t total = 0;
        for (std::size_t i = 0; i < count_; ++i)
            total += at(i).size();
        return total;
    }

    /// Total number of bytes referenced, saturating at `limit`.
    std::size_t total_size_up_to(std::size_t limit) const noexcept
    {
        std::size_t total = 0;
        for (std::size_t i = 0; i < count_ && total < limit; ++i)
        {
            const std::size_t size = at(i).size();
            total += size < (limit - total) ? size : (limit - total);
        }
        return total;
    }

private:
    void allocate(std::size_t count)
    {
        count_ = count;
        if (count_ > inline_capacity)
            overflow_.reset(new Buffer[count_ - inline_capacity]);
    }

    Buffer& at(std::size_t index) noexcept
    {
        return index < inline_capacity ? inline_[index]
                                       : overflow_[index - inline_capacity];
    }

    Buffer const& at(std::size_t index) const noexcept
    {
        return index < inline_capacity ? inline_[index]
                                       : overflow_[index - inline_capacity];
    }

    std::array<Buffer, inline_capacity> inline_{};
    std::unique_ptr<Buffer[]> overflow_;
    std::size_t count_ = 0;
};

using mutable_buffer_range = buffer_range<boost::asio::mutable_buffer>;
using const_buffer_range = buffer_range<boost::asio::const_buffer>;

/** Scatters up to `count` bytes from `source` into `destination`.

    The bytes that do not fit are left untouched.  Returns the number of
    bytes written, which is `min(count, destination.total_size())`.
*/
inline std::size_t scatter(
    const unsigned char* source,
    std::size_t count,
    mutable_buffer_range& destination) noexcept
{
    std::size_t remaining = count;
    std::size_t offset = 0;
    for (std::size_t i = 0; i < destination.size() && remaining > 0; ++i)
    {
        const std::size_t chunk =
            destination[i].size() < remaining ? destination[i].size() : remaining;
        if (chunk > 0)
            std::memcpy(destination[i].data(), source + offset, chunk);
        offset += chunk;
        remaining -= chunk;
    }
    return offset;
}

/** Copies the contents of any buffer range into `destination`.

    Returns the number of bytes copied.  This is the gather counterpart of
    #scatter and is used by the mock and RFC 2217 backends to capture
    written bytes.
*/
template <class BufferRange>
inline std::size_t gather(
    const BufferRange& source,
    unsigned char* destination,
    std::size_t capacity) noexcept
{
    std::size_t written = 0;
    for (std::size_t i = 0; i < source.size() && written < capacity; ++i)
    {
        const std::size_t chunk = source[i].size() < (capacity - written)
                                      ? source[i].size()
                                      : (capacity - written);
        if (chunk > 0)
            std::memcpy(destination + written, source[i].data(), chunk);
        written += chunk;
    }
    return written;
}

} // namespace detail
} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_DETAIL_BUFFER_RANGE_HPP
