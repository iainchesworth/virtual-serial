# Architectural blueprint

This document explains how Boost.VirtualSerial substitutes the transport of a
`boost::asio::serial_port` without breaking `io_context`, executors, strands,
allocators or the Asio stream concepts, and records the trade-offs behind the
implementation.

---

## 1. The constraint

`boost::asio::serial_port` is a concrete class:

```cpp
using serial_port = basic_serial_port<any_io_executor>;   // not a template parameter
```

It owns its native handle, its service, and its option types. There is no
virtual destructor to derive from, and no protocol template to substitute, so
the usual Asio escape hatch — *make the transport a template parameter* — does
not exist for serial ports. Four designs were considered:

| Option | Verdict |
|---|---|
| Derive from `serial_port` | Impossible: no virtual interface, non-virtual handle access. |
| Template all user code on `Stream` | Works, but every call site, every class holding a port and every translation unit becomes a template. Loses run-time selection and the typed option API. |
| `std::variant<Backend...>` | Compile-time closed set; no extension point for vendor backends; every visitor must be written by hand; `std::variant` needs C++17 while the library targets C++14. |
| **Custom type erasure + template front end** | Chosen. One vtable per port, not per operation; the same front-end code for both static and dynamic selection. |

The chosen structure has two layers:

```
basic_serial_port<Backend>        <- the entire public API, options, timeouts
    │
    ├── physical_port             (static, zero indirection)
    ├── mock_port
    ├── rfc2217_port
    ├── tcp_bridge_port
    ├── udp_bridge_port
    └── detail::erased_backend    (dynamic, one vtable call per operation)
            └── detail::endpoint ──▶ concrete backend via backend_model<B>
```

`any_serial_port` is not a second implementation. It is
`basic_serial_port<detail::erased_backend>`, so option forwarding, timeout
handling, error mapping and the async initiation machinery are **the same
code** in the tested and the production path. A behaviour difference between
compile-time and run-time selection is therefore a type-system contradiction,
not a test-coverage problem.

---

## 2. Where the OS handle is mocked

Asio requires a stream object to own, at minimum:

- `executor_type` and `get_executor()`;
- `read_some(buffers, ec)` / `write_some(buffers, ec)`;
- `async_read_some(buffers, token)` / `async_write_some(buffers, token)`.

Nothing in that contract mentions a file descriptor. The mock backend simply
never creates one: `native_handle()` returns `0`, and `read_some` copies bytes
out of an in-memory `std::deque`. Because `mock_port` implements the same
member functions, it satisfies the concepts exactly as `serial_port` does, and
`boost::asio::read`, `write`, `async_read`, `async_write`, `read_until` and
`streambuf` cannot tell the difference.

The type-erased adapter (`detail::endpoint` + `backend_model<B>`) is the only
place that knows a vtable exists. It exposes the same twelve operations, and
`backend_model<B>` forwards each one to the concrete backend.

### Recovering the concrete handle

The erased front end stores `typeid(Backend)` and a `void*` identity per
backend. `backend_cast<B>` compares type information and static-casts the
pointer back, so a test can keep a typed `shared_ptr<mock_port>` and inject
data even after the port has been erased:

```cpp
auto mock = vs::make_mock_port(io.get_executor());
vs::any_serial_port port(io.get_executor(), mock);   // erased
mock->inject_read("OK");                             // typed handle retained
```

With `BOOST_NO_RTTI` the cast returns `nullptr`; the typed handle remains the
supported route.

---

## 3. Executor fidelity — the hard part

Erasing a handler is easy. Erasing it *without losing the executor it is bound
to* is the part that breaks naive implementations, and the failure is subtle:

1. Asio resolves a handler's executor as
   `get_associated_executor(handler, fallback)`. The fallback must be the
   **I/O object's executor**; otherwise an unbound lambda completes on
   `system_executor`, i.e. on a system thread pool, outside the caller's
   `io_context`.
2. If the erased handler is invoked directly from a backend without going
   through the associated executor, a handler bound to a strand runs on the
   wrong thread, and `io_context::run()` may return before the handler has run
   because no outstanding work was registered.

`detail::any_completion` handles both. It stores the concrete handler and the
concrete type of its associated executor in the same erased box:

```cpp
template <class Handler>
any_completion(Handler&& handler, executor_type fallback)
{
    using executor_type = decay_t<decltype(
        boost::asio::get_associated_executor(handler, fallback))>;
    using model_type = model<decay_t<Handler>, executor_type>;

    // Allocated with the handler's associated allocator.
    impl_ = std::allocate_shared<model_type>(
        boost::asio::get_associated_allocator(handler),
        std::forward<Handler>(handler),
        boost::asio::get_associated_executor(handler, fallback));
}

void operator()(Args... args)
{
    std::shared_ptr<base> impl = std::move(impl_);
    if (!impl) return;
    impl->dispatch(std::move(args)...);      // posts through the captured executor
}
```

Three deliberate decisions live in those lines:

- **`get_associated_executor(handler, fallback)` with the I/O object's executor
  as fallback.** This is exactly what `basic_stream_socket` does internally.
  Without it, an unbound lambda would default to `system_executor` and run on
  a system thread pool, outside the caller's `io_context`.
- **The executor type is erased, not unified.** The obvious implementation
  stores `boost::asio::any_io_executor`, and it is wrong: not every executor
  can be held by `any_io_executor` (`use_future`'s promise executor cannot be
  queried for `execution::context`). Converting would either fail to compile
  or silently re-parent the completion. Erasing the *call* instead keeps every
  executor usable, including ones written after this library.
- **`post`, never `dispatch`.** Asio guarantees that a completion handler is
  not invoked before its initiating function returns. A backend that can
  satisfy a read immediately (the mock always can) would violate that
  guarantee if completion used `dispatch`, because `dispatch` executes inline
  when already on the right thread. `post` also registers outstanding work,
  which keeps `io_context::run()` alive until the handler has run.
- **`std::allocate_shared` with the handler's associated allocator.** The
  erased box is allocated from the same allocator the caller supplied for its
  composed operation, so custom allocation strategies still see the
  allocation. The type-erased *backend-side* operation state uses the default
  allocator, which is documented.

The erased handler is a first-class Asio handler: it exposes `executor_type`
and `get_executor()`, so Asio's own `associated_executor` machinery finds the
right executor when the erasure is wrapped again.

**Verified in tests:** a handler bound with `bind_executor(strand, ...)` runs
with `strand.running_in_this_thread() == true`, and `use_future` produces a
usable future. Both are exercised by `test/unit/any_serial_port_test.cpp`, and
`test/asio/async_contract_test.cpp` extends this to a handler bound to a
*second* `io_context` (it runs there, not on the port's) and to 100 handlers on
one strand distributed over four `io.run()` threads, where a concurrency
counter must never exceed one.

### Work tracking

An outstanding asynchronous operation must count as work; otherwise
`io_context::run()` returns while the operation is still pending. Native Asio
operations provide this implicitly, but a queued mock operation does not, so
`read_queue` and the mock's delayed-write queue each hold an
`executor_work_guard` for the lifetime of the operation. The async contract
suite asserts that `run()` on a worker thread is still running 100 ms after a
read or a delayed write is issued, that it returns once the operation
completes, and that `close()` releases the work. This was a real defect found
by that suite.

### Re-erasure is not re-wrapping

A backend receives a completion handler that was already erased by the front
end. Overload resolution prefers an rvalue reference binding to a const one,
so the generic constructor would win for an rvalue and wrap the handler a
second time — one extra allocation and one extra post per operation. Explicit
`any_completion(any_completion&&, executor_type)` and
`any_completion(any_completion const&, executor_type)` overloads keep adoption
the selected path; the async contract suite asserts that a single operation
runs exactly one handler.

---

## 4. Buffer lifetime

Asio requires the buffer *sequence* to stay valid for the duration of an
asynchronous operation, while ownership of the *memory* stays with the caller.
A backend whose concrete type is unknown cannot be handed the caller's
sequence type, so `detail::buffer_range<Buffer>` normalises the sequence into
descriptors:

- inline storage for up to eight descriptors — the overwhelmingly common case
  (one `buffer()` call) never allocates;
- a heap spill for larger scatter/gather lists;
- no payload bytes are ever copied.

`buffer_range` models the Asio `MutableBufferSequence` / `ConstBufferSequence`
concepts (it has `begin()`/`end()` over buffers), which means it can be handed
straight to `boost::asio::buffer_copy`, to `asio::ip::tcp::socket` or to a
platform `WSABUF`/`iovec` array. The tests call `buffer_copy` on it to prove
it.

Where bytes *are* copied, it is explicit and intentional:

| Path | Copy | Reason |
|---|---|---|
| `physical_port::async_read_some` | none | descriptor range handed to Asio, which copies descriptors into its op |
| mock read | one `memcpy` into the caller's buffers | the bytes live in the FIFO |
| mock write | one copy into `written_` | fixtures must observe writes |
| RFC 2217 write | one copy to escape `0xFF` | Telnet quoting changes the payload |
| RFC 2217 read | one copy into the read queue | the decoder consumes bytes independently of user buffers |
| TCP/UDP bridge | none | the descriptor range goes straight to the socket |

---

## 5. Timeouts and cancellation

Timeouts are a front-end feature so that every backend gets them, including
backends that have no notion of time (the mock's virtual clock). The
initiation path is:

```
async_read_some(buffers, token)
   └─ async_initiate                  (token → concrete handler)
        └─ start_read(range, erased_handler)
             ├─ no timeout ──▶ backend.async_read_some(...)
             └─ timeout ────▶ backend.async_read_some(wrapped_handler)
                               steady_timer(expires_after(timeout)).async_wait { cancel() }
```

The wrapper handler completes the user's handler **exactly once**, converts a
cancelled operation into `error::timeout`, and cancels the timer when data
arrives first.

The important subtlety is what happens on expiry. The timer handler does not
complete the user handler directly. It records `timed_out = true` and calls
`port.cancel()`, letting the backend's operation finish with
`operation_aborted`. Only then is the user handler invoked, with
`error::timeout`. That ordering guarantees the backend is no longer touching
the caller's buffers when the application regains control — the classic
timeout data race is closed.

Cancellation is therefore at the *port* level (as with
`serial_port::cancel()`), not per operation: aborting one read also aborts a
concurrent write. That is the Asio serial-port model, and it is documented
rather than hidden.

---

## 6. The backend contract

`detail::endpoint` is intentionally small and free of templates:

| Method | Purpose |
|---|---|
| `get_executor`, `is_open`, `kind` | observers |
| `open(target, ec)`, `close(ec)`, `cancel(ec)` | lifecycle |
| `read_some(range, ec)`, `write_some(range, ec)` | synchronous I/O |
| `async_read_some(range, completion)`, `async_write_some(range, completion)` | asynchronous I/O |
| `apply_options(config, ec)`, `options()` | option application |
| `native_handle()` | platform handle, or `0` |
| `target_address()`, `target_type()` | identity for `backend_cast` |

Adding a vendor backend requires implementing these fifteen member functions
plus the Asio stream quartet — no library changes, no registration, no
recompilation of the front end. That is the extension point a closed
`std::variant` cannot offer.

---

## 7. The mock engine

The mock is the reason the library exists, so its internal model is
deliberately simple and observable.

```
inject_read ──▶ stage_bytes ──▶ staged_ (due times) ──advance/step──▶ input_
                                                                       │
async_read_some ──▶ pending_ ◀──────────────────────── pump ───────────┘
```

- **Ordering.** A queued fault is delivered *before* the data that follows it,
  which is what a UART does: the error belongs to the character being
  received, not to the bytes behind it.
- **Synchronous reads never block.** A mock cannot block the calling thread
  while the test drives its clock, so `read_some` reports
  `boost::asio::error::would_block` when the FIFO is empty. The asynchronous
  API has no such restriction and is the documented route for blocking
  styles.
- **Virtual clock.** Time is a `std::chrono::nanoseconds` counter owned by the
  port. Every injected byte carries a due time derived from the framing
  (`start + data + parity + stop` bits over the baud rate), optionally
  offset by `read_latency`. `advance()` moves the clock and releases due
  bytes, `step(n)` releases exactly `n` bytes regardless of the clock, and
  `flush_input()` releases everything. No timer, no sleep, no flakiness.
- **Overflow.** The receive FIFO can be bounded; injections that exceed the
  capacity follow `overflow_policy` (`drop_newest` or `drop_oldest`), raise
  `error::overrun` on the next read and increment `overruns()`.

---

## 8. RFC 2217 layering

```
socket bytes ──▶ telnet::decoder ──┬──▶ data bytes ──▶ read_queue ──▶ user buffers
                                   ├──▶ negotiation events ──▶ WILL/DO/WONT/DONT replies
                                   └──▶ subnegotiation ──▶ SET-*/NOTIFY-* handling
                                                                  │
                                    acks ◀── apply_options ───────┘
```

- The decoder is a pure, allocation-light state machine over the byte stream;
  it survives command splits across TCP segments, which is the failure mode
  that naive Telnet implementations get wrong.
- `SET-*` commands are emitted only when a value changes, in the order the RFC
  recommends, once the port is configured.
- `NOTIFY-LINESTATE` bits are translated into the same `virtual_serial::error`
  values the mock injects, so application error handling is identical across
  hardware, simulation and network.
- Outgoing payloads escape `0xFF` as `0xFF 0xFF`; incoming payloads are
  unescaped by the decoder.
- The background read loop starts lazily on the first asynchronous read, so a
  purely synchronous caller drives the socket itself and two readers never
  compete for the same bytes.

---

## 9. Threading model

Asio's rule applies unchanged: distinct objects may be used from different
threads, shared objects must be serialised (typically with a strand). Every
backend keeps its state on the object; the mock's virtual clock is not atomic
because it is advanced by the test thread in the same way a real UART is
driven by hardware.

The front end never spawns threads, never uses global state, and never
introduces a scheduler of its own.

---

## 10. Performance notes

| Path | Per-operation cost |
|---|---|
| `basic_serial_port<mock_port>::async_read_some` | one allocation for the erased completion box, one posted completion |
| `any_serial_port::async_read_some` | the same allocation, plus one vtable dispatch; no second erasure, because the non-template re-erasure constructor shares the box |
| timeout-enabled operation | one extra control block for the timer state |
| `physical_port` | zero additional copies; Asio's native operation as usual |
| `tcp/udp` bridge | zero additional copies; the descriptor range is handed to the socket |
| `buffer_range` | no allocation up to eight descriptors |
| synchronous paths | no allocation at all |

The erased layer is a per-operation vtable call, not a per-byte one; measured
overhead is a few percent (see below), in the same class as `std::function`
dispatch.

### Measured behaviour

`perf/` measures the claims above, while
`test/asio/async_contract_test.cpp` pins the semantics. A quick run on
Windows 11 (MSVC 19.51, x64, Release, median of three repetitions) produced:

| Benchmark | Result |
|---|---|
| mock sync write / read | 1,858 / 796 MB/s |
| erased sync write | 1,781 MB/s |
| erasure overhead | 1.04x |
| async read / write operations | 824,800 / 806,100 ops/s |
| async read latency (p50 / p99) | 1.4 / 1.6 us |
| TCP loopback write | 385 MB/s |
| TCP ping-pong (p50 / p99) | 45 / 84 us |
| Telnet encode / decode | 585 / 345 MB/s |
| allocations per erased read | 1.0 (104 bytes) |

The allocation count confirms the "one box" claim, and the erasure ratio shows
that runtime switching costs a fixed few percent rather than a per-byte copy.
Numbers are machine-specific: CI publishes per-OS values as artifacts and
enforces only the conservative sanity floors in `perf/floors.json`.

---


## 11. C++20 coroutines and concepts

### Coroutines

Nothing in the library intercepts tokens: every operation is declared with
`async_initiate` and an arbitrary completion token, so
`boost::asio::use_awaitable` works exactly as it does on
`boost::asio::serial_port`.  The optional
`<boost/virtual_serial/awaitable.hpp>` adds four helpers (`read_some`,
`read_exactly`, `write_all`, `read_until`) that wrap the common composed
operations; they are ordinary awaitables running on the port's executor.

Consequences worth knowing:

- `co_await` on a timed-out read throws `boost::system::system_error` carrying
  `virtual_serial::error::timeout`, because the front-end timer completes the
  operation with that code.
- `co_spawn` and strands behave as usual; the handler's associated executor is
  the one the coroutine runs on.
- The helpers are compiled only when `BOOST_ASIO_HAS_CO_AWAIT` and C++20 are
  both available, so the C++14 build carries no coroutine machinery.

### Concepts

The C++20 concepts live in `<boost/virtual_serial/concepts.hpp>` and describe
the interface that `basic_serial_port`, `any_serial_port` and every backend
already model.  They are diagnostics, not constraints on the C++14 API: the
library never requires concepts to compile.  `AsyncSerialStream` checks the
executor type, `open`/`close`, and the synchronous and asynchronous read and
write operations, including their buffer-sequence overloads; `SerialBackend`
adds the backend-only members; `SerialOption` accepts the forwarded option
objects.

Both headers expand to nothing when the feature is unavailable, which keeps
`test/compat/cxx14_headers.cpp` meaningful.  Boost.Asio enables
`BOOST_ASIO_HAS_CO_AWAIT` per compiler and Boost release - 1.74, for example,
does not enable it for MSVC - so `cmake/DetectCxx20.cmake` compiles the same
probe the headers rely on and skips the coroutine targets on unsupported
combinations; concepts still build there.

## 12. Testing strategy

1. **Pure units** — URI grammar, option mapping, buffer range behaviour and
   the Telnet codec are tested without any I/O.
2. **Asynchronous contract** — `test/asio/async_contract_test.cpp` asserts the
   four Asio stream concepts at compile time and then proves work tracking,
   no-inline completion, exactly-once completion, associated-executor
   delivery, strand serialisation over four threads, generic composition and
   completion-token interoperability at run time.
3. **Fixture units** — every injection mode, fault ordering, clocking,
   capacity and statistics is asserted against `mock_port` directly.
4. **Front-end units** — the erased port is tested for option forwarding,
   timeouts, strand affinity, futures, composed operations and error mapping,
   using the mock as the backend.
5. **Loopback integration** — in-process TCP, UDP and RFC 2217 servers assert
   real wire behaviour, including IAC quoting and the exact subnegotiation
   bytes for the initial configuration.
6. **Mode matrix** — `serial_modes_test.cpp` runs every backend through both
   directions, synchronous and asynchronous, injects each line fault and then
   proves the next read succeeds, and repeats the exercise for timeouts, peer
   loss and reconnects.
7. **Hardware path** — `physical_loopback_test.cpp` opens two real devices.
   CI creates them with `tools/pty_loopback.py`; the tests skip on machines
   without a pair unless `VIRTUAL_SERIAL_REQUIRE_LOOPBACK=1`, which CI sets so
   a missing pair is a failure rather than a silent pass.
8. **Language level** — `test/compat/cxx14_headers.cpp` includes every public
   header and compiles as C++14; `cxx20_concepts.cpp` proves the same types
   satisfy the C++20 concepts.
9. **Platform matrix and coverage** — GitHub Actions runs the suite on Ubuntu
   22.04/24.04, macOS 14/15 and Windows 2022/2025 with GCC 11/12/13, Clang
   14/18, AppleClang and MSVC at C++14/17/20 against Boost 1.74 and 1.92. Three
   jobs instrument the primary test binary with LLVM profile data and fail
   the build below **90% line** or **80% branch** coverage; the report is
   emitted per header by `tools/coverage.py`. Windows runs the full suite but
   does not gate coverage, because clang-cl counts each template
   instantiation as a separate branch region.
10. **Performance** — `perf/` measures throughput, operation rate, latency
    percentiles, type-erasure cost, codec cost and allocations per operation.
    The `Performance (<os>)` jobs publish the table and JSON for every platform
    and enforce conservative floors, so a change that shifts the cost by an
    order of magnitude fails CI.
11. **C++20 features** — `test/concept/concepts_test.cpp` runs functions
    constrained by the public concepts, and `test/coroutine/awaitable_test.cpp`
    covers the awaitable helpers, partial delivery, a timeout and a TCP round
    trip.  Both are compiled only when the toolchain probe succeeds.
12. **Sanitizers and static analysis** — CI builds the full suite with
    AddressSanitizer + UndefinedBehaviorSanitizer on Linux and macOS and with
    ThreadSanitizer on Linux; CodeQL analyses the same build on every pull
    request and weekly.  `cmake/Sanitizers.cmake` exposes the sanitizer
    selection to users.

---

## 13. Known limitations and future work

- `open()` is synchronous, mirroring `serial_port::open`. An `async_open()`
  would be a natural addition for network backends.
- Per-operation cancellation slots are not forwarded through the erasure;
  cancellation is at the port level, as with `serial_port::cancel()`.
- The backend-side operation state uses the default allocator even when the
  caller supplied a custom one for its handler.
- `FLOWCONTROL-SUSPEND` from an RFC 2217 server is recorded and exposed via
  `flow_suspended()`, but not enforced, because configuration writes are
  synchronous.
- A TLS-wrapped RFC 2217 transport would be a new backend, not a change to
  the front end — which is exactly the property this architecture is meant to
  demonstrate.
- Windows has no user-mode serial pair, so the physical backend's success
  paths run under the pseudo-terminal pair in the Linux and macOS coverage
  jobs; the Windows coverage job still exercises every physical path that does
  not need a driver, and the Windows build matrix runs the real-hardware code
  paths used by `open()` failure, option application and closed-port
  operations.
