# BATTLE UI SFX — the two posters, their 30 sites, and why our HUD is silent

Derives the complete battle command-menu / HUD sound surface. User report (translated from Portuguese): *"the battle hud's sound does not
play"* (the battle HUD/command-menu sound never plays).

Every claim is anchored to a ROM address (`python tools/dis.py <fileoffset>`, fileoffset =
`(bank-0xC0)<<16 | addr`) **and/or** an oracle observation. Claims pinned by only one are labelled
**HYPOTHESIS**. Writer PCs from `--writer` are the PC **after** the store retires. States used:
`saves/battle_re/forest_battle_3hetake.state`, `forest_holdleft_pre.state`, `bat3marle.state`.
Probe: a copy of `build/ct.exe`.

> **HEADLINE.** The whole battle-UI sound surface is **exactly two posters in bank C1**, both
> `cmd = $19`, `pan = $80`, neither touching `$1E03`:
> **`C1/1B41`** (`LDA #42 / STA $1E01 / LDA #19 / STA $1E00 / LDA #80 / STA $1E02 / JSL C7/0004`) = the
> **PC-BECOMES-READY chime**, and **`C1/1B55`** (`STZ $1E01 / ...`) = the **generic UI cursor blip**
> (id `$00`). They are **not peers**. `C1/1B41` is the fall-through TAIL of the ready-ENQUEUE routine
> `C1/1B19` — it has **zero JSR/JMP/JSL entries anywhere in the 4 MB image** and is reached only through
> the battle dispatcher `C1/0003 -> C1/0045 -> JSR ($0051,X)` with **A = 1**, from the single site
> `C1/856D`, on the RISING EDGE of the per-PC latch `$93EE + 7*pc`. `C1/1B55` has **29 `JSR` sites** —
> every menu button edge (5 command level, 4 tech submenu, 6 item submenu, 4 target-select, 10 in the six
> per-mode target handlers). 29 + 1 = the 30 sites `battle_sfx_ops.md` counts.
> **Neither is gated on the `$A013` action freeze.** Measured on the oracle:
> `f21 001E01=00*(C1:1B58) 0095DC=01*(C1:126B) 00A013=00` (blip, un-frozen) and
> `f2 001E01=42(C1:1B46) 00A6DE=01 00A013=00` (chime, un-frozen).
> **Our port posts BOTH sounds only inside `battleMenuFrozen()` (`src/battle.c:2095` and `:2106`), whose
> ONE call site is `if (wasFrozen) { ...; return 0; }` at `src/battle.c:6728`** with
> `int wasFrozen = s[0xA013];` at `src/battle.c:6629`. No action freeze in flight => no battle UI sound at
> all. That is the user's defect, and it is **structural, not a missing constant.**

---

## RAM MAP

| Addr | Meaning | Writer(s) observed |
|---|---|---|
| `$7E:1E00` | SFX command (`$19` = panned SFX) | `C1:1B46` / `C1:1B58` (and `C7:0196` on the driver's exit clear) |
| `$7E:1E01` | SFX id | `C1:1B46` (id `$42`), `C1:1B58` (id `$00`) |
| `$7E:1E02` | pan (`$80` = centre) | `C1:1B50` / `C1:1B62` |
| `$7E:1E03` | **never written by either poster** | — |
| `$7E:1E05` | driver busy byte; `C7/0157 LDA $05 / BPL` DROPS every command while a song upload is in flight | `$E0` (idle) on the normative state |
| `$7E:93EE + 7*pc` | per-PC turn latch; the chime's rising-edge gate. `0x81` after a chime (`ORA #80` at `C1/1B29`) | `C1:1B2E` |
| `$7E:95D6..$95D9` | 4-slot pending-enqueue FIFO | `C1:1B41` (`STA $95D6,X` @ `C1/1B34`) |
| `$7E:95DA` | FIFO count; `INC` at `C1/1B3E` is the frame the chime posts | `C1:1B41` (inc), `C1:1B9D` (dec) |
| `$7E:95DB` | submenu state: 0 command, 1 tech, 2 item | `C1:12E4` / `C1:131D` (open), `C1:1395` / `C1:14E1` (cancel) |
| `$7E:95DC + pc` | the Att/Tech/Item cursor row | `C1:126B` (Down/INC), `C1:1257` (Up/DEC), `C1:1275` / `C1:125E` (wrap) |
| `$7E:95D5` | the current chooser (queue head) | `C1:058D` |
| `$7E:9609` | target-select active | `C1:12B9` (enter), `C1:1580` (cancel), `C1:1629` (confirm) |
| `$7E:9614` | selected target slot | `C1:176F` (INC), `C1:1789` (DEC), `C1:157A` (clear) |
| `$7E:A6D9 + seat` | per-seat READY flag: seat index when enqueued, `0xFF` when not | `C1:1B83` (in), `C1:1BE3` (out) |
| `$7E:A6DD` | head cursor (which enqueued PC is chooser) | `C1:11ED` (DEC), `C1:1224` (INC), `C1:122E` (wrap) |
| `$7E:A6DE` | occupancy count; **the L/R blip's gate** | `C1:1BA0` (inc), `C1:1BD6` (dec) |
| `$7E:960C` | b7 set => the generic target nav (and its blip) is SKIPPED | `C1/27EA` (`LDA #80`), `C1/1F83` (`STZ`) |
| `$7E:00EE` / `$7E:00EF` | the battle's per-frame button EDGE pair (low / high) | `CD:038B` (build), `C1:1B18` / `C1:10DC` / `C1:179C` (clear) |
| `$7E:A013` | the ACTION freeze | `C1:40A9` (set, `INC $A013` @ `C1/40A6`), `C1:41BD` (clear) |

---

## PART 1 — THE TWO POSTERS (ROM_DERIVED, re-disassembled for this dossier)

`python tools/dis.py 11B19 0x50 1 0`:
```
11B19: LDA $A1 / 11B1B: TAX
11B1C: LDA $A6D9,X / 11B1F: BPL ->11B54        <- BAIL if the seat is ALREADY enqueued (no chime)
11B21: LDA $CCFAF0,X / 11B25: TAX             <- pc -> pc*7 via the ROM table CC/FAF0 = 00 07 0E ...
11B26: LDA $93EE,X / 11B29: ORA #80 / 11B2B: STA $93EE,X
11B2E: LDA $95DA / 11B31: TAX
11B32: LDA $A1  / 11B34: STA $95D6,X          <- push the seat into the pending FIFO
11B37: TAX / 11B38: LDA $9916,X / 11B3B: STA $95DC,X   <- seed that seat's cursor row
11B3E: INC $95DA                              <- FIFO count++   *** the chime frame ***
11B41: LDA #42 / 11B43: STA $1E01
11B46: LDA #19 / 11B48: STA $1E00
11B4B: LDA #80 / 11B4D: STA $1E02
11B50: JSL C70004 / 11B54: RTS
11B55: STZ $1E01                              <- id 00 (a STZ, not a literal)
11B58: LDA #19 / 11B5A: STA $1E00
11B5D: LDA #80 / 11B5F: STA $1E02
11B62: JSL C70004 / 11B66: RTS
11B67: LDA $95D6 ...                          <- the POP / activation routine, SILENT (see 1.4)
```
Raw bytes (`--read "rom:C1/1B41+19"` / `"rom:C1/1B55+18"`):
`A9 42 8D 01 1E A9 19 8D 00 1E A9 80 8D 02 1E 22 04 00 C7` and
`9C 01 1E A9 19 8D 00 1E A9 80 8D 02 1E 22 04 00 C7 60`.

**`C1/1B41` has no entry of its own.** `C1/1B18` is `RTS`, so `C1/1B19` is a clean subroutine entry and
`C1/1B41` is fall-through code. Whole-image scans: `20 41 1B` -> 1 hit (`DD/9576`, graphics data, not
bank-C1 code); `4C 41 1B` -> 0; `22 41 1B C1` / `5C 41 1B C1` -> 0. The raw word `$1B41` appears nowhere
in bank C1. **A port CANNOT model the chime as an independently-callable "window opened" hook** — it is
the tail of the enqueue.

### 1.1 The chime's ONE call path
```
python tools/dis.py 18540 0x40 1 0:
18546: LDA $14 / ASL A / ASL A / ASL A / SEC / SBC $14 / 1854E: TAX      (X = pc*7)
1854F: LDA $93EE,X / 18552: BNE ->18570                                 <- THE RISING-EDGE GATE
18555: LDA $2989 / 18558: BIT #20 / 1855A: BEQ ->18562
1855C: JSR C1/B70E / 1855F: JMP C1/8588                                 <- $2989 b5 set: NO chime
18562: LDA #01 / 18564: STA $93EE,X                                     <- latch the turn
18567: LDA $14 / 18569: STA $A1                                         <- $A1 = the seat index
1856B: LDA #01 / 1856D: JSR C1/0003                                     <- dispatcher service #1
```
`C1/0003` -> `JMP C1/0045` -> `PHA/PHX/PHY / ASL A / TAX / JSR ($0051,X)`. Table at `C1/0051`:
`0023 1B19 1BAA 106E 4058 2986 006D 1FDD 356D 354D` -> **entry [1] = `$1B19`**.
Of the 27 `JSR C1/0003` sites plus the 2 direct `JSR C1/0045` sites in bank C1, **exactly one loads
A = #01, and it is `C1/856D`** (prev-16-byte dump of all 27; the other indices are 0/2/3/4/5/6/7/8/9).
Empirical corroboration: over 640 monitored frames only TWO `$1E01 = 42` writes occurred, and both were
immediately preceded by `C1:856B` writing `$A1`.

Stride 7 is corroborated **from the ROM**, not only from RAM: the table at `CC/FAF0` reads
`00 07 0E ...` (pc0 -> 0, pc1 -> 7, pc2 -> 14). Oracle: `7E93EE = 8100000000FF008100...` — offsets 0 and
7 both `0x81`.

### 1.2 The 29 blip sites
Whole-image scan for `20 55 1B` (`JSR $1B55`) -> exactly 29, all in bank C1
(`4C 55 1B` -> 0; `22 55 1B C1` -> 0):

| Menu | Sites | Gate | Handler |
|---|---|---|---|
| Command level (`C1/118F`, `$95DB == 0`) | `C1/11B7` | `$EF & 08` Up | `C1/1250` cursor DEC (wrap 0->2) |
| | `C1/11C3` | `$EF & 04` Down | `C1/1264` cursor INC (wrap 3->0) |
| | `C1/11D8` | `$EE & 80` A | `C1/127B` CONFIRM |
| | `C1/11E7` | **`LDA $A6DE / DEC A / BEQ ->111EA`** | `C1/11EA` prev READY PC |
| | `C1/121E` | **`LDA $A6DE / DEC A / BEQ ->11221`** | `C1/1221` next READY PC |
| Tech submenu (`C1/1320`, `$95DB == 1`) | `C1/1334` | `$EE & 80` A | `C1/1369` confirm tech |
| | `C1/1340` | `$EE & 08` B | `C1/138F` cancel |
| | `C1/1350` | `$EF` Up or Left | `C1/13AD` prev |
| | `C1/1360` | `$EF` Down or Right | `C1/13F4` next |
| Item submenu (`C1/143D`, `$95DB >= 2`) | `C1/1443` | `$EE & 80` A | `C1/1498` confirm |
| | `C1/144F` | `$EE & 08` B | `C1/14DB` cancel |
| | `C1/145F` | Up / Left | `C1/14EC` prev |
| | `C1/146F` | Down / Right | `C1/1502` next |
| | `C1/147B` | `$EE & 20` L | `C1/151C` PAGE UP |
| | `C1/1487` | `$EE & 10` R | `C1/1537` PAGE DOWN |
| Target select (`C1/1561`, `$9609 != 0`) | `C1/1574` | `$EE & 08` B | CANCEL |
| | `C1/15EE` | `$EE & C0` A-or-X | `C1/161A` CONFIRM |
| | `C1/1602` | `$EF & 05` Right/Down | `C1/176C` next |
| | `C1/1611` | `$EF & 0A` Left/Up | `C1/1786` prev |
| Per-mode target handlers via `JSR ($1FF8,X)` | `C1/217A`+`C1/2188` (mode `$0B`) | `$EF & 09` / `$EF & 06` | `C1/27FA` |
| | `C1/21C0`+`C1/21CE` (mode `$0D`) | same | `C1/27FA` |
| | `C1/2214`+`C1/2222` (mode `$0C`) | same | `C1/27FA` |
| | `C1/226B`+`C1/2279` (modes `$12`/`$1A`) | same | `C1/27FA` / `C1/2814` |
| | `C1/2301`+`C1/230F` (mode `$0F`) | same | `C1/27FA` / `C1/2814` |

Mode table dump at `C1/1FF8` (33 words): `$0B -> $2169`, `$0C -> $2203`, `$0D -> $21AF`, `$0F -> $22F5`,
`$12 -> $225F`, `$1A -> $225F`. Dispatch at `C1/115F`: `LDA $9609 / BEQ ->11167 / JMP C1/1561` then
`LDA $95DB / BEQ ->11184 / DEC A / BNE ->1117B / JMP C1/1320` / `JMP C1/143D`.

### 1.3 The five gates on the blip (all ROM_DERIVED, four oracle-confirmed)
- **G1 — the L/R ready switch is SILENT with one PC enqueued.** `C1/11E1` and `C1/1218` are
  `LDA $A6DE / DEC A / BEQ ->skip`, and the `BEQ` skips ONLY the `JSR C1/1B55`; the `$A6DD` head still
  cycles. Measured both ways: with `$A6DE == 1`, Right at f375 and Left at f399 each produced an `$A6DD`
  writer (`C1:122E` / `C1:11ED`) and **no `$1E01` post**; with `$A6DE == 2` the same presses posted at
  f421 / f442 / f462.
- **G2 — the chime is skipped when `$2989` bit5 is set** (`C1/8555 BIT #20` -> `JSR C1/B70E` /
  `JMP C1/8588`). `$2989 = 0x00` on the normative state, so this branch was **never exercised**.
- **G3 — the chime's own re-entry guard**: `C1/1B1C LDA $A6D9,X / BPL ->1B54`. The POP routine writes
  `STA $A6D9,X` with the seat index (>= 0), closing the gate until `C1/1BE1 LDA #FF / STA $A6D9,X`
  (inside `C1/1BAA`) reopens it.
- **G4 — the generic target-cursor nav is skipped when `$960C` b7 is set** (`C1/15F4 LDA $960C / BMI
  ->11617`). Every per-mode handler ends `JMP C1/27E8` = `LDA #80 / STA $960C`. That is why the modes that
  do their own nav never double-post: **exactly one blip per press either way**. `$960C` is re-zeroed at
  `C1/1F83` on every rebuild.
- **G5 — an EMPTY eligible-target set auto-cancels SILENTLY**: `C1/1564 LDA $9613 / BPL ->1156E`; the
  negative branch (`STZ $9614 / BRA ->11577`) lands PAST the `JSR C1/1B55` at `C1/1574`.

**G6 — the blip is posted BEFORE the veto.** Every blip site is `JSR C1/1B55` followed by `JMP <handler>`,
and the handler's own gate is checked afterwards: `C1/11D8` blips then `JMP C1/127B`, where `C1/12BC`
tests `LDA $A0A7 / AND #01 / BNE ->112EA` (Tech) and `C1/12ED` tests `LDA $A0A7 / BNE ->1131D` (Item);
`C1/1334` blips then `C1/1369` tests `LDA $A099 / BEQ` and `LDA $9EE5 / BMI`. **A port must post first
and gate second.** Oracle corroboration: with no monitored byte changing at all, the ROM still blipped at
f129 / f173 (tech submenu nav) and f153 / f197 (item submenu nav).

### 1.4 The window-OPEN moment is a SEPARATE, SILENT event
`C1/1B67` (the FIFO pop / activation) is called from `C1/10CE`, `C1/10ED` and `C1/1104`, each gated
`LDA $993A / BEQ` + `STZ $993A`. Its body sets `$A6D9,X = seat`, `$A6DF = #FE`, shifts `$95D7 -> $95D6`,
`DEC $95DA`, `INC $A6DE`. Scanning `C1/1B67..C1/1BA9` for `8D 01 1E` / `9C 01 1E` / `8D 00 1E` /
`22 04 00 C7` returns **zero hits**. `C1/1BAA` (dispatcher index 2, the ready DE-queue) is likewise
silent (`C1/1BD6 DEC $A6DE`, `C1/1BE1 LDA #FF / STA $A6D9,X`, no `$1E0x` store in `C1/1BAA..1C33`).

**Timing correction from the render side (a verifier caught this).** The original derivation said "the
window opens 2 frames later when `C1/1B67` pops the queue". The POP is indeed +2 (`f327` chime ->
`f329 7E95D6=FFFFFFFF0000*(C1:1B9D)`, `f329 7EA6DD=0001*(C1:1BA9)`) — but that is a RAM event, not a
draw. Rendered `--peek` frames: f327 no window, **f329 NO window**, f330 NO window, **f331 window VISIBLE
("Att./Tech/Item")**. So the command window first RENDERS **4 frames** after the chime. This widens the
gap and therefore *reinforces* the headline: **the chime is not the window-draw sound.** Pin it to the
`$93EE` rising edge / the `$95DA` increment, never to a window-open frame.

---

## PART 2 — ORACLE: BOTH SOUNDS FIRE UN-FROZEN, AND BOTH ARE AUDIBLE

### 2.1 The chime, `$A013 == 0`
```
ct_probe.exe --monitor "7E1E00:1,7E1E01:1,7E1E02:1,7E95D5:1,7E95DA:1,7E95DB:1,7E95DC:1,7E9609:1,\
7E9614:1,7EA6DD:1,7EA013:1,7EA6D9:4" --from saves/battle_re/forest_holdleft_pre.state \
  --btnscript "40:240,0:400" --frames 640 --writer
```
```
f327  7E1E01=42*(C1:1B46) 7E1E02=80(C1:1B50) 7E95D5=FF 7E95DA=01*(C1:1B41) 7EA013=00 7EA6D9=FFFFFFFF
f329  7E95D5=00*(C1:058D) 7E95DA=00*(C1:1B9D) 7EA6DD=00*(C1:1BA9) 7EA6D9=00FFFFFF*(C1:1B83) 7EA013=00
f402  7E1E01=42(C1:1B46)  7E95D5=00 7E95DA=01*(C1:1B41) 7EA013=00     <- SECOND PC, window already open
f451  7EA013=01*(C1:40A9)                                             <- the battle's FIRST freeze
```
Two independent facts here: (a) the chime fires with `$A013 = 00`, **124 frames before the first freeze
of the battle** — our port misses the very first chime of every fight, not just an edge case; (b) at
`f402` the ROM chimes **again** for the second PC while Crono's command window is already open
(`$95D5 = 00` throughout) — a window-RISING-EDGE model structurally cannot produce this.

### 2.2 The blip, `$A013 == 0` (reproduced fresh for this dossier)
```
python -c "print(','.join(['0:20,20:2,0:18']*8))" > sc2.txt
ct_probe.exe --monitor "1E01:1,1E00:1,1E02:1,A013:1,95DC:1,A6DE:1,9609:1" \
  --from saves/battle_re/forest_battle_3hetake.state --input "$(cat sc2.txt)" --frames 340 --writer
```
```
f2    001E01=42(C1:1B46)  001E02=80(C1:1B50)  00A013=00  0095DC=00  00A6DE=01   <- chime, un-frozen
f21   001E01=00*(C1:1B58) 001E02=80(C1:1B62)  00A013=00  0095DC=01*(C1:126B)    <- BLIP, UN-FROZEN
f61   001E01=00(C1:1B58)                      00A013=01  0095DC=02*(C1:126B)
f101  001E01=00(C1:1B58)                      00A013=01  0095DC=00*(C1:1275)
f141/f181/f221/f264/f304  ... same, A013=01
```
The first blip of the run is at `$A013 = 00`, **30 frames before** the first freeze. Third independent
un-frozen blip: item-submenu CANCEL on the same state, `f242 7E1E01=00*(C1:1B58) 7E95DB=00*(C1:14E1)
7EA013=00`.

### 2.3 Did sound actually come OUT (the mailbox-vs-DSP trap)
`--aram` ignores `--btnscript`, so the working pattern is `PEEK_SAVE=<f>.state ... --peek ...` then
`--aram --from <f>.state --frames 0|1`.
- **Chime (id `$42`):** pre `v4 srcn=21 gain=07F7 SOUNDING`, `v5 srcn=23 SOUNDING` (music); two frames
  after the post both are key-released (`gain=0000 adsrState=3`); at +6 both are re-keyed
  `v4 srcn=05 pitch=354C gain=07F7 decodeOff=$4817 <-- SOUNDING` and
  `v5 srcn=05 pitch=3504 gain=07F7 decodeOff=$4820 <-- SOUNDING`, falling to 2394/2364 by +10 — a
  detuned **two-voice descending chime on sample `srcn=05`**. Reproduced on `bat3marle.state`.
- **Blip (id `$00`):** A/B differential at the SAME frame (f26) with and without the Down press whose
  blip lands at f21. With: `v5 srcn=02 pitch=3534 gain=07F7 decodeOff=$47A2 <-- SOUNDING`. Without:
  `v5 srcn=23 pitch=16A0 SOUNDING` (still the music). **One voice, sample `srcn=02`.**
  (A verifier trimmed an overstatement here: "every other voice is byte-identical" is false — `decodeOff`
  differs because the two states have 6 frames of different history. The load-bearing part is
  `srcn` + `pitch`, which do match on v0/v1/v2/v3/v6/v7.)

### 2.4 The measured button decode
`$EE` (low) / `$EF` (high) are the battle's per-frame EDGE pair, built by `CD/0340-0398` from the
REMAPPED joypad word `$00F8` (`C2/85AB`), with auto-repeat: 15-frame initial delay in `$CC9B`, then
~every 4 frames (`0x18` when bit15 of the latch is set). `LDA $CC97 / ORA $EE / STA $EE` is a 16-bit
store, so `$EE` = low / `$EF` = high. The latch is consumed by `C1/179C STZ $EE / STZ $EF`, the tail
every menu handler jumps to. **Consequence for the port: holding a direction REPEATS the blip.** Measured
holding Down 90 frames: posts at f21, then f36 (+15), then f41/f46/f48/f53/f57/f61/... roughly every 4.

Measured map (one LakeSnes serial bit at a time, `--monitor "7E00EE:2" --writer`):
`A(0x100) -> $EE 0x80`, `X(0x200) -> 0x40`, `L(0x400) -> 0x20`, `R(0x800) -> 0x10`, `B(0x001) -> 0x0A`,
`Y(0x002) -> 0x01`, `Select(0x004) -> 0x04`, `Start(0x008) -> 0x00` (unmapped);
`Up(0x10) -> $EF 0x08`, `Down(0x20) -> 0x04`, `Left(0x40) -> 0x02`, `Right(0x80) -> 0x01`.

### 2.5 Boundary — nothing else in bank C1 makes a UI sound
Whole-image scan for `22 04 00 C7` (`JSL C7/0004`) -> 59 sites, of which **bank C1 has exactly 6**:
`C1/1B50` (id 42 UI), `C1/1B62` (id 00 UI), `C1/40DD` (act-end id `$FF`, cmd `$18`, **no pan store**),
`C1/61B5` (anim ops `0x78/0x79`), `C1/61DF` (`0x7A/0x7B`), `C1/620F` (`0x7C/0x7D`).
Companion scans: `8D 01 1E` in bank C1 = `C1/1B43`, `C1/40D5`, `C1/61A1`, `C1/61C4`, `C1/61FD`;
`9C 01 1E` in bank C1 = `C1/1B55` only. **There is no separate confirm id, no separate cancel id, no
buzz/refusal id in bank C1.** Over a 795-frame drive the complete `$1E01` writer set was
`{C0:3C21, C7:0A53, CD:3B7D, C1:1B50, C1:1B62, C1:61B5, C1:40D8, CD:3E51}`.
**Scope this verdict honestly:** it is verified for bank C1 only. Banks CC/CD host other battle code and
do carry their own `JSL C7/0004` sites (`CD:03AB`, `CD:0D70`, `CD:0D81`, `CD:3B7D`, `CD:3E54`,
`CD:3E70`); those were not audited. Say *"not present in bank C1"*, not *"absent from battle"*.
`CD/3E44` in particular is **not** part of this family: it is a driver-level raw `$F5` on/off pair gated
`LDA $CD02 / BEQ`, not a `cmd $19` SFX.

### 2.6 Global drop condition inherited by both posters
`C7/0157 LDA $05 (= $1E05) / BPL ->0192` discards EVERY command while a song upload is in flight, and
`C7/0192 STZ $00` clears `$1E00` on every exit including the dropped path. Normative battle state reads
`$1E05 = $E0` (idle/ready).

---

## PART 3 — WHAT OUR ENGINE DOES

### D1 — both posts are unreachable un-frozen (THE defect)
```
src/battle.c:6629   int wasFrozen = s[0xA013];
src/battle.c:6728   if (wasFrozen) { battleMenuFrozen(h, aP, bP, upE, dnE, upH, dnH, lP, rP); return 0; }
src/battle.c:2077   static void battleMenuFrozen(...)          <- the ONLY function with either post
src/battle.c:2095     if (winOpen && !h->cmdWinOpen && (battleSfxPost(h, 0x19, 0x42, 0x80) || !gBattleSndOn))
src/battle.c:2106       if (battleSfxPost(h, 0x19, 0x00, 0x80) || !gBattleSndOn)
```
`grep -n battleMenuFrozen src/battle.c` -> definition 2077, **one call site 6728**. Step F (the ordinary
un-frozen menu, `src/battle.c:6887-6958`) contains **zero** `battleSfxPost` — it only traces
(`"ready-switch"` 6902, `"cursor"` 6915, `"target-select"` 6929, `"tech-open"` 6935, `"item-open"` 6939,
`"target-nav"` 6943, `"target-cancel"` 6953). It does not even track `h->cmdWinOpen`, which is written
only at `src/battle.c:2080/2085/2097` and reset at `:6413`.

**MEASURED consequence** (audio sink live, `[audio] SPC700 up: driver in ARAM, 48000 Hz, 800
samples/frame`):
```
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy CT_AUDIO_FORCE=1 \
CT_BATTLE_SCRIPT="n:600,d:1,n:20,a:1,n:20,d:1,n:20,b:1,n:20,d:1,n:20,a:1,n:20,d:1,n:20,b:1,n:600" \
CT_BATTLE_TRACE=1 CT_BATTLE_SFXVOICES=1 \
build/Belthasar.exe --assets ct_assets.dat --scene 19 --battletest saves/battle_re/forest_pre.raw.wram
```
Six UI transitions in the fully un-frozen window f715..f999 — cursor f762, tech-open f782, tech-cancel
f824, cursor f844, item-open f867, item-cancel f907, **every one `A013=0`** — and **zero** sound posts;
the next `battlesfx` line in the whole file is `f1053 rel=51 op78`.
A complete GATO fight under `CT_BATTLE_MASHA=1` to victory: `grep -c cmd-window` = **0**,
`grep -c cursor-blip` = **0**, across at least four PC command-window openings. Same on the REAL
interactive path (`--scene 130 --loadwram saves/battle_re/party3_field.lakewram --walkscript l:150`):
0 chimes, 0 blips, while 11 `battlesfxout` lines fire for combat/lifecycle SFX. **The wire is fine; the
posts are not there.**

### D2 — the one chime we do have is MIS-EDGED
`h->cmdWinOpen` is sampled only inside `battleMenuFrozen`, so during un-frozen play the edge detector is
never read and the rising edge is deferred to the first frozen frame after the window opened. Measured:
`battletrace f248 enemy-think A013=0` (window open, two PCs enqueued, no chime) ->
`battletrace f251 enemy-arrive A013=1` -> `battlesfx f252 cmd-window id=42 pan=80 (C1/1B41)`. The chime
lands on the second frame of the **ENEMY's** action freeze. Same on forest: `f293 enemy-arrive A013=1` ->
`battlesfx f294 cmd-window`.

Worse, the predicate itself is wrong. Ours is a rising edge of
`(!techOpen && !itemOpen && $9609 == 0)`. The ROM's chime is the **enqueue** (`C1/1B19`). Cancelling back
from a submenu or from target-select re-opens the window but the ROM posts the **BLIP** (id `$00`), not
the chime — measured on all three cancel paths:
target-select `f217 id=00 ... 9609=00*(C1:1580)`, tech `f217 id=00 ... 95DB=00*(C1:1395)`,
item `f242 id=00 ... 95DB=00*(C1:14E1)`. Our rising-edge model would post a `$42` on every one of them —
**a sound the ROM never makes there.**

### D3 — the blip's gate is a state-change test, not a button edge
`src/battle.c:2105` posts only `if (s[0x95DC] != oldCur)`. The ROM posts on the BUTTON EDGE, before the
handler, and blips even when the handler no-ops (measured f129/f173 tech nav, f153/f197 item nav — no
monitored byte moved). Our gate is also the wrong SHAPE and covers only **one** of the seven blip
contexts (of the 20 UI transitions in our engine, 18 are silent even inside the freeze — the tech and
item submenus and target-select post nothing on either path).

### D4 — no check grades battle sound on OUTPUT
`--battletest` forces `selftest = 1` (`src/field.c:6258`), and the audio block is gated
`if (audioOn && (!selftest || getenv("CT_AUDIO_FORCE")) && ...)` (`src/field.c:6841`), so the sink
(`src/field.c:6860-6862`, the tree's ONLY `CtBattleSetSoundSink` call) is OFF for every battle regress
check. With `gBattleSndOn == 0`, `battleSfxPost` returns 0 before writing anything (`src/battle.c:693`),
yet the trace line still prints via the `|| !gBattleSndOn` escape hatch. **The 10 battle-SFX checks grade
the TRACE, never the sound.** Measured: `field-battle-menu-sfx`'s exact argv+env produces 17 `battlesfx`
lines and **zero** `battlesfxout` lines even with `CT_BATTLE_SFXVOICES=1`; adding `CT_AUDIO_FORCE=1`
produces 12. By contrast all 20+ field/naming/menu SFX checks DO set `CT_AUDIO_FORCE=1` +
`SDL_AUDIODRIVER=dummy`.

Two further blind spots in the same wire:
- `battleSfxPostNoPan` (`src/battle.c:710-716`) has **no** `CT_BATTLE_SFXVOICES` block, so the act-end
  `$FF` post can never be graded on output.
- **The `voices N -> N` probe never registers a delta.** In every run `v0 == v1` for ALL posts (11/11 on
  the interactive path, including combat SFX that certainly sound), because it samples immediately around
  the post, before the SPC driver has run. `battlesfxout` proves the SINK IS WIRED (the line only prints
  past `if (!gBattleSndOn) return 0;`) — it does **not** grade audibility. The comment at
  `src/field.c:6861` ("so a battle SFX can be graded on OUTPUT") overstates it.

---

## PART 4 — TRAPS

**TRAP 1 — `--monitor --diff` hides a repeated identical post.** `$1E01` posted `00` again while it is
already `00` prints nothing. Drop `--diff` and grep the `(bank:pc)` writer field, which appears only on
frames the region was actually written. Two early runs looked like "the blip is missing" purely because
of this.

**TRAP 2 — a long `--btnscript` through a Git-Bash `$(cat file)` variable was silently mangled**
(`${#SC}` read 1759 for a 1552-byte script) and the battle never started, producing a clean-looking but
entirely false "the ready queue never changes". Build long drive scripts inside the script that runs the
probe.

**TRAP 3 — `--aram` IGNORES `--btnscript`** (it advances on `--frames` with no input), so it cannot drive
to an event. Use `PEEK_SAVE` + `--peek` to capture the frame, then `--aram --from <that state> --frames 0`.

**TRAP 4 — a mis-aligned `dis.py` start address silently produces garbage GATE conditions.**
`dis.py 11B30 60` prints `11B30: STA $AA,X` and drops the real `11B2E: LDA $95DA / 11B31: TAX`; the same
misalignment turned `C1/1443`'s gate into `INC $8029 / BEQ` and `C1/1574`'s into `ORA ($96,S),Y`.
Disassemble from the routine HEAD (`dis.py 11B19`, `dis.py 1142C`, `dis.py 11561`). The site addresses
survived (they were cross-checked by `ct.py refs` and a raw `20 55 1B` scan) but the conditions did not.

**TRAP 5 — a fabricated frame citation.** One derivation cited "at f141 A was pressed on an empty tech
list and it still blipped". Under the exact script quoted, presses land at f20/f64/f128/f172/f216 and the
`$1E01` stores are f2/f21/f65/f129/f173/f217 — **f141 is a flat row**, and that script never presses A
with the tech submenu open. The POINT survives on other evidence (blip-with-nothing-happening is
oracle-confirmed at f129/f173/f153/f197; blip-before-the-veto is ROM-derived from `C1/11334 JSR C1/1B55`
preceding `JMP C1/1369`'s `LDA $A099 / BEQ`), but **"A on an empty tech list still blips" is a
HYPOTHESIS**, not a measurement.

**TRAP 6 — `C1/1BAA`'s call-site count.** One derivation listed 5 sites (`C1/B43B`, `B7F7`, `B800`,
`B809`, `B963` — the dispatcher-index-2 callers). An in-bank scan also finds three DIRECT calls:
`C1/358B`, `C1/3593`, `C1/359B` (inside the victory stager). Total **8**, not 5. The substantive half
("silent") holds.

**TRAP 7 — quoted "OUT:" blocks that are hand-reformatted digests.** One derivation presented rows as
`f327 SND cmd=00(C7:0196) id=42(C1:1B46) pan=80(C1:1B50) | ctx ...`. Real `--monitor` output is
`f327  7E1E00=00(C7:0196)  7E1E01=42*(C1:1B46)  7E1E02=80(C1:1B50)  ...`. Every value checked out, but
the lines do not appear verbatim. Paste raw output.

---

## Open questions

1. **The id -> sample mapping is NOT derivable from the 65816 side.** `C7/061C` forwards `$1E00-$1E03`
   verbatim to `$2140-$2143`; what the Akao driver renders for `cmd $19 id $42` vs `id $00` is SPC700
   program logic. The `srcn=05` (two-voice chime) / `srcn=02` (one-voice blip) values in 2.3 are the
   observed CONSEQUENCE, not a derived mapping. If the port drives a real SPC core this is moot; if it
   ever synthesises, the sample ids need an ARAM-side derivation.
2. **`$2989` bit5 (the chime-suppressing branch at `C1/8555`) was never exercised** — it read 0 in every
   state used. 16 sites in bank C1 test that byte. Report as *could not reproduce under
   forest_holdleft_pre / forest_battle_3hetake*, never as "never happens".
3. **The 10 per-mode target-handler blip sites (`C1/217A`..`C1/230F`) are ROM-derived only.** No capture
   in `saves/battle_re/` reaches those `$960D` modes; the forest fight only exercises mode `$07` (plain
   physical attack, set at `C1/12A8`). A multi-target tech capture is needed to grade them.
4. **`C1/15EE` (A-or-X CONFIRM target) was not measured** — every drive script cancelled out of
   target-select with B. Disassembly-only.
5. **`C1/119E` (an X-button MUTE shortcut into the confirm handler, gated on `$A0D4`) and `C1/118A`
   (`LDA $A862 / BNE`) were never taken.** Both reach `C1/127B` by `JMP`, bypassing the `JSR C1/1B55` at
   `C1/11D8`. Disassembly-only silent paths.
6. **Whether the chime ever fires for an ENEMY becoming ready.** `C1/1B19` is dispatched per-combatant
   with `$A1` as the index, and its `LDA $A6D9,X` gate is seat-indexed 0..3, which suggests PC-only —
   **HYPOTHESIS**; no enemy was forced through it.
7. **The auto-repeat cadence (15-frame initial delay, then ~4, with one 2-frame gap at f46->f48) is
   measured but not derived** — the exact `$CC9B` reload path for the steady-state repeat was not traced.
8. **A 3-PC party was never in the queue** (`$A6DE` never exceeded 2). The disassembly says the L/R gate
   is `DEC A / BEQ` (i.e. exactly 1), so `$A6DE > 2` should behave identically — untested.
9. **`gato_wait / gato_charm / gato_deadmarle / gato_elixir / gato_wait_tech / atb_menu / atb_sel` could
   not be advanced at all** under `--btnscript "0:600"` (every watched byte frozen, gato_* reading `0x80`
   across `$95D5/$95DA/$95DB/$95DC/$A6DD/$A6D9`). WHY was not determined. Re-capture via the
   `capture-oracle-state` skill before any future work relies on them.
   Likewise `saves/battle_re/gato_lowhp.state` **does not exist in this tree**.
10. **The battle "Wait" submenu (`$99E0`) and the RUN-AWAY path were never entered.** Bank C1 has only
    the two UI posters, so any sound there would have to come from bank CC/CD — untested (see 2.5).

---

## PART 5 — PORTED (this session)

The fix is **structural**, as PART 3 D1 says it must be: the ROM has ONE menu machine and our engine has
two passes (`battleMenuFrozen`, whose single call site is `if (wasFrozen)`, and step F's live pass). The
sound now lives with the DISPATCH, which both passes call, instead of in one caller.

`src/battle.c`
- **`battleUiBlipDispatch(h, aE, bE, upE, dnE, lE, rE, pass)`** = `C1/115F`. It reproduces the ROM's
  dispatch ORDER (target-select `$9609` first even with a submenu open, then tech `C1/1320`, then item
  `C1/143D`, then the command level `C1/118F`) and posts `C1/1B55` on the BUTTON EDGE, before the handler
  -- so a no-op nav still blips (G6). Called from `battleMenuFrozen` **and** from step F. Sites named in
  the trace: `11B7` Up, `11C3` Down, `11D8` A, `11E7`/`121E` the L/R ready switch, `1334`/`1340`/`1350`/
  `1360` tech, `1443`/`144F`/`145F`/`146F` item, `1574`/`15EE`/`1602`/`1611` target-select.
  G1 is ported literally: `site = (s[0xA6DE] > 1) ? ... : 0`.
- **The chime moved to the ENQUEUE** (`battleEnqueue(s, cmb); h->pcInTurn[cmb] = 1;`), which is
  `C1/8562-856D` -> dispatcher `[1]` -> `C1/1B19` -> `INC $95DA` falling through into `C1/1B41`. The
  enclosing `!battleQueued && !h->pcInTurn` is `C1/854F`'s `$93EE` rising edge and `battleEnqueue`'s own
  `$A6D9,X != 0xFF -> return` is `C1/1B1C`'s bail, so no new gate was invented.
- **`h->cmdWinOpen` is DELETED** (field + all writes). It modelled a "window opened" hook the ROM does not
  have, and it fired on the second frame of the ENEMY's freeze.

Measured after (forest, `CT_AUDIO_FORCE=1`): `ready-chime seat0 f178 A013=0`, `seat1 f238 A013=0`, then
blips at `C1/11C3` / `C1/11D8` / `C1/1340` / `C1/11B7` / `C1/121E`, **every one `pass=live A013=0`**, each
with a matching `battlesfxout` line -- which prints only past `if (!gBattleSndOn) return 0;`, so the post
reached the wire. Before the change: 0 chimes, 0 blips across a whole fight.

### Checks
`field-battle-menu-sfx-unfrozen` (new) pins the live pass, both G1 arms, and the sink; `field-battle-menu-sfx`
re-pinned and now grades BOTH passes (`pass=frozen` f380/f401, `pass=live` f422). 5 mutations, 5 killed.

### Two traps this port added to the record
1. **A check with an empty `expect` list asserts NOTHING.** The first `-unfrozen` check had its `pin`
   regexes written but `--pin` never run, so `expect` stayed `[]`, the check passed vacuously and its
   mutation SURVIVED. `regress.py --mutate` is what caught it.
2. **`$A013` alone cannot grade "un-frozen".** The frozen pass runs at the END of the frame, so a span that
   ends inside that frame leaves the live byte reading 0 -- a frozen-pass blip satisfies an `A013=0` regex.
   The trace now carries `pass=live|frozen`, which names the mechanism instead of a side effect.
3. **A SILENCE cannot be pinned by containment.** `regress.py` only checks that expected substrings are
   PRESENT, so dropping the G1 gate first survived. The dispatcher now prints the gate's decision
   (`ready-switch A6DE=<n> blip=<0|1>`) and the drive taps L/R on both sides of it.

### Still OPEN (recorded, not guessed)
- **Auto-repeat blips.** The ROM repeats the blip while a direction is HELD (15-frame initial delay in
  `$CC9B`, then ~every 4). Our nav repeats on its own cadence and stays silent on the repeat; the `$CC9B`
  steady-state reload was never traced, so approximating it would be invention.
- **The item submenu's L/R PAGE blips** (`C1/147B` / `C1/1487`): our input bitmask carries no shoulder
  buttons at all.
- **`$2989` bit5** (`C1/8555 BIT #20` -> `JSR C1/B70E`, no chime): read 0x02 in every fixture.
- **The 10 per-mode target-handler sites** (`C1/217A`..`C1/230F`): no fixture reaches those `$960D` modes.
- Our tech/item LIST nav ignores Left/Right where `C1/1350` / `C1/1360` accept them. That is a separate,
  pre-existing gap; the blip follows the ROM's site either way (and the ROM blips before the handler
  regardless), so the two can be fixed independently.
