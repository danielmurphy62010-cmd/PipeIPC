# Wire protocol

All integer fields are little-endian. TX frame: MAGIC1 (8), payload length (4), MID (8), BLAKE3(length || MID) (32), BLAKE3(payload) (32), listener ID (8), payload (0..16 MiB), MAGIC2 (8).

ACK/NACK is MAGIC1 followed by the exact original 84-byte header (length through listener ID), one ASCII status byte, and MAGIC2. Status `1` means ACK, `0` means NACK, and `2` is the redundant notification.

The receiver must parse incrementally, reject oversized lengths before allocation, verify both magic values and both digests, and resynchronize on MAGIC1 after malformed input. NACK only when enough original header bytes exist to identify the MID/header.

Four logical channels are implemented as per-peer pipe endpoints/queues. The server fans each TX message out to every peer's RX queue; clients never compete for reads on one shared pipe. This per-peer fan-out is required for true broadcast semantics.
