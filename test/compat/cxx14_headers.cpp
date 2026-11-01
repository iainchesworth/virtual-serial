//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//
// The library advertises C++14 as its minimum language level so it can sit
// inside a Boost superproject that still supports C++14.  This translation
// unit includes every public header and must compile with cxx_std_14.
//

#include <boost/virtual_serial.hpp>

#include <boost/virtual_serial/any_serial_port.hpp>
#include <boost/virtual_serial/backends/mock_port.hpp>
#include <boost/virtual_serial/backends/physical_port.hpp>
#include <boost/virtual_serial/backends/rfc2217_port.hpp>
#include <boost/virtual_serial/backends/tcp_bridge_port.hpp>
#include <boost/virtual_serial/backends/udp_bridge_port.hpp>
#include <boost/virtual_serial/basic_serial_port.hpp>
#include <boost/virtual_serial/awaitable.hpp>
#include <boost/virtual_serial/concepts.hpp>
#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/detail/any_completion.hpp>
#include <boost/virtual_serial/detail/buffer_range.hpp>
#include <boost/virtual_serial/detail/erased_backend.hpp>
#include <boost/virtual_serial/detail/native_handle.hpp>
#include <boost/virtual_serial/detail/read_queue.hpp>
#include <boost/virtual_serial/detail/telnet.hpp>
#include <boost/virtual_serial/detail/type_erasure.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/make_serial_port.hpp>
#include <boost/virtual_serial/option.hpp>
#include <boost/virtual_serial/serial_config.hpp>

#include <boost/asio/io_context.hpp>

#include <type_traits>

#if BOOST_VIRTUAL_SERIAL_CPLUSPLUS < 201402L
# error "Boost.VirtualSerial requires at least C++14"
#endif

static_assert(
    std::is_same<
        boost::virtual_serial::any_serial_port::executor_type,
        boost::asio::any_io_executor>::value,
    "the front end must expose the type-erased executor type");

static_assert(
    !std::is_copy_constructible<boost::virtual_serial::any_serial_port>::value,
    "serial ports are move-only, like every Asio I/O object");

int main()
{
    boost::asio::io_context io;
    boost::virtual_serial::any_serial_port port(io.get_executor());

    if (port.is_open())
        return 1;

    boost::virtual_serial::serial_config config;
    config.backend = boost::virtual_serial::backend_kind::mock;
    config.baud_rate = 115200;

    return config.baud_rate == 115200 ? 0 : 1;
}
