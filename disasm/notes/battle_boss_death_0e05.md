# BATTLE — the boss death: CD/0043's arms into CD/0E05, and the staging bytes they read (2026-10-16)

Census family 5 (battle_sim_census.md sec. 6). Every claim cites a ROM address (`tools/dis.py`) or the rule's cartridge
(`build/ct.exe`, `CT_ORACLE_LOGICAL=1`, through `tools/battle_sim.py fight <id> hp=1` from `saves/battle_re/gato_wait.state`).
Port: src/battle.c `battleDeathFxFrame`, `battleDissolvePackets`, `battleDeathSfxTick`, `battleAdvanceDissolves`, the
FD/B121 store in `stageAll`, the CC/EBA4 and CC/EC63 stores in `battleMoveInit`, program op 80 sub-ops 0F and 11.

## 1. CD/0043 after its `$CCEA` wait (dis.py 0D0043 0x90 1 0)

```
D0052  LDA $99D2 / BEQ D0083 / CMP #02 / BEQ D0083     ; no arena (0) or arena kind 2 -> D0083
D005B  LDA $2A21 / AND #10 / BNE D0093                 ; the battle's flag -> the plain death
D0062  LDX $CD00 / LDA $A08C,X / BEQ D0083             ; the corpse is not part of the arena -> D0083
D006B  LDA $1D0C,X / TAX / JSL CD00A0                  ; the dissolve (no #95 post; $99D2 != 0 skips CD/00A5's record)
D0073  LDA #40 / JSL CD0E05                            ; effect 40
D0079  LDA $BE89 / AND #FE / STA $7F1520 / RTL
D0083  LDX $CD00 / LDA $A01D,X / AND #01 / BEQ D0093
D008E  LDA #38 / JMP CD/0E05                           ; effect 38 instead of the whole death: no post, no dissolve
D0093  ... the plain death (#95 post, CD/00A5's red fade when $99D2 == 0, CD/00BF's 10 waits, 32 dither levels)
```

The port read the arms but `$A01D`, `$A08C` and `$99D2` were never staged (all 00), so every boss took D0093.

## 2. The staging bytes

- **`$A01D,cmb`** — C1/FAB9 `JSL FDB121` right after the FD/B438 loop: `LDX #0180 / LDY #0000 / LDA $5E47,X / AND #03 /
  STA $A020,Y`, X += 80, eight times: block +0x1A & 3 (the stat record CC/4700 + id·23, byte 21). Cartridge, Yakra:
  `$A020 = 03` at f271 (FD:B12F). Ids with bit 0: 04, 25, 8F, 90, 99, 9B, 9C, 9D, 9E, 9F, A1, A6, A9, BC, BD, BE, BF, C3,
  C5, C6, C7, CB, E6, EB, EC.
- **`$A08C,cmb`** — the loader CC/EBA4-EBA9, unconditional per present slot: `LDA $9700,Y / AND #04 / STA $A08F,Y`
  ($9700 = E4:F600 + id·10 byte +4). Cartridge: Giga Gaia D1 `$A092 = 04`, the Guardian D4 `$A08F = 04` (CC:EBAC).
- **`$99D2/$99D3`, `$A0A7`, `$A113`** — CC/EC63-ECAF after the eight slots: each `$9853,Y` (the id word, Y = 0..E step 2)
  against E1:DE00's `id, kind` word pairs up to a negative id; kind 3 -> `$80 = 1`, kind 4 -> `$82 = 4`, any other kind ->
  the 16-bit `STA $99D2` (CC/EC90). Then `$A0A7 |= $80`, `$A113 = $82`. The table: D4 6001, BC 5902, BD 6002, D1 7001,
  CB/CC/CD 6001, 5F/80/43 kind 3. Cartridge: `$99D2 = 01` f271 (CC:EC93) for Lavos CC, Giga Gaia, the Guardian.

## 3. CD/0E05 (dis.py 0D0E05 0x1E 1 0)

```
CD/0E05  PHA / JSR CD/0DFA     ; `LDA $5D9B / BEQ`, else a frame: an action's program still running is waited out
         PLA / TAX / JSR CD/0EBD   ; the effect load of A -- the action's own (CD/0D28 is `STZ $5D9B` + this + CD/0D33)
         JSR CD/0D33           ; the two program slots from D1:6240[fx*4]
         INC $5D9B             ; the programs run (CD/1609, the frame service)
CD/0E14  LDA $5D9B / BEQ RTL / JSR CD/3E75 / JSL CD04AA / BRA CD/0E14
```

The load keeps its own frames (battle.c's battleFxActionLoadFrame table: f 0 CD/0EBD, f w+2 CD/0F42, f w+8 the end and the
arm). CD/04AA is the main loop's CD/0009 body: the teardown CD/0453 once the runner's end raised `$CCEA` -- entered on
the very frame the runner ends (the loop calls it right after that CD/3E75), and its last CD/3E75 (CD/04A2) is one more
frame before the RTL.

Cartridge, Yakra (`fight 90 hp=1`): death f834 (`$CD00 = 03`, CD:0046); `$CCA2 = 38` f835 (CD:0EE1); `$CA93 = 3F FF FF FF`
f837 (CD:0F56); `$5D9B = 01` f843 (CD:0E14); program 3F (D1:87EB): three flashes of colour 0x10 (`8E 40 10 09 ..`),
`80 0F` at f1073, the OBJ-pal-4 fade to white (`8E 40 C0 0F ..` / `8E 50 C0 0F ..`, every 4 frames from f947); `$5D9B = 00`
/ `$CCEA = 01` f1368 (CD:1650/1653); the teardown's `$CCA2 = 00` / `$CCEA = 00` f1388; `$96F8 = 00` f1389 (CF:FE7E). The
palette page takes 190 states. Giga Gaia (`fight D1`): the dissolve f841-f884, `$CCA2 = 40` f884, `$CA93 = 4A FF 02 02`,
`$5D9B = 01` f892, `80 11` at f961 (`$99D2 = 81`); program 4A then idles in `83 FF 7F 84` loops past f1500.

## 4. Program op 80's sub-ops the boss programs need (CD/2A4A `LDA [$40] / ASL / TAX / JMP ($2A51,X)`)

- `80 0F` CD/2B5C: `LDX $CD00 / STZ $99B5,X` -- the corpse's OAM priority to 0. `$CD00` is CD/0043's own `STA $CD00`
  (the combatant), which the port now stores at the routine's start.
- `80 11` CD/2B27: `$99D2 |= 80`; `$05A5 = $0599 = $99D3` and `$05A6 = $059A = 80 - $99D3` (objslot records 9 and 10's
  CGRAM range cut to the arena's).

## 5. The dissolve's level is CD/3ED2's upload

CD/00D0-00F8: the level's size is CE:F156[`$967C,X`] (`0400 0400 0800 1000 1000 0800 0400 0400`, X = `$CD00`), shipped by
CD/3ED2 in packets of $400 a frame (`$CE0F` clear, CD/3F47-3F6D) or $1000 (`$CE0F` set). A type-2 or type-5 corpse takes
two frames a level: Lavos's core CC (type 2) runs from f379 to `$96FC = 00` at f454, 75 = 1 + 10 + 32·2; the Guardian
D4 f835 -> its CD/0E05 at f910 (75); Giga Gaia D1 (type 0) f841 -> f884 (43). The port ran one level a frame for all.

## 6. Result (both sides, `fight <x> hp=1`)

90 Yakra, 99 Masa & Mune, 9B Nizbel: the whole monitored WRAM the same, +0 on every change, the death palette 190 states
the same sequence (port 18 before). CC/CD Lavos: 1 state each side (17 before), WRAM the same. D1 Giga Gaia, D4 Guardian:
2 states, WRAM the same (the port ended the fight after its dissolve before). 04 Krawlie: 64 states the same; the port
shows one more change on the run's last frame only (the harness records the port's frame `total`, not the cartridge's).

Not modelled: a two-packet level's FIRST packet on the picture (the upper half of the new level's tiles, a frame early);
the port draws the level when its last packet ships.
