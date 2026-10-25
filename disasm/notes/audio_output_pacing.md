# AUDIO OUTPUT PACING — the crackle is a PRODUCER/CONSUMER drift, not distortion

User report (translated from Portuguese): *"the sound sometimes cracks"*. An earlier report of the same symptom ("the sound
sometimes gets some cracks, mainly when I minimise the window") produced commit `2291d0d`, whose own
message admits **"THE CRACKLE ITSELF IS NOT GRADED"**. It still was not fixed; this is the actual arithmetic.

⚠ **This is a HOST mechanism, not a ROM one.** The ROM decides WHAT is played (the `$C7` driver, the song
loader, the SPC700/DSP emulation — all separately derived and byte-exact); nothing in the ROM knows about
SDL queues. The authority for the host side is the **vendored LakeSnes reference this engine's audio is
ported from** (`_vendor_LakeSnes/main.c`), and the fix is that reference's own shape.

---

## 1. The pipeline as it stood

SDL2 **push mode**, no callback and no threads of ours (`grep SDL_CreateThread|SDL_LockAudio|pthread` over
`src/` → nothing; SDL locks its queue internally, `SDL_audio.h:1213`):

- `src/field.c:6284-6289` — `freq 48000`, `AUDIO_S16SYS`, stereo, `want.samples = 2048`, `allowed_changes = 0`;
  `audioSamplesPerFrame = 48000/60 = 800`; buffer 3200 B. The boot banner prints it:
  `[audio] SPC700 up: driver in ARAM, 48000 Hz, 800 samples/frame`.
- Producer = the **game thread**, once per loop iteration: `CtAudioFrame` (17088 APU cycles = one NTSC
  frame, `src/audio.c:35-36`) → `CtAudioSamples` → `SDL_QueueAudio(3200 B)`.
- The DSP itself produces **534 samples/frame** (`snes/apu.c:85` cycles the DSP every 32 APU cycles) into a
  1024-sample ring, and `snes/dsp.c:584-593` nearest-neighbour-resamples the last 534 up to 800.
- Consumer = SDL's audio thread, draining 2048 frames = **8192 B every 42.67 ms**. On a shortfall it
  **zero-fills** — `SDL_audio.h:1199-1202`: *"If the device needs audio but there is not enough queued, it
  will play silence to make up the difference."*
- Frame governor (`src/field.c:7081`, `:7839`, `:9341`): `if (el < 16) SDL_Delay(16 - el)` = **16 ms**.

## 2. The arithmetic that makes the click

```c
audioAcc += (nowMs - audioT0) * 60.0 / 1000.0;
if (audioAcc < 1.0) audioAcc = 1.0;      // <-- floors AND destroys the fraction
int ticks = (int)audioAcc; if (ticks > 4) ticks = 4;
audioAcc -= ticks;                        // acc is now 0, whatever the real elapsed time was
...
if (SDL_GetQueuedAudioSize(dev) > samples*4*16) SDL_ClearQueuedAudio(dev);
```

The floor guarantees **≥ 1 tick per LOOP ITERATION**, so the producer emits **16.6667 ms of audio per
iteration** (800 samples at 48 kHz) while the governor budgets **16 ms**. Whenever the loop is paced by that
delay rather than by vsync, production exceeds consumption by up to **4.17 %**. Nothing else regulates the
level — the only feedback is the threshold clear, and `samples*4*16 = 51200 B = 12800 stereo frames =
**266.7 ms** discarded in one step`. That is the click; and immediately after it the queue is empty, so SDL
zero-fills every callback for the ~1.5 s it takes to climb back above one 2048-frame device buffer — the
plural "cracks".

**Measured before the fix** (`CT_AUDIO_PROBE=N`, dummy video+audio, four runs N=300..3000): marginal
**16.01 / 16.13 / 16.44 / 16.67 ms per iteration** against the 16.6667 ms break-even. The surplus is
run-dependent (0 to +4.1 %), so the clear fires every ~6-20 s — "sometimes".

The `ticks > 4` clamp is a second, rarer path: an iteration longer than 66.7 ms under-produces (a hole),
then the accumulator drains at 4 blocks per iteration = 786 kB/s against a 192 kB/s drain and crosses the
clear threshold in ~0.08 s — one hitch, one hole, then a burst of splices. (Not observed: the only measured
stall source is `CtAudioLoadSong`, **~13 ms per load** — `--songload 0x40` 112 ms, `0x40,0x40` 125 ms,
five songs 166 ms — which is well under the clamp.)

## 3. The fix (the vendored reference's own shape)

```c
audioAcc += (nowMs - audioT0) * 60.0 / 1000.0;   // no floor: the fraction CARRIES OVER
int ticks = (int)audioAcc; if (ticks > 4) ticks = 4;
audioAcc -= ticks;
for (t < ticks) {
  CtAudioFrame(audio);
  Uint32 q = SDL_GetQueuedAudioSize(audioDev);
  if (q <= samples*4*6) { CtAudioSamples(...); SDL_QueueAudio(...); }   // _vendor_LakeSnes/main.c:323
  else skipped++;
}
```

`_vendor_LakeSnes/main.c:323` is verbatim `if(SDL_GetQueuedAudioSize(glb.audioDevice) <= glb.wantedSamples *
4 * 6)` — queue only while the device holds at most **6 frames (100 ms)**. The device drains at exactly
48000 Hz, so the queue self-levels there and the catastrophic clear is not needed at all (it is gone). A
dropped block at a 100 ms backlog is one 1/60 s, against 266 ms dumped at 266 ms.

## 4. How it is graded

`CT_AUDIO_PACE=<iterations>` prints, once, and exits:

```
audiopace: iters=600 ticks=589 queued=589 skipped=0 maxq=9984 elapsedMs=9829 expect=589.7 drift=-0.7 verdict=OK
```

`ticks` = how many 1/60 s the SPC was advanced; `expect` = how many 1/60 s of wall clock actually passed;
`verdict=OK` iff they agree within 2. Both are measured **in the same run**, so the check does not depend on
the host being fast — only on the producer being clocked by TIME instead of by ITERATIONS. With the floor
restored the same run reads `ticks == iters` and `verdict=DRIFT` (the pinned mutation).

## 5. OPEN

- **The crackle itself is still not observable headlessly.** This grades the drift that causes it; a real
  windowed run is the only place the audio can be heard. `maxq` and `skipped` are printed so a future
  investigation has the two numbers that matter.
- **The 2048-frame device granularity** is what SDL was asked for; whether the backend honours it exactly
  cannot be observed with a prebuilt DLL.
- **`CtAudioLoadSong` is blocking by design** (`src/audio.c:409-411`) and costs ~13 ms; it is under the
  clamp today, but a slower host could push an iteration past 66.7 ms and take the `ticks > 4` path.
- The DSP's **nearest-neighbour 534→800 upsample** (`snes/dsp.c:588-590`) is the vendored resampler,
  untouched here; it is a fidelity question, not a continuity one.

## 6. ⭐ 2026-09-23 — ONE CLOCK FOR THE PICTURE AND THE SOUND (`hostPace`)

User report (translated from Portuguese): *"the demo scene is ending a little before the music"*. Measured (memory
`ct-attract-music-sync`): the attract posts song `$18` once and fades it with its script's `$81`; the
cartridge's fade lands at song position ~8861 frames, the lockstep port's at 8755, and the port **as played**
at ~8675. The last ~80 frames were this file's host: §3 clocked the SPC by the wall clock (60.00/s) but left
the ENGINE on `if (el < 16) SDL_Delay(16 - el)` — at least 16 whole ms, never paid back. On the user's 170 Hz
screen the loop measured **60.58 f/s** (900 passes in 14856 ms); headless under a coarse timer the old binary
ran **44-56 f/s**. Two clocks, drifting apart by the percent.

**The fix is the vendored reference's loop** (`_vendor_LakeSnes/main.c:278-297`): a performance-counter
accumulator says how many frames are due, each frame plays its own audio (`playAudio` after
`snes_runFrame`), the vsync'd present happens once per pass. The port runs one engine frame per pass, so:

* **`hostPace`** (field.c, top): frame k is due at T0 + k/60 s. Ahead → `SDL_Delay` to ~1 ms short, then
  yield to the deadline. Behind → no sleep, and `hostPresent` skips the present (it would block on vsync)
  while a whole frame behind. Behind by more than 250 ms (a drag, a breakpoint) → re-anchor: the stall is
  dropped for picture and sound together.
* **A paced host runs ONE SPC frame per engine frame** (`lockstep || g_sndPaced`). The wall-clock
  accumulator stays only for UNPACED runs (CT_NOPACE / --selftest), where the checks expect it.
* **Why §3's reason no longer holds.** The SPC had been moved off the engine's frames because a minimised
  window starved one-frame-per-pass audio. The cause was the governor: under Windows 11's coarse timer for a
  hidden window (15.6 ms), `SDL_Delay(16 - el)` slept ~31 ms and the lost time was never paid back. The
  deadline pays it back on the next passes — graded by `CT_HOST_TIMER_MS=16` (audio-pacing-coarse-timer).

**Measured after:** windowed on the 170 Hz screen, 840 frames in 14000 ms, ticks == frames. The paced
attract (headless, real time) has an audio output **sample-identical to the lockstep run's over all 11151
frames**, and its fade lands at 8755 like lockstep's. What remains between the port and the cartridge at the
fade (~106 frames) is not the host: 76 lag frames (the zelda3 model) + ~30 frames of port-vs-logical-oracle
leg lengths.

⚠ **SDL's `dummy` audio driver is not a device.** It consumes by sleeping, and under a headless process's
coarse timer it drains slower than 48 kHz: the level gate drops ~10% of blocks, the OLD binary as well
(63 / 235 in 1800 frames). A real device showed none (901 queued, 0 skipped). Three headless runs that logged
to a file in %TEMP% also showed 0.6-5.5 s stalls near attract frame 3420 that no piped run reproduced, and
no frame of an unpaced run takes more than 19 ms: an environment artefact, most likely a scan of the file
being written. Neither is graded.

## 7. ⭐ 2026-09-27 — THE DEMO'S SOUND FELL BEHIND ITS PICTURE AFTER THE RACE

User report (translated from Portuguese): *"in the game's demo, the sound is falling behind after a certain point, probably the mode 7 race
scene"*. Measured with `CT_AUDIO_FIFOTRACE=<n>` (audio.c `CtAudioSamples`: the FIFO before each output frame's take,
the dropped count, the SPC's cycle carry): the output plays one frame of SPC per displayed frame (534 samples), and
every sample the SPC made beyond that waited in the FIFO -- sound behind the picture, for good. At the demo's end the
FIFO held **~1.8 s**. Four leaks, the race's by far the largest:

1. **The race's logical-frame holds ran the APU** (snes.c). The translated race runs under the whole rule (the zelda3
   exception: a lag frame becomes a hold); the oracle's holds run the APU on, the CPU's partner -- right for the ORACLE,
   whose CPU is the cartridge's and whose handshakes inside a hold must be answered. A PLAYER cannot: the race holds
   ~35% of a frame per frame, all of it SPC time no displayed frame plays. `g_ctLogicalHoldApu` (snes.c, default 1 =
   the oracle's way): Belthasar sets 0, so the APU stands with the beam through a hold and a phase hold -- the zelda3
   model's relation (the SPC runs a frame per displayed frame whatever the CPU spent). A CPU that polls the SPC inside
   a hold is in a wait loop (C7:09E1), which releases the hold like any other. `CT_XLAT_HOLDAPU=1` restores the
   oracle's way in Belthasar (a probe).
2. **The field's per-frame sound catch-all kept handshaking through a stage** (field.c, `CtAudioService`): ~53 SPC
   cycles a frame on top of the stage's own 17088 (`CT_XLAT_APUTRACE=<n>` in seq_xlat.c prints the stage's APU
   cycles against its frames). No field code runs during a stage; the gate is `!seqX && !gwX`.
3. **A stage's partial last frame stayed queued** (audio.c's short read): the frame the stage exits in hands over
   fewer than 534 samples; the read took none and left them. Now a short read plays what is there, pads with the last
   sample and consumes it.
4. **The host paths that give the SPC time no frame stands for** -- an unclocked handshake at a location load, a
   clocked session's last opcode: ~100-250 samples a load, ~0.06 a frame. These are **NOT** debited from the SPC:
   tried, and the SPC's state left the oracle's (the 0AD battle-return upload 16 -> 15 frames, the scene-8 vmdiff
   JSL at line 164 against the oracle's 168, entry 3's picture 57344 -> 37465 identical) -- its clock is the
   cartridge's and the song loads' handshakes and the stages are graded on it. The OUTPUT drains them instead
   (audio.c, a host mechanism like the device's level gate): while 4 or more samples wait beyond this frame's 534
   and the SPC time a song load or a clocked JSL legitimately ran ahead (`cycleCarry` < 0: the frames the SPC will
   stand still for, carry/32 samples, which play on the frames they belong to), a frame takes 536 samples instead of
   534 -- 0.4% faster while it lasts, inaudible. The FIFO settles at the frame plus 2-3 samples: a frame's SPC makes
   533-535 (whole instructions against the carried fraction), and draining down to "2 or more" left 79 frames of the
   demo reading 533 and padding one. At "4 or more", 4 short reads remain, all at the translated stages (two race
   frames that made 532/533, and the race's and entry 3's partial exit frames). `CT_AUDIO_PCM` writes each frame AS
   PLAYED, read down to 534 by the nearest sample, centred (a drained frame keeps both ends and skips two samples inside), so frame k of
   the file is output frame k. Both other ways broke battle-jsl-audio-keeps-playing: the first 534 of a drained frame
   cut 2 samples at its end (a boundary jump, end-of-battle step ratio 2.71); the whole 536 drifted the file 2 samples a
   drained frame against the tool's 534-sample frames, its "boundaries" fell mid-frame and both its mutations SURVIVED.
   The same audit found the short read had blinded it too: a stream starved to 1-2 samples a frame became flat frames
   (the last sample held), which neither its all-zero silence test nor its step ratio saw -- a frame that does not move
   now counts as silent.

**Measured after** (CT_AUDIO_FIFOTRACE=1, the whole attract to f10800, net of the song loads' debt): the FIFO holds
the frame plus 2-3 samples on 9243 of 11000 frames and never more than 230 beyond the frame (a location load's leak,
drained within ~100 frames); at the demo's end, 537 samples (it was ~1.8 s).

**The trade-off (the zelda3 model, stated):** with the APU standing through the holds, the SPC leaves the race where a
player's SPC would, not where the logical oracle's (whose holds ran it) does. This is the relation the port's field
and battles ALWAYS had -- their SPC runs 17088 cycles per displayed frame and never ran through the oracle's holds;
only the translated stages ran it on, and now the whole port is one model. What moves: the 087 demo battle's two
driver JSLs, which wait on the SPC, each end one frame sooner (C0/1BDF and C0/1C89 14 -> 13 NMIs, CT_SNDTRACE;
CT_XLAT_HOLDAPU=1 puts both back). The post-race attract pictures re-anchor with their content unchanged: 087's ice
crystal -1 (f9786 -> f9785, its frame before ba39487), 1F0's Epoch departure and return -2 (f10724 -> f10722,
f10832 -> f10830).

**Graded:** `attract-sound-stays-with-the-picture` (tools/attract_audio_latency.py).

**The second report, the same day** -- *"after the race scene, the curtains' sound is damaged"* (translated from Portuguese) -- was the same
defect. The window wipe's whoosh (`18 BA`: voices 6/7 keyed with sources 02/07, then 6 with 04) measured ALONE on both
sides (`CT_DSP_VOICEMASK=C0` + `CT_DSP_RAWPCM` + `CT_DSP_KONTRACE`, tools/sfx_voice_cmp.py): the same on all 15
curtains of the demo, before and after the race -- RMS ~660, peaks 2600-3300, 250-300 zero crossings per 1000 samples
on both (never sample-identical: it is the DSP's noise generator, whose phase is the run's). Its samples, directory
entries and key-on frames are the cartridge's. What the ear got was the OUTPUT: the pre-fix build (0c68f62, rebuilt in
a worktree with only the FIFO trace added) held 1.83-1.87 s of sound behind the picture from the race on, so the 087
curtain's whoosh sounded almost two seconds after its curtain, and it LOST samples -- 396 in the race (the stage ran
more SPC than the DSP's 0x400 ring holds between two reads) and 221 + 224 at the 087 battle's two JSLs, whose pre-run
pushed the FIFO past its 65536 (the oldest samples, the ones about to play, thrown away: clicks). The current build:
one frame queued everywhere, none lost -- graded by attract-sound-stays-with-the-picture's last line.

A trap met on the way: the oracle state's ARAM does not belong to its WRAM. `saves/attract_cena_inicial.state` holds,
from its first frame, instruments $32 / $04+1332 / $1F+2313 at $4901 / $6626 / $79DF while its own loader page
($7E:1E20) says song $18's $2C / $09 / $27 are resident there (the port's ARAM agrees with the page). The cartridge's
MUSIC in that state is therefore not the real game's; its SFX (sources 00-1F, resident) are.
