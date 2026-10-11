# scene 2 — the CAT (obj9) animation, derived from the ROM + oracle

Derived 2026-08-15. Item: "the cat's animation in scene 2 is still not fixed".
**Nothing here is implemented — this is a derivation.** Every claim carries the command that
produced it. Claims I could not measure are marked **NOT DERIVED** in §7.

Probe binary: `build/ct.exe` copied to the scratchpad as `ct_probe.exe`; `build/ct_field.exe` copied
as `ctf_probe.exe` (never probed with `build/` directly, so a concurrent build cannot be blocked).

---

## 1. obj9 IS the cat — confirmed by the OBJ layer, not by assumption

`saves/bed3460.state` is `loc = 2` and renders coherently:

```
$ ct_probe --read "loc,story" --from saves/bed3460.state
  loc                  wram[7E0100+2] = 0200  (u16le=2 / 0x2)
```

```
$ PEEK_ISOBG=4 ct_probe --peek saves/bed3460.state b2obj.raw --btnscript "0:30"
peek: isolated BG5
```

The OBJ-only frame, cropped at screen (100,70)-(200,140), shows an orange **cat** whose pixels sit
inside OAM group idx32-35 — the group whose screen position matches obj9:

```
$ ct_probe --peek saves/bed3460.state b2.raw --btnscript "0:30"
  idx12 raw=(1780,07FF) px=(376,127)          <- obj9
$ python tools/ct.py oam b2.raw.oam
* 32  127   80  180   23   2    1   1   0  0
* 33  111   80  182   23   2    1   1   0  0
* 34  127   96  184   33   3    1   1   0  0
* 35  111   96  186   33   3    1   1   0  0
```

`$ ct_probe --read "7E0A12+2,7E0A92+2,7E1892+2,7E1D0A+1,7E1D0E+1" --from saves/bed3460.state --settle 200`
gives `$0A00,X = 120` / `$0A80,X = 111` (obj9's screen X/Y), `$1880,X = 0x07FF = 127.9375 px`,
camera `$1D0A = 0x20` (=256 px) / `$1D0E = 2` (=16 px). 376-256 = 120 and 127-16 = 111 — the OAM
group and obj9 are the same object.

⚠ **`bedslide.state` has a STALE PPU.** Its WRAM is scene 2, but the framebuffer renders **Leene
Square** at both 8 and 60 frames (`--peek bedslide.state ... --btnscript "0:8"` / `"0:60"`, identical
images). Its WRAM is usable; **any render-based conclusion drawn from it is invalid.**

---

## 2. The cat's script (raw `$7F2000` bytes, not the tool's fn listing)

`python tools/ct.py script 2 --obj 9` lists only fn entries and therefore **hides the autorun
continuation, which is where the whole idle animation lives**. Read from the live image instead
(`b2.raw.wram`, `$7F2000 + off`):

```
0730: 8B 17 07 89 30 A6 02 00 0D 00 AC 15 AD 30 AC 03
0740: B2 00 AB 03 B4 89 1A 96 17 08 96 14 08 92 80 08
0750: 96 14 0B A6 03 A0 17 0E 0A 12 87 08 8B 18 04 8E
```

| addr | bytes | meaning |
|---|---|---|
| 072E | `82 3B` | load sprite 66 (the cat) — same loader byte as the verified scene-1 cat |
| 0730 | `8B 17 07` | SETCOORD tile(23,7) |
| 0733 | `89 30` | speed 0x30 |
| 0735 | `A6 02` | FACE 2 (left) |
| 0737 | `00` | RETURN — **end of fn0** |
| 0738 | `0D 00` | `$1C80,X := 0` (collision gates) |
| **073A** | **`AC 15`** | **SHAPE: hold assembly frame 0x15** |
| 073C | `AD 30` | WAIT 0x30 |
| **073E** | **`AC 03`** | **SHAPE: hold assembly frame 0x03** |
| 0740 | `B2` | park forever |
| 0741 | `00` | RETURN (= fn1/fn2 entry) |
| **0742** | **`AB 03`** | fn3+ entry: ANIME 3 (once) |
| **0744** | **`B4`** | ANIMESET1 → walk loop |
| 0745 | `89 1A` | speed 0x1A |
| 0747.. | `96 17 08 / 96 14 08 / 92 80 08 / 96 14 0B / A6 03 / A0 17 0E / 0A 12` | walk (23,8)→(20,8)→vector→(20,11), face 3, KILL obj9 |

The VM's stored IP = (next op address − 1); measured: `bed3460` obj9 `$1180,X = 0x073B` with the next
op `AD` at 0x073C, `bedslide` `0x073F` with the next op `B2` at 0x0740.

fn3 is entered from **obj1's** wake-up script: a body scan for a call op followed by `12` (= obj9\*2)
finds `040E: 02 12 16` = CALL obj9 fn6 at prio 1 (fn3..15 all alias 0x0742). `saves/slide.state`
catches exactly that: obj9 prio 1, `$1180 = 0x0749`.

---

## 3. The ROM's animation ops (disassembled)

Jump table `C0/5D6E[op*2]`:

```
$ python -c "... rom[0x5D6E+op*2] ..."
op AA -> C0/2E67   op AB -> C0/2E9A   op AC -> C0/2F71
op B2 -> C0/3543   op B4 -> C0/2E8B   op AD -> C0/2F96
```

```
$ python tools/dis.py 2E67 0x40
  2E67: INY / TYX / LDA $7F2001,X       ; the anim-id operand
  2E6D: LDX $6D / STA $1680,X           ; <- $1680 = anim id
  2E72: LDA #01 / STA $1780,X           ; <- mode 1 = LOOP
  2E77: STZ $1601,X
  2E7A: LDA #00 / STA $7F0B01,X
  2E80: STZ $1681,X                     ; <- step cursor := 0
  2E83: TYX / INX / SEC / RTS
  2E8B: LDA #01 / BRA ->2E6D            ; <- op 0xB4 = "$1680 := 1" (the WALK set), mode 1
```

```
$ python tools/dis.py 2F71 0x28
  2F71: TYX / INX / LDA $7F2001,X
  2F77: LDX $6D / STA $1301,X           ; <- the HELD assembly frame
  2F7C: STZ $1681,X / STZ $1601,X
  2F82: LDA $1780,X / BNE ->2F8C / LDA #FF / STA $1680,X
  2F8C: LDA #03 / STA $1780,X           ; <- mode 3 = STATIC POSE
  2F91: TYX / INX / INX / CLC / RTS
```

```
$ python tools/dis.py 3543 0x08
  3543: TYX / CLC / RTS                 ; op 0xB2 = park on the op forever (X = Y, IP unchanged)
```

Frame resolution `C0/CB3A` (`python tools/dis.py CB3A 90` + `dis.py CB8D 80`): the record base comes
from `$1600,X` (facing) via `$1480/$1500`; the anim slot is `$1781,X` when `$1780,X == 2` else
`$1680,X`; the step cursor is `$1681,X`; a `$FF` data byte decrements `$7F0B01,X` (`CBA4`).
**Mode 3 never reaches CB3A** — `C0/CBF2` inside `C0/CBDC` short-circuits it: `$1780,X == 3 →
A = $1301,X` (`disasm/notes/anim_drain_cad9.md` §4), and `C0/CC17` commits it into `$0F01,X`.

---

## 4. What the ORACLE actually does — measured, per frame

### 4.1 Idle cat: a HELD POSE, not an animation

```
$ ct_probe --monitor "7E1792:1,7F0B12:2,7E0F12:2,7E1112:1,7E1B12:2,7E1312:2" \
           --from saves/bed3460.state --input "0:200" --diff
  f0      7E1792=03*  7F0B12=0000*  7E0F12=8015*  7E1112=04*  7E1B12=8001*  7E1312=DE15*
  f159    7E1792=03   7F0B12=0000   7E0F12=8003*  7E1112=04   7E1B12=8001   7E1312=DE03*
```

(`X = obj9*2 = 0x12`; `7E1792` = `$1780,X`; `7E0F12:2` = `$0F00,X`,`$0F01,X`; `7E1312:2` =
`$1300,X`,`$1301,X`.)

Re-run over **800** frames (`--input "0:800"`): still only two rows. So `$1780,X = 3` for the whole
run and the frame index changes **once**.

Writer PCs:

```
$ ct_probe --monitor "7E1313:1,7E1792:1,7E0F13:1" --from saves/bed3460.state --input "0:200" --diff --writer
  f0      7E1313=15*   7E1792=03*         7E0F13=15*
  f159    7E1313=03*(C0:2F7C)  7E1792=03(C0:2F91)  7E0F13=03*(C0:CC17)
```

`C0/2F7C` = op 0xAC's `STA $1301,X`, `C0/2F91` = its `STA $1780,X`, `C0/CC17` = the drain's commit.
**The idle cat runs no frame animation at all: it holds `$1301` (0x15, then 0x03 after the `AD 30`
wait) and parks on `B2`.**

### 4.2 Walking cat: `AB 03` once, then the `B4` walk loop

```
$ ct_probe --monitor "7E1792:1,7E1692:2,7E0F12:2,7E1312:2,7E1112:1,7F0B12:2" \
           --from saves/slide.state --input "0:220" --diff
  f0      7E1792=03*  7E1692=FF00*  7E0F12=8003*  7E1312=DE03*  7E1112=04*  7F0B12=0000*
  f119    7E1792=02*  7E1692=FF01*  7E0F12=8014*  ...           7F0B12=0002*
  f127    7E1792=02   7E1692=FF02*  7E0F12=8012*  ...           7F0B12=0002
  f137    7E1792=02   7E1692=FF02   7E0F12=8012   ...           7F0B12=0001*
  f139    7E1792=01*  7E1692=0101*  7E0F12=8009*  ...           7F0B12=0000*
  f151    7E1792=01   7E1692=0102*  7E0F12=800E*
  f163    7E1792=01   7E1692=0103*  7E0F12=800D*
  f175    7E1792=01   7E1692=0100*  7E0F12=800C*
  f187    7E1792=01   7E1692=0101*  7E0F12=800D*
  f199    7E1792=01   7E1692=0102*  7E0F12=800E*
  f211    7E1792=01   7E1692=0103*  7E0F12=800D*
```

(`7E1692:2` = `$1680,X`,`$1681,X`.) So the canonical walk is: **`$1680 = 1`, `$1780 = 1`, step cursor
0→1→2→3→0 every 12 frames, committed frames (facing 2 = left) `0x0C, 0x0D, 0x0E, 0x0D`.** The single
`0x09` at f139 is facing 1 (the trk below shows `fa=1` at f139, `fa=2` from f143).

---

## 5. What OUR engine does

### 5.1 The VM is right — the IP tracks the oracle byte-for-byte through the whole walk

```
$ VMDIFF_ANIM=1 VMDIFF_ANIMDBG=9 VMDIFF_TRACK=9 ct_probe --vmdiff saves/slide.state --withpos --frames 230
  [trk f100 obj9] ours ip=FFFF(op 00) pr7 ... | oracle ip=073F(op B2) pr7 ...
  [trk f117 obj9] ours ip=0741(op AB) pr1 ... | oracle ip=0741(op AB) pr1 ...
  [trk f139 obj9] ours ip=0746(op 96) pr1 ... | oracle ip=0746(op 96) pr1 ...
  [trk f143 obj9] ours ip=0749(op 96) pr1 ... | oracle ip=0749(op 96) pr1 ...
  [trk f207 obj9] ours ip=074C(op 92) pr1 ... | oracle ip=074C(op 92) pr1 ...
  [trk f215 obj9] ours ip=074F(op 96) pr1 ... | oracle ip=074F(op 96) pr1 ...
```

⚠ **Two reporting traps in that line, both of which look like defects and are not:**
- `ours ip=FFFF` for a prio-7 object is a *printing* artifact: `CtSceneObjIp` (src/ct_vm.c:3265)
  returns −1 whenever `s->prio >= 7`. It does **not** mean our object has no script.
- `ours pos` is always a multiple of 16 because the trk prints `actors[].x * 16` (integer pixels)
  while the oracle prints raw `$1800/$1880` sub-pixels. The 4/8/11/15-unit "differences" are the
  dropped fraction, not divergence.

### 5.2 The anim TABLE and its timing match the ROM exactly

```
$ python -c "import sys;sys.path.insert(0,'assets');import sprites; rom=open('Chrono_Trigger_USA_SNES.sfc','rb').read()
             for a in (0,1,3): print('anim',a,sprites.build_anim(rom,66,a))"
anim 0 [([0, 1, 2, 3], 5)]
anim 1 [([4, 8, 12, 15], 12), ([5, 9, 13, 16], 12), ([6, 10, 14, 17], 12), ([7, 11, 13, 16], 12)]
anim 3 [([13, 13, 13, 16], 8), ([20, 20, 20, 21], 8), ([18, 18, 18, 19], 10)]
```

Entries are `[U,D,L,R], delay`. Facing 2 (L) of anim 1 = **12, 13, 14, 13 = 0x0C, 0x0D, 0x0E, 0x0D at
12 frames each** — identical to the oracle's measured `$0F01` sequence and 12-frame period in §4.2.
Anim 3 facing 2 = 13, **20 (0x14)**, **18 (0x12)** with delays 8/8/10 — identical to the oracle's
`$0F01 = 0x14` at f119 and `0x12` at f127 (8 frames apart) and to `$7F0B01` falling 2→1 ten frames
after f127.

### 5.3 A COLD load of scene 2 runs the idle sequence correctly

```
$ CT_VMTRACE=9 ct_probe --vm ct_assets.dat --scene 2 --frames 250
 t1  addr=04B7 prio=7 op=0D     ($7F2000 0x0738 -- the autorun continuation)
   [vm obj9 ...] @04B7 op=0D (00 AC 15)
 t5  addr=04BB prio=7 op=AD
 t197 addr=04BF prio=7 op=B2
```

```
$ CT_POSALL=1 ctf_probe --assets ct_assets.dat --scene 2 --frames 240 --selftest our.raw
POS f0   obj9 at(376,127) ... anim=0/0
POS f3   obj9 at(376,127) ... anim=3/-1
```

(`anim=%d/%d` is `animMode/animIndex`.) So the live engine reaches mode 3 by f3, holds `AC 15` for
192 frames (48 wait units × cadence 4) and switches to `AC 03` at t197 — the same shape as the ROM.

---

## 6. THE TWO MEASURED DEFECTS

### 6.1 DEFECT A — `CtSceneSeedFromWram` restores anim mode **2 only**; modes 1 and 3 are dropped

`src/ct_vm.c:3080`:

```c
uint8_t am = w[0x1780 + x], aid = w[0x1781 + x], b01 = w[0x10B01 + x];
if (am == 2 && b01 >= 1 && s->addr >= 0 && s->addr < sc->blen) { ... }
```

Nothing else in the seed touches `animMode` / `animIndex` / `animStatic`, so `$1780,X == 3` (held
pose from `$1301,X`) and `$1780,X == 1` (loop from `$1680,X`/`$1681,X`) both come back as the
struct default **animMode 0, animIndex 0** — the engine's own idle/walk selector.

Measured:

```
$ VMDIFF_ANIM=1 VMDIFF_ANIMDBG=9 ct_probe --vmdiff saves/bed3460.state --frames 200
[andbg f1   obj9] mode=0 idx=0 il=0 loops=0 gen=0 sp=66 ... 
[andbg f159 obj9] mode=3 idx=-1 il=0 loops=0 gen=1 sp=66 ...
```
oracle over the same 200 frames (§4.1): `$1780,X = 3` from f0, `$1301,X = 0x15`.
**→ frames 1..158 our cat is in mode 0 playing sprite-66 anim 0 (assembly frame 2 for facing L,
delay 5) where the ROM holds assembly frame 0x15.** Ours only becomes mode 3 at f159, and only
because the script's own `AC 03` happens to run there.

```
$ VMDIFF_ANIM=1 VMDIFF_ANIMDBG=9 ct_probe --vmdiff saves/slide.state --frames 230
[andbg f1   obj9] mode=0 idx=0 ...           oracle: $1780=3, $1301=0x03
[andbg f119 obj9] mode=2 idx=0 il=3 loops=2  oracle: $1780=2  (op AB)   -- agrees
[andbg f139 obj9] mode=1 idx=1 il=3 loops=2  oracle: $1780=1 $1680=01   -- agrees
```

So the divergence is **exclusively the seeded initial state**, and it lasts until the object's own
script next executes an anim op (118 frames here, 158 on `bed3460`). This is a real engine defect,
not a harness-only one: `CtSceneSeedFromWram` is the entry point for `--vmdiff`, for `ct_field
--loadwram`, and for every WRAM-seeded fixture.

The ROM equivalents to seed are already in the WRAM the function is handed:
`$1780,X` mode, `$1680,X` anim id (`0xFF` = "no anim", written by `C0/2F87`), `$1681,X` step cursor,
`$1301,X` held frame.

### 6.2 DEFECT B — our field sprite draws exactly **one scanline low** (attribution NOT DERIVED)

Same map position, same camera, same held pose (0x03) on both sides.

Framebuffer geometry, from the oracle's own source (`snes/ppu.c:1099-1106`):
`ppu_putPixels` writes SNES visible line `y` to buffer rows `y*2 + 16` / `+17`, so after
`tools/ct.py fb2png`'s 2× downsample **`png_row = visible_line + 8`**. And `ppu_runLine`
(`snes/ppu.c:269-273`) evaluates sprites with `line - 1`, so a piece at OAM `Y` first draws on
visible line `Y + 1`.

```
ORACLE  (PEEK_ISOBG=4, saves/bed3460.state, --btnscript "0:200", cat pixels x<=120):
        bbox x 112..120, visible lines 95..110, n = 80
OURS    (ctf_probe --assets ct_assets.dat --scene 2 --at 312 176 --frames 240 --selftest,
         obj9 isolated by diffing against the same run with CT_HIDEOBJ=9):
        bbox x 112..120, lines 96..111, n = 80
common 68 / oracle-only 12 / ours-only 12
per-row counts:  oracle y95=2 96=3 97=4 ... 106=9 107=8 108=7 109=6 110=5
                 ours    y96=2 97=3 98=4 ... 107=9 108=8 109=7 110=6 111=5
```

The row histograms match **exactly** under a one-row shift, i.e. the assembly frame, the palette and
the horizontal placement are right and the whole sprite is one scanline lower than the ROM's. The
oracle's own numbers say where the ROM puts it: `$0A80,X = 111` (screen Y) → bottom pieces at OAM
Y = 95 → first drawn line 96, so the ROM's 32×32 box is lines 80..111 and the cat's art is 95..110.
Ours is 81..112 / 96..111.

⚠ **I could not determine whether this is cat-specific or a global field-sprite offset**, and I am
not going to guess (see §7).

---

## 7. NOT DERIVED — stated explicitly

- **Whether defect B is global.** The two other candidates in scene 2 both failed as controls:
  obj8 (Mom) loads a *different* sprite id on a cold load than in `bed3460` (the `16 6E / 16 5C`
  flag chain at 0x0282-0x029D), so its art extent differs (ours x 131..142, oracle x reaching 152);
  and our engine draws **no sprite pixels at all** for obj11 (`CT_HIDEOBJ=11` changes 0 pixels)
  because its `8E 08` puts it in field.c's op-0x8E prio-0/1 STENCIL path while the oracle emits a
  real OAM group at screen x 160. A proper control needs a scene-2 state where a second object's
  sprite id **and** position agree on both sides.
- **A pixel-level comparison of the cat's WALK.** I could not drive the live engine into obj9 fn3:
  `--loadwram` refuses both a raw 128 KB dump and the `.state` itself
  (`[loadwram] FAILED to find RAM block in saves/slide.state`), and the plain `--scene 2` run's
  fnv32 is byte-identical with and without the flag (`9522E196`). `CT_ACTIVATE` only reaches fn1.
  The walk is therefore validated at the level of *(our VM ops == oracle ops)* + *(our packed anim
  table == the oracle's measured `$0F01` sequence and period)*, **not** pixel-wise.
- **Whether the user's report is defect A, defect B, or something I did not reach.** Defect A is
  invisible in a normal cold-loaded play session (§5.3 shows the cold path is correct); defect B is
  visible in every frame but is one pixel. I am reporting both as measured rather than picking one.
- `$7F0B00,X` was **not** part of this: it read `00` for obj9 in every sample. The anim-family byte
  that moves is `$7F0B01,X` (the `AB`/`B7` repeat countdown), and it agreed on both sides.
- The trap that cost the most time: `--peek` on `bedslide.state` renders Leene Square (§1). Its WRAM
  is scene 2; its PPU is not. Do not draw render conclusions from it.

---

## 8. Fix spec (mandate-safe, no scene gate) — for the implementer, NOT applied here

**Defect A**, `src/ct_vm.c` `CtSceneSeedFromWram`, the block opening at **line 3080**: the anim
restore is gated `am == 2`. Widen it to dispatch on the same `$1780,X` value the ROM keeps, using
only per-object RAM the function already has in `w[]`:

- `am == 3` → `actors[o].animMode = 3`; `animStatic = w[0x1301 + x]`;
  `animIndex = (w[0x1680 + x] == 0xFF) ? -1 : w[0x1680 + x]`; `animGen++`.
  (`$1301` is what `C0/CBF2` reads and `C0/CC17` commits; `$1680 == 0xFF` is what `C0/2F87` writes.)
- `am == 1` → `animMode = 1`; `animIndex = w[0x1680 + x]`; resume the play cursor from
  `w[0x1681 + x]` (the existing `animSeedFrame` field is the vehicle — it is already read by
  `animTickActor`, src/field.c:1497-1500); `animGen++`.
- `am == 0` → leave the engine's idle/walk selector (already the default).
- `am == 2` → unchanged.

No per-object and no per-scene condition: the branch is on `$1780,X` alone, exactly as
`C0/CB3A`/`C0/CBDC` branch on it.

**Defect B** must NOT be "fixed" by nudging a constant until the render matches — that is the
eyeballing the mandate forbids. Derive the anchor first: the ROM chain is
`$0A80,X` (screen Y, `C0/AB45`) → emitter `C0/B788`/`C0/B8CA` `OAM_Y = template[+4] + $C5` →
LakeSnes `ppu_runLine(line)` evaluating at `line - 1`. Compare that against field.c's
`drawSprite` anchor (+1, commit cf9914f, memory `ct-field-sprite-pos`) on an object whose sprite id
and position agree on both sides, then change whichever end is off by one.
