//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_TEST_MOCK_RFC2217_TEST_SERVER_HPP
#define BOOST_VIRTUAL_SERIAL_TEST_MOCK_RFC2217_TEST_SERVER_HPP

#include <boost/virtual_serial/detail/telnet.hpp>

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
#include <utility>
#include <vector>

namespace vs_test {

namespace telnet = boost::virtual_serial::detail::telnet;

/** A loopback RFC 2217 access server.

    It speaks just enough of the protocol to exercise the client: it
    answers the Telnet option negotiation, records every com port command
    it receives and acknowledges it, unescapes the data stream and can
    inject data, line state notifications and signature requests.

    The server reuses the library's own incremental decoder, which keeps
    the fixture honest about quoting and command framing.
*/
class rfc2217_test_server
{
public:
    using decoder = boost::virtual_serial::detail::telnet::decoder;

    explicit rfc2217_test_server(boost::asio::io_context& io)
        : io_(io)
        , acceptor_(io)
    {
        decoder_.set_data_sink(
            [this](const unsigned char* data, std::size_t size) {
                received_.append(reinterpret_cast<const char*>(data), size);
            });
        decoder_.set_negotiation_sink(
            [this](unsigned char command, unsigned char option) {
                handle_negotiation(command, option);
            });
        decoder_.set_subnegotiation_sink(
            [this](
                unsigned char option,
                const unsigned char* payload,
                std::size_t size) {
                handle_subnegotiation(option, payload, size);
            });
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

    bool negotiated() const noexcept
    {
        return negotiated_;
    }

    bool connected() const noexcept
    {
        return peer_ && peer_->is_open();
    }

    const std::vector<std::vector<unsigned char> >& commands() const noexcept
    {
        return commands_;
    }

    /// Returns the first recorded command with `opcode`, or nullptr.
    const std::vector<unsigned char>* find_command(unsigned char opcode) const
    {
        for (std::size_t i = 0; i < commands_.size(); ++i)
        {
            if (!commands_[i].empty() && commands_[i][0] == opcode)
                return &commands_[i];
        }
        return nullptr;
    }

    const std::string& received() const noexcept
    {
        return received_;
    }

    const std::string& client_signature() const noexcept
    {
        return client_signature_;
    }

    bool signature_received() const noexcept
    {
        return signature_received_;
    }

    std::size_t negotiations() const noexcept
    {
        return negotiations_;
    }

    /// Sends raw serial data, escaping IAC bytes as the protocol requires.
    void send_data(const std::string& text)
    {
        std::vector<unsigned char> bytes;
        telnet::append_escaped(
            bytes,
            reinterpret_cast<const unsigned char*>(text.data()),
            text.size());
        enqueue(std::move(bytes));
    }

    /// Sends a NOTIFY-LINESTATE notification (server opcode 106).
    void send_line_state(unsigned char state)
    {
        enqueue(telnet::make_rfc2217(
            static_cast<unsigned char>(
                telnet::rfc2217::notify_line_state
                + telnet::rfc2217::server_offset),
            state));
    }

    /// Sends a NOTIFY-MODEMSTATE notification (server opcode 107).
    void send_modem_state(unsigned char state)
    {
        enqueue(telnet::make_rfc2217(
            static_cast<unsigned char>(
                telnet::rfc2217::notify_modem_state
                + telnet::rfc2217::server_offset),
            state));
    }

    /// Asks the client for its SIGNATURE text.
    ///
    /// The request carries the signature opcode and no payload, which is
    /// how RFC 2217 distinguishes a request from an advertisement.
    void send_signature_request()
    {
        enqueue(telnet::encode_rfc2217(
            telnet::rfc2217::signature, nullptr, 0));
    }

    /// Sends FLOWCONTROL-SUSPEND (true) or FLOWCONTROL-RESUME (false).
    void send_flow_control(bool suspend)
    {
        enqueue(telnet::encode_rfc2217(
            suspend ? telnet::rfc2217::flow_control_suspend
                    : telnet::rfc2217::flow_control_resume,
            nullptr,
            0));
    }

    /// Sends a command with no payload, for protocol edge cases.
    void send_bare_command(unsigned char opcode)
    {
        enqueue(telnet::encode_rfc2217(opcode, nullptr, 0));
    }

    /// Sends a raw Telnet negotiation from the server side.
    void send_negotiation(unsigned char command, unsigned char option)
    {
        enqueue(telnet::encode_negotiation(command, option));
    }

    /// Sends a raw subnegotiation payload for `option`.
    void send_subnegotiation(
        unsigned char option, const std::vector<unsigned char>& payload)
    {
        enqueue(telnet::encode_subnegotiation(
            option, payload.data(), payload.size()));
    }

    /// Sends an acknowledgement for a client command with a chosen body, so
    /// the client's ack-decoding can be exercised with arbitrary values.
    void send_server_ack(
        unsigned char client_opcode, const std::vector<unsigned char>& body)
    {
        std::vector<unsigned char> payload;
        payload.push_back(static_cast<unsigned char>(
            client_opcode + telnet::rfc2217::server_offset));
        payload.insert(payload.end(), body.begin(), body.end());
        send_subnegotiation(telnet::option_com_port, payload);
    }

    /// Negotiation commands the client has sent, in order.
    const std::vector<std::pair<unsigned char, unsigned char> >&
    client_negotiations() const noexcept
    {
        return client_negotiations_;
    }

    bool client_sent(unsigned char command, unsigned char option) const
    {
        for (std::size_t i = 0; i < client_negotiations_.size(); ++i)
        {
            if (client_negotiations_[i].first == command
                && client_negotiations_[i].second == option)
                return true;
        }
        return false;
    }

    /// Sends a NOTIFY-LINESTATE with every bit set.
    void send_all_line_states()
    {
        send_line_state(0xFF);
    }

    void close_peer()
    {
        if (!peer_)
            return;
        boost::system::error_code ignored;
        peer_->close(ignored);
    }

    void stop()
    {
        close_peer();
        boost::system::error_code ignored;
        acceptor_.close(ignored);
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
                decoder_.feed(buffer_.data(), received);
                read();
            });
    }

    void handle_negotiation(unsigned char command, unsigned char option)
    {
        ++negotiations_;
        client_negotiations_.push_back(std::make_pair(command, option));
        if (option != telnet::option_com_port)
            return;

        if (command == telnet::command_will)
        {
            negotiated_ = true;
            enqueue(telnet::encode_negotiation(
                telnet::command_do, telnet::option_com_port));
        }
        else if (command == telnet::command_do)
        {
            enqueue(telnet::encode_negotiation(
                telnet::command_will, telnet::option_com_port));
        }
    }

    void handle_subnegotiation(
        unsigned char option, const unsigned char* payload, std::size_t size)
    {
        if (option != telnet::option_com_port || size == 0)
            return;

        const unsigned char opcode = payload[0];
        if (opcode == telnet::rfc2217::signature)
        {
            if (size > 1)
            {
                client_signature_.assign(
                    reinterpret_cast<const char*>(payload + 1), size - 1);
                signature_received_ = true;
            }
            return;
        }

        commands_.push_back(
            std::vector<unsigned char>(payload, payload + size));

        // Acknowledge every configuration command, as a real access
        // server does, echoing the value it applied.
        const unsigned char acknowledgement = static_cast<unsigned char>(
            opcode + telnet::rfc2217::server_offset);
        enqueue(telnet::encode_rfc2217(
            acknowledgement, payload + 1, size - 1));
    }

    void enqueue(const std::vector<unsigned char>& bytes)
    {
        if (!peer_ || !peer_->is_open())
            return;
        outbox_.push_back(
            std::make_shared<std::vector<unsigned char> >(bytes));
        if (!writing_)
            write_next();
    }

    void enqueue(std::vector<unsigned char>&& bytes)
    {
        if (!peer_ || !peer_->is_open())
            return;
        outbox_.push_back(
            std::make_shared<std::vector<unsigned char> >(std::move(bytes)));
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
        const std::shared_ptr<std::vector<unsigned char> > data = outbox_.front();
        boost::asio::async_write(
            *peer_,
            boost::asio::buffer(*data),
            [this, data](boost::system::error_code ec, std::size_t) {
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
    decoder decoder_;
    std::array<unsigned char, 1024> buffer_{};
    std::deque<std::shared_ptr<std::vector<unsigned char> > > outbox_;
    std::vector<std::vector<unsigned char> > commands_;
    std::string received_;
    std::string client_signature_;
    unsigned short port_ = 0;
    std::vector<std::pair<unsigned char, unsigned char> > client_negotiations_;
    std::size_t negotiations_ = 0;
    bool negotiated_ = false;
    bool signature_received_ = false;
    bool writing_ = false;
};

} // namespace vs_test

#endif // BOOST_VIRTUAL_SERIAL_TEST_MOCK_RFC2217_TEST_SERVER_HPP
