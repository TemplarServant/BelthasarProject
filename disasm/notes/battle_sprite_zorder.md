# BATTLE SPRITE Z-ORDER — the OAM list is a per-frame DESCENDING SORT BY SCREEN Y

User report (translated from Portuguese): *"the Z priority of the sprites in the battle's attacks is not canonical (when
sprites overlap)"* — when two battle sprites overlap (most visibly while a PC is attacking, i.e. while
it is standing on top of its target), the wrong one wins.

Companion to `battle_animeng_builder.md` (PART 3: `$967B` → assembly → `$9F47` → the CF/EAC5 compose that
writes `$7E0700`). That dossier derived what a combatant's cells look like and where they go; it left
**which OAM slots a combatant gets** (`$C9`, "the OAM cursor") unexplained. This closes it.

---

## HEADLINE

**Every frame the battle engine BUILDS a list of its render objects and SELECTION-SORTS it, descending, by
the object's live screen Y (`$1D17,obj` = `$1D23+cmb`). The sorted list is then walked in order, handing out
OAM blocks from entry 4 upward. Lower OAM index = in front on SNES, so the combatant with the LARGEST screen
Y is the FRONTMOST — a plain depth sort, with PCs and enemies fully INTERLEAVED.** There is no
"PCs draw over enemies" rule anywhere in the ROM; the port had one, and that is the report.

---

## 1. The ROM (bank CF; `tools/dis.py 0x0FE830 0x60` and `0x0FE880 0x50`; DP = `$1D00`)

```
FE83F  LDY #000C                       ; render objects 0x0C .. 0x16  (obj = cmb + 0x0C, combatants 0..10)
FE842  LDX #0000                       ; X = list cursor
FE845  LDA $96E9,Y / BEQ ->FE86A       ; PRESENT? ($96E9+0x0C = $96F5[cmb] -- the same present array the
                                       ;   enemy-name builder reads as $96F8 = $96F5[3+slot])
FE84A  LDA $1D17,Y / STA $8A,X         ; ** THE SORT KEY = $1D17,obj = $1D23+cmb = the combatant's LIVE
                                       ;    SCREEN Y ** -- the very byte CF/EAF2 uses as the compose baseY
FE84F  TYA / ORA #40 / STA $A1,X       ; id[X] = obj | 0x40   (bit6 = the body entry)
FE854  INX
FE855  LDA $A5C1,Y / STA $1D39,Y       ; a SECOND entry for this object when $A5C1 != 0:
FE85B  BEQ ->FE86A
FE85D  LDA #01 / STA $1D39,Y
FE862  LDA #FE / STA $8A,X             ;   key = 0xFE -> always sorts FIRST = always frontmost
FE866  TYA / STA $A1,X                 ;   id (bit6 CLEAR)
FE869  INX
FE86A  INY / CPY #0017 / BNE ->FE845   ; 11 objects
FE870  DEX / STX $30                   ; $30 = count-1
FE873  LDY #0000
FE87A  .outer: TYX
FE87B  .inner: LDA $1D8A,Y / CMP $8B,X / BCS ->FE897   ; key[Y] >= key[X+1] ? keep
FE882          <swap key[Y] <-> key[X+1]>  <swap id[Y] <-> id[X+1]>     ; else swap
FE897  INX / CPX $30 / BNE .inner
FE89C  INY / CPY $30 / BNE .outer      ; ** SELECTION SORT: position Y ends up holding the MAXIMUM of the
                                       ;    remainder -> the list is DESCENDING in Y **
FE8A1  LDA #FF / STA $1DA2,Y           ; terminator
FE8A6  LDY #0000 / STY $C5 / STY $C7 / STY $C3
FE8AF  LDY #0010 / STY $C9             ; ** the OAM cursor starts at ENTRY 4 ** (entries 0-3 are the
                                       ;   bracket / target hand -- which is why those are always in front)
FE8B4  loop: LDX $C3 / LDA $A1,X / ... ; walk the sorted list; each entry composes its cells at $C9 and the
                                       ;   compose advances $C9 by 0x10 per 4-sprite block (CF/EABD, EB7F)
```

Ties: the compare is `key[Y] >= key[X+1] -> keep`, so **equal keys preserve the build order** — the LOWER
object index stays in front.

## 2. Oracle proof (four fights, exact)

`--peek <state> OUT [--btnscript ...]` writes `OUT.oam` + `OUT.wram`; `tools/ct.py oam` decodes the blocks
and `$1D23+cmb` comes from the same capture's WRAM. Each combatant's tile base is fixed for the fight
(`battle_animeng_builder.md` §4: Crono 0x08-0x0E, Marle 0x20-0x26, seat2 0x28-0x2E, enemies 0x40+), so the
blocks identify themselves.

| fight | `$1D23` of the present objects | expected (desc Y) | OAM blocks 4-7, 8-11, 12-15, … |
|---|---|---|---|
| forest f100 | Crono 71, Marle 144, cmb3 67, cmb4 95, cmb5 127 | 1, 5, 4, 0, 3 | Marle, 060, 048, Crono, 040 = **1,5,4,0,3** ✓ |
| forest, Crono mid-attack (`100:4,0:8,100:4,0:60`) | Crono 71, Marle 144, cmb3 **63**, cmb4 95, cmb5 127 | 1, 5, 4, 0, 3 | Marle, 060, 048, **Crono, 040** ✓ — the attacker draws IN FRONT of the enemy it is standing on |
| GATO (`gato_charm2`) | Crono 147, Marle 111, GATO (cmb6) 118 | 0, 6, 1 | Crono, GATO (12 cells), Marle ✓ — **GATO draws in FRONT of Marle** |
| bat9 3-PC (`bat9_lake`) | 0:128, 1:124, 2:128, 6:134, 7:127, 8:143 | 8, 6, 0, 2, 7, 1 | 088, 068, Crono, seat2 (pal7), 080, seat1 (pal6) ✓ — including the **0-before-2 TIE** |

## 3. What the port did, and the fix

`src/field.c` composed a FIXED layer stack: `GATO → notes → Hetake → generic enemies → dissolve → [FG punch]
→ PC stance → entrance → Crono idle → Crono swing → Marle swing → victory pose`. So every PC beat every
enemy in an overlap — and GATO's own fight, where the oracle puts him in FRONT of Marle, was wrong the other
way round.

Shipped:

- **`battleObjOrder()`** — the §1 build + selection sort, key `h->mvLiveY[cmb]` (our port of `$1D23+cmb`;
  `mem[0x1D23+cmb]` when the mover has not staged yet, e.g. the `CT_BATTLE_RENDERWRAM` vehicle).
- **`battleCombatantsRender()`** — ONE walk over that list, back-to-front (our compositor paints later on
  top, the SNES puts the lowest OAM index on top), offering every per-combatant layer each combatant in
  turn; each layer filters itself on `BATTLE_ZSKIP(cmb)`. The per-combatant call ORDER inside one iteration
  is unchanged (stance → entrance → idle → swing → victory pose) because those are LAYERS OF ONE combatant,
  not separate objects.
- The interactive compose, the headless `CT_BATTLESHOT_AT` hook and the `CT_BATTLE_RENDERWRAM` vehicle all
  call it, so the three agree — renderwram used to draw no GATO / Hetake / dissolve at all (bat3's pinned
  frame had NO enemies in it).
- `CT_BATTLE_ZTRACE=1` prints the list whenever it changes: `zorder f400: 8(143) 6(134) 0(128) 2(128) …`.

The **FOLIAGE (FG) punch** moved from "after the enemies, before the PCs" to after the whole combatant pass:
with the two interleaved there is no longer a moment "after the enemies", and the hardware treats them alike
anyway — PC and enemy cells carry the SAME OBJ priority in every capture (forest all prio 2: Crono attr
0x2A, Marle 0x2C, Hetake 0x22; GATO and bat9 all prio 3: 0x3A / 0x3C / 0x32). The prio-3 bracket/hand still
draws after the punch, as it must.

## 4. OPEN

- **`$A5C1` — the always-front second entry.** Set for **bat2's Avian Chaos** (`$A5C1[obj 0x14] = 0x80`, a
  FLYING enemy) and zero for every walker in every other capture; its extra entry's key is `#FE`, so the
  object is hoisted to the front, and bat2's OAM indeed starts with the Avian's block although its Y (111)
  would place it fourth. **Not modelled**: nothing in the ROM statically writes `$A5C1` (`tools/ct.py refs
  0xA5C1` finds no store; the boot only CLEARS it, writer CF:FB9A), so where the flag comes from — and what
  the two entries emit differently (bit6 of the id byte) — is underived. Our list puts such an object at its
  Y position: right for every non-flying formation, one slot too far back for a flyer.
- **The bit6 id flag** (`obj | 0x40` on the body entry) is carried into the emit loop at FE8B4-FE8CF and not
  decoded here.
- **`$96F5` presence** is the ROM's list gate; the port does not model that array, so `battleObjOrder` sorts
  all 11 and lets each renderer decide whether it draws anything. That cannot change the ORDER of the
  objects that do draw.
- The **effect objects** (whirl, ice, song notes, damage popups) also live in OAM
  (`battle_tech_cyclone_fx.md` observes the whirl at oidx 8, i.e. inside the same entry-4-up space the
  combatants are allocated from) and are NOT part of this list; the compose loop at FE8B4 walks combatant
  entries, and where the effects are inserted is underived. They keep their fixed compose slots — with one
  forced move: the song notes used to be drawn between GATO and the other enemies, which is no longer a
  moment that exists, so they now draw immediately after the whole combatant pass. In GATO's fight (the
  only one with notes) they float above him and above nothing else, so the two placements render the same;
  a formation where a note passes over a nearer combatant would tell them apart, and none is captured.

## 5. The OBJ priority against the field's priority tiles (2026-09-26, user defects #13 / #18)

The per-fight priority the section above calls uniform is `$A5E3 = ($2989 & 10) | 20` (CF/FBAE), copied to all
eleven combatants' `$99B5,X` (and the restore copy `$9F3B,X`) by CC/E3F9, and OR'd into every cell's attribute by the
compose (CF/EB1E `$9930,X | $99A9,X`, X = cmb + $0C). The anim-script ops at C1/7555 (`LDA $80 / STA $99B5,X`, the
role's combatant or the `$A018` list) and C1/7595 (`$9F3B,X -> $99B5,X`) change and restore it per role. Forest: 20;
GATO and Manoria's cathedral: 30 (oracle, m81_nuns_lake: `$A5E3 = 30` by CF/FBB8 at f392).

Mode 1 layers OBJ priority 3 in FRONT of BG1/BG2 priority tiles and 2 behind them. The port's FG punch drew every
field priority tile over every combatant, whatever its priority -- the Naga-ettes went behind the altar's table and
the PCs behind the cathedral's floor tiles. `battleCombatantsRender` now records, per pixel, the priority of the
combatant whose cells won it (the pixels it changed, painted back to front), and the punch skips a pixel won by a
priority-3 cell. `CT_BATTLE_PRIOTRACE` counts the cell pixels over priority tiles (`in front` / `covered`); check
`battle-obj-priority-3-over-priority-tiles`. Not graded against the cartridge's picture: the cathedral fixture is an
s9x splice (no VRAM), and no attract fight puts a priority-3 cell over a priority tile. Open: an effect object carries
its own priority (`$AB5F,X`, the same default) -- the port records the combatant's for the effect cells it draws in the
same iteration.
