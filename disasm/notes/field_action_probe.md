# The ACTION-button probe (C0/1AAC -> C0/1CFC -> C0/5AC5)

Derived from the ROM only. Every claim cites `tools/dis.py <fileoffset>` (bank C0 = the offset
directly). The field/event code runs with **D = $0100**, so a direct-page `$97` in the disasm is
WRAM **$0197**, and `$C3`/`$C5`/`$DB`/`$EB` are `$01C3`/`$01C5`/`$01DB`/`$01EB`.

Written 2026-08-15 because `src/field.c`'s activate search was an INVENTION -- a hand-written
proximity box ("ACTIVATE search v2", reach 1-2 tiles, `dx <= 10 && dy <= 12`, nearest-wins) whose own
comment admitted the case it missed: *"the curtain obj 10 is sprite-less"*.

**PORTED 2026-08-22** as `CtSceneActionProbe` + `CtVmActBoxAt` (`src/ct_vm.c`), which is where it
belongs -- the probe reads per-object WRAM (`$0F00`, `$1100`, `$1800`/`$1880`), so it is a VM routine,
not a render-level one, and `field.c` now just calls it. Pinned by `field-scene2-curtain-activate`
(3 mutations, all killed: the `$0F00` latch, the `#01C0` limit, the up/down arm sense).

## 1. The trigger -- C0/1AAC

```
1AAC  LDA $00F6 / BIT #80 / BNE ->1AB4 / RTS   ; A button; not pressed -> nothing
1AB4  LDX $34 / BEQ ->1AC1 ... STX $34 / RTS   ; a pending-thing guard (not derived here)
1AC1  LDA $1F / BNE ->1AC6 / RTS               ; gate (not derived here)
1AC6  LDA $EB / BPL ->1AD5                     ; $01EB ALREADY holds a target -> invoke it directly
1ACA  LDX $97                                  ; $0197 = the player object index (stride 2)
1ACC  LDA $1600,X / ASL A                      ; the player's FACING, doubled
1AD0  JSR C0/1CFC                              ; THE PROBE -- sets $EB, returns carry
1AD3  BCC ->1ADC                               ; carry clear = no target -> skip only the INVOKE
1AD5  SEP #30
1AD7  JSR C0/5AC5                              ; THE INVOKE
1ADC  BRL ->1DF4                              ; ...and then ALWAYS the TREASURE PROBE
```

> **CORRECTION (treasure pass).** This block used to annotate `1AD3 BCC ->1ADC` as *"carry clear = no
> target -> nothing happens"*. `1ADC` is not the end of the handler: it is `BRL ->C0/1DF4`, the
> **treasure-chest probe**, and the branch skips only `C0/5AC5`. So an A press ALWAYS ends in the chest
> probe, hit or miss. Reading that branch as a dead end is why the port shipped with no chests at all;
> the whole subsystem is in [field_treasure_chests.md](field_treasure_chests.md).

Note `1AC6`: a target already latched in `$01EB` (the touch scan's hit) is activated **without
re-probing**. `$01EB` is the same latch the touch path uses (`field_touch_preempt.md`).

## 2. The probe -- C0/1CFC (`dis.py 1CFC`)

```
1CFC  REP #20 / AND #0006 / STA $DB            ; facing*2 & 6 -> 0/2/4/6 = the handler index
1D03  LDA #0080 / STA $EB                      ; $EB = $80 = "no target"
1D08  LDX $97 / LDA $1800,X / STA $C3          ; $C3 = player X   ($1800 = 16-bit X, pixels*16)
1D0F  LDA $1880,X / STA $C5                    ; $C5 = player Y   ($1880 = 16-bit Y)
1D16  LDA $7F2000 / ASL A / TAX                ; X = objectCount*2 -- the scan starts at the LAST
1D1C  LDA $0F00,X / BNE ->1D2D                 ;   object and walks DOWN
1D21  DEX / DEX / BNE ->1D1C                   ; X reaching 0 ends the scan (obj0 is never probed)
1D25  LDA #80 / STA $EB / CLC / RTS            ; nothing found -> CARRY CLEAR
1D2D  LDA $1100,X / BMI ->1D21                 ; bit7 -> skip
1D32  CMP #07 / BEQ ->1D48                     ; == 7 -> the arm that does NOT skip the player
1D36  CPX $97 / BEQ ->1D21                     ; the player itself -> skip
1D3A  STX $EB / LDX $DB / JSR ($1D56,X)        ; the per-direction range test
1D43  BCC ->1D21                               ; rejected -> keep scanning
1D45  RTS                                      ; accepted -> CARRY SET, $EB = the target
```

**FIRST match wins, scanning from the highest object index downward** -- not the nearest. Our
`best = min(dx + dy)` is a different selection rule and can pick a different object.

## 3. The four direction handlers -- table C0/1D56 = `5E 1D 84 1D AB 1D D1 1D`

| `$DB` | facing | handler | along-axis sign | along limit | perpendicular limit |
|---|---|---|---|---|---|
| 0 | UP    | C0/1D5E | `dy` must be NEGATIVE (`BPL` rejects) | `< $01C0` | `abs(dx) < $00C0` |
| 2 | DOWN  | C0/1D84 | `dy` must be POSITIVE (`BMI` rejects) | `< $01C0` | `abs(dx) < $00C0` |
| 4 | LEFT  | C0/1DAB | `dx` must be NEGATIVE (`BPL` rejects) | `< $01C0` | `abs(dy) < $00C0` |
| 6 | RIGHT | C0/1DD1 | `dx` must be POSITIVE (`BMI` rejects) | `< $01C0` | `abs(dy) < $00C0` |

Reject falls to the shared `1DA7 SEP #20 / CLC / RTS`; accept ends `SEC / RTS`.

Positions are pixels*16 (`field.c` integrates `*px += vx / 16.0`), so the box is **28 px along the
faced axis and 12 px to either side** -- asymmetric and directional. The absolute value is taken with
`EOR #FFFF` (one's complement), NOT a true negate.

WARNING -- **`SBC` runs with NO preceding `SEC`.** ~~The carry entering each handler is whatever
`1D32 CMP #07` left.~~ **CORRECTED 2026-08-22 while porting it.** On the normal path `1D36 CPX $97`
runs *after* `CMP #07` and is itself a compare, and nothing between it and the handler touches C
(`STX` sets no flags, `LDX` sets only N/Z, `JSR` none). So the carry is **C = (X >= $97)**, and since
1D36's own `BEQ` already removed equality, **C = (obj > playerObj)** -- exactly the rule `C0/5BD7`'s
`CPX $6D` gives the NPC-solid box `C0/5BFA`, which this repo had already ported with that reading.
Only the `$1100,X == 7` arm at `1D48` really inherits `CMP #07`'s equality, i.e. C = 1. An object
with a LOWER index than the player measures one subpixel further away than the same object with a
higher one. The SECOND `SBC` always runs with C = 0 (the last flag-setter before it is the primary
axis's `CMP #01C0` with its BCS not taken), so the cross-axis delta is always -1.

## 4. The invoke -- C0/5AC5 (`dis.py 5AC5`)

Structurally the twin of the touch invoke C0/3154, with three differences:

```
5AC5  LDX $EB
5AC7  LDA $1C01,X / BNE ->5B1E           ; busy -> refuse
5ACC  LDA $1100,X / BMI ->5B1E
5AD1  LDA $1000,X / BMI ->5B1E
5AD6  LDA $1C00,X / CMP #02 / BCC ->5B1E ; priority must be >= 2    (touch uses #03)
5AE9  ... $4216 = $EB * 128 ... $D9      ; push $1180,X onto the return stack $7F0580,X
5AFA  LDA $EB / ASL x4 / CLC / ADC #0002 ; slot 1                   (touch adds #0004 = slot 2)
5B04  TAX / LDA $7F2001,X / STA $1180,X  ; IP := fn slot 1
5B10  LDA #01 / STA $1C00,X              ; priority := 1            (touch sets 2)
5B15  STZ $1A80,X / STZ $1A01,X / STZ $1001,X
```

The fn-slot table is `$7F2001 + obj*32 + slot*2`, the same addressing the touch invoke uses.

## 5. The port, and what porting it EXPOSED

`src/ct_vm.c`: `CtVmActBoxAt` is the four handlers (one body, `facing` picking the axis and the sign,
`#01C0` / `#00C0`), `CtSceneActionProbe` is the scan. Both are transcriptions, and the box shares its
shape with `CtVmSolidBoxAt` (C0/5BFA) line for line -- signed test, one's-complement absolute value,
`CMP` limit, cross axis with carry 0 -- which is a useful cross-check: two independently disassembled
ROM routines agreeing on an idiom.

| | ROM (and now ours) | the retired invention |
|---|---|---|
| shape | directional box, 28 px along / 12 px across | square-ish `dx <= 10 && dy <= 12` |
| reach | one test, from the player's own position | 1 tile, then 2 "across a blocked tile" |
| pick | FIRST hit scanning objects high -> low | NEAREST by `dx + dy` |
| gates | `$0F00,X != 0`, `$1100,X` bit7, the `== 7` arm, self via `$0197` | `CtSceneActorActivatable` + `i == pObj` |

**Oracle-validated end to end.** Drive `saves/wake.state` to control (`$011F` flips to 1 at f681, the
recipe in `field_action_probe_opens.md`), hold Up, press A:

```
build/ct.exe --from saves/wake.state --btnscript "<50x '0:8,100:6'>,0:20,<40x '10:4'>,0:10,<8x '0:10,100:6'>" \
             --frames 1200 --monitor "7E01EB:1" --diff --writer
  f901   7E01EB=14*(C0:1D4A)
```

`$01EB = 0x14` is object index **10 -- the curtain**, and the writer `C0:1D4A` is inside the
`$1100 == 7` arm, the same arm the port takes for `cls == 0x07`. The oracle's player is at
`$1802 = 0x18A4` / `$1882 = 0x067F` = **(394.25, 103.94)**, the curtain at `$1814 = 0x1880` /
`$1894 = 0x04FF` = **(392, 79.94)**: dy = 384 < `#01C0`, |dx| = 36 < `#00C0`. Ours returns obj10 from
that same point.

### ⚠ The invention was MASKING a collision defect

`field-scene2-curtain-activate` used to stand the player at **(392,112)** -- **32 px** from the
curtain, i.e. OUTSIDE the ROM's 28 px box. Only the invented extra reach made it pass. And our player
cannot get closer: from (392,112), holding Up for 10, 24, 40 or **60** frames leaves the foot at
exactly **112.0**, so a tile the ROM lets the foot enter reads solid here. The oracle walks to
**103.94** -- about **8 px**, the lower half of tile row 6.

**That is a live, player-visible bug in the collision, not in the probe**, and it is now visible
instead of compensated for. The check was moved to the oracle's own stand-point so that it grades the
PROBE; the collision needs its own derivation (candidates: the foot-Y `-1/16` the slope-solid work
already touches, or a half-tile offset in the player's collision probe point -- `ct-field-collision`,
`ct-slope-solid-z`).

## 6. Open

- The `$34` and `$1F` guards at `1AB4`/`1AC1` are not derived (`$011F` itself is -- see
  `field_action_probe_opens.md`, and `$0134` is tested BEFORE it).
- ~~The `$1100,X == 7` arm: which objects carry 7 is not measured.~~ **MEASURED**: the scene-2 curtain
  does (`$1114 = 0x07`, both sides), and the oracle's own latch goes through `C0:1D4A`, that arm's
  own instruction.
- ~~The `SBC`-without-`SEC` borrow is read off the disassembly, not confirmed.~~ **CORRECTED** in
  section 3 -- it is `1D36 CPX $97`, not `1D32 CMP #07`.
- ~~`LDA $0F00,X` is the scan's liveness gate ... that it is the SAME byte here is untested.~~ Settled
  earlier by `field-scene2-curtain-activate`'s own derivation (the mode-7 latch pass).
- **The scan ORDER is not yet graded.** From the curtain stand-point only one object is inside the
  faced box, so high->low, low->high and nearest-wins all agree. A scene with two in-range candidates
  is what would separate them.
- `$7F2000` (the count the scan starts from) is not read here; the port walks `nobj-1` down to 1,
  which is equivalent because a one-past object has no `$0F00` and `1D1C` skips it either way.
