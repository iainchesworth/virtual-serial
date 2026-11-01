# Boost.VirtualSerial

**Drop-in, mockable serial streams for Boost.Asio.** One `serial_port`-shaped
API over real hardware, RFC 2217 (Serial-over-Telnet), raw TCP/UDP bridges and
a deterministic in-memory fixture — switchable at compile time or at run time.

| | |
|---|---|
| Language | C++14 minimum, C++17 baseline, C++20 concepts |
| Dependency | Boost.Asio headers, Boost 1.74 or newer |
| Build | CMake ≥ 3.16 and Boost.Build (b2) compatible |
| Tests | 239 test cases: unit, fixture, async-contract, C++20 concept and coroutine, fault-recovery and loopback protocol tests |
| License | [Boost Software License 1.0](LICENSE_1_0.txt) |

---

## 1. Why this library exists

`boost::asio::serial_port` is a concrete type. Its executor, its native
handle, its option types and its error behaviour are all baked in, and there
is no supported way to substitute the transport. That single design decision
has three consequences that every team eventually hits:

1. **Tests need hardware.** CI cannot open `COM3`, and a Raspberry Pi farm is
   not a unit-test fixture. Teams wrap the serial port in their own interface,
   lose the Asio stream concepts in the process, and still cannot provoke a
   framing error on demand.
2. **Remote devices are out of reach.** Serial consoles are frequently
   published by RFC 2217 access servers or raw TCP bridges. Application code
   written against `serial_port` cannot be pointed at them without a second
   implementation and a second set of tests.
3. **Failure paths are untestable.** Parity errors, overruns, cable pulls and
   slow links are precisely the states in which user-space protocol state
   machines break, and precisely the states that hardware never reproduces
   when asked to.

Boost.VirtualSerial keeps the Asio programming model — the same
`read_some` / `write_some` / `async_read_some` / `async_write_some` quartet,
the same `serial_port_base` options, the same executor semantics — and puts
the *transport* behind a seam. Application code is written once against
`basic_serial_port<Backend>`; production instantiates it over hardware or a
network backend, tests instantiate it over the fixture.

```cpp
// Application code: no knowledge of which backend it runs on.
template <class Port>
void send_command(Port& port, const std::string& command)
{
    boost::asio::write(port, boost::asio::buffer(command));
}
```

## 2. Architecture at a glance

```
                 ┌──────────────────────────────────────────────┐
   user code ───▶│ basic_serial_port<Backend>                   │
                 │   read_some / write_some                     │
                 │   async_read_some / async_write_some         │
                 │   set_option / get_option, timeouts          │
                 └───────────────────────┬──────────────────────┘
                                         │  Backend is a template parameter
              ┌──────────────────────────┼───────────────────────────┐
              │                          │                           │
   ┌──────────▼─────────┐   ┌────────────▼────────────┐   ┌──────────▼──────────┐
   │ physical_port      │   │ mock_port               │   │ rfc2217_port        │
   │ boost::asio::      │   │ in-memory FIFO,         │   │ Telnet COM Port     │
   │ serial_port        │   │ fault + clock injection │   │ Control (RFC 2217)  │
   └────────────────────┘   └─────────────────────────┘   └─────────────────────┘
              │                          │                           │
   ┌──────────▼─────────┐   ┌────────────▼────────────┐   ┌──────────▼──────────┐
   │ tcp_bridge_port    │   │ udp_bridge_port         │   │ any_serial_port     │
   │ raw TCP byte pipe  │   │ connected datagrams     │   │ type-erased wrapper │
   └────────────────────┘   └─────────────────────────┘   │ over all of the above│
                                                          └─────────────────────┘
```

The design rationale, including why the type-erasure layer does not break
`io_context` or executor bindings, is documented in
[doc/architecture.md](doc/architecture.md).

### Design pillars

- **Concept compliance, not approximation.** Every backend models the Asio
  `SyncReadStream`, `SyncWriteStream`, `AsyncReadStream` and `AsyncWriteStream`
  concepts, so `boost::asio::read`, `write`, `async_read`, `async_write`,
  `read_until` and `streambuf` work unchanged.
- **Executor fidelity.** The type-erased front end captures each completion
  handler's *associated executor* and *associated allocator* before erasing
  it, and dispatches completions through `post`. Strands, `use_future`,
  custom executors and allocators keep working; the `io_context` never stops
  early because the erased wrapper holds outstanding work.
- **Zero-cost when static.** `basic_serial_port<mock_port>` compiles to direct
  calls. `any_serial_port` adds one virtual dispatch per operation and shares
  the *same* front-end code, so the tested and production paths cannot drift.
- **Deterministic first.** The mock never sleeps, never races the scheduler
  and never depends on machine speed: time is a value the test advances.

## 3. Building

### CMake (primary, standalone)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Boost headers are discovered from `BOOST_ROOT`, an already-imported
`Boost::headers` target, or `find_package(Boost 1.74)`. To make the checkout
fully self-contained, fetch the headers into `.cache/`:

```bash
python tools/fetch_boost.py           # downloads Boost 1.92 headers only
cmake -S . -B build -DBOOST_ROOT=$PWD/.cache/boost-root
```

Options: `BOOST_VIRTUAL_SERIAL_BUILD_TESTS`, `..._BUILD_EXAMPLES`,
`..._INSTALL`, `..._WARNINGS_AS_ERRORS` (all default to `ON` at top level),
`BOOST_VIRTUAL_SERIAL_COVERAGE` (default `OFF`) for instrumented builds, and
`BOOST_VIRTUAL_SERIAL_SANITIZERS` (default empty) for ASan, UBSan or TSan.

### Boost.Build (for the Boost tree)

The repository keeps the Boost layout, so it drops into a superproject
checkout unchanged:

```bash
b2 libs/virtual_serial                      # tests, examples, docs
b2 libs/virtual_serial/test                 # tests only
```

`build/Jamfile` declares `boost_virtual_serial` as a header-only target and
calls `boost-install`; `meta/libraries.json` carries the library metadata.

## 4. Continuous integration

Every push runs the full matrix below; nothing in this repository depends on a
system Boost installation, because the workflow fetches one.

| Axis | Values |
|---|---|
| Operating systems | Ubuntu 22.04, Ubuntu 24.04, macOS 14, macOS 15, Windows 2022, Windows 2025 |
| Compilers | GCC 11/12/13, Clang 14/18, AppleClang, MSVC |
| Language levels | C++14, C++17, C++20 |
| Boost releases | 1.74 (oldest supported) and 1.92 (current) |
| Build systems | CMake (the matrix), Boost.Build metadata kept in tree |

### Serial modes, both directions, including fault recovery

The suite is not only run; the interesting parts are run *visibly*:

- `async_*` in [test/asio/async_contract_test.cpp](test/asio/async_contract_test.cpp)
  proves the Asio contract itself; see the next subsection.
- `modes_mock_*`, `modes_tcp_*`, `modes_udp_*` and `modes_rfc2217_*` drive every
  backend through host→device and device→host traffic, synchronous and
  asynchronous entry points, each injectable line fault followed by a
  successful recovery read, front-end timeouts followed by recovery, peer loss
  followed by a reconnect, and mock buffer overflow followed by a clean queue.
- `physical_loopback_*` opens two real serial devices and exchanges data in
  both directions, then exercises timeout, cancellation, close and reopen.

### The asynchronous contract, as a test

[test/asio/async_contract_test.cpp](test/asio/async_contract_test.cpp) exists
to answer "how do we know the async side works?" with assertions rather than
claims:

| Guarantee | Evidence |
|---|---|
| All four Asio stream concepts | Compile-time detection traits asserted for all five backends, `basic_serial_port<B>` and `any_serial_port` |
| An outstanding operation keeps `io_context` alive | `run()` on a worker thread is proven still running 100 ms after a read or a delayed write is issued, and returns only once the operation completes; `close()` releases the work |
| Handlers are never invoked inline | A read satisfied by already-injected data has not called its handler when `async_read_some` returns |
| Handlers run exactly once | Cancel, then inject late data: one completion, ever |
| Associated executor is honoured | A handler bound to a *second* `io_context` runs there, not on the port's |
| Strand serialisation | 100 handlers, four `io.run()` threads, a counter that must never exceed 1 |
| Generic composition | `async_read` over partial deliveries, `async_write` with a 3-buffer scatter, `async_read_until`, and a user-written `async_compose` operation run on both the erased port and a TCP bridge |
| Token interoperability | `detached`, `redirect_error` and `use_future` |
| Ordering | Concurrent reads complete in submission order |

Two real defects were found by this suite and fixed: queued mock operations did
not register `io_context` work (so `run()` returned while a read was pending),
and an rvalue completion handler was re-wrapped instead of adopted, adding an
allocation and a redundant post per operation.

On Linux and macOS CI creates that pair from two pseudo terminals with
`tools/pty_loopback.py`, so the physical backend — not a stand-in — is part of
the build. The tests skip on developer machines without a pair, unless
`VIRTUAL_SERIAL_REQUIRE_LOOPBACK=1` is set, which CI does:

```bash
python tools/pty_loopback.py > pty.txt &
export VIRTUAL_SERIAL_LOOPBACK_A=$(sed -n 1p pty.txt)
export VIRTUAL_SERIAL_LOOPBACK_B=$(sed -n 2p pty.txt)
export VIRTUAL_SERIAL_REQUIRE_LOOPBACK=1
ctest --test-dir build --output-on-failure
```

On Windows a real pair needs a virtual-COM driver (com0com or similar); the
same test picks it up from `VIRTUAL_SERIAL_LOOPBACK_A` / `_B` when one is
installed, and the rest of the physical backend is covered by its closed-port
and option paths.

### Coverage gates

`BOOST_VIRTUAL_SERIAL_COVERAGE=ON` instruments the test binaries with LLVM
profile instrumentation. Two jobs — Clang on Linux and AppleClang on macOS —
run the suite, merge the profiles with `llvm-profdata`, export lcov with
`llvm-cov` and enforce the gates with
[`tools/coverage.py`](tools/coverage.py):

| Metric | Gate | Measured locally (Windows build, physical success paths not runnable) |
|---|---|---|
| Line / statement | **≥ 90%** | 93.99% |
| Branch | **≥ 80%** | 88.52% |

Reproduce locally:

```bash
cmake -S . -B build -DBOOST_VIRTUAL_SERIAL_COVERAGE=ON
cmake --build build
LLVM_PROFILE_FILE="$PWD/build/profiles/%p-%m.profraw" \
  ctest --test-dir build --output-on-failure
python tools/coverage_report.py \
  --objects build/test/boost_virtual_serial_tests \
  --profiles build/profiles \
  --min-lines 90 --min-branches 80
```

The report is printed per header and the job fails if either gate is missed,
so a pull request cannot lower coverage silently. Linux and macOS include the
physical backend in the denominator (the PTY pair covers it); Windows excludes
nothing, which is why its headline line number is the lowest of the three.

Only the primary `boost_virtual_serial_tests` executable is instrumented and
reported; the C++14 and C++20 compatibility executables are compile-time
checks. The Windows matrix runs the full suite but does not gate coverage:
clang-cl's coverage mapping counts each template instantiation separately, so
the same source branch appears several times and the ratio is not comparable
with the other platforms.

### Sanitizers and static analysis

Every push and pull request also runs three sanitizer builds, and any report
fails the job:

| Job | Toolchain | Sanitizers |
|---|---|---|
| `Sanitizers / Linux / ASan + UBSan` | Clang 18 | AddressSanitizer with leak detection, UndefinedBehaviorSanitizer |
| `Sanitizers / Linux / TSan` | Clang 18 | ThreadSanitizer |
| `Sanitizers / macOS / ASan + UBSan` | AppleClang | AddressSanitizer, UndefinedBehaviorSanitizer |

The tests, examples, coroutine tests and concept tests all run under the
sanitizers. Reproduce locally with:

```bash
cmake -S . -B build-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DBOOST_VIRTUAL_SERIAL_SANITIZERS="address;undefined"
cmake --build build-sanitize
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir build-sanitize --output-on-failure
```

`BOOST_VIRTUAL_SERIAL_SANITIZERS=thread` builds the ThreadSanitizer variant.
The option is supported with Clang and GCC on Linux and macOS; MSVC and
clang-cl are rejected with a configure error because their link step cannot
carry the sanitizer runtime.

CodeQL runs from [`.github/workflows/codeql.yml`](.github/workflows/codeql.yml)
on every push to `main`, on pull requests and weekly on a schedule. It builds
the full suite with CMake and analyses C++ with the `security-and-quality`
query pack; findings appear in the repository's **Security → Code scanning**
tab.

A dedicated `Changelog fragments` job validates every file in `changelog.d/`,
prints the section that the next release would contain, and runs the changelog
tool's own unit tests. A `Commit authorship` job rejects attribution trailers,
generated-by statements and commits whose author is not the maintainer. The
`Performance (<os>)` jobs run the benchmark suite in Release on Linux, macOS
and Windows, publish the rendered table to the job summary, upload the JSON and
the report as artifacts, and enforce [`perf/floors.json`](perf/floors.json).
C++20 toolchains additionally build and run the coroutine and concept tests
and examples; the feature probe in `cmake/DetectCxx20.cmake` keeps those
targets out of toolchains that cannot compile them.

## 5. Quick start — production code

```cpp
#include <boost/virtual_serial.hpp>
#include <boost/asio.hpp>

namespace vs = boost::virtual_serial;

int main()
{
    boost::asio::io_context io;
    vs::basic_serial_port<vs::physical_port> port(io.get_executor());

    // Options can be set before open(): they are remembered and applied
    // when the device is opened.
    port.set_option(boost::asio::serial_port_base::baud_rate(115200));
    port.set_option(boost::asio::serial_port_base::parity(
        boost::asio::serial_port_base::parity::none));
    port.set_option(vs::read_timeout(std::chrono::milliseconds(500)));

    port.open("COM3");                       // or "/dev/ttyUSB0"

    boost::asio::write(port, boost::asio::buffer("AT\r\n", 4));

    char reply[128];
    boost::system::error_code ec;
    const std::size_t n = port.read_some(boost::asio::buffer(reply), ec);
    // ec == vs::error::timeout if nothing arrived within 500 ms
}
```

The same device can be named by URI, which is what configuration files and
command lines usually prefer:

```cpp
auto port = vs::make_serial_port(io.get_executor(), "serial://COM3?baud=115200");
auto remote = vs::make_serial_port(io.get_executor(), "rfc2217://lab-host:7001?baud=115200");
auto bridge = vs::make_serial_port(io.get_executor(), "tcp://192.168.1.50:4001");
```

`make_serial_port` returns an `any_serial_port` — the runtime-switchable
front end — already opened and configured. The URI grammar is documented in
`boost/virtual_serial/serial_config.hpp`; unknown query keys are rejected so
a typo in a deployment file is never silently ignored.

### Compile-time versus run-time selection

```cpp
// Compile-time: direct calls, inlinable, no allocation for dispatch.
vs::basic_serial_port<vs::rfc2217_port> port(io.get_executor());

// Run-time: backend chosen by configuration.
vs::any_serial_port port = vs::make_serial_port(io.get_executor(), uri);

// Same application code either way.
send_command(port, "AT\r\n");
```

## 6. Mocking setup — testing without a COM port

The fixture is a real Asio stream backed by in-memory FIFOs. No driver, no
device, no `sleep`:

```cpp
#include <boost/virtual_serial.hpp>

TEST(at_command_returns_ok)
{
    boost::asio::io_context io;

    auto mock = boost::virtual_serial::make_mock_port(io.get_executor());
    boost::virtual_serial::any_serial_port port(io.get_executor(), mock);

    // Scripted device: answer every command.
    mock->set_write_handler([mock](const unsigned char*, std::size_t) {
        mock->inject_read("OK\r\n");
    });

    port.open("at-modem");
    boost::asio::write(port, boost::asio::buffer("AT\r\n", 4));

    char reply[16] = {};
    std::size_t received = 0;
    boost::system::error_code ec;
    port.async_read_some(boost::asio::buffer(reply),
        [&](boost::system::error_code error, std::size_t count) {
            ec = error;
            received = count;
        });

    io.run();                                  // no waiting: data is there

    REQUIRE(!ec);
    REQUIRE(std::string(reply, received) == "OK\r\n");
    REQUIRE(mock->written() == "AT\r\n");      // captured verbatim
}
```

### Raw data injection

```cpp
mock->inject_read("HELLO");                 // char literal, no NUL
mock->inject_read(std::string("\x02\x10\x00", 3));   // binary payload
mock->inject_line("READY");                 // appends CRLF
mock->inject_eof();                         // close the read side cleanly
```

### Fault injection

Every fault is delivered by the next read, exactly where a real UART would
report it, and the port stays usable afterwards:

```cpp
mock->inject_framing_error();               // vs::error::framing_error
mock->inject_parity_error();                // vs::error::parity_error
mock->inject_overrun();                     // vs::error::overrun
mock->inject_break();                       // vs::error::break_detected
mock->inject_corrupt_byte(vs::fault::framing, 0x00);  // garbage + error
mock->inject_hangup();                      // permanent vs::error::hangup
mock->inject_disconnect();                  // drops the port, not_connected
mock->inject_error(boost::asio::error::bad_descriptor);
mock->inject_write_error(vs::error::not_connected);

static_cast<void>(mock->overruns());        // how many injections overflowed
```

Buffer overflows are simulated with a bounded receive FIFO:

```cpp
mock->set_read_capacity(16);                                   // bytes
mock->set_overflow_policy(vs::overflow_policy::drop_oldest);   // or drop_newest
mock->inject_read("0123456789abcdefghij");    // 20 bytes into 16
// -> vs::error::overrun on the next read, overruns() == 1
```

### Clocked data and baud-rate timing

By default injected bytes are visible immediately. With pacing enabled every
byte gets a due time computed from the configured framing, and only becomes
readable when the virtual clock reaches it:

```cpp
vs::serial_config config;
config.baud_rate = 9600;                       // 8N1 -> 1.0417 ms per byte
auto mock = std::make_shared<vs::mock_port>(io.get_executor(), config);
mock->open("clocked");
mock->set_pacing(true);

mock->inject_read("GO");
mock->available();                             // 0
mock->step();                                  // release exactly one byte
mock->available();                             // 1, clock == 1 byte time
mock->advance(mock->byte_duration() * 2);      // release the rest
```

This is how race conditions in user-space state machines are reproduced
without ever touching the scheduler:

```cpp
mock->step();                                  // first byte arrives
assert(parser.state() == state::waiting_for_second_byte);
mock->step();                                  // second byte arrives
assert(parser.state() == state::complete);
```

Latency and back-pressure use the same clock:

```cpp
mock->set_read_latency(std::chrono::milliseconds(5));
mock->set_write_latency(std::chrono::milliseconds(2));
mock->set_max_read_size(1);                    // force partial reads
```

### Expectations

```cpp
mock->expect_write("AT+BAUD=115200\r\n");
device.write_configuration(115200);
REQUIRE(mock->expectations_satisfied());
```

### Asserting on the fixture from the erased port

```cpp
auto* mock = vs::backend_cast<vs::mock_port>(port);   // nullptr if wrong type
mock->inject_read("OK");
```

> **Rule of thumb:** create the typed `shared_ptr<mock_port>` first, hand it
> to the port, and keep it for injection. `backend_cast` exists for the cases
> where the port was built by the factory from a URI.

## 7. Backends

| Backend | Header | Transport | Applies serial options | Faults | Clock |
|---|---|---|---|---|---|
| `physical_port` | `backends/physical_port.hpp` | `boost::asio::serial_port` | yes, to the UART | OS/GUI errors | wall clock |
| `mock_port` | `backends/mock_port.hpp` | memory FIFOs | stored and validated | injected | virtual |
| `rfc2217_port` | `backends/rfc2217_port.hpp` | TCP + Telnet option 44 | yes, on the access server | from NOTIFY-LINESTATE | wall clock |
| `tcp_bridge_port` | `backends/tcp_bridge_port.hpp` | raw TCP | stored only | transport errors | wall clock |
| `udp_bridge_port` | `backends/udp_bridge_port.hpp` | connected UDP | stored only | transport errors | wall clock |

All five are usable directly (`basic_serial_port<Backend>`) or through
`any_serial_port`, which erases the backend behind one vtable.

### RFC 2217 conformance

`rfc2217_port` implements the client half of RFC 2217:

- Telnet option 44 negotiation (`WILL`/`DO`), refusing every unrelated option;
- `SET-BAUDRATE`, `SET-DATASIZE`, `SET-PARITY`, `SET-STOPSIZE`, `SET-CONTROL`
  in the order the RFC recommends, sent only when the value changes;
- `SET-LINESTATE-MASK` requesting framing, parity, overrun and break
  notifications, which are delivered as the same `virtual_serial::error`
  values the other backends use;
- `NOTIFY-LINESTATE`, `NOTIFY-MODEMSTATE` (including a handler hook),
  `FLOWCONTROL-SUSPEND`/`RESUME` state, `SIGNATURE` exchange, `PURGE-DATA`
  and `BREAK` via `set_break()`;
- IAC (`0xFF`) quoting in both directions, decoded incrementally so that
  commands split across TCP segments are handled correctly.

The protocol codec (`detail/telnet.hpp`) is pure and unit-tested against
byte-exact vectors.

## 8. Options and timeouts

The five Asio serial options are accepted directly:

```cpp
port.set_option(boost::asio::serial_port_base::baud_rate(115200));
port.set_option(boost::asio::serial_port_base::character_size(8));
port.set_option(boost::asio::serial_port_base::parity(
    boost::asio::serial_port_base::parity::even));
port.set_option(boost::asio::serial_port_base::stop_bits(
    boost::asio::serial_port_base::stop_bits::one));
port.set_option(boost::asio::serial_port_base::flow_control(
    boost::asio::serial_port_base::flow_control::none));

port.get_option(baud);                     // read back what is in force
```

plus the two library options, which are enforced by the front end for every
backend:

```cpp
port.set_option(vs::read_timeout(std::chrono::milliseconds(250)));
port.set_option(vs::write_timeout(std::chrono::milliseconds(250)));
```

An option type the library does not know reports
`vs::error::unsupported_option` at run time rather than failing to compile,
so vendor-specific option objects degrade gracefully.

## 9. Error model

| Value | Meaning |
|---|---|
| `vs::error::not_connected` | port closed, or the peer went away |
| `vs::error::already_open` | `open()` on an open port |
| `vs::error::bad_target` | unparseable URI or endpoint |
| `vs::error::invalid_option` | option value outside its range |
| `vs::error::unsupported_option` | backend/option mismatch |
| `vs::error::framing_error` | UART framing error |
| `vs::error::parity_error` | UART parity error |
| `vs::error::overrun` | receive FIFO overflow |
| `vs::error::break_detected` | break condition on the line |
| `vs::error::hangup` | carrier lost |
| `vs::error::timeout` | front-end read/write timeout |
| `vs::error::protocol_error` | malformed transport frame |
| `vs::error::queue_full` | mock FIFO rejected injected bytes |
| `vs::error::aborted` | operation cancelled |
| `vs::error::not_supported` | feature not implemented by a backend |

`boost::system::is_error_code_enum` is specialised, so
`boost::system::error_code ec = vs::error::timeout;` works, as does comparison
against the enum.

## 10. Threading and executor guarantees

- Distinct port objects are safe to use from different threads; sharing one
  object requires external serialisation, exactly as with `serial_port`.
- A handler bound to a `boost::asio::strand` runs on that strand; a handler
  bound to `use_future` produces a future; a custom executor stays in charge.
- Completions are always delivered through `boost::asio::post`, so no handler
  can run before its initiating function returns.
- The type-erased front end holds outstanding work while an operation is in
  flight: `io_context::run()` does not return early.
- Buffers passed to `async_*` are not copied; the descriptors are, and the
  underlying memory belongs to the caller — the standard Asio contract.
- After a read timeout the buffer stays valid until the in-flight operation
  has been cancelled and completed, which the front end waits for.

## 11. Repository layout

```
include/boost/virtual_serial.hpp           umbrella header
include/boost/virtual_serial/              library headers
  backends/                                physical, mock, rfc2217, tcp, udp
  detail/                                  buffer ranges, completions, codec
  concepts.hpp                             C++20 concepts (optional)
  awaitable.hpp                            C++20 coroutine helpers (optional)
test/                                      unit, integration, protocol, fixtures
test/package/                              downstream find_package() consumer
example/                                   runnable examples
perf/                                      benchmark suite (opt-in)
doc/                                       Quickbook documentation and blueprint
build/                                     Boost.Build metadata
cmake/                                     package, coverage and sanitizer helpers
CHANGELOG.md                               Keep a Changelog history
changelog.d/                               one fragment per unreleased change
CONTRIBUTING.md                            contribution and commit-authorship rules
.githooks/commit-msg                       local attribution guard
tools/fetch_boost.py                       fetch Boost headers for offline CI
tools/pty_loopback.py                      create a pseudo-terminal serial pair
tools/coverage.py                          enforce line and branch thresholds
tools/coverage_report.py                   llvm-profdata/llvm-cov driver
tools/changelog.py                         validate, merge and release fragments
tools/check_commit_message.py              reject attribution and trailers
tools/perf_report.py                       render and gate benchmark results
```

## 12. Testing

The suite covers the option model, URI grammar, buffer normalisation, the
Telnet/RFC 2217 codec, every mock injection mode, the erased front end, and
end-to-end loopback tests against in-process TCP, UDP and RFC 2217 servers.
The RFC 2217 tests run a real server on `127.0.0.1` and assert the exact
subnegotiation bytes the client emits.

Two suites are written specifically for CI visibility:

- `test/integration/serial_modes_test.cpp` — the all-modes, all-directions
  matrix: every backend in both directions, synchronous and asynchronous, with
  each fault injected and then recovered from.
- `test/integration/physical_loopback_test.cpp` — the physical backend over a
  real device pair: bidirectional traffic, timeout, cancel, close and reopen.

```bash
ctest --test-dir build --output-on-failure
./build/test/boost_virtual_serial_tests --filter=modes_ --list
./build/test/boost_virtual_serial_tests --filter=physical_loopback --list
```

Every public header is also compiled as C++14 by
`test/compat/cxx14_headers.cpp`, and the C++20 conformance of every front end
and backend (`AsyncSerialStream`, `SerialBackend`, `SerialOption`) is asserted
by `test/compat/cxx20_concepts.cpp`.

## 13. Coroutines and concepts (C++20)

The library is C++14 at its core, and the same port types work with C++20
coroutines and concepts without a second API.

### Coroutines

Every port is an ordinary Asio async stream, so coroutine tokens compose
directly:

```cpp
char buffer[64];
const std::size_t count = co_await port.async_read_some(
    boost::asio::buffer(buffer), boost::asio::use_awaitable);
```

`<boost/virtual_serial/awaitable.hpp>` wraps the common composed operations:

| Helper | Operation |
|---|---|
| `read_some(stream, buffers)` | one `async_read_some` |
| `read_exactly(stream, buffers)` | `boost::asio::async_read` |
| `write_all(stream, buffers)` | `boost::asio::async_write` |
| `read_until(stream, buffer, delimiter)` | `boost::asio::async_read_until` |

They run on the port's executor, so `co_spawn`, strands and the read and write
timeouts behave exactly as they do in callback code. A timeout surfaces as a
`boost::system::system_error` with `boost::virtual_serial::error::timeout`.
`BOOST_VIRTUAL_SERIAL_HAS_COROUTINES` reports whether the toolchain and Boost
provide the feature; without it the header is empty and nothing changes.
Boost.Asio enables its coroutine layer per compiler and Boost release (Boost
1.74, for example, does not enable it for MSVC), so the CMake probe follows
Asio's own macro and any unsupported combination simply skips the coroutine
targets while concepts still build.

See [`example/awaitable_echo_example.cpp`](example/awaitable_echo_example.cpp)
and [`test/coroutine/awaitable_test.cpp`](test/coroutine/awaitable_test.cpp).

### Concepts

`<boost/virtual_serial/concepts.hpp>` defines three concepts:

- `AsyncSerialStream` — executor, `open`/`close`, synchronous and asynchronous
  reads and writes, for every front end and backend;
- `SerialBackend` — adds `backend()`, `apply_options()` and `options()`;
- `SerialOption` — the option objects the front end can forward.

```cpp
template <boost::virtual_serial::AsyncSerialStream Stream>
std::size_t send(Stream& stream, const char* data, std::size_t size);

static_assert(boost::virtual_serial::AsyncSerialStream<
    boost::virtual_serial::any_serial_port>);
static_assert(!boost::virtual_serial::AsyncSerialStream<int>);
```

`BOOST_VIRTUAL_SERIAL_HAS_CONCEPTS` reports availability. Both headers are
empty below C++20, so the C++14 build is unchanged. See
[`example/concepts_demo_example.cpp`](example/concepts_demo_example.cpp) and
[`test/concept/concepts_test.cpp`](test/concept/concepts_test.cpp).

## 14. Performance

The benchmark suite lives in `perf/` and is opt-in, so normal builds stay fast:

```bash
cmake -S . -B build-perf -DCMAKE_BUILD_TYPE=Release \
  -DBOOST_VIRTUAL_SERIAL_BUILD_PERF=ON
cmake --build build-perf
./build-perf/perf/virtual_serial_perf --quick --output perf.json
python tools/perf_report.py perf.json --floors perf/floors.json
```

Without `--quick` the suite runs longer iterations. It measures:

| Benchmark | Parameter | Why it matters |
|---|---|---|
| `mock_sync_write` / `mock_sync_read` | MB/s | Cost floor for tests and simulations |
| `erased_sync_write` / `erasure_overhead` | MB/s, ratio | Price of runtime backend switching |
| `mock_async_read_ops` / `mock_async_write_ops` | ops/s | Completion-path throughput |
| `mock_async_latency` | µs, p50/p90/p99 | Initiation-to-handler latency |
| `tcp_sync_write`, `tcp_pingpong` | MB/s, µs, round trips/s | Bridge throughput and loopback round-trip time |
| `telnet_encode` / `telnet_decode` | MB/s | RFC 2217 codec cost |
| `async_allocations_per_op`, `async_bytes_per_op` | allocs/op, B/op | Whether the erased path allocates per operation |

Indicative quick-run results (Windows 11, MSVC 19.51, x64, Release; median of
three repetitions; run-to-run variation is roughly +/-10%):

| Metric | Result |
|---|---|
| `mock_sync_write` | 1,858 MB/s |
| `mock_sync_read` | 796 MB/s |
| `erasure_overhead` | 1.04× |
| `mock_async_read_ops` | 824,800 ops/s |
| `mock_async_write_ops` | 806,100 ops/s |
| `mock_async_latency` (p50 / p99) | 1.4 µs / 1.6 µs |
| `tcp_sync_write` | 385 MB/s |
| `tcp_pingpong` (p50 / p99) | 45 µs / 84 µs |
| `telnet_encode` / `telnet_decode` | 585 / 345 MB/s |
| `async_allocations_per_op` | 1 allocation, 104 bytes |

CI runs the quick suite in Release on Linux, macOS and Windows. Each
`Performance (<os>)` job publishes the Markdown table to its summary, uploads
the JSON and the report as artifacts, and enforces `perf/floors.json`. The
floors are deliberately conservative sanity limits (for example 20 MB/s for the
mock paths), because shared runners are noisy; the artifacts are the source of
truth for comparing revisions. Render several runs side by side with:

```bash
python tools/perf_report.py perf-Linux.json perf-macOS.json perf-Windows.json
```

## 15. Changelog and releases

The history follows [Keep a Changelog 1.1.0](https://keepachangelog.com/en/1.1.0/)
and [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Pull requests
never edit `CHANGELOG.md`; they add one file to `changelog.d/` named
`<slug>.<category>.md`:

```
changelog.d/async-contract.added.md
changelog.d/1217.fixed.md
```

The category is one of `added`, `changed`, `deprecated`, `removed`, `fixed`
and `security`. Because every change owns its own file, two branches touching
different features cannot conflict in the changelog, and the release notes are
assembled from the files that actually merged.

```bash
python tools/changelog.py --check            # what CI runs
python tools/changelog.py --dry-run          # preview the Unreleased section
python tools/changelog.py                    # merge into [Unreleased]
python tools/changelog.py --release 1.1.0    # cut a dated release
```

`--release` requires a semantic version greater than every released one, moves
the entries into a dated section, empties `[Unreleased]` and refreshes the link
references at the bottom of the file. The workflow is documented in
[`changelog.d/README.md`](changelog.d/README.md), and the tool's behaviour is
pinned by [`tools/test_changelog.py`](tools/test_changelog.py).

## 16. Contributing

This repository has a single committer: its maintainer. Every commit is
authored and committed with the maintainer's identity, and no commit message
or file carries `Co-authored-by:`, `Signed-off-by:` or any other trailer, a
"generated by/with" statement, or AI/tool attribution. The policy is written
down in [`CONTRIBUTING.md`](CONTRIBUTING.md), checked locally by
[`tools/check_commit_message.py`](tools/check_commit_message.py) and the
`.githooks/commit-msg` hook, and enforced in CI by the `Commit authorship`
job.

```bash
git config core.hooksPath .githooks      # enable the local check once
python tools/check_commit_message.py --commit HEAD
```

## 17. License

Distributed under the [Boost Software License, Version 1.0](LICENSE_1_0.txt).
