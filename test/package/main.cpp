//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// A downstream consumer of the installed package: it proves that
// find_package(boost_virtual_serial) supplies the include path, the Boost
// dependency and the Boost::virtual_serial target.
//

#include <boost/virtual_serial.hpp>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>

#include <iostream>
#include <string>

namespace vs = boost::virtual_serial;

int main()
{
    boost::asio::io_context io;
    vs::any_serial_port port =
        vs::make_serial_port(io.get_executor(), "mock://consumer");
    if (!port.is_open())
        return 1;

    vs::mock_port* mock = vs::backend_cast<vs::mock_port>(port);
    if (mock == nullptr)
        return 2;

    mock->inject_read("OK");
    char buffer[4] = {};
    boost::system::error_code ec;
    const std::size_t received =
        port.read_some(boost::asio::buffer(buffer), ec);
    if (ec)
        return 3;

    std::cout << "consumer received: " << std::string(buffer, received) << '\n';
    return received == 2 ? 0 : 4;
}
