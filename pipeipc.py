"""PipeIPC Python API: shared values are ordinary instance attributes.

Use state.name = value, mutate supported containers in place, and use
del state.name to remove a shared variable. A background thread hashes the
instance dictionary and sends only creates, updates, and deletions.
"""
from __future__ import annotations

import copy
import hashlib
import math
import struct
import threading
import weakref
from typing import Any

from _pipeipc import Client, Server, _SharedVarsBackend

__all__ = ["Client", "Server", "SharedVars"]

_STATE_LOCK = threading.RLock()
_STATES: weakref.WeakKeyDictionary = weakref.WeakKeyDictionary()


def _state_for(owner: "SharedVars") -> dict[str, Any]:
    with _STATE_LOCK:
        return _STATES[owner]


def _value_hash(value: Any) -> bytes:
    """Stable type-aware hash for the value types accepted by PipeIPC."""
    digest = hashlib.blake2b(digest_size=16)

    def part(tag: bytes, raw: bytes = b"") -> None:
        digest.update(tag)
        digest.update(len(raw).to_bytes(8, "little"))
        digest.update(raw)

    def visit(item: Any) -> None:
        if item is None:
            part(b"N")
        elif isinstance(item, bool):
            part(b"B", b"\x01" if item else b"\x00")
        elif isinstance(item, int):
            part(b"I", str(item).encode("ascii"))
        elif isinstance(item, float):
            part(b"D", b"nan" if math.isnan(item) else struct.pack("<d", item))
        elif isinstance(item, str):
            part(b"S", item.encode("utf-8", "surrogatepass"))
        elif isinstance(item, bytes):
            part(b"Y", item)
        elif isinstance(item, list):
            part(b"L", len(item).to_bytes(8, "little"))
            for child in item:
                visit(child)
        elif isinstance(item, tuple):
            part(b"T", len(item).to_bytes(8, "little"))
            for child in item:
                visit(child)
        elif isinstance(item, dict):
            if any(not isinstance(key, str) for key in item):
                raise TypeError("shared dictionary keys must be strings")
            keys = sorted(item)
            part(b"M", len(keys).to_bytes(8, "little"))
            for key in keys:
                visit(key)
                visit(item[key])
        elif isinstance(item, (set, frozenset)):
            encoded = sorted(_value_hash(child) for child in item)
            part(b"E", len(encoded).to_bytes(8, "little"))
            for child_hash in encoded:
                part(b"H", child_hash)
        else:
            raise TypeError(f"unsupported shared value type: {type(item).__name__}")

    visit(value)
    return digest.digest()


def _hash_snapshot(values: dict[str, Any]) -> dict[str, bytes]:
    return {str(key): _value_hash(value) for key, value in values.items()}


class SharedVars:
    """Shared variables exposed as normal instance attributes.

    Assign with state.name = value, mutate nested built-in containers normally,
    and remove a variable with del state.name. The instance dictionary contains
    only application variables; use vars(state) for a local snapshot.
    """

    __slots__ = ("__dict__", "__weakref__")

    def __init__(self, client: Client, poll_interval: float = 0.01):
        if poll_interval <= 0:
            raise ValueError("poll_interval must be positive")

        backend = _SharedVarsBackend(client)
        initial = copy.deepcopy(backend.snapshot())
        stop = threading.Event()
        state = {
            "backend": backend,
            "stop": stop,
            "sync_lock": threading.RLock(),
            "hash_table": _hash_snapshot(initial),
            "revision": backend.revision,
            "sync_error": None,
            "poll_interval": float(poll_interval),
            "closed": False,
            "thread": None,
        }
        with _STATE_LOCK:
            _STATES[self] = state
        self.__dict__.update(initial)

        thread = threading.Thread(
            target=SharedVars._watch_ref,
            args=(weakref.ref(self), stop, state["poll_interval"]),
            name="PipeIPC-SharedVars",
            daemon=True,
        )
        state["thread"] = thread
        thread.start()

    @staticmethod
    def _watch_ref(owner_ref, stop: threading.Event, interval: float) -> None:
        while not stop.wait(interval):
            owner = owner_ref()
            if owner is None:
                return
            try:
                SharedVars._sync_once(owner)
            except Exception as exc:
                try:
                    _state_for(owner)["sync_error"] = exc
                except Exception:
                    pass
            finally:
                del owner

    @staticmethod
    def _copy_dict(values: dict[str, Any]) -> dict[str, Any]:
        return copy.deepcopy(dict(values))

    def _sync_once(self) -> None:
        state = _state_for(self)
        with state["sync_lock"]:
            if state["closed"]:
                return

            attrs = self.__dict__
            current = SharedVars._copy_dict(attrs)
            current_hashes = _hash_snapshot(current)
            hash_table = state["hash_table"]

            # Keep local edits, but refresh every untouched attribute from a
            # newer broker revision.
            dirty = {
                key for key in set(hash_table) | set(current_hashes)
                if hash_table.get(key) != current_hashes.get(key)
            }

            revision = state["backend"].revision
            if revision > state["revision"]:
                remote = state["backend"].snapshot()
                stable_revision = state["backend"].revision
                if stable_revision != revision:
                    remote = state["backend"].snapshot()
                    stable_revision = state["backend"].revision

                if stable_revision > state["revision"]:
                    for key in set(hash_table) | set(current_hashes) | set(remote):
                        if key in dirty:
                            continue
                        if key in remote:
                            attrs[key] = copy.deepcopy(remote[key])
                        else:
                            attrs.pop(key, None)

                    hash_table = _hash_snapshot(remote)
                    state["hash_table"] = hash_table
                    state["revision"] = stable_revision
                    current = SharedVars._copy_dict(attrs)
                    current_hashes = _hash_snapshot(current)

            failed = False
            for key in sorted(set(hash_table) | set(current_hashes)):
                if key not in current_hashes:
                    try:
                        state["backend"]._erase(key)
                        hash_table.pop(key, None)
                    except Exception:
                        failed = True
                elif hash_table.get(key) != current_hashes[key]:
                    try:
                        state["backend"]._set(key, current[key])
                        hash_table[key] = current_hashes[key]
                    except Exception:
                        failed = True

            state["sync_error"] = (
                RuntimeError("One or more shared-variable changes could not be synchronized")
                if failed else None
            )

    def close(self) -> None:
        state = _state_for(self)
        with state["sync_lock"]:
            if state["closed"]:
                return
            state["stop"].set()

        thread = state["thread"]
        if thread is not None and thread.is_alive() and thread is not threading.current_thread():
            thread.join()

        error = None
        try:
            SharedVars._sync_once(self)
        except Exception as exc:
            error = exc
        with state["sync_lock"]:
            state["closed"] = True
            state["backend"].close()
        if error is not None:
            raise error

    def __enter__(self) -> "SharedVars":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        SharedVars.close(self)

    def __del__(self):
        try:
            SharedVars.close(self)
        except Exception:
            pass
