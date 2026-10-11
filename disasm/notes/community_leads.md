# COMMUNITY DOCUMENTATION LEADS — SNES Chrono Trigger battle system

**Purpose.** Every entry below is a **HINT harvested from community reverse-engineering docs**, to be
verified against the ROM/oracle before ANY use (project mandate — CLAUDE.md, `no-invention-rule`). Facts are
paraphrased (no source text copied); each is tagged `[SRC: url]` and `[STATUS: UNVERIFIED HINT]` with a
per-lead "how to verify" (`dis.py` / oracle probe). Conflicts between sources are flagged inline.
Cross-references to our own byte-exact dossiers use `[OURS: file §]`.

**ROM addressing convention.** Community offsets are almost always **unheadered file offsets** (HiROM). To get
our SNES bank/addr: `bank = 0xC0 + (fileoff >> 16)`, `addr = 0x8000 | (fileoff & 0x7FFF)` for the low-page
banks, OR for the CC/CD/CE data banks the file offset maps directly: fileoff `0x0Cxxxx → CC/xxxx`,
`0x0Dxxxx → CD/xxxx`, `0x0Exxxx → CE/xxxx` (e.g. `0x0C8B08 → CC/8B08`; our `dis.py` flat = `(bank−0xC0)<<16 |
addr`, so `CC/8B08 → dis.py 0xC8B08`... note file `0x0C8B08` already IS that flat value −0 — verify the ±0x200
header on any headered-source doc). **Always confirm the header offset before trusting an address.**

**Sources consulted** (● = fetched full; ◐ = via search summary only, page blocked WebFetch; ○ = referenced,
not read):
- ● Data Crystal — Enemy AI Documentation: https://datacrystal.tcrf.net/wiki/Chrono_Trigger/Enemy_AI_Documentation
- ● Data Crystal — Tech and Attack Documentation: https://datacrystal.tcrf.net/wiki/Chrono_Trigger_(SNES)/Tech_and_Attack_Documentation
- ● Data Crystal — RAM map (SRAM/save only, NOT runtime battle RAM): https://datacrystal.tcrf.net/wiki/Chrono_Trigger_(SNES)/RAM_map
- ● GitHub Pseudoarc/ctrando (data layouts as code): https://github.com/Pseudoarc/ctrando
  - `src/ctrando/attacks/animationcommands.py`, `cttechtypes.py`, `enemytech.py`; `characters/ctpcstats.py`; `common/memory.py`
- ○ GitHub abyssonym/eternalnightmare (older CT randomizer): https://github.com/abyssonym/eternalnightmare
- ◐ StrategyWiki — Formulae (403 to WebFetch; read via search): https://strategywiki.org/wiki/Chrono_Trigger/Formulae
- ◐ gamercorner Guides — Game Systems (403; via search): https://guides.gamercorner.net/ct/systems/
- ◐ Chrono Wiki (Fandom) — Stats & Attributes (402; via search): https://chrono.fandom.com/wiki/Stats_%26_Attributes_(Chrono_Trigger)
- ◐ GameFAQs — Save-State Hacking Guide, Terii_senshi (403; via search): https://gamefaqs.gamespot.com/snes/563538-chrono-trigger/faqs/13569
- ○ Chrono Compendium — Enemy AI / Attacks-Tech Scripts / Game Mechanics / Stats-and-Status-Ailments (all hung/timed-out to WebFetch — high-value, retry from a browser): https://www.chronocompendium.com/Term/Enemy_AI.html · /Attacks_/_Tech_Scripts.html · /Game_Mechanics.html · /Stats_and_Status_Ailments_(Chrono_Trigger).html
- ○ GameFAQs — DragonKnightZero Mechanics Guide (faqs/14148); GirkDently Enemy AI Guide (faqs/78740); Neoseeker mirror (3085264-enemy-ai)
- ○ romhacking.net — doc 444 "CT Data Offsets" (Geiger); doc 153 "CT Database" (both 403; canonical, retry from browser)

> **Fetch note for a follow-up pass:** Chrono Compendium, StrategyWiki, GameFAQs, romhacking.net, Fandom,
> gamercorner ALL block the `WebFetch` user-agent (403/402/socket-hang). `web.archive.org` is also blocked here.
> They render fine in a real browser — use the Chrome browser tools (extension was offline this session) or a
> manual paste to capture the Compendium AI/mechanics pages and Geiger's offsets verbatim.

---

## AREA 1 — Battle ANIMATION SCRIPT format (the C1/CC OBJ-anim engine — our biggest open subsystem)

Our dossiers pin the RAM side (anim slots `$0520+n*0xC`, `FD:E3A9` ticker, `$7F04D0`/`FD:E170` tile streamer)
but call the **bytecode + pointer tables a BOUNDARY** `[OURS: battle_stance.md S4, battle_actionfx.md A2/A4]`.
Community fully documents the script.

**L1.1 — Attack/tech animation-script DATA + pointer tables.** `[STATUS: CORRECTED — battle_interp_verify.md T2: the LIVE anim table is CD/5FF0 (attacks) / CD/61F0 (tech), into bank CD, indexed by $9877×2 (C1/4535). CD/5EC6→CE/0380 is a SEPARATE real table (tech graphic-effect scripts), NOT the OBJ anim.]`
- Attack **script data at `CE/0380`**; attack **pointers at `CD/5EC6`** (2 bytes each). `[SRC: DC Tech/Attack]`
- Tech **data pointers `0x0D45AD–0x0D4925`** (7 bytes/tech, 127 techs); tech **sound+animation pointers
  `0x0D5EF0–0x0D5FDB`** (2 bytes/tech). `[SRC: DC Tech/Attack]`
- **Enemy death-script pointers `CE/0000`**; **attack graphic-effect pointers `D1/0000`**; **attack graphic
  palette pointers `CD/686E`**. `[SRC: DC Tech/Attack]`
- Tech graphics header (6 bytes, tech variant 7): `+0 script_id, +1/+2 sprite_packet_1/2, +3/+5
  assembly_packet_1/2, +4 palette, +6 layer3_packet_id`. **PC-tech gfx ptr `0x0145BC`, enemy-tech gfx ptr
  `0x014694`**. `[SRC: ctrando cttechtypes.py]`
- **Verify:** `script_id` (gfx-header +0) indexes the script table at `CE/0380` via `CD/5EC6`. Disasm the C1/CC
  reader (our `battle_actionfx` A2 says the swing is driven by `JSL CC/F156/F278/F1E7/F110` from `C1/4090`) —
  set a monitor writer on the script-pointer fetch during Crono's swing (`gato_press_action` + mash) and check
  it lands in `CE/0380..`; dump ~32 bytes at that pointer and decode with the L1.2 opcode table.

**L1.2 — The animation-script OPCODE table** (from ctrando `animationcommands.py`, the most complete extant
decode; overlaps the DC "Decoded Tech Hex Command" list). `[STATUS: VERIFIED — battle_interp_verify.md T2: clean parse of anims 0x00/0x31/0x34/0x10; 72 facing / 78 sound / 20 pause / 03·06 playAnim / 1E SUPERCMD=the hit / 50 ShowDamage / 00·01 end all confirmed by position + semantics.]` `[SRC: ctrando animationcommands.py; DC Tech/Attack]`

| op | bytes | meaning |
|---|---|---|
| `00` | 1 | Return | `01` | 1 | **EndTech** (terminate) |
| `02 aa` | 2 | play animation `aa` looped | `03 aa` | 2 | play once | `04/05/06 aa` | 2 | first-frame only |
| `07` | 1 | default speed | `08..0F` | 1 | speed slowest→fastest (`08` slowest … `0C` fastest, `0D/0E/0F` fastest-shorter variants) |
| `10 xx yy` | 3 | slide sprite to coords | `11` | 1 | slide to stored coords | `12 tt` | 2 | slide to target `tt` |
| `13../15..` | | spiral to coords/target | `16 xx yy` | 3 | move X-then-Y to coords |
| `19 xx yy` | 3 | **teleport to coords** | `1A` | 1 | teleport to stored | `1B tt` | 2 | teleport to target |
| `1C cc` | 2 | link effect-obj to caster | `1D` | 2 | unlink |
| `1E ss` | 2 | **PerformSuperCommand `ss`** (the CORE attack-execution sub-sequence — the actual hit) | `1F` | 1 | return from super |
| `20 dd` | 2 | **pause `dd` frames** | `22 cc vv` | 3 | wait until counter==value | `23/24 vv` | wait counter 1C/1D |
| `25 tt` | 2 | return if target absent | `26 aa ff` | 3 | load animation `aa` frame `ff` |
| `29/2A` | 1 | hide / show caster-or-target | `2F` | 1 | end (alt) |
| `30 cc vv` .. `4A` | | counter ops (set/inc/dec/add/copy/swap; `4E/4F` swap counter with X/Y coord) |
| `33 tt` | 2 | store target coords | `43 tt xo yo` | 4 | store target coords + offset | `44` | 1 | store current obj coords |
| `47/48 tt` | 2 | write target index to RAM `$7E:A4EC/A4ED`; `81..84 tt` → `$7E:A4E8..A4EB` |
| `50..55` | 1 | **ShowDamage** (display the damage number) | `56..5C` | 1 | end variants |
| `5F` | 2 | nop | `60 pp` | 2 | switch to palette `pp` | `69 pp` | 2 | set object palette (**ROM base `0x114D90`, 0x18 bytes/palette**) | `6A` | 1 | reset palette | `6C cc dd` | 3 | flash palette w/ color |
| `6E/6F` | 1 | draw/hide all effects | `70/71` | 1 | draw/hide single effect |
| `72 ff` | 2 | set facing (0=N,1=S,2=W,3=E) | `73/74 pr` | 2 | set priority/layer (0/3 front, 2/4 back) |
| `75/76/77/85` | | angle ops | `78 sd` | 2 | **play sound `sd`** | `79/7A/7B` | sound variants | `7C` | 1 | regular-weapon sound | `7D` | 1 | **critical-weapon sound** |
| `80 ..` | var | flash screen color (`0x0F` mask = length−1) | `98/9A/9B/9C tt` | 3 | move to target | `A2` | 1 | move forward fixed | `A4 sp tr` | 3 | draw spaced copies (sprite trail) | `A8/A9 dd` | 2 | move forward `dd` |
| `C0..C3` | 4-5 | circular/orbital motion | `D0/D1` | 1 | draw/hide shadow | `D8 ?? sp nn` | 4 | **shake sprite `nn` times at speed `sp`** | `D9 aa` | 2 | load graphics packet |
- **Target enum** (the `tt` byte): `0x00 CASTER_0, 0x01 CASTER_1, 0x0C TARGET_0, 0x0D TARGET_1`.
- **Verify:** decode Crono's basic-attack script (found via L1.1) and expect the ladder to match our measured
  swing `[OURS: battle_actionfx A2.1]`: an approach (`10/12` slide/move) → a `1E` super-command at the strike
  frame (our f473-499 A4 pose, GATO flashes) → `50` ShowDamage (our number popup) → `7D` crit sound on crits →
  `01/2F` end. Confirming the `1E` super-command lands on our measured strike frame proves the decode.

---

## AREA 2 — ENEMY AI script format (bank-CC AI bytecode — our S4 boundary)

Our `battle_loop.md §S4` documents this as a BOUNDARY ("the per-enemy AI-script pointer table + bytecode is a
large separate subsystem"; we only reached the memclear at `CC/F06B`). Community pins the whole thing.

**L2.1 — AI pointer table + data location.** `[STATUS: VERIFIED — battle_interp_verify.md T1: CC/8B08[id×2] (C1/AFD6), GATO 0x92→CC/A77B; CC/8D08 = data base AND the confusion-override script (C1/B4DA).]` `[SRC: DC Enemy AI Doc]`
- **Pointer table `0x0C8B08–0x0C8D07` (CC/8B08)** → per-enemy script; **AI script data `0x0C8D08–0x0CCBC8`
  (CC/8D08)**.
- **Verify:** for GATO (id `0x92`) read `word[CC/8B08 + 0x92*2]` → a pointer into `CC/8D08..`; `dis.py` /
  hex-dump there and expect it to START with the condition/action structure (L2.2). Cross-check by monitoring
  the AI dispatch during our GATO idle→attack (`gato_press_action`, HOLD driver) — the fetch of GATO's script
  pointer should read `CC/8B08+0x124`. This is the single highest-value AI lead.

**L2.2 — Script structure.** `[STATUS: VERIFIED — battle_interp_verify.md T1: two blocks (autonomous / when-hit split at first $FF, C1/B488), cond/action sections $FE-delimited, conditions exactly 4 bytes (C1/AFD2 +4 advance). Decoded GATO/Hetake/Beetle/Avian parse cleanly.]` `[SRC: DC Enemy AI Doc; Neoseeker/GirkDently]`
- Each enemy = **two blocks, each `$FF`-terminated**: **block 1 = autonomous actions**, **block 2 = reactions
  when hit**. Each block = alternating **Condition / Action sections**, each section **`$FE`-terminated**.
- **Conditions are exactly 4 bytes** each; ALL conditions in a section must hold for its Action to run. Engine
  scans sections top-down, takes the **first satisfied**. Between action COMMANDS the enemy's **ATB must refill**.
- **Verify:** matches our observation that GATO only ever physical-attacks the lead PC + waits (`battle_loop
  §S4`). Decode GATO's block1 and expect a `01` (attack) action gated on a trivial/`00` condition; our
  "~72-tick AI first-turn delay" `[OURS: battle_loop ADDENDUM T2]` should correspond to the ATB-refill-between-
  commands rule, NOT a special constant.

**L2.3 — CONDITION opcodes (4 bytes each).** `[STATUS: VERIFIED — battle_interp_verify.md T1: jump table C1/B80D, 0x30 opcodes, 23-28 alias 18. Spot-checked 00 always (C1/8EA7), 17 random (C1/95DA: rand<prob), 1F distance.]` `[SRC: DC Enemy AI Doc]`
`00` always-true · `01` HP ≥ threshold · `04` monster-index alive · `05` ≥N enemies exist · `07` times-attacked ·
`09` been-hit-at-all · `0C ss` status active (`ss`: 01=chaos/confuse, 06=sleep) · `11` hit-by-magic · `12 tt`
hit-by-tech `tt` · `15 ee` hit-by-element (`80`=lightning) · `17 cc` random (cc=probability) · `18 vv jj oo`
**stat compare** (vv=value, jj=flags, oo=block-offset) · `1A` monsters-alive · `1B` count of living PCs · `1F`
distance · `20` defeated · `23–28` = `18`-format variants.

**L2.4 — ACTION opcodes (variable length).** `[STATUS: VERIFIED — battle_interp_verify.md T1: jump table C1/B88D, 0x18 opcodes. Byte-lengths pinned: 01 attack=3B (p1→$AE93, p2=idx), 02 tech=4B, 0F message=2B, 00 wander=1B. NOTE: 03/04/06/0E are RTS/no-op in this ROM.]` `[SRC: DC Enemy AI Doc]`
`00` wander · `01 aa` **attack** (aa=attack index into the enemy-attack table, AREA 3) · `02 tt .. ss` **tech**
(tt=enemy-tech index, ss=message) · `04` random-action selector · `07 mm aa hh` transform-to-monster · `0A .. ss`
**run away** · `0B oo vv mm ss` **set stat** (oo=block-offset, vv=value) · `0C oo vv ss` **stat math** (vv amount,
`0x80` flag = subtract) · `0F ss` display message · `10` revive enemies · `11/12` multi-stat-set · `14/15`
multi-stat-math · `16` multi-revive+set.

**L2.5 — TARGETING routines** (pointer computed as **`2×value + $01B8BB`**). `[STATUS: VERIFIED — battle_interp_verify.md T1: jump table C1/B8BB (their bank-01 = our C1), JSR ($B8BB,X) at C1/AC2E, X=value×2 (ASL A/TAX). Exactly 2×value+C1/B8BB.]` `[SRC: DC Enemy AI Doc]`
`00` none · `01` all PCs · `02` all enemies · `03` current enemy · `04` **attacking PC** · `05` random PC · `06`
nearest PC · `07` farthest PC · `08` lowest-HP PC · `09–0D` PCs by status · `0E` sleeping PCs · `0F` stopped PCs ·
`15` other enemies · `16` living enemies · `17` nearest enemy · `18` farthest enemy · `19` lowest-HP enemy ·
`30/31/32` specific PC slots 1/2/3 · `37` highest-HP PC · `38` random enemy≠self.
- **Verify:** GATO targeting the lead PC `[OURS: battle_loop §S4, target $B1F6=0]` should resolve through one of
  `04`/`06`/`30`. Disasm `01B8BB` and confirm the `2×value+base` pointer math.

**L2.6 — AI-referenced stat OFFSETS + RAM** (the `0C`/`0B`/`18` operand `oo`). `[STATUS: UNVERIFIED HINT]` `[SRC: DC Enemy AI Doc]`
Battle-block offsets: **`0x38` Speed · `0x39` Magic · `0x3B` Evade · `0x3C` (unlabeled — our dossiers call it
magic-def) · `0x3D` Offense · `0x3E` Defense(physical) · `0x3F` Lightning-def · `0x40` Shadow-def · `0x41`
Water-def · `0x42` Fire-def**. Block **`+0x1D` = status/player-enemy flags**, **`+0x1F` = flags**. RAM:
**`7E:AED8–AECC` copy buffer** (the `jj`=`80` flag), **`7E:000E` temp**, **`7E:AF15` bitflag** (targeting `2F`).
- **This RESOLVES two of our OPENs** (see AREA 8 / "what this unlocks"): our `battle_loop §A` already reads
  `+0x3E` as physical DEF and `+0x3C` as magic def — community **independently confirms `+0x3E`=Defense** — and
  our `battle_scenario2` OPEN "which stat byte is physical DEF" is answered: **`+0x3E`**.

---

## AREA 3 — TECH data tables

**L3.1 — Tech ROM tables (unheadered file offsets).** `[STATUS: VERIFIED — battle_interp_verify.md T3: record base CC/213F 12-byte (+5 type, +6 defsel, +9 power), 0x0C214B=record#1; MP CC/253C; names CC/15C4 decode Cyclone/Slash/*Lightning/Spincut/Confuse.]` `[SRC: DC Tech/Attack]`
- Tech **power/props `0x0C214B–0x0C23EA`** (12 bytes/tech, 8 techs/char) · combo-tech extra `0x0C23EB–0x0C247A`
  (12 combos) · **MP costs `0x0C253C–0x0C2573`** (1 byte/tech, 56) + redundant `0x0C2577` · "stat increase"
  `0x0C29D7–0x0C2AD6` (128×2 bytes) · **targeting `0x0C1ACD`** (2 bytes/tech) · **names `0x0C15CF–0x0C1ACA`**
  (11 bytes) / menu-names `0x02BD40` / battle-names `0x3DA89D` / descriptions `0x02BE6A`,`0x0D0323`.
- **Cross-confirms OURS:** our attack/tech record base is **`CC/213F + $B2C8×12`** (12-byte records)
  `[OURS: battle_loop §A]`. Community's `0x0C214B` = the same table at tech-index 1 (`0xC213F + 12`). **The
  12-byte tech record IS the tech power/props table.** Verify by dumping `CC/213F` and matching Crono's basic
  (our `$AEEF`=8) to power byte 11 (physical power).

**L3.2 — Tech record layout (ctrando's split model; supersedes the flat "12-byte" view).** `[STATUS: DamageFormula enum VERIFIED — battle_interp_verify.md T3: oracle $AEEB = 1 PC-melee (Crono, E0F5) / 2 PC-ranged (Marle, E184) / 4 ENEMY_PHYS (GATO, E27B). ctrando ENEMY_PHYS=4 is RIGHT; OUR "type 5 = enemy physical" REFUTED → it is type 4. Split-record pointer layout not separately traced.]` `[SRC: ctrando cttechtypes.py]`
A tech is FOUR parallel records (own pointer tables):
- **Control Header (0xB):** `+0x3` element/flags (bits `0x10/0x20/0x40/0x80` element; `0x04`=physical,
  `0x02`=magical), `+0x5..0x7` three effect-indices, `+0x8..0xA` weapon-effect mods; PC variant `+0x0` =
  battle_group_id (`0x7F`). Ptrs: **PC `0x01CBA1`, enemy `0x0C6FC9`**.
- **Effect Header (0xC) = the damage record:** `+0` effect-type, `+1` power/status-chance, `+2` **status
  bitmask**, **`+5` damage-formula (enum below)**, `+6` defense byte, `+7` status flag (`0x04`=inflicts-status),
  `+9` power. Ptrs: **PC `0x01BF96`, enemy `0x0C7AC9`**.
- **Target Data (0x2):** `+0` target-type (`0x7F` mask) + `0x80`=hide-from-menu, `+1` fallback target. Ptrs:
  **PC `0x01C25A`, enemy `0x0C86C9`**.
- **Enums:** Element `{NONE 0, FIRE 0x10, ICE 0x20, SHADOW 0x40, LIGHTNING 0x80}` · **DamageFormula `{NONE 0,
  PC_MELEE 1, PC_RANGED 2, MAGIC 3, ENEMY_PHYS 4, PC_AYLA 5, MISSING_HP 6}`** · EffectType `{HEALING 0,
  HEALSTATUS 1, STATUS 2, DAMAGE 3, STEAL 6, MULTIHIT 8}` · TargetType `{ONE_PC 0, ALL_PCS 1, SELF 2,
  ONE_FALLEN_PC 3, AYLA 5, FROG 6, ONE_ENEMY 7, ALL_ENEMIES 8, ALL 9, IN_LINE 0xB, LINE_TO_TARGET 0xC,
  AREA_ENEMY 0x12, AREA_MAGUS 0x1B, …}`.
- **⚠ CONFLICT / high-value cross-check:** ctrando's **DamageFormula enum (`ENEMY_PHYS=4`, `PC_AYLA=5`)** vs our
  `battle_loop §A` labels — we call GATO's formula **"type 5 — enemy physical"** and Marle-type "type 2".
  ctrando says **ENEMY_PHYS = 4**, PC_RANGED = 2, PC_AYLA = 5. Either our `$AEEB` dispatch index ≠ this enum, or
  our "type 5" label is off. **Verify:** read `$AEEB` (our seed `$16`) for GATO vs Crono vs Marle and map to
  `{1 melee,2 ranged,3 magic,4 enemyphys,5 ayla,6 missinghp}`. Also note **`MISSING_HP=6`** — an HP%-based
  formula we have not seen; watch for it on HP-drain enemies.

**L3.3 — DC's flat 12-byte tech-props view** (older, coarser; the Effect Header re-expressed). `[STATUS: UNVERIFIED HINT]` `[SRC: DC Tech/Attack]`
byte1 magic-type · byte2 tech-type (00 heal,01 status+,02 status−,03 damage,06 steal,08 multihit) · byte3
status-bits · bytes4-5 hit% (byte5 significant) · byte7 damage-mod (higher=less) · byte9 damage-mod (higher=more)
· byte10 magic-power · byte11 physical-power. **Verify against L3.2** (ctrando's is finer; treat DC's byte
numbers as 1-indexed into the same 0xC record).

**L3.4 — Character basic-attack type bytes** (headered; subtract `0x200`). `[STATUS: UNVERIFIED HINT]` `[SRC: DC Tech/Attack]`
`CD/4001` Marle · `CD/4002` Lucca · `CD/4007` Crono · `CD/400A` Robo · `CD/400B` Frog · `CD/400C` Ayla · `CD/400D`
Magus · `CD/400E` 8th. Element→menu-gfx `C2/A2C6–A2CC`: 00 Lightning,02 Shadow,04 Water,06 Fire,08 blank.

---

## AREA 4 — Battle ITEM usage  (THINNEST — community coverage weak; our field RE partly covers it)

**L4.1** `[STATUS: UNVERIFIED HINT]` — Item data is a **separate table from tech/attack** (the "Itemizer" editor
domain; DC/Compendium note consumable heal/status handled there). Item **targeting reuses the TargetType enum
(L3.2)**. `[SRC: DC Tech/Attack; ctrando]` **Our own memory already RE'd the FIELD item-use effect table
`CC:05CC` + `$7700` props `[OURS: ct-field-item-menu-use]`** — the battle item-use dispatch is likely the same
data behind a battle-menu entry point.
- **Verify:** the exact item ROM byte-structure was NOT locatable in free community docs this pass — it lives in
  **Geiger's offsets (romhacking doc 444)** and **Itemizer's table**, both blocked to WebFetch. Retry those from
  a browser. Meanwhile, disasm the battle Item-command dispatch (our `battle_actionfx` P5.3 says command-confirm
  `C1/1290` branches Item→`C1/12ED`) and follow it to the item-effect table; compare to the field `CC:05CC`
  table to test the "same data" hypothesis.

---

## AREA 5 — ATB / turn internals

**L5.1** `[STATUS: UNVERIFIED HINT]` `[SRC: gamercorner/StrategyWiki via search]` — Community confirms the
*model* we already derived byte-exact: in **Active**, enemies act on their own ATB regardless of the player
menu; **Wait** pauses; **Speed** governs ATB fill rate. No community source gives the byte formula.
- **OURS is authoritative and finer:** reload `$B158 = 105 − speed×6 + CC2E31[(speed−1)+(bs&7)×16]`,
  `−1`/logic-tick, cadence 2.5 real-frames/tick, freeze on `$99E0` `[OURS: battle_loop §B, ADDENDUM §C;
  battle_atbmode]`. Use community only as a sanity frame; **do not replace our derived equation with it.**
- **Speed byte:** char-record raw **`+0x0D`** (L8.1) and battle-block derived **`+0x38`** (L2.6) — our reload
  patch-experiment pinned `+0x0D` `[OURS: battle_loop §B]`; the AI reads `+0x38`. Verify both exist (raw vs
  effective speed) rather than assuming one is wrong.

---

## AREA 6 — DAMAGE / CRIT / MISS / EVADE formulas

**L6.1 — Physical damage.** `[STATUS: UNVERIFIED HINT]` `[SRC: StrategyWiki, gamercorner, Fandom via search]`
- Non-random base scales by **Attack**; one stated form **`Attack = Power×4/3 + Weapon×5/9`**, with a coarser
  "base = Attack×4" elsewhere. **Target physical defense is "out of 256"** (128 DEF ⇒ half damage) → the
  `(256−DEF)/256` multiplier. **Random component = attacker-only**: ≈**⅓ of Power** (Crono/Magus/Frog/Robo),
  ≈⅓ of **Hit** (Lucca/Marle), ⅓ of **Level** (Ayla); random is **NOT** affected by weapon/level/target-DEF.
- **STRONGLY CROSS-CONFIRMS OURS** `[OURS: battle_loop §A]`: our byte-exact `base = $AEEF×(A[+0x36]×12 +
  A[+0x43]×5)/18 × (256−DEF)/256 + bonus`, `bonus = rand(0..A[+0x36]/3)`. The community `(256−DEF)/256`,
  the `/18`-family structure, and **`bonus = ⅓·Power`** all match. The community `Power×4/3 + Weapon×5/9`
  (= `(Power×24 + Weapon×10)/18`) is a different decomposition of the same `/18` core — reconcile which of
  `A[+0x36]`/`A[+0x43]` is "Power" vs "Weapon-derived Attack". **Verify:** already verified on our side; use
  community to LABEL our offsets (`+0x36` ≈ Attack/Power-derived, `+0x43` ≈ weapon term).

**L6.2 — Magic / tech damage.** `[STATUS: UNVERIFIED HINT — WE HAVE NOT DERIVED THIS]` `[SRC: StrategyWiki via search]`
- Uses the **Magic** stat + tech **magic-power** byte (L3.2 Effect-Header `+9` / DC byte10). **Magic defense is
  "out of 102.4" (capped at 100)** — i.e. the magic analogue of `(256−DEF)/256` is a `/102.4` (≈`×2.5/256`)
  reduction. Most magic **cannot be evaded**.
- **Verify (NEW WORK):** our damage dossier only pins the physical path (DamageFormula `PC_MELEE`/`ENEMY_PHYS`).
  Capture a magic tech (e.g. Lucca Flame Toss / Marle's ice) and monitor the `C1/DBxx-E5xx` block — the
  magic branch should read Magic (`+0x?`, likely the derived-Magic `+0x39` L2.6) and target magic-def `+0x3C`,
  applying a `/102.4`-equivalent instead of `(256−DEF)/256`. This is the concrete next damage-derivation target.

**L6.3 — Hit / evade.** `[STATUS: UNVERIFIED HINT]` `[SRC: StrategyWiki via search]`
- **Physical hit-rate = Hit / target-Evade, capped 99%.** Confirms OURS: the `LDA #$64 / JSR C1/AF22` at
  `C1/E9F0` is the hit-roll (`rand<hit%`, miss ⇒ `$16=0xFE`) `[OURS: battle_loop §A "⚠ correction"]`. Verify the
  hit% fed to that roll = `Hit(+0x0F or derived) / Evade(+0x3B)` clamped 99.

**L6.4 — Critical hits.** `[STATUS: UNVERIFIED HINT]` `[SRC: StrategyWiki/Fandom via search]`
- Crit = **×2 damage** (non-elemental single physical); global **9999 cap**; a few weapons (Bronze Fist,
  Apocalypse Arm) are **always-crit / 9999**. Confirms OURS: crit = `C1/EDB0` doubles the base `[OURS:
  battle_loop §S3, §A]`. **Crit RATE is NOT numerically pinned in the free community summaries** (it's a
  per-weapon/character property; the Compendium Game-Mechanics page — blocked — is the place to get the exact
  crit% and the Dragon-Tear/Bronze-Fist modifiers). Verify by monitoring the crit branch predicate upstream of
  `C1/EDB0` and correlating to a `crit%` byte.

---

## AREA 7 — STATUS effects

**L7.1 — Status bitmask** (Effect-Header `+2` / DC tech-props byte3). `[STATUS: UNVERIFIED HINT]` `[SRC: DC Tech/Attack]`
One byte, meaning depends on positive/negative context:
`0x80` Berserk / **Stop** · `0x40` Shell / **Poison** · `0x20` Regen / — · `0x10` — / **Doom** · `0x08` — /
**Silence(Mute)** · `0x04` Protect / **Confuse(Chaos)** · `0x02` — / **Sleep** · `0x01` — / **Blind**.
AI condition `0C ss` reads status with `ss` 01=chaos, 06=sleep (L2.3) — a **different (index, not bitmask)
encoding**, flag it.
- **Verify:** our damage-status multiplier `C1/E575` reads attacker status at block **`$5E4B/$5E4E/$5E53`**
  (`+0x1E/+0x21/+0x26`) `[OURS: battle_loop §A]`. Determine whether the persistent per-combatant status lives
  in those bytes or in the `+0x1D` flags (L2.6). Map each bit to the `×3/2, ×5/4, ×4/5, ×1/4` multipliers we
  measured.

**L7.2 — Status semantics / durations.** `[STATUS: UNVERIFIED HINT — SPARSE]` — The canonical source is the
Chrono Compendium **"Stats and Status Ailments (Chrono Trigger)"** page (blocked this pass). It documents each
ailment's effect and stat roles (Stamina = defense +1/pt; Magic drives tech power; Hit/Evade drive the hit
formula). **Retry that page from a browser** for durations and the full ailment list. `[SRC: Compendium Stats&Status]`

---

## AREA 8 — Battle RAM map

**L8.1 — Character record (0x50 stride; live sheet `$7E2600+id×0x50`, mirrored to SRAM save).** `[STATUS:
UNVERIFIED HINT — but this is our known char-sheet, now field-labelled]` `[SRC: ctrando ctpcstats.py; Terii_senshi save-hack guide; DC RAM/SRAM]`
`+0x00` char_id · `+0x01` element-resist(`0xF0`)/all-techs-learned(`0x10`) · **`+0x03` curHP(2)** · **`+0x05`
maxHP(2)** · **`+0x07` curMP(2)** · `+0x09` maxMP(2) · **`+0x0B` Power · `+0x0C` Stamina · `+0x0D` Speed ·
`+0x0E` Magic · `+0x0F` Hit · `+0x10` Evade · `+0x11` MagDef · `+0x12` Level** · `+0x13` XP(3) · `+0x1B`
assigned-char(`0x7F`)/reassigned(`0x80`) · `+0x27` helm · `+0x28` armor · `+0x29` weapon · `+0x2A` accessory ·
`+0x2B` xp-to-next(2) · `+0x2D` tp-to-next(2) · `+0x2F..0x35` base Pow/Stm/Spd/Hit/Evd/Mag/MagDef.
- **Confirms + EXTENDS OURS:** we pinned `+3/+5/+7/+9` HP/MP and `+0x0D`=Speed on the battle block; community
  supplies the **raw-stat names `+0x0B..0x12`** (which our block inherits via the boot copy `FD/AE52`, whole
  0x50). **Verify:** these are RAW stats; our damage formula reads DERIVED stats in `+0x30..0x4F` (L8.2) —
  confirm the boot-copy carries `+0x0B..0x12` verbatim into the block and that `kBattleStatDerive` computes
  `+0x36..0x45` from them + equipment.

**L8.2 — Battle-block DERIVED-stat region (0x80 stride, `$5E2D+seat×0x80` / `$5FAD+slot×0x80`).** `[STATUS:
elemental defs +0x3F..0x42 VERIFIED — battle_interp_verify.md T4: seeded from enemy record CC/4700+id×23 (rec+16..19) via FD/B438; live GATO block $616C = 04 04 04 04, +0x3E=0x7F phys DEF. Non-neutral enemies exist (id0x17 Fire=0x80, id0x20 Lit/Shd=0x40).]` `[SRC: DC Enemy AI Doc]`
`+0x1D` status/dead flags(bit7=dead) · `+0x1F` flags · **`+0x38` Speed · `+0x39` Magic · `+0x3B` Evade · `+0x3C`
(magic-def, per OURS) · `+0x3D` Offense · `+0x3E` Defense(physical) · `+0x3F` Lightning-def · `+0x40` Shadow-def
· `+0x41` Water-def · `+0x42` Fire-def**.
- **This is the community half of our OWN derived map** `[OURS: battle_loop §A: +0x36/+0x43 PC atk, +0x3D enemy
  atk, +0x3E phys DEF, +0x3C magic def]`. Community independently gives `+0x3D` Offense, `+0x3E` Defense, and
  adds the **four elemental defenses `+0x3F..0x42`** (NEW to us) and Magic/Evade/Speed at `+0x39/0x3B/0x38`.
  **Verify:** patch each of `+0x3F..0x42` on a state and fire the matching-element enemy tech; expect the
  `(256/102.4 − elemDef)` reduction. This is the map for elemental resistance (never derived on our side).

**L8.3 — Party + resources (runtime RAM).** `[STATUS: UNVERIFIED HINT]` `[SRC: ctrando memory.py]`
Active party **`$7E2980/81/82`**, reserve `$7E2983..88` · copy-PC `$7E1EED..EF` · Gold `$7E2C53..55` · Epoch
status `$7E0294`. Confirms our `$2980` party usage `[OURS: ct-vm-party-ops]`.

**L8.4 — Growth / level-up ROM tables.** `[STATUS: UNVERIFIED HINT]` `[SRC: ctrando ctpcstats.py]`
HP-growth `0x01F886` · MP-growth `0x01F8D1` · stat-growth `0x01F659`(ptr)/`0x01F64B` · XP-thresholds `0x01F83E`
· TP-thresholds `0x01F3F4` · stat-MAX `0x3FCF33` · StatBlock source `0x029584`(ptr `0x02958E`) · tech-level
table `+0x230` off the StatBlock ptr. (Level-up is post-battle, but feeds the victory sequence we stub.)

**L8.5 — GAP: runtime battle-RAM (`$5E2D` blocks, `$AF00` region, `$B1xx–B3xx`).** Community docs do **NOT** map
these (DC "RAM map" is SRAM/save only; save-state guides map the `$2600` char sheet, not the in-battle blocks).
**Our own dossiers are the authoritative source** for `$5E2D`/`$5FAD` blocks, `$AF02/0D/15/27` lists, `$AFAB`
countdown, `$B158` reload, `$B163` order, `$95D5/95DB/9609/95DC` menu state, `$A6D9` queue, `$A013` freeze,
`$B328/B1F6/AD89` damage staging `[OURS: battle_loop, battle_actionfx, battle_atbmode, battle_hud_render]`.
Treat community only for the ROM-side tables above and the derived-stat offset *names*.

---

## CONFLICTS / CAVEATS (flagged)

1. **DamageFormula enum (L3.2)** — ~~ctrando `ENEMY_PHYS=4`, `PC_AYLA=5` vs our `battle_loop` "type 5 = enemy
   physical"~~ **RESOLVED (battle_interp_verify.md T3): oracle `$AEEB`=4 for GATO's enemy physical → ctrando is
   right (ENEMY_PHYS=4); our "type 5" label is WRONG. Dispatch C1/E0F5 branch-chain → E27B for type 4.**
2. **Speed offset (L5.1)** — raw `+0x0D` (ours, patch-pinned for reload) vs AI-read `+0x38` (community). Likely
   raw-vs-effective; verify both exist.
3. **`+0x3C`** — community leaves it "unknown"; ours calls it magic-def. Ours is the more specific claim; keep
   community's "unknown" as a caution, not a contradiction.
4. **Status encoding** — Effect-Header `+2` is a BITMASK (L7.1) but AI condition `0C ss` uses an INDEX
   (01=chaos,06=sleep). Two different encodings for "status"; don't conflate.
5. **Header offset ambiguity** — DC mixes headered (`CD/4001` "subtract 0x200") and unheadered (`0x0Cxxxx`)
   addresses. Verify the ±0x200 on every DC address before use.
6. **Physical-damage decomposition (L6.1)** — community `Power×4/3+Weapon×5/9` vs ours `(A36×12+A43×5)/18×$AEEF`
   differ in factoring; same `/18` core. Ours is byte-exact — use community only to name the terms.

---

## WHAT THIS UNLOCKS — prioritized (leads → our open boundaries)

1. **★★★ ENEMY AI interpreter (our S4 BOUNDARY → now has a full map).** L2.1 pointer table **`CC/8B08`** + data
   **`CC/8D08`**, L2.2 structure, L2.3-2.5 condition/action/targeting opcodes. Directly closes `battle_loop §S4`
   ("the AI-script interpreter is a large separate subsystem not reached"). **Next:** verify GATO's script at
   `word[CC/8B08+0x92*2]`, decode it, prove it = "attack lead PC + wait".
2. **★★★ Battle ANIMATION-script engine (our biggest open subsystem → full opcode table).** L1.1 script data
   `CE/0380` / pointers `CD/5EC6` + L1.2 the complete `00-DA` opcode set (incl. `1E` super-command = the hit,
   `50` ShowDamage, `7D` crit-sound, `D8` shake). Closes the `battle_stance S4` / `battle_actionfx A2/A4`
   "C1/CC engine bytecode = BOUNDARY" — the per-frame tile stream we bake can instead be *interpreted*.
3. **★★★ Elemental defense + magic damage (never derived).** L8.2 `+0x3F..0x42` elemental defenses + L6.2 magic
   formula (`/102.4`) + L3.2 element enum. Gives us the whole magic/elemental damage path we've never captured.
4. **★★ TECH tables (near-zero → full structure).** L3.1 addresses + L3.2 four-record layout + enums. Enables a
   real Tech command (our menu stubs it). Cross-confirms our `CC/213F` attack-record base.
5. **★★ Physical-damage / hit / crit LABELS + confidence.** L6.1/6.3/6.4 independently confirm our byte-exact
   `(256−DEF)/256`, `bonus=⅓Power`, hit=`Hit/Evade` cap-99, crit=×2 — and name our `+0x36/+0x43/+0x3E` offsets.
6. **★★ Resolves two named OPENs:** `battle_scenario2` "which byte is physical DEF" → **`+0x3E`** (L2.6/L8.2);
   the derived-stat region `+0x38..0x42` gets field names.
7. **★ Status-effect system.** L7.1 bitmask + AI `0C` index encoding → a target for deriving our `C1/E575`
   status multipliers' source bytes. (Durations still need the blocked Compendium page.)
8. **★ Item battle-use (still thin).** L4.1 points at the Itemizer/Geiger tables (blocked) + the reuse of our
   field `CC:05CC` table; disasm `C1/12ED` (battle Item dispatch) to test the "same data" hypothesis.

**Still genuinely OPEN after this pass (community didn't cover):** the runtime battle-RAM block map (`$5E2D`/
`$AF00`/`$B1xx` — ours only, L8.5); the exact crit RATE% and item byte-structure (behind blocked
Compendium/romhacking pages — retry from a browser); tech/dual/triple **combo** trigger tables (combo-extra
`0x0C23EB` located, semantics not); status durations.
