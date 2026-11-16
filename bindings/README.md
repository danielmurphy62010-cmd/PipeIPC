# Language interfaces

Both bindings expose the C++ `SharedVars` value container. They currently wrap the existing in-process C++ class; they do not add cross-process snapshot/update transport.

## Python

Build with CMake after installing a 64-bit Python 3.10+ development environment:

```powershell
cmake -S . -B build -A x64 -DQUICKIPC_BUILD_TESTS=ON -DQUICKIPC_BUILD_PYTHON=ON
cmake --build build --config Release
$env:PYTHONPATH = "$PWD/build/Release"
python examples/python_example.py
```

```python
from quickipc import SharedVars
state = SharedVars("game")
state.set("player", {"name": "Ada", "position": (10, 20), "tags": {"admin"}})
print(state.get("player"))
```

Supported values: None, bool, signed 64-bit int, float, str (UTF-8), bytes, list, tuple, set/frozenset, and dict with string keys. Custom Python objects must be converted to these types by the application.

## Java

Build with a 64-bit JDK (JNI headers and JVM) and CMake:

```powershell
cmake -S . -B build -A x64 -DQUICKIPC_BUILD_JAVA=ON
cmake --build build --config Release
javac -d build/java bindings/java/quickipc/SharedVars.java examples/JavaExample.java
java -Djava.library.path=build/Release -cp build/java JavaExample
```

Java values: null, String, Boolean, Byte/Short/Integer/Long, Float/Double, byte[], List, Set, and Map with String keys. Java List is used for both C++ List and Tuple on return.

The bindings reject unsupported objects instead of stringifying them or silently losing type information. A value must fit the C++ wire-format limits and supported types.
