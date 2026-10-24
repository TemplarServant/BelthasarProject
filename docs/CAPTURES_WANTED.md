# Captures wanted — the savestates that are actually blocking work

> **Historical (2026-08).** This page records the snes9x captures requested from the owner in August 2026. It is
> superseded by recipes: since 2026-10-22 every state a check reads is made from power-on by a recipe
> (`tools/fixtures/`, `python tools/fixtures.py make all`) or is a public battery save (`python tools/fetch_saves.py`),
> except one capture still waiting for its recipe ([ROADMAP.md](ROADMAP.md#11-checks-still-on-private-captures-l)). Do
> not ask for new captures here; write a recipe. Several items below have been ported since (the vehicles of 2.5, for
> example: ROADMAP G1a item 3).

> **2026-08-15 — all four §2 requests DELIVERED and verified.**
> `grupo_3_mapa` (loc 082, party 00,02,04, `$011F`=1 free roam) — the Form gate computes `$0F0B` = 2
> and `$0067 = 05` PERSISTS: the entered Form screen has been captured for the first time.
> `com_jogo_salvo` (overworld, SRAM slot 0 signed `0xE41B`) — the used-slot Save screen captured;
> it corrected two field labels (see `menu_open_items.md` §4.4) and exposed a real SRAM-addressing
> bug in `CtMenuSaveSlot`. `pre_derrota` (Crono 1/96, PB=C1) and `status_aplicado` (PB=C1) carry the
> right RAM/CPU; their VRAM comes from the donor base, so they are usable for RAM-level derivation
> and NOT for pixel compares — a battle-specific base would be needed for that.
> **Tooling fix they forced:** `s9x_to_lake.py` spliced only WRAM+CPU, so a converted state silently
> inherited the DONOR's SRAM — the saved game read back as three empty slots. It now splices the
> cart SRAM too (`cart_handleState` is the payload's tail) and prints which slots are used.

Written 2026-08-15. **Before adding anything here, check the table in §0**: an audit of the 64
snes9x states already in `Desktop/aux crono decompile/` found that most items previously filed as
"blocked on a capture" were already on disk. Ask only for what §2 lists.

How a capture is used: record a savestate in **snes9x**, leave it in
`C:/Users/Icaro Ferris/Desktop/aux crono decompile/<name>.state`. Conversion to a runnable oracle
state is the `capture-oracle-state` skill (`tools/s9x_to_lake.py` + a base state of the same scene).
The `.state` carries WRAM, VRAM, CGRAM, **and SRAM**, so a capture is a full machine, not a photo.

---

## 0. Audit of what already exists (2026-08-15)

Decoded straight from each state's `RAM` block: `$0100` location, `$7F0000` storyline, the active
party `$7E2980-82`, the reserve roster `$7E2983-88`, and the party-lock bitfield `$7F01DF`.

| Item once filed as blocked | Status | The state that covers it |
|---|---|---|
| Mid-slide bed (anim-drain op 0xAB timing) | **covered** | `bed_slide.state`, `scene_2_sliding_bed.state` (loc 002, story 00) |
| Conga follower, 2 members | **covered** | `two_member.state` (loc 1B7 = scene 439, party 00,01) |
| Post-join fair, scene 439 | **covered** | `candy_shop.state`, `marle_candy_shop_animation.state`, `two_member.state` (loc 1B7, story 08, 2 members) |
| Non-GATO encounters (AI, affinity, multi-hit) | **covered** | `batalha_4..7` (story 0C), `batalha_8` (story 12), `batalha_9` (story 15) |
| A party with RESERVE members | **covered** | `batalha_4..9` all carry 1 reserve |
| A 3-member party | **partly** — exists in `batalha_9` (party 00,02,04) but that capture is **in battle**, so the menu cannot open there. See §2.1 |
| Balloon sprite capture state | **covered** | `start_scene.state`, recorded in `tools/owbpal0.py` |
| Gull atlas ↔ ROM tile mapping | **no longer needed** | derived by pixel identity from `saves/ow_w0.state` (`tools/_gen_owgullmap.py`) |

---

## 1. How each request below gets verified on arrival

Every entry states a machine condition, not a vibe. On arrival the condition is checked by decoding
the state's WRAM directly (the same one-liner the audit used) **before** any work is built on it —
a capture that does not meet its condition is worse than no capture, because it silently grades
the wrong thing.

---

## 2. What is actually still needed

### 2.1 `party3_field.state` — a party of THREE, standing in the FIELD  ⭐ highest value

> **DELIVERED (verified 2026-08-30) — do not ask for this again.** `saves/battle_re/party3_field.state`
> meets every condition stated below and is already consumed by six checks (`vmdiff-wmask-party3`,
> `vmdiff-oam-lockstep`, `vmdiff-draw-visibility`, ...). Decoded from the state itself:
> `$7E:2980-82 = 00 02 04` (three members), `$7E:0100 = 0082` (a FIELD map, loc 130), storyline `0x15`,
> and — the part that makes it the discriminating fixture for item 2 — `$0197/$0199/$019B = 02/06/08`,
> i.e. objects **1, 3, 4**, which are NOT consecutive, so `playerObj + i` and the real party-slot table
> give different answers on it.

- **Where to be**: the same point in the story you already reached for `batalha_9` (three characters
  in the party — Crono + two others), but **walking around a map**, not inside a fight. Anywhere is
  fine. Opening the X-menu first is welcome but not required; the menu can be driven from the state.
- **Condition**: `$7E2980-82` = three non-`0x80` entries, and at least **two of them unlocked** in
  `$7F01DF` (Crono is bit7-locked in every state so far, so three members gives exactly two
  unlocked). That makes the Form gate `($0F0B + $0F0C) >= 2` **open** — verified arithmetically
  against `batalha_9`'s party (`$0F0B` = 2).
- **Unblocks three separate items**:
  1. the **entered Form screen** — its layout and `ctMenuChromeEnteredTM[4]` are the last
     NOT-DERIVED piece of the menu (`disasm/notes/menu_open_items.md` §3.4/§6); every state we own
     makes the ROM *refuse* the screen, so its real pixels have never been seen;
  2. the **party-slot table** (`$0197/$0199/$019B`) — the engine still resolves PCs as
     `playerObj + i`, an invention that only a party whose object indices are NOT consecutive can
     discriminate;
  3. the **conga follower, member 3** — mode 2 / DELAY 46 is extrapolated and has never been diffed.

### 2.2 `saved_game.state` — any moment, but with the game SAVED to the cartridge ⭐ trivial to make

- **Where to be**: save the game normally (a save point, or anywhere on the overworld), then record
  a state. The state may be taken anywhere afterwards — what matters is the SRAM.
- **Condition**: the state's `SRA` block must have `$30:7FF8 + slot*2 == 0xE41B` for at least one
  slot. **Every one of the 64 existing states has all three slots empty (`0x6060`)**, i.e. the game
  was never saved in-cart, which is why this is missing.
- **Unblocks**: what a USED save slot actually renders (the 128×16 BG3 summary block: name / LV /
  location / time field positions), whether the `+0x059C` word really is **gold** (the ROM caps it
  at 999 — the label is currently unverified), and the **"Erase old save?" overwrite dialog**, which
  is only *staged* in BG1 until A is pressed on a used slot.

### 2.3 `defeat.state` — losing a battle

> **PARTLY DELIVERED (verified 2026-08-30).** `saves/pre_derrota.state` exists (Crono 1/96, PB=C1) and
> is the “few seconds before the last character falls” half of this request. As the header note above
> already says, its VRAM comes from the donor base, so it is usable for RAM-level derivation of
> `C1/813F` + dp `$24` and NOT for pixel compares. What is still genuinely missing is only the second
> capture: a state ON the game-over screen itself.

- **Where to be**: a fight the party is about to lose. Ideal: recorded a few seconds **before** the
  last character falls, so the whole defeat sequence can be driven from it; a state on the
  game-over screen itself is also useful as a second capture.
- **Condition**: a live battle with the party at or near 0 HP. (`batalha_8`/`batalha_9` are the
  closest existing states — 27/83 and 34/83 — but both are winnable positions.)
- **Unblocks**: the defeat path `C1/813F` + dp `$24`, and the game-over reboot `C0/000E`. Session 24
  derived only the UI teardown (`C1/B7F2`), never the flow itself.

### 2.4 `status_natural.state` — a status inflicted by the enemy, not by a test flag

- **Where to be**: a fight where an enemy actually puts a character to **sleep** (or poisons /
  confuses one). Record while the status icon is showing.
- **Condition**: a combatant's status byte non-zero, set by the ROM's own attack-header staging
  (`$AEE6`), not by our harness.
- **Unblocks**: removing the `CT_STATUSTEST` / `CT_CHARMTEST` env gates — the status rider is
  implemented but still keyed on test flags because natural staging was never observed.

### 2.5 Vehicles — later-story, lowest priority

- **Where to be**: (a) aboard the **Epoch**, and (b) the frame after an **era-arrival flight**
  lands. Also useful: standing on a boat/ferry mid-crossing.
- **Condition**: the vehicle table `$7E:8300` populated / an active vehicle record.
- **Unblocks**: the vehicle layer, and specifically the retirement of the **disembark tile
  heuristic** — a walkable-scan stand-in that is flagged as a mandate violation in
  `[[ct-overworld-boat-ride]]`.

---

## 3. Not worth asking for yet

- A state where `$0294 / $0299 / $029E` is non-zero (the Save gate's three extra block bytes). All
  three were 00 everywhere measured, so only `$0D34` is exercised; the arms are implemented and
  cited but unexercised. Nothing is blocked on it today.
- The original `--owbirds` / `--owboat` capture states. They were never recorded, but the mapping
  they would have pinned was recovered by pixel identity instead (§0), and the recipe is now written
  into `tools/_gen_owgullmap.py`.
