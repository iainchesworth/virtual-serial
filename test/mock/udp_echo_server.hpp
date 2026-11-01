//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_MOCK_UDP_ECHO_SERVER_HPP
#define BOOST_VIRTUAL_SERIAL_TEST_MOCK_UDP_ECHO_SERVER_HPP

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstddef>
#include <deque>
#include <memory>
#include <string>

namespace vs_test {

/// A loopback UDP peer that records datagrams and echoes them back.
class udp_echo_server
{
public:
    explicit udp_echo_server(boost::asio::io_context& io)
        : socket_(io)
    {
    }

    void start()
    {
        namespace asio = boost::asio;
        socket_.open(asio::ip::udp::v4());
        socket_.bind(asio::ip::udp::endpoint(asio::ip::address_v4::loopback(), 0));
        port_ = socket_.local_endpoint().port();
        receive();
    }

    unsigned short port() const noexcept
    {
        return port_;
    }

    const std::string& received() const noexcept
    {
        return received_;
    }

    bool has_peer() const noexcept
    {
        return has_peer_;
    }

    void send(const std::string& text)
    {
        if (!has_peer_)
            return;
        enqueue(text, peer_);
    }

private:
    struct datagram
    {
        std::shared_ptr<std::string> payload;
        boost::asio::ip::udp::endpoint destination;
    };

    void receive()
    {
        socket_.async_receive_from(
            boost::asio::buffer(buffer_),
            peer_,
            [this](boost::system::error_code ec, std::size_t received) {
                if (ec)
                    return;
                has_peer_ = true;
                received_.append(buffer_.data(), received);
                enqueue(std::string(buffer_.data(), received), peer_);
                receive();
            });
    }

    void enqueue(
        const std::string& text, const boost::asio::ip::udp::endpoint& to)
    {
        datagram item;
        item.payload = std::make_shared<std::string>(text);
        item.destination = to;
        outbox_.push_back(item);
        if (!sending_)
            send_next();
    }

    void send_next()
    {
        if (outbox_.empty())
        {
            sending_ = false;
            return;
        }
        sending_ = true;
        const datagram item = outbox_.front();
        socket_.async_send_to(
            boost::asio::buffer(*item.payload),
            item.destination,
            [this, item](boost::system::error_code ec, std::size_t) {
                if (ec)
                {
                    sending_ = false;
                    outbox_.clear();
                    return;
                }
                outbox_.pop_front();
                send_next();
            });
    }

    boost::asio::ip::udp::socket socket_;
    boost::asio::ip::udp::endpoint peer_;
    std::deque<datagram> outbox_;
    std::array<char, 1500> buffer_{};
    std::string received_;
    unsigned short port_ = 0;
    bool has_peer_ = false;
    bool sending_ = false;
};

} // namespace vs_test

#endif // BOOST_VIRTUAL_SERIAL_TEST_MOCK_UDP_ECHO_SERVER_HPP
