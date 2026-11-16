"""PipeIPC Python API with automatically monitored shared attributes."""
from __future__ import annotations

import copy
import threading
import weakref
from typing import Any

from _pipeipc import Client, Server, _SharedVarsBackend

__all__ = ["Client", "Server", "SharedVars"]

# Internal state is kept outside the instance dictionary. Therefore vars(state)
# contains only application variables, including names beginning with "_".
_STATE_LOCK = threading.RLock()
_STATES: weakref.WeakKeyDictionary = weakref.WeakKeyDictionary()


def _state_for(owner: "SharedVars") -> dict[str, Any]:
    with _STATE_LOCK:
        return _STATES[owner]


def _same(left: Any, right: Any) -> bool:
    """Compare supported values recursively, treating NaN as unchanged."""
    if type(left) is not type(right):
        return False
    if isinstance(left, float) and left != left and right != right:
        return True
    if isinstance(left, dict):
        return left.keys() == right.keys() and all(
            _same(left[key], right[key]) for key in left
        )
    if isinstance(left, (list, tuple)):
        return len(left) == len(right) and all(
            _same(a, b) for a, b in zip(left, right)
        )
    return left == right


class SharedVars:
    """Shared variables stored as normal instance attributes.

    Assign with `state.name = value`, mutate nested built-in containers
    normally, and remove a variable with `del state.name`.
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
            "baseline": copy.deepcopy(initial),
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
            previous = state["baseline"]
            current = SharedVars._copy_dict(attrs)
            dirty = {
                key for key in set(previous) | set(current)
                if key not in previous or key not in current
                or not _same(previous[key], current[key])
            }

            # Apply broker changes to untouched attributes. A simultaneous
            # local edit takes precedence and is sent back as the newer write.
            revision = state["backend"].revision
            if revision > state["revision"]:
                remote = state["backend"].snapshot()
                for key in set(previous) | set(current) | set(remote):
                    if key in dirty:
                        continue
                    if key in remote:
                        attrs[key] = copy.deepcopy(remote[key])
                    else:
                        attrs.pop(key, None)
                current = SharedVars._copy_dict(attrs)
                merged_base = SharedVars._copy_dict(current)
                for key in dirty:
                    if key in previous:
                        merged_base[key] = copy.deepcopy(previous[key])
                    else:
                        merged_base.pop(key, None)
                previous = merged_base
                state["baseline"] = merged_base
                state["revision"] = revision

            changed = {
                key for key in set(previous) | set(current)
                if key in current and (
                    key not in previous or not _same(previous[key], current[key])
                )
            }
            removed = set(previous) - set(current)
            failed: set[str] = set()

            for key in sorted(removed):
                try:
                    state["backend"]._erase(key)
                except Exception:
                    failed.add(key)
            for key in sorted(changed):
                try:
                    state["backend"]._set(key, current[key])
                except Exception:
                    failed.add(key)

            new_base = SharedVars._copy_dict(current)
            for key in failed:
                if key in previous:
                    new_base[key] = copy.deepcopy(previous[key])
                else:
                    new_base.pop(key, None)
            state["baseline"] = new_base
            # Do not advance state["revision"] here: a newer broker event may
            # have arrived while the outgoing changes above were being sent.
            state["sync_error"] = (
                RuntimeError("One or more shared-variable changes could not be synchronized")
                if failed else None
            )

    def get(self, key: str, default: Any = None) -> Any:
        return self.__dict__.get(key, default)

    def contains(self, key: str) -> bool:
        return key in self.__dict__

    def __contains__(self, key: str) -> bool:
        return key in self.__dict__

    def snapshot(self) -> dict[str, Any]:
        return SharedVars._copy_dict(self.__dict__)

    def is_ready(self) -> bool:
        return _state_for(self)["backend"].ready

    def get_revision(self) -> int:
        return _state_for(self)["backend"].revision

    def last_sync_error(self) -> Exception | None:
        return _state_for(self)["sync_error"]

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
