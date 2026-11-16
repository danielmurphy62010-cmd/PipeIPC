from quickipc import SharedVars

state = SharedVars("example")
state.set("message", "hello")
state.set("nested", {"numbers": [1, 2.5, True], "coords": (10, 20), "tags": {"ipc", "demo"}})
print(state.get("message"))
print(state.snapshot())
