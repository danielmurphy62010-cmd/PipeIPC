# PipeIPC

PipeIPC is a Windows local IPC library built around named pipes. It provides two independent features over one broker:

- **Messages:** labeled broadcasts between connected clients. The server can also send messages to connected clients.
- **Shared variables:** a broker-managed key/value store synchronized to every connected client. Any connected client can create, replace, or delete any key; there is no creator-only ownership rule.

The native transport is C++20. Python bindings use pybind11, and Java bindings use JNI.

## Build

Run build.bat from a Visual Studio x64 developer environment. The script builds the native library and attempts the Python and Java bindings when their development headers and runtimes are available.

The build script downloads the pinned BLAKE3 C implementation and pybind11. Python and Java binding builds may be skipped if the matching development files are unavailable. The Python and Java examples are also used as smoke tests when their bindings are built.

## Python

The application-facing shared-variable API consists of normal instance attributes. There is no set(), get(), property, or setter API for application variables.

~~~python
from pipeipc import Server, Client, SharedVars

server = Server("my-app")
server.start()

client = Client("my-app")
client.connect()

state = SharedVars(client)
state.counter = 1
state.config = {"enabled": True, "ports": [8000, 8001]}

client.send("app.command", {"action": "reload", "force": True})
message = client.recv(5000)
if message is not None:
    label, value = message

# Nested built-in containers are scanned and hashed too.
state.config["enabled"] = False
state.config["ports"].append(8002)
state.config["tags"] = {"stable"}
state.config["tags"].add("local")

# Delete a variable by deleting its actual instance attribute.
del state.counter

# vars(state) is the local application-variable snapshot.
print(vars(state))

state.close()
client.disconnect()
server.stop()
~~~

send(label, value) accepts None, booleans, signed 64-bit integers, floats, strings, bytes, lists, tuples, sets/frozensets, and dictionaries with string keys. recv(timeout_ms=-1) blocks until a message arrives or the client disconnects; a nonnegative timeout returns None when it expires. on(label, callback) invokes the callback with (label, value) when that label arrives. Use recv for labels without a registered handler; messages matching an on subscription go to that callback instead of the receive queue.

Each Python SharedVars instance runs a 10 ms default updater. It hashes the real instance dictionary, compares it with the last-known hash table, sends creations and changed values, and sends deletions for hash-table entries whose attributes no longer exist. Incoming broker revisions refresh attributes that have not been edited locally. Nested list, tuple, set, and dictionary values are hashed recursively, so in-place container mutations are detected. Use one SharedVars object per client and close it before disconnecting the client.

## Java

Java applications subclass SharedVars and declare their application variables as mutable instance fields. Call startSync() in the subclass constructor after its field initializers have run. There are no application-facing set(), get(), property, or setter methods.

~~~java
import pipeipc.*;

final class RobotState extends SharedVars {
    public volatile Object counter = 0L;
    public volatile Object status = "idle";

    RobotState(Client client) {
        super(client);
        startSync();
    }
}

// After client.connect():
try (RobotState state = new RobotState(client)) {
    state.counter = 42L;
    state.status = "ready";
    System.out.println(state.counter);
    state.status = SharedVars.DELETE; // delete the shared variable
}
~~~

The updater scans the subclass's non-static, non-final, non-transient fields. It hashes scalar values and nested maps/lists/sets; a missing hash-table entry creates a variable, a changed hash updates it, and a hash-table entry with no corresponding live field is deleted. Use Object fields for values that need to accept arbitrary remote types or the SharedVars.DELETE sentinel. Declare mutable fields volatile when they are read or written by multiple application threads. Java values support null, booleans, integral/floating-point numbers, strings, byte arrays, lists, sets, and maps with string keys.

## Recording

varrec.py passively records all broadcast messages and shared-variable changes to a compact .vrec file. It uses its own broker client, does not register message handlers, and does not write or delete shared variables. Variables are sampled every 10 ms by default; two changes to the same variable within one poll interval may collapse into the later value, while messages are never collapsed.

~~~powershell
python varrec.py record my-app -o run.vrec
python varrec.py view run.vrec
python varrec.py view run.vrec --text
python varrec.py record --demo
~~~

The interactive viewer can step through events, play at adjustable speeds, inspect variable history, and browse recent messages. The recorder can also be embedded with Recorder(PipeTap("my-app"), "run.vrec").

## Native C++

~~~cpp
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

client.on("app.event", [](const std::string& label, const pipeipc::Value& value) {
    // Handle a matching message.
});
client.send("app.event", pipeipc::Value(std::string("handled by on")));

pipeipc::SharedVars vars(client);
vars.write("counter", pipeipc::Value(std::int64_t(1)));
auto current = vars.read("counter");
~~~

## Behavior and limits

- The broker namespace is the Server/Client name. Processes using different names do not communicate.
- The broker must be running before clients connect.
- Messages are broadcast to all connected clients, including the sender. recv() receives the next message without a registered handler; on(label, callback) handles matching labels instead of queueing them. Callbacks run on the receive thread and should hand long-running work to an application worker thread.
- Shared variables are volatile: stopping the broker clears the store. They are not persisted to disk.
- A raw message or encoded value is limited to 16 MiB. The server's per-client outbound queue defaults to 256 MiB.
- A client that exceeds the server's outbound queue limit is disconnected.
- PipeIPC uses local Windows named pipes and rejects remote named-pipe clients. The server does not implement per-message authentication or authorization; use it only among mutually trusted local processes.
- The internal __pipeipc.shared message label is reserved for synchronization and must not be used by applications.
