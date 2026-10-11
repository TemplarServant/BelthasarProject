# BATTLE ENEMY POSE STAGING — id → E4:F600 record → pose SCRIPT + E3 base + stride (O6 script/base CLOSED)

Closes the pose-script half of the anim-engine's **O6** (`battle_anim_engine.md` §D O6, `battle_sheet_decomp.md` §5:
"Diablos's E3 idle-pose SCRIPT … still needs its own derivation"). The sheet lane resolved the enemy sprite
**pixels** from the id alone; this lane resolves the enemy pose **SCRIPT pointers + assembly base + window stride**
from the id alone. It turns "an enemy's idle pose needs a live oracle capture of `$1CA2/$1CC3/$1C78`" into "any
enemy's pose staging is computed from the ROM". Every claim is anchored to a ROM disasm (`tools/dis.py`, HiROM
flat = `(bank−0xC0)<<16|addr`) or a byte-exact oracle diff; nothing invented.

**RESULT: the enemy pose staging is a pure id-keyed table walk — `E4:F600[id*10]` → `+3` animId → `E4:2600/2800`
= the pose scripts `$1CA2/$1CC3`; `+1` packet → `E4:2300` = the E3 assembly base `$1C78`; the window stride
`$9671 = (E4:2600[animId+1] − E4:2600[animId]) >> 2`. Verified BYTE-EXACT on 4 enemies (GATO / Naga-ette / Hetake
/ Diablos) against the live oracle staging, and the chain resolves 252/256 ids to real E3 bases. Diablos's idle
SCRIPT + BASE + rest INDEX are now ROM-derived with ZERO capture.**

---

## 1. THE 10-BYTE `E4:F600` RECORD — every byte decoded

`E4:F600 + id*10` (flat `0x24F600 + id*10`). The battle staging routine (§2) and the field op-0x83 loader
(`ct_vm.c` C0/46F7-476A comment) read it. Roles, byte-cited:

| off | role | consumer | GATO(0x92) |
|---|---|---|---|
| **+0** | **battle-SHEET index** → `E4:2000[+0*3]` = 24-bit compressed-sheet ROM ptr | the SHEET lane (`battle_sheet_decomp.md`) | 0x7B → `$DF0000` |
| **+1** | **packet / assembly index** → `E4:2300[+1*3]` = 24-bit **E3 assembly base** (`$1C78`); also `E4:2000[+1*3]` = the FIELD sprite | pose builder `C1/1C4A` | 0x76 → `$E30000` |
| **+2** | **palette index** → `+2 * 24` = the bank-E4 palette DMA offset | CGRAM upload | 0xB6 → 0x1110 |
| **+3** | **initial animId `$8E`** → `E4:2600[+3*2]` = ptrA (`$1CA2`), `E4:2800[+3*2]` = ptrB (`$1CC3`) | pose tick `C1/3360` | 0x57 |
| **+4** | **anim-set / size class** (stand/dissolve death behavior) → `$9700[slot]` (bits 0x80/0x08/0x20 flags) | `fieldBattleFinish` (already ROM-driven, `battle.c:1361`) | 0x02 (hold-in-place) |
| **+5..+9** | targeting **hitbox / size + flags** (`+5` Y-nudge, `+6` vertical anchor, `+7/+8` half-W/half-H, `+9` flags) — **NOT read by the pose staging** | targeting / hit-detection | `00 D0 10 10 41` |

The four record dumps (ROM-read):
```
GATO     0x92: 7B 76 B6 57 02 | 00 D0 10 10 41
Naga-ette0x1E: 57 3C 79 2A 00 | 00 F0 0C 14 21
Hetake   0x5E: 70 4E 6E 30 00 | 00 F8 10 10 41
Diablos  0x48: 6F 4C 68 26 00 | 00 F0 08 08 22   (Gargoyle 0x49 = same +1/+3 → shared anim, palette variant)
```

## 2. THE STAGING ROUTINE (bank CC, `dis.py 0x0CEAD2`) — byte-cited

The per-combatant battle-entry staging loop (`CC/E97E` party head → PC pass `CC/EA00` → enemy pass `CC/EA5B`).
Its enemy pass reads the `E4:F600` record and writes the pose-script pointers. `$80` = enemy slot; `Y = $80*3`
(the pose-tick's `$1CA2,Y` index space); `X = id*10`; the E3/pose vars alias the builder's cmb-space via
`cmb = slot + 3` (`$1C81[slot] == $1C78[cmb]`, `$966E[cmb] == $9671[slot]`, see §4).

```
CEAD2  LDA $29C4,X / STA $8C        ; $8C = enemy id (from the staged $29xx combatant block)
CEAD7  ASL A / STA $82              ; id*2
CEADA  ASL A / ASL A / ADC $82      ; id*8 + id*2 = id*10   ** record stride **
CEADF  TAX                          ; X = id*10
CEAE0  LDA $E4F603,X / AND #00FF / STA $8E   ; ** record[+3] = initial animId → $8E **
CEAE9  LDA $E4F601,X / AND #00FF / STA $82   ; record[+1] = packet
CEAF2  ASL A / CLC / ADC $82 / TAX          ; packet*3
CEAF7  LDA $E42300,X / STA $1C81,Y          ; ** E4:2300[pkt*3] lo/mid = E3 base addr16 → $1C81,Y (=$1C78[cmb]) **
CEB01  LDA $E42302,X / STA $1C83,Y          ; E4:2300[pkt*3+2] = E3 base BANK → $1C83,Y
CEB0A  LDA $8E / ASL A / TAX                ; animId*2
CEB0E  LDA $E42600,X / STA $1CA2,Y / STA $84 ; ** ptrA = E4:2600[animId*2] → $1CA2,Y **
CEB17  LDA $E42800,X / STA $1CC3,Y / STA $86 ; ** ptrB = E4:2800[animId*2] → $1CC3,Y **
CEB23  LDA #E4 / STA $1CA4,Y ; LDA #E4 / STA $1CC5,Y   ; both pose-script banks = E4
   ; --- window STRIDE = size of one animId's ptrA block, /4 ---
CEB2F  LDA $8E / INC A / ASL A / TAX        ; (animId+1)*2
CEB34  LDA $E42600,X / STA $88              ; ptrA[animId+1]
CEB40  SEC / LDA $88 / SBC $84 / LSR / LSR / STA $84   ; ** (ptrA[animId+1] − ptrA[animId]) >> 2 = stride **
CEB53  CLC / LDA $80 / ADC #03 / TAX        ; X = slot+3 = cmb
CEB59  LDA $84 / STA $966E,X                ; ** $966E[cmb] = stride (== $9671[slot]) **
   ; --- size / anim-set byte ---
CEB65  LDA $8C / ASL/ASL/ASL/ADC $82 / TAX ; X = id*10 again
CEB73  LDY $80 / LDA $E4F604,X / STA $9700,Y ; ** record[+4] = anim-set/size → $9700[slot] (bits 0x80/08/20) **
```
(The PC pass `CC/EA00` runs the identical `(ptrB[+1]−ptrB)>>2` / `(ptrA[+1]−ptrA)>>2` stride math for the 3
party PCs — pose staging is one machine for PCs and enemies.)

## 3. VERIFICATION — ROM-computed staging vs the LIVE ORACLE (byte-exact)

Computed from the ROM chain (§1-2) and matched against the live oracle WRAM (`$1CA2`/`$1CC3` = pose-tick
slot-space `slot*3`; `$1C78`/`$1C57` = builder cmb-space `cmb*3`; `$9671` = slot-space). Sources: GATO from
`gato_press_action.state` driven to battle-live (staging fires f292); Naga-ette `bat8.raw.wram`; Diablos
`bat9.raw.wram`; Hetake `forest_battle_3hetake.state` (WRAM at file off 0x45).

| enemy | id | animId(+3) | ptrA `$1CA2` | ptrB `$1CC3` | E3 base `$1C78` | stride `$9671` | sheet `$1C57` | match |
|---|---|---|---|---|---|---|---|---|
| **GATO** | 0x92 | 0x57 | `$E472C0` | `$E4BF09` | `$E30000` | 0x28 | `$7F8000` | **byte-exact** |
| **Naga-ette** | 0x1E | 0x2A | `$E45F50` | `$E4BA24` | `$DD4628` | 0x24 | `$7F8CE0` | **byte-exact** |
| **Hetake** | 0x5E | 0x30 | `$E462F0` | `$E4BB0C` | `$DD8610` | 0x20 | `$7FC040` | **byte-exact** |
| **Diablos** | 0x48 | 0x26 | `$E45CE0` | `$E4B988` | `$DD7E68` | 0x28 | `$7FB0A0` | **byte-exact** |

- The sheet dest `$1C57` / sheet source (`E4:2000[+0]`) match `battle_sheet_decomp.md` §3 exactly (independent lane).
- **Diablos's staging is byte-exact even though his ATB was mid-action in bat9** — the CD/5FF0 action path
  rewrites only `$8E`, never `$1CA2/$1CC3/$1C78` (`battle_animeng_script.md` §4). So his STAGED pose script is
  the intact idle anchor; only his live `$8E`/`$967B` were polluted. This is the O6-closing datum: no capture.
- **Structural sweep:** 252/256 ids resolve to a real E3 base (`0xC0≤bank≤0xFF`) + plausible stride, with the
  expected family sharing (Diablos 0x48 / Gargoyle 0x49 share ptrA/E3; Naga 0x1E / Ruminator 0x20 share sheet).

## 4. THE cmb = slot + 3 ALIAS (why two dossiers used different bases)

The pose **tick** (`C1/3360`) indexes `$1CA2/$1CC3/$9671` by enemy **slot** (`slot*3`); the **builder**
(`C1/1C4A`) indexes `$1C78/$1C57/$967C` by **combatant** `cmb` (`pidx = cmb*3`, `CCF92D`). The two spaces
overlap: **`cmb = slot + 3`** (the 3 party PCs occupy cmb 0/1/2). Hence `$1C78[cmb] == $1C81[slot]` (Δ = 9 =
3*3) and `$966E[cmb] == $9671[slot]` — the staging routine (§2) writes the E3 base to `$1C81,Y`/stride to
`$966E,X`, which the builder reads as `$1C78`/`$9671`. Verified: GATO slot 3 = cmb 6; bat8 4×Naga at slots
0-3 = cmb 3-6; bat9 3×Diablos at slots 3-5 = cmb 6-8; forest 3×Hetake at slots 0-2 = cmb 3-5.

## 5. THE IDLE REST INDEX (Diablos 0x00 / Naga 0x17) — ROM-derivable + oracle-staged

The pose tick copies `ptrA[$82..$82+7] → $97D9[slot*8]` (the current facing's assembly-frame window) and emits
`$967B = $97D9[cursor]`. The **rest pose = cursor 0 = `$97D9[slot*8]`**, and it is BOTH ROM-derivable (the first
window of the initial-animId ptrA) AND readable from the oracle-STAGED `$97D9` (stable even when `$9907`/`$967B`
are polluted mid-action):

| enemy | staged `$97D9` window (bat8/bat9) | dwell `$9799` | rest idx | ROM ptrA source | built `$9907` |
|---|---|---|---|---|---|
| **Naga-ette** | `17 18 FF 00 …` | (loop) | **0x17** | ptrA[0x18] (mode4 `$86`=0x18, facing0) | 0x17 ✓ (direct, no remap) |
| **Diablos** | `00 01 02 01 FF …` | 02 02 02 02 | **0x00** | ptrA[0x00] (mode1 `$86`=0x00, facing0) | 0 (builder idle, un-run) |

Both windows use **DIRECT E3 indices** (`0x17→0x17`, `0x00→0x00`) — no `$97D9→$967B` remap (contrast GATO's
`04 14` where `0x14→idx5`, the O4 remap). So Diablos's idle rest assembly is `E3(base $DD7E68, idx 0x00)`,
verifiable against bat8/bat9's staged `$97D9` and against the ROM ptrA first-window. His E3 records are type-0
(4-cell, `$967C`=0, stride 0x28, `[16 cell-words +0x00][4 dx,dy +0x20]` = the PC layout of `animeng_e3_pc.h`);
E3[0x00] @`$DD7E68` decodes to a sane 4-cell pose (cell-words `0000 0000 0000 003C 0042 0043 003E 003F`, dx/dy
in-range). Naga is type-0 too.

**The engine's earlier "Diablos idle un-captured → rendering violates the mandate" (`field.c:3933`) rested on
`$9907[6..8]=0` (the BUILT cache, empty because his ATB was mid-action). But the pose SCRIPT `$97D9` carries the
idle window regardless — so the idle IS derivable; the `$9907` read was the wrong probe.**

## 6. OPEN / RESIDUAL

- **The tick's facing→window map** `$82 = CC/FB91[mode] + facing × $9671` — the per-mode class base `$86`
  (`CC/FB91[mode]`: idle-GATO mode → 0x0C, Naga mode4 → 0x18, Diablos mode1 → 0x00) is oracle-inferred here from
  the `$97D9` match, not yet dumped from `CC/FB91`. Pinning `CC/FB91` fully closes the window-offset derivation.
- **The `$97D9→$967B` remap** (GATO `0x14→idx5`, `0x10→idx4`) — the >0x0F ptrA bytes' mapping is un-derived (O4).
  Enemies whose idle window uses ≤0x0F indices (Naga, Diablos) are unaffected; GATO's idle SWAP frame needs it.
- **No oracle Diablos-idle pixel frame** — bat9 captured him mid-action, so his rendered idle POSE cannot be
  byte-diffed against the oracle (unlike the sheet's 10112/10112). The staging + rest-index are byte-exact; the
  pixel render is ROM-derived-and-`$97D9`-confirmed but pixel-unverifiable until a Diablos-idle capture exists.
- **Record `+5..+9`** exact hitbox/size field split (values suggest `+7/+8` half-extent, `+9` flags) — not
  consumed by pose staging, left uncharacterized to the byte.

## 7. WHAT A C PORT NEEDS (retiring the captured pose-staging bakes)

1. The **id-keyed resolver**: `E4:F600[id*10]` → `+3` animId → `E4:2600[animId*2]`/`E4:2800[animId*2]` = ptrA/ptrB
   (bank E4); `+1` packet → `E4:2300[pkt*3]` = E3 base; `stride = (E4:2600[animId+1]−E4:2600[animId])>>2`;
   `+0` → `E4:2000[+0*3]` = sheet. All ROM reads (like `battle_trig.h`).
2. This RETIRES the captured `ctAnimengWinGato` **staging** anchor (the hardcoded GATO `$E30000`/`$E472C0`/0x28)
   → the resolver returns them for id 0x92 byte-identically. The per-facing **window→index** resolution (the
   tick + O4 remap) stays where the anim-engine dossiers left it — this lane closes the SCRIPT/BASE STAGING, the
   `battle_sheet_decomp.md` analogue for the pose pointers.

---

## Scratchpad artifacts (paths for the caller; NOT committed)
Dir: `%TEMP%/claude/…-9e84b785-…/scratchpad/`
- `ct.exe` (= build/ct.exe) + repo `SDL2.dll`. `chain.py` (the ROM chain-follower + oracle matcher).
- Oracle reads: GATO `--monitor "1CA2:30,1CC3:30,1C78:30,1C57:30,9671:8" --from gato_press_action.state
  --btnscript "100:8,0:300"` (staging fires f292); Naga/Diablos from `saves/battle_re/bat8|bat9.raw.wram`
  (WRAM images, `$97D9`/`$9907`/`$967C` per slot); Hetake `forest_battle_3hetake.state` (WRAM off 0x45).
- Disasm: staging routine `tools/dis.py 0x0CEAD2` (record→animId/E3/ptrA-B), `0x0CEB2F` (stride), `0x0CEA00`
  (PC pass). ROM tables E4:F600 (0x24F600), E4:2000 (0x242000), E4:2300 (0x242300), E4:2600 (0x242600),
  E4:2800 (0x242800).
