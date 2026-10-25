---
name: capture-oracle-state
description: Turn a snes9x savestate (from the user's emulator, e.g. on the Desktop "aux crono decompile" folder) into a runnable LakeSnes oracle state, or capture new mid-scenario oracle states/dumps. Use when the user provides a .state repro, or when RE work needs a state at a specific moment.
---

# capture-oracle-state — s9x → lake, and scenario captures

## A. Convert a snes9x state the user captured

snes9x states are gzip files starting `#!s9xsnp`. The oracle (LakeSnes inside
ct.exe) cannot load them directly — you SPLICE the s9x WRAM+CPU into a lake
"base" state of the SAME scene (the base supplies PPU/APU/cart).

1. Find the scene the capture is in (no oracle needed):
   ```
   python tools/ct.py objs "C:/path/to/user_capture.state"
   ```
   Read `loc` from the printout header (also note storyline $7F0000 and player pos).
2. Make a base lake state of that scene through the ROM's own loader:
   ```
   cp build/ct.exe "$TMP/ct_probe.exe"
   ct_probe --teleport <loc> <tx> <ty> <facing> --from two_member.state --out "$TMP/base_locN.state"
   ```
   (`two_member.state`, made by the recipe `telepod_show` in
   `tools/fixtures/field.json` -- loc 1B7, storyline 0x08, two members -- is a
   good donor for 1000AD-era captures; any recipe-made state from
   `python tools/fixtures.py make all` will do. Pick a donor with a compatible
   storyline if the teleport lands wrong. tx/ty ≈ the capture's player tile.)
3. Splice:
   ```
   python tools/s9x_to_lake.py "user_capture.state" "$TMP/base_locN.state" my_repro.state
   ```
   It prints the REG decode + `spliced WRAM(loc=XXX)`. loc must match step 1.
4. Verify it runs:
   ```
   ct_probe --read loc,story --from my_repro.state
   ct_probe --peek my_repro.state "$TMP/sanity.raw" --btnscript "0:60"
   python tools/ct.py fb2png "$TMP/sanity.raw" "$TMP/sanity.png"   # then LOOK at it
   ```
   A garbled screen = wrong-scene base (PPU mismatch). Redo step 2 with the right loc.

## B. Capture new states/dumps mid-scenario

- Drive + dump: `ct_probe --peek my_repro.state out.raw --btnscript "100:8,0:592"`
  writes the framebuffer + `.wram` (128KB) + `.vram` + `.oam` sidecars at the end
  frame. Chain `--btnscript` segments to reach any moment.
- Save a RUNNABLE state at the end frame: set env `PEEK_SAVE=path.state` on that
  same command.
- Frame-pin a timeline: repeat with increasing frame counts (0:200, 0:300, ...)
  and fb2png each; name files by frame (gf301.png).

## C. Storage conventions

- Precious repro states → `saves/` (gitignored on purpose; they survive on disk).
  Sidecar dump sets for byte-compares → `saves/<topic>_re/`.
- Record the EXACT repro command line (state + btnscript + env) in the dossier in
  `disasm/notes/` — a state without its command line is half useless.
- Root-level scratch states are fine short-term (also gitignored).
- A state is fine for derivation, but a state a regression check READS must be
  made by a recipe (`tools/fixtures/<domain>.json` -> `tools/recipes/`) or come
  from a public source (`tools/third_party_saves.json`):
  `python tools/input_provenance.py` (the check
  `inputs-come-from-recipes-or-sources`) fails on a check that reads a local
  capture. Write the recipe that reaches the same moment from power-on before
  pinning a check on it.

## Gotchas

- Belthasar F5/F9 `.state` files are a DIFFERENT format (CTFS, our engine's debug
  snapshot) — not oracle states. Don't mix them up.
- The splice resumes the CPU mid-frame wherever s9x saved; the first 1-2 frames can
  be transitional. `--settle N` on --read/--monitor runs N frames first.
- `--scene N` direct-load in Belthasar skips the scene-entry settle (NPC positions
  may differ from a real entry); the oracle --teleport goes through the real loader.
