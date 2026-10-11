# The vblank-flag spin under the rule (`$4210` RDNMI) -- ADOPTED, the default since 2026-10-09

2026-10-08: the user delegated the choice ("I don't know. make the best decision") after the staff roll showed the port 2 and 7
frames ahead of the rule's cartridge leaving scenes 05B and 05C (field_caption_plane.md sec. 7); the first version was
measured and left off (sec. 4). 2026-10-09: the rule as it stands (sec. 2) is the default; `CT_ORACLE_RDNMI=0` brings the
race back, for measurement only (snes/snes.c `lfRdnmiMain` / `lfRdnmiArmed`).

## 1. The race

ROM census of every `$4210` read (`AD/AF/2C 10 42`): four spins `LDA $4210 / BPL` and acks whose value is discarded.

| spin | NMI | reached by |
|---|---|---|
| C0/0B38 | off (`LDA #81 / STA $4200` after) | C0/286C's tail (C0/0B28) |
| FD/EADA | off (`LDA #81 / STA $4200` after) | FD |
| C0/3B71 | ON | ops 0xDF / 0xE1 (C0/3B98 / 3B4B), then C0/0B4E, `$0100` at 3B89, `BRL ->0000` |
| C0/4181 | ON | op FF 96 (table C0/400E sub 6), then C0/0B4E, `JML FD/C000` -- one site, scene 0AC |

(The $C3 sequence program, `$C3:1313` decompressed, reads no `$4210`.) The step into line 225 raises RDNMI and the NMI
together (snes/snes.c `inNmi = true; cpu_nmi`); the field NMI reads `$4210` itself (C0/EA70), so an NMI-on spin leaves only
when its own read cycle falls after the flag rose and before the NMI is taken -- the CPU's cycle phase against the beam.
The infinitely fast CPU does not remove it: the spin is a wait, the beam advances through it, and the phase decides.
91 static DF/E1 sites.

Measured on the rule's cartridge before the rule decided it (CT_ORACLE_RDNMI=0; `--monitor 7E0112,7E0100 --writer`; `$0112` = the op's visit (C0:3B5A), `$0100` = the
store (C0:3B89)):

| run | exit | race (frames, visit -> store) |
|---|---|---|
| saves/field_re/s5b_roll_f2280.state | 05B -> 05C | 3 (f27 -> f30) |
| | 05C -> 05D | 6 (f549 -> f555) |
| saves/attract_cena_inicial.state | 01B -> 0AD, -> 1B1, -> 087, -> 1F0 | 1, 1, 6, 5 |
| power-on (`--from boot`) | the same exits | 1, 1, 2(+), .. (other runs, other phases) |

## 2. The rule (default)

The race is decided for the poll. A main-thread `$4210` read (interrupt depth 0) that comes back clear arms the poll
(`lfRdnmiArmed`); the next step into 225 with the NMI enabled, under the infinitely fast CPU, raises a main-thread flag
beside `inNmi` (`lfRdnmiMain`) if, and only if, the poll is armed; the next depth-0 read returns bit 7 if either is set and
clears both; the end of vblank clears it. So the vblank that rises WHILE the spin waits is the spin's, whatever the NMI's
ack did first: the spin leaves on the first vblank after it began, one frame after the visit, every time. A handler's read
is the hardware's; a NMI-off spin (C0/0B38, FD/EADA) has no handler racing it and reads what it always read.

Not the vblank before the spin. The visit runs inside the main loop's iteration, which begins after the NMI of its own
vblank -- and that NMI's C0/EA70 has read (acked) `$4210` before the main thread runs a single instruction of the
iteration. No CPU at any speed can see that flag from the op. The infinitely fast CPU puts the visit at line ~228 of that
same vblank (the beam stands while the CPU works), which is why a flag that only expired at line 0 looked visible there
(sec. 4).

MEASURED under the rule (`--hookpc 3B5A / 3B89 / 0B3F / 2866`, `--monitor 7E0112,7E0100,7E0119 --writer`):

| run | exit | visit | store `$0100` | NMI on (0B3F) | ramp done (2866) | port (ct_field, unchanged) |
|---|---|---|---|---|---|---|
| s5b_roll_f2280 | 05B -> 05C | f27 | f28 | f46 | f62 | load f29, entry f46, main loop f62 |
| | 05C -> 05D | f547 | f548 | f567 | f583 | entry f567, main loop f583 |
| attract_cena_inicial | 01B -> 0AD | f1411 | f1412 | f1437 | f1438 | (as before: its race was 1) |
| | 1B1 -> 087 | | f6959 (race: f6964) | | | |
| | -> 1F0 | | f8431 (race: f8440) | | | |

The port's reboot (field.c 'THE DF/E1 REBOOT', at the head of the next iteration, after that vblank's NMI) was this all
along. Targets: tools/caption_plane_cmp.py 05C / 05D d = +2 / +7 -> 0 (461/461, 789/789, the counts unchanged);
tools/f8_cursor_cmp.py 14/14; attract-07d-frog-squash-colours-vs-oracle 12320/12320 on its old pair (c4755 / p6828);
the full suite 782 pass / 0 fail / 10 skip (the baseline's ten missing inputs) with the caption check re-pinned to d = 0
and nothing else moved -- the attract's checks after 087 (Zombor's legs, the 1B0 / 1F0 legs) pass on their old pairs
although 087's store and 1F0's moved 5 and 9 frames earlier on the cartridge.

## 3. How a load begun at the vblank is phased (the NMI-off rule, measured)

A DF/E1 store (and an op-0xE0 commit, C0/0C9A) leaves the main loop at line ~228 -- the head of an iteration, right after
its NMI -- through C0/0B4E (`STZ $4200`, the NMI off) and `BRL C0/0000`. Then, NMI-off, all real time:

- snes.c's phase hold: the NMI went off `spent` cycles after the step into 225 (4,132 here, three lines); the next step
  into 225 holds the beam that long, so the stretch's work W runs from phase 0, line 225 of the store's frame I.
- C0/0066 (`LDX $0100`) on line 228; C0/1B8B's song JSL on line 231 (instant when the song is carried: 1,116 cycles);
  the decompressions, C0/A33B, the object init pass C0/5910 (each object's fn0, whose sound ops `JSL C7/0004` from
  C0/3C43 and wait on the SPC), C0/286C's pass, its tail C0/0B28 -> FD/C124 / FD/C2C1 / FD/C1EE (~29 lines), then
- C0/0B38 `LDA $4210 / BPL`, NMI OFF: the hardware's flag, set from line 225 to the end of vblank (cleared at line 0).
  The work ends inside the vblank (the 37 lines 225-261): the spin leaves at once. It ends past it: the spin waits for
  the next line 225. Either way `LDA #81 / STA $4200` (0B3F) falls in the label frame (frames counted from line 0) the
  work ends in, J = I + floor((225 * 1364 + W) / 357,368), and C0/28A4 `JSR C0/EC60` puts C0/2848's first decision (INC
  or 2866) on J + 1.

Measured (`--cputrace` with CPUTRACE_BEAM=1 from a state at c1405; `--hookpc`), the race against the first version:

| exit | rule | store (I) | 0A50 | 0B28 | first 0B38 | 0B3F (J) | 2866 |
|---|---|---|---|---|---|---|---|
| 01B -> 0AD | race (RDNMI=0) | f1412 l228 | f1433 l75 | f1437 l228 | f1437 l257 (in vblank) | f1437 l257 | f1438 |
| | first version | f1411 l228 | f1432 l90 | f1436 l243 | f1437 l9 (past it) | f1437 l225 | f1438 |
| 05B -> 05C | race | f30 | f44 l258 | f48 l19 | f48 l47 | f48 l225 | f64 |
| | first version | f27 | f42 l40 | f45 l63 | f45 l92 | f45 l225 | f61 |

So the start's phase moves nothing (the phase hold is that), and a load begun a frame earlier ends a frame earlier unless
W changed. At the AD exit W grew 15 lines (25 F + 32 lines -> 25 F + 47 lines), the end crossed the vblank's last line,
and J stayed on f1437 -- the "absorbed" frame. The 15 lines are the SPC's: the 0AD init pass's first song post (C0/3C43,
`10 18`) took 104,100 cycles from f1412's load and 124,572 from f1411's (the second post 79,644 / 79,326). At 05B W grew
44 lines (C0/1B8B's post of a new song) and did not cross: J moved with the store, -3 frames.

The port prices C0/1B8B on its own SPC (CtSceneLoadSongPrice: 5,456 -> 69,564 cycles for the same frame earlier -- the
cartridge's +44 lines) but not the init pass's posts (ct_vm.c runFunc, `init pass: the cost is not consumed here`), and
rounds a load as (I + 1) + ceil(price) against the cartridge's J = I + floor((225 * 1364 + W) / F): the open load clock
(memory ct-load-clock-floor-rule, whose `I + floor(W/F) + 1` is this J + 1 when W ends inside the vblank). The menu's
reload already prices its NMI-off stretch this way (field.c, `ct_nmi_wait_frames`: floor(C / F), + 1 when C0/0B38's
read lands past the vblank's 37 lines). Under the rule's poll the question does not arise for DF/E1: the store is the
port's frame, and the load after it starts where the port starts it.

## 4. The first version (store in the visit's frame) -- measured, rejected

`lfRdnmiMain` raised at every step into 225 with the NMI on, unarmed: the spin's first read (line 228, the visit's own
iteration) saw the vblank that began that iteration, so every exit stored `$0100` in the visit's frame, a frame before
the port. Measured with it:

| port delivery | `$0100` | after the exit |
|---|---|---|
| head of the next iteration | 1 frame late | the old timeline; the play clock ticks one NMI more per exit -- `$01F8` seeds 14/14 -> 6/14 |
| the visit's frame (after the tick; the load on the next iteration) | exact | caption 05C / 05D d = -1 / -2 (the 05B load's price crosses 17.0 with the SPC's +44 lines: 17 -> 18 black frames); the attract: every leg after the AD exit 1 frame early (the cartridge absorbs it, sec. 3; the port does not price the init pass's SPC waits), then 07D's battle a second frame early -- attract-07d 2516/12320 on f6828, 12320 on f6826 |

## 5. Repro

```
CT_ORACLE_LOGICAL=1 [CT_ORACLE_RDNMI=0] build/ct.exe --monitor 0112:2,0100:2 --writer --from saves/field_re/s5b_roll_f2280.state --input 0:600 --diff
CT_ORACLE_LOGICAL=1 [CT_ORACLE_RDNMI=0] build/ct.exe --monitor 7E0112:2,7E0100:2,7E0119:1 --writer --from saves/attract_cena_inicial.state --frames 9000 --diff
CT_ORACLE_LOGICAL=1 build/ct.exe --hookpc 0B38 --from saves/field_re/s5b_roll_f2280.state --frames 600 --btnscript 0:600    (and 3B89 / 0B3F / 2866)
CT_ORACLE_LOGICAL=1 PEEK_SAVE=ad_f1405.state build/ct.exe --peek saves/attract_cena_inicial.state x.raw --btnscript 0:1405
CT_ORACLE_LOGICAL=1 CPUTRACE_BEAM=1 build/ct.exe --cputrace ad_f1405.state 35 ad.txt      (C0:0B5D, the C7 calls from C0:3C43, 0B28 / 0B38 / 0B3F)
```
