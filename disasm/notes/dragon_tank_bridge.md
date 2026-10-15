# The Dragon Tank on the bridge (loc 01C) -- a review against the rule's cartridge (2026-10-15)

User reports, 2026-10-14 (translated from Portuguese): "the tank's image got broken" (build/ct_debug_049.state, loc 01C,
storyline 2E, after the fight) and "the battle against the dragon tank had several bugs ... including the scene after
defeating it". Check: field-dragon-tank-bridge-vs-rule-cartridge (tools/dragon_tank_cmp.py).

## The seed and the drive

No oracle state reached the bridge, so one was made: saves/downloaded/ctjackie/states/ch05.state (the prison cell,
storyline 2D) with the story page of the user's run (storyline 2E) and the party Crono + Lucca ($2980..), the two flags
of the bridge's script cleared -- `$7F0198 bit 2` (obj1 fn1's first walk-on: Lucca's "What was that?") and `$7F0199
bit 2` (obj5 fn4: the tank's arrival, set at its 02C8) -- then `ct.exe --teleport 28 44 9 2 --method jmp --frames 60`
(saves/field_re/tank/tele01c.state; its WRAM is the port's --loadwram seed).

Scene 28's conditionals read `$7F0100+idx` when the mode byte has bit 7 (`16 99 04 86` = `$7F0199 & 04`, C0/63E6's
`ORA #0100`); `18 2E` runs the next op while the storyline is BELOW 2E. With the user's post-fight page the tank is never
spawned again (obj13 fn0 kills it on `$7F0199 & 04`).

Drive: Left to the first trigger (f321), A every 16 frames through the box (obj1 moves to tile 13), Left into the second
(f1209: obj5 fn4, the tank rolls from tile 4 to 10, `D8 87 C0`), A every 16 frames (Attack, the default target). The
port takes each button one frame later: its field answers a press one frame sooner than the console (field.c
fieldPadNmi's PHASE note), which this lines up; the fight's pad (g_padAutoRead) is then one frame behind.

## What was wrong, and the ROM's answer

1. **The tank's body was invisible on the field.** obj13 is sprite 403 (header size 0x23: class 3, 24 pieces). Op 0x83
   on a class-3 sprite runs C0/E12A (C0/47DC `LDA $1201,X / AND #03 / CMP #03` -> 47E9), which stages the assembly's
   FIRST frame -- `$0F01,X = 0`, the 0x60 chips from `[$1380,X]` at Y = 0 into the $7F:3800 slot and VRAM, the live OAM
   records from the offsets at Y = $C0 -- and nothing re-stages a class-3 sprite (the drain rejects it). The camera
   latch C0/A88D then writes `$0F01,X = FF` (A902). The cartridge keeps drawing frame 0 (OAM #44-67 = tiles 040-08E);
   the port's frame loop took the FF for "no frame yet" and ran its ticker, which drew frame 1 -- all chips 0000.
   What was left on screen was the head (obj14, sprite 429) and the wheel (obj15, sprite 404). field.c's frame loop.
2. **The body vanished for ~20 frames on every hit.** The hurt script asks for pose 01; C1/1C4A returns at 1C50-1C54 on
   `$967C,X = 03` ahead of the $9907 publish, the upload and the dx,dy template, so a type-3 combatant keeps the pose the
   staging published (CC:E366, `$990A = 00` the whole fight). battleEnemyGenRender drew the script's pose.
3. **Crono attacked the head and the grinder with the near anim.** C1/49FF: `$9930 = 1`; the 40-px test C1/2AE3 clears it
   when far; then, for a MELEE weapon (CC/F63C != 0) or a ranged one the distance kept close, C1/4A71 floods CC/F110's
   grid (the arena $7BFD copied to $7CFD, 0x40 on every combatant with `$96F5,Y`, Y from 3 with `$2989` bit 2) from the
   attacker (0x20) toward the target (0xFF, one row up when they share a cell), waves 1..1E; no touch -> `$92 = 80` ->
   `$9930 = 0` and CD/4000's far anim (Crono 0E). battle.c battleTargetWalkable.
4. **A "Got 5 tech point(s)!" line the cartridge never shows.** FD/AD32 gates the TP line on `$B2AF bit 4`, whose only
   writer is C1/F4A2 inside the tech pass's C1/F45F, behind `CC/1BEB[tech*11] bit 7 -> AND $7F01E0`. TP total 5, `$B2AF
   = C0`, lines: XP, Gold. battleBuildResults now runs the read-only tech pass first and takes its bit.
5. **The first reward line was never drawn**, and every line waited out its dwell. `D8 87`: `$2989` bit 0 is the
   no-celebration fight (C1/35A9); battleResultsTick skips the 64-frame hold, field.c's box gate still waited 72. And the
   dwell's A test (CD/01F2 `LDA $F8 / BPL`) read the host keyboard only: it reads `$CC99` now, the copy CD/0348 makes of
   `$F8` in the same NMI. (GATO's Crono-solo ack moved f2981 -> f2672, the cartridge's f2673.)

After these the fight is the cartridge's hit for hit: the same frames and the same damage to the end, Crono's 0E
attacks and the closing critical (01) included.

## Open, measured and not fixed

* **The battle HUD's window colour.** The strip's chrome is a bake (src/data/battle_hud.h, "field-INDEPENDENT") of window
  colour 0. The ROM loads the window palette only in the field (C0/70E9, `$2991 & 7` -> FF:B210) and the menu (C2/ED15);
  the fight draws with the colours the field left. This save has colour 6: the cartridge's window bodies are near-black
  ((41,41,66) -> (0,0,24) -> (0,0,0) down the box), the port's grey ((74,82,90) -> (41,49,57)).
* **Op E4 redraws the picture at once; the ROM does not.** C0/3D97 is `JSR C0/AF4E` on the WRAM map and nothing else -- no
  tilemap upload. At the battle's return obj0 fn1 runs `E4 07 0C 0B 0D 07 09 41`, which empties BG1 cols 7-11 rows 9-10
  (the railing) in WRAM on both sides; the cartridge keeps showing the railing (VRAM untouched) in front of the wreck's
  lower piece, the port removes it (CtFieldSceneCopyTiles) and the piece shows. A fix needs the picture to follow the
  ROM's uploads (the load, op E5's style branch, the camera streamer), not the WRAM copy -- E4's curtains and levers ride
  on it too.
* **2-3 frames of lag after the second trigger** (Lucca's `$0F0B = 73` at +4 against +1, the tank's first step +70
  against +68) and Lucca 4 px nearer Crono at the stop (the follower ring: 255 against 259).
* **The shakes' residue.** F4 01's auto-scroll (C0/91F8) takes its direction from the play clock `$0400`; where the shake
  stops, the camera keeps a 0/1-pixel phase. With the port's field input one frame early, the clock phase at the shake
  differs and the residue can too: the first run left `$1D96 = 1`, and the D8's alignment (C0/885A) then spent 12 more
  frames on Y. The fight's exit also ends one frame late on the shifted drive (the clock's last tick f7349 / f7348), so
  the third shake (the explosion) leaves the camera a tile off for the rest of the scene. Both are the input phase, not
  a separate defect.
