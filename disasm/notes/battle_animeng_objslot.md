# BATTLE ANIM ENGINE — PART 1: the OBJ-slot TICK (FD/E39C) = the TILE/PALETTE frame streamer

Derives, from the ROM disasm (`tools/dis.py`, HiROM flat = `(bank−0xC0)<<16|addr`) + the LakeSnes oracle
(`ct.exe --monitor`, base `gato_press_action.state`, HOLD driver `100:8,0:N`) **only**, the core of the
battle animation engine's per-frame OBJ-slot tick — the routine `battle_stance.md` S4 flagged as
`FD:E3A9-E3CE` (per-slot records `$0520+n*0xC`). **Zero invention.** Every field/handler is byte-cited and
cross-checked against a live monitor of the `$0520` region across the GATO fight.

> ⚠ **CORRECTION to the anchoring assumption (`battle_stance.md` S4).** S4 guessed FD:E3A9's "output =
> the OAM shadow `$7E0700`" and that it drives the combatant idle poses. **BOTH are wrong.** FD/E39C is the
> battle **TILE-GRAPHICS + PALETTE frame streamer**: it advances 12 per-slot tile-animation channels and
> streams their pixels/colours into the **OBJ-tile DMA staging buffer `$7E2000/$7E2200`** (NOT the OAM).
> It does **not** touch `$7E0700` and does **not** animate GATO/Crono's poses — those are the separate
> POSE/ASSEMBLY engine derived in `battle_animeng_script.md` (Part 2). S4's field GUESSES, however, are
> exactly right: `+4`=tick, `+5`=period, `+3`=frame index, `+6..+11`=pointers. The dossier just
> mis-identified what the ticked slots produce. Live proof below (§4): every `$0520` slot sits at **mode 0
> (dormant)** through GATO's whole idle + attack-A + physical strike; the only activation in the fight is
> the **SONG**, which lights slots 8-11 as **mode 5** (the musical-note colour effect).

Oracle exe: `ct_slot.exe` (= `build/ct.exe`, copied to scratch). All `$0520` semantics confirmed with
`--monitor "0520:144"` (12 slots × 12 bytes, decimal LEN) on `gato_press_action.state`.

---

## 1 — Routine anatomy: FD/E39C, DP=$0500, 12 slots × 12 bytes, RTL

Entry **FD/E39C** (a far/JSL target — ends `RTL` at FD/E436). `tools/dis.py 0x3DE39C`:

```
FD/E39C  PHB / PHD                 ; save caller bank + direct page
FD/E39E  TDC / PHA / PLB           ; DB = low byte of caller's D
FD/E3A1  LDX #$0500 / PHX / PLD    ; ** DP = $0500 ** (so DP temps $18/$1A -> $0518/$051A)
FD/E3A6  LDA #$00 / XBA            ; clear the 16-bit accumulator's high byte
FD/E3A9  LDY #$0000                ; slot index Y = 0            (X/Y are 16-bit here)
   loop body @FD/E3AC ...
FD/E425  LDA #$00 / XBA / TYA / CLC / ADC #$0C / TAY   ; Y += 0x0C  (12-byte stride)
FD/E42D  CMP #$90 / BEQ FD/E434 / JMP FD/E3AC          ; ** cap 0x90 -> 0x90/0x0C = 12 slots **
FD/E434  PLD / PLB / RTL
```

**Slot count = 12** (Y = 0,C,18,24,30,3C,48,54,60,6C,78,84 then stops at 0x90). Slots occupy
**`$0520 .. $05AF`** (12 × 12 bytes = 0x90). The mission's "monitor 0520:0xC0-ish" is 0x90 in fact.

**The tile-animation output plane is `$7E2000/$7E2200`** — a WRAM staging buffer for OBJ character data,
DMA'd to VRAM by the frame's transfer (the `battle_boot.md §2` / `battle_stance.md` S1 "$7F04D0 / FD:E170
chip streamer" tail). Every handler below reads from ROM bank **$F6** (or from `$7E2000`) and writes to
`$7E2200` — never to `$7E0700`.

## 2 — The 12-byte slot record (byte-by-byte, live-confirmed)

The **union** is keyed on the mode nibble (`+0>>4`). Two field-layouts coexist:

| off | tile-stream modes (1,8) / scroll (2,3) | palette-osc modes (4,5,6,7) | tick uses |
|---|---|---|---|
| **+0** | `mode<<4 \| frameCount` | `mode<<4 \| count` | mode dispatch; count = loop length |
| **+1** | dest tile base (word offset into `$7E2200`) | dest colour base | — |
| **+2** | words per frame / tiles to scroll | colour count | stream/scroll/osc loop count |
| **+3** | **frame index** (0..count, wraps) | oscillator **step/velocity** (signed) | advanced on frame tick (modes 1,8) |
| **+4** | **tick counter** (++ every call) | same | `++`; compared to +5 |
| **+5** | **period** (frame advances when tick==period) | sign-gate (BMI branch) | frame-tick gate |
| **+6..+8** | 24-bit ROM tile-source pointer (bank $F6) | osc params | src of the streamed pixels |
| **+9..+11** | 24-bit **frame-list pointer** (mode 8) | osc params | mode-8 indirection table |

Live snapshot (`--monitor "0520:144"`, `gato_press_action` f0, decoded by `parse_slots.py`):

```
slot0 $0520: 00 28 01 01 0F 10 | ptrA(+6..8)=F67456  ptrB(+9..11)=FDFE6C   mode8 (dormant, +0 cleared to 00)
slot1 $052C: 00 51 08 0B 07 08 | ptrA=F67628         ptrB=FDFE7F           mode8/period8
slot2 $0538: 00 38 04 03 07 08 | ptrA=F67580         ptrB=FDFE96
slot3 $0544: 00 4E 01 00 01 02 | ptrA=F67442         ptrB=(unused, mode1)  mode1
slot4 $0550: 00 62 03 05 0F 10 | ptrA=F67A34         ptrB=FDFEAA
slot8 $0580: 00 10 38 08 00 02 | ptrB=150E18   |  slot9 $058C: 00 48 38 08 00 02 | ptrB=150E16
slot10 $0598:00 10 10 10 00 01 | +7/+8=F0FF  ptrB=001C00  |  slot11 $05A4: 00 19 03 08 00 02
```

Only **+4 (tick)** changes over the 646-frame idle (the tick runs unconditionally every frame for every
slot); **+0 (mode) stays 0** and **+3 (frame idx) never advances** — the streamer is dormant (§4).

## 3 — The tick control flow (FD/E3AC, byte-exact)

```
FD/E3AC  LDA $0524,Y / INC A / STA $0524,Y   ; +4 tick++   (EVERY slot, EVERY frame, before any gate)
FD/E3B3  CMP $0525,Y / BNE ->next            ; if tick != +5 period -> DONE this slot (no advance)
   ; --- tick reached the period: advance one animation frame ---
FD/E3B8  STZ $0524,Y                         ; +4 tick = 0
FD/E3BD  LDA $0520,Y / AND #F0               ; mode nibble
FD/E3C2  CMP #10 / BEQ .adv                  ; mode 1  ->
FD/E3C6  CMP #80 / BNE .disp                 ; mode 8  -> advance +3 (only modes 1 & 8 loop a frame index)
 .adv:
FD/E3CA  LDA $0523,Y / INC A / STA $0523,Y   ; +3 frameIndex++
FD/E3D1  LDA $0520,Y / AND #0F / INC A       ; count+1  (loop length from the +0 low nibble)
FD/E3D7  CMP $0523,Y / BNE .disp
FD/E3DC  STZ $0523,Y                         ; wrap frameIndex -> 0
 .disp:
FD/E3E1  LDA $0520,Y / LSR×4                  ; mode = +0 >> 4
FD/E3E8  BEQ ->next                           ; ** mode 0 = INACTIVE -> skip (the idle state) **
         DEC A×n / BNE -> the 8-way dispatch (see §5)
```

**Summary:** `+4` ticks every frame; at `+4 == +5` the tick resets `+4`, and for **modes 1 & 8** bumps the
**`+3` loop frame-index** (wrapping at `(+0 & 0x0F)+1`); then it dispatches the mode handler. **Mode 0 does
nothing** (the dormant/idle state — what every slot sits at during the GATO idle). So `+5` = the animation
SPEED (frames of dwell per tile-frame), `+0` low nibble = the loop length, `+0` high nibble = the effect
class.

## 4 — The 8 mode handlers (what each reads/writes)

Dispatch order from FD/E3EA (`DEC A / BNE` chain): mode1→E437 · 2→E532 · 3→E4E8 · 4→E82C · 5→E5A4 ·
6→E807 · 7→E57C · 8→E485.

**Modes 1 & 8 — TILE STREAMERS** (`FD/E437`, `FD/E485`): copy `+2` words of 4bpp tile pixels from ROM
into the OBJ buffer.
```
FD/E437 (mode 1, DIRECT):
  dest $18 = $7E:(2200 + (+1)*2)
  X = (+6/+7) + (+3 frameIdx) * ((+2)*2)     ; $4202=+3, $4203=(+2)*2, product $4216
  copy (+2) words from $F6:X -> [$18]        ; writer FD:E473
FD/E485 (mode 8, INDIRECT via a frame-LIST):
  realFrame = [ (+9/+10) + (+3 frameIdx) ]   ; bank (+11)   <- +9..11 = the FRAME-LIST pointer
  then identical stream with realFrame in place of +3        ; writer FD:E4D6
```
So a **mode-1** slot plays its ROM tile frames sequentially (`+3` = 0,1,2,…); a **mode-8** slot plays a
*named sequence* — `+3` indexes a **frame-list** (the low-level anim script, §6) whose bytes are the real
tile-frame numbers.

**Modes 2 & 3 — TILE-BUFFER SCROLLERS** (`FD/E532` up, `FD/E4E8` down): rotate `(+2)` tiles of the OBJ
buffer between the twin planes `$7E2000`/`$7E2200` by ±1 row (`$2002,X→$2000,X` INX loop / `$1FFE,X→$2000,X`
DEX loop), saving/wrapping the end tile — a marquee/scroll effect on already-resident tiles.

**Modes 4,5,6,7 — PALETTE COLOUR OSCILLATORS** (`FD/E82C`,`E5A4`,`E807`,`E57C`): accumulate `+7 += +3`
(signed velocity), gate/clamp against `+8`, then (tail `FD/E5E1`→`FD/E64F` for 5/7, `FD/E869`→`FD/E8D7`
for 4/6) read each BGR555 colour word from `$7E2000,X`, scale its R(`&1F`)/G(`&3E0`)/B(`&7C00`) components
by the accumulator via the hw multiply, and write the result to `$7E2200,X`.
- `FD/E64F` (modes 5,7): `component × phase >> ...` → **fade toward black**.
- `FD/E8D7` (modes 4,6): `component + (component EOR mask)×phase` → the complementary **fade toward white**.
- modes 4/5 **clamp and stop** (`STA $0520,Y = 0` on limit → a one-shot fade); modes 6/7 **ping-pong**.

## 5 — The `$FDFExx` anim-script DEFINITIONS (the source the slots are loaded from)

The `+9..+11` pointers (`$FDFE6C`, `$FDFE7F`, `$FDFE96`, `$FDFEAA` for slots 0,1,2,4) point into a table in
bank **FD around `$FE00`**. Each entry is a **7-byte header + a frame-list**:

```
[+0 mode|count] [+1 dest] [+2 size] [+5 period] [+6 srcLo] [+7 srcHi] [+8 srcBank=F6]   <- 7-byte header
[frame indices ...]                                                                      <- the frame-LIST
```
The live slot is loaded from it: header → slot `+0,+1,+2,+5,+6,+7,+8`; `+9/+10/+11 = &frame-list`
(right after the header); `+3 = 0`, `+4 = 0`. Verified byte-for-byte — e.g. slot0's live
`+1=28 +2=01 +5=10 ptrA=F67456` matches header **`$FDFE65: 8B 28 01 10 56 74 F6`** (mode 8, count 0xB,
period 0x10), and its `ptrB=$FDFE6C` is the frame-list immediately after:

```
$FDFE6C:  00 00 00 01 02 03 04 04 04 03 02 01    <- slot0 frame-list: a 12-entry PING-PONG (frames 0->4->0)
$FDFE7F:  00 00 00 00 00 00 01 02 03 04 05 05 04 03 02 01   <- slot1 frame-list
$FDFE96:  00 01 02 03 04 05 05 05 05 04 03 02 01            <- slot2 frame-list
```
So a mode-8 tile animation = **stream tile[frameList[i]] for each i, `+5` frames of dwell each, looping** —
e.g. slot0 cycles its ROM tile frames 0,0,0,1,2,3,4,4,4,3,2,1 at 0x10 dwell.

## 6 — Live validation (the mandate step): dormant idle · mode-5 song

`--monitor "0520:1,052C:1,…,05A4:1,9877:1,A013:1"` (the 12 mode bytes + action state) over a full HOLD
fight (`slot_modes.txt`):
- **f0–f477 idle, f478 attack-A (`$9877=31,$A013=1`), f635 strike:** **every slot mode stays 0.** The
  streamer contributes *nothing* to GATO's idle poses or his physical — those are the pose engine (Part 2).
- **f1151 SONG (`$9877=10`):** at **f1193** slots 8,9,10,11 (`$0580/$058C/$0598/$05A4`) flip to **mode 5**
  (`+0 = 0x50`), then release in sequence (f1214-1238). Full dump at f1204: `$0580 = 50 50 30 10 02 03 00
  BF 80 …` = mode-5 colour oscillator, source ptr `$80BF00`, period 3. These four are the sweeping
  **musical-note** colour effects (`battle_gato_moveset.md` B.3 "five moving-effect tracks"). This is the
  ONLY streamer activity in the GATO fight — confirming FD/E39C is an **effect/tile** engine, not a pose
  engine.

## 7 — Slot count + what the 12 slots ARE (mapping)

**12 slots, NOT a 1:1 combatant map.** They are general **tile/palette animation CHANNELS** assigned to
battle visual elements. In the GATO fight: slots 0–4 carry mode-1/8 tile animations sourced from bank F6
(dormant here — animated background chips / pre-armed effect channels; the exact element each drives is not
pinned, §9), slots 8–11 are the song's note-effect channels. The combatant SPRITE poses (GATO claw-swap,
Crono bob, attack swings, the four facings) are entirely the **`$1CA2/$1CC3` pose engine** of Part 2 — none
of them touch `$0520`.

## 8 — Integration note (how Part 1 composes with the rest)

- FD/E39C runs **once per frame** (proven: `+4` ticks every frame for every slot; the oracle's per-frame
  `--monitor` shows the increment). Its job is to keep the OBJ-tile buffer `$7E2000/$2200` current for any
  slot whose tile pixels or palette are animating.
- It is **downstream of nothing in the pose pipeline** — a port can implement Part 2 (poses) and Part 1
  (tile/palette effects) independently. The only coupling is that both ultimately feed the OBJ hardware:
  Part 2 writes **positions/tile-numbers to OAM `$7E0700`**; Part 1 writes **tile PIXELS/PALETTE to the
  `$7E2000/$2200` DMA buffer**. A sprite whose OAM tile-number points at a slot's dest region shows the
  streamed pixels.
- **Port shape:** 12 records `{mode,count,dest,size,frameIdx,tick,period,srcPtr24,listPtr24}`; a per-frame
  tick (`+4`/`+5`/`+3` as §3); 8 handlers (2 ROM→buffer copiers, 2 buffer rotators, 4 BGR555 component
  scalers, §4); a loader that expands the `$FDFExx` header+frame-list into a record (§5). The streamed
  pixels come from **ROM bank F6** (a bake target, like `ct_charbase.h`) and the frame-lists from bank FD.

## 9 — Open questions
- **The caller.** No static `JSL/JML/pointer` to FD/E39C or FD/E3A9 exists in the ROM (exhaustive scan:
  zero hits for `22/5C ?? 9C E3 FD`, `?? A9 E3 FD`, and the 3-byte pointer). It runs every frame
  (`+4` proves it), so it is dispatched through a **runtime-built vector** (CT's indirect-JSL style, cf.
  the `$B85F`/`$B8BB` battle tables) or an NMI/transfer-time callback. Pinning the exact dispatch site is
  open.
- **Slot→element assignment.** Which battle element each of the 12 channels drives (which are animated
  background chips vs. pre-armed effect slots) and the routine that *chooses* which `$FDFExx` scripts to
  load into `$0520` at battle start — not traced (the slots were dormant all fight; only the song wrote
  new ones live, and its writer is the song's tech-effect setup, `battle_gato_moveset.md` G.2).
- **The DMA of `$7E2000/$2200 → VRAM`.** The per-frame transfer that ships the streamed buffer to OBJ VRAM
  (ch7, `$7F04D0`/FD:E170 per `battle_boot.md §2`) is cited but not re-derived here.
- **Mode-4/6 vs 5/7 exact colour math + the clamp/ping-pong stop conditions** — the component-scale is
  decoded (§4); the precise fixed-point rounding of `FD/E64F`/`FD/E8D7` and the accumulator limit tests
  (`+8`) were read structurally, not bit-exact-validated (no active mode-4/6 capture in this fight; only
  mode-5's activation was observed).

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/C--Users-Icaro-Ferris-Desktop-Crono-Trigger-Decompile/e5608df4-.../scratchpad/`
- `ct_slot.exe` (= build/ct.exe). Base state (repo root) `gato_press_action.state`.
- `slot_diff.txt` (`--monitor "0520:144"` 646f idle), `slot_modes.txt` (12 mode bytes + action state, 1520f
  — the idle-dormant / song-mode-5 proof), `parse_slots.py` / `analyze_slots.py` (decoders).
- Disasm: `tools/dis.py 0x3DE39C 0x140` (tick), `0x3DE437`/`0x3DE485` (streamers), `0x3DE4E8`/`0x3DE532`
  (scrollers), `0x3DE57C`/`0x3DE5A4`/`0x3DE807`/`0x3DE82C` (oscillators) + `0x3DE64F`/`0x3DE8D7` (appliers).
- Script table: ROM `$FDFE00..` (headers+frame-lists), tile source ROM `$F6xxxx`.
