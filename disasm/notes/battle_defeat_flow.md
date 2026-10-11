# Battle DEFEAT / game-over — the flow, observed end to end (2026-08-15)

Status: **derived + observed. NOT PORTED.** The backlog carried this as "blocked on a
lose-the-battle capture"; the capture arrived (`saves/pre_derrota.state`) and the whole path has now
been driven and traced. What follows is what the ROM actually did, with the writer PCs the oracle
reported — not a reading of the disassembly alone.

## Repro (exact)

`saves/pre_derrota.state` — a live battle (PB=C1) at loc 082, storyline 15, party `00,02,04`, and
**all three members at 1 HP** (`$2603` / `$26A3` / `$2743` all read `0x0001`). Converted from the
user's snes9x capture `pre_derrota.state`; its VRAM comes from the donor base, so it is good for
RAM-level work and not for pixel compares.

```
S=$(python -c "print(','.join(['0:40,100:2']*60))")     # idle, tap A, repeat
build/ct.exe <rom> --monitor "7F01EC:1,2603:2,0100:1" --from saves/pre_derrota.state \
  --btnscript "$S" --writer
```

Driving matters: with **no input at all the battle never advances** — it sits in the command-select
state (Wait battle mode freezes time), and 3000 idle frames change nothing. The A taps let the
enemies act, and with everyone at 1 HP the wipe follows quickly.

## Observed timeline (frame, value, WRITER PC)

| frame | what | writer |
|---|---|---|
| f0 | `$7F01EC = 00` | (entry state) |
| **f51** | **`$7F01EC = 01`** — the LOSS flag is set | **`C1:8143`** |
| f132 | `$2603 = 0000` — Crono's HP zeroed | `C1:B475` |
| f224 | `$0100 = 00` — the location word cleared (battle teardown) | |
| f245 | `$7F01EC = 00` (flag cleared) and `$2603 = 0x0046` (= 70 HP restored) | `C2:95D2` / `C2:958F` |
| f287 | `$0100 = B1` then f508 `$0100 = 00` | `C0:0CB8` |

`C1:8143` is inside **C1/813F**, the routine the backlog named without ever having seen it run.

## The dispatch that consumes it — C0/0283

```
0283  JSR C0/B262 / JSR C0/0905
0289  LDA $7F01EC
028D  BEQ ->0295        ; 0 -> normal end
028F  DEC A / BEQ ->0298 ; 1 -> THE LOSS ARM
0292  DEC A / BEQ ->02CD ; 2 -> the reload-location arm
0295  BRL ->0304
0298  LDA $7E2A1F
029C  BIT #D020         ; <-- 16-bit immediate
...   (button-EDGE wait: LDA $00F0 / BNE / JSR C0/00EB)
02AF  LDA $19 / BEQ ->02C3 / BMI ->02C3
02B5  DEC $19 / JSR C0/881E / JSL FD/FFF7 / JSR C0/EC60 / BRA ->02AF   ; the FADE loop
02C3  JSR C0/0B4E
02C6  LDX #06FF / TXS   ; stack reset
02CA  BRL ->000E        ; REBOOT into the C0 dispatcher
```

**Correction to `battle_menu_synthesis.md` 1.9**, which recorded this test as `BIT #20`: the
instruction is `BIT #$D020`, a 16-bit immediate. On this capture `$7E2A1F = 00`, so the test fails
and the flow takes the **game-over** side rather than the scripted-loss continue.

Two things the fade loop settles, both already recorded elsewhere and now seen live: the world keeps
TICKING while the screen fades (`JSR C0/881E`, the player controller, runs every step), and the
brightness steps once per frame off `$19`.

## What the player sees

Peeking the same drive at ~f1200 lands on the **naming screen** (Crono's portrait + the keyboard
grid): the `BRL ->000E` reboot really does restart the machine, the title comes up, and the A taps
in the drive script walked straight into a new game. The reboot is not a metaphor — it is the whole
game re-entering its dispatcher with a reset stack.

## Boundary

The PORT is not attempted here. It needs the game-over screen, the fade loop, `C0/0B4E`, and a
reboot path our engine does not have — a feature, not a patch. What this note buys is that the
mechanism is no longer unknown: the flag, its writer, the dispatch arm, the fade, and the reboot are
all pinned, and the repro above reproduces them on demand.

---

## The module's exit after a defeat -- CF/FBE5 and CD/03B4, the victory's (2026-10-03)

C1/8164 `JMP C1/8423` -> 8443 -> 845A `JMP C1/0006` -> `JML CF/FBE5`: the defeat leaves through the SAME tail as the
victory and the run-off -- CF/FBE5's `$A4` loop (CF/FCB5-FCC3), `$0900 = FF`, CF/FCCF-FCE4 (a curHP word of 0 -> 1 on all
seven sheets), `JSL CD/0006` -> CD/03B4 (CD/039E's sound + 15 waits, CD/0516, the band, `$CA25` 0E / 08). The port left
on C1/8164's frame (`return 1` beside battleModuleTail): 42 frames early, and the party came back on 0 HP.

Then, in every exit, CD/0415-0438 (dis.py D03E8 0x62): `JSR CD/3E7D` (the `$CA25 = 08` frame's wait); `$0501/$0503 =
D1:F4C0` (the exit NMI, CtBattleTailFrame), `$45 = $BB00`, `JSR CD/044A` (a wait); then `LDA $2A1F / AND #40 / BNE
->0438`, else `LDA $002141 / BNE ->0428` -- one CD/044A wait per round until the SPC's port 1 reads 0 --, `STZ $2A21`,
CD/043E `JSL C0/000B` (C0/1BE6's chain). The port posted the chain on the `$CA25 = 08` frame.

MEASURED, the rule's cartridge -- battle_ab's enemy B6 on GATO (`tools/battle_ab.py enemies B6`; a party wipe, `$2A1F` =
bit5 set, bit6 clear -> the scripted-loss continue C0/0304, not the game over):

| frame | what | writer |
|---|---|---|
| f830 | `$7F01EC = 01` | C1:8143 |
| f911 | `$29AD`, the sheets (C1/8423-8448) | C1:8448, C1:B475 |
| f913 / f915 / f917 | `$A4` 1 / 2 / 3 | CF:E479 |
| f918 | `$2603` / `$2653` 0 -> 1, `$1E01 = 10` | CF:FCE2, CD:03A8 |
| f933 / f934 | `$CE16 = 1`, `$CA25 = 0C` | CD:051B, CD:0523 |
| f947 / f948 | `$CA25 = 0E`, `= 08` | CD:03E8, CD:0412 |
| f949-f953 | CD/0428 each frame, `$2141` 02 02 01 01 | (--hookpc CD0428) |
| f954 | C0/1BE6's chain (`$1E01` by C0:1C35) | |
| f964 | the ack, `$0118 = 01` | C0:18C1 |

The attract's 07D fight (`$2A1F` bit6 set, saves/attract_07d_battle_f4540.state): `$CA25 = 08` f616, C0/1C89's `70 00`
f618 -- the two waits and no poll.

PORTED (battle.c battleDefeatStageTick -> battleFbe5Begin/Step -> the run-off's stage 2; stage 4 + CtBattleExitPoll, the
SPC's port through CtBattleSoundSink.port; field.c's battleExitPoll). The port on B6: every frame above equal, the chain
f954, the ack f964.

After the ack C0/0304 rebuilds the map in WRAM on its own waits -- C0/A33B (cells + C0/A66B's exit plane at
$7E:7080, `$AE93 = 80` f964 by C0:A689) on the ack's frame, C0/09DD after C0/0319's wait, C0/0A14 + C0/A712 (the
treasure plane at $7E:70C0, `$95DB = 80` f966 by C0:A730) after C0/0327's, the regroup after C0/0340's, C0/28E1 after
the gate's. PORTED (field.c fieldExitPlaneWram, fieldChestPrebumpWram, fieldBattleResume's stages); the port on B6:
f964 / f966.

Open: C0/0304's C0/56D4 + C0/595C (obj0 slot 1) run on the ack's frame in the port, two frames before the cartridge's --
at ack + 2 the port's SPC answers the location's next post (`18 BA`, the 0AD's f2222) in 13 NMIs where the cartridge
answers it in its frame. And C0/1BE6's chain prices one NMI more than the cartridge's on some fights (battle_ab 6E / E3 /
F4: the ack +1; the attract's 07D: 13 NMIs against 12). Both the SPC's state after the exit's posts.
