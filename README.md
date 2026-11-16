# QuickIPC

Windows-only C++ IPC library with native named pipes, per-peer broadcast queues, BLAKE3-framed messages, listener routing, typed shared values, and C++ client/server APIs.

## Build

Visual Studio 2022, CMake 3.24+, and internet access for CMake FetchContent (BLAKE3).

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Start `quickipc_server.exe`, then run any number of `quickipc_client.exe` processes.

## Important current limitations

This release provides the native transport skeleton and typed serialization, but request/reply routing and shared-variable snapshot exchange are not yet connected to the wire protocol. The current client callback dispatch also needs a label registry (the frame carries only its 8-byte label hash). These methods fail explicitly rather than silently pretending to work. Do not use this version as a production IPC service until those paths and Windows integration tests are completed.

See docs/ for the frame and value contracts.
