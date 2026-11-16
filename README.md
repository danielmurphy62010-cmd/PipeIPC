# PipeIPC

PipeIPC is a Windows local IPC library built around named pipes. It provides two independent features over one broker:

- **Messages:** labeled broadcasts between connected clients. A client can also receive messages published by the server.
- **Shared variables:** a broker-managed key/value store synchronized to every connected client. Any connected client can create, replace, or delete any key; there is no creator-only ownership rule.

The native transport is C++20. Python bindings use pybind11, and Java bindings use JNI.

## Build

Run `build.bat` from a Visual Studio x64 developer environment. The script builds the native library and attempts the Python and Java bindings when their development headers and runtimes are available.

The build script downloads the pinned BLAKE3 C implementation and pybind11. Python and Java binding builds may be skipped if the matching development files are unavailable. The Python and Java examples are also used as smoke tests when their bindings are built.

## Python

Start one broker for a namespace, then connect each participating process using the same namespace:

```python
from pipeipc import Server, Client, SharedVars

server = Server("my-app")
server.start()

client = Client("my-app")
client.connect()

state = SharedVars(client)  # waits for the current broker snapshot
state.counter = 1
state.config = {"enabled": True, "ports": [8000, 8001]}

print(state.counter)
print(state.snapshot())

def handle_message(label, payload):
    print(label, payload)

client.on("app.log", handle_message)           # raw bytes
client.publish("app.log", b"worker started")

client.on_value("app.command", lambda label, value: print(value))
client.send("app.command", {"action": "reload", "force": True})

# Any connected client can update or delete the same keys.
state.counter = 2
del state.config

state.close()
client.disconnect()
server.stop()
```

`Client.publish()` and `Server.publish()` send raw bytes. `Client.send()` and `Server.send()` encode a PipeIPC value; receive those messages with `Client.on_value()`. Supported Python values are `None`, booleans, signed 64-bit integers, floats, strings, bytes, lists, tuples, sets/frozensets, and dictionaries with string keys.

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
        System.out.println(state.get("counter"));

        client.on("app.log", (label, payload) ->
            System.out.println(label + ": " + new String(payload)));
        client.publish("app.log", "worker started".getBytes());
    }
}
server.close();
```

Java message payloads are byte arrays. Java shared values support null, booleans, integral and floating-point numbers, strings, byte arrays, lists, sets, and maps with string keys. Java clients use `MessageListener.onMessage(String, byte[])` for incoming messages.

## Native C++

```cpp
#include "pipeipc.hpp"

pipeipc::Server server;
server.start();

pipeipc::Client client;
client.connect();

client.on("app.log", [](const std::string& label, const pipeipc::Bytes& bytes) {
    // Handle a message.
});
client.publish("app.log", pipeipc::Bytes{'o', 'k'});

pipeipc::SharedVars vars(client);
vars.write("counter", pipeipc::Value(std::int64_t(1)));
vars.write("config", pipeipc::Value(std::string("ready")));
auto current = vars.read("counter");
```

A `SharedVars` object requires an already-connected client and waits up to ten seconds for the initial snapshot. Use one `SharedVars` object per client. In native C++, destroy `SharedVars` before its `Client`. Values written locally are updated optimistically; the broker then publishes the committed update to connected clients. Variable updates are asynchronous across clients. Concurrent writes are applied in the order processed by the broker, with the last processed write determining the final value.

## Behavior and limits

- The broker namespace is the `Server`/`Client` name. Processes using different names do not communicate.
- The broker must be running before clients connect.
- Messages are broadcast to all connected clients, including the sender. A client receives a label only after registering it with `on` or `on_value`. Callbacks run on the receive thread and should hand long-running work to an application worker thread.
- Shared variables are volatile: stopping the broker clears the store. They are not persisted to disk.
- A raw message or encoded value is limited to 16 MiB. The server's per-client outbound queue defaults to 256 MiB.
- A client that exceeds the server's outbound queue limit is disconnected.
- PipeIPC uses local Windows named pipes and rejects remote named-pipe clients. The server does not implement per-message authentication or authorization; use it only among mutually trusted local processes.
- The internal `__pipeipc.shared` message label is reserved for synchronization and must not be used by applications.
