# The Hetake "stops attacking forever" latch — measured on OUR engine

Measured 2026-08-15 while the ROM-side derivation ran separately. This file records only what was
MEASURED here; the canonical ROM answer (what re-arms the AI after a turn that opened no window)
belongs in `hetake_damage_turns.md` and is **NOT** decided in this file.

## 1. The repro (no input at all — the enemies are the only actors)

```
CT_BATTLE_TRACE=1 build\ct_field.exe --assets ct_assets.dat --scene 19 \
    --battletest saves\battle_re\forest_pre.raw.wram
```

`pcAtk=0` for the whole run — the PCs never act, so every RNG draw and every turn in the trace is
the enemy AI's.

## 2. What the trace shows

Seven enemy attacks land, then every enemy noops forever:

| event | frames | HP change |
|---|---|---|
| attack 1 | arrive f293 → act-end f495 | crono 70 → 63 (**7**) |
| attack 2 | f513 → f715 | crono 63 → 41 (**22**) |
| attack 3 | f1002 → f1204 | crono 41 → 33 (**8**) |
| attack 4 | f1222 → f1424 | marle 65 → 20 (**45**) |
| attack 5 | f1711 → f1913 | marle 20 → 12 (**8**) |
| attack 6 | f2216 → f2418 | crono 33 → 26 (**7**) |
| attack 7 | f2720 → f2922 | marle 12 → 5 (**7**) |

After f2937 the trace is nothing but `enemy-noop` for all three slots, once per ~300f, to the end of
the run (f3837+): `#1 f2937, #2 f3217, #0 f3222, #1 f3237, #2 f3517, #0 f3522, #1 f3537, …`.
`cronoHP=26 marleHP=5` never move again. Both user-reported symptoms are in this one run: the damage
spread (7/8 vs **22/45**) and the enemies going permanently silent.

## 3. The latch, isolated by the RNG stream

Re-run with `CT_BATTLE_RNGTRACE=1`. The last draw of the entire run is:

```
battlerng idx=38 raw=4A max=1  -> 0
battlerng idx=39 raw=67 max=99 -> 3      <-- the last draw, at f2937's noop
battletrace f2937 enemy-noop id5E#1 ...
battletrace f3217 enemy-noop id5E#2 ...   <-- and NO battlerng line ever again
```

**After idx=39 the RNG is never drawn again.** So the later noops are not the AI evaluating a
condition and getting an unlucky roll — no condition is being evaluated at all.

## 4. The Hetake AI script, read from the ROM

`ROM_AI_PTR_TABLE` = `CC/8B08`; id `0x5E` → ptr `9E36` → flat `0C9E36`. Cursor-advance lengths from
`FD/BA4A` = `04 04 06 00 FF FF 04 04 04 02 03 05 04 03 02 02 04 0A 10 03`.

```
17 10 00 00 | FE | 00 00 06 04             | FE      <- §1  cond 0x17 prob 0x10 (16%) -> ONE move-only op
00 00 00 00 | FE | 01 01 05 00 01 01 06 0E | FE      <- §2  cond 0x00 (always) -> two attacks
FF                                                    <- block1 ends
```

So §1's action list is a single op-00 (move-only, `aiIsFiller`) and contains **no substantive op at
all**. §2 alternates the two attacks — `01 01 05 00` (targeting 05 = random PC) and `01 01 06 0E`
(06 = nearest), which is exactly the `AEE3=01,01,05` / `01,01,06` alternation in the trace.

The last draw `idx=39 raw=67 max=99 -> 3` is a cond-17 roll: `3 < 0x10` → **§1 passed**, so §1 was
the section the eval chose at f2937.

## 5. The mechanism (in OUR engine)

`battleAiEval` — the full eval that walks conditions WITH draws and latches the chosen section into
`aiArmBase/aiArmEnd/aiArmBlk/aiArmSec` — runs from exactly two sites:

* `battleCombatStart` (src/battle.c:1381), once per battle;
* `battleSpanEndArm` (src/battle.c:3816), called from **one** place: src/battle.c:4454, inside the
  span-end path.

A turn whose AI returns no substantive op never opens a window (`battleOpenAction`, src/battle.c:3941
— reload the ATB, `battleTrace("enemy-noop")`, `return`), so it has **no span, therefore no span
end, therefore no re-eval**. `battleAiRunTurn` then replays the latched section forever via
`battleAiExecSection`, which walks §1's single filler op, finds nothing substantive, and returns 0 —
without drawing, which is precisely the silent RNG in §3.

**Net: the first time a Hetake rolls the 16% §1 branch, that Hetake never attacks again for the rest
of the fight.** A 16% no-action turn is canonical (the script says so); a one-way latch into it is
not — it is a property of where our engine calls the re-eval, and it is mechanism-level, not
scene-specific.

## 6. NOT DERIVED here (the open question this file hands over)

`battle_ai_interp.md` §4 states the law as: **A) FULL EVAL** at combat start *and at every own-action
span end*; **B) TURN ATTEMPT** at every ATB-0, which re-resolves the saved entry's target with no
cond re-walk. Under that law a turn that produces no window has no "own-action span end", so the
question the ROM must answer is:

> When an enemy's eval lands on a move-only section (the 16% mode-4 closed mushroom), what does the
> ROM do at that turn's end that lets the NEXT turn re-walk the conditions?

Candidates, none of them chosen here: the move-only turn is itself an action with a window (so it
has a span end); or the eval is re-run per attempt for a section with no substantive entry; or the
cursor/section latch is not persistent across a no-action turn. **This must be read off the ROM +
oracle before anything in `battleAiEval`/`battleSpanEndArm`/`battleOpenAction` is changed.**

## 7. The damage half (measured, not explained here)

The seven landed hits are 7, 22, 8, 45, 8, 7, 7. The enemy branch of `battleComputeDamage`
(src/battle.c:1881-1891) is:

```c
unsigned recIdx = rom[actBase + act2 * 11 + 4];                       // = $B2C8
int coef = (rom && !(recIdx & 0x80)) ? rom[atkBase + recIdx * 12 + 9] : 18;
base = coef * (s[ab + 0x3D] * 2) / 18;
int def = s[tb + 0x3E];
int dmg = (base * (256 - def)) >> 8;
dmg += battleRng(s, rom, s[ab + 0x36] / 3);                           // <- the PC-shaped bonus term
```

Two things to check against the ROM (both open, neither concluded here): the `: 18` fallback
coefficient taken when `recIdx` has bit7 set, and whether the enemy path's bonus range is really the
PC's `block+0x36 / 3`. The 22 and 45 outliers are the thing to explain — they are ~3x and ~6x the
7/8 baseline.
