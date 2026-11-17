#!/usr/bin/env python3
"""varrec - passive recorder for a PipeIPC broker: logs every shared-variable change and every message, then lets you scrub through it.
Passive means varrec is its own process and its own broker client. It never sends a message, never sets or deletes a
shared variable and never registers handlers, so it cannot change what your applications see. It only listens:
  * messages   - the broker broadcasts every message to every client, so a client that registers no on() handlers
                 receives all labels through recv(); each one is stored with its arrival time.
  * variables  - read through the read-only shared-variable replica (no sync thread, no write path). A change is
                 noticed by watching the replica's revision (every --poll seconds, default 10 ms) and stored as a delta.
The only frame varrec ever transmits is the standard "give me a snapshot" request that every client sends to learn
the current variables; the broker answers that to varrec alone.
    python varrec.py record my-app -o run.vrec        # record broker "my-app" until Ctrl+C, then open the viewer
    python varrec.py record my-app -p C:\\path\\to\\pipeipc   # folder containing pipeipc.py/_pipeipc.pyd, if not next to varrec.py
    python varrec.py record --demo                     # built-in fake broker, to try the tool without PipeIPC
    python varrec.py view run.vrec                     # interactive viewer      (--text: plain-text timeline)
From Python (own process or a thread of your app - it uses its own Client, so it never steals your recv() messages):
    from varrec import Recorder, PipeTap
    rec = Recorder(PipeTap("my-app"), "run.vrec").start()
    ...
    rec.stop()
Limits: two changes of the SAME variable within one poll interval collapse into the later value (messages are never
collapsed). Variables are only seen through the broker, so the broker must be running when recording starts.
File format (.vrec, version 2): magic "VREC"+version, then ONE zlib stream of timestamped events - variable deltas
(only what changed, keys interned), messages (labels interned) and notes - sync-flushed after every event. Only
activity costs bytes, and a file cut off by a crash is still readable up to the last event. Version 1 files still load.
"""
import argparse
import bisect
import math
import os
import shutil
import signal
import struct
import sys
import threading
import time
import zlib
if os.name == "nt":
    import msvcrt
else:
    import select
    import termios
    import tty
_GONE = object()  # history marker: the variable was deleted at that event
# --------------------------------------------------------------------------------------------------------------
# value codec
# --------------------------------------------------------------------------------------------------------------
class _Codec:
    """Compact tagged binary encoding for None/bool/int/float/str/bytes/list/tuple/set/dict (the PipeIPC value types)."""
    @staticmethod
    def varint(out, n):
        while n > 0x7F: out.append((n & 0x7F) | 0x80); n >>= 7
        out.append(n)
    @staticmethod
    def read_varint(buf, pos):
        shift = result = 0
        while True:
            b = buf[pos]; pos += 1; result |= (b & 0x7F) << shift
            if not b & 0x80: return result, pos
            shift += 7
    @classmethod
    def _blob(cls, out, tag, raw):
        out += tag; cls.varint(out, len(raw)); out += raw
    @classmethod
    def _read_blob(cls, buf, pos):
        n, pos = cls.read_varint(buf, pos); raw = buf[pos:pos + n]
        if len(raw) < n: raise IndexError("truncated")
        return bytes(raw), pos + n
    @classmethod
    def dumps(cls, v):
        out = bytearray(); cls.encode(v, out); return bytes(out)
    @classmethod
    def encode(cls, v, out):
        if v is None: out += b"N"
        elif isinstance(v, bool): out += b"T" if v else b"F"
        elif isinstance(v, int): out += b"i"; cls.varint(out, (v << 1) if v >= 0 else ((-v << 1) - 1))
        elif isinstance(v, float): out += b"d" + struct.pack("<d", v)
        elif isinstance(v, str): cls._blob(out, b"s", v.encode("utf-8", "surrogatepass"))
        elif isinstance(v, (bytes, bytearray)): cls._blob(out, b"b", bytes(v))
        elif isinstance(v, (list, tuple)):
            out += b"l" if isinstance(v, list) else b"t"; cls.varint(out, len(v))
            for x in v: cls.encode(x, out)
        elif isinstance(v, (set, frozenset)):
            parts = sorted(cls.dumps(x) for x in v); out += b"e"; cls.varint(out, len(parts)); out += b"".join(parts)
        elif isinstance(v, dict):
            items = sorted((((k if isinstance(k, str) else repr(k)), x) for k, x in v.items()), key=lambda kv: kv[0])
            out += b"m"; cls.varint(out, len(items))
            for k, x in items: kb = k.encode("utf-8", "surrogatepass"); cls.varint(out, len(kb)); out += kb; cls.encode(x, out)
        else: cls._blob(out, b"s", repr(v).encode("utf-8", "surrogatepass"))  # unknown type: keep a readable repr
    @classmethod
    def decode(cls, buf, pos):
        tag = bytes(buf[pos:pos + 1]); pos += 1
        if tag == b"N": return None, pos
        if tag == b"T": return True, pos
        if tag == b"F": return False, pos
        if tag == b"i": n, pos = cls.read_varint(buf, pos); return ((n >> 1) if not n & 1 else -((n + 1) >> 1)), pos
        if tag == b"d": return struct.unpack_from("<d", buf, pos)[0], pos + 8
        if tag == b"s": raw, pos = cls._read_blob(buf, pos); return raw.decode("utf-8", "surrogatepass"), pos
        if tag == b"b": return cls._read_blob(buf, pos)
        if tag in (b"l", b"t", b"e"):
            n, pos = cls.read_varint(buf, pos); items = []
            for _ in range(n): x, pos = cls.decode(buf, pos); items.append(x)
            if tag == b"l": return items, pos
            if tag == b"t": return tuple(items), pos
            return set(frozenset(x) if isinstance(x, set) else (tuple(x) if isinstance(x, list) else x) for x in items), pos
        if tag == b"m":
            n, pos = cls.read_varint(buf, pos); d = {}
            for _ in range(n): kb, pos = cls._read_blob(buf, pos); d[kb.decode("utf-8", "surrogatepass")], pos = cls.decode(buf, pos)
            return d, pos
        raise IndexError("bad tag")
# --------------------------------------------------------------------------------------------------------------
# file writer / reader
# --------------------------------------------------------------------------------------------------------------
class RecordWriter:
    """Appends timestamped events (variable deltas, messages, notes) to a .vrec file."""
    _MAGIC, _VERSION = b"VREC", 2
    _VARS, _MSG, _NOTE = 0, 1, 2
    def __init__(self, path, started_at=None):
        self._path = path
        self._f = open(path, "wb"); self._f.write(self._MAGIC + bytes([self._VERSION]))
        self._z = zlib.compressobj(6)
        self._last = {}; self._keys = {}; self._labels = {}; self._prev_ms = 0; self._frames = 0; self._messages = 0
        self._emit(struct.pack("<d", time.time() if started_at is None else started_at))
    path = property(lambda self: self._path)
    frames = property(lambda self: self._frames)      # events written so far (all kinds)
    messages = property(lambda self: self._messages)
    size = property(lambda self: os.path.getsize(self._path))
    def _emit(self, chunk):
        self._f.write(self._z.compress(chunk) + self._z.flush(zlib.Z_SYNC_FLUSH)); self._f.flush()
    @staticmethod
    def _intern(table, out, text):
        tid = table.get(text)
        if tid is None:
            tid = table[text] = len(table); raw = text.encode("utf-8", "surrogatepass")
            _Codec.varint(out, tid); _Codec.varint(out, len(raw)); out += raw
        else: _Codec.varint(out, tid)
    def _event(self, t_ms, kind, body):
        t_ms = max(t_ms, self._prev_ms); out = bytearray()
        _Codec.varint(out, t_ms - self._prev_ms); _Codec.varint(out, kind); out += body
        self._emit(bytes(out)); self._prev_ms = t_ms; self._frames += 1
    def variables(self, t_ms, revision, snapshot, force=False):
        """Store what changed since the previous variable event. Returns False (nothing written) if nothing changed."""
        encoded = {str(k): _Codec.dumps(v) for k, v in snapshot.items()}
        removed = sorted(k for k in self._last if k not in encoded)
        changed = sorted(k for k, b in encoded.items() if self._last.get(k) != b)
        if not (removed or changed or force): return False
        body = bytearray(); _Codec.varint(body, max(0, int(revision))); _Codec.varint(body, len(removed))
        for k in removed: self._intern(self._keys, body, k)
        _Codec.varint(body, len(changed))
        for k in changed: self._intern(self._keys, body, k); body += encoded[k]
        self._event(t_ms, self._VARS, body); self._last = encoded; return True
    def message(self, t_ms, label, value):
        raw = _Codec.dumps(value); body = bytearray(); self._intern(self._labels, body, str(label)); body += raw
        self._event(t_ms, self._MSG, body); self._messages += 1
    def note(self, t_ms, text):
        body = bytearray(); _Codec._blob(body, b"", str(text).encode("utf-8", "surrogatepass")); self._event(t_ms, self._NOTE, body)
    def close(self):
        if self._f is None: return
        self._f.write(self._z.flush()); self._f.close(); self._f = None
    def __enter__(self): return self
    def __exit__(self, *exc): self.close()
class RecordReader:
    """Loads a .vrec file (also a truncated one, also version 1) and rebuilds the full variable set after every event."""
    _KINDS = ("vars", "msg", "note")
    def __init__(self, path):
        self._path = path
        with open(path, "rb") as f: raw = f.read()
        if raw[:4] != RecordWriter._MAGIC or raw[4:5] not in (b"\x01", b"\x02"): raise ValueError(f"{path} is not a .vrec file")
        version = raw[4]; data = zlib.decompressobj().decompress(raw[5:])
        if len(data) < 8: raise ValueError(f"{path} holds no recording data (damaged or empty)")
        self._started_at = struct.unpack_from("<d", data, 0)[0]
        self._times = []; self._kinds = []; self._states = []; self._changed = []; self._revs = []; self._payload = []
        self._msg_idx = []; self._history = {}; self._n_vars = 0
        tables = ([], []); pos = 8; t_ms = 0; state = {}; rev_now = 0
        while pos < len(data):
            try: dt, kind, rev, removed, changed, payload, pos = self._parse_event(data, pos, tables, version)
            except (IndexError, struct.error): break  # cut-off last event
            idx = len(self._times); t_ms += dt
            if kind == 0:
                state = dict(state); self._n_vars += 1; rev_now = rev if version >= 2 else rev_now
                for k in removed: state.pop(k, None); self._history.setdefault(k, []).append((idx, _GONE))
                for k, v in changed.items(): state[k] = v; self._history.setdefault(k, []).append((idx, v))
            elif kind == 1: self._msg_idx.append(idx)
            self._times.append(t_ms / 1000.0); self._kinds.append(kind); self._states.append(state); self._revs.append(rev_now)
            self._changed.append(frozenset(removed) | frozenset(changed) if kind == 0 else frozenset()); self._payload.append(payload)
        self._keys = sorted(tables[0])
    @staticmethod
    def _read_name(data, pos, table):
        tid, pos = _Codec.read_varint(data, pos)
        if tid == len(table):
            raw, pos = _Codec._read_blob(data, pos); table.append(raw.decode("utf-8", "surrogatepass"))
        return table[tid], pos
    @classmethod
    def _parse_event(cls, data, pos, tables, version):
        dt, pos = _Codec.read_varint(data, pos); kind = 0; rev = 0; removed = []; changed = {}; payload = None
        if version >= 2: kind, pos = _Codec.read_varint(data, pos)
        if kind == 0:
            if version >= 2: rev, pos = _Codec.read_varint(data, pos)
            n, pos = _Codec.read_varint(data, pos)
            for _ in range(n): k, pos = cls._read_name(data, pos, tables[0]); removed.append(k)
            n, pos = _Codec.read_varint(data, pos)
            for _ in range(n): k, pos = cls._read_name(data, pos, tables[0]); changed[k], pos = _Codec.decode(data, pos)
        elif kind == 1:
            label, pos = cls._read_name(data, pos, tables[1]); value, pos = _Codec.decode(data, pos); payload = (label, value)
        elif kind == 2:
            raw, pos = _Codec._read_blob(data, pos); payload = raw.decode("utf-8", "surrogatepass")
        else: raise IndexError("bad event kind")
        return dt, kind, rev, removed, changed, payload, pos
    path = property(lambda self: self._path)
    started_at = property(lambda self: self._started_at)
    keys = property(lambda self: list(self._keys))
    duration = property(lambda self: self._times[-1] if self._times else 0.0)
    n_variable_updates = property(lambda self: self._n_vars)
    n_messages = property(lambda self: len(self._msg_idx))
    def __len__(self): return len(self._times)
    def time_of(self, i): return self._times[i]
    def kind_of(self, i): return self._KINDS[self._kinds[i]]
    def state(self, i): return self._states[i]
    def changed(self, i): return self._changed[i]
    def revision_of(self, i): return self._revs[i]
    def payload_of(self, i): return self._payload[i]   # message: (label, value) | note: text | variables: None
    def history(self, key, upto, limit):
        """Most recent changes of `key` at or before event `upto`, newest first: [(seconds, value | _GONE)]."""
        h = self._history.get(key, []); n = bisect.bisect_right([idx for idx, _ in h], upto)
        return [(self._times[idx], v) for idx, v in reversed(h[max(0, n - limit):n])]
    def messages_upto(self, upto, limit):
        """The last `limit` messages at or before event `upto`, oldest first: [(event_index, seconds, label, value)]."""
        n = bisect.bisect_right(self._msg_idx, upto)
        return [(i, self._times[i], *self._payload[i]) for i in self._msg_idx[max(0, n - limit):n]]
# --------------------------------------------------------------------------------------------------------------
# taps: read-only windows onto a broker
# --------------------------------------------------------------------------------------------------------------
class PipeTap:
    """Read-only view of a PipeIPC broker through a dedicated Client. It only ever listens:
    open() connects and fetches the initial variable snapshot, read_message() drains the client's own inbox,
    variables() reads the local replica. There is no call anywhere in here that sends, sets or erases."""
    def __init__(self, broker, paths=()):
        self._name = broker; self._client = None; self._vars = None
        for p in [os.getcwd()] + list(paths):
            if os.path.abspath(p) not in sys.path: sys.path.insert(0, os.path.abspath(p))
    def open(self):
        import _pipeipc  # built next to pipeipc.py; use --path if it isn't importable
        client = _pipeipc.Client(self._name); client.connect()
        try: self._vars = _pipeipc._SharedVarsBackend(client)  # replica + one snapshot request; no sync thread, no writes
        except BaseException: client.disconnect(); raise
        self._client = client
    alive = property(lambda self: bool(self._client and self._client.connected))
    def read_message(self, timeout): return self._client.recv(max(0, int(timeout * 1000)))  # (label, value) | None
    def variables(self): return self._vars.revision, self._vars.snapshot()
    def close(self):
        for closer in (lambda: self._vars.close(), lambda: self._client.disconnect()):
            try: closer()
            except Exception: pass
class _DemoTap:
    """Stand-in broker with changing variables and a message every 0.7 s, so varrec can be tried without PipeIPC."""
    def __init__(self): self._t0 = None; self._sent = 0
    def open(self): self._t0 = time.monotonic()
    alive = property(lambda self: True)
    def variables(self):
        n = int(time.monotonic() - self._t0)
        d = {"counter": n, "sine": round(math.sin(n), 3), "status": ["idle", "working", "done"][n // 3 % 3],
             "config": {"enabled": n % 2 == 0, "ports": list(range(8000, 8000 + n % 4))}, "tags": {"a", "b"} | ({"c"} if n % 6 >= 3 else set()),
             "coords": (n, n * n), "static": "never changes"}
        if n % 8 >= 4: d["temporary"] = n
        return n + 1, d
    def read_message(self, timeout):
        due = self._t0 + 0.7 * (self._sent + 1); wait = min(timeout, due - time.monotonic())
        if wait > 0: time.sleep(wait)
        if time.monotonic() < due: return None
        k = self._sent; self._sent += 1
        return ["app.event", "app.command", "app.log"][k % 3], {"seq": k, "text": "message number %d" % k}
    def close(self): pass
# --------------------------------------------------------------------------------------------------------------
# recorder
# --------------------------------------------------------------------------------------------------------------
class Recorder:
    """Listens on a tap until stopped (Ctrl+C / stop()), the duration ends, or the broker goes away, and writes everything
    it hears to a .vrec file. Variables are noticed by watching the tap's revision every `poll` seconds; messages are
    drained on a second thread so they are stamped when they arrive. run() blocks (Ctrl+C = clean stop); start() threads it."""
    def __init__(self, source, path, poll=0.01, duration=None, on_event=None):
        for name in ("open", "read_message", "variables", "close"):
            if not callable(getattr(source, name, None)): raise TypeError(f"source needs a {name}() method")
        if poll <= 0: raise ValueError("poll must be > 0")
        self._source = source; self._path = path; self._poll = poll; self._duration = duration; self._on_event = on_event
        self._stop = threading.Event(); self._lock = threading.Lock(); self._writer = None; self._t0 = 0.0
        self._last_rev = None; self._reason = "not started"; self._thread = None; self._error = None; self._pump_error = None
    path = property(lambda self: self._path)
    reason = property(lambda self: self._reason)
    frames = property(lambda self: self._writer.frames if self._writer else 0)
    messages = property(lambda self: self._writer.messages if self._writer else 0)
    size = property(lambda self: self._writer.size if self._writer else 0)
    def _ms(self): return int(round((time.monotonic() - self._t0) * 1000))
    def _notify(self):
        if self._on_event: self._on_event(self)
    def _vars_event(self, force=False):
        rev, snap = self._source.variables()
        if rev == self._last_rev and not force: return
        self._last_rev = rev
        with self._lock: wrote = self._writer.variables(self._ms(), rev, snap, force)
        if wrote: self._notify()
    def _message_event(self, message):
        with self._lock: self._writer.message(self._ms(), message[0], message[1])
        self._notify()
    def _note(self, text):
        with self._lock: self._writer.note(self._ms(), text)
        self._notify()
    def _pump(self):
        try:
            while not self._stop.is_set():
                m = self._source.read_message(0.2)
                if m is not None: self._message_event(m)
        except Exception as e: self._pump_error = e; self._stop.set()
    def _on_sigint(self, *_):
        self._stop.set(); signal.signal(signal.SIGINT, signal.default_int_handler)  # a 2nd Ctrl+C kills hard
    def run(self):
        self._source.open()
        in_main = threading.current_thread() is threading.main_thread()
        old = signal.signal(signal.SIGINT, self._on_sigint) if in_main else None
        self._t0 = time.monotonic(); self._writer = RecordWriter(self._path); self._reason = "stopped by user"
        pump = threading.Thread(target=self._pump, name="varrec-messages", daemon=True)
        try:
            self._vars_event(force=True); self._note("recording started"); pump.start()
            while not self._stop.is_set():
                if self._duration is not None and time.monotonic() - self._t0 >= self._duration: self._reason = "duration reached"; break
                if not self._source.alive: self._reason = "broker disconnected"; break
                try: self._vars_event()
                except Exception:
                    if self._source.alive: raise
                    self._reason = "broker disconnected"; break
                self._stop.wait(self._poll)
        finally:
            self._stop.set()
            if pump.is_alive(): pump.join()
            if self._pump_error is not None: self._reason = "broker disconnected" if not self._source.alive else f"error: {self._pump_error}"
            try:
                while True:  # messages that were already queued when we stopped
                    m = self._source.read_message(0)
                    if m is None: break
                    self._message_event(m)
                self._vars_event()
            except Exception: pass
            self._note(f"recording ended: {self._reason}"); self._writer.close(); self._source.close()
            if in_main: signal.signal(signal.SIGINT, old if old is not None else signal.SIG_DFL)
        return self._path
    def start(self):
        def target():
            try: self.run()
            except Exception as e: self._error = e
        self._thread = threading.Thread(target=target, name="varrec", daemon=True); self._thread.start(); return self
    def stop(self):
        self._stop.set()
        if self._thread: self._thread.join()
        if self._error: raise self._error
        return self._path
# --------------------------------------------------------------------------------------------------------------
# viewer
# --------------------------------------------------------------------------------------------------------------
def _show(v):
    """One-line, deterministic rendering of a value (sets sorted so they don't shuffle between events)."""
    if isinstance(v, set): return "{" + ", ".join(sorted(_show(x) for x in v)) + "}"
    if isinstance(v, list): return "[" + ", ".join(_show(x) for x in v) + "]"
    if isinstance(v, tuple): return "(" + ", ".join(_show(x) for x in v) + ("," if len(v) == 1 else "") + ")"
    if isinstance(v, dict): return "{" + ", ".join(f"{k!r}: {_show(x)}" for k, x in v.items()) + "}"
    if isinstance(v, bytes) and len(v) > 24: return f"<{len(v)} bytes {v[:8].hex()}...>"
    return repr(v)
def _clip(s, n): return s if len(s) <= n else s[:max(0, n - 1)] + "…"
class _Keys:
    """Raw single-key input with a timeout (msvcrt on Windows, termios elsewhere). Returns normalised key names."""
    _WIN = {"K": "left", "M": "right", "H": "up", "P": "down", "G": "home", "O": "end"}
    _ANSI = {"A": "up", "B": "down", "C": "right", "D": "left", "H": "home", "F": "end"}
    def __enter__(self):
        if os.name != "nt": self._fd = sys.stdin.fileno(); self._old = termios.tcgetattr(self._fd); tty.setcbreak(self._fd)
        return self
    def __exit__(self, *exc):
        if os.name != "nt": termios.tcsetattr(self._fd, termios.TCSADRAIN, self._old)
    @staticmethod
    def _plain(ch): return {" ": "space", "\x03": "q", "\x1b": "esc", "\t": "tab"}.get(ch, ch)
    def read(self, timeout):
        if os.name == "nt":
            end = time.monotonic() + timeout
            while not msvcrt.kbhit():
                if time.monotonic() >= end: return None
                time.sleep(0.01)
            ch = msvcrt.getwch()
            return self._WIN.get(msvcrt.getwch(), "") if ch in ("\x00", "\xe0") else self._plain(ch)
        if not select.select([self._fd], [], [], timeout)[0]: return None
        ch = os.read(self._fd, 1).decode("utf-8", "ignore")
        if ch != "\x1b": return self._plain(ch)
        seq = ""
        while len(seq) < 6 and select.select([self._fd], [], [], 0.03)[0]:
            seq += os.read(self._fd, 1).decode("utf-8", "ignore")
            if seq[-1:].isalpha() or seq[-1:] == "~": break
        return self._ANSI.get(seq[-1:], "") if seq else "esc"
class Viewer:
    """Replays a recording: interactive scrubber (steps through every event) or a plain-text timeline."""
    _SPEEDS = [0.25, 0.5, 1, 2, 4, 8, 16, 32]
    _PANEL = 7  # lines in the bottom panel
    _B, _D, _Y, _I, _R = "\x1b[1m", "\x1b[2m", "\x1b[33m", "\x1b[7m", "\x1b[0m"
    def __init__(self, reader): self._r = reader
    # ---- plain text ----
    def print_timeline(self, speed=0.0, out=None):
        out = out or sys.stdout; r = self._r
        out.write(f"{os.path.basename(r.path)}: recorded {time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(r.started_at))}, {len(r)} events "
                  f"({r.n_variable_updates} variable updates, {r.n_messages} messages), {r.duration:.1f}s, {len(r.keys)} variables\n")
        prev_t = 0.0
        for i in range(len(r)):
            if speed > 0 and i: time.sleep(max(0.0, (r.time_of(i) - prev_t) / speed))
            prev_t = r.time_of(i); st = r.state(i); old = r.state(i - 1) if i else {}; kind = r.kind_of(i); stamp = f"[{r.time_of(i):8.2f}s]"
            if kind == "msg": label, value = r.payload_of(i); out.write(f"{stamp} > {label}: {_show(value)}\n")
            elif kind == "note": out.write(f"{stamp} # {r.payload_of(i)}\n")
            else:
                for k in sorted(r.changed(i)):
                    if k not in st: line = f"- {k} (deleted)"
                    elif k not in old: line = f"+ {k} = {_show(st[k])}" if i else f"  {k} = {_show(st[k])}"
                    else: line = f"  {k}: {_show(old[k])} -> {_show(st[k])}"
                    out.write(f"{stamp} {line}\n")
            out.flush()
    # ---- interactive ----
    def _draw(self, i, playing, speed, sel, panel):
        r = self._r; keys = r.keys; n = len(r); P = self._PANEL; w, h = shutil.get_terminal_size((100, 30)); w = max(40, w)
        st = r.state(i); old = r.state(i - 1) if i else {}; chg = r.changed(i) if i else frozenset()
        kind = r.kind_of(i); payload = r.payload_of(i); rev = r.revision_of(i)
        what = "variables changed" if kind == "vars" else (f"message {payload[0]}" if kind == "msg" else f"note: {payload}")
        L = [self._B + _clip(f"{os.path.basename(r.path)}  event {i + 1}/{n}  t={r.time_of(i):.2f}s / {r.duration:.2f}s  "
                             f"{('rev %d  ' % rev) if rev else ''}{speed:g}x  {'PLAYING' if playing else 'paused'}", w) + self._R,
             self._Y + _clip("event: " + what, w) + self._R, self._D + "─" * w + self._R]
        rows = max(3, h - 14); top = min(max(0, sel - rows // 2), max(0, len(keys) - rows)); nw = min(max([len(k) for k in keys] or [1]), 28); drawn = 0
        for m in range(top, min(len(keys), top + rows)):
            k = keys[m]; here = k in st; txt = _show(st[k]) if here else "—"; body = _clip(txt, w - nw - 6)
            if k in chg and k in old and here: body = _clip(f"{txt}   ← was {_show(old[k])}", w - nw - 6)
            line = f"{'>' if m == sel else ' '} {_clip(k, nw).ljust(nw)}  {body}"; drawn += 1
            L.append(self._I + line.ljust(w) + self._R if m == sel else (self._Y + line + self._R if k in chg else (line if here else self._D + line + self._R)))
        if not keys: L.append(self._D + "(no shared variables)" + self._R); drawn = 1
        L += [""] * (rows - drawn); L.append(self._D + "─" * w + self._R)
        if panel == "messages":
            L.append(f"{self._B}messages{self._R} ({r.n_messages} recorded)")
            msgs = r.messages_upto(i, P)
            for idx, t, label, value in msgs:
                line = _clip(f"{'>' if idx == i else ' '} {t:8.2f}s  {label}  {_show(value)}", w); L.append(self._I + line.ljust(w) + self._R if idx == i else line)
            L += [""] * (P - len(msgs))
        else:
            L.append(f"{self._B}{_clip(keys[sel], w - 20) if keys else '-'}{self._R} — recent changes")
            hist = r.history(keys[sel], i, P) if keys else []
            for t, v in hist: L.append(_clip(f"  t={t:8.2f}s  {'(deleted)' if v is _GONE else _show(v)}", w))
            L += [""] * (P - len(hist))
        L.append(self._D + _clip("←/→ step  [ ] ±10  g/G start/end  ↑/↓ variable  tab messages/history  space play  +/- speed  q quit", w) + self._R)
        sys.stdout.write("\x1b[H" + "".join(l + "\x1b[K\n" for l in L) + "\x1b[J"); sys.stdout.flush()
    def run(self):
        r = self._r; n = len(r)
        if n == 0: print("recording is empty"); return
        if os.name == "nt": os.system("")  # enables ANSI escapes in the Windows console
        i = 0; playing = False; speed = 1; sel = 0; panel = "messages"; dirty = True; next_at = 0.0
        gap = lambda j: max(0.03, min(r.time_of(min(j + 1, n - 1)) - r.time_of(j), 2.0) / speed)  # long idle stretches are skipped after 2 s
        sys.stdout.write("\x1b[?25l\x1b[2J")
        try:
            with _Keys() as keys:
                while True:
                    if dirty: self._draw(i, playing, speed, sel, panel); dirty = False
                    key = keys.read(max(0.0, next_at - time.monotonic()) if playing else 0.5)
                    if key is None:
                        if playing and time.monotonic() >= next_at:
                            if i < n - 1: i += 1; dirty = True; next_at = time.monotonic() + gap(i)
                            else: playing = False; dirty = True
                        continue
                    dirty = True
                    if key in ("q", "esc"): break
                    elif key in ("left", "a"): i = max(0, i - 1)
                    elif key in ("right", "d"): i = min(n - 1, i + 1)
                    elif key == "[": i = max(0, i - 10)
                    elif key == "]": i = min(n - 1, i + 10)
                    elif key in ("g", "home"): i = 0
                    elif key in ("G", "end"): i = n - 1
                    elif key in ("up", "w"): sel = max(0, sel - 1)
                    elif key in ("down", "s"): sel = max(0, min(len(r.keys) - 1, sel + 1))
                    elif key == "tab": panel = "history" if panel == "messages" else "messages"
                    elif key in ("+", "="): speed = self._SPEEDS[min(len(self._SPEEDS) - 1, self._SPEEDS.index(speed) + 1)]
                    elif key == "-": speed = self._SPEEDS[max(0, self._SPEEDS.index(speed) - 1)]
                    elif key == "space":
                        playing = not playing
                        if playing and i >= n - 1: i = 0
                    else: dirty = False
                    if playing: next_at = time.monotonic() + gap(i)
        except KeyboardInterrupt: pass
        finally: sys.stdout.write("\x1b[?25h\x1b[2J\x1b[H"); sys.stdout.flush()
# --------------------------------------------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------------------------------------------
def _human(n): return f"{n} B" if n < 1024 else f"{n / 1024:.1f} KB" if n < 1024 ** 2 else f"{n / 1024 ** 2:.2f} MB"
def _summary(reader, out):
    return (f"saved {out}: {len(reader)} events ({reader.n_variable_updates} variable updates, {reader.n_messages} messages), "
            f"{reader.duration:.1f}s, {len(reader.keys)} variables, {_human(os.path.getsize(out))}")
def _cmd_record(a):
    if bool(a.broker) == a.demo: raise SystemExit("give either a BROKER name or --demo")
    if a.poll <= 0 or (a.duration is not None and a.duration <= 0): raise SystemExit("poll and duration must be > 0")
    source = _DemoTap() if a.demo else PipeTap(a.broker, a.path)
    out = a.output or os.path.join(os.getcwd(), time.strftime("vars_%Y%m%d_%H%M%S.vrec")); tty_out = sys.stdout.isatty(); last = [0.0]
    def tick(rec):
        if tty_out and time.monotonic() - last[0] > 0.1:
            last[0] = time.monotonic()
            sys.stdout.write(f"\rrecording to {out}   events={rec.frames}  messages={rec.messages}  size={_human(rec.size)}   (Ctrl+C to stop)\x1b[K"); sys.stdout.flush()
    rec = Recorder(source, out, a.poll, a.duration, tick)
    print(("listening to the demo broker" if a.demo else f"listening to broker '{a.broker}'") + " (passive: nothing is sent or changed) — press Ctrl+C to stop")
    try: rec.run()
    except ImportError as e: raise SystemExit(f"cannot import PipeIPC's Python module ({e}); build it, or pass --path DIR where _pipeipc is")
    except (OSError, RuntimeError) as e: raise SystemExit(f"could not connect to broker '{a.broker}': {e}")
    print(); reader = RecordReader(out); print(f"{_summary(reader, out)} — {rec.reason}")
    if a.no_view: return
    if sys.stdin.isatty() and sys.stdout.isatty(): Viewer(reader).run()
    else: Viewer(reader).print_timeline()
def _cmd_view(a):
    try: reader = RecordReader(a.file)
    except (OSError, ValueError) as e: raise SystemExit(f"cannot open recording: {e}")
    v = Viewer(reader)
    if a.text or not (sys.stdin.isatty() and sys.stdout.isatty()): v.print_timeline(a.speed or 0.0)
    else: v.run()
def main(argv=None):
    if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(errors="replace")
    ap = argparse.ArgumentParser(prog="varrec", description="Passive recorder for a PipeIPC broker: shared variables + messages over time.")
    sub = ap.add_subparsers(dest="cmd", required=True)
    rec = sub.add_parser("record", help="listen to a broker until Ctrl+C, then open the viewer")
    rec.add_argument("broker", nargs="?", help="broker name, i.e. the name your Server/Client use")
    rec.add_argument("--demo", action="store_true", help="record a built-in fake broker instead of a real one")
    rec.add_argument("-o", "--output", help="output file (default: vars_<timestamp>.vrec in the current directory)")
    rec.add_argument("--poll", type=float, default=0.01, help="seconds between checks for variable changes (default 0.01)")
    rec.add_argument("-d", "--duration", type=float, help="stop by itself after this many seconds")
    rec.add_argument("-p", "--path", action="append", default=[], help="directory containing pipeipc.py / _pipeipc.pyd (repeatable)")
    rec.add_argument("--no-view", action="store_true", help="don't open the viewer after stopping")
    rec.set_defaults(func=_cmd_record)
    view = sub.add_parser("view", help="watch a recording")
    view.add_argument("file")
    view.add_argument("--text", action="store_true", help="print every event as text instead of the interactive viewer")
    view.add_argument("--speed", type=float, help="with --text: replay in real time at this speed (default: instant)")
    view.set_defaults(func=_cmd_view)
    a = ap.parse_args(argv); a.func(a)
if __name__ == "__main__":
    main()