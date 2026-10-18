# Phase F — the byte census

Run 2026-09-12. The plan's §0.2 asks for this because "markers only find what somebody already
noticed": take a fixture, run the oracle alongside, diff **every** byte, and give each difference a
verdict. This is the record of the first run.

---

## What was actually missing

Not the comparison. `CtVmWmaskStats` has always reported an `unmodelled` count — bytes the oracle
drove away from the seed that the port never claims — and `vmdiff-wmask-party3` has been pinning that
number for weeks. What was missing is that **a count cannot be given a verdict**. There was no way to
ask *which* bytes.

`CtVmWmaskCensusReport` now enumerates them on exactly the counter's own condition, folded into runs.
Two switches:

| vehicle | how | scope |
|---|---|---|
| `ct.exe --vmdiff` | `VMDIFF_WMASK=1 VMDIFF_CENSUS=1` | the field **VM** only |
| `Belthasar --selftest` | `CT_FIELDLOCK=<state> VMDIFF_WMASK=1 CT_FIELDLOCK_CENSUS=<frame>` | the **live engine**'s own `$7E` |

---

## ⭐ Finding 1 — the vehicle decides the number, and one of them is wrong by 5x

`party3_field`, 130 frames, the calibrated `vmdiff-wmask-party3` vehicle: **391 unmodelled bytes**.

The same mask against the live engine on a comparable station: **85**.

`ct.exe` links neither `field.c` nor `battle.c` — its own scope line says so — so every byte the live
engine writes reads as unmodelled there. **306 of the 391 were vehicle artefact.** The big blocks that
vanish are the tell: `$7EC880..C8BD`, `$7ED080..D0BD`, `$7ED880..D8BD`, the OAM shadow `$7E0770..07AF`
and the whole `$1D40..1DF1` band. The live engine claims all of them.

⚠ **So the pinned `unmodelled 391` is not a debt figure**, and nothing should start treating it as one.
It measures the field VM's share, which is exactly what that check is for.

## ⭐ Finding 2 — across a location transition the census stops measuring behaviour

Three live-engine runs:

| fixture | frames | crosses a load? | unmodelled |
|---|---|---|---|
| `forest_exit` (projections walk) | 90 | no | **85** |
| `forest_exit` (hop crossing) | 400 | yes | 6252 |
| `scene468_stairs_to_120` | 240 | yes | 20966 |

By page, the stable run is entirely `$00`–`$22` — the engine's own variable pages. The two that cross
a load are dominated by `$30xx $39xx $C7xx $B6xx $CCxx $F0xx` and `$C0xx–$CFxx $B4xx`: **map and
graphics staging buffers**, not 20966 defects. The census is meaningful **within a stable station**;
across a transition it measures buffer residency and needs a different reading.

## Finding 3 — a third of the residue is the 65816 stack

Of the 85, **29 fall in `$0508–$06FF`**. The port has no hardware stack, so no amount of porting will
ever claim them — `--bootcheck` already excludes exactly that window. Now filtered from the listing
(and counted separately) but **never from `CtVmWmaskStats`**, whose number another check is holding.

That took the stable-station residue to 56 — and Finding 5 then took 8 more off it. **The honest
residue is 48 unclaimed bytes; 85 = 48 unclaimed + 29 stack + 8 modelled outside this image.**

---

## ⭐⭐ Finding 4 — the census rediscovered a documented invention, with no prior knowledge

`$7E01AB` came out of the list as unclaimed. Its writer, measured:

```
ct.exe --monitor "7E01AB:1" --from saves/forest_exit.state --btnscript "0:20,40:20,10:20,0:30" --writer --diff
  f52  7E01AB=11*(C0:9E49)   ... one increment per walking frame
```

`C0/9E49` is inside `C0/9E3F` — **the party-follower speed ring**. Its head is `$01AB`, its chained
cursors are `$01AC/$01AD`, and the port's substitute for it (a `DELAY 23 / CLAMP 15` model) is already
recorded as invention that diverges with three party members.

The census had no access to that note. It found the byte from the diff alone. That is the method
working exactly as §0.2 claims it does.

---

## ⭐⭐ Finding 5 — the `$0770` pattern was the census's own blind spot, not a gap

Probed 2026-09-12. It looked like the most promising row in the table: 2 bytes of every 4, four
entries running, a shape that reads like one mechanism. It was two separate things, and **neither was
an unmodelled mechanism**.

**The "oracle changed it" half.** The census condition is *oracle changed it AND we never claim it* —
so a byte the oracle never touches cannot appear no matter who claims it. Measured, same walk:

| byte | changes in 90 frames |
|---|---|
| `$0772`, `$0773`, `$0776`, `$0777` | 1 (the frame-0 baseline only) |
| `$0770` | 14 |
| `$0771` | 25 |
| `$0774` / `$0775` | 11 / 22 |

OAM entries are 4 bytes — X, Y, tile, attr. **X and Y move; tile and attr are static.** The 2-of-4
shape is the *changed* half of the condition, and says nothing about claiming.

**The "we never claim it" half** is written in the port already, at `main.c:440`: *"the build writes
into SCRATCH buffers, not `$7E:0700`"*. `CtBuildFieldOAM` is handed `R + 0x0700` and `R + 0x0900` as
scratch (`main.c:695`). The mask watches the `$7E` image, so it can never see a claim there — **no
matter how correct the port's OAM is**. And it is correct: `oam-build-field` pins `checkoam: 10302
bytes compared over 180 frames -> 0 mismatches`.

⚠ **So the census has a third category, and it needed to exist rather than be a footnote.** Any
subsystem whose port writes to scratch instead of the `$7E` image is invisible to this method and will
report unmodelled forever, looking exactly like a real gap. `kWmElsewhere` in `ct_vm.c` names those
regions — and every entry **must** carry the check that actually grades it, or the table becomes a
way to hide a gap. Today: `$0700-$08FF` and `$0900-$091F`, both graded by `oam-build-field`.

The runs are still **listed**, tagged with their grader, never silently dropped.

**Revised residue: 48 unclaimed + 29 stack + 8 modelled-elsewhere = 85.**

## The residue — 48 unclaimed, 38 runs (forest_exit, f88, stable station)

| run | bytes | verdict |
|---|---|---|
| `$00F1 $00F3 $00F5 $00F7 $00F9 $00FB $00FD $00FF` | 8 | the joypad page, **odd bytes only** — the ROM's auto-read writes 16-bit words and the port writes the low half. Needs a writer probe to confirm. |
| `$0153`, `$0158` | 2 | `$01xx` control page. Unidentified. |
| **`$01AB`** | 1 | ⭐ **the follower speed-ring head, `C0/9E49`** — a known invention, above. |
| `$01C1`, `$01C7-C8`, `$01D3-D4`, `$01D6-D7`, `$01EE`, `$01F0-F4`, `$01F6-F7` | 15 | `$01xx` tail. Unidentified; the `$01F0+` band is adjacent to bytes `oam.c` does claim. |
| `$0770-0771`, `$0774-0775`, `$0778-0779`, `$077C-077D` | 8 | ⭐ **SETTLED — not a gap. See Finding 5.** |
| `$1684-1685` | 2 | the animset page (`$1680/$1681` are the animset bytes). |
| `$1904`, `$1984` | 2 | unidentified. |
| `$1A1D`, `$1A33` | 2 | the scripted-mover page (`$1A00/$1A01`). |
| `$1D2F`, `$1D31`, `$1D54`, `$1D56`, `$1D5C`, `$1D66-69`, `$1D70`, `$1D72-73` | 12 | the camera/player page. |
| `$2222-2223`, `$2225`, `$2227` | 4 | unidentified. |

⚠ **These are reproductions, not diagnoses.** Only `$01AB` has a measured writer. Every other row is
"the oracle drives it, we never claim it" — which is the census's job — and each still needs the
`--monitor --writer` pass that named `$01AB`. Recording them unidentified is the point: they were
invisible to every marker and to all 447 checks before this ran.

⚠ A first attempt classified the `ct.exe` list by grepping `src/*.c` for each address as a hex
literal. It reported "282 bytes nobody writes" — and it was wrong, because the port writes most of
them through computed indices (`s[base + k]`), never as a literal. That is a heuristic standing in for
a mechanism, the thing the mandate forbids; the live-engine vehicle replaced it and answered properly.

## Next

Per-row `--monitor --writer` passes, cheapest first. `$0770` is settled (Finding 5); the `$00Fx` odd-byte
pattern is the remaining one that looks like a single mechanism rather than scattered bytes, so it is
worth the most per probe. And Finding 5 is the caution to carry into each: check BOTH halves of the
census condition before calling a row a gap -- "the oracle changed it" is as load-bearing as "we never
claim it", and the port writing somewhere else is not the same as the port not writing.
