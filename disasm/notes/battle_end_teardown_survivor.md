# BATTLE END — a staged enemy's field object survives iff it was staged MOVE-MODE 6

User report (translated from Portuguese): *"in a battle in the 600ad forest one monster was left over, and it did not even vanish at the end"* — a
monster stayed on screen and did not go away when the battle ended.

The obvious theory — an enemy-slot CAP that is right for the 3-enemy fixtures this port was built on and
wrong for a 6-enemy formation — is **DISPROVEN**. Every enemy bound in `src/` is already 8 (or 11), and each
one matches its ROM counterpart. What is wrong is the **survivor test**, and it was invented.

---

## 1. The bounds, for the record (all read from the ROM, all already correct in the port)

| ROM site | enumerates | bound |
|---|---|---|
| `FD/B251` | `$AEFF`/`$AF0A` FF-init | `CPX #000B` = **11 combatants** |
| `FD/B25F` | `$AF15` zero-init | `CPX #0008` |
| `FD/B26C` | PC seats from `$2980` | `CPX #0003` |
| `FD/B2A9` | staged pack `$29C4+12k` → `$AF02`/`$AF0D` | `CPX #0060`, stride 12 = **8** |
| `C1/8167` | the VICTORY scan | `CPX #0008` (+ `C1/818E`'s `$AF15` bit-6 exception) |
| `C1/8C09` | the enemy AI poll | 8 |
| `C1/1F79` | the target-cursor builder | 11 (combatant-indexed) |
| `C1/B2B1` | the death routine | combatant, 3/11 split |
| `C0/076B` | the FIELD epilogue | 8 unrolled entries (`$A3..$AA`) |

`FD/A80B` is **11** pointers — `$5E2D $5EAD $5F2D` then `$5FAD + slot*0x80` up to `$632D`.
⚠ `battle_boot.md:43,80` lists only six and `:44` says "slot 0-5"; that is wrong.
⚠ `battle_forest_return.md:448-451` says "for each of the **3** enemy slots" and calls `$29C6` "the battle
staging ALIVE byte"; it is **8** slots, and the byte is not an alive flag (below).

Scene 119 (Guardia Forest 600 AD) stages 24 monsters across six ambushes using slots 0,1,2,3,4,6 — and its
two "six-enemy" groups never fight six: `obj52 fn1` moves four decoys to tile (28,19) and HIDEs them
(`fn4 = 91 HIDE / 00`) before the `D8`, so `$0F00 = 0` drops them from `C0/0617`'s list.

## 2. The law — `C1/B3BB` then `C0/07ED`

```
1B3BB TDC / TAX / TAY
1B3BE LDA $29C6,Y / AND #40 / STA $29C6,Y
1B3C6 TYA / CLC / ADC #000C / TAY / INX / CPX #0008 / BCC ->1B3BE     ; EIGHT slots
```

Four call sites in the whole 4 MB image (`20 BB B3`): **`C1/813A` (defeat), `C1/8176` (VICTORY),
`C1/821B`, `C1/83A3` (flee)** — every exit that hands control back to the field. Its twin `C1/B3D2` forces
`| 0x40` instead and is reached only from `C1/83B8` (the `$2989` bit5 scripted branch); the victory path
jumps past it (`C1/818B JMP C1/83BB`).

```
C0/076B  LDA $A3 / BMI + / STA $6D / JSR C0/A98A / LDA $A3 / LDX #00 / JSR C0/07EC   ; slot 0
         …$A4/#0C …$A5/#18 …$A6/#24 …$A7/#30 …$A8/#3C …$A9/#48 …$AA/#54              ; slots 1..7
C0/07EC  TAY / LDA $7E29C6,X / BNE ->080D                    ; nonzero -> SURVIVOR
C0/07F3  LDA $1000,Y / ORA #80 / STA $1000,Y                 ; else DEAD-MARK (writer C0:07FB)
C0/07FB  LDA $1A81,Y / BPL + / (JSR C0/734C) / LDA #00 / STA $1A81,Y
C0/080D  restore $1801/$1881/$1800/$1880/$0F01/$0A00/$0A80/$1601 from $29BF+X…
```

**Bit 6 of `$29C6 + 12k` is set at STAGING by `C0/05B2-05C2`** — `LDA $1100,Y / CMP #06 / BNE / … ORA #40`
— i.e. the field object's **move-mode is 6**. So, after the mask, the byte is `0x40` for a mode-6 object and
`0x00` for everything else, and:

> **A staged enemy's field object survives the battle IFF it was staged with move-mode 6 — whether or not it
> died, dissolved, or fled.**

MEASURED both directions on the oracle:

* **GATO** — `$29EA 0x42 → 0x40` (writer `C1:B3C6`) and his object comes back with `$1000` bit7 CLEAR. He
  persists because he is **mode 6**, not because his death "stands".
* **Scene 119 group A** — the three Rolys' `$1022/$1024/$1026` go `02 → 00` at the fight's start and
  `→ 82` at `f2118`, writer **`C0:07FB`**: all three dead-marked.
* **The forest ambush** — `$29F6/$2A0E` `01 → 00` at `f1498` (the `C1/B3BB` mask) and both objects removed.

## 3. What the port did instead

```c
src/field.c   int alive = (s[0xAF02 + slot] != 0xFF) || h->deadStanding[slot];
              if (!alive) CtSceneBattleRemoveObj(vm, obj); else CtSceneSetActorPos(...);
```

* **`C1/B3BB` was not ported at all.** `$29C6` was written at staging — `r[7] = (…&3) | (mode == 6 ? 0x40 :
  0) | ($1A81 & 0x80)`, the correct bit computed and stored — and then **never read back** by anything.
* The substituted axis (alive-list ∪ `deadStanding`) agrees with the law on exactly the two fixtures this
  port was built on: **GATO is anim-set 2 AND mode 6**, the **Hetakes are anim-set 0, mode 5, all dead**. It
  disagrees structurally on:
  1. **anim-set 2 but mode 5** → ROM removes it, the port kept it drawn and on the field **forever**. That
     is the reported symptom, and ten scenes stage an anim-set-2 id alongside other ids (100, 101, 309,
     313, 314, 317, 318, 319, 324, 475), i.e. a kill that is not the last one.
  2. **mode 6, not anim-set 2, and it dies** → ROM keeps it, the port removed it.
  3. **still in `$AF02` at victory** — the `$AF15` bit-6 enemy `C1/818E` excludes from the victory scan
     (our `battle.c` already models that exception) → ROM removes it, the port kept it.

Both are now the ROM's: the mask runs for all eight slots at battle end, and the epilogue reads the byte.

## 4. Test surface

`CT_STAGE_MODE6=<hex slot mask>` forces the staging record's bit 6 for the named slots. Every battle fixture
in the suite stages move-mode 5 enemies, so **the KEEP arm is otherwise unreachable** and a check cannot tell
the ROM's law apart from the axis it replaced. With it, `field-battle-endteardown-mode6` pins a slot that is
`alive=0 stand=0` and still survives.

`CT_BATTLE_TRACE` gained two lines:

```
endclear    slot=1 29C6 01 -> 00
endteardown slot=0 obj=21 29C6=40 alive=0 stand=0 -> keep
```

`endclear` is what makes `C1/B3BB` itself gradable — but only on a slot whose byte has a **low** bit set,
which no stock fixture has. `saves/battle_re/forest_teardown.lakewram` is `forest_pre_wrapped` with one byte
changed (obj22 `$1201 = 0x01`, the `(… & 3)` source of the record's `+7`).

## 5. OPEN

* **The in-battle DISSOLVE gate is a different, still-underived mechanism.** `h->deadStanding` is set by
  `src/battle.c`'s `animSet == 2` test (`$E4:F600 + id*0x10 + 4`), which has no ROM citation, and
  `src/battle.h` documents a *contradictory* law ("the FIGHT-ENDING kill does not dissolve … set when a
  death empties the alive-list"). NEW here: the ROM's own arm is `C1/B3A0`, and it has **no anim-set gate**
  — `LDA $AE82,Y / BNE skip` (already armed; `$AE82` is set to 1 at `C1/B7B6` and `C1/EC71` whenever a
  dissolve is armed) and `LDA $AF12,Y / BIT #80 / BNE skip` (**hidden**), then `$AEB0,Y = 1`. A per-enemy
  "does not dissolve" property does exist, but in a *different* routine: `C1/B77C` reaches `LDX $B1D2 /
  LDA $CC0000,X / CMP #20 / BEQ skip` after `C1/B488` walks `$CC:8B08[id*2]`. **Probe:** `--monitor
  "7EAEB0:8,7EAE82:8,7EAF12:8" --diff --writer` across an anim-set-2 non-last kill (scene 313/318) and a
  Hetake kill, and disassemble what jumps to `C1/B77C`.
* **The port's epilogue is gated `outcome == 0`.** The ROM runs `C1/B3BB` on defeat and flee too
  (`C1/813A`, `C1/83A3`); our mask now does, but the object removal still only runs on victory.
* ~~**`C0/080D`'s survivor branch is not fully ported**~~ — **DONE 2026-09-28, section 6**: `$0F01`,
  `$0A00/$0A80`, `STZ $1601`, the unlink/link pair and the `C0/CB0A` stamp, for the enemies and the PCs alike.
* **`FD/B25F`'s 8-entry zero-init of `$AF15` is missing** from `stageAll` (currently inert — bit 7 is
  written but never read and bit 6 never written — but it becomes live the moment `FD/B8B8` is ported).
* **`src/field.c`'s `slot > 7` guard has no ROM counterpart.** `C0/0557`/`C0/0560` accept any `$0D00 <
  0xC0`, and `0xB8..0xBF` yields slot 8, whose `STA $7E29BF,X` with X=96 lands on **`$7E2A1F`** — the op-`D8`
  operand-2 flags byte. Keep the guard; that is what it is protecting.
* **The user's actual scene was never identified.** All six scene-119 formations behave identically in
  oracle and port, and none of them stages an anim-set-2 id. The honest statement is "not reproduced in
  scene 119; the reachable mechanism is the survivor test and it is now the ROM's".

## 6. The pose handoff: the field objects take the frame the fight ended on (2026-09-28)

Audit item 5 (`build/_divergence/audit_2026-09-28.md`): after the attract's 07D Frog Squash the cartridge's two
Hench stay turned away through the rest of the fight AND for ~23 frames of the field after it; the port turned
them to the camera the frame the tech's script ended. Three links, all measured on the rule's cartridge:

1. **The pose engine's publish is `$9907`.** `C1/4304 STA $967B / C1/4307 JSR C1/1C4A` with `$9679` = the
   combatant (`C1/42A8`), and `C1/1C4A` -- unless `$967C,X` is 3 -- does `C1/1C5A STZ $9922,X` and
   `C1/1C66 STA $9907,X`, the one displayed byte the idle walks publish into too. `--monitor 7E9907:11` from the
   c7220 state: `4F D1 37 01 01 01 01 01` from the last publish (C1:4307, c7231) until the field takes the page
   back at c7307. The port mirrored the publish for the PCs only (`pcPub`); now for the enemies too (`idlePub`).
2. **The battle's exit copies it into the records, once.** `C1/8443-845E` (after `JSL FD/AAB0`) `JMP C1/0006` ->
   `JMP C1/001F` -> `JML CF/FBE5`, whose `CF/FC08-FC71` writes each combatant's live foot (`$1D0C/$1D23`, tile
   `>> 4` via `CF/FCF6`, fine `& 0F`) and `$9907` into `$29B0 + 5k` / `$29BF + 12s`, `+0..+4`, with no presence
   test. It is NOT a per-frame stager: `--monitor 7E29B0:15,7E29BF:60` over the whole fight (c6927-c7234) shows
   nothing until `CF:FC33` / `CF:FC69` at c7234 (the five enemies' `+4` 04/04/01/01/00 -> 01). Ported as
   `CtBattleStagerRecords` (battle.c), called where `fieldBattleFinish` acks.
3. **The epilogue hands it to the objects.** `C0/076B` (enemies, gate = the back-map `$A3+slot` BMI) and
   `C0/0871` (the party via `$0197/$0199/$019B`) `JSR C0/A98A` (unlink), then `C0/080D` / `C0/08A0`: the position,
   `LDA +4 / STA $0F01,Y` (`C0/0837` / `C0/08CB`), `$0A00 = +0 << 4 | +2`, `$0A80 = +1 << 4 | +3`, `STZ $1601`,
   `JSR C0/A9CD` (link), and `BRL C0/CB0A` -- both BRLs land there (`82 99 C2` / `82 05 C2`; `tools/dis.py` prints
   the displacement, not the target). `CB0A` is the drain's twin: its four gates, then `CDC8`/`D124`/`DD28`
   rebuild the OAM records of that `$0F01` into `$7F4BC0 + $1700,X` and always end `LDA #80 / STA $1B00,X`
   (`CEEE`, `D283`, `DF28`/`E123`). The port composes sprites from `$0F01` itself, so the stamp is what it keeps.
   MEASURED: `$0F01` = 01 on obj17-21 and 4F/37/D1 on the party from c7294 (`C0:08D2`) until the drain moves the
   Hench to 04 at c7317 (`C0:CC17`).

Result: the Hench and the followers are the cartridge's frame for frame (`attract-07d-hench-keep-the-frog-squash-
pose-vs-oracle`, `attract-07d-field-objects-take-the-battle-pose-vs-oracle`). `--battletest` grades the staging
from a snapshot now (`g_stagedRecs`), because the exit rewrites `+0..+4` before it samples.

**OPEN: the lead.** The port draws the lead from its own player machine (`pPlay`, `animTickActor`) and writes
that frame over the lead's `$0F01` every frame, so the lead shows its pre-battle frame where the cartridge shows
the battle's last pose until the drain re-commits (07D: Crono's sword-up at p7190-p7219 against c7290-c7319's
standing pose, ~500 px). The ROM treats the lead's `$0F01` like any object's; closing it means putting the lead on
the drain's legs, not a hold in `pPlay`.
