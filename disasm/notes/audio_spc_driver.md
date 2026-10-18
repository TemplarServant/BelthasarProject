# The SPC700 sound driver -- the map, read from the ROM to port it to C

**Status (2026-10-18): step 2 DONE -- the driver in C** (`src/sound/driver.c`), identical tick by tick to this
program on the vendored SPC700 over the attract, the quick tier's runs and a constructed command stream ("The port",
below). Step 1 was the reading. The user's decision of 2026-10-18: the game must not need the ROM once `ct_assets.dat`
is extracted, through DECODED assets, and the sound first -- the cartridge's sound driver ran as ROM code on the
vendored SPC700 (`snes/spc.c`, through `src/audio.c`), which is emulation. This note is the map and, per routine, per
event and per command, the reading the port was written from.

Addresses are SPC700 ARAM addresses unless they carry a bank. No byte of the driver is reproduced here: the listing is
made from your ROM by `tools/spc700dis.py` (below).

## Tools

| What | How |
|---|---|
| The listing | `python tools/spc700dis.py driver` (linear) or `--follow 0200 --table 1554:60,1608:60,1680:16,16A0:16,16C0:32,1544:8` (reachable code only). Block 0 is found through the upload tables at `$C7:0010` / `$C7:001C` (audio_boot.md): source `$C7:24C3`, a size word, 7151 bytes, ARAM `$0200-$1DEE`. |
| The opcode table | 256 SPC700 opcodes written from the instruction set; the lengths are graded against the vendored core: `tests/spc700_lengths.c` runs each non-control-flow opcode on `snes/spc.c` (231 of 256; the 25 jumps, calls, returns and halts leave the PC elsewhere) -- 0 differ. Build it with `cl /I snes tests\spc700_lengths.c snes\spc.c snes\statehandler.c` (or `cc -Isnes ...`) and run it with the output of `python tools/spc700dis.py lengths` as its one argument. |
| Coverage | `CT_SPC_COVERAGE=<file>` on Belthasar: every SPC700 address the driver executed, `ADDR COUNT` a line, at exit (`%d` in the name = the process id, for parallel runs). `--coverage FILE` marks a listing. |
| DSP writes | `CT_DSP_LOG=<file>`: every S-DSP register write, `frame apucycle register value`. |
| Report | `python tools/spc700dis.py report --coverage 'a.cov,dir/q_*.cov'`: the reachable code and each dispatch table's entries, run or not. |

The hooks are `g_apuOpHook` / `g_apuDspWriteHook` in `snes/apu.c` (NULL unless asked for: one pointer test per
instruction and per DSP write).

## The program

Reachable from `$0200` and the six dispatch tables: **3156 instructions, 6547 bytes of code**. Everything else in the
block is data, in two runs: `$1544-$1721` (the dispatch tables and the operand counts) and `$1D6D-$1DEE` (the pitch
table, the note lengths and other small tables). Nothing outside block 0 executes (the IPL at `$FFC0` aside).

These counts are the same as the ones zdiemer/chrono-trigger-reimplemented's `docs/SOUND.md` gives for the same block
(3156 / 6547), reached independently from our listing -- a cross-check that both read the same program. That repository
has no licence: it is a map to compare readings against, never a source of code or tables.

### Dispatch: `PUSH hi / PUSH lo / RET`

The driver jumps through word tables by pushing the address and returning to it (SPC700 `RET` pops the exact address).

| Table | Words | Indexed by | Site |
|---|---|---|---|
| `$1554` | 60 | sequence event `$C4-$FF` (`$073D`: `(byte - $C4) * 2`) | the event handlers; operand counts per event at `$15CC` (`$0743-$074D`, `$0796`) |
| `$1608` | 60 | the same events, for the note look-ahead (`$075E`); a zero high byte at `$1609+Y` = the event does not matter to it | `$077D-$0793` |
| `$1680` | 16 | commands `$10-$1F` (`$0BC2`) | `$0BBE-$0BD4` |
| `$16A0` | 16 | the same commands: a routine pushed to run BEFORE the `$1680` one (zero = none) | `$0BCA-$0BD3` |
| `$16C0` | 32 | commands `$80-$8F` and `$F0-$FF` (`command & $1F`; `$90-$EF` fall through untouched) | `$0DFB-$0E10` |
| `$1544` | 8 | the upload's kind (`$100E` -> `$105D-$1061`) | |

### Boot (`$0200-$0298`)

Clears the direct page `$00-$EF`, sets `$80 = $FF` and `$BB/$BC = $FFFF`, writes S-DSP registers through the helper
`$07A3` (`Y` = register, `A` = value: `$2C`/`$3C` the echo volumes to 0, `$5D` DIR = `$1E`), puts every voice's GAIN
register (`$x7`) at `$A0`, clears the command queue (`$F16E+X`, 16 entries of 4), starts the timers (`$F1`; timer 0
target `$2A` = 8 kHz / 42, about 190 Hz; timer 1 `$80`), allocates the echo buffer (`$132E`), sets up the three fades
(`$F15F+X` / `$F164+X`), installs one sample directory entry (`$1E40-$1E43`) and one instrument (`$1FA0` / `$1F20`), sets
the main volumes (`$0C` / `$1C`) and enters the loop.

### The loop (`$0299`) and the tick (`$02A0-$0374`)

`$0299` takes a command waiting on port 0 (`$0B96`), then reads timer 0's counter (`$FD`) and loops until it ticked. A
tick:

1. **Flush the shadows** (`$02A0-$02C0`): 17 S-DSP registers, the register numbers from `$16FF+Y` and the RAM address
   of each value from `$1710+Y` (Y = 17..1). Then the key-on and key-off shadows `$22` / `$23` are cleared.
2. **Status** (`$02C2-$02DB`): port 0 = 0 and port 1 = `$F166` (a word store to `$F4`); port 2 = `$D3` in the high
   nibble and `$7B` in the low; port 3 = `$53` (the music tracks playing) folded to a nibble, OR `$86` (the voices the
   effects own). A meter mode (`$88` bit 7) calls `$1438` instead.
3. **Unless paused** (`$87` bit 4): every 7th tick (`$BD`) the fades (`$1492`).
4. **The sequencer** `$0377`: the music tempo (`$47`, times a modifier `$AA` when set) is added to the accumulator
   `$48`; on its carry every playing music track (`$53`) counts down (`$26+X`) and steps (`$0507`) at zero; then the
   per-track slides (`$07AF`) and the global ones (`$04C7`). The effect tracks run on their own accumulator `$49`, a
   fixed step of `$81` a tick. `$88` bit 5 makes every tick a step (both).
5. **The queued commands** `$08CD` (the queue at `$F172`, 4 bytes an entry).
6. **The voices** (`$02F8-$0367`): for each music voice not owned by an effect, `$0903` (or `$09F6` while effects own
   voices), then two effect-voice passes; each calls the port poll `$0892` between voices.
7. `$08CD` again, then `$0B5C` (one byte of the built-in sample rewritten a tick), back to `$0299`.

`$0415` is the same tick for the upload receivers that keep the music playing during a transfer (the flush, the fades,
a step of the tracks, the voices -- no command queue).

### The sequencer (`$0377`, `$0507-$07A2`)

- `$0377`: the music's add is the tempo `$47`, scaled by the tempo modifier `$AA` when set (below `$80`: `$47 + 2 x
  hi($47 x $AA)`, held to `$FF`; from `$80`: `hi($47 x $AA)`, at least 1), added to `$48`; on the carry every playing
  music track (`$53`, from bit 0, X = 0 on) counts down `$26+X`. At zero `$88` bit 3 is cleared and the track steps
  (`$0507`); a track that ended (bit 3 set) takes no slides and no poll. Not at zero: when the countdown has just become
  **2** and the look-ahead saw no tie (`$5A`/`$5B` with the voice's bit), the voice's bit leaves `$24`/`$25` and, for an
  effect track or a music voice the effects do not own, goes into KOFF -- the gate, two ticks before the next step. Then
  `$07AF` (the track's slides) and the poll (`$0892`, return `$03E3`). After the music: `$04C7` and `$D0`/`$D1` lose the
  effects' voices. The effects run on `$49 += $81`, tracks X = `$18-$1E` (bits `$10-$80` of `$86`). `$88` bit 5 (command
  F6): both step every tick. The music's last track ending sets `$80 = $FF`.
- `$0753` reads `[$02+X]` into `$A5` and advances the pointer.
- `$0507`: bytes from `$C4` are events (`$073D`); a byte below is a note, **byte = 14 x class + length**: the QUOTIENT
  (`DIV` by 14) is the class -- 0-11 a semitone, 12 (`$A8-$B5`) a tie, 13 (`$B6-$C3`) a rest -- and the REMAINDER the
  length index into `$1DB3` (14 lengths), taken into `$26+X` unless an event
  (E8) set one already. (Step 1's map had the two the other way round.) A rest ends the step there; a tie goes to the
  look-ahead; a note: `$F200+X` (the octave base) + the semitone, or in percussion mode (`$7C`, music voices only)
  `$0720`'s note from the table at `$F120` (3 bytes a semitone: instrument, note, pan unless negative); then + the
  transpose `$F361+X` - 10 into `$F3A1+X`, the pitch (`$06BD`) into `$F520+X`; the vibrato (`$0151+X` nonzero:
  `$18A2`, the depth scaled to the pitch through `$1D87` -- the effects' index is `$0110+X` -- into `$F300+X`); the
  tremolo (`$0170+X`: `$1960`); `$F500`/`$F4A0`/`$F3C0`/`$F4E0` cleared; the key-on (`$24`/`$25`, KON unless the effects
  own the voice, or a slur's second note: `$5C` set, `$60` clear, `$5E` already set); `$D0`/`$D1`; `$1AED` (SRCN and
  ADSR).
- `$0601` (the look-ahead's verdict): the next byte a tie sets the voice's tie bit (`$5A`/`$5B`); a rest or the end
  clears the slur and roll bits; otherwise the tie bit = slur or roll. Then `$064B`: a pending pitch slide (`$0150+X`,
  event C8) moves `$F3A1+X` by its semitones, and the 24-bit step `[$F3C1:$F3C0:$F4E0]` = (target - pitch) x 256 / the
  slide's ticks (`$F360+X`), signed.
- `$06BD` (a note's pitch): octave = note / 12; the semitone's word from `$1D6D`; the multiplier `m = $F380+X +
  $F3A0+X` (the instrument's tuning high byte + the fine tune); `t = hi x m + (lo x m >> 8)`, plus `hi(t) x $F381+X >> 8`
  when the tuning's low byte is set, plus the word itself when `m < $80`; shifted right `4 - octave` times, or left.
- `$075E` (the look-ahead): from the track's pointer, past every event (their operand counts at `$15CC`) to the first
  byte below `$C4`, or `$EB`. Its own handlers (`$1608`): DC and E4-E7 `$1B86` (clear the voice's slur and roll bits --
  DC's operand is NOT skipped and is read as the next byte); E3 `$1CC9` (back to the loop's start unless its last pass);
  E9/EA `$1BD9`/`$1BDD` (into the effect's track; a high byte 0 = the end); F5 `$1C39`, F6 `$1BFC`, FA `$1D2A` (FA's
  operand lands in `$A5`) follow their jumps. The loop slot it walks is its own copy (`$BA`).

### The sequence events (`$073D`, the table at `$1554`, operand counts at `$15CC`)

`$073D` stores `byte - $C4` in `$A5`; with operands, the first is read (A = `$A5` = it), else A = 0 and `$A5` keeps the
index. X = track x 2; `$91` = the voice's bit. "Slide" = `$0E5F`'s step: the difference (signed by the SBC's carry) x 256
/ the ticks (`$8E`).

| Ev | Ops | Handler | What |
|---|---|---|---|
| C4 | 1 | `$175F` | volume `$F241` = op & 7F, `$F240` = 0, `$D0`, the volume slide off |
| C5 | 2 | `$1770` | volume slide: ticks, target (0 ticks = C4) -> `$F260/61` |
| C6 | 1 | `$17BA` | pan `$F281` = op x 2 (music tracks only) |
| C7 | 2 | `$17CE` | pan slide (its ticks stored for the effects too, then ignored) -> `$F2A0/A1` |
| C8 | 2 | `$17F2` | the next note's pitch slide: `$F360` = ticks + 1, `$0150` = semitones |
| C9 | 3 | `$186E` | vibrato: delay `$0110` (an effect's: op % 12, the depth table's index), rate `$F2E0`, depth `$0151`; step `$18DE` -> `$F400` |
| CA | 0 | `$192E` | vibrato off (`$0151`, `$F500` = 0) |
| CB | 3 | `$1938` | tremolo: delay `$0130`, rate `$F320`, depth `$0170`; step -> `$F440` |
| CC | 0 | `$198E` | tremolo off |
| CD | 2 | `$1998` | pan sweep: period (`$FF`: no division) `$F340/41`, depth & direction -> `$F3E0`, `$0171` |
| CE | 0 | `$19EE` | pan sweep off: CD's tail with A = 0 (`$0171`, `$F4C0/C1` = 0) |
| CF | 1 | `$1A78` | noise clock `$62`/`$63` (bits above 4 set: relative) -> FLG + NON shadows |
| D0/D1 | 0 | `$1A3C`/`$1A68` | noise on/off |
| D2/D3 | 0 | `$1A98`/`$1AB5` | pitch modulation on/off (an effect's only on voices other than `$50`'s) |
| D4/D5 | 0 | `$1A15`/`$1A2C` | echo on/off |
| D6 | 1 | `$1A0E` | octave base `$F200` = op x 12 |
| D7/D8 | 0 | `$19FA`/`$1A04` | octave up/down (+-12) |
| D9/DA | 1 | `$1801`/`$17FD` | transpose `$F361` = op / += op |
| DB | 1 | `$1CF2` | fine tune `$F3A0` |
| DC | 1 | `$1ACF` | instrument `$F201`, its tuning `$F380/81` and ADSR `$F540/41` from `$1F00`/`$1F80` |
| DD/DE/DF/E0 | 1 | `$1AFC`/`$1B2A`/`$1B3D`/`$1B4F` | ADSR attack / decay / sustain level / sustain rate (written unless the effects own the voice) |
| E1 | 0 | `$1B5F` | the instrument's ADSR again |
| E2 | 1 | `$1C64` | loop start: the track's slot (4 a track, a ring at `$27+X`), count + 1 (0 = for ever) `$F580`, the music's pass counter `$F560` = 1, the pointer `$F5C0` |
| E3 | 0 | `$1C94` | loop end: the pass counter + 1, back to the start, the last pass pops the slot |
| E4 | 0 | `$1B72` | slur on (`$5C`/`$5D`, its keyed bit `$5E`/`$5F` cleared) |
| E5/E7 | 0 | `$12DD` | nothing (their look-ahead handler clears the slur) |
| E6 | 0 | `$1B9A` | roll on (`$60`/`$61`) |
| E8 | 1 | `$1CEF` | the next note's length `$26+X` |
| E9/EA | 1 | `$1BA8`/`$1BAC` | the track goes to effect op's first / second track (`$2F00 + op x 4`); a high byte 0 ends the track |
| EB-EF, FE, FF | 0 | `$1D4A` | the end of the track (`$1D4C`: `$88` bit 3; the music's bit leaves `$53`, the last one `$80 = $FF`; an effect's leaves `$86` and -- the ROM's -- the MUSIC's `$5E`; noise, pitch modulation and echo off) |
| F0 | 1 | `$1722` | tempo `$47` = op + (op x 20 >> 8) |
| F1 | 2 | `$1732` | tempo slide -> `$4B/4C` |
| F2 | 1 | `$1791` | echo volume `$4F` |
| F3 | 2 | `$179A` | echo volume slide (signed) -> `$50/51` |
| F4 | 1 | `$1751` | master volume `$4D`, all music voices' volume rewritten |
| F5 | 3 | `$1C09` | on loop pass op1, leave the loop to the target (the last pass pops the slot) |
| F6 | 2 | `$1BEE` | go to (relative to `$00/$01`) |
| F7 | 2 | `$1805` | echo feedback slide (signed, no zero test) -> `$79/7A` |
| F8 | 2 | `$1826` | the echo FIR: one of four sets at `$1D93` (8 bytes each), at once or as a slide (`$0100-$010F`) |
| F9 | 1 | `$1CF6` | the marker `$7B` = op & 0F (port 2's low nibble) |
| FA | 3 | `$1CFB` | jump when command 88's value `$D2` >= op & 0F; `$D3` keeps the largest op seen (port 2's high nibble) |
| FB/FC | 0 | `$1AC5`/`$1AC9` | percussion mode on/off (`$7C`) |
| FD | 1 | `$1757` | expression `$F220` = op x 2 |

### The per-track slides (`$07AF`) and the global ones (`$04C7`)

`$07AF` (each tick a track's countdown runs): the vibrato and tremolo delays (`$0111`/`$0131`, with the direct page at
page 1) count down; the volume and pan slides add their step while their ticks last (`$D0` when the high byte moved);
the pitch slide adds its 24-bit step to `[$F521:$F520:$F4E1]` (`$D1`; on its last tick the step is cleared but still
added once); the pan sweep adds its step to `$F4C0/C1` and reverses it every period. `$04C7`: the tempo, the echo
volume, the echo feedback and the FIR's eight coefficients (`$65+2k`, steps at `$0100+2k`), each while its ticks last.

### The voices (`$0903`, `$09F6`)

- `$0903`: the vibrato (unless its depth is 0, or a music voice's delay `$0111` runs -- `$A5` takes the depth BEFORE
  that test): `$F480/81 += $F420/21`; when the high byte moved, the offset `$F500/01` = the scaled depth `$F300/01` x
  (the accumulator x 2's high byte, signed) >> 8 and `$D1`; every period (`$F2E1` from `$F2E0`) `$0ADE` steps the
  waveform. The tremolo (`$0170`, delay `$0131`, any voice): `$F4A0/A1 += $F460/61`, `$D0`, its period `$F321`.
- `$0ADE`, a waveform step: the depth's top bits pick the shape (below `$80`, `$80-$BF`, from `$C0`); from the old
  delta, the step / 4 and the phase (3 bits of `$F221`: the vibrato's low, the tremolo's in bits 4-6) the new delta
  and phase.
- `$09F6` (`$D0`/`$D1` both 0: nothing): the volume when `$D0` names the voice -- pan `$80` in mono (`$87` bit 0), else
  NOT (pan `$F281` + the sweep `$F4C1`, held to 0-255); the volume `$F241` with the tremolo (`$F4A1`, a factor), x the
  pass's fade (`$A8`: `$F161` music, `$F163` effects 7-6, `$F165` effects 5-4), for the music x the expression `$F220` x
  the master volume `$4D` (a muted voice `$A7`: 0); L = v x pan, R = v x NOT pan, each into `$BE+` and VOL L/R. The pitch
  when `$D1` names it: `$F520/21 + $F500/01`, the music's scaled by the pitch modifier `$AF` (below `$80`: + 2 x p x
  `$AF` / 256; from `$80`: p x `$AF` / 256), the effects' voices 6-7 by command 89's `$B1` (+ 2 x p x `$B1` / 256).
- While the effects own any voice (`$86` nonzero) the music's voices only output (`$09F6`): no vibrato or tremolo step.

### The tick's other parts

- The fades `$1492` (every 7th tick, `$BD`): three volume fades (music `$F160/61`, effects A `$F162/63`, effects B
  `$F164/65`; ticks `$F166/68/6A`, steps `$F16C-$F171`), each ending at 0 stopping its group (the music keeps
  `$BB/$BC`); the tempo modifier `$A9/AA` and the pitch modifier `$AE/AF` slides.
- The status (`$02C2`): port 2 = XCN(`$D3`) | `$7B`, port 3 = the music's tracks folded to a nibble | `$86`, port 0 = 0,
  port 1 = `$F166` (the music fade's ticks left) -- or, in the meter mode (`$88` bit 7, command FF 01 01), `$1438`: four
  voices' OUTX against their stored volumes, alternate halves.
- `$0B5C`: one nibble of the built-in sample's loop (`$1DCB+`, the 9-byte blocks sample `$10` points at) flipped a
  tick, `$D8` sweeping 2..`$23` -- a pulse whose width moves.

### The command port (`$0892`, `$0B96`) and the commands

`$0892` (between tracks and voices): a nonzero byte on port 0, read twice until stable, other than `$FE` and `$10-$17`,
is queued at `$F172 + $D7` (16 entries of 4; a full queue overwrites its last), the ports cleared (`$07A8`: `$F1` = `$13`,
`$03`) and echoed. `$08CD` runs the queue (`$0BAC` on each) and empties it; it runs after the sequencer and at the
tick's end, and inside the echo wait (`$12F9`: re-entrant). `$0B96` (the loop): ports 0-3 into `$8D-$90`, the ack, then
`$0BAC`: from `$80` echo and dispatch `$16C0` (`$80-$8F` and `$F0-$FF`; `$90-$EF` nothing); `$10-$1F` through `$16A0`
(the pre-routine) and `$1680`; the rest echoed only.

| Cmd | Routine | What |
|---|---|---|
| 10 / 11 | `$0D29` + `$0BDB` / `$0BE3` | song start, track table A (`$2004`) / B (`$2014`) |
| 14 / 15 | `$0D29` + `$0BD8` / `$0BE0` | the same, the playing song saved first (`$13A9`: `$F200-$F7FF`, `$00-$7F`, `$0100-$018F` to `$F800`/`$FE00`/`$FF00`) |
| 18 / 19 | `$0D45` / `$0D51` | an effect on voices 7-6 / 5-4 (op1 = the effect, op2 = the pan) |
| 80 | `$0E11` | the three fades |
| 81 / 82 / 83 | `$0E1A` / `$0E22` / `$0E2A` | a fade (music / effects A / effects B): op1 ticks x 7, op2 the volume |
| 85 | `$0E7F` | 87 then 86 |
| 86 / 87 | `$0E82` / `$0EAA` | the tempo / pitch modifier, to op2 over op1 x 7 ticks |
| 88 | `$12B6` | `$D2` = op1 & 0F (event FA's test) |
| 89 | `$0ED6` | `$B1` (the effects' voices 6-7 pitch factor) |
| 8E | `$0EE6` | loud: main volume `$7F` (else `$40`), the echo volume doubled (`$87` bit 3) |
| F0 / F1 | `$0F0E` / `$0F17` | stop everything / the music |
| F2 | `$0F3C` | stop effects A (op1 bit 0: B instead) |
| F3 | `$0F00` | mono (`$87` bit 0) |
| F4 / F8 / F9 | `$0FA5` / `$0FAC` / `$0FB5` | the mute mask `$A7` = / clear / set op1 |
| F5 | `$0FBE` | pause (op1 nonzero: every ADSR1's enable bit cleared, VOL 0, `$87` bit 4) / resume |
| F6 | `$129D` | fast: timer 0 target `$01` (else `$2A`), a step every tick (`$88` bit 5) |
| FC | `$132E` | the echo delay (`$1352`, the old and the new buffer's waits) |
| FD | `$12F2` | an S-DSP write (op1 the register, op2 the value) |
| FE | `$100E` | an upload session |
| FF | `$12BD` | op1 01: the meter mode (op2); op1 F0: back to the IPL (FLG `$E0`, `$F1 = $80`, `JMP $FFC0`) |

The song start (`$0BE6`): KOFF all, `$BC` = the id, `$4D` = op2, the echo delay op3 & 0F (a new one: `$1316` stops the
echo and, with no old buffer, starts it again); the upload session (`$100E`: the song's data); `$86`, `$D2/D3`, `$D1`
cleared; the saved song (`$BB == $BC`) is restored (`$13E5`, fading in: `$F166 = $10`, step `$0EF0`) and resumed (each
voice's instrument, VOL 0, its pitch, KON = `$24`), or the new one's tracks set up: the header at `$2000` (its own base:
`$00/$01 = $2024 - [$2000]`; `[$2002]` = the empty marker; 8 track words from `$2004` or `$2014`), each `$0CF6`
(`$27+X` = 2x, the modulations off, `$26+X = 1`, `$F301 = $10`) and `$F220 = $FF`. Then the echo's waits (`$12F9`, timer 1
from `$83`), the shadows, `MOV SP,#$FF` and the loop. A song already playing (`$80`) is refused by `$0D29`: port 1 =
`$FF`, then the driver waits for port 0 to be nonzero and acks it (the CPU's release).

### The upload session (`$100E`)

The protocol, the driver's side: the first token is (command + 1) & `$7F`; each block header is port 1 = the kind
(`$E0` = the session's end), ports 2-3 = the destination, echoed, then the receiver; data tokens are below `$80`; a
block ends on port 0 = `$E0` with the NEXT header on ports 1-3; `$FF` with port 1 = `$F0` sends the driver to the IPL.
At each header (`$1029`) a pending echo change (`$88` bit 0) adds timer 1 to `$83`; a destination below `$1E00` makes
any kind 0 (the driver's own code is never overwritten); a destination page `$2F` stops both effect groups first. The
receivers (`$1544`): 0, 4, 6 `$11E4` take tokens and store nothing; 1 `$11B2` one byte a token (port 1); 2 `$1167` two;
3 `$107C` three; 5 `$10EC` three, running `$0415` -- the upload tick: the flush, the fades, the music's step (no poll,
no effects) and voices -- whenever timer 0 ticked; 7 `$1206` moves memory (source, length, destination pairs, both
directions). The receivers rewrite their own store instructions' operands (self-modifying code).

## RAM

The C port names every field (`src/sound/driver.c`: D_ the direct page, P_ page 1, T_ the per-track arrays (x 2 a track,
32 bytes a field), A_ the rest, R_ block 0's tables):

| Where | What |
|---|---|
| `$00/01` | the songs' relocation; `$02+X` the track pointers; `$22/$23` KON/KOFF shadows; `$24/$25` the voices holding a note |
| `$26+X`, `$27+X` | the countdown, the loop slot |
| `$46/47`, `$48`, `$49`, `$4A-$4C` | tempo, its accumulator, the effects' accumulator, the tempo slide |
| `$4D`; `$4E-$52` | master volume; echo volume and its slide |
| `$53`; `$54-$59` | music tracks playing; echo / noise / pitch modulation masks (music, effects) |
| `$5A-$61` | tie, slur, slur keyed, roll (music, effects) |
| `$62/$63`; `$65-$74`; `$75-$7A` | noise clocks; the FIR (8 words); echo feedback, the FIR's and feedback's slides |
| `$7B`, `$7C`, `$80-$83` | the marker, percussion, the song, the echo delay (new, old, the wait's count) |
| `$86-$8C` | the effects' voices, flags, the EON/NON/PMON/FLG shadows |
| `$8D-$90`, `$91`, `$A3`, `$A5`, `$A6`, `$A7`, `$A8` | the command; the voice's bit; the pass mask; the byte; X; mute; the pass's fade |
| `$A9-$AD`, `$AE-$B4` (`$B1`) | the tempo modifier, the pitch modifier (the effects' pitch factor) |
| `$B9`, `$BB/$BC`, `$BD`, `$BE-$CD` | the track table, saved / requested song, the fade divider, the written volumes |
| `$D0/$D1`, `$D2/$D3`, `$D7`, `$D8`, `$D9` | volume / pitch to write, command 88 and FA's largest, the queue head, the sample sweep, EVOL out |
| scratch | `$92-$96`, `$9B-$A2`, `$A4`, `$B5-$B8`, `$BA`: written before read in every routine (the lockstep skips them) |
| `$0100-$010F`, `$0110-$018F` | the FIR's slide steps; per track: vibrato delay, tremolo delay, pitch slide, vibrato depth, tremolo depth, pan sweep |
| `$1E00-`, `$1F00`, `$1F80` | the sample directory, per instrument tuning and ADSR |
| `$F120-$F14F`, `$F160-$F171`, `$F172-$F1B1` | the percussion table, the fades, the command queue |
| `$F200-$F6xx`, `$F800-$FF8F` | the per-track arrays (and the loop stacks `$F560/$F580/$F5C0`), the saved song |

## The port (steps 2-5)

**Step 2 DONE (2026-10-18): the driver in C**, `src/sound/driver.c` / `driver.h`, validated tick by tick.

- The driver as game code: the state in `SndDrv::m`, the cartridge's ARAM layout (the lockstep compares it byte for
  byte), every field named; block 0's tables read from where the loader put them (today the ARAM image). The outside
  through `SndDrvHost`: the S-DSP register sink and its reads (FLG, EDL, ADSR1, OUTX), the poll points (`poll` with the
  ROM's return address as the site), the status ports, timer 1, the control writes, the upload session. API:
  `SndDrvBoot`, `SndDrvCommand` (the loop's command), `SndDrvTick`, and inside a session `SndDrvUploadHeader` (returns
  the receiver), `SndDrvUploadTick` (`$0415`), `SndDrvUploadReset`; the loader stores the blocks' bytes in `SndDrvMem`.
- The lockstep, `src/sound/lockstep.c` (`CT_SNDDRV_LOCKSTEP=1` on Belthasar or ct.exe, every CtAudio's SPC): the
  original's inputs logged at its own points (the loop's command, each poll's ports, timer 1, the S-DSP reads, the
  session's headers, receivers, bytes and ticks), replayed into the C driver at each `$02A0`; compared: the S-DSP writes
  in order, the status and control writes (the protocol's own left out), the S-DSP register file, the RAM (the scratch,
  the stack, the I/O and the echo buffer left out). `CT_SNDDRV_LOCKSTEP_OUT=<file>` (`%d` = pid), `CT_SNDDRV_TRACE`,
  `CT_SNDDRV_WATCH=<addr>`. Hooks: `g_apuReadHook` / `g_apuWriteHook` added to snes/apu.c.
- The fuzz, `src/sound/fuzz.c`: `CT_SNDDRV_LOCKSTEP=1 ct.exe --snddrvfuzz <actions> [seed]` -- constructed songs from
  all 60 events, random commands, upload sessions of every kind, effects, cold boots (FF F0).
- Results: the attract (`--scene 433`, 11,200 frames) 34,859 ticks identical; the quick tier's runs (242 processes, these two checks among them) 1,129,203 ticks
  identical; the fuzz (600 actions, seed 2) 122,567 ticks identical, all 60 events, all 29 commands it posts, upload
  kinds 0-6, the 7 look-ahead handlers. Kind 7 (the defragmenter) runs in the attract.
- What the lockstep found in the C (fixed): `$0903`/`$0995` take the depth into `$A5` before the delay's test; event CE
  is the pan sweep's end (its handler is CD's tail), not a jump. A gap in the lockstep itself, found by its mutation
  checks (fixed): the echo buffer left out of the RAM compare is the set of bytes the S-DSP may have written, marked from
  its own state (writes on, the buffer's address, the length it runs and the next one, 4 bytes at EDL 0) at each level,
  compare and write to `$6C`/`$6D`/`$7D`; taken first from the lowest ESA register, it left the RAM below `$F104`
  unchecked after a boot (ESA = 0 at the S-DSP's reset).
- Pinned: regress checks `snddrv-lockstep-attract` and `snddrv-lockstep-fuzz` (quick tier), each with mutations of the
  C driver that must fail them (`python tools/regress.py --mutate snddrv-lockstep-attract snddrv-lockstep-fuzz`).
- Out of the driver, found by the fuzz: the port's loader (`src/audio.c` CtAudioLoadSong) with command `$10` on songs 03
  06 11 15 2F 3B 3C 49 from a fresh CtAudio, and on some song changes, sends a defragmentation (kind 7) that runs over
  the driver (the SPC copies forever at `$122C`); `$14` loads them. The fuzz loads with `$14/$15` on a fresh CtAudio.
- For step 5 (the C driver playing for real), what the host must give it: the ticks (timer 0: `$2A` = 190.5 Hz, `$01`
  in fast mode, from the control writes; a tick missed while a command runs is lost -- `$FD` is read and cleared), timer
  1 for the echo waits (16 ms units, restarted by `$1352`/`$1397`), the refusal's release, the upload tick for kind-5
  blocks, the S-DSP's OUTX for the meter mode.

2. **The mixer**: `snes/dsp.c`'s voices, envelopes, echo and noise behind the voice operations, sample-exact against
   the S-DSP (in parallel).
3. **The assets** -- the loader's side DONE 2026-10-18: `assets/extract_assets.py pack_sound` makes CT_ASSET_SND_BOOT
   (the six upload blocks), _SEQ (each song's sequence, with its ROM address -- the upload's bank-crossing loop prices
   it -- and the 3 bytes its last unit reads past the block), _SAMPLE (BRR, with its address), _INSTR (loop / `$1F40` /
   `$1FC0` words, id 0 the bank-`$C8` wrap), _SONG (instrument list, percussion, record), _SFXSET (both sets) and _MISC
   (the song count and exception list; FD/C1EE's channels for the loader clock). `CtAudioInitWith(rom, len, &assets)`
   reads them when the container holds them (Belthasar does; `CT_AUDIO_FROM_ROM=1` is the reference). Proved by
   `ct.exe --sndassets` (957 items identical) and `tools/snd_assets_cmp.py` (the attract's S-DSP writes and output
   identical); the quick tier passes with the sound from the container. Still to do with step 5: block 0's tables
   (`$15CC`, `$16FF/$1710`, `$1D6D-$1DEE`) as the C driver's own data -- today they reach it in the uploaded block 0,
   from the container. The uploads' protocol cancels out between two C halves, but the FRAMES the game waits on a busy
   SPC are part of the game under the rule (CLAUDE.md, "The SPC under the rule"): the native model must charge them.
4. **Belthasar without the SPC700** and without bank `$C7`.
