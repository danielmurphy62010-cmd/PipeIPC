# Shared variables

Each SharedVars object has a unique listener label; processes may join multiple labels. The creator is authoritative and serves snapshots. Joining clients must request the creator snapshot before installing it. Mutations on non-creators are rejected.

Recursive value tags: None=0, Dict=1, List=2, Tuple=3, Set=4, Int64=5, Float64=6, Bool=7, Bytes=8. Integer is signed 64-bit; float is IEEE-754 binary64. Dict keys are UTF-8 strings. Lists, tuples and sets use distinct wrapper types and distinct tags.

Routing matches exact labels and ancestors on dot boundaries: abc.def.ghi reaches abc.def.ghi, abc.def and abc, but not abc.def.jkl or abcd.
