# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Changes that have not been released yet live as one file per change in
[`changelog.d/`](changelog.d/); see [`changelog.d/README.md`](changelog.d/README.md).
Run `python tools/changelog.py --dry-run` to preview the section below.

## [Unreleased]

## [0.1.0-beta.1] - 2026-11-02

### Added

- `basic_serial_port<Backend>`, a header-only serial stream that models the
- `any_serial_port`, the same interface with a backend chosen at run time
- Five backends: `physical_port` over `boost::asio::serial_port`;
- `serial_config` with URI parsing and formatting for `serial://`, `mock://`,
- A `boost::virtual_serial::error` category mapping UART line errors,
- Unit, fixture, protocol and loopback tests, in-process TCP/UDP/RFC 2217 test
- CMake install, export and package-config rules, Boost.Build (`b2`) Jamfiles
- `test/asio/async_contract_test.cpp` asserts the Asio stream concepts for every backend and front end and proves work tracking, no-inline and exactly-once completion, associated-executor delivery, strand serialisation over four threads, generic composition and the `detached`, `redirect_error` and `use_future` completion tokens.
- A Boost.Build CI job that copies the library into a Boost 1.92 superproject, generates the header tree, bootstraps `b2` and builds and runs the test suite, including the C++14 and C++20 targets and the physical loopback tests over a pseudo-terminal pair.
- The `changelog.d/` fragment workflow described in `changelog.d/README.md`, the `tools/changelog.py` validator and merger, its unit tests and a CI check that rejects malformed fragments.
- A GitHub Actions matrix covering Ubuntu 22.04/24.04, macOS 15/26 and Windows 2022/2025 with GCC 11/12/13, Clang 14/18, AppleClang and MSVC at C++14/17/20, against Boost 1.74 (oldest supported) and 1.92 (current).
- CodeQL code scanning for C++ on every push to `main`, on pull requests and on a weekly schedule, using the `security-and-quality` query pack and publishing results to the repository's code-scanning dashboard.
- Commit authorship is now enforced: `tools/check_commit_message.py`, the opt-in `.githooks/commit-msg` hook and a `Commit authorship` CI job reject attribution trailers, generated-by statements, robot markers and commits that are not authored by the maintainer. The rules are documented in `CONTRIBUTING.md`.
- The C++20 concepts moved to the public `<boost/virtual_serial/concepts.hpp>` header, now covering the asynchronous operations as well as the synchronous interface, with constrained runtime tests, a `concepts_demo_example` and a CMake feature probe that enables the C++20 targets only when the toolchain and Boost support them.
- C++20 coroutine support: the optional `<boost/virtual_serial/awaitable.hpp>` header with `read_some`, `read_exactly`, `write_all` and `read_until` awaitable helpers, a runnable `awaitable_echo_example` and a coroutine test suite covering partial delivery, timeouts, the typed and erased front ends and a TCP round trip. `BOOST_VIRTUAL_SERIAL_HAS_COROUTINES` reports availability and the header is empty without it.
- Line and branch coverage is measured with LLVM profile data and enforced on Linux, macOS and Windows at a minimum of 90% of lines and 80% of branches, through `tools/coverage.py`, `tools/coverage_report.py` and the `BOOST_VIRTUAL_SERIAL_COVERAGE` build option.
- C++20 concept definitions (`AsyncSerialStream`, `SerialBackend`, `SerialOption`) and a compile-time conformance test for every front end and backend, alongside the existing C++14 header test.
- `tools/fetch_boost.py` now resumes interrupted downloads with HTTP range requests and retries, so a flaky connection no longer discards a partial Boost archive.
- `test/package` is a downstream CMake project that consumes the installed package with `find_package(boost_virtual_serial CONFIG REQUIRED)` and is built in CI.
- An opt-in benchmark suite in `perf/` measuring synchronous and asynchronous throughput, operation rate, latency percentiles, type-erasure overhead, TCP loopback throughput and round-trip time, Telnet codec cost and allocations per asynchronous operation, with `tools/perf_report.py` to render Markdown, compare runs and enforce `perf/floors.json`, and a `Performance` CI job on Linux, macOS and Windows that publishes the table and uploads the JSON results.
- A physical-loopback suite that exchanges data over two real serial devices, plus `tools/pty_loopback.py` to create the pair on Linux and macOS. `VIRTUAL_SERIAL_REQUIRE_LOOPBACK=1` turns a missing pair into a failure rather than a skip.
- ASan + UBSan and TSan builds in CI, with the `BOOST_VIRTUAL_SERIAL_SANITIZERS` CMake option and leak detection enabled, so the full test suite, examples, coroutine tests and concept tests run under the sanitizers on every push and pull request.
- A serial-mode matrix suite that drives every backend in both directions, synchronously and asynchronously, injects each line fault and proves the next read recovers, and repeats the exercise for timeouts, peer loss and reconnects.

### Changed

- Replaced the legacy `src/asio_serial.h` / `src/asio_serial.cpp` sample, which
- The CMake build is now a target-based monorepo exposing
- `vcpkg.json` identifies the new package and dependency set.
- Timeouts are enforced asynchronously; the mock's synchronous `read_some`
- Per-operation cancellation slots are not forwarded through the erasure;
- The test suite is split into `test/unit`, `test/integration`, `test/asio` and `test/compat`, with shared fixtures under `test/mock`.

### Fixed

- Default-constructed backends now report their own kind from `options()`.
- Re-erasing an rvalue completion handler now adopts it instead of wrapping it a second time, removing one allocation and one redundant post per asynchronous operation.
- Network backends now close their socket when a connect fails, leaving a visibly closed port that can be retried.
- RFC 2217 parameter acknowledgements with out-of-range parity or stop-bit indices are ignored instead of being mapped onto a different setting.
- A Telnet `DONT` for the com port option now resets both halves of the negotiation, so a later `WILL` is answered with `DO` again.
- Queued mock operations now register `io_context` work, so `io_context::run()` no longer returns while a read or a delayed write is still outstanding.

[Unreleased]: https://github.com/iainchesworth/virtual-serial/compare/v0.1.0-beta.1...HEAD
[0.1.0-beta.1]: https://github.com/iainchesworth/virtual-serial/releases/tag/v0.1.0-beta.1
