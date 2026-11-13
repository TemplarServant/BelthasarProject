# A fight's picture: what the NMI uploads against what HDMA reads live

Status: MEASURED on the rule's cartridge (2026-11-13, the forest recipe's twin `saves/battle_re/forest_holdleft_pre.state`,
120 frames left into the Hetakes, L + R held from f350); the port's change is described in sec. 3.

## 1. Two latencies in one picture

Numbering: a `--monitor` row r is WRAM after frame r + 1; `--peek` picture N is the picture drawn during that frame
(so picture N is drawn while row N - 1 is being computed).

| what | the cell (row r) | first picture | latency |
|---|---|---|---|
| a walker's foot (`$1D0F..`, CF:F9CE; the travel CF:F13F) | f849, f850, f853 ... | f851, f852, f855 ... | r + 2 |
| the OBJ palettes' fade page (`$0598` acc -> `$7E:2320..`) | f999, f1001 ... | f1001, f1003 ... | r + 2 |
| the strip's image (a menu pass's post) | the pass's row | two pictures later | r + 2 |
| the BG palettes' fade page (`$05A4` acc) | f1000, f1002 ... | f1002, f1004 ... | r + 2 |
| the layout's other side (`$CA22`, D1/FE4C's install) | f704 (GATO, the Y press) | f706 | r + 2 |
| a pose the main loop sets (a Hetake's cell, the idle cells) | -- | a picture after its foot's | r + 3 |
| the mosaic `$BE9A`, lines below the cut | f999 | f1000 | r + 1 |
| the mosaic `$BE9A`, lines above the cut | f999 | f1001 | r + 2 |

Why. OAM, CGRAM, the strip's tiles and the animated BG tiles are UPLOADED: the NMI at the head of a frame ships the
mirrors the frame before filled (CF/E300-E330 the OAM mirror $00:0700 x $220, CF/E334-E3C0 the strip's posted image,
CF/E3C0 the CGRAM page; the IRQ's line-211 DMA the tiles), so what frame F computes is on the screen in F + 1. A
register HDMA feeds from a WRAM shadow is READ during the frame itself: the service writes the shadow on line 64 (the
line cut), the run of the next table entry below it reads the new value, and the lines from there down show it in the
frame it was computed in. So in one cartridge picture the sprites, the palettes, the strip and the tiles are a frame
OLDER than the mosaic, the bands' contents and the effect registers of the lines below the cut.

And the OAM mirror is not filled at the frame's end. The frame service (CF/E73B-E780, `JSL`'d by the NMI after its
line-64 wait) runs, in this order:

    CF/E748  JSR CF/EFAF        the walk stepper (feet)
    CF/E74B  LDA $A013 / BEQ    in an action: CF/EC78 (the action's walkers), CF/ECC2 (the effect objects),
                                CF/ED18 (the afterimage's record), CF/E781 (the popups)
    CF/E75C  LDA $AA63 / BEQ    JSR CF/F9D3, the part link
    CF/E764  LDA $A028 / BNE    else CF/E778 JSR CF/E83A: THE COMPOSE -- the OAM mirror
    CF/E77B  STZ $9E            (the main loop's wait C1/007E ends: its iteration runs after this)

so the mirror holds the feet the movers of ITS frame wrote and every cell, pose and offset as the main loop of the
frame BEFORE left it: a pose is one picture later than a foot.

## 2. The port before

The port composed a fight's picture at the end of its frame from the live state: the combatants (feet and poses
alike), their fade factors, the strip's image and the room's animated tiles from row P -- its picture P the cartridge's
P + 2 for a foot, P + 3 for a pose -- and the HDMA-fed registers from row P below the cut and P - 1 above it (the
cartridge's P + 1). What it already latched at its NMI head (the message strip's half upload, the CGRAM page, the
effect's OAM cloud, the hand, the bracket, the palette rows `objPalShown`, the dither level) sat with the HDMA class.
So inside one port picture the combatants and the strip stood a frame AHEAD of the mosaic, the message band and the
arena's palette, and the poses two. A single picture always fitted the cartridge at SOME lag, which is why the
single-picture checks never saw it (each takes its best lag); frame by frame at ONE lag:

| | whole frames identical at port -1, before | after |
|---|---|---|
| the forest's fled fight f172-f1140 (969) | 688 | 968 |
| its flee f840-f1140 (301) | 250 | 301 |
| GATO idling f600-f840 (241) | 104 | 241 |

## 3. The port now

`battleOamCompose` (battle.c) copies the host and its WRAM page where the ROM composes: after the tick's service-side
steps -- the action's engines (battleAdvanceAction), the stepper, the part link -- and before its main-loop iteration,
behind `$A028` (h->composeHeld). A frame whose path has no such point (the boot, a blocked exit, a stalled main
thread) composes with the state it leaves. One part of the port's service-side call is the main loop's in the ROM: an
action's script and its pose machine (C1/423A -> C1/42C4, publishing through C1/4307 `JSR C1/1C4A`), which
battleAdvanceAction steps with the engines. The poses it sets (`$9907..`, `ascPmPose`) are put back as the frame
before left them (battleOamPosesBefore / battleOamPosesKeep): the forest's Hetake publishes `$990A` = 03 on row f439
and its new cell is on the cartridge's picture f442. So are, each measured on the forest's command phase (the check
`battle-picture-seq-forest-commands-vs-cartridge`, 486 -> 541 of 541):

* the ground shadow's flag, ops D0 / D1 `$A5D8,X` (h->ascShadow), read by the compose's CF/EA8A: the Hetake's new cell
  on f442 and the shadow under it on f443 (the hop's offset `$A4AF` is the service's own, CF/F158-F18B);
* the damage numbers, spawn and rise: ShowDamage (C1/5B3B-5CD4) spawns them and CF/E781 steps their rows in the
  staging table `$922E..`, not in the mirror -- `$A3D2` 01 on row f520, the number first on picture f522, its bounce
  on f527-f533, gone on f579 (field.c draws them from the copy too);
* the foot a script's teleport writes (ops 19 / 1A / 1B end in C1/561F `STA $1D0C,X / STA $1D23,X`): a slide's steps
  are the service's (CF:F13F / CF:F149) and stay the frame's, the snap to the goal after them is the script's -- the
  Hetake back from its hop, `$1D27` 5E on row f714 (the step) and 5F on row f715 (C1:5639), the mirror's on rows
  f714 and f716: pictures f716 and f718 with a held picture between them (gOamPosScript marks the feet a teleport
  wrote).

The boot is on the same diagonal since 2026-11-13 (the check `battle-picture-seq-forest-boot-vs-cartridge`, 97 -> 127
of 127; the forest's f231-f278 had been a frame or two AHEAD): the entrance ladder's stepper C1/369A is the main
thread's too (C1/3686's loop waits a frame and calls it; it publishes through C1/1C66 -- `$9907` 30 on row f244,
Crono's ready cell on picture f247), so its poses, and the ladder's own copy of them, are put back like a script's;
the band's seed stands a whole frame and its settled tables are the V-IRQ's install (battle_hud_band_anim.md
sec. 9).

`battleOamUpload`, at the next NMI head beside the other shown-latches,
hands the copy over (its feet and its fade factors `palK` are the frame's own: the port's compose point is past its
movers and its objslot service, as the ROM's is past CF/EFAF). `CtBattleOamShown` gives the picture that copy, with
the host's own "as uploaded" members -- the palette rows, the dither level, the effect objects, the hand, the bracket,
the cloud, CGRAM, the strips -- taken from the live host, where this frame's NMI head has already put them. The
afterimage's entries stay the copy's: the trail is drawn with the body it trails. The single-victim view of a
script's white flash (flashTarget / fxFlashPal) is live with its mask while a script owns the flash: both project
the uploaded `$993C` (objPalShown), and the copy's pair kept the victim white a picture after the mask went (the
forest: `$993C` 08 on row f457, 0A on row f473; white on the cartridge's f460-f475).

field.c draws from it:

* the combatants (battleCombatantsRender: cells, poses, positions, fade);
* the strip's image as the frame before left it (`g_bStripShown`, copied before the tick on CD/088D's gates:
  `CtBattleNmiUploads` -- not the boot's NMI, not with `$CE0F`, not under the pause);
* the room's animated tiles a frame after their step (`g_bTileStepPend`: the arena is re-baked with a fight's tile
  step on the next frame);
* a layout's other side a frame after its install (battle.c `CtBattlePictureTables`: the nine tables and the `$CA22`
  that D1/FE4C's `$CA21 != $CA22` branch replaced are swapped into the page, and into that frame's line-cut copies,
  around the compose of the install's own frame -- battle_hud_band_anim.md sec. 5's law, "installed on N + 1, on
  screen at N + 2").

The mosaic's rows, the effect layer's tables, the bands and everything the live host latches at its NMI head are read
as before. The headless battle shots (CT_BATTLESHOT, the frame canaries) compose as before: they are not pictures of a
frame the cartridge shows.

A fight's picture is now ONE diagonal: port P = the cartridge's P + 1 (the field's stays P + 2). Tools that graded a
fight's uploads at P + 2 moved: tools/battle_strip_upload_cmp.py (every thumbnail identical again, at P + 1), the
attract's pairs (07D + 2067 -- its RAM runs at + 2068, measured on `$9931` / `$A013` / `$993A` under the rule -- 0AD and
087 + 2066; the Ice Sword's afterimage frames are identical WHOLE on 087's diagonal now).

What got better beside the table above: the Y press (tools/battle_window_side_cmp.py) 24 -> 0 of 140 thumbnails
differing; GATO's entry 2 -> 1 of 88; the Hypno Wave 147 -> 128 differing frames; GATO's song 583-590 -> 443 px on the
frames where the singer's cell led the spotlight.

## 4. Open

* (Not a difference: the forest's f173, the one frame of f172-f1140 that is not the port's a frame earlier, is the
  FIELD's last picture -- on the field's diagonal, the port's f171, as f172 is its f170. The fight's own pictures
  start on f174, whole from there.)
* The OBJ priority a script's op D9 / DA writes (`$99B5,X`, C1/753A / C1/7579) is the main loop's too: the forest's
  `$99B9` 30 -> 20 by C1:7598 on row f717, the mirror's attribute `$0733` 32 -> 22 on row f718. The port takes it with
  the frame; no measured picture shows the difference yet (nothing over the Hetake there), so it is not changed.
* (Read wrongly before, for the record: "the choosing PC's head under L + R", f520-f545, was the damage number over
  it a frame early; "a cell's tiles", f442, was the ground shadow's flag. Both are sec. 3's now.)
* GATO's fight left alone, f230-f1600 at one lag: 1184 of 1371 (918 of the 1171 to f1400 before the boot's fixes and
  the ladders' gate, field.c battleGatoRender: a scripted action shows the published pose only -- check
  `battle-picture-seq-gato-first-attack-vs-cartridge`). Left: one frame of the song's pre-roll, f1018 (436 px, not
  looked at), and the song's lit window f1049-f1230, 443 px a picture at the cone's top -- lines 0-14 between
  x 150 and 185, where the cartridge's beam reaches the top of the screen and tints the tent's edge and the port's
  does not (f1050, f1058, f1066 and f1082 have 842-884 px, not looked at). The beam is BG3 TILES there, not a window: the
  cartridge's lines 0-160 of a lit frame are one band (`--hdma` at f1093: TM 13, TS 04, CGADSUB 03, BG3's scroll
  (985,123), no window enabled), so the port's BG3 map lacks cells of the rows those lines show (map rows 15-16, about
  columns 13-18). Not derived: who writes them.
* The layers' own scroll under a fight (leg 95's mist) and the room's palette cycles are still the frame's own state.
* The frame after a side's install shows ~80 px on the lines above a top strip (GATO, f706).
* The effect sprites have not been measured against either class with a frame-by-frame tool.
* CD/088D's gates on the OAM upload (`$CE0F`, `$CD02`) are the strip upload's own; no measured frame has them set
  while a sprite moves.
* A re-bake for another reason (a CGRAM change) on the frame of a tile step shows that step a frame early.
