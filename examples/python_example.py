from quickipc import SharedVars

state = SharedVars("example")
state.message = "hello"
state.nested = {
    "numbers": [1, 2.5, True],
    "coords": (10, 20),
    "tags": {"ipc", "demo"},
}
print(state.message)
print(state.nested)
print(state.snapshot())
del state.message
