# BATTLE — the enemy-death DISSOLVE-vs-STAND gate + the TRUE Hetake dissolve color (both re-derived)

Closes two user reports and re-scopes a rule our engine over-generalized:
1. **"the last of the hetake is not doing the dissolve"** — our engine makes the LAST (fight-ending) Hetake
   **stand** (deadStanding), but the oracle **dissolves** it. The stands-vs-dissolves distinction is
   **PER-ENEMY, not per-kill-position** — GATO is the special "stands" machine, not "the last kill."
2. **"the colour of the hetake's dissolve is still wrong"** — our baked `ctDissolvePal4Forest` (all dark red)
   is a **low-byte CGRAM misread** (the ADDENDUM-X2 "fix" was STILL wrong); the real dissolve pal4 is a
   red/orange **+ white + pink** palette, and it is **DYNAMIC (faded at death), not a boot constant**.

Method: **`--peek` is authoritative for the RENDERED death** (per every prior battle dossier — `--monitor`
fast-forwards the `$A013`-paced post-kill phase); `--monitor --writer` for RAM sequencing/writer PCs; `dis.py`
for ROM. OBJ 16×16 4bpp, CGRAM `128+pal*16+v`, `(v<<3)|(v>>2)`, OBJ px at OAM (x,y) → fb (x,y+8).
CGRAM word = **2-byte BGR555 LE stride** (`cg[i*2]|cg[i*2+1]<<8`) — the single-byte read is the recurring
bug (see §2.4). States: `saves/battle_re/forest_battle_3hetake.state` (in-battle, 3 Hetakes),
`saves/battle_re/forest_holdleft_pre.state` (pre-battle), `gato_kill.state`/`gato_press_action.state` (GATO).
Peek observable = enemy OBJ cells (tiles 0x40-0x77 Hetake / 0x06+0x68-0x8F GATO), by palette, + alive-list
`$AF02`, from the `.wram`/`.oam` sidecars. Scratch driver `dgate.py`/`probe.py`/`forest.py`/`color.py`.

---

# PART 1 — THE GATE: dissolve iff the enemy's ANIM-SET plays a death-dissolve (GATO's does NOT)

## §1.1 OBSERVED — the LAST Hetake DISSOLVES (peek, forest, mash-A)

`forest_battle_3hetake` + A-mash, peek sweep (enemy OAM cells by pal + `$AF02`):

| peek F | `$AF02` (alive) | last-enemy OBJ | reading |
|---|---|---|---|
| 620–720 | `FF FF 5E` (slot2 alive) | n=4 **pal1** | Hetake alive, green idle |
| **740** | `FF FF FF` (**all dead**) | n=4 **pal4** | death recolor: cells flip **pal1→pal4** |
| 760 | all FF | n=4 pal4 | pal4 held, tiles dither-clearing |
| **780** | all FF | **n=0** | OBJ **dropped** (fully dissolved, gone) |

All three Hetakes dissolve **identically** (slot0 pal4 @F=200, slot1 @F=400, slot2 @F=740) — the
**fight-ending kill dissolves too**. Rendered proof `LAST_HETAKE_dissolve.png` (F=720/740/760/780): green
mushroom → **red/pink recolor** → red **dither-out** (the S3 8×8 matrix) → gone. This is the standard
`battle_dissolve.md` dissolve, on the LAST enemy. **The user is right.**

## §1.2 CONTROL — GATO STANDS (same peek observable)

`gato_kill.state` (GATO 6 HP) + A-mash, and a fresh-boot HP=8 control (`gato_press_action` + patched-HP ROM):

| peek F | `$AF02` | GATO OBJ | reading |
|---|---|---|---|
| 40–120 | `…92…` (alive) | n=12 pal1 (F=80 pal4 = **hurt-flash**, transient) | GATO alive |
| **160** | all FF (**dead**) | **n=12 pal1** | removed from alive-list but **still drawn, pal1** |
| 200–360 | all FF | **n=12 pal1 (static)** | **STANDS** — no pal4 flip, no dither, no drop |

GATO's 12 cells persist byte-static through the whole victory (matches `battle_gato_idle_victory.md §B.1`).
**GATO never dissolves; the Hetake always does — including its final kill. The gate is per-ENEMY.**

## §1.3 The death PATH is IDENTICAL for both (so the gate is downstream, in the anim engine)

Enemy-death code path (`dis.py`), same for GATO and Hetake:
- **Clamp-death** (`C1/ECC7`, `battle_hp_timing.md §b`): dead-flag block+0x1D bit7; **enemy → `JSL FD/ABA2`**.
- **`FD/ABA2`** (`dis.py 3DABA2`) = **reward bookkeeping ONLY** — `$B28C += CC5E00,X` (XP), `$B2A5 +=
  CC5E02,X`, `$B2DB += CC5E06,X` (item) + a steal roll. **No OBJ/VRAM/CGRAM write.** (Confirms
  `battle_dissolve.md S1`.) Not the visual gate.
- **Enemy-death routine `C1/B2B1`** (`dis.py 1B2B1`): set dead-flag `$5E4A,X=#80`; **clear alive-list
  `$AEFF,Y=#FF`** (@`C1/B2C0`, writer **`C1:B2C3`**, `$AEFF+slot`=`$AF02+slot`); clear status; then
  **`CPY #0003` → `BCC` PC(idx<3) / else JMP `C1/B3A0`** (enemy path).
- **`C1/B3A0`** (`dis.py 1B3A0`): sets the **death-notify `$AEB0,Y=1`** (+global `$AEB2=1`), **gated**:
  `LDA $AE82,Y / BNE skip` and `LDA $AF12,Y / BIT #80 / BNE skip`.

**Oracle check of that gate (`.wram` at the death frame, GATO vs Hetake):** `$AE82`, `$AF12`, `$AEB0` are
**identical** between GATO (stand) and Hetake (dissolve) — both pass the gate and get the death-notify
(`$AF12`=`FF FF FF 00 00 00 00 00` in BOTH fights; `$AE82` transient 01s appear for both). **So the
death-notify path does NOT distinguish them.** The dissolve-vs-stand decision is **downstream**: how the
**bank-C1/CC animation engine responds** to a dead enemy — it plays the dither-dissolve for a normal enemy
and holds the idle pose for GATO. That response is selected by the enemy's **battle animation SET**.

## §1.4 The SELECTOR — the enemy gfx/anim record `$E4/F600 + id*10`, byte4 = anim-set

`$E4/F600` (10 B/enemy, the field-LOADMON + battle gfx/anim record; `gato_script.md`, byte4 = **anim-set**):

| enemy | id | `$E4/F600` record | byte4 anim-set | death |
|---|---|---|---|---|
| Hetake | 5E | `70 4E 6E 30 **00** 00 F8 10 10 41` | **0** | dissolve |
| Beetle | 8D | `5C 31 29 90 **00** 00 F8 08 10 61` | **0** | dissolve |
| Poly | 11 | `56 3F 40 2C **00** 00 F0 07 0E 41` | **0** | dissolve |
| Avian Chaos | 19 | `55 29 1B 20 **C0** 00 D8 02 10 41` | **0** (+flags 0xC0) | dissolve |
| **GATO** | 92 | `7B 76 B6 57 **02** 00 D0 10 10 41` | **2** | **STAND** |

**All four dissolving enemies = anim-set 0; GATO = anim-set 2.** The anim-set selects the animation-script
set the C1/CC engine runs, **including the death animation** — anim-set 0 has the standard dither-dissolve
death, GATO's anim-set 2 has a hold-in-place ("stand") death. This is the **derived gate**.

## §1.5 RULED OUT / boundary (so the derivation is honest, not correlation)

- **Enemy STAT record `$CC:4700 + id*23` — RULED OUT (patch experiment).** The only non-stat bytes that
  differ (Hetake `…00…00` vs GATO `+4=FF, +22=01`) were cleared in a ROM copy; GATO booted **fresh** from
  `gato_press_action` (HP patched to 8 → block loaded **8**, proving the fresh CC/4700 read took), and GATO
  **still stood** (n=12 pal1 through F=900). +4 is nonzero for 124/192 enemies (a stat, not a flag). So
  the stat record is NOT the gate.
- **E4/F600 anim-set — CANDIDATE, could not be PATCH-confirmed (scene-load staging).** The enemy gfx/anim
  is staged into RAM at **scene-load** (field op-0x83 LOADMON), BEFORE any available savestate. A ROM patch
  to `$E4/F600` does not reach the already-staged enemies — proven: GATO given **Hetake's entire E4/F600
  record** still rendered as the 12-cell GATO and still stood. No pre-scene-load state exists to re-stage
  from, so the anim-set→death-anim link stays a strong correlation (4 dissolving enemies=animset0 vs
  GATO=animset2) + the ruled-out alternatives, not a patched proof.
- **The C1/CC anim-engine death dispatch itself = the standing BOUNDARY** (`battle_stance.md S4` /
  `battle_interp_verify.md`): the code that reads the anim-set and plays dither-dissolve-vs-hold does not
  linearly disassemble (M/X desync). The **observable** rule (per-enemy, anim-set 0 dissolves / GATO holds)
  is the deliverable; the exact branch is the boundary.

**GATE VERDICT:** *An enemy dissolves on death iff its battle animation-set plays a death-dissolve.* The
dissolving enemies (Hetake/Beetle/Poly/Avian) use **anim-set 0** (`$E4/F600+4` low-nibble 0) and dissolve on
**every** death incl. the fight-ender; **GATO uses anim-set 2** and holds his idle pose (stands). NOT
per-kill-position. Our engine's `anyAlive` gate (`battle.c:716`) is the wrong axis.

---

# PART 2 — THE TRUE HETAKE DISSOLVE COLOR (correct read + where our bake is wrong)

## §2.1 The rendered pal4 (steady), cross-validated against the framebuffer

Correct 2-byte-stride read of OBJ **pal4** (`0xC0..0xCF`) at the dissolve, identical across all three Hetake
deaths (peek F=200/400/760):

```
pal4 = 381E 08BE 7FFF 6B7E 5B1E 5EDE 3E7E 423E 257E 2DFE 197E 10DE 14FE FF1F FF1F FFFF
RGB idx0..7 = (247,0,115)(247,41,16)(255,255,255)(247,222,214)(247,198,181)(247,181,189)(247,156,123)(247,140,132)
```
= red/orange (idx1) **+ WHITE (idx2 = 7FFF)** + pinks/tans. **Cross-check (definitive):** the actual fb
pixels of the dissolving Hetake are `(148,41,16)`, `(206,156,123)`, `(214,140,132)`… = **exactly pal4 idx1/
idx6/idx7** at that frame. So this palette IS what renders. (The live pal1 idx0 = `0240` = green `(0,148,0)`;
pal4 recolors the greens to red, keeps the white/pink highlights → a **red-and-white fire sparkle**.)

## §2.2 pal4 is DYNAMIC — LOADED/FADED at DEATH, not a boot constant

pal4 is a **shared OBJ slot** whose content changes with the effect (peek, forest):

| frame | pal4 | what it is |
|---|---|---|
| pre-death | `0009 1486 6F9E 535E 2AFF 0D70 15F5 0CC8` | the **stance/entrance** palette (`battle_stance.h` pal4) |
| some pre-death | `0009 7FFF 7FFF 7FFF …` | the **hurt-flash** (all-white) |
| at death → | ramps → `381E 08BE 7FFF …` | the **death recolor** (fades in ~15 f, then holds) |

So pal4 is **NOT present/constant from battle start** (this refutes `battle_dissolve.md` ADDENDUM-X2's "pal4
present from START, constant"). It is produced at death.

## §2.3 The LOADER — palette cross-fade engine `FD/E962` (closes the "pal4 ROM source" OPEN)

`--monitor --writer` on the OBJ palette shadow during the Hetake deaths pins it:
- **WRAM palette buffers:** SOURCE/target `$7E2000` (pal4 at `$7E2180`, pal1 at `$7E2120`), **LIVE CGRAM
  shadow `$7E2200`** (pal4 at **`$7E2380`**, pal1 at `$7E2320`; DMA'd to CGRAM each frame).
- **Fade engine `FD/E962`** (`dis.py 3DE962`; per-channel variants E962/E98F/E9CA): reads `$7E2000,X`,
  interpolates with the **hardware multiplier** (`STA $4203` / `ADC $4217`), **`STA $7E2200,X`** (store
  retires as monitor writer **`FD:E989`** @`3DE985`).
- **On enemy death** the engine ramps pal4 (`$7E2380`) from the **live pal1 (green)** toward the **red
  recolor** over ~15 frames — f170→185: idx0 `380A→381E`, idx1 `08A6→08BE` — then holds. Identical for
  every Hetake death (f168 slot0, f365 slot1). During the dissolve the SOURCE pal4 (`$7E2180`) = pal1 with
  idx0 recolored red (`0009`); the fade adds a ramping red flash to reach the rendered `381E 08BE …` endpoint.

So the dissolve color is **a per-death palette fade** (live pal1 → red recolor via `FD/E962`), not a static
loaded palette. The **endpoint** (`381E 08BE 7FFF …`) is what our compositor must emit.

## §2.4 Where our extraction diverges (root cause)

Our `ctDissolvePal4Forest` (`field.c:2130-2131`):
```
000D 0000 00F5 0026 0033 001E 00EF 0019 008D 0015 002C 0011 002A 0011 0009 0011   ← WRONG
```
**Every word has high-byte 00** — impossible for a real 16-color palette with white/highlights. It is a
**low-byte CGRAM misread** (the same class of bug X3 blamed on the green value; the X2 "red fix" was itself
still a misread — different bytes, same 1-byte-stride error). It renders as all **dull dark red**; the real
pal4 is **bright red + white + pink**. Two divergences:
1. **Wrong bytes** (misread) → dull/dark, no white/pink sparkle → the "colour still wrong" the user sees.
2. **Wrong model** — a single STATIC bake vs a DYNAMIC per-death fade off the live enemy palette; our
   compositor has no live-battle-CGRAM model, so it cannot fade.

---

# PART 3 — IMPLEMENTATION SPEC (for the re-implement lane; NOT applied here, no src/ touched)

**Fix A — the deadStanding GATE (fixes "last hetake does not dissolve").** In `battleApplyDamage`
(`battle.c:711-724`) REPLACE the `anyAlive` gate with a **per-enemy-type** gate:
```
on enemy death:
    if (enemy id == 0x92 /* GATO, anim-set 2 = hold/stand death */)  -> deadStanding[slot]=1 (+ latch frame)
    else                                                             -> dissolveActive[slot]=1 (dither)
    // regardless of anyAlive: a non-GATO enemy dissolves on EVERY death, incl. the fight-ender.
```
This makes the **last Hetake dissolve** (and any non-GATO enemy) while **GATO still stands** (X5). Derivation:
GATO id 0x92 = `$E4/F600+4` anim-set **2** (the no-dissolve death set); Hetake 0x5E and the other forest
enemies = anim-set **0** (standard dissolve). Our engine only bakes GATO + Hetake, so **keying on id is exact
for both**; the general rule ("anim-set 2 ⇒ stand") is the ROM selector behind the anim-engine boundary.
The `deadStanding`/`deadStandFrame` latch (GATO idle-freeze, `battle_gato_idle_victory.md D.2`) stays — only
its TRIGGER changes from position (`anyAlive`) to identity (`id==0x92`).

**Fix B — the dissolve COLOR (fixes "dissolve colour wrong").** Replace `ctDissolvePal4Forest`
(`field.c:2130-2131`, the low-byte misread) with the correct steady dissolve pal4 (2-byte BGR555 LE):
```
381E 08BE 7FFF 6B7E 5B1E 5EDE 3E7E 423E 257E 2DFE 197E 10DE 14FE FF1F FF1F FFFF
```
The dither matrix (S3) + hold/rate (`battle_dissolve.h`) are UNAFFECTED — only the palette bytes were wrong.
`ctDissolvePal4Gato` is dead code (GATO never dissolves after Fix A) — leave, or fix for generality; it is
also a misread. **Optional byte-exactness:** fade pal4 from the live pal1 to this endpoint over ~15 frames
(mirrors `FD/E989`); the steady endpoint is the color fix the user asked for.

**No pinned regression signature changes** (same reasoning as `battle_hp_timing.md §d`): the battle-loop pins
(`field-battleloop-*` victory frame / attack count / HP) are functions of damage/turn-order/apply-at-span-end,
none touched by a dissolve-gate or palette change; `field-battlehud-gato` is a phase-0/f60 pre-death shot.
A NEW check that screenshots a mid-dissolve would capture the fix — pin only after it lands.

---

# PART 4 — OPEN

- **Exact dissolve-vs-stand ROM branch (anim-engine boundary).** The enemy **anim-set** (`$E4/F600+4`:
  GATO 2 vs dissolving 0) is the derived ROM selector, but the C1/CC anim-engine dispatch that reads it and
  plays dither-dissolve-vs-hold does not linearly disassemble, and it could NOT be patch-confirmed because
  gfx/anim is staged at **scene-load** (no pre-scene-load state to re-stage from). Confirmed by correlation
  (4 dissolving enemies=animset0, GATO=animset2) + ruling out `$CC:4700` + the identical death-notify path.
- **Deeper writer of the red-recolor TARGET** (`$7E2180` source pal4 = pal1 with idx0→red `0009`) at death —
  one level above the `FD/E962` fade; not traced. The fade **rate/factor** (idx0 `380A→381E` over ~15 f) is
  observed but the `FD/E989` multiplier math is not decoded byte-exact; the **endpoint is pinned**.
- **Other anim-set-2 enemies** (`0B 8E 8F 96 99 9B 9C BE`) presumably also stand — untested (no states).
- **Per-enemy dissolve pal4** — only the Hetake's is captured (derived from its green pal1). A general engine
  would fade each enemy's own pal1 → its red recolor; our bake is Hetake-specific (the only forest dissolver).
- **A byte-exact dissolve** needs a live-battle-CGRAM model (pal4 = shared dynamic slot: stance/flash/
  dissolve), which the compositor lacks; the interim baked endpoint is the color the user reported wrong.

---

# PART 5 -- THE RECOLOR IS DERIVED (2026-10-04): CF/FE20's copy + CD/00A5's fade record; PART 4's second bullet CLOSED

The "deeper writer" and the fade law of PART 4 are two ROM routines and an objslot record the port already ran:

- **CF/FE20** (C1/AC75 -> C1/0045 entry 6 = C1/006D `JSL CF/FE20`, the post-action death scan; dis.py `0FFE20 70 1 0`).
  For X = 10 down to 3 with `$AEB0,X` set: `$9FF7,X` set -> only `STZ $96F5,X`; else `INC $A0A8,X`, `STZ $AEB0,X`,
  `STZ $98A4,X`; `$99B5,X` == 0 -> `STZ $96F5,X`; the record's byte +7 (`$29C6 + (X-3)*12`) bit 6 -> skip; else
  `Y = $993C,X * 16`, **`$993C,X := 08`** (OBJ palette 4), **24 bytes `$2302,Y` -> `$2382` and `$2182`** (colours 1-12
  of its own palette), and `JSL CD/0024` = CD/0043, the corpse's whole death routine, INSIDE the loop -- which is why
  the corpses go one at a time, highest index first (battle_death_dissolve_order.md).
- **CD/0043**: the `$CCEA` wait (one frame at least), the #95 post (D0093), `$CD01 = 7`, then **CD/00A5**: with
  `$99D2 == 0`, CE:EFE1-EFE9 -> `$CCD9..$CCE1` = `40 C0 0F 10 00 01 01 00 FF` and `$CCE5 |= 80`; the NMI's CD/0C4F
  makes it record 10 (`$0598`) the next frame, and FD/E39C runs it: mode 4 sub-mode 1 over CGRAM `$C0` + 15 = OBJ
  palette 4's colours 0-14, acc `10` -> `F0` by `10` a frame, then `FF` and off -- each colour's red pulled toward 31,
  `r + (31 - r) * acc / 256` (idx 1 `A508`: r 5 -> 6 8 9 11 13 14 16 18 19 21 22 24 26 27 29 30). The dither loop
  (`$CCFE` 0..1F, CD/010A, CD/04AA) runs alongside.

Oracle, the rule (saves/battle_re/forest_battle_3hetake.state, A mashed, `--monitor 7E993F:3,7E2380:32,7E0598:12
--diff --writer`): `$993F = 08` (CF:FE5C) and `$2380 = 0038 A508 FF7F 7C6B ..` (CF:FE6B) f161, the #95 post f162,
`$0598 = 40 C0 0F 10 ..` (CD:0C5D) and the first fade f163 (FD:E989), the last f178 with the record off (FD:E849) --
the end state is the old `ctDissolvePal4Forest`. The Roly Rider (saves/battle_re/roly_rider_battle.state) does the same
from its own palette (`A508 FF7F B957 4F3E ..` at f324): it ends `BE08 FF7F BE57 5E3E ..`, not the Hetake's colours.

**Port** (src/battle.c battleDeathPalCopy + battleDeathSfxTick; src/field.c battleDissolveLivePal): the copy runs at
the corpse's routine start (the frame before its #95 post), the record is staged at the post, and the existing commit
and objslot service fade the live page; the dissolve draws with the palette its `$993C` names, read from the CGRAM
the NMI uploaded (`h->fxCgram`). The forest fixture (`--battletest forest_pre.raw.wram`, mash A) and the Roly
ambush both give `$2380` byte-identical to the cartridge from the copy to the record's end. The baked target, the
linear ramp (CT_DISSOLVE_FADE_FRAMES) and the misread ctBattleDissolvePal are gone.
~~**Still open:** the dither's own timing (the port's HOLD 9 + 32 steps against CD/0043's wait + the `$CCFE` loop).~~
CLOSED 2026-10-15: CD/0043 frame by frame (battle_death_dissolve_order.md, last addendum) -- the copy on the delist's frame,
the post +1, CD/00BF's ten waits, one level a frame, the return at +43.

# PART 6 -- THE WHITE FRAME AT THE DEATH (2026-10-07): the corpse reads the attribute the PICTURE carries

User: *"when the enemy is about to do the dissolve, it flashes and sometimes even teleports to another spot"*.

**Measured** (tools/dissolve_flash_cmp.py; the forest ambush `forest_wait_ambush_active` .state / .lakewram, A held on
both sides, the three deaths -- Beetle, Beetle, Avian Chaos -- and the 3-Hetake fight `forest_pre_wrapped.lakewram` by
eye): the port drew **one frame** of the dying enemy entirely in white/pale green, the frame after the death (178-358
px of `min(R,G,B) >= C0` in its box against 2-27 before and after). The rule's cartridge (`--peek` PEEK_ISOBG=4 at every
frame from the delist -3 to +8) has none: the corpse keeps its own colours until CD/00A5's record turns them red, in
the same pose and place. The one-frame white silhouette -- every index of the cells painted alike -- is also the
"jump" the user saw: the outline and the eyes vanish into a blob.

**Cause.** CF/FE57 `$993C,X := 08` and CF/FE61-FE72's copy happen in one main-loop pass. That frame's NMI uploads the
page (CF/E3C0); the OAM attribute is written by the next frame's compose (CF/EA11, the IRQ chain) and uploaded by the
NMI after it. So the picture shows the old palette row (unchanged colours) for those frames, then palette 4 already
holding the copy. `battleDissolveLivePal` (src/field.c) took the row from the LIVE `$993C` (`h->objPal`, set by
battleDeathPalCopy at the corpse's routine start) and the colours from the UPLOADED page (`h->fxCgram`, the NMI head
at the start of the tick): for one frame palette 4 as the hurt flash had left it (C1/5E40's whites). **Fix:** the row
is `$993C` as the compose read it and the NMI shipped it -- `h->objPalShown`, the law battleCmbObjPal already uses for
every living combatant.

**Same commit, not graded by a measured case:** the corpse's position terms. CF/EAE3 / CF/EAF8 add `$A4A4` / `$A4AF`
to `$1D0C` / `$1D23` for every combatant's cells, a corpse's too. The Hetake branch of battleDissolveRender drew its
cells at the staged anchor alone (no walk delta, no draw offset) and the generic branch without the draw offset, while
the living renderers draw with all three -- a corpse killed off its staged foot or under an offset would jump on the
death frame. No death in the measured fixtures happens there (every one is at home with the offsets 0).

Check: battle-dissolve-no-flash-vs-rule-cartridge (the live-`$993C` mutation brings the white frame back on all three).

---

# PART 7 -- THE HETAKE HAS AN IDLE ANIMATION (2026-10-07): drawn by the generic renderer, alive and dead

User: *"check the hetake, whether at the moment of the deadly attack it is at a position different from the dissolve's default position"*,
with a cartridge save (snes9x; saves/battle_re/hetake_dissolve_anim.s9x.state): Crono's blow under way on the middle
Hetake, the three Hetakes' `$9907` = 00 / 07 / 01.

**Measured, the cartridge from that save** (`tools/s9x_to_lake.py ... --ppu`, `--monitor 7E9907:11 --writer`, PEEK_ISOBG=4):
the Hetake's idle walk publishes its poses like every enemy's (C1:1C69 -- 00 -> 01 -> 02 -> 01 ..., and 07, the squat),
the struck one goes 07 -> 03 (the recoil, under the flash) -> 01 (the settle) -> dies in 01; the others keep walking
(CF:F9CE) and cycling. field.c's dedicated `battleHetakeRender` (from the first forest captures) drew ONE baked pose for
every idle frame and built the corpse from the same baked cells.

**Change:** the Hetake is drawn by the generic renderer -- `battleEnemyGenRender` (the published pose `$9907`, i.e. the
port's idlePub, which the pose engine's publish also writes: ONE byte, the last writer wins, C1/4307 / C1/1C66) and the
generic branch of `battleDissolveRender` (the pose held at death). `battleHetakeRender` and the Hetake dissolve branch
are retired.

**Graded:** enemy_cells_cmp from the save (frame 12) 1487 -> 2899 of 2960 px (the isolated slot 1024/1024; the rest is
Crono over slot 1, which the static WRAM render does not draw); the live forest fight's idle Hetakes over 241 frames,
best-aligned, 124672 -> 245413 of 272926 px and 0 -> 114 frames identical; the kill (pose 01) unchanged at 634/634.
Check battle-hetake-idle-poses-from-the-users-cartridge-save; 17 picture pins re-pinned with this measurement.

**Tooling found on the way:** `tools/s9x_to_lake.py` spliced only WRAM/CPU/SRAM, so a converted battle capture drew the
BASE state's tiles (the Hetakes' cells showed another fight's: the body above the cap, 825/2960). `--ppu` now splices the
snes9x VRAM / OAM / CGRAM too.

---

# Scratchpad artifacts (paths for the caller; NOT committed, NOT in src/)
Dir: `%TEMP%/claude/…/e5608df4-…/scratchpad/`
- Drivers: `dgate.py`/`probe.py` (peek sweep enemy-OAM+alive-list), `forest.py` (fresh hold-LEFT boot),
  `color.py` (correct 2-byte pal4 + fb cross-check), `recdump.py`/`mkrom.py` (record read + ROM patch).
- Captures: `pk_620..780.*` (last-Hetake death), `pd_80/100/150.*` (pre-death pal4), `pb_40..360.*` (GATO
  control), `gah_*/ga0_*/gfl_*.*` (patch experiments). **`LAST_HETAKE_dissolve.png`** (the visual: green →
  red recolor → red dither → gone).
- Patched ROMs (experiments): `gato_hp8_flags.sfc` (CC/4700 +4/+22 cleared — GATO still stood),
  `gato_animset0.sfc`/`hetake_animset2.sfc`/`gato_as_hetake.sfc` (E4/F600 — staging-blocked).
- `ct_probe.exe` (oracle copy of build/ct.exe).
