---
name: re-derive
description: Derive the canonical truth for ANY Chrono Trigger behavior from the ROM + oracle before touching engine code. Use whenever the user reports a behavior ("X should happen", "Y looks wrong"), when a mechanism is unknown, or before implementing anything not already covered by a dossier in disasm/notes/.
---

# re-derive — find the canonical mechanism (ROM + oracle only)

**Rule 0 (the mandate, CLAUDE.md):** the user's words are a POINTER, never the spec.
"The NPC should stop here" means: go find the ROM routine and the oracle RAM that
produce that behavior, and report THAT. If you cannot derive it, STOP and say so.
Never guess, never approximate, never invent.

## Setup (do this once per session)

1. Work from the repo root (the oracle needs `Chrono_Trigger_USA_SNES.sfc` + `ct_assets.dat` in cwd).
2. Copy the oracle so builds never collide with your probes:
   ```
   cp build/ct.exe "$TMP/ct_probe.exe"     # NEVER probe with build/ct.exe directly
   ```
3. Read `docs/DEBUG_SURFACE.md` (all oracle modes + env vars) and skim
   `disasm/notes/` (and the agent's memory index `MEMORY.md`, outside the
   repository, when you have one) for prior work on your topic. Do not redo pinned RE.

## The probe cookbook (exact commands)

| Goal | Command |
|---|---|
| Read RAM/ROM bytes | `ct_probe --read "1801+4,flags,story,loc" --from S.state [--settle N] [--json]` |
| Watch RAM per frame | `ct_probe --monitor "0117:2,2989:1" --from S.state --input "100:8,0:300" --diff` |
| WHO writes a byte | add `--writer` to --monitor (reported PC = the instruction AFTER the store) |
| Scene/loc flow | `ct_probe --followscene --from S.state --input "..." --frames N` |
| Event-VM stepping | `ct_probe --followvm --obj K --from S.state --input "..."` |
| Frame screenshot + dumps | `ct_probe --peek S.state out.raw --btnscript "100:8,0:592"` → writes fb + .wram/.vram/.oam |
| View a screenshot | `python tools/ct.py fb2png out.raw out.png` (then Read the png) |
| Save a mid-scenario state | env `PEEK_SAVE=path.state` during --peek |
| Disassemble ROM | `python tools/dis.py <bankaddr-hex> <len-hex> [m] [x]` e.g. `dis.py C05A93 40` |
| Decode an event script | `python tools/ct.py script <sceneNo>` (any scene; fn table + annotated ops) |
| Find callers/writers of an addr | `python tools/ct.py refs <addr> [--rom PATH]` (heuristic byte scan) |
| Object array from a state | `python tools/ct.py objs <state-or-wram>` (tile/foot/solid/draw/mode/IP/anim...) |
| First divergence vs oracle | `ct_probe --vmdiff S.state` (prints seed fidelity + first-divergence frame) |

Button grammar (LakeSnes currentState): B=0x01 Y=0x02 Sel=0x04 Start=0x08 Up=0x10
Down=0x20 Left=0x40 Right=0x80 A=0x100 X=0x200. Input script = `"hexbtn:frames,..."`.

## Standard derivation procedure

1. **Reproduce in the oracle.** Load the closest state (repo root / saves/; see the
   memory index for which state exercises what). Watch the behavior happen with
   --monitor/--followvm/--peek. Record the FRAME TIMELINE (f-numbers of each event).
2. **Find the RAM.** Wide --monitor windows with --diff to find what flips; narrow
   down; then --writer to get the writer PC.
3. **Disassemble the writer** with dis.py. Follow the routine until you can state,
   in bytes, what it reads and writes. The event-op jump table is C0/5D6E
   (handler = table[op*2]); the event/field code runs with D=$0100, so DP $17 in a
   disasm = WRAM $0117.
4. **Confirm in both directions:** the disasm predicts a RAM effect → see it in the
   oracle; the oracle shows a value → find its ROM source. A claim needs BOTH or it
   is labeled HYPOTHESIS.
5. **Write the dossier** to `disasm/notes/<topic>.md`: mechanism, RAM map, routine
   addresses, frame timeline, repro command lines, and an explicit **Open questions**
   section for everything NOT pinned. Every sentence cites an address or an observed
   value. Commit the dossier (notes are tracked).

## Gotchas that have burned us (do not repeat)

- **A "doesn't exist" verdict needs a proper state.** A broken/wrong state gives a
  false negative. Confirm the state exercises the feature; report "couldn't
  reproduce under X", never "absent". (memory: verify-negatives-on-proper-state)
- **Dossier tables can be wrong.** Before "fixing" code to match a dossier table,
  re-verify the table with fresh dis.py at the cited address (an op-0x16 cmp table
  in a dossier had entries 3/4 swapped; the code was right).
- **--vmdiff button injection forwards only A** to the oracle; other buttons are
  asymmetric. Use --peek/--monitor for multi-button scenarios.
- **Trust the render, not simulations.** If reconstructing graphics, compare real
  framebuffers (fb2png), not simulated layouts. (memory: ct-overworld-bg3-vram-addr)
- s9x-vs-lake state formats differ; `python tools/ct.py objs` auto-detects. To make
  a runnable oracle state from a snes9x save, use the `capture-oracle-state` skill.
- The scheduler yields on the RASTER, not an op count; cadence defaults differ by
  loader (memory: ct-field-scheduler-re) — do not infer timing from op counts.
