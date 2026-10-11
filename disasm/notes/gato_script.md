# GATO event-script dossier — scene 6 (loc 006), trigger side

Everything below is derived from this ROM (Chrono_Trigger_USA_SNES.sfc, HiROM) + the oracle copy ct_probe.exe run on gato_press_action.state. Handler addresses come from the field event-op jump table located at **C0/5D6E** (256 x u16 handler pointers, bank C0; found by matching known handlers op0xE3→C0/3C05, op0x96→C0/4F61, op0x88→C0/4892 at the correct relative offsets — unique hit). Unconfirmed items are labeled HYPOTHESIS.

## 1. Scene/script identification

- Scene header 0x360000 + 6*14 → **scriptId 111**; script 111 decompresses to **9 objects, body 0xF0 bytes** (`python tools/ct.py script 6`). Script 111 is used by scene 6 ONLY (512-scene header scan).
- WRAM layout (oracle state): $7F2000 = actor-count byte (0x09); fn-pointer table at **$7F2001** (9 x 16 x u16); all VM fetches are `LDA $7F2001,X` (seen in every handler), so **live IP = decoded body offset + 0x120**. Verified: obj1 fn0 live 0x01BB = decoded 0x009B+0x120; obj8 fn0 live 0x01DE = 0x00BE+0x120.
- Cast (ct.py objs on the state): obj0 = scene master (IP $1180=0x013C = body 0x1C at save time); **obj1 = player/Crono (lead, $0197=2)**, obj2 = PC2; obj3-7 = PC loader slots; **obj8 = GATO: tile (8,8), solid, cadence 2** (init op `87 01` → $1000/$1001 = 2, matches the state's cad=2), IP 0x01EF = body 0xCF = parked on its 0xB2 yield-forever.

## 2. GATO = obj8, annotated

fn0 (init) @0x00BE: `83 92 86` LOADMON enemy=0x92 slot-byte=0x86 | `8B 08 08` coord tile(8,8) | `87 01` cadence=2 | `0E 01` $1C81,X=1 (tile-snap gate, handler C0/62A4) | `0D 02` $1C80,X=2 (collision-gate byte, handler C0/6295) | `00` RETURN; autorun continuation 0xCB: `89 20` speed $1A00=0x20 (C0/4B49) | `AA 03` ANIMESET 3 | `B2` = park (handler C0/3543 is literally TYX/CLC/RTS — IP unchanged, yield; oracle confirms obj8 pinned at body 0xCF).
- LOADMON handler C0/46DF: operand1 = enemy id → ×10 index into the field enemy table **$E4/F600** (record for 0x92: `7B 76 B6 57 02 00 D0 10 10 41`); gfx/anim from bytes 1-4 → $1380/$1300/$1201,X; sets $1B01,X=1 (solid). Operand2: `AND #7F, INC` → **battle-slot** (0x86→slot 7), obj index stored at $0B88+slot; slot also derives $0D00,X and $1700,X; operand2 bit7 kept in $E1 (raw flag, purpose not chased).
- **fn1 (ACTIVATE) @0x00D0: `75 09` loc[9]=1 (byte 1 → $7F0212, handler C0/6CBD); RETURN.** That is ALL the A-press does — the battle logic lives in obj0.
- fn2 (touch) @0x00D2: RETURN (no touch behavior).
- fn3 @0x00D3: `AA 06` (sing/arm-swing anim loop). fn4 @0x00D6: `A6 01` face-down + `AA 08`. fn5 @0x00DB: `AA 01` + `A6 01` (idle facing down). fn6 @0x00E0: `89 10` speed, `AA 01`, `96 08 08` MOVETO tile(8,8) (walk back to his spot), `AD 20` wait, `A6 01`, `AA 01`, `AD 08`, RETURN.

## 3. obj0 — the trigger loop + THE BATTLE OP (full annotated listing)

fn0 init @0x0000: `53 52 00 0B` = **loc[11] = byte $7F0052** (op 0x53, C0/693D: reads $7F0000+addr16 → $7F0200+idx*2) — loads the persisted Silver Points into local 11; RETURN @0x04; autorun continues at 0x05 (canonical stopAddr+1):

```
0005 B8 00 E3 DE     string table = $DE/E300
0009 12 0A 00 00 8B  IF !(loc[10]==0) -> 0x98      (op 0x12, C0/62DE; else-target = op+operand4+4, jump-when-FALSE — semantics per oracle-validated src/ct_vm.c:116)
000E 12 09 01 00 86  IF !(loc[9]==1)  -> 0x98      (poll the activate flag)
0013 77 09           loc[9]=0
0015 EA 00           song 0 (stop)                  (op 0xEA, C0/3C2F: APU cmd $1E00=0x10, $1E01=id, shadow $7E29AE, JSL C7/0004)
0017 04 10 33        CALL obj8 fn3  (sing anim)
001A EA 42           play SONG 0x42 (Gato's song)
001C BB 03           TEXTBOX str3 "They call me Gato♫ … earn 15 Silver Points♫"   <- state saved parked HERE
001E AD 20           wait 32/16s
0020 EA 00           song stop
0022 E3 00           control lock ($011F=0)
0024 04 10 45        CALL obj8 fn5 (idle pose)
0027 AD 08           wait
0029 E7 00 02        camera scroll-to (0,2)*2       (op 0xE7, C0/3E30: operands<<1 -> $1D02/$1D03, blocks via $1D00)
002C D9 08 0B 8B 08 85 88   MOVEPARTY: PC1->(8,11) face-up, PC2->(11,8) face-LEFT, PC3->(5,8) face-RIGHT
                     (op 0xD9, C0/326C: per PC saves IP to $7F0580 stack, forces IP=0x171F global walk routine;
                      coord&0x7F = tile, xbit7/ybit7 -> facing 00=0,01=1,10=2,11=3 -> $7F3728/29/2B; blocks on $7F03FC until all arrive)
0033 D8 92 20        *** BATTLE ***  (see section 4)
0036 E3 00           re-lock
0038 AD 08
003A 04 10 45        obj8 fn5
003D AD 08
003F EA 00           song stop (kill battle music)
0041 D9 08 0B 07 0B 09 0B   MOVEPARTY: line up (8,11),(7,11),(9,11) all face-up
0048 04 10 36        CALL obj8 fn6 (GATO walks home to (8,8))
004B 16 EC 01 80 13  IF !(glb[0x1EC]==1) -> 0x62    (op 0x16, C0/63E6: operand3 bit7 -> address+0x100, cmp=operand3&0x7F; so this reads $7F01EC == 1 = "party was DEFEATED")
  --- DEFEAT branch (0x50) ---
0050 E3 00 | 04 10 33 (fn3 sing) | AD 08 | EA 42 (song) | BB 02  str2 "You're so weak♫ … Now run along♫♫" | AD 20 | 04 10 45 | 10 2E -> 0x8F
  --- VICTORY branch (0x62) ---
0062 E3 00 | 04 10 33 | AD 08 | EA 42 | BB 04  str4 "I lost♫ You won♫ Here's 15 Points♫…" | AD 20 | EA 00 | 04 10 45 | AD 08 | 04 10 54 (fn4)
0079 E8 04           sfx 4                          (op 0xE8, C0/3C11: $1E01=id, $1E02=0x80, cmd $1E00=0x18, JSL C7/0004)
007B 5B 0F 0B        loc[11] += 15                  (op 0x5B, C0/6A1F: loc[idx] += imm8)  <- THE 15 SILVER POINTS
007E 12 0B C8 02 06  IF !(loc[11] > 200) -> 0x88    (cmp code 2 = '>')
0083 BB 5B           str91 "Can't carry any more Silver Points."
0085 4F C8 0B        loc[11] = 200 (clamp)
0088 58 0B 52 00     byte $7F0052 = loc[11]         (op 0x58, C0/69C4: store-local-to-global — persists the points)
008C 04 10 65        obj8 fn5
  --- merge (0x8F) ---
008F DA              party-regroup (op 0xDA, C0/345A: waits $7F03FE>=3 when $54 bit0, clears follower $7F0C00/0D00/0F00 rows -> followers resume trailing)
0090 E3 01           control unlock
0092 AD 10
0094 EA 06           song 6 (fair ambient resumes)
0096 75 0A           loc[10]=1  -> the 0x0009 IF now skips the poll: GATO is one-shot per scene load (locals reset on re-entry)
0098 11 90           JUMPBACK -> 0x0009
009A 00
```

## 4. Op 0xD8 — canonical semantics (handler + oracle)

**Handler C0/3A83** (3-byte op `D8 imm1 imm2`):
- If $17 bit0 (DP=$0100 → **$7E0117**) already set → yield (CLC, IP unchanged).
- Else if **$18 bit0** ($7E0118) set → battle finished: `TRB $18` (C0/3AAF), advance IP+3, done.
- Else first visit: `imm1 → $7E2989` (C0/3A91-95), `imm2 → $7E2A1F` (C0/3A9A-9E), `$17 |= 0x21` (C0/3AA2-A6), `STZ $1F` (kills $011F control), then yield with IP unchanged — the op BLOCKS, re-polled every visit.
- Field main loop: C0/1770 `LDA $17 BIT #20` → C0/1779 consumes bit5 (fade/mode-7 swirl prep, JSR C0/885A…); falls to C0/188C `BIT #01` → C0/1890 JSR C0/024C (pre-check; carry-set = no lead PC → battle skipped but still acked at 18CE) → JSL C0/2C41 → **JSL C1/0000 = the battle module entry (C0/18A7)**. The whole field loop is inside that JSL until the fight ends. On return: `TRB $17`bit0 + `TSB $18`bit0 (C0/18B9-18BF) → the parked D8 completes; then JSR C0/0283 = post-battle field re-entry.
- **Battle outcome variable = $7F01EC** (written ONLY by the battle module): defeat → `LDA #01 STA $7F01EC` at **C1/813F** (guarded by battle-DP $24 = party-wiped, C1/8133); victory → `TDC/STA` = 0 at **C1/817A** (all-enemies-dead scan of $AF02,X). The script then branches on it via op 0x16 (+0x100 addressing) — exactly GATO's `16 EC 01 80 13`.

**Operand meanings (bit-tested config bytes, NOT a formation id).** The enemy set comes from the scene's op-0x83 LOADMON objects, not from D8. GATO's `92 20`:
- $7E2989 (imm1) = 0x92 = bits 7,4,1:
  - **bit7 = block run-away**: CF/E70B `LDA $004218 AND #$30 CMP #$30` (L+R held) → 16-bit counter $99CF; at ≥0x78 (120 frames), `LDA $2989 AND #0080 BNE skip` → INC $99CD (the escape commit) — bit7 SET suppresses it (CF/E722-E72A). GATO cannot be fled.
  - **bit4 → OAM priority**: `AND #10 ORA #20 → $0C00,X/$0C01,X` for the battle-entry PC sprite copies (C0/045A, 049C, 04DE; also CF/FBAE) — 0x30 = in-front priority.
  - **bit1 → $EC (0/1)** passed into the battle init `JSL CD/0000` (CF/FB86-FB8E). Effect inside CD not chased (HYPOTHESIS: intro/positioning variant).
  - Other readers exist for bits 0 (C1/35A9), 2 (C1/2897, CC/F123), 5 (BIT #20 at ~13 sites across C1; cleared by C1/8024) — not GATO's, not decoded here. Default is 0 (field init C0/0C5E).
- $7E2A1F (imm2) = 0x20 = bit5 only:
  - **bit5 = continue-after-defeat (no game over)**: post-battle re-entry C0/0283: $7F01EC==1 (defeat) → C0/0298 `LDA $7E2A1F BIT #20 BNE →0304` = reload the field normally; bit5 CLEAR → the C0/02A0 fade path ending `BRL →000E` (game-over). This is exactly why losing to GATO returns you to the tent for the "You're so weak" verse. (Also gates the defeat-path music stop at C0/1BFE.)
  - bit4 = analogous special-case for $7F01EC==2 (C0/02CD path — run-away return); bit6 = battle-music variant (C0/1BB7/1BE6: chooses APU cmd 0x70 vs restoring song $FA via cmd 0x14; also CD/03CC/042B); bit7 = skips the $99/$1F post-entry block at C0/034B-035B. All raw-cited; high-level names beyond bit5 are inference from the branch shape.
- NOTE: GATO's imm1 0x92 numerically equals his enemy id 0x92 (`83 92 86`) — pure coincidence; the consumers are disjoint (bit tests vs ×10 table index).

## 5. Oracle validation (ct_probe.exe on gato_press_action.state, input `100:8,0:…`)

State at f0: loc=6, story=8 (--read); obj0 IP 0x013C = body 0x1C parked ON `BB 03` (the challenge textbox was open at capture; loc[9]=$7F0212=01 because the capture's extra A-press re-fired fn1; $011F=1 control still on).
- --followvm --obj 0 IP walk: f0 body 0x1C (BB) → f14 0x1E (AD 20; 128f wait ends f142 ✓) → f142 0x22 (E3) → f146 0x24 → f150 0x27 (AD 08 → f182 ✓) → f182 0x29 (E7) → f186 0x2C (D9, 28 frames) → **f214 body 0x33 = `D8 92 20`**, then frozen there for the whole battle (the raw bytes at that IP even change mid-run — the battle module reuses $7F2000).
- --monitor --diff --writer: **f218**: $2989=92 (writer C0:3A99), $2A1F=20 (C0:3A9E), $0117=21 (C0:3AA8) — all inside the D8 handler (writer PC = next-instruction granularity). **f219**: $0117=01 (C0:177D) — main loop consumed bit5; battle underway.
- Full fight (mash A, 11000 frames): **f3070** $0118=01 (writer C0:18C1 — the TRB $17/TSB $18 ack; battle WON, $7F01EC stayed 0 throughout, matching the victory writer storing 0). **f3080** $0117/18=0000 (C0:3AB1 = the D8 completion TRB $18) — the op advanced. f3077/f3118 $7F03FE 2→3 (C0:0377 battle-entry, C0:6C81 regroup). **f3670: loc[11]=$7F0216=0F (writer C0:6A40 = op-0x5B handler) and $7F0052=0F (writer C0:69E6 = op-0x58 handler)** — the +15 Silver Points confirmed end-to-end. Dialogue table $DE/E300 strings 2/3/4/91 decoded and match the branches (section 3).

## 6. Other battle triggers (contrast)

Scene 6 contains exactly ONE 0xD8 (obj0 body 0x33); GATO's activate/touch fns contain none. Across all 513 scripts a static fn-walk finds 265 D8 sites / 46 operand pairs — CAVEAT: that walk stops at each fn's first RETURN, so autorun-continuation sites (like GATO's own!) are missed; counts are a floor. Common pairs: `D8 12 00` (29), `D8 10 40` (26), `D8 00 00` (19, plain: game-over on defeat since $2A1F bit5 clear, run allowed, default music), `D8 12 40` (17)… Fn-reachable sites with the defeat-allowed bit5 are rare: scripts 204 (`92 E0`), 213 (`90 E0`), 253 (`93 E0`), 284 (`80 E0`), 286 (`91 E0`) — all also set bits6+7, unlike GATO's bare 0x20.

Artifacts saved in the scratchpad: gato.wram, fvm_obj0.txt, mon_battle.txt, mon_full.txt.

## Key addresses
- C0/5D6E = field event-op jump table (256 x u16 handler pointers)
- C0/3A83 = op 0xD8 handler (BATTLE): imm1->$7E2989 (C0/3A91), imm2->$7E2A1F (C0/3A9A), $17|=0x21, blocks until $18 bit0; completion TRB $18 @C0/3AAF, IP+3
- C0/1770 + C0/188C = field main-loop battle dispatch ($17 bit5 swirl prep @1779; bit0 -> JSL C1/0000 @C0/18A7; ack TRB $17/TSB $18 @C0/18B9-18BF)
- C1/0000 = battle module entry; outcome $7F01EC: defeat=1 @C1/813F, victory=0 @C1/817A
- C0/0283 = post-battle field re-entry; $7F01EC==1 & $2A1F bit5 @C0/0298 = continue-after-defeat (no game-over); ==2 path @C0/02CD ($2A1F bit4)
- CF/E70B-E72A = run-away logic: $4218 L+R -> $99CF counter -> INC $99CD gated by $2989 bit7 (bit7 = cannot flee)
- C0/045A/049C/04DE = $2989 bit4 -> OAM prio $0C00,X (battle-entry sprites); CF/FB86 = bit1 -> $EC for JSL CD/0000
- C0/1BAB/1BE6 = $2A1F bit6/bit5 battle-music APU paths (cmd via $1E00, JSL C7/0004)
- C0/46DF = op 0x83 LOADMON: enemy id x10 -> $E4/F600 table; operand2&0x7F = battle slot -> $0B88+slot
- C0/693D / C0/69C4 / C0/6A1F = ops 0x53/0x58/0x5B (local<->global $7F0052 Silver Points, loc[11]+=15)
- C0/326C = op 0xD9 MOVEPARTY (coord&0x7F tile, bit7 pair -> facing, IP stack $7F0580, global routine 0x171F, blocks on $7F03FC)
- C0/345A = op 0xDA party-regroup; C0/3C2F = op 0xEA play-song (cmd 0x10, shadow $7E29AE); C0/3C11 = op 0xE8 sfx (cmd 0x18); C0/3E30 = op 0xE7 scroll-to
- C0/63E6 = op 0x16 IFFLAG: operand3 bit7 adds 0x100 to the glb address -> GATO tests $7F01EC==1
- $7F2001 = live script fetch base (count byte at $7F2000); live IP = body offset + 0x120; obj IP array $1180,X
- scene 6 header @0x360000+6*14 -> script 111; GATO=obj8: fn0@0x00BE init, fn1@0x00D0 activate (loc[9]=1 -> $7F0212), battle op at obj0 body 0x0033 = D8 92 20

## Open questions
- $7E2989 bit1 -> $EC consumed inside JSL CD/0000 (battle init): effect not disassembled — GATO sets it (0x92), meaning unknown (intro/positioning variant is a hypothesis).
- $7E2989 bit5 is BIT-#20-tested at ~13 battle-module sites (C1/80F4, 81EC, 83AE, 83D2, 8555, 8588, 8640, AC75, AF38, AF8F, B7CE, C85A, FD/B2DE) and cleared at C1/8024 — a major battle-mode switch, not decoded (GATO does not set it).
- $2A1F bit7 (C0/034B skip of the $99/$1F block) and the LOADMON operand2 bit7 ($E1) effects were cited raw but not behavior-characterized.
- $7F01EC==2 writer (run-away outcome?) not located — only the =1 (C1/813F) and =0 (C1/817A, C1/81F9, C1/8357) writers were found; the C0/02CD path shape suggests 2 = fled.
- The all-script D8 census undercounts: the static fn walk stops at each fn's first RETURN, so autorun-continuation battle ops (like GATO's own) are invisible to it; a continuation-aware walker would be needed for exact counts.
- The defeat branch (str2, $7F01EC==1) was confirmed by disasm + the op-0x16 semantics but not exercised in the oracle (the mash run won); a deliberate-loss capture would close it.
- Enemy record $E4/F600[0x92] = 7B 76 B6 57 02 00 D0 10 10 41: only bytes 0-4's consumers were traced (gfx/anim/$1201); bytes 5-9 unmapped.
