# The combatants' OBJ palette byte $993C: who restores it from $9888, and over which slots

## The divergence

`python tools/battle_sim.py fight native` (gato_wait's own GATO fight) reported, before this fix:

    wram $993C change #3 differs: cartridge f699 0A0C0E0000000800000000, port f580 0A0C0EFFFFFF02FFFFFFFF

The port wrote FF into `$993C+c` of every EMPTY combatant slot; the cartridge keeps 00 there all fight. The source was
the port's own end-of-action block in `battleAdvanceAction` (src/battle.c, "HOST LIFETIME, not a ROM claim"): with no
action live and `ascOwnsFlash` set it copied `$9888+c` into `$993C+c` for all eleven combatants. An empty slot's
`$9888` is FF (the boot's "absent" marker), so the copy put FF into its `$993C`.

## Every ROM writer of $993C

`python tools/ct.py refs 993C` (byte scan; confirmed by the oracle's --writer below):

| site | routine | what it stores | over which combatants |
|---|---|---|---|
| CC/E6FD | battle boot, PCs | `$9888 = $993C = 0A / 0C / 0E` | the three PC seats |
| CC/EAB2 | battle boot, enemies (CC/EAAB `LDA $29C7,X / ASL A / STA $9888,Y / STA $993C,Y`) | the record's palette x2 | each PRESENT enemy slot (CC/EA62 `LDA $29C5,X / BPL`: bit 7 = absent, skipped) |
| C1/5DFC, C1/5E17 | C1/5DDC (op 69, op 6C ON) | `08` | `$A014,t` for a track t < 4, else every entry of the `$A018` list up to its FF |
| C1/5EA3, C1/5EAB, C1/5ED4 | C1/5E8B (op 6A, op 6C OFF) | `$9888,X` | the same: `$A014,t` (t < 4) or the `$A018` list |
| CD/1B82 | CD/1B78 (background program op C9) | `$993C,X EOR 08` | X = 0..10, all eleven (an empty slot toggles 00 <-> 08, never FF) |
| CF/FE59 | CF/FE20 (the death recolour) | `08` | the dying combatant |
| EC/A1DA | -- | scan hit inside bank EC, not battle code (no oracle write from it) | -- |

C1/5E87 (disassembled, `python tools/dis.py 15E87 60`):

    C1/5E8B  LDA $A010 / CMP #04 / BCS ->5EC9      track >= 4: the target-list path
    C1/5E92  TAX / LDA $A014,X / TAX               the track's combatant
    C1/5E97  CMP #03 / BCC ->5EA8                  a PC: plain restore
    C1/5E9B  LDA $99D2 / BEQ ->5EA8
    C1/5EA0  LDA $9888,X / STA $993C,X / BRA ->5EB0   an arena enemy: restore + live page from the base page
    C1/5EA8  LDA $9888,X / STA $993C,X / BRA ->5EDA

So the only `$9888 -> $993C` copy after the boot names ONE combatant (the track's) or the target list. Nothing in the
ROM restores all eleven, and an empty slot is never a track's combatant or a target: its `$993C` keeps the boot's 00.

## Oracle

`CT_ORACLE_LOGICAL=1 ct --monitor "7E993C:11,7E9888:11" --from saves/battle_re/forest_holdleft_pre.state --input
"40:2600" --diff --writer` (three Hetake: slots 3-5 present, 6-10 empty):

    f226  993C=0A0C0E0202020000000000 (CC:EAB5)   9888=0A0C0E020202FFFFFFFFFF (CC:EAB2)
    f457  993C=080C0E0202020000000000 (C1:5DFF)   -- op 69 on Crono (track path)
    f473  993C=0A0C0E0202020000000000 (C1:5EAE)   -- op 6A, $9888,X back into Crono's byte only
    ... the same pair for every enemy attack through f2553; slots 6-10 stay 00 the whole fight.

## The port

`battleAdvanceAction` no longer touches `$993C` when no action is live: the scripts' own op 6A / 6C (battleAscFlashOff,
C1/5E8B) are its only restore, as in the ROM. Its host-side render fields (flashTarget, fxFlashPal, ascOwnsFlash) are
still cleared there.

Validation (CT_BUILD_DIR=build_alt):

- `python tools/battle_sim.py fight native`: the `$993C` line is gone (the fight's remaining report line is the
  pre-existing extra action at port f1496, `$9877` / `$A013`, unrelated).
- `python tools/battle_sim.py fight @048:9 hp=1`: "wram same values in the same order on both sides".

## Open questions

- The removed block's stated purpose was an action cut short (defeat, teardown) stranding a combatant at 08. The ROM
  has no such guard; if the port ever cuts a script before its op 6A where the cartridge does not, that is a divergence
  in the action's end, to be fixed there -- not by a blanket restore.
- Where the absent slots' `$9888 = FF` is written (before CC/EAB2 in the same boot frame) is not pinned here.
