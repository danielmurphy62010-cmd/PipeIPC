# QuickIPC

Windows-only, message-oriented inter-process communication in C++ with thin Python and Java bindings planned.

## Design

- Native Windows named pipes; no POSIX transport.
- Four logical channels: TX, RX, TX ACK, RX ACK.
- A broker fans each published message out to every connected process. A single shared FIFO is not used for broadcast because multiple readers would compete for bytes.
- Per-client queues isolate slow consumers.
- Protocol frames use two server-generated 8-byte magic values, a 16 MiB maximum payload, monotonically assigned message IDs, BLAKE3 header and payload digests, and an 8-byte listener-label identifier.
- ACK/NACK messages carry the exact original frame header. A redundant notification uses status byte `'2'`.
- Listeners are registered on a client instance, not globally. Hierarchical labels dispatch to exact and ancestor listeners only.
- Request/reply registration is exclusive per listener label.
- Shared-variable containers are independently named by listener label. The creator owns and serves variable requests; joining clients request the container's current variables.

## Shared value types

The wire format has distinct tags for Dict, List, Tuple, Set, Int64, Float64, Bool, Bytes, and None. Integers and floating-point values are always encoded as 64-bit values. List, Tuple, and Set are never conflated.

## Build

Requires Visual Studio 2022 (or another MSVC toolchain), CMake 3.24+, and the BLAKE3 C library. Configure with:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The BLAKE3 dependency is located with CMake's `find_package(blake3 CONFIG REQUIRED)`; provide a package exporting `blake3::blake3`.

## Status

Initial repository scaffold. See `docs/PROTOCOL.md` and `docs/SHARED_VARS.md` for wire and shared-state contracts.
