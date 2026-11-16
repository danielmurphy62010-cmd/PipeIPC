# Language interfaces

The application-facing API is attribute/field based. There are no public `set` or `get` calls.

## Python

```powershell
cmake -S . -B build -A x64 -DQUICKIPC_BUILD_TESTS=ON -DQUICKIPC_BUILD_PYTHON=ON
cmake --build build --config Release
$env:PYTHONPATH = "$PWD/build/Release"
python examples/python_example.py
```

```python
from quickipc import SharedVars
state = SharedVars("game")
state.player = {"name": "Ada", "position": (10, 20), "tags": {"admin"}}
print(state.player)
del state.player
```

Assign and read attributes directly. `snapshot()` returns a normal dictionary for enumeration; `"name" in state` checks existence. Names beginning with underscore are reserved.

Supported values: None, bool, signed 64-bit int, float, UTF-8 str, bytes, list, tuple, set/frozenset, and dict with string keys.

## Java

Java has no dynamic instance attributes, so declare the shared fields in a subclass. The fields must be non-static, non-final, non-transient, and public or reflectively accessible.

```java
public final class GameState extends SharedVars {
    public Object player;
    public GameState(String label) { super(label); }
}
try (GameState state = new GameState("game")) {
    state.player = Map.of("name", "Ada");
    System.out.println(state.player);
}
```

Build with a 64-bit JDK (JNI headers and JVM) and CMake:

```powershell
cmake -S . -B build -A x64 -DQUICKIPC_BUILD_JAVA=ON
cmake --build build --config Release
javac -d build/java bindings/java/quickipc/SharedVars.java examples/JavaExample.java
java -Djava.library.path=build/Release -cp build/java JavaExample
```

Java values: null, String, Boolean, Byte/Short/Integer/Long, Float/Double, byte[], List, Set, and Map with String keys. Java List represents both C++ List and Tuple when converted back.

**Important status:** the current C++ SharedVars storage and these bindings are an in-process value container only. They do not yet synchronize attributes between separate processes over the named-pipe runtime. Cross-process propagation remains unimplemented.
