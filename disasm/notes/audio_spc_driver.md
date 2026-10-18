# The SPC700 sound driver -- the map, read from the ROM to port it to C

**Status (2026-10-18): step 1 of the native sound (the reading).** The user's decision of 2026-10-18: the game must not
need the ROM once `ct_assets.dat` is extracted, through DECODED assets, and the sound first -- today the cartridge's
sound driver runs as ROM code on the vendored SPC700 (`snes/spc.c`, through `src/audio.c`), which is emulation. This
note is the map the port (step 2) is read against: the program's parts, its tables, the RAM it keeps, its tick, and what
the game's runs reach. The routine-by-routine semantics are derived as each one is ported (re-derive -> re-implement),
each with its lockstep.

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

### The sequencer (`$0507-$07A2`)

- `$0753` reads the track's next byte (`[$02+X]`, X = 2 x track) into `$A5` and advances its pointer.
- `$0507`: bytes `$C4` and up are events (`$073D` dispatches, then the loop reads on); a byte below is a note:
  `$A8-$B5` a tie, `$B6-$C3` a rest, below `$A8` a pitch. The note's length index is the byte divided by 14, through
  the length table `$1DB3` into the countdown `$26+X` (unless one is set already).
- A pitch: the semitone (byte / 14 with the octave `$F200+X`, the transpose `$F361+X`, minus 10, into `$F3A1+X`), then
  `$06BD` (octave from / 12, the semitone table `$1D6D`) into `$B5`/`$B6`, kept at `$F520+X`; a pitch slide (`$0151+X`,
  `$18A2`), vibrato and the rest follow, and the look-ahead `$075E` decides whether the voice is keyed.
- Percussion mode (`$7C` with the voice's bit) takes the note through `$0720`.

### The command port (`$0892`, `$0B96`)

`$0892` (called between tracks and voices): a nonzero byte on port 0, read twice until stable, is echoed and queued
(song starts `$10-$17` and the upload `$FE` are left for the loop's `$0B96`). `$0B96` takes port 0-3 (`$8D-$90`) and
dispatches; a song start (`$0D29` for `$10`) refuses the song already playing (`$80` = the current id).

### Small helpers

`$07A3` writes one S-DSP register (`$F2` = Y, `$F3` = A). `$07A8` resets the timers (`$F1`). `$132E` (re)allocates the
echo buffer from the delay in `$8E` (via `$1352`). `$1492` advances the three volume fades (`$F160-$F166`).

## RAM, as far as read

| Where | What |
|---|---|
| `$02+X` / `$03+X` | each track's read pointer (X = 2 x track: 0-`$0E` music, `$10-$1E` effects) |
| `$22` / `$23` | key on / key off shadows (flushed at the start of the next tick) |
| `$26+X` | each track's tick countdown |
| `$47`, `$48` | music tempo, its accumulator; `$49` the effects' accumulator; `$AA` a tempo modifier |
| `$53` | music tracks playing; `$86` voices the effects own |
| `$7B`, `$D3` | the two nibbles of port 2 (a marker the song sets, and its high nibble) |
| `$80` | the song playing (`$FF` = none) |
| `$87`, `$88` | flags: `$87` bit 4 paused, bit 3 (mono?); `$88` bit 7 meter mode, bit 5 every tick a step, bit 3 a step's "the track ended" |
| `$8D-$90` | the command being run (port 0-3) |
| `$91` | the current voice's bit; `$A6` the current track x 2; `$A3` the work mask of a pass; `$A5` the byte read |
| `$BD` | the 7-tick fade divider |
| `$D0` / `$D1` | the voices whose volume / pitch changed this tick |
| `$F1`, `$F2`/`$F3`, `$F4-$F7`, `$FA`/`$FB`, `$FD` | the SPC700's control, DSP address/data, ports, timer targets, timer 0 counter |
| `$0110-$01xx` | per-track bytes below the stack, reached with the direct page at `$01xx` (`SETP`, `$07AF`) |
| `$1E00-` | the sample directory (DIR = `$1E`) |
| `$1F00` / `$1F80` | per instrument: tuning / ADSR |
| `$F15F-$F166` | the three fades; `$F166` is put on port 1 |
| `$F172-$F1B1` | the command queue |
| `$F200-$F5FF` | the per-track arrays (32 bytes a field, a word per track): octave `$F200`, transpose `$F361`, the note `$F3A1`, its pitch `$F520`, ... |

## What the game reaches

The union of `CT_SPC_COVERAGE` over the attract (11,200 frames, `--scene 433`, `CT_AUDIO_LOCKSTEP=1`) and every
Belthasar run of the quick tier (211 processes: battles, menus, shops, the story's scenes;
`CT_SPC_COVERAGE=build/_spccov/q_%d.cov python tools/regress.py --quick`):

```
spc-driver: 3156 instructions (6547 bytes) reachable from $0200 and the dispatch tables; 2646 run (84%)
spc-driver: event     $1554: 60 entries, 54 run; not run: CA CC DE F1 F4 F9
spc-driver: lookahead $1608: 11 entries, 10 run; not run: EA
spc-driver: cmd10     $1680: 16 entries, 15 run; not run: 15
spc-driver: cmd10pre  $16A0: 4 entries, 4 run; not run: -
spc-driver: cmd80F0   $16C0: 32 entries, 23 run; not run: 80 8E F4 F5 F6 F8 F9 FD FF
spc-driver: upload    $1544: 8 entries, 4 run; not run: 00 01 04 06
```

The attract alone reaches 76% (2412). What no run reaches -- six events, one song command, nine of the `$80`/`$F0`
commands, four upload kinds and scattered branches -- needs constructed command streams in the port's lockstep.

## The port (steps 2-5)

1. **The driver in C** against a voice-level interface (key on with sample / pitch / envelope, pitch, volume, echo,
   noise), whose first implementation writes the S-DSP registers, so it is checked **tick by tick** against this
   program on the vendored SPC700: per tick the DSP register file, the key-on / key-off values written, the four status
   bytes, and (while the layout is shared) the driver's RAM. The tick is the unit both agree on: a command acts at the
   end of the tick it arrived in, or at the loop's poll. Paths no run reaches (above) get constructed command streams.
2. **The mixer**: `snes/dsp.c`'s voices, envelopes, echo and noise behind the voice operations, sample-exact against
   the S-DSP.
3. **The assets**: songs (tracks), samples (BRR), instruments (tuning, ADSR), percussion and the effect sets made by the
   extractor; the loader reads them instead of bank `$C7`. The uploads' protocol cancels out between two C halves, but
   the FRAMES the game waits on a busy SPC are part of the game under the rule (CLAUDE.md, "The SPC under the rule"):
   the native model must charge exactly those.
4. **Belthasar without the SPC700** and without bank `$C7`.
