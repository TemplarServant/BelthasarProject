# C0/595C — obj0's slot 1, re-run after every script-image rebuild (load, menu exit, battle return)

**Question (2026-09-13):** after the attract's demo battle at loc 0AD the cartridge's mailbox shows
`70 00` (C0/1C89), then `10 18` written by op EA's handler (C0/3C3C/3C41), then `18 BA 80` (op E8). The only
`EA 18` reachable there is in obj0's slot-1 function. The port never posted it. What runs it?

**Answer:** C0/595C, a synchronous runner for obj0's slot-1 function, called by the battle-return tail
C0/0330. It is the same routine the location load calls (C0/56C8, already ported — sequence_table.md 9.25), and
it has a third caller, the menu-exit reload chain (C0/0206). Nothing re-runs *every* object's slot 1: the
runner reads obj0's pointer only.

## 1. The runner (dis.py 595C 0x21)

```
595C  REP #20 / STZ $6D / LDA $7F2003 / TAX / SEP #20    X = obj0's slot-1 pointer ($7F2001 + 0*32 + 1*2)
5967  LDA $7F2001,X / BEQ ->597C                           op 00 ends the run -- it is NOT executed
596D  TXY / REP #20 / AND #00FF / ASL A / TAX / SEP #20
5977  JSR ($5D6E,X) / BRA ->5967                           dispatch through the event-op table, carry ignored
597C  RTS
```

It never touches obj0's script state: not `$1180,X` (IP), not `$1C00,X` (priority), not `$1001,X`, not the
`$7F0580` return rows. (Contrast its neighbour C0/597D, the per-object *fn0* re-run used by the party
reconcile, which writes `$1180`, zeroes the eight return words and sets `$1C00 = 7`.) A handler that parks
(returns the same IP) would spin the loop forever, so no shipped location has one in slot 1.

Byte scan `20 5C 59` in bank C0 finds exactly three callers:

| site | routine | context |
|---|---|---|
| C0/56C8 | C0/56B0 scene load | after the init runner C0/5709 |
| C0/0206 | C0/01A5-0206 reload chain, called from C0/1975 (menu resume) | after C0/56D4 re-decompresses `$7F:2000`; before C0/1978's party reconcile |
| C0/0330 | C0/0304-038C battle-return tail | after C0/56D4; before `JSR C0/A712` and the regroup C0/5B1F |

## 2. The battle-return path (dis.py 0283 0xB0, 0330 0x60)

```
18B9  LDA #01 / TRB $17 / LDA #01 / TSB $18 / JSR C0/0283      the ack, then the dispatch
0283  JSR C0/B262 / JSR C0/0905                                 C0/0905 = `LDY #0920 / LDX #2000 / LDA #14DF /
                                                                MVN $00,$7F`: $0920-$1DFF <- $7F:2000, the object tables
                                                                C0/0918 parked there (MVN $7F,$00) -- ON TOP OF the
                                                                script image, which is why C0/56D4 must re-decompress
                                                                it. Its PLB, C0/0914, is the PC the write hook reports
                                                                for $1180/$1C00 (not a separate "IP restore")
0289  LDA $7F01EC / BEQ ->0295 / DEC / BEQ ->0298 / DEC / BEQ ->02CD / (0295) BRL ->0304
0298  LDA $7E2A1F / BIT #20 / BNE ->0304                        scripted-loss continue; clear -> game over 02A0
02CD  LDA $7E2A1F / BIT $0010 / BNE ->0304                      outcome 2, vestigial (no ROM writer of 2)
0304  LDX #0900 / STX $7D / LDX #0770 / STX $7F / LDX #08A0 / STX $7B
0313  JSR 0715 / JSR A33B / JSR 00EB / JSL C0/29F7 / TSB $47#09 / JSR 09DD / JSR 00EB / JSR 0A14 / JSR 56D4
      (C0/0715: $F0/$F1 from $1D0A/$1D0E, JSR 076B the enemy epilogue, JSR 0871 the PC snap, JSR B271, and for
       $7F01EC == 1 only, the defeat-pose staging 0738-075A)
0330  JSR C0/595C
0333  PHD / DP := $1D00 / JSR C0/A712 / PLD / JSR 00EB / JSL C0/2B78 / TSB $47#02
034B  the regroup ($2A1F bit7 skip; $99/$9B followers -> C0/5B1F; none -> $011F = 1)
```

## 3. Oracle evidence

`build/ct.exe --lifetrace` (opcode-stepped; W = write, X = PC hit; a write's PC is the next instruction):

**Loc 0AD, attract** — `--from saves/attract_loc0AD_flea.state --frames 330 --wwatch 7E1E00:4,7E1180:2
--pcwatch C0/0283,C0/0330,C0/595C,C0/5977,C0/597C,C0/A712`

```
f260  W $1E01 <- 00 @C0/1C84   W $1E00 <- 70 @C0/1C89          C0/1BE6's bit6 arm
f261  X C0/0283                W $1180 <- EC @C0/0914
f268  X C0/0330  X C0/595C
      X C0/5977 X=002C Y=01F2  X=002C Y=01FF  X=01D4 Y=0204     16@0032 16@003F EA@0044 (live = body + 14*32)
      W $1E01 <- 18 @C0/3C3C   W $1E00 <- 10 @C0/3C41
      X C0/5977 X=01C8 Y=0206
f269  X C0/5977 X=01C8 Y=020E / 0216 / 021E, X=0020 Y=0226    E4@004E E4@0056 E4@005E 10@0066
      X C0/597C X=022B                                           end@006B
      X C0/A712
f274  W $1180 <- F0 @C0/5AC2                                     the VM visits obj0 again
f311  `18 BA 80` (op E8, C0/3C1C-3C26)
```

No write to `$1180`/`$1C00` between C0/595C and C0/597C. Slot 1's first op, `16 DF 01 06 09`, jumps from
0x0032 to 0x003F: **the HIDE obj5/obj11/obj13 + SHOW obj12 block is skipped on the cartridge** in this state,
so the run changes no visibility; its visible ops are the song post and the four `E4` copies, which re-copy
the tiles the load's own run (C0/56C8) already copied.

**Loc 082, a two-Diablos fight** — `--from saves/battle_re/party3_field.state --btnscript
"40:150,(100:20,0:40) x45 expanded,0:250"`: the C0/1C09 restore chain `81 00 / 11 3C / 81 40 / 82 00 / 83 00`
at f2609-f2647, C0/0283 f2648, C0/0330 -> C0/595C at f2667 dispatching `16@009F 12@00B6 12@00C3 12@00D0
12@00DD`, C0/597C at `056A` (= body 0x00EA + 36*32), then C0/A712 in the same frame.

**Loc 082, menu exit** — `--from saves/grupo_3_mapa.state --btnscript "0:20,200:2,0:90,1:2,0:150"`: C0/1975 at
f121, C0/0206 -> C0/595C at f154 with the same five-op trail; C0/597C.

Also run on `saves/menu_open_gato2.state` (B closes): C0/1975 -> C0/0206 -> C0/595C -> C0/597C with no op
dispatched (that location's slot 1 is a bare `00`).

## 4. The port

- `CtSceneRunObj0Slot1(sc, site)` (src/ct_vm.c) is the runner: saves obj0's `CtSS`, steps slot 1 with the live
  stepper until op 00 (or a park), restores the saved state. The load site calls it (unchanged behaviour: the
  load still forces `prio = 7`, `dc = 1` and lands queued copies before frame 1).
- `fieldBattleResume` (src/field.c) is C0/0304-038C: it runs for `$7F01EC` 0, for 1 with `$2A1F` bit5, and for
  >= 3; it calls the runner, then (outcome 0, as before) `CtSceneBattleReturn`. Both battle paths call it right
  after `fieldBattleFinish`; the interactive one AFTER the C0/1BE6 sound chain, which keeps the cartridge's
  mailbox order (the ROM's 1BE6 posts at f260, one frame before the dispatch).
- The menu exit in field.c calls the runner first thing in the C0/011B restore, before the party reconcile.
- `CT_SLOT1TRACE=1` prints `slot1: <site> ops OP@ADDR ... end@ADDR obj0 @A pP -> @A pP`.

Graded by `tools/slot1_runner_cmp.py` (check `slot1-runner-battle-menu-vs-oracle`): trail, obj0 untouched and the
mailbox window, all three scenarios, identical to the cartridge.

## 5. Open questions / boundaries

- **Timing.** The cartridge spends f261..f274 in the reload chain with the VM frozen (00EB ticks, the
  decompress) and the runner fires 7 frames after the dispatch (19 at loc 082). The port runs the whole tail on
  the battle's end frame and resumes the VM on the next tick; only the ORDER is graded.
- **Scripted-loss continue.** C0/0304 (so C0/0715's epilogue + PC snap, C0/0330's runner and the regroup) is
  shared with `$7F01EC = 1` + `$2A1F` bit5. The runner now follows the ROM there; the port's epilogue and
  regroup (fieldBattleFinish / CtSceneBattleReturn) are still gated on outcome 0 — no captured state exercises
  a scripted loss, so that gap is recorded, not changed.
- The C0/0206 reload chain's other work (`$AE` -> C0/E12A, the `$7F03FE` regroup completion at C0/0215-024B)
  is not examined here.
- An `E5` (queued copy) in slot 1 at the battle return or the menu exit would arm the port's copy freeze on
  those frames; no location in the repo's states has one.

## 6. The map the copies land on (2026-09-25)

Every caller of C0/595C runs it over a map that was just built in WRAM, and slot 1's `E4` copies (C0/3D97 -> `JSR C0/AF4E`)
are written onto that map:

- the location load: `0088 JSR C0/00F4` = `00FA C0/0960 / 00FD C0/6DCF / 0100 C0/7084 / 0103 C0/7F7E / 0106 C0/A33B /
  0109 C0/09DD / 010C C0/0A14 / 010F C0/56D4 / 0112 FD/FFFA / 0116 FD/FFF4`, and only then `008E JSR C0/56A6` ->
  C0/5709 (the init runner) -> `56C8 JSR C0/595C`;
- the menu's reload C0/01A5: `01C3 C0/0960 / 01C6 C0/6DCF / 01C9 C0/7084 / 01CC C0/7F7E / 01CF C0/A33B / 01D2 C0/09DD /
  01D5 C0/0A14 / 01D8 C0/56D4`, the `$7F:1D1E..26` restores, then `0206 JSR C0/595C`; C0/1A03, C0/B192 and C0/2824 ->
  C0/286C (`286C JSR C0/28E1`, `286F JSR C0/0A50` = the camera C0/74A6 and the tilemaps) come after.

Loc 002's slot 1 (`ct.py script 2 --obj 0`, fn1 @010E) is `16 5C 01 80 06 / 4A .. / E4 13 02 1B 0C 13 12 3B /
E4 13 02 1B 0C 13 12 3C / 16 42 40 86 11 / E4 33 32 3B 3C 13 02 3B / E4 33 32 3B 3C 13 02 3C / 00`: the curtain rect (rows
$02-$0C, cols $13-$1B) is parked at rows $12-$1C, and with `$7F:0142` bit 6 the open curtain (rows $32-$3C) is pasted over
it. Mask $3B = planes $7E:3000 / 3040 / 30C0 / 7000 / 7040; $3C = the bit-2..5 planes.

MEASURED, the logical oracle. Seed `saves/field_re/loc002_curtain_ctl.state` (`saves/wake.state`, `100:2,0:10` x53 then
`0:90`; control returns at f664; `$7F:0142` = 40):

- **reload** (X f4, B f60): C0/A33B at f82 writes the closed curtain (`$7E:3253..` = `..3D3E3F..`), C0/0206 at f100 with four
  C0/3D97 visits, and from f100 the rect holds the open curtain (`3A3B3C`) on all five planes. The port, before: the
  closed curtain from the RTL frame on, for good.
- **load** (`--teleport 2 8 8 1` from the seed, C0:0066's loader): rows $02-$0C open, rows $12-$1C the closed curtain parked
  (`$7E:4253..` = `3D3E3F`). The port, before (`--reentertest 2`): the bare decompress in both.

The code order was only half of it. The port's live VM (ct_vm.c `step()`) handled E4/E5 by queuing the RENDER's copy
(`tcQ` -> CtScenePollTileCopy -> CtFieldSceneCopyTiles / fieldCollCopyTiles) and never ran C0/AF4E on WRAM; the WRAM body
existed only in move.c's runner, graded by --sweepops. So no scripted copy had ever reached `$7E:3000`.

The port now:

- `CtVmTileCopyE4` (move.c) is C0/3D97, one body for both callers: CtFieldRunnerOne (--sweepops) and `step()`, which calls it
  on the host WRAM beside the render's queue.
- The menu reload (field.c, the RTL block) stages the map (fieldMapHeaderStage) before `CtSceneRunObj0Slot1(.., "C0/0206")`,
  then the reconcile, `fieldDoorListApply("reload")` (C0/28E1), the camera entry + the A385 decode, the tilemap fill.
- `fieldLoad` stages the map before `fieldBgAnimLoad` (FD/FFFA / FFF4) and `CtSceneCreateIn` (C0/56A6); the cold entry and the
  transition no longer stage after the scene is built.
- The A385 layer-scroll decode stays beside the camera entry in both paths: CtSceneCameraEntry carries C0/7F7E's clear of
  the `$1D` page, which in the ROM runs before A33B, so the decode has to follow it.

Graded by `tools/slot1_map_copy_cmp.py` (check `field-slot1-map-copies-vs-logical-oracle`): reload 55/55 rows the same at the
end, the only differing run f21-f99 (the menu program's own use of $7E:3000 and the reload's NMI-off frames, the port's frame
model); load 110/110 rows identical. The C0/7084 pass now also runs at every load; a `--battletest` seed, whose load
previously never called fieldWindowFromRom, draws its window with the ROM's characters (graded on the cartridge's `Ran away!`
frame: the changed 8x8 cell 64/64, was 49/64).

Boundaries (not changed here):

- **E5.** The queued copy (C0/3DC5: `$013E-$0145`, `$0117` bit 5) is drained by C0/1770 -> `1790 JSR C0/AF4E`. PORTED
  2026-09-26 as the VM's `$0117` bit-5 service (disasm/notes/field_tile_copy_e5.md), which replaced the copyFreeze
  approximation and the field consumer's `entry` machine.
- **The init pass** (`runFunc`, the fn0s of C0/5709) has no E4 arm.
- **The `$1D` page.** A slot-1 op writing `$1D24..` (E6) would survive C0/7F7E's clear in the ROM (it runs before slot 1)
  but be cleared by the port's CtSceneCameraEntry, which runs after.
- **The battle return** (C0/0304 -> `0316 JSR C0/A33B`) still does not re-decompress the map in the port, so C0/0330's copies
  land on the map the battle left (field_door_list.md section 6).
