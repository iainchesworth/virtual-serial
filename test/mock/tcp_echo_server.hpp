//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_MOCK_TCP_ECHO_SERVER_HPP
#define BOOST_VIRTUAL_SERIAL_TEST_MOCK_TCP_ECHO_SERVER_HPP

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>

namespace vs_test {

/** A loopback TCP peer that records what it receives and echoes it back.

    Writes are queued and owned, because Asio forbids overlapping
    asynchronous writes on one socket: an echo triggered by a read can
    otherwise race a write started by the test, which is exactly the kind of
    fixture bug this helper must not have.
*/
class tcp_echo_server
{
public:
    explicit tcp_echo_server(boost::asio::io_context& io)
        : io_(io)
        , acceptor_(io)
    {
    }

    void start()
    {
        namespace asio = boost::asio;
        const asio::ip::tcp::endpoint endpoint(
            asio::ip::address_v4::loopback(), 0);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        port_ = acceptor_.local_endpoint().port();
        accept();
    }

    unsigned short port() const noexcept
    {
        return port_;
    }

    std::size_t bytes_received() const noexcept
    {
        return received_.size();
    }

    const std::string& received() const noexcept
    {
        return received_;
    }

    std::size_t connection_count() const noexcept
    {
        return connection_count_;
    }

    bool connected() const noexcept
    {
        return peer_ && peer_->is_open();
    }

    /// Queues `text` for transmission to the client.
    void send(const std::string& text)
    {
        enqueue(text);
    }

    void close_peer()
    {
        if (!peer_)
            return;
        boost::system::error_code ignored;
        peer_->close(ignored);
        outbox_.clear();
    }

private:
    void accept()
    {
        peer_ = std::make_shared<boost::asio::ip::tcp::socket>(io_);
        acceptor_.async_accept(
            *peer_,
            [this](boost::system::error_code ec) {
                if (ec)
                    return;
                ++connection_count_;
                read();
            });
    }

    void read()
    {
        peer_->async_read_some(
            boost::asio::buffer(buffer_),
            [this](boost::system::error_code ec, std::size_t received) {
                if (ec)
                    return;
                received_.append(buffer_.data(), received);
                enqueue(std::string(buffer_.data(), received));
                read();
            });
    }

    void enqueue(const std::string& text)
    {
        if (!connected())
            return;
        outbox_.push_back(std::make_shared<std::string>(text));
        if (!writing_)
            write_next();
    }

    void write_next()
    {
        if (outbox_.empty())
        {
            writing_ = false;
            return;
        }
        writing_ = true;
        const std::shared_ptr<std::string> payload = outbox_.front();
        boost::asio::async_write(
            *peer_,
            boost::asio::buffer(*payload),
            [this, payload](boost::system::error_code ec, std::size_t) {
                if (ec)
                {
                    writing_ = false;
                    outbox_.clear();
                    return;
                }
                outbox_.pop_front();
                write_next();
            });
    }

    boost::asio::io_context& io_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<boost::asio::ip::tcp::socket> peer_;
    std::deque<std::shared_ptr<std::string> > outbox_;
    std::array<char, 1024> buffer_{};
    std::string received_;
    unsigned short port_ = 0;
    std::size_t connection_count_ = 0;
    bool writing_ = false;
};

} // namespace vs_test

#endif // BOOST_VIRTUAL_SERIAL_TEST_MOCK_TCP_ECHO_SERVER_HPP
