# The ops the interpreter's inert default used to catch -- 27/28, 43, C0/C2, 50-55 -- and the item's script close

First written 2026-09-18 on a branch (commit a7b7a5d, never merged). Main ported most of the same ROM routines on its own
on 2026-09-30 (d45cff6, disasm/notes/battle_asc_ops_orbit_shake_offsets.md). This note is the reconciliation of the two,
**re-measured against today's code**: for each item, what main does, what the logical oracle does, and what was missing and
is ported now (2026-09-30, the commit that carries this file).

All oracle numbers are the LOGICAL oracle (`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`, CLAUDE.md's rule) unless
marked real time. The item fixtures on the cartridge side are `saves/battle_re/gato_elixir.state` with the item swapped
by `PEEK_POKE` (`2401=C0` Ether, `2401=C6,266E=04` Heal, `2401=C7,2653=00,2654=00,266D=80` Revive; the state's own item
is the Elixir), driven by `--btnscript 100:8,0:373,20:2,0:10,20:2,0:10,100:2,0:14,100:2,0:14,20:2,0:10,100:2,0:540`
(Item, the item, Marle). The port's side is `Belthasar --battletest` on `gato_elixir.raw.wram` / `ether_restore.raw.wram` /
`heal_cure.raw.wram` / `revive.raw.wram` with `CT_BATTLE_SCRIPT=n:40,d:2,n:8,d:2,n:10,a:2,n:10,a:2,n:10,r:2,n:10,a:2,n:200`
(Revive: `n:10,d:2,n:8,d:2,n:10,a:2,n:10,a:2,n:14,r:2,n:10,a:2,n:200`). The two sides start the item on different frames
(oracle `$A013` up f611 for the Elixir / Ether / Revive, f800 for the Heal; port f384, Revive f380), so everything below is
compared by **rel = frame - `$A013` rise**.

| item | ROM | main before | now |
|---|---|---|---|
| ops 27/28, the `$A5CD` write | C1/5806 / C1/5840 | ported (d45cff6), equivalent | unchanged |
| the draw list's read of `$A5CD` | CF/E83A -> CF/E855 | read LIVE: every FE entry one frame early | **ported**: the compose snapshot `frontCmp` |
| op 43 | C1/5A0C | ported (d45cff6), equivalent | a trace line (`STORE-OFFSET`) |
| ops C0/C2, mover mode 5 | C1/6CF3 / C1/6F53, CF/F7FC | ported (d45cff6, with C3 and the combatant arms), equivalent | traces only (mode 5 in ORBTRACE, op/mode in ORBIT) |
| ops 50-55, visit 1 / the record set | C1/5ADB | 50-53 ported; 54/55 inert | 54/55 take the same case |
| the popup animator, `$AC5A` | CF/E781 | not ported: `$AC5A` cleared when no popup was alive | **ported** |
| the second rise curve | CF/E794 -> D1/5E49 | not ported: every number on D1/5E10 | **ported** |
| `$A0DC`'s clear | CC/F097 | missing: the flag stayed set for the whole fight | **ported** |
| the item's close on its op 01 | C1/40CE, C1/4CF7 | ported (d45cff6), equivalent | unchanged |

## 1. Ops 27 / 28 -- the always-front flag (`python tools/dis.py 15806 0x3E 1 1`)

```
C1/5806 (27)  LDA #01 / STA $8E           C1/5840 (28)  STZ $8E / BRA C1/580A
C1/580A       LDA $A010 / CMP #08 / BCS done       ; an effect object: nothing
              CMP #04 / BEQ list                   ; track 4: every $A018 entry until one with bit 7
              TAX / LDA $A014,X / TAX              ; else the track's own combatant
              LDA $A5CD,X / AND #80 / ORA $8E / STA $A5CD,X
C1/583B       LDA #01 / JMP C1/75BB                ; advance 1, no INC $A00F: the frame is spent
```

Main's `case 0x27: case 0x28:` (battleAscCmbs: t >= 8 none, t == 4 the `$A018` list, else `ascRole[t]`) writes the same
byte, keeps bit 7 (the enemy loader's airborne flag, CC/EB86, which C1/28C2 tests), and costs the frame
(battleAscOpFree finds no `INC $A00F` in the handler). Equivalent.

**What was missing: the frame the draw list sees it.** CF/E83A is the list build (CF/E83F) falling into the compose
(CF/E8AF), in the IRQ chain AHEAD of the script (CF/E764 `LDA $A028 / BNE` skips both). CF/E84A takes each body's key from
`$1D17,Y` (= `$1D23 + cmb`), which the movers (CF/EFAF, earlier in the same chain) left THIS frame; CF/E855 `LDA $A5C1,Y /
STA $1D39,Y / BEQ` (= `$A5CD + cmb`) adds the FE entry from the byte the script left LAST frame. Main's field.c
`battleObjOrder` read `$A5CD` live, so its FE entries came and went one frame early while its keys (the live feet) were
right. Oracle, the attract's X-Strike (`saves/attract_demo_battle_f1850.state`, `--monitor 7EA013:1,7EA5CD:11,7E1DA1:12,
7E1D23:11 --diff --writer`, `$A013` up f9):

| event | `$A5CD` (C1:5824) | `$1DA1` (CF:E8A6) | rel | port before (live) | port now (`frontCmp`) |
|---|---|---|---|---|---|
| Frog 01 | f84 | `0E 4C 4E 4D 53` f85 | 75 / 76 | zorder f188 (rel 75) | f189 (rel 76) |
| Frog's first step 90 -> 8E (CF:F07C) | | `0E 4C 4D 4E 53` f86 | 77 | f190 | f190 |
| Crono 01 | f89 | `0C 0E 4C 4D 4E 53` f90 | 80 / 81 | f193 | f194 |
| Crono 00 | f215 | drops `0C` f216 | 206 / 207 | f319 | f320 |
| Frog 00 | f220 | drops `0E` f221 | 211 / 212 | f324 | f325 |

(port cast f113.) The mover's re-sort (Frog's first step) lands on the same rel on both sides with the live feet, so the
port's zorder at frame P is the cartridge's list at the same rel, and the snapshot puts the FE edges there too. The
Cyclone (tech 01, `D0 | 27 | D9 30 | .. | 28 | DA` on track 0; `saves/battle_re/gato_cyclone.state` with the driver of
tools/cyclone_blink_cmp.py, `$A013` up f432): `$A5CD[0] = 01` f468, `$1DA1` leads with `0C` f469 .. f642, `00` f642 ->
`4C 52 4D` f643 -- rel 37 to 210 inclusive; the port (`gato_cyclone.raw.wram`, cast f384): FE from f421, gone at f595 --
rel 37 / 211. At those two edges Crono stands at home (Y 147, in front of GATO's 118 anyway), so the picture cannot tell
the frame; the list can.

Ported: `CtBattleHost.frontCmp[11]`, taken in battleTickBody beside `objPalCmp` under the same `$A028` gate, seeded from
the loaders' `$A5CD` at CtBattleBegin; field.c battleObjOrder reads it on a live host (`mvInited`), the seed's byte
otherwise.

## 2. Op 43 -- store a point at an offset (`python tools/dis.py 15A0C 0x32 1 1`)

`43 tt dx dy`: `$80 = tt`, `$5D9E = dx`, `$5D9F = dy`, `JSR C1/762E` (the resolver: `$A2B0/$A2B2`), then
`$5D9E = $A2B0 + dx`, `$5D9F = $A2B2 + dy` (8-bit ADCs, CLC before each: they wrap), `INC $A00F` (free), advance 4. It is
the point op 1A teleports to and 11/99/9B slide to.

Main's `case 0x43` is this, through battleAscResolve (which stores `$A2B0/$A2B2` as C1/762E does). Only a mode the port's
resolver does not cover leaves the raw dx/dy (C1/762E would have produced a point): named, no fixture reaches one.

Oracle (`--monitor 7E5D9E:2 --writer`): Heal on Marle, C1:5A36 stores `(A8,77) (B0,75) (B6,6F) (B8,67) (B0,69) (AA,6F)` at
rel 55/60/64/68/72/76 -- her foot (B0,6F) plus tracks 8-13's operands -- and each `1A` puts its object there the same frame:
`$A240 = A8 B0 B6 B8 B0 AA`, `$A250 = 77 75 6F 67 69 6F` from rel 76 (f876). The port: the same six stores on the same
rels, the same six positions from f460 (rel 76). Revive's `43 03 00 F4`: `(B0,63)` at rel 24 (f635), its object then
drifts down with two A8s (`$A250` 63 rel 24, 72 rel 56, 7D rel 116); the port's `m4` trace: the same store at rel 24 and
193 / 193 frames of `$A240/$A250` identical to the close wipe. Added: the `STORE-OFFSET` trace line (CT_BATTLE_ASCTRACE).

## 3. Ops C0 / C2 -- the orbits around a resolved point (`python tools/dis.py 16CF3 0x20 1 1`, `16D10 0x250 1 1`)

| op | operands | centre | `$8E` (advance) | `$8F` (mover mode) |
|---|---|---|---|---|
| C0 (C1/6CF3) | `tt a b c` | resolve(tt), C1/6D09 | 5 | 3 (C1/6D10) |
| C1 (C1/6F31) | `a b c` | `$5D9E/$5D9F` | 4 | 3 (C1/6D10) |
| C2 (C1/6F53) | `tt a b c` | resolve(tt), C1/6F69 | 5 | 5 (C1/6F70 `STA $8F`) |
| C3 (C1/6F75) | `a b c` | `$5D9E/$5D9F` | 4 | 5 |

Main's `case 0xC0 .. 0xC3` and battleEffectMoverFrame's mode 5 (CF/F7FC: the depth write `$A3DF[(slot * 11 + $AB4F) & FF]`
by the angle BEFORE the step, `| 80` in front below 80, `| 40` behind; the sine term's radius halved) are the ROM's; main
also has the combatant arms (C1/6D25, track 4 C1/6DC7, modes 3/5 in CF/F1F8 / CF/F354), which a7b7a5d had left named.
Graded on the Elixir (`--monitor 7EA240:8,7EA250:8,7EAAA5:4,7EAAC3:8,7EA878:4,7EA3F6:1,7EA401:1,7EA1E0:4`): tracks 8/9
`C0 0C 1C 60 02` / `C0 0C 9C 20 02` arm at rel 26 (`$A878 = 03 03`, `$AAA5 = 5C 24`, `$AAC3 = 0020`), tracks 10/11
`C2 03 9C 60 02` / `C2 03 1C 20 02` at rel 29 (`$A878 = 05 05`), the releases at rel 58 / 61; slot 3's `$A3DF[0x22]` 83 ->
43 at rel 55 and slot 2's `$A3DF[0x17]` 82 -> 42 at rel 56. The port's ORBTRACE against the monitor: 35 / 35 orbit frames
with `$A240`, `$A250` and `$AAA5` identical; the arms, releases and depth flips on the same rels. Equivalent; only the
traces were extended (CT_BATTLE_ORBTRACE now also prints while a mode-5 slot alone is live; the ASCTRACE `ORBIT` line
names the op and the mode it armed).

## 4. Ops 50-55 -- ShowDamage, and the popup animator behind it (`python tools/dis.py 15ADB 0x216 1 0`, `0FE781 0x56 1 0`)

The jump table C1/7A6B sends 50, 51, 52, 53, 54 and 55 to C1/5ADB. The handler is a three-visit machine on `$A3D1`:

- visit 1 (C1/5AE0): `INC $A3D1`, `$922D..$92DC := F0` and `$92DD..$92E7 := 0` (the digit OAM rows hidden), `STZ
  $A3B9-$A3BC` (the palette cycles stop), `$2300..$2307 := $999F..`, `$2380..$2387 := $99A7..` (the two digit palettes the
  boot staged from D1/4D80 at CC/EF07); advance 0. **Main: ported** (and the boot staging).
- visit 2 (C1/5B2D): `STZ $A3D2`, `$A3D3 = $9895 & 7` -- the op's low bits pick the record set, `$AD9C + CC/F829[k]`
  (`00 2C 58 84 B0 DC`) `+ 4 * cmb` -- then per combatant with a kind: kind 5 the MISS glyph, else the digits, attribute
  0x39 for kinds 1/2; **every kind but 1, 3 and 5 sets `$A0DC,cmb`** (C1/5BA1); `$AC5A := 1` (C1/5CD7); advance 0.
  **Main: ported** (battleSpawnActionPopups; the record sets are battleShowRecordPut's). Sets 4/5 lie past the 0xB0 bytes
  FD/ACEE clears and no script the census walks carries a 54/55; the port draws nothing for them (named).
- visit 3 (C1/5CE0): `LDA $AC5A / BEQ` -- set: advance 0; clear: `STZ $A3D1`, advance 1. **Main read "no popup alive" as
  `$AC5A`** -- the same frame while the pool's numbers live exactly the animator's 57 frames, and an early release whenever
  a set drew nothing (the ROM holds the track to the terminator either way). **Now `$AC5A` alone.**

**The popup animator CF/E781** -- `JSR` from CF/E759, inside CF/E74B `LDA $A013 / BEQ`, i.e. every frame an action is up,
in the IRQ chain ahead of the script:

```
CF/E781  LDA $AC5A / BEQ rts
         LDX $A3D2 / LDA D1/5E10,X / CMP #80 / BEQ -> CF/E7D3 STZ $AC5A
CF/E792  for Y = 0..10: $A0E7,Y := ($A0DC,Y != 0) ? D1/5E49,X : D1/5E10,X
CF/E7AF  INC $A3D2
CF/E7B2  for Y = 0..10, k = 0..3: $922E + 16*Y + 4*k += $A0E7,Y        ; every row's four OAM Y bytes
```

`D1/5E10 = F8 F8 FA FD FE FF FF FF FF`, sixteen `01`, thirty-one `00`, `80`: net -15 -- the port's baked rise table
(`ctBattleFxRiseDY`, 7 -1 -7 .. 0) is exactly `15 + ` its running sum. `D1/5E49` = thirty-three `FF`, then `00`: net -33,
the number rests **18 px higher**. `$A0DC` is cleared only by CC/F06B's `STZ $A0DC,X` (CC/F097) -- the action's open AND
close wipe -- and the boot's CF/FB92.

Oracle, the Elixir on Marle (`--monitor 7EA013:1,7EA3D1:3,7EAC5A:1,7EA0DC:2,7E923D:8,7EA0E8:1,7EA3D3:1 --writer`, rel from
f611): the HP "65" (set 0, kind 1) -- `$A3D1 = 01` rel 74 (C1:5AE3), `$AC5A = 01` rel 75 (C1:5CDC), `$A0E8` F8 F8 FA FD FE
FF .. 01 .. 00 (CF:E7A0), `$A3D2` 01 .. 38, `$AC5A = 00` rel 132 (CF:E7D6, `$A3D1 = 00` C1:5CEC); the MP "12" (op 51, set
1, kind 2) -- `$A3D3 = 01` and `$A0DC[1] = 01` rel 134 (C1:5B3B / C1:5BA6), `$A0E8 = FF` (CF:E7A9) for 33 frames (row 1's
hidden entries F0 -> EF .. CF), `$AC5A = 00` rel 191, `$A0DC = 00` rel 197 (CC:F09A, the close wipe), `$A013 = 00` rel 200.
The Ether (set 0, kind 2): `$A0DC[1] = 01` rel 75, the "10" born at OAM Y 0x6F, 0x6E the next frame .. 0x4E at rel 108 and
held, `$A0DC = 00` rel 138, `$A013 = 00` rel 141.

Ported: battlePopupAnimator (CF/E781 whole: `$AC5A`'s terminator, `$A0E7`, `$A3D2`, the `$922E` rows) in
battleAdvanceAction gated on `$A013`; the pool's numbers take the same steps (battlePopupRise: per frame, D1/5E49 where
the row's `$A0DC` is set, else D1/5E10, from the birth offset 15; field.c draws `restY + rise`); CtBattlePcLadderWipe
clears `$A0DC` (CC/F097). The port, same fixtures: every rel above identical (`$AC5A` 75 / 132 / 134 / 191, `$A0DC` 134 /
197, Ether 75 / 138); the MP "12" and the Ether's "10" are drawn at y 110 on their first frame, one pixel higher a frame,
78 (= 0x4E) from age 32 -- the HP "65" still rests at 96 (0x60).

## 5. The item's close is its script's (C1/40CE)

C1/40CE `LDA $9876 / BEQ ->40B0` is the action loop's only exit, written by the script's op 01 (C1/4CF7 -> C1/4D00 `INC
$9876`), which first waits while a ShowDamage is up (`LDA $A3D1 / BNE` -> advance 0). Main runs the item's script arm
beside the tech arm, before the close check, with `actScriptEnd` (d45cff6), and the op 01 wait (battleAscTick's
`end-action waits`). Measured with tools/battle_ab.py (GATO's fight from gato_wait.state, both sides; the cartridge under
the rule), 2026-09-30, cartridge / port span: Tonic on Crono 165 / 165, Tonic on Marle 141 / 141, Ether 141 / 141, Elixir
200 / 200, Heal 124 / 124, Revive on a dead Marle 217 / 217, and the effect bytes (HP, MP, status) SAME on every one.
Equivalent; nothing to port.

## 6. What grades it (tools/regress_checks.json; every mutation killed)

| check | grades | mutations |
|---|---|---|
| `field-battle-item-elixir-script-vs-oracle` | C0/C2 arms and releases, the orbit arrays at rel 30, mode 5's `$A3DF` flips, visit 1's `$2300/$2380` copy, the animator's `$AC5A` / `$A3D2` / `$A0E8`, set 1 and `$A0DC`'s set and clear, the MP number's rest | C0 inert; C2 as mode 3; no flip; full vertical radius; no D1/5E49; no `$A0DC` store; no `$A0DC` clear; no animator; the terminator one step early; the set ignored; no palette copy; no `$999F` staging |
| `field-battle-item-ether-mp-row-vs-oracle` | the Ether's kind-2 number on D1/5E49, `$A0DC` set at the spawn and cleared at the close | no D1/5E49; no `$A0DC` clear |
| `field-battle-item-heal-op43-vs-oracle` | the six op-43 stores and the six objects' positions after them | 43 inert; the offset dropped |
| `field-battle-item-revive-op43-vs-oracle` | the store and the object's drift after it | 43 inert |
| `field-battle-tech-cyclone-front-entry-vs-oracle` | the FE entry's first and last frame | 27/28 inert; 28 never clears; `$A5CD` read live |
| `attract-0ad-battle-turn-machinery-vs-oracle` (+ ZTRACE) | the X-Strike's four FE-entry edges | `$A5CD` read live (and its two mutations whose text had drifted from battleTimerPass / battleCursorPass, re-anchored to today's lines: 6 / 6 killed) |

## 7. Not ported, named

- ShowDamage sets 4/5 (ops 54/55): the machine runs (the case), nothing is drawn; no script the census walks carries one,
  so what the ROM would read past FD/ACEE's cleared bytes is not graded.
- C1/5B49's digit OAM bytes themselves (`$922D` rows: X, tile, attribute): the port draws its numbers from its pool; the
  animator's adds land on the rows visit 1 hid, as on the cartridge, but the digits visit 2 writes there are not written.
- Visit 1's palette-cycle stop (C1/5AF7) is ported but not graded: on these fixtures the cycles are already off.
- An op 43 / C0 / C2 whose `tt` the port's resolver does not cover keeps the raw operands (C1/762E has a case for every
  mode): no fixture reaches one.
