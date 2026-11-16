# Shared-variable containers

Each container has its own listener label; one process may join multiple containers. The creator is authoritative. A joining client requests the complete variable snapshot from the creator and installs it before join completes. Only the creator answers variable requests and commits authoritative updates.

Recursive values use distinct stable tags: None=0, Dict=1, List=2, Tuple=3, Set=4, Int64=5, Float64=6, Bool=7, Bytes=8. Integers are signed 64-bit two's-complement; floats are IEEE-754 binary64. Dict keys are UTF-8 strings. Lists, tuples, and sets retain distinct tags during round trips.

Sub-container routing is dot-segment aware. A message on `abc.def.ghi` reaches exact and ancestor listeners `abc.def.ghi`, `abc.def`, and `abc`; it does not reach `abc.def.jkl` or `abcd`.
