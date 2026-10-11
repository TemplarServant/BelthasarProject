# The scene transition's clock: the fade-out, the black load, and C0/2848's fade-in

Derived 2026-09-06 from the ROM + the oracle only. User report (translated from Portuguese): *"check the timing and the fade-out/fade-in
in the scene transition. it is not canonical"*. Fixture: `saves/field_re/scene468_stairs_to_120.state` (the
600 A.D. castle spiral staircase — three same-scene map exits in a row, so one 220-frame run measures
three complete transitions), drive Right+B (`--btnscript "81:220"`).

## 1. The four phases, and which of them is the main loop

The field main loop is `C0/0097-00BD`. A location load is **not a state of that loop** — it is a `BRL`
out of it and a return into it:

```
0C8A  LDA $17 / BNE ->0C8F / RTS          ; the drain, called from C0/0C76 <- C0/00A4 (main loop leg)
0C91  LDA $19 / BEQ/BMI ->0C9A            ; a change is pending:
0C97  DEC $19 / RTS                       ;   fade OUT, one INIDISP step per frame
0C9A  JSR C0/0B4E ... 0CB4 LDX $12 / STX $00 ... 0CC4 BRL ->0000    ; commit + RELOAD
0000  BRA ->005D
005D  JSR C0/0B4E / 0B64 / 0B75           ; PPU/IRQ quiesced, screen off
0066  LDX $0100 / CPX #01F0 / BMI ->0072 / JML C2/0000               ; >= $1F0 = the overworld
0085  JSR C0/0B86                         ; load init -- 0B96 STZ $17 (the latch), 0BA2 STZ $19 (BLACK)
0088  JSR C0/00F4 / B192 / 56A6 / 28AA    ; THE LOAD (map, objects, scripts, palettes, song)
0094  JSR C0/2848                         ; THE FADE-IN  <-- section 2
0097  <main loop>                         ; control
```

So a transition is **fade-out (main loop live) → black (nothing runs) → fade-in ramp (a loop inside the
loader) → main loop**, and only the first and last of those four are frames of `C0/0097`.

## 2. C0/2848 — the fade-in ramp, byte for byte

```
2848  JSR C0/286C                  ; ONE full pipeline frame (ends in C0/EC60); returns LDA $1E
284B  BNE ->2866                   ; $011E != 0 -> skip the whole ramp
284D  INC $19                      ; brightness, ONE step per frame
284F  LDA $1F / PHA / STZ $1F      ; <-- the controller runs with $011F = 0
      JSR C0/881E                  ;     so C0/8838's d-pad gate fails: C0/88EE never runs
      PLA / STA $1F                ;     restored BEFORE 00BF, so a script's op-0xE3 still takes
285A  JSR C0/00BF                  ; 00C1 JSR C0/59D9 (scheduler) / 00C4 C0/5A46 / A810 / CA76 (anim)
285D  JSR C0/EC60                  ; vblank
2860  LDA $19 / CMP #0F / BMI ->284D
2866  STZ $1E / STZ $0407
```

Two things this says that the port had wrong:

* the ramp **runs the scene** — `C0/00BF` is the same routine the main loop calls at `C0/00B7`, so the
  new location's object scripts, its animation drain and its camera all advance while it fades up;
* the ramp **is not the main loop** — no `C0/0097` joypad latch, no `C0/18D9` menu poll, no `C0/1F87`
  box machine, no `C0/0C76` (so neither the op-0xF0 brightness ramp `C0/1F24` nor the `$0117` changeloc
  drain ticks), and the player controller is called with the control byte forced to zero.

$011E (set by op 0xDE, `C0/3B45 LDA #01 / STA $1E`) is read in exactly one place in the whole bank —
`C0/28A7`, this skip — and cleared at 2842/2866. Not ported: with the ramp skipped the ROM leaves
`$0119` at 0 and something else must raise it, and that consumer is not derived. (`STA $1E` at
C0/73AA-73BC is the map-setup page, D=$1D00 → `$1D1E`, a different byte.)

## 3. The oracle timeline (measured, scene 468, Right+B)

`build/ct.exe --monitor "0119:1,0117:1,0100:2,1801:2,1881:2" --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:220" --frames 220 --diff`

| frame | what |
|---|---|
| f4 | the step's destination cell is an exit: `C0/9D24` raises `$0117 bit7`. `$0119` still `0F` |
| f5-f19 | `$0119` `0E`→`00`, one DEC per frame — **and `$1801/$1881` keep changing: the lead walks through the whole fade-out** |
| f20 | the drain finds 0 → commit; `$0117` back to 00 (`C0/0B96`), the load begins |
| f20-f50 | **black.** `--hookpc 0097`: 0 visits. `--hookpc 59D9`: 0 visits. Nothing runs at all |
| f46 | the arrival placement lands (`$1801/$1881` jump to the new cell) |
| f51 | `C0/2848`'s entry frame (`JSR C0/286C`): a pipeline frame, no `C0/00BF` — 59D9 still 0 visits |
| f52-f66 | `$0119` `01`→`0F`, one INC per frame; `--hookpc 59D9` = **1 visit on every one of them** |
| f67 | first `C0/0097` frame = the lead's first step |

The second transition in the same run repeats it exactly: stage f70, drain f71-f85, commit f86, black
f86-f117, ramp f118-f132, control f133. **Cycle: 66 frames; 15 fade-out + 1 commit + 31 black + 16 ramp
+ 3 running.**

## 4. What the port did, and what it does now

Before (`CT_FADETRACE=1` on the same fixture and drive): stage f3, ramp-out f4-f18, load f19, and then
a fade-IN with **the whole engine live** — the lead ran on through it and crossed the next flight at
f23, five frames after the previous load. Three warps in ten frames where the oracle takes 132.

After: load f19 (the `C0/286C` entry frame — no VM tick), `$0119` 01→0F over f20-f34, first main-loop
frame f35, next flight staged f38 — **three frames of running after control, exactly as the oracle's
f67→f70.** The shape is now the ROM's: `15 + 1 + [black] + 16 + 3`.

Ported in `src/field.c` (the fade machine drives `$0119` itself, `inLoadFade` kills the frame's input
and `loadFadeEntry` the entry frame's tick) and `src/ct_vm.c` (`CtSceneSetLoadFadeIn` — `CtSceneTick`
then skips the three legs that belong to `C0/0097`: the `$0151` latch, `tbMachineTick`, the `$0117`
drain, plus the op-0xF0 ramp).

**Independently confirmed by an older measurement**: `field-door-follower-lands-on-lead`'s notes already
recorded, from the oracle, that "the port's lead keeps consuming the held direction across the
transition (it ends at y=332 where the oracle's stays at 335.94) — the ROM ignores the d-pad while the
arrival runs". With the ramp frozen the port's lead now ends that drive at 336, i.e. on the arrival cell
the loader placed it on.

## 5. The one piece that is NOT ported: the black hold  [SUPERSEDED 2026-09-06]

> Most of it is ported now: `field_load_cost.md` prices the load's decompressions from C3/0557's
> own listing (cycle-exact against the oracle) and the port holds black for those frames. What
> follows is the original finding; the two phases still unmodelled are named in that dossier's §5.


The 31 frames at `$0119 = 0` are the ROM **doing the load** — bank `$C3` decompression, the object
loaders at `C0/A5xx-A7xx`, the DMA. This port loads in zero frames, so it is 31 frames short there and
the whole transition runs at half the ROM's length.

There is no constant to take, and none was invented: the same measurement on the op-0xE0 change of
scene 120 → 108 (`field_op_e0_fadechange.md` §4) gives **14** frames of black, not 32 — it is work, and
it is per-location. The one component that *is* already priced in this port is the location loader's
song upload (`C0/1B86`, `song_load_freeze.md` §6 — priced by the loader clock but not consumed on this
path); it is not the bulk of it, and on this fixture it is not even charged (`--hookpc 1B86` shows the
call at f20 for a same-location warp). Closing the hold needs the loader's own cost modelled, and that
is a separate lane.

Also still open, and unchanged by this work: the port's exit scan probes the **foot tile** every frame
where `C0/9CA1` sits on the **accepted step's destination cell** (`field_exit_scan_guard.md` §3), which
is why our stage lands one frame before the oracle's.

## 6. Repro / validation

```
build/ct.exe --monitor "0119:1,0117:1,0100:2,1801:2,1881:2" --from saves/field_re/scene468_stairs_to_120.state \
             --btnscript "81:220" --frames 220 --diff
build/ct.exe --hookpc 0097 --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:75" --frames 75
build/ct.exe --hookpc 59D9 --from saves/field_re/scene468_stairs_to_120.state --btnscript "81:72" --frames 72
CT_RUN=1 CT_FADETRACE=1 build/ct_field.exe --assets ct_assets.dat --scene 468 \
  --loadwram saves/field_re/scene468_stairs_to_120.lakewram --selftest --frames 400 --walkscript "r:400"
python tools/regress.py --only field-stairs-run-three-flights
python tools/regress.py --mutate field-stairs-run-three-flights
```
