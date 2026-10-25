# scene 439 candy shop — control is never released after the scene ends

**Item:** `scene439-control-release`. User: *"in the candy shop of scene 439, after the scene ends, the
movement is not being released"*.

**Verdict: REPRODUCED and DERIVED.** The candy CONCLUSION ends with event op `0xDA` (body `0x0446`).
Our port of that op (`src/ct_vm.c:2285-2297`) hands control back only when **`glb[0x54]` bit 0 is
CLEAR** — and `glb[0x54]` bit 0 is *the cat-delivery story flag* set by scene 439's own cat girl
(obj12, `0x0352: 65 00 54`). Deliver the cat first, then do the candy shop, and our engine locks
forever. The ROM's C0/345A tests **`$7E0154`** (DP `$54` under DP=`$0100`), its own re-entry latch —
a different byte entirely, and one the oracle proves is untouched by the story flag.

Every claim below carries the command that produced it. All oracle work used a copy of `build/ct.exe`
(`ct_probe.exe`); nothing was built or edited.

---

## 1. The repro states

```
ct_probe.exe --teleport 439 24 8 0 --from candy_shop_lake.state --frames 120 --out tele439.state
```
```
teleport[jmp]: from 1B7 -> requested 1B7, now 1B7  scene=LOADED  player=PLACED
  leadPC obj=1 pos=(392,143) facing=0  party=00/01/80 storyline=08
  objects: 15 active, 14 placed
```

A second state with only ONE byte changed (`$7F0054 |= 0x01`, via `tools/ctlib/lake.py`
`WRAM_OFF + 0x10000 + 0x54`), saved as `tele439_cat.state`:

```
ct_probe.exe --read "7F0054:1,7F0055:1,0100:2" --from tele439_cat.state
  7F0054:1             wram[7F0054+1] = 01
  7F0055:1             wram[7F0055+1] = 9A
  0100:2               wram[000100+1] = B7          (= loc 0x1B7 = 439)
```

Drive script (Up into the candy zone, then A-mash): `10:90,` + `100:4,0:8` ×250.

---

## 2. ORACLE — the canonical timeline

```
ct_probe.exe --monitor "011F:1,0162:1,7F023E:2,119C:2,1C1C:1,1182:2,1C02:1" \
             --from tele439.state --input <mash> --diff
```
```
  f0      00011F=01  000162=00  7F023E=0000  00119C=2408  001C1C=07  001182=D504  001C02=07
  f85     00011F=00* 000162=00  7F023E=0000  00119C=1108* ...
  f321    00011F=01* 000162=00  7F023E=0000  00119C=3108* ...
  f861    00011F=00* 000162=00  7F023E=6100* 00119C=3A08* ...
  f865    00011F=00  000162=00  7F023E=6100  00119C=3D08* ...
  f969    00011F=00  ...  00119C=3F08*
  f1009   00011F=00  ...  00119C=4408*
  f1077   00011F=00  ...  00119C=4608*
  f1095   00011F=01* ...  00119C=4608
  f1097   00011F=01  ...  00119C=4708*
```

- **`$0162` is `00` on every row of the whole run, and so is `$0138`** (measured separately:
  `--monitor "0162:1,011F:1,0138:1,7F0054:1,7F0055:1"` → only `$011F` ever changes).
  **`$011F` is the only gate byte that moves in this scenario.**
- `$7F023E` = `loc[0x1F]` (locals are 2 bytes: `$7F0200 + k*2`) — the linger counter, `0x00 → 0x61`.
- `$119C` = obj14's `$1180` IP. **Raw IP = body offset + 0x400** (the 0x400-byte fn-pointer table
  ahead of the body in the `$7F2000` image); the sampled value is the NEXT op.
- obj1 (`$1182`) sits at IP `0x04D5` prio 7 (`$1C02=07`) for the entire conclusion — **the lead never
  enters an event fn during the conclusion; the lead-scripted half of `ctlLocked` is not involved.**

`glb[0x55]` `9A → 92` (candy bit3 cleared) at f865, writer `C0:6BCE`
(`--monitor ... --writer`, run in §4).

### obj14's script (the scene's own bytes)

`python tools/ct.py script 439 --obj 14` gives fn0 @0x03EF ending `0x03F8: 00 RETURN`; the autorun
continuation is `stopAddr+1`. Decoded from there with `ctlib.script.iter_ops(body, 0x03F9)`:

```
  0x03F9: 16 55 08 06 4A     IFFLAG glb[85]bit/cmp6 else+74      <- the candy gate (bit3)
  0x03FE: 12 0C 04 03 22     IF loc[12]?4 cmp3 else+34           <- Crono tile Y < 4
  0x0403: 12 0B 17 02 1D     IF loc[11]?23 cmp2 else+29          <- tile X >= 23
  0x0408: 12 0B 1A 03 18     IF loc[11]?26 cmp3 else+24          <- tile X < 26
  0x040D: E3 00              SETCTRL $011F 00                    <- f85 LOCK
  ...  the stand choreography (obj3 fn6, obj1 fn7/fn8, obj3 fn7/fn8)
  0x0420: 75 1E              loc[30]=1
  0x0422: E3 01              SETCTRL $011F 01                    <- f321 UNLOCK
  0x0424: 12 1E 00 02 1F     IF loc[30]?0 cmp2 else+31
  0x0429: 12 0C 06 02 04     IF loc[12]?6 cmp2 else+4            <- walked SOUTH -> the drag
  0x042E: 04 1A 34           CALL/REQ obj13 fn4                  <- the "kidnapper" drag block
  0x0431: 71 1F              loc[31]++                           <- the LINGER counter
  0x0433: 12 1F 60 02 10     IF loc[31]?96 cmp2 else+16          <- > 0x60 -> the CONCLUSION
  0x0438: E3 00              SETCTRL $011F 00                    <- f861 LOCK
  0x043A: 66 03 55           CLRGLBFLAG glb[85].bit3             <- f865 (C0:6BCE)
  0x043D: C2 42              TEXTBOX bottom str=66
  0x043F: 04 06 0A           CALL/REQ obj3 fn10
  0x0442: 7D 12              HIDE obj9
  0x0444: BB 43              TEXTBOX auto str=67
  0x0446: DA                                                     <- f1077..f1095 THE RELEASE
  0x0447: 11 4F              JUMPBACK -79 -> 0x03F9
  0x0449: B2                 HANDTOPLAYER(0xB2)                  (unreachable)
  0x044A: 00                 RETURN
```

After the conclusion the oracle alternates IP `0x07F9`/`0x0847` forever (= body `0x03F9` gate →
`0x0447` JUMPBACK), prio 7, `$011F=01` — a harmless failing-gate poll loop. That part our port
already matches.

---

## 3. What op 0xDA actually is (C0/345A) — disassembled

`python tools/dis.py 345A 0x60` and `python tools/dis.py 34DA 0x68`:

```
  345A: STY  $C7
  345F: LDA  $99          <- party slot 1 index ($7E0199)
  3461: BPL  ->3466       <- bit7 SET (no follower) -> 3463 INX / CLC = IP+1, INERT
  3466: LDA  $54          <- $7E0154, DP=$0100
  3468: BIT  #01
  346A: BEQ  ->349F       <- latch clear -> FIRST visit (release)
  346C: LDA  $7F03FE      <- latch set -> LATER visit: wait for the phase
  3470: CMP  #03
  3472: BCS  ->3477
  3474: TYX / 3475 CLC / 3476 RTS      <- phase < 3: PARK, IP unchanged
  3477: LDA #01 / 3479 TRB $54         <- phase >= 3: drop the latch
  347D..3496:  the five-store clear ($7F0C00,X x2, $7F0D00,X x2, $7F0F00,X, X=$AB)
  349A: LDX $C7 / 349C INX / 349D SEC  <- IP += 1, end the visit
  349F: STX  $C3
  34A1: LDA  $99 / 34A3 TAX            <- release slot 1 (C0/32C8 copy)
  ...
  34E2: LDA  $9B                       <- party slot 2
  34E4: BPL  ->34F4                    <- present -> release it too
  34E6: LDA #02 / 34E8 STA $7F03FE     <- ABSENT: phase := 2
  34EE: LDA #01 / 34F0 TSB $54         <- raise the latch
  34F2: CLC / 34F3 RTS                 <- PARK
  34F6..352C: release slot 2 (same block)
  3533: LDA #01 / 3535 STA $7F03FE     <- BOTH released: phase := 1
  3539: LDA #01 / 353B TSB $54
  353D: CLC / 353E RTS                 <- PARK
```

The release block itself (already ported verbatim in `src/move.c:1506` `ctVmPartyRelease`, and swept
by `--sweepops`) parks the released PC's script pointer at the literal **`$16FF`**:

```
  3519: LDA  #16FF
  351C: STA  $1180,X
  3521: LDA #00 / 3523 STA $1C00,X     <- priority 0
  3526: STZ $1A80,X / 3529 STZ $1A01,X / 352C STZ $1001,X
```

### The shared routine at script IP `$16FF`

Read straight out of the oracle's script image (`$7F2000 + 0x1700`):

```
ct_probe.exe --read "7F36F0+0x60" --from tele439.state
  wram[7F36F0+96] = 00000000000000000000000000000000
                    [25 ROM bytes at 0x5D37: python tools/dis.py 0x5D37 25]
                    E3 00 89 40 0D 00 AE ...
```

Byte-for-byte identical to `src/data/ct_regroup.h`'s `ctRegroupReturn` — the block `C0/5CEC` copies to
`$7F3700`, which our engine **already materializes** at `sc->rgBase` (`src/ct_vm.c:2704-2717`).
Decoded:

```
  1700: E3 00     lock $011F
  1702: 89 40     (speed)
  1704: 0D 00     collision off
  1706: 0E 02     arrival-snap mode 2
  1708: AE        autoanime
  1709: 95 00     MOVE-toward-PC        <- the follower walks back to the lead
  170B: 0E 00     snap mode 0
  170D: 71 FF     loc[0xFF]++           <- loc[0xFF] = $7F0200 + 0xFF*2 = $7F03FE  = THE PHASE
  170F: 12 FF 03 04 04                  <- barrier: only the LAST arriver falls through
  1714: E3 01     UNLOCK $011F          <- THE RELEASE
  1716: 00        RETURN
  1717: 11 09     JUMPBACK
```

`71` = op INC-local, handler `C0/6C78 LDA $7F0200,X / INC A / STA $7F0200,X` (`dis.py 6C70 0x20`);
`E3` = `C0/3C05 ... 3C0B STA $1F` (`dis.py 3C05 0x14`) = `$7E011F`.

**So the barrier law is `phase := 3 - nFollowers`** (2 for one follower, 1 for two) — the identical
law our `CtSceneBattleReturn` already uses (`src/ct_vm.c:4270`, `w8(... LOC + 0xFF*2, (3 - nf))`).

---

## 4. ORACLE — the mechanism, measured with writer PCs

```
ct_probe.exe --monitor "011F:1,0154:1,7F03FE:1,0199:1,019B:1" \
             --from tele439.state --input <mash> --diff --writer
```
```
  f0      00011F=01  000154=00  7F03FE=00  000199=06  00019B=80
  f85     00011F=00*(C0:3C0D)  000154=00  7F03FE=00  000199=06  00019B=80
  f321    00011F=01*(C0:3C0D)  000154=00  7F03FE=00  ...
  f861    00011F=00*(C0:3C0D)  000154=00  7F03FE=00  ...
  f1077   00011F=00           000154=01*(C0:34F2)  7F03FE=02*(C0:34EC)  ...
  f1095   00011F=01*(C0:3C0D)  000154=01           7F03FE=03*(C0:6C81)  ...
  f1097   00011F=01           000154=00*(C0:347B)  7F03FE=03  ...
```
(`--monitor --writer` reports the instruction AFTER the store, per `.claude/skills/re-derive`.)

And the released object, obj3 = Marle (`$0199 = 0x06` = obj index 3; `$019B = 0x80` = slot 2 absent):

```
ct_probe.exe --monitor "1186:2,1C06:1,011F:1,7F03FE:1" --from tele439.state --input <mash> --diff --writer
  f1006   001186=8005*(C0:5AC2)  001C06=07*(C0:5FA3)  00011F=00  7F03FE=00
  f1077   001186=FF16*(C0:34CC)  001C06=00*(C0:34D3)  00011F=00  7F03FE=02*(C0:34EC)
  f1078   001186=0817*(C0:5AC2)  001C06=00           00011F=00(C0:3C0D)   7F03FE=02
  f1095   001186=1517*(C0:5AC2)  001C06=00           00011F=01*(C0:3C0D)  7F03FE=03*(C0:6C81)
  f1096   001186=8005*(C0:5AC2)  001C06=07*(C0:5FA3)  00011F=01  7F03FE=03
```

The whole chain, end to end, with no inference left in it:

1. **f1077** — op 0xDA's first visit. Marle's `$1180` := `$16FF` (`C0:34CC`), her `$1C00` := 0
   (`C0:34D3`), `$7F03FE` := 2 (`C0:34EC`, the slot-2-absent arm), `$0154` bit0 := 1 (`C0:34F2`).
   obj14's IP stays parked on `0xDA`.
2. **f1078-f1094** — Marle runs the `$16FF` block: her IP walks `0x1708 → 0x1715`; its opening
   `E3 00` re-writes `$011F` (`C0:3C0D`), `95 00` walks her to the lead.
3. **f1095** — she reaches the barrier: `71 FF` bumps `$7F03FE` 2→3 (`C0:6C81`) and, the same frame,
   her `E3 01` at block offset `0x14` writes **`$011F = 01`** (`C0:3C0D`). Her IP `0x1715` is exactly
   that op's operand byte. **This is the release.**
4. **f1096** — `00 RETURN`: her IP/prio pop back (`0x0580`, prio 7).
5. **f1097** — op 0xDA sees `$7F03FE >= 3`, `TRB $54` (`C0:347B`), runs the five-store clear, and
   advances obj14's IP to `0x0847` (the JUMPBACK).

**`glb[0x54]` (`$7F0054`) appears nowhere in it.** Proof by the one-byte A/B on the oracle:

```
ct_probe.exe --monitor "011F:1,0154:1,7F03FE:1,7F0054:1,7F0055:1" --from tele439_cat.state --input <mash> --diff --writer
  f0      00011F=01  000154=00  7F03FE=00  7F0054=01  7F0055=9A
  f85     00011F=00*(C0:3C0D)  ...  7F0054=01
  f321    00011F=01*(C0:3C0D)  ...  7F0054=01
  f861    00011F=00*(C0:3C0D)  ...  7F0054=01
  f865    ...  7F0055=92*(C0:6BCE)
  f1077   000154=01*(C0:34F2)  7F03FE=02*(C0:34EC)  7F0054=01
  f1095   00011F=01*(C0:3C0D)  7F03FE=03*(C0:6C81)  7F0054=01
  f1097   000154=00*(C0:347B)  7F0054=01
```

Frame-for-frame identical to the `$7F0054=00` run. **The story flag has no effect on the ROM.**

---

## 5. OUR ENGINE — the same A/B, and the freeze

`story8_cat.bin` = `story8.bin` with byte `0x54 |= 0x01` (only that byte differs).
Drive: `u:100`, then `a:1,i:11` ×100, then `d:300` (walk away at the end).

```
SDL_VIDEODRIVER=dummy CT_CARRYPARTY="0,1,128" CT_STORYDBG=1 build/Belthasar.exe \
  --assets ct_assets.dat --scene 439 --loadflags <FLAGS> --at 392 143 \
  --selftest --frames 1600 --walkscript "u:100,(a:1,i:11)x100,d:300"
```

```
=== flags story8.bin  ($54 = 00) ===
f0   storyline=8 $54=00 explore=1
f83  storyline=8 $54=00 explore=0        <- candy lock      (oracle f85)
f184 storyline=8 $54=00 explore=1        <- candy unlock    (oracle f321)
f823 storyline=8 $54=00 explore=0        <- conclusion lock (oracle f861)
f943 storyline=8 $54=00 explore=1        <- RELEASED        (oracle f1095)
selftest: ... player (456,256) ...       <- he walks away

=== flags story8_cat.bin  ($54 = 01) ===
f0   storyline=8 $54=01 explore=1
f83  storyline=8 $54=01 explore=0
f184 storyline=8 $54=01 explore=1
f823 storyline=8 $54=01 explore=0        <- conclusion lock
(no further line to frame 1600 — explore stays 0)
selftest: ... player (456,111) ...       <- FROZEN at the candy stand
```

Held out to 2100 frames with `CT_STORYOBJ=14`: still `explore=0`. WRAM dump of the *passing* run
(`CT_DUMPWRAM_AT=1500`) confirms the good path ends at `011F=01 0162=00 0138=00`, `glb[55]=80`
(bit3 correctly cleared).

**The single line responsible — `src/ct_vm.c:2294`:**

```c
if (!(r8(sc->mem, GLB + 0x54) & 1)) sc->explore = 1;
```

with `#define GLB 0x7F0000u` (`src/ct_vm.c:98`). `grep -rn "GLB + 0x54\|0x7F0054" src/*.c` returns
**exactly this one site**, so the A/B above isolates it completely.

The flag it reads is set by scene 439 itself — the cat-delivery girl, obj12
(`ctlib.script.iter_ops(body, 0x0320)`):

```
  0x033B: 12 19 25 02 18     IF loc[25]?37 cmp2 else+24      <- the cat's tile X > 37
  0x0340: BB 09              TEXTBOX auto str=9              <- "You brought back my cat!"
  0x0352: 65 00 54           SETGLBFLAG glb[84].bit0         <- glb[0x54] bit 0 := 1
```

**Reproduction path in real play:** bring the cat to the girl (obj12, tile 38,13) *before* walking
into the candy zone. Both are ordinary post-join fair activities.

---

## 6. Where our port stands vs. the ROM

| ROM (C0/345A) | our `src/ct_vm.c:2285-2297` |
|---|---|
| tests `$7E0154` bit0 (its own re-entry latch) | tests `$7F0054` bit0 (the cat story flag) |
| BLOCKS: parks the IP until `$7F03FE >= 3` | never blocks (`s->addr = nxt; return 0`) |
| releases the followers onto the `$16FF` block | releases nothing |
| seeds `$7F03FE` = 2 / 1, raises `$0154` bit0 | writes neither |
| control is restored by the block's own `E3 01`, after the follower WALKS BACK | `sc->explore = 1` instantly, on the wrong condition |
| clears `$0154` bit0 + the five stores on completion | — |

The mechanism is not missing from the codebase: `ctVmPartyRelease` / `ctVmPartyClear` /
`case 0xDA` already exist micro-executed in `src/move.c:1506` / `src/move.c:3266` (swept by
`--sweepops`), the `$7F3700` block is already baked (`src/data/ct_regroup.h`) and materialized at
`sc->rgBase`, and the `3 - nFollowers` barrier seed + the "CALL the block on each follower" loop
already exist in `CtSceneBattleReturn` (`src/ct_vm.c:4261-4283`). Only the *field VM's* op-0xDA
handler was left as a net-effect shortcut.

---

## 7. FIX SPEC (no code — the mandate-safe change)

`src/ct_vm.c:2285-2297`, op `0xDA`. Delete the `glb[0x54]` gate outright and run C0/345A's arms:

1. **C0/345F-3465** — if there is no follower in party slot 1, `s->addr = nxt` and return 0. Nothing
   else. (Follower identity: reuse the pair `CtSceneBattleReturn` already derives —
   move-mode 1 then 2, cited to C0/0330 at `src/ct_vm.c:4263-4268`. The raw `$0197/$0199/$019B`
   bytes in our `Mem` are **not** the live party slots: `memBoot` seeds them `0x80`
   (`src/ct_vm.c:180`) and `field.c:805` reuses them as collision scratch.)
2. **C0/3466-3476** — if `$7E0154` bit0 is set: if `loc[0xFF]` (`$7F03FE`) `< 3`, **yield with the IP
   parked** (`return 1`, `s->addr` unchanged). Otherwise clear `$7E0154` bit0 (C0/3477-3479), run the
   C0/3484 five-store clear, `s->addr = nxt`, end the visit.
3. **C0/349F-353E** — first visit: for each present follower, do the C0/32C8 release the port already
   models (`ctVmPartyRelease` semantics): push its current IP, point it at `sc->rgBase` (our
   materialized `$7F3700` / script-IP `$16FF` block), priority := 0, zero mover/ticket/cadence — i.e.
   exactly what `CtSceneBattleReturn` does per follower. Then set `$7F03FE` := 2 if slot 2 is absent
   (C0/34E6) else 1 (C0/3533), set `$7E0154` bit0 (C0/34F0 / C0/353B), and **yield with the IP parked**.

Control then comes back where the ROM puts it: the block's own `71 FF` / `12 FF 03 04 04` / `E3 01`,
through the existing op-0xE3 handler (`src/ct_vm.c:2280`), when the last follower arrives. No scene
gate, no story flag, and the same machine as the post-battle regroup.

---

## 8. NOT DERIVED / open

- **The op-0xDA span (oracle 18 frames, f1077→f1095) is NOT a constant** — it is however long the
  follower's `95 00` walk takes. Nothing here pins a frame count, and none should be baked.
- **Solo-party arm (`$0199` bit7 set → op 0xDA is inert, IP+1, nothing re-unlocks `$011F`):
  disasm-only, unconfirmed.** Not reachable on the candy path (obj14's gate is `glb[0x55]` bit3 =
  Marle joined, so slot 1 is always occupied), so I did not construct a state for it.
- **`glb[0x55]` seed mismatch:** our `story8.bin` carries `0x88`, the oracle state `0x9A` (bits 1 and
  4 extra). Irrelevant to this item (bit3 is the candy bit and both have it); not investigated.
- **Unrelated observation, NOT diagnosed:** from scene 439's *default* spawn our engine's lead can
  only walk DOWN — `--walkscript u/l/r:40` all leave him at `(392,176)`, only `d:40` moves him
  (to `(392,216)`). With `CT_CARRYPARTY="0,1,128"` obj3 spawns at the *same pixel* `(392,176)`
  (`CT_DUMPALL=1`), and without it obj18 sits at `(392,159)`. That is why this repro places the lead
  with `--at 392 143` (the oracle's own teleport landing). It may be a separate defect; it is not
  the freeze reported here and I did not chase it.
- `$0162` and `$0138` were measured at `00` for every frame of the oracle run, so nothing in this
  item exercises them. A claim that they are *never* used in scene 439 would be a negative I did not
  test properly — this only says they never move in this scenario.

## 9. Which followers the DA releases -- the party slots, not the move modes (2026-10-20)

C0/345A takes the followers from the PARTY SLOTS, the bytes `$0199` / `$019B` (object * 2; bit 7 = the slot is empty):
`345F LDA $99 / BPL` (slot 1; empty -> IP + 1, CLC, the visit ends), `34A1 LDA $99 / TAX` (the release of slot 1),
`34E2 LDA $9B / BPL` (slot 2; empty -> `$7F03FE := 2`, one arrival owed), else the release of slot 2 and `$7F03FE := 1`.
The battle return C0/0330 reads the same two bytes (`0355 LDA $99 / BPL`, `036D LDA $9B / BPL`). The port used to pick
the objects of move-mode 1 and 2 instead; a member who has LEFT the party keeps his move mode. MEASURED, the playthrough's
leg 27 (the rule's cartridge, from leg 26's end in 078, the throne room after Yakra): the scene's `5A 1C / D4 04 / D1 04 /
DA` leaves `$0197/$0199/$019B` = `02 06 8A` (Frog's slot emptied) on both sides; at the DA the cartridge stores `$7F03FE` =
02 (f584) and gives control back at f602 when Lucca arrives, where the port stored 01, waited for Frog as well and gave it
back at f659 -- and every press of the leg after that missed. Both now read the slots (src/ct_vm.c ctPartyFollowers):
control at f602, and legs 01-26 unchanged. The solo-party arm of sec. 8 is the same read: with `$0199` = 80 the DA steps
over itself.
