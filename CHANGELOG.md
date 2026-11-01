# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Changes that have not been released yet live as one file per change in
[`changelog.d/`](changelog.d/); see [`changelog.d/README.md`](changelog.d/README.md).
Run `python tools/changelog.py --dry-run` to preview the section below.

## [Unreleased]

## [1.0.0] - 2026-11-01

### Added

- `basic_serial_port<Backend>`, a header-only serial stream that models the
  Asio `SyncReadStream`, `SyncWriteStream`, `AsyncReadStream` and
  `AsyncWriteStream` concepts, forwards the `boost::asio::serial_port_base`
  options and adds front-end read and write timeouts.
- `any_serial_port`, the same interface with a backend chosen at run time
  through a custom type-erasing backend, plus `make_serial_port` and
  `make_mock_port` factories.
- Five backends: `physical_port` over `boost::asio::serial_port`;
  `mock_port` with data injection, line-fault injection, hangup and disconnect
  simulation, bounded-FIFO overflow policies, partial reads, virtual-clock
  pacing and write expectations; `rfc2217_port` implementing the client half of
  RFC 2217; and `tcp_bridge_port` / `udp_bridge_port` for raw network bridges.
- `serial_config` with URI parsing and formatting for `serial://`, `mock://`,
  `rfc2217://`, `tcp://` and `udp://`, including the serial options and
  front-end timeouts.
- A `boost::virtual_serial::error` category mapping UART line errors,
  transport failures and protocol errors onto one enumeration.
- Unit, fixture, protocol and loopback tests, in-process TCP/UDP/RFC 2217 test
  servers, runnable examples, Quickbook documentation and an architecture
  blueprint.
- CMake install, export and package-config rules, Boost.Build (`b2`) Jamfiles
  and `meta/libraries.json`, and a `tools/fetch_boost.py` helper that makes
  the checkout self-contained for offline and CI builds.

### Changed

- Replaced the legacy `src/asio_serial.h` / `src/asio_serial.cpp` sample, which
  depended on `boost::asio::io_service` and could not be mocked, with the
  header-only library, examples and tests.
- The CMake build is now a target-based monorepo exposing
  `Boost::virtual_serial`, requires C++14 as its minimum language level, and no
  longer hard-codes a local vcpkg toolchain.
- `vcpkg.json` identifies the new package and dependency set.
- Timeouts are enforced asynchronously; the mock's synchronous `read_some`
  reports `boost::asio::error::would_block` instead of blocking, because a
  fixture cannot advance its own clock while blocked.
- Per-operation cancellation slots are not forwarded through the erasure;
  `cancel()` aborts all pending operations on the port, as
  `boost::asio::serial_port::cancel()` does.

[Unreleased]: https://github.com/iainchesworth/boost-virtual-serial/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/iainchesworth/boost-virtual-serial/releases/tag/v1.0.0
