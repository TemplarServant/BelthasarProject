# The battle HUD strip's upload pipeline: WRAM buffers, posts, the NMI's DMA (2026-10-01)

This note covers how the bottom strip's two tilemap layers get from the menu machinery to VRAM, and on which frame the
picture changes. Read it together with battle_hud_command_windows.md (what the buffers hold) and battle_menu_pass.md
(the 4-frame pass that writes them).

The measurements were made on the rule's oracle (`CT_ORACLE_LOGICAL=1 CT_ORACLE_VIRTUAL_BEAM=1`, plus the post-flip
defaults). The fixture is `saves/battle_re/gato_wait.state` with
`PEEK_POKE=2607=63,2608=00,2609=63,260A=00,2830=08,2837=FF,2980=00,2981=80,2982=80` (Crono alone, all techs, MP 99).

The main drive is `--input 100:8,0:632,20:2,0:10,100:2,0:10,100:2,0:10,100:2,0:200`:

| Input | Frames | Effect |
|---|---|---|
| Down | f640-641 | |
| A | f652-653 | Tech |
| A | f664-665 | Cyclone |
| A | f676-677 | target |

LakeSnes pad bits: B = 0x001, Down = 0x020, A = 0x100.

The instruments were:
- `--lifetrace` for the ordered PC visits and writes in each frame;
- `--monitor --diff --writer --json` over 32 regions covering every buffer below;
- `--peek` `.vram` and `.wram` at single frames, to compare VRAM against WRAM;
- `PEEK_THUMBS_EVERY=1` thumbnails, diffed per row.

The thumbnail is 128x112, so a thumbnail row is two screen lines. The strip is thumbnail rows 80..103 (renderY 160..207).

In every `--lifetrace` frame the battle NMI (D1/E77C) comes first and the main loop after it. A writer PC is the
instruction after the store, as `--writer` reports it.

## 0. Answer in one paragraph

The menu pass (C1/1B67, then the head C1/0C2D, the key C1/1153, the tail C1/17DD) writes the strip's tilemap images in
WRAM and then **posts** them. Nothing in the main loop touches VRAM.

The next frame's NMI DMAs a posted image to VRAM. It copies the buffer **as it is at that NMI**, so the key's and the
tail's writes after the post are included. The picture of that frame shows it.

The head draws the strip from the state **before** the key, so a key's effect normally reaches the screen one whole pass
later:

| Event | Main loop | NMI upload | Thumbnail |
|---|---|---|---|
| Key moves the hand / opens the Tech list | pass N | after pass N+1 (N+5) | N+6 |
| Item list open / item cursor (posted by the key or tail itself) | pass N | N+1 | N+2 |
| Confirm, then the party/enemy band (next pass at f684 after the wipe restarted `$9931`) | f678 | f685 | f686 |

A seat's HP, MP and ATB in the strip are refreshed by one rotating seat slot per pass (`$A6E0`). A change can wait up
to three passes.

## 1. The NMI side: everything that writes VRAM $7800-$7FFF during a fight

### 1.1 The path

The NMI path is D1/E77C, then `JSL CD/0C95`, which jumps through CD/0C83[`$7E`]. In a fight `$7E` = 1 or 2, so it
reaches CD/088D. During the boot `$7E` = 0 and it reaches CD/09F1 (see 1.4).

```
CD/088D  LDA $7ECE0F / BEQ ->08A0        ; $CE0F != 0 (a boot mode; STZ D1/F26E at the battle boot): CGRAM only
         JSL CD/09CE / JSR CD/3EB4 / BRA ->08B8         ;   and the CF/E300 upload is SKIPPED
CD/08A0  LDA $CA1E / BNE / JSR CD/0607   ; the top message box / effect BG3 map (sec. 1.3)
CD/08AC  JSR CD/3EB4                     ; the queued VRAM post $CA26 (sec. 1.3)
CD/08AF  LDA $CD02 / BNE ->08B8          ; $CD02 = the Start pause (D1/E81E-E834): no strip upload while paused
CD/08B4  JSL C1/0009  = JML CF/E300      ; THE STRIP UPLOADS (sec. 1.2), then the rest of the NMI
```

### 1.2 CF/E300 (RTL at CF/E589), in order

| PC | Gate | DMA (channel 7, mode 01 -> $2118) | Clears |
|---|---|---|---|
| CF/E300 | every frame | OAM $0700 x $220 | |
| CF/E334-E37D | `$A0` != 0 (the 7-byte record, slot 0: `$9EEA` DMAP, `$9EF0` BBAD, `$9EF6/$9EFC/$9F02` source, `$9F08/$9F0E` size, `$9F14/$9F1A` VRAM) | **the strip TEXT**, see the post table below | `STZ $A0` CF/E37D (writer CF:E37F) |
| CF/E37F-E3C0 | `$99E2` != 0 | **the strip FRAME**: $00:0B40 x $0180 -> VRAM $7A00 (BG1 rows 16..21) | `STZ $99E2` CF/E3C0 (writer CF:E3C3) |
| CF/E3C3 | every frame | CGRAM $7E:2200 x $200 | |
| CF/E3F8-E47F | `$A013` = 0 and `$E5` != 0 | PC sprite tiles, 4 phases `$A4`: CC/F957/F95F sources, CC/F967 = $0080 $0400 $0680 $0A00 and CC/F96F = $0180 $0700 $0780 $0D00 destinations | |
| CF/E481-E50C | `$A012` = 1 | per-sprite tiles from `$A654/$A66A` to `$A680/$A696` | `STZ $A012` |
| CF/E510-E585 | `$A118` counting down to 0 | CC/F957[0] -> $0080 | |

Only the second and third rows touch the strip's map. The record is written only by three post routines, each with
`TDC / TAX`, so it is always slot 0:

| Post routine | Source | Size | VRAM | `$A0` written | Callers |
|---|---|---|---|---|---|
| CF/FD02 | $00:0CC0 (= $7E:0CC0) | $0180 | $7820 (BG3 rows 1..6) | 80 (CF:FD35); `$9EF6` = C0 (CF:FD13) | C1/0C79 (the head, command row) |
| CF/FD36 | $7E:A6E1 | $0180 | $7820 | 80 (CF:FD69); `$9EF6` = E1 (CF:FD47) | C1/0E99 (the head, Tech list) |
| CF/FD6A | $00:0E80 | $0180 | $7820 | 80 (CF:FD9D); `$9EF6` = 80 (CF:FD7B) | C1/1302 (the key, Item opens), C1/1972 (the tail, item list) |

What the gates mean:
- **The record is one slot, so the last post before the NMI wins.**
  - Measured f666 of the item drive: the head posts $0CC0 (CF:FD13 C0), then the key posts $0E80 (CF:FD7B 80). The NMI
    at f667 uploads $0E80, and thumbnail f668 shows the item list.
- **The DMA reads the buffer at the NMI, not at the post.**
  - Measured f666 of the main drive: the head posted $A6E1 with the list's hand. Then the tail's C1/1987 -> C1/0EBA
    cleared the hand (`$A6E3/$A723` = FF, C1:0EC2/0EC8).
  - peek r666: VRAM still has 2960/2962. peek r667: 29FF. Thumbnail f668: the hand is gone from strip rows 83-89.
- **`$99E2` is a nonzero flag, not a count.** The head and the key both INC it at f666 of the item drive (`$99E2` = 02),
  and the NMI uploads once and stores 0.

Who sets the flags:

| Flag | Site | Routine | Pass step |
|---|---|---|---|
| `$A0` | C1/0C79 `JSL CF/FD02` | head, `$95DB` = 0 | head, every pass |
| `$A0` | C1/0E99 `JSL CF/FD36` | head, `$95DB` = 1 | head, every pass |
| `$A0` | C1/1302 `JSL CF/FD6A` | C1/12ED Item opens | key |
| `$A0` | C1/1972 `JSL CF/FD6A` | C1/18B0 item list, only when the arrows or the hand changed (`$80` != 0) | tail |
| `$99E2` | C1/0C7D INC | head, `$95DB` = 0 | head, every pass |
| `$99E2` | C1/0EB1 INC | head, `$95DB` = 1, only with `$A86A` (after `$0B40` := $D1:5A50, C1/0EA4) | head, once per list open |
| `$99E2` | C1/1315 INC | C1/12ED Item opens (after `$0B40` := $D1:5BD0, C1/1308) | key |
| `$99E2` | C1/15A9 INC | C1/1561 target cancel back into the item list (`$A86B` - 1 = 2, `$0B40` := $D1:5BD0) | key |
| `$99E2` | C1/15C5 INC | **unreachable**: block C1/15B6-15C7 follows `BRA` C1/15B4, and no branch, JMP or JSR in C1 targets it (byte scan). Its "copy loop" `BNE` goes to C1/15E8 after one byte. | -- |

Other clears: CF/FB81 `STZ $A0` (the boot's direct-page clear) and the boot's memory clear $2D00-$ACEB for `$99E2`.

### 1.3 The other VRAM writers at and above $7800 in a fight: none of them is the strip's map ($7800-$7BFF)

- **CD/0607** (`$CA17` set): `JSL D1/E8D9`. It writes 16 rows x $20 bytes at VRAM CD/05FF[`$CA16`] = $7C00, $7C10,
  $7E00 or $7E10. That is the effect BG3 map. When the box is closed or open it clears or writes the top box at $5800
  and $5980 (battle_top_message_box.md).
- **CD/3EB4** (`$CA26`, posted by CD/3ED2): its callers' destinations are
  - CE/F17C: $0080..$0A80;
  - CE/EFF2: $5000..$5600;
  - CD/1308: $0C00..$1600;
  - CD/0FFD: $7E:D000 x $800 -> **$7C00** (the effect map);
  - D1/ED49: `$CD3F` = $2000 (D1/ED02);
  - D1/EDC5: $6800.
- **The boot** (NMI mode 0, CD/09F1 -> CD/0AB4 `JSL C1/000F` = JML CC/E000 while `$CAD3`) runs a step machine on
  `$98CA` (CC/F058 arms it):
  - step 2, CC/E0AF:
    - $00:0CC0 x **$0780** -> $7820 (rows 1..30);
    - D1/5F02 chars;
    - $7E:7FFD x $1C0 -> $7B00;
    - $7E:81BD x $40 -> $7800 (row 0);
  - step 3, CC/E1B0: $00:0B40 x $180 -> $7A00, and $7E:2D00 -> $1800.
- A byte scan of the whole ROM for `LDA #78..7B / STA $2117` found only CC/E0E1, E170, E19F and E1E4, and CF/E3B6. No
  other routine aims VRAM at the strip's map.

So during a fight the strip's map ($7800-$7BFF) changes only in two ways:
- rows 1..6 through the `$A0` record;
- rows 16..21 through `$99E2`.

Rows 0, 7..15 and 22..31 keep the boot's upload.

### 1.4 The first content

At the boot, CC/E4CB-E4FA fill the WRAM images (writers CC:E4D5, E4E6 and E4F6 at f275 of this drive):
- `$A6E1` x $180 := 2900;
- `$0CC0` x $4C0 := 2900;
- `$0B40` := $D1:5800 (the base).

The `$98CA` steps 2 and 3 above copy them to VRAM. The first pass that posts is the first `$993A` consumption (f378 in
battle_menu_pass.md's Lucca drive).

## 2. The main-loop side: every writer of the four images, and its step of the pass

The pass is `JSR C1/1B67 / JSR C1/0C2D / JSR C1/1153 / JSR C1/17DD` at three entries:
- C1/10C6, the idle loop;
- C1/10FA, the action loops;
- C1/10E3, an RTL.

**The caller of C1/10E3, an open item of battle_menu_pass.md, is closed here.** It is CD/3E75: `JSR CD/3E7D` (the CD
frame wait), then `JSL C1/0012`, which is `JMP C1/10E3` in C1's jump table. CD/3E75 has 16 JSR sites in CD.

Measured on the main drive: after the confirm, Cyclone's effect load CD/0015 -> CD/1323 waits through CD/3E75 (returns
CD/1331 and CD/3F6A visited f683-f690). The passes at f684 and f688 come from C1:10E8. From f692 the passes are
C1/10FF again.

The images (each is 6 tilemap rows x 32 words = $180 bytes):

| Image | VRAM rows | Holds |
|---|---|---|
| `$0B40-$0CBF` | BG1 16..21 | the window frames |
| `$0CC0-$0E3F` | BG3 1..6 | the command-level text: names, the party rows, the command windows, the hand |
| `$A6E1-$A860` | BG3 1..6 | the Tech list text. Its characters come from `$99E3`, which C1/0A88 -> C1/0B0B builds from the `$94D0` list |
| `$0E80-$0FFF` | BG3 1..6 | the item list text |

### 2.1 The head C1/0C2D, before the key (it reads the state the previous pass left)

**`$95DB` = 0 (C1/0C49-0C80): the command row.**
- With `$A6DD` != `$A6DF`:
  - `$A6DF` := `$A6DD`;
  - C1/1C3A (`$0B40` := the base $D1:5800, writer C1:1C43);
  - C1/0299, the full rebuild:
    - clear rows 1..6 to 2900 (C1:02A5) and the HP/MP headers C1/02AD (layout `$9F20`);
    - party rows C1/0305..;
    - names C1/04DC-0545 (C1:0509/052C);
    - C1/0560 -> C1/0785, the command windows, and C1/081E -> C1/0929, the frames (C1:0946);
    - C1/08E8 (the stash row) and C1/0872 (the hand).
- Otherwise:
  - C1/05A7 refreshes ONE seat (below);
  - then, with `$A43F` != 0 and `$A6DD` >= 0, C1/0872 redraws the hand:
    - it clears cols 6s..6s+1 of rows 1..6, with s = `$95D5` (C1/087E-089F, C1:0893/0896/0899/089C/089F/08A2);
    - unless `$9609`, it draws 60 61 / 62 63 at CC/FADD[`$95DC`,`$A6D9,$A6DD`] + 12 x `$A6DD` (C1:08D2-08E4);
    - `STZ $A43F` (C1/08E4).
- Then, with `$95D5` < 0, C1/1C3A again.
- **Always** `JSL CF/FD02` (C1/0C79) and `INC $99E2` (C1/0C7D).

**C1/05A7, the one-seat refresh.**
- `$A6E0` := (`$A6E0` + 1) mod 3 (C1:05BF INC, C1:05C9 wrap).
- For that seat, if present (`$96F5,X`):
  - HP digits and MP digits (C1/0628-06D1: `$949D-$949F` to `$0CC0,Y`; attribute 29, or 2D through `$A10F`, which the
    low-HP blink `$A110` C1/0620 feeds);
  - the ATB gauge C1/0700-0785 (C1:0762/0774/0784).
- A whole layout is skipped to C1/06D4 when `$A6DB` >= 0, `$A09A` = 0 and `$9F20` = 1 (not measured here).
- Measured:
  - GATO's hit writes Crono's HP `$5E30` 46 -> 3D (C1:ED1A) at f584, during the action's tail, where no pass runs;
  - `$A6E0` goes 01 at f586, 02 at f590 and 00 at f594;
  - the digits `$0D28` 7A 73 -> 79 74 (C1:0655) at f594;
  - thumbnail f596 changes rows 85-89.
  - Idle: seat 0's cells are rewritten every 12 frames (f529, f541, f553, ... C1:0655/0774).

**`$95DB` = 1 (C1/0C40 -> C1/0EE1, then C1/0C83-0EB7): the Tech list.**
- C1/0EE1 is the grey pass: attributes `$A6E2/$A722` at C1/1016/1023, and the MP costs `$9EE3..`.
- C1/0A88 rebuilds `$99E3` (C1:0C25 at f654, ...).
- C1/0C89 copies its even (character) bytes to `$A6E1`, keeping `$A6E1`'s attribute bytes.
- CC/FA95 x 9 x 6 rows -> `$A70B`, the MP column.
- The MP numbers and costs go to `$A751/$A753/$A75B/$A75D` and the same for seats 1 and 2 (C1/0CC3-0E57).
- C1/0EBA clears the hand cells. Unless `$9609`, the hand is drawn at CC/FAE3[`$95DF,$95D5`] (C1:0E7B-0E8A).
- With `$A0DB` bit 7, the description goes to the top box (CD/0027; a different VRAM, battle_top_message_box.md).
- **Always** `JSL CF/FD36` (C1/0E99).
- With `$A86A` (once per list open):
  - `$0B40` := $D1:5A50 (C1:0EAB);
  - `INC $99E2` (C1/0EB1);
  - `STZ $A86A`.

**`$95DB` = 2:** `JMP C1/103D` (RTS). The head writes nothing and posts nothing.

### 2.2 The key C1/1153 (one key per pass, battle_menu_pass.md sec. 3): what it writes and posts

| Handler | Image writes | Post |
|---|---|---|
| Up / Down C1/1250, C1/1264 | `$95DC,X` +-1; `INC $A43F` (C1/125E, C1/1275) | none: the next head draws the hand |
| Att C1/129C | `INC $A43F` twice (C1/12A2/12A5), `$9609` | none |
| Tech C1/12BC | C1/0A88 -> `$99E3`, C1/0AD3 -> `$1CDB`; `$95DB` := 1 (C1:12E4); `INC $A86A` (C1/12E7) | **none**: the next head posts |
| Item C1/12ED | C1/095D -> `$0E80` rows; `$0B40` := $D1:5BD0 (C1/1308); `$95DB` := 2 (C1:131D) | `JSL CF/FD6A` (C1/1302), `INC $99E2` (C1/1315) |
| Tech list B C1/138F | C1/1C3A (`$0B40` := base); `$95DB` := 0; `$A6DF` := FF | **none**: the frame base waits for the next head |
| Item list B C1/14DB | C1/1C3A; `$95DB` := 0; `$A6DF` := FF; `$9920` := FF | none |
| Item scrolls C1/17A1/17BF/151C/1537, the `$A0D0` redraw C1/154B | C1/095D -> `$0E80`; `$991F/$9920` := FF | the tail posts |
| Target cancel C1/1561 with the stash (`$A86B`) | to the item list: `$0B40` := $D1:5BD0 (C1/1597), `$9920` := FF; to the Tech list: `INC $A86A` (C1/15B1) | item: `INC $99E2` (C1/15A9); tech: the next head |
| Confirm C1/161A | `$95DB` := 0, `$A6DF` := FE, `$A6D9,X` := FF, `$95D5`/`$A6DD` := the next queued seat or FF | none: the next head rebuilds |

### 2.3 The tail C1/17DD, after the key

- `$95D5` < 0: C1/1AFE (OAM only).
- `$9609`: C1/1979, the picker. With `$95DB` = 1 it runs C1/1987 `JSR C1/0EBA`, which clears the list's hand in
  `$A6E1` and does not post. The head's post of the same pass carries it.
- `$95DB` = 1: C1/17FE (OAM arrows).
- `$95DB` = 2: C1/18B0-196E.
  - The arrows: `$0EFC/$0FFC/$0EFD/$0FFD`, from `$95E6` against `$9920`.
  - The hand: C1/192A-1969 at CC/FAE9[`$95E5`].
  - Then `JSL CF/FD6A` (C1/1972) when either changed.
- `$95DB` = 0: C1/1B14, which only clears the latch.

### 2.4 Outside the pass

- C1/1BAA, the dequeue teardown: C1/1C33 -> C1/1C3A rewrites `$0B40` without posting.
- The boot: CC/E4CB-E4FA.
- The return to the field: C0/0905 (battle_hud_command_windows.md sec. 5).
- The byte scan of every abs or abs-indexed store into the four ranges, over banks C1/CC/CD/CF/D1/FD and every long
  store, found nothing else.
  - The hits outside C1/0299-C1/1C49 and CC/E4CB-E4FA are operand bytes of other instructions. The ones checked are
    C1/20A8, 2DEE, 306F, 3293, 3329, 26A7, 3E34, 4012, C95C, 108B and 034D.
  - Bank C0's hits are the field's own object arrays.
- The oracle agrees: `--monitor --writer` over the whole fight (f0-f876) names only these writers for the regions:
  - CC:E4D5, E4E6 and E4F6 at f275;
  - C1:02A5, 0509, 052C, 0655, 0774, 076B, 0893-08E4, 0946, 0C25, 0E83-0EE0, 0EAB and 1C43;
  - C0 writers only before f221, the field's.

## 3. The oracle timeline, f638-f690 (main drive), frame by frame where something happens

| Frame | Main loop (pass) | NMI of the next frame | Picture (thumbnail) |
|---|---|---|---|
| 638, 642, 646, 650, 654 | `$993A` C1:10CE. Head posts `$0CC0` (CF:FD35) and `$99E2` (C1:0C80) | f+1: CF:E37F and CF:E3C3, both DMAs | |
| 642 | head first (hand still on row 0), then key C1:126B `$95DC` := 01, `INC $A43F` | 643 uploads the OLD hand | f644: no strip change |
| 646 | head C1/0872: rows 1-2 cols 0-1 := 2900 (C1:0893/0896), `$0D40` c0-1 := 2960 2961 (C1:08DA), `$0D80` c0-1 := 2962 2963 (C1:08E4) | 647. peek r646: VRAM rows 1-2 hold the hand, 8 bytes differ from `$0CC0`. r647: VRAM = `$0CC0` exactly | **f648**: rows 83-95 |
| 654 | head posts the command row; key C1:12E4 `$95DB` := 01, `$99E3` built (C1:0C25) | 655: command row again | f656: none |
| 658 | head Tech path: `$A6E1` (C1:0E83 ...), `$0B40` := $D1:5A50 (C1:0EAB), `$9EF6` := E1 (CF:FD47), `$A0` (CF:FD69), `$99E2` (C1:0EB4) | 659. peek r658: VRAM text = `$0CC0`, frame differs by 27. r659: text = `$A6E1`, frame = `$0B40` | **f660**: rows 80-102, plus the top box |
| 662 | head re-posts `$A6E1` only | 663: `$A0` only (CF/E3BA not visited) | |
| 666 | head re-posts; key C1:138C `$9609` := 01; tail C1/0EBA clears the hand (C1:0EC2/0EC8) | 667. peek r666: VRAM 2960 2962. r667: 29FF | **f668**: rows 83-89, plus the OBJ cursor |
| 670, 674 | head re-posts `$A6E1` | | |
| 678 | head re-posts `$A6E1`; key confirm: C1:1629 `$9609` 0, C1:16BF `$95DC` 0, C1:1719 `$95DB` 0, C1:1745 `$95D5` FF | 679 uploads `$A6E1`: the list without its hand | the list stays through f685 |
| 680-682 | C1/4058, whose wipe CC/F06B restarts `$9931` (CC:F0E9); waits C1/405F and C1/40A0; C1/40A6 `INC $A013` (f682) | | |
| 684 | CF/E748 raises `$993A` (`$9931` = 04) and CD/3E75 -> C1/10E8 consumes it. Head: `$A6DD` FF != `$A6DF` FE: C1/1C3A (C1:1C43), C1/0299 (C1:02A5/0509/052C), C1/1C3A, `$9EF6` := C0 (CF:FD13), `$A0` (CF:FD35), `$99E2` (C1:0C80) | 685. peek r684: VRAM text = `$A6E1`. r685: text = `$0CC0`, frame = `$0B40` | **f686**: rows 80-102 |

**The rule: a post in main-loop frame P is uploaded by the NMI that opens frame P+1 and is seen in thumbnail P+2.** This
is the frame-alignment P+2 rule, measured here on VRAM directly.

The two other drives:

| Drive | Input | What it shows |
|---|---|---|
| Item | `100:8,0:632,20:2,0:10,20:2,0:10,100:2,0:10,20:2,0:10,8000:2,0:30` | Second Down: `$95DC` := 02 at f654, and the hand moves at thumbnail f660 (the f658 head). A at f664: key C1/12ED at f666 (`$99E2` = 02), NMI f667, **item list at f668**. Down at f676: key C1:1519 `$95E5` := 01 at f678, tail C1/1972 posts, NMI f679, **thumbnail f680**. The item list's idle passes post nothing. |
| Tech cancel | `100:8,0:632,20:2,0:10,100:2,0:10,1:2,0:40` | B at f664-665: key C1/138F at f666 writes the base frame (C1:1C43, not posted) and `$95DB` := 0. NMI f667 uploads only the head's `$A6E1`. Head at f670: rebuild (C1:1C43, C1:0946), both posts, NMI f671, **thumbnail f672**. |

In the item drive's input the `8000` is not B; that part of the drive did nothing.

## 4. Implementation spec for the port

### 4.1 What the port does today

The `$0B40` frame rows:
- battle.c `battleHudChromeFrame` (called at ~18868, at the end of every tick) recomputes `$0B40` from the queue on every
  frame.
- It returns with `$95DB` != 0. The ROM writes `$0B40` only at C1/1C3A, C1/0929, C1/0EA4, C1/1308 and C1/1597, and with
  a list up it holds $D1:5A50 or $D1:5BD0.

The strip renderer, field.c `battleHudRenderStrip` (from 4846, called by `CtBattleHudRender` 5252), reads live WRAM at
render time:
- the chrome: live `$0B40` (~4909). With `$95DB` = 1 or 2 it draws the baked `ctBattleHudBg1SubTech` / `SubItem`
  instead of the ROM's frames.
- the baked text maps `ctBattleHudMapCmd` / `Enemy`;
- the hand: `$95DC` / `$A6DD` / `$9609` / `$95D5`;
- the lists: `$95DB`, `$95DF`, `$95E5`, `$95E6`, the `$1580` records, `CtBattleTechRow` over `$94D0`, `$9EE6/$9EE8/$9EE9`;
- the names: `$ACBC`, `$2C23`;
- the party: HP/MP from the `$5E2D` blocks, the ATB from `$99DD` / `$9F22`;
- the windows: `$A6D9` / `$A6DE` / `$A86B` / `$A62D`;
- the low-HP blink from its own frame counter.

The port keeps none of `$0CC0`, `$A6E1`, `$99E3`, `$0E80`, the record, `$A0` or `$99E2`, and nothing models the NMI's
copy. So every strip change shows at port frame P (thumbnail P+2), while on the cartridge it shows at the upload:

| Change | Delay on the cartridge |
|---|---|
| The hand | +4 |
| The Tech list open | +4 |
| Its hand clear on the picker | +0 |
| The confirm's band | +6 here (the next pass after the action's wipe) |
| The Tech cancel | +4 |
| The item list and cursor | +0 |
| HP, MP and ATB | up to 3 passes (12 frames) |

A snapshot of live state at the NMI is **not** equivalent:
- The head draws the state before the key, so `$95DC`'s change would show a pass early.
- The seat rotation `$A6E0` would show HP and ATB up to 3 passes early.

Shortcuts like that are inventions, and are ruled out.

### 4.2 What the port must keep (WRAM, `h->mem`, at the ROM's write points and in the pass step named in sec. 2)

**The images:**
- `$0B40`: written at C1/1C3A, C1/081E -> C1/0929, C1/0EA4 ($D1:5A50), C1/1308 and C1/1597 ($D1:5BD0), and CC/E4ED at
  the boot.
- `$0CC0`:
  - C1/0299 and its subroutines;
  - C1/05A7 (with `$A6E0`, `$A10F`, `$A110`);
  - C1/0872 (with `$A43F` and every INC site in 2.2).
- `$99E3`: C1/0A88.
- `$A6E1`: C1/0EE1 attributes, C1/0C83-0E8A, C1/0EBA.
- `$0E80`: C1/095D/09B0, C1/18B0-196E (with `$991F/$9920`).
- The boot clears CC/E4CB-E4EB.

**The post state:**
- `$A0`;
- the record's source, only `$9EF6/$9EFC/$9F02` (the size and VRAM never vary);
- `$99E2`, and `$A86A`.

**Where they run:** the port's `battleMenuPass` is already the pass. These writes belong in:
- `battleMenuPassHead`: C1/0C49-0C80 and C1/0C83-0EB7;
- the key handlers, in the rows of 2.2;
- `battleMenuPassTail`: C1/1987 and C1/18B0-1972.

The per-tick `battleHudChromeFrame` call must be removed. The `$0B40` writes move to those sites.

### 4.3 The NMI upload (the port's VRAM image of the strip)

Keep two host-side arrays, `vramText[0x180]` (VRAM $7820-$78DF) and `vramFrame[0x180]` (VRAM $7A00-$7ABF). At the end
of every battle tick (the ROM's CD/08B4 is the first thing to touch them in the next frame's NMI, so before the render is
equivalent), when `$CE0F` == 0 and `$CD02` == 0:

```
if (mem[0xA0])  { src = record ($9EF6/$9EFC/$9F02: $0CC0 | $A6E1 | $0E80); memcpy(vramText, mem + src, 0x180); mem[0xA0] = 0; }   // CF/E338-E37D
if (mem[0x99E2]){ memcpy(vramFrame, mem + 0x0B40, 0x180); mem[0x99E2] = 0; }                                                    // CF/E384-E3C0
```

At the boot, initialise them as CC/E0AF (`$0CC0` -> `vramText`) and CC/E1B0 (`$0B40` -> `vramFrame`) do.

### 4.4 The renderer reads the image, never the live state

- **BG1 frame rows:** from `vramFrame` (today the live `$0B40`). The submenu's baked BG1 layers must give way to the
  ROM's $D1:5A50 / $D1:5BD0 tiles arriving through `vramFrame` (see Open).
- **BG3 text rows 1..6:** every cell of `vramText`. Word w gives:
  - tile (w & 3FF), and a glyph when the tile is >= 0x100, with font code w & FF;
  - palette (w >> 10) & 7: 29xx = palette 2, 2Dxx = palette 3;
  - bit 15 = V-flip: the item down arrow, A97F.

  This one pass replaces the baked-map pass, the hand pass, the submenu panel, the name pass, the party rows, the ATB
  bars and the low-HP blink. The blink's 2D attribute is already in the image (C1/0620 -> C1/0640-0652, per pass).
- **Decisions such as `subPanel`** (`$95DB`) must also come from what the image holds: the cartridge shows the Tech
  list f660-f685 although `$95DB` = 0 from f678.

### 4.5 Validation

Compare per frame on the main drive and the two side drives:
- the port's `$0CC0`, `$A6E1`, `$0E80` and `$0B40` against the oracle's `--monitor` (same frame);
- the port's `vramText` / `vramFrame` against the oracle's `--peek` `.vram` at byte $F040 / $F400 (same frame);
- the picture at thumbnail P+2.

The expected visible changes are f648, f660, f668, f672 (cancel), f668 and f680 (item), f686 and f596 (HP). A
regression check should pin the hand f648, the list f660 and the band f686.

## 5. Open

- **The strip's BG3 row geometry:** rows are 12 px apart; `ctBattleHudRowY` = -1 163 171 -1 183 -1 195 is measured, and
  the per-scanline BG3 V-scroll is HDMA-compressed (battle_hud_render.md). A pass driven by `vramText` needs which
  scanline shows which BG3 row and line, taken from the band's HDMA tables. It is not derived here.
- **The $D1:5A50 / $D1:5BD0 frame tiles** (the list windows): are they all in `ctBattleHudChromeWord`'s 20-tile table,
  or does the chrome table need the list windows' tiles? Not checked.
- **Layouts:** `$9F20` (C1/02AD headers, the C1/05BC/0658/0689 branches, C1/06D4: every present seat's bar at once) and
  the C1/05A7 head test (`$A6DB` >= 0, `$A09A` = 0, `$9F20` = 1) are read off the code only. No 3-PC or other-layout
  drive was made.
- **The boot:** the frames of `$98CA` steps 2 and 3 against the band open, and so the strip's first picture, were not
  measured.
- **The end of the fight:** C0/0905 handing `$0B40` back, and the strip's VRAM through the victory, were not covered.
- **The PC-tile uploads** (`$A4`, `$A012`, `$A118`) are classified by their destination tables only (CC/F967/F96F all
  below $0E00). `$A680/$A696` were not traced to a value.
- **The `$CD02` pause:** read off the code (D1/E81E-E877, Start), not driven.

## 6. Ported (2026-10-01)

### 6.1 What the port runs now

**The images** are the ROM's, in `h->mem`, written only by ports of the ROM's routines (src/battle.c, "THE STRIP'S
WRAM IMAGES"), at the ROM's step of the menu pass (`battleMenuPass`):

| Step | Routines |
|---|---|
| head (`battleMenuPassHead`) | C1/0C2D: `$95DB` = 0 -> C1/0C4E-0C80 (C1/1C3A + C1/0299, or C1/05A7 + C1/0872; C1/1C3A when `$95D5` < 0; CF/FD02; `INC $99E2`); `$95DB` = 1 -> C1/0EE1 (the grey pass, now with its row attributes C1/100F-1029) then C1/0C83-0EB7 (C1/0A88 -> C1/0B0B/0BB6/0BD3/0C00 + C1/01A9, the copy, CC/FA95, the MP digits, the resolver C1/0D4D and its cost digits, C1/0EBA + the hand, the description, CF/FD36, `$A86A` -> $D1:5A50 + `INC $99E2`) |
| key (`battleMenuInput` and the submenu handlers) | `INC $A43F` (Up/Down C1/125E/1275, Att x2 C1/12A2/12A5); C1/0A88 at the Tech open (C1/12D1); the item open C1/12F2-1315 (C1/095D, CF/FD6A, $D1:5BD0, `INC $99E2`); C1/1C3A at the Tech and item cancels (C1/138F, C1/14DB + `$9920 := FF`); the item scrolls C1/17A1 / 17BF / 17D1 / 17B3 and the `$A0D0` redraw C1/154B; the target cancel's tail C1/158F-15CD (`battleTargetCancel`); `$9920 := FF` at the item confirm (C1/168B); C1/1C3A at the stash (C1/1FB9) and in the dequeue C1/1C33 (`battleQueueRemoveTeardown`, both boxed paths) |
| tail (`battleMenuPassTail`) | C1/1979 -> C1/1987 (`JSR C1/0EBA`, unless `$9613` bit 7) and C1/18B0-1976 (the item list's arrows and hand, CF/FD6A) |
| boot (`CtBattleBegin` / the stager) | CC/E4BA ($99E3 x $500), CC/E4CB/E4DC (2900 fills), CC/E4ED (the base), CC/E554 (row 0's source $81BD); CC/E30B-E376's `$A6DD/$A6DF/$A6E0/$9920/$9919-$991F := FF`; CC/E391 `$9F20 := CC/FBC5[$2992]`; CC/E5F3-E6B3 (the party names into $9403 and, through C1/01A9, $9412; `$A0FF`, `$A115`, `$A117`); FD/B93B (`$99DD`, `$9F22`); D1/F26E `STZ $CE0F`; then C1/0027's rebuild C1/0299 |

**The VRAM** is a host-side image, `CtBattleHost.stripVram` (BG3 rows 0..8 and BG1 rows 16..21 of the tilemap at
$7800): CC/E0AF / CC/E1B0 at the boot, and `battleStripNmiUpload` at the end of every tick -- CF/E334-E37D (`$A0`, the
record's source, `STZ $A0`) and CF/E37F-E3C0 (`$99E2`, `STZ $99E2`), gated by the fight NMI (the band's arm, as the pad
latch), `$CE0F` and `$CD02`. The per-tick `battleHudChromeFrame` is gone (kept only behind `CtBattleHudChromeRebuild`, for
a render host that patches a captured WRAM's queue).

**The picture** (src/field.c `battleHudRenderStrip`) is drawn from the VRAM image only: BG1 per (tile, line) from
src/data/battle_hud_chrome.h, BG3 every word of rows 0..8 (tile, palette, V/H flip; the menu font for tiles 0x100-0x1FF).
The baked maps (`ctBattleHudMapCmd/Enemy`), the row table `ctBattleHudRowY`, the submenu bakes (`battle_hud_sub.h`) and
every live-state pass (names, party rows, gauges, hand, lists, blink, `subPanel`) are gone from the renderer. A WRAM-only
fixture (`CT_BATTLE_RENDERWRAM`) has no VRAM: its image is what its NMI would upload (`CtBattleStripVramSeed`: the
record's source and `$0B40`).

### 6.2 The Open items of sec. 5 closed here

- **The BG3 row geometry: derived and measured.** The strip's BG3 scroll comes from HDMA records D1/F388-F3C2 stage at the
  boot, 4 bytes per strip line from $BEAB (top strip) or $BF6B (bottom): HOFS 0, VOFS v (top) or v - $98 (bottom), v = FFFC
  for 19 lines then +4 every 12. Scanline S = renderY + 1 shows BG3 line (S + VOFS) & FF: row 0 lines 5-7, rows 1-2, row 3
  lines 4-7, row 4, row 5 lines 4-7, row 6, row 7 lines 4-7, row 8 line 0. `ct.exe --hdma` with `HDMA_TRACE_BG=1`: gato_wait
  + 642 frames (bottom) VOFS 868 on scanlines 161-179, 872 on 180-191, 876 on 192-203, 880 on 204-208; bat4_lake (top)
  1020 on 9-27, 0 on 28-39, 4 on 40-51, 8 on 52-56. BG1 VOFS 991 / 119: rows 16..21 one line per line. The renderer reads
  the records from WRAM. The same trace has `bg3prio=1`: a BG3 word without the priority bit is behind BG1's opaque windows
  (the item image's cleared 0000 cells and the 00FF its hand clear leaves).
- **The list windows' frame tiles: covered.** $D1:5A50 and $D1:5BD0 use only the 20 words of `ctBattleHudChromeWord`, and
  every (word, strip line) pair they place is defined in `ctBattleHudChromePix` (checked over all 3 x 192 cells).
- **The boot:** the oracle's images at f275 (CC:E4D5/E4E6/E4F6, C1:02A5, CC:E55E), its VRAM = them at f277 / f278, the strip
  on screen with the band's arm (f279), the first post at the first pass (f357). The port does the same work at its
  boot's frame (invisible: the boot's HDMA preset 08 has no strip entry).

### 6.3 Graded (`tools/battle_strip_upload_cmp.py`, the rule's oracle, both sides from the same WRAM)

From the band's settled frame (f296) to each drive's end:

| Drive | Images ($0CC0, $A6E1, $0E80, $0B40, $99E3; port f = cartridge f) | VRAM $7820 / $7A00 (port f = cartridge f + 1) | Strip thumbnails (port P = cartridge P + 2) | The strip changes at |
|---|---|---|---|---|
| main | 542 / 542 | 542 / 542 | 540 / 540 | c596 c648 c660 c668 c686, both sides |
| item | 422 / 422 | 422 / 422 | 420 / 420 | c596 c648 c660 c668 c680, both sides |
| cancel | 402 / 402 | 402 / 402 | 400 / 400 | c596 c648 c660 c672, both sides |

Full resolution, main drive, the strip's 12288 pixels: port f594 / f646 / f658 / f666 / f684 against the cartridge's
f596 / f648 / f660 / f668 / f686, 0 differing pixels each. The thumbnails at lag +1 or +3 differ (1098 px over the main
drive), so the comparison sees the frame. `$A0` and `$99E2` themselves are 00 at every port frame end: the port's upload
(the next frame's NMI) has already consumed them.

Checks: battle-strip-upload-main-vs-oracle, battle-strip-upload-item-vs-oracle, battle-strip-upload-cancel-vs-oracle.

### 6.4 Still open

- **The band's iris** (c281-c293): the strip's colours inside the opening band differ from the port's by up to 5 per
  channel (and 33 on its last two frames): the port clips the settled chrome to the band, the cartridge's animating preset
  carries its own COLDATA ramp. Not the strip's content (sec. 1); not changed here.
- `$9F20` layouts 0 and 1 (C1/02AD, C1/0658-06B4, C1/06D4's layout-1 shortcut) are ported off the code and not driven:
  every save in the repo has layout 2.
- The Combo window CC/FA6B (C1/07A2-07BB) is ported off the code; no drive with a dual-capable seat was made.
- C1/0C2D-0C30's `JSL CF/FD9E` (`$A2` from a 16-bit A whose high byte is the caller's) is not mirrored: C1/0299 and C1/05A7
  load `$A2` again before reading it.
- The battle's end (C0/0905 handing `$0B40` back) and the strip through the victory: still not covered.
- The direct-page scratch ($80-$9B, $AD-$B7) the routines use is not mirrored in WRAM (nothing reads it across a pass).
- C1/077A's partial gauge cell (and C1/06F0 at a running timer) is ported but no live drive grades it: the dossier's
  drives keep Crono ready (a full gauge) until the action, and the action freezes the ATB. Its mutation survives the
  upload checks, so it is not declared there; battle-hud-atb-partial grades the renderer on the cartridge's own cells.
- After the main drive's action (f987) the port's next pass falls 3 frames after the cartridge's (c991 / p994, `$A013`
  c989 / p990): the battle loop's timing out of an action, not the strip's (the drives end before it).
- Passes run now where the ROM's CD/3E75 waits run C1/10E3 in CD/0453 and in the results' lines; the victory's celebration
  hold (C1/356D's C1/007E waits, the pose ladder) and the demo end's other waits are not checked for passes.
