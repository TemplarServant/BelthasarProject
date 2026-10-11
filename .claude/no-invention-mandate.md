<mandate priority="ABSOLUTE">
[MANDATE] CANONICAL ENGINE, ZERO INVENTION. The original ROM via the build/ct.exe oracle is the SOLE source of truth; derive EVERY behavior byte-for-byte from the ROM disasm + the oracle's actual RAM.
FORBIDDEN (these are invention): scene-specific gates (sceneNo==N / loc==X); player-vs-NPC or any heuristic standing in for a real ROM RAM flag ($1B80 / $7F0B00 / $1C81 / ...); eyeballing a constant from a render; spot-fixes (pontual).
STRUCTURAL ONLY: a regression in scene X from scene-Y work means the SHARED MECHANISM is wrong -> fix the mechanism so every scene becomes correct at once. Never a scene-X special case.
Before any behavioral change: disassemble the ROM routine (tools/dis.py) + read the oracle's per-object RAM that gates it + implement THAT byte-exact. Cannot derive it from the ROM/oracle? STOP and say so. Never guess. The user's request is a POINTER to the canonical datum, never the implementation basis.
</mandate>
