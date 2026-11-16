# QuickIPC

Windows-only C++20 IPC library using named pipes, BLAKE3-framed messages, listener routing, typed values, and C++ client/server APIs.

## Layout

All project source files, headers, bindings, examples, and tests are in the repository root. Build output is temporary; the build script copies finished executables and bindings into the root.

## Build (CMD)

Run `build.bat` from CMD. It uses the MSVC compiler already configured in the environment; Visual Studio and CMake are not required.

The script downloads BLAKE3 1.5.5 into the temporary build directory. If Python is available, it installs pybind11 2.13.6 if needed and builds/tests `quickipc.pyd`. Java is built when a JDK with JNI headers is available; otherwise that binding is skipped.

Outputs: `quickipc_core.lib`, `quickipc_server.exe`, `quickipc_client.exe`, `quickipc_tests.exe`, optionally `quickipc.pyd`, `quickipc_jni.dll`, and `quickipc.jar`.

Start `quickipc_server.exe`, then run any number of `quickipc_client.exe` processes.

## Language APIs

Python uses direct attribute access (no public set/get methods):

```python
from quickipc import SharedVars
state = SharedVars("game")
state.player = {"name": "Ada", "position": (10, 20), "tags": {"admin"}}
print(state.player)
del state.player
```

Java declares fields in a subclass because Java has no dynamic instance attributes:

```java
public final class GameState extends SharedVars {
    public Object player;
    public GameState(String label) { super(label); }
}
```

Supported values include null/None, booleans, signed 64-bit integers, floats, strings, bytes, lists, tuples, sets, and dictionaries/maps with string keys.

## Current limitations

Request/reply routing and shared-variable snapshot exchange are not connected to the wire protocol. The current SharedVars storage and language bindings are in-process containers; they do not synchronize attributes between separate processes. The client callback dispatch also requires a matching local label registration. Do not treat this version as production-ready IPC.
