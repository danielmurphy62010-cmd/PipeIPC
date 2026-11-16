# Protocol and transport

Windows only. Server creates two fresh 8-byte magic values with BCryptGenRandom at startup. The client receives both during its control-pipe handshake. The data frame is MAGIC1(8), payload length(4 LE), MID(8 LE), BLAKE3(length||MID)(32), BLAKE3(payload)(32), listener ID(8), payload(0..16 MiB), MAGIC2(8).

ACK/NACK records are MAGIC1 + exact original header (84 bytes after MAGIC1) + status ('1' ACK, '0' NACK, '2' redundant receipt) + MAGIC2. The server acknowledges valid client frames and broadcasts the original frame to every peer's RX queue. Each peer has distinct TX, RX, TX_ACK, RX_ACK named-pipe endpoints; no competing readers share a FIFO.

The implementation must treat the frame parser as a byte-stream parser and cap lengths before allocation. On malformed data, it closes/resets the peer rather than guessing a frame boundary. This avoids issuing a NACK for an unidentifiable or attacker-controlled partial header.
