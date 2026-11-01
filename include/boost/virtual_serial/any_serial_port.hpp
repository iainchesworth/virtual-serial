//
// Copyright (c) 2026 VirtualSerial contributors
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_VIRTUAL_SERIAL_ANY_SERIAL_PORT_HPP
#define BOOST_VIRTUAL_SERIAL_ANY_SERIAL_PORT_HPP

#include <boost/virtual_serial/config.hpp>
#include <boost/virtual_serial/basic_serial_port.hpp>
#include <boost/virtual_serial/detail/erased_backend.hpp>

namespace boost {
namespace virtual_serial {

/** A serial port whose backend is chosen at run time.

    `any_serial_port` is `basic_serial_port` instantiated over the
    type-erasing backend.  It owns the concrete backend through a
    `std::shared_ptr`, so a test can keep a typed handle to its fixture
    while production code sees only the erased type:

    @code
    auto mock = std::make_shared<mock_port>(io.get_executor());
    any_serial_port port(io.get_executor(), mock);   // erased
    port.open("mock");
    mock->inject_read("AT\r\n");                     // fixture access
    @endcode

    Nothing else changes: the option objects, the timeouts and the
    `read_some` / `async_read_some` interface are identical to
    `basic_serial_port<concrete_backend>`.
*/
using any_serial_port = basic_serial_port<detail::erased_backend>;

/** Recovers the concrete backend from an #any_serial_port.

    Returns `nullptr` when the port holds a different backend type, or
    when RTTI is disabled.  Prefer keeping the typed `shared_ptr` when
    RTTI is not available.

    @code
    if (auto* mock = backend_cast<mock_port>(port))
        mock->inject_read("OK\r\n");
    @endcode
*/
template <class Backend>
Backend* backend_cast(any_serial_port& port) noexcept
{
    if (!port.native_backend())
        return nullptr;
    return port.native_backend()->template backend_as<Backend>();
}

} // namespace virtual_serial
} // namespace boost

#endif // BOOST_VIRTUAL_SERIAL_ANY_SERIAL_PORT_HPP
