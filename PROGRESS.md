# Implementation Progress

## Status: Phase 2 — Service Layer (Complete)

Last updated: 2026-02-12

------------------------------------------------------------------------

## Implementation Phases

### Phase 1a: Project Setup — DONE
- [x] CMake project structure (src/, tests/, include/)
- [x] Google Test integration via FetchContent
- [x] Basic build + empty test target compiles

### Phase 1b: Ring Buffer — DONE
- [x] Ring buffer implementation (lock-free, SPSC, header-only template)
- [x] 8 unit tests (single write/read, sequential, wraparound, full, empty, peek, skip, capacity)

### Phase 1c: RunLoop — DONE
- [x] RunLoop implementation (epoll-based, pipe wakeup)
- [x] 9 unit tests (init, run/stop, stop-before-run, stop-from-callable, destructor, runOnThread, multi-thread posts, post order, restart)

### Phase 2: Service Layer (Done)
- [x] Platform abstraction layer (shared memory, sockets, epoll fd management)
- [x] Service class — server-side (listen, accept, connection management)
- [x] Client class — client-side (connect, handshake, shared memory setup)
- [x] call() and notify() on Service/Client
- [x] Frame encoding/decoding (24-byte header)
- [x] Connection tests, RPC tests, notification tests

### Phase 3: IDL Parser + Code Generator (Python)
- [ ] IDL lexer/tokenizer
- [ ] IDL parser (based on EBNF grammar in spec)
- [ ] AST representation
- [ ] C++ code emitter — client stubs, server skeletons, marshalling
- [ ] Sample IDL file for testing

### Phase 4: Integration Tests
- [ ] End-to-end with generated StorageService stubs
- [ ] Multi-client notification delivery with generated code
- [ ] All IDL data types (scalars, enums, structs, arrays, strings)

### Phase 5: Python Bindings (Future)
- [ ] pybind11 wrapper for RunLoop
- [ ] Python client/server stub generation

### Phase 6: macOS Support (Future)
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

1. **RunLoop is a pure event loop** — init(name), run(), stop(),
   runOnThread(fn). No RPC, transport, or service knowledge.
   Renamed from EventDispatcher (name was taken).
2. **call() and notify() belong to Service class** — not RunLoop.
3. **Generated class hierarchy** — Server: user subclasses FooSkeleton with
   virtual handleXxx() methods. Client: generated FooClient with RPC call
   methods + virtual onXxx() notification callbacks.
4. **Sample IDL added** — StorageService example in Requirements.md section 6.4.
5. **Connection handshake** — Client creates memfd, sends FD + version over UDS.
   Server validates, mmaps, ACKs. Linux abstract namespace for socket path.
   macOS differences bookmarked with MACOS_BOOKMARK comments in spec.
6. **Ring buffer size** — 256KB per direction, power of 2, compile-time constant.
7. **Error codes** — Negative = framework, 0 = success, positive = user-defined.
8. **Notification flow** — Generated skeleton has non-virtual notify*() methods.
   Server app calls them, skeleton marshals and broadcasts to all clients.
9. **IDL types** — Language-agnostic built-in names (uint32, string, etc.).
   Code generator maps to target language. Typedefs supported.
10. **Code generator** — Python.
11. **Build system** — CMake 3.14+, C++17, Google Test via FetchContent.
12. **C++ coding standards** — Member variables use `m_` prefix.

------------------------------------------------------------------------

## Build & Run Notes

Build system: CMake 3.14+
C++ standard: C++17
Test framework: Google Test (gtest) via FetchContent
Run tests: `cd build && cmake .. && make && ctest --output-on-failure`
