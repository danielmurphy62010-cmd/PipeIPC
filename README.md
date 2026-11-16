# QuickIPC

Windows-only C++20 named-pipe IPC prototype.

## Files

- `quickipc.hpp` / `quickipc.cpp`: entire native API and implementation.
- `module.cpp`: Python extension.
- `jni.cpp` / `SharedVars.java`: Java binding.
- `example.cpp`: run as server with `quickipc.exe server`, or as client with `quickipc.exe`.
- `tests.cpp`: native tests.
- `python_example.py`, `JavaExample.java`: binding examples.
- `build.bat`: CMD-only build; no CMake or Visual Studio installation required when the MSVC compiler environment is already configured.

Run `build.bat` from CMD. It fetches the five small BLAKE3 source/header files into a temporary build folder, compiles and runs the C++ tests, and builds the optional Python and Java bindings when their development toolchains are present.

## Limits

The current SharedVars API is an in-process container; it does not synchronize data between processes. Request/reply is not implemented. This is not production-ready IPC.
