"""PipeIPC Python API.

SharedVars stores variables in its real instance dictionary and periodically
synchronizes changes to the broker. Nested built-in containers are monitored
by snapshot comparison, so in-place mutations are propagated as well.
"""
from __future__ import annotations

import copy
import threading
import weakref
from typing import Any

from _pipeipc import Client, Server, _SharedVarsBackend


__all__ = ["Client", "Server", "SharedVars"]


def _same(left: Any, right: Any) -> bool:
    """Compare supported shared values, treating NaN as unchanged."""
    if type(left) is not type(right):
        return False
    if isinstance(left, float) and left != left and right != right:
        return True
    if isinstance(left, dict):
        return (left.keys() == right.keys()
                and all(_same(left[k], right[k]) for k in left))
    if isinstance(left, (list, tuple)):
        return len(left) == len(right) and all(
            _same(a, b) for a, b in zip(left, right)
        )
    if isinstance(left, set):
        return left == right
    return left == right


class SharedVars:
    """Process-shared variables exposed as ordinary Python instance attributes.

    Assign with `state.name = value`; remove with `del state.name`.
    Mutations inside dictionaries, lists, and sets are detected automatically.
    """

    __slots__ = (
        "_backend", "_stop", "_thread", "_sync_lock", "_baseline",
        "_revision", "_sync_error", "_poll_interval", "_closed",
        "__dict__", "__weakref__",
    )

    def __init__(self, client: Client, poll_interval: float = 0.01):
        if poll_interval <= 0:
            raise ValueError("poll_interval must be positive")
        backend = _SharedVarsBackend(client)
        object.__setattr__(self, "_backend", backend)
        object.__setattr__(self, "_stop", threading.Event())
        object.__setattr__(self, "_sync_lock", threading.RLock())
        object.__setattr__(self, "_poll_interval", float(poll_interval))
        object.__setattr__(self, "_sync_error", None)
        object.__setattr__(self, "_closed", False)

        initial = copy.deepcopy(backend.snapshot())
        self.__dict__.update(initial)
        object.__setattr__(self, "_baseline", copy.deepcopy(initial))
        object.__setattr__(self, "_revision", backend.revision)

        thread = threading.Thread(
            target=SharedVars._watch_ref,
            args=(weakref.ref(self), self._stop, self._poll_interval),
            name="PipeIPC-SharedVars",
            daemon=True,
        )
        object.__setattr__(self, "_thread", thread)
        thread.start()

    @staticmethod
    def _watch_ref(owner_ref, stop: threading.Event, interval: float) -> None:
        while not stop.wait(interval):
            owner = owner_ref()
            if owner is None:
                return
            try:
                owner._sync_once()
            except Exception as exc:
                object.__setattr__(owner, "_sync_error", exc)
            finally:
                del owner

    @staticmethod
    def _copy_dict(values: dict[str, Any]) -> dict[str, Any]:
        return copy.deepcopy(dict(values))

    def _sync_once(self) -> None:
        with self._sync_lock:
            if self._closed:
                return
            attrs = self.__dict__
            previous = self._baseline
            current = self._copy_dict(attrs)

            dirty = {
                key for key in set(previous) | set(current)
                if key not in previous or key not in current
                or not _same(previous[key], current[key])
            }

            # Merge broker changes into attributes, but never overwrite a
            # local edit that has not yet been sent.
            revision = self._backend.revision
            if revision > self._revision:
                remote = self._backend.snapshot()
                for key in set(previous) | set(current) | set(remote):
                    if key in dirty:
                        continue
                    if key in remote:
                        attrs[key] = copy.deepcopy(remote[key])
                    else:
                        attrs.pop(key, None)
                current = self._copy_dict(attrs)
                merged_base = self._copy_dict(current)
                for key in dirty:
                    if key in previous:
                        merged_base[key] = copy.deepcopy(previous[key])
                    else:
                        merged_base.pop(key, None)
                previous = merged_base
                object.__setattr__(self, "_baseline", merged_base)
                object.__setattr__(self, "_revision", revision)

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
                    self._backend._erase(key)
                except Exception:
                    failed.add(key)
            for key in sorted(changed):
                try:
                    self._backend._set(key, current[key])
                except Exception:
                    failed.add(key)

            new_base = self._copy_dict(current)
            for key in failed:
                if key in previous:
                    new_base[key] = copy.deepcopy(previous[key])
                else:
                    new_base.pop(key, None)
            object.__setattr__(self, "_baseline", new_base)
            # Only advance to the revision whose snapshot was merged above.
            # A newer event may arrive while outgoing writes are being sent.
            object.__setattr__(
                self, "_sync_error",
                RuntimeError("One or more shared-variable changes could not be synchronized")
                if failed else None,
            )

    def get(self, key: str, default: Any = None) -> Any:
        return self.__dict__.get(key, default)

    def contains(self, key: str) -> bool:
        return key in self.__dict__

    def snapshot(self) -> dict[str, Any]:
        return self._copy_dict(self.__dict__)

    @property
    def ready(self) -> bool:
        return self._backend.ready

    @property
    def revision(self) -> int:
        return self._backend.revision

    @property
    def sync_error(self) -> Exception | None:
        return self._sync_error

    def close(self) -> None:
        with self._sync_lock:
            if self._closed:
                return
            self._stop.set()
        thread = self._thread
        if thread.is_alive() and thread is not threading.current_thread():
            thread.join()
        error = None
        try:
            self._sync_once()
        except Exception as exc:
            error = exc
        with self._sync_lock:
            object.__setattr__(self, "_closed", True)
            self._backend.close()
        if error is not None:
            raise error

    def __enter__(self) -> "SharedVars":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def __del__(self):
        try:
            SharedVars.close(self)
        except Exception:
            pass
