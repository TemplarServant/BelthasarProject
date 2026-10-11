# BATTLE HUD — PC NAMES + LOW-HP FLASH (two missing behaviors, DERIVED)

Derives the two HUD-strip behaviors the user reports missing from our battle HUD:
**(1) the PC NAME glyphs before the HP values**, and **(2) the LOW-HP text FLASH** (HP/MP
blink white↔grey when a PC is critical). Everything below is anchored to the ROM
(`tools/dis.py`, fileoffset = (bank−0xC0)<<16 | addr) or an oracle read/peek/monitor from
`gato_press_action.state` + a spliced in-battle command state. Companion dossiers:
`battle_hud_render.md` (strip geometry/glyph/palette), `battle_loop.md` (RAM ledger, block layout).

**ROM/oracle base facts used:** PC battle block `$5E2D + seat*0x80` (`+3` curHP u16, `+5` maxHP u16,
`+7` curMP, `+9` maxMP, `+0x1D` dead-flag bit7) — `battle_loop.md`. HUD tilemap shadow base
`$7E0C80`, cell word = `$0C80 + row*0x40 + col*2` (tile lo, attr hi); attr hi 0x29 = **pal2**,
0x2D = **pal3** — `battle_hud_render.md §Q1/ADDENDUM2`. Party name buffers `$7E2C23`, 6-byte
records (5 CT-charset bytes + `$00`), char-id indexed — memory `ct-field-name-vars`.

Repro base: `cmd_inbattle.state` = `--peek gato_press_action.state … --btnscript "100:8,0:420"` with
`PEEK_SAVE` (COMMAND phase settled: `$95D5`=0 Crono active, `$9609`=0, queue `$A6D9`=[0,FF,FF,FF]).
Splice recipe: LakeSnes `.state` WRAM base = file offset **0x45**, so `state[0x45+addr]` patches any
WRAM byte (used to force low curHP).

---

## PART A — the PC NAMES

### A1. The name SOURCE is `$7E2C23` and it IS populated in battle (byte-exact)

`$7E2C23` holds the party name table as **6-byte records** (5 CT-charset glyph codes + `$00`
terminator), indexed by char-id. Read directly from WRAM (offset `$2C23`) in **every** state, field
AND in-battle:

```
$2C23: A2 CB C8 C7 C8 00 | AC BA CB C5 BE 00 | AB CE BC BC BA 00 | B1 C8 BB C8 00 00 | ...
       C  r  o  n  o  \0    M  a  r  l  e  \0    L  u  c  c  a  \0    R  o  b  o  \0 \0
```

Verified identical in: `gato_press_action.state`, `gato_wait.state`, `forest_battle_3hetake.state`
(states, +0x45) and `gato_t0/t2/t3.raw.wram` (in-battle dumps, offset 0). Full table (char-id → 6-byte
record): Crono `A2 CB C8 C7 C8 00`, Marle `AC BA CB C5 BE 00`, Lucca `AB CE BC BC BA 00`,
Robo `B1 C8 BB C8 00 00`, Frog `A5 CB C8 C0 00 00`, Ayla `A0 D2 C5 BA 00 00`,
Magus `AC BA C0 CE CC 00`, Epoch (record 7, `$2C4D`) `A4 C9 C8 BC C1 00`.

The HUD shadow proves the strip reads THIS buffer: in `cmd_inbattle.state` the party-box name row is
`$0D1A` = `A2 29 CB 29 C8 29 C7 29 C8 29` = "Crono" (tiles = 0x100+code, pal 0x29), i.e. the `$2C23`
codes copied verbatim into the tilemap with a palette attribute. **The glyph tiles = 0x100 + the
`$2C23` CT-code** (the same font-code→tile mapping as every other strip glyph).

**The name-tile PALETTE (attr hi) is an ACTIVE-PC indicator** (`$95D5`), NOT HP-driven and NOT a blink:
in `cmd_inbattle` ($95D5=0=Crono) Crono's name row is pal **0x29** (white/pal2), Marle's is pal
**0x2D** (grey/pal3). Confirmed static across every rendered frame and every HP condition tested
(Part B): **active PC ($95D5) name = pal2 (white); non-active = pal3 (grey).**

### A2. Our render reads it correctly, but the baked reference name is SKIPPED

`src/field.c CtBattleHudRender` (party-row loop, ~line 1655) reads the name correctly, stride 6, with
the terminator break:
```c
for (int i = 0; i < 5; i++) {
    uint8_t code = mem[0x2C23 + charId * 6 + i];   // $7E2C23 + charId*6
    if (!code) break;                              // $00 terminator
    battleHudGlyph(fb, pal8, namePal, code, (13 + i) * 8, y);
}
```
And the baked REFERENCE name in `ctBattleHudMap*` (which contains "Crono"/"Marle" as the layout
template) is **deliberately SKIPPED** for present seats (field.c ~1599):
```c
if (c >= 13 && c <= 30 && (r == 2 || r == 4 || r == 6)
    && !(mem[0x2980 + (r - 2) / 2] & 0x80)) continue;   // present seat's party row -> live pass owns it
```
This skip is correct and intentional (a menu-font glyph has transparent value-0 pixels, so overwriting
the baked "70" with a live "20" would leave the "7" ghosting through → the "77" double-glyph). So for a
present seat, **the baked name is skipped AND the live name is drawn from `$2C23`.**

### A3. ROOT CAUSE — our field VM never SEEDS `$2C23` (porting gap)

`grep -rn 2C23 src/` → the **only** access to `$2C23` is the *read* at field.c:1655. **Nothing anywhere
writes `mem.s[0x2C23]`.** `CtSceneCreate` (ct_vm.c) `calloc`-zeros the whole `CtScene`, and its seed
block (field.c ~1436) seeds `$2600` (char sheets, from `ctCharBase`), `$2C53` (gold), `$2400`
(items) — **but not `$2C23`**. The naming screen (op 0xC8, ct_vm.c:1187 → `CtSetName`, field.c:4444)
writes only the ASCII `g_ctName[8][8]` table (used by `CtResolveNames` for dialogue), NOT `mem.s`.

⇒ In normal interactive play (fresh scene via `CtSceneCreate`, no WRAM/state load) `mem[0x2C23]` = 0,
so the live read hits `if(!code) break` on the first byte → **no name glyph drawn**. Combined with the
baked-name skip (A2), the net result is **BLANK names** — exactly the user's report.

**Why the pinned checks don't catch it:** `field-battlehud-gato` / `field-battletest-gato` invoke
`--battletest saves/battle_re/gato_t0.raw.wram`, and that oracle WRAM **has `$2C23` populated** (A1), so
the load overwrites the zero seed and names render. Fresh play has no such load. (The dossier
`battle_hud_render.md §D4` 12288/12288 verify is a Python reconstruction from the *baked* map, which
also has the names — neither path exercises a zeroed `$2C23`, so the gap was masked.)

---

## PART B — the LOW-HP FLASH (HP/MP text blinks white↔grey)

### B1. Empirical: the HP/MP digits blink; name/bar do not

Splice Crono curHP=1 into `cmd_inbattle` (`state[0x45+0x5E30]=01 00`, maxHP stays 70) → render a run of
frames via `--peek … --btnscript "0:N"` and read the shadow attr per cell:

| frame | Crono name(col13) | HP ones(col21) | MP(col24) | bar(col27) |
|---|---|---|---|---|
| f12 | A2:**29** | 74:**2D** | 7B:**2D** | 6F:29 |
| f18 | A2:**29** | 74:**29** | 7B:**29** | 6F:29 |
| f30 | A2:**29** | 74:**2D** | 7B:**2D** | 6F:29 |
| f36 | A2:**29** | 74:**29** | 7B:**29** | 6F:29 |

The **HP and MP numeric cells toggle pal 0x2D (grey/pal3) ↔ 0x29 (white/pal2)**; the **name stays 0x29**
(active) and the **bar stays 0x29** (ATB-ready). The full HP:MP field (cols 19–24: leading spaces + HP
digits + `:` + MP digits) flashes together; name (13–17), HP/MP headers, and bar (25–30) do not.
The HP DIGIT itself tracks the block: patched curHP=1 rendered `74`=0x73+1="1" (so the digits read
`$5E30` directly — **no hidden persistent HP mirror**; the flash and digits both key off the battle block).

Non-active low PC (Marle curHP=1, `$95D5`=0=Crono): Marle **name = static 2D** (never blinks),
Marle **HP+MP blink 2D↔29 in sync**, Crono (full HP) **HP static 29**. ⇒ flash is **per-PC on HP/MP
only**, all critical PCs blink in the same global phase.

### B2. Threshold = curHP ≤ maxHP/8 (maxHP >> 3) — disasm + boundary-verified

The critical test lives in the HP/MP builder (`C1/0560`→ inline, `dis.py 10604`):
```
1060B LDA $5E30,X   ; A = curHP (block+3)
1060E STA $9499      ; $9499 = curHP scratch (NOT a persistent mirror — used only for the compare)
10611 BEQ ->10625    ; curHP==0 (dead) -> skip
10613 LDA $5E32,X   ; A = maxHP (block+5)
10616 JSR C1/011B    ; C1/011B = 3x LSR = A >> 3  (routine C1/0116 is 8x LSR=>>8; entry +5 at 011B = >>3)
10619 CMP $9499      ; (maxHP>>3) - curHP
1061C BEQ ->10620    ; maxHP/8 == curHP -> CRITICAL
1061E BCC ->10628    ; maxHP/8 <  curHP -> NOT critical  (curHP above 1/8)
      (fall through)  ; maxHP/8 >  curHP -> CRITICAL
```
`C1/011B` confirmed (`dis.py 10116`): `10116..1011D` = eight `LSR A`; the JSR target `1011B` is the 6th,
so it executes **3× LSR = >>3**. ⇒ **critical ⟺ curHP ≤ (maxHP >> 3) = maxHP / 8.**

Boundary-verified empirically (Crono maxHP=70, 70>>3=8): **curHP=8 → flashes, curHP=9 → steady white,
curHP=7 → flashes.** Matches `≤ maxHP/8` exactly.

### B3. The blink toggle `$A110` — period = 12 frames (24-frame cycle)

The grey/white phase is the global byte **`$A110`** (0/1), toggled by the render/HUD bank CF
(`dis.py 9E58A`, hand-decoded from raw ROM `CF/E585`; the linear pass desyncs on M/X here):
```
CF/E58A CE 12 A1   DEC $A112        ; blink countdown
CF/E58D D0 0D      BNE ->E59C        ; not zero -> keep current phase
CF/E58F A9 0C      LDA #$0C          ; reload = 12
CF/E591 8D 12 A1   STA $A112
CF/E594 AD 10 A1   LDA $A110
CF/E597 49 01      EOR #$01          ; flip bit0
CF/E599 8D 10 A1   STA $A110         ; $A110 ^= 1  every 12 frames   (monitor writer = CF:E59C)
```
Pinned two ways: consecutive-frame peeks — `$A110` = 0 for f10–21 (12 frames), = 1 for f22–33 (12
frames); and `--monitor "A110:1" --writer` — toggles at f9,21,33,45,57 (Δ=12), writer `CF:E59C`.
⇒ **12 frames per phase, 24-frame full white/grey cycle** (~0.4 s at 60 Hz). The initial phase depends
on `$A112`'s battle-start value (not byte-pinned; visually irrelevant).

### B4. The apply — HP/MP digit palette = (critical AND `$A110`) ? grey : white

`dis.py 10620` (HP digits) + `dis.py 1066B/106A2/106C2` (MP digits):
```
105FB STZ $A10F                       ; clear per-build grey-enable (start of each PC row)
...
10620 LDA $A110 / BEQ ->10628 / INC $A10F   ; if critical(=reached here) AND $A110!=0 -> $A10F=1
...  (HP digits written to $0CC0/2/4,Y) ...
10640 LDX #0029 / LDA $A10F / BEQ / LDX #002D ; pal2(white) default; if $A10F -> pal3(grey)
1064C STA $0CC1,Y / $0CC3,Y / $0CC5,Y / $0CC7,Y   ; -> HP-digit attr cells
...  (MP identical: 106A2 / 106C2, STA $0CC1/3,Y and $0CC3/5,Y) ...
```
So per HUD build: `$A10F` starts 0; it is set to 1 **only** when the PC is critical (B2) **and**
`$A110 != 0` (B3). The HP and MP digit cells then take pal **0x2D (grey/pal3)** when `$A10F`, else
**0x29 (white/pal2)**. A non-critical PC never reaches `10620`, so `$A10F` stays 0 → always white (no
blink). The name and bar are built by other code paths and never consult `$A10F` (B1).

Writers confirmed by `--monitor "A110:1,A10F:1" --writer`: `$A110` = `CF:E59C`; `$A10F` set = `C1:0628`,
cleared = `C1:05FE`.

### B5. The two palettes (CGRAM, from the peek `.oam`, offset 1056, u16 LE)

pal2 = CGRAM[8..11] = `0000 1483 4A51 7FFF`; pal3 = CGRAM[12..15] = `0000 2506 35AB 4A51`. The digit
ink is value 3 → **pal2[3]=0x7FFF (white) ↔ pal3[3]=0x4A51 (grey)**. Our engine's baked
`ctBattleHudPal*[2]`/`[3]` already carry these, so the flash is a pal-INDEX select (2↔3), no CGRAM edit.

---

## PART C — C-side implementation spec (`src/field.c`)

### C1. Names (Part A) — seed `$7E2C23`, mandate-clean

The ROM seeds `$2C23` from the new-game/save template (like `$2600`/`$2C53`); our engine must do the
same. In `CtSceneCreate`'s seed block (field.c ~1436, alongside the `ctCharBase` → `$2600` seed and the
`g_sheetCarry` restore), write an 8×6 CT-byte name table to `s7 + 0x2C23`:

```c
static const uint8_t ctBattleNameSeed[8][6] = {
  {0xA2,0xCB,0xC8,0xC7,0xC8,0x00}, // Crono
  {0xAC,0xBA,0xCB,0xC5,0xBE,0x00}, // Marle
  {0xAB,0xCE,0xBC,0xBC,0xBA,0x00}, // Lucca
  {0xB1,0xC8,0xBB,0xC8,0x00,0x00}, // Robo
  {0xA5,0xCB,0xC8,0xC0,0x00,0x00}, // Frog
  {0xA0,0xD2,0xC5,0xBA,0x00,0x00}, // Ayla
  {0xAC,0xBA,0xC0,0xCE,0xCC,0x00}, // Magus
  {0xA4,0xC9,0xC8,0xBC,0xC1,0x00}, // Epoch (record 7 -> $2C4D)
};
memcpy(s7 + 0x2C23, ctBattleNameSeed, sizeof ctBattleNameSeed);
```
Follow the same seed-once/carry discipline as `$2600` if renames must persist across scenes; simplest
is to seed every load (names are effectively constant unless renamed). **Also** make the naming screen
write it: where `CtSetName(id, nm)` runs (field.c:4444), additionally CT-encode `nm` into
`mem.s[0x2C23 + id*6]` (5 codes + `$00`) so a rename tracks in battle. Tests unaffected: `--battletest`
loads `gato_t0.raw.wram` (which overwrites `$2C23`) AFTER the seed.

**Name palette refinement (optional, byte-truth):** the current heuristic `namePal =
(phase && k==0) ? 2 : 3` (field.c:1654) should key on the active PC, not seat 0:
`namePal = (mem[0x95D5] == k) ? 2 : 3` (active `$95D5` = pal2 white, others = pal3 grey; A1). Correct
for the common Crono-active/seat-0 case either way; only differs when a non-seat-0 PC is active.

### C2. Low-HP flash (Part B) — add to the party-row HP/MP draw

In `CtBattleHudRender`'s party loop (field.c ~1671–1677), before drawing the HP/MP digits + `:`:
```c
int critical = (curHP <= (maxHP >> 3));                 // B2: curHP <= maxHP/8
int blinkGrey = ((frameCounter / 12) & 1);              // B3: $A110 toggles every 12 render frames
int hpPal = (critical && blinkGrey) ? 3 : 2;            // B4: grey(pal3) when critical & phase, else white(pal2)
```
Apply `hpPal` (instead of the hardcoded `2`) to the HP tens, HP ones, `:` separator, MP tens, MP ones
glyphs (field.c:1673–1677). Leave the NAME (use `namePal`), the HP/MP headers, and the ATB bar palettes
unchanged — they do not flash. `frameCounter` = any free-running battle render-frame counter (e.g.
`liveHost.frames` / the host's per-frame count); phase alignment is arbitrary (B3), only the 12-frame
period is load-bearing. Guard is inherent: a dead PC (curHP 0) isn't drawn; `maxHP>>3` with maxHP≥1 is
safe.

---

## OPEN items (honest)

- **Blink phase alignment.** `$A112`'s value at battle start (which sets whether frame 0 is grey or
  white) is not pinned — arbitrary and visually irrelevant; only the 12-frame period is derived.
- **Name-builder routine address.** The name-tile palette RULE (active `$95D5`=pal2 / non-active=pal3)
  is empirically pinned (A1) across the two available 2-PC captures, but the exact ROM routine that
  writes the name row (vs the HP/MP builder `C1/0560`) was not isolated; a 3-PC / non-seat-0-active
  capture would confirm the `$95D5`-keyed rule beyond 2 PCs.
- **User wording vs ROM truth.** The report says "name/HP/MP blinks"; the ROM flashes **HP/MP digits
  only**. The name's white/grey is a *static* active-PC indicator (`$95D5`), not a blink — implement
  per the ROM (mandate), i.e. flash HP/MP, keep the name static.
- **Numeric roll-down.** CT animates the HP number counting down on a damage hit (a separate display
  counter in the damage-apply path); orthogonal to this flash, which reads the settled block
  `$5E30/$5E32`. Not derived here (belongs to `battle_loop.md`'s damage pipeline if ever needed).
- **`>99`/3-digit HP layout.** Unchanged boundary from `battle_hud_render.md`; the flash logic is
  digit-count-agnostic (it recolors whatever HP/MP cells are drawn).

---

## Scratch artifacts (paths for the caller; not committed)

Dir: `%TEMP%/claude/…/scratchpad/`
- `ct_probe.exe` (oracle copy), `cmd_inbattle.state` (in-battle COMMAND, Crono active),
  `cmd_lowhp1.state` (Crono curHP=1), `marle_lowhp.state` (Marle curHP=1), `thr_{7,8,9}.state`
  (threshold-boundary), `fA_*/s1_*/lo_*/ml_*/t_*.raw*` (per-frame peek dumps).
- Commands of record: `--peek … --btnscript "0:N"` (renders the flash); `--monitor "A110:1,A10F:1"
  … --writer` (toggle period + writers); `dis.py 10604 / 10620 / 10116 / 9E585`.

## CORRECTION 2026-09-15 -- there is no load-time name seed in the ROM

§A3's "the ROM seeds these from the new-game/save template" was wrong, and so was the fix built on it (copying the default
names on every location load). The records are written by the NEW-GAME EVENT SCRIPT's op 0x4E (`4E 23 2C 7E 32 00` + 48 bytes;
gold `4E 53 2C 7E 05 00 90 01 00`), by each naming screen (C2/E489) and by a save load; the boot wipe C2/956E does not touch
them. The attract never runs the new game: the cartridge's demo battles hold `$2C23 = 00` and draw an empty name column
(saves/attract_cena_inicial.state --peek f7740). The port's live VM already ran op 0x4E, so --newgame gets saves/crononame.state's
bytes exactly; the seed is gone (commit 0804a5b), and a `--scene` entry that skips the new game shows the zeros a cartridge that
never started one holds. Checks: newgame-names-from-event-script-vs-oracle, attract-names-zero-vs-oracle.
