# PipeIPC

PipeIPC is a Windows local IPC library built around named pipes. It provides two independent features over one broker:

- **Messages:** labeled broadcasts between connected clients. The server can also send messages to connected clients.
- **Shared variables:** a broker-managed key/value store synchronized to every connected client. Any connected client can create, replace, or delete any key; there is no creator-only ownership rule.

The native transport is C++20. Python bindings use pybind11, and Java bindings use JNI.

## Build

Run `build.bat` from a Visual Studio x64 developer environment. The script builds the native library and attempts the Python and Java bindings when their development headers and runtimes are available.

The build script downloads the pinned BLAKE3 C implementation and pybind11. Python and Java binding builds may be skipped if the matching development files are unavailable. The Python and Java examples are also used as smoke tests when their bindings are built.

## Python

The message API is deliberately small: `send`, `recv`, and `on`.

```python
from pipeipc import Server, Client, SharedVars

server = Server("my-app")
server.start()

client = Client("my-app")
client.connect()

state = SharedVars(client)
state.counter = 1
state.config = {"enabled": True, "ports": [8000, 8001]}

client.send("app.command", {"action": "reload", "force": True})

# Receive the next message. Returns (label, value), or None on timeout.
label, value = client.recv(5000)

# Or handle a label with a callback.
client.on("app.command", lambda label, value: print(value))

state.counter = 2
del state.config

state.close()
client.disconnect()
server.stop()
```

`send(label, value)` accepts `None`, booleans, signed 64-bit integers, floats, strings, bytes, lists, tuples, sets/frozensets, and dictionaries with string keys. `recv(timeout_ms=-1)` blocks until a message arrives or the client disconnects; a nonnegative timeout returns `None` when it expires. `on(label, callback)` invokes the callback with `(label, value)` when that label arrives. Both `recv` and `on` can be used together.

## Java

```java
import pipeipc.*;

Server server = new Server("my-app");
server.start();

try (Client client = new Client("my-app")) {
    client.connect();

    try (SharedVars state = new SharedVars(client)) {
        state.set("counter", 1L);
        state.set("config", java.util.Map.of("enabled", true));

        client.send("app.command", java.util.Map.of("action", "reload"));
        Message message = client.recv(5000);
        if (message != null) System.out.println(message.label() + ": " + message.value());

        client.on("app.command", (label, value) -> System.out.println(value));
    }
}
server.close();
```

Java message values support null, booleans, integral and floating-point numbers, strings, byte arrays, lists, sets, and maps with string keys. `Message` is the value returned by `Client.recv()`; `MessageListener` is used by `Client.on()`.

## Native C++

```cpp
#include "pipeipc.hpp"

pipeipc::Server server;
server.start();

pipeipc::Client client;
client.connect();

client.send("app.command", pipeipc::Value(std::string("reload")));
auto message = client.recv(std::chrono::seconds(5));
if (message) {
    // message->label and message->value
}

client.on("app.command", [](const std::string& label, const pipeipc::Value& value) {
    // Handle a matching message.
});

pipeipc::SharedVars vars(client);
vars.write("counter", pipeipc::Value(std::int64_t(1)));
auto current = vars.read("counter");
```

A `SharedVars` object requires an already-connected client, waits up to ten seconds for the initial snapshot, and must be destroyed before its C++ `Client`. Use one `SharedVars` object per client. Values written locally are updated optimistically; the broker then propagates the update to connected clients. Variable updates are asynchronous across clients. Concurrent writes are applied in the order processed by the broker, with the last processed write determining the final value.

## Behavior and limits

- The broker namespace is the `Server`/`Client` name. Processes using different names do not communicate.
- The broker must be running before clients connect.
- Messages are broadcast to all connected clients, including the sender. `recv()` receives the next queued message; `on(label, callback)` invokes a callback for matching labels. Callbacks run on the receive thread and should hand long-running work to an application worker thread.
- Shared variables are volatile: stopping the broker clears the store. They are not persisted to disk.
- A raw message or encoded value is limited to 16 MiB. The server's per-client outbound queue defaults to 256 MiB.
- A client that exceeds the server's outbound queue limit is disconnected.
- PipeIPC uses local Windows named pipes and rejects remote named-pipe clients. The server does not implement per-message authentication or authorization; use it only among mutually trusted local processes.
- The internal `__pipeipc.shared` message label is reserved for synchronization and must not be used by applications.
