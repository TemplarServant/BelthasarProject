# The DEFEAT path — post-battle outcome 1 with `$2A1F` bit5 clear (GAME OVER → reboot)

Closes the port's oldest battle-side placeholder. `src/field.c:4139-4143` today prints

```
battle: GAME OVER (party defeated, $2A1F bit5 clear) -- reboot sequence unported, input frozen
```

and sets a `gameOver` flag that gates the player controller, the talk key, the X-menu and movement
(field.c:5447/6241/6374/6451). **Losing a battle in `ct_field` is therefore a permanent freeze** — the
window stays up, nothing responds, only Escape quits. This dossier derives the ROM's real behaviour.

Every line below is `tools/dis.py` output on the USA ROM. Nothing here is inferred.

---

## 1. Where the defeat path starts — the outcome dispatch `C0/0283`

```
0283: JSR  C0/B262
0286: JSR  C0/0905          ; MVN $00->$7F block move (0x14DF bytes) -- state stash
0289: LDA  $7F01EC          ; the battle OUTCOME cell
028D: BEQ  ->0295           ;  0 -> normal continue
028F: DEC A
0290: BEQ  ->0298           ;  1 -> the DEFEAT branch (below)
0292: DEC A
0293: BEQ  ->02CD           ;  2 -> vestigial (no ROM writer; see battle_menu_synthesis.md 1.9)
0295: BRL  ->0304           ;      the field-resume tail
```

The `1` branch splits on one bit:

```
0298: LDA  $7E2A1F
029C: BIT  #20
029E: BNE  ->0304           ; bit5 SET  = SCRIPTED loss -> just continue (GATO's tutorial case)
                            ; bit5 CLEAR = real GAME OVER, falls through to 02A0
```

The bit5-set half is already ported (field.c:4129-4137, the `$1780,Y=3 / $1301,Y=$0F01,Y` defeat-pose
staging from `C0/0715`). The bit5-clear half is what follows.

## 2. The game-over sequence, byte-exact

```
; ---- (a) WAIT for any button EDGE, running the game meanwhile ----
02A0: LDA  $00F6            ; remapped edge byte (X=0x40, Y=0x01, Sel=0x04, B=0x0A -- menu_entry.md 1)
02A3: BNE  ->02AF
02A5: LDA  $00F0            ; raw edge byte  (Start=0x01 Sel=0x02 Y=0x04 B=0x08 X=0x40 A=0x80)
02A8: BNE  ->02AF
02AA: JSR  C0/00EB          ; ONE game frame: JSR C0/881E (player controller) + JSR C0/00DE
02AD: BRA  ->02A0           ; spin

; ---- (b) SCREEN FADE-OUT, one brightness step per frame ----
02AF: LDA  $19              ; $0119 = the INIDISP brightness shadow (NMI C0/EC4E -> $2100;
02B1: BEQ  ->02C3           ;         0 substitutes forced-blank #80). NOT a music volume --
02B3: BMI  ->02C3           ;         see the CORRECTION block in battle_menu_synthesis.md.
02B5: DEC  $19
02B7: JSR  C0/881E          ; the world keeps TICKING while it fades (controller still runs)
02BA: JSL  FD/FFF7
02BE: JSR  C0/EC60          ; frame wait: $0158 = ($0158+1) & 0x83FF; INC $0152; spin til NMI clears it
02C1: BRA  ->02AF

; ---- (c) QUIESCE + REBOOT ----
02C3: JSR  C0/0B4E          ; SEI / DBR=0 / $2100=#80 force blank / $4200=0 / $420B=0 / $420C=0
02C6: LDX  #06FF            ; (needs 16-bit X to disassemble: A2 FF 06 = LDX #$06FF, 9A = TXS.
02C9: TXS                   ;  With x=1 the tool mis-splits it as "LDX #FF / ASL $9A".)
02CA: BRL  ->000E           ; full re-init
```

Note what (a) and (b) share: the loop bodies keep calling `C0/881E`, the player controller. The party
is dead but the engine is not paused — the game keeps running one frame at a time while it waits for
the press and while it fades. Any faithful port must keep ticking, not stall.

## 3. Where `BRL ->000E` lands

```
000E: SEP #20 / REP #10
0012: JSR  C0/0B64          ; rebuild the NMI trampoline at $7E0500 (LDA #5C 'JML' / $0501 / $0503=C0)
0015: JSR  C0/0B75          ; rebuild the IRQ trampoline at $7E0504
0018: LDA #0100 / TCD       ; D = $0100
0020: JSR  C0/0B4E          ; quiesce again
0023: LDX #0500 / STX $4E   ; set up the WRAM block clear ($4B/$4D/$4E)
0028: LDX #0000 / STX $4B
002D: LDA #7E   / STA $4D
0031: JSR  C0/2DF1
      ...                   ; and on to the JSL $C7:0000 sound-driver upload (apu_upload.md)
```

`$C0:000E` is the **reset-time init** — the same entry the console reaches out of `$FD:C000`. So the
defeat path is a genuine cold reboot: WRAM re-initialised, the Akao driver re-uploaded to the SPC700's
ARAM, and control handed to the title/attract chain. Chrono Trigger has no separate "GAME OVER"
screen; death returns you to the title.

## 4. Port plan (NOT yet implemented — scoped and bounded here on purpose)

Steps (a) and (b) port cleanly and are the whole user-visible defeat experience:

1. **(a)** Replace the freeze at `field.c:4142` with a game-over MODE, not a dead flag: keep the
   field/battle tick running, poll for a fresh key edge (ct_field already tracks edges — the
   `!e.key.repeat` KEYDOWN block at field.c:5078).
2. **(b)** On the edge, ramp the existing frame brightness down one step per frame while the tick
   continues. ct_field already owns a brightness path — the `fadeBri`/`FADE_STEP` machine at
   field.c:5106 — so this is the same ramp the door transitions use, driven from the same
   canonical `$0119` semantics.
3. **(c)** is where the honest boundary sits. `BRL ->000E` is a cold reboot into the TITLE/attract
   chain, and `ct_field` has no title screen (`ct-field-newgame-boot`: the port's cold entry is
   `--newgame` → scene 0 naming → loc 496 pan → loc 271 → loc 2). Rebooting into the *new-game*
   chain is the nearest faithful endpoint and must be labelled as the approximation it is: the ROM
   reaches the title first. Do NOT dress it up as byte-exact.

**Validation.** A defeat has never been captured — `battle_menu_synthesis.md` records "Defeat path
unexercised ... a deliberate-loss capture (let GATO counter ~15x) would close it", and that is still
true: everything above is disasm-derived, with no frame-level oracle observation of (a)/(b)/(c). The
capture that would close it: drive a battle to a party wipe under `--peek`/`--monitor` with a watch on
`$7F01EC`, `$7E2A1F` and `$0119`, and confirm the edge-wait, the one-step-per-frame ramp and the
`$C7` re-upload (the last is now directly observable with `ct.exe --aram`).

## 5. Corrections this pass makes to earlier notes

- `C0/02C6` is `LDX #$06FF / TXS`, not `LDX #FF / ASL $9A` (a 16-bit-X disassembly artefact).
- `C0/0B4E` is a PPU/IRQ **quiesce**, not a sound routine.
- The `$19` drain in (b) is a **screen** fade, not a music fade (full derivation in the `$0119`
  CORRECTION block at the end of `battle_menu_synthesis.md`).
- `field_op_e0_fadechange.md` line 142 calls `C0/02A0` "the title-attract frame loop". That reading
  is understandable — the loop shape is identical and it does reboot to the title — but `C0/02A0` is
  reached from `C0/0298`, i.e. only from the post-battle outcome dispatch. It is the DEFEAT path.

---

## 6. PORTED, and captured (2026-09-14)

**The capture this dossier asked for.** `ct.exe` modes now take `PEEK_POKE="ADDR=HH,..."` on any loaded state, so a party wipe
is cheap: `saves/battle_re/forest_battle_3hetake.state` ($2A1F = 00, Crono + Marle) with `PEEK_POKE=5E30=01,5E31=00,5EB0=01,5EB1=00`:

| frame | what |
|---|---|
| f439 | `$7F01EC` = 01 (C1:8143) -- then nothing, with no button, for as long as it is run (3000 frames checked) |
| f700 | A pressed |
| f701-f715 | `$0119` 0E .. 00, one step a frame (C0:02B7) -- the edge's own pass takes the first |
| f716 | `$0100` = 0: C0/000E's clears |
| f716 line 210 -> f737 line 172 | C0:0053 -> C0:0057, the driver re-upload (20.85 frames) |
| f737 | the SRAM scan's wipe (C2:95D2, a blank cart: `$7F0061` = 0) |
| f737 line 253 -> f757 line 52 | loc 0's `10 00` song JSL (19.23 frames) |
| f763 / f779 / f794 | scene 0's E0 / `$0100` = 1B1 / the title (`$0384` = 02) |

⚠ Those are runFrame CALL counts: the reset's `$E900`-byte DMA runs through a vblank AND the frame wrap inside one opcode, so the
call it starts in carries 830,754 master cycles (`CT_HOOK_CYCLES=1`) and every later count is one frame short of real time.

**PORTED** (field.c): (a) reads the ROM's bytes, `$00F6 | $00F0` (the pad latch, so the d-pad does not release it -- the old
host test took the arrows), after this frame's NMI latch; (b) takes its first step on the edge's own frame and writes `$0119`;
(c) requests the cold init the attract's demo battle also reaches (boot_loader.md 8), performed once below the battle block for
both. Port, the same fight from `forest_pre_wrapped` with `CT_BATTLE_PCHP=1`: press f1000 -> fade f1001-f1015 -> reset f1016
(press + 16, the cartridge's) -> 1B1 at f1081 = reset + 65 against the cartridge's 64 real frames. Check
`field-gameover-reboots-to-title`.

