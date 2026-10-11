# op 0x80 — the party-aware PC loader (C0/421E) + C0/448D

Status: **PORTED AND GRADED** (`--sweepops` 80:60, all six arms reached, ten mutations red).
Originally landed as derivation-only; this file is the reference for the implementation in `move.c`. Every address, constant and branch below was read from the
ROM with `tools/dis.py` or from raw bytes; nothing here is inferred from a sibling routine. The
byte-diff in §3 is the load-bearing evidence and was measured, not asserted.

Ranked **1062 static fn-reachable sites** (`python tools/ophist.py`), the top self-contained
unported opcode: 0xE8 (3017) is measured blocked on bank $C7, and 0x83 (1471) needs the unported
VRAM/DMA upload C0/E12A.

Dispatch: `$C0:5D6E + 0x80*2 = $5E6E` → **C0/421E**. (0x81 → C0/4476 is the always-NPC loader; the
memory note [[ct-vm-party-ops]] records that confusing 0x80 with 0x81 once broke Marle walking
439→7, so the two must not be merged.)

---

## 1. Shape

Two bytes, `SEC` on every path, and **no yield**. It never parks.

```
421E INY / TYX / LDA $7F2001,X / 4224 STA $D9      ; operand = the PC id, staged at $01D9
4226..4268  a SEVEN-WAY dispatch on the id, 0..6:
              id 0 -> STA $8D   id 1 -> $8E   id 2 -> $8F   id 3 -> $90
              id 4 -> $91       id 5 -> $92   id 6 -> $93
            each stores $6D (this object's index) — so $018D+id is "which object slot
            holds PC id". An id > 6 stores NOTHING and falls straight through.
426A LDA $D9
426C CMP $7E2980 / BNE       -> in party slot 0 -> A=0, 429C
4276 CMP $7E2981 / BNE       -> in party slot 1 -> A=1, 4310
4281 CMP $7E2982 / BNE       -> in party slot 2 -> A=2, 43C3
428C NOT IN THE ACTIVE PARTY:
       $1100,X = $80 (inactive) / $1101,X = id / TYX INX SEC RTS   -> ip+2
```

`$7E2980-2982` is the active-party array ([[ct-vm-party-ops]]: op 0xD2 = membership in
`$7E2980`).

### The IP is ip+2 on every arm, and that needed checking

The three in-party arms end `TYX / SEC / RTS` with **no `INX`** (4305, 430D, 4387, 43B8, 443A…),
which read alone would make this a one-byte opcode that re-executes its own operand. It is not:
`C0/448D` does `4490 INY` and spills with `4496 STY $C7`, then **restores it at `4552 LDY $C7`**
before returning. So Y is ip+2 when the arm does its `TYX`. The not-in-party arm never calls
448D, so its Y is still ip+1 and it supplies the missing `INX` itself at 4299. Both give ip+2.

---

## 2. The three party arms

Identical skeletons; everything that differs is in the table.

```
LDX $6D / STA $1100,X          ; state = the slot number (0, 1 or 2)
STZ $1C80,X                    ; clear the per-object collision gate
$1A81,X = 1                    ; visible
$1B01,X = 1                    ; solid
$1101,X = $D9                  ; the PC id
STX <spill>                    ; 42B3 / 4327 / 43DA
JSR C0/448D                    ; the PC descriptor load
LDX $6D
<the $78 block — slots 1 and 2 only>
STX <slotTable>                ; 42C4 / 4339 / 43EC
$0D00,X = <oamBase>  (16-bit)  ; 42CC / 4341 / 43F4
$1700,X = <tplBase>  (16-bit)  ; 42D2 / 4347 / 43FA
$1000,X = ($1000,X & $80) | 1  ; 42D7 / 434C / 43FF
<placement>
```

| | slot 0 (429C) | slot 1 (4310) | slot 2 (43C3) |
|---|---|---|---|
| spill | `STX $97` → $0197 | `STX $99` → $0199 | `STX $9B` → $019B |
| slot table | `STX $0B89` | `STX $0B8A` | `STX $0B8B` |
| `$0D00,X` | `$0008` | `$0020` | `$0028` |
| `$1700,X` | `$0000` | `$0020` | `$0040` |
| `$78` block | **absent** | present | present |
| placement | always the default | follow leader, else default | follow leader, else default |

**The `$78` block** (432E / 43E1) — `LDA $78 / BEQ skip / LDA #02 / STA $78 / STZ $1601,X`.
`$0178` non-zero is forced to 2 and the anim frame-timer is cleared.

**`STZ $AB / $AC / $AD`** (4356 / 4409) — slots 1 and 2 only: the party path-history ring pointer
([[ct-field-party-follower]]; `$AB` is the `INC A / AND #7F` cursor C0/9E29 advances).

**Follow-the-leader** (435C / 440F) — `LDA $97 / BMI default`. `$0197` is the slot-0 arm's own
spill, i.e. the leader's object index, and **bit 7 set means "no leader"**. Otherwise copy the
leader's `$1800` and `$1880` (16-bit), `STZ $1900,X`, and copy `$1600,X` (facing).

**The default placement is one routine reached three times** — 42BA-430F, 438A-43C2, 443D-…
byte-for-byte the same:

```
$1801,X = $0102 ;  $1881,X = $0103 ;  $1600,X = $0104 & 3
$0104 bit 3 ?  $1880,X = $7F  :  $1880,X = $FF
$0104 bit 2 ?  $1800,X = $00  :  $1800,X = $80
```

`$0102/$0103/$0104` are **not** filled by C0/448D — that was this file's original guess and it was
wrong. They come from the ChangeLocation handler **C0/3B1B**, so they are *inputs* to op 0x80; see
§5. The `$FF`/`$80` literals are the same tile-foot / tile-centre convention op 0x8B uses.

---

## 3. C0/448D is the object loader's descriptor walk with a different table base

**Measured, not assumed.** Aligning the two at their `LDX $4216` (`AE 16 42`) — NPC C0/45A3,
PC C0/449B — and comparing 140 bytes gives **18 differing bytes**, and they account completely:

| offset | NPC | PC | meaning |
|---|---|---|---|
| +6 | `24` | `01` | `$E4F024,X` vs `$E4F001,X` |
| +41 | `25` | `02` | |
| +74 | `26` | `03` | |
| +115 | `27` | `04` | |
| +126 | `23` | `00` | the record base itself |
| +88, +90 | `88 … 38` | `38 … 88` | `DEY DEY SEC` vs `SEC DEY DEY` — **inert**, SEC does not touch Y and DEY does not touch carry |
| +129…+139 | — | — | where the tails genuinely part |

So the record base is **$E4:F000** for PCs and **$E4:F023** for NPCs, and `$23 = 35 = 7*5` is
exactly the seven PC records the NPC table starts after — which is what `CtObjLoad`'s existing
comment already says about `$F023 = $F000 + 7*5`, now confirmed from the other side.

**The tails differ and this is the real work.** The NPC loader calls the C0/5C90 dedup and
`JSL C3/0002` decompression; the PC loader does none of that. From 451C:

```
451C  $E4F000,X -> *3 -> [$AF],Y -> $1280,X   (16-bit: the graphics address)
4532  INY INY / [$AF],Y -> $1200,X            (the graphics bank)
453B  $0C00,X = $2020   (16-bit)
4543  $1680,X = 0 ; $1681,X = 0 ; $1600,X = 1
4552  LDY $C7 / TYX / SEC / RTS
```

`C0/448D` runs 448D..4556 — 202 bytes, ending immediately before C0/4557, the NPC prologue.

---

## 4. Implementation plan

1. Factor `CtObjLoad`'s descriptor walk (the part from `bf`/`rec` through the `$1201,X` class
   byte) into a helper taking the **record base**, and call it with `0x24F023` from `CtObjLoad`
   and `0x24F000` from the new `ctPcLoad`. §3 is what licenses sharing it; do not share the
   tails, which genuinely differ.
2. `ctPcLoad` = that walk + the 451C-4556 tail above. It must also write the `$01C7` spill
   (4496) and the DP scratch the walk makes, the way `CtObjLoad` now does.
3. `case 0x80` = the 7-way dispatch + the four arms, with the default placement as one helper.
4. `--sweepops`: **six** arms to reach and count (id>6 / not-in-party / each of slots 0,1,2 /
   leader-present vs leader-absent), tallied from the ORACLE's post-state and failing on a zero,
   the way op 0x04's are.
5. Seed and **poison** deliberately. Every output this touches — `$018D-$0193`, `$0197/$99/$9B`,
   `$0B89-$0B8B`, `$0D00`, `$1700`, `$1000`, `$1600`, `$1800/$1801`, `$1880/$1881`, `$1900`,
   `$1101`, `$1C80`, `$1A81`, `$1B01`, `$0102-$0104` — must be poisoned, not merely compared.
   The mode's own poison-audit line will say if any were missed; it found a real defect in
   C0/5B95 exactly this way.
6. Mutation-test at minimum: the id>6 no-store arm, each per-slot constant in the §2 table, the
   `BMI` leader-absent polarity, `$0104` bits 2 and 3, and the ip+2 that §1 resolves.

## 5. Resolved and still open

**RESOLVED — `$0102/$0103/$0104` are not written by C0/448D at all.** They are the *arrival
staging* laid down by the **ChangeLocation handler C0/3B1B**: `3B2C LDA $7F2001,X / 3B30 STA $02`
is a 16-bit store of the arrival tile into `$0102/$0103`, and `3B34 LDA $DA / LSR / AND #0F /
3B39 STA $04` plus `3B3D LDA $DA / AND #80 / 3B3F TSB $04` builds `$0104` — facing in bits 0-1,
bit 7 carried separately. So they are **inputs** to op 0x80, which is what makes the opcode
placement-free on its own, and in the sweep they are seeded rather than poisoned.

**RESOLVED — the follow arm uses TWO index widths.** `4362 AND #00FF / 4365 TAX` zero-extends
for the `$1800` read, but `436E LDX $97` and `437D LDX $97` are **16-bit** and read `$0197` *and*
`$0198`. They coincide only because the 16-bit `STX $97` at 42B3 guarantees `$0198 = 0`. The port
models both widths rather than collapsing them, since collapsing is an assumption about `$0198`
rather than a reading of the instruction.

**A LIMIT OF THE HARNESS, worth knowing before "fixing" it:** `$0198/$019A/$019C` cannot be
graded by `--sweepops`. They are written (0, by the 16-bit spills) *and* read (as the high half
of those `LDX`), so poisoning them non-zero sends the ROM to `$1880 + $5A04` — a bank-0 address
above `$2000`, i.e. MMIO/ROM, which a flat 128 KB WRAM image cannot represent. They sit in the
mode's poison-audit residue as `80:3` deliberately.

### Still open

- `$0178`'s meaning (the `$78` block) is unread — only its effect is recorded here.
- `$0178`'s meaning (the `$78` block) is unread — only its effect is recorded here.
- Whether `$0B89-$0B8B` here and the OBJ slot allocator's `$0B88` table
  ([[ct-objpal-allocator]], `disasm/notes/objpal_allocator.md`) are the same table indexed
  differently. They look adjacent; that is not evidence.

---

# The two COLLISION bytes every loader arm writes (2026-09-06)

User: *"when pressing the button to ring the bell, crono steps around marle on the way, unlike the original"*,
with their own F5 debug state (`build/ct_debug.state`).

## 1. What the state showed

Scene 5, Crono mid-script at ip `0x05C6` -- the bell's return leg, `96 05 24` (op 0x96, MoveTo tile
5,36) -- with Marle parked on the destination tile (88,591). Ours walked it as

```
f0 (88,619)  f3 (94,601)  f6 (99,587)  f9 (93,580)  f12 (90,583)  f15 (88,586)  f21 (88,591)
```

a swing out to x = 99 and back. `CT_SOLIDLIVE` reported **5 deflections**. The ORACLE walks the same
leg with x pinned at **88.0 on every frame** (`--monitor 7E1802:2,7E1882:2 --from
saves/field_re/s5_bell_marle.state`): y 634.6 / 627.1 / 619.7 / 612.2 / 604.8 / 597.4 / 589.9 / 590.9
/ 591.9. It never steps sideways.

## 2. The ROM: three stores, three arms, one prologue

`C0/426A` routes a PC load by char-id vs `$7E2980/81/82` to one of three arms, and all three open with
the same three stores **before** the shared `JSR C0/448D` descriptor tail:

| | slot 0 | slot 1 | slot 2 |
|---|---|---|---|
| `STZ $1C80,X` | 42A1 | 4315 | 43C8 |
| `LDA #01 / STA $1A81,X` | 42A6 | 431A | 43CD |
| `LDA #01 / STA $1B01,X` | 42AB | 431F | 43D2 |

The NPC arms make the third one too, around the same tail: op 0x81 at `C0/447D-447F`, op 0x82 at
`C0/459E-45A0`, op 0x83 at `C0/46ED-46EF`. And `$1B01` has exactly one more writer in the whole ROM --
op 0x84, `C0/4867`: `INY / TYX / LDA $7F2001,X / LDX $6D / STA $1B01,X`, the whole operand byte.
(A ROM-wide scan for `STA $1B01,X` / `STZ $1B01,X` returns those seven plus `C0/58ED`, the location
loader's clear, and two in bank F1.)

## 3. What the port had

- `$1C80` was written on the **op-0x80** entry only. The op-0x57/0x5C/0x62 entry -- the one scene 5
  uses (`57 00`) -- set the C field `a->coll = 0` instead, and that field reaches WRAM exactly once,
  at `CtSceneCreate`, **before** the init pass runs. So a lead loaded by op 0x57 kept the seed's
  leftover `0x03`.
- `$1B01` was written only for MONSTERS (`loadMonMirror`). A plain NPC read 00 where the oracle reads 01.
- op 0x84 assigned only `a->solid`, never the byte.

`$1C80` bit1 is the gate `C0/5BA0` tests and `$1B01` bit0 the candidate test `C0/5BDB`. With the lead's
bit1 set, the LEAD PC entered the object-solid scan and `npcMoveAngle` deflected its heading by +0x30
around Marle -- which is the detour, exactly.

## 4. After

`pcSlotArmCommon` makes the three stores for every PC load whichever opcode entered; `npcLoadMirror`
makes the NPC arms' `$1B01 = 1`; op 0x84 writes the byte in both the init pass and the live VM.
Fresh `--scene 5` with the party carried, against the oracle's own row: **0 of 29 objects differ on
$1C80 or $1B01**. The bell's return leg runs with x pinned at 88.

## 5. Why nothing was red

`field-solid-liveness-fair` and `field-solid-party-arms-226/399` already pin `reached=0 deflected=0`
for a party -- but in those runs nothing drives the LEAD through a scripted move, so it never calls
`npcMoveAngle` and its gate byte is never read. And deleting BOTH of op 0x84's WRAM stores was a
mutation the whole 378-check suite survived (measured: rebuilt without them, 378 pass, 0 fail).

Pinned now by `field-pc-slot-arm-stores` (a REAL location load via `CT_CHANGELOC` onto the bell tile,
then the minigame -- a `--loadwram` fixture seeds `$1C80` from the oracle image and cannot grade the
loader at all), `field-npc-loader-solid-byte`, and `field-op84-solid-byte`. Three mutations, all
KILLED. `CT_DUMPOBJROW_AT=<frame>` was added for the third: the racers' `84 00` lives in a runtime
function, so the load-time row cannot see it.
