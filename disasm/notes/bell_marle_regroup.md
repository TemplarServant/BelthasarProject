# The bell minigame WITH MARLE: op 0xDA resets the party velocity ring (defect #6)

*"the bell with Marle"* — 2026-09-02 list, item 6. Everything below is measured on the ROM + the oracle.

## 1. What the defect actually is

The bell strength game itself (scene 5, obj26 fn1, Crono's fn3 bob, op 0x24 / 0x34 / the 0x96 arrival
clamp — `ct-field-bell-minigame`) was already functional. What had never been run is the game **with a
follower in the party**, because no state in the tree stood in scene 5 with Marle. Once one existed the
oracle showed the whole sequence, and the port matched it station for station **except the last one**:

> after the minigame, when Crono walks away, Marle walks off north on her own instead of trailing him.

The cause is in op 0xDA (`C0/345A`, the event's "hand back to the player" + party regroup): at its
completion it **resets the party velocity ring** — the ring `party_follower_ring.md` ported — and the
port's op 0xDA skipped exactly those stores as "five OAM stores, a render detail" (written when the
conga was still a position trail, where they were inert).

```
C0/3477  LDA #01 / TRB $54              drop the re-entry latch
C0/347B  TDC / XBA                       high byte 0 -> X is 8-bit below
C0/347D  LDA $AB / STA $AC / STA $AD    both ring CURSORS := the head   -> every gap is 0
C0/3483  TAX / LDA #00
C0/3486  STA $7F0C00,X  (9F 00 0C 7F)    velX row at the head
C0/348A  STA $7F0C00,X  (9F 00 0C 7F)    again -- the raw bytes say 0C00 twice, NOT 0C80
C0/348E  STA $7F0D00,X  (9F 00 0D 7F)    preX row
C0/3492  STA $7F0D00,X  (9F 00 0D 7F)    again
C0/3496  STA $7F0F00,X  (9F 00 0F 7F)    shape row
C0/349A  LDX $C7 / INX / SEC             IP += 1, end the visit
```

Without the cursor equalisation a follower that has just been walked ONTO the lead still has a cursor
24+ entries behind the head from before the cutscene; the moment the lead walks, the gap is already
past 0x18 and the follower **consumes the stale entries** — here the pre-minigame walk-in (`Up 24`), so
Marle walked north.

## 2. The fixture, and why it took an oracle-driven pathfinder

With Marle in the party the fair loads as scene **439**, which has no bell (none of `24 00 1E`, `34 03`,
`07 00 33`, `E8 5A` occur in its script blob). The bell is scene 5's obj26, reached through 439's real
exit (box (352,352) 80×16 → arrival (392,367) facing down). Neither teleport method makes a usable state:
`--teleport … jmp` leaves the lead PC object inactive (`$0F02 = 80`, prio 7 — the d-pad does nothing),
and `… req` runs the real loader (Marle placed) but keeps the OLD coordinates, which in scene 5 is a
dead-end pocket beside the Gato-tent door (every step leaves the scene).

`tools/_walkto.py` finds the route by probing the oracle: 16-frame steps, `--peek` + `PEEK_SAVE`
chaining, depth-first with a visited set. From `saves/base_loc6.state` (Gato's tent, Crono+Marle):
Down 55 → scene 439; 228 probes to 439's exit (`--exit`); settle 90 (Marle placed at the top stairs,
slots `$0197=02 / $0199=04` → **Marle is obj2 in scene 5**, obj3 in 439); 44 probes to the bell's talk
tile (87,606). Result: `saves/field_re/s5_bell_marle.state` / `.lakewram`.

The A presses must be **held**, not edged: the bob-stop is op 0x34's `LDA $F2` test of the held joypad.
Headless drives carry them with `CT_HOLDBTN` (the `$00F2` mask hook). The ringing press is the top of the
swing: Crono's bob reaches y=634 at f201 (`local[$14]` = 22); at f197 (y=628) it loses.

## 3. The oracle's sequence (--monitor, `0:10,10:24,0:10,100:4,0:153,100:4,0:155,100:4,0:116,80:60,0:60`)

| frames | what |
|---|---|
| f0–8 | Marle finishes catching up: (94,591) → (87,592), turns down |
| f11–33 | Crono walks Up 24 to (87,583); Marle trails to (87,605), settles at (87,598) **facing north** |
| f44 | A: `$011F=00`, obj1 prio 3 — Crono's fn3; `96 05 24` puts him at (88,588) |
| f56–201 | the bob: y 588 ↔ 634, period ~96. **Marle stays parked at (87,598); Crono bobs THROUGH her** |
| f201 | A at y=634 → stop; f207–215 he returns to (88,591) facing north |
| f241 | ball at its peak y=515; `E8 5A` → sound cmd `005A8005` (the ring) |
| f277–341 | reward box: `$0129` 01→07 (print done, waiting) |
| f360 | A dismisses: 07 → 0D → 00 (f372) |
| f373 | `$0154` bit0 set — op 0xDA's first visit: Marle's script parked at `$16FF`, prio 0 |
| f375–381 | Marle walks ONTO Crono: (87,598) → (88,591), writers C0:30F6 / C0:3149 (op 0x95 + walk-onto) |
| f382 | `$011F=01` from the `$16FF` routine's own `E3 01` (C0:3C0D) |
| f385 | `$0154` cleared and **`$01AB/AC/AD := 48/48/48`** (C0:3483) — the ring reset |
| f480– | Crono walks Right; head 49,4A,… — Marle stands on his tile |
| f505 | gap = 0x18 (ring 61/4A/48): Marle starts trailing, 1:1 behind him |
| f529– | both stop at x=136 (a wall); Marle ends at **(121,591) facing right, ring 78/69/48** |

## 4. The port, before and after

Before the fix the port matched every station above through f382 (positions identical at every
sampled frame but for the harness's one-frame input phase) and then kept `48/39/00`: at f489 Marle's
cursor crossed 0x18 and she walked **north** — (88,598) → (88,580) — while the oracle's Marle stood still.

With the reset ported into `ct_vm.c`'s op 0xDA (cursors := head; the head entry's `$7F0C00/0D00/0F00`
zeroed, literally as the bytes say): `ring=484848` at f385, Marle holds until f504, trails, and the end
state is the oracle's — `selftest-party: ring=78/69/48 fol1=obj2(121,591)f3`.

Pinned as `field-bell-marle-regroup-ring` (quick). The mutation that drops the cursor equalisation is
killed. The head-entry zeroing is ported but unobservable on this fixture (a cursor advances PAST the
head entry; only the C0/9F6F stair hold reads it) and is declared as such.

## 5. Residuals

- The known one-frame input phase of the headless harness (walkscript vs the SNES auto-joypad).
- `$0154` bit0 is set one frame early in the port (f372 vs f373) — the same phase.
- The silver-point tally (`02 00 33` → obj0 fn3, `local[$1B]`) was not read back; the box and the ring
  sound were.

## 6. The ball keeps its FROZEN SHAPE while it rises — the vector moves do not re-anim (2026-09-06)

User: *"the strength indicator that goes up and down is 'changing palette' differently from the original."* It was not a
palette: it was a different **assembly frame** of the same sprite, which reads as a flat orange disc instead
of the shaded striker.

**The script.** obj26 (sprite 63) is the strength indicator. Its fn0 ends with `AC 00` — op 0xAC, the SHAPE
freeze, which writes `$1780,X = 3` and `$1680,X = FF` ("no anim, hold the current assembly frame"). Its fn1
raises it with `9D 1C 14` and drops it with `9C C0 02`.

**The ROM fact.** Ops 0x9C and 0x9D are the two VECTOR moves; the op jump table `C0/5D6E` sends them to
`C0/4EC7` and `C0/4F0A`, and op 0x92 to `C0/4E73`. A scan of the whole ROM for `20 14 56` (`JSR C0/5614`,
the step-issue re-anim) returns thirteen sites:

```
4EC0 4FA5 4FE3 5037 507D 50E4 5122 527B 52B9 53C4 5409 54B0 54EE
```

`4EC0` is inside op 0x92's body, and **there is no site anywhere in `4EC7..4FA4`** — the range that holds
both vector handlers. So the vector moves issue the step and never re-select the animation.

**The port's half-fix.** `ct_vm.c`'s C-side actor mirror already excluded 0x9C/0x9D from the mode-3 promote,
but the WRAM half did not: the vector branch called `mvIssueRegs(sc, obj, 1)`, and `mvIssueRegs` with
`anim != 0` runs `CtVmMoveStepAnim` — the C0/5614 port — over the object's bytes. C0/5614's mode-3 arm
(`LDA $1680,X / CMP #FF / BEQ -> STZ $1780,X`) is exactly the code that lifts a `$1680 = FF` object out of
the freeze, so the ball dropped to `$1780 = 00 / $1680 = 01` on its first step and the drain wrote assembly
frame 1 to `$0F01,X`.

**Measured**, on this same fixture, with `--peek saves/field_re/s5_bell_marle.state out.raw --btnscript
"0:10,10:24,0:10,100:4,0:153,100:4,0:N"` and the `.raw.wram` sidecar, reading obj26's stride `X = 0x34`:

| frame | oracle y | oracle `$1780/$1680/$1301/$0F01` | port after the fix |
|---|---|---|---|
| f213 (first step) | 559.9 | 03 / FF / 00 / 00 | 556.1, 03 / FF / 00 / 00 |
| f225 (mid-rise) | 536.7 | 03 / FF / 00 / 00 | 532.8, same |
| f240 (peak) | 515.4 | 03 / FF / 00 / 00 | 515.4, same |
| f260 (falling) | 530.9 | 03 / FF / 00 / 00 | 534.8, same |
| f300 (back down) | 559.9 | 03 / FF / 00 / 00 | 559.9, same |

Before the fix the port read `$1780 = 00 / $1680 = 01 / $0F01 = 01` from f213 on. `$1681` (the phase counter)
still ticks in both engines and is deliberately **not** graded: with `$1680 = FF` nothing reads it.

**The fix** is one argument: `mvIssueRegs(sc, obj, op == 0x92)` — only op 0x92 re-anims. It is a MECHANISM
change, not a bell fix: every scripted object moved by a vector op now keeps whatever pose its script froze.

**Pinned** by `field-bell-ball-keeps-frozen-shape` (quick tier). It greps the renderer's own `af=` — the
drain's `$0F01,X`, the byte the window actually shows — at four frames spanning rest → first step → mid-rise
→ peak, with the ball's position inside the pinned line so a mutation cannot pass by freezing the ball
instead of animating it. Mutation (restore `mvIssueRegs(sc, obj, 1)`): KILLED at f213.

## 7. "Crono glitches if Marle is in the way" — NOT REPRODUCED (2026-09-06)

Reported alongside section 6. The user then supplied their own snes9x capture of the exact moment,
`marle_no_caminho_bell.state`, converted with `tools/s9x_to_lake.py` onto this scene's own lake state
and kept as `saves/field_re/bellblock_lake.state` / `bellblock.lakewram`. In it Crono is mid-return
(script ip `0x05C6`, prio 3) at tile (5,37) and Marle is parked one tile north, on the marker.

**The VM half agrees byte for byte.** Running that capture forward 240 frames, oracle vs port:
Crono settles at (88, 591.7) and stays, Marle holds (88, 591.9) — the same tile — and the ball rises
559.9 → 517.4 and falls back. Marle's whole per-object row is identical in both engines
(`$0F01 = 00`, `$0F80 = 80`, `$1301 = 00`, `$1600 = 00`, `$1680 = 00`, `$1780 = 00`, `$1100 = 01`),
and so is Crono's (`$0F01 = 03`, `$1301 = 45`).

**Six other approaches to the report, all of them clean:**

| what was tried | result |
|---|---|
| the pinned bell drive, whole minigame with Marle parked in the bob path | port tracks the oracle at every sampled frame (bob y 588↔634) |
| walking the lead INTO the follower (Up 40 with Marle standing north) | identical within 1 px: they swap, both stop at the same wall |
| approaching the marker from the north so the follower blocks the target tile | the conga puts her behind before the talk; the minigame starts normally |
| pressing A for a SECOND game right after the reward box | both engines ignore it — the lead is on the bell tile, not the talk tile |
| Crono's and Marle's anim bytes through the bob (f120/150/180) | `$0F01`, `$1780`, `$1680` identical on both |
| the composed FRAME at the overlap (f480 of the pinned drive, both PCs on (88,591)) | identical — see below |

**The frame.** On a NATIVE oracle state each PC is a 16x64 stack of four 16x16 OBJ entries
(`objSize = 0`, so large = 16x16), the lead at the lower OAM indices. Classifying the rendered frame
by the OBJ-palette colours unique to one palette gives, on BOTH engines, lead = 164 px on rows
112-141 and follower = 5 px on rows 111-130: the ROM hides the follower behind the lead exactly as we
do. A full-frame diff has zero differing pixels in the two characters' columns; the 237 that differ
are all at x ≥ 103 (the fair racers, the harness's known one-frame input phase). Pinned as
`field-follower-on-lead-tile-frame`, mutation KILLED.

**⚠ The trap this cost an hour to.** `tools/s9x_to_lake.py` splices **WRAM + CPU only** — VRAM, CGRAM
and the PPU come from the base state. The OBJ graphics allocator's slot tables (`$0920`/`$0930`/
`$0B80`) ARE spliced, so a converted capture can name VRAM tiles that the base filled with a different
pose, and the ROM re-uploads nothing because nothing changed. On this capture that rendered the
follower **split** — head 16 px too high, legs 32 px too low, torso missing — which looks exactly like
the reported defect and is a conversion artifact. Positions and WRAM from a spliced state are sound;
**sprite pixels from one are not.** Grade pixels on a state the oracle's own loader produced.

Reported to the user as not reproduced under these conditions rather than as absent from the ROM
(see [[verify-negatives-on-proper-state]]).
