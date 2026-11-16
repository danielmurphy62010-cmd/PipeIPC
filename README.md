# QuickIPC

Windows-only C++20 named-pipe IPC prototype.

## Build

Run `build.bat` in CMD. It uses the configured MSVC compiler, fetches BLAKE3 into a temporary folder, and builds the native example/tests plus available language bindings. No CMake or Visual Studio installation is required.

Python is built when the active `python` has development headers and its matching import library. Java requires a full JDK (including `include\\jni.h`); a JRE alone cannot build the JNI DLL.

To select a JDK before building:

```bat
set "JAVA_HOME=C:\\Path\\To\\jdk-21"
set "JBIN=%JAVA_HOME%\\bin"
build.bat
```

## Python

After a successful Python binding build, `quickipc.pyd` is placed beside this README. From this folder:

```bat
python python_example.py
```

Import it as a normal Python module:

```python
from quickipc import SharedVars

state = SharedVars("example")
state.message = "hello"
print(state.message)
print(state.snapshot())
```

Use the same 64-bit Python version that built `quickipc.pyd`. If running your script from another folder, add this repository folder to `PYTHONPATH`.

## Java

After a successful Java binding build, `quickipc.jar` and `quickipc_jni.dll` are placed beside this README. Import the class with:

```java
import quickipc.SharedVars;
```

Java requires a subclass to declare its fields:

```java
public final class State extends SharedVars {
    public Object message;
    public State(String label) { super(label); }
}
```

Compile and run an application from this folder:

```bat
javac -cp quickipc.jar MyApp.java
java -Djava.library.path=. -cp ".;quickipc.jar" MyApp
```

The JNI DLL must remain available at runtime. `-Djava.library.path=.` tells Java to find it in the current directory.

## Native example

```bat
quickipc.exe server
quickipc.exe
```

Run the first command in one CMD window and the second in another.

## Current limitations

`SharedVars` is currently an in-process value container. It does not synchronize attributes between separate processes. Request/reply is not implemented. This is a prototype, not production-ready IPC.
