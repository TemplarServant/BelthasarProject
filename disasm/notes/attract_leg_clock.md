# The attract's leg clock -- port vs the LOGICAL oracle, leg by leg (2026-09-23)

User report (translated from Portuguese): *"the demo scene is ending a little before the music"*. The attract posts song `$18` once (loc 433's
`EA 18`) and fades it with its script's `$81`; nothing in the ROM re-syncs the picture to it, so every frame a leg
loses or gains moves the fade within the song. Against the REAL cartridge the gap has three parts (memory
`ct-attract-music-sync`): the host clock (fixed, 3a274a9 -- `audio_output_pacing.md` §6), 76 lag frames (the zelda3
model, CLAUDE.md), and the port's own leg lengths against the LOGICAL oracle -- this note.

## 1. How it is measured

Both sides dump the same WRAM bytes every frame -- the logical oracle with `--monitor`, the port (lockstep) with
`CT_FIELD_MEMMON` -- and a leg is the span between two `$0100` writes:

```
CT_ORACLE_LOGICAL=1 ct.exe --monitor 7E0100:2,7E0119:1,7E1E00:2,7E0384:1,7E011E:1,7E0153:1,7EA013:1,7E29C6:1 \
    --from saves/attract_cena_inicial.state --frames 9120 --diff
CT_NOPACE=1 CT_AUDIO_LOCKSTEP=1 CT_FIELD_MEMMON="0100:2,0119:1,1E00:2,0384:1,011E:1,0153:1,A013:1,29C6:1" \
    CT_SHOTS=x CT_SHOTS_AT=11200 CT_SHOTS_EXIT=1 build/ct_field.exe --assets ct_assets.dat --scene 433
```

⚠ The port's memmon line can be CUT by an interleaved stderr print (`[snd] ...`); a parser that takes a cut line's
`0100=B1` as a location write splits a leg in two and misaligns every leg after it. Drop lines whose fields are
not all there at their full width.

Song `$18` post -> the title's song 0: cartridge 9068 frames, logical oracle **8992**, port 8957 before this note.

## 2. Op 0xD4 (C0/394E, LEAVE) ends the visit on its last `CMP` -- FIXED

The first hub (loc 433, obj1 fn3: `EA 18`, then `D4 05 / D4 02 / D4 01 / D4 04`, then `E0 F5 03 09 09`) was 8
frames short. `--hookpc` on the logical oracle: the post at f1 (the JSL holds C0/2848's ramp to f31), `C0/394E` at
f35, f39, f43, f47 -- ONE leave per visit -- and `C0/3AB7` (`E0`) at f51. The port ran the first leave at t47 and
the next THREE in one visit at t51, so its `E0` came 8 ticks early.

C0/394E scans the party OBJECT slots `$0197/$0199/$019B`; for each non-empty one `LDA $1101,X / CMP $D9`, and the
exit carry -- which `5AB4 BCC` turns into the end of the visit -- is the last `CMP` that ran, or the entry 0
(`5AAD ASL A` is 16-bit) when `3998 BMI ->39B1` skips them all. The live VM's arm scanned the MEMBERSHIP array
`$2980` instead, "on the assumption that the two agree on emptiness". In the hub they do not: logical oracle f0-f47,
`$0197/$0199/$019B = 80 80 80` and `$2980 = 00 01 02`. So every hub leave ends its visit on the cartridge, and the
port's arm matched Lucca and Marle, compared EQUAL (carry 1) and ran on. `$2980` goes `00 01 82` (f39) -> `00 82 82`
(f43) on both sides.

The fix routes 0xD4 through `CtVmPartyGateFamily` (move.c, the --sweepops-graded C0/394E), as 0xCF/D0/D1/D7/F8-FA
already were. First hub: 58 -> **66** frames, the logical oracle's.

## 3. The PC loader writes `$1101,X` and `$018D+id` -- FIXED

With 0xD4 byte-exact, the 0AD leg went +3 -> +13: its closing `D4 01 / D4 04 / D3 03 / D3 02` runs with the party ON
the field. Logical oracle f2286: `$0197/$0199/$019B = 02 04 0A -> 02 08 80` and `$2980 00 01 04 -> 00 03 84` in ONE
visit (the two leaves match, carry 1, and `D3 03` ends it), `D3 02` at f2290. The port's `$1101` for objects 1-8
read `00` where the cartridge holds `00 01 02 03 04 05 06 7E`, so no leave matched an object.

`--monitor 7E1105,7E1107,7E110B,7E1111 --writer`: C0:4298 / 4327 / 43DA (the PC loader's three party arms and its
not-in-party arm) and C0:4567 (an NPC loader). C0/421E-4268 (op 0x80) and C0/41E4-4214 (the seven fixed-char ops
0x57/0x5C/0x62/0x6A/0x68/0x6C/0x6D) store `$6D -> $018D+id` (op 0x80 only for id <= 6: the `CMP #n` chain has no
default arm) and put the id in `$D9`; C0/426A's arms store `$D9 -> $1101,X` on every path (4295 / 42B0 / 4324 /
43D7). move.c's `ctVmPcLoad` had both; the live loaders (init pass and live ops) now call `pcLoaderIdStores`. 0AD:
**+5**, and its closing leaves run three ops in the f4369 visit and one in f4373 -- the cartridge's split.

Still open here: the live op 0xD3 arm edits `$2980` only; C0/3919 also places `$018D+id` into the first empty
party-object slot (`3933 LDX $D9 / LDA $8D,X`). It moves no frame in the attract (the next load resets the slots,
C0/0C1E) and is not routed yet.

Pinned by **attract-party-leave-visit-vs-logical-oracle** (lockstep, `CT_FIELD_MEMMON=0100:2,2980:3`): the hub's
leaves at post+38 / +42 and the 1F5 at post+66, the 0AD's `00 03 84` then `00 03 02` four frames apart. Three
mutations killed (a leave always continuing, no `$1101` store, 0xD4 not delegated).

### What moved with it (and one frame it cost)

Every attract anchor after the first hub moved, per vehicle, measured on the `$0100/$0119/$1E00` timeline of the
old binary against the new: `--selftest` +8 to the 1F7, +12 from the 0A5 hub, +16 from the 0E6, +15 inside entry 3,
+19 087, +23 from the 1F3 hub; lockstep +8 to the 0AD fight, +9 in it, +10 after, +14 0A5/07D, +18 0E6, +22 087, +26
from the 1F3. 42 checks were re-anchored to the same content at the moved frame (one, attract-fade-rate-01B, also
gained 1402 identical pixels -- its bottom band, in place, cause not isolated).

⚠ **The 0AD fight's module frames moved +1 against the CARTRIDGE.** Its boot `70 01` (C0/1BDF) now crosses 14 NMIs,
returning on line 4 (was 13, line 110; the cartridge's 13), and the return's `70 00` (C0/1C89) 16 (was 15). The
driver answers from where song $18 is: the port posts 1750 frames into it, the cartridge ~1745, the old build 1742.
The ~5 frames are the §5 load residuals before the fight; the hub's -8 had hidden them and landed the phase on 13 by
chance. The two 0AD battle checks pin the +1 and say so. The same SPC phase makes the selftest's entry 3 open one
frame sooner against its hub (its `$2143` wait) and the reset's re-upload JSL cost 7434248 master cycles, not 7410474
(still 20 frames).

After both: post -> song 0 = 8983 (logical 8992). Per leg, port - logical:

| 1F5 | 16E | 01C | 01B | 0AD | 112 | 0DB | 1F7 | 1B1 | 0A5 | 07D | 1B1 | 0E6 | 1F2 | 1B1 | 06B | 1B1 | 087 | 1B1 | 1F3 | 1B1 | 1B0 | 1F0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| +3 | +2 | 0 | +2 | +5 | -16 | +4 | +3 | +1 | -11 | -9 | +2 | -15 | +3 | -3 | -1 | +4 | +16 | +2 | +3 | +1 | -1 | +2 |

## 4. ⚠ The runner's BEAM BUDGET -- C0/5A46 (not ported, and not portable without a CPU clock)

loc 112 (-16): obj0 waits (`04 08 17`) on obj4 fn7, a walk of three `96` MOVETOs. The second and third match the
cartridge to the frame (56, 39); the FIRST takes 107 frames on the cartridge and 95 in the port -- and obj3/obj1's
first legs lose the same. Their start positions are identical; the difference is CADENCE: on the cartridge obj4
steps (+$27) every other frame and obj1/obj3 two frames in three; the port steps them every frame.

C0/5A46, the runner's queue loop, reads the BEAM before every visit:

```
5A49 LDA $2137 / 5A4C LDA $213D / XBA / LDA $213D / AND #01 / XBA     V counter
5A56 REP #20 / CMP #00F0 / BPL ->5A61 / 5A5D CMP $69 / BCS ->5A90     vPos >= $69 (= $48, line 72): END THE QUEUE
```

and the visits left in the queue wait for the next frame. `--hookpc C05A90` on the logical oracle: from f2453 (the
walk) the runner leaves with A = $48..$4D (cut) almost every frame, where before it left with A = $80 (queue
empty). Frames cut per leg over the whole attract (logical oracle, 9000 frames): 112 **63**, 0A5 **154**, 0E6 11,
07D 7, 087 16, every other leg 0. The legs the port runs short are the legs the beam cuts.

The port's runner (`CtSceneTick`) keeps `rasterLine` at its measured start (12) and moves it only by a priced
sound post (song_load_freeze.md §6), so the cut never fires on CPU work. Deciding it needs the CPU's position in
the frame at every visit: the NMI and C0/0097's head before the runner, then each visit's handler path (C0/4F61's
re-aim, C0/ABA2, C0/ACFD, ...). That is the class CLAUDE.md's ONE exception names -- the CPU's timing against the
beam -- but the exception covers C3/02DD's reads and lag frames only; C0/5A46 is neither. **Not ported; a decision
for the user** (price the runner from the listing, the way audio.c's LdClock prices the song loader, or extend the
exception).

## 5. Open, measured

* The +1..+5 on nearly every load (1F5, 16E, 01B, 0AD, 0DB, 1F7, 1F2, 1F3, 1F0, the hub visits): no beam cut in any
  of them. Not yet derived (field_load_cost.md §7 lists the unpriced parts).
* 087 (+16) and 07D (-9) are battles; their field parts carry 16 / 7 beam cuts.

## 6. op 0xFF < $90 fades before it jumps (C0/3FBB-3FD1) -- FIXED 2026-09-28

Re-measured 2026-09-28 against the oracle UNDER THE RULE (CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1; the runner's
deadline C0/5A46 is the fixed V = 0 on both sides since 30ccdce, so §4's cuts are gone from both): 112 -3, 0A5 **-11**,
07D -8, 0E6 **-15**, the rest +-1..+6. 0A5 and 0E6 were ONE mechanism. Their hub (1B1) runs `DE <loc>` and `FF 61` inside
C0/2848's ramp; `FF 61` is C0/3FAF's < $90 half, whose prologue is a fade of its own -- `3FBB LDA $19 / BEQ / BMI ->3FD3`,
else `3FC1 DEC $19 / JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60`, `3FCD LDA $19 / CMP #01 / BPL ->3FC1` -- and only then
3FD3 `JSR C0/0B4E` / `JML $C3:0000` (the credits page). Logical oracle, hub before 0A5: the ramp's INC to 0D and the two
ops in one frame (f3294, $0119 = 0C), 0B..00 f3295-3306, C3:0000 at f3307 line 239; before 0E6 the ramp had reached 0F
(DE/FF at f5256, C3:0000 at f5271). The port opened the page on the frame after the op. Every credits page of the demo
ticks first at J + 18 and reboots at J + 122 on the cartridge (J = the jump's frame), so the whole difference was the
fade: J = T + $0119 at the op. Ported as the op's own loop for both targets (gate_warp_fd_family.md, 2026-09-28).

Per leg after it, port - logical oracle (lockstep):

| 1F5 | 16E | 01C | 01B | 0AD | 112 | 0DB | 1F7 | 1B1 | 0A5 | 07D | 1B1 | 0E6 | 1F2 | 1B1 | 06B | 1B1 | 087 | 1B1 | 1F3 | 1B1 | 1B0 | 1F0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| +4 | +2 | -1 | +2 | +6 | -3 | +4 | +3 | +2 | **+2** | -9 | +2 | **0** | +3 | **+8** | -1 | +5 | +3 | +2 | +3 | +2 | -3 | +3 |

The 603-frame 1B1 (the race, then a credits page) went -2 -> +8: its two `FF`s met the ramp at 05, and the ten frames
their missing fades took had been hiding what §7 measures. 087 moved 0 -> +3 and 07D -8 -> -9 with no fade of their own:
the battle return's driver JSL C0/1C89 (`70 00`) waits on the SPC, and the SPC's phase against the picture moved with the
timeline (CT_SNDTRACE, old binary -> new): 07D 13 -> 12 NMIs, 087 13 -> 16. The logical oracle (--hookpc C01C89 / C01C8D):
07D 12, 087 12 -- and the boots C0/1BDF 14 / 14 / 14 against the port's 14 / 13 / 13. Its SPC also runs through the
rule's holds and the port's does not (audio_output_pacing.md sec. 7), so these waits are a phase class of their own, not
this fix's.

## 6b. The count after the load -- FIXED 2026-09-28

The load's fill leaves every scheduler countdown `$1001,X` at 00 and the first queue build visits every object
(frame_wiring.md, "RETRACTED 2026-09-28"); the port wrote the period there, so each object of period 4 made its first visit
3 frames late. Per leg after it, port - logical oracle (lockstep, same measurement as §6):

| 1F5 | 16E | 01C | 01B | 0AD | 112 | 0DB | 1F7 | 1B1 | 0A5 | 07D | 1B1 | 0E6 | 1F2 | 1B1 | 06B | 1B1 | 087 | 1B1 | 1F3 | 1B1 | 1B0 | 1F0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| +4 | **-1** | -1 | **-1** | **+1** | -4 | **+1** | +3 | +2 | **-1** | -10 | +2 | 0 | +3 | +8 | -1 | +5 | **+1** | +2 | +3 | +2 | -3 | +3 |

The legs' total |port - oracle| went 73 -> 62 frames. The 0AD battle's boot `70 01` (C0/1BDF) now crosses the cartridge's
13 NMIs (14 before) and the return's `70 00` (C0/1C89) 15 (16): the port posts song $18 where the cartridge does, and every
frame of that fight is the cartridge's (attract-0ad-battle-turn-machinery-vs-oracle). The whole-demo divergence audit
against the rule's cartridge went from 35 events to 31: 16E's clouds (sev 427 -> 3), 0A5's four events and 0AD's late Flea
are gone; 112's curtain and 0E6's robots shrank (sev 517 -> 392, 347 -> 304) -- what is left there is not the count.

## 7. The load's clock, measured (open)

Every hub load (1B1) lights one or two frames late: the ramp's first INC is 5 or 6 frames after C0/0000 on the logical
oracle and 7 in the port. The cartridge, `--cputrace` with the beam from the hub's three kinds of entry:

| entry | NMI off | NMI on (C0/0B3F) | W (cycles) | floor(W/F) | INC |
|---|---|---|---|---|---|
| world 1F7 (C2/24D7) | f17 line 98 | f21 line 247 | 1 811 572 | 5 | f22 |
| after the credits (7E:302F) | f10 line 231 | f15 line 260 | 1 835 044 | 5 | f16 |

(F = 357 368; the phase-0 hold of the rule at the first vblank after the NMI goes off is inside W.) The INC lands on the
iteration I + floor(W/F) + 1, I the iteration the NMI went off in (the one that began at the vblank before it). The
port's window is `(I + 1) + ceil(W'/F') + 1`: it loads one iteration after the commit (a documented stand-in for the
unpriced object/script load) and rounds up, and its W' is the priced work plus LOADC_FIRST_PASS = 1 426 744 -- C0/286C's
pass measured on scenes 468/122, where it is 1046 lines; the hub's pass (C0/2848 -> C0/0B3F) is 1 150 332 cycles, 843
lines, C0/0A50 alone 968 832 of them. So the fix is the one field_load_cost.md §7 names -- price C0/286C per location
(what C0/0A50's builder costs) and the loader's non-decompression work, then charge from the NMI-off's iteration with
floor + 1 -- and it moves every load of the suite. Also measured: a translated stage opened on J + 1 ends its run one
frame later than the cartridge's (the race: J + 425 there, J + 426 here), and a stage's `JML $C0:0000` is loaded on the
frame after it.

### 7b. Measured 2026-10-13: every attract load's window, and where loc 112's frame goes

The rule's cartridge (`CT_ORACLE_LOGICAL=1`, saves/attract_cena_inicial.state, `--hookpc C00B5D` / `C00B3F`): the
window W from the NMI going off (C0/0B5D) to it coming back (C0/0B3F), against the port's 1 + black frames (CT_LOADCOST):

| loc | W (frames) | port | | loc | W | port |
|---|---|---|---|---|---|---|
| 16E | 17.96 | 17 | | 0A5 | 26.99 | 25 |
| 01C | 33.99 | 32 | | 07D | 47.99 | 47 |
| 01B | 38.09 | 38 | | 0E6 | 36.99 | 36 |
| 0AD | 25.11 | 24 | | 06B | 21.99 | 21 |
| 112 | 29.99 | 29 | | 087 | 36.99 | 36 |
| 0DB | 22.99 | 24 | | 1B0 | 22.99 | 22 |
| hub 1B1 | 4.97 | 7 | | | | |

The price is short by one or two frames on most field loads (the hub over by two); the one-iteration-after convention
and, after a $C3 sequence, the host seeing the sequence's done flag a frame late (CtTitleIntroTick runs after the
check) put most of them back. Loc 112, leg by leg (`--hookpc`, frame / line): C0/005D f2392 l228, C0/00F4 l230, the
song's JSL to l260, C0/0960 l260, C0/6DCF f2404 l42, C0/7084 f2405 l237, C0/A33B l247, C0/09DD f2410 l161, C0/0A14
f2412 l95, C0/56D4 f2413 l40, C0/008E / C0/56A6 f2414 l45, C0/28AA f2417 l164, C0/2848 / C0/286C l165, C0/0B3F f2422
l225. Before the pass the cartridge spends ~8.59M non-refresh cycles where the port prices ~8.27M: the gap is the
OBJECT INIT, C0/56A6 .. C0/28AA (3.45 frames), whose script runs the port does not price (only their sprite
decompressions, g_ctLzSpriteCycles) -- and pricing them needs every VM op's cycle cost (section 4's class).
Consequence met: moving the battle entry's C0/1BAB post to C1/0000's frame (the cartridge's, which makes enemy 07's
fight end exact) changes the SPC phase at 112's song load (a 3-line handshake becomes 23; the cartridge's is 29), the
rounded price goes 28 -> 29 black and 112 runs 422 frames against the cartridge's 421 -- HEAD's 421 is the two errors
above cancelling the short price.
