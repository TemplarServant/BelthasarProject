#!/usr/bin/env python3
r"""Walk the race's $C3 scripts statically and list every op (C3/0B03's encoding, sequence_table.md sec. 12).

    python tools/race_script_walk.py [wram] [start ...]

Reads the overlay from a cartridge WRAM image taken during the race (default saves/race_re/race_f300.wram) and walks from
each start (default 3F14 = page A, 400A = page B, the story's) through every reachable op: jumps (E, D), gosubs (3), the
loop counters (8n / 9n -- both the loop target and the fall-through). Prints the listing and, at the end, the call
targets (op 0, `JSL $7E:37F3` with $00F0 = the target), the tasks and hooks it adds and removes, and its pokes.
Encoding: bit 7 clear = an assignment (low nibble = register; $0n set / $1n add a 16-bit immediate, $2n / $3n a register
given by the next byte); $8n / $9n = loop n (word limit, word target; 9 yields); else $Fn with n the command.
"""
import sys

OPS = {0: ('call', 2), 1: ('poke8', 3), 2: ('poke16', 4), 3: ('gosub', 2), 4: ('return', 0), 5: ('waitapu', 1),
       6: ('sound', 4), 7: ('hookdel', 2), 8: ('hookadd', 2), 9: ('taskdel', 2), 0xA: ('taskadd', 2), 0xB: ('ppu', 2),
       0xC: ('yield', 0), 0xD: ('jumpyield', 2), 0xE: ('jump', 2), 0xF: ('end', 0)}


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'saves/race_re/race_f300.wram'
    w = open(path, 'rb').read()
    starts = [int(a, 16) for a in sys.argv[2:]] or [0x3F14, 0x400A]
    w16 = lambda a: w[a] | w[a + 1] << 8
    seen, todo, listing = set(), list(starts), {}
    calls, tasks, hooks, pokes = set(), set(), set(), []
    while todo:
        ip = todo.pop()
        while ip not in seen and 0x3000 <= ip < 0xB000:
            seen.add(ip)
            op = w[ip]
            if not op & 0x80:
                r, hi = op & 0x0F, op & 0xF0
                if hi & 0x20:
                    listing[ip] = 'r%d %s r%d' % (r, '=' if hi == 0x20 else '+=', (w[ip + 1] & 0x1E) >> 1); ip += 2
                else:
                    listing[ip] = 'r%d %s %04X' % (r, '=' if hi == 0x00 else '+=', w16(ip + 1)); ip += 3
                continue
            hi, n = op & 0xF0, op & 0x0F
            if hi in (0x80, 0x90):
                lim, tgt = w16(ip + 1), w16(ip + 3)
                listing[ip] = 'loop%d %s limit %04X -> %04X' % (n, '(yield)' if hi == 0x90 else '', lim, tgt)
                todo.append(tgt); ip += 5; continue
            # $A0-$FF: C3/0B6A dispatches every one of them on its low nibble ($Fn is only the usual spelling)
            name, ln = OPS[n]
            arg = w16(ip + 1) if ln >= 2 else (w[ip + 1] if ln == 1 else None)
            if n == 1:
                listing[ip] = 'poke8 [%04X] = %02X' % (arg, w[ip + 3]); pokes.append((ip, arg, w[ip + 3], 1))
            elif n == 2:
                listing[ip] = 'poke16 [%04X] = %04X' % (arg, w16(ip + 3)); pokes.append((ip, arg, w16(ip + 3), 2))
            elif n == 6:
                listing[ip] = 'sound $1E00=%04X $1E02=%04X' % (arg, w16(ip + 3))
            elif n == 0xB:
                listing[ip] = 'ppu $21%02X = %02X' % (w[ip + 2], w[ip + 1])
            else:
                listing[ip] = name + ('' if arg is None else (' %04X' % arg if ln >= 2 else ' %02X' % arg))
            if n == 0: calls.add(arg)
            if n in (9, 0xA): tasks.add(('add' if n == 0xA else 'del', arg))
            if n in (7, 8): hooks.add(('add' if n == 8 else 'del', arg))
            if n in (0xD, 0xE):
                todo.append(arg); break
            if n == 3:
                todo.append(arg)
            if n in (4, 0xF):
                break
            ip += 1 + ln
    for a in sorted(listing):
        print('%04X  %s' % (a, listing[a]))
    print('calls:', ' '.join('%04X' % c for c in sorted(calls)))
    print('tasks:', ' '.join('%s %04X' % t for t in sorted(tasks, key=lambda t: t[1])))
    print('hooks:', ' '.join('%s %04X' % t for t in sorted(hooks, key=lambda t: t[1])))
    print('pokes:', ' '.join('[%04X]=%X' % (p[1], p[2]) for p in pokes))


if __name__ == '__main__':
    main()
