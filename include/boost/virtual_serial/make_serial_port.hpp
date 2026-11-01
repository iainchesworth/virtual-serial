//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_MAKE_SERIAL_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_MAKE_SERIAL_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/error.hpp>
#include <boost/virtual_serial/serial_config.hpp>
#include <boost/virtual_serial/any_serial_port.hpp>
#include <boost/virtual_serial/backends/mock_port.hpp>
#include <boost/virtual_serial/backends/physical_port.hpp>
#include <boost/virtual_serial/backends/rfc2217_port.hpp>
#include <boost/virtual_serial/backends/tcp_bridge_port.hpp>
#include <boost/virtual_serial/backends/udp_bridge_port.hpp>

#include <boost/asio/any_io_executor.hpp>
#include <boost/system/error_code.hpp>

#include <memory>
#include <string>
#include <utility>

namespace boost {
namespace virtual_serial {

/** Creates a mock fixture and returns its typed handle.

    The caller keeps the `shared_ptr`, which is how a test injects data
    and faults even after the port has been erased into an
    #any_serial_port.

    @code
    auto mock = make_mock_port(io.get_executor());
    any_serial_port port(io.get_executor(), mock);
    @endcode
*/
inline std::shared_ptr<mock_port> make_mock_port(
    const boost::asio::any_io_executor& executor,
    const serial_config& config = serial_config())
{
    serial_config settings = config;
    settings.backend = backend_kind::mock;
    return std::make_shared<mock_port>(executor, settings);
}

/** Creates and opens a port for `config`.

    This is the runtime "API switcher": the same call produces a physical
    port, a mock fixture, an RFC 2217 client or a raw TCP/UDP bridge
    depending on `config.backend`.

    On failure `ec` is set and the returned port is closed with no
    backend attached.

    @code
    serial_config config = parse_uri("rfc2217://lab-host:7001?baud=115200");
    any_serial_port port = make_serial_port(io.get_executor(), config, ec);
    @endcode
*/
inline any_serial_port make_serial_port(
    const boost::asio::any_io_executor& executor,
    const serial_config& config,
    boost::system::error_code& ec)
{
    error failure = error::success;
    if (!detail::validate(config, failure))
    {
        ec = failure;
        return any_serial_port(executor);
    }

    switch (config.backend)
    {
    case backend_kind::mock:
    {
        std::shared_ptr<mock_port> backend =
            std::make_shared<mock_port>(executor, config);
        any_serial_port port(executor, backend);
        port.open(config.target, ec);
        return port;
    }
    case backend_kind::physical:
    {
        std::shared_ptr<physical_port> backend =
            std::make_shared<physical_port>(executor, config);
        any_serial_port port(executor, backend);
        port.open(config.target, ec);
        return port;
    }
    case backend_kind::rfc2217:
    {
        std::shared_ptr<rfc2217_port> backend =
            std::make_shared<rfc2217_port>(executor, config);
        any_serial_port port(executor, backend);
        port.open(config.target, ec);
        return port;
    }
    case backend_kind::tcp:
    {
        std::shared_ptr<tcp_bridge_port> backend =
            std::make_shared<tcp_bridge_port>(executor, config);
        any_serial_port port(executor, backend);
        port.open(config.target, ec);
        return port;
    }
    case backend_kind::udp:
    {
        std::shared_ptr<udp_bridge_port> backend =
            std::make_shared<udp_bridge_port>(executor, config);
        any_serial_port port(executor, backend);
        port.open(config.target, ec);
        return port;
    }
    }

    ec = error::bad_target;
    return any_serial_port(executor);
}

/// Creates and opens a port, throwing on failure.
inline any_serial_port make_serial_port(
    const boost::asio::any_io_executor& executor, const serial_config& config)
{
    boost::system::error_code ec;
    any_serial_port port = make_serial_port(executor, config, ec);
    detail::throw_error(ec);
    return port;
}

/** Creates and opens a port from a URI.

    @code
    auto port = make_serial_port(io.get_executor(), "tcp://192.168.1.50:4001");
    @endcode
*/
inline any_serial_port make_serial_port(
    const boost::asio::any_io_executor& executor,
    const std::string& uri,
    boost::system::error_code& ec)
{
    const serial_config config = parse_uri(uri, ec);
    if (ec)
        return any_serial_port(executor);
    return make_serial_port(executor, config, ec);
}

/// Creates and opens a port from a URI, throwing on failure.
inline any_serial_port make_serial_port(
    const boost::asio::any_io_executor& executor, const std::string& uri)
{
    boost::system::error_code ec;
    any_serial_port port = make_serial_port(executor, uri, ec);
    detail::throw_error(ec);
    return port;
}

/** Creates and opens a port for a backend kind and target.

    @code
    auto port = make_serial_port(io.get_executor(), backend_kind::mock, "");
    @endcode
*/
inline any_serial_port make_serial_port(
    const boost::asio::any_io_executor& executor,
    backend_kind backend,
    const std::string& target)
{
    serial_config config;
    config.backend = backend;
    config.target = target;
    return make_serial_port(executor, config);
}

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_MAKE_SERIAL_PORT_HPP
