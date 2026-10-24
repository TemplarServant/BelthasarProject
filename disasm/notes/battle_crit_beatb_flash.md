# CRIT BEAT-B VICTIM FLASH — why the enemy sometimes does NOT go white on Crono's second slash

User report (translated from Portuguese): *"on some of crono's criticals, the enemy does not turn white on the second blow."*
Note the **some** — the report is conditional, so this dossier's job is the CONDITION.

**Verdict: REPRODUCED, byte-isolated, and both ends derived.**
* The ROM writes the victim's flash byte `$993C` **twice** per Crono crit, **unconditionally** (§1, oracle-measured).
* Our engine gates the second write on `battleAlive(s, h->actTarget)` (`src/battle.c:5584`) — a test the ROM
  does not have (§2).
* That gate lies whenever the victim's `block+0x1D` bit7 is **stale-set**, and our engine puts *every* enemy
  into that state from the **second battle of a session onward** (§3, measured: post-battle field WRAM has
  `+0x1D` = `81 / C3 / 87`). The ROM cannot reach that state because `C1/B279` re-derives the flag from
  `curHP` on every roster refresh (§3.2) — a routine 549706b explicitly recorded as **not ported**.
* Isolation: with the flag forced, beat A is **byte-identical** (fnv `4F75DA7B` both runs) and only beat B
  changes (fnv `90ACA583` → `50A41E8D`): the enemy stays coloured and the whole-screen wash disappears (§4).

Probe binaries: `build/ct.exe` / `build/Belthasar.exe` snapshotted to `%TMP%\ctwf3\crit\`
(`ct_probe.exe`, `Belthasar.exe`, `SDL2.dll`). All commands run with cwd = repo root.

---

## 1 — WHAT THE ROM DOES ON THE SECOND SLASH (oracle + disasm, both directions)

### 1.1 The script: the victim's beat-B block carries its own `69 … 6A` pair

`battle_crit_timeline.md` §2.6 / `battle_hurt_flash_sync.md` §2 (re-read, not re-derived):

```
CE/D47D (BEAT A)   69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 1F
CE/D428 (BEAT B)   72 0A | 69 00 | 20 02 | 06 05 | 20 02 | 6A | 03 05 | 06 03 | 50 | 1F
```

Both blocks run on the **role-3 (primary victim) track**; beat B is entered off the `23 01` sync to role 0's
second `03 32` play-anim. `69` = flash ON, `6A` = restore.

`op 0x69` handler `C1/5DD3` (`python tools/dis.py 15DD3 0xC0 1 0`) — the ONLY branch in it is the enemy-CGRAM
half (`$99D2` / `$A08C,X`); there is **no HP test, no `block+0x1D` test, no alive-list test**:

```
C1/5DE5  TAX / LDA $A014,X / TAX      ; role -> combatant
C1/5DEA  CMP #03 / BCC 5DFA           ; PC skips the enemy CGRAM half
C1/5DEE  LDA $99D2 / BEQ 5DFA / LDA $A08C,X / BEQ 5DFA / INC $82
C1/5DFA  LDA #08 / STA $993C,X        ; ** unconditional: OAM palette field := 08 = pal4 white **
```

⇒ **the second white is not conditional in the ROM.**

### 1.2 Measured on a real crit — `saves/battle_re/bat3crit.state`

```
"$TMP/ctwf3/crit/ct_probe.exe" --monitor "993C:11,A013:1,9877:1,AE9B:1,5FB0:2,5FCA:1" \
    --from saves/battle_re/bat3crit.state --input "100:8,0:8,100:8,0:400" --frames 260 --diff
```

```
f0     993C=0A0C0E 02 02 02 …   A013=00  9877=00  AE9B=00  5FB0=000E  5FCA=00
f23    …                                          AE9B=80*                     <- crit rolled
f26    …                        A013=01* 9877=01*                              <- DISPATCH (A = 30)
f74    993C=0A0C0E 08 02 02*                                                   <- BEAT A flash ON   (rel 48 = A+18)
f85    993C=0A0C0E 02 02 02*                                                   <-        …    OFF   (rel 59 = A+29)  11 f
f118   993C=0A0C0E 08 02 02*                                                   <- BEAT B flash ON   (rel 92 = A+62)
f128   993C=0A0C0E 02 02 02*                                                   <-        …    OFF   (rel102 = A+72)  10 f
f202   A013=00*  5FB0=0000*  5FCA=80*                                          <- SPAN END: HP-apply + death
f203   993C=0A0C0E 08 02 02*                                                   <- the corpse goes pal4 and STAYS
```

Facts this pins:
* the victim's `$993C` **is** written a second time, by the beat-B block's `69`, **10 frames** wide;
* the victim is **ALIVE at both beats** — `$5FB0` (cmb3 curHP) is flat 14 until f202 and the dead flag
  `$5FCA` (= `block+0x1D`) is **00** until f202;
* nothing about the flash is HP- or death-dependent; the death only arrives 84 frames later.

### 1.3 ⚠ Correction to `battle_facing_engine.md` ADDENDUM-CRIT

That addendum claims *"Beat B: NOTHING here — the 14-HP Hetake died at slash A"* (and `src/battle.c:5581-5583`
still quotes it). **It is wrong on this very state**: the monitor above shows HP 14 / dead-flag 00 through both
beats and an `08` at f118. `battle_crit_timeline.md` §3.2 already said so; this dossier is the direct
measurement. The "f202-212 all pal1" observation behind it looked at the *post-death* window, where the
combatant is no longer drawn by the live layer at all (and where `$993C` is in fact `08`).

---

## 2 — WHAT OUR ENGINE DOES: the only alive-gated visual in the engine

`src/battle.c:5575-5590` (the beat-B one-shot):

```c
{ int flash2Rel = h->pcAtkAnchored && h->pcAtkArrRel >= 0 ? h->pcAtkArrRel + CT_CRONO_CRIT_STRIKE2 : INT_MAX;
if (h->actActive && h->actCrit && h->actCritFlash && !h->critFlash2Done && rel >= flash2Rel) {
  h->critFlash2Done = 1;
  h->critFlashLeft = CT_CRIT_FLASH_DUR; h->critFlashDur = CT_CRIT_FLASH_DUR;
  if (h->actTarget >= 0 && battleAlive(s, h->actTarget)) {          // <-- src/battle.c:5584  THE GATE
    h->flashTarget = h->actTarget; h->fxFlashPal = battleFlashPal(h->actTarget);
    h->flashLeft = CT_FX_FLASH_DUR;
  } else { h->critFlashLeft = 0; h->critFlashDur = 0; }             // <-- :5587 also kills the WASH
  battleTrace(h, "crit-flash2");
} }
```

Guard-by-guard, for a Crono crit (all measured on `forest_pre` / `gato_t0` / `battle_crono_solo`,
`CT_BATTLE_CRITRATE=255`):

| guard | can it be false on a Crono crit? | evidence |
|---|---|---|
| `pcAtkAnchored && pcAtkArrRel >= 0` | no — `battlePcAtkArm`'s crit branch sets both at dispatch (`pcAtkArrRel = 23 + dom/4`), and the zero-distance bail-out was deleted in session 24 | `battle_crit_timeline.md` IMPLEMENTATION ADDENDUM |
| `actActive` | no — the crit span is `pcAtkArrRel + 146`, beat B is `+59` | trace: dispatch f198, flash2 f287, act-end f374 |
| `actCrit && actCritFlash` | no for Crono (`CD/400E[0]` = 0x01 ≠ normal 0x00) | `battle_crit.md` §C.3 |
| `!critFlash2Done` | no — reset on all three dispatch paths (`:4006`, `:4422`, `:4523`) | grep |
| `rel >= flash2Rel` | no | 13 fixtures swept: crit dispatches == `crit-flash2` traces, 1:1 |
| **`battleAlive(s, actTarget)`** | **YES — §3** | §3/§4 |

Sweep that established the 1:1 (so the *arm* is not the defect):

```
for w in bat2 bat3 bat4 bat8 bat9 ambush_pre gato_solo gato_t1 gato_t2 gato_t3 \
         gato_deadmarle solo_forest forest_seek_pre; do
  CT_BATTLE_TRACE=1 CT_BATTLE_CRITRATE=255 CT_BATTLE_MASHA=1 SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
    "$TMP/ctwf3/crit/Belthasar.exe" --assets ct_assets.dat --battletest saves/battle_re/$w.raw.wram
done      # every fixture: "pc-act …9877=01" count == "crit-flash2" count
```

Note the asymmetry that produces *exactly* the reported shape: the **beat-A** flash
(`src/battle.c:5511`, inside the `wantStrike` arm) has **no** alive test, and **targeting** everywhere uses
`$AF02` only (`:815 :822 :834 :842 :1326 :1334 :1680 :1691 :4479`). So a combatant that is "present but
flagged dead" is fully targetable, gets hit, gets the first white — and loses only the second.

---

## 3 — THE CONDITION: a STALE `block+0x1D` bit7

`battleAlive` (`src/battle.c:851`) = `battlePresent(idx) && !(s[battleBlock(idx)+0x1D] & 0x80)`.
`battlePresent` for an enemy is `s[0xAF02+slot] != 0xFF`. Those two can disagree in our engine.

### 3.1 How our engine gets there (both paths measured)

**(a) The dead flag is never cleared for enemies.**
* SET: `src/battle.c:2295` `s[tb + 0x1D] |= 0x80;` at every death (with `$AF02[slot] = 0xFF` at `:2304`).
* CLEARED: only the item revive (`:2479`) and the end-of-battle clear (`src/field.c:5011-5026`), which is the
  ROM's `C1/EAE8 TDC / STA $5E4A,X` over **the 3 PC seats only** (549706b).
* STAGING never initializes it: the enemy block staging step (iv), `src/battle.c:658-679`, writes
  `+0`, `+3..+6`, `+0x12`, `+0x19..+0x1B`, `+0x22..+0x26`, `+0x37..+0x42` — **`+0x1D` is left as-is**, and
  step (v) (`:691-692`) deliberately zeroes only `+0x1E`, with the comment *"+0x1D/dead-flag is governed by
  `$AF02` + battleAlive's bit7"*.

MEASURED — the field `$7E` after one forest battle (three Hetakes killed):

```
CT_BATTLE_TRACE=1 CT_BATTLE_CRITRATE=255 CT_BATTLE_MASHA=1 \
CT_DUMPWRAM="$TMP/ctwf3/crit/post.wram" CT_DUMPWRAM_AT=2600 \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy "$TMP/ctwf3/crit/Belthasar.exe" --assets ct_assets.dat \
  --scene 19 --loadwram saves/forest_re/battle_crono_solo.dumpwram --selftest o.raw --frames 2700 \
  --walkscript "l:2600,."
```
```
enemy slot 0  block+0x1D = 0x81   curHP = 0   AF02 = 0xFF
enemy slot 1  block+0x1D = 0xC3   curHP = 0   AF02 = 0xFF
enemy slot 2  block+0x1D = 0x87   curHP = 0   AF02 = 0xFF
```

⇒ the next battle of the session stages fresh, full-HP enemies into those same blocks
(`ENEMY_BLOCK(slot) = 0x5FAD + slot*0x80`) with bit7 still set, and `$AF02` re-filled with their ids.
**From the second fight onward every enemy is `battlePresent` but `!battleAlive`.**

**(b) The pre-battle field WRAM is uninitialized there anyway.** `saves/battle_re/forest_pre.raw.wram` already
carries `+0x1D` = `01 / 43 / 07` on slots 0/1/2 (bit7 clear by luck, not by construction).

### 3.2 Why the ROM never has this state — `C1/B279` re-derives the flag from curHP

`python tools/dis.py 1B279 0x40 1 0` (called per combatant from the 11-slot refresh sweep — `C1/B234
JSR C1/B279` inside the `INC $B2D3 / CMP #0B / BCC` loop at `C1/B25D-B265`):

```
C1/B279  LDA $AF0A,Y / CMP #FF / BNE ->B283 / JMP C1/B3B7   ; absent -> skip
C1/B283  LDA $5E4A,X / AND #7F / STA $5E4A,X                ; ** UNCONDITIONALLY CLEAR bit7 **
C1/B28B  LDA $AF0A,Y / STA $AEFF,Y / STZ $24
C1/B293  LDA $B03A,Y / ORA #01 / STA $B03A,Y
C1/B29B  CPY #0003 / BCC B2A7 / LDA $AF12,Y / BIT #80 / BNE B2B9
C1/B2A7  REP #20 / LDA $5E30,X / BEQ ->B2B1                 ; curHP == 0 ?
C1/B2AE  JMP C1/B3B7                                        ;   HP > 0  -> stays CLEARED
C1/B2B1  TDC / SEP #20 / LDA #80 / STA $5E4A,X              ;   HP == 0 -> SET
C1/B2B9  LDA #00 / STA $B03A,Y                              ; ⭐ and B2A5's HIDDEN arm lands HERE too
C1/B2BE  LDA #FF / STA $AEFF,Y                              ; ⭐⭐ the roster byte -> $FF
C1/B2C3  LDA #00 / STA $5E4B,X / $5E4C,X / $5E4D,X / $5E4E,X ; the four status bytes
C1/B2D1  LDA $5E2F,X / AND #FE / STA $5E2F,X                ; block +$02 bit0
C1/B2D9  CPY #0003 / BCC ->B2E1 / JMP C1/B3A0               ; enemies leave through C1/B3A0
C1/B2E1  PHX / PHY / TYA / ASL A / TAX / LDA $FDA849,X ...  ; PCs: a per-seat record, +1 bit4
C1/B300  JSR C1/BC60                                        ;   -> the death-effect dispatch
```

⚠ **§3.2 USED TO STOP AT `B2B6`, ONE INSTRUCTION SHORT OF THE BYTE THAT MATTERS MOST (added
2026-09-11).** Everything from `B2B9` down was missing from this transcription, and the port matched
the transcription rather than the ROM. Two consequences:

* ⭐ **`$AEFF,Y` GOES TO `$FF` ON A DEATH, AND IT IS THIS ROUTINE THAT WRITES IT.** That is the byte
  every `C1/B8BB` targeting resolver gates on — `CMP #FF`, exact — so in the ROM a PC who dies
  mid-battle drops out of every candidate list at the next refresh. Nothing in this port does that:
  staging sets `$AEFF` once at boot and no code path has ever cleared it. It is latent today only
  because the resolver family is ported and not yet wired
  (`[[battle_enemy_move_target]]` §I.4), and it is precisely what blocks wiring it.
* **The HIDDEN arm is not a skip.** `B2A5 BNE ->B2B9` jumps *into* the block above, so a hidden enemy
  is marked `$AEFF = $FF` and `$B03A = 0` **without** getting the dead bit set at `B2B1`. The port
  read that branch as "leave this combatant alone", which is a different thing.

**The LIVING path was short three stores too**, all of them in the stretch this file did transcribe
but the port skipped: `B28B-B28E` republishes `$AEFF,Y` from `$AF0A,Y` on *every* refresh, `B291`
clears the `$24` all-dead latch, `B293-B298` sets bit0 of `$B03A,Y`. Those three are ported now and
graded by `--sweepdeadflag` (450/450 byte-exact on the absent and living arms). They are **no-ops on
a live boot** — staging leaves `$AEFF` and `$AF0A` equal, and nothing in this engine reads `$24` or
`$B03A` yet — which is exactly why only a micro-execution sweep that poisons the three outputs
independently of their inputs could grade them at all.

**The dead/hidden arm is ported too, except the PC tail** (second pass, same day). Everything from
`B2B9` to `B2D8` is unconditional -- `$B03A,Y = 0`, `$AEFF,Y = $FF`, the four status bytes, `$5E2F`
bit0 -- and then `B2D9 CPY #0003 / BCC` forks. **Enemies go to `C1/B3A0`, which is five instructions**
and is ported with them:

```
C1/B3A0  LDA $AE82,Y / BNE ->B3B7        ; already dealt with -> nothing
C1/B3A8  LDA $AF12,Y / BIT #80 / BNE     ; hidden -> nothing
C1/B3AF  LDA #01 / STA $AEB0,Y           ; the per-slot death mark
C1/B3B4  STA $AEB2                       ; and the battle-wide one (C1/AC5E reads it)
```

**PCs (`B2E1`) still are not**: `$FD:A849[cmb*2]` -> a record, `+1` bit4 -> `JSR C1/BC60`, then
`$5E4F` bit7 and `C1/C90B`. That is its own subsystem. The sweep now compares **792/792 on every arm**
including the PC death arm -- the dispatch writes `$5E4F`, `$B18B` and `$AD89/$AD8A`, none of which are
in the window, so the stores *before* the fork are graded for PCs too, and `$AEB0`/`$AEB2` staying
poisoned is how "a PC never reaches `C1/B3A0`" gets graded rather than assumed. 15 mutations, 15 red.

⚠⚠ **AND A SECOND HALF OF THE SAME GAP LIVES IN THE CALLER.** The ROM runs the 11-slot sweep
`C1/B223` from the battle MAIN LOOP (`C1/8626`, `C1/8787`) as well as from the boot (`C1/8130`); this
port only calls it at the boot. So a combatant who dies MID-BATTLE is never re-derived at all, and
`$AEFF,Y` never reaches `$FF` for him however complete `C1/B279` itself becomes. Both halves have to
land before the `C1/B8BB` resolvers can be wired.

`$5E4A,X` is `block+0x1D` on the one `$5E2D + cmb*0x80` array (enemies included: `$5E2D + 3*0x80 = $5FAD`).
So the ROM's dead flag is a **derived** quantity, refreshed from `curHP` for all 11 combatants; a stale bit7 on
a living combatant is unrepresentable. Our port only ever SETs it — 549706b records exactly this:
*"NOT ported, recorded: the per-refresh death sweep C1/B279 that re-derives the flag from curHP — we only SET
it at damage-apply."*

Direction check: the SET half is oracle-observed (`$5FCA` 00 → 80 on the HP-0 frame f202, §1.2) and every
oracle capture has `$5FCA = 00` while HP > 0. The CLEAR half is ROM-derived (disasm above); it is not directly
observed, because no capture on disk carries a stale flag to scrub. Labelled accordingly.

---

## 4 — THE REPRODUCTION (byte-isolated)

Craft the state the second battle of a session hands us — `forest_pre` with slot 0's dead flag pre-set:

```
python -c "
d=bytearray(open('saves/battle_re/forest_pre.raw.wram','rb').read())
d[0x5FAD+0x1D]=0x80                      # ENEMY_BLOCK(0)+0x1D bit7  (= $5FCA)
open(r'%TMP%\ctwf3\crit\forest_deadflag.raw.wram','wb').write(bytes(d))"
```

Both runs (control = the untouched fixture), same script, same seed:

```
CT_BATTLE_TRACE=1 CT_BATTLE_CRITRATE=255 CT_BATTLE_SCRIPT="n:20,a:1,n:3,a:1,n:400" \
CT_BATTLESHOT=<out>.raw CT_BATTLESHOT_AT=<250|290> SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$TMP/ctwf3/crit/Belthasar.exe" --assets ct_assets.dat --scene 19 --battletest <wram>
python tools/ct.py fb2png <out>.raw <out>.png
```

Identical action timeline in both (`pc-act f198  →  strike f248 (A+20, A=30)  →  crit-flash2 f287 (A+59)
→  death/act-end f374 (A+146)`), and:

| frame | control fnv | stale-flag fnv | what the render shows |
|---|---|---|---|
| **f250** (beat A + 2) | `4F75DA7B` | **`4F75DA7B`** — identical | victim = solid white silhouette, BG washed |
| **f290** (beat B + 3) | `90ACA583` | **`50A41E8D`** — differs | control: white victim + washed BG; stale: **victim in normal colours, no wash** |

So the stale flag changes **nothing** in the whole fight except the beat-B frame — which is the user's report,
exactly: white on the first slash, not white on the second, and only in *some* fights.

`field-battle-crit-beatB-dead-suppressed` (shot f289 on `forest_pre`) does **not** pin a suppressed case: on
that fixture the victim is alive at beat B, the alive branch is taken, and f290 renders the victim white
(verified by render). The check grades a *flashing* beat B under a name that says the opposite.

**Renamed 2026-10-24: `field-battle-crit-beatB-flash-hetake`** (docs/MUTATION_AUDIT.md). The retimed fight had
also moved its fixed frame off the flash; it is anchored on `pc-act#1+92` now, the frame the victim shows white, and a
mutation of op 0x69's colour record kills it.

---

## 5 — THE FIX (minimal, mandate-safe) — host arm, not the interpreter

**5.1 Primary — delete the invented gate.** `src/battle.c:5584-5587`: drop the
`&& battleAlive(s, h->actTarget)` test and the `else { critFlashLeft = 0; critFlashDur = 0; }` arm, leaving

```c
if (h->actTarget >= 0) { h->flashTarget = h->actTarget; h->fxFlashPal = battleFlashPal(h->actTarget);
                         h->flashLeft = CT_FX_FLASH_DUR; }
```

and delete the stale comment at `:5580-5583` + `:5589-5592` (both cite the falsified ADDENDUM-CRIT claim).
Canonical basis: `C1/5DD3` has no aliveness branch (§1.1) and `bat3crit` writes `$993C` at beat B with the
victim at 14 HP and `$5FCA = 00` (§1.2). `battle_crit_timeline.md` §5.4 already asked for exactly this.
**Expected suite impact: none** — every crit capture on disk has a living, unflagged victim, so the gate is
already passing there (control f289/f290 verified).

**5.2 Root cause — port `C1/B279`'s re-derivation** so `battleAlive` stops lying engine-wide:
for each present combatant, `block+0x1D &= 0x7F; if (curHP == 0) block+0x1D = 0x80;` (§3.2). This is a separate
change with a wider blast radius (`battleAlive` is read at `:966 :1004 :1011 :1024 :1286 :1445 :3990 :4026
:4470 :4566 :5584 :6330 :6355 :6366` — a stale-flagged enemy is also skipped by the ATB tick and the AI/target
pools while remaining targetable through `$AF02`), so it wants its own derivation of the sweep's CADENCE
(which refresh calls `C1/B227`) before it lands. The 1-line stop-gap — zero `ENEMY_BLOCK(slot)+0x1D` in the
staging step (iv) — is weaker than the ROM's law (it does not cover mid-fight staleness) and should be
labelled as such if used.

**5.3 Should the crit move onto the interpreter?** The beat-B flash genuinely IS script-carried
(`CE/D428`, role-3 track, entered off the `23 01` sync to role 0's second `03 32`), so running PC physicals
through `battleAscTick` would deliver both beats on the ROM's own clock and retire `CT_CRONO_CRIT_STRIKE`/
`CT_CRONO_CRIT_STRIKE2`/`CT_FX_FLASH_DUR` at once. **Not yet derivable safely** — what is missing:
* the bank-CE role-0 track's `03 xx` playAnim **playback durations** (the sprite-streamer boundary — the same
  reason `battleAnimParse` only decides WHETHER an event fires, never WHEN);
* the `18`/`98`/`1B` walker+return parking semantics against our `pcAtkArrRel` anchor model (today the anchor
  is simulated, not interpreted);
* the multi-role `$5D9D` rendezvous scheduling for a PC action (`23`/`24`/`36` across roles 0/3/8);
* the PC-physical super expansion (`1E 2B` / `1E 44`) and its `$AE9B`-gated crit variant;
* every PC-physical pin (pose ladder, `battlesfx` rels, spans) is baked on the host route and would move.

So: fix on the host arm now (5.1), root-cause the flag (5.2), and treat the interpreter move as its own
derivation (that is also why 48150f2 deliberately left PC physicals off `battleAscTick`).

---

## 6 — SECONDARY DELTAS (measured here, NOT part of the fix)

Recording them so they are not re-measured, and deliberately not bundling them (they move pinned canaries):

| quantity | ROM (bat3crit, A = 30) | ours | note |
|---|---|---|---|
| beat-A `$993C` ON | rel 48 = **A+18** | `strikeRel = A+20` (`CT_CRONO_CRIT_STRIKE`) | 2 f late |
| beat-A width | f74→f85 = **11 f** | `CT_FX_FLASH_DUR = 9` | |
| beat-B `$993C` ON | rel 92 = **A+62** (op at A+61, store retires A+62) | `A+59` (`CT_CRONO_CRIT_STRIKE2`) | 3 f early |
| beat-B width | f118→f128 = **10 f** | 9 | |
| beat-B hurt POSE | `06 05` then `06 03` in `CE/D428` | not re-armed (only beat A arms `hurtPoseLeft`) | |
| post-death palette | `$993C` := 08 at f203 and **stays** (white corpse) | dissolve path owns it | separate dossier |

## 7 — OPEN QUESTIONS

1. The CLEAR half of `C1/B279` is disasm-only; no capture on disk carries a stale flag to watch it scrub.
   A state that ENTERS a second battle after a first one killed enemies would confirm it directly.
2. Which refresh(es) call the `C1/B227` sweep, and how often — needed before 5.2 lands.
3. `$AF0A,Y` vs `$AF02,Y` in `C1/B279`'s presence test (offset +8 into the same region) is not decoded here.
4. Whether a stale flag has already been silently perturbing multi-battle sessions elsewhere (ATB skip at
   `src/battle.c:1286`, AI/target pools at `:1004/:1024`) — not measured; `--battletest` always starts from a
   clean single-fight seed, which is why the suite never saw it.
5. The beat-B `50` ShowDamage in `CE/D428` proves the popup is beat-B-anchored (A+83); our late-spawn model
   derives it from the span end instead. Consistent today; not re-checked here.

---

## Adversarial review (independent re-derivation, 2026-08-22)

Default verdict was "not proven". Every ROM address was re-disassembled and every oracle number
re-measured from a state I loaded myself, with my own binary snapshots
(`%TMP%\ctwf3\crit-adv\{ct_probe.exe,Belthasar.exe,SDL2.dll}`, cwd = repo root).

**Net verdict: the MECHANISM survives intact — but the dossier is wrong on four factual points,
badly understates the defect, and its fix ORDERING is the CLAUDE.md spot-fix shape.**

### A — What I reproduced and could NOT refute

| claim | my re-measurement | status |
|---|---|---|
| `C1/5DD3` op-0x69 stores `#08` into `$993C,X` with no aliveness branch | `python tools/dis.py 15DD3 0xC0 1 0` -> `15DFA LDA #08 / 15DFC STA $993C,X`; the only branches are `$A010 CMP #04 BCS` (the role-LIST arm, which also stores `#08` per listed combatant at `15E15/15E17`) and the `$99D2`/`$A08C,X` CGRAM half | CONFIRMED |
| `C1/5E87` op-0x6A restores from `$9888,X` | `15EA0 LDA $9888,X / 15EA3 STA $993C,X` (and the `<4` twin at `15EA8`) | CONFIRMED |
| `CE/D428` = `72 0A 69 00 20 02 06 05 20 02 6A 03 05 06 03 50 1F` | ROM file offset `0x0ED428` = `72 0a 69 00 20 02 06 05 20 02 6a 03 05 06 03 50 1f` | CONFIRMED byte-for-byte |
| `CE/D47D` = the same minus `72 0A` and `50` | `0x0ED47D` = `69 00 20 02 06 05 20 02 6a 03 05 06 03 1f` | CONFIRMED byte-for-byte |
| `C1/B279` re-derives `$5E4A,X` from `$5E30,X` | `1B283 LDA $5E4A,X / AND #7F / STA $5E4A,X` ... `1B2A9 LDA $5E30,X / BEQ 1B2B1` ... `1B2B4 LDA #80 / STA $5E4A,X` | CONFIRMED |
| the bat3crit monitor timeline | re-ran verbatim: `f74 993C[3]=08`, `f85 ->02`, `f118 =08`, `f128 ->02`, `$5FB0=0E00` and `$5FCA=00` flat until `f202`, `f203 993C[3]=08` | CONFIRMED, identical |
| `battle_facing_engine.md` ADDENDUM-CRIT ("the 14-HP Hetake died at slash A") is FALSE | the victim is at 14 HP with the dead flag 00 through BOTH beats; the kill is 84 f later | CONFIRMED FALSIFIED |
| post-battle field `$7E` enemy `+0x1D` = `81 / C3 / 87` | re-ran the `CT_DUMPWRAM_AT=2600` forest run: slot0 `0x81`, slot1 `0xC3`, slot2 `0x87`, all curHP 0, `$AF02 = FF FF FF` | CONFIRMED, identical |
| beat A byte-identical, beat B differs | `f250` `4F75DA7B` in BOTH; `f290` `90ACA583` (control) vs `50A41E8D` (slot-0 flag set) | CONFIRMED, identical |
| the RENDER | I looked at both PNGs (`tools/ct.py fb2png`): control = solid-white Hetake + washed grey BG; stale = Hetake in normal colours, BG black. Exactly the user's report | CONFIRMED |
| the beat-B ARM always fires | 3/3 crits in a live forest run (`crit-flash2` f284/f1120/f1538, targets 3/4/5) + 1/1 on `forest_pre` | CONFIRMED (spot check) |
| the ASC projection cannot clobber the host arm | `src/battle.c:5333` is gated on `ascDrives = animParsed && (!isPc || actIsTech)` = 0 for a PC physical | CONFIRMED |

### B — REFUTED / CORRECTED

**B1. "no capture on disk carries a stale flag to scrub" (SS3.2 / open question 1) and "every crit
capture on disk has a living, unflagged victim, so no pinned signature moves" (SS5.1) are FALSE.**
Sweeping every `saves/**/*.raw.wram` for `ENEMY_BLOCK(slot)+0x1D` next to the block id:

```
ambush_pre.raw.wram   slot0 id5E +1D=01   slot1 id5E +1D=80 *   slot2 id5E +1D=80 *
loc112_pre.raw.wram   slot0 id0C +1D=00   slot1 id0C +1D=80 *   slot2 id0C +1D=00
forest_pre / forest_seek_pre / forest_cyclone / solo_forest / battle_crono_solo:
                      slots 0/1/2 = 01/43/07 (clean) but slots 3,6,7 = CD (bit7 SET, id FF)
```

`ambush_pre` is IN the dossier's own 13-fixture sweep list and its `$AF0D` slots 1/2 = `5E`
(present). So the stale state is already in the repo, and the "the CLEAR half is disasm-only, no
capture carries a stale flag" framing is wrong: `ambush_pre` is a directly runnable stale-flag
capture. It also means the condition is NOT confined to "the second battle of a session" — the
forest fixtures already carry bit7 on slots 3/6/7, so any fight that staged four or more enemies
would hit it on the FIRST battle.

**B2. The blast-radius citation is mis-attributed.** SS5.2 says a stale flag is "also read at
`:1286` ATB tick and `:1004/:1024` AI/target pools". Those three sites loop over **PC seats 0..2
only** (`for (int seat = 0; seat < 3; ...)`), and PC `+0x1D` **is** cleared at battle end
(`src/field.c:5020-5026`, `C1/EAE8`). They can never see a stale ENEMY flag. The sites that
actually break are:

* `src/battle.c:6330` — `for (int i = 0; i < 11; i++) if (battleAlive(s, i) && ...) s[0xAFAB + i]--;`
  -> a stale-flagged enemy's **ATB never decrements**;
* `src/battle.c:6366` — `if (cmb >= 3 && cmb < 11 && battleAlive(s, cmb) && s[0xAFAB+cmb] == 0)`
  -> and he **never opens a turn**;
* `src/battle.c:2861` — no footprint in the PC walk-grid occupancy;
* `src/battle.c:4214` — invisible to the PC entry-facing nearest-enemy pick (`C1/2A73`).

**B3. Open question 2 ("which refresh calls the sweep -- needed before 5.2 lands") is NOT open.**
The sweep's ENTRY is `C1/B223` (`TDC / STA $B2D3`), not `B221`/`B227` (`C1/B222` is an `RTS`).
Byte-scan for `20 23 B2` in bank C1 and `22 23 B2 C1` ROM-wide:

```
JSR C1/B223 from C1/8130   <- C1/8121 INC $0027 / 8124 STA $B2C7 / 8129 JSR C1/0003
                              / 812C JSL FD/ACFD / 8130 JSR C1/B223 / 8133 LDA $24 / BEQ
                              ($24 = B279's "all 3 PCs dead" latch, C1/B274 -> the $7F01EC=1 defeat)
JSR C1/B223 from C1/8626   <- inside dispatch handler C1/8461
JSR C1/B223 from C1/8787   <- never executed in the capture measured below
```

⚠ **"`C1/8130` <- battle BOOT" WAS WRONG, and only running the ROM showed it.** From a MID-battle
capture — no boot anywhere in it — `C1/8130` executes **30 times in ~90 frames**. It is a main-loop
site like the other two. `build/ct.exe --trace 900000 --from saves/battle_re/bat3_lake.state`, then
the instruction executed immediately before each `C1/B223`:

```
     30 C1/8130
     14 C1/8626
```

⭐ **AND THE GATE ON EACH IS NOW DERIVED, after two static attempts failed to find even the enclosing
routines.** The trace gives what a byte scan cannot: what actually ran, and in what order.

**`C1/8626` is inside one of thirteen DISPATCHED HANDLERS.** `C1/8318-8340` is the loop:

```
831C  LDA #1000 / STA $AEC3        ; a ONE-HOT mask, starting at bit 12
8323  TAX                          ; X = 0
8324  LDA $AEC3 / BIT $B186        ; is this slot's bit set?
832A  BEQ ->8339                   ;   no  -> skip the handler entirely
832C  PHX / TXA / ASL A / TAX
8333  JSR ($B92D,X)                ;   yes -> run handler[X]
8339  LSR $AEC3 / INX / CPX #000D / BCC ->8324
```

The table at `C1/B92D` is `8876 88C5 8975 89B9 8A05 8A51 8A9D 8A9E 8A9F 8B10 8BB9 8C08 8461`, and in
the trace `JSR ($B92D,X)` entered **exactly one** target, 15 times: `C1/8461` — index **12**, whose
mask is `$1000 >> 12` = **bit 0 of `$B186`**. So that refresh runs on the frames a turn is pending in
slot 0, and `$B186` is FF-initialised to 0 at battle boot (`C1/801C-8029`, `[[battle_menu_synthesis]]`
1.12).

**`C1/8130` is the main pass's own tail.** `C1/83AE LDA $2989 / BIT #20 / BNE ->83B8` else
`83B5 JMP C1/8120`, and battle boot does `$B31F = $2989 & 0x20` then **clears** that bit
(`C1/801C-8029`), so in a fight the jump is always taken. Its cadence is therefore the **main loop's
own pass rate, not the video frame rate** — ~30 passes per ~90 frames in this capture.

⚠ `C1/8345 LDA $C1FFFF / BEQ ->8353` is dead weight: `C1/FFFF` is `00` in this build, so the
`834B LDA $00421B / AND #20` arm below it never runs. Noted so nobody derives a mechanism from it.

plus three DIRECT `JSR C1/B279` at `C1/A460 / A469 / A472` with `X = 0/0x80/0x100, Y = 0/1/2` —
the **3 PC seats only** (`C1/A452 STZ $AECB / LDA $0024 / BNE`, the "is any PC still alive"
re-derive that feeds `C1/A475 LDA $AEFF / BPL ...`). So the sweep runs **at battle boot,
immediately after the staging JSL** — which means the "1-line stop-gap ... weaker than the ROM's
law" the dossier disparages in SS5.2 is in fact the ROM's own FIRST call site. The boot call alone
would have fixed every case measured in this dossier.

**B4. Open question 3 (`$AF0A` vs `$AF02`) is answerable, and it changes the port.** Oracle
`ct_probe --read "AF02+20" --from saves/battle_re/bat3crit.state`:

```
$AF02 5E 5E 5E FF FF FF FF FF | $AF0A 00 01 FF | $AF0D 5E 5E 5E FF FF FF FF FF
```

=> `$AEFF,Y` (Y = combatant 0..10) is the **combatant-indexed ALIVE list** whose enemy half is our
`$AF02`, and `$AF0A,Y` is the **combatant-indexed ROSTER** whose enemy half is our `$AF0D`.
`C1/B279` is therefore a full roster refresh, not just a flag scrub:

```
if ($AF0A,Y == FF) return                  ; absent by ROSTER (not by the alive list)
$5E4A,X &= 7F                              ; clear dead
$AEFF,Y := $AF0A,Y                         ; ** RE-LIST from the roster **
$B03A,Y |= 01
if (Y >= 3 && ($AF12,Y & 80)) goto tail
if ($5E30,X != 0) return                   ; HP > 0 -> done, flag stays CLEAR
tail: $5E4A,X := 80 ; $B03A,Y := 00 ; $AEFF,Y := FF ; $5E4B..$5E4E := 0 ; $5E2F,X &= FE
```

The dossier's proposed two-liner keyed on `battlePresent` (= `$AF02`) is a **partial port on the
wrong presence test**: it would skip exactly the combatants the ROM re-lists, and it drops the
`$AEFF := $AF0A` re-list, the `$B03A` bit and the `$5E4B..$5E4E` status clear.

### C — UNDERSTATED: the stale flag FREEZES the enemies, it does not just skip a flash

The user's missing white is the *smallest* consequence. Grafting the engine's OWN measured
post-battle `+0x1D` values (`81/C3/87`, SS3.1) onto `forest_pre` and counting enemy turns:

```
python -c "d=bytearray(open('saves/battle_re/forest_pre.raw.wram','rb').read())
for s,v in ((0,0x81),(1,0xC3),(2,0x87)): d[0x5FAD+s*0x80+0x1D]=v
open(r'%TMP%\ctwf3\crit-adv\forest_b2.raw.wram','wb').write(bytes(d))"

CT_BATTLE_TRACE=1 CT_BATTLE_CRITRATE=255 CT_BATTLE_SCRIPT="n:20,a:1,n:400,a:1,n:2000" \
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy <Belthasar> --assets ct_assets.dat --scene 19 \
  --battletest <wram> | grep -c enemy-arrive
```

| seed | `enemy-arrive` |
|---|---|
| `forest_pre` (clean) | **9** |
| `forest_b2` (the engine's own post-battle flags) | **0** |

Cross-check on the on-disk stale fixture: `ambush_pre` victory `f2051`; with `+0x1D` zeroed on all
8 slots, `f2059`.

And the f290 render with all three flags stale is `AB1422F9`, not the slot-0-only `50A41E8D` — the
other two enemies' staleness moves the frame too (`:2861`/`:4214`).

**So: from the second battle of a session onward, every enemy in our engine is present, targetable,
damageable — and completely passive.** The proposed "PRIMARY" fix (delete the gate) makes the ONE
symptom the user reported disappear while leaving that intact. That is the CLAUDE.md *spot-fix
(pontual)* shape: patching the symptom, leaving the shared mechanism wrong.

**The ordering must be inverted.**

1. **FIRST — port `C1/B223`** (the full `B279` refresh above) at the battle-boot call site the ROM
   uses (`C1/8130`, immediately after staging), for combatants 0..10. This alone restores the
   beat-B white (`battleAlive` stops lying) AND un-freezes the enemies. Cadence is derived (B3);
   the two in-loop call sites (`C1/8626`, `C1/8787`) can follow once their routines are decoded.
2. **THEN — delete the invented gate** at `src/battle.c:5584-5587` plus the falsified comments at
   `:5580-5583` / `:5589-5592`. After (1) the gate is *unreachable* (targeting picks alive; the HP
   write lands at span end `A+146`, long after beat B at `A+59`), so this is code hygiene, not the
   fix. It is still required: the ROM has no such branch (`C1/5DD3`).

### D — Pinned checks (read `tools/regress_checks.json`, 234 checks; suite NOT run)

**Deleting the gate — nothing moves. Verified, not assumed:**

* `field-battle-crit-beatB-dead-suppressed` (`forest_pre`, `CT_BATTLESHOT_AT=289`,
  `expect battleshot: f289 fnv=E616CF03`). Trace on that exact fixture: `pc-act f198` ->
  `strike f248` -> `crit-flash2 f287` (`AF02=5E5E5EFF`) -> `death f374`. The victim is ALIVE at
  beat B and `+0x1D` = `01` (bit7 clear), so the alive branch is taken today; I reproduced
  `E616CF03` with my snapshot binary. Deleting the gate cannot change it.
  WARNING: **the check's NAME and NOTES assert the opposite of what it grades** ("f289 = rel 91
  with a dead victim: no white"). Poking slot-0 `+0x1D := 0x80` gives `f289 = 50A41E8D` !=
  `E616CF03` — the pinned frame IS the flashing one. Rename + re-note it in the same commit, or
  the suite keeps asserting the falsified ADDENDUM-CRIT claim in prose.
* `field-battle-crit-flash1` / `-flash2` (`gato_t0`): GATO stages into **slot 3**
  (`src/field.c:3583`), and `gato_t0.raw.wram` slot 3 `+0x1D = 0x00`. Alive branch -> unaffected.
* `field-battle-crit-leap-mid` (f223 = rel 25) is before beat B.

**Porting `C1/B223` — the fixtures at risk are the two with a present, stale-flagged enemy:**

* `ambush_pre` -> only `field-battle-ambush-generic-dissolve` (shot `f362`). **MEASURED: unchanged**
  (`CD0E9659` with and without the flags). The fight LENGTH does move (`f2051` -> `f2059`), so any
  future pin on that fixture would move; the current one does not.
* `loc112_pre` -> no pinned check uses it (grep of `argv` across all 234 checks).
* Every `gato_*`, `bat*`, `forest_*`, `heal_*`, `revive`, `p*` fixture has bit7 clear on every
  **present** enemy slot -> untouched. `forest_*` slots 3/6/7 carry `0xCD` but with id `FF`
  (absent), and both enemy-side readers (`:2861`, `:4214`) test `battlePresent` first.

### E — Open items this review ADDS

1. **Beat B is not gated on `actMiss`, beat A is.** `wantStrike` (`src/battle.c:5502`) carries
   `&& !h->actMiss`, so a MISS suppresses beat A; the beat-B one-shot at `:5578` has no such term.
   `battle_miss_evade.md` measured "a MISS gives the victim NO hurt-flash". Whether a missed crit
   can occur, and what the ROM's miss script does with the role-3 `69`, is underived. The fix
   touches this exact block and leaves the asymmetry in place.
2. The `23 01` sync that enters `CE/D428` off role 0's second `03 32` is inherited from
   `battle_hurt_flash_sync.md`; I verified the BLOCK bytes but not the sync wiring. Not
   load-bearing for the fix.
3. `C1/8626` / `C1/8787` — the two in-loop `JSR C1/B223` sites are not decoded here, so the
   *mid-fight* refresh cadence (what would scrub a flag set DURING a fight) is still open even
   though the boot cadence is settled.
4. `C1/B279`'s `CPY #0003 / BCC / LDA $AF12,Y / BIT #80 / BNE tail` — the enemy-only `$AF12,Y`
   bit7 branch (an enemy forced dead regardless of HP?) is undecoded and must be ported with it.
5. Whether a real second battle reached by PLAY (not by grafting the measured bytes) shows the
   frozen enemies: I could not trigger a second encounter in `--scene 19` (walked `l/r` x3,
   9000 f, one battle only), and `--loadwram` rejects the raw `CT_DUMPWRAM` image
   ("FAILED to find RAM block"). The graft used the engine's own measured output bytes, so the
   chain is closed on VALUES, not on a single end-to-end run.
6. `field.c:2057` (`memset(s7 + 0x5E2D, 0xA5, ...)`) is a `CT_OWDIRTY` test hook only — it is NOT a
   normal-play clear of the battle blocks. Nothing in normal play clears the enemy `+0x1D`,
   confirming the dossier's SS3.1(a).
