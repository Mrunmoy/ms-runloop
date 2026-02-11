# Implementation Progress

## Status: Pre-implementation (Design Complete)

Last updated: 2026-02-11

------------------------------------------------------------------------

## Implementation Phases

### Phase 1a: Project Setup
- [ ] CMake project structure (src/, tests/, include/)
- [ ] Google Test integration via FetchContent
- [ ] Basic build + empty test target compiles

### Phase 1b: Ring Buffer
- [ ] Ring buffer implementation (lock-free, SPSC)
- [ ] Ring buffer unit tests (tests 1-5 from spec section 11.2)

### Phase 1c: UDS + Shared Memory Connection
- [ ] Platform abstraction layer (shared memory, sockets, event loop)
- [ ] Service::Create() — server-side endpoint creation (bind, listen)
- [ ] Client connect() — blocking connection with retry + backoff
- [ ] Shared memory setup and FD exchange handshake
- [ ] Connection unit tests (tests 6-10 from spec section 11.2)

### Phase 1d: Frame Encoding + Dispatch Loop
- [ ] Frame encoding/decoding (24-byte header)
- [ ] Dispatch loop (epoll-based, multiplexing multiple clients)
- [ ] `call()` — synchronous RPC with seq correlation + condvar
- [ ] RPC unit tests (tests 11-15 from spec section 11.2)

### Phase 1e: Notifications
- [ ] `notify()` — async notification broadcast to connected peers
- [ ] Notification unit tests (tests 16-19 from spec section 11.2)

### Phase 1f: Lifecycle + Error Handling
- [ ] `stop()` with deterministic cleanup
- [ ] Multi-client connection management
- [ ] Lifecycle unit tests (tests 20-23 from spec section 11.2)
- [ ] Crash/error handling tests (tests 24-26 from spec section 11.2)

### Phase 2: IDL Parser + Code Generator (Python)
- [ ] IDL lexer/tokenizer
- [ ] IDL parser (based on EBNF grammar in spec)
- [ ] AST representation
- [ ] C++ code emitter — client stubs
- [ ] C++ code emitter — server skeletons
- [ ] C++ code emitter — marshalling/unmarshalling functions
- [ ] C++ code emitter — notification senders
- [ ] C++ code emitter — dispatch tables (handleMessage switch/case)
- [ ] Sample IDL file for testing

### Phase 3: Integration Tests
- [ ] End-to-end with generated StorageService stubs
- [ ] Multi-client notification delivery with generated code
- [ ] All IDL data types (scalars, enums, structs, arrays, strings)

### Phase 4: Python Bindings (Future)
- [ ] pybind11 wrapper for EventDispatcher
- [ ] Python client stub generation
- [ ] Python server skeleton generation

### Phase 5: macOS Support (Future)
- [ ] `shm_open` backend for shared memory
- [ ] `SOCK_STREAM` backend for UDS
- [ ] `kqueue` backend for event loop

------------------------------------------------------------------------

## Open Questions

All resolved. See decisions log below.

### Q1: Handler registration API
**Decision: RESOLVED** — No registration API. Generated skeleton/client
classes have a `handleMessage()` that switch/cases on methodId/notifyId
and calls virtual methods. User subclasses the skeleton (server) or
client (for notification callbacks). Service::Create() and
client.connect() wire it up.

### Q2: Connection handshake sequence
**Decision: RESOLVED** — Client creates memfd and sends FD + version to
server via SCM_RIGHTS. Server validates version, mmaps, sends ACK/NACK.
Linux uses abstract namespace for socket path (\0rpc_<serviceName>).
macOS will use filesystem path (bookmarked with MACOS_BOOKMARK comments).

### Q3: Ring buffer default size
**Decision: RESOLVED** — 256KB per direction (512KB total per connection).
Power of 2, compile-time constant.

### Q4: Error code conventions
**Decision: RESOLVED** — Negative = framework errors (RPC_ERR_DISCONNECTED,
etc.), 0 = success, positive = user/service-defined errors.

### Q5: IDL primitive type names
**Decision: RESOLVED** — Language-agnostic names: uint8, uint16, uint32,
uint64, int8, int16, int32, int64, float32, float64, bool, string.
Code generator maps to target language types. Users can typedef aliases.

### Q6: Code generator language
**Decision: RESOLVED** — Python. Easy to write, easy to maintain, good
string/template handling for code generation.

------------------------------------------------------------------------

## Decisions Log

1. **EventDispatcher is a pure event loop** — no init(), no endpoint name.
   Connection setup is done by generated Service::Create() (server) and
   client.connect() (client). Dispatcher just runs run()/stop().
2. **Generated class hierarchy** — Server: user subclasses FooSkeleton with
   virtual handleXxx() methods. Client: generated FooClient with RPC call
   methods + virtual onXxx() notification callbacks.
3. **Sample IDL added** — StorageService example in Requirements.md section 6.4.
4. **Connection handshake** — Client creates memfd, sends FD + version over UDS.
   Server validates, mmaps, ACKs. Linux abstract namespace for socket path.
   macOS differences bookmarked with MACOS_BOOKMARK comments in spec.
5. **Ring buffer size** — 256KB per direction, power of 2, compile-time constant.
6. **Error codes** — Negative = framework, 0 = success, positive = user-defined.
7. **Notification flow** — Generated skeleton has non-virtual notify*() methods.
   Server app calls them, skeleton marshals and broadcasts to all clients.
8. **IDL types** — Language-agnostic built-in names (uint32, string, etc.).
   Code generator maps to target language. Typedefs supported.
9. **Code generator** — Python.
10. **Build system** — CMake 3.14+, C++17, Google Test via FetchContent.
11. **Test-first approach** — EventDispatcher runtime implemented and fully
    tested (26 unit tests) before IDL parser / code generator work begins.

------------------------------------------------------------------------

## Build & Run Notes

Build system: CMake 3.14+
C++ standard: C++17
Test framework: Google Test (gtest) via FetchContent
Run tests: `cd build && cmake .. && make && ctest --output-on-failure`
