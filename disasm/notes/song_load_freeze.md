# The SONG-LOAD FREEZE — `JSL $C7:0004` uploads a song with the field engine stopped

2026-09-02. Found by the gate-cutscene lockstep (`vmdiff-scene8-gate-cutscene`) once the anim family
completed on the ROM's bytes: the next divergence, f154, is obj11 stepping past `EA 18` while the
oracle's IP sits on it for 39 frames. NOT PORTED; this note holds the measurements, the two
transfer-cost models tried, the one that does not fit yet, and the design a port needs.

## 1. What the ROM does

`EA nn` (C0/3C2F) stores the song id to `$7E29AE` and `$1E01`, `$1E00 = $10`, and `JSL $C7:0004`
(C0/3C43). `$C7:0140` gates on `$1E05` bit 7 (driver idle), dispatches on the command, and for a
song load runs the loader ($C7:01A1, ported byte-exact in `src/audio.c CtAudioLoadSong`): the
instrument-set diff, the sample blocks, the sequence block and the per-song block are pushed to the
SPC700 through the ports, THREE BYTES PER TOKEN HANDSHAKE (C7/06CD-06F3 + C7/09DA `STA $2140 / CMP
$2140 / BNE`), and the JSL does not return until the last echo. Nothing else in the field runs
meanwhile: NMI still fires (the frame counter, OAM DMA of the last frame, the joypad read), but the
main loop C0/0097-00DB is parked inside the op. When the JSL returns, the runner continues the
queue -- and stops at once on the raster gate (C0/5A5D `CMP $69`, `$0169 = $48`) because the
scanline is past 72 -- then A810/CA76/B271 run once for that frame.

## 2. Oracle measurements

| where | op / cmd | song | units (port count, seeded WRAM) | oracle cost |
|---|---|---|---|---|
| scene 8 gate cutscene, obj11 @029D | `EA 18` cmd $10 | $18 | 11152 handshakes, 33353 B, 13 blocks | f155 vPos 12 -> f194 vPos 169 = **39 frames + 157 lines = 39.6 f** (`--hookpc 3C43` / `3C47`) |
| same fixture, obj11 @0298 | `EA 00` cmd $10 | $00 (stop) | 95 handshakes, 206 B, 5 blocks | vPos 12 -> 117 = **105 lines = 0.40 f** |
| im_so_sorry.state, obj18 | `EA 00` | $00 | (same) | vPos 20 -> 125 = **105 lines** (reproduces) |
| forest ambush, posted by the FIELD side C0/1BAB (`$1E10 = $FF` first) | cmd **$14** | $45 | 14079 handshakes, 42125 B, 16 blocks (the mode swap adds the `$2F00` SFX block; 12029 without it) | `--hookpc 1BCE` f174 vPos 141 -> `1BD2` f230 vPos 83 = **55 frames + 204 lines = 55.8 f** |

`--songload <id> [--from STATE]` now prints the transfer accounting (this session): token
round-trips, streamed bytes, command blocks, and the SPC700 cycles the emulated driver stepped.

## 3. The battle path, probed (2026-09-02, later the same day)

* `cmd $14` is NOT a different handler: the service's table at `$C7:0AD9` maps `$10/$11/$14/$15` all to
  the loader `C7/01A1` (`$12/$13/$16/$17` -> the epilogue `C7/0192`). The battle entry's poster is the
  FIELD side, `C0/1BAB`: `$1E10 = $FF` (C0/1BC1), `$1E01 = $FA` (the song), `$1E00 = $14`, `JSL` at
  C0/1BCE. Exact span on the forest ambush: `--hookpc 1BCE` f174 vPos 141 -> `--hookpc 1BD2` f230
  vPos 83 = **55 frames + 204 lines = 55.8 f**.
* **`$1E10 = $FF` is the driver MODE switch** (C7/04C2: `LDA $10 / CLC / ADC #FF / LDA #00 / ROL` ->
  0/1, compared with `$11`; a change bumps `$F3`, and `$F3 != 0` uploads the SFX-set block to ARAM
  `$2F00` -- `C7_SFXSET_A/B`, `cmd 03`). The port's loader already has it (`audio.c` "$C7:04C0"), so
  `--songload` gained `CT_SONGLOAD_CMD=<hex>` and `CT_SONGLOAD_1E10=<hex>` to stage the battle's exact
  post. Result on the forest WRAM: `$45` cmd $10 = 12029 handshakes / 35978 B / 15 blocks; with
  `$1E10 = $FF` = **14079 handshakes / 42125 B / 16 blocks** (+2050 handshakes = the SFX block).
  That is most of the gap the first two models could not explain.
* Per-handshake cost on the oracle: `$18` 674,700 SPC-cycle-equivalents / 11152 = **60.5**; `$45`
  950,700 / 14079 = **67.5**. A first-principles split: the 65816 side of one unit (C7/06CD-06F3 +
  the C7/09DA token, counted from the listing, FastROM) is ~91 CPU cycles = ~26 SPC-cycle
  equivalents; the SPC side is ~30 cycles (the port at CT_AUDIO_QUANTUM=4: 342,608 / 11152 = 30.7);
  plus poll latencies -> ~60 for `$18` (fits). `$45`'s extra ~7/unit is still unexplained (the
  `$2F00` block's own cost? bank-crossing `INC $14` arms?). Two candidates ruled in/out the same day:
  the ECHO-DELAY change is on the OTHER load -- the gate fixture's `$1EF0` is 04 and song `$18` sets 06
  (`sndCull` + the SPC re-inits its echo buffer at GO), while the forest's `$45` keeps 04 -- so it
  cannot be `$45`'s surplus; and **cmd `$14` itself costs the driver more**: on the forest WRAM with
  `$1E10` untouched the port's SPC count is 644,336 (cmd `$10`) vs 673,392 (cmd `$14`) at quantum 16,
  i.e. the SPC does ~4.5% more work for a `$14` (a stop/fade step before the load is the natural
  reading; the driver side needs an SPC700 disassembly to confirm). The residual after both is a
  fixed ~6 frames on the battle path, not a per-unit rate.
* ⚠ **The port's SPC clock is not a clock.** `--songload`'s stepped-cycle count depends on the polling
  quantum: `$18` = 595,840 at quantum 16, 342,608 at 4, and at 1 the handshake breaks (the 0x400-poll
  caps expire: every load reports 18,432). The emulated driver's per-unit PROCESSING is the
  quantum-4 figure; the rest is the port's own detection latency. Do not fit anything to quantum 16.

## 3b. Cost models tried before the probe (kept for the record)

1. **Linear in handshakes** from the two field points: T = 76 + 0.9288 lines/unit (0.9288 lines =
   1267 master cycles = ~60 SPC cycles per unit). Predicts `$45` at 42.7 f with the cmd-$10 unit
   count; with the mode-swap units (14079) it predicts 50.2 f against 55.8 measured.
2. **The port's own SPC clock** at quantum 16: `$18` 34.97 f vs 39.6, `$45` (mode swap) 45.84 vs
   55.8, `$00` 4.49 vs 0.40 -- see the quantum note above.

What a derived model looks like now: T = units x (CPU_unit + SPC_unit + latency) + per-block and
fixed overheads, with CPU_unit from the instruction listing and SPC_unit from the emulated driver
measured at a fine quantum with the poll caps raised -- no fitted constant. Not built yet.

## 5. MEASURED (2026-09-02, 'meca'): the load is three phases with three rates

Instruments: `--hookpc C709E5` (the instruction after the token echo: one visit per handshake, with the
CPU's PC at every frame boundary), `--monitor 7E1E20:32` (the resident-set table, which gains an entry the
frame a sample block starts), and `--songload` on the ORACLE'S OWN driver page (`CT_SONGLOAD_SEEDPAGE=1`
copies $1E00-$1FFF from the `--from` state, skipping the field loader's $81 pre-post that the event op
does not send). With the page seeded the port's transfer list matches the oracle's block by block:

| load | port plan (seeded page) | oracle phases (`--hookpc C709E5`) |
|---|---|---|
| `$18`, scene 8, `EA 18` (cmd $10) | 6 sample blocks 6693 units, dir/params 64, seq 944+13; defrag moved 10269 B | f155 3 tokens, f156 silent, f157 3 tokens, **f158-f164 silent (7 f)**, f165 142, f166-f189 **~266/frame**, f190-f193 ~257, f194 169 |
| `$45`, forest, `C0/1BAB` (cmd $14, `$1E10=$FF`) | 8 sample blocks 8402 units, dir/params 64, `$2F00` SFX 2049, seq 545+13; defrag moved 8964 B | f174-f175 silent, f176 3 tokens, **f177-f182 silent (6 f)**, f183 3 tokens, f184 silent, f185 96, f186-f229 **~249/frame**, f230 83 |

* **The silent phases are the DEFRAGMENTER's memmoves on the SPC** (the CPU parks in the C7/09DA spin
  after the move's third token): 10269 bytes in ~8 frames and 8964 in ~7 -> **~13.3 SPC cycles per byte
  moved**, consistent across the two loads. The cold-page port never defragments (nothing is
  resident), which is why the first cost models could not see this phase.
* **The streaming rate is per-scene**: 266 handshakes/frame in Leene Square, 249 in the forest = 64.2
  vs 68.6 APU-cycle-equivalents per handshake. The per-unit floor from first principles is ~52
  (the 65816 loop is 96 cycles = 720 master with SlowROM -- CT never writes $420D [WRONG: it does, C2/0026; see section 6] -- = 34.4
  APU-eq; the SPC's token->echo residual is ~13 once it has had >= 24 cycles since its last echo,
  its full loop is ~19; poll latencies ~4), i.e. ~328/frame with nothing else running. The rest is
  the frame's background CPU load -- NMI (OAM/HDMA/VRAM queue DMA), active HDMA channels, DRAM
  refresh -- which is scene-dependent: ~19% of the frame in Leene Square, ~24% in the forest.
* **The head**: the post's handshake plus the driver's own preamble, ~1-2 frames; cmd $14 makes the
  driver do ~4.5% more (a stop/fade step, port-measured); the tail (GO) is small.
* Song `$00` is a real load with an empty set: 95 handshakes, 105 lines, and it moves the echo
  delay (04 -> 05), which culls the topmost resident set through C7/0A12 (the fixture lost `$31`).

Model that follows (oracle-anchored the way copyFreeze is, nothing fitted to the port's own clock):

    frames = head + units_stream / R_scene + moved_bytes * 13.3 / 17038

with `units_stream` and `moved_bytes` from the port's loader on the live page (the two counters
`CtAudioLastLoadUnits` now reports), `R_scene` the scene's streaming rate (266 / 249 measured; its
first-principles form is 17038 / (52 + background)), and `head` ~2 frames. Checked: `$18` ->
6693/266 + 957/257 + 64/266 + 8.0 + 2 = 37.7 (measured 39.6); `$45` -> 8402/249 + 2049/249 + 558/249
+ 7.0 + 2 = 53.2 (measured 55.8). Both ~5% short, in the same direction: the per-block setup on the
SPC side (6 and 8 sample blocks) and the cmd-$14 preamble are the unmodelled remainder.

What blocks the port is no longer the rate: it is that `R_scene` needs the scene's background load,
which the port does not model at all (no NMI cost, no HDMA cost). Two honest options: pin R per
measured scene as a ratchet, or derive the background from the ROM's NMI/HDMA work per frame.
## 4. What a port needs (design, not built)

* **The freeze**: after a sound op's post, `K` whole VM ticks are lost, `K = floor((v0 + T) / 262)`
  with `v0` the visit's scanline (12-20 measured for the early-queue objects) and `T` the JSL's
  cost; the tick of the op runs its legs once (that is the ROM's resume frame), then the skipped
  ticks, then normal ticks. Both hosts: `CtSceneTick` early-returns while `soundFreeze > 0`; field.c
  skips its own per-frame work (player controller, camera input, box machine, the compose's anim
  tick) while the VM is frozen.
* **The raster cut**: any op costing more than ~60 lines ends the runner's queue for that frame
  (`$0169` = 72 lines). Even `EA 00` (105 lines) does. This is the C0/5A46 gate the port does not
  model at all; a sound-op cost is the first place it is measurable.
* **The cost**: needs a model that fits all three points before it can be pinned. Until then the
  gate-cutscene lockstep stays a ratchet at f154.
* `--vmdiff` needs an audio instance (or a WRAM-only planner) to price the post; `Belthasar` can
  read the units straight from the service it already calls synchronously (`fieldSoundPost`).

## 6. PORTED (2026-09-02, 'go ahead'): the loader clock, the suspended tick, the tail overrun

Two premises of sections 3-5 were wrong and are corrected here:

* **CT DOES write `$420D`.** `C2/0026: LDA #01 / STA $420D` at boot -- FastROM is ON, banks >= $80
  run at 6 master cycles a byte, and the loader (bank $C7 code reading bank $C7/$C8 data) runs at
  FastROM speed. The '96 cycles = 720 master, SlowROM' unit of section 5 was a SlowROM count. The
  real 65816 side of a streaming unit is **520 master cycles outside the poll loop** (the FAST loop
  C7/06F9: no per-byte bank test) or 574 (the C7/06CD loop, chosen at C7/06C1-06CB when Y + size
  carries, i.e. the block crosses a 64 KB bank; every byte then pays a taken `BNE ->` past `INC $14`),
  plus **42 per extra poll of `$2140`** (CMP 24 + BNE 18) and 36 for the first CMP and the fall-through.
  Measured on the oracle with a POLL BUCKET in the cycle-tag probe (`CT_HOOK_CYCLES=1`, bucket 5 = the
  CPU inside C7/09E0-09E3): gate cutscene f166-f171, main (outside the poll loop) = 520 +- 2 per unit,
  poll = 41.6 per CMP visit -- the listing, to the cycle.
* **The background is per ENGINE MODE, not per scene.** The cycle-tag probe splits every frame's
  357,366 master cycles into CPU-outside-interrupts / CPU-inside-NMI+IRQ / DMA / HDMA / DRAM refresh
  (snes.c `g_cycTagCycles`, dma.c tags its runs, main.c detects interrupt entry by `intWanted` and
  exit by the stack pointer). With the field's main loop parked: **NMI+IRQ 12,097 (+-100), DMA 0,
  HDMA 14,310 (+-200), refresh 10,480** -- identical on Leene Square and the forest field (the same
  six HDMA channels: ch0 $2107 m4, ch1-3 $210D/$210F/$2111 m3, ch4 $212C m4, ch6 $2131 m1, all
  indirect from $7F:1238+). In BATTLE mode (the forest ambush's cmd $14 is posted after the vector
  switch): NMI+IRQ ~27,600 with 5,434 of DMA in a 4-frame pattern, HDMA 17,904 (eight channels).
  'R_scene' was those two blocks; nothing in them is per scene beyond +-0.1% of the frame.

The per-unit SPC side is not modelled at all: the port RUNS the driver. Its response distribution
matches the oracle's mode for mode (`CT_HOOK_POLLHIST=1` on the oracle vs `CT_AUDIO_LDTRACE=1` on the
port, the $18 load: 16 > 15 > 14 > 13 extra polls, means 15.46 vs ~15.3; the long tail at 28-45 polls
is the driver's periodic tick landing inside a unit, on both).

### The loader clock (audio.c)

`CtAudioService` brackets every `JSL $C7:0004` with a master-cycle clock (`LdClock`):

* the 65816 side is charged instruction by instruction from the listing through the `I_*` macros
  (ROM 6, WRAM 8, B-bus 6, internal 6): the unit loops, the token handshake C7/09DA, the block
  open C7/0680-06CB, the two-byte pushes C7/03EF, the defragmenter's stores C7/025A-02DB, the post
  loop C7/0A5A (4 stores, 66 per poll, a re-post every $400), GO C7/0601, the raw-post path C7/061C
  (which DOES wait for the echo), the prologue/epilogue, and the DIFF C7/01B5-0257 + append
  C7/0326-037B **charged loop by loop as the port walks the same tables** -- the resident search is
  88 cycles a compare and the DIFF alone cost 20 lines (~27k cycles) on the $18 load
  (`--hookpc C701B5` line 17 -> `C7025A` line 37); charging it as a flat 3,000 was the 18-line
  shortfall the first build had;
* the SPC is caught up before every port access by the master cycles elapsed x apuCyclesPerMaster
  with LakeSnes's whole-opcode overshoot carried (`ldCatch` = `snes_catchupApu`); a port write lands
  at the END of the STA's B-bus cycle, a poll reads at the end of the CMP's (`snes_cpuWrite/Read`);
* the raster walks with the clock (`ldWalk`) and queues the background as STOLEN time the CPU sits
  out while the SPC keeps running: refresh 40 at hPos 536, the mode's HDMA share at hPos 1104 on
  lines 0-224, the mode's NMI+IRQ(+DMA) block when line 225 is reached;
* what comes out: master cycles, NMIs crossed (= frames the field loses), the return line, polls.
  `CtAudioSetEngineMode` (field.c sets battlePhase) picks the background, `CtAudioSetPostLine` the
  line the JSL begins on (12 = the runner's early queue, measured), `CtAudioLastPostRaster` reads
  the result; `CtAudioSeedFromApu` adopts a savestate's SPC/DSP/ARAM so the driver is in the
  oracle's state; the SPC time a clocked JSL ran is DEBITED from `CtAudioFrame`'s budget (the host
  freezes for those frames, and the emulated SPC must not run them twice). ⚠ WRONG, CORRECTED 2026-09-14:
  `CtAudioSamples` used to output SILENCE on a debited frame, 'which is what the SNES outputs' -- it is not.
  The CPU waits in C7's handshake; the SPC and the S-DSP keep running, and the voices keep sounding.
  Measured with the new streams (ct.exe --peek `PEEK_PCM`, Belthasar `CT_AUDIO_PCM`) on the attract's demo
  battle: the cartridge's output has sound on every one of the 14 frames of `70 01` (C0/1BDF) and the 11 of
  `70 00` (C0/1C89); the port had 14 and 11 silent frames -- the user's 'when battles start/end the
  music stutters'. And the samples the DSP emitted during the JSL were mostly LOST: dsp.c keeps a
  0x400-sample ring that dsp_getSamples reads the newest 534 of, fine for one frame of SPC between reads,
  not for 14. Now every SPC run in audio.c goes through `apuRun` (<= 16000 cycles a piece, then `audTake`
  moves the new ring samples into a FIFO) and each output frame is the next 534 samples of that stream.
  Check: battle-jsl-audio-keeps-playing (tools/battle_jsl_audio_cmp.py).

Results, seeded page + seeded APU (`--songload 0x18 --from scratchpad/s8_f153.state`; the id is
parsed with strtol base 0 -- `18` is decimal 18 = song $12, which is how one run 'refused'):

| load | port clock | oracle |
|---|---|---|
| `$18`, gate cutscene, from line 12 | 39.60 f, **39 NMIs, line 168** (SPC phase +-6 lines) | 39 NMIs, line 169 |
| `$45` cmd $14, forest ambush, battle mode, from line 141 | 55.90 f, **56 NMIs, line 115** | 56 NMIs, line 83 |
| `EA 00` (stop), from line 12 | 0 NMIs, line 116 | line 117 |

The port's block timeline matches the oracle's `$1E20` registrations frame for frame (+2.87 / 15.00 /
17.43 / 19.26 / 25.59 / 30.18 f vs +3 / 15 / 17 / 19 / 25 / 30); the two defrag moves land at 0.04 and
2.86 f (the oracle's second at f157 line 259 = 2.85 f): the SPC's memmove time is emergent, not a rate.

### The consumer (ct_vm.c) and the frame's real shape

`--hookpc` on the gate cutscene (frames 140-200) gives the field frame its actual layout: the main
loop head C0/0097 at line 239 (right after the NMI + handlers), the queue-build C0/59D9 at 254, the
runner C0/5A46 at 259 with its visits (C0/5AA3) spilling into lines 0-8 of the NEXT frame, the drain
C0/00D6 at 36-48, the draw pass C0/00DB at 35-70, and the NMI WAIT C0/EC60 entered at 69-102 (134-188
on heavy frames). So a 'tick' straddles two raster frames, and the vmdiff window w (vblank to vblank)
holds the tick that started at line 254 of hookpc frame w. The sink's `post` now returns
`frames*1024 + line`; after the posting visit the tick:

* SUSPENDS when frames > 0 (`sndWait`): pass 1 is skipped on the resume tick (the ROM's queue was built
  before the JSL and no cadence moves while the loop is parked), the eligibility snapshot is kept
  (`sndElig`), the op's ip commit is DEFERRED to the return frame (`sndSavedAddr`: the ROM stores the
  ip when the visit ends, so `$1196` reads 063D for the 39 frames -- the f154 divergence after the
  first build was exactly that), the queue runs on from `sndResumeN` -- it was CUT when the return line
  >= 72 (C0/5A5D) until 2026-09-27, when the runner's gate took the zelda3 model's fixed virtual beam (V = 0,
  the user's decision: the gate never cuts, on either side; anim_drain_cad9.md sec. 16); the legs run once
  on the return frame;
* loses ONE MORE window when the tail overruns the NMI: C0/EC60 is clear-then-wait (`INC $0152 / LDA
  $0152 / BNE`, the handler clears it), so a tail that ends past line 225 waits for the FOLLOWING NMI.
  Measured: the return at f194 line 169, the draw pass at 205, EC60 entered at 245; no C0/59D9 at f194
  254, the next at f195 -- window 194 had no tick. Rule: `return line + SND_TAIL_LINES >= 225`, with
  `SND_TAIL_LINES = 61` the normal tail (EC60 at 69 minus the runner's end at 8). The tail is emergent
  (heavy frames reach 134-188), so returns within ~20 lines of the boundary can go either way: that is
  why the clock had to land within a few lines of the oracle before the gate fixture could be CLEAN
  (its return line 169 sits 5 lines above the boundary);
* the same rule and cut apply to a post that costs no frame (the K = 0 path).

`--vmdiff` installs a sound sink whose `CtAudio` is seeded from the savestate's APU and advanced one
`CtAudioFrame` per oracle frame (`VMDIFF_NOAUDIO=1` restores the unpriced sink, `VMDIFF_SNDTRACE=1`
prints every priced JSL); the live host's VM sink (`fieldSoundPostVm`) returns the same price, and
`CtSceneSoundFrozen` gates the host's own legs (the followers) while a tick is lost.

**Graded**: `vmdiff-scene8-gate-cutscene` is CLEAN over 1500 frames (it was a ratchet at f154), and it
pins the two [snd] lines against the oracle's measured returns (117 and 169). Mutations declared and
killed: no suspension (f154), ip committed at the op (f154), no tail overrun (f197: obj13 activated a
frame early), SlowROM access costs (a frame late).

### Still open

* The host's OTHER posters are priced but not consumed: the location loader's song (C0/1B86, flushed
  by the per-frame catch-all service) and the battle entry's C0/1BAB post through the void battle
  sink -- `Belthasar` does not yet lose those frames (the location change is a black screen; the battle
  module would start K frames later). The init-pass posts (`runFunc`) ignore the price.
* `SND_TAIL_LINES` and the post line (12) are measured on one scene; a runner-position model would
  replace both.
* The clock's residuals: +-6 lines from the SPC's phase at the post, ~30 lines on the battle path
  (the battle background's 4-frame DMA pattern is charged as its mean).
