# Op 0xD9: the party walks on the ROM's own programs -- and the queue puts the lead first (2026-09-23)

Exposed by field_op8c_place_from_vars.md section 7: with the 07D's enemies placed right, the port's party walk before the
battle ended 8 frames early. The port drove the three movers straight from the op (the battle_entry_camera.md D.3 stand-in,
with its own per-frame arrival stop). The ROM does not walk the party at all.

## 1. The programs (C0/5CEC)

After the location script is decompressed to `$7F:2000` (C0/56D4), C0/5CEC copies four templates into the same image with MVN:

| source | destination | IP | what |
|---|---|---|---|
| C0:5D37, $20 | $7F3700 | 16FF | the battle-return regroup (ops 0xDA, C0/5B1F) |
| C0:5D50, $20 | $7F3720 / 3740 / 3760 | 171F / 173F / 175F | op 0xD9's walk, one per party slot |
| C0:5D68, $06 | $7F3780 | 177F | (6 bytes) |

The walk: `E3 00 / 89 40 / 0D 00 / AE / 96 x y / A6 f / 71 FE / 12 FE 04 04 04 ... 11 09 / E3 00 / 00` -- control off,
speed $40, no collision, auto-anim, MOVETO (op 0x96, the one every script uses), face, count the arrival in var FE =
`$7F03FC`, wait for 4, RETURN. The largest shipped script is 0x16FA bytes: none reaches $7F3700.

## 2. The op (C0/326C, `python tools/dis.py 0326C 0x230 1 0`)

* FIRST visit (`$7F03FC == 0`, 32C8): `STZ $1F`; for slot 0 (`LDX $97`, 16-bit, no bit-7 test) and slots 1/2 when
  present (`$99`/`$9B` BPL): push `$1180,X` at `$7F0580 + $1C00,X * $80 + X`, `$1180,X := 171F/173F/175F`,
  `$1C00,X := 0`, `STZ $1A80,X / $1A01,X / $1001,X`; write the pair into the program (`$7F3728/29 & 7F`, the sign
  bits -> `$7F372B`). `$7F03FC := 1`, or `3 / 2` when slot 1 / 2 is absent. Park (`LDX $C7 / CLC`).
* LATER visits: park while `$7F03FC < 4`; reset the party ring (327B-3296, as op 0xDA's 347D); park while a present
  slot's `$1C00` is still 0; `$7F03FC := 0`, `INY x7 / TYX / CLC`.

The RETURN at priority 0 is the ordinary C0/5F74: clear the level, climb to the next level with a saved IP, resume.

## 3. The queue (C0/59D9)

`5A01 LDA $1100,X / BEQ ->5A31`: the move-mode-0 object (the lead) is PREPENDED to the run queue (5A3B `STA $80,X`
links it to the old head, 5A3D `STX $0174`); every other object is appended at `$0175` (5A11-5A1B). The runner walks
from the head, so the lead runs FIRST each frame. The port ran the queue in plain index order.

Oracle, saves/gato_t0.state (`--vmdiff`): obj0's `D9` at f41 leaves the lead at IP 171F (the program's first byte)
while the follower obj2 runs its first ops that frame -- the lead had already had its visit.

## 4. The consumer's frames (C0/0097 / C0/1770)

The main loop is `0097 latch / 18D9 / 0C76 (-> 1770) / 881E / ... / 00BF / EC60`. After op D8 (frame N), the consumer
arms at N+1 and runs the align loop there (`885A` / `00BF` / `EC60`); the frame after it is the main loop's own
(`881E .. 00BF`); the bit-0 pass (C0/188C, the staging) is the NEXT frame's first act -- before that frame's legs. Oracle
from gato_t0.state: D8 f73, consumer f74, staging f76 with GATO's pose `$0F11` = 04 (its idle would have stepped to 05 in
f76's legs, which never run). The port staged at the end of its frame N+3, after those legs; now at the end of N+2.

## 5. The seed

A state captured mid-walk carries the programs' destinations only in WRAM, so CtSceneSeedFromWram copies $7F3700-3785
into the VM's script image. saves/attract_demo_battle_pre.lakewram (attract f1735): `$7F03FC = 02`, the lead in its
program's wait loop (IP 172D), the followers on its `96` (1746 / 1766); the walk completes at once as on the cartridge
and the party stages the cartridge's C0:04DE bytes.

## 6. The port (src/ct_vm.c, src/field.c)

* CtSceneCreate: the four templates at raw[0x1700..0x1785]; `rgBase` = IP 16FF, `d9Base` = IP 171F (the regroup used
  to be appended past the script body at an IP of the port's own).
* op 0xD9: the ROM handler above (push `ret[prio]`, repoint, prio 0, `$1180` written at once, the destination/facing
  bytes into `raw`); the D.3 stand-in (d9Active, the per-frame arrival stop, the regroup cancel) is gone.
* PASS 2 of CtSceneTick: the queue order of C0/59D9.
* fieldBattleConsumer: the bit-0 pass on the frame after the align frame.
* CtSceneSeedFromWram: the programs from WRAM.

## 7. Graded

* `vmdiff-07d-field-sorcerer-placement`: first divergence f81 -> f176 (the battle staging).
* `vmdiff-gato-d9-release-vs-oracle`: f41 (HEAD) -> f144 (the battle boot).
* `vmdiff-forest-ambush-d9-vs-oracle`: f247 (HEAD, the release itself) -> f282 (the battle consumer's freeze, section 8).
* `attract-demo-battle-midwalk-seed-stages-vs-oracle`: `$29B0` = the cartridge's, where HEAD had 14 / 1A.
* `field-battletest-gato`: staging 18/18 with the new consumer frame.
* loc 0x82's encounter (party3_field, Left held): control off, D9, the three arrivals, D9 done, D8 and staging all on the
  oracle's frames +1 (the port's tick numbering); the decision box of loc 434 opens and waits on the oracle's f395 / f515;
  the Telepod's `8E 33` lands 73 frames after the arrival, the ROM's 73.
* The attract audit (tools/attract_divergence.py): the 0AD, 112 and 07D legs closer to the cartridge (-27 -> -23,
  -16 -> -15, -28 -> -22), the 087 one frame further (-13 -> -14); every attract check re-pinned at the shifted frames with
  the same content (the 0AD HUD close: the strip's two lines identical, the starfield's phase moved).

## 8. Open

* ~~forest ambush lockstep: obj14's X at f282.~~ CLOSED 2026-09-23: f282 is the harness's reach -- op D8 at f279 on both
  sides, the consumer arms at f280 and the cartridge's objects stand still; `ct.exe` links no field.c. The real host stages
  byte for byte on the cartridge's frame (`field-forest-ambush-stages-vs-oracle`); battle_hud_command_windows.md.
* The `$7F0580` IP stack and `$1C00` are kept in the port's C state (`ret[]`, `prio`) and not mirrored to WRAM.
* The 087 leg (no D9) moved one frame further from the real-time cartridge: the staging's frame (section 4), measured
  against the real-time oracle, whose lag frames the port does not have.
