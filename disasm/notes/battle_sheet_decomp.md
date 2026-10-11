# BATTLE ENEMY SHEET DECOMPRESSOR — the $7F8000 sprite-sheet load (O3/C5 CLOSED)

Closes the anim-engine's #1 standing boundary (`battle_anim_engine.md` O3/C5, `battle_animeng_tiles.md` §8,
`battle_boot.md` open "GATO's OBJ graphics ROM source … unexplored"): **how the compressed enemy sprite SHEET
gets from ROM into WRAM `$7F8000`.** It turns "an enemy's poses need a live `$7F8000` capture" into "any enemy's
sheet loads from the ROM alone". Every claim is anchored to a ROM disassembly (`tools/dis.py`, HiROM flat =
`(bank−0xC0)<<16|addr`) or a byte-exact oracle/ROM diff; nothing invented.

**RESULT: the enemy sheet is the SAME `ct_lz_decompress` (Evil Peer LZ, `src/lz.c`) already in-tree, run on a
ROM source resolved by a two-table pointer chain keyed on the enemy id. Verified BYTE-EXACT on 4 enemies
(GATO / Naga-ette / Hetake / Diablos) against the live oracle `$7F` sheets, and the chain resolves 227/256 enemy
ids to clean CT-LZ sheets.**

---

## 1. THE POINTER CHAIN (enemy id → compressed ROM source), byte-exact

```
enemy id  (= field object enemy id $1101,X = battle pack rec +5 = stat/name id; GATO 0x92, Naga-ette 0x1E,
            Hetake 0x5E, Diablos 0x48)
   │
   ▼   E4:F600 + id*10   = the 10-byte per-enemy GFX-META record (the "id→gfx chain")
   │        +0  BATTLE-SHEET index   (into E4:2000)          ← the sheet
   │        +1  FIELD-sprite / E3 packet index (into E4:2000 field-spr, E4:2300 E3-assembly)
   │        +3  animId ($8E)         (initial pose; GATO 0x57)
   │        (+2,+4..+9 = other gfx-meta: DMA/size/palette bytes, not needed for the sheet source)
   ▼   E4:2000 + (rec+0)*3   = 24-bit ROM POINTER to the COMPRESSED sheet   (GATO 0x7B → $DF:0000)
   │        (E4:2000 is ONE shared graphics-source table: PC sheets by charid, enemy field sprites by
   │         packet(+1), enemy BATTLE sheets by (+0). E4:2300[pkt] is the parallel E3-assembly table.)
   ▼   ct_lz_decompress(ROM, source)   = the CT LZ (Evil Peer / src/lz.c)      → the 4bpp sheet
   ▼   written to WRAM $7F**dest**  (per-combatant slot dest; GATO $7F8000, Naga-ette $7F8CE0,
        Diablos $7FB0A0, Hetake $7FC040 — the $1C57[slot] sheet ptr)
```

**Field-side caller (`C0:46DF`, op-0x83 LoadEnemy):** reads `E4:F601,X` (packet, the +1 field/E3 index),
computes `pkt*3`, `LDA [$B2],Y → $1380/$1300` = the FIELD-sprite gfx source from the `E4:2000` base `$B2`.
So `E4:2000` is confirmed the graphics-source table; the FIELD sprite uses +1 (GATO +1=0x76 → `E4:2000[0x76]` =
`$DA:1C16`, a 288-byte field sprite), while the BATTLE sheet uses +0. `E4:2300[0x76]` = `$E30000` = GATO's E3
assembly (matches `animeng_e3_gato.h`), confirming the packet index and the parallel-table layout.

### Confirmations (read from the ROM)
- `E4:F600[0x92*10]` (GATO) = `7B 76 B6 57 02 00 D0 10 10 41`  → +0=0x7B, +1=0x76, +3=0x57(animId).
- `E4:2000[0x7B*3=$E4:2171]` = `00 00 DF` = **$DF:0000** (GATO battle sheet source).
- `E4:2000[0x76*3=$E4:2162]` = `16 1C DA` = **$DA:1C16** (GATO field sprite); `E4:2300[0x76]` = **$E3:0000** (E3).
- Crono charid 0: `E4:2000[0]` = `$D272C0` (the known uncompressed Crono PC sheet), `E4:2300[0]` = `$DC27B0`
  — proving E4:2000 = the graphics-source table (PCs are uncompressed & loaded directly; enemies are compressed).

---

## 2. THE DECOMPRESSOR = CT LZ (Evil Peer), bank C3 — byte-for-byte `src/lz.c`

The routine that fills `$7F8000` is CT's standard graphics decompressor (bank C3), the **same segmented LZ**
already ported in `src/lz.c` / `assets/lz.py`. It reads the compressed source **directly from ROM** (data-bank =
the source bank) and writes the 4bpp output straight into WRAM via the **WMDATA port**:

- **Literal byte** → `STA $002180` (WMDATA, auto-increments WMADD) — `C3:0840` `LDA $0000,X / STA $2180`.
- **Back-reference** → `MVN $7F,$7F` (copy already-emitted output within bank $7F) then `TYA / STA $002181`
  (resync WMADDL) — the destination bank is $7F, WMADD pre-set to the slot dest ($7F8000 for GATO).
- **8 raw literals fast path** (flag byte `00`) → `C3:07E5` `LDA $0001,X / STA $2180 …` ×8.

Two window variants, exactly `src/lz.c`'s `small`:
| variant | ROM | count shift | offset mask | dest bank |
|---|---|---|---|---|
| SMALL (11-bit window) | `C3:0850` (MVN@`C3:087A`, store `C3:087D`) | `>>3` | `&0x07FF` | `MVN $7F,$7F` |
| LARGE (12-bit window) | `C3:06D7` (MVN@`C3:06F6`, store `C3:06F9`) | `>>4` | `&0x0FFF` | `MVN $7F,$7F` |
| (SMALL, $7E dest)     | `C3:07B0` (MVN@`C3:07B8`) | `>>3` | `&0x07FF` | `MVN $7E,$7E` |

`src/lz.c` picks small/large once from `data[byte_after] & 0xC0` (the byte just past the first segment) — the
same discriminant; it reproduced all four sheets byte-exact, so the format is fully captured by the existing port.
No new decompressor code is needed — only the pointer chain (§1) to feed it the right ROM source.

**Format recap (already documented in `src/lz.c`):** `+0/+1` = 2-byte compressed-size header; then flag bytes
whose bits select literal(0)/backref(1); backref = 2 bytes `off|count`; segments rebased via a 3-byte control
(`ctl&0x3F` = next bit-count, `0` ends the stream). The enemy sheet's leading tile-0 is a 32-byte zero run — it
is NOT encoded as a flag-`00` fast path (that was a wrong scan prefilter); the true stream mixes literal+backref
for it, which is why a naive "flag 00 then zeros" search missed the source.

### Why the CPU-write monitor showed no writer on $7F8000
Literals land via `STA $2180` (a write to the I/O register `$002180`, not to `$7F8024`), and backrefs via `MVN`;
the WRAM byte is deposited inside the WMDATA handler. So `--monitor --writer` on the `$7F8000` window reports no
CPU store for the sheet (the deposit bypasses the tracked `cpu->write` address) — the write is real but arrives
through the WMDATA port. (The C3-LZ MVN writes ARE caught when a broad `$7F` window is watched — that is how
`C3:06F9`/`C3:087D` were first located, during an unrelated storyline-03 loc-6 load.)

---

## 3. VERIFICATION TABLE (byte-exact, CT-LZ output vs live oracle `$7F` sheet)

| enemy | id | E4:F600[+0] | E4:2000 entry (source) | csize | decomp size | live $7F dest | match |
|---|---|---|---|---|---|---|---|
| **GATO**      | 0x92 | 0x7B | `$E4:2171` = **$DF:0000** | 0x202B | 0x2C80 (11392) | $7F8000 (gato_t0) | **11392/11392** |
| **Naga-ette** | 0x1E | 0x57 | `$E4:2105` = **$D6:C77E** | —      | 0x3000 (12288) | $7F8CE0 (bat8)    | **12288/12288** |
| **Hetake**    | 0x5E | 0x70 | `$E4:2150` = **$D7:2AD8** | 0xBFD  | 0x1000 (4096)  | $7FC040 (forest)  | **4096/4096**   |
| **Diablos**   | 0x48 | 0x6F | `$E4:214D` = **$D7:0D18** | —      | 0x2780 (10112) | $7FB0A0 (bat9)    | **10112/10112** |

Notes:
- GATO source `$DF:0000` also decompresses byte-exact vs the in-tree fixture `animeng_sheet_gato.h` (first 0x160
  tiles = 0x2C00 bytes, 11264/11264; the full decompress is 0x2C80 = 4 tiles more than the fixture captured).
- Naga-ette/Diablos live sheets were located at their per-slot `$7F` dests inside `bat8`/`bat9` (whose `$7F8000`
  is a spliced/leftover Slash field packet — do NOT diff a battle sheet at a fixed `$7F8000` for multi-formations;
  read `$1C57[slot]`). Their dests `$7F8CE0` / `$7FB0A0` match `battle_3pc_lucca_frog.md` §257/§270 exactly.
- Hetake's chain source `$D7:2AD8` is shared by the whole rock family (0x5B Crater / 0x5C Volcano / 0x5D Shitake /
  0x5E Hetake / 0x5F Rubble / 0x61 Shist / 0x62 Pahoehoe) — palette-swap variants, the canonical CT pattern.
- **Structural sweep:** resolving `E4:F600[id*10+0] → E4:2000 → ct_lz_decompress` over all 256 ids yields **227
  clean sheets** (tile-0 zero header, plausible size) across **83 distinct sources** (the 29 "other" are unused
  slots), with the expected family sharing (Reptite, Terrasaur/Megasaur/Gigasaur, Blue/Green/Stone Imp, …).

---

## 4. HOW TO REPRODUCE
- Source lookup: `E4:F600+id*10 → +0 → E4:2000+idx*3` (`build/ct.exe --read "rom:E4/F600+10"`,
  `rom:E4/2171+3`, …). GATO = `$DF:0000`.
- Decompress: `build/ct.exe --lz 1F0000` (= $DF:0000) → size + FNV; cross-check `python assets/lz.py <rom> 1F0000`.
- Live sheet: `build/ct.exe --read "wram:7F/8000+16" --from <state>`; GATO from `gato_t0.raw.wram`[0x18000],
  Hetake from `forest_battle_3hetake.state` `$7FC040`, Naga-ette `bat8.raw.wram`[$7F8CE0], Diablos
  `bat9.raw.wram`[$7FB0A0].
- The decompress is a **field scene-load / battle-preload** step (each enemy's sheet decompressed to its `$7F`
  slot when the enemy is staged), NOT a per-battle-boot event: a booted state re-uses the resident sheet, so
  `--monitor` a battle-boot shows `$7F8000` unchanged. `--teleport <scene> … --method jmp` from a state that
  already holds the sheet reproduces it byte-exact; a cleared state's simplified jmp does NOT re-preload it.

## 5. OPEN / RESIDUAL
- The exact CALLER that sets WMADD to each slot dest + issues the C3-LZ call per staged enemy (the "battle-gfx
  preload" pass) is not disassembled to the instruction — the CHAIN + decompressor + dests are byte-proven, which
  is all a port needs. The `E4:F600` bytes +2/+4..+9 (DMA/size/palette meta) are unused by the sheet load.
- Diablos (0x48): sheet **now derivable ROM-only** ($D7:0D18 → 10112 B, byte-exact). This unblocks the O6
  idle-pose sheet dependency WITHOUT a user capture. His E3 idle-pose SCRIPT (the E4 pose-window / assembly
  indices for the idle) still needs its own derivation/anchor — sheet-only is the win here; do not invent the
  pose script.
