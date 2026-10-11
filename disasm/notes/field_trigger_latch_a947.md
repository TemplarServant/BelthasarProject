# The CLASS-7 activation latch (C0/A947) and op 0x8D's raw placement words

Two defects that share one visible symptom: **a sprite-less trigger object never fires**.
User reports: *"crono, when stepping onto the pod in scene 8, does not trigger the event: does not teleport"* and
*"in scene 129, pressing action on the pendant on the floor (sparkle) does not trigger the event"*.

---

## 1. What a class-7 object is, and what gates it

`$1100,X` is the per-object CLASS / move-mode. An object whose init runs **no loader op**
(0x80-0x83) gets class **7**: a positioned, sprite-less coordinate object. Both of the
reported triggers are exactly that:

| scene | obj | init (fn0) | the function that never ran |
|---|---|---|---|
| 8 | 12 | `8B 04 04` SETCOORD tile(4,4) / `00` | fn2 (TOUCH) = the whole Telepod demo: `04 06 37` REQ Crono fn7, `02 06 38` fn8 (the spin/teleport), `04 00 33` the sparkles, `C1 68` the "Oh wow" box |
| 129 | 15 | `8B 08 0A` SETCOORD tile(8,10) / `87 08` / `00` | fn1 (ACTIVATE) = the pendant pickup: SHOW obj16-19, `E8 7B` x4, KILL the nuns, `D8 92 80` = the naga-ette BATTLE |
| 129 | 13 | `8D E0 02 00 08` SETCOORDpx (736,2048) / `0D 00` / `87 10` / `00` | fn1, gated on storyline >= 21 |
| 2 | 10 | (the bedroom curtain — already fixed, 6be9ada era) | fn1 |

Both ROM object scans skip an object whose `$0F00,X` low byte is 0:

- **activate** C0/1CFC (the action button)
- **touch** C0/9923 (the player-overlap probe)

`$0F00` is not a script flag: it is the per-frame CAMERA-WINDOW ACTIVATION LATCH that
C0/A810 maintains. A810 dispatches on the class:

```
A81B: LDA $1100,X
A81E: CMP #03 / A820: BCC ->A870     ; classes 0-2 (the PCs): AAFD then A88D
A822: CMP #08 / A824: BEQ ->A83E     ; class 8: SKIPPED, $0F00 untouched
A826: CMP #07 / A828: BEQ ->A86B     ; class 7: JSR C0/A947   <-- the leg in question
A82A:                                ; else: AA07 then A88D
```

## 2. A947 vs A88D — the compares are SIGNED and the edges are UNCLAMPED

```
C0/A947                                   C0/A88D (classes 0-6)
  LDA $1D0A / SEC / SBC #04                 LDA $1D0A / SEC / SBC #04
  STA $C3            <- NO clamp            BCS +2 / LDA #00      <- SATURATES to 0
  LDA $1D0C / CLC / ADC #04                 LDA $1D0C / CLC / ADC #04
  STA $C4            <- NO clamp            BCC +2 / LDA #FF      <- SATURATES to 0xFF
  LDA $1D0E / STA $C5   (no -2)             LDA $1D0E / SEC / SBC #02  (+ clamp)
  LDA $1D10 / CLC / ADC #05 / STA $C6       LDA $1D10 / CLC / ADC #05  (+ clamp)
  LDX $6D                                   ...
  LDA $1801,X / ASL A                       LDA $1801,X / ASL A
  CMP $C3 / BMI ->A985     <- SIGNED        CMP $C3 / BCC ->A910     <- UNSIGNED
  CMP $C4 / BPL ->A985     <- SIGNED        CMP $C4 / BCS ->A910     <- UNSIGNED
  LDA $1881,X / ASL A                       LDA $1881,X / ASL A
  CMP $C5 / BMI ->A985     <- SIGNED        CMP $C5 / BCC ->A910
  CMP $C6 / BPL ->A985     <- SIGNED        CMP $C6 / BCS ->A910
  LDA #80 / STA $0F00,X / SEC / RTS         ... LDA #80 / STA $0F00,X ...
A985: STZ $0F00,X / CLC / RTS
```

A88D's unsigned compares are safe **because** A8A3/A8AF/A8BB/A8C7 saturate the four edges
first. A947 does neither: it leaves `$1D0A - 4` wrapped and reads the difference's SIGN.

**The port used A88D's unsigned form on A947's unclamped edges** (ct_vm.c, the class-7 pass).
With the camera parked at the map origin `$1D0A = 0`, `c3 = (0 - 4) & 0xFF = 0xFC`; read
unsigned that is 252, so `tx >= c3` is false for every object on the map and **every class-7
object's latch stayed 0**. Scene 8's camera is at (0,0) and scene 129's x-cell is 0 — which is
why exactly those two triggers were dead while scene 2's curtain (camera mid-map, `$1D0A = 0x20`)
worked and hid the defect.

### Measurement (both directions)

```
$ ct.exe --monitor "7E0F18:1" --from out_s8.state --frames 12 --writer
  f0      7E0F18=80*(C0:A983)
  f1      7E0F18=80(C0:A983)     ...   obj12 = $0F00 + 12*2 = $0F18
```
`C0:A983` is the instruction after `A980 STA $0F00,X` **inside A947**. Oracle `out_s8.state`
obj12: `$1100 = 07`, `$1A81 = 01`, `$0F00 = 80`. Oracle scene 129 at storyline 0x12
(`--teleport 129 7 16 0`): obj13 and obj15 both `$1100 = 07`, `$0F00 = 80`.

### The fix

`src/ct_vm.c`, the class-7 pass: the four compares became signed 8-bit differences
(`(int8_t)(tx - c3) >= 0`, `(int8_t)(tx - c4) < 0`, …), matching BMI/BPL. Nothing else changed —
the pass already ran unconditionally for every mode-7 object with a position.

---

## 3. op 0x8D stores the script's words VERBATIM

`C0/4C27` (op 0x8D, SETCOORDpx):
```
INY / TYX / REP #20 / LDA $7F2001,X / LDX $6D / STA $1800,X
INY / INY / TYX     / LDA $7F2001,X / LDX $6D / STA $1880,X / SEP #20
```
Two 16-bit reads, two 16-bit stores. **No adjustment of any kind.**

Compare `C0/4B74` (op 0x8B, SETCOORD tile), which SYNTHESISES the low bytes:
```
$1801,X := tileX ; $1800,X := #$80      (X = the tile CENTRE)
$1881,X := tileY ; $1880,X := #$FF      (Y = the FOOT, i.e. anchor - 1/16)
```

So the "-1/16 foot anchor" is **op 0x8B's 0xFF**, not a property of every setter. The port
applied `y - 0.0625` to every placement at scene-create time, so a tile-ALIGNED 0x8D word
crossed a tile boundary: scene 129 obj13's `8D E0 02 00 08` has y = `0x0800` = 128.0 exactly,
and the port made it 127.9375 → **tile 7 instead of the oracle's tile 8**. The A947 window test
reads `$1881,X` (the tile), so obj13 sat one row above the camera window and could never be
activated even after fix 2.

Oracle `o129.raw.wram`: obj13 `$1880/$1881 = 0x0800` (46.0, 128.0), obj15 `= 0x0AFF`
(136.0, 175.9375 — the 0x8B foot form).

### The fix

`CtVmActor` gained `px16/py16` (the raw words, -1 = never placed); ops 0x8B/0x8C/0x8D fill them
(0x8B as `tile*256 + 0x80` / `tile*256 + 0xFF`, literally C0/4B7C-4B95); `CtSceneCreate` seeds
`fx/fy = px16/16.0, py16/16.0` when they are set and keeps the old `y - 1/16` only as the
fallback for an actor with no captured word. Scene 5 was the visible beneficiary — its
0x8D-placed objects (obj12 y `0x1F40` = 500.0, obj17 `0x21F0` = 543.0, obj23 `0x1780` = 376.0,
obj27 `0x2EF0` = 751.0, obj28 `0x2E80` = 744.0) now match the oracle exactly, which is why
`render-scene5-snapshot` / `camera-reenter-scene5` were re-pinned.

---

## Repro / pins

```
ct.exe --monitor "7E0F18:1" --from out_s8.state --frames 12 --writer
ct.exe --teleport 129 7 16 0 --from <storyline>=0x12 state> --out o129b.state --frames 240
ct.exe --peek o129b.state o129b.raw --btnscript "0:60"      # then read $1100/$0F00/$1880
python tools/dis.py A947 0x50 ; python tools/dis.py A88D 0x60 ; python tools/dis.py 4C27 0x1A
CT_TRIGDBG=1 build/ct_field.exe --assets ct_assets.dat --scene 8 --selftest --frames 6
```
Regression checks: `field-scene8-telepod-touch-trigger`, `field-scene129-class7-action-probe`
(the latter also grades the 0x8D words and the sub-screen foreground mask).

## Open questions

- The Telepod demo was pinned at the *touchable* level, not end-to-end: driving the player onto
  tile(4,4) from `--at 72 95` does not move him (he is blocked before the pod). The step-on
  geometry from natural play is still unverified headlessly — the same caveat the earlier
  scene-8 work recorded.
- op 0x8C (C0/4BC3) reads ONE byte from `$7F0200,X` into `$1801,X` and never writes `$1800,X`;
  the port still treats the local as a 12.4 fixed value (`rx >> 4`). Not touched here — no
  reported symptom — but it does not match the disassembly and should be re-derived.
- Scenes 92/240 show their LOADMON objects in our engine while the oracle (via `--teleport`)
  keeps them hidden (`$1A81 = 0`, all three have `91 HIDE` in fn0). Not chased: the oracle side
  of that comparison is a forced-load state, not natural play.
